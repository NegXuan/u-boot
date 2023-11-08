// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 */

#include <config.h>
#include <linux/kernel.h>
#ifdef CONFIG_SECURE_POWER_CONTROL
#include <asm/arch/pwr_ctrl.h>
#endif
#include <amlogic/media/vout/lcd/aml_lcd.h>
#include "DP_tx.h"
#include "../lcd_tablet.h"
#include "../../lcd_reg.h"
#include "../../lcd_common.h"

#define EDP_HPD_TIMEOUT     500
#define EDP_READY_AFTER_HPD 150

void edp_tx_init(struct aml_lcd_drv_s *pdrv)
{
	uint8_t auxdata;
	uint16_t i = 0;
	uint32_t offset;
	int ret;
	struct edp_config_s *eDP_cfg = &pdrv->config.control.edp_cfg;
	struct dptx_EDID_s edp_edid1;
	struct dptx_detail_timing_s *tm;

	if (!pdrv)
		return;
	if (pdrv->index > 1) {
		LCDERR("[%d]: %s: invalid drv_index\n", pdrv->index, __func__);
		return;
	}

	offset = pdrv->data->offset_venc_data[pdrv->index];

	lcd_vcbus_write(ENCL_VIDEO_EN + offset, 0);

	dptx_reset(pdrv);

	dptx_wait_phy_ready(pdrv);
	mdelay(1);

	dptx_reg_write(pdrv->index, EDP_TX_TRANSMITTER_OUTPUT_ENABLE, 0x1);
	dptx_reg_write(pdrv->index, EDP_TX_AUX_INTERRUPT_MASK, 0xf);	//turn off interrupt

	while (i++ < EDP_HPD_TIMEOUT) {
		eDP_cfg->HPD_level = dptx_reg_getb(pdrv->index, EDP_TX_AUX_STATE, 0, 1);
		if (eDP_cfg->HPD_level)
			break;
		mdelay(1);
	}
	LCDPR("[%d]: eDP HPD state: %d, %ums\n", pdrv->index, eDP_cfg->HPD_level, i);
	if (eDP_cfg->HPD_level == 0)
		return;

	mdelay(EDP_READY_AFTER_HPD);

	ret = DPCD_capability_detect(pdrv); //! before timing and training
	if (ret) {
		LCDERR("[%d]: DP DPCD_capability_detect ERROR\n", pdrv->index);
		return;
	}
	eDP_cfg->lane_count = eDP_cfg->max_lane_count;
	eDP_cfg->link_rate = eDP_cfg->max_link_rate;

	if (eDP_cfg->edid_en)
		dptx_EDID_probe(pdrv, &edp_edid1);

	dptx_set_lane_config(pdrv);
	dptx_set_phy_config(pdrv, 1);

	// Power up link
	auxdata = 0x1;
	ret = dptx_aux_write(pdrv, DPCD_SET_POWER, 1, &auxdata);
	if (ret) {
		LCDERR("[%d]: eDP sink power up link failed.....\n", pdrv->index);
		return;
	}
	mdelay(30);

	dptx_reg_write(pdrv->index, EDP_TX_MAIN_STREAM_ENABLE, 0x0);

	dptx_link_training(pdrv);

	if (eDP_cfg->edid_en) {
		dptx_manage_timing(pdrv, &edp_edid1);
		tm = dptx_get_optimum_timing(pdrv);
		if (tm) {
			dptx_timing_update(pdrv, tm);
			dptx_timing_apply(pdrv);
		}
	}
	dptx_update_ctrl_bootargs(pdrv);

	dptx_set_ContentProtection(pdrv);

	dptx_set_msa(pdrv);

	dptx_fast_link_training(pdrv);

	lcd_vcbus_write(ENCL_VIDEO_EN + offset, 1);
	dptx_reg_write(pdrv->index, EDP_TX_FORCE_SCRAMBLER_RESET, 0x1);
	dptx_reg_write(pdrv->index, EDP_TX_MAIN_STREAM_ENABLE, 0x1);

	LCDPR("[%d]: eDP enable main stream video\n", pdrv->index);
}

static void edp_tx_disable(struct aml_lcd_drv_s *pdrv)
{
	unsigned char auxdata;
	int index, ret;

	index = pdrv->index;
	if (index > 1) {
		LCDERR("[%d]: %s: invalid drv_index\n", index, __func__);
		return;
	}

	dptx_clear_timing(pdrv);
	// Power down link
	auxdata = 0x2;
	ret = dptx_aux_write(pdrv, DPCD_SET_POWER, 1, &auxdata);
	if (ret)
		LCDERR("[%d]: edp sink power down link failed.....\n", index);

	dptx_reg_write(index, EDP_TX_MAIN_STREAM_ENABLE, 0x0);
	LCDPR("[%d]: edp disable main stream video\n", index);

	// disable the transmitter
	dptx_reg_write(index, EDP_TX_TRANSMITTER_OUTPUT_ENABLE, 0x0);
}

static void edp_power_init(int index)
{
#ifdef CONFIG_SECURE_POWER_CONTROL
//#define PM_EDP0          48
//#define PM_EDP1          49
//#define PM_MIPI_DSI1     50
//#define PM_MIPI_DSI0     41
	if (index)
		pwr_ctrl_psci_smc(PM_EDP1, 1);
	else
		pwr_ctrl_psci_smc(PM_EDP0, 1);
	LCDPR("[%d]: edp power domain on\n", index);
#endif
}

void edp_tx_ctrl(struct aml_lcd_drv_s *pdrv, int flag)
{
	if (flag) {
		edp_power_init(pdrv->index);
		edp_tx_init(pdrv);
	} else {
		edp_tx_disable(pdrv);
	}
}

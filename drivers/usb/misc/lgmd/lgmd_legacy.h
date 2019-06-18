/*
 * Copyright(c) 2017, LG Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LGMD_LEGACY_H
#define __LGMD_LEGACY_H

#include <linux/qpnp/qpnp-adc.h>
#include <linux/usb/class-dual-role.h>
#ifdef CONFIG_LGE_USB_FACTORY
#include <soc/qcom/lge/board_lge.h>
#endif

#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
#include <linux/usb/fusb252.h>
#endif

enum moisture_method {
	MOISTURE_SBU_PU = 0,
	MOISTURE_EDGE_PU_SBU_PU,
	MOISTURE_EDGE_PD_SBU_PU_ASYNC,
	MOISTURE_EDGE_PD_SBU_PU,
};

enum moisture_adc_state {
	ADC_STATE_DRY = 0,
	ADC_STATE_START_WET_DETECT, //Wet DeTection
	ADC_STATE_WAIT_FOR_DRY, //Wait For Dry
	ADC_STATE_WET,
	ADC_STATE_GND,
	ADC_STATE_CC_STOP_ONLY,
};

static const char * const adc_state_strings[] = {
	"DRY",
	"Start_Wet_Detection",
	"Wait_For_Dry",
	"WET",
	"GND",
	"CC Stop Only",
};

struct lge_moisture {
	struct device			dev;
	struct workqueue_struct 	*wq;
	struct delayed_work		init_sbu_adc_work;
	struct delayed_work		init_edge_adc_work;
	struct delayed_work		sbu_adc_work;
	struct delayed_work		edge_adc_work;
	struct delayed_work		sbu_ov_adc_work;
	struct delayed_work		sbu_corr_check_work;
	struct work_struct		change_fusb_work;
	struct hrtimer			sbu_timer;
	struct hrtimer			edge_timer;
	struct qpnp_vadc_chip		*vadc_dev;
	struct qpnp_adc_tm_chip		*adc_tm_dev;
	struct qpnp_adc_tm_btm_param	sbu_adc_param;
	struct qpnp_adc_tm_btm_param	edge_adc_param;
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
	struct fusb252_desc		fusb252_desc;
	struct fusb252_instance		*fusb252_inst;
	bool				sbu_sel;
#else
	struct gpio_desc		*sbu_sel;
	struct gpio_desc		*sbu_oe;
#endif
	struct mutex			moist_lock;
	struct mutex			vadc_read_lock;

	struct timespec			edge_mtime;

	struct power_supply		*usb_psy;

	enum qpnp_tm_state 		sbu_tm_state;
	enum qpnp_tm_state 		edge_tm_state;

        enum moisture_adc_state		sbu_adc_state;
        enum moisture_adc_state		edge_adc_state;
	enum dual_role_prop_moisture_en		prop_moisture_en;
	enum dual_role_prop_moisture    prop_moisture;

	int				method;
	int				sbu_moisture;
	int				edge_moisture;
	int				pending_adc;
	int				edge_only_count;
	int				long_wet_count;
	bool				edge_int_only;
	bool				sbu_lock;
	bool				edge_lock;
	bool				forced_moisture;
	bool   		                cc_disabled;
	bool   		                force_pr_sink;
	bool				adc_initialized;
	bool				sbu_run_work;
	bool				edge_run_work;
	bool				vbus_present;
	bool				is_factory_boot;
};

struct lge_moisture *lge_moisture_create(struct device *parent);
void lge_moisture_destroy(struct lge_moisture *moist);
int lge_moisture_get_sbu_moisture(void);
void lge_moisture_set_moisture_enable(int enable);
void lge_moisture_set_moisture(int force_moisture);
int lge_moisture_check_moisture(bool enable);
void stop_usb_from_moisture(void);
void update_dual_role_instance(void);
int check_data_role(void);
#endif /* __LGMD_LEGACY_H */

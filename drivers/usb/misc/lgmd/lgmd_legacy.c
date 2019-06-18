#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include "lgmd_legacy.h"
#include "usbpd.h"
#include <soc/qcom/lge/board_lge.h>

#define ADC_WFD_TIMEOUT (1000 * HZ/1000) /* 1000 msec */
#define ADC_CC_CHANGED_TIME     (3000 * HZ/1000) /* 3000 msec */
#define ADC_MAX_DRY_COUNT       5
#define ADC_CHANGE_THR          150000 /* 150mV */

#define PU_1V_ADC_THR_1P6MOHM		890000
#define PU_1V_ADC_THR_1P1MOHM		846000
#define PU_1V_ADC_THR_1P0MOHM		833333
#define PU_1V_ADC_THR_700KOHM		777000
#define PU_1V_ADC_THR_600KOHM		750000
#define PU_1V_ADC_THR_470KOHM		714000
#define PU_1V_ADC_THR_466KOHM		700000
#define PU_1V_ADC_THR_200KOHM		500000
#define PU_1V_ADC_THR_13KOHM		 61000
#define PU_1V_ADC_THR_10KOHM		 50000
#define PU_1V_ADC_THR_4KOHM		 20000

#define EDGE_ADC_CHANNEL		VADC_AMUX2_GPIO
#define SBU_ADC_CHANNEL			VADC_AMUX_THM2

/* SBU ADC Threshold values */
static int sbu_adc_snk_low_threshold = PU_1V_ADC_THR_1P6MOHM;
module_param(sbu_adc_snk_low_threshold, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(sbu_adc_snk_low_threshold, "SBU ADC Pr Sink Low voltage threashold");

static int sbu_adc_snk_high_threshold = PU_1V_ADC_THR_1P6MOHM;
module_param(sbu_adc_snk_high_threshold, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(sbu_adc_snk_high_threshold, "SBU ADC Pr Sink High voltage threashold");

static int sbu_adc_low_threshold = PU_1V_ADC_THR_200KOHM;
module_param(sbu_adc_low_threshold, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(sbu_adc_low_threshold, "SBU ADC Low voltage threashold");

static int sbu_adc_high_threshold = PU_1V_ADC_THR_466KOHM;
module_param(sbu_adc_high_threshold, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(sbu_adc_high_threshold, "SBU ADC High voltage threashold");

static int sbu_adc_gnd_low_threshold = PU_1V_ADC_THR_4KOHM;
module_param(sbu_adc_gnd_low_threshold, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(sbu_adc_gnd_low_threshold, "SBU ADC GND Low voltage threshold");

static int sbu_adc_gnd_high_threshold = PU_1V_ADC_THR_13KOHM;
module_param(sbu_adc_gnd_high_threshold, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(sbu_adc_gnd_high_threshold, "SBU ADC GND High voltage threshold");

static int sbu_adc_ovp_threshold = 1875000;
module_param(sbu_adc_ovp_threshold, int, S_IRUGO |S_IWUSR);
MODULE_PARM_DESC(sbu_adc_ovp_threshold, "SBU ADC OVP voltage threshold");

static int sbu_adc_meas_interval = ADC_MEAS1_INTERVAL_1S;
module_param(sbu_adc_meas_interval, int, S_IRUGO |S_IWUSR);
MODULE_PARM_DESC(sbu_adc_meas_interval, "SBU ADC Polling period");

/* EDGE ADC Threshold values */
static int edge_adc_low_threshold = 300000;
module_param(edge_adc_low_threshold, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(edge_adc_low_threshold, "EDGE ADC Low voltage threashold");

static int edge_adc_high_threshold = 300000;
module_param(edge_adc_high_threshold, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(edge_adc_high_threshold, "EDGE ADC High voltage threashold");

static int edge_adc_meas_interval = ADC_MEAS1_INTERVAL_1S;
module_param(edge_adc_meas_interval, int, S_IRUGO |S_IWUSR);
MODULE_PARM_DESC(edge_adc_meas_interval, "EDGE ADC Polling period");

static int edge_adc_gnd_low_threshold = PU_1V_ADC_THR_10KOHM;
module_param(edge_adc_gnd_low_threshold, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(edge_adc_gnd_low_threshold, "EDGE ADC GND Low voltage threshold");

static int edge_adc_gnd_high_threshold = PU_1V_ADC_THR_13KOHM;
module_param(edge_adc_gnd_high_threshold, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(edge_adc_gnd_high_threshold, "EDGE ADC GND High voltage threshold");

static struct lge_moisture *__moist = NULL;

/*************************************************************
 * Internal Utility Static Functions
 */

static bool get_vbus_present(struct lge_moisture *moist)
{
	int ret;
	union power_supply_propval pval = { .intval = 0 };
	bool vbus_present;

	ret = power_supply_get_property(moist->usb_psy,
			POWER_SUPPLY_PROP_PRESENT, &pval);
	if (ret) {
		dev_err(&moist->dev, "Unable to read USB VBUS_PRESENT: %d\n", ret);
		return ret;
	}

	vbus_present = pval.intval;

	return vbus_present;
}

static int get_typec_mode(struct lge_moisture *moist)
{
	int ret;
	union power_supply_propval pval = { .intval = 0 };
	enum power_supply_typec_mode typec_mode;

	ret = power_supply_get_property(moist->usb_psy,
			POWER_SUPPLY_PROP_TYPEC_MODE, &pval);
	if (ret) {
		dev_err(&moist->dev, "Unable to read USB TYPEC_MODE: %d\n", ret);
		return ret;
	}

	typec_mode = pval.intval;

	return typec_mode;
}

static int set_input_suspend(struct lge_moisture *moist, bool set)
{
	union power_supply_propval pval = { .intval = 0 };

	pval.intval = set;

	if (!set && moist->sbu_moisture) {
		dev_info(&moist->dev, "%s: set true, sbu: %d\n"
				, __func__, moist->sbu_moisture);
		pval.intval = true;
	}
	return power_supply_set_property(moist->usb_psy, POWER_SUPPLY_PROP_INPUT_SUSPEND, &pval);
}

static int set_force_power_role(struct lge_moisture *moist, int role)
{
	union power_supply_propval pval = { .intval = 0 };

	if (role == 2) {
		pval.intval = POWER_SUPPLY_TYPEC_PR_SINK;
	} else if (role == 3) {
		pval.intval = POWER_SUPPLY_TYPEC_PR_SOURCE;
	} else {
		pval.intval = POWER_SUPPLY_TYPEC_PR_DUAL;
	}
	return power_supply_set_property(moist->usb_psy, POWER_SUPPLY_PROP_TYPEC_POWER_ROLE, &pval);
}

static int set_cc_disable(struct lge_moisture *moist, bool disable)
{
	union power_supply_propval pval = { .intval = 0 };

	if ((moist->edge_moisture || moist->sbu_moisture) && !disable)
		return 0;

	moist->cc_disabled = disable;

	if (disable) {
		pval.intval = POWER_SUPPLY_TYPEC_PR_NONE;
	} else {
		if (moist->force_pr_sink)
			pval.intval = POWER_SUPPLY_TYPEC_PR_SINK;
		else
			pval.intval = POWER_SUPPLY_TYPEC_PR_DUAL;
	}

	if (!disable && moist->sbu_moisture) {
		dev_info(&moist->dev, "%s: set true, sbu: %d\n"
				, __func__, moist->sbu_moisture);
		pval.intval = POWER_SUPPLY_TYPEC_PR_NONE;
	}

	return power_supply_set_property(moist->usb_psy, POWER_SUPPLY_PROP_TYPEC_POWER_ROLE, &pval);
}

static int set_pr_sink(struct lge_moisture *moist, bool sink)
{
	union power_supply_propval pval = { .intval = 0 };

	if (moist->sbu_moisture)
		return 0;

	if (moist->cc_disabled)
		return 0;

	moist->force_pr_sink = sink;

	if (sink) {
		pval.intval = POWER_SUPPLY_TYPEC_PR_SINK;
	} else {
		pval.intval = POWER_SUPPLY_TYPEC_PR_DUAL;
	}

	return power_supply_set_property(moist->usb_psy, POWER_SUPPLY_PROP_TYPEC_POWER_ROLE, &pval);
}

static unsigned int get_check_timeout(struct lge_moisture *moist, struct timespec mtime)
{
	struct timespec timeout_remain;

	unsigned int timeout_remain_ms;

	timeout_remain = timespec_sub(CURRENT_TIME, mtime);
	timeout_remain_ms = (timeout_remain.tv_sec * 1000) + (timeout_remain.tv_nsec / 1000000);

	if (timeout_remain_ms <= (60 * 1000)) { /* 10s delay for 1M */
		return (10 *HZ);
	}

	return (60 * HZ);
}

static int sbu_vadc_read(struct lge_moisture *moist)
{
	int usbid_vol = -EINVAL;
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
	unsigned long flag;
#else
	int sbu_oe_gpio_val, sbu_sel_gpio_val;
#endif
	mutex_lock(&moist->vadc_read_lock);

#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
	flag = fusb252_get_current_flag(moist->fusb252_inst);

	if (moist->method == MOISTURE_EDGE_PD_SBU_PU && flag != FUSB252_FLAG_SBU_MD_ING)
		fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_MD_ING);
	if (flag == FUSB252_FLAG_SBU_DISABLE)
		fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_DISABLE);
#else
	sbu_oe_gpio_val = gpiod_get_value(moist->sbu_oe);
	if (sbu_oe_gpio_val) {
		gpiod_direction_output(moist->sbu_oe, 0);
		usleep_range(40000, 41000);
	}
	sbu_sel_gpio_val = gpiod_get_value(moist->sbu_sel);
	if (!sbu_sel_gpio_val) {
		gpiod_direction_output(moist->sbu_sel, 1);
		msleep(100);
	}
#endif
	if (IS_ERR_OR_NULL(moist->vadc_dev)) {
		moist->vadc_dev = qpnp_get_vadc(moist->dev.parent->parent, "moisture-detection");
		if (IS_ERR(moist->vadc_dev)) {
			dev_err(&moist->dev, "qpnp vadc not yet probed\n");
			goto out;
		}
	}

{	struct qpnp_vadc_result result = {
		.physical = 0,
		.adc_code = 0,
	};

	qpnp_vadc_read(moist->vadc_dev, SBU_ADC_CHANNEL, &result);
	usbid_vol = result.physical;
	dev_info(&moist->dev, "USB-ID: %s: USB SBU ADC=%d\n", __func__, usbid_vol);
}

out:
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
	if (flag == FUSB252_FLAG_SBU_DISABLE) {
		if (!moist->sbu_moisture)
			dev_info(&moist->dev,"%s: sbu_moisture isn't detected. "
					"but FUSB252 flag is SBU_DISABLE\n", __func__);
		else  {
			fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_DISABLE);
		}
	}
	if (moist->method == MOISTURE_EDGE_PD_SBU_PU && flag != FUSB252_FLAG_SBU_MD_ING)
		fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_MD_ING);
#else
	gpiod_direction_output(moist->sbu_oe, sbu_oe_gpio_val);
	gpiod_direction_output(moist->sbu_sel, sbu_sel_gpio_val);
#endif

	mutex_unlock(&moist->vadc_read_lock);

	return usbid_vol;

}

static int edge_vadc_read(struct lge_moisture *moist)
{
	struct qpnp_vadc_result results;
	int edge_vol;
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
	unsigned long flag;
#else
	int sbu_sel_gpio_val;
#endif

	if (moist->method < MOISTURE_EDGE_PU_SBU_PU)
		return 0;

	mutex_lock(&moist->vadc_read_lock);
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
	flag = fusb252_get_current_flag(moist->fusb252_inst);

	if (moist->method == MOISTURE_EDGE_PD_SBU_PU) {
		if (flag == FUSB252_FLAG_SBU_MD_ING)
			fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_MD_ING);
		if (flag != FUSB252_FLAG_EDGE_MD_ING)
			fusb252_get(moist->fusb252_inst, FUSB252_FLAG_EDGE_MD_ING);
	}
	if (flag == FUSB252_FLAG_SBU_DISABLE)
		fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_DISABLE);
#else
	sbu_sel_gpio_val = gpiod_get_value(moist->sbu_sel);
	gpiod_direction_output(moist->sbu_sel, 0);
	msleep(100);
#endif

	qpnp_vadc_read(moist->vadc_dev, EDGE_ADC_CHANNEL, &results);
	edge_vol = (int)results.physical;
	dev_info(&moist->dev, "%s: USB EDGE ADC=%d\n", __func__, edge_vol);

#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
	if (moist->method == MOISTURE_EDGE_PD_SBU_PU && flag == FUSB252_FLAG_SBU_MD_ING)
		fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_MD_ING);
	if (flag == FUSB252_FLAG_SBU_DISABLE) {
		if (!moist->sbu_moisture)
			dev_info(&moist->dev,"%s: sbu_moisture isn't detected. "
					"but FUSB252 flag is SBU_DISABLE\n", __func__);
		else {
			fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_DISABLE);
		}
	}
#else
	gpiod_direction_output(moist->sbu_sel, sbu_sel_gpio_val);
#endif


	mutex_unlock(&moist->vadc_read_lock);
	return edge_vol;
}

static void set_edge_dry(struct lge_moisture *moist)
{
	if (moist->edge_moisture) {
		dev_info(&moist->dev, "%s: edge wet state: %s -> %s\n", __func__,
				moist->edge_moisture ? "wet" : "dry", "dry");
		moist->edge_moisture = 0;
		moist->edge_only_count = 0;
		set_cc_disable(moist, false);

		if (moist->method == MOISTURE_EDGE_PD_SBU_PU) {
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
			fusb252_get(moist->fusb252_inst, FUSB252_FLAG_EDGE_MD_ING);
#else
			gpiod_direction_output(moist->sbu_sel, 0);
#endif
		}
	}
}

static void set_edge_wet(struct lge_moisture *moist)
{
	dev_info(&moist->dev, "%s: edge wet state: %s -> %s\n", __func__,
			moist->edge_moisture ? "wet" : "dry", "wet");

	moist->edge_moisture = 1;
	moist->edge_only_count++;
	set_cc_disable(moist, true);

	if (moist->method == MOISTURE_EDGE_PU_SBU_PU) {
		moist->edge_tm_state = ADC_TM_HIGH_STATE;
	} else if (moist->method == MOISTURE_EDGE_PD_SBU_PU_ASYNC) {
		moist->edge_tm_state = ADC_TM_LOW_STATE;
	} else  if (moist->method == MOISTURE_EDGE_PD_SBU_PU) {
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
		fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_MD_ING);
#else
		gpiod_direction_output(moist->sbu_sel, 1);
#endif
		moist->edge_tm_state = ADC_TM_LOW_STATE;
		moist->sbu_tm_state = ADC_TM_LOW_STATE;
		qpnp_adc_tm_disable_chan_meas(moist->adc_tm_dev, &moist->edge_adc_param);
	}

	moist->edge_adc_state = ADC_STATE_WET;
}

static void set_sbu_dry(struct lge_moisture *moist)
{
	if (moist->sbu_moisture) {
		dev_info(&moist->dev, "%s: wet state: %s -> %s\n", __func__,
				moist->sbu_moisture ? "wet" : "dry", "dry");
		moist->sbu_moisture = 0;
		moist->long_wet_count = 0;
		set_input_suspend(moist, false);
		set_cc_disable(moist, false);

#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
		fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_MD);
		fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_DISABLE);
#else
		gpiod_direction_output(moist->sbu_oe, 0);
		gpiod_direction_output(moist->sbu_sel, 1);
#endif
		update_dual_role_instance();
		power_supply_changed(moist->usb_psy);
	}
	if (moist->edge_moisture && moist->method == MOISTURE_EDGE_PD_SBU_PU) {
		moist->edge_adc_state = ADC_STATE_WET;
		moist->edge_tm_state = ADC_TM_LOW_STATE;
		qpnp_adc_tm_disable_chan_meas(moist->adc_tm_dev, &moist->sbu_adc_param);
		set_cc_disable(moist, false);
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
		fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_MD);
		fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_MD_ING);
		fusb252_get(moist->fusb252_inst, FUSB252_FLAG_EDGE_MD_ING);
#else
		gpiod_direction_output(moist->sbu_sel, 0);
#endif
	}
}

static void set_sbu_wet(struct lge_moisture *moist)
{
	if (!moist->sbu_moisture) {
		dev_info(&moist->dev, "%s: wet_state: %s -> %s\n", __func__,
				moist->sbu_moisture? "wet" : "dry", "wet");
		moist->sbu_moisture = 1;
		moist->edge_only_count = 0;
		set_cc_disable(moist, true);
		set_input_suspend(moist, true);

		update_dual_role_instance();
		power_supply_changed(moist->usb_psy);
		stop_usb_from_moisture();
		moist->sbu_adc_state = ADC_STATE_WET;
		moist->sbu_tm_state = ADC_TM_LOW_STATE;
	}
}

static bool check_sbu_by_count(struct lge_moisture *moist, int vadc)
{
	int prev_adc = 0, wet_count = 0, force_sink_count = 0;
	int i, vadc_physical;

	vadc_physical = vadc;
	if (moist->pending_adc) {
		dev_info(&moist->dev, "%s: pending_adc=%d\n", __func__, moist->pending_adc);
		return true;
	} else {
		set_cc_disable(moist, true);
		for (i = 0; i < 10; i++) {
			msleep(20);
			prev_adc = vadc_physical;
			vadc_physical = sbu_vadc_read(moist);
			if (prev_adc) {
				if (!moist->force_pr_sink &&
						(vadc_physical < sbu_adc_snk_low_threshold)) {
					force_sink_count++;
				}
				else {
					if (prev_adc - vadc_physical > 100000 ||
							prev_adc - vadc_physical < -100000) {
						wet_count++;
					} else if (vadc_physical < sbu_adc_low_threshold &&
							(prev_adc - vadc_physical > 1000 ||
						prev_adc - vadc_physical < -1000)) {
						wet_count++;
					}
				}
			}
			dev_info(&moist->dev, "%s: sbu adc %d: %d->%d, w:%d\n", __func__,
					i, prev_adc, vadc_physical, wet_count);
		}
		set_cc_disable(moist, false);
		dev_info(&moist->dev, "%s: wet_count = %d\n", __func__,	wet_count);
		if (wet_count > 0) {
			return true;
		} else {
			if (force_sink_count > 0) {
				set_pr_sink(moist, true);
				moist->sbu_adc_param.low_thr = sbu_adc_low_threshold;
				moist->sbu_adc_param.high_thr = sbu_adc_snk_high_threshold;
				moist->sbu_adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
				return false;
			}

			if (vadc_physical > sbu_adc_high_threshold) {
				return false;
			} else {
				dev_info(&moist->dev, "%s: wet isn't detected."
						"wait adc change\n", __func__);
				moist->sbu_adc_param.low_thr = vadc_physical - ADC_CHANGE_THR > 0 ?
						vadc_physical - ADC_CHANGE_THR : 0;
				moist->sbu_adc_param.high_thr = vadc_physical + ADC_CHANGE_THR >
						sbu_adc_low_threshold ? sbu_adc_low_threshold :
						vadc_physical + ADC_CHANGE_THR;
				moist->sbu_adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
			}
		}
		wet_count = 0;
	}
	moist->pending_adc = vadc_physical;
	return false;
}

/*************************************************************
 * QPNP ADC Notifications
 */

static void lge_moist_sbu_notification(enum qpnp_tm_state state, void *ctx)
{
	struct lge_moisture *moist = ctx;
	dev_info(&moist->dev, "%s: state: %s\n", __func__,
			state == ADC_TM_HIGH_STATE ? "high" : "low");

	if (state >= ADC_TM_STATE_NUM) {
		dev_err(&moist->dev, "%s: invalid notification %d\n",
				__func__, state);
		return;
	}

	pm_stay_awake(&moist->dev);
	moist->sbu_run_work = true;

	if (state == ADC_TM_HIGH_STATE) {
		moist->sbu_tm_state = ADC_TM_HIGH_STATE;
	} else {
		moist->sbu_tm_state = ADC_TM_LOW_STATE;
	}
	schedule_delayed_work(&moist->sbu_adc_work, msecs_to_jiffies(1000));
}

static void lge_moist_edge_notification(enum qpnp_tm_state state, void *ctx)
{
	struct lge_moisture *moist = ctx;
	dev_info(&moist->dev, "%s: state: %s\n", __func__,
			state == ADC_TM_HIGH_STATE ? "high" : "low");

	if (state >= ADC_TM_STATE_NUM) {
		dev_err(&moist->dev, "%s: invalid notification %d\n",
				__func__, state);
		return;
	}

	pm_stay_awake(&moist->dev);
	moist->edge_run_work = true;

	if (state == ADC_TM_HIGH_STATE) {
		moist->edge_tm_state = ADC_TM_HIGH_STATE;
	} else {
		moist->edge_tm_state = ADC_TM_LOW_STATE;
	}
	schedule_delayed_work(&moist->edge_adc_work, msecs_to_jiffies(1000));
}

/*************************************************************
 * Symbol Functions
 */

int lge_moisture_get_sbu_moisture(void) {
	struct lge_moisture *moist = __moist;

	if (!moist) {
		pr_err("lge moisture is not registered\n");
		return 0;
	}

	return moist->sbu_moisture;
}
EXPORT_SYMBOL(lge_moisture_get_sbu_moisture);

void lge_moisture_set_moisture_enable(int enable)
{
	struct lge_moisture *moist = __moist;
	int moist_en;

	if (!moist) {
		pr_err("lge moisture is not registered\n");
		return;
	}

	dev_info(&moist->dev, "%s: enable=%d\n", __func__, enable);
	mutex_lock(&moist->moist_lock);
	moist_en = enable ? DUAL_ROLE_PROP_MOISTURE_EN_ENABLE : DUAL_ROLE_PROP_MOISTURE_EN_DISABLE;

	if (moist_en == moist->prop_moisture_en) {
		mutex_unlock(&moist->moist_lock);
		return;
	} else
		moist->prop_moisture_en = moist_en;

	if (moist_en == DUAL_ROLE_PROP_MOISTURE_EN_DISABLE) {
		if (!moist->adc_initialized) {
			mutex_unlock(&moist->moist_lock);
			return;
		}
		moist->adc_initialized = false;
		if (moist->method >= MOISTURE_EDGE_PU_SBU_PU) {
			moist->edge_moisture = 0;
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
			if (moist->method == MOISTURE_EDGE_PD_SBU_PU)
				fusb252_put(moist->fusb252_inst, FUSB252_FLAG_EDGE_MD_ING);
#else
			gpiod_direction_output(moist->sbu_oe , 0);
#endif
			if (moist->edge_run_work) {
				moist->edge_run_work = false;
				pm_relax(&moist->dev);
			}
			cancel_delayed_work(&moist->edge_adc_work);
			moist->edge_adc_state = ADC_STATE_DRY;
			qpnp_adc_tm_disable_chan_meas(moist->adc_tm_dev, &moist->edge_adc_param);
		}
		if (moist->sbu_sel) {
			moist->sbu_moisture = 0;
			if (moist->sbu_run_work) {
				moist->sbu_run_work = false;
				pm_relax(&moist->dev);
			}
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
			if (moist->method == MOISTURE_SBU_PU) {
				fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_MD_ING);
			}
#endif
			cancel_delayed_work(&moist->sbu_adc_work);
			moist->sbu_adc_state = ADC_STATE_DRY;
			qpnp_adc_tm_disable_chan_meas(moist->adc_tm_dev, &moist->sbu_adc_param);
		}
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
		fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_USBID);
#endif
		dev_info(&moist->dev, "%s: disable moisture detection\n", __func__);
		set_input_suspend(moist, false);
		set_cc_disable(moist, false);
		moist->prop_moisture = DUAL_ROLE_PROP_MOISTURE_FALSE;
		update_dual_role_instance();
		power_supply_changed(moist->usb_psy);
	} else if (moist_en == DUAL_ROLE_PROP_MOISTURE_EN_ENABLE) {
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
		if (moist->method == MOISTURE_EDGE_PD_SBU_PU) {
			if (!moist->edge_moisture) {
				fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_USBID);
				fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_MD_ING);
			}
			fusb252_get(moist->fusb252_inst, FUSB252_FLAG_EDGE_MD_ING);
		} else {
			fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_USBID);
		}
#endif
		dev_info(&moist->dev, "%s: enable moisture detection\n", __func__);
		if (moist->method >= MOISTURE_EDGE_PU_SBU_PU) {
			schedule_delayed_work(&moist->init_edge_adc_work, 0);
		}
	}
	if (moist->sbu_sel) {
		schedule_delayed_work(&moist->init_sbu_adc_work, 0);
	}
	mutex_unlock(&moist->moist_lock);
}
EXPORT_SYMBOL(lge_moisture_set_moisture_enable);

void lge_moisture_set_moisture(int force_moisture)
{
	struct lge_moisture *moist = __moist;
	int moist_val;

	if (!moist) {
		pr_err("lge moisture is not registered\n");
		return;
	}

	mutex_lock(&moist->moist_lock);
	moist_val = force_moisture ? DUAL_ROLE_PROP_MOISTURE_TRUE : DUAL_ROLE_PROP_MOISTURE_FALSE;

	if (!moist->adc_initialized) {
		mutex_unlock(&moist->moist_lock);
		return;
	}
	if (moist->prop_moisture_en == DUAL_ROLE_PROP_MOISTURE_EN_DISABLE) {
		dev_info(&moist->dev, "%s: moisture detection is disabled\n", __func__);
		mutex_unlock(&moist->moist_lock);
		return;
	} else if (moist->sbu_moisture) {
		dev_info(&moist->dev, "%s: skip, wet state\n", __func__);
		mutex_unlock(&moist->moist_lock);
		return;
	} else if (moist_val == moist->prop_moisture) {
		mutex_unlock(&moist->moist_lock);
		return;
	} else
		moist->prop_moisture = moist_val;

	if (moist_val == DUAL_ROLE_PROP_MOISTURE_TRUE) {
		dev_info(&moist->dev, "%s: set moisture ture\n", __func__);
		if (moist->sbu_sel) {
			qpnp_adc_tm_disable_chan_meas(moist->adc_tm_dev, &moist->sbu_adc_param);
			if (moist->sbu_run_work) {
				moist->sbu_run_work = false;
				pm_relax(&moist->dev);
			}
			cancel_delayed_work(&moist->sbu_adc_work);
			moist->sbu_lock = false;
			moist->sbu_adc_state = ADC_STATE_WET;
			moist->sbu_tm_state = ADC_TM_LOW_STATE;
		}
		if (moist->method >= MOISTURE_EDGE_PU_SBU_PU) {
			qpnp_adc_tm_disable_chan_meas(moist->adc_tm_dev, &moist->edge_adc_param);
			if (moist->edge_run_work) {
				moist->edge_run_work = false;
				pm_relax(&moist->dev);
			}
			cancel_delayed_work(&moist->edge_adc_work);
			moist->edge_lock = false;
			moist->edge_adc_state = ADC_STATE_WET;
			moist->edge_tm_state = ADC_TM_LOW_STATE;
		}
		if (moist->sbu_sel)
			schedule_delayed_work(&moist->sbu_adc_work, msecs_to_jiffies(0));
		if (moist->method >= MOISTURE_EDGE_PU_SBU_PU)
			schedule_delayed_work(&moist->edge_adc_work, msecs_to_jiffies(0));

	} else if (moist_val == DUAL_ROLE_PROP_MOISTURE_FALSE) {
		dev_info(&moist->dev, "%s: set moisture false\n", __func__);
	}
	mutex_unlock(&moist->moist_lock);

	return;
}
EXPORT_SYMBOL(lge_moisture_set_moisture);

int lge_moisture_check_moisture(bool enable)
{
	struct lge_moisture *moist = __moist;
	enum power_supply_typec_mode typec_mode;
	bool vbus_present;
	int vbus_off_backup;

	if (!moist) {
		pr_err("lge moisture is not registered\n");
		return 0;
	}

	typec_mode = get_typec_mode(moist);

	vbus_present = get_vbus_present(moist);

	if (!vbus_present)
		vbus_off_backup = 0;

	moist->vbus_present = vbus_present;
	if (moist->adc_initialized) {
		if (moist->sbu_sel && !moist->sbu_moisture) {
			if (moist->sbu_run_work) {
				moist->sbu_run_work = false;
				pm_relax(&moist->dev);
			}
			cancel_delayed_work(&moist->sbu_adc_work);
			cancel_delayed_work(&moist->init_sbu_adc_work);
			moist->sbu_lock = true;
			if (!moist->vbus_present &&
					typec_mode == POWER_SUPPLY_TYPEC_NONE && enable) {
				schedule_delayed_work(&moist->init_sbu_adc_work, (1500*HZ/1000));
			} else {
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
				if (moist->vbus_present)
					schedule_work(&moist->change_fusb_work);
#else
				gpiod_direction_output(pd->sbu_sel, 0);
#endif
				if (lge_get_boot_mode() == LGE_BOOT_MODE_CHARGERLOGO &&
						!vbus_off_backup && moist->vbus_present &&
						check_data_role() != DR_DFP) {
					schedule_delayed_work(&moist->sbu_ov_adc_work,
							msecs_to_jiffies(1000));
					vbus_off_backup = vbus_present;
				}
			}
		}
		if (moist->method >= MOISTURE_EDGE_PU_SBU_PU && !moist->edge_moisture) {
			if (moist->edge_run_work) {
				moist->edge_run_work = false;
				pm_relax(&moist->dev);
			}
			cancel_delayed_work(&moist->edge_adc_work);
			cancel_delayed_work(&moist->init_edge_adc_work);
			moist->edge_lock = true;
			if (!moist->vbus_present && typec_mode == POWER_SUPPLY_TYPEC_NONE && enable) {
				schedule_delayed_work(&moist->init_edge_adc_work, (1500*HZ/1000));
			}
		}

		if (((moist->method == MOISTURE_EDGE_PU_SBU_PU ||
				moist->method == MOISTURE_EDGE_PD_SBU_PU_ASYNC) ||
				(moist->method == MOISTURE_EDGE_PD_SBU_PU && !moist->sbu_moisture))
				&& moist->edge_moisture && !moist->vbus_present) {
			if (moist->edge_run_work) {
				moist->edge_run_work = false;
				pm_relax(&moist->dev);
			}
			cancel_delayed_work(&moist->edge_adc_work);
			schedule_delayed_work(&moist->edge_adc_work, 0);
		}
		if (moist->sbu_sel && moist->sbu_moisture && !moist->vbus_present) {
			if (moist->sbu_run_work) {
				moist->sbu_run_work = false;
				pm_relax(&moist->dev);
			}
			cancel_delayed_work(&moist->sbu_adc_work);
			schedule_delayed_work(&moist->sbu_adc_work, 0);
		}
	}

	if (moist->sbu_moisture) {
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(lge_moisture_check_moisture);

static enum hrtimer_restart lge_moisture_sbu_timeout(struct hrtimer *timer)
{
	struct lge_moisture *moist = container_of(timer, struct lge_moisture, sbu_timer);

	dev_info(&moist->dev, "timeout");
	cancel_delayed_work(&moist->sbu_adc_work);
	schedule_delayed_work(&moist->sbu_adc_work, 0);

	return HRTIMER_NORESTART;
}

static enum hrtimer_restart lge_moisture_edge_timeout(struct hrtimer *timer)
{
	struct lge_moisture *moist = container_of(timer, struct lge_moisture, edge_timer);

	dev_info(&moist->dev, "timeout");
	cancel_delayed_work(&moist->edge_adc_work);
	schedule_delayed_work(&moist->edge_adc_work, 0);

	return HRTIMER_NORESTART;

}

/*************************************************************
 * Work Queues
 */

static void lge_moist_change_fusb_work(struct work_struct *w)
{
	struct lge_moisture *moist = container_of(w, struct lge_moisture, change_fusb_work);

	fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_UART);
	fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_MD_ING);
}

static void lge_moist_sbu_corr_check_work(struct work_struct *w)
{
	struct lge_moisture *moist = container_of(w, struct lge_moisture, sbu_corr_check_work.work);
	int vadc_physical;
	int vadc_volt[20], vadc_diff[20];
	unsigned int vadc_tot = 0;
	int vadc_diff_tot = 0;
	int i = 0;
	int vadc_avr, vadc_diff_avr;

	mutex_lock(&moist->moist_lock);

	set_cc_disable(moist, true);
	msleep(100);

	for(i = 0; i < 20; i++)
	{
		vadc_physical = sbu_vadc_read(moist);
		vadc_volt[i] = vadc_physical;
		vadc_tot += vadc_physical;
		if (i > 0 && i < 20) {
			vadc_diff[i] = abs(vadc_volt[i-1] - vadc_volt[i]);
			vadc_diff_tot += vadc_diff[i];
		}
	}
	vadc_avr = vadc_tot/20;
	vadc_diff_avr = vadc_diff_tot/20;

	dev_err(&moist->dev, "%s: sbu adc avr:%d, diff avr:%d\n",
			__func__, vadc_tot/20, vadc_diff_tot/20);

	if (vadc_diff_avr < 10000) {
		dev_err(&moist->dev, "%s: vadc diff avr is lower than 0.01V."
				"recepticle maybe corroded\n", __func__);
		sbu_adc_high_threshold = vadc_avr;
		sbu_adc_low_threshold = vadc_avr;
	}
	else {
		dev_err(&moist->dev, "%s: vadc diff avr is higher than 0.01V\n",
				__func__);
	}

	mutex_unlock(&moist->moist_lock);

	schedule_delayed_work(&moist->sbu_adc_work, 0);
}

static void lge_moist_sbu_ov_adc_work(struct work_struct *w)
{
	struct lge_moisture *moist = container_of(w, struct lge_moisture, sbu_ov_adc_work.work);
	static int count;
	int vadc_physical;

	mutex_lock(&moist->moist_lock);
	if (!moist->sbu_sel || moist->prop_moisture_en == DUAL_ROLE_PROP_MOISTURE_EN_DISABLE) {
		count = 0;
		mutex_unlock(&moist->moist_lock);
		return;
	}

	if (!get_vbus_present(moist) || check_data_role() == DR_DFP) {
		count = 0;
		dev_info(&moist->dev, "%s: vbus off, stop\n", __func__);
	} else {
		vadc_physical = sbu_vadc_read(moist);
		dev_info(&moist->dev, "%s: usb sbu adc = %d\n", __func__, vadc_physical);
		if (vadc_physical > sbu_adc_ovp_threshold) {
			qpnp_adc_tm_disable_chan_meas(moist->adc_tm_dev, &moist->sbu_adc_param);
			if (moist->sbu_run_work) {
				moist->sbu_run_work = false;
				pm_relax(&moist->dev);
			}
			cancel_delayed_work(&moist->sbu_adc_work);
//			moist->forced_moisture = true;
			moist->sbu_lock = false;
			moist->sbu_adc_state = ADC_STATE_WET;
			moist->sbu_tm_state = ADC_TM_LOW_STATE;
			schedule_delayed_work(&moist->sbu_adc_work, msecs_to_jiffies(0));
		} else if (++count < 10) {
			schedule_delayed_work(&moist->sbu_ov_adc_work, msecs_to_jiffies(1000));
		} else {
			count = 0;
			dev_info(&moist->dev, "%s: exceed count, stop\n", __func__);
		}
	}
	mutex_unlock(&moist->moist_lock);
}

static void lge_moist_edge_adc_work(struct work_struct *w)
{
	struct lge_moisture *moist = container_of(w, struct lge_moisture, edge_adc_work.work);
	static struct qpnp_adc_tm_btm_param prev_adc_param;
	int ret, i, work = 0;
	int vadc_physical;
	static int dry_count = 0, polling_count = 0;
	unsigned long delay = 0;

	mutex_lock(&moist->moist_lock);

	hrtimer_cancel(&moist->edge_timer);
	dev_info(&moist->dev, "%s: adc state: %s, tm_state: %s\n", __func__,
			adc_state_strings[moist->edge_adc_state],
			moist->edge_tm_state == ADC_TM_HIGH_STATE ? "high" : "low");

	if (moist->edge_lock) {
		dev_info(&moist->dev, "%s: cable is connected, skip work\n", __func__);
		moist->edge_adc_state = ADC_STATE_DRY;
		moist->edge_run_work = false;
		goto out;
	}

	vadc_physical = edge_vadc_read(moist);

	if (moist->edge_tm_state == ADC_TM_HIGH_STATE)
		moist->edge_adc_param.state_request = ADC_TM_LOW_THR_ENABLE;
	else
		moist->edge_adc_param.state_request = ADC_TM_HIGH_THR_ENABLE;

	moist->edge_adc_param.low_thr = edge_adc_low_threshold;
	moist->edge_adc_param.high_thr = edge_adc_high_threshold;

	switch (moist->edge_adc_state) {
	case ADC_STATE_DRY:
		if ((moist->method >= MOISTURE_EDGE_PD_SBU_PU_ASYNC &&
				moist->edge_tm_state == ADC_TM_LOW_STATE) ||
				(moist->method == MOISTURE_EDGE_PU_SBU_PU &&
				moist->edge_tm_state == ADC_TM_HIGH_STATE)) {
			set_edge_dry(moist);
		} else {
			moist->edge_adc_state = ADC_STATE_START_WET_DETECT;
			work = 1;
		}
		break;
	case ADC_STATE_START_WET_DETECT:
		if (moist->method == MOISTURE_EDGE_PU_SBU_PU) {
			set_cc_disable(moist, true);
			msleep(100);
		}
		vadc_physical = edge_vadc_read(moist);
		if (moist->method == MOISTURE_EDGE_PU_SBU_PU) {
			set_cc_disable(moist, false);
		}
		dev_info(&moist->dev, "%s: ADC_STATE_START_WET_DETECT: usb edge adc = %d\n", __func__,
				vadc_physical);
		if (moist->method == MOISTURE_EDGE_PU_SBU_PU &&
				vadc_physical < edge_adc_gnd_low_threshold) {
			moist->edge_adc_state = ADC_STATE_GND;
			work = 1;
		} else if ((moist->method >= MOISTURE_EDGE_PD_SBU_PU_ASYNC &&
				vadc_physical > edge_adc_high_threshold) ||
				(moist->method == MOISTURE_EDGE_PU_SBU_PU &&
				vadc_physical < edge_adc_low_threshold)) {

			if (moist->method == MOISTURE_EDGE_PU_SBU_PU) {
				set_cc_disable(moist, true);
				msleep(100);
			}
			for (i = 0; i < 10; ++i) {
				msleep(20);
				vadc_physical = edge_vadc_read(moist);
				dev_info(&moist->dev, "%s: usb edge adc(#%d) = %d\n",
						__func__, i, vadc_physical);
				if (vadc_physical > edge_adc_high_threshold) {
					break;
				} else if (moist->method == MOISTURE_EDGE_PU_SBU_PU &&
						vadc_physical < edge_adc_gnd_low_threshold) {
					break;
				}
			}
			if (moist->method == MOISTURE_EDGE_PU_SBU_PU) {
				set_cc_disable(moist, false);
			}

			if (vadc_physical < edge_adc_gnd_low_threshold) {
				if (moist->method == MOISTURE_EDGE_PU_SBU_PU) {
					moist->edge_adc_state = ADC_STATE_GND;
					work = 1;
				} else {
					moist->edge_adc_state = ADC_STATE_DRY;
					work = 1;
				}
			} else if (vadc_physical > edge_adc_high_threshold) {
				if (moist->method >= MOISTURE_EDGE_PD_SBU_PU_ASYNC) {
					moist->edge_adc_state = ADC_STATE_WET;
					delay = 1 * HZ;
					work = 1;
				} else {
					moist->edge_adc_state = ADC_STATE_DRY;
					work = 1;
				}
			} else {
				if (moist->method >= MOISTURE_EDGE_PD_SBU_PU_ASYNC) {
					moist->edge_adc_state = ADC_STATE_DRY;
					work = 1;
				} else {
					moist->edge_adc_state = ADC_STATE_WET;
					delay = 1 * HZ;
					work = 1;
				}
			}
			if (moist->method == MOISTURE_EDGE_PU_SBU_PU && get_vbus_present(moist)) {
				moist->edge_adc_state = ADC_STATE_DRY;
				work = 1;
			}
		} else {
			if (moist->method >= MOISTURE_EDGE_PD_SBU_PU_ASYNC)
				moist->edge_tm_state = ADC_TM_LOW_STATE;
			moist->edge_adc_state = ADC_STATE_DRY;
			work = 1;
		}
		break;

        case ADC_STATE_GND:
                if (moist->method == MOISTURE_EDGE_PU_SBU_PU) {
			if (moist->edge_tm_state == ADC_TM_HIGH_STATE) {
	                        moist->edge_adc_state = ADC_STATE_DRY;
				work = 1;
			} else {
				moist->edge_adc_param.high_thr = edge_adc_gnd_high_threshold;
			}
                }
                break;

	case ADC_STATE_WAIT_FOR_DRY:
		if ((moist->method >= MOISTURE_EDGE_PD_SBU_PU_ASYNC &&
				vadc_physical < edge_adc_low_threshold) ||
				(moist->method == MOISTURE_EDGE_PU_SBU_PU &&
				vadc_physical > edge_adc_high_threshold)) {
			if (dry_count < ADC_MAX_DRY_COUNT) {
				dry_count++;
				delay = ADC_WFD_TIMEOUT;
				work = 1;
			} else {
				moist->edge_adc_state = ADC_STATE_DRY;
				work = 1;
			}
		} else {
			moist->edge_adc_state = ADC_STATE_WET;
			work = 1;
		}
		break;

	case ADC_STATE_WET:
		if (!moist->edge_moisture) {
			polling_count = 0;
			moist->edge_mtime = CURRENT_TIME;
		}

		if ((moist->method >= MOISTURE_EDGE_PD_SBU_PU_ASYNC &&
				moist->edge_tm_state == ADC_TM_LOW_STATE) ||
				(moist->method == MOISTURE_EDGE_PU_SBU_PU &&
				moist->edge_tm_state == ADC_TM_HIGH_STATE)) {
			if ((moist->method >= MOISTURE_EDGE_PD_SBU_PU_ASYNC &&
					vadc_physical < edge_adc_low_threshold) ||
					(moist->method == MOISTURE_EDGE_PU_SBU_PU &&
					vadc_physical > edge_adc_high_threshold)) {
				// if state is wet, called when vbus is off
				if (!get_vbus_present(moist)) {
					moist->edge_adc_state = ADC_STATE_WAIT_FOR_DRY;
					dry_count = 0;
					// wet_adc = 0;
					work = 1;
					pm_stay_awake(&moist->dev);
					moist->edge_run_work = true;
				} else {
					dev_info(&moist->dev, "%s: maybe adc is up by cable\n",
							__func__);
				}
			} else if (vadc_physical < edge_adc_gnd_low_threshold &&
					moist->method == MOISTURE_EDGE_PU_SBU_PU) {
				if (!moist->vbus_present && !moist->sbu_moisture) {
					moist->edge_adc_state = ADC_STATE_DRY;
					work = 1;
				}
			} else {
				if (get_vbus_present(moist)) {
					dev_info(&moist->dev, "%s: vbus is on\n", __func__);
				} else {
					dev_info(&moist->dev, "%s: vbus is off\n", __func__);
				}
				if (moist->method != MOISTURE_EDGE_PD_SBU_PU)
					moist->edge_adc_param.state_request = ADC_TM_HIGH_THR_ENABLE;
			}
		}

		if (moist->edge_adc_state == ADC_STATE_WET) {
			set_edge_wet(moist);
			moist->edge_run_work = false;

			if (moist->method != MOISTURE_EDGE_PD_SBU_PU)
			{
				work = 1;
				polling_count++;

				if ((moist->method == MOISTURE_EDGE_PU_SBU_PU &&
						vadc_physical > edge_adc_low_threshold) ||
						(moist->method >= MOISTURE_EDGE_PD_SBU_PU_ASYNC &&
						vadc_physical < edge_adc_high_threshold))
					delay = (10 * HZ);
				else
					delay = get_check_timeout(moist, moist->edge_mtime);

				hrtimer_start(&moist->edge_timer, ms_to_ktime(delay/HZ*1000), HRTIMER_MODE_REL);
					dev_info(&moist->dev, "%s: count: %d delay: %lu(s)\n",
							__func__, polling_count, delay/HZ);
			}
		}
		break;
	default:
		break;
	}

	if (work) {
		schedule_delayed_work(&moist->edge_adc_work, delay);
	} else {
		moist->edge_run_work = false;
		msleep(50);
		if (moist->method !=  MOISTURE_EDGE_PD_SBU_PU || !moist->edge_moisture) {
			prev_adc_param = moist->edge_adc_param;
			dev_info(&moist->dev, "%s: ADC PARAM low: %d, high: %d, irq: %d\n",
					__func__, moist->edge_adc_param.low_thr,
					moist->edge_adc_param.high_thr,
					moist->edge_adc_param.state_request);
			ret = qpnp_adc_tm_channel_measure(moist->adc_tm_dev, &moist->edge_adc_param);
			if (ret) {
				dev_info(&moist->dev, "%s: request ADC error %d\n", __func__, ret);
				goto out;
			}
		} else {
			if (moist->edge_only_count > 20) {
				delay = (5 * HZ);
				dev_info(&moist->dev, "%s: edge_only_count over 10,"
						"delay: 5(s)\n", __func__);
			} else {
				delay = 100;
			}
			schedule_delayed_work(&moist->sbu_adc_work, delay);
		}
	}

out:
	if (!moist->edge_run_work)
		pm_relax(&moist->dev);
	mutex_unlock(&moist->moist_lock);
}

static void lge_moist_sbu_adc_work(struct work_struct *w)
{
	struct lge_moisture *moist = container_of(w, struct lge_moisture, sbu_adc_work.work);
	static struct qpnp_adc_tm_btm_param prev_adc_param;
	static int dry_count = 0;
	int vadc_physical;
	int ret, work = 0;
	bool is_wet = 0;
	unsigned long delay = 0;

	mutex_lock(&moist->moist_lock);

	hrtimer_cancel(&moist->sbu_timer);
	dev_info(&moist->dev, "%s: adc state: %s, tm state: %s\n", __func__,
			adc_state_strings[moist->sbu_adc_state],
			moist->sbu_tm_state == ADC_TM_HIGH_STATE ? "high" : "low");

	if (moist->sbu_lock) {
		dev_info(&moist->dev, "%s: cable is connected, skip work\n", __func__);
		moist->sbu_adc_state = ADC_STATE_DRY;
		moist->sbu_run_work = false;
		goto out;
	}

	vadc_physical = sbu_vadc_read(moist);

	if (moist->force_pr_sink) {
		moist->sbu_adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
		moist->sbu_adc_param.low_thr = sbu_adc_low_threshold;
		moist->sbu_adc_param.high_thr = sbu_adc_snk_high_threshold;
	} else {
		if (moist->sbu_tm_state == ADC_TM_HIGH_STATE)
			moist->sbu_adc_param.state_request = ADC_TM_LOW_THR_ENABLE;
		else
			moist->sbu_adc_param.state_request = ADC_TM_HIGH_THR_ENABLE;

		moist->sbu_adc_param.low_thr = sbu_adc_snk_low_threshold;
		moist->sbu_adc_param.high_thr = sbu_adc_snk_high_threshold;
	}

	switch (moist->sbu_adc_state) {
	case ADC_STATE_DRY:
		if (moist->sbu_tm_state == ADC_TM_HIGH_STATE) {
			set_sbu_dry(moist);
			if (moist->force_pr_sink) {
				if (vadc_physical > sbu_adc_snk_high_threshold){
					set_pr_sink(moist, false);
					moist->sbu_adc_param.low_thr = sbu_adc_snk_low_threshold;
					moist->sbu_adc_param.high_thr = sbu_adc_snk_high_threshold;
					moist->sbu_adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
				} else {
					moist->sbu_adc_state = ADC_STATE_CC_STOP_ONLY;
				}
			}
		} else {
			moist->sbu_adc_state = ADC_STATE_START_WET_DETECT;
			delay = ADC_CC_CHANGED_TIME;
			work = 1;
		}
		moist->pending_adc = 0;
		break;
	case ADC_STATE_START_WET_DETECT:
		set_cc_disable(moist, true);
		msleep(100);
		vadc_physical = sbu_vadc_read(moist);

		dev_info(&moist->dev, "%s: ADC_STATE_START_WET_DETECT: usb sbu adc = %d\n", __func__,
				vadc_physical);
		if (!moist->edge_moisture)
			set_cc_disable(moist, false);

		if (vadc_physical < sbu_adc_gnd_low_threshold) {
			moist->sbu_adc_state = ADC_STATE_GND;
			work = 1;
		} else if ((!moist->force_pr_sink && vadc_physical < sbu_adc_snk_low_threshold) ||
				(moist->force_pr_sink && vadc_physical < sbu_adc_low_threshold)) {
			if (!get_vbus_present(moist)){
				is_wet = check_sbu_by_count(moist, vadc_physical);
			} else if (vadc_physical < sbu_adc_low_threshold) {
				dev_info(&moist->dev, "%s: vbus is on, connector is wet\n",
						__func__);
				moist->pending_adc = vadc_physical;
				moist->sbu_adc_param.low_thr = vadc_physical - ADC_CHANGE_THR > 0 ?
						vadc_physical - ADC_CHANGE_THR : 0;
				moist->sbu_adc_param.high_thr = vadc_physical + ADC_CHANGE_THR > sbu_adc_low_threshold ?
						sbu_adc_low_threshold : vadc_physical + ADC_CHANGE_THR;
				moist->sbu_adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
			}
			if (moist->force_pr_sink && !is_wet) {
				moist->sbu_adc_state = ADC_STATE_CC_STOP_ONLY;
				work = 1;
				break;
			}
			if (is_wet) {
				moist->sbu_adc_state = ADC_STATE_WET;
				delay = 0;
				work = 1;
			} else {
				if (moist->method == MOISTURE_EDGE_PD_SBU_PU &&
						moist->edge_moisture)
					moist->sbu_tm_state = ADC_TM_HIGH_STATE;
				moist->sbu_adc_state = ADC_STATE_DRY;
				work = 1;
			}
		} else {
			moist->sbu_adc_state = ADC_STATE_DRY;
			moist->sbu_tm_state = ADC_TM_HIGH_STATE;
			work = 1;
		}
		break;

	case ADC_STATE_CC_STOP_ONLY:
		if (moist->force_pr_sink) {
			set_cc_disable(moist, true);
			msleep(100);
			vadc_physical = sbu_vadc_read(moist);
			set_cc_disable(moist, false);
			set_pr_sink(moist, true);
			if (vadc_physical > sbu_adc_snk_high_threshold){
				moist->sbu_adc_state = ADC_STATE_DRY;
				work = 1;
			} else if (vadc_physical < sbu_adc_low_threshold) {
				moist->sbu_adc_state = ADC_STATE_START_WET_DETECT;
				work = 1;
			}
		}
		break;

	case ADC_STATE_WAIT_FOR_DRY:
		if (vadc_physical > sbu_adc_high_threshold) {
			if (dry_count < ADC_MAX_DRY_COUNT) {
				dry_count++;
				delay = ADC_WFD_TIMEOUT;
				work = 1;
			} else {
				moist->sbu_adc_state = ADC_STATE_DRY;
				work = 1;
			}
		} else {
			moist->sbu_adc_state = ADC_STATE_WET;
			work = 1;
		}
		break;
	case ADC_STATE_WET:
		set_sbu_wet(moist);
		if (moist->sbu_tm_state == ADC_TM_HIGH_STATE) {
			if (vadc_physical > sbu_adc_high_threshold) {
				if (!get_vbus_present(moist)) {
					moist->sbu_adc_state = ADC_STATE_WAIT_FOR_DRY;
					dry_count = 0;
					work = 1;
					pm_stay_awake(&moist->dev);
					moist->sbu_run_work = true;
				} else {
					dev_info(&moist->dev, "%s: maybe adc is up by cable\n",
						__func__);
				}
			} else {
				if (get_vbus_present(moist)) {
					dev_info(&moist->dev, "%s: vbus is on\n", __func__);
				} else {
					dev_info(&moist->dev, "%s: vbus is off\n", __func__);
				}
				moist->sbu_adc_param.state_request = ADC_TM_HIGH_THR_ENABLE;
			}
		}
		if (moist->sbu_adc_state == ADC_STATE_WET) {
			moist->sbu_lock = false;
			moist->sbu_run_work = false;
			set_cc_disable(moist, true);
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
			fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_MD);
			fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_DISABLE);
#else
			gpiod_direction_output(moist->sbu_oe, 1);
			gpiod_direction_output(moist->sbu_sel, 0);
#endif
			moist->sbu_tm_state = ADC_TM_HIGH_STATE;
			work = 1;
			delay = (10 * HZ);
			moist->long_wet_count++;
			hrtimer_start(&moist->sbu_timer, ms_to_ktime(60 * 1000), HRTIMER_MODE_REL);
			dev_info(&moist->dev, "%s: delay: %lu(s)\n", __func__, delay/HZ);

			if ((moist->method == MOISTURE_EDGE_PU_SBU_PU ||
					moist->method == MOISTURE_EDGE_PD_SBU_PU_ASYNC) &&
					moist->edge_adc_state != ADC_STATE_WET) {
				dev_info(&moist->dev, "%s: forcely set wet state to edge\n",
						__func__);
				moist->edge_adc_state = ADC_STATE_WET;
				cancel_delayed_work(&moist->edge_adc_work);
				schedule_delayed_work(&moist->edge_adc_work, 0);
			}
		}
		break;
	case ADC_STATE_GND:
		if (moist->sbu_tm_state == ADC_TM_HIGH_STATE) {
			moist->sbu_adc_state = ADC_STATE_DRY;
			work = 1;
		} else {
			moist->sbu_adc_param.high_thr = sbu_adc_gnd_high_threshold;
		}
		break;
	default:
		break;
	}

	if (work) {
		schedule_delayed_work(&moist->sbu_adc_work, delay);
	} else {
		moist->sbu_run_work = false;
		msleep(50);
		if (moist->method != MOISTURE_EDGE_PD_SBU_PU || moist->sbu_moisture) {
			prev_adc_param = moist->sbu_adc_param;
			dev_info(&moist->dev, "%s: ADC PARAM low: %d, high: %d, irq: %d\n",
					__func__, moist->sbu_adc_param.low_thr,
					moist->sbu_adc_param.high_thr,
					moist->sbu_adc_param.state_request);
			ret = qpnp_adc_tm_channel_measure(moist->adc_tm_dev, &moist->sbu_adc_param);
			if (ret) {
				dev_err(&moist->dev, "%s: request ADC error %d\n", __func__, ret);
				goto out;
			}
		} else {
			if (moist->edge_moisture) {
				schedule_delayed_work(&moist->edge_adc_work, 100);
			}
		}
	}
 out:
 	if (!moist->sbu_run_work)
		pm_relax(&moist->dev);
	mutex_unlock(&moist->moist_lock);
}

static void lge_moist_init_sbu_adc_work(struct work_struct *w)
{
	struct lge_moisture *moist = container_of(w, struct lge_moisture, init_sbu_adc_work.work);
	int ret = 0;

	if (!moist->sbu_sel)
		return;

#ifdef CONFIG_LGE_USB_FACTORY
	if (moist->is_factory_boot)
		moist->prop_moisture_en = DUAL_ROLE_PROP_MOISTURE_EN_DISABLE;
#endif
#ifdef CONFIG_LGE_USB_COMPLIANCE_TEST
	moist->prop_moisture_en = DUAL_ROLE_PROP_MOISTURE_EN_DISABLE;
#endif

	mutex_lock(&moist->moist_lock);
	dev_info(&moist->dev, "%s\n", __func__);
	if (moist->prop_moisture_en == DUAL_ROLE_PROP_MOISTURE_EN_DISABLE) {
		dev_err(&moist->dev, "%s: DUAL_ROLE_PROP_MOISTURE_EN_DISABLE\n", __func__);
#ifndef CONFIG_LGE_USB_SWITCH_FUSB252
		gpiod_direction_output(moist->sbu_oe, 0);
		gpiod_direction_output(moist->sbu_sel, 0);
#endif
		goto out;
	}

	if (IS_ERR_OR_NULL(moist->adc_tm_dev)) {
		moist->adc_tm_dev = qpnp_get_adc_tm(moist->dev.parent->parent, "moisture-detection");
		if (IS_ERR(moist->adc_tm_dev)) {
			if (PTR_ERR(moist->adc_tm_dev) == -EPROBE_DEFER) {
				dev_err(&moist->dev, "qpnp vadc not yet probed.\n");
				schedule_delayed_work(&moist->init_sbu_adc_work,
						msecs_to_jiffies(200));
				goto out;
			}
		}
	}

	moist->adc_initialized = true;
	if (moist->method <= MOISTURE_EDGE_PD_SBU_PU_ASYNC || moist->edge_moisture) {
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
		fusb252_put(moist->fusb252_inst, FUSB252_FLAG_SBU_DISABLE);
		if (moist->method <= MOISTURE_EDGE_PD_SBU_PU_ASYNC) {
			fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_USBID);
		} else {
			if (!moist->sbu_moisture) {
				fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_MD_ING);
			}
		}
#else
		dev_info(&moist->dev, "sbu switch to usbid\n");
		gpiod_direction_output(moist->sbu_oe, 0);
		if (!moist->sbu_moisture)
			gpiod_direction_output(moist->sbu_sel, 1);
#endif
	}

	moist->sbu_lock = false;
	moist->sbu_adc_state = ADC_STATE_DRY;
	moist->sbu_adc_param.low_thr = sbu_adc_snk_low_threshold;
	moist->sbu_adc_param.high_thr = sbu_adc_snk_high_threshold;
	moist->sbu_adc_param.timer_interval = sbu_adc_meas_interval;
	moist->sbu_adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
	moist->sbu_adc_param.btm_ctx = moist;
	moist->sbu_adc_param.threshold_notification = lge_moist_sbu_notification;
	moist->sbu_adc_param.channel = SBU_ADC_CHANNEL;
	moist->force_pr_sink = false;

	if(moist->method <= MOISTURE_EDGE_PD_SBU_PU_ASYNC || moist->edge_moisture) {
		pr_err("[Jayci] %s: channel measure sbu\n", __func__);
		ret = qpnp_adc_tm_channel_measure(moist->adc_tm_dev, &moist->sbu_adc_param);
		if (ret) {
			dev_err(&moist->dev, "request ADC error %d\n", ret);
			goto out;
		}
	}
out:
	mutex_unlock(&moist->moist_lock);
}

static void lge_moist_init_edge_adc_work(struct work_struct *w)
{
	struct lge_moisture *moist = container_of(w, struct lge_moisture, init_edge_adc_work.work);
	int ret = 0;

	if (moist->method < MOISTURE_EDGE_PU_SBU_PU)
		return;

#ifdef CONFIG_LGE_USB_FACTORY
	if (moist->is_factory_boot)
		moist->prop_moisture_en = DUAL_ROLE_PROP_MOISTURE_EN_DISABLE;
#endif
#ifdef CONFIG_LGE_USB_COMPLIANCE_TEST
	moist->prop_moisture_en = DUAL_ROLE_PROP_MOISTURE_EN_DISABLE;
#endif

	mutex_lock(&moist->moist_lock);
	dev_info(&moist->dev, "%s\n", __func__);
	if (moist->prop_moisture_en == DUAL_ROLE_PROP_MOISTURE_EN_DISABLE) {
		dev_err(&moist->dev, "%s: DUAL_ROLE_PROP_MOISTURE_EN_DISABLE\n", __func__);
		goto out;
	}

	if (IS_ERR_OR_NULL(moist->adc_tm_dev)) {
		moist->adc_tm_dev = qpnp_get_adc_tm(moist->dev.parent->parent, "moisture-detection");
		if (IS_ERR(moist->adc_tm_dev)) {
			if (PTR_ERR(moist->adc_tm_dev) == -EPROBE_DEFER) {
				dev_err(&moist->dev, "qpnp vadc not yet probed.\n");
				schedule_delayed_work(&moist->init_edge_adc_work,
						msecs_to_jiffies(200));
				goto out;
			}
		}
	}

	if (IS_ERR_OR_NULL(moist->vadc_dev)) {
		moist->vadc_dev = qpnp_get_vadc(moist->dev.parent->parent, "moisture-detection");
		if (IS_ERR(moist->vadc_dev)) {
			if (PTR_ERR(moist->vadc_dev) == -EPROBE_DEFER) {
				dev_err(&moist->dev, "qpnp vadc not yet probed\n");
				schedule_delayed_work(&moist->init_edge_adc_work,
						msecs_to_jiffies(200));
				goto out;
			}
		}
	}

	moist->adc_initialized = true;
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
	if (moist->method == MOISTURE_EDGE_PU_SBU_PU ||
		moist->method == MOISTURE_EDGE_PD_SBU_PU_ASYNC)
		fusb252_get(moist->fusb252_inst, FUSB252_FLAG_SBU_USBID);
	else if (moist->method == MOISTURE_EDGE_PD_SBU_PU)
		fusb252_get(moist->fusb252_inst, FUSB252_FLAG_EDGE_MD_ING);
#else
	if (moist->method == MOISTURE_EDGE_PU_SBU_PU ||
			moist->method == MOISTURE_EDGE_PD_SBU_PU_ASYNC)
		gpiod_direction_output(moist->sbu_sel, 1);
	else if (moist->method == MOISTURE_EDGE_PD_SBU_PU)
		gpiod_direction_output(moist->sbu_sel, 0);
#endif
	if (moist->method == MOISTURE_EDGE_PU_SBU_PU) {
		edge_adc_high_threshold = PU_1V_ADC_THR_600KOHM;
		edge_adc_low_threshold = PU_1V_ADC_THR_470KOHM;
	}
	pr_err("[Jayci] %s: release edge_lock\n", __func__);
	moist->edge_lock = false;
	moist->edge_adc_state = ADC_STATE_DRY;
	moist->edge_adc_param.low_thr = edge_adc_low_threshold;
	moist->edge_adc_param.high_thr = edge_adc_high_threshold;
	moist->edge_adc_param.timer_interval = edge_adc_meas_interval;
	moist->edge_adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
	moist->edge_adc_param.btm_ctx = moist;
	moist->edge_adc_param.threshold_notification = lge_moist_edge_notification;
	moist->edge_adc_param.channel = EDGE_ADC_CHANNEL;
	ret = qpnp_adc_tm_channel_measure(moist->adc_tm_dev, &moist->edge_adc_param);
	if (ret) {
		dev_err(&moist->dev, "%s: request ADC error %d\n",__func__, ret);
		goto out;
	}
out:
	mutex_unlock(&moist->moist_lock);


}

/*************************************************************
* Sysfs nodes
*/

static ssize_t store_moisture_method(struct device *dev, struct device_attribute *attr,
               const char *buf, size_t size)
{
       struct lge_moisture *moist = dev_get_drvdata(dev);
       int method;

       sscanf(buf, "%d", &method);

       moist->method = method;

       if (moist->sbu_run_work) {
               moist->sbu_run_work = false;
               pm_relax(&moist->dev);
       }
       cancel_delayed_work(&moist->sbu_adc_work);
       cancel_delayed_work(&moist->init_sbu_adc_work);
       moist->sbu_lock = true;

       schedule_delayed_work(&moist->init_sbu_adc_work, (1500*HZ/1000));

       if (moist->edge_run_work) {
               moist->edge_run_work = false;
               pm_relax(&moist->dev);
       }
       cancel_delayed_work(&moist->edge_adc_work);
       cancel_delayed_work(&moist->init_edge_adc_work);
       moist->edge_lock = true;

       schedule_delayed_work(&moist->init_edge_adc_work, (1500*HZ/1000));


       return size;
}

static ssize_t show_moisture_method(struct device *dev, struct device_attribute *attr,
               char *buf)
{
       struct lge_moisture *moist = dev_get_drvdata(dev);
       int method;

       method = moist->method;

       return sprintf(buf, "%d\n", method);
}

static DEVICE_ATTR(moisture_method, S_IRUGO | S_IWUSR | S_IWGRP,
               show_moisture_method, store_moisture_method);

#ifndef CONFIG_LGE_USB_SWITCH_FUSB252
static ssize_t store_sbu_sel(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int sbu_sel;

	sscanf(buf, "%d", &sbu_sel);

	if (sbu_sel)
		gpiod_direction_output(moist->sbu_sel, 1);
	else
		gpiod_direction_output(moist->sbu_sel, 0);

	return size;
}

static ssize_t show_sbu_sel(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int sbu_sel;

	sbu_sel = gpiod_get_value(moist->sbu_sel);

	return sprintf(buf, "%d\n", sbu_sel);
}

static DEVICE_ATTR(sbu_sel, S_IRUGO | S_IWUSR | S_IWGRP, show_sbu_sel, store_sbu_sel);

static ssize_t store_sbu_oe(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int sbu_oe;

	sscanf(buf, "%d", &sbu_oe);

	if (sbu_oe)
		gpiod_direction_output(moist->sbu_oe, 1);
	else
		gpiod_direction_output(moist->sbu_oe, 0);

	return size;
}

static ssize_t show_sbu_oe(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int sbu_oe;

	sbu_oe = gpiod_get_value(moist->sbu_oe);

	return sprintf(buf, "%d\n", sbu_oe);
}

static DEVICE_ATTR(sbu_oe, S_IRUGO | S_IWUSR | S_IWGRP, show_sbu_oe, store_sbu_oe);
#endif

static ssize_t show_sbu_vadc(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int sbu_vadc;

	mutex_lock(&moist->moist_lock);
	sbu_vadc = sbu_vadc_read(moist);
	mutex_unlock(&moist->moist_lock);

	return sprintf(buf, "%d\n", sbu_vadc);
}

static DEVICE_ATTR(sbu_vadc, S_IRUGO, show_sbu_vadc, NULL);

static ssize_t show_edge_vadc(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int edge_vadc;

	edge_vadc = edge_vadc_read(moist);

	return sprintf(buf, "%d\n", edge_vadc);
}
static DEVICE_ATTR(edge_vadc, S_IRUGO, show_edge_vadc, NULL);

static ssize_t show_vbus_present(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	bool vbus_present;

	vbus_present = get_vbus_present(moist);
	if (vbus_present) {
		return sprintf(buf, "%d\n", 1);
	}

	return sprintf(buf, "%d\n", 0);
}

static DEVICE_ATTR(vbus_present, S_IRUGO, show_vbus_present, NULL);

static ssize_t show_typec_mode(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int typec_mode;

	typec_mode = get_typec_mode(moist);

	return sprintf(buf, "%d\n", typec_mode);
}

static DEVICE_ATTR(typec_mode, S_IRUGO, show_typec_mode, NULL);

static ssize_t show_sbu_moisture(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int sbu_moisture;

	sbu_moisture = moist->sbu_moisture;

	return sprintf(buf, "%d\n", sbu_moisture);
}

static ssize_t store_sbu_moisture(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int sbu_moisture;

	sscanf(buf, "%d", &sbu_moisture);

	mutex_lock(&moist->moist_lock);
	sbu_moisture ? set_sbu_wet(moist) : set_sbu_dry(moist);
	mutex_unlock(&moist->moist_lock);

	return size;
}

static DEVICE_ATTR(sbu_moisture, S_IRUGO | S_IWUSR | S_IWGRP, show_sbu_moisture,
		store_sbu_moisture);

static ssize_t show_edge_moisture(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int edge_moisture;

	edge_moisture = moist->edge_moisture;

	return sprintf(buf, "%d\n", edge_moisture);
}

static ssize_t store_edge_moisture(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int edge_moisture;

	sscanf(buf, "%d", &edge_moisture);

	mutex_lock(&moist->moist_lock);
	edge_moisture ? set_edge_wet(moist) : set_edge_dry(moist);
	mutex_unlock(&moist->moist_lock);

	return size;
}

static DEVICE_ATTR(edge_moisture, S_IRUGO | S_IWUSR | S_IWGRP, show_edge_moisture,
		store_edge_moisture);

static ssize_t show_cc_disable(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int cc_disable;

	cc_disable = moist->cc_disabled;

	return sprintf(buf, "%d\n", cc_disable);
}

static ssize_t store_cc_disable(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int cc_disable;

	sscanf(buf, "%d", &cc_disable);

	if (cc_disable)
		set_cc_disable(moist, true);
	else
		set_cc_disable(moist, false);

	return size;
}

static DEVICE_ATTR(cc_disable, S_IRUGO | S_IWUSR | S_IWGRP, show_cc_disable,
		store_cc_disable);

static ssize_t show_pr_sink(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	union power_supply_propval pval = { .intval = 0 };
	int pr_sink;
	int real_pr;

	pr_sink = moist->force_pr_sink;

	power_supply_get_property(moist->usb_psy, POWER_SUPPLY_PROP_TYPEC_POWER_ROLE, &pval);
	real_pr = pval.intval;

	return sprintf(buf, "%d,%d\n", pr_sink, real_pr);
}

static ssize_t store_pr_sink(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int pr_sink;

	sscanf(buf, "%d", &pr_sink);

	if (pr_sink)
		set_pr_sink(moist, true);
	else
		set_pr_sink(moist, false);

	return size;
}

static DEVICE_ATTR(pr_sink, S_IRUGO | S_IWUSR | S_IWGRP, show_pr_sink,
		store_pr_sink);

static ssize_t store_force_power_role(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int power_role;

	sscanf(buf, "%d", &power_role);


	set_force_power_role(moist, power_role);

	return size;
}

static DEVICE_ATTR(force_power_role, S_IWUSR | S_IWGRP, NULL,
		store_force_power_role);

static ssize_t show_moisture_enable(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct lge_moisture *moist = dev_get_drvdata(dev);
	int moisture_enable;

	moisture_enable = moist->prop_moisture_en;

	return sprintf(buf, "%d\n", moisture_enable);
}

static ssize_t store_moisture_enable(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int moisture_enable;

	sscanf(buf, "%d", &moisture_enable);
	lge_moisture_set_moisture_enable(moisture_enable);

	return size;
}

static DEVICE_ATTR(moisture_enable, S_IRUGO | S_IWUSR | S_IWGRP, show_moisture_enable,
		store_moisture_enable);

static void lge_moist_sysfs_init(struct device *dev, struct lge_moisture *moist)
{
	int ret = 0;

#ifndef CONFIG_LGE_USB_SWITCH_FUSB252
	if (moist->sbu_sel) {
		ret = device_create_file(dev, &dev_attr_sbu_sel);
		if (ret) {
			device_remove_file(dev, &dev_attr_sbu_sel);
			return;
		}
	}

	if (moist->sbu_oe) {
		ret = device_create_file(dev, &dev_attr_sbu_oe);
		if (ret) {
			device_remove_file(dev, &dev_attr_sbu_oe);
			return;
		}
	}
#endif

	ret = device_create_file(dev, &dev_attr_moisture_method);
	if (ret) {
		device_remove_file(dev, &dev_attr_moisture_method);
		return;
	}

	ret = device_create_file(dev, &dev_attr_sbu_vadc);
	if (ret) {
		device_remove_file(dev, &dev_attr_sbu_vadc);
		return;
	}

	ret = device_create_file(dev, &dev_attr_edge_vadc);
	if (ret) {
		device_remove_file(dev, &dev_attr_edge_vadc);
		return;
	}

	ret = device_create_file(dev, &dev_attr_vbus_present);
	if (ret) {
		device_remove_file(dev, &dev_attr_vbus_present);
		return;
	}

	ret = device_create_file(dev, &dev_attr_typec_mode);
	if (ret) {
		device_remove_file(dev, &dev_attr_typec_mode);
		return;
	}

	ret = device_create_file(dev, &dev_attr_sbu_moisture);
	if (ret) {
		device_remove_file(dev, &dev_attr_sbu_moisture);
		return;
	}

	ret = device_create_file(dev, &dev_attr_edge_moisture);
	if (ret) {
		device_remove_file(dev, &dev_attr_edge_moisture);
		return;
	}

	ret = device_create_file(dev, &dev_attr_cc_disable);
	if (ret) {
		device_remove_file(dev, &dev_attr_cc_disable);
		return;
	}

	ret = device_create_file(dev, &dev_attr_pr_sink);
	if (ret) {
		device_remove_file(dev, &dev_attr_pr_sink);
		return;
	}

	ret = device_create_file(dev, &dev_attr_moisture_enable);
	if (ret) {
		device_remove_file(dev, &dev_attr_moisture_enable);
		return;
	}
	ret = device_create_file(dev, &dev_attr_force_power_role);
	if (ret) {
		device_remove_file(dev, &dev_attr_force_power_role);
		return;
	}

}

static struct class lge_moisture_class = {
	.name = "lge_moisture",
	.owner = THIS_MODULE,
};

static int num_moisture_instances;

struct lge_moisture *lge_moisture_create(struct device *parent)
{
	int ret = 0;
	struct lge_moisture *moist;
#if defined(CONFIG_LGE_USB_FACTORY) && defined(CONFIG_LGE_PM_VENEER_PSY)
	union power_supply_propval pval = { .intval = 0 };
	int lge_factory_id = 0;
#endif

	moist = kzalloc(sizeof(*moist), GFP_KERNEL);
	if (!moist)
		return ERR_PTR(-ENOMEM);

	device_initialize(&moist->dev);
	moist->dev.class = &lge_moisture_class;
	moist->dev.parent = parent;
	dev_set_drvdata(&moist->dev, moist);

	ret = dev_set_name(&moist->dev, "lge_moisture%d", num_moisture_instances++);
	if (ret)
		goto free_moist;

	ret = device_init_wakeup(&moist->dev, true);
	if (ret)
		goto free_moist;

	ret = device_add(&moist->dev);
	if (ret)
		goto free_moist;

	moist->wq = alloc_ordered_workqueue("lge_moisture_wq", WQ_FREEZABLE);
	if (!moist->wq) {
		ret = -ENOMEM;
		goto del_moist;
	}

	__moist = moist;

	of_property_read_u32(parent->parent->of_node, "lge,moisture-method",
			&moist->method);

	dev_info(&moist->dev, "Using Moisture Detection Method:%d\n", moist->method);
	moist->edge_moisture = 0;

	hrtimer_init(&moist->sbu_timer, CLOCK_BOOTTIME, HRTIMER_MODE_REL);
	moist->sbu_timer.function = lge_moisture_sbu_timeout;

	mutex_init(&moist->moist_lock);
	mutex_init(&moist->vadc_read_lock);
	moist->long_wet_count = 0;
	moist->edge_only_count = 0;
	moist->sbu_moisture = 0;
	moist->edge_moisture = 0;

	if (moist->method >= MOISTURE_EDGE_PU_SBU_PU)  {
		hrtimer_init(&moist->edge_timer, CLOCK_BOOTTIME, HRTIMER_MODE_REL);
		moist->edge_timer.function = lge_moisture_edge_timeout;
		INIT_DELAYED_WORK(&moist->init_edge_adc_work, lge_moist_init_edge_adc_work);
		INIT_DELAYED_WORK(&moist->edge_adc_work, lge_moist_edge_adc_work);
	}
	INIT_DELAYED_WORK(&moist->init_sbu_adc_work, lge_moist_init_sbu_adc_work);
	INIT_DELAYED_WORK(&moist->sbu_ov_adc_work, lge_moist_sbu_ov_adc_work);
	INIT_DELAYED_WORK(&moist->sbu_adc_work, lge_moist_sbu_adc_work);
	INIT_DELAYED_WORK(&moist->sbu_corr_check_work, lge_moist_sbu_corr_check_work);
	INIT_WORK(&moist->change_fusb_work, lge_moist_change_fusb_work);

	moist->usb_psy = power_supply_get_by_name("usb");
	if (!moist->usb_psy) {
		dev_err(&moist->dev, "Could not get USB power_supply, deferring probe\n");
		ret = -EPROBE_DEFER;
		goto destroy_wq;
	}

#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
	moist->fusb252_desc.flags = FUSB252_FLAG_SBU_DISABLE |
		FUSB252_FLAG_SBU_MD | FUSB252_FLAG_SBU_MD_ING |
		FUSB252_FLAG_EDGE_MD | FUSB252_FLAG_EDGE_MD_ING |
		FUSB252_FLAG_SBU_USBID;
	moist->fusb252_inst = devm_fusb252_instance_register(&moist->dev,
						     &moist->fusb252_desc);
	if (!moist->fusb252_inst) {
		dev_err(&moist->dev, "Could not get FUSB252, deferring probe\n");
		ret = -EPROBE_DEFER;
		goto put_psy;
	}
	moist->sbu_sel = true;
#else
	moist->sbu_sel = devm_gpiod_get(parent->parent, "lge,sbu-sel", GPIOD_OUT_LOW);
	if (IS_ERR(moist->sbu_sel)) {
		dev_err(&moist->dev, "Unable to get sbu gpio\n");
		moist->sbu_sel = NULL;
	}

	moist->sbu_oe = devm_gpiod_get(parent->parent, "lge,sbu-oe", GPIOD_OUT_LOW);
	if (IS_ERR(moist->sbu_oe)) {
		dev_err(&moist->dev, "Unable to get sbu-oe gpio\n");
		moist->sbu_oe = NULL;
	}
#endif

#if defined(CONFIG_LGE_USB_FACTORY) && defined(CONFIG_LGE_PM_VENEER_PSY)
	ret = power_supply_get_property(moist->usb_psy,
			POWER_SUPPLY_PROP_RESISTANCE_ID, &pval);
	if (ret) {
		dev_err(&moist->dev, "USB-ID check fail: %d\n", ret);
	} else {
		lge_factory_id = pval.intval / 1000;
	}

	if (lge_get_factory_boot() ||
			lge_factory_id == 56 || lge_factory_id == 130 || lge_factory_id == 910) {
		pr_err("factory cable connected, disable moisture detection\n");
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
		moist->sbu_sel = false;
#else
		moist->sbu_sel = NULL;
#endif
		moist->is_factory_boot = true;
		moist->prop_moisture_en = DUAL_ROLE_PROP_MOISTURE_EN_DISABLE;
	} else {
		moist->is_factory_boot = false;
	}
#endif
	lge_moist_sysfs_init(&moist->dev, moist);

#ifdef CONFIG_LGE_USB_COMPLIANCE_TEST
	moist->prop_moisture_en = DUAL_ROLE_PROP_MOISTURE_EN_DISABLE;
#else
	moist->prop_moisture_en = DUAL_ROLE_PROP_MOISTURE_EN_ENABLE;
	schedule_delayed_work(&moist->init_sbu_adc_work, 0);
	if (moist->method >= MOISTURE_EDGE_PU_SBU_PU)
		schedule_delayed_work(&moist->init_edge_adc_work, 0);
#endif

	return moist;

#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
put_psy:
	power_supply_put(moist->usb_psy);
#endif
destroy_wq:
	destroy_workqueue(moist->wq);
del_moist:
	device_del(&moist->dev);
free_moist:
	kfree(moist);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(lge_moisture_create);

void lge_moisture_destroy(struct lge_moisture *moisture)
{
	if (!moisture)
		return;
#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
	devm_fusb252_instance_unregister(&moisture->dev,
					 moisture->fusb252_inst);
#endif
	destroy_workqueue(moisture->wq);
	device_del(&moisture->dev);
	kfree(moisture);
}
EXPORT_SYMBOL(lge_moisture_destroy);

static int __init lge_moisture_init(void)
{
	return class_register(&lge_moisture_class);
}
module_init(lge_moisture_init);

static void __exit lge_moisture_exit(void)
{
	class_unregister(&lge_moisture_class);
}

module_exit(lge_moisture_exit);
MODULE_DESCRIPTION("LGE Moisture Detection Driver");
MODULE_LICENSE("GPL v2");

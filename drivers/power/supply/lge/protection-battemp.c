#define pr_fmt(fmt) "BTP: %s: " fmt, __func__
#define pr_battemp(reason, fmt, ...)			\
do {							\
	if (pr_debugmask & (reason))			\
		pr_info(fmt, ##__VA_ARGS__);		\
	else						\
		pr_debug(fmt, ##__VA_ARGS__);		\
} while (0)

static int pr_debugmask;

#include <linux/of.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pm_wakeup.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>

#include "veneer-primitives.h"

#define BATTEMP_NOTREADY INT_MAX
#define BATTEMP_WAKELOCK "lge-btp-scenario"

#define VOTER_NAME_ICHARGE "BTP"
#define VOTER_NAME_VFLOAT "BTP"

static struct protection_battemp {
	struct delayed_work	battemp_dwork;
	struct wakeup_source	battemp_wakelock;
	bool			battemp_charging;
	int			battemp_health;

	// processed in external
	bool (*get_protection_battemp)(bool* charging, int* temperature, int* mvoltage);
	void (*set_protection_battemp)(int health, int micharge, int mvfloat);

	struct voter_entry voter_icharge;
	struct voter_entry voter_vfloat;

// below fields are set in device tree
	int threshold_degc_upto_cool;	//  30 by default
	int threshold_degc_upto_good;	// 120 by default
	int threshold_degc_upto_warm;	// 450 by default
	int threshold_degc_upto_hot;	// 550 by default
	int threshold_degc_downto_warm;	// 520 by default
	int threshold_degc_downto_good;	// 430 by default
	int threshold_degc_downto_cool;	// 100 by default
	int threshold_degc_downto_cold;	//   0 by default

	int period_ms_emergency;	// 10000 by default
	int period_ms_warning;		// 30000 by default
	int period_ms_normal;		// 60000 by default
const	int period_ms_unknown;

	int cool_mv_alert;
	int cool_ma_alert;
	int cool_ma_normal;

	int warm_ma_charge;
	int warm_mv_float;

} battemp_me  = {
	.battemp_health = POWER_SUPPLY_HEALTH_UNKNOWN,
	.battemp_charging = false,

	.voter_icharge = { .type = VOTER_TYPE_INVALID },
	.voter_vfloat = { .type = VOTER_TYPE_INVALID },

	.threshold_degc_upto_cool = BATTEMP_NOTREADY,
	.threshold_degc_upto_good = BATTEMP_NOTREADY,
	.threshold_degc_upto_warm = BATTEMP_NOTREADY,
	.threshold_degc_upto_hot = BATTEMP_NOTREADY,
	.threshold_degc_downto_warm = BATTEMP_NOTREADY,
	.threshold_degc_downto_good = BATTEMP_NOTREADY,
	.threshold_degc_downto_cool = BATTEMP_NOTREADY,
	.threshold_degc_downto_cold = BATTEMP_NOTREADY,

	.period_ms_emergency = BATTEMP_NOTREADY,
	.period_ms_warning = BATTEMP_NOTREADY,
	.period_ms_normal = BATTEMP_NOTREADY,
	.period_ms_unknown = 1000,

	.cool_mv_alert	= BATTEMP_NOTREADY,
	.cool_ma_alert	= BATTEMP_NOTREADY,
	.cool_ma_normal	= BATTEMP_NOTREADY,
	.warm_ma_charge	= BATTEMP_NOTREADY,
	.warm_mv_float	= BATTEMP_NOTREADY,
};

static const char* health_to_string(int health) {

	switch (health) {
	case POWER_SUPPLY_HEALTH_UNKNOWN :
		return "HEALTH_UNKNOWN";
	case POWER_SUPPLY_HEALTH_COLD :
		return "HEALTH_COLD";
	case POWER_SUPPLY_HEALTH_COOL :
		return "HEALTH_COOL";
	case POWER_SUPPLY_HEALTH_GOOD :;
		return "HEALTH_GOOD";
	case POWER_SUPPLY_HEALTH_WARM :
		return "HEALTH_WARM";
	case POWER_SUPPLY_HEALTH_HOT :
		return "HEALTH_HOT";
	default :
		return "Undefined health";
	}
}

static int health_to_index(int health) {

	switch (health) {
	case POWER_SUPPLY_HEALTH_UNKNOWN :
		return 0;
	case POWER_SUPPLY_HEALTH_COLD :
		return 1;
	case POWER_SUPPLY_HEALTH_COOL :
		return 2;
	case POWER_SUPPLY_HEALTH_GOOD :;
		return 3;
	case POWER_SUPPLY_HEALTH_WARM :
		return 4;
	case POWER_SUPPLY_HEALTH_HOT :
		return 5;
	default :
		return -1;
	}
}

static long health_to_period(int health) {
	int msecs = 0;

	switch (health) {
	case POWER_SUPPLY_HEALTH_HOT :
	case POWER_SUPPLY_HEALTH_COLD :
		msecs = battemp_me.period_ms_emergency;
		break;
	case POWER_SUPPLY_HEALTH_WARM :
	case POWER_SUPPLY_HEALTH_COOL :
		msecs = battemp_me.period_ms_warning;
		break;
	case POWER_SUPPLY_HEALTH_GOOD :
		msecs = battemp_me.period_ms_normal;
		break;
	case POWER_SUPPLY_HEALTH_UNKNOWN :
		msecs = battemp_me.period_ms_unknown;
		break;
	default :
		pr_battemp(ERROR, "Check the battemp_health\n");
		break;
	}

	return msecs_to_jiffies(msecs);
}

static int health_to_icharge(int health, int batvol) {

	switch (health) {
	case POWER_SUPPLY_HEALTH_COOL :
		return (batvol >= battemp_me.cool_mv_alert)
			? battemp_me.cool_ma_alert : battemp_me.cool_ma_normal;
	case POWER_SUPPLY_HEALTH_WARM :
		return battemp_me.warm_ma_charge;

	case POWER_SUPPLY_HEALTH_HOT :
	case POWER_SUPPLY_HEALTH_COLD :
		return VOTE_TOTALLY_BLOCKED;

	case POWER_SUPPLY_HEALTH_GOOD :
	case POWER_SUPPLY_HEALTH_UNKNOWN :
		return VOTE_TOTALLY_RELEASED;

	default :
		return -EINVAL;
	}
}

static int health_to_vfloat(int health) {

	switch (health) {
	case POWER_SUPPLY_HEALTH_GOOD :
	case POWER_SUPPLY_HEALTH_COOL :
	case POWER_SUPPLY_HEALTH_COLD :
	case POWER_SUPPLY_HEALTH_UNKNOWN :
		return VOTE_TOTALLY_RELEASED;

	case POWER_SUPPLY_HEALTH_HOT :
	case POWER_SUPPLY_HEALTH_WARM :
		return battemp_me.warm_mv_float;

	default :
		return -EINVAL;
	}
}

static int polling_health(int health_now, int battemp_now) {
	int health_new;

	#define STAT_NOW (health_now)
	#define TEMP_NOW (battemp_now)

	#define TEMP_UPTO_COOL (battemp_me.threshold_degc_upto_cool)		//   30 by default
	#define TEMP_UPTO_GOOD (battemp_me.threshold_degc_upto_good)		//  120 by default
	#define TEMP_UPTO_WARM (battemp_me.threshold_degc_upto_warm)		//  450 by default
	#define TEMP_UPTO_HOT (battemp_me.threshold_degc_upto_hot)		//  550 by default
	#define TEMP_DOWNTO_WARM (battemp_me.threshold_degc_downto_warm)	//  520 by default
	#define TEMP_DOWNTO_GOOD (battemp_me.threshold_degc_downto_good)	//  430 by default
	#define TEMP_DOWNTO_COOL (battemp_me.threshold_degc_downto_cool)	//  100 by default
	#define TEMP_DOWNTO_COLD (battemp_me.threshold_degc_downto_cold)	//    0 by default

	switch (STAT_NOW) {
	case POWER_SUPPLY_HEALTH_UNKNOWN :
		if (TEMP_NOW < TEMP_DOWNTO_COLD)
			health_new = POWER_SUPPLY_HEALTH_COLD;
		else if (TEMP_NOW < TEMP_DOWNTO_COOL)
			health_new = POWER_SUPPLY_HEALTH_COOL;
		else if (TEMP_NOW < TEMP_UPTO_WARM )
			health_new = POWER_SUPPLY_HEALTH_GOOD;
		else if (TEMP_NOW < TEMP_UPTO_HOT)
			health_new = POWER_SUPPLY_HEALTH_WARM;
		else
			health_new = POWER_SUPPLY_HEALTH_HOT;
		break;

	case POWER_SUPPLY_HEALTH_COLD : // on the cold
		if (TEMP_NOW < TEMP_UPTO_COOL)
			health_new = POWER_SUPPLY_HEALTH_COLD;
		else if (TEMP_NOW < TEMP_DOWNTO_COOL)
			health_new = POWER_SUPPLY_HEALTH_COOL;
		else if (TEMP_NOW < TEMP_UPTO_WARM )
			health_new = POWER_SUPPLY_HEALTH_GOOD;
		else if (TEMP_NOW < TEMP_UPTO_HOT)
			health_new = POWER_SUPPLY_HEALTH_WARM;
		else
			health_new = POWER_SUPPLY_HEALTH_HOT;
		break;

	case POWER_SUPPLY_HEALTH_COOL : // on the cool
		if (TEMP_NOW < TEMP_DOWNTO_COLD)
			health_new = POWER_SUPPLY_HEALTH_COLD;
		else if (TEMP_NOW < TEMP_UPTO_GOOD)
			health_new = POWER_SUPPLY_HEALTH_COOL;
		else if (TEMP_NOW < TEMP_UPTO_WARM )
			health_new = POWER_SUPPLY_HEALTH_GOOD;
		else if (TEMP_NOW < TEMP_UPTO_HOT)
			health_new = POWER_SUPPLY_HEALTH_WARM;
		else
			health_new = POWER_SUPPLY_HEALTH_HOT;
		break;

	case POWER_SUPPLY_HEALTH_GOOD : // on the normal
		if (TEMP_NOW < TEMP_DOWNTO_COLD)
			health_new = POWER_SUPPLY_HEALTH_COLD;
		else if (TEMP_NOW < TEMP_DOWNTO_COOL)
			health_new = POWER_SUPPLY_HEALTH_COOL;
		else if (TEMP_NOW < TEMP_UPTO_WARM )
			health_new = POWER_SUPPLY_HEALTH_GOOD;
		else if (TEMP_NOW < TEMP_UPTO_HOT)
			health_new = POWER_SUPPLY_HEALTH_WARM;
		else
			health_new = POWER_SUPPLY_HEALTH_HOT;
		break;

	case POWER_SUPPLY_HEALTH_WARM : // on the warm
		if (TEMP_NOW < TEMP_DOWNTO_COLD)
			health_new = POWER_SUPPLY_HEALTH_COLD;
		else if (TEMP_NOW < TEMP_DOWNTO_COOL)
			health_new = POWER_SUPPLY_HEALTH_COOL;
		else if (TEMP_NOW < TEMP_DOWNTO_GOOD )
			health_new = POWER_SUPPLY_HEALTH_GOOD;
		else if (TEMP_NOW < TEMP_UPTO_HOT)
			health_new = POWER_SUPPLY_HEALTH_WARM;
		else
			health_new = POWER_SUPPLY_HEALTH_HOT;
		break;

	case POWER_SUPPLY_HEALTH_HOT : // on the hot
		if (TEMP_NOW < TEMP_DOWNTO_COLD)
			health_new = POWER_SUPPLY_HEALTH_COLD;
		else if (TEMP_NOW < TEMP_DOWNTO_COOL)
			health_new = POWER_SUPPLY_HEALTH_COOL;
		else if (TEMP_NOW < TEMP_UPTO_WARM )
			health_new = POWER_SUPPLY_HEALTH_GOOD;
		else if (TEMP_NOW < TEMP_DOWNTO_WARM)
			health_new = POWER_SUPPLY_HEALTH_WARM;
		else
			health_new = POWER_SUPPLY_HEALTH_HOT;
		break;
	default :
		health_new = POWER_SUPPLY_HEALTH_UNKNOWN;
		break;
	}

	return health_new;
}

static void polling_work(struct work_struct* work) {
	bool charging;
	int  temperature;
	int  mvoltage;

	if (battemp_me.get_protection_battemp(&charging, &temperature, &mvoltage)) {
		// calculating data
		int updated_health =
			polling_health(battemp_me.battemp_health, temperature);
		int updated_icharge = charging ?
			health_to_icharge(updated_health, mvoltage) : VOTE_TOTALLY_RELEASED;
		int updated_vfloat = charging ?
			health_to_vfloat(updated_health) : VOTE_TOTALLY_RELEASED;

		// configure wakelock
		if ((updated_health != POWER_SUPPLY_HEALTH_GOOD && charging)
			|| (updated_health == POWER_SUPPLY_HEALTH_HOT)) {
			if (!battemp_me.battemp_wakelock.active) {
				pr_battemp(UPDATE, "Acquiring wake lock\n");
				__pm_stay_awake(&battemp_me.battemp_wakelock);
			}
		}
		else {
			if (battemp_me.battemp_wakelock.active) {
				pr_battemp(UPDATE, "Releasing wake lock\n");
				__pm_relax(&battemp_me.battemp_wakelock);
			}
		}

		// logging for changes
		if (battemp_me.battemp_health != updated_health)
			pr_battemp(UPDATE, "%s(%d) -> %s(%d), temperature=%d\n",
				health_to_string(battemp_me.battemp_health),
					health_to_index(battemp_me.battemp_health),
				health_to_string(updated_health),
					health_to_index(updated_health),
				temperature);

		// Voting for icharge and vfloat
		veneer_voter_set(&battemp_me.voter_icharge, updated_icharge);
		veneer_voter_set(&battemp_me.voter_vfloat, updated_vfloat);

		// update member status in 'battemp_me'
		battemp_me.battemp_health = updated_health;
		battemp_me.battemp_charging = charging;

		// finallly, notify results to client
		battemp_me.set_protection_battemp(battemp_me.battemp_health, updated_icharge, updated_vfloat);
	}
	else
		pr_battemp(UPDATE, "temperature is not valid.\n");

	schedule_delayed_work(to_delayed_work(work), health_to_period(battemp_me.battemp_health));
	return;
}

static bool battemp_create_parsedt(struct device_node* dnode, int mincap) {
	int rc = 0;
	int cool_ma_pct = 0, warm_ma_pct = 0;

	OF_PROP_READ_S32(dnode, battemp_me.threshold_degc_upto_cool,
		"threshold-degc-upto-cool", rc);
	OF_PROP_READ_S32(dnode, battemp_me.threshold_degc_upto_good,
		"threshold-degc-upto-good", rc);
	OF_PROP_READ_S32(dnode, battemp_me.threshold_degc_upto_warm,
		"threshold-degc-upto-warm", rc);
	OF_PROP_READ_S32(dnode, battemp_me.threshold_degc_upto_hot,
		"threshold-degc-upto-hot", rc);
	OF_PROP_READ_S32(dnode, battemp_me.threshold_degc_downto_warm,
		"threshold-degc-downto-warm", rc);
	OF_PROP_READ_S32(dnode, battemp_me.threshold_degc_downto_good,
		"threshold-degc-downto-good", rc);
	OF_PROP_READ_S32(dnode, battemp_me.threshold_degc_downto_cool,
		"threshold-degc-downto-cool", rc);
	OF_PROP_READ_S32(dnode, battemp_me.threshold_degc_downto_cold,
		"threshold-degc-downto-cold", rc);

	OF_PROP_READ_S32(dnode, battemp_me.period_ms_emergency,
		"period-ms-emergency", rc);
	OF_PROP_READ_S32(dnode, battemp_me.period_ms_warning,
		"period-ms-warning", rc);
	OF_PROP_READ_S32(dnode, battemp_me.period_ms_normal,
		"period-ms-normal", rc);

	OF_PROP_READ_S32(dnode, battemp_me.cool_mv_alert,
		"cool-mv-alert", rc);
	OF_PROP_READ_S32(dnode, battemp_me.cool_ma_alert,
		"cool-ma-alert", rc);
	OF_PROP_READ_S32(dnode, cool_ma_pct, "cool-ma-pct", rc);
		battemp_me.cool_ma_normal = mincap * cool_ma_pct / 100;

	OF_PROP_READ_S32(dnode, battemp_me.warm_mv_float,
		"warm-mv-float", rc);
	OF_PROP_READ_S32(dnode, warm_ma_pct, "warm-ma-pct", rc);
		battemp_me.warm_ma_charge = mincap * warm_ma_pct / 100;

	return !rc;
}

static bool battemp_create_voters(void) {
	return veneer_voter_register(&battemp_me.voter_icharge, VOTER_NAME_ICHARGE, VOTER_TYPE_IBAT, false)
		&& veneer_voter_register(&battemp_me.voter_vfloat, VOTER_NAME_VFLOAT, VOTER_TYPE_VFLOAT, false);
}

static bool battemp_create_preset(bool (*feed_protection_battemp)(bool* charging, int* temperature, int* mvoltage),
	void (*back_protection_battemp)(int health, int micharge, int mvfloat)) {

	if( feed_protection_battemp && back_protection_battemp ) {
		battemp_me.get_protection_battemp = feed_protection_battemp;
		battemp_me.set_protection_battemp = back_protection_battemp;
	}
	else {
		pr_battemp(ERROR, "feed/back func should not be null\n");
		return false;
	}

	wakeup_source_init(&battemp_me.battemp_wakelock,
		BATTEMP_WAKELOCK);

	INIT_DELAYED_WORK(&battemp_me.battemp_dwork,
		polling_work);

	return true;
}

void protection_battemp_monitor(void) {
	if (delayed_work_pending(&battemp_me.battemp_dwork))
		cancel_delayed_work(&battemp_me.battemp_dwork);
	schedule_delayed_work(&battemp_me.battemp_dwork, msecs_to_jiffies(0));
}

bool protection_battemp_create(struct device_node* dnode, int mincap,
	bool (*feed_protection_battemp)(bool* charging, int* temperature, int* mvoltage),
	void (*back_protection_battemp)(int health, int micharge, int mvfloat)) {
	pr_debugmask = ERROR | UPDATE;

	if (!battemp_create_preset(feed_protection_battemp, back_protection_battemp)) {
		pr_battemp(ERROR, "error on battemp_create_preset");
		goto destroy;
	}

	if (!battemp_create_parsedt(dnode, mincap)) {
		pr_battemp(ERROR, "error on battemp_create_devicetree");
		goto destroy;
	}

	if (!battemp_create_voters()) {
		pr_battemp(ERROR, "error on battemp_create_voters");
		goto destroy;
	}

	protection_battemp_monitor();
	pr_battemp(UPDATE, "Complete to create\n");
	return true;
destroy:
	protection_battemp_destroy();
	return false;
}

void protection_battemp_destroy(void) {

	wakeup_source_trash(&battemp_me.battemp_wakelock);
	cancel_delayed_work_sync(&battemp_me.battemp_dwork);

	battemp_me.battemp_health = POWER_SUPPLY_HEALTH_UNKNOWN;
	battemp_me.battemp_charging = false;

	battemp_me.get_protection_battemp = NULL;
	battemp_me.set_protection_battemp = NULL;

	veneer_voter_unregister(&battemp_me.voter_icharge);
	veneer_voter_unregister(&battemp_me.voter_vfloat);

	battemp_me.threshold_degc_upto_cool   = BATTEMP_NOTREADY;
	battemp_me.threshold_degc_upto_good   = BATTEMP_NOTREADY;
	battemp_me.threshold_degc_upto_warm   = BATTEMP_NOTREADY;
	battemp_me.threshold_degc_upto_hot    = BATTEMP_NOTREADY;
	battemp_me.threshold_degc_downto_warm = BATTEMP_NOTREADY;
	battemp_me.threshold_degc_downto_good = BATTEMP_NOTREADY;
	battemp_me.threshold_degc_downto_cool = BATTEMP_NOTREADY;
	battemp_me.threshold_degc_downto_cold = BATTEMP_NOTREADY;

	battemp_me.period_ms_emergency = BATTEMP_NOTREADY;
	battemp_me.period_ms_warning   = BATTEMP_NOTREADY;
	battemp_me.period_ms_normal    = BATTEMP_NOTREADY;

	battemp_me.cool_mv_alert  = BATTEMP_NOTREADY,
	battemp_me.cool_ma_alert  = BATTEMP_NOTREADY,
	battemp_me.cool_ma_normal = BATTEMP_NOTREADY,
	battemp_me.warm_ma_charge = BATTEMP_NOTREADY;
	battemp_me.warm_mv_float  = BATTEMP_NOTREADY;
}



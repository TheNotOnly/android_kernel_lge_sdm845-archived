#define pr_fmt(fmt) "SHOWCASE: %s: " fmt, __func__
#define pr_showcase(fmt, ...) pr_err(fmt, ##__VA_ARGS__)

#include <linux/of.h>
#include <linux/power_supply.h>

#include "veneer-primitives.h"

enum showcase_status {
	SHOWCASE_CHARGING_RELEASE,
	SHOWCASE_CHARGING_BLOCK,
	SHOWCASE_CHARGING_HOLD,
};

static struct showcase_charging {
	enum showcase_status charging_status;
	bool (*charging_getdata)(bool* enabled, bool* charging, int* capacity);
	void (*charging_changed)(const char* status);

	struct voter_entry voter_ibat;
	struct voter_entry voter_iusb;
	struct voter_entry voter_idc;

	int soc_min;
	int soc_max;
} showcase;

static const char* status_to_string(enum showcase_status status) {
	switch (status) {
	case SHOWCASE_CHARGING_RELEASE :
		return "SHOWCASE_CHARGING_RELEASE";
	case SHOWCASE_CHARGING_BLOCK :
		return "SHOWCASE_CHARGING_BLOCK";
	case SHOWCASE_CHARGING_HOLD :
		return "SHOWCASE_CHARGING_HOLD";
	default :
		return "SHOWCASE_CHARGING_INVALID";
	}
}

static void status_to_vote(enum showcase_status status) {
	switch (status) {
	case SHOWCASE_CHARGING_RELEASE :
		veneer_voter_release(&showcase.voter_ibat);
		veneer_voter_release(&showcase.voter_iusb);
		veneer_voter_release(&showcase.voter_idc);
		break;
	case SHOWCASE_CHARGING_BLOCK :
		veneer_voter_set(&showcase.voter_ibat, VOTE_TOTALLY_BLOCKED);
		veneer_voter_set(&showcase.voter_iusb, VOTE_TOTALLY_BLOCKED);
		veneer_voter_set(&showcase.voter_idc, VOTE_TOTALLY_BLOCKED);
		break;
	case SHOWCASE_CHARGING_HOLD :
		veneer_voter_set(&showcase.voter_ibat, VOTE_TOTALLY_BLOCKED);
		veneer_voter_release(&showcase.voter_iusb);
		veneer_voter_release(&showcase.voter_idc);
		break;
	default :
		pr_showcase("Invalid parameter %d\n", status);
		break;
	}
}

static enum showcase_status showcase_charging_transit(bool enabled, bool charging, int capacity, enum showcase_status now) {
	#define SOC_MAX showcase.soc_max
	#define SOC_MIN showcase.soc_min

	if (enabled && charging) {
		if (capacity < SOC_MIN)
			return SHOWCASE_CHARGING_RELEASE;
		else if (SOC_MAX < capacity)
			return SHOWCASE_CHARGING_BLOCK;
		else if (capacity == SOC_MAX || now == SHOWCASE_CHARGING_BLOCK) /* SOC_MIN<capacity<=SOC_MAX && DISCHARGING */
			return SHOWCASE_CHARGING_HOLD;
		else /* SOC_MIN<capacity<=SOC_MAX && ~DISCHARGING */
			return now; // not changed
	}
	else
		return SHOWCASE_CHARGING_RELEASE;
}

static bool showcase_create_parsedt(struct device_node* dnode) {
	int rc = 0;

	OF_PROP_READ_S32(dnode, showcase.soc_max, "soc-max", rc);
	OF_PROP_READ_S32(dnode, showcase.soc_min, "soc-min", rc);

	pr_showcase("soc-max : %d, soc-min : %d\n",
		showcase.soc_max, showcase.soc_min);

	return !rc;
}

static bool showcase_create_voters(void) {
	#define SHOWCASE_VOTER "SHOWCASE"

	return veneer_voter_register(&showcase.voter_ibat, SHOWCASE_VOTER, VOTER_TYPE_IBAT, false)
		&& veneer_voter_register(&showcase.voter_iusb, SHOWCASE_VOTER, VOTER_TYPE_IUSB, true)
		&& veneer_voter_register(&showcase.voter_idc, SHOWCASE_VOTER, VOTER_TYPE_IDC, true);
}

static bool showcase_create_preset(bool (*feed_protection_showcase)(bool* enabled, bool* charging, int* capacity),
	void (*back_protection_showcase)(const char* status)) {

	if(feed_protection_showcase && back_protection_showcase) {
		showcase.charging_getdata = feed_protection_showcase;
		showcase.charging_changed = back_protection_showcase;
	}
	else {
		pr_showcase("feed/back func should not be null\n");
		return false;
	}

	return true;
}

void protection_showcase_update(void) {
	enum showcase_status status_now = showcase.charging_status;
	enum showcase_status status_new;

	bool enabled;
	bool charging;
	int capacity;

	if (showcase.charging_getdata && showcase.charging_changed
		&& showcase.charging_getdata(&enabled, &charging, &capacity)) {
		status_new = showcase_charging_transit(enabled, charging, capacity, status_now);

		if (status_now != status_new) {
			status_to_vote(status_new);
			showcase.charging_status = status_new;
			showcase.charging_changed(status_to_string(status_new));
		}
	}
	else {
		pr_showcase("Updating showcase charging is not ready\n");
	}
}

bool protection_showcase_create(struct device_node* dnode,
	bool (*feed_protection_showcase)(bool* enabled, bool* charging, int* capacity),
	void (*back_protection_showcase)(const char* status)) {

	if (!showcase_create_preset(feed_protection_showcase, back_protection_showcase)) {
		pr_showcase("error on showcase_create_preset");
		goto destroy;
	}

	if (!showcase_create_parsedt(dnode)) {
		pr_showcase("error on showcase_create_devicetree");
		goto destroy;
	}

	if (!showcase_create_voters()) {
		pr_showcase("error on showcase_create_voters");
		goto destroy;
	}

	pr_showcase("Complete to create\n");
	return true;

destroy:
	protection_showcase_destroy();
	return false;
}

void protection_showcase_destroy(void) {
	veneer_voter_unregister(&showcase.voter_ibat);
	veneer_voter_unregister(&showcase.voter_iusb);
	veneer_voter_unregister(&showcase.voter_idc);

	memset(&showcase, 0 ,sizeof(showcase));
}

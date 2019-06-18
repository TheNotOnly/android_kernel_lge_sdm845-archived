#define pr_fmt(fmt) "BOOTMOD: %s: " fmt, __func__
#define pr_bootmod(fmt, ...) pr_err(fmt, ##__VA_ARGS__)

#include "veneer-primitives.h"


//////////////////////////////////////////////////////////////////////
// for detecting boot mode
//////////////////////////////////////////////////////////////////////

static char bootmode_cable [128] = { 0, };
static char bootmode_android [128] = { 0, };
static enum veneer_bootmode bootmode_type = BOOTMODE_ETC_UNKNOWN;

static void unified_bootmode_setup(void) {
	bootmode_type =
		!strcmp(bootmode_android, "qem_56k")	 ?
			(!strcmp(bootmode_cable, "LT_56K")
				? BOOTMODE_MINIOS_56K : BOOTMODE_MINIOS_AAT) :
		!strcmp(bootmode_android, "miniOS")	 ? BOOTMODE_MINIOS_AAT :
		!strcmp(bootmode_android, "qem_130k")	 ? BOOTMODE_MINIOS_130K :
		!strcmp(bootmode_android, "pif_910k")	 ? BOOTMODE_LAF_910K :
		!strcmp(bootmode_android, "qem_910k")	 ? BOOTMODE_LAF_910K :
		!strcmp(bootmode_android, "pif_56k")	 ? BOOTMODE_ANDROID_56K :
		!strcmp(bootmode_android, "pif_130k")	 ? BOOTMODE_ANDROID_130K :
		!strcmp(bootmode_android, "chargerlogo") ? BOOTMODE_ETC_CHARGERLOGO
			: BOOTMODE_ANDROID_NORMAL;

	pr_bootmod("input=(%s, %s)->%s\n", bootmode_cable, bootmode_android,
		unified_bootmode_name());
}

const char* unified_bootmode_name(void) {
	switch (bootmode_type) {
	case BOOTMODE_ANDROID_NORMAL:	return "A_N";
	case BOOTMODE_ANDROID_56K:	return "A_5";
	case BOOTMODE_ANDROID_130K:	return "A_1";
	case BOOTMODE_ANDROID_910K:	return "A_9";
	case BOOTMODE_MINIOS_AAT:	return "M_A";
	case BOOTMODE_MINIOS_56K:	return "M_5";
	case BOOTMODE_MINIOS_130K:	return "M_1";
	case BOOTMODE_MINIOS_910K:	return "M_9";
	case BOOTMODE_LAF_NORMAL:	return "L_N";
	case BOOTMODE_LAF_910K:		return "L_9";
	case BOOTMODE_ETC_CHARGERLOGO:	return "E_C";
	case BOOTMODE_ETC_RECOVERY:	return "E_R";
	default :
		break;
	}
	return "???"; // BOOTMODE_ETC_UNKNOWN
}

enum charger_usbid unified_bootmode_usbid(void) {
	switch (bootmode_type) {
	case BOOTMODE_ANDROID_56K:
	case BOOTMODE_MINIOS_56K:	return CHARGER_USBID_56KOHM;

	case BOOTMODE_ANDROID_130K:
	case BOOTMODE_MINIOS_130K:	return CHARGER_USBID_130KOHM;

	case BOOTMODE_ANDROID_910K:
	case BOOTMODE_MINIOS_910K:
	case BOOTMODE_LAF_910K:		return CHARGER_USBID_910KOHM;

	default:
		break;
	}

	return CHARGER_USBID_OPEN;
}

bool unified_bootmode_fabproc(void) {
	switch (bootmode_type) {
	case BOOTMODE_MINIOS_56K:
	case BOOTMODE_MINIOS_130K:
	case BOOTMODE_LAF_910K:
		return true;
	default:
		break;
	}

	return false;
}

void unified_bootmode_cable(char* arg) {
	strcpy(bootmode_cable, arg);
	unified_bootmode_setup();
}

void unified_bootmode_android(char* arg) {
	strcpy(bootmode_android, arg);
	unified_bootmode_setup();
}

bool unified_bootmode_chargerlogo(void) {
	return bootmode_type == BOOTMODE_ETC_CHARGERLOGO;
}

enum veneer_bootmode unified_bootmode_type(void) {
	return bootmode_type;
}


//////////////////////////////////////////////////////////////////////
// for branching the "charger_verbose" sysfs
//////////////////////////////////////////////////////////////////////

static bool bootmode_chgverbose = false;

static int __init setup_bootmode_chgverbose(char* s) {
	int result = 0;

	if (!kstrtoint(s, 10, &result))
		bootmode_chgverbose = !!result;
	else
		bootmode_chgverbose = false;

	pr_bootmod("lge.charger_verbose=%s->%s\n",
		s, bootmode_chgverbose ? "true" : "false");

	return 1;
}
__setup("lge.charger_verbose=", setup_bootmode_chgverbose);

bool unified_bootmode_chgverbose(void) {
	return bootmode_chgverbose;
}



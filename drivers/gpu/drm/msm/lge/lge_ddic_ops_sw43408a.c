#define pr_fmt(fmt)	"[Display][sw43408a-ops:%s:%d] " fmt, __func__, __LINE__

#include "dsi_panel.h"
#include "lge_ddic_ops_helper.h"

#define ADDR_PTLAR 0x30
#define ADDR_PLTAC 0x31
#define ADDR_RDDISPM 0x3F

#define WORDS_TO_BYTE_ARRAY(w1, w2, b) do {\
		b[0] = WORD_UPPER_BYTE(w1);\
		b[1] = WORD_LOWER_BYTE(w1);\
		b[2] = WORD_UPPER_BYTE(w2);\
		b[3] = WORD_LOWER_BYTE(w2);\
} while(0)


extern int lge_mdss_dsi_panel_cmd_read(struct dsi_panel *panel,
					u8 cmd, int cnt, char* ret_buf);

const struct drs_res_info sw43408a_res[3] = {
	{"qhd", 0, 1440, 3120},
	{"fhd", 1, 1080, 2340},
	{"hd", 3, 720, 1440},
};

static void adjust_roi(struct dsi_panel *panel, int *sr, int *er)
{
	u32 cur_res = panel->cur_mode->timing.v_active;
	int type, num = panel->num_timing_nodes;

	for (type = 0; type < num; type++) {
		if (cur_res == sw43408a_res[type].height)
			break;
	}
	if (type == num) {
		pr_err("invalid height\n");
		*sr = 0;
		*er = panel->cur_mode->timing.h_active - 1;
		return;
	}

	if ((panel->lge.aod_area.w == 0) || (panel->lge.aod_area.h == 0)) {
		pr_err("area (w=%d)(h=%d), please check with application\n",
				panel->lge.aod_area.w, panel->lge.aod_area.h);
		goto full_roi;
	}

	if (!strncmp(sw43408a_res[type].resolution, "hd", 2)) {
		goto full_roi;
	}
	else if (!strncmp(sw43408a_res[type].resolution, "fhd", 3)) {
		*sr = (((panel->lge.aod_area.y) >> 2) << 2);
		*er = *sr + panel->lge.aod_area.h - 1;
	} else {
		*sr = panel->lge.aod_area.y;
		*er = *sr + panel->lge.aod_area.h - 1;
	}

	return;

full_roi:
	*sr = 0;
	*er = *sr + sw43408a_res[0].height - 1;
	return;
}

static void prepare_cmd(struct dsi_cmd_desc *cmds, int cmds_count, int addr, int param1, int param2)
{
	struct dsi_cmd_desc *cmd = NULL;
	char *payload = NULL;

	cmd = find_cmd(cmds, cmds_count, addr);
	if (cmd) {
		payload = (char *)cmd->msg.tx_buf;
		payload++;
		WORDS_TO_BYTE_ARRAY(param1, param2, payload);
	} else {
		pr_warn("cmd for addr 0x%02X not found\n", addr);
	}
}

static int prepare_aod_cmds_sw43408a(struct dsi_panel *panel, struct dsi_cmd_desc *cmds, int cmds_count)
{
	int rc = 0, sr = 0, er = 0;

	if (panel == NULL || cmds == NULL || cmds_count == 0)
		return -EINVAL;

	adjust_roi(panel, &sr, &er);
	prepare_cmd(cmds, cmds_count, ADDR_PTLAR, sr, er);

	return rc;
}

static int get_current_resolution_sw43408a(struct dsi_panel *panel)
{
	u8 reg;

	lge_mdss_dsi_panel_cmd_read(panel, (u8)ADDR_RDDISPM, 1, &reg);

	return (int)reg;
}

static void get_support_resolution_sw43408a(int idx, void *input)
{
	struct drs_res_info *res = (struct drs_res_info *)input;

	res->data = sw43408a_res[idx].data;
	res->height = sw43408a_res[idx].height;

	return;
}

struct lge_ddic_ops sw43408a_ops = {
	.store_aod_area = store_aod_area,
	.prepare_aod_cmds = prepare_aod_cmds_sw43408a,
	.hdr_mode_set = NULL,
	.bist_ctrl = NULL,
	.release_bist = NULL,
	.get_current_res = get_current_resolution_sw43408a,
	.get_support_res = get_support_resolution_sw43408a,
};

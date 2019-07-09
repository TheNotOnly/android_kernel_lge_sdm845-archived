#define pr_fmt(fmt)	"[Display][rm69299-ops:%s:%d] " fmt, __func__, __LINE__

#include "dsi_panel.h"
#include "lge_ddic_ops_helper.h"
#include "cm/lge_color_manager.h"

#define ADDR_PTLAR 0x30

#define WORDS_TO_BYTE_ARRAY(w1, w2, b) do {\
		b[0] = WORD_UPPER_BYTE(w1);\
		b[1] = WORD_LOWER_BYTE(w1);\
		b[2] = WORD_UPPER_BYTE(w2);\
		b[3] = WORD_LOWER_BYTE(w2);\
} while(0)

extern int lge_ddic_dsi_panel_tx_cmd_set(struct dsi_panel *panel,
				enum lge_ddic_dsi_cmd_set_type type);

const struct drs_res_info rm69299_res[1] = {
	{"fhd", 0, 1080, 2248},
};

static void adjust_roi(struct dsi_panel *panel, int *sr, int *er)
{
	u32 cur_res = panel->cur_mode->timing.v_active;
	int type, num = panel->num_timing_nodes;

	for (type = 0; type < num; type++) {
		if (cur_res == rm69299_res[type].height)
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

	*sr = panel->lge.aod_area.y;
	*er = *sr + panel->lge.aod_area.h - 1;

	return;

full_roi:
	*sr = 0;
	*er = *sr + rm69299_res[0].height - 1;
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

static void prepare_power_optimize_cmds(struct dsi_panel *panel, struct dsi_cmd_desc *cmds, int cmds_count, bool optimize)
{
	/* To Do */
	/*
	struct dsi_cmd_desc *cmd = NULL;
	char *payload = NULL;
	*/
	pr_info("%s:%s\n", __func__, (optimize ? "set" : "unset"));
}

static void prepare_aod_area_rm69299(struct dsi_panel *panel, struct dsi_cmd_desc *cmds, int cmds_count)
{
	int sr = 0, er = 0;

	if (panel == NULL || cmds == NULL || cmds_count == 0)
		return;

	adjust_roi(panel, &sr, &er);
	prepare_cmd(cmds, cmds_count, ADDR_PTLAR, sr, er);

	return;
}

static int prepare_aod_cmds_rm69299(struct dsi_panel *panel, struct dsi_cmd_desc *cmds, int cmds_count)
{
	int rc = 0;

	if (panel == NULL || cmds == NULL || cmds_count == 0)
		return -EINVAL;

	if (panel->lge.aod_power_mode &&
			(panel->lge.aod_area.h != rm69299_res[0].height)) {
		prepare_power_optimize_cmds(panel, cmds, cmds_count, true);
	} else {
		prepare_power_optimize_cmds(panel, cmds, cmds_count, false);
	}

	return rc;
}

void lge_check_vert_black_line_rm69299(struct dsi_panel *panel)
{
	int rc = 0;

	mutex_lock(&panel->panel_lock);
	pr_info("send LGE_DDIC_DSI_DETECT_BLACK_VERT_LINE\n");
	rc = lge_ddic_dsi_panel_tx_cmd_set(panel, LGE_DDIC_DSI_DETECT_BLACK_VERT_LINE);
	mutex_unlock(&panel->panel_lock);

	if (rc)
		pr_err("failed to send DETECT_BLACK_VERT_LINE cmd, rc=%d\n", rc);
}

void lge_check_vert_white_line_rm69299(struct dsi_panel *panel)
{
	int rc = 0;

	mutex_lock(&panel->panel_lock);
	pr_info("send LGE_DDIC_DSI_DETECT_WHITE_VERT_LINE\n");
	rc = lge_ddic_dsi_panel_tx_cmd_set(panel, LGE_DDIC_DSI_DETECT_WHITE_VERT_LINE);
	mutex_unlock(&panel->panel_lock);

	if (rc)
		pr_err("failed to send DETECT_WHITE_VERT_LINE cmd, rc=%d\n", rc);
}

void lge_check_vert_line_restore_rm69299(struct dsi_panel *panel)
{
	int rc = 0;

	pr_info("%s\n", __func__);

	mutex_lock(&panel->panel_lock);
	pr_info("send LGE_DDIC_DSI_DETECT_VERT_LINE_RESTORE");
	rc = lge_ddic_dsi_panel_tx_cmd_set(panel, LGE_DDIC_DSI_DETECT_VERT_LINE_RESTORE);
	mutex_unlock(&panel->panel_lock);

	if (rc)
		pr_err("failed to send DETECT_VERT_LINE_RESTORE cmd, rc=%d\n", rc);
}

struct lge_ddic_ops rm69299_ops = {
	.store_aod_area = store_aod_area,
	.prepare_aod_cmds = prepare_aod_cmds_rm69299,
	.prepare_aod_area = prepare_aod_area_rm69299,
	.lge_check_vert_black_line = lge_check_vert_black_line_rm69299,
	.lge_check_vert_white_line = lge_check_vert_white_line_rm69299,
	.lge_check_vert_line_restore = lge_check_vert_line_restore_rm69299,
	/* To Do : dimming control */
	.lge_bc_dim_set = NULL,
	.lge_set_therm_dim = NULL,
	.lge_get_brightness_dim = NULL,
	.lge_set_brightness_dim = NULL,
	/* To Do : Image Quality */
	.lge_set_custom_rgb = NULL,
	.lge_display_control_store = NULL,
	.lge_set_screen_tune = NULL,
	.lge_set_screen_mode = NULL,
	.sharpness_set = NULL,
	.lge_set_video_enhancement = NULL,
	/* drs not supported */
	.get_current_res = NULL,
	.get_support_res = NULL,
	/* bist not supported */
	.bist_ctrl = NULL,
	.release_bist = NULL,
	/* error detect not supported */
	.err_detect_work = NULL,
	.err_detect_irq_handler = NULL,
	.set_err_detect_mask = NULL,
	/* pps not used */
	.set_pps_cmds = NULL,
	.unset_pps_cmds = NULL,
	/* irc not supported */
	.set_irc_default_state = NULL,
	.set_irc_state = NULL,
	.get_irc_state = NULL,
	/* bist not supported */
	.bist_ctrl = NULL,
	.release_bist = NULL,
};

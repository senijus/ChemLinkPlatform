#include "ui_alarm.h"
#include "ui_common.h"
#include "alarm_mgr.h"
#include "auth.h"
#include <stdio.h>

#define ALARM_ITEM_H 72

static lv_obj_t *alarm_container = NULL;
static lv_obj_t *lbl_count = NULL;
static lv_obj_t *btn_clear = NULL;
static int g_last_alarm_version = -1;

static uint32_t type_color(int type)
{
    switch (type) {
        case ALARM_TYPE_TEM_HIGH: case ALARM_TYPE_TEM_LOW:
            return 0xe94560;
        case ALARM_TYPE_HUM_HIGH: case ALARM_TYPE_HUM_LOW:
            return 0x3498db;
        case ALARM_TYPE_GAS_HIGH: case ALARM_TYPE_GAS_LOW:
            return 0x2ecc71;
        case ALARM_TYPE_ACC_HIGH: case ALARM_TYPE_ACC_LOW:
            return 0xf39c12;
        case ALARM_TYPE_RS485_VOLT_HIGH: case ALARM_TYPE_RS485_VOLT_LOW:
        case ALARM_TYPE_RS485_ELEC_HIGH: case ALARM_TYPE_RS485_ELEC_LOW:
            return 0x1abc9c;
        case ALARM_TYPE_CAN_SPEED_HIGH: case ALARM_TYPE_CAN_SPEED_LOW:
        case ALARM_TYPE_CAN_FLOW_HIGH: case ALARM_TYPE_CAN_FLOW_LOW:
            return 0xe67e22;
        default:
            return 0xaaaaaa;
    }
}

static void clear_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    AlarmMgrClearAll();
    ui_alarm_refresh();
}

lv_obj_t *ui_alarm_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, SCREEN_WIDTH, CONTENT_H);
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, STATUS_BAR_H);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x0a0a23), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 8, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    // 标题栏
    lv_obj_t *title_bar = lv_obj_create(page);
    lv_obj_set_size(title_bar, SCREEN_WIDTH - 16, 36);
    lv_obj_set_pos(title_bar, 4, 4);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 6, 0);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_count = lv_label_create(title_bar);
    lv_label_set_text(lbl_count, "Alarms: 0 (Unread: 0)");
    lv_obj_set_style_text_color(lbl_count, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_font(lbl_count, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_count, LV_ALIGN_LEFT_MID, 10, 0);

    btn_clear = lv_btn_create(title_bar);
    lv_obj_set_size(btn_clear, 90, 26);
    lv_obj_align(btn_clear, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(btn_clear, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_radius(btn_clear, 4, 0);
    lv_obj_add_event_cb(btn_clear, clear_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_btn = lv_label_create(btn_clear);
    lv_label_set_text(lbl_btn, "Clear All");
    lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl_btn, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_btn);

    // 可滚动的告警容器
    alarm_container = lv_obj_create(page);
    lv_obj_set_size(alarm_container, SCREEN_WIDTH - 16, CONTENT_H - 50);
    lv_obj_set_pos(alarm_container, 4, 46);
    lv_obj_set_style_bg_color(alarm_container, lv_color_hex(0x0a0a23), 0);
    lv_obj_set_style_bg_opa(alarm_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(alarm_container, 0, 0);
    lv_obj_set_style_radius(alarm_container, 0, 0);
    lv_obj_set_style_pad_all(alarm_container, 0, 0);
    lv_obj_set_scrollbar_mode(alarm_container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(alarm_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(alarm_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(alarm_container, 4, 0);

    return page;
}

void ui_alarm_refresh(void)
{
    if (alarm_container == NULL) return;

    const AlarmList_t *alarm_list = AlarmMgrGetList();
    if (alarm_list->version == g_last_alarm_version) return;
    g_last_alarm_version = alarm_list->version;

    lv_obj_clean(alarm_container);

    char buf[64];
    snprintf(buf, sizeof(buf), "Alarms: %d (Unread: %d)", alarm_list->count, alarm_list->unread);
    lv_label_set_text(lbl_count, buf);

    if (AuthIsAdmin())
        lv_obj_clear_flag(btn_clear, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(btn_clear, LV_OBJ_FLAG_HIDDEN);

    for (int i = alarm_list->count - 1; i >= 0; i--) {
        const AlarmRecord_t *rec = &alarm_list->records[i];
        uint32_t c = type_color(rec->type);
        lv_color_t color = lv_color_hex(c);

        // 告警面板
        lv_obj_t *panel = lv_obj_create(alarm_container);
        lv_obj_set_size(panel, SCREEN_WIDTH - 30, ALARM_ITEM_H);
        lv_obj_set_style_bg_color(panel,
            rec->confirmed ? lv_color_hex(0x16213e) : lv_color_hex(0x1a2740), 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_radius(panel, 6, 0);
        lv_obj_set_style_pad_all(panel, 6, 0);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 2, 0);

        // 左边框颜色指示
        lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(panel, color, 0);
        lv_obj_set_style_border_width(panel, 3, 0);

        // 第1行：类型名（彩色）+ 来源
        lv_obj_t *row1 = lv_obj_create(panel);
        lv_obj_set_size(row1, LV_SIZE_CONTENT, 18);
        lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row1, 0, 0);
        lv_obj_set_style_pad_all(row1, 0, 0);
        lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row1, 6, 0);

        lv_obj_t *lbl_type = lv_label_create(row1);
        lv_label_set_text(lbl_type, AlarmMgrTypeName(rec->type));
        lv_obj_set_style_text_color(lbl_type, color, 0);
        lv_obj_set_style_text_font(lbl_type, &lv_font_montserrat_14, 0);

        lv_obj_t *lbl_source = lv_label_create(row1);
        char src_buf[32];
        snprintf(src_buf, sizeof(src_buf), "· %s", rec->source);
        lv_label_set_text(lbl_source, src_buf);
        lv_obj_set_style_text_color(lbl_source, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(lbl_source, &lv_font_montserrat_12, 0);

        // 第2行：数值 vs 阈值
        lv_obj_t *row2 = lv_obj_create(panel);
        lv_obj_set_size(row2, LV_SIZE_CONTENT, 18);
        lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row2, 0, 0);
        lv_obj_set_style_pad_all(row2, 0, 0);

        lv_obj_t *lbl_value = lv_label_create(row2);
        lv_label_set_text(lbl_value, "");
        AlarmMgrFormatDesc(rec, buf, sizeof(buf));
        // 提取 "value op threshold" 部分（去掉 [source] 前缀）
        // FormatDesc 输出: "[source] TypeName: value unit op threshold unit"
        // 我们只取 ":" 后面的部分
        char *colon = strchr(buf, ':');
        if (colon) {
            lv_label_set_text(lbl_value, colon + 2);  // 跳过 ": "
        } else {
            lv_label_set_text(lbl_value, buf);
        }
        lv_obj_set_style_text_color(lbl_value,
            rec->confirmed ? lv_color_hex(0x888888) : lv_color_hex(0xdddddd), 0);
        lv_obj_set_style_text_font(lbl_value, &lv_font_montserrat_12, 0);

        // 第3行：时间戳
        lv_obj_t *row3 = lv_obj_create(panel);
        lv_obj_set_size(row3, LV_SIZE_CONTENT, 16);
        lv_obj_set_style_bg_opa(row3, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row3, 0, 0);
        lv_obj_set_style_pad_all(row3, 0, 0);

        lv_obj_t *lbl_time = lv_label_create(row3);
        lv_label_set_text(lbl_time, rec->timestamp);
        lv_obj_set_style_text_color(lbl_time, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_12, 0);
    }
}

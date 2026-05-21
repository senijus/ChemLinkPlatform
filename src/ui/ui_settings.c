#include "ui_settings.h"
#include "ui_common.h"
#include "alarm_mgr.h"
#include "auth.h"
#include "dev_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLIDER_COUNT 16

static lv_obj_t *sliders[SLIDER_COUNT] = {NULL};
static lv_obj_t *lbl_vals[SLIDER_COUNT] = {NULL};

static const char *slider_labels[SLIDER_COUNT] = {
    "Temp Max:", "Temp Min:", "Hum Max:", "Hum Min:",
    "Gas Max:",  "Gas Min:",  "Acc Max:", "Acc Min:",
    "RS485 Volt Max:", "RS485 Volt Min:", "RS485 Elec Max:", "RS485 Elec Min:",
    "CAN Speed Max:",  "CAN Speed Min:",  "CAN Flow Max:",  "CAN Flow Min:"
};

static const uint32_t slider_colors_hex[SLIDER_COUNT] = {
    0xe94560, 0xe94560, 0x3498db, 0x3498db,
    0x2ecc71, 0x2ecc71, 0xf39c12, 0xf39c12,
    0x1abc9c, 0x1abc9c, 0x9b59b6, 0x9b59b6,
    0xe67e22, 0xe67e22, 0x3498db, 0x3498db
};

// 每个滑块的最大值（下限为 0）
static const int slider_ranges[SLIDER_COUNT] = {
    150, 150, 100, 100,
    150, 150, 100, 100,
    1000, 1000, 500, 500,
    100000, 100000, 100000, 100000
};

static lv_obj_t *btn_logout = NULL;
static lv_obj_t *ta_new_pwd = NULL;
static lv_obj_t *lbl_pwd_msg = NULL;

// 设备管理
static lv_obj_t *list_rs485 = NULL;
static lv_obj_t *list_can = NULL;
static lv_obj_t *ta_rs485_id = NULL;
static lv_obj_t *ta_can_id = NULL;
static lv_obj_t *lbl_dev_msg = NULL;

// 设置页键盘
static lv_obj_t *settings_keyboard = NULL;
static lv_obj_t *active_settings_ta = NULL;

static void settings_ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        active_settings_ta = ta;
        if (settings_keyboard == NULL) {
            settings_keyboard = lv_keyboard_create(lv_scr_act());
            lv_obj_set_size(settings_keyboard, SCREEN_WIDTH, 160);
            lv_obj_align(settings_keyboard, LV_ALIGN_BOTTOM_LEFT, 0, -NAV_BAR_H);
        }
        lv_keyboard_set_textarea(settings_keyboard, ta);
        lv_obj_clear_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY) {
        if (settings_keyboard) {
            lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        active_settings_ta = NULL;
    }
}

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    char buf[16];
    for (int i = 0; i < SLIDER_COUNT; i++) {
        if (slider == sliders[i]) {
            snprintf(buf, sizeof(buf), "%.0f", (double)lv_slider_get_value(slider));
            lv_label_set_text(lbl_vals[i], buf);
            break;
        }
    }
}

static void apply_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    AlarmThreshold_t thr;
    thr.tem_max = (double)lv_slider_get_value(sliders[0]);
    thr.tem_min = (double)lv_slider_get_value(sliders[1]);
    thr.hum_max = (double)lv_slider_get_value(sliders[2]);
    thr.hum_min = (double)lv_slider_get_value(sliders[3]);
    thr.gas_max = (double)lv_slider_get_value(sliders[4]);
    thr.gas_min = (double)lv_slider_get_value(sliders[5]);
    thr.acc_max = (double)lv_slider_get_value(sliders[6]);
    thr.acc_min = (double)lv_slider_get_value(sliders[7]);
    thr.rs485_volt_max = (double)lv_slider_get_value(sliders[8]);
    thr.rs485_volt_min = (double)lv_slider_get_value(sliders[9]);
    thr.rs485_elec_max = (double)lv_slider_get_value(sliders[10]);
    thr.rs485_elec_min = (double)lv_slider_get_value(sliders[11]);
    thr.can_speed_max = (double)lv_slider_get_value(sliders[12]);
    thr.can_speed_min = (double)lv_slider_get_value(sliders[13]);
    thr.can_flow_max = (double)lv_slider_get_value(sliders[14]);
    thr.can_flow_min = (double)lv_slider_get_value(sliders[15]);
    AlarmMgrSetThreshold(&thr);
}

static void logout_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    AuthLogout();
    ui_status_bar_set_role("Guest");
    ui_nav_bar_update_visibility(ROLE_NONE);
    lv_obj_add_flag(g_nav_bar, LV_OBJ_FLAG_HIDDEN);
    ui_switch_page(PAGE_LOGIN);
}

static void pwd_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    const char *new_pwd = lv_textarea_get_text(ta_new_pwd);
    if (strlen(new_pwd) < 3) {
        lv_label_set_text(lbl_pwd_msg, "Too short (min 3)");
        lv_obj_set_style_text_color(lbl_pwd_msg, lv_color_hex(0xff4444), 0);
        return;
    }

    if (AuthIsAdmin()) {
        AuthChangeOperatorPassword(new_pwd);
        lv_label_set_text(lbl_pwd_msg, "Operator pwd changed");
        lv_obj_set_style_text_color(lbl_pwd_msg, lv_color_hex(0x44ff44), 0);
    } else {
        lv_label_set_text(lbl_pwd_msg, "No permission");
        lv_obj_set_style_text_color(lbl_pwd_msg, lv_color_hex(0xff4444), 0);
        return;
    }

    lv_textarea_set_text(ta_new_pwd, "");
}

static void rs485_list_btn_cb(lv_event_t *e);
static void can_list_btn_cb(lv_event_t *e);

static void refresh_rs485_list(void)
{
    if (list_rs485 == NULL) return;
    lv_obj_clean(list_rs485);

    int ids[RS485_MAX_DEV];
    int count = 0;
    Rs485GetDevices(ids, &count);

    char buf[32];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "RS485-%d  " LV_SYMBOL_CLOSE, ids[i]);
        lv_obj_t *btn = lv_list_add_btn(list_rs485, LV_SYMBOL_OK, buf);
        lv_obj_set_user_data(btn, (void *)(intptr_t)ids[i]);
        lv_obj_add_event_cb(btn, rs485_list_btn_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void refresh_can_list(void)
{
    if (list_can == NULL) return;
    lv_obj_clean(list_can);

    int ids[CAN_MAX_DEV];
    int count = 0;
    CanGetDevices(ids, &count);

    char buf[32];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "CAN-0x%X  " LV_SYMBOL_CLOSE, ids[i]);
        lv_obj_t *btn = lv_list_add_btn(list_can, LV_SYMBOL_OK, buf);
        lv_obj_set_user_data(btn, (void *)(intptr_t)ids[i]);
        lv_obj_add_event_cb(btn, can_list_btn_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void rs485_add_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char *text = lv_textarea_get_text(ta_rs485_id);
    if (text == NULL || text[0] == '\0') {
        lv_label_set_text(lbl_dev_msg, "Please enter RS485 ID (1-247)");
        lv_obj_set_style_text_color(lbl_dev_msg, lv_color_hex(0xff4444), 0);
        return;
    }
    int id = atoi(text);
    if (Rs485AddDevice(id) == 0) {
        lv_label_set_text(lbl_dev_msg, "RS485 device added");
        lv_obj_set_style_text_color(lbl_dev_msg, lv_color_hex(0x44ff44), 0);
        refresh_rs485_list();
    } else {
        lv_label_set_text(lbl_dev_msg, "Invalid or duplicate ID (1-247)");
        lv_obj_set_style_text_color(lbl_dev_msg, lv_color_hex(0xff4444), 0);
    }
    lv_textarea_set_text(ta_rs485_id, "");
}

static void can_add_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char *text = lv_textarea_get_text(ta_can_id);
    if (text == NULL || text[0] == '\0') {
        lv_label_set_text(lbl_dev_msg, "Please enter CAN ID (e.g. 0x100)");
        lv_obj_set_style_text_color(lbl_dev_msg, lv_color_hex(0xff4444), 0);
        return;
    }
    int id = (int)strtol(text, NULL, 0);
    if (CanAddDevice(id) == 0) {
        lv_label_set_text(lbl_dev_msg, "CAN device added");
        lv_obj_set_style_text_color(lbl_dev_msg, lv_color_hex(0x44ff44), 0);
        refresh_can_list();
    } else {
        lv_label_set_text(lbl_dev_msg, "Invalid or duplicate CAN ID");
        lv_obj_set_style_text_color(lbl_dev_msg, lv_color_hex(0xff4444), 0);
    }
    lv_textarea_set_text(ta_can_id, "");
}

static void rs485_list_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *btn = lv_event_get_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(btn);
    Rs485RemoveDevice(id);
    refresh_rs485_list();
}

static void can_list_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *btn = lv_event_get_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(btn);
    CanRemoveDevice(id);
    refresh_can_list();
}

static void create_slider_row(lv_obj_t *parent, int idx, int y_pos)
{
    lv_color_t color = lv_color_hex(slider_colors_hex[idx]);

    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, slider_labels[idx]);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl, 10, y_pos);

    sliders[idx] = lv_slider_create(parent);
    lv_obj_set_size(sliders[idx], 260, 10);
    lv_obj_set_pos(sliders[idx], 190, y_pos + 2);
    lv_slider_set_range(sliders[idx], 0, slider_ranges[idx]);
    lv_obj_set_style_bg_color(sliders[idx], lv_color_hex(0x2c3e50), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sliders[idx], color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sliders[idx], color, LV_PART_KNOB);
    lv_obj_add_event_cb(sliders[idx], slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_vals[idx] = lv_label_create(parent);
    lv_label_set_text(lbl_vals[idx], "0");
    lv_obj_set_style_text_color(lbl_vals[idx], lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl_vals[idx], &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_vals[idx], 460, y_pos);
}

lv_obj_t *ui_settings_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, SCREEN_WIDTH, CONTENT_H);
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, STATUS_BAR_H);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x0a0a23), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);

    // === 区域 1：告警阈值（左半屏） ===
    lv_obj_t *lbl_section1 = lv_label_create(page);
    lv_label_set_text(lbl_section1, "Alarm Thresholds");
    lv_obj_set_style_text_color(lbl_section1, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_font(lbl_section1, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_section1, 10, 8);

    for (int i = 0; i < SLIDER_COUNT; i++) {
        create_slider_row(page, i, 30 + i * 24);
    }

    lv_obj_t *btn_apply = lv_btn_create(page);
    lv_obj_set_size(btn_apply, 80, 26);
    lv_obj_set_pos(btn_apply, 420, 30 + SLIDER_COUNT * 24 / 2 - 13);
    lv_obj_set_style_bg_color(btn_apply, lv_color_hex(0x0f3460), 0);
    lv_obj_add_event_cb(btn_apply, apply_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_apply = lv_label_create(btn_apply);
    lv_label_set_text(lbl_apply, "Apply");
    lv_obj_set_style_text_color(lbl_apply, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl_apply, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_apply);

    // === 区域 2：设备管理（右半屏） ===
    int dev_y = 8;

    lv_obj_t *lbl_section2 = lv_label_create(page);
    lv_label_set_text(lbl_section2, "Device Management");
    lv_obj_set_style_text_color(lbl_section2, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_font(lbl_section2, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_section2, 520, dev_y);
    dev_y += 22;

    lv_obj_t *lbl_rs485 = lv_label_create(page);
    lv_label_set_text(lbl_rs485, "RS485:");
    lv_obj_set_style_text_color(lbl_rs485, lv_color_hex(0x1abc9c), 0);
    lv_obj_set_style_text_font(lbl_rs485, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_rs485, 520, dev_y);

    list_rs485 = lv_list_create(page);
    lv_obj_set_size(list_rs485, 250, 80);
    lv_obj_set_pos(list_rs485, 520, dev_y + 16);
    lv_obj_set_style_bg_color(list_rs485, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(list_rs485, lv_color_hex(0x2c3e50), 0);
    lv_obj_set_style_radius(list_rs485, 4, 0);
    dev_y += 100;

    ta_rs485_id = lv_textarea_create(page);
    lv_obj_set_size(ta_rs485_id, 100, 26);
    lv_obj_set_pos(ta_rs485_id, 520, dev_y);
    lv_textarea_set_one_line(ta_rs485_id, true);
    lv_textarea_set_placeholder_text(ta_rs485_id, "ID (1-247)");
    lv_textarea_set_max_length(ta_rs485_id, 3);
    lv_obj_add_event_cb(ta_rs485_id, settings_ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btn_rs485_add = lv_btn_create(page);
    lv_obj_set_size(btn_rs485_add, 60, 26);
    lv_obj_set_pos(btn_rs485_add, 630, dev_y);
    lv_obj_set_style_bg_color(btn_rs485_add, lv_color_hex(0x2ecc71), 0);
    lv_obj_add_event_cb(btn_rs485_add, rs485_add_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_a = lv_label_create(btn_rs485_add);
    lv_label_set_text(lbl_a, "Add");
    lv_obj_set_style_text_color(lbl_a, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl_a, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_a);
    dev_y += 32;

    lv_obj_t *lbl_can = lv_label_create(page);
    lv_label_set_text(lbl_can, "CAN:");
    lv_obj_set_style_text_color(lbl_can, lv_color_hex(0xe67e22), 0);
    lv_obj_set_style_text_font(lbl_can, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_can, 520, dev_y);

    list_can = lv_list_create(page);
    lv_obj_set_size(list_can, 250, 80);
    lv_obj_set_pos(list_can, 520, dev_y + 16);
    lv_obj_set_style_bg_color(list_can, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(list_can, lv_color_hex(0x2c3e50), 0);
    lv_obj_set_style_radius(list_can, 4, 0);
    dev_y += 100;

    ta_can_id = lv_textarea_create(page);
    lv_obj_set_size(ta_can_id, 100, 26);
    lv_obj_set_pos(ta_can_id, 520, dev_y);
    lv_textarea_set_one_line(ta_can_id, true);
    lv_textarea_set_placeholder_text(ta_can_id, "ID (hex)");
    lv_textarea_set_max_length(ta_can_id, 5);
    lv_obj_add_event_cb(ta_can_id, settings_ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btn_can_add = lv_btn_create(page);
    lv_obj_set_size(btn_can_add, 60, 26);
    lv_obj_set_pos(btn_can_add, 630, dev_y);
    lv_obj_set_style_bg_color(btn_can_add, lv_color_hex(0xe67e22), 0);
    lv_obj_add_event_cb(btn_can_add, can_add_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_c = lv_label_create(btn_can_add);
    lv_label_set_text(lbl_c, "Add");
    lv_obj_set_style_text_color(lbl_c, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl_c, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_c);
    dev_y += 32;

    lbl_dev_msg = lv_label_create(page);
    lv_label_set_text(lbl_dev_msg, "");
    lv_obj_set_style_text_color(lbl_dev_msg, lv_color_hex(0x44ff44), 0);
    lv_obj_set_style_text_font(lbl_dev_msg, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_dev_msg, 520, dev_y);

    // === 区域 3：用户管理（阈值下方） ===
    int user_y = 30 + SLIDER_COUNT * 24 + 10;

    lv_obj_t *lbl_section3 = lv_label_create(page);
    lv_label_set_text(lbl_section3, "User Management");
    lv_obj_set_style_text_color(lbl_section3, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_font(lbl_section3, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_section3, 10, user_y);
    user_y += 22;

    lv_obj_t *lbl_pwd = lv_label_create(page);
    lv_label_set_text(lbl_pwd, "New Op Pwd:");
    lv_obj_set_style_text_color(lbl_pwd, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(lbl_pwd, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_pwd, 10, user_y);

    ta_new_pwd = lv_textarea_create(page);
    lv_obj_set_size(ta_new_pwd, 160, 26);
    lv_obj_set_pos(ta_new_pwd, 120, user_y - 3);
    lv_textarea_set_one_line(ta_new_pwd, true);
    lv_textarea_set_max_length(ta_new_pwd, 31);
    lv_obj_add_event_cb(ta_new_pwd, settings_ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btn_pwd = lv_btn_create(page);
    lv_obj_set_size(btn_pwd, 70, 26);
    lv_obj_set_pos(btn_pwd, 290, user_y - 3);
    lv_obj_set_style_bg_color(btn_pwd, lv_color_hex(0x0f3460), 0);
    lv_obj_add_event_cb(btn_pwd, pwd_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_btn_pwd = lv_label_create(btn_pwd);
    lv_label_set_text(lbl_btn_pwd, "Change");
    lv_obj_set_style_text_color(lbl_btn_pwd, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl_btn_pwd, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_btn_pwd);

    lbl_pwd_msg = lv_label_create(page);
    lv_label_set_text(lbl_pwd_msg, "");
    lv_obj_set_style_text_color(lbl_pwd_msg, lv_color_hex(0x44ff44), 0);
    lv_obj_set_style_text_font(lbl_pwd_msg, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_pwd_msg, 370, user_y);

    btn_logout = lv_btn_create(page);
    lv_obj_set_size(btn_logout, 120, 30);
    lv_obj_set_pos(btn_logout, 10, user_y + 30);
    lv_obj_set_style_bg_color(btn_logout, lv_color_hex(0xc0392b), 0);
    lv_obj_add_event_cb(btn_logout, logout_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_logout = lv_label_create(btn_logout);
    lv_label_set_text(lbl_logout, LV_SYMBOL_POWER " Logout");
    lv_obj_set_style_text_color(lbl_logout, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl_logout, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_logout);

    return page;
}

void ui_settings_refresh(void)
{
    if (sliders[0] == NULL) return;

    const AlarmThreshold_t *thr = AlarmMgrGetThreshold();

    double vals[SLIDER_COUNT] = {
        thr->tem_max, thr->tem_min, thr->hum_max, thr->hum_min,
        thr->gas_max, thr->gas_min, thr->acc_max, thr->acc_min,
        thr->rs485_volt_max, thr->rs485_volt_min,
        thr->rs485_elec_max, thr->rs485_elec_min,
        thr->can_speed_max, thr->can_speed_min,
        thr->can_flow_max, thr->can_flow_min
    };

    char buf[16];
    for (int i = 0; i < SLIDER_COUNT; i++) {
        int v = (int)vals[i];
        if (v > slider_ranges[i]) v = slider_ranges[i];
        lv_slider_set_value(sliders[i], v, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%.0f", vals[i]);
        lv_label_set_text(lbl_vals[i], buf);
    }

    refresh_rs485_list();
    refresh_can_list();
}

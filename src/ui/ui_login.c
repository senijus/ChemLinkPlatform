#include "ui_login.h"
#include "ui_common.h"
#include "auth.h"

static lv_obj_t *ta_password = NULL;
static lv_obj_t *kb = NULL;
static lv_obj_t *lbl_msg = NULL;

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY)
    {
        const char *pwd = lv_textarea_get_text(ta_password);
        if (AuthLogin(pwd) == 0)
        {
            lv_label_set_text(lbl_msg, "Login OK");
            lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0x44ff44), 0);

            ui_status_bar_set_role(AuthGetRoleName());
            ui_nav_bar_update_visibility(AuthGetRole());

            lv_obj_add_flag(g_nav_bar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_nav_bar, LV_OBJ_FLAG_HIDDEN);

            ui_switch_page(PAGE_DASHBOARD);
            ui_login_clear_input();
        }
        else
        {
            lv_label_set_text(lbl_msg, "Wrong password!");
            lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0xff4444), 0);
        }
    }
    else if (code == LV_EVENT_CANCEL)
    {
        lv_textarea_set_text(ta_password, "");
    }
}

static void ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED)
    {
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_DEFOCUSED)
    {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *ui_login_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, SCREEN_WIDTH, CONTENT_H);
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, STATUS_BAR_H);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x0a0a23), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = lv_label_create(page);
    lv_label_set_text(lbl_title, "Industrial Monitor System");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *lbl_sub = lv_label_create(page);
    lv_label_set_text(lbl_sub, "Please enter password to login");
    lv_obj_set_style_text_color(lbl_sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_sub, LV_ALIGN_TOP_MID, 0, 80);

    ta_password = lv_textarea_create(page);
    lv_obj_set_size(ta_password, 300, 40);
    lv_obj_align(ta_password, LV_ALIGN_TOP_MID, 0, 130);
    lv_textarea_set_password_mode(ta_password, true);
    lv_textarea_set_placeholder_text(ta_password, "Password");
    lv_textarea_set_one_line(ta_password, true);
    lv_textarea_set_max_length(ta_password, 31);
    lv_obj_add_event_cb(ta_password, ta_event_cb, LV_EVENT_ALL, NULL);

    lbl_msg = lv_label_create(page);
    lv_label_set_text(lbl_msg, "Operator: 1234 / Admin: admin");
    lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(lbl_msg, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_msg, LV_ALIGN_TOP_MID, 0, 180);

    kb = lv_keyboard_create(page);
    lv_obj_set_size(kb, SCREEN_WIDTH - 40, 180);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(kb, ta_password);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);

    return page;
}

void ui_login_clear_input(void)
{
    if (ta_password)
        lv_textarea_set_text(ta_password, "");
    if (lbl_msg)
        lv_label_set_text(lbl_msg, "Operator: 1234 / Admin: admin");
}

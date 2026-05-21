#include "ui_common.h"
#include "ui_login.h"
#include "ui_dashboard.h"
#include "ui_trend.h"
#include "ui_alarm.h"
#include "ui_settings.h"

lv_obj_t *g_pages[PAGE_COUNT] = {NULL};
lv_obj_t *g_status_bar = NULL;
lv_obj_t *g_nav_bar = NULL;
lv_obj_t *g_lbl_time = NULL;
lv_obj_t *g_lbl_role = NULL;
lv_obj_t *g_lbl_alarm = NULL;
lv_obj_t *g_nav_btns[4] = {NULL};
lv_obj_t *g_btn_switch_user = NULL;
int g_current_page = PAGE_LOGIN;

static const char *nav_labels[] = {"Monitor", "Trend", "Alarm", "Settings"};

// 密码提权对话框相关
static lv_obj_t *g_pwd_msgbox = NULL;
static lv_obj_t *g_pwd_textarea = NULL;
static lv_obj_t *g_pwd_keyboard = NULL;

static void pwd_keyboard_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_READY) {
        // 关闭对话框
        if (code == LV_EVENT_CANCEL) {
            if (g_pwd_msgbox) {
                lv_msgbox_close(g_pwd_msgbox);
                g_pwd_msgbox = NULL;
                g_pwd_keyboard = NULL;
                g_pwd_textarea = NULL;
            }
        }
        return;
    }

    // 用户按了确认
    if (g_pwd_textarea == NULL) return;
    const char *pwd = lv_textarea_get_text(g_pwd_textarea);

    if (AuthLogin(pwd) == 0 && AuthIsAdmin()) {
        // 提权成功
        ui_status_bar_set_role(AuthGetRoleName());
        ui_nav_bar_update_visibility(ROLE_ADMIN);
        lv_obj_clear_flag(g_nav_btns[3], LV_OBJ_FLAG_HIDDEN);
        ui_switch_page(PAGE_SETTINGS);
    } else {
        // 密码错误，清空输入框
        lv_textarea_set_text(g_pwd_textarea, "");
    }

    if (g_pwd_msgbox) {
        lv_msgbox_close(g_pwd_msgbox);
        g_pwd_msgbox = NULL;
        g_pwd_keyboard = NULL;
        g_pwd_textarea = NULL;
    }
}

static void show_admin_pwd_dialog(void)
{
    if (g_pwd_msgbox) return;  // 已有对话框

    static const char *btns[] = {"OK", "Cancel", ""};
    g_pwd_msgbox = lv_msgbox_create(NULL, "Admin Password",
                                     "Enter admin password to switch:", btns, true);
    lv_obj_set_size(g_pwd_msgbox, 300, 200);
    lv_obj_center(g_pwd_msgbox);

    // 密码输入框
    g_pwd_textarea = lv_textarea_create(g_pwd_msgbox);
    lv_textarea_set_one_line(g_pwd_textarea, true);
    lv_textarea_set_password_mode(g_pwd_textarea, true);
    lv_textarea_set_placeholder_text(g_pwd_textarea, "Password");
    lv_obj_set_width(g_pwd_textarea, 250);
    lv_obj_align(g_pwd_textarea, LV_ALIGN_TOP_MID, 0, 10);

    // 键盘
    g_pwd_keyboard = lv_keyboard_create(g_pwd_msgbox);
    lv_keyboard_set_textarea(g_pwd_keyboard, g_pwd_textarea);
    lv_obj_set_size(g_pwd_keyboard, 280, 120);
    lv_obj_align(g_pwd_keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(g_pwd_keyboard, pwd_keyboard_cb, LV_EVENT_ALL, NULL);
}

static void switch_user_btn_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED)
        return;

    if (AuthGetRole() == ROLE_OPERATOR) {
        // 操作员：弹出密码对话框提权
        show_admin_pwd_dialog();
    } else {
        // 管理员：登出
        AuthLogout();
        ui_status_bar_set_role("Guest");
        ui_nav_bar_update_visibility(ROLE_NONE);
        lv_obj_add_flag(g_nav_bar, LV_OBJ_FLAG_HIDDEN);
        ui_switch_page(PAGE_LOGIN);
        ui_login_clear_input();
    }
}

static void nav_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED)
        return;

    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    PageId_t target = PAGE_DASHBOARD + idx;

    if (target == PAGE_SETTINGS && !AuthIsAdmin())
        return;

    ui_switch_page(target);
}

lv_obj_t *ui_create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCREEN_WIDTH, STATUS_BAR_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    g_lbl_time = lv_label_create(bar);
    lv_label_set_text(g_lbl_time, "00:00:00");
    lv_obj_set_style_text_color(g_lbl_time, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(g_lbl_time, &lv_font_montserrat_14, 0);
    lv_obj_align(g_lbl_time, LV_ALIGN_LEFT_MID, 8, 0);

    g_lbl_role = lv_label_create(bar);
    lv_label_set_text(g_lbl_role, "Guest");
    lv_obj_set_style_text_color(g_lbl_role, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(g_lbl_role, &lv_font_montserrat_14, 0);
    lv_obj_align(g_lbl_role, LV_ALIGN_CENTER, -40, 0);

    // 切换用户按钮（所有已登录角色可见）
    g_btn_switch_user = lv_btn_create(bar);
    lv_obj_set_size(g_btn_switch_user, 80, 24);
    lv_obj_align(g_btn_switch_user, LV_ALIGN_CENTER, 40, 0);
    lv_obj_set_style_bg_color(g_btn_switch_user, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_radius(g_btn_switch_user, 4, 0);
    lv_obj_add_event_cb(g_btn_switch_user, switch_user_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_switch = lv_label_create(g_btn_switch_user);
    lv_label_set_text(lbl_switch, "Switch");
    lv_obj_set_style_text_color(lbl_switch, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl_switch, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_switch);

    // 初始隐藏（未登录时不显示）
    lv_obj_add_flag(g_btn_switch_user, LV_OBJ_FLAG_HIDDEN);

    g_lbl_alarm = lv_label_create(bar);
    lv_label_set_text(g_lbl_alarm, "");
    lv_obj_set_style_text_color(g_lbl_alarm, lv_color_hex(0xff4444), 0);
    lv_obj_set_style_text_font(g_lbl_alarm, &lv_font_montserrat_14, 0);
    lv_obj_align(g_lbl_alarm, LV_ALIGN_RIGHT_MID, -8, 0);

    return bar;
}

void ui_status_bar_set_time(const char *time_str)
{
    if (g_lbl_time)
        lv_label_set_text(g_lbl_time, time_str);
}

void ui_status_bar_set_role(const char *role_name)
{
    if (g_lbl_role)
        lv_label_set_text(g_lbl_role, role_name);

    // 登录后显示切换按钮，Guest 时隐藏
    if (g_btn_switch_user) {
        if (strcmp(role_name, "Guest") == 0) {
            lv_obj_add_flag(g_btn_switch_user, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(g_btn_switch_user, LV_OBJ_FLAG_HIDDEN);
            // 更新按钮文字：操作员显示"切换"，管理员显示"登出"
            lv_obj_t *lbl = lv_obj_get_child(g_btn_switch_user, 0);
            if (lbl) {
                if (strcmp(role_name, "Operator") == 0)
                    lv_label_set_text(lbl, "Switch");
                else
                    lv_label_set_text(lbl, "Logout");
            }
        }
    }
}

void ui_status_bar_set_alarm(int has_alarm)
{
    if (g_lbl_alarm)
        lv_label_set_text(g_lbl_alarm, has_alarm ? "ALARM" : "");
}

lv_obj_t *ui_create_nav_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCREEN_WIDTH, NAV_BAR_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 2, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *btn = lv_btn_create(bar);
        lv_obj_set_size(btn, 160, 36);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0f3460), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x533483), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_add_event_cb(btn, nav_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, nav_labels[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);

        g_nav_btns[i] = btn;
    }

    return bar;
}

void ui_nav_bar_set_active(int index)
{
    for (int i = 0; i < 4; i++)
    {
        if (i == index)
            lv_obj_set_style_bg_color(g_nav_btns[i], lv_color_hex(0xe94560), 0);
        else
            lv_obj_set_style_bg_color(g_nav_btns[i], lv_color_hex(0x0f3460), 0);
    }
}

void ui_nav_bar_update_visibility(Role_t role)
{
    if (g_nav_btns[3])
    {
        if (role == ROLE_ADMIN)
            lv_obj_clear_flag(g_nav_btns[3], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(g_nav_btns[3], LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_switch_page(PageId_t page_id)
{
    if (page_id < 0 || page_id >= PAGE_COUNT)
        return;

    // 关闭可能存在的模态对话框，防止界面残留
    ui_trend_close_picker();
    if (g_pwd_msgbox) {
        lv_msgbox_close(g_pwd_msgbox);
        g_pwd_msgbox = NULL;
        g_pwd_keyboard = NULL;
        g_pwd_textarea = NULL;
    }

    for (int i = 0; i < PAGE_COUNT; i++)
    {
        if (g_pages[i])
        {
            if (i == page_id)
                lv_obj_clear_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    g_current_page = page_id;

    if (page_id >= PAGE_DASHBOARD && page_id <= PAGE_SETTINGS)
        ui_nav_bar_set_active(page_id - PAGE_DASHBOARD);

    // 切换到 alarm/setting 页面时刷新
    if (page_id == PAGE_ALARM)
        ui_alarm_refresh();
    if (page_id == PAGE_SETTINGS)
        ui_settings_refresh();
}

void ui_common_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a23), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* 确保 screen 不会滚动，防止 scroll chain 导致导航栏移位 */
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

    /* 所有页面放在 screen 层 */
    g_pages[PAGE_LOGIN] = ui_login_create(scr);
    g_pages[PAGE_DASHBOARD] = ui_dashboard_create(scr);
    g_pages[PAGE_TREND] = ui_trend_create(scr);
    g_pages[PAGE_ALARM] = ui_alarm_create(scr);
    g_pages[PAGE_SETTINGS] = ui_settings_create(scr);

    /* 状态栏和导航栏放在系统 top 层 —— 永远在最上面，不受页面滚动影响 */
    lv_obj_t *top_layer = lv_layer_top();
    g_status_bar = ui_create_status_bar(top_layer);
    g_nav_bar = ui_create_nav_bar(top_layer);

    ui_switch_page(PAGE_LOGIN);
    lv_obj_add_flag(g_nav_bar, LV_OBJ_FLAG_HIDDEN);
}

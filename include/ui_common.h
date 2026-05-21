#ifndef __UI_COMMON_H__
#define __UI_COMMON_H__

#include "lvgl/lvgl.h"
#include "auth.h"

#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 480

#define STATUS_BAR_H  36
#define NAV_BAR_H     44
#define CONTENT_Y     STATUS_BAR_H
#define CONTENT_H     (SCREEN_HEIGHT - STATUS_BAR_H - NAV_BAR_H - 6)

typedef enum {
    PAGE_LOGIN = 0,
    PAGE_DASHBOARD,
    PAGE_TREND,
    PAGE_ALARM,
    PAGE_SETTINGS,
    PAGE_COUNT
} PageId_t;

/* Status bar */
lv_obj_t *ui_create_status_bar(lv_obj_t *parent);
void ui_status_bar_set_time(const char *time_str);
void ui_status_bar_set_role(const char *role_name);
void ui_status_bar_set_alarm(int has_alarm);

/* Navigation bar */
lv_obj_t *ui_create_nav_bar(lv_obj_t *parent);
void ui_nav_bar_set_active(int index);
void ui_nav_bar_update_visibility(Role_t role);

/* Page switching */
void ui_switch_page(PageId_t page_id);

/* Init all pages */
void ui_common_init(void);

/* Global objects */
extern lv_obj_t *g_pages[PAGE_COUNT];
extern lv_obj_t *g_status_bar;
extern lv_obj_t *g_nav_bar;
extern lv_obj_t *g_lbl_time;
extern lv_obj_t *g_lbl_role;
extern lv_obj_t *g_lbl_alarm;
extern lv_obj_t *g_nav_btns[4];
extern int g_current_page;

#endif

#ifndef __UI_DASHBOARD_H__
#define __UI_DASHBOARD_H__

#include "lvgl/lvgl.h"
#include "mailbox.h"

lv_obj_t *ui_dashboard_create(lv_obj_t *parent);
void ui_dashboard_update(const Data_t *data);

#endif

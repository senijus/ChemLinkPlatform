#ifndef __UI_TREND_H__
#define __UI_TREND_H__

#include "lvgl/lvgl.h"
#include "mailbox.h"

lv_obj_t *ui_trend_create(lv_obj_t *parent);
void ui_trend_update(const Data_t *data);
void ui_trend_close_picker(void);

#endif

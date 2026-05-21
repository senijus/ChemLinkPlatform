#ifndef __LVGL_UI_H__
#define __LVGL_UI_H__

#include <stdint.h>
#include "mailbox.h"

int LvglUiInit(void);
void LvglUiDeinit(void);
void LvglUiUpdateData(const Data_t *data);
void *LvglUiThread(void *arg);
uint32_t custom_tick_get(void);

#endif

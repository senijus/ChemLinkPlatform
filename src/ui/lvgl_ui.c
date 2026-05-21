#define _GNU_SOURCE
#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include "lvgl_ui.h"
#include "ui_common.h"
#include "ui_dashboard.h"
#include "ui_trend.h"
#include "ui_alarm.h"
#include "ui_settings.h"
#include "alarm_mgr.h"
#include "auth.h"
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

#define DISP_BUF_SIZE (800 * 100)  /* 100 rows = ~20% of screen, reduces tearing */

static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
static Data_t latest_data = {0};
static int has_new_data = 0;
static volatile int lvgl_running = 0;

static void update_timer_cb(lv_timer_t *timer)
{
    Data_t data;
    int need_update = 0;

    pthread_mutex_lock(&data_mutex);
    if (has_new_data)
    {
        data = latest_data;
        has_new_data = 0;
        need_update = 1;
    }
    pthread_mutex_unlock(&data_mutex);

    if (!need_update)
        return;

    if (AuthGetRole() != ROLE_NONE)
    {
        ui_dashboard_update(&data);
        ui_trend_update(&data);
    }

    AlarmMgrCheck(&data);
    int alarm_count = AlarmMgrGetUnreadCount();
    ui_status_bar_set_alarm(alarm_count > 0);

    if (g_current_page == PAGE_ALARM)
        ui_alarm_refresh();
}

static void clock_timer_cb(lv_timer_t *timer)
{
    time_t cur;
    struct tm *ptm = NULL;
    char buf[24];

    time(&cur);
    ptm = localtime(&cur);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    ui_status_bar_set_time(buf);
}

int LvglUiInit(void)
{
    lv_init();

    fbdev_init();

    static lv_color_t buf1[DISP_BUF_SIZE];
    static lv_color_t buf2[DISP_BUF_SIZE];
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    lv_disp_drv_register(&disp_drv);

    evdev_init();
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);

    AuthInit();
    AlarmMgrInit();

    ui_common_init();

    lv_timer_create(update_timer_cb, 200, NULL);
    lv_timer_create(clock_timer_cb, 1000, NULL);

    lvgl_running = 1;

    return 0;
}

void LvglUiDeinit(void)
{
    lvgl_running = 0;
}

void LvglUiUpdateData(const Data_t *data)
{
    pthread_mutex_lock(&data_mutex);
    latest_data = *data;
    has_new_data = 1;
    pthread_mutex_unlock(&data_mutex);
}

void *LvglUiThread(void *arg)
{
    (void)arg;

    while (lvgl_running)
    {
        lv_timer_handler();
        usleep(5000);
    }

    return NULL;
}

uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if (start_ms == 0)
    {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    return (uint32_t)(now_ms - start_ms);
}

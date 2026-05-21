#include "ui_trend.h"
#include "ui_common.h"
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define MAX_CHART_POINTS 200
#define MAX_QUERY_RECORDS 800

// ======================== 外部图表数组 ========================
static lv_coord_t g_ext_tem[MAX_CHART_POINTS];
static lv_coord_t g_ext_hum[MAX_CHART_POINTS];
static lv_coord_t g_ext_gas[MAX_CHART_POINTS];
static lv_coord_t g_ext_acc[MAX_CHART_POINTS];

static lv_obj_t *chart = NULL;
static lv_obj_t *btn_start = NULL;
static lv_obj_t *btn_end = NULL;
static lv_obj_t *btn_query = NULL;
static lv_obj_t *lbl_info = NULL;
static lv_obj_t *lbl_x0 = NULL;
static lv_obj_t *lbl_x1 = NULL;
static lv_obj_t *lbl_x2 = NULL;

static lv_chart_series_t *ser_tem = NULL;
static lv_chart_series_t *ser_hum = NULL;
static lv_chart_series_t *ser_gas = NULL;
static lv_chart_series_t *ser_acc = NULL;
static lv_obj_t *dd_series = NULL;

// ======================== 时间滚轮选择器 ========================
#define ROLLER_W 58
#define ROLLER_H 110
#define ROLLER_GAP 4
#define ROLLER_COUNT 6

static lv_obj_t *g_picker_dlg = NULL;
static lv_obj_t *g_rollers[ROLLER_COUNT];
static int g_picker_target = 0; // 0=start, 1=end

// 当前选中的起止时间字符串
static char g_start_str[24] = {0};
static char g_end_str[24] = {0};

// 查询结果
typedef struct {
    char time_str[24];
    double tem, hum, gas, acc;
} ChartPoint_t;

static ChartPoint_t g_points[MAX_CHART_POINTS];
static int g_point_count = 0;
static pthread_mutex_t g_chart_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_chart_data_ready = 0;
static volatile int g_chart_loading = 0;

static char g_query_start[24] = {0};
static char g_query_end[24] = {0};

// ======================== 滚轮选项字符串构造 ========================
static void roller_set_options_int(lv_obj_t *roller, int min, int max)
{
    char buf[256] = {0};
    int off = 0;
    for (int v = min; v <= max; v++) {
        off += snprintf(buf + off, sizeof(buf) - off, "%d\n", v);
    }
    // 去掉最后的 \n（如果有内容）
    if (off > 0) buf[off - 1] = '\0';
    lv_roller_set_options(roller, buf, LV_ROLLER_MODE_NORMAL);
}

// ======================== 时间选择器对话框 ========================
static void picker_ok_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    // 读取所有滚轮当前值
    char time_str[24];
    snprintf(time_str, sizeof(time_str), "%04d-%02d-%02d %02d:%02d:%02d",
             (int)lv_roller_get_selected(g_rollers[0]) + 2020,
             (int)lv_roller_get_selected(g_rollers[1]) + 1,
             (int)lv_roller_get_selected(g_rollers[2]) + 1,
             (int)lv_roller_get_selected(g_rollers[3]),
             (int)lv_roller_get_selected(g_rollers[4]),
             (int)lv_roller_get_selected(g_rollers[5]));

    if (g_picker_target == 0) {
        strncpy(g_start_str, time_str, sizeof(g_start_str) - 1);
        lv_label_set_text(lv_obj_get_child(btn_start, 0), g_start_str);
    } else {
        strncpy(g_end_str, time_str, sizeof(g_end_str) - 1);
        lv_label_set_text(lv_obj_get_child(btn_end, 0), g_end_str);
    }

    lv_obj_del(g_picker_dlg);
    g_picker_dlg = NULL;
}

static void picker_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_del(g_picker_dlg);
    g_picker_dlg = NULL;
}

static void show_time_picker(int target)
{
    if (g_picker_dlg) return;

    g_picker_target = target;

    // 解析当前时间字符串获取初始滚轮位置
    const char *cur = (target == 0) ? g_start_str : g_end_str;
    int y = 2020, mo = 1, d = 1, h = 0, mi = 0, s = 0;
    sscanf(cur, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s);

    // 模态对话框
    g_picker_dlg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_picker_dlg, 420, 190);
    lv_obj_center(g_picker_dlg);
    lv_obj_set_style_bg_color(g_picker_dlg, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_color(g_picker_dlg, lv_color_hex(0x533483), 0);
    lv_obj_set_style_border_width(g_picker_dlg, 2, 0);
    lv_obj_set_style_radius(g_picker_dlg, 10, 0);
    lv_obj_set_style_pad_all(g_picker_dlg, 8, 0);

    // 标题
    lv_obj_t *title = lv_label_create(g_picker_dlg);
    lv_label_set_text(title, target == 0 ? "Select Start Time" : "Select End Time");
    lv_obj_set_style_text_color(title, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    // 滚轮行
    const char *labels[] = {"Y", "M", "D", "H", "M", "S"};
    int defaults[6] = { y - 2020, mo - 1, d - 1, h, mi, s };
    int mins[6]     = { 0, 0, 0, 0, 0, 0 };
    int maxs[6]     = { 10, 11, 30, 23, 59, 59 };

    int total_w = ROLLER_COUNT * ROLLER_W + (ROLLER_COUNT - 1) * ROLLER_GAP;
    int start_x = (420 - total_w) / 2;

    for (int i = 0; i < ROLLER_COUNT; i++) {
        // 标签
        lv_obj_t *lbl = lv_label_create(g_picker_dlg);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(lbl, start_x + i * (ROLLER_W + ROLLER_GAP) + ROLLER_W / 2 - 6, 42);

        // 滚轮
        g_rollers[i] = lv_roller_create(g_picker_dlg);
        lv_obj_set_size(g_rollers[i], ROLLER_W, ROLLER_H);
        lv_obj_set_pos(g_rollers[i], start_x + i * (ROLLER_W + ROLLER_GAP), 54);
        lv_obj_set_style_bg_color(g_rollers[i], lv_color_hex(0x16213e), 0);
        lv_obj_set_style_bg_color(g_rollers[i], lv_color_hex(0x533483), LV_PART_SELECTED);
        lv_obj_set_style_text_color(g_rollers[i], lv_color_hex(0xffffff), LV_PART_SELECTED);
        lv_obj_set_style_text_font(g_rollers[i], &lv_font_montserrat_14, 0);
        lv_roller_set_visible_row_count(g_rollers[i], 3);

        if (mins[i] == maxs[i])
            lv_roller_set_options(g_rollers[i], "0", LV_ROLLER_MODE_NORMAL);
        else
            roller_set_options_int(g_rollers[i], mins[i], maxs[i]);

        lv_roller_set_selected(g_rollers[i], defaults[i], LV_ANIM_OFF);
    }

    // 按钮行
    lv_obj_t *btn_ok = lv_btn_create(g_picker_dlg);
    lv_obj_set_size(btn_ok, 80, 26);
    lv_obj_set_pos(btn_ok, 120, 160);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x2ecc71), 0);
    lv_obj_add_event_cb(btn_ok, picker_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "OK");
    lv_obj_set_style_text_color(lbl_ok, lv_color_hex(0xffffff), 0);
    lv_obj_center(lbl_ok);

    lv_obj_t *btn_cancel = lv_btn_create(g_picker_dlg);
    lv_obj_set_size(btn_cancel, 80, 26);
    lv_obj_set_pos(btn_cancel, 220, 160);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0xc0392b), 0);
    lv_obj_add_event_cb(btn_cancel, picker_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_set_style_text_color(lbl_cancel, lv_color_hex(0xffffff), 0);
    lv_obj_center(lbl_cancel);
}

// ======================== 起止按钮点击 → 弹出时间选择器 ========================
static void btn_start_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    show_time_picker(0);
}

static void btn_end_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    show_time_picker(1);
}

// ======================== 降采样 ========================
static void downsample_avg(DataEntry_t *raw, int raw_count,
                           ChartPoint_t *out, int *out_count, int max_points)
{
    if (raw_count <= 0) { *out_count = 0; return; }
    if (raw_count <= max_points) {
        for (int i = 0; i < raw_count; i++) {
            strncpy(out[i].time_str, raw[i].timestamp, sizeof(out[i].time_str) - 1);
            out[i].time_str[sizeof(out[i].time_str) - 1] = '\0';
            out[i].tem = raw[i].data.Tem;
            out[i].hum = raw[i].data.Hum;
            out[i].gas = raw[i].data.Gas;
            out[i].acc = raw[i].data.Acc_xyz;
        }
        *out_count = raw_count;
        return;
    }
    double step = (double)raw_count / max_points;
    *out_count = 0;
    for (int i = 0; i < max_points; i++) {
        int start = (int)(i * step);
        int end = (int)((i + 1) * step);
        if (end > raw_count) end = raw_count;
        if (start >= raw_count) break;
        double sum_tem = 0, sum_hum = 0, sum_gas = 0, sum_acc = 0;
        int cnt = end - start;
        if (cnt <= 0) break;
        for (int j = start; j < end; j++) {
            sum_tem += raw[j].data.Tem;
            sum_hum += raw[j].data.Hum;
            sum_gas += raw[j].data.Gas;
            sum_acc += raw[j].data.Acc_xyz;
        }
        strncpy(out[*out_count].time_str, raw[start].timestamp, sizeof(out[0].time_str) - 1);
        out[*out_count].time_str[sizeof(out[0].time_str) - 1] = '\0';
        out[*out_count].tem = sum_tem / cnt;
        out[*out_count].hum = sum_hum / cnt;
        out[*out_count].gas = sum_gas / cnt;
        out[*out_count].acc = sum_acc / cnt;
        (*out_count)++;
    }
}

// ======================== 刷新图表 ========================
static void update_chart_from_points(ChartPoint_t *points, int count)
{
    if (chart == NULL) return;
    if (count <= 0) {
        lv_chart_set_point_count(chart, 0);
        lv_chart_refresh(chart);
        return;
    }
    double max_val = 0;
    for (int i = 0; i < count; i++) {
        if (points[i].tem > max_val) max_val = points[i].tem;
        if (points[i].hum > max_val) max_val = points[i].hum;
        if (points[i].gas > max_val) max_val = points[i].gas;
        if (points[i].acc > max_val) max_val = points[i].acc;
    }
    int y_max = (int)(max_val + 9) / 10 * 10 + 10;
    if (y_max < 100) y_max = 100;

    for (int i = 0; i < count; i++) {
        g_ext_tem[i] = (lv_coord_t)points[i].tem;
        g_ext_hum[i] = (lv_coord_t)points[i].hum;
        g_ext_gas[i] = (lv_coord_t)points[i].gas;
        g_ext_acc[i] = (lv_coord_t)points[i].acc;
    }
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, y_max);
    lv_chart_set_point_count(chart, count);
    lv_chart_refresh(chart);

    if (count >= 3) {
        lv_label_set_text(lbl_x0, points[0].time_str + 11);
        lv_label_set_text(lbl_x1, points[count / 2].time_str + 11);
        lv_label_set_text(lbl_x2, points[count - 1].time_str + 11);
    }
    uint16_t sel = lv_dropdown_get_selected(dd_series);
    if (ser_tem) ser_tem->hidden = !(sel == 0 || sel == 1);
    if (ser_hum) ser_hum->hidden = !(sel == 0 || sel == 2);
    if (ser_gas) ser_gas->hidden = !(sel == 0 || sel == 3);
    if (ser_acc) ser_acc->hidden = !(sel == 0 || sel == 4);
}

// ======================== 后台查询线程 ========================
static void *query_thread_func(void *arg)
{
    (void)arg;
    int max_records = MAX_QUERY_RECORDS;
    DataEntry_t *raw = malloc(sizeof(DataEntry_t) * max_records);
    if (raw == NULL) {
        g_chart_loading = 0;
        pthread_mutex_lock(&g_chart_mutex);
        g_point_count = -1;
        g_chart_data_ready = 1;
        pthread_mutex_unlock(&g_chart_mutex);
        return NULL;
    }
    int count = max_records;
    int ret = StorageQueryDataEntries(g_query_start, g_query_end, raw, &count);
    if (ret != 0 || count == 0) {
        free(raw);
        pthread_mutex_lock(&g_chart_mutex);
        g_point_count = 0;
        g_chart_data_ready = 1;
        pthread_mutex_unlock(&g_chart_mutex);
        g_chart_loading = 0;
        return NULL;
    }
    ChartPoint_t pts[MAX_CHART_POINTS];
    int pt_count = 0;
    downsample_avg(raw, count, pts, &pt_count, MAX_CHART_POINTS);
    free(raw);

    pthread_mutex_lock(&g_chart_mutex);
    memcpy(g_points, pts, sizeof(ChartPoint_t) * pt_count);
    g_point_count = pt_count;
    g_chart_data_ready = 1;
    pthread_mutex_unlock(&g_chart_mutex);
    g_chart_loading = 0;
    return NULL;
}

static void do_query(const char *start, const char *end)
{
    if (g_chart_loading) return;
    strncpy(g_query_start, start, sizeof(g_query_start) - 1);
    g_query_start[sizeof(g_query_start) - 1] = '\0';
    strncpy(g_query_end, end, sizeof(g_query_end) - 1);
    g_query_end[sizeof(g_query_end) - 1] = '\0';
    g_chart_loading = 1;
    if (lbl_info) lv_label_set_text(lbl_info, "Loading...");
    pthread_t tid;
    pthread_create(&tid, NULL, query_thread_func, NULL);
    pthread_detach(tid);
}

// ======================== Query 按钮 ========================
static void query_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (strlen(g_start_str) < 10 || strlen(g_end_str) < 10) {
        if (lbl_info) lv_label_set_text(lbl_info, "Please set start and end time");
        return;
    }
    do_query(g_start_str, g_end_str);
}

// ======================== 快捷按钮 (1h/6h/24h/7d) ========================
static void quick_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int hours = (int)(intptr_t)lv_event_get_user_data(e);
    time_t now = time(NULL);
    struct tm tm_end, tm_start;
    time_t t_now = time(NULL);
    time_t t_start = t_now - hours * 3600;
    // UI 线程单线程运行，localtime 安全
    struct tm *p = localtime(&t_now);  memcpy(&tm_end,   p, sizeof(tm_end));
    p = localtime(&t_start);           memcpy(&tm_start, p, sizeof(tm_start));

    snprintf(g_start_str, sizeof(g_start_str), "%04d-%02d-%02d %02d:%02d:%02d",
             tm_start.tm_year + 1900, tm_start.tm_mon + 1, tm_start.tm_mday,
             tm_start.tm_hour, tm_start.tm_min, tm_start.tm_sec);
    snprintf(g_end_str, sizeof(g_end_str), "%04d-%02d-%02d %02d:%02d:%02d",
             tm_end.tm_year + 1900, tm_end.tm_mon + 1, tm_end.tm_mday,
             tm_end.tm_hour, tm_end.tm_min, tm_end.tm_sec);

    lv_label_set_text(lv_obj_get_child(btn_start, 0), g_start_str);
    lv_label_set_text(lv_obj_get_child(btn_end, 0), g_end_str);
    do_query(g_start_str, g_end_str);
}

// ======================== 序列下拉切换 ========================
static void series_dd_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    pthread_mutex_lock(&g_chart_mutex);
    if (g_point_count > 0)
        update_chart_from_points(g_points, g_point_count);
    pthread_mutex_unlock(&g_chart_mutex);
}

// ======================== 创建页面 ========================
lv_obj_t *ui_trend_create(lv_obj_t *parent)
{
    time_t now = time(NULL);
    time_t t_start = now - 3600;  // 默认最近 1 小时
    struct tm tm_now, tm_start;
    struct tm *p = localtime(&now);     memcpy(&tm_now,   p, sizeof(tm_now));
    p = localtime(&t_start);            memcpy(&tm_start, p, sizeof(tm_start));

    snprintf(g_start_str, sizeof(g_start_str), "%04d-%02d-%02d %02d:%02d:%02d",
             tm_start.tm_year + 1900, tm_start.tm_mon + 1, tm_start.tm_mday,
             tm_start.tm_hour, tm_start.tm_min, tm_start.tm_sec);
    snprintf(g_end_str, sizeof(g_end_str), "%04d-%02d-%02d %02d:%02d:%02d",
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, SCREEN_WIDTH, CONTENT_H);
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, STATUS_BAR_H);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x0a0a23), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 4, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    // Row 0: Start btn | End btn | Query
    int y = 4;

    lv_obj_t *lbl_s = lv_label_create(page);
    lv_label_set_text(lbl_s, "Start:");
    lv_obj_set_style_text_color(lbl_s, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(lbl_s, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_s, 10, y + 6);

    btn_start = lv_btn_create(page);
    lv_obj_set_size(btn_start, 200, 30);
    lv_obj_set_pos(btn_start, 55, y);
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(btn_start, lv_color_hex(0x1abc9c), 0);
    lv_obj_set_style_border_width(btn_start, 1, 0);
    lv_obj_set_style_radius(btn_start, 4, 0);
    lv_obj_add_event_cb(btn_start, btn_start_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_bs = lv_label_create(btn_start);
    lv_label_set_text(lbl_bs, g_start_str);
    lv_obj_set_style_text_color(lbl_bs, lv_color_hex(0x1abc9c), 0);
    lv_obj_set_style_text_font(lbl_bs, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_bs);

    lv_obj_t *lbl_e = lv_label_create(page);
    lv_label_set_text(lbl_e, "End:");
    lv_obj_set_style_text_color(lbl_e, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(lbl_e, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_e, 265, y + 6);

    btn_end = lv_btn_create(page);
    lv_obj_set_size(btn_end, 200, 30);
    lv_obj_set_pos(btn_end, 300, y);
    lv_obj_set_style_bg_color(btn_end, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(btn_end, lv_color_hex(0xe67e22), 0);
    lv_obj_set_style_border_width(btn_end, 1, 0);
    lv_obj_set_style_radius(btn_end, 4, 0);
    lv_obj_add_event_cb(btn_end, btn_end_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_be = lv_label_create(btn_end);
    lv_label_set_text(lbl_be, g_end_str);
    lv_obj_set_style_text_color(lbl_be, lv_color_hex(0xe67e22), 0);
    lv_obj_set_style_text_font(lbl_be, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_be);

    btn_query = lv_btn_create(page);
    lv_obj_set_size(btn_query, 70, 30);
    lv_obj_set_pos(btn_query, 515, y);
    lv_obj_set_style_bg_color(btn_query, lv_color_hex(0x0f3460), 0);
    lv_obj_add_event_cb(btn_query, query_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_q = lv_label_create(btn_query);
    lv_label_set_text(lbl_q, "Query");
    lv_obj_set_style_text_color(lbl_q, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl_q, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_q);

    // Row 1: Quick buttons | Series dropdown | Info
    y += 36;
    const char *quick_labels[] = {"1h", "6h", "24h", "7d"};
    const int quick_hours[] = {1, 6, 24, 168};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(page);
        lv_obj_set_size(btn, 50, 24);
        lv_obj_set_pos(btn, 10 + i * 58, y);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x533483), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_add_event_cb(btn, quick_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)quick_hours[i]);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, quick_labels[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
    }

    dd_series = lv_dropdown_create(page);
    lv_dropdown_set_options(dd_series, "All Series\nTemperature\nHumidity\nGas\nAcceleration");
    lv_obj_set_size(dd_series, 130, 24);
    lv_obj_set_pos(dd_series, 250, y);
    lv_obj_add_event_cb(dd_series, series_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_info = lv_label_create(page);
    lv_label_set_text(lbl_info, "Tap Start/End to pick time");
    lv_obj_set_style_text_color(lbl_info, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_info, 395, y + 4);

    // Chart area
    y += 30;
    int chart_h = CONTENT_H - y - 24;
    chart = lv_chart_create(page);
    lv_obj_set_size(chart, SCREEN_WIDTH - 20, chart_h);
    lv_obj_set_pos(chart, 10, y);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 0);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(chart, 5, 10);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(chart, lv_color_hex(0x2c3e50), 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x2c3e50), LV_PART_MAIN);
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);

    ser_tem = lv_chart_add_series(chart, lv_color_hex(0xe94560), LV_CHART_AXIS_PRIMARY_Y);
    ser_hum = lv_chart_add_series(chart, lv_color_hex(0x3498db), LV_CHART_AXIS_PRIMARY_Y);
    ser_gas = lv_chart_add_series(chart, lv_color_hex(0x2ecc71), LV_CHART_AXIS_PRIMARY_Y);
    ser_acc = lv_chart_add_series(chart, lv_color_hex(0xf39c12), LV_CHART_AXIS_PRIMARY_Y);

    lv_chart_set_ext_y_array(chart, ser_tem, g_ext_tem);
    lv_chart_set_ext_y_array(chart, ser_hum, g_ext_hum);
    lv_chart_set_ext_y_array(chart, ser_gas, g_ext_gas);
    lv_chart_set_ext_y_array(chart, ser_acc, g_ext_acc);

    int lbl_y = y + chart_h + 2;
    lbl_x0 = lv_label_create(page);
    lv_label_set_text(lbl_x0, "--:--:--");
    lv_obj_set_style_text_color(lbl_x0, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl_x0, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_x0, 15, lbl_y);

    lbl_x1 = lv_label_create(page);
    lv_label_set_text(lbl_x1, "--:--:--");
    lv_obj_set_style_text_color(lbl_x1, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl_x1, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_x1, SCREEN_WIDTH / 2 - 20, lbl_y);

    lbl_x2 = lv_label_create(page);
    lv_label_set_text(lbl_x2, "--:--:--");
    lv_obj_set_style_text_color(lbl_x2, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl_x2, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_x2, SCREEN_WIDTH - 70, lbl_y);

    return page;
}

void ui_trend_close_picker(void)
{
    if (g_picker_dlg) {
        lv_obj_del(g_picker_dlg);
        g_picker_dlg = NULL;
    }
}

void ui_trend_update(const Data_t *data)
{
    (void)data;
    if (g_current_page != PAGE_TREND) return;
    if (!g_chart_data_ready) return;

    pthread_mutex_lock(&g_chart_mutex);
    g_chart_data_ready = 0;
    int count = g_point_count;

    if (count < 0) {
        pthread_mutex_unlock(&g_chart_mutex);
        if (lbl_info) lv_label_set_text(lbl_info, "Memory error, please retry");
        return;
    }
    if (count == 0) {
        pthread_mutex_unlock(&g_chart_mutex);
        if (lbl_info) lv_label_set_text(lbl_info, "No data found");
        return;
    }
    update_chart_from_points(g_points, count);
    char buf[64];
    snprintf(buf, sizeof(buf), "Query: %d points", count);
    if (lbl_info) lv_label_set_text(lbl_info, buf);
    pthread_mutex_unlock(&g_chart_mutex);
}

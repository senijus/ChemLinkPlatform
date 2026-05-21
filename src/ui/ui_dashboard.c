#include "ui_dashboard.h"
#include "ui_common.h"
#include <stdio.h>
#include <stdlib.h>

#define FIXED_CARD_COUNT 4
#define CARD_W  220
#define CARD_H  140
#define ARC_SIZE 80
#define CARD_GAP_X 10
#define CARD_GAP_Y 8
#define COLS 2
#define MARGIN_X ((SCREEN_WIDTH - COLS * CARD_W - (COLS - 1) * CARD_GAP_X) / 2)

typedef struct {
    lv_obj_t *card;
    lv_obj_t *arc;
    lv_obj_t *lbl_value;
    lv_obj_t *lbl_title;
} DashCard_t;

static DashCard_t fixed_cards[FIXED_CARD_COUNT];

// 动态设备卡片
static DashCard_t *rs485_cards = NULL;
static int rs485_card_count = 0;
static DashCard_t *can_cards = NULL;
static int can_card_count = 0;

lv_obj_t *page_outer = NULL;
static lv_obj_t *lbl_rs485_title = NULL;
static lv_obj_t *lbl_can_title = NULL;

static const char *fixed_titles[] = {
    "Temperature", "Humidity", "Gas", "Acceleration"
};

static const lv_color_t card_colors[] = {
    LV_COLOR_MAKE(0xe9, 0x45, 0x60),
    LV_COLOR_MAKE(0x34, 0x98, 0xdb),
    LV_COLOR_MAKE(0x2e, 0xcc, 0x71),
    LV_COLOR_MAKE(0xf3, 0x9c, 0x12),
};

static void create_card(DashCard_t *c, lv_obj_t *parent, int x, int y,
                        const char *title, lv_color_t color)
{
    c->card = lv_obj_create(parent);
    lv_obj_set_size(c->card, CARD_W, CARD_H);
    lv_obj_set_pos(c->card, x, y);
    lv_obj_set_style_bg_color(c->card, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_opa(c->card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c->card, color, 0);
    lv_obj_set_style_border_width(c->card, 2, 0);
    lv_obj_set_style_radius(c->card, 10, 0);
    lv_obj_set_style_pad_all(c->card, 6, 0);
    lv_obj_clear_flag(c->card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(c->card, LV_OBJ_FLAG_CLICKABLE);

    c->arc = lv_arc_create(c->card);
    lv_obj_set_size(c->arc, ARC_SIZE, ARC_SIZE);
    lv_obj_align(c->arc, LV_ALIGN_TOP_MID, 0, 2);
    lv_arc_set_rotation(c->arc, 135);
    lv_arc_set_bg_angles(c->arc, 0, 270);
    lv_arc_set_range(c->arc, 0, 100);
    lv_arc_set_value(c->arc, 0);
    lv_obj_set_style_arc_color(c->arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(c->arc, lv_color_hex(0x2c3e50), LV_PART_MAIN);
    lv_obj_set_style_arc_width(c->arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(c->arc, 6, LV_PART_MAIN);
    lv_obj_clear_flag(c->arc, LV_OBJ_FLAG_CLICKABLE);

    c->lbl_value = lv_label_create(c->card);
    lv_label_set_text(c->lbl_value, "--");
    lv_obj_set_style_text_color(c->lbl_value, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(c->lbl_value, &lv_font_montserrat_20, 0);
    lv_obj_align(c->lbl_value, LV_ALIGN_CENTER, 0, -5);
    lv_obj_clear_flag(c->lbl_value, LV_OBJ_FLAG_SCROLLABLE);

    c->lbl_title = lv_label_create(c->card);
    lv_label_set_text(c->lbl_title, title);
    lv_obj_set_style_text_color(c->lbl_title, color, 0);
    lv_obj_set_style_text_font(c->lbl_title, &lv_font_montserrat_12, 0);
    lv_obj_align(c->lbl_title, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_clear_flag(c->lbl_title, LV_OBJ_FLAG_SCROLLABLE);
}

static void update_card(DashCard_t *c, double value, int max_val, const char *unit)
{
    int arc_val = (int)value;
    if (arc_val > max_val) arc_val = max_val;
    if (arc_val < 0) arc_val = 0;
    lv_arc_set_value(c->arc, arc_val);

    char buf[32];
    if (value < 100)
        snprintf(buf, sizeof(buf), "%.1f %s", value, unit);
    else
        snprintf(buf, sizeof(buf), "%.0f %s", value, unit);
    lv_label_set_text(c->lbl_value, buf);
}

static void clear_device_cards(DashCard_t **cards, int *count)
{
    if (*cards == NULL) return;
    for (int i = 0; i < *count; i++) {
        if ((*cards)[i].card) lv_obj_del((*cards)[i].card);
    }
    free(*cards);
    *cards = NULL;
    *count = 0;
}

static int ensure_device_cards(DashCard_t **cards, int *cur_count, int need_count,
                                lv_obj_t *parent, int start_y,
                                const char *prefix, lv_color_t cols[])
{
    if (need_count == *cur_count) return 0;

    int card_x[2] = { MARGIN_X, MARGIN_X + CARD_W + CARD_GAP_X };

    if (need_count > *cur_count) {
        int old_count = *cur_count;
        DashCard_t *new_cards = realloc(*cards, sizeof(DashCard_t) * need_count);
        if (new_cards == NULL) {
            clear_device_cards(cards, cur_count);
            return -1;
        }
        *cards = new_cards;
        *cur_count = need_count;

        for (int d = old_count; d < need_count; d++) {
            int row = d / 2;
            int col = d % 2;
            int x = card_x[col];
            int y = start_y + row * (CARD_H + CARD_GAP_Y);

            char title[32];
            snprintf(title, sizeof(title), "%s #%d", prefix, d / 2 + 1);
            create_card(&(*cards)[d], parent, x, y, title, cols[d % 2]);
        }
    } else {
        for (int d = need_count; d < *cur_count; d++) {
            if ((*cards)[d].card) lv_obj_del((*cards)[d].card);
        }
        if (need_count == 0) {
            free(*cards);
            *cards = NULL;
            *cur_count = 0;
        } else {
            DashCard_t *shrunk = realloc(*cards, sizeof(DashCard_t) * need_count);
            if (shrunk != NULL) *cards = shrunk;
            *cur_count = need_count;
        }
    }
    return 0;
}

lv_obj_t *ui_dashboard_create(lv_obj_t *parent)
{
    page_outer = lv_obj_create(parent);
    lv_obj_set_size(page_outer, SCREEN_WIDTH, CONTENT_H);
    lv_obj_align(page_outer, LV_ALIGN_TOP_LEFT, 0, STATUS_BAR_H);
    lv_obj_set_style_bg_color(page_outer, lv_color_hex(0x0a0a23), 0);
    lv_obj_set_style_bg_opa(page_outer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_outer, 0, 0);
    lv_obj_set_style_radius(page_outer, 0, 0);
    lv_obj_set_style_pad_all(page_outer, 0, 0);
    lv_obj_set_scrollbar_mode(page_outer, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(page_outer, LV_DIR_VER);
    lv_obj_clear_flag(page_outer, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(page_outer, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(page_outer, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

    // 固定卡片 2×2
    int card_x[2] = { MARGIN_X, MARGIN_X + CARD_W + CARD_GAP_X };

    for (int i = 0; i < FIXED_CARD_COUNT; i++) {
        int row = i / COLS;
        int col = i % COLS;
        int x = card_x[col];
        int y = CARD_GAP_Y + row * (CARD_H + CARD_GAP_Y);
        create_card(&fixed_cards[i], page_outer, x, y, fixed_titles[i], card_colors[i]);
    }

    return page_outer;
}

static int g_last_rs485_count = -1;
static int g_last_can_count = -1;

void ui_dashboard_update(const Data_t *data)
{
    if (g_current_page != PAGE_DASHBOARD) return;

    // 固定卡片：4 个本地传感器
    double aranges[4] = { 150.0, 100.0, 150.0, 100.0 };
    const char *aunits[4] = { "°C", "%", "%", "g" };

    update_card(&fixed_cards[0], data->Tem, aranges[0], aunits[0]);
    update_card(&fixed_cards[1], data->Hum, aranges[1], aunits[1]);
    update_card(&fixed_cards[2], data->Gas, aranges[2], aunits[2]);
    update_card(&fixed_cards[3], data->Acc_xyz, aranges[3], aunits[3]);

    int fixed_h = 2 * (CARD_H + CARD_GAP_Y) + CARD_GAP_Y;

    // RS485 section title
    int rs485_title_y = fixed_h + 4;
    if (lbl_rs485_title == NULL) {
        lbl_rs485_title = lv_label_create(page_outer);
        lv_obj_set_style_text_color(lbl_rs485_title, lv_color_hex(0x1abc9c), 0);
        lv_obj_set_style_text_font(lbl_rs485_title, &lv_font_montserrat_14, 0);
    }
    lv_obj_set_pos(lbl_rs485_title, MARGIN_X, rs485_title_y);

    int need_rs485 = data->rs485_count > 0 ? data->rs485_count * 2 : 0;
    int rs485_changed = (need_rs485 != g_last_rs485_count);

    if (need_rs485 > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "RS485 Devices (%d)", data->rs485_count);
        lv_label_set_text(lbl_rs485_title, buf);
        lv_obj_clear_flag(lbl_rs485_title, LV_OBJ_FLAG_HIDDEN);

        if (rs485_changed) {
            lv_color_t rs485_colors[] = {
                LV_COLOR_MAKE(0x1a, 0xbc, 0x9c),
                LV_COLOR_MAKE(0x9b, 0x59, 0xb6)
            };
            if (ensure_device_cards(&rs485_cards, &rs485_card_count, need_rs485,
                                    page_outer, rs485_title_y + 20, "RS485", rs485_colors) == 0)
                g_last_rs485_count = need_rs485;
        }
    } else {
        lv_label_set_text(lbl_rs485_title, "");
        lv_obj_add_flag(lbl_rs485_title, LV_OBJ_FLAG_HIDDEN);
        if (rs485_changed) {
            clear_device_cards(&rs485_cards, &rs485_card_count);
            g_last_rs485_count = 0;
        }
    }

    // CAN section title
    int can_title_y = rs485_title_y;
    if (need_rs485 > 0 && rs485_card_count > 0) {
        int rs485_rows = (rs485_card_count + 1) / 2;
        can_title_y = rs485_title_y + 20 + rs485_rows * (CARD_H + CARD_GAP_Y);
    }

    if (lbl_can_title == NULL) {
        lbl_can_title = lv_label_create(page_outer);
        lv_obj_set_style_text_color(lbl_can_title, lv_color_hex(0xe67e22), 0);
        lv_obj_set_style_text_font(lbl_can_title, &lv_font_montserrat_14, 0);
    }
    lv_obj_set_pos(lbl_can_title, MARGIN_X, can_title_y);

    int need_can = data->can_count > 0 ? data->can_count * 2 : 0;
    int can_changed = (need_can != g_last_can_count);

    if (need_can > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "CAN Devices (%d)", data->can_count);
        lv_label_set_text(lbl_can_title, buf);
        lv_obj_clear_flag(lbl_can_title, LV_OBJ_FLAG_HIDDEN);

        if (can_changed) {
            lv_color_t can_colors[] = {
                LV_COLOR_MAKE(0xe6, 0x7e, 0x22),
                LV_COLOR_MAKE(0x34, 0x98, 0xdb)
            };
            if (ensure_device_cards(&can_cards, &can_card_count, need_can,
                                    page_outer, can_title_y + 20, "CAN", can_colors) == 0)
                g_last_can_count = need_can;
        }
    } else {
        lv_label_set_text(lbl_can_title, "");
        lv_obj_add_flag(lbl_can_title, LV_OBJ_FLAG_HIDDEN);
        if (can_changed) {
            clear_device_cards(&can_cards, &can_card_count);
            g_last_can_count = 0;
        }
    }

    // 卡片数量变化后触发布局更新，LVGL 根据子对象绝对坐标自动计算滚动范围
    if (rs485_changed || can_changed) {
        lv_obj_update_layout(page_outer);
        lv_obj_invalidate(page_outer);
    }

    // Update RS485 device cards data
    for (int d = 0; d < data->rs485_count && d * 2 < rs485_card_count; d++) {
        int base = d * 2;
        char buf1[32], buf2[32];
        snprintf(buf1, sizeof(buf1), "RS485 #%d Volt", data->rs485_dev[d].id);
        snprintf(buf2, sizeof(buf2), "RS485 #%d Elec", data->rs485_dev[d].id);
        lv_label_set_text(rs485_cards[base].lbl_title, buf1);
        lv_label_set_text(rs485_cards[base + 1].lbl_title, buf2);
        update_card(&rs485_cards[base],     data->rs485_dev[d].Volt, 1000, "V");
        update_card(&rs485_cards[base + 1], data->rs485_dev[d].Elec, 500, "A");
    }

    // Update CAN device cards data
    for (int d = 0; d < data->can_count && d * 2 < can_card_count; d++) {
        int base = d * 2;
        char buf1[32], buf2[32];
        snprintf(buf1, sizeof(buf1), "CAN #0x%X Speed", data->can_dev[d].id);
        snprintf(buf2, sizeof(buf2), "CAN #0x%X Flow", data->can_dev[d].id);
        lv_label_set_text(can_cards[base].lbl_title, buf1);
        lv_label_set_text(can_cards[base + 1].lbl_title, buf2);
        update_card(&can_cards[base],     data->can_dev[d].Speed, 100000, "rpm");
        update_card(&can_cards[base + 1], data->can_dev[d].Flow,  100000, "L/min");
    }
}

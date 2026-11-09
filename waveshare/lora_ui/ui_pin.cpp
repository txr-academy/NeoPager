#include "ui_pin.h"
#include "config.h"
#include "ui_nav.h"
#include "ui_common.h"
#include <string.h>

static_assert(sizeof(MSG_PIN) - 1 == PIN_LEN, "PIN_LEN must equal the length of MSG_PIN");

static lv_obj_t* pin_dots[PIN_LEN];
static lv_obj_t* pin_err = NULL;
static lv_timer_t* pin_timer = NULL;      /* one-shot check, 150 ms after the last digit */
static char pin_buf[PIN_LEN + 1];
static uint8_t pin_len = 0;
static bool pin_busy = false;             /* true while the check timer is pending */
static bool pin_err_shown = false;

static const char* pin_map[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    LV_SYMBOL_BACKSPACE, "0", "Clear", ""
};

/* Only the dot that changed is restyled */
static void pin_set_dot(int i, bool filled) {
    lv_obj_set_style_bg_opa(pin_dots[i], filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void pin_set_error(const char* msg) {
    lv_label_set_text(pin_err, msg);
    pin_err_shown = (msg[0] != '\0');
}

void pin_reset(void) {
    if (pin_timer) { lv_timer_del(pin_timer); pin_timer = NULL; }
    for (int i = 0; i < pin_len; i++) pin_set_dot(i, false);
    pin_len = 0;
    pin_buf[0] = '\0';
    pin_busy = false;
    if (pin_err_shown) pin_set_error("");
}

static void pin_check_cb(lv_timer_t* t) {
    pin_timer = NULL;                      /* LVGL deletes a repeat-count-1 timer after this returns */
    pin_busy = false;

    if (strcmp(pin_buf, MSG_PIN) == 0) {
        pin_reset();
        go_inbox();
    } else {
        for (int i = 0; i < pin_len; i++) pin_set_dot(i, false);
        pin_len = 0;
        pin_buf[0] = '\0';
        pin_set_error("Wrong PIN");        /* retry freely, no lockout */
    }
}

static void on_pin_key(lv_event_t* e) {
    if (pin_busy) return;

    lv_obj_t* kb = lv_event_get_target(e);
    uint16_t id = lv_btnmatrix_get_selected_btn(kb);
    if (id == LV_BTNMATRIX_BTN_NONE) {
        uint16_t* p = (uint16_t*)lv_event_get_param(e);
        if (p) id = *p;
    }
    if (id == LV_BTNMATRIX_BTN_NONE) return;

    const char* txt = lv_btnmatrix_get_btn_text(kb, id);
    if (!txt || txt[0] == '\0') return;

    if (txt[0] >= '0' && txt[0] <= '9' && txt[1] == '\0') {
        if (pin_len >= PIN_LEN) return;
        if (pin_err_shown) pin_set_error("");
        pin_buf[pin_len] = txt[0];
        pin_buf[pin_len + 1] = '\0';
        pin_set_dot(pin_len, true);
        pin_len++;

        if (pin_len == PIN_LEN) {
            pin_busy = true;
            pin_timer = lv_timer_create(pin_check_cb, 150, NULL);
            lv_timer_set_repeat_count(pin_timer, 1);
        }
    } else if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        if (pin_len > 0) {
            pin_len--;
            pin_buf[pin_len] = '\0';
            pin_set_dot(pin_len, false);
        }
    } else if (strcmp(txt, "Clear") == 0) {
        for (int i = 0; i < pin_len; i++) pin_set_dot(i, false);
        pin_len = 0;
        pin_buf[0] = '\0';
    }
}

static void on_pin_back(lv_event_t* e) {
    pin_reset();
    go_home();
}

void pin_build(void) {
    scr_pin = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_pin, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_anim_time(scr_pin, 0, 0);
    lv_obj_clear_flag(scr_pin, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- left half ---- */
    lv_obj_t* left = lv_obj_create(scr_pin);
    lv_obj_set_size(left, 240, LCD_VER_RES);
    lv_obj_set_pos(left, 0, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* btn_back = ui_back_button(left, 44, 36, on_pin_back);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 8, 6);

    lv_obj_t* title = lv_label_create(left);
    lv_label_set_text(title, "Enter PIN");
    lv_obj_set_style_text_font(title, FONT_L, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x1F1F1F), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t* dots = lv_obj_create(left);
    lv_obj_set_size(dots, 200, 30);
    lv_obj_align(dots, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_set_style_bg_opa(dots, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dots, 0, 0);
    lv_obj_set_style_pad_all(dots, 0, 0);
    lv_obj_set_style_pad_column(dots, 16, 0);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < PIN_LEN; i++) {
        pin_dots[i] = lv_obj_create(dots);
        lv_obj_set_size(pin_dots[i], 22, 22);
        lv_obj_set_style_radius(pin_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(pin_dots[i], 2, 0);
        lv_obj_set_style_border_color(pin_dots[i], lv_color_hex(0x0B57D0), 0);
        lv_obj_set_style_bg_color(pin_dots[i], lv_color_hex(0x0B57D0), 0);
        lv_obj_set_style_bg_opa(pin_dots[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_anim_time(pin_dots[i], 0, 0);
        lv_obj_clear_flag(pin_dots[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(pin_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }

    pin_err = lv_label_create(left);
    lv_label_set_text(pin_err, "");
    lv_obj_set_style_text_color(pin_err, lv_color_hex(0xB3261E), 0);
    lv_obj_align(pin_err, LV_ALIGN_TOP_MID, 0, 180);

    /* ---- right half: 3x4 keypad ---- */
    lv_obj_t* pad = lv_btnmatrix_create(scr_pin);
    lv_obj_set_size(pad, 222, 292);
    lv_obj_set_pos(pad, 250, 14);
    lv_obj_set_style_anim_time(pad, 0, 0);
    lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
    lv_btnmatrix_set_map(pad, pin_map);

    lv_obj_set_style_bg_color(pad, lv_color_hex(0xE9EEF6), 0);
    lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(pad, 6, 0);
    lv_obj_set_style_pad_row(pad, 6, 0);
    lv_obj_set_style_pad_column(pad, 6, 0);

    lv_obj_set_style_radius(pad, 10, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pad, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(pad, 1, LV_PART_ITEMS);
    lv_obj_set_style_shadow_color(pad, lv_color_hex(0xC4C7C5), LV_PART_ITEMS);
    lv_obj_set_style_shadow_ofs_y(pad, 1, LV_PART_ITEMS);
    lv_obj_set_style_text_font(pad, FONT_L, LV_PART_ITEMS);
    lv_obj_set_style_text_color(pad, lv_color_hex(0x1F1F1F), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pad, lv_color_hex(0x0B57D0), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(pad, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_PRESSED);

    lv_obj_add_event_cb(pad, on_pin_key, LV_EVENT_VALUE_CHANGED, NULL);

    pin_reset();
}

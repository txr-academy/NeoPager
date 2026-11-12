#include "ui_notify.h"
#include "config.h"
#include "app_data.h"
#include "ui_nav.h"
#include "ui_power.h"
#include <Arduino.h>
#include <lvgl.h>

static lv_obj_t* toast = NULL;
static lv_obj_t* toast_lbl = NULL;
static lv_timer_t* hide_timer = NULL;
static lv_timer_t* alert_timer = NULL;

static void hide_toast(void) {
    if (toast) lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
    if (hide_timer) lv_timer_pause(hide_timer);
}

static void on_hide_timer(lv_timer_t* t) { hide_toast(); }

static void on_toast_clicked(lv_event_t* e) {
    hide_toast();
    /* From Home or Settings a tap goes straight to the PIN screen; elsewhere Messages is already open. */
    lv_obj_t* act = lv_scr_act();
    if (act == scr_home || act == scr_settings) go_pin();
}

#if ALERT_GPIO >= 0
static void on_alert_off(lv_timer_t* t) {
    digitalWrite(ALERT_GPIO, LOW);
    lv_timer_pause(alert_timer);
}
#endif

void ui_notify_build(void) {
    toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(toast, 280, 40);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x0B57D0), 0);
    lv_obj_set_style_radius(toast, 20, 0);
    lv_obj_set_style_border_width(toast, 0, 0);
    lv_obj_set_style_pad_all(toast, 0, 0);
    lv_obj_set_style_shadow_width(toast, 0, 0);
    lv_obj_set_style_anim_time(toast, 0, 0);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(toast, on_toast_clicked, LV_EVENT_CLICKED, NULL);

    toast_lbl = lv_label_create(toast);
    lv_obj_set_style_text_color(toast_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(toast_lbl, LV_SYMBOL_ENVELOPE "  New message");
    lv_obj_center(toast_lbl);

    lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);

    hide_timer = lv_timer_create(on_hide_timer, NOTIFY_MS, NULL);
    lv_timer_pause(hide_timer);

#if ALERT_GPIO >= 0
    pinMode(ALERT_GPIO, OUTPUT);
    digitalWrite(ALERT_GPIO, LOW);
    alert_timer = lv_timer_create(on_alert_off, ALERT_PULSE_MS, NULL);
    lv_timer_pause(alert_timer);
#endif
}

void ui_notify_rx(int thread, bool thread_visible) {
    power_activity();                         /* turn the screen on (also restarts the idle timer) */

#if ALERT_GPIO >= 0
    digitalWrite(ALERT_GPIO, HIGH);
    lv_timer_reset(alert_timer);
    lv_timer_resume(alert_timer);
#endif

    if (thread_visible || !toast) return;     /* the message is already on screen */

    char buf[64];
    int total = app_unread_total();
#if NOTIFY_SHOW_SENDER
    if (total <= 1) snprintf(buf, sizeof(buf), LV_SYMBOL_ENVELOPE "  %s", messages[thread].sender);
    else            snprintf(buf, sizeof(buf), LV_SYMBOL_ENVELOPE "  %d new messages", total);
#else
    if (total <= 1) snprintf(buf, sizeof(buf), LV_SYMBOL_ENVELOPE "  New message");
    else            snprintf(buf, sizeof(buf), LV_SYMBOL_ENVELOPE "  %d new messages", total);
#endif
    lv_label_set_text(toast_lbl, buf);
    lv_obj_center(toast_lbl);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast);
    lv_timer_reset(hide_timer);
    lv_timer_resume(hide_timer);
}

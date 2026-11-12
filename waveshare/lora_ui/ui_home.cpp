#include "ui_home.h"
#include "config.h"
#include "ui_nav.h"
#include "app_data.h"
#include "lora_modem.h"
#include <stdio.h>
#include <string.h>

/* Status strip (top) and unread badge (Messages card). Refreshed by a 1 s timer, only while Home is on screen. */
static lv_obj_t* st_dot = NULL;
static lv_obj_t* st_lbl = NULL;
static lv_obj_t* st_rx_lbl = NULL;
static lv_obj_t* badge = NULL;
static lv_obj_t* badge_lbl = NULL;
static int  shown_level = -1;
static const char* shown_status = NULL;
static char shown_rx[28] = "";
static int  shown_unread = -1;

static void on_settings_card(lv_event_t* e) { go_settings(); }
static void on_messages_card(lv_event_t* e) { go_pin(); }

/* 216x200 card: blue circle with an icon, label underneath */
static lv_obj_t* make_card(int x, const char* symbol, const char* text, lv_event_cb_t cb) {
    lv_obj_t* card = lv_btn_create(scr_home);
    lv_obj_set_size(card, 216, 200);
    lv_obj_set_pos(card, x, (LCD_VER_RES - 200) / 2);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xEDF2FA), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xD3E3FD), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(card, lv_color_hex(0xD3E3FD), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_anim_time(card, 0, 0);
    lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* circle = lv_obj_create(card);
    lv_obj_set_size(circle, 84, 84);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, -24);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0x0B57D0), 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_set_style_anim_time(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);     /* let the tap reach the card */
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* icon = lv_label_create(circle);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, FONT_XL, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(icon);

    lv_obj_t* lbl = lv_label_create(card);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, FONT_L, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x1F1F1F), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 56);
    return card;
}

static void home_refresh(void) {
    /* dot colour + status text */
    int lvl = lora_link_level();
    if (lvl != shown_level) {
        shown_level = lvl;
        uint32_t col = (lvl == 1) ? 0x2E7D32 : (lvl == 2) ? 0xE28B00 : (lvl == 3) ? 0xB3261E : 0x9AA0A6;
        lv_obj_set_style_bg_color(st_dot, lv_color_hex(col), 0);
    }
    const char* st = lora_modem_status_text();
    if (st != shown_status) {                        /* status text comes from string literals: pointer compare is enough */
        shown_status = st;
        char b[48];
        snprintf(b, sizeof(b), "LoRa: %s", st);
        lv_label_set_text(st_lbl, b);
    }

    /* age of the last message */
    char r[28];
    int32_t age = lora_last_rx_age_s();
    if (age < 0)         snprintf(r, sizeof(r), "No messages yet");
    else if (age < 60)   snprintf(r, sizeof(r), "Last message %ld s ago", (long)age);
    else if (age < 3600) snprintf(r, sizeof(r), "Last message %ld min ago", (long)(age / 60));
    else                 snprintf(r, sizeof(r), "Last message %ld h ago", (long)(age / 3600));
    if (strcmp(r, shown_rx) != 0) {
        snprintf(shown_rx, sizeof(shown_rx), "%s", r);
        lv_label_set_text(st_rx_lbl, r);
    }

    /* unread badge on the Messages card */
    int u = app_unread_total();
    if (u != shown_unread) {
        shown_unread = u;
        if (u == 0) {
            lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
        } else {
            char b[6];
            if (u > 9) snprintf(b, sizeof(b), "9+"); else snprintf(b, sizeof(b), "%d", u);
            lv_label_set_text(badge_lbl, b);
            lv_obj_center(badge_lbl);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void home_timer_cb(lv_timer_t* t) {
    if (lv_scr_act() == scr_home) home_refresh();
}

void home_build(void) {
    scr_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_home, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_anim_time(scr_home, 0, 0);
    lv_obj_clear_flag(scr_home, LV_OBJ_FLAG_SCROLLABLE);

    make_card(16,  LV_SYMBOL_SETTINGS, "Settings", on_settings_card);   /* left  */
    lv_obj_t* msg_card = make_card(248, LV_SYMBOL_ENVELOPE, "Messages", on_messages_card);   /* right */

    /* unread badge, top-right corner of the Messages card */
    badge = lv_obj_create(msg_card);
    lv_obj_set_size(badge, 30, 30);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0xB3261E), 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_set_style_anim_time(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
    badge_lbl = lv_label_create(badge);
    lv_obj_set_style_text_color(badge_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(badge_lbl, "");
    lv_obj_center(badge_lbl);

    /* status strip above the cards: link dot, LoRa state, age of the last message */
    st_dot = lv_obj_create(scr_home);
    lv_obj_set_size(st_dot, 12, 12);
    lv_obj_set_pos(st_dot, 20, 24);
    lv_obj_set_style_radius(st_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(st_dot, 0, 0);
    lv_obj_set_style_pad_all(st_dot, 0, 0);
    lv_obj_set_style_bg_color(st_dot, lv_color_hex(0x9AA0A6), 0);
    lv_obj_set_style_anim_time(st_dot, 0, 0);
    lv_obj_clear_flag(st_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    st_lbl = lv_label_create(scr_home);
    lv_label_set_text(st_lbl, "LoRa: waiting for modem");
    lv_obj_set_pos(st_lbl, 40, 20);
    lv_obj_set_style_text_color(st_lbl, lv_color_hex(0x1F1F1F), 0);

    st_rx_lbl = lv_label_create(scr_home);
    lv_label_set_text(st_rx_lbl, "No messages yet");
    lv_obj_set_width(st_rx_lbl, 220);
    lv_obj_set_style_text_align(st_rx_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(st_rx_lbl, LCD_HOR_RES - 220 - 20, 20);
    lv_obj_set_style_text_color(st_rx_lbl, lv_color_hex(0x747775), 0);

    lv_timer_create(home_timer_cb, 1000, NULL);
    home_refresh();
}

#include "ui_home.h"
#include "config.h"
#include "ui_nav.h"

static void on_settings_card(lv_event_t* e) { go_settings(); }
static void on_messages_card(lv_event_t* e) { go_pin(); }

/* 216x200 card: blue circle with an icon, label underneath */
static void make_card(int x, const char* symbol, const char* text, lv_event_cb_t cb) {
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
}

void home_build(void) {
    scr_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_home, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_anim_time(scr_home, 0, 0);
    lv_obj_clear_flag(scr_home, LV_OBJ_FLAG_SCROLLABLE);

    make_card(16,  LV_SYMBOL_SETTINGS, "Settings", on_settings_card);   /* left  */
    make_card(248, LV_SYMBOL_ENVELOPE, "Messages", on_messages_card);   /* right */
}

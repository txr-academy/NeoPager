#include "ui_contacts.h"
#include "config.h"
#include "app_data.h"
#include "ui_nav.h"
#include "ui_common.h"
#include "ui_inbox.h"
#include "ui_keyboard.h"

static lv_obj_t* ta_name = NULL;
static lv_obj_t* ta_id = NULL;
static lv_obj_t* active_ta = NULL;
static lv_obj_t* status_lbl = NULL;

static void set_status(const char* msg, bool is_error) {
    lv_label_set_text(status_lbl, msg);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(is_error ? 0xB3261E : 0x747775), 0);
}

static void focus_field(lv_obj_t* ta) {
    active_ta = ta;
    lv_obj_add_state(ta, LV_STATE_FOCUSED);
    lv_obj_clear_state(ta == ta_name ? ta_id : ta_name, LV_STATE_FOCUSED);
    kb_set_textarea(ta);
    kb_auto_mode_for_text();
}

static void do_save(void) {
    const char* err = NULL;
    int slot = contact_add(lv_textarea_get_text(ta_name), lv_textarea_get_text(ta_id), &err);
    if (slot < 0) {
        set_status(err ? err : "Could not add contact", true);
        return;
    }
    inbox_rebuild();
    go_inbox();
}

/* keyboard OK key: name -> ID -> save; keyboard key: jump to the other field */
static void on_ok_key(void)     { if (active_ta == ta_name) focus_field(ta_id); else do_save(); }
static void on_switch_key(void) { focus_field(active_ta == ta_name ? ta_id : ta_name); }

static void on_ta_clicked(lv_event_t* e) { focus_field(lv_event_get_target(e)); }
static void on_save_clicked(lv_event_t* e) { do_save(); }
static void on_back_clicked(lv_event_t* e) { go_inbox(); }

static lv_obj_t* make_field(int y, const char* placeholder, int max_len) {
    lv_obj_t* ta = lv_textarea_create(scr_addcontact);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_set_size(ta, LCD_HOR_RES - 16, 36);
    lv_obj_set_pos(ta, 8, y);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0xEDF2FA), 0);
    lv_obj_set_style_radius(ta, 12, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0xD3E3FD), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_anim_time(ta, 0, 0);
    lv_obj_set_style_anim_time(ta, 0, LV_PART_CURSOR);
    lv_obj_add_event_cb(ta, on_ta_clicked, LV_EVENT_CLICKED, NULL);
    return ta;
}

void contacts_build(void) {
    scr_addcontact = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_addcontact, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_anim_time(scr_addcontact, 0, 0);
    lv_obj_clear_flag(scr_addcontact, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bar = ui_make_appbar(scr_addcontact);
    lv_obj_t* btn_back = ui_back_button(bar, 44, 36, on_back_clicked);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 2, 0);

    lv_obj_t* title = lv_label_create(bar);
    lv_label_set_text(title, "New contact");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 52, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0B57D0), 0);

    lv_obj_t* btn_save = lv_btn_create(bar);
    lv_obj_set_size(btn_save, 44, 36);
    lv_obj_align(btn_save, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0xD3E3FD), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn_save, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_save, 0, 0);
    lv_obj_set_style_anim_time(btn_save, 0, 0);
    lv_obj_add_event_cb(btn_save, on_save_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(lbl_save, lv_color_hex(0x0B57D0), 0);
    lv_obj_center(lbl_save);

    /* 148 px are left above the 172 px keyboard */
    ta_name = make_field(46, "Name", MAX_NAME_LEN);
    ta_id   = make_field(86, "Contact ID (1-4 letters or digits)", MAX_TAG_LEN);
    lv_textarea_set_accepted_chars(ta_id, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

    status_lbl = lv_label_create(scr_addcontact);
    lv_obj_set_pos(status_lbl, 12, 126);
    lv_obj_set_width(status_lbl, LCD_HOR_RES - 24);
    lv_label_set_long_mode(status_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text(status_lbl, "");
}

void contacts_open(void) {
    lv_textarea_set_text(ta_name, "");
    lv_textarea_set_text(ta_id, "");
    set_status("Use the ID registered for this person on the relay", false);
    kb_attach(scr_addcontact, ta_name, on_ok_key, on_switch_key);
    kb_show();
    focus_field(ta_name);
    lv_scr_load(scr_addcontact);
}

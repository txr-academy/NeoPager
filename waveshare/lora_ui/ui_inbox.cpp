#include "ui_inbox.h"
#include "config.h"
#include "app_data.h"
#include "ui_nav.h"
#include "ui_common.h"
#include "ui_chat.h"
#include "lora_modem.h"
#include <stdlib.h>
#include <string.h>

static lv_obj_t* list = NULL;
static lv_obj_t* list_rows[NUM_MESSAGES];
static lv_obj_t* row_name_labels[NUM_MESSAGES];
static lv_obj_t* row_snip_labels[NUM_MESSAGES];
static lv_obj_t* row_time_labels[NUM_MESSAGES];
static int8_t    row_style_unread[NUM_MESSAGES];   /* last applied unread state (-1 = none yet) */
static lv_obj_t* row_badge[NUM_MESSAGES];          /* unread count bubble at the right of the row */
static lv_obj_t* row_badge_lbl[NUM_MESSAGES];
static int16_t   row_badge_n[NUM_MESSAGES];        /* last count shown (-1 = none yet) */

/* Touch drag discrimination */
static lv_point_t row_press_pos;
static bool row_touch_dragged = false;
static bool row_long_pressed = false;
static int  pending_delete = -1;

static void update_row_read_styling(int idx) {
    if (!row_name_labels[idx]) return;
    int8_t u = messages[idx].unread ? 1 : 0;
    if (row_style_unread[idx] == u) return;
    row_style_unread[idx] = u;

    if (u) {
        lv_obj_set_style_text_color(row_name_labels[idx], lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(row_snip_labels[idx], lv_color_hex(0x1F1F1F), 0);
        lv_obj_set_style_text_color(row_time_labels[idx], lv_color_hex(0x0B57D0), 0);
        if (list_rows[idx]) lv_obj_set_style_border_color(list_rows[idx], lv_color_hex(0xA8C7FA), 0);
    } else {
        lv_obj_set_style_text_color(row_name_labels[idx], lv_color_hex(0x5E6368), 0);
        lv_obj_set_style_text_color(row_snip_labels[idx], lv_color_hex(0x747775), 0);
        lv_obj_set_style_text_color(row_time_labels[idx], lv_color_hex(0x80868B), 0);
        if (list_rows[idx]) lv_obj_set_style_border_color(list_rows[idx], lv_color_hex(0xE0E2EC), 0);
    }
}

static void update_badge(int idx) {
    if (!row_badge[idx]) return;
    int n = app_unread_count(idx);
    if (row_badge_n[idx] == n) return;
    row_badge_n[idx] = (int16_t)n;
    if (n == 0) { lv_obj_add_flag(row_badge[idx], LV_OBJ_FLAG_HIDDEN); return; }
    char b[6];
    if (n > 9) snprintf(b, sizeof(b), "9+"); else snprintf(b, sizeof(b), "%d", n);
    lv_label_set_text(row_badge_lbl[idx], b);
    lv_obj_center(row_badge_lbl[idx]);
    lv_obj_clear_flag(row_badge[idx], LV_OBJ_FLAG_HIDDEN);
}

/* Called whenever a chat has new activity (message sent or received): update the row and bring it to the top */
void inbox_refresh_row(int idx) {
    if (idx < 0 || idx >= NUM_MESSAGES || !row_snip_labels[idx]) return;
    lv_label_set_text(row_snip_labels[idx], messages[idx].preview);
    lv_label_set_text(row_time_labels[idx], messages[idx].time);
    update_row_read_styling(idx);
    update_badge(idx);
    if (list_rows[idx]) lv_obj_move_background(list_rows[idx]);      /* first child = top of the list */
}

/* ---------- delete a contact (long press on its row) ---------- */
static const char* delete_btns[] = { "Delete", "Cancel", "" };

static void on_delete_mbox(lv_event_t* e) {
    lv_obj_t* mbox = lv_event_get_current_target(e);
    uint16_t btn = lv_msgbox_get_active_btn(mbox);
    if (btn == 0 && pending_delete > 0) {
        int slot = pending_delete;
        lora_purge_thread(slot);       /* messages, RAM + flash */
        contact_remove(slot);          /* table entry, flash */
        chat_forget_thread(slot);      /* cached bubbles */
        inbox_rebuild();
    }
    pending_delete = -1;
    lv_msgbox_close(mbox);
}

static void confirm_delete(int slot) {
    pending_delete = slot;
    char txt[64];
    snprintf(txt, sizeof(txt), "Delete %s and the saved messages?", messages[slot].sender);
    lv_obj_t* mbox = lv_msgbox_create(NULL, "Delete contact", txt, delete_btns, false);
    lv_obj_set_width(mbox, 300);
    lv_obj_center(mbox);
    lv_obj_add_event_cb(mbox, on_delete_mbox, LV_EVENT_VALUE_CHANGED, NULL);
}

/* Touch discrimination on list items */
static void on_message_row_touch(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_get_act();
    intptr_t idx = (intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_PRESSED) {
        row_touch_dragged = false;
        row_long_pressed = false;
        if (indev) lv_indev_get_point(indev, &row_press_pos);
    } else if (code == LV_EVENT_PRESSING) {
        if (indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            if (abs(p.x - row_press_pos.x) > 7 || abs(p.y - row_press_pos.y) > 7) {
                row_touch_dragged = true;
            }
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        if (row_touch_dragged || idx < 1 || idx >= NUM_MESSAGES) return;   /* slot 0 cannot be deleted */
        row_long_pressed = true;
        confirm_delete((int)idx);
    } else if (code == LV_EVENT_CLICKED) {
        if (row_touch_dragged || row_long_pressed) return;   /* CLICKED also fires after a long press */
        if (indev && lv_indev_get_scroll_obj(indev) != NULL) return;

        if (idx >= 0 && idx < NUM_MESSAGES && contact_used((int)idx)) {
            app_unread_clear((int)idx);
            update_badge((int)idx);
            update_row_read_styling((int)idx);
            go_chat((int)idx);
        }
    }
}

static void on_inbox_back(lv_event_t* e) {
    go_home();      /* Messages relocks: the next visit goes through the PIN screen */
}

static void on_add_clicked(lv_event_t* e) {
    go_addcontact();
}

/* (Re)creates one row per used slot. Safe to call whenever the contact list changes. */
void inbox_rebuild(void) {
    lv_obj_clean(list);
    memset(list_rows, 0, sizeof(list_rows));
    memset(row_name_labels, 0, sizeof(row_name_labels));
    memset(row_snip_labels, 0, sizeof(row_snip_labels));
    memset(row_time_labels, 0, sizeof(row_time_labels));
    memset(row_style_unread, -1, sizeof(row_style_unread));
    memset(row_badge, 0, sizeof(row_badge));
    memset(row_badge_lbl, 0, sizeof(row_badge_lbl));
    for (int i = 0; i < NUM_MESSAGES; i++) row_badge_n[i] = -1;

    /* most recent activity first; chats without activity keep slot order (stable insertion sort) */
    int order[NUM_MESSAGES];
    int cnt = 0;
    for (int i = 0; i < NUM_MESSAGES; i++) if (contact_used(i)) order[cnt++] = i;
    for (int a = 1; a < cnt; a++) {
        int v = order[a];
        uint32_t va = app_last_activity(v);
        int b = a - 1;
        while (b >= 0 && app_last_activity(order[b]) < va) { order[b + 1] = order[b]; b--; }
        order[b + 1] = v;
    }

    for (int k = 0; k < cnt; k++) {
        int i = order[k];

        list_rows[i] = lv_obj_create(list);
        lv_obj_set_size(list_rows[i], LCD_HOR_RES - 16, 68);
        lv_obj_set_style_radius(list_rows[i], 12, 0);
        lv_obj_set_style_bg_color(list_rows[i], lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(list_rows[i], lv_color_hex(0xEDF2FA), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(list_rows[i], lv_color_hex(0xE0E2EC), 0);
        lv_obj_set_style_border_width(list_rows[i], 1, 0);
        lv_obj_set_style_pad_all(list_rows[i], 0, 0);
        lv_obj_set_style_anim_time(list_rows[i], 0, 0);
        lv_obj_clear_flag(list_rows[i], LV_OBJ_FLAG_SCROLLABLE);

        /* only the events the handler uses */
        lv_obj_add_event_cb(list_rows[i], on_message_row_touch, LV_EVENT_PRESSED,      (void*)(intptr_t)i);
        lv_obj_add_event_cb(list_rows[i], on_message_row_touch, LV_EVENT_PRESSING,     (void*)(intptr_t)i);
        lv_obj_add_event_cb(list_rows[i], on_message_row_touch, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
        lv_obj_add_event_cb(list_rows[i], on_message_row_touch, LV_EVENT_CLICKED,      (void*)(intptr_t)i);

        lv_obj_t* avatar = lv_obj_create(list_rows[i]);
        lv_obj_set_size(avatar, 42, 42);
        lv_obj_set_pos(avatar, 10, 13);
        lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(avatar, lv_color_hex(messages[i].avatar_color), 0);
        lv_obj_set_style_border_width(avatar, 0, 0);
        lv_obj_set_style_anim_time(avatar, 0, 0);
        lv_obj_clear_flag(avatar, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* avatar_txt = lv_label_create(avatar);
        char initial[2] = { messages[i].sender[0] ? messages[i].sender[0] : '?', '\0' };
        lv_label_set_text(avatar_txt, initial);
        lv_obj_set_style_text_color(avatar_txt, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(avatar_txt);

        row_name_labels[i] = lv_label_create(list_rows[i]);
        lv_label_set_text(row_name_labels[i], messages[i].sender);
        lv_obj_set_pos(row_name_labels[i], 62, 12);
        lv_obj_set_width(row_name_labels[i], 280);
        lv_label_set_long_mode(row_name_labels[i], LV_LABEL_LONG_DOT);
        lv_obj_clear_flag(row_name_labels[i], LV_OBJ_FLAG_CLICKABLE);

        row_time_labels[i] = lv_label_create(list_rows[i]);
        lv_label_set_text(row_time_labels[i], messages[i].time);
        lv_obj_set_pos(row_time_labels[i], (LCD_HOR_RES - 16) - 85, 14);
        lv_obj_set_width(row_time_labels[i], 75);
        lv_obj_set_style_text_align(row_time_labels[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_clear_flag(row_time_labels[i], LV_OBJ_FLAG_CLICKABLE);

        row_snip_labels[i] = lv_label_create(list_rows[i]);
        lv_label_set_text(row_snip_labels[i], messages[i].preview);
        lv_obj_set_pos(row_snip_labels[i], 62, 38);
        lv_obj_set_width(row_snip_labels[i], (LCD_HOR_RES - 16) - 74 - 36);     /* leaves room for the badge */
        lv_label_set_long_mode(row_snip_labels[i], LV_LABEL_LONG_DOT);
        lv_obj_clear_flag(row_snip_labels[i], LV_OBJ_FLAG_CLICKABLE);

        row_badge[i] = lv_obj_create(list_rows[i]);
        lv_obj_set_size(row_badge[i], 24, 24);
        lv_obj_set_pos(row_badge[i], (LCD_HOR_RES - 16) - 40, 36);
        lv_obj_set_style_radius(row_badge[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(row_badge[i], lv_color_hex(0x0B57D0), 0);
        lv_obj_set_style_border_width(row_badge[i], 0, 0);
        lv_obj_set_style_pad_all(row_badge[i], 0, 0);
        lv_obj_set_style_anim_time(row_badge[i], 0, 0);
        lv_obj_clear_flag(row_badge[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row_badge[i], LV_OBJ_FLAG_HIDDEN);
        row_badge_lbl[i] = lv_label_create(row_badge[i]);
        lv_obj_set_style_text_color(row_badge_lbl[i], lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(row_badge_lbl[i], "");
        lv_obj_center(row_badge_lbl[i]);

        update_row_read_styling(i);
        update_badge(i);
    }
}

void inbox_build(void) {
    if (!scr_inbox) scr_inbox = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_inbox, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_anim_time(scr_inbox, 0, 0);
    lv_obj_clear_flag(scr_inbox, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = lv_obj_create(scr_inbox);
    lv_obj_set_size(header, LCD_HOR_RES - 16, 44);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xEDF2FA), 0);
    lv_obj_set_style_radius(header, 22, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0xD3E3FD), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_pad_hor(header, 16, 0);
    lv_obj_set_style_anim_time(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* btn_back = ui_back_button(header, 40, 32, on_inbox_back);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, -8, 0);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Messages");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0B57D0), 0);

    /* add contact */
    lv_obj_t* btn_add = lv_btn_create(header);
    lv_obj_set_size(btn_add, 40, 32);
    lv_obj_align(btn_add, LV_ALIGN_RIGHT_MID, 8, 0);
    lv_obj_set_style_bg_color(btn_add, lv_color_hex(0xD3E3FD), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn_add, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_add, 0, 0);
    lv_obj_set_style_anim_time(btn_add, 0, 0);
    lv_obj_add_event_cb(btn_add, on_add_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_add = lv_label_create(btn_add);
    lv_label_set_text(lbl_add, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(lbl_add, lv_color_hex(0x0B57D0), 0);
    lv_obj_center(lbl_add);

    list = lv_obj_create(scr_inbox);
    lv_obj_set_size(list, LCD_HOR_RES, LCD_VER_RES - 54);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 54);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_style_anim_time(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    inbox_rebuild();
}

#include "ui_chat.h"
#include "config.h"
#include "app_data.h"
#include "lora_modem.h"
#include "ui_nav.h"
#include "ui_inbox.h"
#include "ui_keyboard.h"
#include "ui_common.h"
#include <Arduino.h>
#include <string.h>

static lv_obj_t* appbar = NULL;
static lv_obj_t* detail_sender_lbl = NULL;
static lv_obj_t* detail_avatar_lbl = NULL;
static lv_obj_t* detail_avatar_box = NULL;
static lv_obj_t* chat_scroll_cont = NULL;
static lv_obj_t* input_bar_cont = NULL;
static lv_obj_t* ta_input = NULL;
static lv_obj_t* char_count_lbl = NULL;
static lv_obj_t* chat_in_time_lbl = NULL;    /* time label of the incoming card (refreshed on cache hit) */

/* Rebuild cache: the bubbles are rebuilt only when the thread changed or something
   arrived while the chat was hidden. */
static bool chat_dirty = true;
static int  chat_built_idx = -1;

static bool kb_shown = false;
static uint8_t char_tier = 0;                /* 0 normal, 1 warning, 2 critical */

/* What the chat has already pulled from lora_modem */
static uint32_t chat_seen_seq = 0;
static uint32_t chat_seen_drops = 0;
static uint32_t chat_seen_result_rev = 0;
static const char* last_status = NULL;
static int chat_limit = LORA_MAX_PAYLOAD;    /* characters allowed in the input box for the open chat */

static void set_keyboard_visible(bool visible);

/* ==================== helpers ==================== */
static bool chat_thread_visible(int thread) {
    return scr_detail && lv_scr_act() == scr_detail && current_msg_idx == thread;
}

static void lora_refresh_header(void) {
    if (!detail_sender_lbl) return;
    char buf[64];
    const char* tag = app_tag(current_msg_idx);
    if (tag[0]) snprintf(buf, sizeof(buf), "%s (%s) - %s", messages[current_msg_idx].sender, tag, lora_modem_status_text());
    else        snprintf(buf, sizeof(buf), "%s - %s", messages[current_msg_idx].sender, lora_modem_status_text());
    lv_label_set_text(detail_sender_lbl, buf);
}

static const char* tx_status_text(const LoraEntry& e) {
    switch (e.kind) {
        case LORA_TX_SENDING: return "Sending...";
        case LORA_TX_SENT:    return "Sent via LoRa";
        case LORA_TX_FAILED:  return "Send failed";
        default:              return e.info;
    }
}

static void scroll_bottom(void) {
    lv_obj_scroll_to_y(chat_scroll_cont, LV_COORD_MAX, LV_ANIM_OFF);
}

/* Received-message bubble (left side, same look as the incoming card) */
static void make_rx_bubble(const char* text, const char* info) {
    lv_obj_t* bubble = lv_obj_create(chat_scroll_cont);
    lv_obj_set_width(bubble, 340);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(0xF0F4F9), 0);
    lv_obj_set_style_border_color(bubble, lv_color_hex(0xE0E2EC), 0);
    lv_obj_set_style_border_width(bubble, 1, 0);
    lv_obj_set_style_radius(bubble, 16, 0);
    lv_obj_set_style_pad_all(bubble, 10, 0);
    lv_obj_set_style_pad_row(bubble, 4, 0);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* info_lbl = lv_label_create(bubble);
    lv_label_set_text(info_lbl, info);
    lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x5F6368), 0);

    lv_obj_t* msg_lbl = lv_label_create(bubble);
    lv_label_set_text(msg_lbl, text);
    lv_obj_set_width(msg_lbl, 318);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0x1F1F1F), 0);
}

/* Sent bubble (right side). Does not scroll: callers scroll once after a batch. */
static void make_tx_bubble(const char* text, const char* timestamp) {
    lv_obj_t* row_wrap = lv_obj_create(chat_scroll_cont);
    lv_obj_set_size(row_wrap, LCD_HOR_RES - 32, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_wrap, 0, 0);
    lv_obj_set_style_pad_all(row_wrap, 0, 0);
    lv_obj_clear_flag(row_wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bubble = lv_obj_create(row_wrap);
    lv_obj_set_width(bubble, 340);
    lv_obj_align(bubble, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(0x0B57D0), 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_radius(bubble, 16, 0);
    lv_obj_set_style_pad_all(bubble, 10, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* time_lbl = lv_label_create(bubble);
    lv_label_set_text(time_lbl, timestamp);
    lv_obj_align(time_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_text_color(time_lbl, lv_color_hex(0xD3E3FD), 0);

    lv_obj_t* msg_lbl = lv_label_create(bubble);
    lv_label_set_text(msg_lbl, text);
    lv_obj_set_width(msg_lbl, 320);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_align(msg_lbl, LV_ALIGN_TOP_LEFT, 0, 16);
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0xFFFFFF), 0);
}

/* After make_tx_bubble(): the bubble's time label, which doubles as the TX status label */
static lv_obj_t* grab_last_bubble_label(void) {
    lv_obj_t* row_wrap = lv_obj_get_child(chat_scroll_cont, -1);
    lv_obj_t* bubble = row_wrap ? lv_obj_get_child(row_wrap, 0) : NULL;
    return bubble ? lv_obj_get_child(bubble, 0) : NULL;
}

static void forget_labels(void) {              /* call before lv_obj_clean(chat_scroll_cont) */
    int n = lora_hist_count();
    for (int i = 0; i < n; i++) lora_hist_at(i)->lbl = NULL;
}

static void sync_tx_labels(void) {             /* TX status changed: update every TX label whose text differs */
    int n = lora_hist_count();
    for (int i = 0; i < n; i++) {
        LoraEntry* e = lora_hist_at(i);
        if (e->kind == LORA_RX_MSG || !e->lbl) continue;
        const char* t = tx_status_text(*e);
        if (strcmp(lv_label_get_text(e->lbl), t) != 0) lv_label_set_text(e->lbl, t);
    }
}

static void render_content(int idx) {
    lv_obj_t* in_card = lv_obj_create(chat_scroll_cont);
    lv_obj_set_width(in_card, LCD_HOR_RES - 32);
    lv_obj_set_style_bg_color(in_card, lv_color_hex(0xF0F4F9), 0);
    lv_obj_set_style_border_color(in_card, lv_color_hex(0xE0E2EC), 0);
    lv_obj_set_style_border_width(in_card, 1, 0);
    lv_obj_set_style_radius(in_card, 16, 0);
    lv_obj_set_style_pad_all(in_card, 12, 0);
    lv_obj_clear_flag(in_card, LV_OBJ_FLAG_SCROLLABLE);

    chat_in_time_lbl = lv_label_create(in_card);
    lv_label_set_text(chat_in_time_lbl, messages[idx].time);
    lv_obj_set_pos(chat_in_time_lbl, 0, 0);
    lv_obj_set_style_text_color(chat_in_time_lbl, lv_color_hex(0x5F6368), 0);

    lv_obj_t* in_body = lv_label_create(in_card);
    lv_label_set_text(in_body, messages[idx].full_body);
    lv_obj_set_width(in_body, LCD_HOR_RES - 56);
    lv_label_set_long_mode(in_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(in_body, 0, 24);
    lv_obj_set_style_text_color(in_body, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_text_line_space(in_body, 4, 0);

    int n = lora_hist_count();
    for (int i = 0; i < n; i++) {
        LoraEntry* e = lora_hist_at(i);
        if (e->thread != idx) continue;               // only this chat's messages
        if (e->kind == LORA_RX_MSG) {
            make_rx_bubble(e->text, e->info);
        } else {
            make_tx_bubble(e->text, tx_status_text(*e));
            e->lbl = grab_last_bubble_label();
        }
    }
    scroll_bottom();
}

/* Counter text always follows the length; the colour style is only touched when the tier changes */
static void char_counter_update(int len) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d/%d", len, chat_limit);
    lv_label_set_text(char_count_lbl, buf);

    uint8_t tier = (len >= chat_limit - 4) ? 2 : (len >= chat_limit * 4 / 5) ? 1 : 0;
    if (tier != char_tier) {
        char_tier = tier;
        uint32_t col = (tier == 2) ? 0xB3261E : (tier == 1) ? 0xE28B00 : 0x747775;
        lv_obj_set_style_text_color(char_count_lbl, lv_color_hex(col), 0);
    }
}

/* ==================== sending ==================== */
static void handle_send_message(void) {
    const char* txt = lv_textarea_get_text(ta_input);
    if (!txt || txt[0] == '\0') return;

    int idx = current_msg_idx;
    LoraEntry* e = lora_send(txt, idx);               /* transmit + add to history */
    chat_seen_seq = lora_next_seq();                  /* our own entry: nothing to pull */
    if (lora_drop_count() != chat_seen_drops) {       /* oldest entry fell off: rebuild next open */
        chat_seen_drops = lora_drop_count();
        chat_dirty = true;
    }
    app_touch_time(idx);

    strncpy(messages[idx].reply, txt, 200);
    messages[idx].reply[200] = '\0';
    messages[idx].has_reply = true;
    snprintf(messages[idx].preview, sizeof(messages[idx].preview), "You: %s", messages[idx].reply);
    inbox_refresh_row(idx);

    make_tx_bubble(messages[idx].reply, e ? tx_status_text(*e) : "Just now");
    if (e) e->lbl = grab_last_bubble_label();
    scroll_bottom();

    lv_textarea_set_text(ta_input, "");
    char_counter_update(0);

    set_keyboard_visible(false);
}

static void kb_send_action(void) { handle_send_message(); }
static void kb_hide_action(void) { set_keyboard_visible(false); }

/* ==================== events ==================== */
static void on_ta_event(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        set_keyboard_visible(true);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        const char* txt = lv_textarea_get_text(ta_input);
        char_counter_update(txt ? (int)strlen(txt) : 0);
    }
}

static void on_send_btn_clicked(lv_event_t* e) {
    handle_send_message();
}

static void on_chat_bg_clicked(lv_event_t* e) {
    lv_indev_t* indev = lv_indev_get_act();
    if (indev && lv_indev_get_scroll_obj(indev) != NULL) return;

    if (kb_shown) set_keyboard_visible(false);
}

static void on_poll_clicked(lv_event_t* e) {
    lora_request_poll();
}

static void on_back_clicked(lv_event_t* e) {
    set_keyboard_visible(false);
    go_inbox();
}

static void set_keyboard_visible(bool visible) {
    if (visible == kb_shown) return;      /* FOCUSED + CLICKED both fire: skip the duplicate relayout */
    kb_shown = visible;

    if (visible) {
        kb_auto_mode_for_text();
        kb_show();
        lv_obj_add_flag(appbar, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_pos(chat_scroll_cont, 8, 2);
        lv_obj_set_size(chat_scroll_cont, LCD_HOR_RES - 16, 102);

        lv_obj_set_pos(input_bar_cont, 8, 106);
        scroll_bottom();
    } else {
        kb_hide();
        lv_obj_clear_flag(appbar, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_pos(chat_scroll_cont, 8, 44);
        lv_obj_set_size(chat_scroll_cont, LCD_HOR_RES - 16, 226);
        lv_obj_set_pos(input_bar_cont, 8, 272);
        lv_obj_clear_state(ta_input, LV_STATE_FOCUSED);
    }
}

/* ==================== open / poll ==================== */
void chat_forget_thread(int slot) {
    if (chat_built_idx == slot) chat_built_idx = -1;      /* forces a rebuild if the slot is reused */
    chat_dirty = true;
    chat_seen_drops = lora_drop_count();
}

void chat_open(int idx) {
    bool thread_changed = (chat_built_idx != idx);
    current_msg_idx = idx;

    if (thread_changed) {
        char init_buf[2] = { messages[idx].sender[0] ? messages[idx].sender[0] : '?', '\0' };
        lv_label_set_text(detail_avatar_lbl, init_buf);
        lv_obj_set_style_bg_color(detail_avatar_box, lv_color_hex(messages[idx].avatar_color), 0);
    }
    lora_refresh_header();                          /* contact name + modem status in the app bar */

    if (chat_dirty || thread_changed) {
        forget_labels();                            /* bubbles are about to be deleted */
        lv_obj_clean(chat_scroll_cont);
        render_content(idx);
        chat_built_idx = idx;
        chat_dirty = false;
        chat_seen_seq = lora_next_seq();
        chat_seen_drops = lora_drop_count();
        chat_seen_result_rev = lora_result_rev();
    } else {
        lv_label_set_text(chat_in_time_lbl, messages[idx].time);
        scroll_bottom();
    }

    /* text + "ID:" tag must fit the modem's payload; slot 0 sends untagged */
    int tl = (int)strlen(app_tag(idx));
    chat_limit = LORA_MAX_PAYLOAD - (tl ? tl + 1 : 0);
    lv_textarea_set_max_length(ta_input, chat_limit);

    kb_attach(scr_detail, ta_input, kb_send_action, kb_hide_action);   /* the keyboard may be on another screen */
    lv_textarea_set_text(ta_input, "");
    char_counter_update(0);
    set_keyboard_visible(false);

    lv_scr_load(scr_detail);
}

void chat_poll(void) {
    uint32_t nseq = lora_next_seq();
    if (nseq != chat_seen_seq) {
        bool appended = false;
        int n = lora_hist_count();
        for (int i = 0; i < n; i++) {
            LoraEntry* e = lora_hist_at(i);
            if (e->seq < chat_seen_seq || e->kind != LORA_RX_MSG) continue;
            int t = e->thread;
            bool vis = chat_thread_visible(t);
            if (vis) { make_rx_bubble(e->text, e->info); appended = true; }
            else if (t == chat_built_idx) chat_dirty = true;     // that chat's cached bubbles are stale
            snprintf(messages[t].preview, sizeof(messages[t].preview), "%s", e->text);
            app_touch_time(t);
            messages[t].unread = !vis;                           // shows as unread in the inbox
            inbox_refresh_row(t);
        }
        if (appended) scroll_bottom();
        if (lora_drop_count() != chat_seen_drops) {
            chat_seen_drops = lora_drop_count();
            chat_dirty = true;
        }
        chat_seen_seq = nseq;
    }

    uint32_t rr = lora_result_rev();
    if (rr != chat_seen_result_rev) {
        chat_seen_result_rev = rr;
        sync_tx_labels();
    }

    const char* cur = lora_modem_status_text();
    if (cur != last_status) {
        last_status = cur;
        if (scr_detail && lv_scr_act() == scr_detail) lora_refresh_header();
    }
}

/* ==================== build ==================== */
void chat_build(void) {
    scr_detail = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_detail, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_anim_time(scr_detail, 0, 0);
    lv_obj_clear_flag(scr_detail, LV_OBJ_FLAG_SCROLLABLE);

    appbar = ui_make_appbar(scr_detail);

    lv_obj_t* btn_back = ui_back_button(appbar, 44, 36, on_back_clicked);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 2, 0);

    detail_avatar_box = lv_obj_create(appbar);
    lv_obj_set_size(detail_avatar_box, 32, 32);
    lv_obj_align(detail_avatar_box, LV_ALIGN_LEFT_MID, 48, 0);
    lv_obj_set_style_radius(detail_avatar_box, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(detail_avatar_box, 0, 0);
    lv_obj_set_style_pad_all(detail_avatar_box, 0, 0);
    lv_obj_set_style_anim_time(detail_avatar_box, 0, 0);

    detail_avatar_lbl = lv_label_create(detail_avatar_box);
    lv_obj_set_style_text_color(detail_avatar_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(detail_avatar_lbl);

    detail_sender_lbl = lv_label_create(appbar);
    lv_obj_align(detail_sender_lbl, LV_ALIGN_LEFT_MID, 88, 0);
    lv_obj_set_style_text_color(detail_sender_lbl, lv_color_hex(0x1F1F1F), 0);
    lv_label_set_long_mode(detail_sender_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(detail_sender_lbl, 320);

    lv_obj_t* btn_poll = lv_btn_create(appbar);
    lv_obj_set_size(btn_poll, 44, 36);
    lv_obj_align(btn_poll, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(btn_poll, lv_color_hex(0xD3E3FD), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn_poll, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_poll, 0, 0);
    lv_obj_set_style_anim_time(btn_poll, 0, 0);
    lv_obj_add_event_cb(btn_poll, on_poll_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_poll = lv_label_create(btn_poll);
    lv_label_set_text(lbl_poll, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(lbl_poll, lv_color_hex(0x1F1F1F), 0);
    lv_obj_center(lbl_poll);

    chat_scroll_cont = lv_obj_create(scr_detail);
    lv_obj_set_size(chat_scroll_cont, LCD_HOR_RES - 16, 226);
    lv_obj_set_pos(chat_scroll_cont, 8, 44);
    lv_obj_set_style_bg_opa(chat_scroll_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chat_scroll_cont, 0, 0);
    lv_obj_set_style_pad_all(chat_scroll_cont, 2, 0);
    lv_obj_set_style_anim_time(chat_scroll_cont, 0, 0);
    lv_obj_set_flex_flow(chat_scroll_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(chat_scroll_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(chat_scroll_cont, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(chat_scroll_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(chat_scroll_cont, on_chat_bg_clicked, LV_EVENT_CLICKED, NULL);

    input_bar_cont = lv_obj_create(scr_detail);
    lv_obj_set_size(input_bar_cont, LCD_HOR_RES - 16, 42);
    lv_obj_set_pos(input_bar_cont, 8, 272);
    lv_obj_set_style_bg_color(input_bar_cont, lv_color_hex(0xEDF2FA), 0);
    lv_obj_set_style_radius(input_bar_cont, 21, 0);
    lv_obj_set_style_border_color(input_bar_cont, lv_color_hex(0xD3E3FD), 0);
    lv_obj_set_style_border_width(input_bar_cont, 1, 0);
    lv_obj_set_style_pad_all(input_bar_cont, 2, 0);
    lv_obj_set_style_anim_time(input_bar_cont, 0, 0);
    lv_obj_clear_flag(input_bar_cont, LV_OBJ_FLAG_SCROLLABLE);

    ta_input = lv_textarea_create(input_bar_cont);
    lv_textarea_set_one_line(ta_input, true);
    lv_textarea_set_max_length(ta_input, 200);
    lv_textarea_set_placeholder_text(ta_input, "Type message...");
    lv_obj_set_size(ta_input, 340, 36);
    lv_obj_align(ta_input, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_bg_opa(ta_input, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ta_input, 0, 0);
    lv_obj_set_style_anim_time(ta_input, 0, 0);
    lv_obj_set_style_anim_time(ta_input, 0, LV_PART_CURSOR);
    /* only the three events the handler uses (was LV_EVENT_ALL) */
    lv_obj_add_event_cb(ta_input, on_ta_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ta_input, on_ta_event, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta_input, on_ta_event, LV_EVENT_VALUE_CHANGED, NULL);

    char_count_lbl = lv_label_create(input_bar_cont);
    lv_label_set_text(char_count_lbl, "0/40");
    lv_obj_align(char_count_lbl, LV_ALIGN_RIGHT_MID, -54, 0);
    lv_obj_set_style_text_color(char_count_lbl, lv_color_hex(0x747775), 0);

    lv_obj_t* btn_send = lv_btn_create(input_bar_cont);
    lv_obj_set_size(btn_send, 46, 36);
    lv_obj_align(btn_send, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_radius(btn_send, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_send, lv_color_hex(0x0B57D0), 0);
    lv_obj_set_style_shadow_width(btn_send, 0, 0);
    lv_obj_set_style_anim_time(btn_send, 0, 0);
    lv_obj_add_event_cb(btn_send, on_send_btn_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t* icon_send = lv_label_create(btn_send);
    lv_label_set_text(icon_send, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(icon_send, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(icon_send);

    /* Keyboard last, so it sits on top */
    kb_create(scr_detail, ta_input, kb_send_action, kb_hide_action);

    chat_dirty = true;
    chat_built_idx = -1;
}

#include "ui_settings.h"
#include "config.h"
#include "lora_modem.h"
#include "ui_nav.h"
#include "ui_common.h"
#include <Arduino.h>
#include <string.h>
#include "esp_heap_caps.h"

enum {
    R_MODEM, R_RADIO, R_TX, R_RX, R_RSSI,            /* LoRa Device: live */
    R_FREQ, R_SF, R_BW, R_PWR, R_FW,                 /* LoRa Device: from INFO: lines ("--" until received) */
    R_UPTIME, R_CPU, R_RAM, R_PSRAM,                 /* System */
    R_COUNT
};

static const char* ROW_NAMES[R_COUNT] = {
    "Modem status", "Radio state", "Messages sent", "Messages received", "Last RSSI",
    "Frequency", "Spreading factor", "Bandwidth", "TX power", "Modem firmware",
    "Uptime", "CPU", "Free internal RAM", "Free PSRAM"
};

struct SettingsRow {
    lv_obj_t* lbl;
    char last[40];          /* text currently shown */
};
static SettingsRow rows[R_COUNT];
static lv_timer_t* settings_timer = NULL;

/* ---------- label updates (only when the text changed) ---------- */
static void put(int r, const char* txt) {
    if (strcmp(rows[r].last, txt) == 0) return;
    snprintf(rows[r].last, sizeof(rows[r].last), "%s", txt);
    lv_label_set_text(rows[r].lbl, txt);
}

/* fmt contains one %s; "--" stays "--" */
static void put_fmt(int r, const char* val, const char* fmt) {
    if (strcmp(val, "--") == 0) { put(r, "--"); return; }
    char b[40];
    snprintf(b, sizeof(b), fmt, val);
    put(r, b);
}

void settings_refresh(void) {
    char b[40];

    put(R_MODEM, lora_modem_status_text());
    int rs = lora_radio_state();
    put(R_RADIO, rs == 1 ? "ready" : rs == 0 ? "no radio" : "unknown");
    snprintf(b, sizeof(b), "%lu", (unsigned long)lora_tx_count()); put(R_TX, b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)lora_rx_count()); put(R_RX, b);
    put_fmt(R_RSSI, lora_last_rssi(), "%s dBm");

    put_fmt(R_FREQ, lora_info_freq(), "%s MHz");
    put_fmt(R_SF,   lora_info_sf(),   "SF%s");
    put_fmt(R_BW,   lora_info_bw(),   "%s kHz");
    put_fmt(R_PWR,  lora_info_pwr(),  "%s dBm");
    put_fmt(R_FW,   lora_info_fw(),   "%s");

    uint32_t s = millis() / 1000;
    snprintf(b, sizeof(b), "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
    put(R_UPTIME, b);
    snprintf(b, sizeof(b), "%u MHz", (unsigned)getCpuFrequencyMhz()); put(R_CPU, b);
    snprintf(b, sizeof(b), "%u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024)); put(R_RAM, b);
    snprintf(b, sizeof(b), "%u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024)); put(R_PSRAM, b);
}

static void settings_timer_cb(lv_timer_t* t) { settings_refresh(); }

void settings_open(void) {
    settings_refresh();
    if (settings_timer) lv_timer_resume(settings_timer);
}

static void on_settings_back(lv_event_t* e) {
    if (settings_timer) lv_timer_pause(settings_timer);    /* no work while the screen is closed */
    go_home();
}

/* ---------- build ---------- */
static lv_obj_t* make_card(lv_obj_t* parent, const char* title) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_width(card, LCD_HOR_RES - 16);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF0F4F9), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xE0E2EC), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_style_anim_time(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(0x0B57D0), 0);
    return card;
}

static void make_row(lv_obj_t* card, int r) {
    lv_obj_t* row = lv_obj_create(card);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* name = lv_label_create(row);
    lv_label_set_text(name, ROW_NAMES[r]);
    lv_obj_set_style_text_color(name, lv_color_hex(0x5F6368), 0);

    rows[r].lbl = lv_label_create(row);
    lv_label_set_text(rows[r].lbl, "--");
    lv_obj_set_style_text_color(rows[r].lbl, lv_color_hex(0x1F1F1F), 0);
    rows[r].last[0] = '\0';
}

void settings_build(void) {
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_anim_time(scr_settings, 0, 0);
    lv_obj_clear_flag(scr_settings, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bar = ui_make_appbar(scr_settings);
    lv_obj_t* btn_back = ui_back_button(bar, 44, 36, on_settings_back);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 2, 0);

    lv_obj_t* title = lv_label_create(bar);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 52, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x1F1F1F), 0);

    lv_obj_t* body = lv_obj_create(scr_settings);
    lv_obj_set_size(body, LCD_HOR_RES, LCD_VER_RES - 44);
    lv_obj_set_pos(body, 0, 44);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 8, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_style_anim_time(body, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* card_lora = make_card(body, "LoRa Device");
    for (int r = R_MODEM; r <= R_FW; r++) make_row(card_lora, r);

    lv_obj_t* card_sys = make_card(body, "System");
    for (int r = R_UPTIME; r <= R_PSRAM; r++) make_row(card_sys, r);

    settings_timer = lv_timer_create(settings_timer_cb, 1000, NULL);
    lv_timer_pause(settings_timer);
}

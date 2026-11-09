#include "ui_keyboard.h"
#include "config.h"
#include <Arduino.h>
#include <string.h>

static lv_obj_t* kb_on_screen = NULL;
static lv_obj_t* kb_ta = NULL;
static kb_action_cb_t cb_send = NULL;
static kb_action_cb_t cb_hide = NULL;

static kb_layout_mode_t current_kb_mode = KB_MODE_UPPER;
static uint32_t last_shift_time = 0;
static uint32_t last_space_time = 0;

enum { KBMAP_NONE = 0, KBMAP_LC, KBMAP_UC, KBMAP_SPEC };
static uint8_t kb_map_applied = KBMAP_NONE;

/* ==================== Layouts ==================== */
static const char* kb_map_lc[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    LV_SYMBOL_UP, "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
    "?123", LV_SYMBOL_KEYBOARD, " ", ".", LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t kb_ctrl_lc[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    3, 2, 2, 2, 2, 2, 2, 2, 3 | LV_BTNMATRIX_CTRL_CHECKED,
    3 | LV_BTNMATRIX_CTRL_CHECKED, 2 | LV_BTNMATRIX_CTRL_CHECKED, 8, 2, 3 | LV_BTNMATRIX_CTRL_CHECKED
};

static const char* kb_map_uc[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    LV_SYMBOL_UP, "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
    "?123", LV_SYMBOL_KEYBOARD, " ", ".", LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t kb_ctrl_uc[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    3 | LV_BTNMATRIX_CTRL_CHECKED, 2, 2, 2, 2, 2, 2, 2, 3 | LV_BTNMATRIX_CTRL_CHECKED,
    3 | LV_BTNMATRIX_CTRL_CHECKED, 2 | LV_BTNMATRIX_CTRL_CHECKED, 8, 2, 3 | LV_BTNMATRIX_CTRL_CHECKED
};

static const char* kb_map_spec[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "@", "#", "$", "%", "&", "-", "+", "(", ")", "/", "\n",
    "=", "*", "\"", "\'", ":", ";", "!", "?", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", LV_SYMBOL_KEYBOARD, " ", ",", LV_SYMBOL_OK, ""
};
static const lv_btnmatrix_ctrl_t kb_ctrl_spec[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 3 | LV_BTNMATRIX_CTRL_CHECKED,
    3 | LV_BTNMATRIX_CTRL_CHECKED, 2 | LV_BTNMATRIX_CTRL_CHECKED, 8, 2, 3 | LV_BTNMATRIX_CTRL_CHECKED
};

/* ==================== Mode switcher ==================== */
void set_kb_mode(kb_layout_mode_t mode) {
    current_kb_mode = mode;
    if (!kb_on_screen) return;

    uint8_t want;
    switch (mode) {
        case KB_MODE_LOWER: want = KBMAP_LC;   break;
        case KB_MODE_NUM:   want = KBMAP_SPEC; break;
        default:            want = KBMAP_UC;   break;   /* UPPER and CAPS share one map */
    }
    if (want == kb_map_applied) return;

    switch (want) {
        case KBMAP_LC:
            lv_btnmatrix_set_map(kb_on_screen, kb_map_lc);
            lv_btnmatrix_set_ctrl_map(kb_on_screen, kb_ctrl_lc);
            break;
        case KBMAP_UC:
            lv_btnmatrix_set_map(kb_on_screen, kb_map_uc);
            lv_btnmatrix_set_ctrl_map(kb_on_screen, kb_ctrl_uc);
            break;
        default:
            lv_btnmatrix_set_map(kb_on_screen, kb_map_spec);
            lv_btnmatrix_set_ctrl_map(kb_on_screen, kb_ctrl_spec);
            break;
    }
    kb_map_applied = want;
}

void kb_auto_mode_for_text(void) {
    const char* txt = lv_textarea_get_text(kb_ta);
    int len = txt ? strlen(txt) : 0;
    if (len == 0 || (len >= 2 && (txt[len - 2] == '.' || txt[len - 2] == '?' || txt[len - 2] == '!') && txt[len - 1] == ' ')) {
        set_kb_mode(KB_MODE_UPPER);
    } else {
        set_kb_mode(KB_MODE_LOWER);
    }
}

void kb_attach(lv_obj_t* parent, lv_obj_t* textarea, kb_action_cb_t on_send, kb_action_cb_t on_hide) {
    kb_ta = textarea;
    cb_send = on_send;
    cb_hide = on_hide;
    if (lv_obj_get_parent(kb_on_screen) != parent) lv_obj_set_parent(kb_on_screen, parent);
    lv_obj_align(kb_on_screen, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb_on_screen, LV_OBJ_FLAG_HIDDEN);
}

void kb_set_textarea(lv_obj_t* textarea) { kb_ta = textarea; }

void kb_show(void) { lv_obj_clear_flag(kb_on_screen, LV_OBJ_FLAG_HIDDEN); }
void kb_hide(void) { lv_obj_add_flag(kb_on_screen, LV_OBJ_FLAG_HIDDEN); }

/* ==================== Fast-path event engine ==================== */
static void on_keyboard_event(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_VALUE_CHANGED) return;

    lv_obj_t* kb = lv_event_get_target(e);
    uint16_t btn_id = lv_btnmatrix_get_selected_btn(kb);
    if (btn_id == LV_BTNMATRIX_BTN_NONE) {
        uint16_t* p = (uint16_t*)lv_event_get_param(e);
        if (p) btn_id = *p;
    }
    if (btn_id == LV_BTNMATRIX_BTN_NONE) return;

    const char* txt = lv_btnmatrix_get_btn_text(kb, btn_id);
    if (!txt || txt[0] == '\0') return;

    /* FAST PATH 1: single characters (letters, digits, punctuation) */
    if (txt[1] == '\0') {
        if (txt[0] == ' ') {
            uint32_t now = millis();
            const char* cur = lv_textarea_get_text(kb_ta);
            int len = cur ? strlen(cur) : 0;

            if ((now - last_space_time < 400) && len > 0 && cur[len - 1] == ' ') {
                lv_textarea_del_char(kb_ta);
                lv_textarea_add_text(kb_ta, ". ");
                last_space_time = 0;

                if (current_kb_mode != KB_MODE_NUM && current_kb_mode != KB_MODE_CAPS) {
                    set_kb_mode(KB_MODE_UPPER);
                }
            } else {
                lv_textarea_add_char(kb_ta, ' ');
                last_space_time = now;
            }
            return;
        }

        lv_textarea_add_char(kb_ta, txt[0]);

        if (current_kb_mode == KB_MODE_UPPER) {
            set_kb_mode(KB_MODE_LOWER);
        }
        return;
    }

    /* FAST PATH 2: multi-byte and function keys */
    if (strcmp(txt, LV_SYMBOL_UP) == 0) {
        uint32_t now = millis();
        if (current_kb_mode == KB_MODE_LOWER) {
            set_kb_mode(KB_MODE_UPPER);
            last_shift_time = now;
        } else if (current_kb_mode == KB_MODE_UPPER) {
            if (now - last_shift_time < 350) {
                set_kb_mode(KB_MODE_CAPS);
            } else {
                set_kb_mode(KB_MODE_LOWER);
            }
            last_shift_time = now;
        } else if (current_kb_mode == KB_MODE_CAPS) {
            set_kb_mode(KB_MODE_LOWER);
            last_shift_time = 0;
        }
        return;
    }

    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_del_char(kb_ta);
        const char* cur = lv_textarea_get_text(kb_ta);
        if ((!cur || cur[0] == '\0') && current_kb_mode == KB_MODE_LOWER) {
            set_kb_mode(KB_MODE_UPPER);
        }
        return;
    }

    if (strcmp(txt, "?123") == 0 || strcmp(txt, "123") == 0) {
        set_kb_mode(KB_MODE_NUM);
        return;
    }

    if (strcmp(txt, "ABC") == 0 || strcmp(txt, "abc") == 0) {
        set_kb_mode(KB_MODE_LOWER);
        return;
    }

    if (strcmp(txt, LV_SYMBOL_OK) == 0) {
        if (cb_send) cb_send();
        return;
    }

    if (strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
        if (cb_hide) cb_hide();
        return;
    }
}

lv_obj_t* kb_create(lv_obj_t* parent, lv_obj_t* textarea, kb_action_cb_t on_send, kb_action_cb_t on_hide) {
    kb_ta = textarea;
    cb_send = on_send;
    cb_hide = on_hide;

    kb_on_screen = lv_btnmatrix_create(parent);
    lv_obj_set_size(kb_on_screen, LCD_HOR_RES, 172);
    lv_obj_align(kb_on_screen, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_anim_time(kb_on_screen, 0, 0);
    lv_obj_clear_flag(kb_on_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_btnmatrix_set_map(kb_on_screen, kb_map_lc);
    lv_btnmatrix_set_ctrl_map(kb_on_screen, kb_ctrl_lc);
    kb_map_applied = KBMAP_LC;

    lv_obj_set_style_bg_color(kb_on_screen, lv_color_hex(0xE9EEF6), 0);
    lv_obj_set_style_bg_opa(kb_on_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(kb_on_screen, 4, 0);
    lv_obj_set_style_pad_row(kb_on_screen, 5, 0);
    lv_obj_set_style_pad_column(kb_on_screen, 4, 0);

    lv_obj_set_style_radius(kb_on_screen, 8, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb_on_screen, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb_on_screen, 1, LV_PART_ITEMS);
    lv_obj_set_style_shadow_color(kb_on_screen, lv_color_hex(0xC4C7C5), LV_PART_ITEMS);
    lv_obj_set_style_shadow_ofs_y(kb_on_screen, 1, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb_on_screen, lv_color_hex(0x1F1F1F), LV_PART_ITEMS);

    lv_obj_set_style_bg_color(kb_on_screen, lv_color_hex(0xD3E3FD), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb_on_screen, lv_color_hex(0x041E49), LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_set_style_bg_color(kb_on_screen, lv_color_hex(0x0B57D0), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb_on_screen, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_PRESSED);

    lv_obj_add_flag(kb_on_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb_on_screen, on_keyboard_event, LV_EVENT_CLICKED, NULL);
    return kb_on_screen;
}

#pragma once
#include <lvgl.h>

typedef enum {
    KB_MODE_LOWER,   /* Lowercase alphabet */
    KB_MODE_UPPER,   /* Single-shot uppercase (reverts to lower after 1 char) */
    KB_MODE_CAPS,    /* Persistent Caps Lock */
    KB_MODE_NUM      /* Numbers & Symbols */
} kb_layout_mode_t;

typedef void (*kb_action_cb_t)(void);

/* Builds the on-screen keyboard inside `parent`, hidden. Typing goes into `textarea`.
   on_send: OK key.  on_hide: keyboard key. */
lv_obj_t* kb_create(lv_obj_t* parent, lv_obj_t* textarea, kb_action_cb_t on_send, kb_action_cb_t on_hide);

void set_kb_mode(kb_layout_mode_t mode);     /* skips the matrix rebuild if the same map is loaded */
void kb_auto_mode_for_text(void);            /* UPPER at start / after ". ? !", else LOWER */
/* The keyboard is a single object. kb_attach() moves it to another screen, retargets typing and the
   OK / keyboard-key callbacks, and hides it. kb_set_textarea() only switches the target field. */
void kb_attach(lv_obj_t* parent, lv_obj_t* textarea, kb_action_cb_t on_send, kb_action_cb_t on_hide);
void kb_set_textarea(lv_obj_t* textarea);
void kb_show(void);
void kb_hide(void);

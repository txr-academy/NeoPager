#pragma once
#include <lvgl.h>

/* Shared widgets so every screen uses the same look */
lv_obj_t* ui_make_appbar(lv_obj_t* parent);                                    /* 480x42 top bar */
lv_obj_t* ui_back_button(lv_obj_t* parent, int w, int h, lv_event_cb_t cb);    /* transparent "<" button */

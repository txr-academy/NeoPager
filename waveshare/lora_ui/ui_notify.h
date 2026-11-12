#pragma once
#include <stdbool.h>

/* New-message notification. With Class C a message can arrive at any moment, on any screen. */
void ui_notify_build(void);                          /* once, after display_init() (the toast lives on lv_layer_top) */
void ui_notify_rx(int thread, bool thread_visible);  /* a chat message arrived: wake the screen, show the toast, pulse ALERT_GPIO */

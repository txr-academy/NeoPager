#pragma once
#include <stdint.h>
/* I2C, IO expander, touch, ST7796 panel, LVGL core, draw buffers, input device. */
void display_init(void);

/* Backlight, 0..100 %. PWM when BACKLIGHT_PWM is 1 (config.h), otherwise anything above 0 means on. */
void display_set_backlight(uint8_t percent);

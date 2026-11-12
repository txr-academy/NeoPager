#include "ui_power.h"
#include "config.h"
#include "display_driver.h"
#include "ui_nav.h"
#include <lvgl.h>

enum { PW_ACTIVE = 0, PW_DIM, PW_OFF };
static uint8_t state = PW_ACTIVE;

static void set_state(uint8_t s) {
    if (s == state) return;
    state = s;
    switch (s) {
        case PW_ACTIVE: display_set_backlight(BL_LEVEL_FULL); break;
        case PW_DIM:    display_set_backlight(BL_LEVEL_DIM);  break;
        case PW_OFF:    display_set_backlight(0);             break;
    }
}

void power_init(void) {
    state = PW_ACTIVE;
    display_set_backlight(BL_LEVEL_FULL);
}

void power_activity(void) {
    lv_disp_trig_activity(NULL);          /* restarts LVGL's inactivity timer */
    set_state(PW_ACTIVE);
}

bool power_screen_off(void) { return state == PW_OFF; }

bool power_consume_wake_touch(void) {
    if (state != PW_OFF) return false;    /* dim: the touch works normally and brightens the screen via LVGL activity */
    power_activity();
    return true;
}

void power_tick(void) {
    uint32_t idle = lv_disp_get_inactive_time(NULL);

    if (state != PW_ACTIVE && idle < SCREEN_DIM_MS) {      /* activity while dim (touch) */
        set_state(PW_ACTIVE);
        return;
    }

    if (SCREEN_OFF_MS && idle >= SCREEN_OFF_MS) {
        if (state != PW_OFF) {
            set_state(PW_OFF);
#if RELOCK_ON_SCREEN_OFF
            go_home();                                      /* Messages needs the PIN again */
#endif
        }
    } else if (SCREEN_DIM_MS && idle >= SCREEN_DIM_MS && state == PW_ACTIVE) {
        set_state(PW_DIM);
    }
}

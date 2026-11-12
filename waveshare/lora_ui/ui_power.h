#pragma once
#include <stdbool.h>

/* Screen power. With LoRaWAN Class C the Heltec stays awake and listening all the time, but the
   display does not have to: it dims, turns off, and wakes on a touch or a received message.
   Messages stays locked: when the screen turns off we return to Home, so the PIN is asked again. */

void power_init(void);                 /* after display_init() */
void power_tick(void);                 /* every loop(): idle -> dim -> off (+ relock) */
void power_activity(void);             /* wake / restart the idle timer (touch, received message) */
bool power_screen_off(void);
bool power_consume_wake_touch(void);   /* touch driver: true if this touch only woke a sleeping screen (swallow it) */

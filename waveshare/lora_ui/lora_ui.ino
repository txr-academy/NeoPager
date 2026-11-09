/* LoRa Messenger UI - Waveshare ESP32-S3-Touch-LCD-3.5 + LVGL 8.x
   setup() and loop() only. Everything else lives in the .h/.cpp pairs next to this file. */
#include <lvgl.h>
#include "config.h"
#include "app_data.h"
#include "display_driver.h"
#include "lora_modem.h"
#include "ui_nav.h"
#include "ui_home.h"
#include "ui_settings.h"
#include "ui_pin.h"
#include "ui_inbox.h"
#include "ui_chat.h"
#include "ui_contacts.h"

void setup() {
  Serial.begin(115200);
  app_data_init();       /* opens flash, loads contacts */
  lora_history_load();   /* restores the last 5 messages of every contact; must run before the UI is built */

  display_init();

  home_build();
  settings_build();
  pin_build();
  chat_build();     /* before inbox: keeps the original build order */
  inbox_build();
  contacts_build();
  lv_scr_load(scr_home);

  lora_init();

#if LV_MEM_CUSTOM == 0
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  Serial.printf("[LVGL] heap used %u%% (%u of %u bytes)\n",
                (unsigned)mon.used_pct,
                (unsigned)(mon.total_size - mon.free_size),
                (unsigned)mon.total_size);
#endif

  Serial.println("Setup done");
}

void loop() {
  lora_poll();          /* UART + TX timeouts (no LVGL) */
  chat_poll();          /* pulls new modem data into the inbox row / chat view */
  lv_timer_handler();
  delay(1);
}

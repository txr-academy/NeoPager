/* Heltec LoRa 32 V3 modem for the Waveshare LoRa display  --  LoRaWAN Class C (v2).
   setup() and loop() only; see the other files in this folder. */
/* Included here only so the Arduino IDE adds the Heltec library to the build
   (library search follows the main .ino). The code that uses it is in lorawan_node.cpp. */
#include "LoRaWan_APP.h"
#include "app_config.h"
#include "board.h"
#include "display_link.h"
#include "usb_test.h"
#include "lorawan_node.h"

void setup()
{
  Serial.begin(115200);
  lorawan_begin();                      // Mcu.begin
  oledOff();                            // after Mcu.begin so it stays off
  display_link_begin();
  Serial.println("Pager modem v2: LoRaWAN Class C (always listening)");
}

void loop()
{
  display_link_poll();                  // always, non-blocking; also flushes queued downlinks
  usb_test_poll();
  lorawan_update_idle_flag();
  display_link_tick();                  // heartbeat + INFO
  lorawan_step();
}

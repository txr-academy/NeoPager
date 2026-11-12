#include "board.h"
#include "app_config.h"
#include <Arduino.h>

void oledOff(void) {
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, CUT_VEXT ? HIGH : LOW);
}

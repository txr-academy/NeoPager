#include "usb_test.h"
#include "display_link.h"
#include <Arduino.h>

static String usbLine = "";

void usb_test_poll(void) {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      usbLine.trim();
      if (usbLine.length()) {
        if (usbLine.startsWith("TX:") || usbLine.startsWith("SIM:") || usbLine.startsWith("CMD:"))
          display_link_handle_line(usbLine);
        else
          display_link_handle_line("TX:" + usbLine);
      }
      usbLine = "";
    } else if (c != '\r' && usbLine.length() < 300) {
      usbLine += c;
    }
  }
}

#include "usb_test.h"
#include "display_link.h"
#include <Arduino.h>
#include <string.h>

static char usbLine[304];
static size_t usbLen = 0;

void usb_test_poll(void) {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      usbLine[usbLen] = '\0';
      if (usbLen) {
        if (!strncmp(usbLine, "TX:", 3) || !strncmp(usbLine, "TX2:", 4) ||
            !strncmp(usbLine, "SIM:", 4) || !strncmp(usbLine, "CMD:", 4)) {
          display_link_handle_line(usbLine);
        } else {
          char b[320];
          snprintf(b, sizeof(b), "TX:%s", usbLine);
          display_link_handle_line(b);
        }
      }
      usbLen = 0;
    } else if (c != '\r' && usbLen < sizeof(usbLine) - 1) {
      usbLine[usbLen++] = c;
    }
  }
}

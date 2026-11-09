#pragma once
/* Test console on the USB Serial Monitor (115200, line ending "Newline").
   Plain text -> sent as an uplink | SIM:hello -> fake receive on the display | CMD:STATUS */
void usb_test_poll(void);

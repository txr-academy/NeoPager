#pragma once
/* Test console on the USB Serial Monitor (115200, line ending "Newline").
   Plain text -> queued as an uplink | TX2:<id>:<text> | SIM:hello -> fake receive | CMD:INFO */
void usb_test_poll(void);

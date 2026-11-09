#pragma once
#include <Arduino.h>

/* UART protocol with the Waveshare display (line based, '\n' terminated)
     display -> here : TX:<text> | SIM:<text> | CMD:STATUS
     here -> display : RX:<rssi>,<text> | STATUS:... | INFO:... | LOG:... */

void display_link_begin(void);                  /* Serial1 + first STATUS:JOINING */
void display_link_poll(void);                   /* non-blocking UART read */
void display_link_tick(void);                   /* periodic heartbeat */
void display_link_heartbeat_now(void);          /* send a heartbeat immediately */
void display_link_handle_line(String line);     /* also used by the USB test console */

void sendLine(const String &s);
void logLine(const String &s);                  /* USB serial + LOG: line to the display */
void sanitize(String &s);                       /* non-printable -> space */

#pragma once
#include <Arduino.h>

/* State shared between the LoRaWAN module and the display link */
extern String pendingMsg;
extern bool txPending;          /* a message from the display is waiting to be sent */
extern bool joined;
extern bool wasIdle;            /* in SEND state with nothing to do: ready for a new message */
extern unsigned long uplinkCount, downlinkCount, uartLines;
extern bool pollPending;       /* display asked for a poll uplink (opens the receive windows) */

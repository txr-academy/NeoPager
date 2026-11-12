#pragma once
#include <Arduino.h>
#include "app_config.h"

/* State shared between the LoRaWAN module and the display link */

struct TxItem {
  uint16_t id;                     /* 0 = legacy "TX:" line without an id */
  char     text[MAX_MSG_BYTES + 1];
};

/* Small FIFO of messages typed on the display, drained by the LoRaWAN state machine */
bool txq_push(uint16_t id, const char* text);   /* false when full */
bool txq_pop(TxItem* out);
int  txq_count(void);

/* Delivery receipts: the relay acknowledges with CRC-8 of the payload we sent (no message id travels over the air) */
uint8_t crc8(const char* s);                               /* poly 0x07, init 0 */
void    ack_record(uint16_t id, const char* payload);      /* remember a sent chat message (id != 0) */
bool    ack_match(uint8_t crc, uint16_t* id);              /* oldest un-acked message with that CRC; consumes it */

extern bool joined;
extern bool wasIdle;            /* in SEND state with nothing to do: radio is listening (Class C) */
extern unsigned long uplinkCount, downlinkCount, uartLines;
extern int  lastRssi;           /* RSSI of the last application downlink */

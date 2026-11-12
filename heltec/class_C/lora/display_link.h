#pragma once
#include <Arduino.h>

/* UART protocol with the Waveshare display (line based, '\n' terminated)  -- protocol revision 2

   display -> here
     TX2:<id>:<text>     queue a chat message. <id> = 1..65535 chosen by the display.
                         <text> is "TAG:body" for a contact, plain body for the untagged thread.
     TX:<text>           legacy form (no id); still accepted, answered with id-less STATUS lines
     SIM:<text>          test: pretend <text> was received
     CMD:INFO            send INFO: now   (CMD:STATUS is an alias)
     CMD:POLL            obsolete in Class C; answered with STATUS:POLLFAIL

   here -> display
     RX:<rssi>,<text>               application downlink (chat message)
     STATUS:QUEUED:<id>             accepted into the TX queue
     STATUS:SENT:<id>               handed to the LoRaWAN stack (uplink started)
     STATUS:ACK:<id>                delivery receipt from the relay (downlink "#A:<crc8 hex of the sent payload>")
     STATUS:FAIL:<id>               rejected (not joined / queue full / empty)
     STATUS:JOINING | LISTENING | BUSY     heartbeat every HEARTBEAT_MS
     STATUS:POLLFAIL                reply to the obsolete CMD:POLL
     INFO:PROTO=2,CLASS=C,NET=..,REG=..,RX2=..,DR=..,JOIN=..,UL=..,DL=..,UPT=..,EUI=..,FW=..
     LOG:...                        diagnostics (USB monitor only on the display side) */

void display_link_begin(void);                  /* Serial1 + first STATUS:JOINING */
void display_link_poll(void);                   /* non-blocking UART read + flush of queued output */
void display_link_tick(void);                   /* periodic heartbeat + INFO */
void display_link_heartbeat_now(void);          /* send a heartbeat immediately */
void display_link_info_now(void);               /* send INFO: immediately */
void display_link_handle_line(const char* line);/* also used by the USB test console */

void sendLine(const char* s);                   /* immediate; call from loop context only */
void sendLineDeferred(const char* s);           /* queued; safe from the LoRaWAN downlink callback */
void send_status(const char* what, uint16_t id);/* "STATUS:what" or "STATUS:what:id" (id 0 = legacy, no id) */
void logf(const char* fmt, ...);                /* USB serial + LOG: line to the display */
void sanitize(char* s);                         /* non-printable -> space */

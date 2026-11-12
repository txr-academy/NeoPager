#pragma once
#include "app_types.h"

/* Modem layer: UART parsing, history, counters, status. Makes NO LVGL calls.
   The UI pulls from it (sequence numbers / revision counters / getters).

   Protocol 2 (line based, '\n' terminated) -- Heltec firmware v2, LoRaWAN Class C

     board -> modem   TX2:<id>:<text>      queue a message; <text> = "TAG:body" or plain body
                      CMD:INFO             ask for an INFO: line now
                      TX:<text>            legacy (LORA_PROTO_DEFAULT 1)
                      CMD:POLL             Class A only (obsolete in Class C)
     modem -> board   RX:<rssi>,<body>     chat message (body may start with "TAG:")
                      STATUS:QUEUED:<id> | SENT:<id> | ACK:<id> | FAIL:<id>
                      STATUS:JOINING | LISTENING | BUSY | NORADIO | POLLFAIL      (heartbeat every ~3 s)
                      INFO:PROTO=2,CLASS=C,NET=..,REG=..,RX2=..,DR=..,JOIN=..,UL=..,DL=..,UPT=..,EUI=..,FW=..
                      LOG:...              (USB serial only)
*/

void lora_init(void);                 /* Serial1 begin + first CMD:INFO */
void lora_poll(void);                 /* call every loop(): UART read, TX timeouts */

/* History (oldest first) */
int        lora_hist_count(void);
LoraEntry* lora_hist_at(int i);
LoraEntry* lora_find_seq(uint32_t seq);              /* NULL if that entry has scrolled out of history */
LoraEntry* lora_send(const char* txt, int thread);   /* sanitise, transmit (tagged for contacts), add TX entry in SENDING state */
bool       lora_resend(LoraEntry* e);                /* retry a FAILED entry in place (new id); false if it is not FAILED */

/* Flash history: call once in setup() right after app_data_init(), before the UI is built */
void lora_history_load(void);               /* restores the last LORA_PER_THREAD messages of every contact */
void lora_purge_thread(int thread);         /* drops a removed contact's messages from RAM and flash */

/* Change tracking for the UI */
uint32_t lora_next_seq(void);         /* seq the next entry will get; changes when history grows */
uint32_t lora_drop_count(void);       /* entries dropped off the front so far */
uint32_t lora_result_rev(void);       /* bumps whenever a TX entry changes state */

/* Link status */
const char* lora_modem_status_text(void);     /* "listening", "joining network", "busy", "modem silent"...; string literals (pointer-comparable) */
int         lora_link_level(void);            /* 0 unknown (grey), 1 ok (green), 2 working (amber), 3 problem (red) */
int         lora_radio_state(void);           /* -1 unknown, 0 no radio, 1 ready */
bool        lora_class_is_c(void);            /* modem reports (or is assumed to be) Class C */
bool        lora_poll_supported(void);        /* true only in Class A: the poll button is useful there */
bool        lora_request_poll(void);          /* Class A: asks the modem for a poll uplink; false if one is already running */
void        lora_request_info(void);          /* sends CMD:INFO; the reply refreshes the getters below */

/* Counters */
uint32_t    lora_rx_count(void);
uint32_t    lora_tx_count(void);
const char* lora_last_rssi(void);             /* raw text, "--" if none yet */
int32_t     lora_last_rx_age_s(void);         /* seconds since the last chat message, -1 if none since boot */

/* Values reported by INFO: lines ("--" until received) */
const char* lora_info_class(void);            /* "A" / "C" */
const char* lora_info_net(void);              /* BUILTIN / TTN ... */
const char* lora_info_region(void);
const char* lora_info_rx2(void);              /* where Class C listens */
const char* lora_info_dr(void);
const char* lora_info_join(void);             /* "yes" / "no" / "--" */
const char* lora_info_ul(void);               /* modem's uplink count */
const char* lora_info_dl(void);               /* modem's downlink count */
const char* lora_info_eui(void);              /* DevEUI to register in the network server */
const char* lora_info_fw(void);
int         lora_info_proto(void);            /* protocol revision in use (LORA_PROTO_DEFAULT until INFO says otherwise) */

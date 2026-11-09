#pragma once
#include "app_types.h"

/* Modem layer: UART parsing, history, counters, status. Makes NO LVGL calls.
   The UI pulls from it (sequence numbers / revision counters / getters).

   Protocol (line based, '\n' terminated):
     board -> modem   TX:<text>
     modem -> board   RX:<rssi>,<body>
     modem -> board   STATUS:SENT | STATUS:FAIL | STATUS:READY | STATUS:NORADIO...
     modem -> board   INFO:FREQ=<MHz>,SF=<n>,BW=<kHz>,PWR=<dBm>,FW=<text>   (any subset, any order)
     modem -> board   LOG:...                                              (USB serial only)

   Heltec side (Arduino, send once at boot and every ~10 s so the board catches it):
     Serial1.printf("INFO:FREQ=%.1f,SF=%d,BW=%d,PWR=%d,FW=%s\n", 868.1, 7, 125, 14, "1.0");
*/

void lora_init(void);                 /* Serial1 begin */
void lora_poll(void);                 /* call every loop(): UART read, TX timeouts */

/* History (oldest first) */
int        lora_hist_count(void);
LoraEntry* lora_hist_at(int i);
LoraEntry* lora_send(const char* txt, int thread);   /* sanitise, transmit (tagged for contacts), add TX entry in SENDING state */
bool lora_request_poll(void);

/* Flash history: call once in setup() right after app_data_init(), before the UI is built */
void lora_history_load(void);               /* restores the last LORA_PER_THREAD messages of every contact */
void lora_purge_thread(int thread);         /* drops a removed contact's messages from RAM and flash */   /* asks the Heltec to send a poll uplink; false if one is already running */

/* Change tracking for the UI */
uint32_t lora_next_seq(void);         /* seq the next entry will get; changes when history grows */
uint32_t lora_drop_count(void);       /* entries dropped off the front so far */
uint32_t lora_result_rev(void);       /* bumps whenever a TX entry changes state */

/* Status / counters */
const char* lora_modem_status_text(void);     /* returns string literals (pointer-comparable) */
int         lora_radio_state(void);           /* -1 unknown, 0 no radio, 1 ready */
uint32_t    lora_rx_count(void);
uint32_t    lora_tx_count(void);
const char* lora_last_rssi(void);             /* raw text, "--" if none yet */

/* Radio parameters reported by INFO: lines ("--" until received) */
const char* lora_info_freq(void);
const char* lora_info_sf(void);
const char* lora_info_bw(void);
const char* lora_info_pwr(void);
const char* lora_info_fw(void);

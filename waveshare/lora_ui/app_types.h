#pragma once
#include <Arduino.h>
#include <lvgl.h>

/* TX life cycle:  SENDING (line written to the modem) -> QUEUED (modem accepted it) -> SENT (uplink started)
                   -> DELIVERED (relay receipt, optional).  Any step can end in FAILED. */
enum LoraKind { LORA_RX_MSG, LORA_TX_SENDING, LORA_TX_QUEUED, LORA_TX_SENT, LORA_TX_DELIVERED, LORA_TX_FAILED };

struct LoraEntry {
    LoraKind kind;
    char text[201];
    char info[24];        /* e.g. "RSSI -70 dBm" for received messages */
    uint32_t t_ms;        /* creation time, used for the TX timeout */
    uint32_t seq;         /* ever-increasing id, assigned by lora_modem */
    uint16_t id;          /* message id sent to the modem (TX2:<id>:...), 1..65535; 0 for received / restored entries */
    uint8_t thread;       /* which chat this entry belongs to */
    lv_obj_t* lbl;        /* status label of the TX bubble (NULL if not on screen). Owned by ui_chat. */
};

struct MessageItem {
    const char* sender;
    char preview[100];
    const char* full_body;
    const char* time;
    bool unread;
    uint32_t avatar_color;
    char reply[201];
    bool has_reply;
};

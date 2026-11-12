#pragma once

/* ================= Display link (UART) ================= */
#define MODEM_RX_PIN 5      // Heltec receives  <- Waveshare GPIO43
#define MODEM_TX_PIN 4      // Heltec transmits -> Waveshare GPIO44
#define MODEM_BAUD   115200

/* ================= OLED off ================= */
#define VEXT_PIN 36         // Vext: LOW = on, HIGH = off
#define OLED_RST 21
#define CUT_VEXT 1          // if joining stops working, set to 0

/* ================= Firmware identity ================= */
#define FW_VERSION     "2.0-C"
#define PROTO_VERSION  2    // UART protocol revision, reported in INFO:PROTO=

/* ================= Network (informational, shown on the display) =================
   These strings only describe your setup to the UI. The real values are set in the
   Arduino Tools menu (region), the network server (RX2) and the library (data rate). */
#define NET_NAME       "BUILTIN"          // BUILTIN = WisGate built-in server, TTN, CHIRPSTACK ...
#ifndef REGION_NAME
  #if defined(REGION_IN865)
    #define REGION_NAME "IN865"
  #elif defined(REGION_EU868)
    #define REGION_NAME "EU868"
  #elif defined(REGION_US915)
    #define REGION_NAME "US915"
  #elif defined(REGION_AS923)
    #define REGION_NAME "AS923"
  #elif defined(REGION_AU915)
    #define REGION_NAME "AU915"
  #else
    #define REGION_NAME "?"
  #endif
#endif
#define RX2_INFO       "866.55MHz SF10"   // Class C listens here. MUST match the server's RX2 setting
#define UPLINK_DR      3                  // matches LoRaWAN.setDefaultDR(3) in lorawan_node.cpp (IN865: SF9/125k)

/* ================= Message handling ================= */
#define MAX_MSG_BYTES 40    // must equal LORA_MAX_PAYLOAD in the display's config.h
#define TX_QUEUE_LEN  4     // messages accepted while a previous one is still going out
#define TX_GUARD_MS   2500  // after an uplink: TX + RX1 window. (Class A used 8000 for TX + RX1 + RX2)
#define MSG_PORT      2     // chat messages (uplink)
#define HELLO_PORT    3     // 1-byte "hello"/keep-alive uplink: opens Class C downlink scheduling
#define HELLO_REPEAT_MS 30000UL   // send the hello a second time after join (0 = off), in case the first was lost
#define KEEPALIVE_MS  3600000UL   // idle keep-alive uplink every hour (0 = off)
#define ACK_PREFIX    "#A:" // downlink "#A:<2 hex digits>" = delivery receipt from the relay (not shown as a message).
                            // The hex is CRC-8 (poly 0x07) of the uplink payload "TAG:text"; we remember the last ACK_RING sent messages.
#define ACK_RING      8

/* ================= Timing ================= */
#define HEARTBEAT_MS  3000  // STATUS:JOINING / LISTENING / BUSY
#define INFO_MS       10000 // INFO:KEY=VAL,... for the Settings screen

/* ================= Class C service loop =================
   1 = while idle keep calling LoRaWAN.sleep(loraWanClass), the library's own Class C service path.
   0 = call Radio.IrqProcess() directly instead.
   If a message typed on the display is accepted late while idle, switch this to 0. */
#define SERVICE_VIA_SLEEP 1

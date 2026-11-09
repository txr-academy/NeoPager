#pragma once

/* ================= Display link (UART) ================= */
#define MODEM_RX_PIN 5      // Heltec receives  <- Waveshare GPIO43
#define MODEM_TX_PIN 4      // Heltec transmits -> Waveshare GPIO44
#define MODEM_BAUD   115200

/* ================= OLED off ================= */
#define VEXT_PIN 36         // Vext: LOW = on, HIGH = off
#define OLED_RST 21
#define CUT_VEXT 1          // if joining stops working, set to 0

/* ================= Message handling ================= */
#define MAX_MSG_BYTES 40    // keep small: TTN fair use allows ~30 s airtime/day
#define SETTLE_MS     8000  // time in library sleep after a send (TX + RX windows)
#define HEARTBEAT_MS  3000
#define POLL_PORT 3        // poll uplinks use their own port so your app can ignore them

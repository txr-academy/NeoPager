#include "display_link.h"
#include "app_config.h"
#include "node_state.h"
#include "lorawan_node.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static char uartLine[304];
static size_t uartLen = 0;
static unsigned long lastBeat = 0, lastInfo = 0;

/* ---------- deferred output (the downlink callback must not block on the UART) ---------- */
#define OUTQ_LEN      8
#define OUT_LINE_MAX  128
static char outq[OUTQ_LEN][OUT_LINE_MAX];
static uint8_t oq_head = 0, oq_n = 0;

void sanitize(char* s) {
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if (c < 32 || c > 126) *s = ' ';
  }
}

void sendLine(const char* s) {
  Serial1.print(s);
  Serial1.print("\n");
}

void sendLineDeferred(const char* s) {
  if (oq_n >= OUTQ_LEN) return;                       /* drop rather than block */
  char* dst = outq[(oq_head + oq_n) % OUTQ_LEN];
  strncpy(dst, s, OUT_LINE_MAX - 1);
  dst[OUT_LINE_MAX - 1] = '\0';
  oq_n++;
}

static void flush_outq(void) {
  while (oq_n > 0) {
    sendLine(outq[oq_head]);
    oq_head = (oq_head + 1) % OUTQ_LEN;
    oq_n--;
  }
}

void send_status(const char* what, uint16_t id) {
  char b[40];
  if (id) snprintf(b, sizeof(b), "STATUS:%s:%u", what, (unsigned)id);
  else    snprintf(b, sizeof(b), "STATUS:%s", what);
  sendLine(b);
}

void logf(const char* fmt, ...) {
  char m[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(m, sizeof(m), fmt, ap);
  va_end(ap);
  Serial.println(m);
  sanitize(m);
  if (strlen(m) > 120) m[120] = '\0';
  char l[168];
  snprintf(l, sizeof(l), "LOG:%s", m);
  sendLine(l);
}

/* ---------- heartbeat / info ---------- */
static void sendHeartbeat(void) {
  if (!joined)                              sendLine("STATUS:JOINING");
  else if (wasIdle && txq_count() == 0)     sendLine("STATUS:LISTENING");
  else                                      sendLine("STATUS:BUSY");
}

void display_link_heartbeat_now(void) {
  sendHeartbeat();
  lastBeat = millis();
}

void display_link_info_now(void) {
  char b[220];
  snprintf(b, sizeof(b),
           "INFO:PROTO=%d,CLASS=%c,NET=%s,REG=%s,RX2=%s,DR=%d,JOIN=%d,UL=%lu,DL=%lu,UPT=%lu,EUI=%s,FW=%s",
           PROTO_VERSION, lorawan_class_char(), NET_NAME, REGION_NAME, RX2_INFO, UPLINK_DR,
           joined ? 1 : 0, uplinkCount, downlinkCount, millis() / 1000UL, lorawan_eui_text(), FW_VERSION);
  sendLine(b);
  lastInfo = millis();
}

void display_link_tick(void) {
  unsigned long now = millis();
  if (now - lastBeat >= HEARTBEAT_MS) display_link_heartbeat_now();
  if (now - lastInfo >= INFO_MS)      display_link_info_now();
}

/* ---------- incoming lines ---------- */
static void handle_tx(uint16_t id, const char* payload) {
  char m[MAX_MSG_BYTES + 64];
  strncpy(m, payload, sizeof(m) - 1);
  m[sizeof(m) - 1] = '\0';
  sanitize(m);

  /* trim */
  char* s = m;
  while (*s == ' ') s++;
  size_t n = strlen(s);
  while (n && s[n - 1] == ' ') s[--n] = '\0';

  if (n == 0)                { send_status("FAIL", id); return; }
  if (n > MAX_MSG_BYTES)     { s[MAX_MSG_BYTES] = '\0'; }

  if (!joined) {
    Serial.println("TX rejected (not joined)");
    send_status("FAIL", id);
    return;
  }
  if (!txq_push(id, s)) {
    Serial.println("TX rejected (queue full)");
    send_status("FAIL", id);
    return;
  }
  send_status("QUEUED", id);
}

void display_link_handle_line(const char* raw) {
  char line[304];
  strncpy(line, raw, sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';

  /* trim both ends */
  char* l = line;
  while (*l == ' ' || *l == '\r' || *l == '\t') l++;
  size_t n = strlen(l);
  while (n && (l[n - 1] == ' ' || l[n - 1] == '\r' || l[n - 1] == '\t')) l[--n] = '\0';
  if (n == 0) return;

  uartLines++;
  Serial.print("[UART from display] ");
  Serial.println(l);

  if (strncmp(l, "TX2:", 4) == 0) {
    char* p = l + 4;
    char* colon = strchr(p, ':');
    long id = 0;
    if (colon) { *colon = '\0'; id = atol(p); }
    if (!colon || id < 1 || id > 65535) { send_status("FAIL", 0); return; }
    handle_tx((uint16_t)id, colon + 1);
  } else if (strncmp(l, "TX:", 3) == 0) {
    handle_tx(0, l + 3);
  } else if (strncmp(l, "SIM:", 4) == 0) {            /* display test: fake receive */
    char t[120];
    strncpy(t, l + 4, sizeof(t) - 1);
    t[sizeof(t) - 1] = '\0';
    sanitize(t);
    char o[140];
    snprintf(o, sizeof(o), "RX:-72,%s", t);
    sendLine(o);
  } else if (strcmp(l, "CMD:INFO") == 0 || strcmp(l, "CMD:STATUS") == 0) {
    display_link_info_now();
    display_link_heartbeat_now();
  } else if (strcmp(l, "CMD:POLL") == 0) {
    sendLine("STATUS:POLLFAIL");                       /* Class C: nothing to poll */
  }
}

void display_link_poll(void) {
  flush_outq();                                        /* downlinks queued by the LoRaWAN callback */
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n') {
      uartLine[uartLen] = '\0';
      display_link_handle_line(uartLine);
      uartLen = 0;
    } else if (c != '\r' && c >= 32 && c < 127 && uartLen < sizeof(uartLine) - 1) {
      uartLine[uartLen++] = c;
    }
  }
}

void display_link_begin(void) {
  Serial1.setRxBufferSize(1024);        // must be before begin()
  Serial1.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  sendLine("STATUS:JOINING");
}

#include "display_link.h"
#include "app_config.h"
#include "node_state.h"

static String uartLine = "";
static unsigned long lastBeat = 0;

void sanitize(String &s) {
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (c < 32 || c > 126) s.setCharAt(i, ' ');
  }
}

void sendLine(const String &s) {
  Serial1.print(s);
  Serial1.print("\n");
  Serial1.flush();               // push it out before any sleep
}

void logLine(const String &s) {
  Serial.println(s);
  String m = s;
  sanitize(m);
  if (m.length() > 120) m = m.substring(0, 120);
  sendLine("LOG:" + m);
}

static void sendHeartbeat() {
  if (!joined)                    sendLine("STATUS:JOINING");
  else if (wasIdle && !txPending) sendLine("STATUS:READY");
  else                            sendLine("STATUS:BUSY");
}

void display_link_heartbeat_now(void) {
  sendHeartbeat();
  lastBeat = millis();
}

void display_link_tick(void) {
  if (millis() - lastBeat >= HEARTBEAT_MS) {
    display_link_heartbeat_now();
  }
}

void display_link_handle_line(String line) {
  line.trim();
  if (line.length() == 0) return;
  uartLines++;
  Serial.print("[UART from display] ");
  Serial.println(line);

  if (line.startsWith("TX:")) {
    String m = line.substring(3);
    sanitize(m);
    m.trim();
    if (m.length() == 0) { sendLine("STATUS:FAIL"); return; }
    if (m.length() > MAX_MSG_BYTES) m = m.substring(0, MAX_MSG_BYTES);

    if (joined && wasIdle && !txPending && !pollPending) {
      pendingMsg = m;
      txPending = true;
      sendLine("STATUS:QUEUED");
    } else {
      Serial.println("TX rejected (joining or busy)");
      sendLine("STATUS:FAIL");       // was STATUS:BUSY: lets the display show "Send failed"
    }
  } else if (line.startsWith("SIM:")) {        // display test: fake receive
    String t = line.substring(4);
    sanitize(t);
    sendLine("RX:-72," + t);
  } else if (line == "CMD:STATUS") {
    sendLine("INFO:joined:" + String(joined ? "Y" : "N") +
             " ul:" + String(uplinkCount) +
             " dl:" + String(downlinkCount) +
             " up:" + String(millis() / 1000) + "s");
  } else if (line == "CMD:POLL") {
    if (joined && wasIdle && !txPending && !pollPending) {
      pollPending = true;
      sendLine("STATUS:POLLING");
    } else {
      Serial.println("POLL rejected (joining or busy)");
      sendLine("STATUS:POLLFAIL");
    }
  }
}

void display_link_poll(void) {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n') {
      display_link_handle_line(uartLine);
      uartLine = "";
    } else if (c != '\r' && c >= 32 && c < 127 && uartLine.length() < 300) {
      uartLine += c;
    }
  }
}

void display_link_begin(void) {
  Serial1.setRxBufferSize(1024);        // must be before begin()
  Serial1.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  sendLine("STATUS:JOINING");
}

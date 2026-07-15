#include <RadioLib.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Keypad.h>
#include <Preferences.h>

// ── CHANGE THIS PER BOARD ──
#define DEVICE_ID  2   

// ── Heltec V3 internal pins ──
#define LORA_NSS   8
#define LORA_SCK   9
#define LORA_MOSI  10
#define LORA_MISO  11
#define LORA_RST   12
#define LORA_BUSY  13
#define LORA_DIO1  14
#define OLED_SDA   17
#define OLED_SCL   18
#define OLED_RST   21
#define VEXT_CTRL  36

// ── LoRa settings ──
#define LORA_FREQ  866.0
#define LORA_BW    125.0
#define LORA_SF    7
#define LORA_CR    5
#define LORA_SYNC  0x12
#define LORA_PWR   14

// ── Keypad ──
const byte ROWS = 4, COLS = 4;
char keyMap[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {38, 1, 2, 3};
byte colPins[COLS] = {4, 5, 6, 7};
Keypad keypad = Keypad(makeKeymap(keyMap), rowPins, colPins, ROWS, COLS);

// ── Instant messages ──
const char* MSG_A = "I need help, please come now";
const char* MSG_B = "I am at the meeting";
const char* MSG_C = "Where are you?";
const char* MSG_D = "Task done, acknowledged";

// ── Templates ──
const char* TMPL_1_PRE = "Will be back in ";
const char* TMPL_1_SUF = " min";
const char* TMPL_2_PRE = "Meet me at floor ";
const char* TMPL_2_SUF = "";
const char* TMPL_3_PRE = "Call me in ";
const char* TMPL_3_SUF = " min";

// ── Message history ──
#define HISTORY_SIZE 15
String msgHistory[HISTORY_SIZE];
String msgSender[HISTORY_SIZE];

// ── Objects ──
SPIClass loraSPI(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
Preferences prefs;   // persistent storage for message history (survives power loss)

// ── State ──
volatile bool radioFlag  = false;
bool templateMode        = false;
bool historyMode         = false;
bool alertMode           = false;
char activeTemplate      = 0;
String typedNumber       = "";
int  historySelected     = 0;
int  historyTop          = 0;   // index of the topmost visible row (window into 15 items)

// ── Message/ACK handshake state ──
uint16_t nextMsgId        = 1;
bool     ackPending       = false;
uint16_t pendingMsgId     = 0;
String   pendingWireMsg   = "";
String   pendingDisplayMsg = "";
unsigned long pendingSentTime = 0;
int      pendingRetries   = 0;
const unsigned long ACK_TIMEOUT_MS = 3000;   // wait 3s before retrying
const int MAX_RETRIES = 2;
String lastRxMessage     = "";
String lastRxSender      = "";
uint16_t lastRxMsgId     = 0;

void IRAM_ATTR onRadio() { radioFlag = true; }

void addToHistory(const String& sender, const String& message) {
  for (int i = HISTORY_SIZE - 1; i > 0; i--) {
    msgHistory[i] = msgHistory[i - 1];
    msgSender[i]  = msgSender[i - 1];
  }
  msgHistory[0] = message;
  msgSender[0]  = sender;
  saveHistoryToFlash();
}

// Persists the full 15-item history to flash (NVS). Called after every new message.
void saveHistoryToFlash() {
  for (int i = 0; i < HISTORY_SIZE; i++) {
    String key = "h" + String(i);
    String combined = msgSender[i] + "|" + msgHistory[i];
    prefs.putString(key.c_str(), combined);
  }
}

// Loads the 15-item history back from flash on boot
void loadHistoryFromFlash() {
  for (int i = 0; i < HISTORY_SIZE; i++) {
    String key = "h" + String(i);
    String stored = prefs.getString(key.c_str(), "");
    int sep = stored.indexOf('|');
    if (sep >= 0) {
      msgSender[i]  = stored.substring(0, sep);
      msgHistory[i] = stored.substring(sep + 1);
    } else {
      msgSender[i]  = "";
      msgHistory[i] = "";
    }
  }
}

String truncate(const String& s, int maxChars) {
  if ((int)s.length() <= maxChars) return s;
  return s.substring(0, maxChars - 3) + "...";
}

void startListening() {
  radioFlag = false;
  radio.startReceive();
}

void showStatus(const char* l1, const char* l2 = "",
                const char* l3 = "", const char* l4 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  String hdr = "Pager " + String(DEVICE_ID) + "  866MHz";
  u8g2.drawStr(0, 9, hdr.c_str());
  u8g2.drawHLine(0, 11, 128);
  u8g2.setFont(u8g2_font_9x18_tf);
  u8g2.drawStr(0, 28, l1);
  u8g2.setFont(u8g2_font_7x13_tf);
  if (strlen(l2)) u8g2.drawStr(0, 43, l2);
  u8g2.setFont(u8g2_font_6x10_tf);
  if (strlen(l3)) u8g2.drawStr(0, 55, l3);
  if (strlen(l4)) u8g2.drawStr(0, 64, l4);
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.sendBuffer();
}

void showReady() {
  showStatus("Ready", "0 = history");
}

void scrollBigMessage(const char* header, const char* message, int repeatCount, const char* footer = "") {
  u8g2.setFont(u8g2_font_10x20_tf);
  int textWidth = u8g2.getUTF8Width(message);
  int screenW   = 128;

  for (int rep = 0; rep < repeatCount; rep++) {
    if (textWidth <= screenW) {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(0, 9, header);
      u8g2.drawHLine(0, 11, 128);
      u8g2.setFont(u8g2_font_10x20_tf);
      u8g2.drawStr(0, 40, message);
      if (strlen(footer) > 0) {
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(0, 62, footer);
      }
      u8g2.drawFrame(0, 0, 128, 64);
      u8g2.sendBuffer();
      delay(2000);
    } else {
      for (int x = screenW; x > -textWidth; x -= 3) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(0, 9, header);
        u8g2.drawHLine(0, 11, 128);
        u8g2.setFont(u8g2_font_10x20_tf);
        u8g2.drawStr(x, 40, message);
        if (strlen(footer) > 0) {
          u8g2.setFont(u8g2_font_6x10_tf);
          u8g2.drawStr(0, 62, footer);
        }
        u8g2.drawFrame(0, 0, 128, 64);
        u8g2.sendBuffer();
        delay(18);
      }
      delay(300);
    }
  }
}

void drawHistoryFrame(int scrollX) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  String hdr = "4^  7v  #view  *back  " + String(historySelected + 1) + "/" + String(HISTORY_SIZE);
  u8g2.drawStr(0, 9, hdr.c_str());
  u8g2.drawHLine(0, 11, 128);

  int yPos[3]    = {24, 40, 56};
  int rowHeight  = 13;

  for (int row = 0; row < 3; row++) {
    int i = historyTop + row;
    if (i >= HISTORY_SIZE) break;

    String line;
    if (msgHistory[i].length() == 0) {
      line = String(i + 1) + ". -- empty --";
    } else {
      line = String(i + 1) + ". P" + msgSender[i] + ":" + msgHistory[i];
    }

    if (i == historySelected) {
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, yPos[row] - rowHeight, 128, rowHeight + 2);
      u8g2.setDrawColor(0);
      u8g2.setFont(u8g2_font_7x13_tf);
      u8g2.drawStr(scrollX, yPos[row], line.c_str());
      u8g2.setDrawColor(1);
    } else {
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(1, yPos[row], truncate(line, 21).c_str());
    }
  }

  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.sendBuffer();
}

// Keeps the 3-row visible window aligned with historySelected
void adjustHistoryWindow() {
  if (historySelected < historyTop) historyTop = historySelected;
  if (historySelected > historyTop + 2) historyTop = historySelected - 2;
}

void showHistoryScreen() {
  adjustHistoryWindow();

  u8g2.setFont(u8g2_font_7x13_tf);
  String selLine = (msgHistory[historySelected].length() == 0)
    ? String(historySelected + 1) + ". -- empty --"
    : String(historySelected + 1) + ". P" + msgSender[historySelected] + ":" + msgHistory[historySelected];

  int textWidth = u8g2.getStrWidth(selLine.c_str());
  int screenW   = 128;

  if (textWidth <= screenW) {
    drawHistoryFrame(1);

    while (true) {
      char k = keypad.getKey();
      if (k) {
        if (k == '4') {
          historySelected = max(0, historySelected - 1);
          showHistoryScreen();
          return;
        } else if (k == '7') {
          historySelected = min(HISTORY_SIZE - 1, historySelected + 1);
          showHistoryScreen();
          return;
        } else if (k == '#') {
          if (msgHistory[historySelected].length() > 0) {
            String hdr = "FROM " + msgSender[historySelected];
            scrollBigMessage(hdr.c_str(), msgHistory[historySelected].c_str(), 1);
            showHistoryScreen();
          }
          return;
        } else if (k == '*') {
          historyMode = false;
          showReady();
          return;
        }
      }
      if (radioFlag) { historyMode = false; return; }
      checkAckTimeout();
      delay(30);
    }
  }

  int x = screenW;
  while (true) {
    drawHistoryFrame(x);
    x -= 3;
    if (x < -textWidth) x = screenW;

    char k = keypad.getKey();
    if (k) {
      if (k == '4') {
        historySelected = max(0, historySelected - 1);
        showHistoryScreen();
        return;
      } else if (k == '7') {
        historySelected = min(HISTORY_SIZE - 1, historySelected + 1);
        showHistoryScreen();
        return;
      } else if (k == '#') {
        if (msgHistory[historySelected].length() > 0) {
          String hdr = "FROM " + msgSender[historySelected];
          scrollBigMessage(hdr.c_str(), msgHistory[historySelected].c_str(), 1);
          showHistoryScreen();
        }
        return;
      } else if (k == '*') {
        historyMode = false;
        showReady();
        return;
      }
    }
    if (radioFlag) { historyMode = false; return; }
    checkAckTimeout();
    delay(35);
  }
}

void transmitMessage(const String& wireMsg, const String& displayMsg) {
  radio.standby();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_9x18_tf);
  u8g2.drawStr(0, 30, "Sending...");
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.sendBuffer();

  Serial.print("TX: "); Serial.println(wireMsg);
  String txMsg = wireMsg;
  int state = radio.transmit(txMsg);

  startListening();   // back to RX immediately so an incoming ACK is never missed

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("Sent OK");
    String txHdr = "SENT Msg#" + String(pendingMsgId);
    scrollBigMessage(txHdr.c_str(), displayMsg.c_str(), 1);
    showStatus("Waiting...", "for ACK");
  } else {
    Serial.print("TX failed: "); Serial.println(state);
    showStatus("TX FAILED", ("Err:" + String(state)).c_str());
    delay(2000);
    showReady();
  }
}

// Fires off a message and starts tracking it for ACK / retry
void sendTrackedMessage(const String& payload) {
  uint16_t msgId = nextMsgId++;
  if (nextMsgId > 9999) nextMsgId = 1;

  String wire = "MSG|" + String(DEVICE_ID) + "|" + String(msgId) + "|" + payload;

  ackPending        = true;
  pendingMsgId      = msgId;
  pendingWireMsg    = wire;
  pendingDisplayMsg = payload;
  pendingSentTime   = millis();
  pendingRetries    = 0;

  transmitMessage(wire, payload);
}

// Sends a short ACK packet back to whoever sent us a message
void sendAck(const String& toSender, uint16_t msgId) {
  radio.standby();
  String ackWire = "ACK|" + String(DEVICE_ID) + "|" + String(msgId) + "|";
  String tx = ackWire;
  Serial.print("ACK TX: "); Serial.println(ackWire);
  radio.transmit(tx);
}

// Checked frequently (non-blocking) - retries or gives up on a pending message
void checkAckTimeout() {
  if (!ackPending) return;
  if (millis() - pendingSentTime < ACK_TIMEOUT_MS) return;

  if (pendingRetries < MAX_RETRIES) {
    pendingRetries++;
    Serial.println("No ACK yet - retry #" + String(pendingRetries));
    radio.standby();
    String resend = pendingWireMsg;
    radio.transmit(resend);
    startListening();
    pendingSentTime = millis();
  } else {
    Serial.println("No ACK after retries - giving up");
    ackPending = false;
    showStatus("No response", "Message may not", "have reached", "the other pager");
  }
}

void handleInstant(char key) {
  String msg;
  switch (key) {
    case 'A': msg = MSG_A; break;
    case 'B': msg = MSG_B; break;
    case 'C': msg = MSG_C; break;
    case 'D': msg = MSG_D; break;
    default: return;
  }
  sendTrackedMessage(msg);
}

String buildTemplate(char tmpl, const String& num) {
  String pre, suf;
  switch (tmpl) {
    case '1': pre = TMPL_1_PRE; suf = TMPL_1_SUF; break;
    case '2': pre = TMPL_2_PRE; suf = TMPL_2_SUF; break;
    case '3': pre = TMPL_3_PRE; suf = TMPL_3_SUF; break;
    default:  return "";
  }
  return pre + (num.length() ? num : "__") + suf;
}

void showTemplateScreen() {
  String pre, suf;
  switch (activeTemplate) {
    case '1': pre = TMPL_1_PRE; suf = TMPL_1_SUF; break;
    case '2': pre = TMPL_2_PRE; suf = TMPL_2_SUF; break;
    case '3': pre = TMPL_3_PRE; suf = TMPL_3_SUF; break;
    default: pre = ""; suf = ""; break;
  }
  String valueLine = (typedNumber.length() ? typedNumber : "__") + suf;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  String hdr = "Pager " + String(DEVICE_ID) + "  866MHz";
  u8g2.drawStr(0, 9, hdr.c_str());
  u8g2.drawHLine(0, 11, 128);
  u8g2.setFont(u8g2_font_7x13_tf);
  u8g2.drawStr(0, 26, pre.c_str());
  u8g2.setFont(u8g2_font_9x18_tf);
  u8g2.drawStr(0, 46, valueLine.c_str());
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 60, "# = send   * = cancel");
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.sendBuffer();
}

void enterTemplateMode(char key) {
  templateMode   = true;
  activeTemplate = key;
  typedNumber    = "";
  showTemplateScreen();
}

void runAlertLoop() {
  alertMode = true;
  String hdr = "FROM P" + lastRxSender + "  Msg#" + String(lastRxMsgId);
  const char* msg = lastRxMessage.c_str();

  u8g2.setFont(u8g2_font_10x20_tf);
  int textWidth = u8g2.getUTF8Width(msg);
  int screenW   = 128;
  int x         = screenW;

  while (alertMode) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 9, hdr.c_str());
    u8g2.drawHLine(0, 11, 128);
    u8g2.setFont(u8g2_font_10x20_tf);
    u8g2.drawStr(x, 40, msg);
    u8g2.setFont(u8g2_font_6x10_tf);
    String ftr = "Msg #" + String(lastRxMsgId);
    u8g2.drawStr(0, 62, ftr.c_str());
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.sendBuffer();

    x -= 3;
    if (x < -textWidth) x = screenW;

    char k = keypad.getKey();
    if (k) {
      alertMode = false;
      switch (k) {
        case 'A': case 'B': case 'C': case 'D':
          handleInstant(k);
          return;
        case '1': case '2': case '3':
          enterTemplateMode(k);
          templateMode = true;
          return;
        default:
          showReady();
          return;
      }
    }

    if (radioFlag) {
      alertMode = false;
      return;
    }

    checkAckTimeout();
    delay(18);
  }
}

void handleIncoming() {
  String raw;
  int state = radio.readData(raw);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.print("RX raw: "); Serial.println(raw);

    // Packet format: TYPE|SenderID|MsgID|Payload
    int p1 = raw.indexOf('|');
    int p2 = raw.indexOf('|', p1 + 1);
    int p3 = raw.indexOf('|', p2 + 1);

    if (p1 < 0 || p2 < 0 || p3 < 0) {
      Serial.println("Malformed packet, ignoring");
      startListening();
      return;
    }

    String type     = raw.substring(0, p1);
    String sender   = raw.substring(p1 + 1, p2);
    uint16_t msgId   = raw.substring(p2 + 1, p3).toInt();
    String payload   = raw.substring(p3 + 1);

    if (type == "ACK") {
      // this acknowledges a message WE sent
      if (ackPending && msgId == pendingMsgId) {
        ackPending = false;
        Serial.println("ACK received - delivered!");
        String l2 = "Msg #" + String(msgId);
        String l3 = "To Pager " + sender;
        showStatus("Delivered", l2.c_str(), l3.c_str());
        // stay on Delivered screen until any key is pressed
        while (!keypad.getKey()) {
          if (radioFlag) break;   // exit if new message arrives
          delay(30);
        }
        showReady();
      }
      startListening();
      return;
    }

    if (type == "MSG") {
      // reply with an ACK immediately, then resume listening
      sendAck(sender, msgId);
      startListening();

      addToHistory(sender, payload);
      lastRxMessage = payload;
      lastRxSender  = sender;
      lastRxMsgId   = msgId;

      String hdr = "FROM P" + sender + "  Msg#" + String(msgId);
      String ftr = "Msg #" + String(msgId);
      scrollBigMessage(hdr.c_str(), payload.c_str(), 2, ftr.c_str());

      runAlertLoop();

      if (!templateMode) showReady();
      return;
    }

    Serial.println("Unknown packet type, ignoring");
    startListening();

  } else {
    Serial.print("RX error: "); Serial.println(state);
    startListening();
  }
}

void setup() {
  Serial.begin(115200);

  prefs.begin("neopager", false);   // open (or create) persistent storage namespace
  loadHistoryFromFlash();           // restore last 15 messages saved before power-off

  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(100);
  u8g2.begin();
  showStatus("Booting...", "Init LoRa...");
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                           LORA_SYNC, LORA_PWR);
  if (state != RADIOLIB_ERR_NONE) {
    showStatus("Radio FAILED", ("Err:" + String(state)).c_str());
    while (true) delay(1000);
  }
  radio.setDio1Action(onRadio);
  startListening();
  showStatus("Radio OK!", ("Pager " + String(DEVICE_ID)).c_str());
  delay(1500);
  showReady();
}

void loop() {
  checkAckTimeout();   // non-blocking retry/give-up check for pending ACKs

  if (radioFlag && !templateMode && !historyMode) {
    radioFlag = false;
    handleIncoming();
    return;
  }

  char key = keypad.getKey();
  if (!key) return;
  Serial.print("Key: "); Serial.println(key);

  if (historyMode) {
    showHistoryScreen();
    return;
  }

  if (templateMode) {
    if (key >= '0' && key <= '9') {
      if (typedNumber.length() < 3) {
        typedNumber += key;
        showTemplateScreen();
      }
    } else if (key == '*') {
      templateMode = false;
      typedNumber  = "";
      showReady();
    } else if (key == '#') {
      if (typedNumber.length() == 0) {
        showStatus("Enter a number", "then press #");
        delay(1500);
        showTemplateScreen();
      } else {
        String msg = buildTemplate(activeTemplate, typedNumber);
        templateMode = false;
        typedNumber  = "";
        sendTrackedMessage(msg);
      }
    }
    return;
  }

  switch (key) {
    case 'A': case 'B': case 'C': case 'D':
      handleInstant(key);
      break;
    case '1': case '2': case '3':
      enterTemplateMode(key);
      break;
    case '0':
      historyMode     = true;
      historySelected = 0;
      historyTop      = 0;
      showHistoryScreen();
      break;
    default:
      break;
  }
}
#include <Keypad.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>

// =================================================================
#define DEVICE_ID 1 // change device id for other pager 

// =================================================================
// BOARD PINS
// These match how everything is wired on the Heltec board.
// Don't touch unless you rewire something.
// =================================================================
#define LORA_NSS 8
#define LORA_SCK 9
#define LORA_MOSI 10
#define LORA_MISO 11
#define LORA_RST 12
#define LORA_BUSY 13
#define LORA_DIO1 14
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21
#define VEXT_CTRL 36
#define BUZZER_PIN 45   // buzzer wire goes here

// =================================================================
// RADIO (LoRa) SETTINGS
// Both pagers must use the SAME settings here or they won't talk.
// =================================================================
#define LORA_FREQ 866.0 // frequency allowed for LoRa use in India
#define LORA_BW 125.0   // bandwidth
#define LORA_SF 10      // spreading factor (range vs speed tradeoff)
#define LORA_CR 7       // coding rate
#define LORA_SYNC 0x12  // sync word, like a "channel code" between the 2 pagers
#define LORA_PWR 22     // transmit power

// =================================================================
// KEYPAD LAYOUT
// This is just a map of which key is which on the 4x4 keypad,
// and which physical pins the rows/columns are wired to.
// =================================================================
const byte ROWS = 4, COLS = 4;
char keyMap[ROWS][COLS] = {{'1', '2', '3', 'A'},
                           {'4', '5', '6', 'B'},
                           {'7', '8', '9', 'C'},
                           {'*', '0', '#', 'D'}};
byte rowPins[ROWS] = {38, 1, 2, 3};
byte colPins[COLS] = {4, 5, 6, 7};
Keypad keypad = Keypad(makeKeymap(keyMap), rowPins, colPins, ROWS, COLS);

// =================================================================
// QUICK MESSAGES
// Press A, B, C or D and it sends this exact message instantly.
// Change the text here if you want different quick messages.
// =================================================================
const char *MSG_A = "I need help, please come now";
const char *MSG_B = "I am at the meeting";
const char *MSG_C = "Where are you?";
const char *MSG_D = "Task done, acknowledged";

// =================================================================
// FILL-IN-THE-BLANK MESSAGES
// Press 1, 2 or 3, then type a number, then press # to send.
// Example: press 1, type 5, press # -> sends "Will be back in 5 min"
// =================================================================
const char *TMPL_1_PRE = "Will be back in ";
const char *TMPL_1_SUF = " min";
const char *TMPL_2_PRE = "Meet me at floor ";
const char *TMPL_2_SUF = "";
const char *TMPL_3_PRE = "Call me in ";
const char *TMPL_3_SUF = " min";

// =================================================================
// MESSAGE HISTORY (last 15 messages, saved to flash so they survive reboot)
// =================================================================
#define HISTORY_SIZE 15
String msgHistory[HISTORY_SIZE];   // the actual message text
String msgSender[HISTORY_SIZE];    // who sent it (device ID)

// =================================================================
// MAIN OBJECTS - radio, screen, flash storage
// =================================================================
SPIClass loraSPI(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
Preferences prefs;

// =================================================================
// STATE FLAGS - these track "what screen/mode are we in right now"
// =================================================================
volatile bool radioFlag = false;   // true = a LoRa packet just arrived
bool templateMode = false;         // true = user is typing a fill-in-blank message
bool historyMode = false;          // true = user is browsing old messages
bool alertMode = false;            // true = showing a just-received message
char activeTemplate = 0;           // which template (1/2/3) is being filled
String typedNumber = "";           // number the user is typing in template mode
int historySelected = 0;           // which history row is highlighted
int historyTop = 0;                // which history row is at top of screen

// =================================================================
// ACK / DELIVERY TRACKING
// When we send a message we wait for the other pager to confirm
// it got it (ACK). If no ACK comes in time, we resend.
// =================================================================
uint16_t nextMsgId = 1;            // each message gets its own ID number
bool ackPending = false;           // true = waiting for confirmation
uint16_t pendingMsgId = 0;
String pendingWireMsg = "";        // exact message being sent over radio
String pendingDisplayMsg = "";     // the readable version for the screen
unsigned long pendingSentTime = 0;
int pendingRetries = 0;
const unsigned long ACK_TIMEOUT_MS = 6000;   // wait 6 sec before resending
const int MAX_RETRIES = 2;                    // give up after 2 resends

// last message we received, kept handy for the alert screen
String lastRxMessage = "";
String lastRxSender = "";
uint16_t lastRxMsgId = 0;

// this runs the instant a LoRa packet lands (interrupt), just sets a flag
void IRAM_ATTR onRadio() { radioFlag = true; }

// =================================================================
// BUZZER
// Buzzer is active-LOW, meaning LOW = beep, HIGH = quiet.
// =================================================================
void beep() {
  digitalWrite(BUZZER_PIN, LOW);    // buzzer ON
  delay(200);
  digitalWrite(BUZZER_PIN, HIGH);   // buzzer OFF
}

// =================================================================
// SAVING / LOADING HISTORY TO FLASH MEMORY
// So old messages are still there after a power cut or reset.
// =================================================================
void saveHistoryToFlash() {
  for (int i = 0; i < HISTORY_SIZE; i++) {
    String key = "h" + String(i);
    // store as "sender|message" in one string
    prefs.putString(key.c_str(), msgSender[i] + "|" + msgHistory[i]);
  }
}

void loadHistoryFromFlash() {
  for (int i = 0; i < HISTORY_SIZE; i++) {
    String key = "h" + String(i);
    String stored = prefs.getString(key.c_str(), "");
    int sep = stored.indexOf('|');
    if (sep >= 0) {
      msgSender[i] = stored.substring(0, sep);
      msgHistory[i] = stored.substring(sep + 1);
    } else {
      msgSender[i] = "";
      msgHistory[i] = "";
    }
  }
}

// adds a new message at the top of the list, pushes everything else down
void addToHistory(const String &sender, const String &message) {
  for (int i = HISTORY_SIZE - 1; i > 0; i--) {
    msgHistory[i] = msgHistory[i - 1];
    msgSender[i] = msgSender[i - 1];
  }
  msgHistory[0] = message;
  msgSender[0] = sender;
  saveHistoryToFlash();
}

// =================================================================
// SMALL HELPERS
// =================================================================

// cuts a long string short and adds "..." at the end, so it fits on screen
String truncate(const String &s, int maxChars) {
  if ((int)s.length() <= maxChars)
    return s;
  return s.substring(0, maxChars - 3) + "...";
}

// puts the radio back into "listening" mode so it can receive packets
void startListening() {
  radioFlag = false;
  radio.startReceive();
}

// =================================================================
// SCREEN FUNCTIONS
// =================================================================

// draws a simple 4-line status screen (title bar + up to 3 lines of text)
void showStatus(const char *l1, const char *l2 = "", const char *l3 = "",
                const char *l4 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  String hdr = "Pager " + String(DEVICE_ID) + "  866MHz";
  u8g2.drawStr(0, 9, hdr.c_str());
  u8g2.drawHLine(0, 11, 128);   // line under the header
  u8g2.setFont(u8g2_font_9x18_tf);
  u8g2.drawStr(0, 28, l1);
  u8g2.setFont(u8g2_font_7x13_tf);
  if (strlen(l2))
    u8g2.drawStr(0, 43, l2);
  u8g2.setFont(u8g2_font_6x10_tf);
  if (strlen(l3))
    u8g2.drawStr(0, 55, l3);
  if (strlen(l4))
    u8g2.drawStr(0, 64, l4);
  u8g2.drawFrame(0, 0, 128, 64);   // border around the whole screen
  u8g2.sendBuffer();
}

// shows the normal "waiting for input" home screen
void showReady() { showStatus("Ready", "0 = history"); }

// Shows a message big on screen. If it's too long to fit, it scrolls
// sideways like a news ticker. Also keeps checking if an ACK is needed
// while it scrolls, so nothing gets stuck waiting.
void scrollBigMessage(const char *header, const char *message, int repeatCount,
                      const char *footer = "") {
  u8g2.setFont(u8g2_font_10x20_tf);
  int textWidth = u8g2.getUTF8Width(message);
  int screenW = 128;

  for (int rep = 0; rep < repeatCount; rep++) {
    if (textWidth <= screenW) {
      // message fits fully on screen, no need to scroll
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
      unsigned long t = millis();
      while (millis() - t < 2000) {
        if (radioFlag)   // new packet came in, stop and handle it
          return;
        delay(20);
      }
    } else {
      // message is too long, scroll it right to left
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
        if (radioFlag)
          return;
        delay(18);
      }
      delay(300);
    }
  }
}

// =================================================================
// HISTORY SCREEN
// Lets you scroll up/down through old messages with keys 4 and 7,
// press # to view one fully, press * to go back.
// =================================================================

// draws the 3 visible rows of the history list, highlighting the selected one
void drawHistoryFrame(int scrollX) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  String hdr = "4^  7v  #view  *back  " + String(historySelected + 1) + "/" +
               String(HISTORY_SIZE);
  u8g2.drawStr(0, 9, hdr.c_str());
  u8g2.drawHLine(0, 11, 128);

  int yPos[3] = {24, 40, 56};
  int rowHeight = 13;

  for (int row = 0; row < 3; row++) {
    int i = historyTop + row;
    if (i >= HISTORY_SIZE)
      break;

    String line =
        (msgHistory[i].length() == 0)
            ? String(i + 1) + ". -- empty --"
            : String(i + 1) + ". P" + msgSender[i] + ":" + msgHistory[i];

    if (i == historySelected) {
      // draw this row inverted (highlighted) since it's selected
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

// keeps the selected row visible inside the 3-row window
void adjustHistoryWindow() {
  if (historySelected < historyTop)
    historyTop = historySelected;
  if (historySelected > historyTop + 2)
    historyTop = historySelected - 2;
}

// main loop for the history screen - waits for keys, redraws as needed
void showHistoryScreen() {
  adjustHistoryWindow();

  u8g2.setFont(u8g2_font_7x13_tf);
  String selLine = (msgHistory[historySelected].length() == 0)
                       ? String(historySelected + 1) + ". -- empty --"
                       : String(historySelected + 1) + ". P" +
                             msgSender[historySelected] + ":" +
                             msgHistory[historySelected];

  int textWidth = u8g2.getStrWidth(selLine.c_str());
  int screenW = 128;

  // if the selected line fits fully, no need to scroll it
  if (textWidth <= screenW) {
    drawHistoryFrame(1);
    while (true) {
      char k = keypad.getKey();
      if (k) {
        if (k == '4') {              // move selection up
          historySelected = max(0, historySelected - 1);
          showHistoryScreen();
          return;
        } else if (k == '7') {       // move selection down
          historySelected = min(HISTORY_SIZE - 1, historySelected + 1);
          showHistoryScreen();
          return;
        } else if (k == '#') {       // view full message
          if (msgHistory[historySelected].length() > 0) {
            String hdr = "FROM " + msgSender[historySelected];
            scrollBigMessage(hdr.c_str(), msgHistory[historySelected].c_str(),
                             1);
            showHistoryScreen();
          }
          return;
        } else if (k == '*') {       // back to home screen
          historyMode = false;
          showReady();
          return;
        }
      }
      if (radioFlag) {   // new message arrived while browsing history
        historyMode = false;
        return;
      }
      checkAckTimeout();
      delay(30);
    }
  }

  // selected line is too long, scroll it sideways while still checking keys
  int x = screenW;
  while (true) {
    drawHistoryFrame(x);
    x -= 3;
    if (x < -textWidth)
      x = screenW;

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
    if (radioFlag) {
      historyMode = false;
      return;
    }
    checkAckTimeout();
    delay(35);
  }
}

// =================================================================
// SENDING MESSAGES OVER LORA
// =================================================================

// actually sends the message over radio and shows "Sending..." etc on screen
void transmitMessage(const String &wireMsg, const String &displayMsg) {
  radio.standby();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_9x18_tf);
  u8g2.drawStr(0, 30, "Sending...");
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.sendBuffer();

  Serial.print("TX: ");
  Serial.println(wireMsg);
  String txMsg = wireMsg;
  int state = radio.transmit(txMsg);
  startListening();   // go back to listening right after sending

  if (state == RADIOLIB_ERR_NONE) {
  String txHdr = "SENT M#" + String(pendingMsgId);
    scrollBigMessage(txHdr.c_str(), displayMsg.c_str(), 1);
    showStatus("Waiting...", "for delivery ACK");
  } else {
    showStatus("TX FAILED", ("Err:" + String(state)).c_str());
    delay(2000);
    showReady();
  }
}

// wraps a message with an ID number and starts tracking it for an ACK
void sendTrackedMessage(const String &payload) {
  uint16_t msgId = nextMsgId++;
  if (nextMsgId > 9999)   // wrap back around so the ID never overflows
    nextMsgId = 1;

  // wire format: MSG|senderID|msgID|actual text
  String wire =
      "MSG|" + String(DEVICE_ID) + "|" + String(msgId) + "|" + payload;

  ackPending = true;
  pendingMsgId = msgId;
  pendingWireMsg = wire;
  pendingDisplayMsg = payload;
  pendingSentTime = millis();
  pendingRetries = 0;

  transmitMessage(wire, payload);
}

// sends back a small "got it" packet to whoever sent us a message
void sendAck(const String &toSender, uint16_t msgId) {
  radio.standby();
  String tx = "ACK|" + String(DEVICE_ID) + "|" + String(msgId) + "|";
  Serial.print("ACK TX: ");
  Serial.println(tx);
  radio.transmit(tx);
}

// checks if we've been waiting too long for an ACK, and resends if so
void checkAckTimeout() {
  if (!ackPending)
    return;
  if (millis() - pendingSentTime < ACK_TIMEOUT_MS)
    return;   // still within the wait time, do nothing yet

  if (pendingRetries < MAX_RETRIES) {
    pendingRetries++;
    Serial.println("No ACK — retry #" + String(pendingRetries));
    radio.standby();
    String resend = pendingWireMsg;
    radio.transmit(resend);
    startListening();
    pendingSentTime = millis();   // reset the clock for this retry
  } else {
    // tried enough times, give up and tell the user
    ackPending = false;
    showStatus("No response", "Message may not", "have reached",
               "the other pager");
  }
}

// =================================================================
// KEY HANDLING
// =================================================================

// handles the instant quick-message keys: A, B, C, D
void handleInstant(char key) {
  String msg;
  switch (key) {
  case 'A':
    msg = MSG_A;
    break;
  case 'B':
    msg = MSG_B;
    break;
  case 'C':
    msg = MSG_C;
    break;
  case 'D':
    msg = MSG_D;
    break;
  default:
    return;
  }
  sendTrackedMessage(msg);
}

// puts together the final text for a fill-in-blank template, e.g.
// template '1' + "5" -> "Will be back in 5 min"
String buildTemplate(char tmpl, const String &num) {
  String pre, suf;
  switch (tmpl) {
  case '1':
    pre = TMPL_1_PRE;
    suf = TMPL_1_SUF;
    break;
  case '2':
    pre = TMPL_2_PRE;
    suf = TMPL_2_SUF;
    break;
  case '3':
    pre = TMPL_3_PRE;
    suf = TMPL_3_SUF;
    break;
  default:
    return "";
  }
  return pre + (num.length() ? num : "__") + suf;
}

// draws the screen you see while typing a number into a template
void showTemplateScreen() {
  String pre, suf;
  switch (activeTemplate) {
  case '1':
    pre = TMPL_1_PRE;
    suf = TMPL_1_SUF;
    break;
  case '2':
    pre = TMPL_2_PRE;
    suf = TMPL_2_SUF;
    break;
  case '3':
    pre = TMPL_3_PRE;
    suf = TMPL_3_SUF;
    break;
  default:
    pre = "";
    suf = "";
    break;
  }
  String valueLine = (typedNumber.length() ? typedNumber : "__") + suf;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 9, ("Pager " + String(DEVICE_ID) + "  866MHz").c_str());
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

// starts template mode when user presses 1, 2 or 3
void enterTemplateMode(char key) {
  templateMode = true;
  activeTemplate = key;
  typedNumber = "";
  showTemplateScreen();
}

// =================================================================
// ALERT SCREEN
// This is what shows up right when a new message comes in.
// It keeps scrolling the message until any key is pressed.
// =================================================================
void runAlertLoop() {
  alertMode = true;
 String hdr = "P" + lastRxSender + " M#" + String(lastRxMsgId) + " " + String((int)radio.getRSSI()) + "dB";
  const char *msg = lastRxMessage.c_str();

  u8g2.setFont(u8g2_font_10x20_tf);
  int textWidth = u8g2.getUTF8Width(msg);
  int screenW = 128;
  int x = screenW;

  while (alertMode) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 9, hdr.c_str());
    u8g2.drawHLine(0, 11, 128);
    u8g2.setFont(u8g2_font_10x20_tf);
    u8g2.drawStr(x, 40, msg);
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 62, ("Msg #" + String(lastRxMsgId)).c_str());
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.sendBuffer();

    x -= 3;
    if (x < -textWidth)
      x = screenW;   // loop the scroll back to the start

    char k = keypad.getKey();
    if (k) {
      alertMode = false;
      switch (k) {
      case 'A':
      case 'B':
      case 'C':
      case 'D':
        handleInstant(k);   // quick-reply with an instant message
        return;
      case '1':
      case '2':
      case '3':
        enterTemplateMode(k);   // quick-reply with a fill-in-blank message
        templateMode = true;
        return;
      default:
        showReady();
        return;
      }
    }

    if (radioFlag) {   // another message came in already
      alertMode = false;
      return;
    }
    checkAckTimeout();
    delay(18);
  }
}

// =================================================================
// WHAT TO DO WHEN A PACKET ARRIVES
// =================================================================
void handleIncoming() {
  String raw;
  int state = radio.readData(raw);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("RX error: ");
    Serial.println(state);
    startListening();
    return;
  }

  Serial.print("RX: ");
  Serial.println(raw);

  // packet format is: TYPE|senderID|msgID|payload
  int p1 = raw.indexOf('|');
  int p2 = raw.indexOf('|', p1 + 1);
  int p3 = raw.indexOf('|', p2 + 1);

  if (p1 < 0 || p2 < 0 || p3 < 0) {
    Serial.println("Malformed packet, ignoring");
    startListening();
    return;
  }

  String type = raw.substring(0, p1);
  String sender = raw.substring(p1 + 1, p2);
  uint16_t msgId = raw.substring(p2 + 1, p3).toInt();
  String payload = raw.substring(p3 + 1);

  // ---- case 1: this is a delivery confirmation (ACK) ----
  if (type == "ACK") {
    if (ackPending && msgId == pendingMsgId) {
      ackPending = false;
      Serial.println("Delivered!");
      String l2 = "Msg #" + String(msgId);
      String l3 = "To Pager " + sender;
      showStatus("Delivered", l2.c_str(), l3.c_str());
      while (!keypad.getKey()) {   // wait for any key before going back
        if (radioFlag)
          break;
        delay(30);
      }
      showReady();
    }
    startListening();
    return;
  }

  // ---- case 2: this is a new message (MSG) ----
  if (type == "MSG") {
    // send the ACK straight away, sender only waits 6 sec max
    sendAck(sender, msgId);
    delay(50);
    startListening();

    beep();   // one short beep - safe to do this after the ACK is sent

    addToHistory(sender, payload);
    lastRxMessage = payload;
    lastRxSender = sender;
    lastRxMsgId = msgId;

  String hdr = "P" + sender + " M#" + String(msgId) + " " + String((int)radio.getRSSI()) + "dB";
    String ftr = "Msg #" + String(msgId);
    scrollBigMessage(hdr.c_str(), payload.c_str(), 2, ftr.c_str());

    runAlertLoop();

    if (!templateMode)
      showReady();
    return;
  }

  Serial.println("Unknown packet type, ignoring");
  startListening();
}

// =================================================================
// SETUP - runs once when the pager powers on
// =================================================================
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);   // buzzer OFF at boot

  // one beep on boot, just to confirm the buzzer works
  digitalWrite(BUZZER_PIN, LOW);
  delay(400);
  digitalWrite(BUZZER_PIN, HIGH);

  prefs.begin("neopager", false);
  loadHistoryFromFlash();   // bring back old messages from flash

  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(100);

  u8g2.begin();
  showStatus("Booting...", "Init LoRa...");

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state =
      radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, LORA_SYNC, LORA_PWR);
  if (state != RADIOLIB_ERR_NONE) {
    // radio didn't start, no point continuing, just show error forever
    showStatus("Radio FAILED", ("Err:" + String(state)).c_str());
    while (true)
      delay(1000);
  }

  radio.setDio1Action(onRadio);   // call onRadio() whenever a packet lands
  startListening();

  showStatus("Radio OK!", ("Pager " + String(DEVICE_ID)).c_str());
  delay(1500);
  showReady();
}

// =================================================================
// MAIN LOOP - runs over and over, forever
// =================================================================
void loop() {
  checkAckTimeout();   // always keep an eye on pending ACKs

  // if a new packet arrived and we're not busy in a menu, handle it now
  if (radioFlag && !templateMode && !historyMode) {
    radioFlag = false;
    handleIncoming();
    return;
  }

  char key = keypad.getKey();
  if (!key)
    return;   // no key pressed, nothing to do this round
  Serial.print("Key: ");
  Serial.println(key);

  if (historyMode) {
    showHistoryScreen();
    return;
  }

  if (templateMode) {
    if (key >= '0' && key <= '9') {
      if (typedNumber.length() < 3) {   // limit to 3 digits
        typedNumber += key;
        showTemplateScreen();
      }
    } else if (key == '*') {   // cancel
      templateMode = false;
      typedNumber = "";
      showReady();
    } else if (key == '#') {   // confirm and send
      if (typedNumber.length() == 0) {
        showStatus("Enter a number", "then press #");
        delay(1500);
        showTemplateScreen();
      } else {
        String msg = buildTemplate(activeTemplate, typedNumber);
        templateMode = false;
        typedNumber = "";
        sendTrackedMessage(msg);
      }
    }
    return;
  }

  // normal key presses from the home screen
  switch (key) {
  case 'A':
  case 'B':
  case 'C':
  case 'D':
    handleInstant(key);   // send a quick message
    break;
  case '1':
  case '2':
  case '3':
    enterTemplateMode(key);   // start typing a fill-in-blank message
    break;
  case '0':
    historyMode = true;
    historySelected = 0;
    historyTop = 0;
    showHistoryScreen();
    break;
  default:
    break;
  }
}
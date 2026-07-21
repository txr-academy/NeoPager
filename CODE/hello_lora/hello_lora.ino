#include <RadioLib.h>   // library that lets us use the LoRa radio (send/receive)
#include <U8g2lib.h>    // library that lets us draw text on the OLED screen
#include <Wire.h>       // needed for the screen to talk to the board

// CHANGE THIS ON EACH BOARD
// Board A gets "1", Board B gets "2" - so each pager knows its own name
#define MY_ID "1"

// sets up the LoRa radio and tells it which pins it is connected to
SX1262 radio = new Module(8, 14, 12, 13);

// sets up the OLED screen and tells it which pins it is connected to
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/21, /*clock=*/18, /*data=*/17);

#define PRG_PIN 0   // the built-in PRG button is connected to pin 0

// this becomes true automatically the moment a message arrives
// "volatile" means: this can change any time, even in the background
volatile bool messageArrived = false;

// this counts how many messages THIS pager has sent (starts at 1)
// each pager has its own separate counter
int myMsgCount = 1;

// this function runs BY ITSELF whenever the radio receives something
// we never call this ourselves - the radio calls it automatically
void onReceive() {
  messageArrived = true;   // just raise a flag saying "something came in!"
}

// ===================================================================
// SETUP - runs one time only, when the board first turns on
// ===================================================================
void setup() {
  Serial.begin(115200);   // start Serial so we can see messages on the computer

  pinMode(PRG_PIN, INPUT_PULLUP);   // the PRG button reads LOW when pressed

  // turn on power to the OLED screen (this board needs this step first)
  pinMode(36, OUTPUT);
  digitalWrite(36, LOW);
  delay(50);   // small wait so the screen is ready

  u8g2.begin();            // start the OLED screen
  u8g2.setContrast(255);   // 255 = brightest possible

  radio.begin(866.0);              // start the LoRa radio at 866 MHz
  radio.setDio1Action(onReceive);  // "call onReceive() when a message shows up"
  radio.startReceive();            // start listening for messages right away

  // show the starting screen: which pager this is, and what to do
  showOnScreen("Pager " MY_ID, "Waiting / PRG=Send");
}

// ===================================================================
// LOOP - runs over and over, forever, automatically
// ===================================================================
void loop() {

  // ---- PART 1: check if a message arrived ----
  if (messageArrived) {
    messageArrived = false;   // put the flag back down, we're handling it now

    String received;
    radio.readData(received);   // grab the message that came in

    // the message looks like "1:5:Hello"
    // meaning: senderId : messageNumber : text
    // we find the two ":" symbols and cut the string into 3 pieces
    int firstColon  = received.indexOf(':');
    int secondColon = received.indexOf(':', firstColon + 1);

    String fromId = received.substring(0, firstColon);              // e.g. "1"
    String msgNum = received.substring(firstColon + 1, secondColon); // e.g. "5"
    String text   = received.substring(secondColon + 1);             // e.g. "Hello"

    // print what we got to the Serial monitor (for checking on computer)
    Serial.println("Got msg #" + msgNum + " from Pager " + fromId + ": " + text);

    // show it on the OLED screen: header says who + which message number
    String header = "From P" + fromId + " #" + msgNum;
    showOnScreen(header.c_str(), text.c_str());

    radio.startReceive();   // go back to listening for the next message
  }

  // ---- PART 2: check if the PRG button is currently pressed ----
  if (digitalRead(PRG_PIN) == LOW) {

    // build the message to send: "myId:myMsgCount:Hello"  e.g. "1:1:Hello"
    String outgoing = String(MY_ID) + ":" + String(myMsgCount) + ":Hello";
    radio.transmit(outgoing.c_str());   // send it out through the air using LoRa

    // print what we sent to Serial monitor (for checking on computer)
    Serial.println("Sent msg #" + String(myMsgCount) + ": Hello");

    // show it on our own screen: confirms what we just sent
    String header = "Sent by P" MY_ID " #" + String(myMsgCount);
    showOnScreen(header.c_str(), "Hello");

    myMsgCount++;   // next message we send will be #2, then #3, and so on

    delay(300);             // small wait so one button press = one message only
    radio.startReceive();   // go back to listening after sending
  }
}

// ===================================================================
// Draws 2 lines of text on the OLED screen:
// line1 = small header text (top)
// line2 = bigger message text (below it)
// ===================================================================
void showOnScreen(const char* line1, const char* line2) {
  u8g2.clearBuffer();   // start with a blank "invisible notebook page"

  u8g2.setFont(u8g2_font_6x10_tf);   // pick small font
  u8g2.drawStr(0, 15, line1);        // draw the header near the top

  u8g2.setFont(u8g2_font_9x18_tf);   // pick bigger font
  u8g2.drawStr(0, 40, line2);        // draw the message below the header

  u8g2.sendBuffer();   // now actually show everything on the real screen
}
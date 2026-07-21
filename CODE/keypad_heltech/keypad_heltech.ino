#include <Keypad.h>     // library that lets us read the 4x4 keypad
#include <U8g2lib.h>    // library that lets us draw text on the OLED screen
#include <Wire.h>       // needed for the screen to talk to the board

// sets up the OLED screen and tells it which pins it is connected to
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/21, /*clock=*/18, /*data=*/17);

// ===================================================================
// KEYPAD SETUP
// This tells the code what each button on the 4x4 keypad is called,
// and which pins the rows and columns are wired to.
// ===================================================================
const byte ROWS = 4, COLS = 4;   // the keypad has 4 rows and 4 columns

// this grid matches the physical layout of buttons on the keypad
char keyMap[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {38, 1, 2, 3};   // which pins the 4 rows are wired to
byte colPins[COLS] = {4, 5, 6, 7};    // which pins the 4 columns are wired to

// this creates our keypad object, using the layout and pins above
Keypad keypad = Keypad(makeKeymap(keyMap), rowPins, colPins, ROWS, COLS);

// ===================================================================
// PREDEFINED MESSAGES
// Each button (A, B, C, D) has its own fixed message.
// Change the text here if you want different messages.
// ===================================================================
const char* MSG_A = "I need help, please come now";
const char* MSG_B = "I am at the meeting";
const char* MSG_C = "Where are you?";
const char* MSG_D = "Task done, acknowledged";

// ===================================================================
// SETUP - runs once when the board turns on
// ===================================================================
void setup() {
  Serial.begin(115200);

  // turn on power to the OLED screen
  pinMode(36, OUTPUT);
  digitalWrite(36, LOW);
  delay(50);

  u8g2.begin();
  u8g2.setContrast(255);   // max brightness

  showOnScreen("Ready", "Press A/B/C/D");
}

// ===================================================================
// LOOP - runs forever, checking for a key press each time
// ===================================================================
void loop() {

  char key = keypad.getKey();   // check if any button is currently pressed

  if (!key) return;   // nothing pressed - do nothing, check again next time

  Serial.print("Key pressed: ");
  Serial.println(key);

  // pick which message matches the key that was pressed
  String msg;
  switch (key) {
    case 'A': msg = MSG_A; break;
    case 'B': msg = MSG_B; break;
    case 'C': msg = MSG_C; break;
    case 'D': msg = MSG_D; break;
    default:  return;   // if it's not A/B/C/D, do nothing
  }

  // show the matching message on the screen
  showOnScreen("Message:", msg.c_str());
}

// ===================================================================
// Draws 2 lines of text on the OLED screen:
// line1 = small header text (top)
// line2 = bigger message text (below it)
// ===================================================================
void showOnScreen(const char* line1, const char* line2) {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 15, line1);

  u8g2.setFont(u8g2_font_7x13_tf);   // slightly smaller than before, since some messages are long
  u8g2.drawStr(0, 40, line2);

  u8g2.sendBuffer();
}
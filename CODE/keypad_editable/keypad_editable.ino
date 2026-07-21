#include <Keypad.h>
#include <U8g2lib.h>
#include <Wire.h>

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/21, /*clock=*/18, /*data=*/17);

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

// ===================================================================
// FIXED MESSAGES (A, B, C, D) - one press, sends right away
// ===================================================================
const char* MSG_A = "I need help, please come now";
const char* MSG_B = "I am at the meeting";
const char* MSG_C = "Where are you?";
const char* MSG_D = "Task done, acknowledged";

// ===================================================================
// EDITABLE TEMPLATES (1, 2, 3) - each has fixed text before/after,
// but the number in the middle is typed in by you
// ===================================================================
const char* TMPL_1_PRE = "Will be back in ";
const char* TMPL_1_SUF = " min";

const char* TMPL_2_PRE = "Meet me at floor ";
const char* TMPL_2_SUF = "";

const char* TMPL_3_PRE = "Call me in ";
const char* TMPL_3_SUF = " min";

// ===================================================================
// EDIT MODE STATE
// editMode = true means we are currently typing a number for a template
// activeTemplate remembers WHICH template (1, 2, or 3) we are editing
// typedNumber stores the digits typed so far, as text
// ===================================================================
bool editMode        = false;
char activeTemplate  = 0;
String typedNumber   = "";

void setup() {
  Serial.begin(115200);

  pinMode(36, OUTPUT);
  digitalWrite(36, LOW);
  delay(50);

  u8g2.begin();
  u8g2.setContrast(255);

  showOnScreen("Ready", "A-D=msg 1-3=type");
}

void loop() {

  char key = keypad.getKey();
  if (!key) return;   // nothing pressed, check again next loop

  Serial.print("Key pressed: ");
  Serial.println(key);

  // ---- IF WE ARE CURRENTLY IN EDIT MODE (typing a number) ----
  if (editMode) {
    handleEditKey(key);
    return;   // don't fall through to the normal key handling below
  }

  // ---- NOT IN EDIT MODE - check what kind of key this is ----

  // A, B, C, D = fixed messages, show immediately
  if (key == 'A') { showOnScreen("Message:", MSG_A); return; }
  if (key == 'B') { showOnScreen("Message:", MSG_B); return; }
  if (key == 'C') { showOnScreen("Message:", MSG_C); return; }
  if (key == 'D') { showOnScreen("Message:", MSG_D); return; }

  // 1, 2, 3 = start editing that template
  if (key == '1' || key == '2' || key == '3') {
    editMode       = true;
    activeTemplate = key;
    typedNumber    = "";        // start with nothing typed yet
    showTemplateScreen();
    return;
  }

  // any other key does nothing
}

// ===================================================================
// Handles key presses WHILE we are typing a number for a template
// ===================================================================
void handleEditKey(char key) {

  // if a digit (0-9) is pressed, add it to what we've typed so far
  if (key >= '0' && key <= '9') {
    typedNumber += key;         // add this digit to the end
    showTemplateScreen();       // update the screen to show it
    return;
  }

  // '*' cancels editing, goes back to the ready screen
  if (key == '*') {
    editMode = false;
    showOnScreen("Ready", "A-D=msg 1-3=type");
    return;
  }

  // '#' confirms - build the final message and show it
  if (key == '#') {
    String finalMsg = buildTemplateText();
    editMode = false;
    showOnScreen("Message:", finalMsg.c_str());
    return;
  }
}

// ===================================================================
// Builds the final text by joining: prefix + typed number + suffix
// Example: "Will be back in " + "5" + " min" = "Will be back in 5 min"
// ===================================================================
String buildTemplateText() {
  String pre, suf;

  if (activeTemplate == '1') { pre = TMPL_1_PRE; suf = TMPL_1_SUF; }
  if (activeTemplate == '2') { pre = TMPL_2_PRE; suf = TMPL_2_SUF; }
  if (activeTemplate == '3') { pre = TMPL_3_PRE; suf = TMPL_3_SUF; }

  return pre + typedNumber + suf;
}

// ===================================================================
// Shows the template screen WHILE typing - so you can see
// the number appear as you press digits
// ===================================================================
void showTemplateScreen() {
  String pre, suf;

  if (activeTemplate == '1') { pre = TMPL_1_PRE; suf = TMPL_1_SUF; }
  if (activeTemplate == '2') { pre = TMPL_2_PRE; suf = TMPL_2_SUF; }
  if (activeTemplate == '3') { pre = TMPL_3_PRE; suf = TMPL_3_SUF; }

  // show "__" if nothing typed yet, otherwise show what's typed
  String valueLine = (typedNumber.length() ? typedNumber : "__") + suf;

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 12, pre.c_str());

  u8g2.setFont(u8g2_font_9x18_tf);
  u8g2.drawStr(0, 35, valueLine.c_str());

  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 55, "# = show   * = cancel");

  u8g2.sendBuffer();
}

// ===================================================================
// Normal 2-line screen, used for Ready screen and fixed messages
// ===================================================================
void showOnScreen(const char* line1, const char* line2) {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 15, line1);

  u8g2.setFont(u8g2_font_7x13_tf);
  u8g2.drawStr(0, 40, line2);

  u8g2.sendBuffer();
}
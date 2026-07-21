#include <U8g2lib.h>   // library that knows how to control the OLED screen
#include <Wire.h>      // library needed for I2C communication (how the screen talks to the board)

// this line creates our screen object
// it tells the code which pins the screen is connected to
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ 21, /* clock=*/ 18, /* data=*/ 17);

void setup() {

  // the OLED needs power turned on first (this board has a power switch pin)
  pinMode(36, OUTPUT);     // tell pin 36 it will send power ON/OFF signal
  digitalWrite(36, LOW);   // LOW = turn power ON for the screen
  delay(50);               // wait a little bit so the screen is ready

  u8g2.begin();            // start the screen
  u8g2.setContrast(255);   // 255 = brightest the screen can go

  u8g2.clearBuffer();      // clear the "invisible notebook page" (screen not updated yet)

  // ---- top small line (title) ----
  u8g2.setFont(u8g2_font_6x10_tf);         // choose a small font
  u8g2.drawStr(0, 9, "Pager 1  866MHz");   // write text at x=0, y=9 (near top)

  u8g2.drawHLine(0, 11, 128);              // draw a line under the title

  // ---- big words ----
  u8g2.setFont(u8g2_font_9x18_tf);   // choose a bigger font
  u8g2.drawStr(0, 35, "Hello");      // write "Hello"
  u8g2.drawStr(0, 55, "Welcome");    // write "Welcome" below it

  u8g2.drawFrame(0, 0, 128, 64);     // draw a box border around the whole screen

  u8g2.sendBuffer();   // now actually show everything on the real screen
}

void loop() {
  // nothing here, screen stays the same forever
}
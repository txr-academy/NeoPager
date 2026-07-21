#include <U8g2lib.h>
#include <Wire.h>

// Heltec WiFi LoRa 32 V3 OLED pins: SDA=17, SCL=18, RST=21
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ 21, /* clock=*/ 18, /* data=*/ 17);

const char* message = "Hello World - Running Text Demo";
int16_t xPos;
int16_t textWidth;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Hello World");

  // Power up the OLED (Vext control)
  pinMode(36, OUTPUT);
  digitalWrite(36, LOW);
  delay(50);

  u8g2.begin();
  u8g2.setContrast(255);           // max brightness
  u8g2.setFont(u8g2_font_ncenB10_tr);

  textWidth = u8g2.getStrWidth(message);
  xPos = 128; // start off-screen to the right
}

void loop() {
  u8g2.clearBuffer();
  u8g2.drawStr(xPos, 30, message);
  u8g2.sendBuffer();

  xPos--; // move left by 1 pixel each frame

  if (xPos < -textWidth) {
    xPos = 128; // reset to right edge once fully scrolled off
  }

  delay(20); // controls scroll speed
}
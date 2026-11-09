#pragma once
#include <lvgl.h>

/* ---------- Display / touch (Waveshare ESP32-S3-Touch-LCD-3.5) ---------- */
#define GFX_BL        6
#define SPI_MISO      2
#define SPI_MOSI      1
#define SPI_SCLK      5
#define LCD_CS        -1
#define LCD_DC        3
#define LCD_RST       -1
#define SPI_FREQ_HZ   80000000

#define LCD_HOR_RES   480            /* landscape */
#define LCD_VER_RES   320
/* Set LCD_ROTATION to 1 or 3 depending on physical USB port orientation */
#define LCD_ROTATION  1

#define I2C_SDA       8
#define I2C_SCL       7

/* ---------- LVGL timing ----------
   Set to 0 if the two timer-period lines in display_driver.cpp fail to compile;
   then use LV_DISP_DEF_REFR_PERIOD 15 / LV_INDEV_DEF_READ_PERIOD 10 in lv_conf.h. */
#define SET_LVGL_TIMER_PERIODS  1
#define DISP_REFR_PERIOD_MS     15
#define INDEV_READ_PERIOD_MS    10

/* ---------- Modem (Heltec on Serial1) ---------- */
#define MODEM_RX_PIN     44          /* board receives  <- Heltec GPIO4 */
#define MODEM_TX_PIN     43          /* board transmits -> Heltec GPIO5 */
#define MODEM_BAUD       115200
#define LORA_HIST_MAX    (NUM_MESSAGES * LORA_PER_THREAD)   /* derived: last LORA_PER_THREAD messages of every chat */
#define LORA_TX_TIMEOUT  8000        /* ms to wait for STATUS:SENT/FAIL */
#define MODEM_SILENT_MS  15000
/* 1 = echo every sent/received message to the USB serial monitor. Set 0 for privacy. */
#define LORA_SERIAL_LOG  1

/* ---------- Messages ---------- */
#define LORA_IDX      0              /* index of the LoRa thread in messages[] */
#define MAX_CONTACTS     8           /* contacts you can add on the display (slots 1..8) */
#define NUM_MESSAGES     (1 + MAX_CONTACTS)   /* slot 0 = "LoRa Radio" (untagged) + contact slots */
#define LORA_PER_THREAD  5           /* messages kept per chat, in RAM and in flash */
#define LORA_MAX_PAYLOAD 40          /* must equal MAX_MSG_BYTES in the Heltec app_config.h */
#define MAX_TAG_LEN      4           /* contact ID length (letters / digits) */
#define MAX_NAME_LEN     12

/* ---------- Messages lock ---------- */
#define MSG_PIN   "1234"
#define PIN_LEN   4                  /* must equal strlen(MSG_PIN) */

/* ---------- Fonts for Home / PIN (fall back to the default font if not enabled in lv_conf.h) ---------- */
#if defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
  #define FONT_XL (&lv_font_montserrat_28)
#elif defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
  #define FONT_XL (&lv_font_montserrat_24)
#else
  #define FONT_XL LV_FONT_DEFAULT
#endif

#if defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
  #define FONT_L (&lv_font_montserrat_20)
#elif defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
  #define FONT_L (&lv_font_montserrat_16)
#else
  #define FONT_L LV_FONT_DEFAULT
#endif

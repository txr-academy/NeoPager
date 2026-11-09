#include "display_driver.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include "TCA9554.h"
#include "TouchDrvFT6X36.hpp"
#include "esp_heap_caps.h"

static TCA9554 TCA(0x20);
static Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, SPI_SCLK, SPI_MOSI, SPI_MISO);
static Arduino_GFX *gfx = new Arduino_ST7796(bus, LCD_RST, LCD_ROTATION, true, 320, 480);
static TouchDrvFT6X36 touch;

static uint32_t screenWidth;
static uint32_t screenHeight;
static uint32_t bufSize;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf1;
static lv_color_t *disp_draw_buf2;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

/* Display flushing */
static void my_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
#ifndef DIRECT_MODE
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
#endif
#endif

  lv_disp_flush_ready(drv);
}

/* Touch coordinate transformer with cached release points */
static void my_touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  int16_t x[1], y[1];
  uint8_t touched = touch.getPoint(x, y, 1);
  static int16_t last_x = 0;
  static int16_t last_y = 0;

  if (touched) {
    data->state = LV_INDEV_STATE_PR;

#if LCD_ROTATION == 1
    data->point.x = y[0];
    data->point.y = 319 - x[0];
#elif LCD_ROTATION == 3
    data->point.x = 479 - y[0];
    data->point.y = x[0];
#else
    data->point.x = x[0];
    data->point.y = y[0];
#endif

    if (data->point.x < 0) data->point.x = 0;
    if (data->point.x >= (int32_t)screenWidth) data->point.x = screenWidth - 1;
    if (data->point.y < 0) data->point.y = 0;
    if (data->point.y >= (int32_t)screenHeight) data->point.y = screenHeight - 1;

    last_x = data->point.x;
    last_y = data->point.y;
  } else {
    data->state = LV_INDEV_STATE_REL;
    data->point.x = last_x;
    data->point.y = last_y;
  }
}

static void lcd_reset(void) {
  TCA.write1(1, 1);
  delay(10);
  TCA.write1(1, 0);
  delay(10);
  TCA.write1(1, 1);
  delay(150);
}

void display_init(void) {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  TCA.begin();
  TCA.pinMode1(1, OUTPUT);
  lcd_reset();

  if (!touch.begin(Wire, FT6X36_SLAVE_ADDRESS)) {
    Serial.println("Failed to find FT6X36");
    while (1) { delay(1000); }
  }

  if (!gfx->begin(SPI_FREQ_HZ)) {
    Serial.println("gfx->begin() failed!");
  }

  gfx->fillScreen(RGB565_BLACK);

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif

  lv_init();

  screenWidth = gfx->width();
  screenHeight = gfx->height();
  bufSize = screenWidth * 40;

  /* Two 480x40 buffers in internal DMA RAM (PSRAM is slower for display transfers) */
  disp_draw_buf1 = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!disp_draw_buf1) {
    Serial.println("[disp] buffer 1: internal DMA alloc failed, using default heap");
    disp_draw_buf1 = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_DEFAULT);
  }
  disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!disp_draw_buf2) {
    Serial.println("[disp] buffer 2: internal DMA alloc failed, using default heap");
    disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_DEFAULT);
  }

  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf1, disp_draw_buf2, bufSize);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  indev_drv.scroll_limit = 3;
  indev_drv.scroll_throw = 0;    /* 0 inertia overhead */
  lv_indev_t *indev = lv_indev_drv_register(&indev_drv);

#if SET_LVGL_TIMER_PERIODS
  /* Depends on the LVGL 8 struct layout. If this fails to compile, set
     SET_LVGL_TIMER_PERIODS to 0 in config.h and use the lv_conf.h defines. */
  if (disp && disp->refr_timer) lv_timer_set_period(disp->refr_timer, DISP_REFR_PERIOD_MS);
  if (indev && indev->driver && indev->driver->read_timer)
    lv_timer_set_period(indev->driver->read_timer, INDEV_READ_PERIOD_MS);
#else
  (void)disp; (void)indev;
#endif
}

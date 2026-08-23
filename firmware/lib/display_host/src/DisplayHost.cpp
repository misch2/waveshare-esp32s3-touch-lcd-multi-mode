#include "DisplayHost.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_timer.h>
#include <lvgl.h>

#include "Display_ST7701.h"
#include "Touch_CST820.h"

namespace {
lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
TouchSampleCallback sampleCallback = nullptr;
void* frameBuffer1 = nullptr;
void* frameBuffer2 = nullptr;

void flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area,
                  lv_color_t* pixels) {
  LCD_addWindow(area->x1, area->y1, area->x2, area->y2,
                reinterpret_cast<uint8_t*>(&pixels->full));
  lv_disp_flush_ready(driver);
}

void readTouch(lv_indev_drv_t*, lv_indev_data_t* data) {
  Touch_Read_Data();
  const bool pressed = touch_data.points > 0;
  if (pressed) {
    data->point.x = touch_data.x;
    data->point.y = touch_data.y;
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }

  if (sampleCallback != nullptr) {
    sampleCallback(pressed, touch_data.x, touch_data.y, millis());
  }
  // The upstream CST820 reader only overwrites this field for a positive
  // sample. Clear it after every observation so a release is visible.
  touch_data.points = 0;
  touch_data.gesture = NONE;
}

void increaseTick(void*) { lv_tick_inc(2); }
}  // namespace

bool displayHostBegin(TouchSampleCallback touchCallback) {
  sampleCallback = touchCallback;
  lv_init();

  if (esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &frameBuffer1,
                                         &frameBuffer2) != ESP_OK ||
      frameBuffer1 == nullptr || frameBuffer2 == nullptr) {
    return false;
  }
  lv_disp_draw_buf_init(&drawBuffer, frameBuffer1, frameBuffer2, 480 * 480);

  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = 480;
  displayDriver.ver_res = 480;
  displayDriver.flush_cb = flushDisplay;
  displayDriver.full_refresh = 1;
  displayDriver.draw_buf = &drawBuffer;
  if (lv_disp_drv_register(&displayDriver) == nullptr) return false;

  static lv_indev_drv_t inputDriver;
  lv_indev_drv_init(&inputDriver);
  inputDriver.type = LV_INDEV_TYPE_POINTER;
  inputDriver.read_cb = readTouch;
  inputDriver.long_press_time = 800;
  if (lv_indev_drv_register(&inputDriver) == nullptr) return false;

  const esp_timer_create_args_t tickTimerArgs = {
      .callback = increaseTick,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lvgl-tick",
      .skip_unhandled_events = true,
  };
  esp_timer_handle_t tickTimer = nullptr;
  if (esp_timer_create(&tickTimerArgs, &tickTimer) != ESP_OK) return false;
  return esp_timer_start_periodic(tickTimer, 2000) == ESP_OK;
}

void displayHostLoop() { lv_timer_handler(); }

void displayHostResync() { LCD_Resync(); }

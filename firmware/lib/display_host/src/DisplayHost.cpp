#include "DisplayHost.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <lvgl.h>

#include "Display_ST7701.h"
#include "Touch_CST820.h"

namespace {
constexpr uint32_t kPanelPixelClockHz = 8U * 1000U * 1000U;
constexpr uint32_t kVsyncTimeoutMs = 100;

lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
TouchSampleCallback sampleCallback = nullptr;
void* frameBuffer1 = nullptr;
void* frameBuffer2 = nullptr;
uint8_t currentBrightness = 35;
bool forcedOff = false;
SemaphoreHandle_t vsyncSemaphore = nullptr;
portMUX_TYPE vsyncMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t vsyncGeneration = 0;
uint32_t flushVsyncTimeoutCount = 0;
uint32_t resyncVsyncTimeoutCount = 0;
uint32_t lastVsyncTimeoutLogMs = 0;
uint32_t completedFlushCount = 0;
uint32_t flushCountAtResyncRequest = 0;
bool resyncPending = false;

bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t,
                       const esp_lcd_rgb_panel_event_data_t*, void*) {
  BaseType_t highPriorityTaskWoken = pdFALSE;
  portENTER_CRITICAL_ISR(&vsyncMux);
  ++vsyncGeneration;
  portEXIT_CRITICAL_ISR(&vsyncMux);
  if (vsyncSemaphore != nullptr) {
    xSemaphoreGiveFromISR(vsyncSemaphore, &highPriorityTaskWoken);
  }
  return highPriorityTaskWoken == pdTRUE;
}

uint32_t currentVsyncGeneration() {
  portENTER_CRITICAL(&vsyncMux);
  const uint32_t generation = vsyncGeneration;
  portEXIT_CRITICAL(&vsyncMux);
  return generation;
}

void logVsyncTimeout(const char* phase, uint32_t& counter) {
  ++counter;
  const uint32_t nowMs = millis();
  if (nowMs - lastVsyncTimeoutLogMs < 1000) return;
  lastVsyncTimeoutLogMs = nowMs;
  Serial.printf("Warning: LCD VSYNC timeout during %s (%lu total)\n", phase,
                static_cast<unsigned long>(counter));
}

bool waitForVsyncAfter(uint32_t generation, const char* phase,
                       uint32_t& timeoutCounter) {
  if (vsyncSemaphore == nullptr) return false;

  const uint32_t startedAt = millis();
  while (currentVsyncGeneration() == generation) {
    const uint32_t elapsed = millis() - startedAt;
    if (elapsed >= kVsyncTimeoutMs) {
      logVsyncTimeout(phase, timeoutCounter);
      return false;
    }

    const uint32_t remainingMs = kVsyncTimeoutMs - elapsed;
    // A binary semaphore token may predate the operation being synchronized.
    // The generation check above is authoritative; stale tokens are consumed
    // and we continue waiting for a genuinely newer VSYNC callback.
    xSemaphoreTake(vsyncSemaphore, pdMS_TO_TICKS(remainingMs));
  }
  return true;
}

void flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area,
                  lv_color_t* pixels) {
  LCD_addWindow(area->x1, area->y1, area->x2, area->y2,
                reinterpret_cast<uint8_t*>(&pixels->full));

  // esp_lcd switches a double-buffered RGB framebuffer at a frame boundary.
  // Do not let LVGL reuse the previous scanout buffer before that swap has
  // actually happened, otherwise full-refresh drawing can corrupt rows still
  // being consumed by the panel.
  const uint32_t submittedGeneration = currentVsyncGeneration();
  waitForVsyncAfter(submittedGeneration, "frame flush",
                    flushVsyncTimeoutCount);
  ++completedFlushCount;
  lv_disp_flush_ready(driver);
}

void servicePendingResync() {
  if (!resyncPending || forcedOff ||
      completedFlushCount == flushCountAtResyncRequest) {
    return;
  }

  // A save may arrive immediately after a full-frame flush. Wait for another
  // clean frame boundary before asking the RGB driver to restart, then require
  // a strictly newer VSYNC generation so a token from before the restart
  // cannot make the recovery look complete.
  const uint32_t beforeSafeBoundary = currentVsyncGeneration();
  waitForVsyncAfter(beforeSafeBoundary, "pre-resync boundary",
                    resyncVsyncTimeoutCount);

  resyncPending = false;
  const uint32_t beforeRestart = currentVsyncGeneration();
  LCD_Resync();
  waitForVsyncAfter(beforeRestart, "panel resync",
                    resyncVsyncTimeoutCount);

  // Present one normal LVGL frame after the restart. This is deliberately not
  // a second LCD restart like the original clock firmware used after Save.
  lv_obj_invalidate(lv_scr_act());
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

  // The combined UI performs frequent full-frame PSRAM redraws. Slow the RGB
  // scanout from the clock driver's 14 MHz default to the 8 MHz timing used by
  // MeteoPlaneRadar, reducing pressure on the shared PSRAM bus. ESP-IDF applies
  // the requested clock safely at the next VSYNC boundary.
  if (esp_lcd_rgb_panel_set_pclk(panel_handle, kPanelPixelClockHz) != ESP_OK) {
    return false;
  }

  if (esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &frameBuffer1,
                                         &frameBuffer2) != ESP_OK ||
      frameBuffer1 == nullptr || frameBuffer2 == nullptr) {
    return false;
  }

  vsyncSemaphore = xSemaphoreCreateBinary();
  if (vsyncSemaphore == nullptr) return false;
  esp_lcd_rgb_panel_event_callbacks_t panelCallbacks = {};
  panelCallbacks.on_vsync = onVsync;
  if (esp_lcd_rgb_panel_register_event_callbacks(
          panel_handle, &panelCallbacks, nullptr) != ESP_OK) {
    vSemaphoreDelete(vsyncSemaphore);
    vsyncSemaphore = nullptr;
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

void displayHostLoop() {
  lv_timer_handler();
  servicePendingResync();
}

void displayHostRequestResync() {
  resyncPending = true;
  flushCountAtResyncRequest = completedFlushCount;
  lv_obj_invalidate(lv_scr_act());
}

void displayHostSetBrightness(uint8_t brightness) {
  currentBrightness = constrain(brightness, 1, 100);
  if (!forcedOff) Set_Backlight(currentBrightness);
}

void displayHostSetForcedOff(bool off) {
  if (forcedOff == off) return;
  forcedOff = off;
  if (forcedOff) {
    Set_Backlight(0);
    LCD_Sleep();
    return;
  }

  LCD_Wake();
  // LCD_Wake already restarts the RGB panel. Do not follow it with another
  // restart requested while the display was asleep.
  resyncPending = false;
  lv_obj_invalidate(lv_scr_act());
  Set_Backlight(currentBrightness);
}

bool displayHostForcedOff() { return forcedOff; }

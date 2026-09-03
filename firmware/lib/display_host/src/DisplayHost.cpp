#include "DisplayHost.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <lvgl.h>
#include <cstring>

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
uint32_t lastVsyncTimeoutLogMs = 0;
bool storageWriteSuspended = false;
bool partialRefreshRequested = false;
uint8_t partialRefreshWarmupFrames = 0;
bool partialRefreshWarmupRequested = false;
bool partialRefreshEnableRequested = false;

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

esp_lcd_rgb_panel_config_t makePanelConfig() {
  esp_lcd_rgb_panel_config_t config = {};
  config.clk_src = LCD_CLK_SRC_DEFAULT;
  config.timings.pclk_hz = kPanelPixelClockHz;
  config.timings.h_res = ESP_PANEL_LCD_HEIGHT;
  config.timings.v_res = ESP_PANEL_LCD_WIDTH;
  config.timings.hsync_pulse_width = ESP_PANEL_LCD_RGB_TIMING_HPW;
  config.timings.hsync_back_porch = ESP_PANEL_LCD_RGB_TIMING_HBP;
  config.timings.hsync_front_porch = ESP_PANEL_LCD_RGB_TIMING_HFP;
  config.timings.vsync_pulse_width = ESP_PANEL_LCD_RGB_TIMING_VPW;
  config.timings.vsync_back_porch = ESP_PANEL_LCD_RGB_TIMING_VBP;
  config.timings.vsync_front_porch = ESP_PANEL_LCD_RGB_TIMING_VFP;
  config.timings.flags.hsync_idle_low = 0;
  config.timings.flags.vsync_idle_low = 0;
  config.timings.flags.de_idle_high = 0;
  config.timings.flags.pclk_active_neg = false;
  config.timings.flags.pclk_idle_high = 0;
  config.data_width = ESP_PANEL_LCD_RGB_DATA_WIDTH;
  config.bits_per_pixel = ESP_PANEL_LCD_RGB_PIXEL_BITS;
  config.num_fbs = ESP_PANEL_LCD_RGB_FRAME_BUF_NUM;
  config.bounce_buffer_size_px = ESP_PANEL_LCD_RGB_BOUNCE_BUF_SIZE;
  config.psram_trans_align = 64;
  config.hsync_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_HSYNC;
  config.vsync_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_VSYNC;
  config.de_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_DE;
  config.pclk_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_PCLK;
  config.disp_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_DISP;
  const int dataGpios[] = {
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA0,  ESP_PANEL_LCD_PIN_NUM_RGB_DATA1,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA2,  ESP_PANEL_LCD_PIN_NUM_RGB_DATA3,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA4,  ESP_PANEL_LCD_PIN_NUM_RGB_DATA5,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA6,  ESP_PANEL_LCD_PIN_NUM_RGB_DATA7,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA8,  ESP_PANEL_LCD_PIN_NUM_RGB_DATA9,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA10, ESP_PANEL_LCD_PIN_NUM_RGB_DATA11,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA12, ESP_PANEL_LCD_PIN_NUM_RGB_DATA13,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA14, ESP_PANEL_LCD_PIN_NUM_RGB_DATA15,
  };
  for (size_t index = 0; index < ESP_PANEL_LCD_RGB_DATA_WIDTH; ++index) {
    config.data_gpio_nums[index] = dataGpios[index];
  }
  config.flags.disp_active_low = 0;
  config.flags.refresh_on_demand = 0;
  config.flags.fb_in_psram = true;
  config.flags.double_fb = true;
  config.flags.no_fb = 0;
  config.flags.bb_invalidate_cache = 0;
  return config;
}

bool bindPanelBuffersAndCallbacks(esp_lcd_panel_handle_t handle,
                                  void*& firstBuffer, void*& secondBuffer) {
  if (esp_lcd_rgb_panel_get_frame_buffer(handle, 2, &firstBuffer,
                                         &secondBuffer) != ESP_OK ||
      firstBuffer == nullptr || secondBuffer == nullptr) {
    return false;
  }

  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_vsync = onVsync;
  return esp_lcd_rgb_panel_register_event_callbacks(handle, &callbacks,
                                                     nullptr) == ESP_OK;
}

bool createFreshPanel() {
  esp_lcd_rgb_panel_config_t config = makePanelConfig();
  esp_lcd_panel_handle_t newPanel = nullptr;
  esp_err_t result = esp_lcd_new_rgb_panel(&config, &newPanel);
  void* newFrameBuffer1 = nullptr;
  void* newFrameBuffer2 = nullptr;
  if (result == ESP_OK &&
      !bindPanelBuffersAndCallbacks(newPanel, newFrameBuffer1,
                                    newFrameBuffer2)) {
    result = ESP_FAIL;
  }
  if (result == ESP_OK) result = esp_lcd_panel_reset(newPanel);
  if (result == ESP_OK) result = esp_lcd_panel_init(newPanel);
  if (result != ESP_OK) {
    Serial.printf("Error: LCD driver recreation failed (0x%x)\n",
                  static_cast<unsigned>(result));
    if (newPanel != nullptr) esp_lcd_panel_del(newPanel);
    return false;
  }

  panel_handle = newPanel;
  frameBuffer1 = newFrameBuffer1;
  frameBuffer2 = newFrameBuffer2;
  lv_disp_draw_buf_init(&drawBuffer, frameBuffer1, frameBuffer2, 480 * 480);

  const uint32_t generation = currentVsyncGeneration();
  const bool synchronized = waitForVsyncAfter(
      generation, "RGB driver recreation", flushVsyncTimeoutCount);
  if (!synchronized) return false;
  lv_obj_invalidate(lv_scr_act());
  return true;
}

void flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area,
                  lv_color_t* pixels) {
  (void)area;
  if (!lv_disp_flush_is_last(driver)) {
    lv_disp_flush_ready(driver);
    return;
  }
  // Direct mode passes a complete physical framebuffer even when only a few
  // LVGL areas changed. Do not call upstream LCD_addWindow: v1.7.2 gives it a
  // separate bounce-frame semaphore, whereas this host owns the VSYNC callback.
  ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 480, 480, pixels));

  // esp_lcd switches a double-buffered RGB framebuffer at a frame boundary.
  // Do not let LVGL reuse the previous scanout buffer before that swap has
  // actually happened, otherwise full-refresh drawing can corrupt rows still
  // being consumed by the panel.
  const uint32_t submittedGeneration = currentVsyncGeneration();
  const bool presented = waitForVsyncAfter(submittedGeneration, "frame flush",
                                           flushVsyncTimeoutCount);
  if (!presented) {
    // Continuing would give LVGL a buffer that may still be scanned out.
    // Never conceal a failed gate or attempt an in-stream panel restart.
    ESP_ERROR_CHECK(ESP_ERR_TIMEOUT);
  }
  // Preserve the upstream analog warm-up: two full frames, then synchronize
  // the now-inactive buffer before enabling partial rendering. The host keeps
  // its normal 20-line bounce pipeline and VSYNC gate throughout.
  if (partialRefreshWarmupFrames > 0 && --partialRefreshWarmupFrames == 0) {
    void* other = pixels == frameBuffer1 ? frameBuffer2
                   : pixels == frameBuffer2 ? frameBuffer1 : nullptr;
    if (other == nullptr) {
      partialRefreshWarmupFrames = 2;
      partialRefreshWarmupRequested = true;
    } else {
      std::memcpy(other, pixels, 480 * 480 * sizeof(lv_color_t));
      partialRefreshEnableRequested = true;
    }
  } else if (partialRefreshWarmupFrames > 0) {
    partialRefreshWarmupRequested = true;
  }
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

  // The combined UI performs frequent full-frame PSRAM redraws. Slow the RGB
  // scanout from the clock driver's 12 MHz default to the 8 MHz timing used by
  // MeteoPlaneRadar, reducing pressure on the shared PSRAM bus. ESP-IDF applies
  // the requested clock safely at the next VSYNC boundary.
  if (!LCD_SetPixelClock(kPanelPixelClockHz)) {
    return false;
  }

  vsyncSemaphore = xSemaphoreCreateBinary();
  if (vsyncSemaphore == nullptr) return false;
  if (!bindPanelBuffersAndCallbacks(panel_handle, frameBuffer1, frameBuffer2)) {
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
  if (storageWriteSuspended) return;
  lv_timer_handler();
  if (partialRefreshEnableRequested) {
    partialRefreshEnableRequested = false;
    displayDriver.full_refresh = 0;
    displayDriver.direct_mode = 1;
  }
  if (partialRefreshWarmupRequested) {
    partialRefreshWarmupRequested = false;
    displayHostRequestFullRedraw();
  }
}

void displayHostSetPartialRefresh(bool enabled, bool rebuildBuffers) {
  partialRefreshRequested = enabled;
  if (!rebuildBuffers && enabled &&
      (displayDriver.direct_mode || partialRefreshWarmupFrames > 0)) return;
  if (!rebuildBuffers && !enabled && !displayDriver.direct_mode &&
      partialRefreshWarmupFrames == 0) return;
  displayDriver.direct_mode = 0;
  displayDriver.full_refresh = 1;
  partialRefreshWarmupFrames = enabled ? 2 : 0;
  partialRefreshWarmupRequested = false;
  partialRefreshEnableRequested = false;
  if (!storageWriteSuspended) displayHostRequestFullRedraw();
}

void displayHostRequestFullRedraw() {
  // Configuration persistence can temporarily compete for memory bandwidth,
  // but restarting RGB DMA after the write can itself leave the controller at
  // a stable, cyclically shifted scan-line origin. A VSYNC-gated redraw is all
  // that is needed to present the new LVGL state.
  lv_obj_invalidate(lv_scr_act());
}

bool displayHostBeginStorageWrite() {
  if (storageWriteSuspended) return true;
  if (drawBuffer.flushing != 0) {
    Serial.println("Error: refusing to delete LCD driver during an LVGL flush");
    return false;
  }

  // In bounce-buffer mode the EOF ISR copies the next rows from a PSRAM frame
  // buffer. NVS/flash writes disable the external-memory cache in the bundled
  // Arduino framework, so continuous scanout cannot be kept coherent. The RGB
  // panel reset API does not stop GDMA or discard the driver's bounce state;
  // delete the complete driver object while no LVGL flush is active instead.
  const uint32_t generation = currentVsyncGeneration();
  if (!waitForVsyncAfter(generation, "storage-write boundary",
                         flushVsyncTimeoutCount)) {
    return false;
  }
  if (!forcedOff) Set_Backlight(0);

  const esp_err_t result = esp_lcd_panel_del(panel_handle);
  if (result != ESP_OK) {
    Serial.printf("Error: LCD driver stop failed before storage write (0x%x)\n",
                  static_cast<unsigned>(result));
    if (!forcedOff) Set_Backlight(currentBrightness);
    return false;
  }

  panel_handle = nullptr;
  frameBuffer1 = nullptr;
  frameBuffer2 = nullptr;
  storageWriteSuspended = true;
  Serial.println("Display: RGB driver stopped for storage write");
  return true;
}

bool displayHostEndStorageWrite() {
  if (!storageWriteSuspended) return true;

  const bool started = createFreshPanel();
  if (!started) return false;

  storageWriteSuspended = false;
  displayHostSetPartialRefresh(partialRefreshRequested, true);
  if (!forcedOff) Set_Backlight(currentBrightness);
  Serial.println("Display: RGB driver recreated after storage write");
  return true;
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
  Set_Backlight(currentBrightness);
}

bool displayHostForcedOff() { return forcedOff; }

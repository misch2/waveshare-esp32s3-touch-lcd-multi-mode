#include "RadarScreen.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>

#ifdef BOOT_PIN
#undef BOOT_PIN
#endif

#include "CHMU.h"
#include "DisplayHost.h"
#include "MeteoSettingsAdapter.h"
#include "Net.h"
#include "NetworkFetchGate.h"
#include "NetworkHost.h"
#include "RainViewer.h"
#include "ScreenWeather.h"
#include "UI.h"

namespace {
constexpr int16_t kDisplayWidth = 480;
constexpr int16_t kDisplayHeight = 480;
constexpr uint32_t kRangeFeedbackDurationMs = 900;

class HostRadarCanvas final : public Arduino_GFX {
 public:
  HostRadarCanvas() : Arduino_GFX(kDisplayWidth, kDisplayHeight) {}

  ~HostRadarCanvas() override {
    if (frameBuffer_ != nullptr) heap_caps_free(frameBuffer_);
  }

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    (void)speed;
    if (frameBuffer_ == nullptr) {
      frameBuffer_ = static_cast<uint16_t*>(heap_caps_malloc(
          static_cast<size_t>(WIDTH) * HEIGHT * sizeof(uint16_t),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    return frameBuffer_ != nullptr;
  }

  uint16_t* getFramebuffer() { return frameBuffer_; }

  void setPresentCallback(void (*callback)()) { presentCallback_ = callback; }

  void flush(bool forceFlush = false) override {
    (void)forceFlush;
    if (presentCallback_ != nullptr) presentCallback_();
  }

  void writePixelPreclipped(int16_t x, int16_t y,
                            uint16_t color) override {
    if (frameBuffer_ == nullptr || x < 0 || x >= WIDTH || y < 0 ||
        y >= HEIGHT) {
      return;
    }
    frameBuffer_[static_cast<int32_t>(y) * WIDTH + x] = color;
  }

  void writeFastVLine(int16_t x, int16_t y, int16_t height,
                      uint16_t color) override {
    if (frameBuffer_ == nullptr || height == 0) return;
    if (height < 0) {
      y += height + 1;
      height = -height;
    }
    if (x < 0 || x >= WIDTH || y >= HEIGHT || y + height <= 0) return;
    if (y < 0) {
      height += y;
      y = 0;
    }
    if (y + height > HEIGHT) height = HEIGHT - y;
    uint16_t* pixel = frameBuffer_ + static_cast<int32_t>(y) * WIDTH + x;
    while (height-- > 0) {
      *pixel = color;
      pixel += WIDTH;
    }
  }

  void writeFastHLine(int16_t x, int16_t y, int16_t width,
                      uint16_t color) override {
    if (frameBuffer_ == nullptr || width == 0) return;
    if (width < 0) {
      x += width + 1;
      width = -width;
    }
    if (y < 0 || y >= HEIGHT || x >= WIDTH || x + width <= 0) return;
    if (x < 0) {
      width += x;
      x = 0;
    }
    if (x + width > WIDTH) width = WIDTH - x;
    uint16_t* pixel = frameBuffer_ + static_cast<int32_t>(y) * WIDTH + x;
    while (width-- > 0) *pixel++ = color;
  }

  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t width,
                               int16_t height, uint16_t color) override {
    if (frameBuffer_ == nullptr || width <= 0 || height <= 0) return;
    if (x < 0) {
      width += x;
      x = 0;
    }
    if (y < 0) {
      height += y;
      y = 0;
    }
    if (x + width > WIDTH) width = WIDTH - x;
    if (y + height > HEIGHT) height = HEIGHT - y;
    if (width <= 0 || height <= 0) return;

    uint16_t* row = frameBuffer_ + static_cast<int32_t>(y) * WIDTH + x;
    for (int16_t rowIndex = 0; rowIndex < height; ++rowIndex) {
      uint16_t* pixel = row;
      for (int16_t column = 0; column < width; ++column) *pixel++ = color;
      row += WIDTH;
    }
  }

 private:
  uint16_t* frameBuffer_ = nullptr;
  void (*presentCallback_)() = nullptr;
};

HostRadarCanvas radarCanvas;

void pollDuringMeteoTransfer() {
  // Upstream CHMI downloads are deliberately synchronous. Keep the sole host
  // display/input pipeline and Wi-Fi provisioning alive while they run; screen
  // dispatch itself remains in the outer main loop and is never re-entered.
  network_host::loop();
  displayHostLoop();
  delay(1);
}
}  // namespace

// The upstream renderers intentionally share one Arduino_GFX target. In the
// combined firmware that target is an off-screen PSRAM canvas, never the panel.
Arduino_GFX* gfx = nullptr;

RadarScreen* RadarScreen::instance_ = nullptr;

bool RadarScreen::begin() {
  if (instance_ != nullptr) return false;
  instance_ = this;

  if (!radarCanvas.begin()) {
    Serial.println("Error: Meteo radar canvas allocation failed");
    instance_ = nullptr;
    return false;
  }
  radarCanvas.setPresentCallback(&RadarScreen::presentFromCanvas);
  gfx = &radarCanvas;

  screen_ = lv_obj_create(nullptr);
  if (screen_ == nullptr) return false;
  lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);
  lv_obj_set_style_border_width(screen_, 0, 0);
  lv_obj_set_style_pad_all(screen_, 0, 0);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

  image_ = lv_img_create(screen_);
  if (image_ == nullptr) return false;

  imageDescriptor_.header.always_zero = 0;
  imageDescriptor_.header.w = kDisplayWidth;
  imageDescriptor_.header.h = kDisplayHeight;
  imageDescriptor_.header.cf = LV_IMG_CF_TRUE_COLOR;
  imageDescriptor_.data_size =
      static_cast<uint32_t>(kDisplayWidth) * kDisplayHeight * sizeof(uint16_t);
  imageDescriptor_.data = reinterpret_cast<const uint8_t*>(
      radarCanvas.getFramebuffer());
  lv_img_set_src(image_, &imageDescriptor_);
  lv_obj_center(image_);
  lv_obj_clear_flag(image_, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  rangeFeedback_ = lv_obj_create(screen_);
  if (rangeFeedback_ == nullptr) return false;
  lv_obj_set_size(rangeFeedback_, 190, 54);
  lv_obj_align(rangeFeedback_, LV_ALIGN_CENTER, 0, 95);
  lv_obj_set_style_radius(rangeFeedback_, 18, 0);
  lv_obj_set_style_bg_color(rangeFeedback_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(rangeFeedback_, LV_OPA_80, 0);
  lv_obj_set_style_border_color(rangeFeedback_, lv_color_hex(0x00BFFF), 0);
  lv_obj_set_style_border_width(rangeFeedback_, 2, 0);
  lv_obj_set_style_pad_all(rangeFeedback_, 0, 0);
  lv_obj_clear_flag(rangeFeedback_,
                    LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  rangeFeedbackLabel_ = lv_label_create(rangeFeedback_);
  if (rangeFeedbackLabel_ == nullptr) return false;
  lv_obj_set_style_text_color(rangeFeedbackLabel_, lv_color_hex(0xFFE066), 0);
  lv_obj_center(rangeFeedbackLabel_);
  lv_obj_add_flag(rangeFeedback_, LV_OBJ_FLAG_HIDDEN);

  CHMU_SetPollFn(pollDuringMeteoTransfer);
  Net_SetPollFn(pollDuringMeteoTransfer);
  RainViewer_SetPollFn(pollDuringMeteoTransfer);

  Serial.printf("Meteo radar: upstream renderer ready, canvas=%u B, PSRAM free=%u\n",
                static_cast<unsigned>(imageDescriptor_.data_size),
                static_cast<unsigned>(ESP.getFreePsram()));
  return true;
}

void RadarScreen::show() {
  visible_ = true;
  ScreenWeather_Enter();
  render();
  if (screen_ != nullptr) lv_scr_load(screen_);
}

void RadarScreen::hide() {
  visible_ = false;
  hideRangeFeedback();
  ScreenWeather_Leave();
  releaseFetchGate();
}

void RadarScreen::tick(uint32_t nowMs) {
  if (!visible_) return;

  if (rangeFeedbackVisible_ &&
      nowMs - rangeFeedbackShownAtMs_ >= kRangeFeedbackDurationMs) {
    hideRangeFeedback();
  }

  // CHMI fetches a complete animation in one call. RainViewer deliberately
  // spreads a tile burst over many calls while retaining one TLS session. Keep
  // one host lease for that whole burst so the clock worker cannot start a
  // competing TLS handshake in the small internal-RAM pool.
  if (!fetchGateHeld_) {
    if (!network_host::acquireFetchGate(0)) return;
    fetchGateHeld_ = true;
  }

  const bool redraw = ScreenWeather_Tick();
  const bool rainViewerBurst =
      meteo_settings::radarConfig().source ==
          app_core::MeteoRadarSource::RainViewer &&
      RainViewer_Busy();
  if (!rainViewerBurst) releaseFetchGate();

  if (redraw) {
    render();
  }
}

bool RadarScreen::handleGesture(const GestureEvent& event) {
  if (event.kind != GestureKind::VerticalSwipe) return false;

  // GestureRecognizer reports up as -1. The agreed module policy maps an
  // upward swipe to the next/wider upstream Meteo range.
  ScreenWeather_ChangeRange(event.direction < 0 ? 1 : -1);
  showRangeFeedback(event.direction);
  return true;
}

void RadarScreen::render() {
  if (gfx == nullptr || image_ == nullptr) return;
  ScreenWeather_Draw();
  present(false);
}

void RadarScreen::present(bool pumpDisplay) {
  if (image_ == nullptr || !visible_) return;
  lv_img_cache_invalidate_src(&imageDescriptor_);
  lv_obj_invalidate(image_);
  if (pumpDisplay) displayHostLoop();
}

void RadarScreen::showRangeFeedback(int8_t direction) {
  if (rangeFeedback_ == nullptr || rangeFeedbackLabel_ == nullptr || !visible_) {
    return;
  }

  char rangeText[32];
  ScreenWeather_RangeText(rangeText, sizeof(rangeText));
  lv_label_set_text_fmt(rangeFeedbackLabel_, "%s  %s",
                        direction < 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN,
                        rangeText);
  lv_obj_clear_flag(rangeFeedback_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(rangeFeedback_);
  lv_obj_invalidate(rangeFeedback_);
  rangeFeedbackVisible_ = true;
  rangeFeedbackShownAtMs_ = millis();

  // Gesture dispatch runs outside lv_timer_handler. Present this one small
  // acknowledgement before ScreenWeather_Tick performs the expensive recrop
  // or starts a new RainViewer tile burst.
  displayHostLoop();
}

void RadarScreen::hideRangeFeedback() {
  if (!rangeFeedbackVisible_ || rangeFeedback_ == nullptr) return;
  lv_obj_add_flag(rangeFeedback_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(rangeFeedback_);
  rangeFeedbackVisible_ = false;
}

void RadarScreen::releaseFetchGate() {
  if (!fetchGateHeld_) return;
  network_host::releaseFetchGate();
  fetchGateHeld_ = false;
}

void RadarScreen::presentFromCanvas() {
  if (instance_ != nullptr) instance_->present(true);
}

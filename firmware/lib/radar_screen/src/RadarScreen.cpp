#include "RadarScreen.h"

#include <Arduino.h>

#ifdef BOOT_PIN
#undef BOOT_PIN
#endif

#include "CHMU.h"
#include "DisplayHost.h"
#include "MeteoSettingsAdapter.h"
#include "MeteoCanvas.h"
#include "Net.h"
#include "NetworkFetchGate.h"
#include "NetworkHost.h"
#include "RainViewer.h"
#include "ScreenWeather.h"
#include "UI.h"

namespace {
constexpr uint32_t kRangeFeedbackDurationMs = 900;

void pollDuringMeteoTransfer() {
  // Upstream CHMI downloads are deliberately synchronous. Keep the sole host
  // display/input pipeline and Wi-Fi provisioning alive while they run; screen
  // dispatch itself remains in the outer main loop and is never re-entered.
  network_host::loop();
  displayHostLoop();
  delay(1);
}
}  // namespace

RadarScreen* RadarScreen::instance_ = nullptr;

bool RadarScreen::begin() {
  if (instance_ != nullptr) return false;
  instance_ = this;

  if (!meteo_canvas::begin()) {
    Serial.println("Error: Meteo radar canvas allocation failed");
    instance_ = nullptr;
    return false;
  }

  screen_ = lv_obj_create(nullptr);
  if (screen_ == nullptr) return false;
  lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);
  lv_obj_set_style_border_width(screen_, 0, 0);
  lv_obj_set_style_pad_all(screen_, 0, 0);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

  image_ = lv_img_create(screen_);
  if (image_ == nullptr) return false;

  imageDescriptor_.header.always_zero = 0;
  imageDescriptor_.header.w = meteo_canvas::kWidth;
  imageDescriptor_.header.h = meteo_canvas::kHeight;
  imageDescriptor_.header.cf = LV_IMG_CF_TRUE_COLOR;
  imageDescriptor_.data_size =
      static_cast<uint32_t>(meteo_canvas::kWidth) * meteo_canvas::kHeight *
      sizeof(uint16_t);
  imageDescriptor_.data = reinterpret_cast<const uint8_t*>(
      meteo_canvas::framebuffer());
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
  meteo_canvas::setPresentCallback(&RadarScreen::presentFromCanvas);
  ScreenWeather_Enter();
  render();
  if (screen_ != nullptr) lv_scr_load(screen_);
}

void RadarScreen::hide() {
  meteo_canvas::clearPresentCallback();
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

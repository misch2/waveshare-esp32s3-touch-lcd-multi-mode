#include "PlanesScreen.h"

#include <Arduino.h>

#ifdef BOOT_PIN
#undef BOOT_PIN
#endif

#include "ADSB.h"
#include "DisplayHost.h"
#include "MeteoCanvas.h"
#include "NetworkFetchGate.h"
#include "NetworkHost.h"
#include "Route.h"
#include "ScreenPlanes.h"

namespace {
constexpr uint32_t kTickPeriodMs = 250;
constexpr uint32_t kRangeFeedbackDurationMs = 900;

void pollDuringAircraftTransfer() {
  // ADSB_Fetch is synchronous in the pinned project. Keep host-owned network
  // and display services alive without re-entering ScreenManager.
  network_host::loop();
  displayHostLoop();
  delay(1);
}
}  // namespace

PlanesScreen* PlanesScreen::instance_ = nullptr;

bool PlanesScreen::begin() {
  if (initialized_) return true;
  if (instance_ != nullptr && instance_ != this) return false;
  instance_ = this;

  if (!meteo_canvas::begin()) {
    Serial.println("Error: aircraft radar canvas allocation failed");
    instance_ = nullptr;
    return false;
  }

  screen_ = lv_obj_create(nullptr);
  if (screen_ == nullptr) {
    instance_ = nullptr;
    return false;
  }
  lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);
  lv_obj_set_style_border_width(screen_, 0, 0);
  lv_obj_set_style_pad_all(screen_, 0, 0);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

  image_ = lv_img_create(screen_);
  if (image_ == nullptr) {
    instance_ = nullptr;
    return false;
  }
  imageDescriptor_.header.always_zero = 0;
  imageDescriptor_.header.w = meteo_canvas::kWidth;
  imageDescriptor_.header.h = meteo_canvas::kHeight;
  imageDescriptor_.header.cf = LV_IMG_CF_TRUE_COLOR;
  imageDescriptor_.data_size =
      static_cast<uint32_t>(meteo_canvas::kWidth) * meteo_canvas::kHeight *
      sizeof(uint16_t);
  imageDescriptor_.data =
      reinterpret_cast<const uint8_t*>(meteo_canvas::framebuffer());
  lv_img_set_src(image_, &imageDescriptor_);
  lv_obj_center(image_);
  lv_obj_clear_flag(image_, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  rangeFeedback_ = lv_obj_create(screen_);
  if (rangeFeedback_ == nullptr) {
    instance_ = nullptr;
    return false;
  }
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
  if (rangeFeedbackLabel_ == nullptr) {
    instance_ = nullptr;
    return false;
  }
  lv_obj_set_style_text_color(rangeFeedbackLabel_, lv_color_hex(0xFFE066), 0);
  lv_obj_center(rangeFeedbackLabel_);
  lv_obj_add_flag(rangeFeedback_, LV_OBJ_FLAG_HIDDEN);

  ADSB_SetPollFn(pollDuringAircraftTransfer);
  initialized_ = true;
  Serial.printf(
      "Aircraft radar: upstream renderer ready, canvas=%u B, PSRAM free=%u\n",
      static_cast<unsigned>(imageDescriptor_.data_size),
      static_cast<unsigned>(ESP.getFreePsram()));
  return true;
}

void PlanesScreen::show() {
  if (!initialized_ || screen_ == nullptr) return;
  visible_ = true;
  tickStarted_ = false;
  meteo_canvas::setPresentCallback(&PlanesScreen::presentFromCanvas);
  ScreenPlanes_Enter();
  render();
  lv_scr_load(screen_);
}

void PlanesScreen::hide() {
  meteo_canvas::clearPresentCallback();
  visible_ = false;
  hideRangeFeedback();
  ScreenPlanes_CloseDetail();
}

void PlanesScreen::tick(uint32_t nowMs) {
  if (!initialized_ || !visible_) return;

  if (rangeFeedbackVisible_ &&
      nowMs - rangeFeedbackShownAtMs_ >= kRangeFeedbackDurationMs) {
    hideRangeFeedback();
  }

  if (tickStarted_ && nowMs - lastTickMs_ < kTickPeriodMs) return;
  tickStarted_ = true;
  lastTickMs_ = nowMs;

  // Both ADS-B polling and the optional route/airframe lookup allocate TLS
  // state. Keep the complete upstream request work under the one host gate so
  // it cannot overlap Home Assistant, weather radar or forecast handshakes.
  network_host::FetchLease lease(0);
  if (!lease) return;

  const bool redrawForAircraft = ScreenPlanes_Tick();
  const RouteState routeBefore = Route_GetState();
  Route_Tick();
  const bool redrawForRoute = Route_GetState() != routeBefore;
  if (redrawForAircraft || redrawForRoute) render();
}

bool PlanesScreen::handleGesture(const GestureEvent& event) {
  if (!visible_) return false;

  if ((event.kind == GestureKind::HorizontalSwipe ||
       event.kind == GestureKind::VerticalSwipe) &&
      ScreenPlanes_DetailOpen()) {
    ScreenPlanes_CloseDetail();
    render();
    return true;
  }

  if (event.kind == GestureKind::Tap) {
    if (!ScreenPlanes_HandleTap(event.endX, event.endY)) return false;
    render();
    return true;
  }

  if (event.kind == GestureKind::VerticalSwipe) {
    ScreenPlanes_ChangeRange(event.direction < 0 ? 1 : -1);
    showRangeFeedback(event.direction);
    return true;
  }

  if (event.kind == GestureKind::LongPress && ScreenPlanes_DetailOpen()) {
    ScreenPlanes_CloseDetail();
    render();
    return true;
  }
  return false;
}

void PlanesScreen::render() {
  if (!visible_ || image_ == nullptr || gfx == nullptr) return;
  ScreenPlanes_Draw();
  present(false);
}

void PlanesScreen::present(bool pumpDisplay) {
  if (!visible_ || image_ == nullptr) return;
  lv_img_cache_invalidate_src(&imageDescriptor_);
  lv_obj_invalidate(image_);
  if (pumpDisplay) displayHostLoop();
}

void PlanesScreen::showRangeFeedback(int8_t direction) {
  if (!visible_ || rangeFeedback_ == nullptr || rangeFeedbackLabel_ == nullptr) {
    return;
  }

  char rangeText[32];
  ScreenPlanes_RangeText(rangeText, sizeof(rangeText));
  lv_label_set_text_fmt(rangeFeedbackLabel_, "%s  %s",
                        direction < 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN,
                        rangeText);
  lv_obj_clear_flag(rangeFeedback_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(rangeFeedback_);
  lv_obj_invalidate(rangeFeedback_);
  rangeFeedbackVisible_ = true;
  rangeFeedbackShownAtMs_ = millis();
  displayHostLoop();
}

void PlanesScreen::hideRangeFeedback() {
  if (!rangeFeedbackVisible_ || rangeFeedback_ == nullptr) return;
  lv_obj_add_flag(rangeFeedback_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(rangeFeedback_);
  rangeFeedbackVisible_ = false;
}

void PlanesScreen::presentFromCanvas() {
  if (instance_ != nullptr) instance_->present(true);
}

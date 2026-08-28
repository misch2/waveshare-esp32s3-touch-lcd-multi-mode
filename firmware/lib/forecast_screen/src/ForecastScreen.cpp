#include "ForecastScreen.h"

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef BOOT_PIN
#undef BOOT_PIN
#endif

#include "DisplayHost.h"
#include "Forecast.h"
#include "MeteoCanvas.h"
#include "Net.h"
#include "NetworkFetchGate.h"
#include "NetworkHost.h"
#include "ScreenForecast.h"

namespace {

void pollDuringForecastTransfer() {
  // Forecast requests are synchronous in the pinned client. Keep the shared
  // Wi-Fi/display lifecycle alive while Open-Meteo is being read; the caller
  // retains the FetchGate through JSON parsing as well.
  network_host::loop();
  displayHostLoop();
  delay(1);
}

void hashBytes(uint32_t& hash, const void* value, std::size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(value);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
}

template <typename T>
void hashValue(uint32_t& hash, const T& value) {
  hashBytes(hash, &value, sizeof(value));
}

void hashText(uint32_t& hash, const char* value) {
  if (value == nullptr) {
    const uint8_t missing = 0;
    hashValue(hash, missing);
    return;
  }
  hashBytes(hash, value, std::strlen(value) + 1);
}

}  // namespace

ForecastScreen* ForecastScreen::instance_ = nullptr;

bool ForecastScreen::begin() {
  if (initialized_) return true;
  if (instance_ != nullptr && instance_ != this) return false;
  instance_ = this;

  if (!meteo_canvas::begin()) {
    Serial.println("Error: Meteo forecast canvas allocation failed");
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
  imageDescriptor_.data = reinterpret_cast<const uint8_t*>(
      meteo_canvas::framebuffer());
  lv_img_set_src(image_, &imageDescriptor_);
  lv_obj_center(image_);
  lv_obj_clear_flag(image_, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  // Net is a shared upstream client. The callback is deliberately identical
  // to the radar adapter and only services host-owned resources; it never
  // dispatches a second screen tick from inside a network request.
  Net_SetPollFn(pollDuringForecastTransfer);
  initialized_ = true;

  Serial.printf(
      "Meteo forecast: upstream renderer ready, canvas=%u B, PSRAM free=%u\n",
      static_cast<unsigned>(imageDescriptor_.data_size),
      static_cast<unsigned>(ESP.getFreePsram()));
  return true;
}

void ForecastScreen::show() {
  if (!initialized_ || screen_ == nullptr) return;
  visible_ = true;
  meteo_canvas::setPresentCallback(&ForecastScreen::presentFromCanvas);
  ScreenForecast_Enter();
  render();
  lastDataSignature_ = dataSignature();
  lv_scr_load(screen_);
}

void ForecastScreen::hide() {
  meteo_canvas::clearPresentCallback();
  visible_ = false;
}

void ForecastScreen::tick(uint32_t nowMs) {
  if (!initialized_ || !visible_) return;

  // Forecast_Tick performs both the HTTP transfer and its ArduinoJson parse.
  // Keep one lease around the complete call so a clock refresh cannot start a
  // second TLS allocation while this module is parsing the response. The
  // upstream fetcher has its own 30-minute/retry timers; this short host-side
  // cadence avoids repeatedly taking the gate on every 5 ms host loop pass.
  if (!forecastTickStarted_ || nowMs - lastForecastTickMs_ >= 250) {
    forecastTickStarted_ = true;
    lastForecastTickMs_ = nowMs;
    network_host::FetchLease lease(0);
    if (lease) Forecast_Tick();
  }

  const bool upstreamRedraw = ScreenForecast_Tick();

  // ScreenForecast_Tick only observes validity/hour changes. The host adapter
  // hashes every public forecast/AQ value so a successful AQ refresh, changed
  // hourly data, or a new pollen value redraws immediately as well.
  const uint32_t signature = dataSignature();
  if (upstreamRedraw || signature != lastDataSignature_) {
    lastDataSignature_ = signature;
    render();
  }
}

bool ForecastScreen::handleGesture(const GestureEvent& event) {
  // Horizontal navigation is owned by ScreenManager. Forecast currently has
  // no module-local gesture, and must not install another recognizer.
  (void)event;
  return false;
}

uint32_t ForecastScreen::dataSignature() const {
  uint32_t hash = 2166136261u;
  const bool valid = Forecast_Valid();
  hashValue(hash, valid);

  const int hourCount = Forecast_HourCount();
  hashValue(hash, hourCount);
  const FcHour* hours = Forecast_Hours();
  for (int i = 0; hours != nullptr && i < hourCount; ++i) {
    hashValue(hash, hours[i].t);
    hashValue(hash, hours[i].temp);
    hashValue(hash, hours[i].precip);
    hashValue(hash, hours[i].wind);
    hashValue(hash, hours[i].code);
  }

  const int dayCount = Forecast_DayCount();
  hashValue(hash, dayCount);
  const FcDay* days = Forecast_Days();
  for (int i = 0; days != nullptr && i < dayCount; ++i) {
    hashValue(hash, days[i].t);
    hashValue(hash, days[i].tmax);
    hashValue(hash, days[i].tmin);
    hashValue(hash, days[i].precip);
    hashValue(hash, days[i].wind);
    hashValue(hash, days[i].code);
    hashValue(hash, days[i].sunrise);
    hashValue(hash, days[i].sunset);
  }

  const bool currentValid = Forecast_CurrentValid();
  hashValue(hash, currentValid);
  hashValue(hash, Forecast_CurrentTemp());
  hashValue(hash, Forecast_CurrentPrecip());
  hashValue(hash, Forecast_CurrentWind());
  hashValue(hash, Forecast_CurrentCode());

  const bool airQualityValid = AirQuality_Valid();
  hashValue(hash, airQualityValid);
  hashValue(hash, AirQuality_Aqi());
  hashValue(hash, AirQuality_Pm25());
  hashValue(hash, AirQuality_PollenMax());
  hashText(hash, AirQuality_PollenWorst());
  return hash;
}

void ForecastScreen::render() {
  if (!visible_ || image_ == nullptr || gfx == nullptr) return;
  ScreenForecast_Draw();
  present(false);
}

void ForecastScreen::present(bool pumpDisplay) {
  if (!visible_ || image_ == nullptr) return;
  lv_img_cache_invalidate_src(&imageDescriptor_);
  lv_obj_invalidate(image_);
  if (pumpDisplay) displayHostLoop();
}

void ForecastScreen::presentFromCanvas() {
  if (instance_ != nullptr) instance_->present(true);
}

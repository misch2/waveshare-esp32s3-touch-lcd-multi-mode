#include "ClockScreen.h"

#include <cstdio>

#include "ClockDashboard.h"

namespace {

ClockValues demoValues() {
  ClockValues values;
  values.weatherCode = 800;
  values.weatherIsDay = true;
  values.leftTemperatureC = 21.4f;
  values.rightTemperatureC = 22.1f;
  values.metricAValue = 620.0f;
  values.metricBValue = 45.0f;
  values.homeAssistantOnline = false;
  return values;
}

}  // namespace

bool ClockScreen::begin() {
  if (initialized_) return true;

  // The upstream dashboard obtains its parent from lv_scr_act(). Load this
  // screen only for construction, then return control to the host screen.
  lv_obj_t* previousScreen = lv_scr_act();
  screen_ = lv_obj_create(nullptr);
  if (screen_ == nullptr) return false;
  lv_scr_load(screen_);

  ClockValues values = demoValues();
  ClockConfig config{};
  clockDashboardInit(values, 35, 10, true, nullptr, nullptr, nullptr,
                     nullptr, nullptr);
  clockDashboardApplyConfiguration(config);
  clockDashboardSetWifiConnected(false);
  clockDashboardSetWebActive(false);
  clockDashboardSetDate("23.08.2026");
  clockDashboardSetTime("12:34");
  clockDashboardSetSecond(0);

  if (previousScreen != nullptr && previousScreen != screen_) {
    lv_scr_load(previousScreen);
  }
  initialized_ = true;
  return true;
}

void ClockScreen::show() {
  if (!initialized_ || screen_ == nullptr) return;
  visible_ = true;
  lv_scr_load(screen_);
}

void ClockScreen::hide() {
  visible_ = false;
}

void ClockScreen::tick(uint32_t nowMs) {
  if (!initialized_ || !visible_) return;

  clockDashboardLoop();
  updateDemoClock(nowMs);
}

bool ClockScreen::handleGesture(const GestureEvent& event) {
  // LVGL receives the original touch stream from DisplayHost. Returning
  // false here deliberately leaves taps and long presses to the upstream
  // dashboard event callbacks (including its settings overlay).
  (void)event;
  return false;
}

void ClockScreen::updateDemoClock(uint32_t nowMs) {
  if (!clockStarted_) {
    clockStarted_ = true;
    startMs_ = nowMs;
  }

  const uint32_t elapsedSeconds = (nowMs - startMs_) / 1000U;
  if (elapsedSeconds == lastSecond_) return;
  lastSecond_ = elapsedSeconds;

  // A deterministic demo baseline keeps the prototype useful before SNTP is
  // wired into AppHost. The dashboard can later receive real system time
  // through the same two upstream setters.
  const uint32_t daySeconds =
      (12U * 60U * 60U) + (34U * 60U) + elapsedSeconds;
  const unsigned hour = (daySeconds / 3600U) % 24U;
  const unsigned minute = (daySeconds / 60U) % 60U;
  const unsigned second = daySeconds % 60U;

  char timeText[6];
  std::snprintf(timeText, sizeof(timeText), "%02u:%02u", hour, minute);
  clockDashboardSetTime(timeText);
  clockDashboardSetSecond(static_cast<uint8_t>(second));
}

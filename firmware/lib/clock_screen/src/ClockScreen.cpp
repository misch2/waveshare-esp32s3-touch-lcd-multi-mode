#include "ClockScreen.h"

#include <cstdio>
#include <cstring>

#include "ClockDashboard.h"

namespace {
ClockScreen* callbackTarget = nullptr;

const char* kCzechWeekdays[] = {
    "NEDĚLE", "PONDĚLÍ", "ÚTERÝ", "STŘEDA",
    "ČTVRTEK", "PÁTEK", "SOBOTA",
};
const char* kCzechMonths[] = {
    "LEDNA", "ÚNORA", "BŘEZNA", "DUBNA", "KVĚTNA", "ČERVNA",
    "ČERVENCE", "SRPNA", "ZÁŘÍ", "ŘÍJNA", "LISTOPADU", "PROSINCE",
};

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

ClockScreen::ClockScreen(ClockConfig& config,
                         ClockBrightnessPreviewCallback brightnessPreview)
    : config_(config), brightnessPreview_(brightnessPreview) {
  callbackTarget = this;
}

bool ClockScreen::begin() {
  if (initialized_) return true;

  // The upstream dashboard obtains its parent from lv_scr_act(). Load this
  // screen only for construction, then return control to the host screen.
  lv_obj_t* previousScreen = lv_scr_act();
  returnScreen_ = previousScreen;
  screen_ = lv_obj_create(nullptr);
  if (screen_ == nullptr) return false;
  lv_scr_load(screen_);

  ClockValues values = demoValues();
  clockDashboardInit(values, config_.dayBrightness, config_.nightBrightness,
                     config_.automaticDayNight,
                     &ClockScreen::onBrightnessPreview, nullptr,
                     &ClockScreen::onSettingsSave, nullptr, nullptr);
  clockDashboardApplyConfiguration(config_);
  clockDashboardSetWifiConnected(false);
  clockDashboardSetWebActive(false);
  clockDashboardSetDate("");
  clockDashboardSetTime("--:--");
  clockDashboardSetSecond(60);

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
  if (visible_ && screen_ != nullptr && lv_scr_act() == screen_ &&
      returnScreen_ != nullptr) {
    lv_scr_load(returnScreen_);
  }
  visible_ = false;
}

void ClockScreen::tick(uint32_t nowMs) {
  if (!initialized_ || !visible_) return;

  clockDashboardLoop();
  (void)nowMs;
}

bool ClockScreen::handleGesture(const GestureEvent& event) {
  // LVGL receives the original touch stream from DisplayHost. Returning
  // false here deliberately leaves taps and long presses to the upstream
  // dashboard event callbacks (including its settings overlay).
  (void)event;
  return false;
}

void ClockScreen::onBrightnessPreview(uint8_t brightness) {
  if (callbackTarget != nullptr) callbackTarget->previewBrightness(brightness);
}

void ClockScreen::onSettingsSave(
    uint8_t dayBrightness, uint8_t nightBrightness, bool automaticDayNight,
    bool secondRingEnabled, uint8_t secondEffect, bool animatedWeatherIcons,
    uint8_t weatherIconStyle, bool automaticFirmwareUpdate, uint8_t webMode) {
  if (callbackTarget != nullptr) {
    callbackTarget->saveSettings(
        dayBrightness, nightBrightness, automaticDayNight, secondRingEnabled,
        secondEffect, animatedWeatherIcons, weatherIconStyle,
        automaticFirmwareUpdate, webMode);
  }
}

void ClockScreen::previewBrightness(uint8_t brightness) {
  if (brightnessPreview_ != nullptr) brightnessPreview_(brightness);
}

void ClockScreen::saveSettings(
    uint8_t dayBrightness, uint8_t nightBrightness, bool automaticDayNight,
    bool secondRingEnabled, uint8_t secondEffect, bool animatedWeatherIcons,
    uint8_t weatherIconStyle, bool automaticFirmwareUpdate, uint8_t webMode) {
  config_.dayBrightness = dayBrightness;
  config_.nightBrightness = nightBrightness;
  config_.automaticDayNight = automaticDayNight;
  config_.secondRingEnabled = secondRingEnabled;
  config_.secondEffect = secondEffect;
  config_.animatedWeatherIcons = animatedWeatherIcons;
  config_.weatherIconStyle = weatherIconStyle;
  config_.automaticFirmwareUpdate = automaticFirmwareUpdate;

  // The upstream overlay has a web-mode control, but the combined prototype
  // has no web service yet. Do not persist or emulate that setting.
  (void)webMode;

  clockDashboardApplyConfiguration(config_);
  configSavePending_ = true;
}

bool ClockScreen::takeConfigSaveRequest() {
  const bool pending = configSavePending_;
  configSavePending_ = false;
  return pending;
}

void ClockScreen::updateNetworkStatus(bool connected, const char* ipAddress) {
  if (!initialized_) return;
  if (connected != lastWifiConnected_) {
    lastWifiConnected_ = connected;
    clockDashboardSetWifiConnected(connected);
  }
  const char* address = connected && ipAddress != nullptr ? ipAddress : "";
  if (std::strncmp(lastWifiAddress_, address, sizeof(lastWifiAddress_)) != 0) {
    std::snprintf(lastWifiAddress_, sizeof(lastWifiAddress_), "%s", address);
    clockDashboardSetWifiAddress(lastWifiAddress_);
  }
}

void ClockScreen::updateLocalTime(const std::tm& localTime) {
  if (!initialized_) return;
  const int64_t secondKey =
      (((static_cast<int64_t>(localTime.tm_year) * 366LL + localTime.tm_yday) *
            24LL +
        localTime.tm_hour) *
           60LL +
       localTime.tm_min) *
          60LL +
      localTime.tm_sec;
  if (secondKey == lastPresentedSecond_) return;
  lastPresentedSecond_ = secondKey;

  char timeText[6];
  std::snprintf(timeText, sizeof(timeText),
                config_.showLeadingHourZero ? "%02d:%02d" : "%d:%02d",
                localTime.tm_hour, localTime.tm_min);
  clockDashboardSetTime(timeText);
  clockDashboardSetSecond(static_cast<uint8_t>(localTime.tm_sec));

  char dateText[64];
  switch (config_.dateFormat) {
    case CLOCK_DATE_FORMAT_HIDDEN:
      dateText[0] = '\0';
      break;
    case CLOCK_DATE_FORMAT_NUMERIC:
      std::snprintf(dateText, sizeof(dateText), "%02d.%02d.%04d",
                    localTime.tm_mday, localTime.tm_mon + 1,
                    localTime.tm_year + 1900);
      break;
    case CLOCK_DATE_FORMAT_DAY_MONTH_YEAR:
      std::snprintf(dateText, sizeof(dateText), "%d. %s %d",
                    localTime.tm_mday, kCzechMonths[localTime.tm_mon],
                    localTime.tm_year + 1900);
      break;
    case CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH_YEAR:
      std::snprintf(dateText, sizeof(dateText), "%s, %d. %s %d",
                    kCzechWeekdays[localTime.tm_wday], localTime.tm_mday,
                    kCzechMonths[localTime.tm_mon], localTime.tm_year + 1900);
      break;
    case CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH:
    default:
      std::snprintf(dateText, sizeof(dateText), "%s, %d. %s",
                    kCzechWeekdays[localTime.tm_wday], localTime.tm_mday,
                    kCzechMonths[localTime.tm_mon]);
      break;
  }
  clockDashboardSetDate(dateText);
}

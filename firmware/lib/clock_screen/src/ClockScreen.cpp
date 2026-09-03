#include "ClockScreen.h"

#include <cstdio>
#include <cstring>

#include "ClockConfig.h"
#include "ClockDashboard.h"
#include "ClockWeatherAnimationPolicy.h"
#include "DisplayHost.h"
#include "WeatherAnimationService.h"

namespace {
static_assert(app_core::kClockWeatherIconStyleMonochrome ==
              CLOCK_WEATHER_ICON_STYLE_MONOCHROME);
ClockScreen* callbackTarget = nullptr;

const char* kCzechWeekdays[] = {
    "NEDĚLE", "PONDĚLÍ", "ÚTERÝ", "STŘEDA",
    "ČTVRTEK", "PÁTEK", "SOBOTA",
};
const char* kCzechMonths[] = {
    "LEDNA", "ÚNORA", "BŘEZNA", "DUBNA", "KVĚTNA", "ČERVNA",
    "ČERVENCE", "SRPNA", "ZÁŘÍ", "ŘÍJNA", "LISTOPADU", "PROSINCE",
};
const char* kEnglishWeekdays[] = {
    "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
    "THURSDAY", "FRIDAY", "SATURDAY",
};
const char* kEnglishMonths[] = {
    "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
    "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER",
};

}  // namespace

void displayDriverSetPartialRefresh(bool enabled, bool rebuildBuffers) {
  if (callbackTarget == nullptr) return;
  // Configuration and appearance can be applied while the clock is hidden.
  // Do not let those calls put the Meteo screens into direct mode; show()
  // explicitly enables it again when the clock becomes active.
  if (!callbackTarget->visible_) {
    if (enabled) return;
    displayHostSetPartialRefresh(false, rebuildBuffers);
    return;
  }
  displayHostSetPartialRefresh(enabled, rebuildBuffers);
}

ClockScreen::ClockScreen(ClockConfig& config,
                         ClockBrightnessPreviewCallback brightnessPreview,
                         ClockSettingsOpenCallback settingsOpen,
                         ClockShortClickAllowedCallback shortClickAllowed,
                         const char* firmwareVersion,
                         ClockAppearanceConfig* appearance)
    : config_(config),
      appearance_(appearance != nullptr ? appearance : &defaultAppearance_),
      brightnessPreview_(brightnessPreview),
      settingsOpen_(settingsOpen),
      shortClickAllowed_(shortClickAllowed),
      firmwareVersion_(firmwareVersion) {
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

  ClockValues values;
  // Select the appearance before creating the upstream widgets so init can
  // construct the analog or digital hierarchy in its final layout.
  clockDashboardApplyAppearance(*appearance_);
  clockDashboardInit(values, config_.dayBrightness, config_.nightBrightness,
                     config_.automaticDayNight,
                     &ClockScreen::onBrightnessPreview,
                     &ClockScreen::onSettingsOpen,
                     &ClockScreen::onSettingsSave, nullptr, nullptr, nullptr,
                     nullptr);
  clockDashboardSetShortClickAllowedCallback(shortClickAllowed_);
  clockDashboardApplyConfiguration(config_);
  clockDashboardSetWifiConnected(false);
  clockDashboardSetWebActive(false);
  clockDashboardSetDate("");
  clockDashboardSetTime("--:--");
  clockDashboardSetSecond(60);
  // The combined image has no release-check/update state. Publish its
  // host-supplied version as informational dashboard text only.
  clockDashboardSetFirmwareVersion(firmwareVersion_, false);

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
  const bool analog = appearance_->style == CLOCK_STYLE_ANALOG;
  displayHostSetPartialRefresh(analog, analog);
}

void ClockScreen::hide() {
  if (visible_) displayHostSetPartialRefresh(false, false);
  if (visible_ && screen_ != nullptr && lv_scr_act() == screen_ &&
      returnScreen_ != nullptr) {
    lv_scr_load(returnScreen_);
  }
  visible_ = false;
}

void ClockScreen::tick(uint32_t nowMs) {
  if (!initialized_ || !visible_) return;

  app_core::ClockWeatherAnimationPolicy animationPolicy;
  animationPolicy.configuredEnabled = config_.animatedWeatherIcons;
  animationPolicy.openMeteo =
      config_.dataSource == CLOCK_DATA_SOURCE_OPEN_METEO;
  animationPolicy.leftUsesWeather =
      std::strcmp(config_.leftSide.icon, "weather") == 0;
  animationPolicy.rightUsesWeather =
      std::strcmp(config_.rightSide.icon, "weather") == 0;
  animationPolicy.nightMode = clockDashboardNightModeEnabled();
  animationPolicy.configuredStyle =
      clockDashboardWeatherIconStyle(config_.weatherIconStyle);
  const app_core::ClockWeatherAnimationDecision animationDecision =
      app_core::selectClockWeatherAnimation(animationPolicy);
  weatherAnimationServiceLoop(latestWeatherCode_, latestWeatherIsDay_,
                              animationDecision.effectiveStyle,
                              animationDecision.enabled);
  clockDashboardLoop();
  (void)nowMs;
}

bool ClockScreen::handleGesture(const GestureEvent& event) {
  if (event.kind == GestureKind::Tap) {
    // LVGL controls still receive the raw release event. This explicit host
    // tap is the dashboard's single-click seam and is ignored while settings
    // or another modal is active.
    clockDashboardHandleShortClick();
    return true;
  }
  // Long presses remain available to the upstream dashboard's settings
  // handler through the original LVGL touch stream.
  return false;
}

void ClockScreen::onBrightnessPreview(uint8_t brightness) {
  if (callbackTarget != nullptr) callbackTarget->previewBrightness(brightness);
}

void ClockScreen::onSettingsOpen() {
  if (callbackTarget != nullptr && callbackTarget->settingsOpen_ != nullptr) {
    callbackTarget->settingsOpen_();
  }
}

void ClockScreen::onSettingsSave(
    uint8_t clockStyle, uint8_t dayBrightness, uint8_t nightBrightness,
    bool automaticDayNight,
    bool secondRingEnabled, uint8_t secondEffect, bool animatedWeatherIcons,
    uint8_t weatherIconStyle, bool automaticFirmwareUpdate, uint8_t webMode) {
  if (callbackTarget != nullptr) {
    callbackTarget->saveSettings(
        clockStyle, dayBrightness, nightBrightness, automaticDayNight,
        secondRingEnabled,
        secondEffect, animatedWeatherIcons, weatherIconStyle,
        automaticFirmwareUpdate, webMode);
  }
}

void ClockScreen::previewBrightness(uint8_t brightness) {
  if (brightnessPreview_ != nullptr) brightnessPreview_(brightness);
}

void ClockScreen::saveSettings(
    uint8_t clockStyle, uint8_t dayBrightness, uint8_t nightBrightness,
    bool automaticDayNight,
    bool secondRingEnabled, uint8_t secondEffect, bool animatedWeatherIcons,
    uint8_t weatherIconStyle, bool automaticFirmwareUpdate, uint8_t webMode) {
  // The combined product is updated by an explicit local flash only. Keep the
  // upstream callback shape for source compatibility, but never persist or
  // schedule the standalone clock's release-check policy here.
  (void)automaticFirmwareUpdate;
  config_.dayBrightness = dayBrightness;
  config_.nightBrightness = nightBrightness;
  config_.automaticDayNight = automaticDayNight;
  config_.secondRingEnabled = secondRingEnabled;
  config_.secondEffect = secondEffect;
  config_.animatedWeatherIcons = animatedWeatherIcons;
  config_.weatherIconStyle = weatherIconStyle;
  config_.automaticFirmwareUpdate = false;
  appearance_->style = clockStyle;
  pendingWebMode_ = webMode;

  clockDashboardApplyConfiguration(config_);
  clockDashboardApplyAppearance(*appearance_);
  configSavePending_ = true;
  appearanceSavePending_ = true;
}

bool ClockScreen::takeConfigSaveRequest(uint8_t& webMode) {
  const bool pending = configSavePending_;
  configSavePending_ = false;
  if (pending) webMode = pendingWebMode_;
  return pending;
}

bool ClockScreen::takeAppearanceSaveRequest() {
  const bool pending = appearanceSavePending_;
  appearanceSavePending_ = false;
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

  const bool english = config_.language == CLOCK_LANGUAGE_ENGLISH;
  const bool analog = appearance_->style == CLOCK_STYLE_ANALOG;
  const uint8_t dateFormat = analog ? appearance_->analogDateFormat
                                    : config_.dateFormat;
  const int weekday = (localTime.tm_wday >= 0 && localTime.tm_wday < 7)
                          ? localTime.tm_wday
                          : 0;
  const int month = (localTime.tm_mon >= 0 && localTime.tm_mon < 12)
                        ? localTime.tm_mon
                        : 0;
  const char* const* weekdays = english ? kEnglishWeekdays : kCzechWeekdays;
  const char* const* months = english ? kEnglishMonths : kCzechMonths;
  char dateText[64];
  switch (dateFormat) {
    case CLOCK_DATE_FORMAT_HIDDEN:
      dateText[0] = '\0';
      break;
    case CLOCK_DATE_FORMAT_NUMERIC:
      std::snprintf(dateText, sizeof(dateText), "%02d.%02d.%04d",
                    localTime.tm_mday, month + 1,
                    localTime.tm_year + 1900);
      break;
    case CLOCK_DATE_FORMAT_DAY_MONTH_YEAR:
      std::snprintf(dateText, sizeof(dateText), "%d. %s %d",
                    localTime.tm_mday, months[month],
                    localTime.tm_year + 1900);
      break;
    case CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH_YEAR:
      std::snprintf(dateText, sizeof(dateText), "%s, %d. %s %d",
                    weekdays[weekday], localTime.tm_mday, months[month],
                    localTime.tm_year + 1900);
      break;
    case CLOCK_DATE_FORMAT_DAY_MONTH:
      std::snprintf(dateText, sizeof(dateText), "%d. %s", localTime.tm_mday,
                    months[month]);
      break;
    case CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH:
    default:
      std::snprintf(dateText, sizeof(dateText), "%s, %d. %s",
                    weekdays[weekday], localTime.tm_mday, months[month]);
      break;
  }
  clockDashboardSetDate(dateText);
}

void ClockScreen::updateValues(const ClockValues& values) {
  if (!initialized_) return;
  latestWeatherCode_ = values.weatherCode;
  latestWeatherIsDay_ = values.weatherIsDay;
  clockDashboardUpdate(values);
}

void ClockScreen::applyConfiguration() {
  if (!initialized_) return;
  clockDashboardApplyConfiguration(config_);
  lastPresentedSecond_ = -1;
}

void ClockScreen::applyAppearance(const ClockAppearanceConfig& appearance) {
  *appearance_ = appearance;
  lastPresentedSecond_ = -1;
  if (!initialized_) return;
  clockDashboardApplyAppearance(*appearance_);
}

void ClockScreen::updateWebStatus(bool active, uint8_t mode) {
  if (!initialized_) return;
  clockDashboardSetWebActive(active);
  clockDashboardSetWebMode(mode);
}

bool ClockScreen::nightModeEnabled() const {
  return initialized_ && clockDashboardNightModeEnabled();
}

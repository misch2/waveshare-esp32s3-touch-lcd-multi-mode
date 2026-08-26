#pragma once

#include <cstdint>
#include <ctime>

#include <lvgl.h>

#include "ScreenModule.h"

struct ClockConfig;
struct ClockValues;
using ClockBrightnessPreviewCallback = void (*)(uint8_t brightness);
using ClockSettingsOpenCallback = void (*)();
using ClockShortClickAllowedCallback = bool (*)();

/**
 * Adapter around the pinned waveshare-hodiny dashboard.
 *
 * The upstream dashboard remains responsible for its LVGL widgets and
 * click/long-press callbacks. This class only owns the screen object and
 * controls when that object is loaded by the host screen manager.
 */
class ClockScreen final : public ScreenModule {
 public:
  explicit ClockScreen(ClockConfig& config,
                       ClockBrightnessPreviewCallback brightnessPreview,
                       ClockSettingsOpenCallback settingsOpen,
                       ClockShortClickAllowedCallback shortClickAllowed,
                       const char* firmwareVersion);

  const char* id() const override { return "clock.dashboard"; }
  const char* label() const override { return "Clock"; }

  bool begin() override;
  void show() override;
  void hide() override;
  void tick(uint32_t nowMs) override;
  bool handleGesture(const GestureEvent& event) override;

  void updateNetworkStatus(bool connected, const char* ipAddress);
  void updateLocalTime(const std::tm& localTime);
  void updateValues(const ClockValues& values);
  void applyConfiguration();
  void updateWebStatus(bool active, uint8_t mode);
  bool nightModeEnabled() const;
  bool takeConfigSaveRequest(uint8_t& webMode);

 private:
  void saveSettings(uint8_t dayBrightness, uint8_t nightBrightness,
                    bool automaticDayNight, bool secondRingEnabled,
                    uint8_t secondEffect, bool animatedWeatherIcons,
                    uint8_t weatherIconStyle, bool automaticFirmwareUpdate,
                    uint8_t webMode);
  void previewBrightness(uint8_t brightness);

  static void onBrightnessPreview(uint8_t brightness);
  static void onSettingsOpen();
  static void onSettingsSave(uint8_t dayBrightness, uint8_t nightBrightness,
                             bool automaticDayNight, bool secondRingEnabled,
                             uint8_t secondEffect,
                             bool animatedWeatherIcons,
                             uint8_t weatherIconStyle,
                             bool automaticFirmwareUpdate, uint8_t webMode);

  ClockConfig& config_;
  ClockBrightnessPreviewCallback brightnessPreview_ = nullptr;
  ClockSettingsOpenCallback settingsOpen_ = nullptr;
  ClockShortClickAllowedCallback shortClickAllowed_ = nullptr;
  const char* firmwareVersion_ = nullptr;
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* returnScreen_ = nullptr;
  bool initialized_ = false;
  bool visible_ = false;
  bool lastWifiConnected_ = false;
  char lastWifiAddress_[16] = {};
  int latestWeatherCode_ = -1;
  bool latestWeatherIsDay_ = true;
  int64_t lastPresentedSecond_ = -1;
  bool configSavePending_ = false;
  uint8_t pendingWebMode_ = 0;
};

#pragma once

#include <cstdint>

#include <lvgl.h>

#include "ScreenModule.h"

/**
 * Adapter around the pinned waveshare-hodiny dashboard.
 *
 * The upstream dashboard remains responsible for its LVGL widgets and
 * click/long-press callbacks. This class only owns the screen object and
 * controls when that object is loaded by the host screen manager.
 */
class ClockScreen final : public ScreenModule {
 public:
  const char* id() const override { return "clock.dashboard"; }
  const char* label() const override { return "Clock"; }

  bool begin() override;
  void show() override;
  void hide() override;
  void tick(uint32_t nowMs) override;
  bool handleGesture(const GestureEvent& event) override;

 private:
  void updateDemoClock(uint32_t nowMs);

  lv_obj_t* screen_ = nullptr;
  bool initialized_ = false;
  bool visible_ = false;
  bool clockStarted_ = false;
  uint32_t startMs_ = 0;
  uint32_t lastSecond_ = UINT32_MAX;
};

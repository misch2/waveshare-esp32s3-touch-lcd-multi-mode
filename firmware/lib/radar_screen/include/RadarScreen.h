#pragma once

#include <lvgl.h>

#include "ScreenModule.h"

class RadarScreen final : public ScreenModule {
 public:
  const char* id() const override { return "meteo.radar"; }
  const char* label() const override { return "Radar"; }
  bool begin() override;
  void show() override;
  void hide() override;
  void tick(uint32_t nowMs) override;
  bool handleGesture(const GestureEvent& event) override;

 private:
  void updateRange();
  void updateSweep(uint32_t nowMs);

  lv_obj_t* screen_ = nullptr;
  lv_obj_t* rangeLabel_ = nullptr;
  lv_obj_t* sweepLine_ = nullptr;
  lv_point_t sweepPoints_[2] = {};
  uint32_t lastSweepMs_ = 0;
};

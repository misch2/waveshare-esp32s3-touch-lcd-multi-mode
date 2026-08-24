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
  void render();
  void present(bool pumpDisplay);
  void showRangeFeedback(int8_t direction);
  void hideRangeFeedback();
  void releaseFetchGate();
  static void presentFromCanvas();

  static RadarScreen* instance_;
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* image_ = nullptr;
  lv_obj_t* rangeFeedback_ = nullptr;
  lv_obj_t* rangeFeedbackLabel_ = nullptr;
  lv_img_dsc_t imageDescriptor_ = {};
  bool visible_ = false;
  bool fetchGateHeld_ = false;
  bool rangeFeedbackVisible_ = false;
  uint32_t rangeFeedbackShownAtMs_ = 0;
};

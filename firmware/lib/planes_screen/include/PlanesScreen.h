#pragma once

#include <lvgl.h>

#include "ScreenModule.h"

// Thin host adapter around MeteoPlaneRadar's aircraft radar. The upstream
// ADS-B client, route lookup, filtering, selection and GFX renderer remain the
// sole owners of module behaviour; this class only bridges them to the shared
// LVGL screen, canvas, gesture router and network fetch gate.
class PlanesScreen final : public ScreenModule {
 public:
  const char* id() const override { return "meteo.planes"; }
  const char* label() const override { return "Aircraft"; }
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
  static void presentFromCanvas();

  static PlanesScreen* instance_;
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* image_ = nullptr;
  lv_obj_t* rangeFeedback_ = nullptr;
  lv_obj_t* rangeFeedbackLabel_ = nullptr;
  lv_img_dsc_t imageDescriptor_ = {};
  bool visible_ = false;
  bool initialized_ = false;
  bool tickStarted_ = false;
  bool rangeFeedbackVisible_ = false;
  uint32_t lastTickMs_ = 0;
  uint32_t rangeFeedbackShownAtMs_ = 0;
};

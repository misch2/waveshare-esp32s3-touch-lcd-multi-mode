#pragma once

#include <lvgl.h>

#include "ScreenModule.h"

// Adapter around the pinned MeteoPlaneRadar forecast renderer.  The renderer
// keeps its original Open-Meteo/AQI data model and GFX drawing code; this
// class only owns the host LVGL screen and the shared Meteo canvas bridge.
class ForecastScreen final : public ScreenModule {
 public:
  const char* id() const override { return "meteo.forecast"; }
  const char* label() const override { return "Forecast"; }
  bool begin() override;
  void show() override;
  void hide() override;
  void tick(uint32_t nowMs) override;
  bool handleGesture(const GestureEvent& event) override;

 private:
  void render();
  void present(bool pumpDisplay);
  uint32_t dataSignature() const;
  static void presentFromCanvas();

  static ForecastScreen* instance_;
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* image_ = nullptr;
  lv_img_dsc_t imageDescriptor_ = {};
  bool visible_ = false;
  bool initialized_ = false;
  bool forecastTickStarted_ = false;
  uint32_t lastForecastTickMs_ = 0;
  uint32_t lastDataSignature_ = 0;
};

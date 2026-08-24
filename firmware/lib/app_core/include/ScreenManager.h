#pragma once

#include <cstddef>
#include <cstdint>

#include "AppConfig.h"
#include "ScreenModule.h"

class ScreenManager {
 public:
  static constexpr size_t kMaxModules = app_core::AppConfig::kMaxScreens;

  explicit ScreenManager(app_core::AppConfig& config);

  bool add(ScreenModule& module);
  bool begin();
  bool showById(const char* id);
  bool step(int8_t direction);
  bool dispatch(const GestureEvent& event);
  void tick(uint32_t nowMs);

  ScreenModule* active() const;
  size_t activeIndex() const;
  size_t moduleCount() const;

 private:
  bool showIndex(size_t index);
  bool moduleEnabled(size_t index) const;

  app_core::AppConfig& config_;
  ScreenModule* modules_[kMaxModules] = {};
  size_t count_ = 0;
  size_t activeIndex_ = kMaxModules;
};

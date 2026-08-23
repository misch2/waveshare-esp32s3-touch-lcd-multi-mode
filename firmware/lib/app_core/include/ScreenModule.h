#pragma once

#include <cstdint>

#include "GestureRecognizer.h"

class ScreenModule {
 public:
  virtual ~ScreenModule() = default;
  virtual const char* id() const = 0;
  virtual const char* label() const = 0;
  virtual bool begin() = 0;
  virtual void show() = 0;
  virtual void hide() = 0;
  virtual void tick(uint32_t nowMs) = 0;
  virtual bool handleGesture(const GestureEvent& event) = 0;
};


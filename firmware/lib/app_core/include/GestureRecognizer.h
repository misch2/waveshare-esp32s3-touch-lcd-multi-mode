#pragma once

#include <cstdint>

enum class GestureKind : uint8_t {
  None,
  Tap,
  LongPress,
  HorizontalSwipe,
  VerticalSwipe,
};

struct GestureEvent {
  GestureKind kind = GestureKind::None;
  int16_t startX = 0;
  int16_t startY = 0;
  int16_t endX = 0;
  int16_t endY = 0;
  int8_t direction = 0;  // -1 = left/up, +1 = right/down.
};

struct GestureThresholds {
  uint16_t releaseDebounceMs = 60;
  uint16_t longPressMs = 500;
  uint16_t swipeMaxMs = 700;
  int16_t tapMovePx = 60;
  int16_t swipeMinPx = 70;
  int16_t swipeCrossAxisMaxPx = 90;
};

class GestureRecognizer {
 public:
  explicit GestureRecognizer(GestureThresholds thresholds = {});

  // Returns true exactly once when a complete gesture has been recognised.
  bool update(bool pressed, int16_t x, int16_t y, uint32_t nowMs,
              GestureEvent& event);
  void reset();

 private:
  GestureThresholds thresholds_;
  bool touching_ = false;
  int16_t startX_ = 0;
  int16_t startY_ = 0;
  int16_t lastX_ = 0;
  int16_t lastY_ = 0;
  uint32_t startMs_ = 0;
  uint32_t lastSeenMs_ = 0;
};


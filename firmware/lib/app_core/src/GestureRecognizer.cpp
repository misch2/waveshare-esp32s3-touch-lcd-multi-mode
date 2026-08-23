#include "GestureRecognizer.h"

#include <cstdlib>

GestureRecognizer::GestureRecognizer(GestureThresholds thresholds)
    : thresholds_(thresholds) {}

void GestureRecognizer::reset() {
  touching_ = false;
  startX_ = startY_ = lastX_ = lastY_ = 0;
  startMs_ = lastSeenMs_ = 0;
}

bool GestureRecognizer::update(bool pressed, int16_t x, int16_t y,
                               uint32_t nowMs, GestureEvent& event) {
  event = {};
  if (pressed) {
    if (!touching_) {
      touching_ = true;
      startX_ = x;
      startY_ = y;
      startMs_ = nowMs;
    }
    lastX_ = x;
    lastY_ = y;
    lastSeenMs_ = nowMs;
    return false;
  }

  if (!touching_ || nowMs - lastSeenMs_ < thresholds_.releaseDebounceMs) {
    return false;
  }

  touching_ = false;
  const int16_t dx = lastX_ - startX_;
  const int16_t dy = lastY_ - startY_;
  const uint32_t duration = lastSeenMs_ - startMs_;
  const int16_t absDx = static_cast<int16_t>(std::abs(dx));
  const int16_t absDy = static_cast<int16_t>(std::abs(dy));

  event.startX = startX_;
  event.startY = startY_;
  event.endX = lastX_;
  event.endY = lastY_;

  if (duration <= thresholds_.swipeMaxMs &&
      absDx >= thresholds_.swipeMinPx &&
      absDy <= thresholds_.swipeCrossAxisMaxPx && absDx >= absDy) {
    event.kind = GestureKind::HorizontalSwipe;
    event.direction = dx < 0 ? -1 : 1;
    return true;
  }
  if (duration <= thresholds_.swipeMaxMs &&
      absDy >= thresholds_.swipeMinPx &&
      absDx <= thresholds_.swipeCrossAxisMaxPx && absDy > absDx) {
    event.kind = GestureKind::VerticalSwipe;
    event.direction = dy < 0 ? -1 : 1;
    return true;
  }

  const bool smallMove =
      absDx < thresholds_.tapMovePx && absDy < thresholds_.tapMovePx;
  if (smallMove && duration >= thresholds_.longPressMs) {
    event.kind = GestureKind::LongPress;
    return true;
  }
  if (smallMove) {
    event.kind = GestureKind::Tap;
    return true;
  }
  return false;
}


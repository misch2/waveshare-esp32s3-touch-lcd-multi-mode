#include "RadarScreen.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "MeteoSettingsAdapter.h"

namespace {
constexpr int kCenterX = 240;
constexpr int kCenterY = 245;
constexpr float kPi = 3.14159265358979323846f;

void styleTransparent(lv_obj_t* object) {
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_pad_all(object, 0, 0);
}
}  // namespace

bool RadarScreen::begin() {
  screen_ = lv_obj_create(nullptr);
  if (screen_ == nullptr) return false;
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x020A0E), 0);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(screen_);
  lv_label_set_text(title, "METEO RADAR · PROTOTYP");
  lv_obj_set_style_text_color(title, lv_color_hex(0xE6F7FF), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 25);

  for (int diameter : {120, 240, 360, 440}) {
    lv_obj_t* ring = lv_obj_create(screen_);
    lv_obj_set_size(ring, diameter, diameter);
    lv_obj_set_pos(ring, kCenterX - diameter / 2, kCenterY - diameter / 2);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x19566A), 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  }

  static lv_point_t horizontal[] = {{25, kCenterY}, {455, kCenterY}};
  static lv_point_t vertical[] = {{kCenterX, 45}, {kCenterX, 445}};
  for (lv_point_t* points : {horizontal, vertical}) {
    lv_obj_t* line = lv_line_create(screen_);
    lv_line_set_points(line, points, 2);
    lv_obj_set_style_line_color(line, lv_color_hex(0x123E4B), 0);
    lv_obj_set_style_line_width(line, 1, 0);
  }

  sweepLine_ = lv_line_create(screen_);
  lv_obj_set_style_line_color(sweepLine_, lv_color_hex(0x4CCBEC), 0);
  lv_obj_set_style_line_width(sweepLine_, 3, 0);
  lv_obj_set_style_line_rounded(sweepLine_, true, 0);

  const lv_point_t aircraftPositions[] = {{315, 155}, {170, 290}, {345, 330}};
  for (const lv_point_t& position : aircraftPositions) {
    lv_obj_t* dot = lv_obj_create(screen_);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_pos(dot, position.x - 5, position.y - 5);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xFFB843), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  }

  rangeLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_color(rangeLabel_, lv_color_hex(0x4CCBEC), 0);
  lv_obj_align(rangeLabel_, LV_ALIGN_BOTTOM_MID, 0, -30);
  updateRange();

  lv_obj_t* hint = lv_label_create(screen_);
  lv_label_set_text(hint, "←/→ obrazovky    ↑/↓ rozsah");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x66858F), 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
  styleTransparent(hint);

  updateSweep(0);
  return true;
}

void RadarScreen::show() {
  if (screen_ != nullptr) lv_scr_load(screen_);
}

void RadarScreen::hide() {}

void RadarScreen::tick(uint32_t nowMs) {
  if (nowMs - lastSweepMs_ >= 50) {
    lastSweepMs_ = nowMs;
    updateSweep(nowMs);
  }
}

bool RadarScreen::handleGesture(const GestureEvent& event) {
  if (event.kind == GestureKind::VerticalSwipe) {
    // GestureRecognizer reports up as -1. The agreed module policy maps an
    // upward swipe to the next/wider upstream Meteo range.
    meteo_settings::stepRadarRange(event.direction < 0 ? 1 : -1);
    updateRange();
    return true;
  }
  if (event.kind == GestureKind::Tap && event.endY > 360) {
    meteo_settings::stepRadarRange(1);
    updateRange();
    return true;
  }
  return false;
}

void RadarScreen::updateRange() {
  if (rangeLabel_ == nullptr) return;
  const app_core::MeteoRadarConfig config = meteo_settings::radarConfig();
  char text[32];
  if (config.wholeCountry()) {
    std::snprintf(text, sizeof(text), "ROZSAH  CELA CR");
  } else {
    std::snprintf(text, sizeof(text), "ROZSAH  %.0f km", config.rangeKm());
  }
  lv_label_set_text(rangeLabel_, text);
}

void RadarScreen::updateSweep(uint32_t nowMs) {
  if (sweepLine_ == nullptr) return;
  const float phase = static_cast<float>(nowMs % 6000U) / 6000.0f;
  const float angle = phase * 2.0f * kPi;
  sweepPoints_[0] = {kCenterX, kCenterY};
  sweepPoints_[1] = {
      static_cast<lv_coord_t>(kCenterX + std::cos(angle) * 215.0f),
      static_cast<lv_coord_t>(kCenterY + std::sin(angle) * 215.0f),
  };
  lv_line_set_points(sweepLine_, sweepPoints_, 2);
}

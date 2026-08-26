#pragma once

// Minimal LVGL surface for compiling the host navigation-indicator adapter in
// the dependency-free native test suite. Keep the supported resolution symbol
// intentionally narrow: LVGL 8 exposes LV_HOR_RES, not LV_HOR_RES_MAX.
#include <cstddef>
#include <cstdint>
#include <iterator>

using lv_coord_t = std::int16_t;
using lv_opa_t = std::uint8_t;
using lv_color_t = std::uint32_t;

struct lv_obj_t {
  lv_coord_t x = 0;
  lv_coord_t y = 0;
  lv_coord_t width = 0;
  lv_coord_t height = 0;
  std::uint32_t flags = 0;
  lv_obj_t* parent = nullptr;
};

// Deliberately do not provide LV_HOR_RES_MAX. This catches accidental use of
// the removed LVGL 7 symbol at compile time.
#define LV_HOR_RES 480

constexpr lv_opa_t LV_OPA_TRANSP = 0;
constexpr lv_opa_t LV_OPA_COVER = 255;
constexpr int LV_RADIUS_CIRCLE = 0x7fff;
constexpr std::uint32_t LV_OBJ_FLAG_HIDDEN = 1u << 0;
constexpr std::uint32_t LV_OBJ_FLAG_SCROLLABLE = 1u << 1;
constexpr std::uint32_t LV_OBJ_FLAG_CLICKABLE = 1u << 2;

namespace lvgl_test {

inline lv_obj_t objects[32] = {};
inline std::size_t objectCount = 0;
inline lv_obj_t topLayer{};

}  // namespace lvgl_test

inline lv_obj_t* lv_layer_top() { return &lvgl_test::topLayer; }

inline lv_obj_t* lv_obj_create(lv_obj_t* parent) {
  if (lvgl_test::objectCount >= std::size(lvgl_test::objects)) return nullptr;
  lv_obj_t* object = &lvgl_test::objects[lvgl_test::objectCount++];
  *object = lv_obj_t{};
  object->parent = parent;
  return object;
}

inline void lv_obj_set_size(lv_obj_t* object, lv_coord_t width,
                            lv_coord_t height) {
  object->width = width;
  object->height = height;
}

inline void lv_obj_set_pos(lv_obj_t* object, lv_coord_t x, lv_coord_t y) {
  object->x = x;
  object->y = y;
}

inline void lv_obj_set_style_bg_opa(lv_obj_t*, lv_opa_t, int) {}
inline void lv_obj_set_style_border_width(lv_obj_t*, lv_coord_t, int) {}
inline void lv_obj_set_style_pad_all(lv_obj_t*, lv_coord_t, int) {}
inline void lv_obj_set_style_radius(lv_obj_t*, lv_coord_t, int) {}
inline void lv_obj_set_style_bg_color(lv_obj_t*, lv_color_t, int) {}
inline void lv_obj_set_style_border_color(lv_obj_t*, lv_color_t, int) {}

inline void lv_obj_clear_flag(lv_obj_t* object, std::uint32_t flags) {
  object->flags &= ~flags;
}

inline void lv_obj_add_flag(lv_obj_t* object, std::uint32_t flags) {
  object->flags |= flags;
}

inline lv_color_t lv_color_white() { return 0xffffffu; }
inline lv_color_t lv_color_hex(std::uint32_t value) { return value; }

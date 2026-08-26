#include "NavigationIndicator.h"

#include <lvgl.h>

#include "NavigationIndicatorModel.h"

namespace navigation_indicator {
namespace {

constexpr std::size_t kMaxDots = app_core::NavigationIndicatorModel::kMaxItems;
constexpr lv_coord_t kDotDiameter = 8;
constexpr lv_coord_t kDotGap = 20;
constexpr lv_coord_t kDotCenterY = 18;

lv_obj_t* overlay = nullptr;
lv_obj_t* dots[kMaxDots] = {};
app_core::NavigationIndicatorModel model;
const char* renderedIds[kMaxDots] = {};
std::size_t renderedCount = 0;
std::size_t renderedActiveIndex =
    app_core::NavigationIndicatorModel::kNoSelection;
bool overlayVisible = false;

bool unchanged() {
  if (renderedCount != model.count() ||
      renderedActiveIndex != model.activeIndex()) {
    return false;
  }
  for (std::size_t index = 0; index < renderedCount; ++index) {
    if (renderedIds[index] != model.idAt(index)) return false;
  }
  return true;
}

void setVisible(bool visible) {
  if (overlay == nullptr || overlayVisible == visible) return;
  if (visible) {
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  }
  overlayVisible = visible;
}

}  // namespace

bool begin() {
  if (overlay != nullptr) return true;

  overlay = lv_obj_create(lv_layer_top());
  if (overlay == nullptr) return false;
  lv_obj_set_size(overlay, LV_HOR_RES_MAX, 36);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_set_style_pad_all(overlay, 0, 0);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);

  for (std::size_t index = 0; index < kMaxDots; ++index) {
    dots[index] = lv_obj_create(overlay);
    if (dots[index] == nullptr) return false;
    lv_obj_set_size(dots[index], kDotDiameter, kDotDiameter);
    lv_obj_set_style_radius(dots[index], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dots[index], 1, 0);
    lv_obj_clear_flag(dots[index],
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(dots[index], LV_OBJ_FLAG_HIDDEN);
  }
  return true;
}

void update(const app_core::AppConfig& config,
            const char* const* registeredIds, std::size_t registeredCount,
            const char* activeId, bool visible) {
  if (overlay == nullptr) return;
  if (!visible) {
    setVisible(false);
    return;
  }

  model.refresh(config, registeredIds, registeredCount, activeId);
  if (model.count() <= 1) {
    setVisible(false);
    return;
  }

  if (overlayVisible && unchanged()) return;

  const lv_coord_t startX = LV_HOR_RES_MAX / 2 -
                             static_cast<lv_coord_t>(model.count() - 1) *
                                 kDotGap / 2;
  for (std::size_t index = 0; index < kMaxDots; ++index) {
    if (index >= model.count()) {
      lv_obj_add_flag(dots[index], LV_OBJ_FLAG_HIDDEN);
      renderedIds[index] = nullptr;
      continue;
    }

    const bool active = model.isActive(index);
    const lv_coord_t centerX = startX + static_cast<lv_coord_t>(index) * kDotGap;
    lv_obj_set_pos(dots[index], centerX - kDotDiameter / 2,
                   kDotCenterY - kDotDiameter / 2);
    lv_obj_set_style_bg_color(dots[index], lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dots[index],
                            active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(dots[index],
                                  active ? lv_color_white()
                                         : lv_color_hex(0x808080),
                                  0);
    lv_obj_clear_flag(dots[index], LV_OBJ_FLAG_HIDDEN);
    renderedIds[index] = model.idAt(index);
  }
  renderedCount = model.count();
  renderedActiveIndex = model.activeIndex();
  setVisible(true);
}

}  // namespace navigation_indicator

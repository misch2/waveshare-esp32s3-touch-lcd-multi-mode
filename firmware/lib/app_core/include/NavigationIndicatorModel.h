#pragma once

#include <cstddef>
#include <cstdint>

#include "AppConfig.h"

namespace app_core {

/**
 * Host-owned model for the small navigation indicator shown on each screen.
 *
 * The persisted AppConfig is the source of ordering and enablement, while the
 * registered ID list describes the screens that are actually present in this
 * firmware.  This deliberately keeps unknown configuration IDs out of the
 * rendered indicator so forward-compatible configuration does not produce
 * dots which cannot be selected.
 *
 * IDs are borrowed from the caller.  They are normally string literals or
 * ScreenModule::id() values and must remain valid while the model is used.
 */
class NavigationIndicatorModel final {
 public:
  static constexpr std::size_t kMaxItems = AppConfig::kMaxScreens;
  static constexpr std::size_t kNoSelection = kMaxItems;

  /**
   * Rebuild the indicator in configured order.
   *
   * Only enabled entries whose stable ID occurs in registeredIds are kept.
   * The active ID may be null or absent; in that case the model still exposes
   * the complete enabled registered list and activeIndex() returns
   * kNoSelection.
   */
  void refresh(const AppConfig& config, const char* const* registeredIds,
               std::size_t registeredCount, const char* activeId);

  std::size_t count() const { return count_; }
  std::size_t activeIndex() const { return activeIndex_; }
  const char* activeId() const;

  /** Return the configured stable ID at a rendered-dot index, or null. */
  const char* idAt(std::size_t index) const;

  /** Return whether the rendered-dot index represents the active screen. */
  bool isActive(std::size_t index) const;

 private:
  static bool idsEqual(const char* left, const char* right);
  bool isRegistered(const char* id, const char* const* registeredIds,
                    std::size_t registeredCount) const;
  bool alreadyIncluded(const char* id) const;

  const char* ids_[kMaxItems] = {};
  std::size_t count_ = 0;
  std::size_t activeIndex_ = kNoSelection;
};

}  // namespace app_core

#include "NavigationIndicatorModel.h"

#include <cstring>

namespace app_core {

// Keep the fixed-bound constants linkable for callers that odr-use them on
// the native test build or the Arduino toolchain.
constexpr std::size_t NavigationIndicatorModel::kMaxItems;
constexpr std::size_t NavigationIndicatorModel::kNoSelection;

void NavigationIndicatorModel::refresh(const AppConfig& config,
                                       const char* const* registeredIds,
                                       std::size_t registeredCount,
                                       const char* activeId) {
  count_ = 0;
  activeIndex_ = kNoSelection;

  if (registeredIds == nullptr || registeredCount == 0) return;

  const std::size_t configuredCount =
      config.screenCount <= AppConfig::kMaxScreens
          ? config.screenCount
          : AppConfig::kMaxScreens;
  for (std::size_t configuredIndex = 0;
       configuredIndex < configuredCount && count_ < kMaxItems;
       ++configuredIndex) {
    const AppConfig::Screen& configured = config.screens[configuredIndex];
    if (configured.enabled == 0 ||
        !isRegistered(configured.id, registeredIds, registeredCount) ||
        alreadyIncluded(configured.id)) {
      continue;
    }

    // Keep the registered spelling.  Config IDs are stable identifiers, but
    // the registered value is the lifetime-owned string used by the host and
    // its ScreenModule implementations.
    const char* registeredId = nullptr;
    for (std::size_t i = 0; i < registeredCount; ++i) {
      if (idsEqual(configured.id, registeredIds[i])) {
        registeredId = registeredIds[i];
        break;
      }
    }
    if (registeredId == nullptr) continue;

    ids_[count_] = registeredId;
    if (idsEqual(registeredId, activeId)) activeIndex_ = count_;
    ++count_;
  }
}

const char* NavigationIndicatorModel::activeId() const {
  return activeIndex_ < count_ ? ids_[activeIndex_] : nullptr;
}

const char* NavigationIndicatorModel::idAt(std::size_t index) const {
  return index < count_ ? ids_[index] : nullptr;
}

bool NavigationIndicatorModel::isActive(std::size_t index) const {
  return index < count_ && index == activeIndex_;
}

bool NavigationIndicatorModel::idsEqual(const char* left, const char* right) {
  return left != nullptr && right != nullptr &&
         std::strncmp(left, right, AppConfig::kScreenIdStorage) == 0;
}

bool NavigationIndicatorModel::isRegistered(
    const char* id, const char* const* registeredIds,
    std::size_t registeredCount) const {
  if (id == nullptr || registeredIds == nullptr) return false;
  for (std::size_t i = 0; i < registeredCount; ++i) {
    if (idsEqual(id, registeredIds[i])) return true;
  }
  return false;
}

bool NavigationIndicatorModel::alreadyIncluded(const char* id) const {
  for (std::size_t i = 0; i < count_; ++i) {
    if (idsEqual(ids_[i], id)) return true;
  }
  return false;
}

}  // namespace app_core

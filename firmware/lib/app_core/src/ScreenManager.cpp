#include "ScreenManager.h"

#include <cstring>

ScreenManager::ScreenManager(app_core::AppConfig& config) : config_(config) {}

bool ScreenManager::add(ScreenModule& module) {
  if (count_ >= kMaxModules || config_.findScreen(module.id()) < 0) {
    return false;
  }
  for (size_t i = 0; i < count_; ++i) {
    if (std::strcmp(modules_[i]->id(), module.id()) == 0) return false;
  }
  modules_[count_++] = &module;
  return true;
}

bool ScreenManager::begin() {
  for (size_t i = 0; i < count_; ++i) {
    if (!modules_[i]->begin()) return false;
  }
  for (uint8_t order = 0; order < config_.screenCount; ++order) {
    const char* wanted = config_.screens[order].id;
    for (size_t i = 0; i < count_; ++i) {
      if (std::strcmp(modules_[i]->id(), wanted) == 0 && moduleEnabled(i)) {
        return showIndex(i);
      }
    }
  }
  return false;
}

bool ScreenManager::moduleEnabled(size_t index) const {
  return index < count_ && config_.isEnabled(modules_[index]->id());
}

bool ScreenManager::showIndex(size_t index) {
  if (!moduleEnabled(index)) return false;
  if (activeIndex_ == index) return true;
  if (activeIndex_ < count_) modules_[activeIndex_]->hide();
  activeIndex_ = index;
  modules_[activeIndex_]->show();
  lastRotationMs_ = 0;
  return true;
}

bool ScreenManager::showById(const char* id) {
  for (size_t i = 0; i < count_; ++i) {
    if (std::strcmp(modules_[i]->id(), id) == 0) return showIndex(i);
  }
  return false;
}

bool ScreenManager::step(int8_t direction) {
  if (count_ == 0 || activeIndex_ >= count_ || direction == 0 ||
      config_.screenCount == 0) {
    return false;
  }

  const int8_t activeOrder = config_.findScreen(modules_[activeIndex_]->id());
  if (activeOrder < 0) return false;
  size_t nextOrder = static_cast<size_t>(activeOrder);
  for (size_t guard = 0; guard < config_.screenCount; ++guard) {
    nextOrder = direction > 0
                    ? (nextOrder + 1) % config_.screenCount
                    : (nextOrder + config_.screenCount - 1) % config_.screenCount;
    const app_core::AppConfig::Screen& preference = config_.screens[nextOrder];
    if (preference.enabled == 0) continue;
    for (size_t moduleIndex = 0; moduleIndex < count_; ++moduleIndex) {
      if (moduleIndex != activeIndex_ &&
          std::strcmp(modules_[moduleIndex]->id(), preference.id) == 0) {
        return showIndex(moduleIndex);
      }
    }
  }
  return false;
}

bool ScreenManager::dispatch(const GestureEvent& event) {
  if (activeIndex_ >= count_) return false;
  if (event.kind == GestureKind::HorizontalSwipe) {
    // A leftward swipe reveals the next screen; a rightward swipe the previous.
    return step(event.direction < 0 ? 1 : -1);
  }
  return modules_[activeIndex_]->handleGesture(event);
}

void ScreenManager::tick(uint32_t nowMs) {
  if (activeIndex_ < count_) modules_[activeIndex_]->tick(nowMs);
  if (config_.autoRotateSeconds == 0 || count_ < 2) return;
  if (lastRotationMs_ == 0) {
    lastRotationMs_ = nowMs;
    return;
  }
  const uint32_t interval = static_cast<uint32_t>(config_.autoRotateSeconds) * 1000U;
  if (nowMs - lastRotationMs_ >= interval) {
    lastRotationMs_ = nowMs;
    step(1);
  }
}

ScreenModule* ScreenManager::active() const {
  return activeIndex_ < count_ ? modules_[activeIndex_] : nullptr;
}

size_t ScreenManager::activeIndex() const { return activeIndex_; }
size_t ScreenManager::moduleCount() const { return count_; }

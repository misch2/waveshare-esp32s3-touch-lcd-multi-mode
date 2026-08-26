#pragma once

#include <cstddef>

#include "AppConfig.h"

namespace navigation_indicator {

// Creates the host-owned LVGL overlay. Call after DisplayHost has initialized
// LVGL and before the ScreenManager first shows a module.
bool begin();

// Presents the configured enabled screen order on Meteo canvas screens. The
// IDs are stable host identities; unknown configured IDs are not rendered.
void update(const app_core::AppConfig& config,
            const char* const* registeredIds, std::size_t registeredCount,
            const char* activeId, bool visible);

}  // namespace navigation_indicator

#pragma once

#include "MeteoRadarConfig.h"

namespace meteo_settings {

using StorageBeginCallback = bool (*)();
using StorageEndCallback = bool (*)();

// Loads the canonical MeteoPlaneRadar Settings model. Call before display
// startup so reads and any upstream migration complete before RGB scanout.
bool begin();

// Install after DisplayHost is ready. Every later upstream Preferences write
// is bracketed by these host-owned callbacks.
void setStorageCallbacks(StorageBeginCallback beginCallback,
                         StorageEndCallback endCallback);

app_core::MeteoRadarConfig radarConfig();
void stepRadarRange(int step);

// Preserve the upstream "location not configured" state during a combined
// restore without clearing unrelated Meteo credentials or settings.
bool clearLocation();

// Flushes the upstream debounced UI-state writes when they become due.
void loop();

}  // namespace meteo_settings

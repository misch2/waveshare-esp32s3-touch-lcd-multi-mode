#pragma once

// Re-export the upstream callback and mode types instead of introducing a
// second web configuration schema. The firmware build exposes the pinned
// submodule include directory through platformio.ini.
#include "ConfigurationWeb.h"
#include "MeteoWebRoutes.h"

namespace web_host {

using ClockConfigLoadCallback = ::ClockConfigLoadCallback;
using ClockConfigSaveCallback = ::ClockConfigSaveCallback;
using ConfigurationWebStatusCallback = ::ConfigurationWebStatusCallback;
using SunTransitionTimesCallback = ::SunTransitionTimesCallback;
using HomeAssistantRefreshCallback = ::HomeAssistantRefreshCallback;
using DisplayPowerCallback = ::DisplayPowerCallback;
using DisplayPowerStatusCallback = ::DisplayPowerStatusCallback;
using DayNightStatusCallback = ::DayNightStatusCallback;
using StorageBeginCallback = ::ConfigurationStorageBeginCallback;
using StorageEndCallback = ::ConfigurationStorageEndCallback;
using Mode = ::ConfigurationWebMode;

constexpr Mode MODE_TIMED = ::CONFIGURATION_WEB_TIMED;
constexpr Mode MODE_ALWAYS = ::CONFIGURATION_WEB_ALWAYS;
constexpr Mode MODE_DISABLED = ::CONFIGURATION_WEB_DISABLED;

// The integration host owns the sole HTTP server and mounts the upstream
// clock and Meteo pages below their canonical module prefixes.
bool begin(ClockConfigLoadCallback loadCallback,
           ClockConfigSaveCallback saveCallback,
           ConfigurationWebStatusCallback statusCallback,
           SunTransitionTimesCallback sunTimesCallback,
           HomeAssistantRefreshCallback refreshCallback,
           DayNightStatusCallback dayNightStatusCallback,
           DisplayPowerCallback displayPowerCallback,
           DisplayPowerStatusCallback displayPowerStatusCallback,
           StorageBeginCallback storageBeginCallback,
           StorageEndCallback storageEndCallback,
           const app_core::MeteoWebRoutes& meteoRoutes);
void loop();
void ensureActive();
bool active();
Mode mode();
bool setMode(Mode mode);

}  // namespace web_host

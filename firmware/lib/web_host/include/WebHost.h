#pragma once

// Re-export the upstream callback and mode types instead of introducing a
// second web configuration schema. The firmware build exposes the pinned
// submodule include directory through platformio.ini.
#include "ConfigurationWeb.h"

namespace web_host {

using ClockConfigLoadCallback = ::ClockConfigLoadCallback;
using ClockConfigSaveCallback = ::ClockConfigSaveCallback;
using ConfigurationWebStatusCallback = ::ConfigurationWebStatusCallback;
using SunTransitionTimesCallback = ::SunTransitionTimesCallback;
using HomeAssistantRefreshCallback = ::HomeAssistantRefreshCallback;
using DisplayPowerCallback = ::DisplayPowerCallback;
using DisplayPowerStatusCallback = ::DisplayPowerStatusCallback;
using DayNightStatusCallback = ::DayNightStatusCallback;
using Mode = ::ConfigurationWebMode;

constexpr Mode MODE_TIMED = ::CONFIGURATION_WEB_TIMED;
constexpr Mode MODE_ALWAYS = ::CONFIGURATION_WEB_ALWAYS;
constexpr Mode MODE_DISABLED = ::CONFIGURATION_WEB_DISABLED;

// The host owns when this lifecycle is called. These functions only delegate
// to the upstream implementation and do not create a second WebServer.
void begin(ClockConfigLoadCallback loadCallback,
           ClockConfigSaveCallback saveCallback,
           ConfigurationWebStatusCallback statusCallback,
           SunTransitionTimesCallback sunTimesCallback,
           HomeAssistantRefreshCallback refreshCallback,
           DayNightStatusCallback dayNightStatusCallback,
           DisplayPowerCallback displayPowerCallback,
           DisplayPowerStatusCallback displayPowerStatusCallback);
void loop();
void ensureActive();
Mode mode();
bool setMode(Mode mode);

}  // namespace web_host

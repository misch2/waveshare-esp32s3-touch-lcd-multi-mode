#include "WebHost.h"

namespace web_host {

void begin(ClockConfigLoadCallback loadCallback,
           ClockConfigSaveCallback saveCallback,
           ConfigurationWebStatusCallback statusCallback,
           SunTransitionTimesCallback sunTimesCallback,
           HomeAssistantRefreshCallback refreshCallback,
           DayNightStatusCallback dayNightStatusCallback,
           DisplayPowerCallback displayPowerCallback,
           DisplayPowerStatusCallback displayPowerStatusCallback) {
  ::configurationWebBegin(
      loadCallback, saveCallback, statusCallback, sunTimesCallback,
      refreshCallback, dayNightStatusCallback, displayPowerCallback,
      displayPowerStatusCallback);
}

void loop() { ::configurationWebLoop(); }

void ensureActive() { ::configurationWebEnsureActive(); }

Mode mode() { return ::configurationWebMode(); }

bool setMode(Mode selectedMode) {
  return ::configurationWebSetMode(selectedMode);
}

}  // namespace web_host

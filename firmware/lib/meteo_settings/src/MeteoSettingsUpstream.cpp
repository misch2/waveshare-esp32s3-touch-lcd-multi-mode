// Thin translation unit: compile the pinned upstream settings implementation
// without adding MeteoPlaneRadar's application entry point.
#include <Arduino.h>
#ifdef BOOT_PIN
#undef BOOT_PIN
#endif
#include "../../../../MeteoPlaneRadar/MeteoPlaneRadar/Settings.cpp"

// Narrow host-only seam in the same translation unit as the pinned upstream
// implementation, so it changes only the location-presence flag while
// retaining Wi-Fi, the admin password and every other planeradar key.
bool MeteoSettings_ClearLocationForHost() {
  if (!beginPreferencesWrite()) return false;
  const bool persisted = prefs.putBool("hasLoc", false) == 1;
  endPreferencesWrite();
  if (persisted) s_hasLoc = false;
  return persisted;
}

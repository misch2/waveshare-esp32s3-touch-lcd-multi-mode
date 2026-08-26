#include "MeteoSettingsAdapter.h"

#include <Arduino.h>

#include "NetworkFetchGate.h"

// Arduino 3.x declares a placeholder BOOT_PIN macro. The pinned Meteo config
// owns the actual board value; remove the placeholder before including it.
#ifdef BOOT_PIN
#undef BOOT_PIN
#endif
#include "../../../../MeteoPlaneRadar/MeteoPlaneRadar/Settings.h"
#include "../../../../MeteoPlaneRadar/MeteoPlaneRadar/GeoIP.h"

extern bool MeteoSettings_ClearLocationForHost();

namespace meteo_settings {
namespace {
bool started = false;
}

bool begin() {
  if (started) return true;
  Settings_Begin();
  started = true;
  return true;
}

void setStorageCallbacks(StorageBeginCallback beginCallback,
                         StorageEndCallback endCallback) {
  Settings_SetStorageCallbacks(beginCallback, endCallback);
}

app_core::MeteoRadarConfig radarConfig() {
  app_core::MeteoRadarConfig config;
  config.latitude = Settings_Lat();
  config.longitude = Settings_Lon();
  config.source = static_cast<app_core::MeteoRadarSource>(
      Settings_RadarSource());
  config.rangeIndex = Settings_MeteoRange();
  config.normalize();
  return config;
}

void stepRadarRange(int step) {
  app_core::MeteoRadarConfig config = radarConfig();
  config.stepRange(step);
  Settings_SetMeteoRange(config.rangeIndex);
}

GeoIpAttemptResult tryDetectLocation() {
  if (Settings_HasLocation()) return GeoIpAttemptResult::NotNeeded;

  network_host::FetchLease lease(0);
  if (!lease) return GeoIpAttemptResult::Busy;
  return GeoIP_DetectIfNeeded() ? GeoIpAttemptResult::Detected
                                : GeoIpAttemptResult::Failed;
}

bool clearLocation() { return MeteoSettings_ClearLocationForHost(); }

void loop() {
  if (started) Settings_Tick();
}

}  // namespace meteo_settings

#include "Outside.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <ctime>

#ifdef BOOT_PIN
#undef BOOT_PIN
#endif

#include "Config.h"
#include "MeteoOutsideTemperaturePolicy.h"
#include "Net.h"
#include "NetworkFetchGate.h"
#include "NetworkHost.h"
#include "Settings.h"

namespace {
constexpr time_t kValidTimeThreshold = 1700000000;

bool haveTemperature = false;
float temperatureC = 0.0f;
app_core::MeteoOutsideTemperaturePolicy temperaturePolicy;

void storeTemperature(float degC, bool fromForecast) {
  if (!std::isfinite(degC)) return;
  temperatureC = degC;
  haveTemperature = true;
  if (fromForecast) {
    temperaturePolicy.noteForecastTemperature(millis());
  } else {
    temperaturePolicy.noteStandaloneTemperature();
  }
}

bool fetchCurrentTemperature(float* value) {
  if (value == nullptr || !network_host::connected()) return false;

  char url[192];
  std::snprintf(url, sizeof(url),
                "%s?latitude=%.4f&longitude=%.4f&current=temperature_2m",
                OUTSIDE_TEMP_URL, Settings_Lat(), Settings_Lon());

  String body;
  if (!Net_GetString(url, body, "TEPLOTA")) return false;

  JsonDocument filter;
  filter["current"]["temperature_2m"] = true;
  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, body, DeserializationOption::Filter(filter));
  if (error) {
    Serial.printf("TEPLOTA: JSON %s\n", error.c_str());
    return false;
  }

  const JsonVariant temperature = document["current"]["temperature_2m"];
  if (temperature.isNull()) {
    Serial.println("TEPLOTA: v odpovedi neni temperature_2m");
    return false;
  }

  const float parsed = temperature.as<float>();
  if (!std::isfinite(parsed)) return false;
  *value = parsed;
  return true;
}
}  // namespace

void Outside_NoteHttpDate(const char* date) {
  // The combined host is the sole owner of SNTP/system time. Upstream network
  // responses may still carry Date, but they must not reseed that clock.
  (void)date;
}

bool Outside_TimeValid() {
  time_t now;
  time(&now);
  return now >= kValidTimeThreshold;
}

void Outside_NoteTemp(float degC) {
  // Forecast_Tick already fetched this value as part of its own request. Keep
  // the fallback quiet for that response's normal lifetime instead of opening
  // a second TLS connection for the same model value.
  storeTemperature(degC, true);
}

void Outside_Tick() {
  // This is called from the host loop, not from a network callback or render
  // path. A zero-timeout lease keeps it from delaying an active screen's
  // request batch when another module currently owns the shared transport.
  if (!network_host::connected()) return;

  const uint32_t nowMs = millis();
  if (!temperaturePolicy.shouldFetch(nowMs)) return;

  network_host::FetchLease lease(0);
  if (!lease) return;

  // Do not consume the retry interval when another module owns the transport;
  // the next host-loop pass can try again as soon as that batch is complete.
  temperaturePolicy.noteAttempt(nowMs);

  float fetchedTemperature = 0.0f;
  if (!fetchCurrentTemperature(&fetchedTemperature)) return;

  storeTemperature(fetchedTemperature, false);
  Serial.printf("Teplota: %.1f C\n", fetchedTemperature);
}

void Outside_StatusText(char* buffer, size_t capacity) {
  if (buffer == nullptr || capacity == 0) return;
  buffer[0] = '\0';

  time_t now;
  time(&now);
  if (now < kValidTimeThreshold) {
    if (haveTemperature) {
      std::snprintf(buffer, capacity, "%.0f degC", temperatureC);
    }
    return;
  }

  struct tm local = {};
  localtime_r(&now, &local);
  if (haveTemperature) {
    std::snprintf(buffer, capacity, "%02d:%02d   %.0f degC", local.tm_hour,
                  local.tm_min, temperatureC);
  } else {
    std::snprintf(buffer, capacity, "%02d:%02d", local.tm_hour,
                  local.tm_min);
  }
}

#include "Outside.h"

#include <Arduino.h>

#include <cmath>
#include <cstdio>
#include <ctime>

namespace {
constexpr time_t kValidTimeThreshold = 1700000000;
bool haveTemperature = false;
float temperatureC = 0.0f;
}

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
  if (!std::isfinite(degC)) return;
  temperatureC = degC;
  haveTemperature = true;
}

void Outside_Tick() {}

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

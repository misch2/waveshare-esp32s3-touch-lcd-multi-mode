#include "ClockDataService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "DayNightLogic.h"
#include "FirmwareHubCa.h"
#include "HomeAssistantBatchPolicy.h"
#include "NetworkFetchGate.h"
#include "NetworkDiagnostics.h"
#include "TmepService.h"

namespace {

constexpr uint32_t HOME_ASSISTANT_REFRESH_MS = 60UL * 1000UL;
constexpr uint32_t HOME_ASSISTANT_RETRY_MS = 5UL * 1000UL;
constexpr uint32_t HOME_ASSISTANT_CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t HOME_ASSISTANT_RESPONSE_TIMEOUT_MS = 8000;
constexpr uint8_t HOME_ASSISTANT_REQUEST_ATTEMPTS = 2;
constexpr uint32_t HOME_ASSISTANT_REQUEST_RETRY_DELAY_MS = 250;
constexpr uint32_t OPEN_METEO_REFRESH_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t TMEP_REFRESH_MS = 60UL * 1000UL;
constexpr uint32_t NETWORK_FETCH_GATE_TIMEOUT_MS = 15UL * 1000UL;
constexpr time_t VALID_TIME_THRESHOLD = 1700000000;

void logNetworkFailure(const char* source, int status) {
  const uint32_t freeInternal = static_cast<uint32_t>(
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const uint32_t largestInternal = static_cast<uint32_t>(
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  Serial.printf("Clock data: %s failed (status %d, internal free %u B, largest %u B)\n",
                source, status, freeInternal, largestInternal);
}

bool extractJsonStringField(const String& payload, const char* key,
                            String& value);
bool stateAsFloat(const String& state, float& value);
int weatherCodeForState(const String& state);

// Upstream extraction: WaveshareHodiny.ino @
// 9537a76932fc9269b2a22a5fb90a62785897c680, lines 582-600.
bool extractJsonNumberField(const String& payload, const char* key,
                            double& value) {
  const String quotedKey = String('"') + key + '"';
  int searchFrom = 0;
  while (true) {
    const int keyPosition = payload.indexOf(quotedKey, searchFrom);
    if (keyPosition < 0) return false;
    const int colonPosition =
        payload.indexOf(':', keyPosition + quotedKey.length());
    if (colonPosition < 0) return false;
    const char* start = payload.c_str() + colonPosition + 1;
    while (*start == ' ' || *start == '\t' || *start == '\r' ||
           *start == '\n' || *start == '[') {
      ++start;
    }
    char* end = nullptr;
    value = strtod(start, &end);
    if (end != start && std::isfinite(value)) return true;
    searchFrom = keyPosition + quotedKey.length();
  }
}

// Upstream extraction: WaveshareHodiny.ino @
// 9537a76932fc9269b2a22a5fb90a62785897c680, lines 606-623.
int openMeteoWeatherCode(int wmoCode) {
  if (wmoCode == 0) return 800;
  if (wmoCode == 1 || wmoCode == 2) return 801;
  if (wmoCode == 3) return 804;
  if (wmoCode == 45 || wmoCode == 48) return 741;
  if (wmoCode == 51 || wmoCode == 53 || wmoCode == 55) return 300;
  if (wmoCode == 56 || wmoCode == 57) return 511;
  if (wmoCode == 61 || wmoCode == 63 || wmoCode == 80 || wmoCode == 81)
    return 500;
  if (wmoCode == 65 || wmoCode == 82) return 502;
  if (wmoCode == 66 || wmoCode == 67) return 511;
  if (wmoCode == 71 || wmoCode == 73 || wmoCode == 77 || wmoCode == 85)
    return 600;
  if (wmoCode == 75 || wmoCode == 86) return 602;
  if (wmoCode == 95) return 200;
  if (wmoCode == 96 || wmoCode == 99) return 202;
  return -1;
}

// Upstream extraction: WaveshareHodiny.ino @
// 9537a76932fc9269b2a22a5fb90a62785897c680, lines 625-688.
bool fetchOpenMeteo(const ClockConfig& config, ClockValues& values) {
  networkDiagnosticsBegin(NetworkDiagnosticKind::OpenMeteoRuntime);
  String url = F("https://api.open-meteo.com/v1/forecast?latitude=");
  url += String(config.openMeteoLatitude, 5);
  url += F("&longitude=");
  url += String(config.openMeteoLongitude, 5);
  url += F("&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,rain,showers,snowfall,weather_code,cloud_cover,pressure_msl,surface_pressure,wind_speed_10m,wind_direction_10m,wind_gusts_10m,uv_index&daily=sunrise,sunset&timeformat=unixtime&timezone=auto&forecast_days=1");
  WiFiClientSecure client;
  client.setCACert(FIRMWARE_RELEASE_ROOT_CA);
  HTTPClient http;
  http.setConnectTimeout(HOME_ASSISTANT_CONNECT_TIMEOUT_MS);
  http.setTimeout(HOME_ASSISTANT_RESPONSE_TIMEOUT_MS);
  int status = HTTPC_ERROR_CONNECTION_REFUSED;
  String payload;
  if (http.begin(client, url)) {
    status = http.GET();
    if (status == HTTP_CODE_OK) payload = http.getString();
    http.end();
  }
  if (status != HTTP_CODE_OK) {
    logNetworkFailure("Open-Meteo", status);
    networkDiagnosticsEnd(NetworkDiagnosticKind::OpenMeteoRuntime, false,
                          status);
    return false;
  }
  double number = 0;
  bool ok = extractJsonNumberField(payload, "weather_code", number);
  if (ok) values.weatherCode = openMeteoWeatherCode(lround(number));
  if (extractJsonNumberField(payload, "is_day", number)) {
    values.weatherIsDay = number >= 0.5;
    values.sunStateAvailable = true;
  }
  if (extractJsonNumberField(payload, "sunrise", number))
    values.nextSunriseTimestamp = static_cast<uint64_t>(number);
  if (extractJsonNumberField(payload, "sunset", number))
    values.nextSunsetTimestamp = static_cast<uint64_t>(number);
  if (values.nextSunriseTimestamp > 0 && values.nextSunsetTimestamp > 0) {
    const time_t now = time(nullptr);
    if (now >= VALID_TIME_THRESHOLD) {
      const time_t dayStarts =
          static_cast<time_t>(values.nextSunriseTimestamp) +
          config.sunriseOffsetMinutes * 60;
      const time_t dayEnds = static_cast<time_t>(values.nextSunsetTimestamp) +
                             config.sunsetOffsetMinutes * 60;
      values.weatherIsDay = now >= dayStarts && now < dayEnds;
      values.sunStateAvailable = true;
    }
  }
  float* destinations[] = {&values.leftTemperatureC, &values.rightTemperatureC,
                           &values.metricAValue, &values.metricBValue};
  for (size_t index = 0; index < 4; ++index) {
    if (config.tmepSlots[index].enabled) continue;
    if (extractJsonNumberField(payload, config.openMeteoSlots[index].value,
                               number)) {
      *destinations[index] = static_cast<float>(number);
    } else {
      *destinations[index] = NAN;
      ok = false;
    }
  }
  values.homeAssistantOnline = false;
  networkDiagnosticsSetDetail(NetworkDiagnosticKind::OpenMeteoRuntime,
                              ok ? F("Aktuální data načtena")
                                 : F("Odpověď neobsahuje všechny hodnoty"));
  networkDiagnosticsEnd(NetworkDiagnosticKind::OpenMeteoRuntime, ok, status);
  return ok;
}

// Adapted from WaveshareHodiny.ino v1.7.2 (581087e). The catalog/parser and
// transport remain upstream; this service only maps its snapshot to slots.
struct TmepValuesContext {
  const ClockConfig& config;
  ClockValues& values;
  bool complete = true;
};

void applyTmepValues(const TmepCatalog& catalog, void* rawContext) {
  auto& context = *static_cast<TmepValuesContext*>(rawContext);
  float* destinations[] = {&context.values.leftTemperatureC,
                           &context.values.rightTemperatureC,
                           &context.values.metricAValue,
                           &context.values.metricBValue};
  for (size_t index = 0; index < 4; ++index) {
    const ClockTmepSlotConfig& slot = context.config.tmepSlots[index];
    if (!slot.enabled) continue;
    const TmepSensor* sensor = tmepFindSensor(catalog, slot.sensorId);
    const TmepValue* value = sensor == nullptr ? nullptr
                                              : tmepFindValue(*sensor, slot.field);
    if (value == nullptr || !value->available) {
      *destinations[index] = NAN;
      context.complete = false;
    } else {
      *destinations[index] = value->value;
    }
  }
}

bool fetchTmepValues(const ClockConfig& config, ClockValues& values) {
  TmepValuesContext context{config, values};
  int status = 0;
  String error;
  // tmepFetchCatalog owns NetworkOperationGuard, bridged to the host gate.
  if (!tmepFetchCatalog(config.tmepExportId, config.tmepExportKey,
                          applyTmepValues, &context,
                          NetworkDiagnosticKind::TmepRuntime, status, error)) return false;
  if (!context.complete) {
    networkDiagnosticsSetDetail(NetworkDiagnosticKind::TmepRuntime,
                                 F("Export neobsahuje všechny vybrané hodnoty."));
  }
  return context.complete;
}

// Upstream extraction: WaveshareHodiny.ino @
// 9537a76932fc9269b2a22a5fb90a62785897c680, lines 691-755.
bool applyHomeAssistantState(const ClockConfig& config, const String& entityId,
                             const String& state, ClockValues& values) {
  float number;
  if (entityId == config.weatherEntityId) {
    values.weatherCode = weatherCodeForState(state);
  } else if (entityId == config.leftSide.temperatureEntityId &&
             stateAsFloat(state, number)) {
    values.leftTemperatureC = number;
  } else if (entityId == config.rightSide.temperatureEntityId &&
             stateAsFloat(state, number)) {
    values.rightTemperatureC = number;
  } else if (entityId == config.metricA.entityId && stateAsFloat(state, number)) {
    values.metricAValue = number;
  } else if (entityId == config.metricB.entityId && stateAsFloat(state, number)) {
    values.metricBValue = number;
  } else if (entityId == config.sunEntityId &&
             (state == "above_horizon" || state == "below_horizon")) {
    values.weatherIsDay = state == "above_horizon";
    values.sunStateAvailable = true;
  } else if (entityId == config.dayNightLightEntityId &&
             (state == "on" || state == "off")) {
    values.dayNightLightOn = state == "on";
    values.dayNightLightStateAvailable = true;
  } else {
    return false;
  }
  return true;
}

bool requestHomeAssistantState(HTTPClient& http, NetworkClient& client,
                               const ClockConfig& config, const char* entityId,
                               String& payload, int& lastStatus,
                               bool& clientStarted, bool& transportFailure) {
  transportFailure = false;
  if (entityId[0] == '\0') return false;
  const String url = String(config.homeAssistantUrl) + "/api/states/" + entityId;
  for (uint8_t attempt = 0; attempt < HOME_ASSISTANT_REQUEST_ATTEMPTS;
       ++attempt) {
    const bool configured = clientStarted ? http.setURL(url) : http.begin(client, url);
    if (!configured) {
      lastStatus = HTTPC_ERROR_CONNECTION_REFUSED;
      transportFailure = true;
      logNetworkFailure("Home Assistant transport", lastStatus);
      break;
    } else {
      clientStarted = true;
      http.addHeader("Accept", "application/json");
      lastStatus = http.GET();
      if (lastStatus == HTTP_CODE_OK) payload = http.getString();
      if (lastStatus == HTTP_CODE_OK) return true;
    }
    const auto decision = app_core::HomeAssistantBatchPolicy::decide(lastStatus);
    if (!decision.continueBatch) {
      transportFailure = true;
      logNetworkFailure("Home Assistant transport", lastStatus);
      break;
    }
    if (!decision.retryRequest ||
        attempt + 1 >= HOME_ASSISTANT_REQUEST_ATTEMPTS) {
      break;
    }
    delay(HOME_ASSISTANT_REQUEST_RETRY_DELAY_MS);
  }
  if (!transportFailure) logNetworkFailure("Home Assistant", lastStatus);
  return false;
}

// Upstream extraction: WaveshareHodiny.ino @
// 9537a76932fc9269b2a22a5fb90a62785897c680, lines 757-805.
bool parseIso8601Timestamp(const String& value, time_t& timestamp) {
  if (value.length() < 19) return false;
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (sscanf(value.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day,
             &hour, &minute, &second) != 6) {
    return false;
  }
  const char* end = value.c_str() + 19;
  while ((*end >= '0' && *end <= '9') || *end == '.') ++end;
  long offsetSeconds = 0;
  if (*end == '+' || *end == '-') {
    const int direction = *end == '+' ? 1 : -1;
    int hours = 0;
    int minutes = 0;
    if (sscanf(end + 1, "%2d:%2d", &hours, &minutes) != 2) return false;
    offsetSeconds = direction * (hours * 3600L + minutes * 60L);
  } else if (*end != 'Z' && *end != '\0') {
    return false;
  }
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear =
      (153U * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2U) /
          5U +
      static_cast<unsigned>(day - 1);
  const unsigned dayOfEra =
      yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
  const int64_t daysSinceEpoch =
      static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
  timestamp = static_cast<time_t>(daysSinceEpoch * 86400 + hour * 3600 +
                                  minute * 60 + second - offsetSeconds);
  return timestamp > 0;
}

bool previousLocalDayTimestamp(time_t nextTimestamp,
                               time_t& previousTimestamp) {
  if (nextTimestamp <= 0) return false;
  struct tm localTransition;
  if (localtime_r(&nextTimestamp, &localTransition) == nullptr) return false;
  --localTransition.tm_mday;
  localTransition.tm_isdst = -1;
  previousTimestamp = mktime(&localTransition);
  return previousTimestamp > 0 && previousTimestamp < nextTimestamp;
}

// Upstream extraction: WaveshareHodiny.ino @
// 9537a76932fc9269b2a22a5fb90a62785897c680, lines 808-853.
bool applySunState(const ClockConfig& config, const String& payload,
                   const String& state, ClockValues& values) {
  values.sunStateAvailable = false;
  if (state != "above_horizon" && state != "below_horizon") return false;
  String sunriseText;
  String sunsetText;
  time_t sunrise = 0;
  time_t sunset = 0;
  if (extractJsonStringField(payload, "next_rising", sunriseText) &&
      parseIso8601Timestamp(sunriseText, sunrise)) {
    values.nextSunriseTimestamp = static_cast<uint64_t>(sunrise);
  }
  if (extractJsonStringField(payload, "next_setting", sunsetText) &&
      parseIso8601Timestamp(sunsetText, sunset)) {
    values.nextSunsetTimestamp = static_cast<uint64_t>(sunset);
  }
  const bool horizonIsDay = state == "above_horizon";
  String lastChangedText;
  time_t lastChanged = 0;
  time_t expectedCompletedTransition = 0;
  const time_t nextSameTransition = horizonIsDay ? sunrise : sunset;
  const time_t nextUpcomingTransition = horizonIsDay ? sunset : sunrise;
  const time_t now = time(nullptr);
  const bool lastChangedAvailable =
      extractJsonStringField(payload, "last_changed", lastChangedText) &&
      parseIso8601Timestamp(lastChangedText, lastChanged);
  const bool expectedCompletedTransitionAvailable = previousLocalDayTimestamp(
      nextSameTransition, expectedCompletedTransition);
  int64_t completedTransition = 0;
  const bool completedTransitionAvailable =
      clockSelectCompletedTransitionTimestamp(
          lastChangedAvailable, static_cast<int64_t>(lastChanged),
          expectedCompletedTransitionAvailable,
          static_cast<int64_t>(expectedCompletedTransition),
          completedTransition);
  const bool nextTransitionAvailable = nextUpcomingTransition > 0;
  const ClockSunDecision decision = clockEvaluateSunDecision(
      horizonIsDay, config.sunriseOffsetMinutes, config.sunsetOffsetMinutes,
      static_cast<int64_t>(now), completedTransitionAvailable,
      completedTransition, nextTransitionAvailable,
      static_cast<int64_t>(nextUpcomingTransition));
  if (decision == ClockSunDecision::Unavailable) return false;
  values.weatherIsDay = decision == ClockSunDecision::Day;
  values.sunStateAvailable = true;
  return true;
}

// Upstream extraction: WaveshareHodiny.ino @
// 9537a76932fc9269b2a22a5fb90a62785897c680, lines 855-959.
bool fetchHomeAssistantStates(NetworkClient& client, const ClockConfig& config,
                              ClockValues& values, bool& transportFailure) {
  networkDiagnosticsBegin(NetworkDiagnosticKind::HomeAssistantRuntime);
  HTTPClient http;
  http.setConnectTimeout(HOME_ASSISTANT_CONNECT_TIMEOUT_MS);
  http.setTimeout(HOME_ASSISTANT_RESPONSE_TIMEOUT_MS);
  http.setReuse(true);
  http.setAuthorizationType("Bearer");
  http.setAuthorization(config.homeAssistantToken);
  bool clientStarted = false;
  const char* entityIds[] = {
      config.weatherEntityId,
      config.leftSide.temperatureEntityId,
      config.rightSide.temperatureEntityId,
      config.metricA.entityId,
      config.metricB.entityId,
      config.sunEntityId,
      config.dayNightLightEntityId,
  };
  values.sunStateAvailable = false;
  values.dayNightLightStateAvailable = false;
  uint8_t configuredCount = 0;
  uint8_t successfulCount = 0;
  int lastStatus = 0;
  transportFailure = false;
  for (size_t index = 0; index < 7; ++index) {
    if (entityIds[index][0] != '\0') ++configuredCount;
  }
  for (size_t index = 0; index < 7; ++index) {
    if (entityIds[index][0] == '\0') continue;
    String payload;
    if (!requestHomeAssistantState(http, client, config, entityIds[index],
                                   payload, lastStatus, clientStarted,
                                   transportFailure)) {
      if (transportFailure) break;
      continue;
    }
    String state;
    if (!extractJsonStringField(payload, "state", state)) continue;
    bool applied = false;
    if (index == 5) {
      applied = applySunState(config, payload, state, values);
    } else {
      applied = applyHomeAssistantState(config, entityIds[index], state, values);
    }
    if (applied) ++successfulCount;
  }
  const bool apiResponded = successfulCount > 0 && !transportFailure;
  String detail = String(successfulCount) + '/' + configuredCount +
                  F(" entit načteno");
  if (transportFailure) detail += F(", transportní batch přerušen");
  if (!apiResponded && lastStatus != 0) {
    detail += F(", poslední výsledek ");
    detail += lastStatus;
  }
  networkDiagnosticsSetDetail(NetworkDiagnosticKind::HomeAssistantRuntime,
                              detail);
  networkDiagnosticsEnd(NetworkDiagnosticKind::HomeAssistantRuntime,
                        apiResponded,
                        apiResponded ? HTTP_CODE_OK : lastStatus);
  http.end();
  return apiResponded;
}

bool fetchHomeAssistantStates(const ClockConfig& config, ClockValues& values,
                              bool& transportFailure) {
  if (config.homeAssistantUrl[0] == '\0' ||
      config.homeAssistantToken[0] == '\0') {
    transportFailure = false;
    return false;
  }
  if (String(config.homeAssistantUrl).startsWith("https://")) {
    WiFiClientSecure client;
    client.setInsecure();
    return fetchHomeAssistantStates(client, config, values, transportFailure);
  }
  WiFiClient client;
  return fetchHomeAssistantStates(client, config, values, transportFailure);
}

bool fetchDayNightStates(NetworkClient& client, const ClockConfig& config,
                         ClockValues& values, bool& transportFailure) {
  HTTPClient http;
  http.setConnectTimeout(HOME_ASSISTANT_CONNECT_TIMEOUT_MS);
  http.setTimeout(HOME_ASSISTANT_RESPONSE_TIMEOUT_MS);
  http.setReuse(true);
  http.setAuthorizationType("Bearer");
  http.setAuthorization(config.homeAssistantToken);
  bool clientStarted = false;
  values.sunStateAvailable = false;
  values.dayNightLightStateAvailable = false;
  bool sunUpdated = false;
  bool lightUpdated = false;
  String payload;
  int status = 0;
  transportFailure = false;
  if (requestHomeAssistantState(http, client, config, config.sunEntityId,
                                payload, status, clientStarted,
                                transportFailure)) {
    String state;
    if (extractJsonStringField(payload, "state", state)) {
      sunUpdated = applySunState(config, payload, state, values);
    }
  }
  if (transportFailure) {
    http.end();
    return sunUpdated;
  }
  payload = "";
  if (requestHomeAssistantState(http, client, config,
                                config.dayNightLightEntityId, payload, status,
                                clientStarted, transportFailure)) {
    String state;
    if (extractJsonStringField(payload, "state", state)) {
      lightUpdated = applyHomeAssistantState(
          config, config.dayNightLightEntityId, state, values);
    }
  }
  http.end();
  return sunUpdated || lightUpdated;
}

bool fetchDayNightStates(const ClockConfig& config, ClockValues& values) {
  if (config.homeAssistantUrl[0] == '\0' ||
      config.homeAssistantToken[0] == '\0') {
    return false;
  }
  if (String(config.homeAssistantUrl).startsWith("https://")) {
    WiFiClientSecure client;
    client.setInsecure();
    bool transportFailure = false;
    return fetchDayNightStates(client, config, values, transportFailure);
  }
  WiFiClient client;
  bool transportFailure = false;
  return fetchDayNightStates(client, config, values, transportFailure);
}

// Upstream extraction: WaveshareHodiny.ino @
// 9537a76932fc9269b2a22a5fb90a62785897c680, lines 962-992.
bool stateAsFloat(const String& state, float& value) {
  char* end = nullptr;
  value = strtof(state.c_str(), &end);
  return end != state.c_str() && *end == '\0' && std::isfinite(value);
}

int weatherCodeForState(const String& state) {
  char* end = nullptr;
  const long numericCode = strtol(state.c_str(), &end, 10);
  if (end != state.c_str() && *end == '\0') return numericCode;
  if (state == "sunny" || state == "clear-night") return 800;
  if (state == "partlycloudy") return 801;
  if (state == "cloudy") return 804;
  if (state == "fog") return 741;
  if (state == "rainy") return 500;
  if (state == "pouring") return 502;
  if (state == "lightning") return 200;
  if (state == "lightning-rainy") return 202;
  if (state == "exceptional") return 200;
  if (state == "snowy") return 600;
  if (state == "snowy-rainy") return 616;
  if (state == "hail") return 511;
  if (state == "windy" || state == "windy-variant") return 771;
  return -1;
}

// The source extraction needs the same string parser used by the upstream
// Home Assistant code. It is kept here verbatim from lines 556-580.
// Upstream revision: 9537a76932fc9269b2a22a5fb90a62785897c680.
bool extractJsonStringField(const String& payload, const char* key,
                            String& value) {
  const String quotedKey = String('"') + key + '"';
  const int keyPosition = payload.indexOf(quotedKey);
  if (keyPosition < 0) return false;
  const int colonPosition = payload.indexOf(':', keyPosition + quotedKey.length());
  const int openingQuote = payload.indexOf('"', colonPosition + 1);
  if (colonPosition < 0 || openingQuote < 0) return false;
  value = "";
  bool escaped = false;
  for (int index = openingQuote + 1; index < payload.length(); ++index) {
    const char character = payload[index];
    if (escaped) {
      value += character;
      escaped = false;
    } else if (character == '\\') {
      escaped = true;
    } else if (character == '"') {
      return true;
    } else {
      value += character;
    }
  }
  return false;
}

}  // namespace

bool ClockDataService::begin(const ClockConfig& config) {
  if (taskHandle_ != nullptr) return false;
  tmepServiceBegin();

  configMutex_ = xSemaphoreCreateMutex();
  valuesQueue_ = xQueueCreate(1, sizeof(ClockValues));
  if (configMutex_ == nullptr || valuesQueue_ == nullptr) {
    if (valuesQueue_ != nullptr) {
      vQueueDelete(valuesQueue_);
      valuesQueue_ = nullptr;
    }
    if (configMutex_ != nullptr) {
      vSemaphoreDelete(configMutex_);
      configMutex_ = nullptr;
    }
    return false;
  }

  config_ = config;
  const BaseType_t created = xTaskCreatePinnedToCore(
      &ClockDataService::taskEntry, "clock-data", 12288, this, 1,
      &taskHandle_, 0);
  if (created != pdPASS) {
    vQueueDelete(valuesQueue_);
    vSemaphoreDelete(configMutex_);
    valuesQueue_ = nullptr;
    configMutex_ = nullptr;
    taskHandle_ = nullptr;
    return false;
  }
  return true;
}

bool ClockDataService::applyConfig(const ClockConfig& config) {
  if (configMutex_ == nullptr) return false;
  xSemaphoreTake(configMutex_, portMAX_DELAY);
  config_ = config;
  xSemaphoreGive(configMutex_);
  if (taskHandle_ != nullptr) xTaskNotifyGive(taskHandle_);
  return true;
}

bool ClockDataService::requestRefresh() {
  if (taskHandle_ == nullptr) return false;
  xTaskNotifyGive(taskHandle_);
  return true;
}

bool ClockDataService::requestDayNightRefresh() {
  if (taskHandle_ == nullptr) return false;
  portENTER_CRITICAL(&refreshMux_);
  dayNightLightRefreshRequested_ = true;
  portEXIT_CRITICAL(&refreshMux_);
  xTaskNotifyGive(taskHandle_);
  return true;
}

bool ClockDataService::consumeValues(ClockValues& values) {
  return valuesQueue_ != nullptr &&
         xQueueReceive(valuesQueue_, &values, 0) == pdTRUE;
}

void ClockDataService::taskEntry(void* argument) {
  static_cast<ClockDataService*>(argument)->workerLoop();
  vTaskDelete(nullptr);
}

ClockConfig ClockDataService::configSnapshot() const {
  ClockConfig config;
  if (configMutex_ == nullptr) return config_;
  xSemaphoreTake(configMutex_, portMAX_DELAY);
  config = config_;
  xSemaphoreGive(configMutex_);
  return config;
}

void ClockDataService::publishValues(const ClockValues& values) {
  if (valuesQueue_ != nullptr) xQueueOverwrite(valuesQueue_, &values);
}

bool ClockDataService::consumeDayNightLightRefreshRequest() {
  portENTER_CRITICAL(&refreshMux_);
  const bool requested = dayNightLightRefreshRequested_;
  dayNightLightRefreshRequested_ = false;
  portEXIT_CRITICAL(&refreshMux_);
  return requested;
}

// Upstream extraction: WaveshareHodiny.ino @
// 9537a76932fc9269b2a22a5fb90a62785897c680, lines 995-1053.
void ClockDataService::workerLoop() {
  ClockValues lastAvailableValues;
  uint32_t nextOpenMeteoRefreshAt = 0;
  uint32_t nextTmepRefreshAt = 0;
  bool tmepCatalogPrimed = false;
  // Keep repeated allocation failures quiet without delaying a user-fixed
  // configuration for more than one normal refresh interval.
  app_core::HomeAssistantBatchPolicy homeAssistantPolicy(5000, 60000);
  for (;;) {
    const ClockConfig config = configSnapshot();
    ClockValues values = lastAvailableValues;
    if (WiFi.status() != WL_CONNECTED) {
      nextOpenMeteoRefreshAt = nextTmepRefreshAt = 0;
      tmepCatalogPrimed = false;
      publishValues(ClockValues{});
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HOME_ASSISTANT_RETRY_MS));
      continue;
    }

    if (config.dataSource == CLOCK_DATA_SOURCE_OPEN_METEO) {
      homeAssistantPolicy.reset();
      const auto due = [](uint32_t now, uint32_t deadline) {
        return deadline == 0 || static_cast<int32_t>(now - deadline) >= 0;
      };
      bool valuesUpdated = false;
      if (due(millis(), nextOpenMeteoRefreshAt)) {
        bool responded = false;
        {
          const network_host::FetchLease lease(NETWORK_FETCH_GATE_TIMEOUT_MS);
          responded = lease && fetchOpenMeteo(config, values);
        }  // Release before TMEP and, importantly, before sleeping.
        nextOpenMeteoRefreshAt = millis() +
            (responded ? OPEN_METEO_REFRESH_MS : TMEP_REFRESH_MS);
        valuesUpdated = true;
      }
      bool tmepEnabled = false;
      for (const auto& slot : config.tmepSlots) tmepEnabled |= slot.enabled;
      const bool tmepConfigured = config.tmepExportId[0] != '\0' &&
                                  config.tmepExportKey[0] != '\0';
      if (tmepConfigured && (tmepEnabled || !tmepCatalogPrimed) &&
          due(millis(), nextTmepRefreshAt)) {
        tmepCatalogPrimed = fetchTmepValues(config, values);
        nextTmepRefreshAt = millis() + TMEP_REFRESH_MS;
        valuesUpdated |= tmepEnabled;
      }
      if (!tmepConfigured) {
        nextTmepRefreshAt = 0;
        tmepCatalogPrimed = false;
        float* slots[] = {&values.leftTemperatureC, &values.rightTemperatureC,
                          &values.metricAValue, &values.metricBValue};
        for (size_t i = 0; i < 4; ++i) {
          if (config.tmepSlots[i].enabled) *slots[i] = NAN;
        }
      }
      if (valuesUpdated) {
        lastAvailableValues = values;
        publishValues(values);
      }
      const uint32_t now = millis();
      uint32_t waitMs = due(now, nextOpenMeteoRefreshAt)
                            ? 0 : nextOpenMeteoRefreshAt - now;
      if (tmepConfigured && (tmepEnabled || !tmepCatalogPrimed)) {
        const uint32_t tmepWait = due(now, nextTmepRefreshAt)
                                      ? 0 : nextTmepRefreshAt - now;
        waitMs = min(waitMs, tmepWait);
      }
      if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs)) > 0) {
        nextOpenMeteoRefreshAt = nextTmepRefreshAt = 0;
        tmepCatalogPrimed = false;
        // Configuration/source changes must not retain another sensor's value
        // when the newly selected source is unavailable.
        lastAvailableValues = ClockValues{};
      }
      continue;
    }

    nextOpenMeteoRefreshAt = nextTmepRefreshAt = 0;
    tmepCatalogPrimed = false;

    if (config.homeAssistantUrl[0] == '\0' ||
        config.homeAssistantToken[0] == '\0') {
      homeAssistantPolicy.reset();
      publishValues(ClockValues{});
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HOME_ASSISTANT_RETRY_MS));
      continue;
    }

    const uint32_t nowMs = millis();
    if (!homeAssistantPolicy.canStart(nowMs)) {
      ulTaskNotifyTake(
          pdTRUE,
          pdMS_TO_TICKS(homeAssistantPolicy.remainingDelayMs(nowMs)));
      continue;
    }

    bool apiResponded = false;
    bool transportFailure = false;
    {
      const network_host::FetchLease fetchLease(
          NETWORK_FETCH_GATE_TIMEOUT_MS);
      apiResponded = fetchLease &&
                     fetchHomeAssistantStates(config, values, transportFailure);
    }
    homeAssistantPolicy.recordBatchResult(
        transportFailure
            ? app_core::HomeAssistantBatchResult::TransportFailure
            : (apiResponded ? app_core::HomeAssistantBatchResult::Success
                            : app_core::HomeAssistantBatchResult::HttpApplicationError),
        millis());
    if (apiResponded) {
      values.homeAssistantOnline = true;
      lastAvailableValues = values;
    } else {
      // Keep the last complete snapshot visible while a transient transport
      // failure is being retried.  Only the online indicator changes.
      values = lastAvailableValues;
      values.homeAssistantOnline = false;
    }
    publishValues(values);
    if (!apiResponded) {
      if (!transportFailure) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HOME_ASSISTANT_RETRY_MS));
      }
      continue;
    }

    const unsigned long fullRefreshAt = millis() + HOME_ASSISTANT_REFRESH_MS;
    while (static_cast<long>(millis() - fullRefreshAt) < 0) {
      const unsigned long remaining = fullRefreshAt - millis();
      if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(remaining)) == 0) break;
      if (!consumeDayNightLightRefreshRequest()) break;

      static ClockConfig lightConfig;
      static ClockValues lightValues;
      lightConfig = configSnapshot();
      lightValues = lastAvailableValues;
      const network_host::FetchLease lightFetchLease(
          NETWORK_FETCH_GATE_TIMEOUT_MS);
      if (lightFetchLease) {
        fetchDayNightStates(lightConfig, lightValues);
      }
      lastAvailableValues.weatherIsDay = lightValues.weatherIsDay;
      lastAvailableValues.sunStateAvailable = lightValues.sunStateAvailable;
      lastAvailableValues.dayNightLightStateAvailable =
          lightValues.dayNightLightStateAvailable;
      lastAvailableValues.dayNightLightOn = lightValues.dayNightLightOn;
      publishValues(lightValues);
    }
  }
}

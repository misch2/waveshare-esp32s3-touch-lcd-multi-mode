#include "AppConfig.h"
#include "BuildProvenance.h"
#include "ClockWeatherAnimationPolicy.h"
#include "CombinedWebRoutes.h"
#include "ConfigurationWebRoutes.h"
#include "DayNightLogic.h"
#include "HomeAssistantBatchPolicy.h"
#include "HomeAssistantConnectionPolicy.h"
#include "GestureRecognizer.h"
#include "MeteoRadarConfig.h"
#include "MeteoOutsideTemperaturePolicy.h"
#include "NavigationIndicatorModel.h"
#include "MeteoWebRoutes.h"
#include "ScreenManager.h"
#include "WeatherIconMapping.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

int failures = 0;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                       \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                   #condition);                                             \
      ++failures;                                                            \
    }                                                                        \
  } while (false)

#define CHECK_EQ(actual, expected)                                           \
  do {                                                                       \
    const auto actualValue = (actual);                                       \
    const auto expectedValue = (expected);                                   \
    if (!(actualValue == expectedValue)) {                                   \
      std::fprintf(stderr, "FAIL %s:%d: %s == %s (actual=%lld expected=%lld)\n", \
                   __FILE__, __LINE__, #actual, #expected,                  \
                   static_cast<long long>(actualValue),                    \
                   static_cast<long long>(expectedValue));                  \
      ++failures;                                                            \
    }                                                                        \
  } while (false)

#define CHECK_STREQ(actual, expected)                                        \
  do {                                                                       \
    const char* actualValue = (actual);                                      \
    const char* expectedValue = (expected);                                  \
    if (actualValue == nullptr || expectedValue == nullptr ||                 \
        std::strcmp(actualValue, expectedValue) != 0) {                       \
      std::fprintf(stderr, "FAIL %s:%d: %s == %s (actual=%s expected=%s)\n", \
                   __FILE__, __LINE__, #actual, #expected,                  \
                   actualValue == nullptr ? "<null>" : actualValue,        \
                   expectedValue == nullptr ? "<null>" : expectedValue);    \
      ++failures;                                                            \
    }                                                                        \
  } while (false)

using app_core::AppConfig;

void testBuildProvenance() {
  CHECK_EQ(app_core::kComponentProvenanceCount, 2u);
  const app_core::ComponentProvenance& meteo =
      app_core::kComponentProvenance[0];
  CHECK_STREQ(meteo.id, "meteo-plane-radar");
  CHECK_STREQ(meteo.upstreamRef, "main");
  CHECK_STREQ(meteo.upstreamTag, "v0.6.3");
  CHECK(std::strlen(meteo.upstreamUrl) > 0);
  CHECK(std::strlen(meteo.upstreamBase) == 40);
  CHECK(std::strlen(meteo.forkUrl) > 0);
  CHECK(std::strlen(meteo.forkPin) == 40);

  const app_core::ComponentProvenance& clock =
      app_core::kComponentProvenance[1];
  CHECK_STREQ(clock.id, "waveshare-hodiny");
  CHECK_STREQ(clock.upstreamRef, "main");
  CHECK_STREQ(clock.upstreamTag, "v1.5.5");
  CHECK(std::strlen(clock.upstreamUrl) > 0);
  CHECK(std::strlen(clock.upstreamBase) == 40);
  CHECK(std::strlen(clock.forkUrl) > 0);
  CHECK(std::strlen(clock.forkPin) == 40);

  // A component which has no exact upstream tag keeps the optional field null;
  // callers must continue to use the immutable commit as its identity.
  app_core::ComponentProvenance untagged{};
  CHECK(untagged.upstreamTag == nullptr);
}

void testAppConfigDefaults() {
  const AppConfig config = AppConfig::defaults();

  CHECK(config.validate());
  CHECK_EQ(config.schemaVersion, AppConfig::kSchemaVersion);
  CHECK_EQ(config.screenCount, 4);
  CHECK_STREQ(config.screens[0].id, "clock.dashboard");
  CHECK_STREQ(config.screens[1].id, "meteo.radar");
  CHECK_STREQ(config.screens[2].id, "meteo.forecast");
  CHECK_STREQ(config.screens[3].id, "meteo.planes");
  CHECK(config.isEnabled("clock.dashboard"));
  CHECK(config.isEnabled("meteo.radar"));
  CHECK(config.isEnabled("meteo.forecast"));
  CHECK(config.isEnabled("meteo.planes"));
}

void testAppConfigNormalizesAndPreservesOrder() {
  AppConfig config{};
  config.schemaVersion = 99;
  config.screenCount = 6;

  std::strcpy(config.screens[0].id, "clock.dashboard");
  config.screens[0].enabled = 1;
  std::strcpy(config.screens[1].id, "extra.screen");
  config.screens[1].enabled = 0;
  std::strcpy(config.screens[2].id, "clock.dashboard");  // duplicate
  config.screens[2].enabled = 0;
  std::strcpy(config.screens[3].id, "bad id");            // invalid ID
  config.screens[3].enabled = 1;
  std::strcpy(config.screens[4].id, "meteo.radar");
  config.screens[4].enabled = 3;  // normalized to a boolean
  config.screens[5].id[0] = '\0';  // invalid/empty ID

  CHECK(config.normalize());
  CHECK(config.validate());
  CHECK_EQ(config.schemaVersion, AppConfig::kSchemaVersion);
  CHECK_EQ(config.screenCount, 5);
  CHECK_STREQ(config.screens[0].id, "clock.dashboard");
  CHECK_STREQ(config.screens[1].id, "extra.screen");
  CHECK_STREQ(config.screens[2].id, "meteo.radar");
  CHECK_STREQ(config.screens[3].id, "meteo.forecast");
  CHECK_STREQ(config.screens[4].id, "meteo.planes");
  CHECK_EQ(config.screens[0].enabled, 1);
  CHECK_EQ(config.screens[1].enabled, 0);
  CHECK_EQ(config.screens[2].enabled, 1);
  CHECK_EQ(config.screens[3].enabled, 1);
}

void testAppConfigFallbackAndEditing() {
  AppConfig config{};
  config.screenCount = 3;
  std::strcpy(config.screens[0].id, "not valid");
  std::strcpy(config.screens[1].id, "also not-valid");
  config.screens[2].id[0] = '\0';

  CHECK(config.normalize());
  CHECK(config.validate());
  CHECK_EQ(config.screenCount, 4);
  CHECK_STREQ(config.screens[0].id, "clock.dashboard");
  CHECK_STREQ(config.screens[1].id, "meteo.radar");
  CHECK_STREQ(config.screens[2].id, "meteo.forecast");
  CHECK_STREQ(config.screens[3].id, "meteo.planes");

  CHECK(config.setEnabled("meteo.radar", false));
  CHECK(!config.isEnabled("meteo.radar"));
  CHECK(!config.setEnabled("missing.screen", true));
  CHECK_EQ(config.findScreen("missing.screen"), -1);

  CHECK(config.moveScreen("meteo.radar", 0));
  CHECK_STREQ(config.screens[0].id, "meteo.radar");
  CHECK_STREQ(config.screens[1].id, "clock.dashboard");
  CHECK(!config.moveScreen("missing.screen", 0));
  CHECK(!config.moveScreen("clock.dashboard", config.screenCount));
}

void testAppConfigKeepsOneScreenReachable() {
  AppConfig config = AppConfig::defaults();
  config.screens[0].enabled = 0;
  config.screens[1].enabled = 0;
  config.screens[2].enabled = 0;
  config.screens[3].enabled = 0;
  CHECK(!config.validate());
  CHECK(config.normalize());
  CHECK(config.validate());
  CHECK(config.isEnabled("clock.dashboard"));

  AppConfig empty{};
  empty.schemaVersion = AppConfig::kSchemaVersion;
  CHECK(!empty.validate());
}

void testAppConfigNormalizationRetainsDisabledPlanes() {
  AppConfig config = AppConfig::defaults();
  CHECK(config.setEnabled("meteo.planes", false));
  CHECK(!config.normalize());
  CHECK(config.validate());
  CHECK_EQ(config.findScreen("meteo.planes"), 3);
  CHECK(!config.isEnabled("meteo.planes"));
}

void testNavigationIndicatorUsesConfiguredVisibleStableIds() {
  AppConfig config = AppConfig::defaults();
  config.screenCount = 6;
  std::strcpy(config.screens[0].id, "meteo.forecast");
  config.screens[0].enabled = 1;
  std::strcpy(config.screens[1].id, "future.screen");
  config.screens[1].enabled = 1;
  std::strcpy(config.screens[2].id, "meteo.radar");
  config.screens[2].enabled = 0;
  std::strcpy(config.screens[3].id, "clock.dashboard");
  config.screens[3].enabled = 1;
  std::strcpy(config.screens[4].id, "meteo.planes");
  config.screens[4].enabled = 1;
  std::strcpy(config.screens[5].id, "future.disabled");
  config.screens[5].enabled = 0;
  CHECK(config.validate());

  // Registration order is deliberately different from the persisted order.
  // The indicator must follow configured order, include only enabled modules,
  // and omit syntactically valid IDs for modules absent from this firmware.
  const char* registeredIds[] = {"clock.dashboard", "meteo.radar",
                                "meteo.forecast", "meteo.planes"};
  app_core::NavigationIndicatorModel indicator;
  indicator.refresh(config, registeredIds, 4, "clock.dashboard");

  CHECK_EQ(indicator.count(), 3u);
  CHECK_STREQ(indicator.idAt(0), "meteo.forecast");
  CHECK_STREQ(indicator.idAt(1), "clock.dashboard");
  CHECK_STREQ(indicator.idAt(2), "meteo.planes");
  CHECK(indicator.idAt(3) == nullptr);

  // Selection is resolved by the stable ID, not by registration or numeric
  // screen index.
  CHECK_EQ(indicator.activeIndex(), 1u);
  CHECK_STREQ(indicator.activeId(), "clock.dashboard");
  CHECK(!indicator.isActive(0));
  CHECK(indicator.isActive(1));
  CHECK(!indicator.isActive(2));

  indicator.refresh(config, registeredIds, 4, "meteo.forecast");
  CHECK_EQ(indicator.activeIndex(), 0u);
  CHECK_STREQ(indicator.activeId(), "meteo.forecast");
  CHECK(indicator.isActive(0));
  CHECK(!indicator.isActive(1));

  indicator.refresh(config, registeredIds, 4, "future.screen");
  CHECK_EQ(indicator.activeIndex(),
           app_core::NavigationIndicatorModel::kNoSelection);
  CHECK(indicator.activeId() == nullptr);
  CHECK(!indicator.isActive(0));
}

void testDayNightTransitionTimestampToleranceAndFallback() {
  int64_t selected = -1;
  constexpr int64_t expected = 1'700'001'000;

  CHECK(clockSelectCompletedTransitionTimestamp(
      false, 0, true, expected, selected));
  CHECK_EQ(selected, expected);

  // A Home Assistant last_changed value on either side of the expected
  // transition is trusted while it is within the documented five-minute
  // tolerance.
  CHECK(clockSelectCompletedTransitionTimestamp(
      true, expected - 5 * 60, true, expected, selected));
  CHECK_EQ(selected, expected - 5 * 60);
  CHECK(clockSelectCompletedTransitionTimestamp(
      true, expected + 5 * 60, true, expected, selected));
  CHECK_EQ(selected, expected + 5 * 60);

  // When the observed value drifts farther away, use the forecast timestamp
  // instead of propagating a stale entity update.
  CHECK(clockSelectCompletedTransitionTimestamp(
      true, expected - 5 * 60 - 1, true, expected, selected));
  CHECK_EQ(selected, expected);
  CHECK(clockSelectCompletedTransitionTimestamp(
      true, expected + 5 * 60 + 1, true, expected, selected));
  CHECK_EQ(selected, expected);

  selected = 123;
  CHECK(!clockSelectCompletedTransitionTimestamp(
      true, expected, false, 0, selected));
  CHECK_EQ(selected, 123);
}

void testDayNightOffsetsAndTransitions() {
  constexpr int64_t sunrise = 1'700'001'000;
  constexpr int64_t sunset = 1'700'004'600;
  constexpr int64_t validNow = 1'700'005'000;

  // With no offset, the current horizon state is used directly and does not
  // require a valid wall clock or transition timestamps.
  CHECK(clockEvaluateSunDecision(true, 0, 0, 0, false, 0, false, 0) ==
        ClockSunDecision::Day);
  CHECK(clockEvaluateSunDecision(false, 0, 0, 0, false, 0, false, 0) ==
        ClockSunDecision::Night);

  // Positive sunrise offset delays the day transition.
  CHECK(clockEvaluateSunDecision(true, 15, 0, sunrise + 14 * 60,
                                 true, sunrise, false, 0) ==
        ClockSunDecision::Night);
  CHECK(clockEvaluateSunDecision(true, 15, 0, sunrise + 15 * 60,
                                 true, sunrise, false, 0) ==
        ClockSunDecision::Day);

  // Positive sunset offset delays the night transition.
  CHECK(clockEvaluateSunDecision(false, 0, 15, sunset + 14 * 60,
                                 true, sunset, false, 0) ==
        ClockSunDecision::Day);
  CHECK(clockEvaluateSunDecision(false, 0, 15, sunset + 15 * 60,
                                 true, sunset, false, 0) ==
        ClockSunDecision::Night);

  // Negative sunset offset advances night while the horizon still reports
  // day; negative sunrise offset advances day while it reports night.
  CHECK(clockEvaluateSunDecision(true, 0, -15, sunset - 15 * 60 - 1,
                                 false, 0, true, sunset) ==
        ClockSunDecision::Day);
  CHECK(clockEvaluateSunDecision(true, 0, -15, sunset - 15 * 60,
                                 false, 0, true, sunset) ==
        ClockSunDecision::Night);
  CHECK(clockEvaluateSunDecision(false, -15, 0, sunrise - 15 * 60 - 1,
                                 false, 0, true, sunrise) ==
        ClockSunDecision::Night);
  CHECK(clockEvaluateSunDecision(false, -15, 0, sunrise - 15 * 60,
                                 false, 0, true, sunrise) ==
        ClockSunDecision::Day);

  // Positive/negative offsets require a valid current timestamp. Missing
  // required transition data is unavailable as well.
  CHECK(clockEvaluateSunDecision(true, 15, 0, validNow - 1'000'000'000,
                                 true, sunrise, false, 0) ==
        ClockSunDecision::Unavailable);
  CHECK(clockEvaluateSunDecision(true, 15, 0, validNow, false, 0, false, 0) ==
        ClockSunDecision::Unavailable);
  CHECK(clockEvaluateSunDecision(true, 0, -15, validNow - 1'000'000'000,
                                 false, 0, true, sunset) ==
        ClockSunDecision::Unavailable);
}

void testHomeAssistantStoredTokenReusePolicy() {
  constexpr const char *storedUrl = "http://ha.example:8123";

  CHECK(homeAssistantMayReuseStoredToken("", storedUrl));
  CHECK(homeAssistantMayReuseStoredToken(storedUrl, storedUrl));
  CHECK(homeAssistantMayReuseStoredToken("", ""));

  // URL identity is deliberately exact: changing scheme, host, or port must
  // not allow a token stored for another Home Assistant endpoint to leak into
  // the new save request.
  CHECK(!homeAssistantMayReuseStoredToken("https://ha.example:8123",
                                          storedUrl));
  CHECK(!homeAssistantMayReuseStoredToken("http://other.example:8123",
                                          storedUrl));
  CHECK(!homeAssistantMayReuseStoredToken("http://ha.example:8124",
                                          storedUrl));
  CHECK(!homeAssistantMayReuseStoredToken("http://ha.example:8123/api",
                                          storedUrl));

  // A null URL is not an empty URL and cannot authorize reuse.
  CHECK(!homeAssistantMayReuseStoredToken(nullptr, storedUrl));
  CHECK(!homeAssistantMayReuseStoredToken(storedUrl, nullptr));
  CHECK(!homeAssistantMayReuseStoredToken(nullptr, nullptr));
}

void testHomeAssistantBatchPolicy() {
  using app_core::HomeAssistantBatchPolicy;
  using app_core::HomeAssistantBatchResult;

  CHECK_EQ(static_cast<int>(HomeAssistantBatchPolicy::classifyStatus(200)),
           static_cast<int>(HomeAssistantBatchResult::Success));
  CHECK_EQ(static_cast<int>(HomeAssistantBatchPolicy::classifyStatus(401)),
           static_cast<int>(HomeAssistantBatchResult::HttpApplicationError));
  CHECK_EQ(static_cast<int>(HomeAssistantBatchPolicy::classifyStatus(500)),
           static_cast<int>(HomeAssistantBatchResult::HttpApplicationError));
  CHECK_EQ(static_cast<int>(HomeAssistantBatchPolicy::classifyStatus(0)),
           static_cast<int>(HomeAssistantBatchResult::TransportFailure));
  CHECK_EQ(static_cast<int>(HomeAssistantBatchPolicy::classifyStatus(-24960)),
           static_cast<int>(HomeAssistantBatchResult::TransportFailure));

  const auto applicationError = HomeAssistantBatchPolicy::decide(401);
  CHECK(applicationError.continueBatch);
  CHECK(!applicationError.retryRequest);

  const auto transientServerError = HomeAssistantBatchPolicy::decide(500);
  CHECK(transientServerError.continueBatch);
  CHECK(transientServerError.retryRequest);
  CHECK(HomeAssistantBatchPolicy::decide(408).retryRequest);
  CHECK(HomeAssistantBatchPolicy::decide(429).retryRequest);

  const auto transportFailure = HomeAssistantBatchPolicy::decide(-24960);
  CHECK(!transportFailure.continueBatch);
  CHECK(!transportFailure.retryRequest);

  HomeAssistantBatchPolicy policy(1000, 4000);
  CHECK(policy.canStart(0));
  policy.recordBatchResult(HomeAssistantBatchResult::TransportFailure, 100);
  CHECK_EQ(policy.consecutiveTransportFailures(), 1);
  CHECK_EQ(policy.remainingDelayMs(100), 1000);
  CHECK(!policy.canStart(1099));
  CHECK(policy.canStart(1100));

  policy.recordBatchResult(HomeAssistantBatchResult::TransportFailure, 1100);
  CHECK_EQ(policy.consecutiveTransportFailures(), 2);
  CHECK_EQ(policy.remainingDelayMs(1100), 2000);
  policy.recordBatchResult(HomeAssistantBatchResult::TransportFailure, 3100);
  CHECK_EQ(policy.consecutiveTransportFailures(), 3);
  CHECK_EQ(policy.remainingDelayMs(3100), 4000);
  policy.recordBatchResult(HomeAssistantBatchResult::TransportFailure, 7100);
  CHECK_EQ(policy.remainingDelayMs(7100), 4000);  // capped

  // Any completed HTTP exchange means the transport is reachable again.
  policy.recordBatchResult(HomeAssistantBatchResult::HttpApplicationError,
                           7100);
  CHECK_EQ(policy.consecutiveTransportFailures(), 0);
  CHECK(policy.canStart(7100));

  // The deadline is intentionally wrap-safe for uint32_t millis().
  policy.recordBatchResult(HomeAssistantBatchResult::TransportFailure,
                           std::numeric_limits<std::uint32_t>::max() - 100);
  CHECK_EQ(policy.remainingDelayMs(
               std::numeric_limits<std::uint32_t>::max() - 100),
           1000);
  CHECK(!policy.canStart(500));
  CHECK(policy.canStart(900));

  policy.recordBatchResult(HomeAssistantBatchResult::Success, 900);
  CHECK_EQ(policy.consecutiveTransportFailures(), 0);
  CHECK_EQ(policy.remainingDelayMs(900), 0);
}

void testMeteoOutsideTemperaturePolicy() {
  using app_core::MeteoOutsideTemperaturePolicy;

  // A connected host must be able to obtain the first value immediately.  If
  // that request fails, keep retrying on the short no-data cadence.
  MeteoOutsideTemperaturePolicy retryPolicy;
  CHECK(retryPolicy.shouldFetch(0));
  retryPolicy.noteAttempt(0);
  CHECK(!retryPolicy.shouldFetch(
      MeteoOutsideTemperaturePolicy::kRetryIntervalMs - 1));
  CHECK(retryPolicy.shouldFetch(
      MeteoOutsideTemperaturePolicy::kRetryIntervalMs));

  // Once a standalone value exists, ordinary refreshes use the longer model
  // interval.  The unsigned subtraction also keeps the schedule valid over a
  // millis() wrap.
  MeteoOutsideTemperaturePolicy refreshPolicy;
  const std::uint32_t beforeWrap = std::numeric_limits<std::uint32_t>::max() -
                                   100;
  CHECK(refreshPolicy.shouldFetch(beforeWrap));
  refreshPolicy.noteAttempt(beforeWrap);
  refreshPolicy.noteStandaloneTemperature();
  CHECK(!refreshPolicy.shouldFetch(
      beforeWrap + MeteoOutsideTemperaturePolicy::kRefreshIntervalMs - 1));
  CHECK(refreshPolicy.shouldFetch(
      beforeWrap + MeteoOutsideTemperaturePolicy::kRefreshIntervalMs));

  // Forecast owns the current-temperature fetch.  Its value is immediately
  // shared with radar/planes and suppresses duplicate standalone requests for
  // the documented 30-minute forecast period plus five-minute slack.
  MeteoOutsideTemperaturePolicy forecastPolicy;
  constexpr std::uint32_t forecastAt = 1000;
  forecastPolicy.noteForecastTemperature(forecastAt);
  CHECK(forecastPolicy.hasTemperature());
  CHECK(!forecastPolicy.shouldFetch(forecastAt));
  CHECK(!forecastPolicy.shouldFetch(
      forecastAt + MeteoOutsideTemperaturePolicy::kForecastFreshIntervalMs -
      1));
  CHECK(forecastPolicy.shouldFetch(
      forecastAt + MeteoOutsideTemperaturePolicy::kForecastFreshIntervalMs));

  // Repeated Forecast ticks refresh the same shared freshness window and must
  // not turn into one fallback request per host loop iteration.
  forecastPolicy.noteForecastTemperature(forecastAt + 1000);
  CHECK(!forecastPolicy.shouldFetch(
      forecastAt + MeteoOutsideTemperaturePolicy::kForecastFreshIntervalMs));
}

bool configurationStorageBeginForTest() { return true; }
bool configurationStorageEndForTest() { return false; }

std::size_t meteoConfigLoadForTest(char* out, std::size_t capacity) {
  constexpr char kPayload[] = "{\"source\":\"chmu\"}";
  constexpr std::size_t kPayloadLength = sizeof(kPayload) - 1;
  if (out == nullptr || capacity <= kPayloadLength) return 0;
  std::memcpy(out, kPayload, sizeof(kPayload));
  return kPayloadLength;
}

bool meteoConfigSaveForTest(const char* json, std::size_t length) {
  constexpr char kPayload[] = "{\"source\":\"chmu\"}";
  constexpr std::size_t kPayloadLength = sizeof(kPayload) - 1;
  return json != nullptr && length == kPayloadLength &&
         std::memcmp(json, kPayload, kPayloadLength) == 0;
}

std::size_t meteoStatusLoadForTest(char* out, std::size_t capacity) {
  constexpr char kPayload[] = "{\"ok\":true}";
  constexpr std::size_t kPayloadLength = sizeof(kPayload) - 1;
  if (out == nullptr || capacity <= kPayloadLength) return 0;
  std::memcpy(out, kPayload, sizeof(kPayload));
  return kPayloadLength;
}

bool meteoAccessAllowedForTest() { return true; }

bool meteoScreenCommandForTest(
    const app_core::MeteoWebScreenCommand& command) {
  return command.kind == app_core::MeteoWebScreenCommandKind::Range &&
         command.value == 1;
}

std::size_t combinedJsonLoadForTest(char* out, std::size_t capacity) {
  constexpr char kPayload[] = "{\"ok\":true}";
  constexpr std::size_t kPayloadLength = sizeof(kPayload) - 1;
  if (out == nullptr || capacity <= kPayloadLength) return 0;
  std::memcpy(out, kPayload, sizeof(kPayload));
  return kPayloadLength;
}

int combinedImportCallCount = 0;
const char* combinedImportJson = nullptr;
std::size_t combinedImportLength = 0;
std::size_t combinedImportMessageCapacity = 0;

bool combinedImportForTest(const char* json, std::size_t length,
                           char* message, std::size_t messageCapacity) {
  ++combinedImportCallCount;
  combinedImportJson = json;
  combinedImportLength = length;
  combinedImportMessageCapacity = messageCapacity;

  constexpr char kDetail[] = "restored";
  constexpr std::size_t kDetailLength = sizeof(kDetail) - 1;
  if (json == nullptr || message == nullptr ||
      messageCapacity <= kDetailLength) {
    return false;
  }
  std::memcpy(message, kDetail, sizeof(kDetail));
  return true;
}

bool combinedAccessAllowedForTest() { return true; }

int combinedFirmwareBeginCallCount = 0;
int combinedFirmwareWriteCallCount = 0;
int combinedFirmwareEndCallCount = 0;
int combinedFirmwareAbortCallCount = 0;
int combinedFirmwareRestartCallCount = 0;
const char* combinedFirmwareFilename = nullptr;
const unsigned char* combinedFirmwareData = nullptr;
std::size_t combinedFirmwareDataLength = 0;
std::size_t combinedFirmwareMessageCapacity = 0;

bool combinedFirmwareBeginForTest(const char* filename, char* message,
                                  std::size_t messageCapacity) {
  ++combinedFirmwareBeginCallCount;
  combinedFirmwareFilename = filename;
  combinedFirmwareMessageCapacity = messageCapacity;
  constexpr char kDetail[] = "upload started";
  constexpr std::size_t kDetailLength = sizeof(kDetail) - 1;
  if (filename == nullptr || message == nullptr ||
      messageCapacity <= kDetailLength) {
    return false;
  }
  std::memcpy(message, kDetail, sizeof(kDetail));
  return true;
}

bool combinedFirmwareWriteForTest(const unsigned char* data,
                                  std::size_t length, char* message,
                                  std::size_t messageCapacity) {
  ++combinedFirmwareWriteCallCount;
  combinedFirmwareData = data;
  combinedFirmwareDataLength = length;
  combinedFirmwareMessageCapacity = messageCapacity;
  constexpr char kDetail[] = "chunk written";
  constexpr std::size_t kDetailLength = sizeof(kDetail) - 1;
  if (data == nullptr || length == 0 || message == nullptr ||
      messageCapacity <= kDetailLength) {
    return false;
  }
  std::memcpy(message, kDetail, sizeof(kDetail));
  return true;
}

bool combinedFirmwareEndForTest(char* message,
                                std::size_t messageCapacity) {
  ++combinedFirmwareEndCallCount;
  combinedFirmwareMessageCapacity = messageCapacity;
  constexpr char kDetail[] = "upload complete";
  constexpr std::size_t kDetailLength = sizeof(kDetail) - 1;
  if (message == nullptr || messageCapacity <= kDetailLength) return false;
  std::memcpy(message, kDetail, sizeof(kDetail));
  return true;
}

void combinedFirmwareAbortForTest() { ++combinedFirmwareAbortCallCount; }

void combinedFirmwareRestartForTest() { ++combinedFirmwareRestartCallCount; }

void resetCombinedFirmwareTestState() {
  combinedFirmwareBeginCallCount = 0;
  combinedFirmwareWriteCallCount = 0;
  combinedFirmwareEndCallCount = 0;
  combinedFirmwareAbortCallCount = 0;
  combinedFirmwareRestartCallCount = 0;
  combinedFirmwareFilename = nullptr;
  combinedFirmwareData = nullptr;
  combinedFirmwareDataLength = 0;
  combinedFirmwareMessageCapacity = 0;
}

void testCombinedWebRoutesDefaultsAndCallbacks() {
  using app_core::CombinedWebOptions;
  using app_core::CombinedWebRoutes;

  CHECK_STREQ(app_core::COMBINED_WEB_STATUS_PATH, "/api/status");
  CHECK_STREQ(app_core::COMBINED_WEB_DIAGNOSTICS_PATH, "/api/diagnostics");
  CHECK_STREQ(app_core::COMBINED_WEB_EXPORT_PATH, "/api/config/export");
  CHECK_STREQ(app_core::COMBINED_WEB_IMPORT_PATH, "/api/config/import");
  CHECK_STREQ(app_core::COMBINED_WEB_FIRMWARE_UPLOAD_PATH,
              "/api/firmware/upload");
  CHECK_EQ(app_core::COMBINED_WEB_MAX_IMPORT_BYTES, 16384u);
  CHECK_EQ(app_core::COMBINED_WEB_MAX_FIRMWARE_BYTES, 0x600000u);

  const CombinedWebRoutes defaults;
  CHECK(defaults.loadStatus == nullptr);
  CHECK(defaults.loadDiagnostics == nullptr);
  CHECK(defaults.loadExport == nullptr);
  CHECK(defaults.validateImport == nullptr);
  CHECK(defaults.importConfig == nullptr);
  CHECK(defaults.accessAllowed == nullptr);
  CHECK(defaults.storageBegin == nullptr);
  CHECK(defaults.storageEnd == nullptr);
  CHECK(defaults.firmwareUploadBegin == nullptr);
  CHECK(defaults.firmwareUploadWrite == nullptr);
  CHECK(defaults.firmwareUploadEnd == nullptr);
  CHECK(defaults.firmwareUploadAbort == nullptr);
  CHECK(defaults.firmwareUploadRestart == nullptr);
  CHECK(!defaults.hasStatusCallback());
  CHECK(!defaults.hasDiagnosticsCallback());
  CHECK(!defaults.hasExportCallback());
  CHECK(!defaults.hasImportValidationCallback());
  CHECK(!defaults.hasImportCallback());
  CHECK(!defaults.hasStorageCallbacks());
  CHECK(!defaults.hasFirmwareUploadCallbacks());

  CombinedWebRoutes routes;
  routes.loadStatus = combinedJsonLoadForTest;
  routes.loadDiagnostics = combinedJsonLoadForTest;
  routes.loadExport = combinedJsonLoadForTest;
  routes.validateImport = combinedImportForTest;
  routes.importConfig = combinedImportForTest;
  routes.accessAllowed = combinedAccessAllowedForTest;
  routes.storageBegin = configurationStorageBeginForTest;
  routes.storageEnd = configurationStorageEndForTest;
  routes.firmwareUploadBegin = combinedFirmwareBeginForTest;
  routes.firmwareUploadWrite = combinedFirmwareWriteForTest;
  routes.firmwareUploadEnd = combinedFirmwareEndForTest;
  routes.firmwareUploadAbort = combinedFirmwareAbortForTest;
  routes.firmwareUploadRestart = combinedFirmwareRestartForTest;

  CHECK(routes.loadStatus == combinedJsonLoadForTest);
  CHECK(routes.loadDiagnostics == combinedJsonLoadForTest);
  CHECK(routes.loadExport == combinedJsonLoadForTest);
  CHECK(routes.validateImport == combinedImportForTest);
  CHECK(routes.importConfig == combinedImportForTest);
  CHECK(routes.accessAllowed == combinedAccessAllowedForTest);
  CHECK(routes.storageBegin == configurationStorageBeginForTest);
  CHECK(routes.storageEnd == configurationStorageEndForTest);
  CHECK(routes.hasStatusCallback());
  CHECK(routes.hasDiagnosticsCallback());
  CHECK(routes.hasExportCallback());
  CHECK(routes.hasImportValidationCallback());
  CHECK(routes.hasImportCallback());
  CHECK(routes.hasStorageCallbacks());
  CHECK(routes.firmwareUploadBegin == combinedFirmwareBeginForTest);
  CHECK(routes.firmwareUploadWrite == combinedFirmwareWriteForTest);
  CHECK(routes.firmwareUploadEnd == combinedFirmwareEndForTest);
  CHECK(routes.firmwareUploadAbort == combinedFirmwareAbortForTest);
  CHECK(routes.firmwareUploadRestart == combinedFirmwareRestartForTest);
  CHECK(routes.hasFirmwareUploadCallbacks());

  char output[32] = {};
  const std::size_t statusLength = routes.loadStatus(output, sizeof(output));
  CHECK_EQ(statusLength, std::strlen("{\"ok\":true}"));
  CHECK_STREQ(output, "{\"ok\":true}");
  std::memset(output, 0, sizeof(output));
  CHECK_EQ(routes.loadDiagnostics(output, sizeof(output)), statusLength);
  CHECK_STREQ(output, "{\"ok\":true}");
  std::memset(output, 0, sizeof(output));
  CHECK_EQ(routes.loadExport(output, sizeof(output)), statusLength);
  CHECK_STREQ(output, "{\"ok\":true}");
  CHECK(routes.accessAllowed());

  constexpr char kImport[] = "{\"schemaVersion\":1}";
  char detail[32] = {};
  combinedImportCallCount = 0;
  combinedImportJson = nullptr;
  combinedImportLength = 0;
  combinedImportMessageCapacity = 0;
  CHECK(routes.importConfig(kImport, sizeof(kImport) - 1, detail,
                            sizeof(detail)));
  CHECK_EQ(combinedImportCallCount, 1);
  CHECK(combinedImportJson == kImport);
  CHECK_EQ(combinedImportLength, sizeof(kImport) - 1);
  CHECK_EQ(combinedImportMessageCapacity, sizeof(detail));
  CHECK_STREQ(detail, "restored");
  std::memset(detail, 0, sizeof(detail));
  CHECK(routes.validateImport(kImport, sizeof(kImport) - 1, detail,
                              sizeof(detail)));
  CHECK_STREQ(detail, "restored");
  CHECK(routes.storageBegin());
  CHECK(!routes.storageEnd());

  resetCombinedFirmwareTestState();
  char uploadMessage[32] = {};
  // The upload route does not use the basename as an image identity.  A
  // user-facing release name may identify the build, while the OTA service
  // validates the application header and combined identity marker.
  CHECK(routes.firmwareUploadBegin("waveshare-multi-mode-v1.2.3.bin",
                                   uploadMessage,
                                   sizeof(uploadMessage)));
  CHECK_EQ(combinedFirmwareBeginCallCount, 1);
  CHECK_STREQ(combinedFirmwareFilename, "waveshare-multi-mode-v1.2.3.bin");
  CHECK_STREQ(uploadMessage, "upload started");
  CHECK_EQ(combinedFirmwareMessageCapacity, sizeof(uploadMessage));

  constexpr unsigned char kFirmwareChunk[] = {0x01, 0xA5, 0xFF, 0x00};
  std::memset(uploadMessage, 0, sizeof(uploadMessage));
  CHECK(routes.firmwareUploadWrite(kFirmwareChunk, sizeof(kFirmwareChunk),
                                   uploadMessage, sizeof(uploadMessage)));
  CHECK_EQ(combinedFirmwareWriteCallCount, 1);
  CHECK(combinedFirmwareData == kFirmwareChunk);
  CHECK_EQ(combinedFirmwareDataLength, sizeof(kFirmwareChunk));
  CHECK_STREQ(uploadMessage, "chunk written");

  std::memset(uploadMessage, 0, sizeof(uploadMessage));
  CHECK(routes.firmwareUploadEnd(uploadMessage, sizeof(uploadMessage)));
  CHECK_EQ(combinedFirmwareEndCallCount, 1);
  CHECK_STREQ(uploadMessage, "upload complete");
  routes.firmwareUploadAbort();
  routes.firmwareUploadRestart();
  CHECK_EQ(combinedFirmwareAbortCallCount, 1);
  CHECK_EQ(combinedFirmwareRestartCallCount, 1);

  // The compatibility alias must expose the same callback DTO and helpers.
  CombinedWebOptions options;
  options.loadStatus = routes.loadStatus;
  options.loadDiagnostics = routes.loadDiagnostics;
  options.loadExport = routes.loadExport;
  options.validateImport = routes.validateImport;
  options.importConfig = routes.importConfig;
  options.accessAllowed = routes.accessAllowed;
  options.storageBegin = routes.storageBegin;
  options.storageEnd = routes.storageEnd;
  options.firmwareUploadBegin = routes.firmwareUploadBegin;
  options.firmwareUploadWrite = routes.firmwareUploadWrite;
  options.firmwareUploadEnd = routes.firmwareUploadEnd;
  options.firmwareUploadAbort = routes.firmwareUploadAbort;
  options.firmwareUploadRestart = routes.firmwareUploadRestart;
  CHECK(options.hasStatusCallback());
  CHECK(options.hasDiagnosticsCallback());
  CHECK(options.hasExportCallback());
  CHECK(options.hasImportValidationCallback());
  CHECK(options.hasImportCallback());
  CHECK(options.hasStorageCallbacks());
  CHECK(options.hasFirmwareUploadCallbacks());
  CHECK(options.accessAllowed());

  // The compatibility alias must preserve the manual upload callbacks and
  // their buffer-oriented signatures, not only the presence helper.
  resetCombinedFirmwareTestState();
  CHECK(options.firmwareUploadWrite(kFirmwareChunk,
                                    sizeof(kFirmwareChunk), uploadMessage,
                                    sizeof(uploadMessage)));
  CHECK_EQ(combinedFirmwareWriteCallCount, 1);
  CHECK(combinedFirmwareData == kFirmwareChunk);
  CHECK_EQ(combinedFirmwareDataLength, sizeof(kFirmwareChunk));
}

void testConfigurationWebRoutesDefaultsAndCallbacks() {
  const ConfigurationWebRoutes defaults;
  CHECK(defaults.webServer == nullptr);
  CHECK_STREQ(defaults.pagePath, "/clock/");
  CHECK_STREQ(defaults.apiPrefix, "/api/modules/clock");
  CHECK_STREQ(defaults.pagePath, CONFIGURATION_WEB_DEFAULT_PAGE_PATH);
  CHECK_STREQ(defaults.apiPrefix, CONFIGURATION_WEB_DEFAULT_API_PREFIX);
  CHECK(defaults.registerLegacyAliases);
  CHECK(defaults.manageServerLifecycle);
  CHECK(defaults.firmwareUpdatesEnabled);
  CHECK(defaults.storageBegin == nullptr);
  CHECK(defaults.storageEnd == nullptr);

  ConfigurationWebRoutes routes;
  routes.pagePath = "/custom/clock/";
  routes.apiPrefix = "/api/custom-clock";
  routes.registerLegacyAliases = false;
  routes.manageServerLifecycle = false;
  routes.firmwareUpdatesEnabled = false;
  routes.storageBegin = configurationStorageBeginForTest;
  routes.storageEnd = configurationStorageEndForTest;

  CHECK_STREQ(routes.pagePath, "/custom/clock/");
  CHECK_STREQ(routes.apiPrefix, "/api/custom-clock");
  CHECK(!routes.registerLegacyAliases);
  CHECK(!routes.manageServerLifecycle);
  CHECK(!routes.firmwareUpdatesEnabled);
  CHECK(routes.storageBegin != nullptr);
  CHECK(routes.storageEnd != nullptr);
  CHECK(routes.storageBegin());
  CHECK(!routes.storageEnd());

  // The compatibility alias must expose the same DTO and callback fields.
  ConfigurationWebOptions options;
  options.storageBegin = routes.storageBegin;
  options.storageEnd = routes.storageEnd;
  CHECK(options.storageBegin == configurationStorageBeginForTest);
  CHECK(options.storageEnd == configurationStorageEndForTest);
}

void testMeteoWebRoutesDefaultsAndCallbacks() {
  using app_core::MeteoWebOptions;
  using app_core::MeteoWebRoutes;

  const MeteoWebRoutes defaults;
  CHECK(defaults.webServer == nullptr);
  CHECK_STREQ(defaults.pagePath, "/meteo/");
  CHECK_STREQ(defaults.apiPrefix, "/api/modules/meteo");
  CHECK_STREQ(defaults.pagePath, app_core::METEO_WEB_DEFAULT_PAGE_PATH);
  CHECK_STREQ(defaults.apiPrefix, app_core::METEO_WEB_DEFAULT_API_PREFIX);
  CHECK(!defaults.registerLegacyAliases);
  CHECK(!defaults.manageServerLifecycle);
  CHECK(!defaults.firmwareUpdatesEnabled);
  CHECK(defaults.loadConfig == nullptr);
  CHECK(defaults.loadStatus == nullptr);
  CHECK(defaults.saveConfig == nullptr);
  CHECK(defaults.handleScreenCommand == nullptr);
  CHECK(defaults.accessAllowed == nullptr);
  CHECK(defaults.storageBegin == nullptr);
  CHECK(defaults.storageEnd == nullptr);
  CHECK(!defaults.hasConfigCallbacks());
  CHECK(!defaults.hasStatusCallback());
  CHECK(!defaults.hasStorageCallbacks());

  MeteoWebRoutes routes;
  routes.pagePath = "/custom/meteo/";
  routes.apiPrefix = "/api/custom-meteo";
  routes.registerLegacyAliases = true;
  routes.manageServerLifecycle = true;
  routes.firmwareUpdatesEnabled = true;
  routes.loadConfig = meteoConfigLoadForTest;
  routes.loadStatus = meteoStatusLoadForTest;
  routes.saveConfig = meteoConfigSaveForTest;
  routes.handleScreenCommand = meteoScreenCommandForTest;
  routes.accessAllowed = meteoAccessAllowedForTest;
  routes.storageBegin = configurationStorageBeginForTest;
  routes.storageEnd = configurationStorageEndForTest;

  CHECK_STREQ(routes.pagePath, "/custom/meteo/");
  CHECK_STREQ(routes.apiPrefix, "/api/custom-meteo");
  CHECK(routes.registerLegacyAliases);
  CHECK(routes.manageServerLifecycle);
  CHECK(routes.firmwareUpdatesEnabled);
  CHECK(routes.loadConfig == meteoConfigLoadForTest);
  CHECK(routes.loadStatus == meteoStatusLoadForTest);
  CHECK(routes.saveConfig == meteoConfigSaveForTest);
  CHECK(routes.handleScreenCommand == meteoScreenCommandForTest);
  CHECK(routes.accessAllowed == meteoAccessAllowedForTest);
  CHECK(routes.storageBegin == configurationStorageBeginForTest);
  CHECK(routes.storageEnd == configurationStorageEndForTest);
  CHECK(routes.hasConfigCallbacks());
  CHECK(routes.hasStatusCallback());
  CHECK(routes.hasStorageCallbacks());

  char json[32] = {};
  const std::size_t loaded = routes.loadConfig(json, sizeof(json));
  CHECK_EQ(loaded, std::strlen("{\"source\":\"chmu\"}"));
  CHECK_STREQ(json, "{\"source\":\"chmu\"}");
  CHECK(routes.saveConfig(json, loaded));

  char status[24] = {};
  const std::size_t statusLength = routes.loadStatus(status, sizeof(status));
  CHECK_EQ(statusLength, std::strlen("{\"ok\":true}"));
  CHECK_STREQ(status, "{\"ok\":true}");
  CHECK(routes.accessAllowed());

  const app_core::MeteoWebScreenCommand rangeCommand{
      app_core::MeteoWebScreenCommandKind::Range, 1};
  CHECK(routes.handleScreenCommand(rangeCommand));
  CHECK(routes.storageBegin());
  CHECK(!routes.storageEnd());

  // The options name is a compatibility alias for the same route DTO.
  MeteoWebOptions options;
  options.loadConfig = routes.loadConfig;
  options.loadStatus = routes.loadStatus;
  options.saveConfig = routes.saveConfig;
  options.handleScreenCommand = routes.handleScreenCommand;
  options.accessAllowed = routes.accessAllowed;
  CHECK(options.loadConfig == meteoConfigLoadForTest);
  CHECK(options.loadStatus == meteoStatusLoadForTest);
  CHECK(options.saveConfig == meteoConfigSaveForTest);
  CHECK(options.handleScreenCommand == meteoScreenCommandForTest);
  CHECK(options.accessAllowed == meteoAccessAllowedForTest);
  CHECK(options.hasConfigCallbacks());
  CHECK(options.hasStatusCallback());
}

void testMeteoRadarConfigDefaultsAndRangePolicy() {
  app_core::MeteoRadarConfig config;
  CHECK(config.validate());
  CHECK(config.source == app_core::MeteoRadarSource::Chmu);
  CHECK_EQ(config.rangeIndex,
           app_core::MeteoRadarConfig::kDefaultRangeIndex);
  CHECK(config.rangeKm() == 50.0f);
  CHECK(!config.wholeCountry());

  config.stepRange(1);
  CHECK(config.rangeKm() == 100.0f);
  config.stepRange(2);
  CHECK(config.wholeCountry());
  config.stepRange(1);
  CHECK(config.rangeKm() == 25.0f);
  config.stepRange(-1);
  CHECK(config.wholeCountry());
}

void testMeteoRadarConfigNormalization() {
  app_core::MeteoRadarConfig config;
  config.latitude = std::numeric_limits<double>::infinity();
  config.longitude = 999.0;
  config.source = static_cast<app_core::MeteoRadarSource>(99);
  config.rangeIndex = 99;

  CHECK(!config.validate());
  CHECK(config.normalize());
  CHECK(config.validate());
  CHECK(config.latitude == app_core::MeteoRadarConfig::kDefaultLatitude);
  CHECK(config.longitude == app_core::MeteoRadarConfig::kDefaultLongitude);
  CHECK(config.source == app_core::MeteoRadarSource::Chmu);
  CHECK_EQ(config.rangeIndex,
           app_core::MeteoRadarConfig::kDefaultRangeIndex);
  CHECK(!config.normalize());

  config.latitude = 0.0;
  config.longitude = 0.0;
  CHECK(config.normalize());
  CHECK(config.validate());

  config.latitude = 48.2;
  config.longitude = 16.37;
  config.source = app_core::MeteoRadarSource::RainViewer;
  config.rangeIndex = 4;
  CHECK(!config.normalize());
  CHECK(config.validate());
  CHECK(config.wholeCountry());
}

GestureEvent recognize(GestureRecognizer& recognizer, bool& recognized,
                       bool pressed, int16_t x, int16_t y, uint32_t nowMs) {
  GestureEvent event;
  recognized = recognizer.update(pressed, x, y, nowMs, event);
  return event;
}

void testGestureDirectionsAndDebounce() {
  GestureRecognizer recognizer;
  bool recognized = false;

  recognize(recognizer, recognized, true, 240, 240, 0);
  recognize(recognizer, recognized, true, 140, 240, 200);
  CHECK(!recognizer.tapCandidate());
  GestureEvent event = recognize(recognizer, recognized, false, 140, 240, 250);
  CHECK(!recognized);  // release debounce has not elapsed
  CHECK(!recognizer.tapCandidate());
  event = recognize(recognizer, recognized, false, 140, 240, 261);
  CHECK(recognized);
  CHECK(event.kind == GestureKind::HorizontalSwipe);
  CHECK_EQ(event.direction, -1);  // left = next screen
  CHECK(!recognizer.tapCandidate());
  CHECK_EQ(event.startX, 240);
  CHECK_EQ(event.endX, 140);
  event = recognize(recognizer, recognized, false, 140, 240, 400);
  CHECK(!recognized);  // one event per touch

  recognizer.reset();
  recognize(recognizer, recognized, true, 100, 200, 1000);
  recognize(recognizer, recognized, true, 190, 200, 1200);
  event = recognize(recognizer, recognized, false, 190, 200, 1260);
  CHECK(recognized);
  CHECK(event.kind == GestureKind::HorizontalSwipe);
  CHECK_EQ(event.direction, 1);

  recognizer.reset();
  recognize(recognizer, recognized, true, 200, 240, 2000);
  recognize(recognizer, recognized, true, 200, 140, 2200);
  event = recognize(recognizer, recognized, false, 200, 140, 2260);
  CHECK(recognized);
  CHECK(event.kind == GestureKind::VerticalSwipe);
  CHECK_EQ(event.direction, -1);  // up

  recognizer.reset();
  recognize(recognizer, recognized, true, 200, 140, 3000);
  recognize(recognizer, recognized, true, 200, 230, 3200);
  event = recognize(recognizer, recognized, false, 200, 230, 3260);
  CHECK(recognized);
  CHECK(event.kind == GestureKind::VerticalSwipe);
  CHECK_EQ(event.direction, 1);  // down
}

void testGestureTapAndLongPress() {
  GestureRecognizer recognizer;
  bool recognized = false;

  recognize(recognizer, recognized, true, 100, 100, 0);
  CHECK(recognizer.tapCandidate());
  GestureEvent event = recognize(recognizer, recognized, false, 105, 102, 60);
  CHECK(recognized);
  CHECK(event.kind == GestureKind::Tap);
  CHECK(recognizer.tapCandidate());

  recognizer.reset();
  recognize(recognizer, recognized, true, 100, 100, 1000);
  recognize(recognizer, recognized, true, 102, 99, 1600);
  event = recognize(recognizer, recognized, false, 102, 99, 1660);
  CHECK(recognized);
  CHECK(event.kind == GestureKind::LongPress);
  CHECK(recognizer.tapCandidate());

  recognizer.reset();
  recognize(recognizer, recognized, true, 100, 100, 2000);
  recognize(recognizer, recognized, true, 200, 200, 2200);
  CHECK(!recognizer.tapCandidate());
  event = recognize(recognizer, recognized, false, 200, 200, 2260);
  CHECK(!recognized);  // too large on both axes to be a tap/long press
  CHECK(!recognizer.tapCandidate());
}

class RecordingModule final : public ScreenModule {
 public:
  explicit RecordingModule(const char* moduleId) : moduleId_(moduleId) {}

  const char* id() const override { return moduleId_; }
  const char* label() const override { return moduleId_; }
  bool begin() override {
    ++beginCount;
    return beginResult;
  }
  void show() override { ++showCount; }
  void hide() override { ++hideCount; }
  void tick(uint32_t nowMs) override {
    ++tickCount;
    lastTickMs = nowMs;
  }
  bool handleGesture(const GestureEvent& event) override {
    ++gestureCount;
    lastGesture = event;
    return consumeGesture;
  }

  bool beginResult = true;
  bool consumeGesture = false;
  int beginCount = 0;
  int showCount = 0;
  int hideCount = 0;
  int tickCount = 0;
  int gestureCount = 0;
  uint32_t lastTickMs = 0;
  GestureEvent lastGesture{};

 private:
  const char* moduleId_;
};

void testScreenManagerNavigationAndDispatch() {
  AppConfig config = AppConfig::defaults();
  CHECK(config.validate());

  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  RecordingModule forecast("meteo.forecast");
  RecordingModule unknown("not.registered");
  ScreenManager manager(config);

  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.add(forecast));
  CHECK(!manager.add(radar));
  CHECK(manager.add(unknown));
  CHECK_EQ(manager.moduleCount(), 4);

  CHECK(manager.begin());
  CHECK(manager.active() == &clock);
  CHECK_EQ(clock.beginCount, 1);
  CHECK_EQ(radar.beginCount, 1);
  CHECK_EQ(forecast.beginCount, 1);
  CHECK_EQ(clock.showCount, 1);

  GestureEvent left{};
  left.kind = GestureKind::HorizontalSwipe;
  left.direction = -1;
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &radar);
  CHECK_EQ(clock.hideCount, 1);
  CHECK_EQ(radar.showCount, 1);

  GestureEvent vertical{};
  vertical.kind = GestureKind::VerticalSwipe;
  vertical.direction = -1;
  radar.consumeGesture = true;
  CHECK(manager.dispatch(vertical));
  CHECK(manager.active() == &radar);  // vertical gestures stay module-local
  CHECK_EQ(radar.gestureCount, 1);
  CHECK(radar.lastGesture.kind == GestureKind::VerticalSwipe);
  radar.consumeGesture = false;

  GestureEvent right{};
  right.kind = GestureKind::HorizontalSwipe;
  right.direction = 1;
  CHECK(manager.dispatch(right));
  CHECK(manager.active() == &clock);

  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &radar);
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &forecast);
  CHECK(manager.showById("meteo.forecast"));
  CHECK(manager.active() == &forecast);
  CHECK(!manager.showById("not.registered"));

  config.setEnabled("meteo.radar", false);
  CHECK(manager.showById("clock.dashboard"));
  CHECK(manager.step(1));  // radar is skipped; the next enabled module is forecast
  CHECK(manager.active() == &forecast);

  CHECK(manager.showById("meteo.forecast"));
  CHECK(manager.active() == &forecast);
  manager.tick(1234);
  CHECK_EQ(forecast.tickCount, 1);
  CHECK_EQ(forecast.lastTickMs, 1234);
}

void testScreenManagerNavigatesFourScreensAndSkipsDisabledPlanes() {
  AppConfig config = AppConfig::defaults();
  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  RecordingModule forecast("meteo.forecast");
  RecordingModule planes("meteo.planes");
  clock.consumeGesture = false;
  radar.consumeGesture = false;
  forecast.consumeGesture = false;
  planes.consumeGesture = false;
  ScreenManager manager(config);
  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.add(forecast));
  CHECK(manager.add(planes));
  CHECK(manager.begin());
  CHECK(manager.active() == &clock);

  GestureEvent left{};
  left.kind = GestureKind::HorizontalSwipe;
  left.direction = -1;
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &radar);
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &forecast);
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &planes);
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &clock);

  // Disabling the last screen must remove it from both navigation directions
  // while retaining the configured order of all other screens.
  CHECK(config.setEnabled("meteo.planes", false));
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &radar);
  GestureEvent right{};
  right.kind = GestureKind::HorizontalSwipe;
  right.direction = 1;
  CHECK(manager.dispatch(right));
  CHECK(manager.active() == &clock);
  CHECK(manager.dispatch(right));
  CHECK(manager.active() == &forecast);
  CHECK(planes.showCount == 1);
}

void testScreenManagerUsesModuleFirstHorizontalFallback() {
  AppConfig config = AppConfig::defaults();
  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  RecordingModule forecast("meteo.forecast");
  RecordingModule planes("meteo.planes");
  ScreenManager manager(config);
  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.add(forecast));
  CHECK(manager.add(planes));
  CHECK(manager.begin());
  CHECK(manager.active() == &clock);

  GestureEvent left{};
  left.kind = GestureKind::HorizontalSwipe;
  left.direction = -1;

  // An active modal/control gets first refusal and can consume the swipe.
  clock.consumeGesture = true;
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &clock);
  CHECK_EQ(clock.gestureCount, 1);
  CHECK_EQ(radar.showCount, 0);

  // Once the module declines the same gesture, global navigation follows the
  // configured order and reveals the next enabled screen.
  clock.consumeGesture = false;
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &radar);
  CHECK_EQ(clock.gestureCount, 2);
  CHECK_EQ(radar.showCount, 1);
}

void testScreenManagerTickNeverChangesScreen() {
  AppConfig config = AppConfig::defaults();
  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  RecordingModule forecast("meteo.forecast");
  ScreenManager manager(config);
  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.add(forecast));
  CHECK(manager.begin());
  CHECK(manager.active() == &clock);

  manager.tick(1U);
  manager.tick(UINT32_MAX - 255U);
  manager.tick(800U);
  CHECK(manager.active() == &clock);
  CHECK_EQ(clock.tickCount, 3);
  CHECK_EQ(radar.tickCount, 0);
  CHECK_EQ(forecast.tickCount, 0);
}

void testScreenManagerUsesConfiguredOrder() {
  AppConfig config = AppConfig::defaults();
  CHECK(config.moveScreen("meteo.forecast", 0));
  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  RecordingModule forecast("meteo.forecast");
  ScreenManager manager(config);

  // Registration order deliberately differs from configuration order.
  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.add(forecast));
  CHECK(manager.begin());
  CHECK(manager.active() == &forecast);

  GestureEvent left{};
  left.kind = GestureKind::HorizontalSwipe;
  left.direction = -1;
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &clock);

  GestureEvent right{};
  right.kind = GestureKind::HorizontalSwipe;
  right.direction = 1;
  CHECK(manager.dispatch(right));
  CHECK(manager.active() == &forecast);
}

void testScreenManagerSkipsDisabledAndUnregisteredEntries() {
  AppConfig config = AppConfig::defaults();
  config.screenCount = 6;
  std::strcpy(config.screens[0].id, "module.unregistered.before");
  config.screens[0].enabled = 1;
  std::strcpy(config.screens[1].id, "meteo.radar");
  config.screens[1].enabled = 0;
  std::strcpy(config.screens[2].id, "module.unregistered.middle");
  config.screens[2].enabled = 1;
  std::strcpy(config.screens[3].id, "clock.dashboard");
  config.screens[3].enabled = 1;
  std::strcpy(config.screens[4].id, "module.unregistered.after");
  config.screens[4].enabled = 1;
  std::strcpy(config.screens[5].id, "meteo.forecast");
  config.screens[5].enabled = 1;
  CHECK(config.validate());

  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  RecordingModule forecast("meteo.forecast");
  ScreenManager manager(config);
  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.add(forecast));
  CHECK(manager.begin());
  CHECK(manager.active() == &clock);

  GestureEvent left{};
  left.kind = GestureKind::HorizontalSwipe;
  left.direction = -1;
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &forecast);
  CHECK_EQ(clock.hideCount, 1);
  CHECK_EQ(forecast.showCount, 1);

  GestureEvent right{};
  right.kind = GestureKind::HorizontalSwipe;
  right.direction = 1;
  CHECK(manager.dispatch(right));
  CHECK(manager.active() == &clock);
  CHECK_EQ(forecast.hideCount, 1);
  CHECK_EQ(clock.showCount, 2);
}

void testScreenManagerDoesNotSelfSwitchWithOneEnabledScreen() {
  AppConfig config = AppConfig::defaults();
  CHECK(config.setEnabled("meteo.radar", false));
  CHECK(config.setEnabled("meteo.forecast", false));
  CHECK(config.setEnabled("meteo.planes", false));

  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  RecordingModule forecast("meteo.forecast");
  RecordingModule planes("meteo.planes");
  ScreenManager manager(config);
  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.add(forecast));
  CHECK(manager.add(planes));
  CHECK(manager.begin());
  CHECK(manager.active() == &clock);

  const int showCount = clock.showCount;
  const int hideCount = clock.hideCount;
  GestureEvent left{};
  left.kind = GestureKind::HorizontalSwipe;
  left.direction = -1;
  CHECK(!manager.dispatch(left));
  CHECK(!manager.step(1));
  CHECK(!manager.showById("meteo.radar"));
  CHECK(manager.active() == &clock);
  CHECK_EQ(clock.showCount, showCount);
  CHECK_EQ(clock.hideCount, hideCount);
  CHECK_EQ(radar.showCount, 0);
  CHECK_EQ(radar.hideCount, 0);
  CHECK_EQ(forecast.showCount, 0);
  CHECK_EQ(forecast.hideCount, 0);
}

void testScreenManagerKeepsLocalGesturesLocal() {
  AppConfig config = AppConfig::defaults();
  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  RecordingModule forecast("meteo.forecast");
  ScreenManager manager(config);
  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.add(forecast));
  CHECK(manager.begin());
  CHECK(manager.showById("meteo.radar"));
  radar.consumeGesture = true;

  const int radarShowCount = radar.showCount;
  const int radarHideCount = radar.hideCount;
  const int clockShowCount = clock.showCount;
  const int clockHideCount = clock.hideCount;
  GestureEvent tap{};
  tap.kind = GestureKind::Tap;
  GestureEvent longPress{};
  longPress.kind = GestureKind::LongPress;
  GestureEvent vertical{};
  vertical.kind = GestureKind::VerticalSwipe;
  vertical.direction = -1;

  CHECK(manager.dispatch(tap));
  CHECK(manager.dispatch(longPress));
  CHECK(manager.dispatch(vertical));
  CHECK(manager.active() == &radar);
  CHECK_EQ(radar.gestureCount, 3);
  CHECK(radar.lastGesture.kind == GestureKind::VerticalSwipe);
  CHECK_EQ(radar.showCount, radarShowCount);
  CHECK_EQ(radar.hideCount, radarHideCount);
  CHECK_EQ(clock.showCount, clockShowCount);
  CHECK_EQ(clock.hideCount, clockHideCount);
}

void testClockWeatherAnimationPolicy() {
  app_core::ClockWeatherAnimationPolicy policy;
  policy.configuredEnabled = true;
  policy.configuredStyle = 1;

  app_core::ClockWeatherAnimationDecision decision =
      app_core::selectClockWeatherAnimation(policy);
  CHECK(!decision.enabled);
  CHECK_EQ(decision.effectiveStyle, 1);

  policy.openMeteo = true;
  decision = app_core::selectClockWeatherAnimation(policy);
  CHECK(decision.enabled);
  CHECK_EQ(decision.effectiveStyle, 1);

  policy.configuredEnabled = false;
  decision = app_core::selectClockWeatherAnimation(policy);
  CHECK(!decision.enabled);

  policy.configuredEnabled = true;
  policy.openMeteo = false;
  policy.leftUsesWeather = true;
  decision = app_core::selectClockWeatherAnimation(policy);
  CHECK(decision.enabled);

  policy.leftUsesWeather = false;
  policy.rightUsesWeather = true;
  policy.configuredStyle = 2;
  decision = app_core::selectClockWeatherAnimation(policy);
  CHECK(decision.enabled);
  CHECK_EQ(decision.effectiveStyle, 2);

  policy.nightMode = true;
  decision = app_core::selectClockWeatherAnimation(policy);
  CHECK(decision.enabled);
  CHECK_EQ(decision.effectiveStyle, 0);
}

void testUpstreamWeatherAnimationAssetKeys() {
  char key[48] = {};
  CHECK(weatherAnimationAssetKey(key, sizeof(key), 800, true,
                                 CLOCK_WEATHER_ICON_STYLE_FLAT));
  CHECK_STREQ(key, "flat-clear-day");
  CHECK(weatherAnimationAssetKey(key, sizeof(key), 800, false,
                                 CLOCK_WEATHER_ICON_STYLE_LINE));
  CHECK_STREQ(key, "line-clear-night");
  CHECK(weatherAnimationAssetKey(key, sizeof(key), 803, true,
                                 CLOCK_WEATHER_ICON_STYLE_MONOCHROME));
  CHECK_STREQ(key, "monochrome-overcast-day");
  CHECK(weatherAnimationAssetKey(key, sizeof(key), 803, false,
                                 CLOCK_WEATHER_ICON_STYLE_FLAT));
  CHECK_STREQ(key, "flat-overcast-night");
  CHECK(weatherAnimationAssetKey(key, sizeof(key), 804, true,
                                 CLOCK_WEATHER_ICON_STYLE_LINE));
  CHECK_STREQ(key, "line-overcast");
  CHECK(weatherAnimationAssetKey(key, sizeof(key), 511, true,
                                 CLOCK_WEATHER_ICON_STYLE_FLAT));
  CHECK_STREQ(key, "flat-sleet");
  CHECK(weatherAnimationAssetKey(key, sizeof(key), 741, true,
                                 CLOCK_WEATHER_ICON_STYLE_MONOCHROME));
  CHECK_STREQ(key, "monochrome-mist");
  CHECK(weatherAnimationAssetKey(key, sizeof(key), 200, true,
                                 CLOCK_WEATHER_ICON_STYLE_LINE));
  CHECK_STREQ(key, "line-thunderstorms");
  CHECK(!weatherAnimationAssetKey(key, sizeof(key), -1, true,
                                  CLOCK_WEATHER_ICON_STYLE_FLAT));
}

}  // namespace

int main() {
  testBuildProvenance();
  testAppConfigDefaults();
  testAppConfigNormalizesAndPreservesOrder();
  testAppConfigFallbackAndEditing();
  testAppConfigKeepsOneScreenReachable();
  testAppConfigNormalizationRetainsDisabledPlanes();
  testNavigationIndicatorUsesConfiguredVisibleStableIds();
  testDayNightTransitionTimestampToleranceAndFallback();
  testDayNightOffsetsAndTransitions();
  testHomeAssistantStoredTokenReusePolicy();
  testHomeAssistantBatchPolicy();
  testMeteoOutsideTemperaturePolicy();
  testCombinedWebRoutesDefaultsAndCallbacks();
  testConfigurationWebRoutesDefaultsAndCallbacks();
  testMeteoWebRoutesDefaultsAndCallbacks();
  testMeteoRadarConfigDefaultsAndRangePolicy();
  testMeteoRadarConfigNormalization();
  testGestureDirectionsAndDebounce();
  testGestureTapAndLongPress();
  testScreenManagerNavigationAndDispatch();
  testScreenManagerNavigatesFourScreensAndSkipsDisabledPlanes();
  testScreenManagerUsesModuleFirstHorizontalFallback();
  testScreenManagerTickNeverChangesScreen();
  testScreenManagerUsesConfiguredOrder();
  testScreenManagerSkipsDisabledAndUnregisteredEntries();
  testScreenManagerDoesNotSelfSwitchWithOneEnabledScreen();
  testScreenManagerKeepsLocalGesturesLocal();
  testClockWeatherAnimationPolicy();
  testUpstreamWeatherAnimationAssetKeys();

  if (failures != 0) {
    std::fprintf(stderr, "%d test assertion(s) failed\n", failures);
    return 1;
  }
  std::puts("All native app-core tests passed");
  return 0;
}

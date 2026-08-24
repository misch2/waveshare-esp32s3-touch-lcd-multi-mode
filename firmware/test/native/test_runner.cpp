#include "AppConfig.h"
#include "ConfigurationWebRoutes.h"
#include "DayNightLogic.h"
#include "HomeAssistantConnectionPolicy.h"
#include "GestureRecognizer.h"
#include "MeteoRadarConfig.h"
#include "ScreenManager.h"

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

void testAppConfigDefaults() {
  const AppConfig config = AppConfig::defaults();

  CHECK(config.validate());
  CHECK_EQ(config.schemaVersion, AppConfig::kSchemaVersion);
  CHECK_EQ(config.screenCount, 2);
  CHECK_STREQ(config.screens[0].id, "clock.dashboard");
  CHECK_STREQ(config.screens[1].id, "meteo.radar");
  CHECK(config.isEnabled("clock.dashboard"));
  CHECK(config.isEnabled("meteo.radar"));
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
  CHECK_EQ(config.screenCount, 3);
  CHECK_STREQ(config.screens[0].id, "clock.dashboard");
  CHECK_STREQ(config.screens[1].id, "extra.screen");
  CHECK_STREQ(config.screens[2].id, "meteo.radar");
  CHECK_EQ(config.screens[0].enabled, 1);
  CHECK_EQ(config.screens[1].enabled, 0);
  CHECK_EQ(config.screens[2].enabled, 1);
}

void testAppConfigFallbackAndEditing() {
  AppConfig config{};
  config.screenCount = 3;
  std::strcpy(config.screens[0].id, "not valid");
  std::strcpy(config.screens[1].id, "also not-valid");
  config.screens[2].id[0] = '\0';

  CHECK(config.normalize());
  CHECK(config.validate());
  CHECK_EQ(config.screenCount, 2);
  CHECK_STREQ(config.screens[0].id, "clock.dashboard");
  CHECK_STREQ(config.screens[1].id, "meteo.radar");

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
  CHECK(!config.validate());
  CHECK(config.normalize());
  CHECK(config.validate());
  CHECK(config.isEnabled("clock.dashboard"));

  AppConfig empty{};
  empty.schemaVersion = AppConfig::kSchemaVersion;
  CHECK(!empty.validate());
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

bool configurationStorageBeginForTest() { return true; }
bool configurationStorageEndForTest() { return false; }

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
  bool consumeGesture = true;
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
  config.screenCount = 3;
  std::strcpy(config.screens[2].id, "meteo.extra");
  config.screens[2].enabled = 1;
  CHECK(config.validate());

  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  RecordingModule extra("meteo.extra");
  RecordingModule unknown("not.registered");
  ScreenManager manager(config);

  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.add(extra));
  CHECK(!manager.add(radar));
  CHECK(!manager.add(unknown));
  CHECK_EQ(manager.moduleCount(), 3);

  CHECK(manager.begin());
  CHECK(manager.active() == &clock);
  CHECK_EQ(clock.beginCount, 1);
  CHECK_EQ(radar.beginCount, 1);
  CHECK_EQ(extra.beginCount, 1);
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
  CHECK(manager.dispatch(vertical));
  CHECK(manager.active() == &radar);  // vertical gestures stay module-local
  CHECK_EQ(radar.gestureCount, 1);
  CHECK(radar.lastGesture.kind == GestureKind::VerticalSwipe);

  GestureEvent right{};
  right.kind = GestureKind::HorizontalSwipe;
  right.direction = 1;
  CHECK(manager.dispatch(right));
  CHECK(manager.active() == &clock);

  CHECK(manager.showById("meteo.extra"));
  CHECK(manager.active() == &extra);
  CHECK(!manager.showById("not.registered"));

  config.setEnabled("meteo.radar", false);
  CHECK(manager.showById("clock.dashboard"));
  CHECK(manager.step(1));  // radar is skipped; the next enabled module is extra
  CHECK(manager.active() == &extra);

  CHECK(manager.showById("meteo.extra"));
  CHECK(manager.active() == &extra);
  manager.tick(1234);
  CHECK_EQ(extra.tickCount, 1);
  CHECK_EQ(extra.lastTickMs, 1234);
}

void testScreenManagerTickNeverChangesScreen() {
  AppConfig config = AppConfig::defaults();
  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  ScreenManager manager(config);
  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.begin());
  CHECK(manager.active() == &clock);

  manager.tick(1U);
  manager.tick(UINT32_MAX - 255U);
  manager.tick(800U);
  CHECK(manager.active() == &clock);
  CHECK_EQ(clock.tickCount, 3);
  CHECK_EQ(radar.tickCount, 0);
}

void testScreenManagerUsesConfiguredOrder() {
  AppConfig config = AppConfig::defaults();
  CHECK(config.moveScreen("meteo.radar", 0));
  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  ScreenManager manager(config);

  // Registration order deliberately differs from configuration order.
  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.begin());
  CHECK(manager.active() == &radar);

  GestureEvent left{};
  left.kind = GestureKind::HorizontalSwipe;
  left.direction = -1;
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &clock);
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
  std::strcpy(config.screens[5].id, "meteo.extra");
  config.screens[5].enabled = 1;
  CHECK(config.validate());

  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  RecordingModule extra("meteo.extra");
  ScreenManager manager(config);
  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.add(extra));
  CHECK(manager.begin());
  CHECK(manager.active() == &clock);

  GestureEvent left{};
  left.kind = GestureKind::HorizontalSwipe;
  left.direction = -1;
  CHECK(manager.dispatch(left));
  CHECK(manager.active() == &extra);
  CHECK_EQ(clock.hideCount, 1);
  CHECK_EQ(extra.showCount, 1);

  GestureEvent right{};
  right.kind = GestureKind::HorizontalSwipe;
  right.direction = 1;
  CHECK(manager.dispatch(right));
  CHECK(manager.active() == &clock);
  CHECK_EQ(extra.hideCount, 1);
  CHECK_EQ(clock.showCount, 2);
}

void testScreenManagerDoesNotSelfSwitchWithOneEnabledScreen() {
  AppConfig config = AppConfig::defaults();
  CHECK(config.setEnabled("meteo.radar", false));

  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  ScreenManager manager(config);
  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
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
}

void testScreenManagerKeepsLocalGesturesLocal() {
  AppConfig config = AppConfig::defaults();
  RecordingModule clock("clock.dashboard");
  RecordingModule radar("meteo.radar");
  ScreenManager manager(config);
  CHECK(manager.add(clock));
  CHECK(manager.add(radar));
  CHECK(manager.begin());
  CHECK(manager.showById("meteo.radar"));

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

}  // namespace

int main() {
  testAppConfigDefaults();
  testAppConfigNormalizesAndPreservesOrder();
  testAppConfigFallbackAndEditing();
  testAppConfigKeepsOneScreenReachable();
  testDayNightTransitionTimestampToleranceAndFallback();
  testDayNightOffsetsAndTransitions();
  testHomeAssistantStoredTokenReusePolicy();
  testConfigurationWebRoutesDefaultsAndCallbacks();
  testMeteoRadarConfigDefaultsAndRangePolicy();
  testMeteoRadarConfigNormalization();
  testGestureDirectionsAndDebounce();
  testGestureTapAndLongPress();
  testScreenManagerNavigationAndDispatch();
  testScreenManagerTickNeverChangesScreen();
  testScreenManagerUsesConfiguredOrder();
  testScreenManagerSkipsDisabledAndUnregisteredEntries();
  testScreenManagerDoesNotSelfSwitchWithOneEnabledScreen();
  testScreenManagerKeepsLocalGesturesLocal();

  if (failures != 0) {
    std::fprintf(stderr, "%d test assertion(s) failed\n", failures);
    return 1;
  }
  std::puts("All native app-core tests passed");
  return 0;
}

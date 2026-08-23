#include <Arduino.h>
#include <Wire.h>

#include <cstring>

#include "AppConfig.h"
#include "AppConfigStore.h"
#include "ClockConfig.h"
#include "ClockDataService.h"
#include "ClockScreen.h"
#include "DisplayHost.h"
#include "GestureRecognizer.h"
#include "HomeAssistantConnectionPolicy.h"
#include "I2C_Driver.h"
#include "NetworkHost.h"
#include "RadarScreen.h"
#include "ScreenManager.h"
#include "TCA9554PWR.h"
#include "Display_ST7701.h"
#include "UpstreamHardware.h"
#include "WebHost.h"

namespace {
app_core::AppConfig appConfig = app_core::AppConfig::defaults();
ClockConfig clockConfig;
ClockDataService clockDataService;
ClockValues latestClockValues;
GestureRecognizer gestureRecognizer;
ScreenManager screenManager(appConfig);

void previewClockBrightness(uint8_t brightness) {
  displayHostSetBrightness(brightness);
}
void openClockSettings();

ClockScreen clockScreen(clockConfig, previewClockBrightness, openClockSettings);
RadarScreen radarScreen;
GestureEvent pendingGesture;
bool gesturePending = false;
bool clockTimeWasSynchronized = false;
bool webConfigApplyPending = false;
uint32_t webConfigApplyAt = 0;

void loadClockConfigForWeb(ClockConfig& config) { config = clockConfig; }

bool saveClockConfigFromWeb(const ClockConfig& config,
                            bool tokenWasSubmitted) {
  ClockConfig candidate = config;
  if (!tokenWasSubmitted) {
    if (homeAssistantMayReuseStoredToken(candidate.homeAssistantUrl,
                                         clockConfig.homeAssistantUrl)) {
      clockConfigCopy(candidate.homeAssistantToken,
                      sizeof(candidate.homeAssistantToken),
                      clockConfig.homeAssistantToken);
    } else {
      candidate.homeAssistantToken[0] = '\0';
    }
  }
  if (!clockConfigSave(candidate)) return false;

  clockConfig = candidate;
  webConfigApplyPending = true;
  webConfigApplyAt = millis() + 250;
  return true;
}

void updateClockWebStatus(bool active) {
  clockScreen.updateWebStatus(active, static_cast<uint8_t>(web_host::mode()));
}

void loadSunTransitionTimes(uint64_t& nextSunriseTimestamp,
                            uint64_t& nextSunsetTimestamp) {
  nextSunriseTimestamp = latestClockValues.nextSunriseTimestamp;
  nextSunsetTimestamp = latestClockValues.nextSunsetTimestamp;
}

bool requestDayNightRefresh() {
  return clockDataService.requestDayNightRefresh();
}

void loadDayNightStatus(bool& sunAvailable, bool& sunIsDay,
                        bool& lightAvailable, bool& lightOn,
                        bool& nightMode) {
  sunAvailable = latestClockValues.sunStateAvailable;
  sunIsDay = latestClockValues.weatherIsDay;
  lightAvailable = latestClockValues.dayNightLightStateAvailable;
  lightOn = latestClockValues.dayNightLightOn;
  nightMode = clockScreen.nightModeEnabled();
}

void setDisplayForcedOff(bool forcedOff) {
  displayHostSetForcedOff(forcedOff);
}

bool displayIsForcedOff() { return displayHostForcedOff(); }

void openClockSettings() {
  web_host::ensureActive();
}

void applyPendingWebConfiguration(uint32_t nowMs) {
  if (!webConfigApplyPending ||
      static_cast<int32_t>(nowMs - webConfigApplyAt) < 0) {
    return;
  }
  webConfigApplyPending = false;
  webConfigApplyAt = 0;
  clockScreen.applyConfiguration();
  clockDataService.applyConfig(clockConfig);
  displayHostResync();
}

void onTouchSample(bool pressed, int16_t x, int16_t y, uint32_t nowMs) {
  GestureEvent event;
  if (gestureRecognizer.update(pressed, x, y, nowMs, event)) {
    pendingGesture = event;
    gesturePending = true;
  }
}

[[noreturn]] void halt(const char* reason) {
  Serial.printf("FATAL: %s\n", reason);
  Set_Backlight(10);
  while (true) delay(1000);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Multi-mode screen prototype starting");

  // ClockConfig owns its own partition and performs schema migrations. It
  // must be ready before display construction so the dashboard never starts
  // with a transient, different configuration.
  const bool clockStorageReady = clockConfigBegin();
  if (!clockStorageReady || !clockConfigLoad(clockConfig)) {
    clockConfigApplyDefaults(clockConfig);
    if (!clockStorageReady) {
      Serial.println("Warning: clock configuration partition unavailable; using defaults");
    } else {
      Serial.println("Warning: clock configuration invalid; using defaults");
    }
  }

  I2C_Init();
  Set_EXIOS(0x0C);
  TCA9554PWR_Init(0x70);
  LCD_Init();
  Set_Backlight(clockConfig.dayBrightness);

  if (!displayHostBegin(onTouchSample)) halt("display host init failed");
  displayHostSetBrightness(clockConfig.dayBrightness);
  if (!appConfigLoad(appConfig)) {
    appConfig = app_core::AppConfig::defaults();
    if (!appConfigSave(appConfig)) {
      Serial.println("Warning: app configuration could not be persisted");
    }
  }
  if (!screenManager.add(clockScreen) || !screenManager.add(radarScreen)) {
    halt("screen registration failed");
  }
  if (!screenManager.begin()) halt("screen init failed");
  if (!network_host::begin()) {
    Serial.println("Warning: network host initialization failed");
  }
  if (!clockDataService.begin(clockConfig)) {
    Serial.println("Warning: clock data service initialization failed");
  }
  web_host::begin(loadClockConfigForWeb, saveClockConfigFromWeb,
                  updateClockWebStatus, loadSunTransitionTimes,
                  requestDayNightRefresh, loadDayNightStatus,
                  setDisplayForcedOff, displayIsForcedOff);

  Serial.printf("Ready: %u screens, active=%s, PSRAM free=%u\n",
                static_cast<unsigned>(screenManager.moduleCount()),
                screenManager.active()->id(),
                static_cast<unsigned>(ESP.getFreePsram()));
}

void loop() {
  network_host::loop();
  displayHostLoop();

  clockScreen.updateNetworkStatus(network_host::connected(),
                                  network_host::ipAddress());
  std::tm localTime;
  if (network_host::localTime(localTime)) {
    clockScreen.updateLocalTime(localTime);
  }
  const bool timeSynchronized = network_host::timeSynchronized();
  if (timeSynchronized && !clockTimeWasSynchronized) {
    clockDataService.requestRefresh();
  }
  clockTimeWasSynchronized = timeSynchronized;

  ClockValues clockValues;
  if (clockDataService.consumeValues(clockValues)) {
    latestClockValues = clockValues;
    clockScreen.updateValues(clockValues);
  }

  uint8_t requestedWebMode = 0;
  if (clockScreen.takeConfigSaveRequest(requestedWebMode)) {
    if (clockConfigSave(clockConfig)) {
      clockDataService.applyConfig(clockConfig);
      if (!web_host::setMode(
              static_cast<web_host::Mode>(requestedWebMode))) {
        Serial.println("Warning: web mode could not be persisted");
      }
      displayHostResync();
    } else {
      Serial.println("Warning: clock configuration could not be persisted");
    }
  }

  web_host::loop();
  applyPendingWebConfiguration(millis());

  if (gesturePending) {
    const GestureEvent event = pendingGesture;
    gesturePending = false;
    const char* before = screenManager.active() != nullptr
                             ? screenManager.active()->id()
                             : "none";
    screenManager.dispatch(event);
    const char* after = screenManager.active() != nullptr
                            ? screenManager.active()->id()
                            : "none";
    if (std::strcmp(before, after) != 0) Serial.printf("Screen: %s\n", after);
  }

  screenManager.tick(millis());
  delay(5);
}

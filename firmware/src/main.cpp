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
#include "I2C_Driver.h"
#include "NetworkHost.h"
#include "RadarScreen.h"
#include "ScreenManager.h"
#include "TCA9554PWR.h"
#include "Display_ST7701.h"
#include "UpstreamHardware.h"

namespace {
app_core::AppConfig appConfig = app_core::AppConfig::defaults();
ClockConfig clockConfig;
ClockDataService clockDataService;
GestureRecognizer gestureRecognizer;
ScreenManager screenManager(appConfig);

void previewClockBrightness(uint8_t brightness) { Set_Backlight(brightness); }

ClockScreen clockScreen(clockConfig, previewClockBrightness);
RadarScreen radarScreen;
GestureEvent pendingGesture;
bool gesturePending = false;
bool clockTimeWasSynchronized = false;

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
    clockScreen.updateValues(clockValues);
  }

  if (clockScreen.takeConfigSaveRequest()) {
    if (!clockConfigSave(clockConfig)) {
      Serial.println("Warning: clock configuration could not be persisted");
    }
    clockDataService.applyConfig(clockConfig);
    displayHostResync();
  }

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

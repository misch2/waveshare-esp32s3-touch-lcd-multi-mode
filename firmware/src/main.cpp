#include <Arduino.h>
#include <Wire.h>

#include <cstring>

#include "AppConfig.h"
#include "AppConfigStore.h"
#include "ClockScreen.h"
#include "DisplayHost.h"
#include "GestureRecognizer.h"
#include "I2C_Driver.h"
#include "RadarScreen.h"
#include "ScreenManager.h"
#include "TCA9554PWR.h"
#include "Display_ST7701.h"
#include "UpstreamHardware.h"

namespace {
app_core::AppConfig appConfig = app_core::AppConfig::defaults();
GestureRecognizer gestureRecognizer;
ScreenManager screenManager(appConfig);
ClockScreen clockScreen;
RadarScreen radarScreen;
GestureEvent pendingGesture;
bool gesturePending = false;

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

  I2C_Init();
  Set_EXIOS(0x0C);
  TCA9554PWR_Init(0x70);
  LCD_Init();
  Set_Backlight(35);

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

  Serial.printf("Ready: %u screens, active=%s, PSRAM free=%u\n",
                static_cast<unsigned>(screenManager.moduleCount()),
                screenManager.active()->id(),
                static_cast<unsigned>(ESP.getFreePsram()));
}

void loop() {
  displayHostLoop();

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

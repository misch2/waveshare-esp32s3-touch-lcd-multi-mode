#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Wire.h>

#include <esp_heap_caps.h>
#include <esp_system.h>

#include <cmath>
#include <cstring>

#include "AppConfig.h"
#include "AppConfigStore.h"
#include "ClockConfig.h"
#include "ClockDataService.h"
#include "ClockScreen.h"
#include "DisplayHost.h"
#include "ForecastScreen.h"
#include "GestureRecognizer.h"
#include "HomeAssistantConnectionPolicy.h"
#include "I2C_Driver.h"
#include "MeteoSettingsAdapter.h"
#include "MeteoWebRoutes.h"
#include "NetworkHost.h"
#include "PlanesScreen.h"
#include "RadarScreen.h"
#include "ScreenManager.h"
#include "TCA9554PWR.h"
#include "Display_ST7701.h"
#include "UpstreamHardware.h"
#include "WebHost.h"

#ifdef BOOT_PIN
#undef BOOT_PIN
#endif
#include "Forecast.h"
#include "ScreenPlanes.h"
#include "ScreenWeather.h"
#include "Settings.h"
#include "Status.h"

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
bool allowClockDashboardShortClick();

ClockScreen clockScreen(clockConfig, previewClockBrightness, openClockSettings,
                        allowClockDashboardShortClick);
RadarScreen radarScreen;
ForecastScreen forecastScreen;
PlanesScreen planesScreen;
GestureEvent pendingGesture;
bool gesturePending = false;
bool clockTimeWasSynchronized = false;
bool webConfigApplyPending = false;
uint32_t webConfigApplyAt = 0;
uint8_t configurationStorageDepth = 0;
app_core::MeteoWebScreenCommand pendingMeteoWebCommand;
bool meteoWebCommandPending = false;
bool meteoConfigApplyPending = false;
bool meteoRestartPending = false;
uint32_t meteoRestartAt = 0;

constexpr char kClockScreenId[] = "clock.dashboard";
constexpr char kPlanesScreenId[] = "meteo.planes";
constexpr char kRadarScreenId[] = "meteo.radar";
constexpr char kForecastScreenId[] = "meteo.forecast";
constexpr char kCombinedFirmwareVersion[] = "development";

[[noreturn]] void halt(const char* reason);

bool beginConfigurationStorageWrite() {
  if (configurationStorageDepth > 0) {
    if (configurationStorageDepth == UINT8_MAX) return false;
    ++configurationStorageDepth;
    return true;
  }
  if (!displayHostBeginStorageWrite()) return false;
  configurationStorageDepth = 1;
  return true;
}

bool endConfigurationStorageWrite() {
  if (configurationStorageDepth == 0) return false;
  --configurationStorageDepth;
  if (configurationStorageDepth > 0) return true;
  if (!displayHostEndStorageWrite()) {
    halt("display driver recreation failed after configuration save");
  }
  return true;
}

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

const char* meteoScreenIdForIndex(int index) {
  switch (index) {
    case 0: return kClockScreenId;
    case 1: return kPlanesScreenId;
    case 2: return kRadarScreenId;
    case 3: return kForecastScreenId;
    default: return nullptr;
  }
}

int meteoScreenIndexForId(const char* id) {
  if (id == nullptr) return -1;
  if (std::strcmp(id, kClockScreenId) == 0) return 0;
  if (std::strcmp(id, kPlanesScreenId) == 0) return 1;
  if (std::strcmp(id, kRadarScreenId) == 0) return 2;
  if (std::strcmp(id, kForecastScreenId) == 0) return 3;
  return -1;
}

size_t writeJsonDocument(const JsonDocument& document, char* out,
                         size_t capacity) {
  if (out == nullptr || capacity == 0) return 0;
  const size_t required = measureJson(document);
  if (required == 0 || required >= capacity) return 0;
  const size_t written = serializeJson(document, out, capacity);
  return written == required ? written : 0;
}

size_t loadMeteoConfigForWeb(char* out, size_t capacity) {
  JsonDocument document;
  JsonObject config = document.to<JsonObject>();
  Settings_ToJson(config);

  // AppConfig is the sole owner of reachability. The upstream bit mask remains
  // a standalone compatibility detail and is never exposed as host truth.
  JsonObject screens = config["screens"].to<JsonObject>();
  screens["clock"] = appConfig.isEnabled(kClockScreenId);
  screens["planes"] = appConfig.isEnabled(kPlanesScreenId);
  screens["meteo"] = appConfig.isEnabled(kRadarScreenId);
  screens["forecast"] = appConfig.isEnabled(kForecastScreenId);
  config["autoRotate"] = 0;
  config["version"] = kCombinedFirmwareVersion;
  config["apMode"] = false;
  config["ip"] = network_host::ipAddress();
  return writeJsonDocument(document, out, capacity);
}

bool atLeastOneRegisteredScreenEnabled(const app_core::AppConfig& candidate) {
  return candidate.isEnabled(kClockScreenId) ||
         candidate.isEnabled(kPlanesScreenId) ||
         candidate.isEnabled(kRadarScreenId) ||
         candidate.isEnabled(kForecastScreenId);
}

bool saveMeteoConfigFromWeb(const char* json, size_t length) {
  if (json == nullptr || length == 0) return false;

  JsonDocument document;
  if (deserializeJson(document, json, length) != DeserializationError::Ok) {
    return false;
  }
  JsonObject config = document.as<JsonObject>();
  if (config.isNull()) return false;

  app_core::AppConfig candidate = appConfig;
  JsonObjectConst screens = config["screens"];
  bool hostConfigChanged = false;
  if (!screens.isNull()) {
    struct ScreenMapping {
      const char* jsonKey;
      const char* stableId;
    };
    constexpr ScreenMapping mappings[] = {
        {"clock", kClockScreenId},
        {"planes", kPlanesScreenId},
        {"meteo", kRadarScreenId},
        {"forecast", kForecastScreenId},
    };
    for (const ScreenMapping& mapping : mappings) {
      JsonVariantConst value = screens[mapping.jsonKey];
      if (value.isNull()) continue;
      const bool enabled = value.as<bool>();
      if (candidate.isEnabled(mapping.stableId) != enabled) {
        if (!candidate.setEnabled(mapping.stableId, enabled)) return false;
        hostConfigChanged = true;
      }
    }
    if (!atLeastOneRegisteredScreenEnabled(candidate) ||
        !candidate.validate()) {
      return false;
    }
  }

  const double oldLatitude = Settings_Lat();
  const double oldLongitude = Settings_Lon();
  const uint8_t oldRadarSource = Settings_RadarSource();

  // These fields belong to the combined host or clock module. They remain in
  // the pinned page DOM for upstream compatibility, but the combined adapter
  // hides them and must not silently translate their different semantics.
  config.remove("screens");
  config.remove("autoRotate");
  config.remove("briDay");
  config.remove("briNight");
  config.remove("nightAuto");
  config.remove("nightOffset");
  config.remove("secStyle");
  config.remove("clockColor");
  config.remove("secColor");
  config.remove("password");
  config.remove("newPassword");
  config.remove("hasPassword");

  if (hostConfigChanged && !appConfigSave(candidate)) return false;
  if (hostConfigChanged) appConfig = candidate;
  const JsonObjectConst upstreamConfig = config;
  Settings_FromJson(upstreamConfig);

  const bool locationChanged =
      std::fabs(oldLatitude - Settings_Lat()) > 1e-6 ||
      std::fabs(oldLongitude - Settings_Lon()) > 1e-6;
  if (locationChanged) Forecast_Invalidate();

  meteoConfigApplyPending = true;
  if (locationChanged || oldRadarSource != Settings_RadarSource()) {
    // The pinned radar keeps decoded frames and crop state tied to both
    // values. Preserve its proven clean-restart behavior, but only after the
    // HTTP response and the host storage transaction have completed.
    meteoRestartPending = true;
    // Some upstream UI fields (notably top bearing) share a 2 s debounced
    // Preferences flush. Keep the loop alive long enough to persist them
    // before the clean restart applies the new radar cache geometry.
    meteoRestartAt = millis() + 2500;
  }
  return true;
}

const char* resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_EXT: return "reset pin";
    default: return "?";
  }
}

size_t loadMeteoStatusForWeb(char* out, size_t capacity) {
  JsonDocument document;
  document["version"] = kCombinedFirmwareVersion;
  document["ip"] = network_host::ipAddress();
  document["ssid"] = WiFi.SSID();
  document["rssi"] = network_host::connected() ? WiFi.RSSI() : 0;

  const unsigned long uptimeSeconds = millis() / 1000UL;
  char uptime[32];
  snprintf(uptime, sizeof(uptime), "%lud %02lu:%02lu:%02lu",
           uptimeSeconds / 86400UL, (uptimeSeconds / 3600UL) % 24UL,
           (uptimeSeconds / 60UL) % 60UL, uptimeSeconds % 60UL);
  document["uptime"] = uptime;
  document["heap"] = static_cast<uint32_t>(
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  document["psram"] =
      static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  document["resetReason"] = resetReasonText();

  const ScreenModule* active = screenManager.active();
  const int activeIndex =
      meteoScreenIndexForId(active != nullptr ? active->id() : nullptr);
  document["screen"] = activeIndex;
  char range[24] = "";
  if (activeIndex == 1) {
    ScreenPlanes_RangeText(range, sizeof(range));
  } else if (activeIndex == 2) {
    ScreenWeather_RangeText(range, sizeof(range));
  }
  document["range"] = range;

  JsonArray enabled = document["enabled"].to<JsonArray>();
  enabled.add(appConfig.isEnabled(kClockScreenId));
  enabled.add(appConfig.isEnabled(kPlanesScreenId));
  enabled.add(appConfig.isEnabled(kRadarScreenId));
  enabled.add(appConfig.isEnabled(kForecastScreenId));

  char status[64];
  Status_Text(ST_ADSB, status, sizeof(status));
  document["adsb"] = status;
  Status_Text(ST_RADAR, status, sizeof(status));
  document["radar"] = status;
  Status_Text(ST_FORECAST, status, sizeof(status));
  document["forecast"] = status;
  return writeJsonDocument(document, out, capacity);
}

bool queueMeteoWebCommand(
    const app_core::MeteoWebScreenCommand& command) {
  if (meteoWebCommandPending) return false;
  if (command.kind == app_core::MeteoWebScreenCommandKind::Select) {
    const char* id = meteoScreenIdForIndex(command.value);
    if (id == nullptr || !appConfig.isEnabled(id)) return false;
  } else if (command.value == 0 || command.value < -1 || command.value > 1) {
    return false;
  }
  pendingMeteoWebCommand = command;
  meteoWebCommandPending = true;
  return true;
}

app_core::MeteoWebRoutes makeMeteoWebRoutes() {
  app_core::MeteoWebRoutes routes;
  routes.loadConfig = loadMeteoConfigForWeb;
  routes.loadStatus = loadMeteoStatusForWeb;
  routes.saveConfig = saveMeteoConfigFromWeb;
  routes.handleScreenCommand = queueMeteoWebCommand;
  routes.storageBegin = beginConfigurationStorageWrite;
  routes.storageEnd = endConfigurationStorageWrite;
  return routes;
}

void applyPendingMeteoWebWork() {
  if (meteoConfigApplyPending) {
    meteoConfigApplyPending = false;
    ScreenModule* active = screenManager.active();
    if (active != nullptr && !appConfig.isEnabled(active->id())) {
      screenManager.step(1);
    }
    displayHostRequestFullRedraw();
  }

  if (!meteoWebCommandPending) return;
  const app_core::MeteoWebScreenCommand command = pendingMeteoWebCommand;
  meteoWebCommandPending = false;
  switch (command.kind) {
    case app_core::MeteoWebScreenCommandKind::Select: {
      const char* id = meteoScreenIdForIndex(command.value);
      if (id != nullptr) screenManager.showById(id);
      break;
    }
    case app_core::MeteoWebScreenCommandKind::Step:
      screenManager.step(command.value);
      break;
    case app_core::MeteoWebScreenCommandKind::Range: {
      GestureEvent event;
      event.kind = GestureKind::VerticalSwipe;
      event.direction = command.value < 0 ? 1 : -1;
      screenManager.dispatch(event);
      break;
    }
  }
}

void openClockSettings() {
  web_host::ensureActive();
}

bool allowClockDashboardShortClick() {
  return gestureRecognizer.tapCandidate();
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
  displayHostRequestFullRedraw();
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
  if (clockConfig.automaticFirmwareUpdate) {
    clockConfig.automaticFirmwareUpdate = false;
    if (clockStorageReady && !clockConfigSave(clockConfig)) {
      Serial.println("Warning: automatic firmware updates could not be disabled in storage");
    }
  }

  // Finish all host-owned first-boot NVS reads and possible default writes
  // before RGB scanout starts. Bounce-buffer refill reads from PSRAM and is not
  // safe while the bundled framework disables external cache for flash access.
  if (!appConfigLoad(appConfig)) {
    appConfig = app_core::AppConfig::defaults();
    if (!appConfigSave(appConfig)) {
      Serial.println("Warning: app configuration could not be persisted");
    }
  }
  if (!meteo_settings::begin()) {
    Serial.println("Warning: Meteo settings initialization failed");
  }
  if (!network_host::begin()) {
    Serial.println("Warning: network host initialization failed");
  }
  if (!web_host::begin(
          loadClockConfigForWeb, saveClockConfigFromWeb, updateClockWebStatus,
          loadSunTransitionTimes, requestDayNightRefresh, loadDayNightStatus,
          setDisplayForcedOff, displayIsForcedOff,
          beginConfigurationStorageWrite, endConfigurationStorageWrite,
          makeMeteoWebRoutes())) {
    Serial.println("Warning: web host initialization failed");
  }

  I2C_Init();
  Set_EXIOS(0x0C);
  TCA9554PWR_Init(0x70);
  LCD_Init();
  Set_Backlight(clockConfig.dayBrightness);

  if (!displayHostBegin(onTouchSample)) halt("display host init failed");
  meteo_settings::setStorageCallbacks(beginConfigurationStorageWrite,
                                      endConfigurationStorageWrite);
  displayHostSetBrightness(clockConfig.dayBrightness);
  if (!screenManager.add(clockScreen) || !screenManager.add(radarScreen) ||
      !screenManager.add(forecastScreen) || !screenManager.add(planesScreen)) {
    halt("screen registration failed");
  }
  if (!screenManager.begin()) halt("screen init failed");
  updateClockWebStatus(web_host::active());
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
  meteo_settings::loop();

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
    const bool displaySuspended = beginConfigurationStorageWrite();
    if (displaySuspended && clockConfigSave(clockConfig)) {
      clockDataService.applyConfig(clockConfig);
      if (!web_host::setMode(
              static_cast<web_host::Mode>(requestedWebMode))) {
        Serial.println("Warning: web mode could not be persisted");
      }
      displayHostRequestFullRedraw();
    } else {
      Serial.println("Warning: clock configuration could not be persisted");
    }
    if (displaySuspended && !endConfigurationStorageWrite()) {
      Serial.println("Warning: unbalanced configuration storage transaction");
    }
  }

  web_host::loop();
  applyPendingWebConfiguration(millis());
  applyPendingMeteoWebWork();

  if (meteoRestartPending &&
      static_cast<int32_t>(millis() - meteoRestartAt) >= 0) {
    Serial.println("Restarting to apply Meteo location/source configuration");
    delay(50);
    ESP.restart();
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

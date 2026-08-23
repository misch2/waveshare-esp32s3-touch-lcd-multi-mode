#include "AppConfigStore.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include <cstring>

namespace {
constexpr char kNamespace[] = "multi-mode";
constexpr char kKey[] = "app-config";
constexpr size_t kJsonCapacity = 2048;
}  // namespace

bool appConfigLoad(app_core::AppConfig& config) {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) return false;
  const String json = preferences.getString(kKey, "");
  preferences.end();
  if (json.isEmpty() || json.length() > kJsonCapacity) return false;

  JsonDocument document;
  if (deserializeJson(document, json) != DeserializationError::Ok) return false;

  app_core::AppConfig loaded{};
  loaded.schemaVersion = document["schemaVersion"] | 0;
  JsonObjectConst navigation = document["navigation"];
  loaded.autoRotateSeconds = navigation["autoRotateSeconds"] | 0;
  JsonArrayConst screens = navigation["screens"];
  for (JsonObjectConst screen : screens) {
    if (loaded.screenCount >= app_core::AppConfig::kMaxScreens) break;
    const char* id = screen["id"] | "";
    std::strncpy(loaded.screens[loaded.screenCount].id, id,
                 app_core::AppConfig::kMaxScreenIdLength);
    loaded.screens[loaded.screenCount]
        .id[app_core::AppConfig::kMaxScreenIdLength] = '\0';
    loaded.screens[loaded.screenCount].enabled =
        (screen["enabled"] | false) ? 1 : 0;
    ++loaded.screenCount;
  }
  loaded.normalize();
  if (!loaded.validate()) return false;
  config = loaded;
  return true;
}

bool appConfigSave(const app_core::AppConfig& config) {
  if (!config.validate()) return false;

  JsonDocument document;
  document["schemaVersion"] = config.schemaVersion;
  JsonObject navigation = document["navigation"].to<JsonObject>();
  navigation["autoRotateSeconds"] = config.autoRotateSeconds;
  JsonArray screens = navigation["screens"].to<JsonArray>();
  for (uint8_t i = 0; i < config.screenCount; ++i) {
    JsonObject screen = screens.add<JsonObject>();
    screen["id"] = config.screens[i].id;
    screen["enabled"] = config.screens[i].enabled != 0;
  }

  String json;
  if (serializeJson(document, json) == 0 || json.length() > kJsonCapacity) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) return false;
  const bool saved = preferences.putString(kKey, json) == json.length();
  preferences.end();
  return saved;
}


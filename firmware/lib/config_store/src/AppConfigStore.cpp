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
  const bool migrated = loaded.normalize();
  if (!loaded.validate()) return false;
  config = loaded;
  // Schema 2 deliberately removed timed screen rotation. Rewrite older host
  // JSON once so the obsolete setting cannot reappear after a later update.
  if (migrated) appConfigSave(config);
  return true;
}

bool appConfigSave(const app_core::AppConfig& config) {
  if (!config.validate()) return false;

  JsonDocument document;
  document["schemaVersion"] = config.schemaVersion;
  JsonObject navigation = document["navigation"].to<JsonObject>();
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

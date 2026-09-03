#include <ArduinoJson.h>
#include <cassert>
#include <cstdio>
#include <cstring>

#include "CombinedConfigCodec.h"

int main() {
  ClockConfig config;
  clockConfigApplyDefaults(config);
  std::strcpy(config.homeAssistantUrl, "https://ha.example.test");
  std::strcpy(config.homeAssistantToken, "test-ha-secret");
  std::strcpy(config.tmepExportId, "12345");
  std::strcpy(config.tmepExportKey, "test-tmep-secret");
  config.tmepSlots[0].enabled = true;
  std::strcpy(config.tmepSlots[0].sensorId, "987");
  std::strcpy(config.tmepSlots[0].field, "teplota");
  std::strcpy(config.tmepSlots[0].unit, "C");
  config.tmepSlots[0].decimals = 2;
  config.leftSide.color = 0x123456;
  config.leftValueColorScale.points[0].color = 0x123456;
  ClockAppearanceConfig appearance;
  appearance.style = CLOCK_STYLE_ANALOG;
  appearance.analogDateColor = 0x654321;
  constexpr char meteo[] = R"({"lat":49.1,"lon":16.6,"hasLoc":true,"lang":0,"metric":true,"radarSrc":0,"topBearing":0,"altMin":0,"altMax":60000,"onlyCallsign":false,"squawkAlert":false,"watch":""})";
  char json[32768], detail[256];
  const auto length = combined_config::writeExport(
      app_core::AppConfig::defaults(), config, appearance, meteo,
      sizeof(meteo) - 1, json, sizeof(json));
  assert(length > 0);
  assert(std::strstr(json, "test-ha-secret") == nullptr);
  assert(std::strstr(json, "test-tmep-secret") == nullptr);
  assert(std::strstr(json, "tmepExportId") == nullptr);
  assert(std::strstr(json, "automaticRadarRotation") == nullptr);
  combined_config::ImportBundle result;
  assert(combined_config::parseImport(json, length, config, result, detail, sizeof(detail)));
  assert(result.clockAppearance.style == CLOCK_STYLE_ANALOG);
  assert(result.clockAppearance.analogDateColor == 0x654321);
  assert(result.clockConfig.tmepSlots[0].decimals == 2);
  assert(std::strcmp(result.clockConfig.tmepExportKey, "test-tmep-secret") == 0);
  assert(std::strcmp(result.clockConfig.homeAssistantToken, "test-ha-secret") == 0);
  assert(!result.clockConfig.automaticRadarRotation);
  ClockConfig freshDevice;
  clockConfigApplyDefaults(freshDevice);
  assert(combined_config::parseImport(json, length, freshDevice, result, detail, sizeof(detail)));
  assert(result.clockConfig.tmepSlots[0].enabled);
  assert(result.clockConfig.tmepExportKey[0] == '\0');
  assert(result.clockConfig.homeAssistantToken[0] == '\0');

  JsonDocument document;
  assert(!deserializeJson(document, json, length));
  document["modules"]["clock"]["config"]["tmepSlots"][0]["decimals"] = 3;
  auto changedLength = serializeJson(document, json, sizeof(json));
  assert(!combined_config::parseImport(json, changedLength, config, result, detail, sizeof(detail)));
  document["modules"]["clock"]["config"]["tmepSlots"][0]["decimals"] = 2;
  document["modules"]["clock"]["appearance"] = nullptr;
  changedLength = serializeJson(document, json, sizeof(json));
  assert(!combined_config::parseImport(json, changedLength, config, result, detail, sizeof(detail)));

  // A real v1 backup has only the schema-20 config fields and no appearance.
  document["schemaVersion"] = 1;
  auto clock = document["modules"]["clock"].as<JsonObject>();
  clock["schemaVersion"] = 20;
  clock.remove("appearance");
  auto settings = clock["config"].as<JsonObject>();
  for (const char* key : {"language", "openMeteoCountry", "leftValue", "rightValue",
                          "leftValueColorScale", "rightValueColorScale", "tmepSlots"})
    settings.remove(key);
  changedLength = serializeJson(document, json, sizeof(json));
  assert(combined_config::parseImport(json, changedLength, config, result, detail, sizeof(detail)));
  assert(result.clockConfig.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(result.clockAppearance.style == CLOCK_STYLE_DIGITAL);
  assert(result.clockConfig.leftValueColorScale.points[0].color == 0x123456);
  assert(!result.clockConfig.tmepSlots[0].enabled);
  assert(result.clockAppearance.monochromeWeatherIconColor == config.openMeteoSlots[0].color);
  assert(result.clockAppearance.analogDateFormat == config.dateFormat);
  assert(result.clockAppearance.analogDateColor == config.dateColor);
  assert(std::strcmp(result.clockConfig.tmepExportKey, "test-tmep-secret") == 0);
  std::puts("Combined config codec tests passed");
}

#include "CombinedConfigCodec.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define COMBINED_CONFIG_HAS_HEAP_CAPS 1
#else
#include <cstdlib>
#define COMBINED_CONFIG_HAS_HEAP_CAPS 0
#endif

namespace combined_config {
namespace {

class PsramAllocator final : public Allocator {
 public:
  void* allocate(size_t size) override {
#if COMBINED_CONFIG_HAS_HEAP_CAPS
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return std::malloc(size);
#endif
  }

  void deallocate(void* pointer) override {
#if COMBINED_CONFIG_HAS_HEAP_CAPS
    heap_caps_free(pointer);
#else
    std::free(pointer);
#endif
  }

  void* reallocate(void* pointer, size_t size) override {
#if COMBINED_CONFIG_HAS_HEAP_CAPS
    return heap_caps_realloc(pointer, size,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return std::realloc(pointer, size);
#endif
  }
};

PsramAllocator& allocator() {
  static PsramAllocator instance;
  return instance;
}

bool fail(char* detail, size_t capacity, const char* message) {
  if (detail != nullptr && capacity != 0) {
    std::snprintf(detail, capacity, "%s", message == nullptr ? "Chyba." : message);
    detail[capacity - 1] = '\0';
  }
  return false;
}

void clearDetail(char* detail, size_t capacity) {
  if (detail != nullptr && capacity != 0) detail[0] = '\0';
}

bool isNumber(JsonVariantConst value) {
  return value.is<JsonInteger>() || value.is<JsonFloat>();
}

bool readInteger(JsonObjectConst object, const char* key, int64_t minimum,
                 int64_t maximum, int64_t& output, char* detail,
                 size_t detailCapacity) {
  const JsonVariantConst value = object[key];
  if (value.isNull()) return true;
  if (!value.is<JsonInteger>())
    return fail(detail, detailCapacity, "Číselná hodnota má chybný typ.");
  const int64_t candidate = static_cast<int64_t>(value.as<JsonInteger>());
  if (candidate < minimum || candidate > maximum)
    return fail(detail, detailCapacity, "Číselná hodnota je mimo rozsah.");
  output = candidate;
  return true;
}

bool requireInteger(JsonObjectConst object, const char* key, int64_t minimum,
                    int64_t maximum, int64_t& output, char* detail,
                    size_t detailCapacity) {
  if (!object.containsKey(key))
    return fail(detail, detailCapacity, "V záloze chybí číselné pole.");
  return readInteger(object, key, minimum, maximum, output, detail,
                     detailCapacity);
}

bool readNumber(JsonObjectConst object, const char* key, double minimum,
                double maximum, double& output, char* detail,
                size_t detailCapacity) {
  const JsonVariantConst value = object[key];
  if (value.isNull()) return true;
  if (!isNumber(value))
    return fail(detail, detailCapacity, "Číselná hodnota má chybný typ.");
  const double candidate = value.as<double>();
  if (!std::isfinite(candidate) || candidate < minimum || candidate > maximum)
    return fail(detail, detailCapacity, "Číselná hodnota je mimo rozsah.");
  output = candidate;
  return true;
}

bool requireNumber(JsonObjectConst object, const char* key, double minimum,
                   double maximum, double& output, char* detail,
                   size_t detailCapacity) {
  if (!object.containsKey(key))
    return fail(detail, detailCapacity, "V záloze chybí číselné pole.");
  return readNumber(object, key, minimum, maximum, output, detail,
                    detailCapacity);
}

bool readBoolean(JsonObjectConst object, const char* key, bool& output,
                 char* detail, size_t detailCapacity) {
  const JsonVariantConst value = object[key];
  if (value.isNull()) return true;
  if (!value.is<bool>())
    return fail(detail, detailCapacity, "Logická hodnota má chybný typ.");
  output = value.as<bool>();
  return true;
}

bool requireBoolean(JsonObjectConst object, const char* key, bool& output,
                    char* detail, size_t detailCapacity) {
  if (!object.containsKey(key))
    return fail(detail, detailCapacity, "V záloze chybí logické pole.");
  return readBoolean(object, key, output, detail, detailCapacity);
}

bool copyString(JsonObjectConst object, const char* key, char* output,
                size_t outputCapacity, char* detail, size_t detailCapacity) {
  const JsonVariantConst value = object[key];
  if (value.isNull()) return true;
  if (!value.is<const char*>())
    return fail(detail, detailCapacity, "Textová hodnota má chybný typ.");
  const char* source = value.as<const char*>();
  if (source == nullptr || std::strlen(source) >= outputCapacity)
    return fail(detail, detailCapacity, "Textová hodnota je příliš dlouhá.");
  std::strcpy(output, source);
  return true;
}

bool requireString(JsonObjectConst object, const char* key, char* output,
                   size_t outputCapacity, char* detail,
                   size_t detailCapacity) {
  if (!object.containsKey(key))
    return fail(detail, detailCapacity, "V záloze chybí textové pole.");
  return copyString(object, key, output, outputCapacity, detail,
                    detailCapacity);
}

bool readObject(JsonObjectConst parent, const char* key, JsonObjectConst& output,
                char* detail, size_t detailCapacity) {
  const JsonVariantConst value = parent[key];
  if (value.isNull()) {
    fail(detail, detailCapacity, "V záloze chybí objekt nastavení.");
    return false;
  }
  if (!value.is<JsonObjectConst>()) {
    fail(detail, detailCapacity, "Objekt nastavení má chybný typ.");
    return false;
  }
  output = value.as<JsonObjectConst>();
  return true;
}

bool forbiddenKey(const char* key);

bool stringInList(const char* value, const char* const* allowed,
                  size_t allowedCount) {
  if (value == nullptr) return false;
  for (size_t index = 0; index < allowedCount; ++index) {
    if (std::strcmp(value, allowed[index]) == 0) return true;
  }
  return false;
}

bool rejectUnknown(JsonObjectConst object, const char* const* allowed,
                   size_t allowedCount, char* detail, size_t detailCapacity) {
  for (JsonPairConst pair : object) {
    const char* key = pair.key().c_str();
    if (key != nullptr && forbiddenKey(key))
      return fail(detail, detailCapacity,
                  "Záloha obsahuje citlivé nebo zakázané pole.");
    bool known = false;
    for (size_t index = 0; index < allowedCount; ++index) {
      if (key != nullptr && std::strcmp(key, allowed[index]) == 0) {
        known = true;
        break;
      }
    }
    if (!known)
      return fail(detail, detailCapacity, "Záloha obsahuje neznámé pole.");
  }
  return true;
}

bool forbiddenKey(const char* key) {
  static constexpr const char* keys[] = {
      "homeAssistantToken", "haToken",       "token",       "controlSecret",
      "password",            "newPassword",  "webPassword", "webPasswordHash",
      "ssid",                "wpass",        "wifiPassword", "adminPassword",
      "automaticFirmwareUpdate", "firmwareUpdatesEnabled", "firmwareVersion",
      "updateUrl",           "webMode",      "tokenConfigured", "hasPassword",
  };
  for (const char* candidate : keys) {
    if (std::strcmp(key, candidate) == 0) return true;
  }
  return false;
}

template <typename T>
void setInteger(JsonObject object, const char* key, T value) {
  object[key] = static_cast<JsonInteger>(value);
}

void putSide(JsonObject parent, const char* key, const ClockSideConfig& side) {
  JsonObject object = parent[key].to<JsonObject>();
  object["name"] = side.name;
  object["temperatureEntityId"] = side.temperatureEntityId;
  object["icon"] = side.icon;
  setInteger(object, "color", side.color);
}

void putMetric(JsonObject parent, const char* key, const ClockMetricConfig& metric) {
  JsonObject object = parent[key].to<JsonObject>();
  object["custom"] = metric.custom;
  object["preset"] = metric.preset;
  object["name"] = metric.name;
  object["entityId"] = metric.entityId;
  object["suffix"] = metric.suffix;
  setInteger(object, "decimals", metric.decimals);
}

void putScale(JsonObject parent, const char* key,
              const ClockMetricColorScale& scale) {
  JsonObject object = parent[key].to<JsonObject>();
  setInteger(object, "count", scale.count);
  JsonArray points = object["points"].to<JsonArray>();
  for (uint8_t index = 0; index < scale.count &&
                             index < CLOCK_METRIC_COLOR_POINT_COUNT;
       ++index) {
    JsonObject point = points.add<JsonObject>();
    point["value"] = scale.points[index].value;
    setInteger(point, "color", scale.points[index].color);
  }
}

void putClock(JsonObject parent, const ClockConfig& config) {
  JsonObject object = parent["config"].to<JsonObject>();
  object["homeAssistantUrl"] = config.homeAssistantUrl;
  object["weatherEntityId"] = config.weatherEntityId;
  object["sunEntityId"] = config.sunEntityId;
  putSide(object, "leftSide", config.leftSide);
  putSide(object, "rightSide", config.rightSide);
  putMetric(object, "metricA", config.metricA);
  putMetric(object, "metricB", config.metricB);
  putScale(object, "metricAColorScale", config.metricAColorScale);
  putScale(object, "metricBColorScale", config.metricBColorScale);
  setInteger(object, "timeColor", config.timeColor);
  setInteger(object, "dateColor", config.dateColor);
  setInteger(object, "leftWeatherIconColor", config.leftWeatherIconColor);
  setInteger(object, "rightWeatherIconColor", config.rightWeatherIconColor);
  object["animatedWeatherIcons"] = config.animatedWeatherIcons;
  setInteger(object, "weatherIconStyle", config.weatherIconStyle);
  setInteger(object, "dayBrightness", config.dayBrightness);
  setInteger(object, "nightBrightness", config.nightBrightness);
  object["automaticDayNight"] = config.automaticDayNight;
  setInteger(object, "sunsetOffsetMinutes", config.sunsetOffsetMinutes);
  object["secondRingEnabled"] = config.secondRingEnabled;
  setInteger(object, "secondEffect", config.secondEffect);
  setInteger(object, "sunriseOffsetMinutes", config.sunriseOffsetMinutes);
  setInteger(object, "secondRingBackgroundColor", config.secondRingBackgroundColor);
  setInteger(object, "secondRingBackgroundBrightness",
             config.secondRingBackgroundBrightness);
  setInteger(object, "secondRingBackgroundDotSize",
             config.secondRingBackgroundDotSize);
  setInteger(object, "secondDotSize", config.secondDotSize);
  setInteger(object, "secondDotColor", config.secondDotColor);
  setInteger(object, "secondDotBrightness", config.secondDotBrightness);
  object["dayNightLightEntityId"] = config.dayNightLightEntityId;
  setInteger(object, "nightVisualMode", config.nightVisualMode);
  setInteger(object, "timeFont", config.timeFont);
  setInteger(object, "dataSource", config.dataSource);
  object["openMeteoCity"] = config.openMeteoCity;
  object["openMeteoLatitude"] = config.openMeteoLatitude;
  object["openMeteoLongitude"] = config.openMeteoLongitude;
  JsonArray slots = object["openMeteoSlots"].to<JsonArray>();
  for (const ClockOpenMeteoSlotConfig& slot : config.openMeteoSlots) {
    JsonObject item = slots.add<JsonObject>();
    item["value"] = slot.value;
    item["name"] = slot.name;
    setInteger(item, "color", slot.color);
  }
  setInteger(object, "timeColonEffect", config.timeColonEffect);
  object["showLeadingHourZero"] = config.showLeadingHourZero;
  setInteger(object, "dateFormat", config.dateFormat);
}

void putMeteoValue(JsonObject target, JsonObjectConst source, const char* key) {
  const JsonVariantConst value = source[key];
  if (!value.isNull()) target[key] = value;
}

bool validateMeteo(JsonObjectConst source, char* detail, size_t detailCapacity) {
  static constexpr const char* allowed[] = {
      "lat", "lon", "hasLoc", "lang", "metric", "radarSrc",
      "topBearing", "altMin", "altMax", "onlyCallsign", "squawkAlert",
      "watch",
  };
  if (!rejectUnknown(source, allowed, sizeof(allowed) / sizeof(allowed[0]),
                     detail, detailCapacity))
    return false;
  double value = 0.0;
  if (!requireNumber(source, "lat", -90.0, 90.0, value, detail,
                     detailCapacity) ||
      !requireNumber(source, "lon", -180.0, 180.0, value, detail,
                     detailCapacity))
    return false;
  int64_t integer = 0;
  if (!requireInteger(source, "lang", 0, 1, integer, detail, detailCapacity) ||
      !requireInteger(source, "radarSrc", 0, 1, integer, detail,
                      detailCapacity) ||
      !requireInteger(source, "topBearing", 0, 359, integer, detail,
                      detailCapacity) ||
      !requireInteger(source, "altMin", 0, 60000, integer, detail,
                      detailCapacity) ||
      !requireInteger(source, "altMax", 0, 60000, integer, detail,
                      detailCapacity))
    return false;
  if (source["altMin"].as<JsonInteger>() >= source["altMax"].as<JsonInteger>())
    return fail(detail, detailCapacity, "Rozsah výšek Meteo je neplatný.");

  for (const char* key : {"hasLoc", "metric", "onlyCallsign", "squawkAlert"}) {
    bool boolean = false;
    if (!requireBoolean(source, key, boolean, detail, detailCapacity))
      return false;
  }
  char watch[10] = {};
  return requireString(source, "watch", watch, sizeof(watch), detail,
                       detailCapacity);
}

bool putMeteo(JsonObject target, JsonObjectConst source, char* detail,
              size_t detailCapacity) {
  if (!validateMeteo(source, detail, detailCapacity)) return false;
  static constexpr const char* keys[] = {
      "lat", "lon", "hasLoc", "lang", "metric", "radarSrc", "topBearing",
      "altMin", "altMax", "onlyCallsign", "squawkAlert", "watch",
  };
  for (const char* key : keys) putMeteoValue(target, source, key);
  return true;
}

bool parseSide(JsonObjectConst object, ClockSideConfig& output, char* detail,
               size_t detailCapacity) {
  static constexpr const char* allowed[] = {"name", "temperatureEntityId",
                                             "icon", "color"};
  if (!rejectUnknown(object, allowed, sizeof(allowed) / sizeof(allowed[0]),
                     detail, detailCapacity) ||
      !requireString(object, "name", output.name, sizeof(output.name), detail,
                      detailCapacity) ||
      !requireString(object, "temperatureEntityId", output.temperatureEntityId,
                      sizeof(output.temperatureEntityId), detail,
                      detailCapacity) ||
      !requireString(object, "icon", output.icon, sizeof(output.icon), detail,
                     detailCapacity))
    return false;
  int64_t integer = 0;
  if (!requireInteger(object, "color", 0, 0xFFFFFF, integer, detail,
                      detailCapacity))
    return false;
  output.color = static_cast<uint32_t>(integer);
  static constexpr const char* icons[] = {
      "weather", "home", "living-room", "bedroom", "kitchen", "none"};
  if (!stringInList(output.icon, icons, sizeof(icons) / sizeof(icons[0])))
    return fail(detail, detailCapacity, "Ikona místnosti hodin není platná.");
  return true;
}

bool parseMetric(JsonObjectConst object, ClockMetricConfig& output, char* detail,
                 size_t detailCapacity) {
  static constexpr const char* allowed[] = {"custom", "preset", "name",
                                             "entityId", "suffix", "decimals"};
  if (!rejectUnknown(object, allowed, sizeof(allowed) / sizeof(allowed[0]),
                     detail, detailCapacity) ||
      !requireBoolean(object, "custom", output.custom, detail,
                      detailCapacity) ||
      !requireString(object, "preset", output.preset, sizeof(output.preset),
                      detail, detailCapacity) ||
      !requireString(object, "name", output.name, sizeof(output.name), detail,
                      detailCapacity) ||
      !requireString(object, "entityId", output.entityId, sizeof(output.entityId),
                      detail, detailCapacity) ||
      !requireString(object, "suffix", output.suffix, sizeof(output.suffix),
                      detail, detailCapacity))
    return false;
  int64_t integer = 0;
  if (!requireInteger(object, "decimals", 0, 2, integer, detail,
                      detailCapacity))
    return false;
  output.decimals = static_cast<uint8_t>(integer);
  return true;
}

bool parseScale(JsonObjectConst object, ClockMetricColorScale& output,
                char* detail, size_t detailCapacity) {
  static constexpr const char* allowed[] = {"count", "points"};
  if (!rejectUnknown(object, allowed, sizeof(allowed) / sizeof(allowed[0]),
                     detail, detailCapacity))
    return false;
  int64_t count = 0;
  if (!requireInteger(object, "count", 1, CLOCK_METRIC_COLOR_POINT_COUNT,
                      count, detail, detailCapacity))
    return false;
  const JsonVariantConst pointsValue = object["points"];
  if (!pointsValue.is<JsonArrayConst>() ||
      pointsValue.as<JsonArrayConst>().size() != static_cast<size_t>(count))
    return fail(detail, detailCapacity, "Barevná škála hodin má chybný počet bodů.");
  output.count = static_cast<uint8_t>(count);
  uint8_t index = 0;
  for (JsonObjectConst point : pointsValue.as<JsonArrayConst>()) {
    static constexpr const char* pointAllowed[] = {"value", "color"};
    if (!rejectUnknown(point, pointAllowed,
                       sizeof(pointAllowed) / sizeof(pointAllowed[0]), detail,
                       detailCapacity))
      return false;
    double numeric = 0.0;
    if (!requireNumber(point, "value", -1.0e9, 1.0e9, numeric, detail,
                       detailCapacity))
      return false;
    int64_t color = 0;
    if (!requireInteger(point, "color", 0, 0xFFFFFF, color, detail,
                        detailCapacity))
      return false;
    output.points[index].value = static_cast<float>(numeric);
    output.points[index].color = static_cast<uint32_t>(color);
    ++index;
  }
  return true;
}

bool parseClock(JsonObjectConst object, ClockConfig& output, char* detail,
                size_t detailCapacity) {
  static constexpr const char* allowed[] = {
      "homeAssistantUrl", "weatherEntityId", "sunEntityId", "leftSide",
      "rightSide", "metricA", "metricB", "metricAColorScale",
      "metricBColorScale", "timeColor", "dateColor", "leftWeatherIconColor",
      "rightWeatherIconColor", "animatedWeatherIcons", "weatherIconStyle",
      "dayBrightness", "nightBrightness", "automaticDayNight",
      "sunsetOffsetMinutes", "secondRingEnabled", "secondEffect",
      "sunriseOffsetMinutes", "secondRingBackgroundColor",
      "secondRingBackgroundBrightness", "secondRingBackgroundDotSize",
      "secondDotSize", "secondDotColor", "secondDotBrightness",
      "dayNightLightEntityId", "nightVisualMode", "timeFont", "dataSource",
      "openMeteoCity", "openMeteoLatitude", "openMeteoLongitude",
      "openMeteoSlots", "timeColonEffect", "showLeadingHourZero", "dateFormat",
  };
  if (!rejectUnknown(object, allowed, sizeof(allowed) / sizeof(allowed[0]),
                     detail, detailCapacity) ||
      !requireString(object, "homeAssistantUrl", output.homeAssistantUrl,
                     sizeof(output.homeAssistantUrl), detail, detailCapacity) ||
      !requireString(object, "weatherEntityId", output.weatherEntityId,
                     sizeof(output.weatherEntityId), detail, detailCapacity) ||
      !requireString(object, "sunEntityId", output.sunEntityId,
                     sizeof(output.sunEntityId), detail, detailCapacity) ||
      !requireString(object, "dayNightLightEntityId", output.dayNightLightEntityId,
                     sizeof(output.dayNightLightEntityId), detail,
                     detailCapacity) ||
      !requireString(object, "openMeteoCity", output.openMeteoCity,
                     sizeof(output.openMeteoCity), detail, detailCapacity))
    return false;
  const bool validHomeAssistantUrl =
      output.homeAssistantUrl[0] == '\0' ||
      std::strncmp(output.homeAssistantUrl, "http://", 7) == 0 ||
      std::strncmp(output.homeAssistantUrl, "https://", 8) == 0;
  if (!validHomeAssistantUrl)
    return fail(detail, detailCapacity,
                "Adresa Home Assistantu není platná.");
  if (output.openMeteoCity[0] == '\0')
    return fail(detail, detailCapacity, "Město Open-Meteo nesmí být prázdné.");

  JsonObjectConst nested;
  if (!readObject(object, "leftSide", nested, detail, detailCapacity) ||
      !parseSide(nested, output.leftSide, detail, detailCapacity) ||
      !readObject(object, "rightSide", nested, detail, detailCapacity) ||
      !parseSide(nested, output.rightSide, detail, detailCapacity) ||
      !readObject(object, "metricA", nested, detail, detailCapacity) ||
      !parseMetric(nested, output.metricA, detail, detailCapacity) ||
      !readObject(object, "metricB", nested, detail, detailCapacity) ||
      !parseMetric(nested, output.metricB, detail, detailCapacity) ||
      !readObject(object, "metricAColorScale", nested, detail, detailCapacity) ||
      !parseScale(nested, output.metricAColorScale, detail, detailCapacity) ||
      !readObject(object, "metricBColorScale", nested, detail, detailCapacity) ||
      !parseScale(nested, output.metricBColorScale, detail, detailCapacity))
    return false;

  int64_t integer = 0;
  const struct IntegerField {
    const char* key;
    int64_t minimum;
    int64_t maximum;
    uint8_t* target;
  } small[] = {
      {"weatherIconStyle", 0, CLOCK_WEATHER_ICON_STYLE_LINE, &output.weatherIconStyle},
      {"dayBrightness", 1, 100, &output.dayBrightness},
      {"nightBrightness", 1, 100, &output.nightBrightness},
      {"secondEffect", 0, CLOCK_SECOND_EFFECT_COMET, &output.secondEffect},
      {"secondRingBackgroundBrightness", 0, 255, &output.secondRingBackgroundBrightness},
      {"secondRingBackgroundDotSize", 1, 10, &output.secondRingBackgroundDotSize},
      {"secondDotSize", 1, 10, &output.secondDotSize},
      {"secondDotBrightness", 0, 255, &output.secondDotBrightness},
      {"nightVisualMode", 0, CLOCK_NIGHT_VISUAL_BRIGHTNESS_ONLY, &output.nightVisualMode},
      {"timeFont", 0, CLOCK_TIME_FONT_DOTO, &output.timeFont},
      {"dataSource", 0, CLOCK_DATA_SOURCE_HOME_ASSISTANT, &output.dataSource},
      {"timeColonEffect", 0, CLOCK_TIME_COLON_FADE, &output.timeColonEffect},
      {"dateFormat", 0, CLOCK_DATE_FORMAT_HIDDEN, &output.dateFormat},
  };
  for (const IntegerField& field : small) {
    if (!requireInteger(object, field.key, field.minimum, field.maximum,
                        integer, detail, detailCapacity))
      return false;
    *field.target = static_cast<uint8_t>(integer);
  }
  const struct ColorField {
    const char* key;
    uint32_t* target;
  } colors[] = {
      {"timeColor", &output.timeColor},
      {"dateColor", &output.dateColor},
      {"leftWeatherIconColor", &output.leftWeatherIconColor},
      {"rightWeatherIconColor", &output.rightWeatherIconColor},
      {"secondRingBackgroundColor", &output.secondRingBackgroundColor},
      {"secondDotColor", &output.secondDotColor},
  };
  for (const ColorField& field : colors) {
    if (!requireInteger(object, field.key, 0, 0xFFFFFF, integer, detail,
                        detailCapacity))
      return false;
    *field.target = static_cast<uint32_t>(integer);
  }
  if (!requireInteger(object, "sunsetOffsetMinutes", -60, 60, integer, detail,
                      detailCapacity))
    return false;
  output.sunsetOffsetMinutes = static_cast<int8_t>(integer);
  if (!requireInteger(object, "sunriseOffsetMinutes", -60, 60, integer,
                      detail, detailCapacity))
    return false;
  output.sunriseOffsetMinutes = static_cast<int8_t>(integer);
  if (output.sunriseOffsetMinutes % 15 != 0 ||
      output.sunsetOffsetMinutes % 15 != 0)
    return fail(detail, detailCapacity,
                "Posuny východu a západu slunce musí být po 15 minutách.");

  if (!requireBoolean(object, "animatedWeatherIcons", output.animatedWeatherIcons,
                      detail, detailCapacity) ||
      !requireBoolean(object, "automaticDayNight", output.automaticDayNight,
                      detail, detailCapacity) ||
      !requireBoolean(object, "secondRingEnabled", output.secondRingEnabled,
                      detail, detailCapacity) ||
      !requireBoolean(object, "showLeadingHourZero", output.showLeadingHourZero,
                      detail, detailCapacity))
    return false;

  double latitude = output.openMeteoLatitude;
  double longitude = output.openMeteoLongitude;
  if (!requireNumber(object, "openMeteoLatitude", -90, 90, latitude, detail,
                     detailCapacity) ||
      !requireNumber(object, "openMeteoLongitude", -180, 180, longitude,
                     detail, detailCapacity))
    return false;
  output.openMeteoLatitude = static_cast<float>(latitude);
  output.openMeteoLongitude = static_cast<float>(longitude);

  const JsonVariantConst value = object["openMeteoSlots"];
  if (!value.is<JsonArrayConst>() || value.as<JsonArrayConst>().size() != 4)
    return fail(detail, detailCapacity, "Open-Meteo má chybný počet položek.");
  uint8_t index = 0;
  for (JsonObjectConst slot : value.as<JsonArrayConst>()) {
    static constexpr const char* slotAllowed[] = {"value", "name", "color"};
    if (!rejectUnknown(slot, slotAllowed,
                       sizeof(slotAllowed) / sizeof(slotAllowed[0]), detail,
                       detailCapacity) ||
        !requireString(slot, "value", output.openMeteoSlots[index].value,
                       sizeof(output.openMeteoSlots[index].value), detail,
                       detailCapacity) ||
        !requireString(slot, "name", output.openMeteoSlots[index].name,
                       sizeof(output.openMeteoSlots[index].name), detail,
                       detailCapacity) ||
        !requireInteger(slot, "color", 0, 0xFFFFFF, integer, detail,
                        detailCapacity))
      return false;
    output.openMeteoSlots[index].color = static_cast<uint32_t>(integer);
    static constexpr const char* values[] = {
        "temperature_2m",       "apparent_temperature",
        "relative_humidity_2m", "pressure_msl",
        "surface_pressure",     "wind_speed_10m",
        "wind_gusts_10m",       "wind_direction_10m",
        "precipitation",        "rain",
        "showers",              "snowfall",
        "cloud_cover",          "uv_index",
    };
    if (!stringInList(output.openMeteoSlots[index].value, values,
                      sizeof(values) / sizeof(values[0])))
      return fail(detail, detailCapacity,
                  "Položka Open-Meteo není podporována.");
    ++index;
  }
  if (output.dataSource == CLOCK_DATA_SOURCE_HOME_ASSISTANT &&
      output.automaticDayNight && output.sunEntityId[0] == '\0')
    return fail(detail, detailCapacity,
                "Automatický režim hodin vyžaduje SUN entitu.");
  output.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
  output.automaticFirmwareUpdate = false;
  return true;
}

bool parseAppConfig(JsonObjectConst host, app_core::AppConfig& output,
                    char* detail, size_t detailCapacity) {
  static constexpr const char* hostAllowed[] = {"schemaVersion", "navigation"};
  if (!rejectUnknown(host, hostAllowed,
                     sizeof(hostAllowed) / sizeof(hostAllowed[0]), detail,
                     detailCapacity))
    return false;
  int64_t version = 0;
  if (!requireInteger(host, "schemaVersion", app_core::AppConfig::kSchemaVersion,
                      app_core::AppConfig::kSchemaVersion, version, detail,
                      detailCapacity))
    return false;
  JsonObjectConst navigation;
  if (!readObject(host, "navigation", navigation, detail, detailCapacity))
    return fail(detail, detailCapacity, "Záloha nemá navigaci obrazovek.");
  static constexpr const char* navigationAllowed[] = {"screens"};
  if (!rejectUnknown(navigation, navigationAllowed,
                     sizeof(navigationAllowed) / sizeof(navigationAllowed[0]),
                     detail, detailCapacity))
    return false;
  const JsonVariantConst screensValue = navigation["screens"];
  if (!screensValue.is<JsonArrayConst>() || screensValue.as<JsonArrayConst>().size() == 0 ||
      screensValue.as<JsonArrayConst>().size() > app_core::AppConfig::kMaxScreens)
    return fail(detail, detailCapacity, "Pořadí obrazovek v záloze je neplatné.");
  output = app_core::AppConfig{};
  output.schemaVersion = app_core::AppConfig::kSchemaVersion;
  uint8_t count = 0;
  bool builtInEnabled = false;
  for (JsonObjectConst screen : screensValue.as<JsonArrayConst>()) {
    static constexpr const char* screenAllowed[] = {"id", "enabled"};
    if (!rejectUnknown(screen, screenAllowed,
                       sizeof(screenAllowed) / sizeof(screenAllowed[0]), detail,
                       detailCapacity) ||
        !screen.containsKey("id") || !screen.containsKey("enabled") ||
        !screen["id"].is<const char*>() || !screen["enabled"].is<bool>() ||
        count >= app_core::AppConfig::kMaxScreens)
      return fail(detail, detailCapacity, "Obrazovka v záloze má chybný tvar.");
    const char* id = screen["id"].as<const char*>();
    if (id == nullptr || std::strlen(id) > app_core::AppConfig::kMaxScreenIdLength)
      return fail(detail, detailCapacity, "ID obrazovky v záloze je neplatné.");
    std::strcpy(output.screens[count].id, id);
    output.screens[count].enabled = screen["enabled"].as<bool>() ? 1 : 0;
    if (output.screens[count].enabled != 0 &&
        (std::strcmp(id, "clock.dashboard") == 0 ||
         std::strcmp(id, "meteo.radar") == 0 ||
         std::strcmp(id, "meteo.forecast") == 0 ||
         std::strcmp(id, "meteo.planes") == 0))
      builtInEnabled = true;
    ++count;
  }
  output.screenCount = count;
  if (!builtInEnabled)
    return fail(detail, detailCapacity, "Alespoň jedna vestavěná obrazovka musí být aktivní.");
  if (!output.validate())
    return fail(detail, detailCapacity, "Navigace obrazovek v záloze je neplatná.");
  output.normalize();
  if (!output.validate())
    return fail(detail, detailCapacity, "Navigace obrazovek v záloze je neplatná.");
  return true;
}

}  // namespace

std::size_t writeExport(const app_core::AppConfig& appConfig,
                        const ClockConfig& clockConfig, const char* meteoJson,
                        std::size_t meteoJsonLength, char* out,
                        std::size_t capacity) {
  if (out == nullptr || capacity == 0 || meteoJson == nullptr ||
      meteoJsonLength == 0 || !appConfig.validate())
    return 0;
  JsonDocument meteoSource(&allocator());
  if (deserializeJson(meteoSource, meteoJson, meteoJsonLength) !=
          DeserializationError::Ok ||
      !meteoSource.is<JsonObjectConst>())
    return 0;
  JsonObjectConst source = meteoSource.as<JsonObjectConst>();
  if (source["settings"].is<JsonObjectConst>())
    source = source["settings"].as<JsonObjectConst>();
  JsonDocument document(&allocator());
  JsonObject root = document.to<JsonObject>();
  root["format"] = kFormat;
  setInteger(root, "schemaVersion", kSchemaVersion);
  JsonObject host = root["host"].to<JsonObject>();
  setInteger(host, "schemaVersion", appConfig.schemaVersion);
  JsonObject navigation = host["navigation"].to<JsonObject>();
  JsonArray screens = navigation["screens"].to<JsonArray>();
  for (uint8_t index = 0; index < appConfig.screenCount; ++index) {
    JsonObject screen = screens.add<JsonObject>();
    screen["id"] = appConfig.screens[index].id;
    screen["enabled"] = appConfig.screens[index].enabled != 0;
  }
  JsonObject modules = root["modules"].to<JsonObject>();
  JsonObject clock = modules["clock"].to<JsonObject>();
  setInteger(clock, "schemaVersion", CLOCK_CONFIG_SCHEMA_VERSION);
  putClock(clock, clockConfig);
  JsonObject meteo = modules["meteo"].to<JsonObject>();
  setInteger(meteo, "schemaVersion", 1);
  JsonObject settings = meteo["settings"].to<JsonObject>();
  char ignored[64] = {};
  if (!putMeteo(settings, source, ignored, sizeof(ignored))) return 0;
  const std::size_t length = serializeJson(document, out, capacity);
  if (length == 0 || length >= capacity) return 0;
  out[length] = '\0';
  return length;
}

bool parseImport(const char* json, std::size_t length,
                 const ClockConfig& currentClock, ImportBundle& out,
                 char* detail, std::size_t detailCapacity) {
  clearDetail(detail, detailCapacity);
  if (json == nullptr || length == 0)
    return fail(detail, detailCapacity, "Záloha je prázdná.");
  JsonDocument document(&allocator());
  if (deserializeJson(document, json, length) != DeserializationError::Ok ||
      !document.is<JsonObjectConst>())
    return fail(detail, detailCapacity, "Záloha není platný JSON objekt.");
  JsonObjectConst root = document.as<JsonObjectConst>();
  static constexpr const char* rootAllowed[] = {"format", "schemaVersion",
                                                 "host", "modules"};
  if (!rejectUnknown(root, rootAllowed,
                     sizeof(rootAllowed) / sizeof(rootAllowed[0]), detail,
                     detailCapacity) ||
      !root["format"].is<const char*>() ||
      std::strcmp(root["format"].as<const char*>(), kFormat) != 0)
    return fail(detail, detailCapacity, "Neznámý formát zálohy.");
  int64_t version = 0;
  if (!requireInteger(root, "schemaVersion", kSchemaVersion, kSchemaVersion,
                      version, detail, detailCapacity))
    return false;
  JsonObjectConst host;
  JsonObjectConst modules;
  if (!readObject(root, "host", host, detail, detailCapacity) ||
      !readObject(root, "modules", modules, detail, detailCapacity))
    return fail(detail, detailCapacity, "Záloha nemá úplnou strukturu.");
  JsonObjectConst clock;
  JsonObjectConst meteo;
  if (!readObject(modules, "clock", clock, detail, detailCapacity) ||
      !readObject(modules, "meteo", meteo, detail, detailCapacity))
    return fail(detail, detailCapacity, "Záloha nemá oba moduly.");
  static constexpr const char* modulesAllowed[] = {"clock", "meteo"};
  if (!rejectUnknown(modules, modulesAllowed,
                     sizeof(modulesAllowed) / sizeof(modulesAllowed[0]), detail,
                     detailCapacity))
    return false;
  static constexpr const char* clockAllowed[] = {"schemaVersion", "config"};
  static constexpr const char* meteoAllowed[] = {"schemaVersion", "settings"};
  if (!rejectUnknown(clock, clockAllowed,
                     sizeof(clockAllowed) / sizeof(clockAllowed[0]), detail,
                     detailCapacity) ||
      !rejectUnknown(meteo, meteoAllowed,
                     sizeof(meteoAllowed) / sizeof(meteoAllowed[0]), detail,
                     detailCapacity) ||
      !requireInteger(clock, "schemaVersion", CLOCK_CONFIG_SCHEMA_VERSION,
                      CLOCK_CONFIG_SCHEMA_VERSION, version, detail,
                      detailCapacity) ||
      !requireInteger(meteo, "schemaVersion", 1, 1, version, detail,
                      detailCapacity))
    return false;
  JsonObjectConst clockConfigObject;
  JsonObjectConst meteoSettings;
  if (!readObject(clock, "config", clockConfigObject, detail, detailCapacity) ||
      !readObject(meteo, "settings", meteoSettings, detail, detailCapacity))
    return fail(detail, detailCapacity, "Záloha nemá konfiguraci modulu.");

  // ImportBundle is several kilobytes. Parse into the caller-owned buffer so
  // the Arduino loop task never needs a second complete copy on its small
  // stack. A failed parse may leave `out` partially populated, but the caller
  // applies it only when this function returns true.
  out.appConfig = app_core::AppConfig::defaults();
  clockConfigApplyDefaults(out.clockConfig);
  out.meteoJson[0] = '\0';
  out.meteoJsonLength = 0;
  out.meteoHasLocation = false;
  if (!parseAppConfig(host, out.appConfig, detail, detailCapacity) ||
      !parseClock(clockConfigObject, out.clockConfig, detail,
                  detailCapacity) ||
      !validateMeteo(meteoSettings, detail, detailCapacity))
    return false;
  out.meteoHasLocation = meteoSettings["hasLoc"].as<bool>();
  if (std::strcmp(out.clockConfig.homeAssistantUrl,
                  currentClock.homeAssistantUrl) == 0) {
    std::strcpy(out.clockConfig.homeAssistantToken,
                currentClock.homeAssistantToken);
  } else {
    out.clockConfig.homeAssistantToken[0] = '\0';
  }
  out.clockConfig.automaticFirmwareUpdate = false;
  JsonDocument meteoDocument(&allocator());
  JsonObject settings = meteoDocument.to<JsonObject>();
  char meteoDetail[64] = {};
  if (!putMeteo(settings, meteoSettings, meteoDetail, sizeof(meteoDetail)))
    return fail(detail, detailCapacity, meteoDetail);
  const std::size_t meteoLength =
      serializeJson(meteoDocument, out.meteoJson, sizeof(out.meteoJson));
  if (meteoLength == 0 || meteoLength >= sizeof(out.meteoJson))
    return fail(detail, detailCapacity, "Meteo konfigurace je příliš dlouhá.");
  out.meteoJson[meteoLength] = '\0';
  out.meteoJsonLength = meteoLength;
  return true;
}

}  // namespace combined_config

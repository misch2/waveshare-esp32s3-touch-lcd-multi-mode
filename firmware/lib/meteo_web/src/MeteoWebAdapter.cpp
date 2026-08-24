#include "MeteoWebAdapter.h"

#include <WebServer.h>

#include <ArduinoJson.h>
#include <Arduino.h>

#include <esp_heap_caps.h>

#include <ctype.h>
#include <string.h>

#ifdef BOOT_PIN
#undef BOOT_PIN
#endif
#include "Config.h"
#include "Lang.h"
#include "Net.h"
#include "NetworkFetchGate.h"
#include "WebPage.h"

namespace meteo_web {
namespace {

constexpr size_t kConfigBufferSize = 8192;
constexpr size_t kStatusBufferSize = 4096;
constexpr size_t kPageChunkSize = 512;
constexpr char kRemoteHintCz[] =
    "Rozsah se mění jen na obrazovkách Letadla a Meteoradar. Zásah "
    "pozastaví automatické střídání.";
constexpr char kRemoteHintEn[] =
    "The range only applies to the Aircraft and Weather screens. Using "
    "this pauses the automatic cycling.";
constexpr char kRestartHintCz[] =
    "Změna obrazovek, zdroje radaru nebo polohy potřebuje restart, takže "
    "se ukládá až tlačítkem dole.";
constexpr char kRestartHintEn[] =
    "Changing the screens, the radar source or the location needs a "
    "restart, so those are saved with the button below.";
constexpr char kScreensHintCz[] =
    "Vypnuté obrazovky se přeskakují. Nastavení je dostupné vždy.";
constexpr char kScreensHintEn[] =
    "Disabled screens are skipped. Settings is always reachable.";

app_core::MeteoWebRoutes s_routes;
WebServer* s_server = nullptr;

String s_configGetPath;
String s_configPostPath;
String s_statusPath;
String s_screenPath;
String s_rangePath;
String s_geocodePath;
String s_pagePath;
String s_pagePathWithoutSlash;

char* s_configBuffer = nullptr;
char* s_statusBuffer = nullptr;

void sendJson(int status, const char* body) {
  s_server->send(status, "application/json", body ? body : "{}");
}

bool allowed() {
  // The host callback owns the denial response.  In particular, the clock
  // authentication policy may send 401/423 and a second response here would
  // corrupt the HTTP exchange.
  return s_routes.accessAllowed == nullptr || s_routes.accessAllowed();
}

bool readJson(JsonDocument& document) {
  if (!s_server->hasArg("plain")) return false;
  return deserializeJson(document, s_server->arg("plain")) ==
         DeserializationError::Ok;
}

bool callbackJson(int status,
                  app_core::MeteoWebConfigLoadCallback callback,
                  char* buffer,
                  size_t capacity) {
  const size_t length = callback ? callback(buffer, capacity) : 0;
  if (length == 0 || length >= capacity) {
    sendJson(500, "{\"error\":\"serialization\"}");
    return false;
  }
  buffer[length] = '\0';
  // Avoid WebServer's const-char overload, which first copies the complete
  // document into an internal-RAM String. The bounded callback buffer already
  // lives in PSRAM specifically to preserve TLS heap headroom.
  s_server->setContentLength(length);
  s_server->send(status, "application/json", "");
  s_server->sendContent(buffer, length);
  return true;
}

void handleGetConfig() {
  if (!allowed()) return;
  callbackJson(200, s_routes.loadConfig, s_configBuffer, kConfigBufferSize);
}

void handleGetStatus() {
  if (!allowed()) return;
  callbackJson(200, s_routes.loadStatus, s_statusBuffer, kStatusBufferSize);
}

void handlePostConfig() {
  if (!allowed()) return;
  if (!s_server->hasArg("plain")) {
    sendJson(400, "{\"error\":\"json\"}");
    return;
  }

  const String body = s_server->arg("plain");
  bool began = true;
  if (s_routes.storageBegin != nullptr) began = s_routes.storageBegin();
  if (!began) {
    sendJson(503, "{\"error\":\"storage\"}");
    return;
  }

  const bool saved = s_routes.saveConfig != nullptr &&
                     s_routes.saveConfig(body.c_str(), body.length());
  bool ended = true;
  if (s_routes.storageEnd != nullptr) ended = s_routes.storageEnd();

  if (!saved || !ended) {
    sendJson(400, saved ? "{\"error\":\"storage\"}"
                        : "{\"error\":\"config\"}");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

bool parseCommandValue(JsonDocument& document,
                       const char* key,
                       int16_t& value,
                       bool& present) {
  present = !document[key].isNull();
  if (!present) return true;
  const JsonVariantConst variant = document[key];
  if (!variant.is<int>() && !variant.is<long>() && !variant.is<short>()) {
    return false;
  }
  const int candidate = variant.as<int>();
  if (candidate < -32768 || candidate > 32767) return false;
  value = static_cast<int16_t>(candidate);
  return true;
}

void handlePostScreen() {
  if (!allowed()) return;
  JsonDocument document;
  if (!readJson(document)) {
    sendJson(400, "{\"error\":\"json\"}");
    return;
  }

  app_core::MeteoWebScreenCommand command;
  bool present = false;
  if (!parseCommandValue(document, "index", command.value, present)) {
    sendJson(400, "{\"error\":\"range\"}");
    return;
  }
  if (present) {
    command.kind = app_core::MeteoWebScreenCommandKind::Select;
    // The combined host exposes four Meteo-compatible screen slots.  The
    // Settings screen from the standalone page is intentionally absent.
    if (command.value < 0 || command.value > 3) {
      sendJson(400, "{\"error\":\"range\"}");
      return;
    }
  } else {
    if (!parseCommandValue(document, "step", command.value, present) ||
        !present || command.value < -1 || command.value > 1) {
      sendJson(400, "{\"error\":\"range\"}");
      return;
    }
    command.kind = app_core::MeteoWebScreenCommandKind::Step;
    command.value = command.value < 0 ? -1 : (command.value > 0 ? 1 : 0);
  }

  if (s_routes.handleScreenCommand == nullptr ||
      !s_routes.handleScreenCommand(command)) {
    sendJson(409, "{\"error\":\"disabled\"}");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

void handlePostRange() {
  if (!allowed()) return;
  JsonDocument document;
  if (!readJson(document)) {
    sendJson(400, "{\"error\":\"json\"}");
    return;
  }
  int16_t value = 0;
  bool present = false;
  if (!parseCommandValue(document, "step", value, present) || !present ||
      value < -1 || value > 1) {
    sendJson(400, "{\"error\":\"range\"}");
    return;
  }

  const app_core::MeteoWebScreenCommand command{
      app_core::MeteoWebScreenCommandKind::Range,
      static_cast<int16_t>(value < 0 ? -1 : (value > 0 ? 1 : 0))};
  if (s_routes.handleScreenCommand == nullptr ||
      !s_routes.handleScreenCommand(command)) {
    sendJson(409, "{\"error\":\"unavailable\"}");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

void appendUrlEncoded(const String& source, String& target) {
  for (size_t i = 0; i < source.length(); ++i) {
    const unsigned char character = static_cast<unsigned char>(source[i]);
    if (isalnum(character) || character == '-' || character == '_' ||
        character == '.' || character == '~') {
      target += static_cast<char>(character);
    } else {
      char encoded[4];
      snprintf(encoded, sizeof(encoded), "%%%02X", character);
      target += encoded;
    }
  }
}

void handleGetGeocode() {
  if (!allowed()) return;
  String query = s_server->arg("q");
  query.trim();
  if (query.length() == 0 || query.length() > 80) {
    sendJson(400, "[]");
    return;
  }

  String encoded;
  encoded.reserve(query.length() * 3);
  appendUrlEncoded(query, encoded);

  char url[384];
  const int urlLength =
      snprintf(url, sizeof(url), "%s?name=%s&count=8&language=%s&format=json",
               GEOCODE_URL, encoded.c_str(),
               Lang_Get() == LANG_EN ? "en" : "cs");
  if (urlLength < 0 || static_cast<size_t>(urlLength) >= sizeof(url)) {
    sendJson(400, "[]");
    return;
  }

  network_host::FetchLease lease(0);
  if (!lease.acquired()) {
    sendJson(503, "[]");
    return;
  }

  String body;
  if (!Net_GetString(url, body, "GEOKOD")) {
    sendJson(502, "[]");
    return;
  }

  JsonDocument filter;
  JsonObject resultFilter = filter["results"].add<JsonObject>();
  resultFilter["name"] = true;
  resultFilter["latitude"] = true;
  resultFilter["longitude"] = true;
  resultFilter["country"] = true;

  JsonDocument document;
  if (deserializeJson(document, body, DeserializationOption::Filter(filter))) {
    sendJson(502, "[]");
    return;
  }

  JsonDocument output;
  JsonArray results = output.to<JsonArray>();
  for (JsonObjectConst source : document["results"].as<JsonArrayConst>()) {
    JsonObject target = results.add<JsonObject>();
    target["name"] = source["name"];
    target["country"] = source["country"];
    target["lat"] = source["latitude"];
    target["lon"] = source["longitude"];
  }

  String response;
  serializeJson(output, response);
  s_server->send(200, "application/json", response);
}

class PageWriter {
 public:
  explicit PageWriter(WebServer& server) : server_(server) {}

  void append(char value) {
    if (length_ == sizeof(buffer_)) flush();
    buffer_[length_++] = value;
  }

  void append(const char* value) {
    if (value == nullptr) return;
    while (*value != '\0') append(*value++);
  }

  void flush() {
    if (length_ == 0) return;
    server_.sendContent(buffer_, length_);
    length_ = 0;
  }

 private:
  WebServer& server_;
  char buffer_[kPageChunkSize];
  size_t length_ = 0;
};

bool sourceMatches(size_t offset, const char* pattern) {
  const size_t length = strlen(pattern);
  if (offset + length > sizeof(PAGE_HTML) - 1) return false;
  for (size_t i = 0; i < length; ++i) {
    if (static_cast<char>(pgm_read_byte(PAGE_HTML + offset + i)) != pattern[i]) {
      return false;
    }
  }
  return true;
}

bool pageTemplateCompatible() {
  constexpr const char* requiredMarkers[] = {
      "/api/config", ",[\"settings\",4]", "</style>", "<body>"};
  const size_t sourceLength = sizeof(PAGE_HTML) - 1;
  for (const char* marker : requiredMarkers) {
    bool found = false;
    for (size_t offset = 0; offset < sourceLength; ++offset) {
      if (sourceMatches(offset, marker)) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

void streamPage() {
  // These small transformations keep the upstream page, translations and
  // form serialization intact while removing controls owned by the combined
  // host.  They also rewrite every absolute API fetch to the module prefix.
  const String apiPrefix = s_routes.apiPrefix != nullptr
                               ? String(s_routes.apiPrefix)
                               : String(app_core::METEO_WEB_DEFAULT_API_PREFIX);
  String apiBase = apiPrefix;
  while (apiBase.endsWith("/")) apiBase.remove(apiBase.length() - 1);
  const String apiRoot = apiBase + "/";

  PageWriter output(*s_server);
  const size_t sourceLength = sizeof(PAGE_HTML) - 1;
  for (size_t offset = 0; offset < sourceLength;) {
    if (sourceMatches(offset, "/api/")) {
      output.append(apiRoot.c_str());
      offset += strlen("/api/");
      continue;
    }
    if (sourceMatches(offset, ",[\"settings\",4]")) {
      // The upstream settings screen belongs to the old standalone scheduler.
      offset += strlen(",[\"settings\",4]");
      continue;
    }
    if (sourceMatches(offset, kRemoteHintCz)) {
      output.append("Rozsah se mění jen na obrazovkách Letadla a Meteoradar.");
      offset += strlen(kRemoteHintCz);
      continue;
    }
    if (sourceMatches(offset, kRemoteHintEn)) {
      output.append("The range only applies to the Aircraft and Weather screens.");
      offset += strlen(kRemoteHintEn);
      continue;
    }
    if (sourceMatches(offset, kRestartHintCz)) {
      output.append(
          "Změna zdroje radaru nebo polohy vyžaduje restart; obrazovky se "
          "projeví ihned.");
      offset += strlen(kRestartHintCz);
      continue;
    }
    if (sourceMatches(offset, kRestartHintEn)) {
      output.append(
          "Changing the radar source or location requires a restart; screen "
          "changes apply immediately.");
      offset += strlen(kRestartHintEn);
      continue;
    }
    if (sourceMatches(offset, kScreensHintCz)) {
      output.append("Vypnuté obrazovky se při swipu přeskakují.");
      offset += strlen(kScreensHintCz);
      continue;
    }
    if (sourceMatches(offset, kScreensHintEn)) {
      output.append("Disabled screens are skipped when swiping.");
      offset += strlen(kScreensHintEn);
      continue;
    }
    if (sourceMatches(offset, "</style>")) {
      output.append(
          ".tabs button[data-tab=\"tLook\"],.tabs button[data-tab=\"tSys\"],"
          "#tLook,#tSys,#cardWifi,#tScr .row:has(#autoRotate),"
          "#autoRotate,[data-i18n=\"autoRotate\"],[data-i18n=\"rotHint\"]"
          "{display:none!important}"
          "</style>");
      offset += strlen("</style>");
      continue;
    }
    if (sourceMatches(offset, "<body>")) {
      output.append(
          "<body><p style=\"padding:8px 16px;margin:0;color:#93a1b3\">"
          "<a href=\"/\" style=\"color:#37c0e8\">Společná konfigurace</a>"
          "</p>");
      offset += strlen("<body>");
      continue;
    }
    output.append(static_cast<char>(pgm_read_byte(PAGE_HTML + offset++)));
  }
  output.flush();
}

void handlePage() {
  if (!allowed()) return;
  s_server->sendHeader("Cache-Control", "no-store");
  s_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_server->send(200, "text/html; charset=utf-8", "");
  streamPage();
}

void handlePageRedirect() {
  if (!allowed()) return;
  s_server->sendHeader("Location", s_pagePath.c_str(), true);
  s_server->send(302, "text/plain", "redirect");
}

void buildPaths() {
  String pagePath = s_routes.pagePath != nullptr
                        ? String(s_routes.pagePath)
                        : String(app_core::METEO_WEB_DEFAULT_PAGE_PATH);
  if (!pagePath.startsWith("/")) pagePath = "/" + pagePath;
  if (!pagePath.endsWith("/")) pagePath += "/";
  s_pagePath = pagePath;
  s_pagePathWithoutSlash = pagePath;
  s_pagePathWithoutSlash.remove(s_pagePathWithoutSlash.length() - 1);

  String prefix = s_routes.apiPrefix != nullptr
                      ? String(s_routes.apiPrefix)
                      : String(app_core::METEO_WEB_DEFAULT_API_PREFIX);
  if (!prefix.startsWith("/")) prefix = "/" + prefix;
  while (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);

  s_configGetPath = prefix + "/config";
  s_configPostPath = s_configGetPath;
  s_statusPath = prefix + "/status";
  s_screenPath = prefix + "/screen";
  s_rangePath = prefix + "/range";
  s_geocodePath = prefix + "/geocode";
}

}  // namespace

bool registerRoutes(const app_core::MeteoWebRoutes& routes) {
  const bool hasStorageBegin = routes.storageBegin != nullptr;
  const bool hasStorageEnd = routes.storageEnd != nullptr;
  if (routes.webServer == nullptr || routes.manageServerLifecycle ||
      routes.registerLegacyAliases || routes.firmwareUpdatesEnabled ||
      !routes.hasConfigCallbacks() || !routes.hasStatusCallback() ||
      routes.handleScreenCommand == nullptr ||
      hasStorageBegin != hasStorageEnd || !routes.hasStorageCallbacks() ||
      !pageTemplateCompatible()) {
    return false;
  }

  s_routes = routes;
  s_server = routes.webServer;
  if (s_configBuffer == nullptr) {
    s_configBuffer = static_cast<char*>(
        heap_caps_malloc(kConfigBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (s_statusBuffer == nullptr) {
    s_statusBuffer = static_cast<char*>(
        heap_caps_malloc(kStatusBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (s_configBuffer == nullptr || s_statusBuffer == nullptr) return false;
  buildPaths();

  s_server->on(s_pagePath.c_str(), HTTP_GET, handlePage);
  s_server->on(s_pagePathWithoutSlash.c_str(), HTTP_GET, handlePageRedirect);
  s_server->on(s_configGetPath.c_str(), HTTP_GET, handleGetConfig);
  s_server->on(s_configPostPath.c_str(), HTTP_POST, handlePostConfig);
  s_server->on(s_statusPath.c_str(), HTTP_GET, handleGetStatus);
  s_server->on(s_screenPath.c_str(), HTTP_POST, handlePostScreen);
  s_server->on(s_rangePath.c_str(), HTTP_POST, handlePostRange);
  s_server->on(s_geocodePath.c_str(), HTTP_GET, handleGetGeocode);
  return true;
}

}  // namespace meteo_web

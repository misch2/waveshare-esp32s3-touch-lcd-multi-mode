#pragma once

#include <cstddef>
#include <cstdint>

// This header deliberately has no Arduino/WebServer/ArduinoJson dependency.
// The route adapter owns parsing and response serialization; these callbacks
// carry the upstream JSON document as UTF-8 bytes so the contract remains
// usable by native tests and does not introduce a second Meteo settings DTO.
class WebServer;

namespace app_core {

using MeteoWebStorageBeginCallback = bool (*)();
using MeteoWebStorageEndCallback = bool (*)();

// The load callback writes one complete, schema-compatible JSON document to
// `out` and returns its byte count. Returning zero means that the document
// could not be produced. The route adapter must send a 500 response if the
// returned count exceeds `capacity` or the callback returns zero.
using MeteoWebConfigLoadCallback = std::size_t (*)(char* out,
                                                   std::size_t capacity);

// Status uses the same bounded JSON-buffer contract as configuration loading.
// Keeping status separate lets the route adapter expose live state without
// widening or mutating the persisted Meteo settings schema.
using MeteoWebStatusLoadCallback = std::size_t (*)(char* out,
                                                   std::size_t capacity);

// A null callback means that the route is allowed. The host can install a
// callback when its web-mode/auth policy needs to gate the Meteo module. When
// it returns false, it must already have sent the denial response; the adapter
// deliberately sends nothing else on that request.
using MeteoWebAccessAllowedCallback = bool (*)();

// The save callback receives one complete JSON document exactly as submitted
// by the client. It owns validation, upstream Settings_FromJson application
// and any deferred/restart notification. Returning false means the payload
// was rejected or could not be persisted.
using MeteoWebConfigSaveCallback = bool (*)(const char* json,
                                            std::size_t length);

enum class MeteoWebScreenCommandKind : std::uint8_t {
  Select = 0,
  Step = 1,
  Range = 2,
};

struct MeteoWebScreenCommand {
  MeteoWebScreenCommandKind kind = MeteoWebScreenCommandKind::Step;
  // Select uses a screen index; Step and Range use -1 or +1. Zero is a valid
  // no-op request and lets the caller reject it consistently with the UI.
  std::int16_t value = 0;
};

using MeteoWebScreenCommandCallback =
    bool (*)(const MeteoWebScreenCommand& command);

inline constexpr char METEO_WEB_DEFAULT_PAGE_PATH[] = "/meteo/";
inline constexpr char METEO_WEB_DEFAULT_API_PREFIX[] = "/api/modules/meteo";

struct MeteoWebRoutes {
  // The combined host supplies the one port-80 server. The adapter must not
  // construct or begin another WebServer when this is non-null.
  WebServer* webServer = nullptr;

  // The page JavaScript and API handlers use the same canonical prefixes.
  const char* pagePath = METEO_WEB_DEFAULT_PAGE_PATH;
  const char* apiPrefix = METEO_WEB_DEFAULT_API_PREFIX;

  // Legacy Meteo routes (`/`, `/api/config`, ...) collide with the host and
  // are disabled by default. A standalone caller may opt into aliases after
  // ensuring it owns the complete server namespace.
  bool registerLegacyAliases = false;

  // The caller owns server begin/handleClient/end. This is false by default
  // because the combined WebHost owns the sole WebServer lifecycle.
  bool manageServerLifecycle = false;

  // Combined firmware updates are manual-only and do not expose the upstream
  // Meteo `/update` endpoint. The field lets a standalone adapter opt in
  // explicitly without making that behavior the integration default.
  bool firmwareUpdatesEnabled = false;

  // Route adapters may leave these null only when they implement equivalent
  // behavior themselves. A storage callback pair must be supplied together.
  MeteoWebConfigLoadCallback loadConfig = nullptr;
  MeteoWebStatusLoadCallback loadStatus = nullptr;
  MeteoWebConfigSaveCallback saveConfig = nullptr;
  MeteoWebScreenCommandCallback handleScreenCommand = nullptr;
  MeteoWebAccessAllowedCallback accessAllowed = nullptr;
  MeteoWebStorageBeginCallback storageBegin = nullptr;
  MeteoWebStorageEndCallback storageEnd = nullptr;

  bool hasConfigCallbacks() const noexcept {
    return loadConfig != nullptr && saveConfig != nullptr;
  }

  bool hasStatusCallback() const noexcept { return loadStatus != nullptr; }

  bool hasStorageCallbacks() const noexcept {
    return storageBegin != nullptr && storageEnd != nullptr;
  }
};

using MeteoWebOptions = MeteoWebRoutes;

}  // namespace app_core

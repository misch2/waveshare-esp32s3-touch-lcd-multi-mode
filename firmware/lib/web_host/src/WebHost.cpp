#include "WebHost.h"

#include <Preferences.h>
#include <WebServer.h>

#include <esp_heap_caps.h>

#include <cstring>

#include "HostWebPage.h"
#include "MeteoWebAdapter.h"

// Defined after the pinned ConfigurationWeb.cpp include.  The function is a
// narrow root-owned seam into that implementation's anonymous-namespace
// authentication policy; it is intentionally not a second auth model.
extern bool configurationWebRequireHostAccess();
extern bool configurationWebRegisterHostClockPage(WebServer& hostServer);

namespace web_host {
namespace {
WebServer server(80);

constexpr std::size_t kStatusBufferSize = 4096;
constexpr std::size_t kDiagnosticsBufferSize = 12288;
constexpr std::size_t kExportBufferSize = 32768;
constexpr std::size_t kImportMessageSize = 256;
constexpr std::size_t kFirmwareMessageSize = 256;

app_core::CombinedWebRoutes combinedRoutes;
char* statusBuffer = nullptr;
char* diagnosticsBuffer = nullptr;
char* exportBuffer = nullptr;

bool firmwareUploadAccessDenied = false;
bool firmwareUploadFailed = false;
bool firmwareUploadStarted = false;
bool firmwareUploadCompleted = false;
bool firmwareUploadAbortCalled = false;
bool firmwareRestartPending = false;
int firmwareUploadErrorStatus = 400;
std::size_t firmwareUploadReceived = 0;
char firmwareUploadMessage[kFirmwareMessageSize] = {};

void clearFirmwareUploadState() {
  firmwareUploadAccessDenied = false;
  firmwareUploadFailed = false;
  firmwareUploadStarted = false;
  firmwareUploadCompleted = false;
  firmwareUploadAbortCalled = false;
  firmwareUploadErrorStatus = 400;
  firmwareUploadReceived = 0;
  firmwareUploadMessage[0] = '\0';
}

void setFirmwareUploadError(const char* message, int status = 400) {
  firmwareUploadFailed = true;
  firmwareUploadErrorStatus = status;
  if (message == nullptr || message[0] == '\0') {
    message = "Nahrání firmwaru selhalo.";
  }
  std::strncpy(firmwareUploadMessage, message,
               sizeof(firmwareUploadMessage) - 1);
  firmwareUploadMessage[sizeof(firmwareUploadMessage) - 1] = '\0';
}

void abortFirmwareUpload() {
  if (!firmwareUploadStarted || firmwareUploadAbortCalled ||
      combinedRoutes.firmwareUploadAbort == nullptr) {
    return;
  }
  firmwareUploadAbortCalled = true;
  combinedRoutes.firmwareUploadAbort();
}

const char* uploadBasename(const String& filename) {
  const char* value = filename.c_str();
  const char* slash = std::strrchr(value, '/');
  const char* backslash = std::strrchr(value, '\\');
  if (backslash != nullptr && (slash == nullptr || backslash > slash)) {
    slash = backslash;
  }
  return slash == nullptr ? value : slash + 1;
}

bool hasBinSuffix(const char* filename) {
  if (filename == nullptr) return false;
  const std::size_t length = std::strlen(filename);
  if (length <= 4) return false;
  const char* suffix = filename + length - 4;
  return suffix[0] == '.' && (suffix[1] == 'b' || suffix[1] == 'B') &&
         (suffix[2] == 'i' || suffix[2] == 'I') &&
         (suffix[3] == 'n' || suffix[3] == 'N');
}

bool ensureClockWebNamespaces() {
  // The combined host starts its web layer before RGB scanout, so this is the
  // safe place to create optional namespaces without interrupting the PSRAM
  // bounce-buffer refill. The upstream clock firmware remains read-only here.
  constexpr const char* kNamespaces[] = {"web-auth", "web-mode"};
  for (const char* name : kNamespaces) {
    Preferences preferences;
    if (!preferences.begin(name, false, "clockcfg")) return false;
    preferences.end();
  }
  return true;
}

bool combinedAccessAllowed() {
  // The combined host deliberately uses the clock module's authentication
  // policy for every host-level route. The callback also owns the denial
  // response (401/423), so callers must return without sending another body.
  return combinedRoutes.accessAllowed == nullptr ||
         combinedRoutes.accessAllowed();
}

void sendJson(int status, const char* body) {
  server.send(status, "application/json; charset=utf-8", body ? body : "{}");
}

bool sendJsonFromCallback(int status,
                          app_core::CombinedJsonLoadCallback callback,
                          char* buffer,
                          std::size_t capacity) {
  if (callback == nullptr || buffer == nullptr || capacity == 0) {
    sendJson(404, "{\"ok\":false,\"message\":\"Není k dispozici.\"}");
    return false;
  }
  const std::size_t length = callback(buffer, capacity);
  if (length == 0 || length >= capacity) {
    sendJson(500, "{\"ok\":false,\"message\":\"Serializace se nezdařila.\"}");
    return false;
  }
  // Avoid WebServer's const-char overload, which copies the complete JSON
  // into an internal-RAM String. The callback buffer lives in PSRAM.
  server.setContentLength(length);
  server.send(status, "application/json; charset=utf-8", "");
  server.sendContent(buffer, length);
  return true;
}

void handleHostRoot() {
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.sendHeader(F("X-Content-Type-Options"), F("nosniff"));
  server.sendHeader(F("X-Frame-Options"), F("DENY"));
  server.send_P(200, PSTR("text/html; charset=utf-8"), HOST_WEB_PAGE);
}

void handleHostStatus() {
  if (!combinedAccessAllowed()) return;
  sendJsonFromCallback(200, combinedRoutes.loadStatus, statusBuffer,
                       kStatusBufferSize);
}

void handleHostDiagnostics() {
  if (!combinedAccessAllowed()) return;
  sendJsonFromCallback(200, combinedRoutes.loadDiagnostics, diagnosticsBuffer,
                       kDiagnosticsBufferSize);
}

void handleHostExport() {
  if (!combinedAccessAllowed()) return;
  server.sendHeader(F("Content-Disposition"),
                    F("attachment; filename=waveshare-multi-mode-backup.json"));
  sendJsonFromCallback(200, combinedRoutes.loadExport, exportBuffer,
                       kExportBufferSize);
}

void sendImportResult(int status, bool ok, const char* message) {
  char response[512];
  std::size_t at = 0;
  const char* prefix = ok ? "{\"ok\":true" : "{\"ok\":false";
  const std::size_t prefixLength = std::strlen(prefix);
  std::memcpy(response + at, prefix, prefixLength);
  at += prefixLength;

  if (message != nullptr && message[0] != '\0' && at + 13 < sizeof(response)) {
    const char field[] = ",\"message\":\"";
    std::memcpy(response + at, field, sizeof(field) - 1);
    at += sizeof(field) - 1;
    for (const unsigned char* source =
             reinterpret_cast<const unsigned char*>(message);
         *source != '\0' && at + 3 < sizeof(response); ++source) {
      const unsigned char value = *source;
      if (value == '\"' || value == '\\') {
        response[at++] = '\\';
        response[at++] = static_cast<char>(value);
      } else if (value >= 0x20U) {
        response[at++] = static_cast<char>(value);
      } else {
        response[at++] = ' ';
      }
    }
    if (at + 2 < sizeof(response)) response[at++] = '\"';
  }
  if (at + 2 >= sizeof(response)) at = sizeof(response) - 3;
  response[at++] = '}';
  response[at] = '\0';
  sendJson(status, response);
}

void handleHostImport() {
  if (!combinedAccessAllowed()) return;
  if (combinedRoutes.validateImport == nullptr ||
      combinedRoutes.importConfig == nullptr ||
      !combinedRoutes.hasStorageCallbacks()) {
    sendImportResult(404, false, "Obnova konfigurace není k dispozici.");
    return;
  }
  if (!server.hasArg("plain")) {
    sendImportResult(400, false, "Chybí tělo požadavku.");
    return;
  }
  const String body = server.arg("plain");
  if (body.length() == 0) {
    sendImportResult(400, false, "Konfigurace je prázdná.");
    return;
  }
  if (body.length() > app_core::COMBINED_WEB_MAX_IMPORT_BYTES) {
    sendImportResult(413, false, "Konfigurace je příliš velká.");
    return;
  }

  char detail[kImportMessageSize];
  detail[0] = '\0';
  if (!combinedRoutes.validateImport(body.c_str(), body.length(), detail,
                                     sizeof(detail))) {
    detail[sizeof(detail) - 1] = '\0';
    sendImportResult(400, false,
                     detail[0] != '\0' ? detail
                                        : "Konfigurace není platná.");
    return;
  }

  if (!combinedRoutes.storageBegin()) {
    sendImportResult(503, false, "Úložiště konfigurace není dostupné.");
    return;
  }

  detail[0] = '\0';
  const bool imported = combinedRoutes.importConfig(
      body.c_str(), body.length(), detail, sizeof(detail));
  detail[sizeof(detail) - 1] = '\0';
  const bool storageEnded = combinedRoutes.storageEnd();
  if (!storageEnded) {
    sendImportResult(500, false, "Dokončení zápisu konfigurace selhalo.");
    return;
  }
  if (!imported) {
    sendImportResult(400, false,
                     detail[0] != '\0' ? detail
                                        : "Konfiguraci se nepodařilo obnovit.");
    return;
  }
  sendImportResult(200, true, detail);
}

void handleHostFirmwareUploadChunk() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    clearFirmwareUploadState();
    // Authenticate before accepting the first chunk.  A denied request is
    // still drained by WebServer; the completion handler must not overwrite
    // the shared policy's response with a second body.
    if (!combinedAccessAllowed()) {
      firmwareUploadAccessDenied = true;
      return;
    }

    if (!combinedRoutes.hasFirmwareUploadCallbacks()) {
      setFirmwareUploadError("Ruční aktualizace není k dispozici.", 404);
      return;
    }

    // The name is only a transport hint.  The OTA service validates the
    // image contents after receiving the data, so a factory image is rejected
    // by the OTA validator rather than by its filename.
    const char* filename = uploadBasename(upload.filename);
    if (!hasBinSuffix(filename)) {
      setFirmwareUploadError("Vyberte soubor firmwaru s příponou .bin.");
      return;
    }

    char detail[kFirmwareMessageSize];
    detail[0] = '\0';
    if (!combinedRoutes.firmwareUploadBegin(filename, detail, sizeof(detail))) {
      detail[sizeof(detail) - 1] = '\0';
      setFirmwareUploadError(
          detail[0] != '\0' ? detail : "Firmware se nepodařilo připravit.",
          503);
      return;
    }
    firmwareUploadStarted = true;
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    // Returning without writing is intentional: WebServer continues to read
    // and discard the remaining multipart body after an earlier failure.
    if (firmwareUploadAccessDenied || firmwareUploadFailed ||
        !firmwareUploadStarted || firmwareUploadCompleted) {
      return;
    }
    if (upload.currentSize > app_core::COMBINED_WEB_MAX_FIRMWARE_BYTES -
                                firmwareUploadReceived) {
      setFirmwareUploadError("Firmware je příliš velký.", 413);
      abortFirmwareUpload();
      return;
    }

    char detail[kFirmwareMessageSize];
    detail[0] = '\0';
    if (!combinedRoutes.firmwareUploadWrite(
            upload.buf, upload.currentSize, detail, sizeof(detail))) {
      detail[sizeof(detail) - 1] = '\0';
      setFirmwareUploadError(
          detail[0] != '\0' ? detail : "Zápis firmwaru selhal.", 500);
      abortFirmwareUpload();
      return;
    }
    firmwareUploadReceived += upload.currentSize;
    delay(0);
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (firmwareUploadAccessDenied || firmwareUploadFailed ||
        !firmwareUploadStarted || firmwareUploadCompleted) {
      return;
    }
    char detail[kFirmwareMessageSize];
    detail[0] = '\0';
    if (!combinedRoutes.firmwareUploadEnd(detail, sizeof(detail))) {
      detail[sizeof(detail) - 1] = '\0';
      setFirmwareUploadError(
          detail[0] != '\0' ? detail : "Firmware se nepodařilo dokončit.",
          500);
      abortFirmwareUpload();
      return;
    }
    firmwareUploadCompleted = true;
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    if (firmwareUploadAccessDenied) return;
    if (!firmwareUploadFailed) {
      setFirmwareUploadError("Nahrání firmwaru bylo přerušeno.", 400);
    }
    abortFirmwareUpload();
  }
}

void handleHostFirmwareUploadDone() {
  if (firmwareUploadAccessDenied) {
    clearFirmwareUploadState();
    return;
  }
  if (!combinedRoutes.hasFirmwareUploadCallbacks()) {
    sendImportResult(404, false, "Ruční aktualizace není k dispozici.");
    clearFirmwareUploadState();
    return;
  }
  if (!firmwareUploadCompleted || firmwareUploadFailed) {
    const int status = firmwareUploadErrorStatus;
    sendImportResult(status, false,
                     firmwareUploadMessage[0] != '\0'
                         ? firmwareUploadMessage
                         : "Nahrání firmwaru selhalo.");
    clearFirmwareUploadState();
    return;
  }

  server.sendHeader(F("Connection"), F("close"));
  sendImportResult(200, true,
                   "Firmware byl nahrán. Zařízení se restartuje.");
  // The HTTP response is sent before loop() invokes this callback.  Keeping
  // the restart out of the multipart callback also lets the browser receive
  // the final JSON verdict instead of losing the connection mid-upload.
  firmwareRestartPending = true;
  clearFirmwareUploadState();
}

char* allocateBufferIfNeeded(char* current, std::size_t capacity) {
  if (current != nullptr) return current;
  return static_cast<char*>(
      heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
}  // namespace

bool begin(ClockConfigLoadCallback loadCallback,
           ClockConfigSaveCallback saveCallback,
           ConfigurationWebStatusCallback statusCallback,
           SunTransitionTimesCallback sunTimesCallback,
           HomeAssistantRefreshCallback refreshCallback,
           DayNightStatusCallback dayNightStatusCallback,
           DisplayPowerCallback displayPowerCallback,
           DisplayPowerStatusCallback displayPowerStatusCallback,
           StorageBeginCallback storageBeginCallback,
           StorageEndCallback storageEndCallback,
           const app_core::MeteoWebRoutes& meteoRoutes,
           const app_core::CombinedWebRoutes& requestedCombinedRoutes) {
  if (!ensureClockWebNamespaces()) return false;

  const bool hasCombinedStorageBegin =
      requestedCombinedRoutes.storageBegin != nullptr;
  const bool hasCombinedStorageEnd =
      requestedCombinedRoutes.storageEnd != nullptr;
  const bool hasAnyFirmwareUploadCallback =
      requestedCombinedRoutes.firmwareUploadBegin != nullptr ||
      requestedCombinedRoutes.firmwareUploadWrite != nullptr ||
      requestedCombinedRoutes.firmwareUploadEnd != nullptr ||
      requestedCombinedRoutes.firmwareUploadAbort != nullptr ||
      requestedCombinedRoutes.firmwareUploadRestart != nullptr;
  if (hasCombinedStorageBegin != hasCombinedStorageEnd ||
      (requestedCombinedRoutes.importConfig != nullptr &&
       (!requestedCombinedRoutes.hasImportValidationCallback() ||
        !requestedCombinedRoutes.hasStorageCallbacks())) ||
      hasAnyFirmwareUploadCallback !=
          requestedCombinedRoutes.hasFirmwareUploadCallbacks()) {
    // An import without both halves of the display-safe storage transaction
    // must never be exposed: a flash write could corrupt RGB scanout.
    return false;
  }

  ConfigurationWebRoutes routes;
  routes.webServer = &server;
  routes.pagePath = CONFIGURATION_WEB_DEFAULT_PAGE_PATH;
  routes.apiPrefix = CONFIGURATION_WEB_DEFAULT_API_PREFIX;
  routes.registerLegacyAliases = false;
  routes.manageServerLifecycle = false;
  routes.firmwareUpdatesEnabled = false;
  routes.storageBegin = storageBeginCallback;
  routes.storageEnd = storageEndCallback;

  if (!::configurationWebBeginWithOptions(
          routes, loadCallback, saveCallback, statusCallback,
          sunTimesCallback, refreshCallback, dayNightStatusCallback,
          displayPowerCallback, displayPowerStatusCallback)) {
    return false;
  }
  if (!::configurationWebRegisterHostClockPage(server)) return false;

  // Meteo uses the same caller-owned server and the same canonical security
  // policy as the clock module.  These host invariants are forced here even
  // if a caller supplies a DTO populated for a standalone adapter.
  app_core::MeteoWebRoutes hostMeteoRoutes = meteoRoutes;
  hostMeteoRoutes.webServer = &server;
  hostMeteoRoutes.pagePath = app_core::METEO_WEB_DEFAULT_PAGE_PATH;
  hostMeteoRoutes.apiPrefix = app_core::METEO_WEB_DEFAULT_API_PREFIX;
  hostMeteoRoutes.registerLegacyAliases = false;
  hostMeteoRoutes.manageServerLifecycle = false;
  hostMeteoRoutes.firmwareUpdatesEnabled = false;
  hostMeteoRoutes.accessAllowed = configurationWebRequireHostAccess;
  if (!meteo_web::registerRoutes(hostMeteoRoutes)) return false;

  combinedRoutes = requestedCombinedRoutes;
  // Ignore a caller-provided policy here. This is intentionally one shared
  // clock-auth policy, rather than a second host-level access model.
  combinedRoutes.accessAllowed = configurationWebRequireHostAccess;
  if (combinedRoutes.loadStatus != nullptr) {
    statusBuffer = allocateBufferIfNeeded(statusBuffer, kStatusBufferSize);
    if (statusBuffer == nullptr) return false;
    server.on(app_core::COMBINED_WEB_STATUS_PATH, HTTP_GET, handleHostStatus);
  }
  if (combinedRoutes.loadDiagnostics != nullptr) {
    diagnosticsBuffer =
        allocateBufferIfNeeded(diagnosticsBuffer, kDiagnosticsBufferSize);
    if (diagnosticsBuffer == nullptr) return false;
    server.on(app_core::COMBINED_WEB_DIAGNOSTICS_PATH, HTTP_GET,
              handleHostDiagnostics);
  }
  if (combinedRoutes.loadExport != nullptr) {
    exportBuffer = allocateBufferIfNeeded(exportBuffer, kExportBufferSize);
    if (exportBuffer == nullptr) return false;
    server.on(app_core::COMBINED_WEB_EXPORT_PATH, HTTP_GET, handleHostExport);
  }
  if (combinedRoutes.importConfig != nullptr) {
    server.on(app_core::COMBINED_WEB_IMPORT_PATH, HTTP_POST, handleHostImport);
  }
  if (combinedRoutes.hasFirmwareUploadCallbacks()) {
    server.on(app_core::COMBINED_WEB_FIRMWARE_UPLOAD_PATH, HTTP_POST,
              handleHostFirmwareUploadDone, handleHostFirmwareUploadChunk);
  }

  server.on("/", HTTP_GET, handleHostRoot);
  server.onNotFound([]() {
    server.send(404, F("text/plain; charset=utf-8"),
                F("Stránka nebyla nalezena."));
  });
  server.begin();
  return true;
}

void loop() {
  server.handleClient();
  ::configurationWebLoop();
  if (firmwareRestartPending &&
      combinedRoutes.firmwareUploadRestart != nullptr) {
    firmwareRestartPending = false;
    delay(250);
    combinedRoutes.firmwareUploadRestart();
  }
}

void ensureActive() { ::configurationWebEnsureActive(); }

bool active() { return ::configurationWebActive(); }

Mode mode() { return ::configurationWebMode(); }

bool setMode(Mode selectedMode) {
  return ::configurationWebSetMode(selectedMode);
}

}  // namespace web_host

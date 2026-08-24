#include "WebHost.h"

#include <Preferences.h>
#include <WebServer.h>

#include "HostWebPage.h"
#include "MeteoWebAdapter.h"

// Defined after the pinned ConfigurationWeb.cpp include.  The function is a
// narrow root-owned seam into that implementation's anonymous-namespace
// authentication policy; it is intentionally not a second auth model.
extern bool configurationWebRequireHostAccess();

namespace web_host {
namespace {
WebServer server(80);

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

void handleHostRoot() {
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.sendHeader(F("X-Content-Type-Options"), F("nosniff"));
  server.sendHeader(F("X-Frame-Options"), F("DENY"));
  server.send_P(200, PSTR("text/html; charset=utf-8"), HOST_WEB_PAGE);
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
           const app_core::MeteoWebRoutes& meteoRoutes) {
  if (!ensureClockWebNamespaces()) return false;

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
}

void ensureActive() { ::configurationWebEnsureActive(); }

bool active() { return ::configurationWebActive(); }

Mode mode() { return ::configurationWebMode(); }

bool setMode(Mode selectedMode) {
  return ::configurationWebSetMode(selectedMode);
}

}  // namespace web_host

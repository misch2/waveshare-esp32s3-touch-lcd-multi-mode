#include "WebHost.h"

#include <WebServer.h>

#include "HostWebPage.h"

namespace web_host {
namespace {
WebServer server(80);

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
           StorageEndCallback storageEndCallback) {
  ConfigurationWebRoutes routes;
  routes.webServer = &server;
  routes.pagePath = CONFIGURATION_WEB_DEFAULT_PAGE_PATH;
  routes.apiPrefix = CONFIGURATION_WEB_DEFAULT_API_PREFIX;
  routes.registerLegacyAliases = false;
  routes.manageServerLifecycle = false;
  routes.storageBegin = storageBeginCallback;
  routes.storageEnd = storageEndCallback;

  if (!::configurationWebBeginWithOptions(
          routes, loadCallback, saveCallback, statusCallback,
          sunTimesCallback, refreshCallback, dayNightStatusCallback,
          displayPowerCallback, displayPowerStatusCallback)) {
    return false;
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
}

void ensureActive() { ::configurationWebEnsureActive(); }

bool active() { return ::configurationWebActive(); }

Mode mode() { return ::configurationWebMode(); }

bool setMode(Mode selectedMode) {
  return ::configurationWebSetMode(selectedMode);
}

}  // namespace web_host

// Compile exactly one copy of the pinned web implementation. HTML, JSON
// serialization, validation and authentication remain upstream. The
// integration host injects its caller-owned WebServer through the upstream
// route-registration seam.
// Source revision: 9537a76932fc9269b2a22a5fb90a62785897c680.
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "../../../../waveshare-hodiny/WaveshareHodiny/ConfigurationWeb.cpp"

// ConfigurationWeb.cpp keeps its authentication policy in an anonymous
// namespace.  This forwarding function deliberately lives in the same
// translation unit, so the Meteo adapter can use the exact same timed-mode,
// session-cookie and Origin checks without duplicating that security logic. A
// protected Meteo page request is redirected to the shared login page instead
// of exposing the API-oriented 401 JSON response in the browser.
bool configurationWebRequireHostAccess() {
  WebServer& sharedServer = *serverInstance;
  const String uri = sharedServer.uri();
  const bool meteoPage = sharedServer.method() == HTTP_GET &&
                         (uri == "/meteo" || uri == "/meteo/");
  const bool needsClockEntry =
      !webActive || (webPasswordEnabled && !webSessionAuthenticated());
  if (meteoPage && needsClockEntry) {
    addSecurityHeaders();
    if (webActive) extendWebAvailability();
    sharedServer.sendHeader(F("Location"), F("/clock/"), true);
    sharedServer.send(302, F("text/plain; charset=utf-8"),
                      F("Otevření společného přístupu ke konfiguraci"));
    return false;
  }
  return requireConfigurationAccess();
}

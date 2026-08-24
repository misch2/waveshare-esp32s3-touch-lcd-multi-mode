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

#include <cstring>

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

namespace {

constexpr char kClockPageBodyMarker[] = "<body>";
constexpr char kClockPageLink[] =
    "<body><p style=\"padding:8px 16px;margin:0;color:#93a1b3\">"
    "<a href=\"/\" style=\"color:#37c0e8\">Společná konfigurace</a>"
    "</p>";
constexpr size_t kClockPageChunkSize = 512;

bool clockPageSourceMatches(size_t offset, const char* marker) {
  const size_t markerLength = std::strlen(marker);
  if (offset + markerLength > sizeof(CONFIGURATION_PAGE) - 1) {
    return false;
  }
  for (size_t index = 0; index < markerLength; ++index) {
    if (static_cast<char>(pgm_read_byte(CONFIGURATION_PAGE + offset + index)) !=
        marker[index]) {
      return false;
    }
  }
  return true;
}

bool clockPageTemplateCompatible() {
  const size_t sourceLength = sizeof(CONFIGURATION_PAGE) - 1;
  for (size_t offset = 0; offset < sourceLength; ++offset) {
    if (clockPageSourceMatches(offset, kClockPageBodyMarker)) return true;
  }
  return false;
}

class ClockPageWriter {
 public:
  explicit ClockPageWriter(WebServer& server) : server_(server) {}

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
  char buffer_[kClockPageChunkSize];
  size_t length_ = 0;
};

void handleCombinedClockRoot() {
  // Keep the upstream login and locked/diagnostic pages byte-for-byte intact.
  // Only the authenticated configuration page is adapted for the combined
  // host, so all existing auth and timed-mode behavior remains upstream-owned.
  if (!webActive || (webPasswordEnabled && !webSessionAuthenticated())) {
    handleRoot();
    return;
  }

  if (!clockPageTemplateCompatible()) {
    // A future upstream page must never be served as a partially transformed
    // response. Falling back keeps the configuration usable and makes the
    // missing adapter marker visible in source review.
    handleRoot();
    return;
  }

  addSecurityHeaders();
  extendWebAvailability();
  WebServer& sharedServer = *serverInstance;
  sharedServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  sharedServer.send(200, F("text/html; charset=utf-8"), F(""));

  ClockPageWriter output(sharedServer);
  const size_t sourceLength = sizeof(CONFIGURATION_PAGE) - 1;
  for (size_t offset = 0; offset < sourceLength;) {
    if (clockPageSourceMatches(offset, kClockPageBodyMarker)) {
      output.append(kClockPageLink);
      offset += std::strlen(kClockPageBodyMarker);
      continue;
    }
    output.append(static_cast<char>(pgm_read_byte(CONFIGURATION_PAGE + offset)));
    ++offset;
  }
  output.flush();
}

}  // namespace

bool configurationWebRegisterHostClockPage(WebServer& hostServer) {
  if (!hostServer.removeRoute(CONFIGURATION_WEB_DEFAULT_PAGE_PATH, HTTP_GET)) {
    // The upstream route is expected to exist after
    // configurationWebBeginWithOptions(). Do not register a second handler
    // if that assumption changes in a future upstream revision.
    return false;
  }
  hostServer.on(CONFIGURATION_WEB_DEFAULT_PAGE_PATH, HTTP_GET,
                handleCombinedClockRoot);
  return true;
}

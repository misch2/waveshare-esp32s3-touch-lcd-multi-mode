// Compile exactly one copy of the pinned web implementation. HTML, JSON
// serialization, validation and authentication remain upstream. The
// integration host injects its caller-owned WebServer through the upstream
// route-registration seam.
// Source revision: 581087e8129e2d24db55f390c110664f1fc178b0.
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

bool clockPageSourceMatches(const char* source, size_t sourceLength,
                            size_t offset, const char* marker) {
  const size_t markerLength = std::strlen(marker);
  if (offset + markerLength > sourceLength) {
    return false;
  }
  for (size_t index = 0; index < markerLength; ++index) {
    if (static_cast<char>(pgm_read_byte(source + offset + index)) != marker[index]) {
      return false;
    }
  }
  return true;
}

bool clockPageTemplateCompatible() {
  const size_t sourceLength = sizeof(CONFIGURATION_PAGE) - 1;
  for (size_t offset = 0; offset < sourceLength; ++offset) {
    if (clockPageSourceMatches(CONFIGURATION_PAGE, sourceLength, offset,
                               kClockPageBodyMarker))
      return true;
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

void serveCombinedClockPage(bool diagnosticsOnly) {
  persistBrowserLanguageIfUnset();
  const char* source = diagnosticsOnly || !webActive
                           ? DIAGNOSTIC_PAGE
                           : (webPasswordEnabled && !webSessionAuthenticated()
                                  ? LOGIN_PAGE
                                  : CONFIGURATION_PAGE);
  const size_t sourceLength = source == DIAGNOSTIC_PAGE
                                  ? sizeof(DIAGNOSTIC_PAGE) - 1
                                  : source == LOGIN_PAGE ? sizeof(LOGIN_PAGE) - 1
                                                          : sizeof(CONFIGURATION_PAGE) - 1;
  if (source == CONFIGURATION_PAGE && !clockPageTemplateCompatible()) {
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
  for (size_t offset = 0; offset < sourceLength;) {
    if (source == CONFIGURATION_PAGE &&
        clockPageSourceMatches(source, sourceLength, offset,
                               kClockPageBodyMarker)) {
      output.append(kClockPageLink);
      offset += std::strlen(kClockPageBodyMarker);
      continue;
    }
    if (clockPageSourceMatches(source, sourceLength, offset, "/api/")) {
      output.append(configurationApiPrefix.c_str());
      output.append('/');
      offset += std::strlen("/api/");
      continue;
    }
    if (clockPageSourceMatches(source, sourceLength, offset,
                               "href=\"/diagnostics\"")) {
      output.append("href=\"");
      output.append(configurationPagePath.c_str());
      output.append("diagnostics\"");
      offset += std::strlen("href=\"/diagnostics\"");
      continue;
    }
    if (clockPageSourceMatches(source, sourceLength, offset,
                               "location.replace(\"/\")")) {
      output.append("location.replace(\"");
      output.append(configurationPagePath.c_str());
      output.append("\")");
      offset += std::strlen("location.replace(\"/\")");
      continue;
    }
    output.append(static_cast<char>(pgm_read_byte(source + offset)));
    ++offset;
  }
  output.flush();
}

void handleCombinedClockRoot() { serveCombinedClockPage(false); }
void handleCombinedClockDiagnostics() { serveCombinedClockPage(true); }

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
  const String diagnosticsPath = configurationPagePath + F("diagnostics");
  if (!hostServer.removeRoute(diagnosticsPath, HTTP_GET)) return false;
  hostServer.on(diagnosticsPath, HTTP_GET, handleCombinedClockDiagnostics);
  return true;
}

#include <HTTPClient.h>

#include <optional>

#include "NetworkFetchGate.h"

namespace {

// The upstream service starts its own download task. Substitute only its local
// HTTP object so the host fetch gate is held by that task for the complete TLS
// request, including response streaming and HTTPClient cleanup.
class FetchGatedWeatherHttpClient : public HTTPClient {
 public:
  ~FetchGatedWeatherHttpClient() { end(); }

  bool begin(NetworkClient& client, String url) {
    end();
    fetchLease_.emplace();
    if (!fetchLease_->acquired()) {
      fetchLease_.reset();
      return false;
    }
    if (HTTPClient::begin(client, url)) return true;
    fetchLease_.reset();
    return false;
  }

  void end() {
    HTTPClient::end();
    fetchLease_.reset();
  }

 private:
  std::optional<network_host::FetchLease> fetchLease_;
};

}  // namespace

// Public weather assets are independent of the standalone firmware update
// service. Defining only these values does not enable update discovery or OTA
// installation in the combined firmware.
#define FIRMWARE_SERVER_URL "https://coolajz.github.io"
#define FIRMWARE_PROJECT_SLUG "waveshare-hodiny"
#define FIRMWARE_WEATHER_ASSET_PATH "/waveshare-hodiny/assets/weather-icons"
#define HTTPClient FetchGatedWeatherHttpClient

// Keep the implementation pinned in the read-only submodule and adapt only
// its host-owned HTTP resource at this integration boundary.
#include "../../../../waveshare-hodiny/WaveshareHodiny/WeatherAnimationService.cpp"

#undef HTTPClient
#undef FIRMWARE_WEATHER_ASSET_PATH
#undef FIRMWARE_PROJECT_SLUG
#undef FIRMWARE_SERVER_URL

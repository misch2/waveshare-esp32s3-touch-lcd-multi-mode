// Expose Arduino library dependencies to PlatformIO's thin-wrapper scanner.
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// Public weather assets are independent of the standalone firmware update
// service. Defining only these values does not enable update discovery or OTA
// installation in the combined firmware.
#define FIRMWARE_SERVER_URL "https://coolajz.github.io"
#define FIRMWARE_PROJECT_SLUG "waveshare-hodiny"
#define FIRMWARE_WEATHER_ASSET_PATH "/waveshare-hodiny/assets/weather-icons"
// v1.7.2 holds NetworkOperationGuard around the entire download. The combined
// ClockNetworkCoordinator bridge maps it to FetchLease's gate; a second HTTP
// wrapper here would recursively acquire the same non-recursive mutex.
#include "../../../../waveshare-hodiny/WaveshareHodiny/WeatherAnimationService.cpp"

#undef FIRMWARE_WEATHER_ASSET_PATH
#undef FIRMWARE_PROJECT_SLUG
#undef FIRMWARE_SERVER_URL

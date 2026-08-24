#pragma once

#include <cstddef>
#include <cstdint>

namespace app_core {

enum class MeteoRadarSource : std::uint8_t {
  Chmu = 0,
  RainViewer = 1,
};

// Module-local snapshot used at the host/service boundary. Persistence remains
// owned by MeteoPlaneRadar Settings; this DTO is deliberately not part of
// AppConfig.
struct MeteoRadarConfig {
  static constexpr std::uint8_t kRangeCount = 5;
  static constexpr std::uint8_t kDefaultRangeIndex = 1;
  static constexpr double kDefaultLatitude = 50.0755;
  static constexpr double kDefaultLongitude = 14.4378;

  double latitude = kDefaultLatitude;
  double longitude = kDefaultLongitude;
  MeteoRadarSource source = MeteoRadarSource::Chmu;
  std::uint8_t rangeIndex = kDefaultRangeIndex;

  bool validate() const;
  bool normalize();

  float rangeKm() const;
  bool wholeCountry() const;

  // Positive steps select a wider range, negative steps a narrower one. The
  // range list wraps exactly like the upstream ScreenWeather implementation.
  void stepRange(int step);
};

}  // namespace app_core

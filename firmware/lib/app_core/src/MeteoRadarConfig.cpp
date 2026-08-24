#include "MeteoRadarConfig.h"

#include <cmath>

namespace app_core {
namespace {

constexpr float kRangesKm[MeteoRadarConfig::kRangeCount] = {
    25.0f, 50.0f, 100.0f, 200.0f, 0.0f};

bool validLocation(double latitude, double longitude) {
  return std::isfinite(latitude) && std::isfinite(longitude) &&
         latitude >= -90.0 && latitude <= 90.0 && longitude >= -180.0 &&
         longitude <= 180.0 && (latitude != 0.0 || longitude != 0.0);
}

bool validSource(MeteoRadarSource source) {
  return source == MeteoRadarSource::Chmu ||
         source == MeteoRadarSource::RainViewer;
}

}  // namespace

constexpr std::uint8_t MeteoRadarConfig::kRangeCount;
constexpr std::uint8_t MeteoRadarConfig::kDefaultRangeIndex;
constexpr double MeteoRadarConfig::kDefaultLatitude;
constexpr double MeteoRadarConfig::kDefaultLongitude;

bool MeteoRadarConfig::validate() const {
  return validLocation(latitude, longitude) && validSource(source) &&
         rangeIndex < kRangeCount;
}

bool MeteoRadarConfig::normalize() {
  bool changed = false;
  if (!validLocation(latitude, longitude)) {
    latitude = kDefaultLatitude;
    longitude = kDefaultLongitude;
    changed = true;
  }
  if (!validSource(source)) {
    source = MeteoRadarSource::Chmu;
    changed = true;
  }
  if (rangeIndex >= kRangeCount) {
    rangeIndex = kDefaultRangeIndex;
    changed = true;
  }
  return changed;
}

float MeteoRadarConfig::rangeKm() const {
  const std::uint8_t normalizedIndex =
      rangeIndex < kRangeCount ? rangeIndex : kDefaultRangeIndex;
  return kRangesKm[normalizedIndex];
}

bool MeteoRadarConfig::wholeCountry() const { return rangeKm() <= 0.0f; }

void MeteoRadarConfig::stepRange(int step) {
  if (rangeIndex >= kRangeCount) rangeIndex = kDefaultRangeIndex;
  const int count = static_cast<int>(kRangeCount);
  int next = (static_cast<int>(rangeIndex) + step) % count;
  if (next < 0) next += count;
  rangeIndex = static_cast<std::uint8_t>(next);
}

}  // namespace app_core

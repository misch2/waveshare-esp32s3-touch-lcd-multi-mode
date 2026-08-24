#pragma once

#include <cstddef>
#include <cstdint>

#include "AppConfig.h"
#include "ClockConfig.h"

namespace combined_config {

inline constexpr char kFormat[] = "waveshare-multi-mode-settings";
inline constexpr std::uint16_t kSchemaVersion = 1;
inline constexpr std::size_t kMeteoJsonCapacity = 3072;

struct ImportBundle {
  app_core::AppConfig appConfig = app_core::AppConfig::defaults();
  ClockConfig clockConfig{};
  char meteoJson[kMeteoJsonCapacity] = {};
  std::size_t meteoJsonLength = 0;
  bool meteoHasLocation = false;
};

// Serialize a complete, versioned backup envelope. The supplied Meteo JSON is
// filtered to the module-owned, non-secret fields by the codec. A return value
// of zero means that an input document was invalid or the output buffer was
// too small.
std::size_t writeExport(const app_core::AppConfig& appConfig,
                        const ClockConfig& clockConfig,
                        const char* meteoJson, std::size_t meteoJsonLength,
                        char* out, std::size_t capacity);

// Parse and validate a complete backup envelope. No persistence or runtime
// state is touched; the caller owns applying the returned bundle.
bool parseImport(const char* json, std::size_t length,
                 const ClockConfig& currentClock, ImportBundle& out,
                 char* detail, std::size_t detailCapacity);

}  // namespace combined_config

// Generated from UPSTREAMS.json; do not edit by hand.
#pragma once

#include <cstddef>

namespace app_core {

struct ComponentProvenance {
  const char* id;
  const char* displayName;
  const char* upstreamUrl;
  const char* upstreamRef;
  const char* upstreamBase;
  const char* upstreamTag;
  const char* forkUrl;
  const char* forkPin;
};

inline constexpr ComponentProvenance kComponentProvenance[] = {
    {"meteo-plane-radar", "MeteoPlaneRadar", "https://github.com/petus/MeteoPlaneRadar", "main", "deb3fb452a22cb90ad61728bb395e9ea6560ee04", "v0.6.4", "https://github.com/misch2/MeteoPlaneRadar", "451aec4881444c38fe0b4465536a0f83ad9eb3d8"},
    {"waveshare-hodiny", "waveshare-hodiny", "https://github.com/CooLajz/waveshare-hodiny", "main", "581087e8129e2d24db55f390c110664f1fc178b0", "v1.7.2", "https://github.com/misch2/waveshare-hodiny-fork", "b99f3752ccfd9717a12da93964f905b89fe6bdbe"},
};
inline constexpr std::size_t kComponentProvenanceCount =
    sizeof(kComponentProvenance) / sizeof(kComponentProvenance[0]);

}  // namespace app_core

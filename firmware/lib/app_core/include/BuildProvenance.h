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
    {"waveshare-hodiny", "waveshare-hodiny", "https://github.com/CooLajz/waveshare-hodiny", "main", "9537a76932fc9269b2a22a5fb90a62785897c680", "v1.5.5", "https://github.com/misch2/waveshare-hodiny-fork", "915fb211c3c700e030ca18a9eebed974557caf13"},
};
inline constexpr std::size_t kComponentProvenanceCount =
    sizeof(kComponentProvenance) / sizeof(kComponentProvenance[0]);

}  // namespace app_core

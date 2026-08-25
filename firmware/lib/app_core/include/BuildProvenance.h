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
  const char* forkUrl;
  const char* forkPin;
};

inline constexpr ComponentProvenance kComponentProvenance[] = {
    {"meteo-plane-radar", "MeteoPlaneRadar", "https://github.com/petus/MeteoPlaneRadar", "main", "792ef8d05b0900a81e0f49697b8e72220a89f4a7", "https://github.com/misch2/MeteoPlaneRadar", "dd77fefd33d6adfa9498a745299e54004cea5694"},
    {"waveshare-hodiny", "waveshare-hodiny", "https://github.com/CooLajz/waveshare-hodiny", "main", "9537a76932fc9269b2a22a5fb90a62785897c680", "https://github.com/misch2/waveshare-hodiny-fork", "e1a66810aba21504cf14c239022620e595430f83"},
};
inline constexpr std::size_t kComponentProvenanceCount =
    sizeof(kComponentProvenance) / sizeof(kComponentProvenance[0]);

}  // namespace app_core

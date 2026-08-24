// Thin translation unit: the renderer and interaction model stay owned by the
// pinned MeteoPlaneRadar submodule.
#include <Arduino.h>

#ifdef BOOT_PIN
#undef BOOT_PIN
#endif
#include "../../../../MeteoPlaneRadar/MeteoPlaneRadar/ScreenPlanes.cpp"

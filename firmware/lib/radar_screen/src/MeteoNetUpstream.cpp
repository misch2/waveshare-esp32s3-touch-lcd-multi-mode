// Thin translation unit: reuse the pinned HTTPS helper unchanged.
#include <Arduino.h>
#ifdef BOOT_PIN
#undef BOOT_PIN
#endif
#include "../../../../MeteoPlaneRadar/MeteoPlaneRadar/Net.cpp"

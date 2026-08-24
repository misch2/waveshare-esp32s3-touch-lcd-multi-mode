// Thin translation unit: reuse the pinned collision/layout helper unchanged.
#include <Arduino.h>
#ifdef BOOT_PIN
#undef BOOT_PIN
#endif
#include "../../../../MeteoPlaneRadar/MeteoPlaneRadar/Layout.cpp"

// Thin translation unit: reuse the pinned streaming body helpers unchanged.
#include <Arduino.h>

#ifdef BOOT_PIN
#undef BOOT_PIN
#endif

#include "../../../../MeteoPlaneRadar/MeteoPlaneRadar/NetSink.cpp"

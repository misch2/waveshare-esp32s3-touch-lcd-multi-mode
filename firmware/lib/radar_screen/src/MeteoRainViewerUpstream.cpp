// Thin translation unit: reuse the pinned RainViewer client unchanged.
#include <Arduino.h>
#ifdef BOOT_PIN
#undef BOOT_PIN
#endif
#include "../../../../MeteoPlaneRadar/MeteoPlaneRadar/RainViewer.cpp"

// Thin translation unit: the pinned upstream remains the renderer owner.
#include <Arduino.h>
#ifdef BOOT_PIN
#undef BOOT_PIN
#endif
#include "../../../../MeteoPlaneRadar/MeteoPlaneRadar/ScreenWeather.cpp"

// Thin translation unit: the pinned upstream remains the forecast data owner.
#include <Arduino.h>

#ifdef BOOT_PIN
#undef BOOT_PIN
#endif

#include "../../../../MeteoPlaneRadar/MeteoPlaneRadar/Forecast.cpp"

// Thin translation unit: compile the pinned upstream settings implementation
// without adding MeteoPlaneRadar's application entry point.
#include <Arduino.h>
#ifdef BOOT_PIN
#undef BOOT_PIN
#endif
#include "../../../../MeteoPlaneRadar/MeteoPlaneRadar/Settings.cpp"

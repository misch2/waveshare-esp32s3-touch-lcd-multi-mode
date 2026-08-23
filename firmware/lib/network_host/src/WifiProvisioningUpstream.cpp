// Reuse the pinned clock firmware's NVS schema, retry behavior and Improv
// provisioning callbacks. Release mode is scoped to this translation unit so
// stored `clock-wifi` credentials work without enabling clock-owned OTA/web.
#define FIRMWARE_RELEASE 1
#include <Preferences.h>

#include "../../../../waveshare-hodiny/WaveshareHodiny/WifiProvisioning.cpp"

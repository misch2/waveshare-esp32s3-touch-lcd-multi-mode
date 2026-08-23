// Keep the upstream implementation pinned in the submodule while exposing
// it to PlatformIO through this integration-owned translation unit. Do not
// add the upstream .cpp directly to the firmware project: it would be built
// as a second copy of the dashboard.
#include "../../../../waveshare-hodiny/WaveshareHodiny/ClockDashboard.cpp"


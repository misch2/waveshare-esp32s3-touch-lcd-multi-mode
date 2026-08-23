// The dashboard only reads a snapshot while rendering its settings page.
// Keep network/OTA implementation out of the combined prototype.
#include "../../../../waveshare-hodiny/WaveshareHodiny/FirmwareUpdateService.h"

FirmwareUpdateSnapshot firmwareUpdateServiceSnapshot() {
  return FirmwareUpdateSnapshot{};
}


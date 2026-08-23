// The dashboard only reads a snapshot while rendering its settings page.
// Keep network/OTA implementation out of the combined prototype.
#include "../../../../waveshare-hodiny/WaveshareHodiny/FirmwareUpdateService.h"

FirmwareUpdateSnapshot firmwareUpdateServiceSnapshot() {
  return FirmwareUpdateSnapshot{};
}

bool firmwareUpdateServiceRequestCheck(bool installWhenAvailable) {
  (void)installWhenAvailable;
  return false;
}

const char* firmwareUpdateStateName(FirmwareUpdateState state) {
  switch (state) {
    case FirmwareUpdateState::Idle:
      return "idle";
    case FirmwareUpdateState::Checking:
      return "checking";
    case FirmwareUpdateState::Available:
      return "available";
    case FirmwareUpdateState::Current:
      return "current";
    case FirmwareUpdateState::Downloading:
      return "downloading";
    case FirmwareUpdateState::Failed:
      return "failed";
    case FirmwareUpdateState::Restarting:
      return "restarting";
  }
  return "unknown";
}

// Deliberately disabled implementation for the combined product. The upstream
// dashboard still references the status API, but this firmware is updated only
// by explicitly flashing a locally built image and must never contact a release
// service or schedule an installation.
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

#pragma once

#include <cstddef>
#include <cstdint>

namespace manual_firmware_update {

// The service deliberately has no WebServer dependency.  The host web layer
// maps its upload callbacks to this small, sequential transaction API.
using DisplayPauseCallback = bool (*)();
using DisplayResumeCallback = bool (*)();
using RestartCallback = void (*)();

struct Callbacks {
  DisplayPauseCallback pauseDisplay = nullptr;
  DisplayResumeCallback resumeDisplay = nullptr;
  RestartCallback restart = nullptr;
};

// Install the host-owned lifecycle callbacks.  This does not start an OTA
// transaction and is safe to call once during setup before routes are exposed.
void configure(const Callbacks& callbacks);

// Start one upload transaction. `filename` must have a .bin suffix. A zero
// content length means that the HTTP layer did not know the final length; the
// partition limit is still enforced while receiving data.
bool begin(const char* filename,
           std::size_t contentLength,
           char* message,
           std::size_t messageCapacity);

// Feed the upload in its original order. The first 288 bytes are buffered and
// validated before esp_ota_begin() erases the target OTA slot.
bool write(const std::uint8_t* data,
           std::size_t length,
           char* message,
           std::size_t messageCapacity);

// Finish and validate the image, then select it as the next boot partition.
// On success the display remains paused until the reboot callback is called.
bool end(char* message, std::size_t messageCapacity);

// Abort an active transaction. This is idempotent; an active transaction is
// erased/aborted and a failed upload restores the display before releasing the
// shared network gate. Once end() succeeds, use restartAfterResponse(); the
// new boot partition has already been selected and must not be rolled back by
// a late upload-disconnect callback.
void abort();

// Call only after the HTTP response has been sent. It releases any remaining
// transaction resources and invokes the host restart callback. The callback
// normally calls ESP.restart(), so it is expected not to return.
bool restartAfterResponse(char* message, std::size_t messageCapacity);

bool inProgress();
bool restartPending();

}  // namespace manual_firmware_update

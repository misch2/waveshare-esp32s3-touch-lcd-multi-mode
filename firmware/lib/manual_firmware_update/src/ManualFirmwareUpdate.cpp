#include "ManualFirmwareUpdate.h"

#include <algorithm>
#include <cstring>

#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_err.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>

#include "NetworkFetchGate.h"

namespace manual_firmware_update {
namespace {

constexpr std::size_t kImageHeaderBytes =
    sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +
    sizeof(esp_app_desc_t);
constexpr std::size_t kMessageFallbackCapacity = 1;

enum class State : std::uint8_t { Idle, Receiving, RestartPending };

Callbacks callbacks;
State state = State::Idle;
const esp_partition_t* targetPartition = nullptr;
esp_ota_handle_t otaHandle = 0;
bool otaStarted = false;
bool displayPaused = false;
bool fetchGateHeld = false;
std::size_t expectedLength = 0;
std::size_t receivedLength = 0;
std::size_t headerLength = 0;
std::uint8_t header[kImageHeaderBytes] = {};

void setMessage(char* message, std::size_t capacity, const char* text) {
  if (message == nullptr || capacity < kMessageFallbackCapacity) return;
  if (text == nullptr) text = "Operace se nezdařila.";
  const std::size_t length = std::strlen(text);
  const std::size_t copyLength = std::min(length, capacity - 1);
  std::memcpy(message, text, copyLength);
  message[copyLength] = '\0';
}

bool hasBinSuffix(const char* filename) {
  if (filename == nullptr) return false;
  const std::size_t length = std::strlen(filename);
  if (length < 4) return false;
  const char* suffix = filename + length - 4;
  return (suffix[0] == '.') && (suffix[1] == 'b' || suffix[1] == 'B') &&
         (suffix[2] == 'i' || suffix[2] == 'I') &&
         (suffix[3] == 'n' || suffix[3] == 'N');
}

void releaseFetchGate() {
  if (!fetchGateHeld) return;
  network_host::releaseFetchGate();
  fetchGateHeld = false;
}

bool restoreDisplay(char* message, std::size_t messageCapacity) {
  if (!displayPaused) return true;
  const bool restored = callbacks.resumeDisplay != nullptr &&
                        callbacks.resumeDisplay();
  if (!restored) {
    setMessage(message, messageCapacity,
               "Obnovení displeje po aktualizaci selhalo.");
    return false;
  }
  displayPaused = false;
  return true;
}

void abortOtaHandle() {
  if (!otaStarted) return;
  esp_ota_abort(otaHandle);
  otaHandle = 0;
  otaStarted = false;
}

void resetUploadState() {
  targetPartition = nullptr;
  expectedLength = 0;
  receivedLength = 0;
  headerLength = 0;
  std::memset(header, 0, sizeof(header));
  state = State::Idle;
}

void failTransaction(char* message,
                     std::size_t messageCapacity,
                     const char* detail) {
  abortOtaHandle();
  // Keep the first failure detail. Restoration itself may fail, but that is
  // still reported when the caller supplied an otherwise empty message.
  setMessage(message, messageCapacity, detail);
  char restoreMessage[96] = {};
  if (!restoreDisplay(restoreMessage, sizeof(restoreMessage)) &&
      (message == nullptr || message[0] == '\0')) {
    setMessage(message, messageCapacity, restoreMessage);
  }
  releaseFetchGate();
  resetUploadState();
}

bool validateHeader(char* message, std::size_t messageCapacity) {
  esp_image_header_t imageHeader = {};
  std::memcpy(&imageHeader, header, sizeof(imageHeader));
  if (imageHeader.magic != ESP_IMAGE_HEADER_MAGIC) {
    setMessage(message, messageCapacity, "Soubor není platný ESP32 firmware.");
    return false;
  }
  if (imageHeader.chip_id != ESP_CHIP_ID_ESP32S3) {
    setMessage(message, messageCapacity,
               "Firmware není určený pro ESP32-S3.");
    return false;
  }

  esp_app_desc_t appDescription = {};
  std::memcpy(&appDescription, header + sizeof(esp_image_header_t) +
                                  sizeof(esp_image_segment_header_t),
              sizeof(appDescription));
  if (appDescription.magic_word != ESP_APP_DESC_MAGIC_WORD) {
    setMessage(message, messageCapacity,
               "Soubor není samostatný firmware této aplikace.");
    return false;
  }

  const esp_app_desc_t* runningDescription = esp_app_get_description();
  if (runningDescription == nullptr ||
      runningDescription->magic_word != ESP_APP_DESC_MAGIC_WORD) {
    setMessage(message, messageCapacity,
               "Nelze ověřit běžící firmware.");
    return false;
  }

  char uploadedProject[sizeof(appDescription.project_name) + 1] = {};
  char runningProject[sizeof(runningDescription->project_name) + 1] = {};
  std::memcpy(uploadedProject, appDescription.project_name,
              sizeof(appDescription.project_name));
  std::memcpy(runningProject, runningDescription->project_name,
              sizeof(runningDescription->project_name));
  uploadedProject[sizeof(appDescription.project_name)] = '\0';
  runningProject[sizeof(runningDescription->project_name)] = '\0';
  if (uploadedProject[0] == '\0' || runningProject[0] == '\0' ||
      std::strcmp(uploadedProject, runningProject) != 0) {
    setMessage(message, messageCapacity,
               "Firmware patří k jiné aplikaci.");
    return false;
  }
  return true;
}

bool beginOta(char* message, std::size_t messageCapacity) {
  if (!validateHeader(message, messageCapacity)) return false;
  if (targetPartition == nullptr ||
      receivedLength > targetPartition->size) {
    setMessage(message, messageCapacity, "Firmware je příliš velký.");
    return false;
  }
  const esp_err_t result =
      esp_ota_begin(targetPartition, OTA_WITH_SEQUENTIAL_WRITES, &otaHandle);
  if (result != ESP_OK) {
    setMessage(message, messageCapacity,
               "Nelze připravit oddíl pro nový firmware.");
    return false;
  }
  otaStarted = true;
  if (esp_ota_write(otaHandle, header, headerLength) != ESP_OK) {
    setMessage(message, messageCapacity,
               "Zápis hlavičky firmwaru selhal.");
    return false;
  }
  return true;
}

}  // namespace

void configure(const Callbacks& requestedCallbacks) { callbacks = requestedCallbacks; }

bool begin(const char* filename,
           std::size_t contentLength,
           char* message,
           std::size_t messageCapacity) {
  if (state != State::Idle) {
    setMessage(message, messageCapacity, "Aktualizace již probíhá.");
    return false;
  }
  if (!hasBinSuffix(filename)) {
    setMessage(message, messageCapacity,
               "Vyber soubor firmwaru s příponou .bin.");
    return false;
  }
  if (callbacks.pauseDisplay == nullptr || callbacks.resumeDisplay == nullptr) {
    setMessage(message, messageCapacity,
               "Aktualizace nemá připravené ovládání displeje.");
    return false;
  }
  targetPartition = esp_ota_get_next_update_partition(nullptr);
  if (targetPartition == nullptr) {
    setMessage(message, messageCapacity,
               "Není dostupný OTA oddíl pro nový firmware.");
    return false;
  }
  if (contentLength != 0 && contentLength > targetPartition->size) {
    setMessage(message, messageCapacity, "Firmware je příliš velký.");
    return false;
  }
  if (contentLength != 0 && contentLength < sizeof(header)) {
    setMessage(message, messageCapacity, "Firmware je příliš malý.");
    return false;
  }
  if (!network_host::acquireFetchGate(5000)) {
    targetPartition = nullptr;
    setMessage(message, messageCapacity,
               "Síť je právě používána, aktualizaci nelze spustit.");
    return false;
  }
  fetchGateHeld = true;
  if (!callbacks.pauseDisplay()) {
    releaseFetchGate();
    targetPartition = nullptr;
    setMessage(message, messageCapacity, "Displej nelze bezpečně zastavit.");
    return false;
  }
  displayPaused = true;
  expectedLength = contentLength;
  state = State::Receiving;
  return true;
}

bool write(const std::uint8_t* data,
           std::size_t length,
           char* message,
           std::size_t messageCapacity) {
  if (state != State::Receiving || data == nullptr) {
    setMessage(message, messageCapacity, "Aktualizace není aktivní.");
    return false;
  }
  if (length == 0) return true;
  if (length > targetPartition->size - receivedLength) {
    failTransaction(message, messageCapacity, "Firmware je příliš velký.");
    return false;
  }
  if (expectedLength != 0 &&
      length > expectedLength - std::min(receivedLength, expectedLength)) {
    failTransaction(message, messageCapacity,
                    "Velikost nahrávaného souboru nesouhlasí.");
    return false;
  }

  std::size_t consumed = 0;
  if (!otaStarted && headerLength < sizeof(header)) {
    const std::size_t toCopy =
        std::min(length, sizeof(header) - headerLength);
    std::memcpy(header + headerLength, data, toCopy);
    headerLength += toCopy;
    consumed = toCopy;
    receivedLength += toCopy;
    if (headerLength == sizeof(header) &&
        !beginOta(message, messageCapacity)) {
      char detail[96] = {};
      if (message != nullptr && messageCapacity > 0) {
        std::strncpy(detail, message, sizeof(detail) - 1);
      }
      failTransaction(message, messageCapacity,
                      detail[0] != '\0' ? detail
                                         : "Firmware nelze připravit k zápisu.");
      return false;
    }
  }

  if (otaStarted && consumed < length) {
    const std::size_t remaining = length - consumed;
    if (esp_ota_write(otaHandle, data + consumed, remaining) != ESP_OK) {
      failTransaction(message, messageCapacity, "Zápis firmwaru selhal.");
      return false;
    }
    receivedLength += remaining;
  }
  return true;
}

bool end(char* message, std::size_t messageCapacity) {
  if (state != State::Receiving || !otaStarted) {
    setMessage(message, messageCapacity, "Aktualizace není kompletní.");
    return false;
  }
  if ((expectedLength != 0 && receivedLength != expectedLength) ||
      receivedLength == 0 || receivedLength > targetPartition->size) {
    failTransaction(message, messageCapacity,
                    "Velikost nahrávaného souboru nesouhlasí.");
    return false;
  }
  const esp_err_t endResult = esp_ota_end(otaHandle);
  otaHandle = 0;
  otaStarted = false;
  if (endResult != ESP_OK) {
    failTransaction(message, messageCapacity,
                    "Ověření nového firmwaru selhalo.");
    return false;
  }
  if (esp_ota_set_boot_partition(targetPartition) != ESP_OK) {
    failTransaction(message, messageCapacity,
                    "Nový firmware nelze nastavit pro další spuštění.");
    return false;
  }

  // The response must be sent while the old UI is still quiescent. The host
  // releases the network lease now, and invokes restartAfterResponse only
  // after WebServer has flushed the success response to the browser.
  releaseFetchGate();
  state = State::RestartPending;
  setMessage(message, messageCapacity,
             "Firmware byl nahrán. Zařízení se restartuje.");
  return true;
}

void abort() {
  if (state == State::Idle || state == State::RestartPending) return;
  abortOtaHandle();
  char ignored[1] = {};
  restoreDisplay(ignored, sizeof(ignored));
  releaseFetchGate();
  resetUploadState();
}

bool restartAfterResponse(char* message, std::size_t messageCapacity) {
  if (state != State::RestartPending) {
    setMessage(message, messageCapacity, "Restart aktualizace není připraven.");
    return false;
  }
  if (callbacks.restart == nullptr) {
    setMessage(message, messageCapacity, "Restart aktualizace není dostupný.");
    return false;
  }
  callbacks.restart();
  // A test callback may return; do not leave the singleton in a state in
  // which a subsequent upload cannot be started.
  displayPaused = false;
  resetUploadState();
  return true;
}

bool inProgress() { return state == State::Receiving; }

bool restartPending() { return state == State::RestartPending; }

}  // namespace manual_firmware_update

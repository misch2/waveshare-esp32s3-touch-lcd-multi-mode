#include "NetworkFetchGate.h"

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace network_host {
namespace {
SemaphoreHandle_t fetchMutex = nullptr;
}

bool initializeFetchGate() {
  if (fetchMutex != nullptr) return true;
  fetchMutex = xSemaphoreCreateMutex();
  return fetchMutex != nullptr;
}

bool acquireFetchGate(std::uint32_t timeoutMs) {
  if (fetchMutex == nullptr) return false;
  const TickType_t timeout =
      timeoutMs == kWaitForever ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
  return xSemaphoreTake(fetchMutex, timeout) == pdTRUE;
}

void releaseFetchGate() {
  if (fetchMutex != nullptr) xSemaphoreGive(fetchMutex);
}

}  // namespace network_host

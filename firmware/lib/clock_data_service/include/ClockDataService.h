#pragma once

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "ClockDashboard.h"

/**
 * Background data owner for the clock dashboard.
 *
 * This service owns only network access and ClockValues publication. It never
 * calls LVGL, ClockDashboard, or display code. The one-item queue deliberately
 * keeps the newest completed sample and drops an older unread sample.
 */
class ClockDataService final {
 public:
  bool begin(const ClockConfig& config);
  bool applyConfig(const ClockConfig& config);
  // Wakes the worker and starts a complete data-source refresh.
  bool requestRefresh();
  // Wakes the worker and refreshes only Home Assistant sun/light state when
  // the current full-refresh cycle is waiting, matching upstream behavior.
  bool requestDayNightRefresh();
  bool consumeValues(ClockValues& values);

  bool running() const { return taskHandle_ != nullptr; }

 private:
  static void taskEntry(void* argument);
  void workerLoop();

  ClockConfig configSnapshot() const;
  void publishValues(const ClockValues& values);
  bool consumeDayNightLightRefreshRequest();

  ClockConfig config_{};
  SemaphoreHandle_t configMutex_ = nullptr;
  QueueHandle_t valuesQueue_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  portMUX_TYPE refreshMux_ = portMUX_INITIALIZER_UNLOCKED;
  volatile bool dayNightLightRefreshRequested_ = false;
};

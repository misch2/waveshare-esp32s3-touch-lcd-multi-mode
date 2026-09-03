#pragma once

#include <cstdint>

using TouchSampleCallback = void (*)(bool pressed, int16_t x, int16_t y,
                                     uint32_t nowMs);

bool displayHostBegin(TouchSampleCallback touchCallback);
void displayHostLoop();
void displayHostRequestFullRedraw();
// UI-context only. Rebuild both panel buffers before entering LVGL direct mode.
void displayHostSetPartialRefresh(bool enabled, bool rebuildBuffers = false);
bool displayHostBeginStorageWrite();
bool displayHostEndStorageWrite();
void displayHostSetBrightness(uint8_t brightness);
void displayHostSetForcedOff(bool forcedOff);
bool displayHostForcedOff();

#pragma once

#include <cstdint>

using TouchSampleCallback = void (*)(bool pressed, int16_t x, int16_t y,
                                     uint32_t nowMs);

bool displayHostBegin(TouchSampleCallback touchCallback);
void displayHostLoop();


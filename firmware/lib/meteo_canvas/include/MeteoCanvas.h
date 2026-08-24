#pragma once

#include <Arduino_GFX_Library.h>

#include <cstdint>

namespace meteo_canvas {

constexpr int16_t kWidth = 480;
constexpr int16_t kHeight = 480;

using PresentCallback = void (*)();

// Initializes the one host-owned RGB565 canvas in PSRAM. The canvas is shared
// by all Meteo renderers; it must be initialized once during host startup.
bool begin();

// Returns the shared renderer target and its backing storage. The returned
// target is also exposed through the upstream-compatible global `gfx` symbol.
Arduino_GFX* graphics();
uint16_t* framebuffer();

// Arduino_GFX::flush() forwards to this callback. The active screen should
// install its callback from show() and clear it from hide().
void setPresentCallback(PresentCallback callback);
void clearPresentCallback();

}  // namespace meteo_canvas

// The pinned Meteo sources use this global renderer pointer. Keep the symbol
// in the shared canvas library so future Meteo screens use the same target.
extern Arduino_GFX* gfx;

#include "MeteoCanvas.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#ifdef BOOT_PIN
#undef BOOT_PIN
#endif

namespace {

class HostMeteoCanvas final : public Arduino_GFX {
 public:
  HostMeteoCanvas() : Arduino_GFX(meteo_canvas::kWidth, meteo_canvas::kHeight) {}

  ~HostMeteoCanvas() override {
    if (frameBuffer_ != nullptr) heap_caps_free(frameBuffer_);
  }

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    (void)speed;
    if (frameBuffer_ == nullptr) {
      frameBuffer_ = static_cast<uint16_t*>(heap_caps_malloc(
          static_cast<size_t>(WIDTH) * HEIGHT * sizeof(uint16_t),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    return frameBuffer_ != nullptr;
  }

  uint16_t* getFramebuffer() { return frameBuffer_; }

  void setPresentCallback(meteo_canvas::PresentCallback callback) {
    presentCallback_ = callback;
  }

  void flush(bool forceFlush = false) override {
    (void)forceFlush;
    if (presentCallback_ != nullptr) presentCallback_();
  }

  void writePixelPreclipped(int16_t x, int16_t y,
                            uint16_t color) override {
    if (frameBuffer_ == nullptr || x < 0 || x >= WIDTH || y < 0 ||
        y >= HEIGHT) {
      return;
    }
    frameBuffer_[static_cast<int32_t>(y) * WIDTH + x] = color;
  }

  void writeFastVLine(int16_t x, int16_t y, int16_t height,
                      uint16_t color) override {
    if (frameBuffer_ == nullptr || height == 0) return;
    if (height < 0) {
      y += height + 1;
      height = -height;
    }
    if (x < 0 || x >= WIDTH || y >= HEIGHT || y + height <= 0) return;
    if (y < 0) {
      height += y;
      y = 0;
    }
    if (y + height > HEIGHT) height = HEIGHT - y;
    uint16_t* pixel = frameBuffer_ + static_cast<int32_t>(y) * WIDTH + x;
    while (height-- > 0) {
      *pixel = color;
      pixel += WIDTH;
    }
  }

  void writeFastHLine(int16_t x, int16_t y, int16_t width,
                      uint16_t color) override {
    if (frameBuffer_ == nullptr || width == 0) return;
    if (width < 0) {
      x += width + 1;
      width = -width;
    }
    if (y < 0 || y >= HEIGHT || x >= WIDTH || x + width <= 0) return;
    if (x < 0) {
      width += x;
      x = 0;
    }
    if (x + width > WIDTH) width = WIDTH - x;
    uint16_t* pixel = frameBuffer_ + static_cast<int32_t>(y) * WIDTH + x;
    while (width-- > 0) *pixel++ = color;
  }

  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t width,
                               int16_t height, uint16_t color) override {
    if (frameBuffer_ == nullptr || width <= 0 || height <= 0) return;
    if (x < 0) {
      width += x;
      x = 0;
    }
    if (y < 0) {
      height += y;
      y = 0;
    }
    if (x + width > WIDTH) width = WIDTH - x;
    if (y + height > HEIGHT) height = HEIGHT - y;
    if (width <= 0 || height <= 0) return;

    uint16_t* row = frameBuffer_ + static_cast<int32_t>(y) * WIDTH + x;
    for (int16_t rowIndex = 0; rowIndex < height; ++rowIndex) {
      uint16_t* pixel = row;
      for (int16_t column = 0; column < width; ++column) *pixel++ = color;
      row += WIDTH;
    }
  }

 private:
  uint16_t* frameBuffer_ = nullptr;
  meteo_canvas::PresentCallback presentCallback_ = nullptr;
};

HostMeteoCanvas canvas;

}  // namespace

Arduino_GFX* gfx = nullptr;

namespace meteo_canvas {

bool begin() {
  if (!canvas.begin()) return false;
  gfx = &canvas;
  return true;
}

Arduino_GFX* graphics() {
  return gfx;
}

uint16_t* framebuffer() {
  return canvas.getFramebuffer();
}

void setPresentCallback(PresentCallback callback) {
  canvas.setPresentCallback(callback);
}

void clearPresentCallback() {
  canvas.setPresentCallback(nullptr);
}

}  // namespace meteo_canvas

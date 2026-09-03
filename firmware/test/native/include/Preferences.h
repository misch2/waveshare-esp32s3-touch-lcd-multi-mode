#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Tiny in-memory stand-in for Arduino-ESP32 Preferences. It intentionally
// models only the byte/blob and scalar operations used by ClockConfig.cpp.
namespace native_preferences {

inline std::vector<std::uint8_t> blob;

inline void setBytes(const void* data, std::size_t size) {
  const auto* source = static_cast<const std::uint8_t*>(data);
  blob.assign(source, source + size);
}

inline void clear() { blob.clear(); }

}  // namespace native_preferences

class Preferences {
 public:
  bool begin(const char*, bool, const char*) { return true; }
  void end() {}

  std::size_t getBytesLength(const char*) const {
    return native_preferences::blob.size();
  }

  std::size_t getBytes(const char*, void* destination, std::size_t size) const {
    if (size > native_preferences::blob.size()) return 0;
    std::memcpy(destination, native_preferences::blob.data(), size);
    return size;
  }

  std::size_t putBytes(const char*, const void* source, std::size_t size) {
    native_preferences::setBytes(source, size);
    return size;
  }

  bool remove(const char*) {
    native_preferences::clear();
    return true;
  }

  std::uint8_t getUChar(const char*, std::uint8_t fallback) const {
    return fallback;
  }
  std::uint32_t getUInt(const char*, std::uint32_t fallback) const {
    return fallback;
  }
  bool getBool(const char*, bool fallback) const { return fallback; }
  std::size_t putUChar(const char*, std::uint8_t) { return sizeof(std::uint8_t); }
  std::size_t putUInt(const char*, std::uint32_t) { return sizeof(std::uint32_t); }
  std::size_t putBool(const char*, bool) { return sizeof(bool); }
};

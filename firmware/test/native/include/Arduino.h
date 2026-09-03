#pragma once

// Minimal compatibility surface for dependency-free tests of upstream inline
// weather-icon mapping helpers. No Arduino runtime APIs are used here.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

class String {
 public:
  String() = default;
  explicit String(const char* value) : value_(value == nullptr ? "" : value) {}
  const char* c_str() const { return value_; }

 private:
  const char* value_ = "";
};

template <typename T, typename L, typename H>
constexpr T constrain(T value, L lower, H upper) {
  return value < static_cast<T>(lower)
             ? static_cast<T>(lower)
             : (value > static_cast<T>(upper) ? static_cast<T>(upper) : value);
}

inline std::size_t strlcpy(char* destination, const char* source,
                           std::size_t destinationSize) {
  if (destinationSize == 0) return source == nullptr ? 0 : std::strlen(source);
  const char* input = source == nullptr ? "" : source;
  const std::size_t length = std::strlen(input);
  const std::size_t copied = length < destinationSize - 1 ? length
                                                            : destinationSize - 1;
  std::memcpy(destination, input, copied);
  destination[copied] = '\0';
  return length;
}

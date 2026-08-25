#pragma once

#include <cstdint>

namespace app_core {

inline constexpr std::uint8_t kClockWeatherIconStyleMonochrome = 0;

struct ClockWeatherAnimationPolicy {
  bool configuredEnabled = false;
  bool openMeteo = false;
  bool leftUsesWeather = false;
  bool rightUsesWeather = false;
  bool nightMode = false;
  std::uint8_t configuredStyle = kClockWeatherIconStyleMonochrome;
};

struct ClockWeatherAnimationDecision {
  bool enabled = false;
  std::uint8_t effectiveStyle = kClockWeatherIconStyleMonochrome;
};

ClockWeatherAnimationDecision selectClockWeatherAnimation(
    const ClockWeatherAnimationPolicy& policy);

}  // namespace app_core

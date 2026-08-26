#pragma once

#include <cstdint>

namespace app_core {

/**
 * Host-independent scheduling state for Meteo's shared outside temperature.
 *
 * The forecast request already contains the current temperature.  A forecast
 * update therefore suppresses the standalone temperature request for a short
 * freshness window, while a standalone request remains available as a
 * fallback when the forecast screen is disabled or cannot refresh.
 */
class MeteoOutsideTemperaturePolicy final {
 public:
  static constexpr std::uint32_t kRefreshIntervalMs = 10 * 60 * 1000;
  static constexpr std::uint32_t kRetryIntervalMs = 60 * 1000;
  static constexpr std::uint32_t kForecastFreshIntervalMs = 35 * 60 * 1000;

  /** Return whether the caller may start one standalone temperature request. */
  bool shouldFetch(std::uint32_t nowMs) const noexcept {
    if (forecastTemperatureFresh(nowMs)) return false;
    if (!attempted_) return true;

    const std::uint32_t interval = haveTemperature_ ? kRefreshIntervalMs
                                                     : kRetryIntervalMs;
    return elapsed(nowMs, lastAttemptMs_) >= interval;
  }

  /** Record that a standalone temperature request was started. */
  void noteAttempt(std::uint32_t nowMs) noexcept {
    attempted_ = true;
    lastAttemptMs_ = nowMs;
  }

  /** Record a valid value returned by the standalone temperature request. */
  void noteStandaloneTemperature() noexcept { haveTemperature_ = true; }

  /**
   * Record a current temperature delivered by the forecast request.
   * Forecast data is also the shared status-line value, so it refreshes the
   * fallback freshness window even when the forecast screen is not visible.
   */
  void noteForecastTemperature(std::uint32_t nowMs) noexcept {
    haveTemperature_ = true;
    forecastTemperatureAtMs_ = nowMs;
    haveForecastTemperature_ = true;
  }

  bool hasTemperature() const noexcept { return haveTemperature_; }

 private:
  static std::uint32_t elapsed(std::uint32_t nowMs,
                               std::uint32_t thenMs) noexcept {
    return nowMs - thenMs;
  }

  bool forecastTemperatureFresh(std::uint32_t nowMs) const noexcept {
    return haveForecastTemperature_ &&
           elapsed(nowMs, forecastTemperatureAtMs_) <
               kForecastFreshIntervalMs;
  }

  bool attempted_ = false;
  bool haveTemperature_ = false;
  bool haveForecastTemperature_ = false;
  std::uint32_t lastAttemptMs_ = 0;
  std::uint32_t forecastTemperatureAtMs_ = 0;
};

}  // namespace app_core

#include "ClockWeatherAnimationPolicy.h"

namespace app_core {

ClockWeatherAnimationDecision selectClockWeatherAnimation(
    const ClockWeatherAnimationPolicy& policy) {
  ClockWeatherAnimationDecision decision;
  decision.enabled =
      policy.configuredEnabled &&
      (policy.openMeteo || policy.leftUsesWeather || policy.rightUsesWeather);
  decision.effectiveStyle = policy.nightMode
                                ? kClockWeatherIconStyleMonochrome
                                : policy.configuredStyle;
  return decision;
}

}  // namespace app_core

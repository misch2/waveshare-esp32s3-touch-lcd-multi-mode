#pragma once

#include <cstdint>

namespace app_core {

/**
 * Result category used by the Home Assistant batch transport policy.
 *
 * A positive HTTP status means that the peer completed an HTTP exchange,
 * even when the application rejected the request (401, 404, 500, ...).
 * Non-positive values are transport/client failures; on ESP32 this also
 * includes TLS allocation errors reported by NetworkClientSecure.
 */
enum class HomeAssistantBatchResult : std::uint8_t {
    Success,
    HttpApplicationError,
    TransportFailure,
};

struct HomeAssistantRequestDecision {
    HomeAssistantBatchResult result;
    bool continueBatch;
    bool retryRequest;
};

/**
 * Small, Arduino-independent policy for a multi-entity Home Assistant
 * refresh. It intentionally treats a transport failure as a batch failure:
 * retrying the remaining entities immediately creates a TLS error storm when
 * internal RAM is fragmented. A real HTTP response permits the caller to
 * continue with the next entity and clears transport backoff.
 *
 * The clock service owns the actual HTTP client and timing. It can use this
 * class around that existing code without moving parsing or networking into
 * app_core.
 */
class HomeAssistantBatchPolicy final {
 public:
    static constexpr std::uint32_t kDefaultBaseBackoffMs = 5000;
    static constexpr std::uint32_t kDefaultMaxBackoffMs = 5 * 60 * 1000;

    explicit HomeAssistantBatchPolicy(
        std::uint32_t baseBackoffMs = kDefaultBaseBackoffMs,
        std::uint32_t maxBackoffMs = kDefaultMaxBackoffMs) noexcept
        : baseBackoffMs_(baseBackoffMs == 0 ? 1 : baseBackoffMs),
          maxBackoffMs_(maxBackoffMs < baseBackoffMs_
                            ? baseBackoffMs_
                            : maxBackoffMs) {}

    static HomeAssistantBatchResult classifyStatus(int status) noexcept {
        if (status == 200) return HomeAssistantBatchResult::Success;
        return status > 0 ? HomeAssistantBatchResult::HttpApplicationError
                          : HomeAssistantBatchResult::TransportFailure;
    }

    static HomeAssistantRequestDecision decide(int status) noexcept {
        const HomeAssistantBatchResult result = classifyStatus(status);
        const bool retryApplicationRequest =
            result == HomeAssistantBatchResult::HttpApplicationError &&
            (status == 408 || status == 429 || status >= 500);
        return {
            result,
            result != HomeAssistantBatchResult::TransportFailure,
            retryApplicationRequest,
        };
    }

    /** Record a completed batch and update transport backoff state. */
    void recordBatchResult(HomeAssistantBatchResult result,
                           std::uint32_t nowMs) noexcept {
        if (result != HomeAssistantBatchResult::TransportFailure) {
            reset();
            return;
        }

        if (consecutiveTransportFailures_ < 31) {
            ++consecutiveTransportFailures_;
        }
        const std::uint64_t multiplier =
            std::uint64_t{1} << (consecutiveTransportFailures_ - 1);
        const std::uint64_t candidate =
            static_cast<std::uint64_t>(baseBackoffMs_) * multiplier;
        backoffMs_ = candidate > maxBackoffMs_
                         ? maxBackoffMs_
                         : static_cast<std::uint32_t>(candidate);
        nextAllowedAtMs_ = nowMs + backoffMs_;
        backoffActive_ = true;
    }

    /** Return false while a previous transport failure is backing off. */
    bool canStart(std::uint32_t nowMs) const noexcept {
        return remainingDelayMs(nowMs) == 0;
    }

    /** Return the wrap-safe remaining delay in milliseconds. */
    std::uint32_t remainingDelayMs(std::uint32_t nowMs) const noexcept {
        if (!backoffActive_) return 0;
        const std::int32_t remaining =
            static_cast<std::int32_t>(nextAllowedAtMs_ - nowMs);
        return remaining > 0 ? static_cast<std::uint32_t>(remaining) : 0;
    }

    std::uint8_t consecutiveTransportFailures() const noexcept {
        return consecutiveTransportFailures_;
    }

    void reset() noexcept {
        consecutiveTransportFailures_ = 0;
        backoffMs_ = 0;
        nextAllowedAtMs_ = 0;
        backoffActive_ = false;
    }

 private:
    std::uint32_t baseBackoffMs_;
    std::uint32_t maxBackoffMs_;
    std::uint32_t backoffMs_ = 0;
    std::uint32_t nextAllowedAtMs_ = 0;
    std::uint8_t consecutiveTransportFailures_ = 0;
    bool backoffActive_ = false;
};

}  // namespace app_core

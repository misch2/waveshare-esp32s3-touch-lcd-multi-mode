#pragma once

#include <cstdint>

namespace network_host {

constexpr std::uint32_t kWaitForever = UINT32_MAX;

bool initializeFetchGate();
bool acquireFetchGate(std::uint32_t timeoutMs = kWaitForever);
void releaseFetchGate();

class FetchLease {
 public:
  explicit FetchLease(std::uint32_t timeoutMs = kWaitForever)
      : acquired_(acquireFetchGate(timeoutMs)) {}
  ~FetchLease() {
    if (acquired_) releaseFetchGate();
  }

  FetchLease(const FetchLease&) = delete;
  FetchLease& operator=(const FetchLease&) = delete;

  bool acquired() const { return acquired_; }
  explicit operator bool() const { return acquired_; }

 private:
  bool acquired_;
};

}  // namespace network_host

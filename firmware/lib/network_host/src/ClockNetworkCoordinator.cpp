// Adapt the upstream network guard to the host's sole TLS/flash fetch gate.
// Do not compile NetworkCoordinator.cpp from the standalone clock application.
#include "NetworkCoordinator.h"
#include "NetworkFetchGate.h"

void networkCoordinatorBegin() { network_host::initializeFetchGate(); }
bool networkCoordinatorAcquire(uint32_t timeoutMs) {
  return network_host::acquireFetchGate(timeoutMs);
}
void networkCoordinatorRelease() { network_host::releaseFetchGate(); }

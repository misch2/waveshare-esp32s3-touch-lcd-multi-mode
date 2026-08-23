#pragma once

#include <ctime>

namespace network_host {

// Starts the single host-owned Wi-Fi provisioning and SNTP lifecycle.
bool begin();

// Services Improv Serial, reconnects Wi-Fi and observes SNTP state.
void loop();

bool connected();
bool timeSynchronized();
bool localTime(std::tm& value);
const char* ipAddress();

}  // namespace network_host

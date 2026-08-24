#include "NetworkHost.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include "ImprovSerialService.h"
#include "NetworkFetchGate.h"
#include "WifiProvisioning.h"

namespace network_host {
namespace {
constexpr uint32_t kNtpSyncIntervalMs = 60UL * 60UL * 1000UL;
constexpr time_t kValidTimeThreshold = 1700000000;
constexpr char kTimezone[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
// Keep the upstream name while its Improv response points to this address.
constexpr char kMdnsName[] = "waveshare-hodiny";

bool started = false;
bool synchronized = false;
bool mdnsStarted = false;
bool wasConnected = false;
char currentIp[16] = {};

void observeConnection() {
  const bool online = WiFi.status() == WL_CONNECTED;
  if (!online) {
    currentIp[0] = '\0';
    wasConnected = false;
    return;
  }

  const String ip = WiFi.localIP().toString();
  strlcpy(currentIp, ip.c_str(), sizeof(currentIp));
  if (!wasConnected) {
    wasConnected = true;
    Serial.printf("Wi-Fi connected: %s\n", currentIp);
  }

  if (!mdnsStarted) {
    mdnsStarted = MDNS.begin(kMdnsName);
    if (mdnsStarted) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("mDNS HTTP: http://%s.local/\n", kMdnsName);
    }
  }

  if (synchronized) return;
  const time_t now = time(nullptr);
  if (now >= kValidTimeThreshold) {
    synchronized = true;
    Serial.println("SNTP synchronized");
  }
}
}  // namespace

bool begin() {
  if (started) return true;

  if (!initializeFetchGate()) {
    Serial.println("Network: fetch gate allocation failed");
    return false;
  }

  // These are the pinned waveshare-hodiny provisioning functions. The host
  // owns their lifecycle; no screen module may start another Wi-Fi stack.
  improvSerialServiceInit(wifiProvisioningStart);
  wifiProvisioningBegin();
  configTzTime(kTimezone, "pool.ntp.org", "time.cloudflare.com");
  sntp_set_sync_interval(kNtpSyncIntervalMs);
  started = true;
  observeConnection();
  return true;
}

void loop() {
  if (!started) return;
  improvSerialServiceLoop();
  wifiProvisioningLoop();
  observeConnection();
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

bool timeSynchronized() { return synchronized; }

bool localTime(std::tm& value) {
  if (!synchronized) return false;
  const time_t now = time(nullptr);
  if (now < kValidTimeThreshold) return false;
  return localtime_r(&now, &value) != nullptr;
}

const char* ipAddress() { return currentIp; }

}  // namespace network_host

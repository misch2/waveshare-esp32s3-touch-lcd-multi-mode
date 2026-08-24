// Compile exactly one copy of the pinned web implementation. HTML, JSON
// serialization, validation and authentication remain upstream. The
// integration host injects its caller-owned WebServer through the upstream
// route-registration seam.
// Source revision: 9537a76932fc9269b2a22a5fb90a62785897c680.
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "../../../../waveshare-hodiny/WaveshareHodiny/ConfigurationWeb.cpp"

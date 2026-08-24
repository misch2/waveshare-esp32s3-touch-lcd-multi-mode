#pragma once

#include "MeteoWebRoutes.h"

namespace meteo_web {

// Register the adapted MeteoPlaneRadar configuration page and API on the
// caller-owned WebServer.  This function deliberately does not start or poll
// the server; WebHost remains the only owner of that lifecycle.
bool registerRoutes(const app_core::MeteoWebRoutes& routes);

}  // namespace meteo_web

#pragma once

#include "sim/SimHttpServer.h"

namespace awtrix {

class Tc002Board;
struct DeviceConfig;

// What the clock adds to the simulator's HTTP server: HTTP basic authentication, the provisioning
// gate, live Wi-Fi scanning, the rotary encoder test route, and the firmware upload.
SimHttpExtension tc002HttpExtension(SimHttpServer& server, Tc002Board& board, DeviceConfig& cfg);

}

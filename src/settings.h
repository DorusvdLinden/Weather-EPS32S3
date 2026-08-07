#pragma once

#include "config.h"

// NVS-backed persistence for DisplayConfig (which, despite the name, is now
// the full runtime settings blob - see config.h). Everything not yet saved
// falls back to DisplayConfig's own default member initializers, so a
// factory-fresh device behaves exactly like the old compile-time defaults
// until someone visits the setup portal.
namespace settings {

DisplayConfig load();
void save(const DisplayConfig& config);

// True once a non-empty WiFi SSID has been saved - main.cpp uses this as
// one of the two setup-portal trigger conditions (the other being the BOOT
// button held at boot).
bool hasWifiCredentials();

}  // namespace settings

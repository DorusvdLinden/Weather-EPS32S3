#pragma once

// SoftAP + captive-portal web UI for WiFi/location/display settings and
// viewing the last cycle's debug log + rendered preview - see README's
// "Web setup/debug portal" section. Entered when either no WiFi credentials
// are saved yet, or the BOOT button is held at boot (see main.cpp and
// config.h's `pins::kBootButton`).
namespace setup_portal {

// Blocks indefinitely, serving requests, until settings are saved or an
// idle timeout elapses - either way it reboots the device itself and never
// returns.
[[noreturn]] void run();

}  // namespace setup_portal

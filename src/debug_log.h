#pragma once

#include <Arduino.h>

// Step-by-step progress log for one wake cycle (startup -> WiFi -> fetch ->
// render -> panel push -> sleep), persisted to LittleFS so it survives deep
// sleep and can be read back from the setup/debug web portal's /log route.
// There's no RTC/NTP wall clock on this device (see open_meteo.h's
// timezone note - deliberately not carried), so entries are keyed by an
// RTC-memory cycle counter (main.cpp) rather than a timestamp.
//
// Bounded, not a ring buffer: if the log has already grown past kMaxBytes
// (see debug_log.cpp) by the time a new cycle starts, it's cleared first
// rather than trimmed line by line - simple, and still leaves plenty of
// recent-cycle history to debug from.
namespace debuglog {

void beginCycle(uint32_t cycleNumber);
void logStep(const char* step, bool ok, const String& detail = "");
void endCycle();

String readAll();
void clear();

}  // namespace debuglog

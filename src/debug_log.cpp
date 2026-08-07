#include "debug_log.h"

#include <LittleFS.h>

namespace debuglog {

namespace {
constexpr const char* kLogPath = "/debug_log.txt";
constexpr size_t kMaxBytes = 32 * 1024;

// LittleFS.begin() is cheap and idempotent once already mounted - main.cpp
// mounts it once at boot, but every function here re-checks defensively so
// a debug-logging failure can never take down the actual weather-display
// pipeline, only silently drop log entries.
bool mounted() { return LittleFS.begin(false); }

void appendLine(const String& line) {
    if (!mounted()) return;
    File f = LittleFS.open(kLogPath, FILE_APPEND);
    if (!f) return;
    f.println(line);
    f.close();
}
}  // namespace

void beginCycle(uint32_t cycleNumber) {
    if (!mounted()) return;
    File existing = LittleFS.open(kLogPath, FILE_READ);
    size_t size = existing ? existing.size() : 0;
    if (existing) existing.close();
    if (size > kMaxBytes) clear();

    appendLine("==== cycle " + String(cycleNumber) + " ====");
}

void logStep(const char* step, bool ok, const String& detail) {
    String line = String("[") + (ok ? "OK  " : "FAIL") + "] " + step;
    if (detail.length()) line += " - " + detail;
    appendLine(line);
}

void endCycle() { appendLine(""); }

String readAll() {
    if (!mounted()) return "(LittleFS not available)";
    File f = LittleFS.open(kLogPath, FILE_READ);
    if (!f) return "(no log yet)";
    String content = f.readString();
    f.close();
    return content;
}

void clear() {
    if (!mounted()) return;
    LittleFS.remove(kLogPath);
}

}  // namespace debuglog

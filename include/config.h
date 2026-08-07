#pragma once

#include <Arduino.h>
#include <cstdint>

// Everything needed to fetch and render one weather snapshot, plus WiFi
// credentials - this doubles as the full runtime settings blob edited via
// the setup/debug web portal (see src/setup_portal.h) and persisted to NVS
// (see src/settings.h). Originally a constexpr struct mirroring InkyPiZero's
// config.py dataclass; string fields became `String` (not `const char*`)
// specifically so they can hold values loaded from NVS at boot rather than
// only compile-time literals. The default member initializers below are
// still the factory defaults settings::load() falls back to on first boot
// or for any individual field never explicitly saved.
struct DisplayConfig {
    String wifiSsid;      // empty until set via the setup portal
    String wifiPassword;

    double latitude = 51.0004365;
    double longitude = 5.8993687;
    String units = "metric";          // "metric" | "imperial" | "standard"
    String timezone = "Europe/Amsterdam";
    String time_format = "24h";       // "24h" | "12h"
    int forecast_days = 7;
    int graph_icon_step = 2;
    bool show_moon_phase = false;
    // White, not InkyPiZero's original 0xFFF8E5 cream tone - the ACeP
    // palette (src/palette.h) has no near neighbor for a pastel, so it
    // dithered into a visible speckle instead of a clean fill (confirmed
    // via tools/mock_render.py's panel-quantized output). White is one of
    // the 7 palette colors exactly, so it dithers with zero error.
    uint32_t background_color = 0xFFFFFF;  // 0xRRGGBB
    uint32_t text_color = 0x000000;
    unsigned refresh_interval_seconds = 600;
};

// EPD panel wiring, checked (not yet physically confirmed - no board in
// this environment) against an ARCELI ESP32-S3-WROOM-1-N16R8
// ESP32-S3-DevKitC-1 clone (amazon.nl/dp/B0FQBYLL6S) - the exact N16R8/
// DevKitC-1 combination this repo already targets in platformio.ini
// (memory_type=qio_opi, flash_size=16MB), cross-referenced against
// Espressif's own ESP32-S3-DevKitC-1 user guide. Reserved on this board:
//   - GPIO 26-32: quad SPI flash - not broken out to headers at all.
//   - GPIO 33-37: EXTRA octal-PSRAM data lines on N16R8/octal-PSRAM
//     variants specifically (33-34 not broken out either; 35-37 are
//     broken out but "used for internal communication... thus not
//     available for external use" per Espressif) - a quad-PSRAM board
//     would not reserve these, so don't copy a generic DevKitC-1 pinout
//     without checking the PSRAM variant.
//   - GPIO 19/20: native USB D-/D+.
//   - GPIO 0/3/45/46: strapping pins (boot mode / voltage select / JTOK).
//   - GPIO 43/44: UART0 TX/RX.
//   - GPIO 48: onboard RGB LED.
// GPIO 7-12 (chosen below) and GPIO 0 (BOOT button, see kBootButton) all
// fall outside every one of those - safe on both the official Espressif
// board and this clone, which follows the same reference pinout.
namespace pins {
constexpr int kEpdCs = 10;
constexpr int kEpdDc = 9;
constexpr int kEpdRst = 8;
constexpr int kEpdBusy = 7;
constexpr int kEpdSck = 12;
constexpr int kEpdMosi = 11;
constexpr int kEpdMiso = -1;  // EPD is write-only; SPI.begin() still wants a value.

// The ESP32-S3-DevKitC-1's onboard BOOT button, active-low - reused as the
// setup/debug portal trigger (see main.cpp) so entering setup mode needs no
// extra wiring beyond the dev board itself. GPIO0 is also a strapping pin,
// but that only matters during the boot ROM's own startup, which has
// already completed by the time application code reads it here - the same
// pattern countless ESP32 sketches use for a "hold BOOT to reset" gesture.
constexpr int kBootButton = 0;
}  // namespace pins

// Deep-sleep wake cadence in microseconds, from the (now runtime-loaded)
// refresh interval.
inline uint64_t wakeIntervalUs(const DisplayConfig& config) {
    return static_cast<uint64_t>(config.refresh_interval_seconds) * 1000000ULL;
}

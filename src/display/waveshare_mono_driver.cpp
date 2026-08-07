#include "waveshare_mono_driver.h"

#include <GxEPD2_BW.h>
#include <SPI.h>
#include <esp_heap_caps.h>

#include "config.h"

namespace {
using Panel = GxEPD2_750_GDEY075T7;
using Display = GxEPD2_BW<Panel, Panel::HEIGHT>;
Display* g_display = nullptr;
}  // namespace

bool WaveshareMonoDriver::begin() {
    SPI.begin(pins::kEpdSck, pins::kEpdMiso, pins::kEpdMosi, pins::kEpdCs);

    // Same PSRAM-placement reasoning as EpdDriver - GxEPD2's page buffer is
    // a fixed-size member array, not something we can redirect to PSRAM
    // after construction. Smaller here (48000B for 1bpp vs. 192000B for
    // 4bpp - see README risk #5) but placed in PSRAM anyway for consistency
    // with the other drivers rather than trusting it fits internal SRAM
    // alongside WiFi/TLS buffers.
    void* mem = heap_caps_malloc(sizeof(Display), MALLOC_CAP_SPIRAM);
    if (!mem) return false;
    g_display = new (mem) Display(Panel(pins::kEpdCs, pins::kEpdDc, pins::kEpdRst, pins::kEpdBusy));

    g_display->init(115200, true, 2, false);
    g_display->setRotation(0);
    g_display->setFullWindow();
    g_display->firstPage();
    return true;
}

void WaveshareMonoDriver::setPixel(int16_t x, int16_t y, palette::Index color) {
    if (g_display) g_display->drawPixel(x, y, palette::kRgb565[color]);
}

void WaveshareMonoDriver::present() {
    if (!g_display) return;
    g_display->nextPage();
    g_display->hibernate();
}

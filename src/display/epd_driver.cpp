#include "epd_driver.h"

#include <GxEPD2_7C.h>
#include <SPI.h>
#include <esp_heap_caps.h>

#include "config.h"

namespace {
using Panel = GxEPD2_730c_ACeP_730;
using Display = GxEPD2_7C<Panel, Panel::HEIGHT>;
Display* g_display = nullptr;
}  // namespace

bool EpdDriver::begin() {
    SPI.begin(pins::kEpdSck, pins::kEpdMiso, pins::kEpdMosi, pins::kEpdCs);

    // GxEPD2_7C's internal page buffer is a fixed-size member array
    // (WIDTH/2 * page_height bytes - 192000B for our single full-height
    // page), not something GxEPD2 allocates via `new` that we could redirect
    // to PSRAM after the fact. The only way to put it in PSRAM is to place
    // the *entire display object* there ourselves via placement new (README
    // risk #5) - a plain global/static `Display display(...)` would land in
    // internal SRAM and, combined with WiFi/TLS buffers, risks running out.
    void* mem = heap_caps_malloc(sizeof(Display), MALLOC_CAP_SPIRAM);
    if (!mem) return false;
    g_display = new (mem) Display(Panel(pins::kEpdCs, pins::kEpdDc, pins::kEpdRst, pins::kEpdBusy));

    g_display->init(115200, true, 2, false);
    g_display->setRotation(0);
    g_display->setFullWindow();
    g_display->firstPage();
    return true;
}

void EpdDriver::setPixel(int16_t x, int16_t y, palette::Index color) {
    if (g_display) g_display->drawPixel(x, y, palette::kRgb565[color]);
}

void EpdDriver::present() {
    if (!g_display) return;
    // page_height == HEIGHT means exactly one page, so this single
    // nextPage() call both flushes the already-drawn buffer and triggers
    // the real panel refresh (see GxEPD2_7C's firstPage()/nextPage() paging
    // protocol - collapses to a single iteration here).
    g_display->nextPage();
    g_display->hibernate();
}

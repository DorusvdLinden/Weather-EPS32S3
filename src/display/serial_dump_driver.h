#pragma once

#include "display/display_sink.h"

// Debug driver: dumps the dithered framebuffer over Serial instead of
// driving real panel hardware. Fills the role InkyPiZero's
// display/mock_driver.py (--mock-output) played - Phase 2's exit criterion
// is validating this dump against InkyPiZero's mock_display_output/
// renders, entirely headless. Pair with tools/dump_to_png.py to reassemble
// the dump into a viewable PNG on a host machine.
class SerialDumpDriver : public DisplaySink {
   public:
    bool begin() override;
    void setPixel(int16_t x, int16_t y, palette::Index color) override;
    void present() override;

   private:
    uint8_t* buf_ = nullptr;  // packed 4-bit palette index, 2px/byte, same layout as the real panel's native format
};

#pragma once

#include "display/display_sink.h"

// Raw-SPI driver for the Pimoroni Inky Impression 7.3" (AC073TC1A
// controller, 800x480, 7-color ACeP) - ported line-by-line from Pimoroni's
// own `inky.inky_ac073tc1a` Python driver (MIT license, see
// docs/attribution.md), since GxEPD2 has no support for this exact
// controller (README Phase 0b / risk #2: "porting the SPI/waveform
// sequence... with no existing C driver to lean on"). This IS that porting
// work, done as faithfully to the reference as possible rather than
// reverse-engineered from a datasheet - but it is **completely unverified
// on real hardware** (none available in this environment). See
// inky_impression_driver.cpp's top comment for the single highest-risk
// assumption in this port (BUSY pin polarity) before trusting it with a
// real panel.
class InkyImpressionDriver : public DisplaySink {
   public:
    bool begin() override;
    void setPixel(int16_t x, int16_t y, palette::Index color) override;
    void present() override;

   private:
    uint8_t* buf_ = nullptr;  // packed 4-bit palette index, 2px/byte - same layout as FrameStore/SerialDumpDriver
};

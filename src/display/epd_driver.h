#pragma once

#include "display/display_sink.h"

// Wraps GxEPD2_7C<GxEPD2_730c_ACeP_730, HEIGHT> (see README Phase 0b - the
// investigated low-risk default panel driver). The GxEPD2 type itself is
// kept out of this header so nothing else in the codebase needs to know
// about GxEPD2/Adafruit_GFX headers just to hold a DisplaySink pointer.
class EpdDriver : public DisplaySink {
   public:
    bool begin() override;
    void setPixel(int16_t x, int16_t y, palette::Index color) override;
    void present() override;
};

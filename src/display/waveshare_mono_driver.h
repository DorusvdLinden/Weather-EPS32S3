#pragma once

#include "display/display_sink.h"

// Wraps GxEPD2_BW<GxEPD2_750_GDEY075T7, HEIGHT> - the Waveshare 7.5" e-Paper
// V2 (800x480, monochrome, ~1.2s full refresh per GxEPD2's own timing
// constant vs. the ACeP panel's ~28-40s - see README Key risk #1). Pair
// with dither::floydSteinbergMono() rather than the 7-color
// dither::floydSteinberg() - see that function's doc comment.
class WaveshareMonoDriver : public DisplaySink {
   public:
    bool begin() override;
    void setPixel(int16_t x, int16_t y, palette::Index color) override;
    void present() override;
};

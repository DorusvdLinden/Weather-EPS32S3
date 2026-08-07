#pragma once

#include <cstdint>

#include "palette.h"

// Common interface for wherever the dithered framebuffer goes - the real
// panel (epd_driver) or the serial-dump debug driver (serial_dump_driver).
// Mirrors InkyPiZero's display/inky_driver.py vs display/mock_driver.py
// split (see README module mapping) so main.cpp's render pipeline doesn't
// care which one it's writing to.
class DisplaySink {
   public:
    virtual ~DisplaySink() = default;
    virtual bool begin() = 0;
    virtual void setPixel(int16_t x, int16_t y, palette::Index color) = 0;
    virtual void present() = 0;
};

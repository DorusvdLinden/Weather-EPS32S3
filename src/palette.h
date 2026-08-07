#pragma once

#include <cstdint>

// The ACeP 7-color palette. Two representations, deliberately different and
// both intentional (not a bug if they don't match):
//
// - kRgb (RGB888): Pimoroni's own *measured* panel colors (`SATURATED_PALETTE`
//   in `inky.inky_ac073tc1a`, MIT-licensed - see docs/attribution.md), i.e.
//   what the physical ACeP panel actually reflects, not idealized primaries.
//   Used for dithering *decisions* (palette::nearest(), error diffusion in
//   dither.cpp) - using the real measured colors there means Floyd-Steinberg
//   sorts each pixel into the bucket it will actually look most like, not
//   the bucket a naive pure-RGB distance would pick.
// - kRgb565: the standard RGB565-encoded primaries GxEPD2_7C's own color
//   discriminator maps to its native panel colors (GxEPD_BLACK/WHITE/GREEN/
//   BLUE/RED/YELLOW/ORANGE in the GxEPD2 headers) - a wire-protocol constant,
//   not a color approximation, so it stays as pure primaries regardless of
//   what kRgb says. Reusing this encoding for widget code drawing through
//   Adafruit_GFX's normal uint16_t-color API means those colors are already
//   exactly what GxEPD2-backed panels expect, no separate table to keep in
//   sync. Panels driven without GxEPD2 (e.g. InkyImpressionDriver, which
//   talks raw SPI) use kRgb's index directly instead and never touch this.
namespace palette {

enum Index : uint8_t {
    kBlack = 0,
    kWhite = 1,
    kGreen = 2,
    kBlue = 3,
    kRed = 4,
    kYellow = 5,
    kOrange = 6,
    kCount = 7,
};

struct Rgb888 {
    uint8_t r, g, b;
};

// Index-aligned with the Index enum above. Pimoroni's `SATURATED_PALETTE`
// (saturation=1.0 blend) - see the namespace comment above.
inline constexpr Rgb888 kRgb[kCount] = {
    {0, 0, 0},        // Black
    {217, 242, 255},  // White
    {3, 124, 76},      // Green
    {27, 46, 198},     // Blue
    {245, 80, 34},     // Red
    {255, 255, 68},    // Yellow
    {239, 121, 44},    // Orange
};

// RGB565 encodings matching GxEPD2's GxEPD_* macros (see GxEPD2.h) - pure
// primaries, deliberately NOT updated to match kRgb above (see namespace
// comment). Index-aligned with the Index enum above.
inline constexpr uint16_t kRgb565[kCount] = {
    0x0000,  // Black
    0xFFFF,  // White
    0x07E0,  // Green
    0x001F,  // Blue
    0xF800,  // Red
    0xFFE0,  // Yellow
    0xFC00,  // Orange
};

inline Rgb888 unpackRgb565(uint16_t color) {
    uint8_t r5 = (color >> 11) & 0x1F;
    uint8_t g6 = (color >> 5) & 0x3F;
    uint8_t b5 = color & 0x1F;
    return Rgb888{
        static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
        static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
        static_cast<uint8_t>((b5 << 3) | (b5 >> 2)),
    };
}

inline constexpr uint16_t packRgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline int distanceSquared(const Rgb888& a, const Rgb888& b) {
    int dr = int(a.r) - int(b.r);
    int dg = int(a.g) - int(b.g);
    int db = int(a.b) - int(b.b);
    return dr * dr + dg * dg + db * db;
}

inline Index nearest(const Rgb888& c) {
    Index best = kBlack;
    int bestDist = distanceSquared(c, kRgb[kBlack]);
    for (uint8_t i = 1; i < kCount; ++i) {
        int d = distanceSquared(c, kRgb[i]);
        if (d < bestDist) {
            bestDist = d;
            best = static_cast<Index>(i);
        }
    }
    return best;
}

}  // namespace palette

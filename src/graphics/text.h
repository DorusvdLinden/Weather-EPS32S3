#pragma once

#include <cstdint>

#include "graphics/rgb_canvas.h"

// Thin anchor-based wrapper around U8g2_for_Adafruit_GFX, playing the role
// InkyPiZero's Pillow `draw.text(..., anchor=...)` calls did in canvas.py -
// see README's font fallback note for why U8g2 fonts were chosen over
// baking the original Jost typeface (module mapping / Phase 5 note in
// assets/asset_store.h has the full rationale).
namespace text {

enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Baseline, Bottom };

void begin(RgbCanvas& canvas);

// `direction` matches u8g2's setFontDirection(): 0 = normal, 1 = 90 deg
// clockwise, 2 = 180, 3 = 270 deg clockwise (270 is what reads bottom-to-top
// in screen coordinates - used for the chart's vertical axis labels).
void draw(const char* str, int x, int y, const uint8_t* font, uint16_t color565, HAlign h = HAlign::Left,
          VAlign v = VAlign::Baseline, int direction = 0);

int width(const char* str, const uint8_t* font);

}  // namespace text

#pragma once

#include "generated/icons_data.h"
#include "graphics/rgb_canvas.h"

// Ports InkyPiZero's widgets/icons.py AssetStore - fonts and icon blitting.
//
// Fonts: uses U8g2_for_Adafruit_GFX's bundled bitmap fonts (Helvetica-ish,
// fixed sizes) instead of baking the original Jost typeface. This is a
// deliberate v1 fidelity simplification (README decision #3: simplified
// v1 first, iterate toward the original design in Phase 5) - baking a
// custom TrueType font into an Adafruit_GFX GFXfont means hand-rolling
// bitmap-font generation (normally Adafruit's `fontconvert`, a C tool
// requiring freetype, not available in this environment) with no way to
// visually verify the result without physical hardware. U8g2 fonts are
// pre-tested, and choosing them is also literally the architecture doc's
// own font-fallback library pick (see README "Font fallback"), so this
// isn't a new dependency - just using it for all text instead of only
// non-Latin fallback. Exact Jost pixel-matching is Phase 5 scope (README
// risk #4).
namespace assets {

enum class FontWeight { Regular, Bold };

// Nearest-fit lookup against U8g2's fixed bitmap sizes {8,10,12,14,18,24}.
const uint8_t* font(FontWeight weight, int pxSize);

// Numeric-only display font for the current-conditions temperature reading
// (digits + minus sign only) - close to InkyPiZero's bold 64px temp text
// without needing an 80-glyph 64px bitmap font baked for one use site.
const uint8_t* bigDigitsFont();

// Blits a baked icon (see tools/convert_assets.py) onto `canvas`, top-left
// origin at (x, y), compositing via its mask over whatever's already drawn
// there.
void drawIcon(RgbCanvas& canvas, int x, int y, const BakedIcon& icon);

}  // namespace assets

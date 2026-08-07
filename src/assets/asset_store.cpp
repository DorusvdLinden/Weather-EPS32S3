#include "asset_store.h"

#include <cstdlib>

#include "palette.h"

extern "C" {
extern const uint8_t u8g2_font_helvR08_tf[];
extern const uint8_t u8g2_font_helvB08_tf[];
extern const uint8_t u8g2_font_helvR10_tf[];
extern const uint8_t u8g2_font_helvB10_tf[];
extern const uint8_t u8g2_font_helvR12_tf[];
extern const uint8_t u8g2_font_helvB12_tf[];
extern const uint8_t u8g2_font_helvR14_tf[];
extern const uint8_t u8g2_font_helvB14_tf[];
extern const uint8_t u8g2_font_helvR18_tf[];
extern const uint8_t u8g2_font_helvB18_tf[];
extern const uint8_t u8g2_font_helvR24_tf[];
extern const uint8_t u8g2_font_helvB24_tf[];
extern const uint8_t u8g2_font_logisoso62_tn[];
}

namespace assets {

namespace {

struct FontEntry {
    int px;
    const uint8_t* regular;
    const uint8_t* bold;
};

const FontEntry kFonts[] = {
    {8, u8g2_font_helvR08_tf, u8g2_font_helvB08_tf},   {10, u8g2_font_helvR10_tf, u8g2_font_helvB10_tf},
    {12, u8g2_font_helvR12_tf, u8g2_font_helvB12_tf},  {14, u8g2_font_helvR14_tf, u8g2_font_helvB14_tf},
    {18, u8g2_font_helvR18_tf, u8g2_font_helvB18_tf},  {24, u8g2_font_helvR24_tf, u8g2_font_helvB24_tf},
};
constexpr int kFontCount = sizeof(kFonts) / sizeof(kFonts[0]);

}  // namespace

const uint8_t* font(FontWeight weight, int pxSize) {
    const FontEntry* best = &kFonts[0];
    int bestDiff = std::abs(kFonts[0].px - pxSize);
    for (int i = 1; i < kFontCount; ++i) {
        int diff = std::abs(kFonts[i].px - pxSize);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = &kFonts[i];
        }
    }
    return weight == FontWeight::Bold ? best->bold : best->regular;
}

const uint8_t* bigDigitsFont() { return u8g2_font_logisoso62_tn; }

void drawIcon(RgbCanvas& canvas, int x, int y, const BakedIcon& icon) {
    int n = int(icon.w) * int(icon.h);
    for (int i = 0; i < n; ++i) {
        if (!iconMaskBit(icon, i)) continue;
        int dx = i % icon.w;
        int dy = i / icon.w;
        uint8_t idx = iconPixelIndex(icon, i);
        const palette::Rgb888& c = palette::kRgb[idx];
        canvas.setPixelRgb(x + dx, y + dy, c.r, c.g, c.b);
    }
}

}  // namespace assets

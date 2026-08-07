#include "text.h"

#include <U8g2_for_Adafruit_GFX.h>

namespace text {

namespace {
U8G2_FOR_ADAFRUIT_GFX g_u8g2;
}

void begin(RgbCanvas& canvas) {
    g_u8g2.begin(canvas);
    g_u8g2.setFontMode(1);  // transparent background, like Pillow's text() over an existing image
    g_u8g2.setFontDirection(0);
}

int width(const char* str, const uint8_t* font) {
    g_u8g2.setFont(font);
    return g_u8g2.getUTF8Width(str);
}

void draw(const char* str, int x, int y, const uint8_t* font, uint16_t color565, HAlign h, VAlign v, int direction) {
    g_u8g2.setFont(font);
    // Confirmed on real hardware: U8g2_for_Adafruit_GFX's u8g2_SetFont()
    // silently resets transparency to opaque whenever the font pointer
    // changes ("u8g2->font_decode.is_transparent = 0" unconditionally on a
    // font change, in U8g2_for_Adafruit_GFX.cpp). Calling setFontMode(1)
    // once in begin() therefore only survives until the *first* setFont()
    // call anywhere - after that every glyph draws as a solid opaque
    // foreground-colored box instead of transparent glyph strokes. Must be
    // re-asserted after every setFont(), not just once at startup.
    g_u8g2.setFontMode(1);
    g_u8g2.setForegroundColor(color565);
    g_u8g2.setFontDirection(direction);

    if (direction != 0) {
        // Rotated text (chart's vertical axis labels): callers pre-compute
        // the anchor point directly rather than going through h/v alignment,
        // which is only meaningful for upright (direction 0) text.
        g_u8g2.drawUTF8(x, y, str);
        g_u8g2.setFontDirection(0);
        return;
    }

    int w = g_u8g2.getUTF8Width(str);
    int ascent = g_u8g2.getFontAscent();    // positive: baseline to top
    int descent = g_u8g2.getFontDescent();  // negative: baseline to bottom

    int drawX = x;
    switch (h) {
        case HAlign::Left: drawX = x; break;
        case HAlign::Center: drawX = x - w / 2; break;
        case HAlign::Right: drawX = x - w; break;
    }

    int baselineY = y;
    switch (v) {
        case VAlign::Baseline: baselineY = y; break;
        case VAlign::Top: baselineY = y + ascent; break;
        case VAlign::Middle: baselineY = y + (ascent + descent) / 2; break;
        case VAlign::Bottom: baselineY = y + descent; break;
    }

    g_u8g2.drawUTF8(drawX, baselineY, str);
}

}  // namespace text

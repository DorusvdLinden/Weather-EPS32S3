#include "forecast.h"

#include "assets/asset_store.h"
#include "generated/icons_data.h"
#include "graphics/text.h"
#include "palette.h"

namespace widgets {

namespace {
constexpr uint16_t kCardBorder565 = palette::packRgb565(90, 156, 90);
constexpr uint16_t kCardFill565 = palette::packRgb565(227, 241, 227);
}  // namespace

void drawForecastCard(RgbCanvas& canvas, const layout::Region& region, const weather::DayForecast& day,
                       uint16_t textColor565, bool showMoon) {
    canvas.fillRoundRect(region.x, region.y, region.w, region.h, 8, kCardFill565);
    canvas.drawRoundRect(region.x, region.y, region.w, region.h, 8, kCardBorder565);

    // Icons are baked at a fixed size (no runtime resize, see
    // tools/convert_assets.py) rather than scaled to region.w/h like the
    // Python original - a v1 simplification, see README decision #3.
    const BakedIcon& icon = weatherIconForecast(day.icon);
    int cx = region.centerX();
    int topY = region.y + 6;
    assets::drawIcon(canvas, cx - icon.w / 2, topY, icon);

    const uint8_t* boldFont = assets::font(assets::FontWeight::Bold, 15);
    int textY = topY + icon.h + 4;
    text::draw(day.dayLabel.c_str(), cx, textY, boldFont, textColor565, text::HAlign::Center, text::VAlign::Top);
    String highLow = String(day.high) + "\xC2\xB0 / " + String(day.low) + "\xC2\xB0";  // UTF-8 degree sign
    text::draw(highLow.c_str(), cx, textY + 17, boldFont, textColor565, text::HAlign::Center, text::VAlign::Top);

    if (showMoon) {
        const BakedIcon& moon = moonIcon(day.moonIcon);
        int moonY = region.bottom() - moon.h - 4;
        assets::drawIcon(canvas, region.x + 6, moonY, moon);
        String pct = day.moonPhasePct + "%";
        text::draw(pct.c_str(), region.x + 6 + moon.w + 4, moonY + moon.h / 2, assets::font(assets::FontWeight::Regular, 11),
                   textColor565, text::HAlign::Left, text::VAlign::Middle);
    }
}

}  // namespace widgets

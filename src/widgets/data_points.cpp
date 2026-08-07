#include "data_points.h"

#include "assets/asset_store.h"
#include "graphics/text.h"
#include "layout.h"

namespace widgets {

namespace {

void drawCenteredIcon(RgbCanvas& canvas, const layout::Region& box, const BakedIcon& icon) {
    int x = box.x + (box.w - icon.w) / 2;
    int y = box.y + (box.h - icon.h) / 2;
    assets::drawIcon(canvas, x, y, icon);
}

// Ports InkyPiZero's widgets/icons.py draw_humidity_drops(): 5 drops in a
// 3-over-2 layout, filled up to `filledCount`. Drops are baked at a fixed
// size (see tools/convert_assets.py) rather than resized per-box, matching
// the "no runtime resize" module-mapping decision.
void drawHumidityDrops(RgbCanvas& canvas, const layout::Region& box, int filledCount) {
    const BakedIcon& filled = humidityDropFilled();
    const BakedIcon& empty = humidityDropEmpty();
    int cx = box.centerX();
    int cy = box.centerY();
    int spacing = filled.w;
    int row1Y = cy - static_cast<int>(box.h * 0.16);
    int row2Y = cy + static_cast<int>(box.h * 0.22);

    struct Pos {
        int x, y;
    };
    Pos positions[5];
    auto rowPositions = [&](int count, int y, Pos* out) {
        int startX = cx - spacing * (count - 1) / 2;
        for (int i = 0; i < count; ++i) out[i] = {startX + i * spacing, y};
    };
    rowPositions(3, row1Y, positions);
    rowPositions(2, row2Y, positions + 3);

    for (int i = 0; i < 5; ++i) {
        const BakedIcon& icon = (i < filledCount) ? filled : empty;
        assets::drawIcon(canvas, positions[i].x - icon.w / 2, positions[i].y - icon.h / 2, icon);
    }
}

}  // namespace

void drawDataPoints(RgbCanvas& canvas, const weather::WeatherSnapshot& data, uint16_t textColor565) {
    const uint8_t* labelFont = assets::font(assets::FontWeight::Regular, 13);
    const uint8_t* valueFont = assets::font(assets::FontWeight::Bold, 18);

    for (int i = 0; i < weather::WeatherSnapshot::kDataPointCount; ++i) {
        layout::Region cell = layout::dataPointCell(i);
        int iconW = static_cast<int>(cell.w * layout::kDataPointIconFraction);
        layout::Region iconBox{cell.x, cell.y, iconW, cell.h};
        const weather::DataPoint& dp = data.dataPoints[i];

        if (dp.kind == weather::DataPointKind::Humidity) {
            drawHumidityDrops(canvas, iconBox, dp.dropCount);
        } else if (dp.kind == weather::DataPointKind::Visibility) {
            drawCenteredIcon(canvas, iconBox, visibilityIcon());
        }
        // wind/pressure/uv/aqi: no icon in v1, see file header.

        int textX = cell.x + iconW + 8;
        int labelY = cell.y + static_cast<int>(cell.h * 0.28);
        int valueY = cell.y + static_cast<int>(cell.h * 0.68);
        text::draw(dp.label.c_str(), textX, labelY, labelFont, textColor565, text::HAlign::Left,
                   text::VAlign::Middle);

        String valueLine = dp.direction.length() ? dp.direction + " " + dp.valueText : dp.valueText;
        text::draw(valueLine.c_str(), textX, valueY, valueFont, textColor565, text::HAlign::Left,
                   text::VAlign::Middle);
    }
}

}  // namespace widgets

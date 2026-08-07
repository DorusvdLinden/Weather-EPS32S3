#include "data_points.h"

#include <algorithm>
#include <cmath>

#include "assets/asset_store.h"
#include "graphics/text.h"
#include "layout.h"
#include "palette.h"

namespace widgets {

namespace {

int I(float v) { return static_cast<int>(std::lround(v)); }

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

// ---- Small 2D geometry helpers for the gauge icons below ----

// Rotates (x,y) around (cx,cy) by angleDeg - matches InkyPiZero's
// widgets/gauge.py rotate_points() (CSS `transform: rotate()` convention,
// screen y-down: positive angle rotates clockwise).
void rotateAroundPoint(float x, float y, float cx, float cy, float angleDeg, float* outX, float* outY) {
    float rad = angleDeg * static_cast<float>(M_PI) / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    float dx = x - cx, dy = y - cy;
    *outX = cx + dx * c - dy * s;
    *outY = cy + dx * s + dy * c;
}

// Point at `angleDeg` around (cx,cy) using Pillow's ImageDraw.arc() angle
// convention (0 deg = +x axis/3 o'clock, increasing clockwise) - identical
// cos/sin formula to a y-down screen, so the numeric angle constants copied
// from gauge.py's draw.arc(...) calls below need no conversion.
void polar(float cx, float cy, float r, float angleDeg, float* outX, float* outY) {
    float rad = angleDeg * static_cast<float>(M_PI) / 180.0f;
    *outX = cx + r * cosf(rad);
    *outY = cy + r * sinf(rad);
}

// Fills a "star-shaped-from-centroid" polygon (true of every needle/arrow
// shape below) by triangle-fanning from its own centroid - Adafruit_GFX has
// no general polygon fill.
void fillPolygonFan(RgbCanvas& canvas, const float* xs, const float* ys, int n, uint16_t color) {
    float cx = 0, cy = 0;
    for (int i = 0; i < n; ++i) {
        cx += xs[i];
        cy += ys[i];
    }
    cx /= n;
    cy /= n;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        canvas.fillTriangle(I(cx), I(cy), I(xs[i]), I(ys[i]), I(xs[j]), I(ys[j]), color);
    }
}

// A radius-thick arc segment - Adafruit_GFX has no arc-with-width primitive,
// so this sweeps the angle range in small steps and draws a short radial
// stroke [rInner,rOuter] at each step (matches Pillow's draw.arc(width=...)
// closely enough at this icon's render size).
void thickArc(RgbCanvas& canvas, float cx, float cy, float rInner, float rOuter, float startDeg, float endDeg,
              uint16_t color) {
    for (float a = startDeg; a <= endDeg; a += 1.5f) {
        float x0, y0, x1, y1;
        polar(cx, cy, rInner, a, &x0, &y0);
        polar(cx, cy, rOuter, a, &x1, &y1);
        canvas.drawLine(I(x0), I(y0), I(x1), I(y1), color);
    }
}

// A dashed line, matching gauge.py's _dashed_line: steps along the path in
// dash+gap increments, only stroking the "dash" portion.
void dashedLine(RgbCanvas& canvas, float x1, float y1, float x2, float y2, uint16_t color, float dash = 3.0f,
                 float gap = 4.5f) {
    float length = std::hypot(x2 - x1, y2 - y1);
    if (length < 0.5f) return;
    float dx = (x2 - x1) / length, dy = (y2 - y1) / length;
    for (float pos = 0; pos < length; pos += dash + gap) {
        float segEnd = std::min(pos + dash, length);
        canvas.drawLine(I(x1 + dx * pos), I(y1 + dy * pos), I(x1 + dx * segEnd), I(y1 + dy * segEnd), color);
    }
}

// A dashed circle, matching gauge.py's _dashed_circle.
void dashedCircle(RgbCanvas& canvas, float cx, float cy, float r, uint16_t color, float dashDeg = 3.5f,
                   float gapDeg = 5.5f) {
    for (float a = 0; a < 360; a += dashDeg + gapDeg) {
        float x0, y0, x1, y1;
        polar(cx, cy, r, a, &x0, &y0);
        polar(cx, cy, r, a + dashDeg, &x1, &y1);
        canvas.drawLine(I(x0), I(y0), I(x1), I(y1), color);
    }
}

// Maps a fixed "design canvas" (matching gauge.py's render_*() image size,
// itself a near-direct port of the original weather.html SVG viewBox) onto
// the data-point icon box, scaled to fit and centered - so every coordinate
// below can be copied straight out of gauge.py without hand-converting it.
struct GaugeMap {
    layout::Region box;
    float designW, designH, scale;

    void operator()(float x, float y, float* outX, float* outY) const {
        *outX = box.centerX() + (x - designW / 2.0f) * scale;
        *outY = box.centerY() + (y - designH / 2.0f) * scale;
    }
    float len(float v) const { return v * scale; }
};

GaugeMap makeMap(const layout::Region& box, float designW, float designH) {
    return GaugeMap{box, designW, designH, (box.h * 0.95f) / designH};
}

// ---- Gauge icons - ports of InkyPiZero's widgets/gauge.py render_*()
// functions and their weather_data.py rotation/color formulas, adapted from
// Pillow's arc/polygon drawing to Adafruit_GFX's line/triangle/circle
// primitives via the helpers above. Dropped: gauge.py's tiny decorative
// rain-cloud/cloud/sun glyph clusters inside the pressure dial - at this
// icon's render size (a ~90x55px box) they dither down to illegible noise;
// everything that actually carries the value (arc, ticks, needle, band
// colors, rotation formulas) is kept, using the same numbers as the Python
// source.

void drawWindIcon(RgbCanvas& canvas, const layout::Region& box, double windDeg) {
    GaugeMap m = makeMap(box, 200, 180);
    float cx, cy;
    m(100, 90, &cx, &cy);
    float r = m.len(75);
    uint16_t ringColor = palette::packRgb565(26, 26, 26);

    dashedCircle(canvas, cx, cy, r, ringColor);
    static const float kSpokes[4][4] = {
        {100, 5, 100, 175}, {15, 90, 185, 90}, {160.1f, 29.9f, 39.9f, 150.1f}, {39.9f, 29.9f, 160.1f, 150.1f},
    };
    for (auto& s : kSpokes) {
        float x0, y0, x1, y1;
        m(s[0], s[1], &x0, &y0);
        m(s[2], s[3], &x1, &y1);
        dashedLine(canvas, x0, y0, x1, y1, ringColor);
    }

    float rotation = static_cast<float>(std::fmod(windDeg + 180.0, 360.0));
    static const float kNeedle[4][2] = {{100, 20}, {116, 125}, {100, 119}, {84, 125}};
    float xs[4], ys[4];
    for (int i = 0; i < 4; ++i) {
        float rx, ry;
        rotateAroundPoint(kNeedle[i][0], kNeedle[i][1], 100, 90, rotation, &rx, &ry);
        m(rx, ry, &xs[i], &ys[i]);
    }
    fillPolygonFan(canvas, xs, ys, 4, palette::packRgb565(183, 28, 28));
    canvas.fillCircle(I(cx), I(cy), std::max(1, I(m.len(8))), palette::packRgb565(255, 255, 255));
}

// Matches weather_data.py's get_pressure_gauge_rotation().
float pressureGaugeRotation(double pressureHpa) {
    double clamped = std::min(1050.0, std::max(970.0, pressureHpa));
    double fraction = (clamped - 970.0) / (1050.0 - 970.0);
    return static_cast<float>(-90.0 + fraction * 180.0);
}

void drawPressureIcon(RgbCanvas& canvas, const layout::Region& box, double pressureHpa) {
    if (std::isnan(pressureHpa)) return;
    GaugeMap m = makeMap(box, 200, 150);
    uint16_t domeColor = palette::packRgb565(13, 71, 161);
    float cx, cy;
    m(100, 110, &cx, &cy);
    float r = m.len(85);

    thickArc(canvas, cx, cy, r - m.len(5.5f), r + m.len(5.5f), 160, 380, domeColor);
    float ax, ay, bx, by;
    m(20.1f, 139.07f, &ax, &ay);
    m(179.9f, 139.07f, &bx, &by);
    canvas.drawLine(I(ax), I(ay), I(bx), I(by), domeColor);
    canvas.fillCircle(I(ax), I(ay), std::max(1, I(m.len(5.5f))), domeColor);
    canvas.fillCircle(I(bx), I(by), std::max(1, I(m.len(5.5f))), domeColor);

    static const float kTicks[7][4] = {
        {15, 110, 28, 110},       {26.4f, 67.5f, 37.6f, 74}, {57.5f, 36.4f, 64, 47.6f}, {100, 25, 100, 38},
        {142.5f, 36.4f, 136, 47.6f}, {173.6f, 67.5f, 162.4f, 74}, {185, 110, 172, 110},
    };
    for (auto& t : kTicks) {
        float x0, y0, x1, y1;
        m(t[0], t[1], &x0, &y0);
        m(t[2], t[3], &x1, &y1);
        canvas.drawLine(I(x0), I(y0), I(x1), I(y1), domeColor);
        canvas.fillCircle(I(x0), I(y0), std::max(1, I(m.len(3.5f))), domeColor);
        canvas.fillCircle(I(x1), I(y1), std::max(1, I(m.len(3.5f))), domeColor);
    }

    float rotation = pressureGaugeRotation(pressureHpa);
    static const float kNeedle[4][2] = {{100, 40}, {117, 98}, {100, 89}, {83, 98}};
    float xs[4], ys[4];
    for (int i = 0; i < 4; ++i) {
        float rx, ry;
        rotateAroundPoint(kNeedle[i][0], kNeedle[i][1], 100, 110, rotation, &rx, &ry);
        m(rx, ry, &xs[i], &ys[i]);
    }
    uint16_t needleColor = palette::packRgb565(17, 17, 17);
    fillPolygonFan(canvas, xs, ys, 4, needleColor);
    canvas.fillCircle(I(cx), I(cy), std::max(1, I(m.len(11))), needleColor);
}

// Matches weather_data.py's get_uv_fraction()/get_uv_color()/get_uv_beam_points().
void drawUvIcon(RgbCanvas& canvas, const layout::Region& box, double uvIndex) {
    if (std::isnan(uvIndex)) return;
    GaugeMap m = makeMap(box, 120, 120);
    float cx, cy;
    m(60, 60, &cx, &cy);

    double fraction = std::min(1.0, std::max(0.0, uvIndex / 11.0));
    const palette::Rgb888 low{255, 179, 0}, high{216, 67, 21};
    uint8_t cr = static_cast<uint8_t>(std::lround(low.r + (high.r - low.r) * fraction));
    uint8_t cg = static_cast<uint8_t>(std::lround(low.g + (high.g - low.g) * fraction));
    uint8_t cb = static_cast<uint8_t>(std::lround(low.b + (high.b - low.b) * fraction));
    uint16_t color = palette::packRgb565(cr, cg, cb);

    constexpr float kCoreR = 24.0f, kMinLen = 10.0f, kMaxLen = 32.0f, kHalfWidth = 5.0f;
    constexpr int kBeamCount = 10;
    float beamLen = kMinLen + (kMaxLen - kMinLen) * static_cast<float>(fraction);
    float outerR = kCoreR + beamLen;
    for (int i = 0; i < kBeamCount; ++i) {
        float angle = (2.0f * static_cast<float>(M_PI) * i / kBeamCount) - static_cast<float>(M_PI) / 2.0f;
        float perp = angle + static_cast<float>(M_PI) / 2.0f;
        float baseX = 60 + kCoreR * cosf(angle), baseY = 60 + kCoreR * sinf(angle);
        float leftX = baseX + kHalfWidth * cosf(perp), leftY = baseY + kHalfWidth * sinf(perp);
        float rightX = baseX - kHalfWidth * cosf(perp), rightY = baseY - kHalfWidth * sinf(perp);
        float tipX = 60 + outerR * cosf(angle), tipY = 60 + outerR * sinf(angle);

        float lx, ly, rx, ry, tx, ty;
        m(leftX, leftY, &lx, &ly);
        m(rightX, rightY, &rx, &ry);
        m(tipX, tipY, &tx, &ty);
        canvas.fillTriangle(I(lx), I(ly), I(tx), I(ty), I(rx), I(ry), color);
    }
    canvas.fillCircle(I(cx), I(cy), std::max(1, I(m.len(kCoreR))), color);
}

void drawAqiIcon(RgbCanvas& canvas, const layout::Region& box, double aqi) {
    if (std::isnan(aqi)) return;
    GaugeMap m = makeMap(box, 200, 150);
    float cx, cy;
    m(100, 120, &cx, &cy);
    float r = m.len(75);
    float half = m.len(14);

    struct Band {
        float start, end;
        palette::Rgb888 color;
    };
    static const Band kBands[] = {
        {180, 225, {242, 80, 74}},
        {225, 270, {245, 163, 0}},
        {270, 315, {247, 198, 0}},
        {315, 360, {51, 168, 82}},
    };
    for (auto& band : kBands) {
        thickArc(canvas, cx, cy, r - half, r + half, band.start, band.end,
                 palette::packRgb565(band.color.r, band.color.g, band.color.b));
    }

    // Matches weather_data.py's get_aqi_rotation_from_fraction()/get_european_aqi_rotation().
    double fractionGood = std::min(1.0, std::max(0.0, 1.0 - std::min(aqi, 100.0) / 100.0));
    float rotation = static_cast<float>(-180.0 + 180.0 * fractionGood);
    static const float kNeedle[7][2] = {
        {100, 114}, {140, 114}, {140, 102}, {168, 120}, {140, 138}, {140, 126}, {100, 126},
    };
    float xs[7], ys[7];
    for (int i = 0; i < 7; ++i) {
        float rx, ry;
        rotateAroundPoint(kNeedle[i][0], kNeedle[i][1], 100, 120, rotation, &rx, &ry);
        m(rx, ry, &xs[i], &ys[i]);
    }
    uint16_t needleColor = palette::packRgb565(61, 61, 82);
    fillPolygonFan(canvas, xs, ys, 7, needleColor);
    canvas.fillCircle(I(cx), I(cy), std::max(1, I(m.len(12))), needleColor);
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

        switch (dp.kind) {
            case weather::DataPointKind::Humidity:
                drawHumidityDrops(canvas, iconBox, dp.dropCount);
                break;
            case weather::DataPointKind::Visibility:
                drawCenteredIcon(canvas, iconBox, visibilityIcon());
                break;
            case weather::DataPointKind::Wind:
                drawWindIcon(canvas, iconBox, dp.rawValue);
                break;
            case weather::DataPointKind::Pressure:
                drawPressureIcon(canvas, iconBox, dp.rawValue);
                break;
            case weather::DataPointKind::Uv:
                drawUvIcon(canvas, iconBox, dp.rawValue);
                break;
            case weather::DataPointKind::Aqi:
                drawAqiIcon(canvas, iconBox, dp.rawValue);
                break;
        }

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

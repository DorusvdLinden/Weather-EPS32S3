#include "chart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "assets/asset_store.h"
#include "generated/icons_data.h"
#include "graphics/text.h"
#include "palette.h"

namespace widgets {

namespace {

constexpr palette::Rgb888 kOrange{230, 81, 0};
constexpr palette::Rgb888 kBlue{13, 71, 161};
constexpr palette::Rgb888 kFillYellow{252, 204, 5};

constexpr int kLeftMargin = 34, kRightMargin = 42, kTopMargin = 12, kBottomMargin = 34, kIconSize = 20;

int I(float v) { return static_cast<int>(std::lround(v)); }

uint16_t pack(const palette::Rgb888& c) { return palette::packRgb565(c.r, c.g, c.b); }

// Adafruit_GFX's drawLine is 1px wide - approximated here by stacking
// `thickness` copies offset in y, which is a fine approximation for the
// gently-sloped temperature line this is used for (not a general solution).
void thickLine(RgbCanvas& canvas, float x1, float y1, float x2, float y2, const palette::Rgb888& c, int thickness) {
    uint16_t color = pack(c);
    int half = thickness / 2;
    for (int o = -half; o <= half; ++o) {
        canvas.drawLine(I(x1), I(y1) + o, I(x2), I(y2) + o, color);
    }
}

void dashedHLine(RgbCanvas& canvas, float x0, float x1, float y, uint16_t color565, int width) {
    float x = x0;
    while (x < x1) {
        float segEnd = std::min(x + 5, x1);
        for (int w = 0; w < width; ++w) {
            canvas.drawLine(I(x), I(y) + w, I(segEnd), I(y) + w, color565);
        }
        x += 9;
    }
}

void fillRectAlpha(RgbCanvas& canvas, int x0, int y0, int x1, int y1, const palette::Rgb888& c, uint8_t alpha) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) canvas.blendPixelRgb(x, y, c.r, c.g, c.b, alpha);
    }
}

}  // namespace

void drawChart(RgbCanvas& canvas, const layout::Region& region, const weather::WeatherSnapshot& data,
                uint16_t textColor565, int graphIconStep, const char* tempUnitLabel, const char* rainUnitLabel) {
    int n = data.hourlyCount;
    if (n == 0) return;

    float plotX0 = region.x + kLeftMargin;
    float plotX1 = region.right() - kRightMargin;
    float plotY0 = region.y + kTopMargin;
    float plotY1 = region.bottom() - kBottomMargin;
    float plotW = plotX1 - plotX0;

    float temps[weather::WeatherSnapshot::kMaxHourly];
    float rains[weather::WeatherSnapshot::kMaxHourly];
    float actualMin = 1e9f, actualMax = -1e9f, maxRain = 0;
    for (int i = 0; i < n; ++i) {
        temps[i] = static_cast<float>(data.hourly[i].temperature);
        rains[i] = std::max(0.0f, data.hourly[i].rain);
        actualMin = std::min(actualMin, temps[i]);
        actualMax = std::max(actualMax, temps[i]);
        maxRain = std::max(maxRain, rains[i]);
    }
    float minTemp = std::min(actualMin, 0.0f);
    float maxTemp = std::max(actualMax, 0.0f);
    float tempSpan = (maxTemp - minTemp) != 0 ? (maxTemp - minTemp) : 1.0f;
    float rainAxisMax = std::max(1.0f, std::ceil(maxRain * 10.0f) / 10.0f);

    float band = plotW / n;
    float xs[weather::WeatherSnapshot::kMaxHourly];
    for (int i = 0; i < n; ++i) xs[i] = plotX0 + band * (i + 0.5f);

    auto yTemp = [&](float v) { return plotY0 + (maxTemp - v) / tempSpan * (plotY1 - plotY0); };
    auto yRain = [&](float v) { return plotY0 + (rainAxisMax - v) / rainAxisMax * (plotY1 - plotY0); };
    float yZero = yTemp(0);

    // Rain bars - skip anything too small to be a meaningful bar (see
    // chart.py's comment: a fixed-height highlight strip alone would read as
    // a false floor across near-zero hours).
    for (int i = 0; i < n; ++i) {
        if (rains[i] < rainAxisMax * 0.03f) continue;
        float top = yRain(rains[i]);
        float w = band * 0.85f;
        fillRectAlpha(canvas, I(xs[i] - w / 2), I(top), I(xs[i] + w / 2), I(plotY1), kBlue, 130);
        fillRectAlpha(canvas, I(xs[i] - w / 2), I(top), I(xs[i] + w / 2), I(std::min(top + 3, plotY1)), kBlue, 230);
    }

    // Temperature fill, between the curve and the 0-degree line - rasterized
    // per pixel column (piecewise-linear interpolation between the two
    // bracketing hourly points), since Adafruit_GFX has no alpha-blended
    // polygon fill.
    int xStart = I(xs[0]), xEnd = I(xs[n - 1]);
    for (int x = xStart; x <= xEnd; ++x) {
        float fx = (x - plotX0) / band - 0.5f;
        int i0 = std::max(0, std::min(static_cast<int>(std::floor(fx)), n - 2));
        float t = n > 1 ? std::max(0.0f, std::min(1.0f, fx - i0)) : 0.0f;
        float temp = temps[i0] + (temps[std::min(i0 + 1, n - 1)] - temps[i0]) * t;
        float curveY = yTemp(temp);
        int y0 = I(std::min(curveY, yZero)), y1 = I(std::max(curveY, yZero));
        for (int y = y0; y <= y1; ++y) canvas.blendPixelRgb(x, y, kFillYellow.r, kFillYellow.g, kFillYellow.b, 90);
    }

    // Temperature line, colored per segment by sign.
    for (int i = 0; i < n - 1; ++i) {
        const palette::Rgb888& c = (temps[i] + temps[i + 1]) >= 0 ? kOrange : kBlue;
        thickLine(canvas, xs[i], yTemp(temps[i]), xs[i + 1], yTemp(temps[i + 1]), c, 4);
    }

    bool kelvin = strcmp(tempUnitLabel, "K") == 0;
    const uint8_t* boldFont12 = assets::font(assets::FontWeight::Bold, 12);

    // Dashed actual min/max lines.
    {
        char buf[8];
        const palette::Rgb888& cMax = actualMax >= 0 ? kOrange : kBlue;
        float yMax = yTemp(actualMax);
        dashedHLine(canvas, plotX0, plotX1, yMax, pack(cMax), 2);
        snprintf(buf, sizeof(buf), kelvin ? "%d" : "%d\xC2\xB0", static_cast<int>(actualMax));
        text::draw(buf, I(plotX0 + plotW / 2), I(yMax) - 14, boldFont12, pack(cMax), text::HAlign::Center,
                   text::VAlign::Middle);

        const palette::Rgb888& cMin = actualMin >= 0 ? kOrange : kBlue;
        float yMin = yTemp(actualMin);
        dashedHLine(canvas, plotX0, plotX1, yMin, pack(cMin), 2);
        snprintf(buf, sizeof(buf), kelvin ? "%d" : "%d\xC2\xB0", static_cast<int>(actualMin));
        text::draw(buf, I(plotX0 + plotW / 2), I(yMin) + 4, boldFont12, pack(cMin), text::HAlign::Center,
                   text::VAlign::Middle);
    }

    // Axes.
    canvas.drawLine(I(plotX0), I(plotY0), I(plotX0), I(plotY1), textColor565);
    canvas.drawLine(I(plotX1), I(plotY0), I(plotX1), I(plotY1), textColor565);
    canvas.drawLine(I(plotX0), I(plotY1), I(plotX1), I(plotY1), textColor565);

    char buf[24];
    snprintf(buf, sizeof(buf), kelvin ? "%d" : "%d\xC2\xB0", static_cast<int>(maxTemp));
    text::draw(buf, I(plotX0) - 6, I(yTemp(maxTemp)), boldFont12, textColor565, text::HAlign::Right,
               text::VAlign::Middle);
    snprintf(buf, sizeof(buf), kelvin ? "%d" : "%d\xC2\xB0", static_cast<int>(minTemp));
    text::draw(buf, I(plotX0) - 6, I(yTemp(minTemp)), boldFont12, textColor565, text::HAlign::Right,
               text::VAlign::Middle);

    snprintf(buf, sizeof(buf), "%g %s", rainAxisMax, rainUnitLabel);
    text::draw(buf, I(plotX1) + 6, I(yRain(rainAxisMax)), boldFont12, textColor565, text::HAlign::Left,
               text::VAlign::Middle);
    snprintf(buf, sizeof(buf), "0 %s", rainUnitLabel);
    text::draw(buf, I(plotX1) + 6, I(yRain(0)), boldFont12, textColor565, text::HAlign::Left, text::VAlign::Middle);

    // Vertical axis unit labels, centered on the plot height - confirmed on
    // real hardware that these need centering explicitly: direction=3 text
    // grows *upward* from its anchor point (the anchor is the bottom end of
    // the rotated label), so anchoring at plotY1 (as an earlier version did)
    // left both labels sitting low near the bottom of the chart instead of
    // centered alongside it. Offsetting the anchor down by half the label's
    // (pre-rotation) width puts its vertical center on plotCenterY instead.
    float plotCenterY = (plotY0 + plotY1) / 2.0f;
    int tempLabelW = text::width(tempUnitLabel, boldFont12);
    text::draw(tempUnitLabel, I(region.x) + 12, I(plotCenterY) + tempLabelW / 2, boldFont12, textColor565,
               text::HAlign::Left, text::VAlign::Baseline, 3);
    int rainLabelW = text::width("Regen", boldFont12);
    text::draw("Regen", I(region.right()) - 14, I(plotCenterY) + rainLabelW / 2, boldFont12, textColor565,
               text::HAlign::Left, text::VAlign::Baseline, 3);

    // X-axis hour labels - same cadence as the icon strip below.
    const uint8_t* normalFont12 = assets::font(assets::FontWeight::Regular, 12);
    for (int i = 0; i < n; i += graphIconStep) {
        text::draw(data.hourly[i].timeLabel.c_str(), I(xs[i]), I(plotY1) + 6, normalFont12, textColor565,
                   text::HAlign::Center, text::VAlign::Top);
    }

    // Hourly/sun-event icon strip - fixed row below the plot, not following
    // the curve (matches chart.py).
    SunIcon sunIconByIndex[weather::WeatherSnapshot::kMaxHourly];
    bool hasSunIcon[weather::WeatherSnapshot::kMaxHourly] = {};
    for (int i = 0; i < data.sunEventCount; ++i) {
        int idx = std::max(0, std::min(static_cast<int>(std::lround(data.sunEvents[i].position)), n - 1));
        sunIconByIndex[idx] = data.sunEvents[i].icon;
        hasSunIcon[idx] = true;
    }
    int iconY = I(plotY1) + 18;
    for (int i = 0; i < n; ++i) {
        const BakedIcon* icon = nullptr;
        if (hasSunIcon[i]) {
            icon = &sunIconChart(sunIconByIndex[i]);
        } else if (i % graphIconStep == 0) {
            icon = &weatherIconChart(data.hourly[i].icon);
        }
        if (icon) assets::drawIcon(canvas, I(xs[i]) - kIconSize / 2, iconY, *icon);
    }
}

}  // namespace widgets

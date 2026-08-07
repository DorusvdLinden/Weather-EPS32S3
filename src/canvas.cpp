#include "canvas.h"

#include <cstring>

#include "assets/asset_store.h"
#include "generated/icons_data.h"
#include "graphics/text.h"
#include "layout.h"
#include "palette.h"
#include "widgets/chart.h"
#include "widgets/data_points.h"
#include "widgets/forecast.h"

namespace canvas {

namespace {

palette::Rgb888 unpackHex(uint32_t rgb) {
    return {static_cast<uint8_t>((rgb >> 16) & 0xFF), static_cast<uint8_t>((rgb >> 8) & 0xFF),
            static_cast<uint8_t>(rgb & 0xFF)};
}

const char* degreeSuffix(const String& tempUnit) { return tempUnit == "K" ? "" : "\xC2\xB0"; }

void drawHeader(RgbCanvas& c, const weather::WeatherSnapshot& data, uint16_t textColor565) {
    const uint8_t* fontDate = assets::font(assets::FontWeight::Bold, 22);
    const uint8_t* fontRefresh = assets::font(assets::FontWeight::Bold, 14);

    String dateText = data.currentDate;
    if (data.location.length()) dateText += ", " + data.location;
    text::draw(dateText.c_str(), layout::kHeader.x, layout::kHeader.bottom(), fontDate, textColor565,
               text::HAlign::Left, text::VAlign::Bottom);

    String refreshText = "Laatste update: " + data.lastRefreshTime;
    uint16_t dimGray565 = palette::packRgb565(51, 51, 51);
    text::draw(refreshText.c_str(), layout::kHeader.right() - 4, layout::kHeader.y, fontRefresh, dimGray565,
               text::HAlign::Right, text::VAlign::Top);
}

void drawCurrentConditions(RgbCanvas& c, const weather::WeatherSnapshot& data, uint16_t textColor565) {
    const layout::Region& region = layout::kCurrentTemperature;

    const BakedIcon& icon = weatherIconCurrent(data.currentIcon);
    int iconCenterX = region.x + static_cast<int>(region.w * 0.22);
    int iconTopY = region.y + static_cast<int>(region.h * 0.12);
    assets::drawIcon(c, iconCenterX - icon.w / 2, iconTopY, icon);

    int textCx = region.x + static_cast<int>(region.w * 0.68);
    const uint8_t* fontTemp = assets::bigDigitsFont();
    const uint8_t* fontUnit = assets::font(assets::FontWeight::Bold, 24);
    const uint8_t* fontSmall = assets::font(assets::FontWeight::Bold, 15);

    String tempStr = String(data.currentTemp);
    int tempY = region.y + static_cast<int>(region.h * 0.42);
    text::draw(tempStr.c_str(), textCx, tempY, fontTemp, textColor565, text::HAlign::Center, text::VAlign::Middle);
    int tempW = text::width(tempStr.c_str(), fontTemp);
    String unitStr = String(degreeSuffix(data.tempUnit)) + data.tempUnit;
    text::draw(unitStr.c_str(), textCx + tempW / 2 + 4, tempY - 22, fontUnit, textColor565, text::HAlign::Left,
               text::VAlign::Middle);

    const char* degree = degreeSuffix(data.tempUnit);
    String feelsLine = "Gevoelstemp. " + String(data.feelsLike) + degree;
    text::draw(feelsLine.c_str(), textCx, tempY + 34, fontSmall, textColor565, text::HAlign::Center,
               text::VAlign::Middle);
    String hiLoLine = String(data.forecastHigh) + degree + " / " + String(data.forecastLow) + degree;
    text::draw(hiLoLine.c_str(), textCx, tempY + 56, fontSmall, textColor565, text::HAlign::Center,
               text::VAlign::Middle);
}

}  // namespace

void render(RgbCanvas& target, const DisplayConfig& config, const weather::WeatherSnapshot& data) {
    palette::Rgb888 bg = unpackHex(config.background_color);
    palette::Rgb888 fg = unpackHex(config.text_color);
    uint16_t bg565 = palette::packRgb565(bg.r, bg.g, bg.b);
    uint16_t fg565 = palette::packRgb565(fg.r, fg.g, fg.b);

    target.fillScreen(bg565);

    drawHeader(target, data, fg565);
    drawCurrentConditions(target, data, fg565);
    widgets::drawDataPoints(target, data, fg565);

    const char* rainUnitLabel = config.units == "imperial" ? "in" : "mm";
    widgets::drawChart(target, layout::kChartArea, data, fg565, config.graph_icon_step, data.tempUnit.c_str(),
                        rainUnitLabel);

    for (int i = 0; i < data.dailyCount; ++i) {
        layout::Region region = layout::forecastCard(i, data.dailyCount);
        widgets::drawForecastCard(target, region, data.daily[i], fg565, config.show_moon_phase);
    }
}

}  // namespace canvas

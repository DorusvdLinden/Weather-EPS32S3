// GENERATED FILE - do not edit by hand. Run tools/convert_assets.py to
// regenerate from assets/icons/*.png (vendored from InkyPiZero, see
// README attribution).
#pragma once

#include <cstdint>

struct BakedIcon {
    uint8_t w, h;
    const uint8_t* pixels;  // 4-bit palette index per pixel, packed 2/byte, flat raster order
    const uint8_t* mask;    // 1 bit per pixel, packed 8/byte MSB-first, flat raster order
};

enum class WeatherIcon : uint8_t {
    W01D,
    W01N,
    W022D,
    W022N,
    W02D,
    W02N,
    W04D,
    W50D,
    W48D,
    W51D,
    W53D,
    W09D,
    W56D,
    W57D,
    W71D,
    W73D,
    W13D,
    W77D,
    W11D,
    COUNT
};

enum class MoonIcon : uint8_t {
    NewMoon,
    WaxingCrescent,
    FirstQuarter,
    WaxingGibbous,
    FullMoon,
    WaningGibbous,
    LastQuarter,
    WaningCrescent,
    COUNT
};

enum class SunIcon : uint8_t {
    Sunrise,
    Sunset,
    COUNT
};

const BakedIcon& weatherIconCurrent(WeatherIcon k);
const BakedIcon& weatherIconForecast(WeatherIcon k);
const BakedIcon& weatherIconChart(WeatherIcon k);
const BakedIcon& sunIconChart(SunIcon k);
const BakedIcon& moonIcon(MoonIcon k);
const BakedIcon& visibilityIcon();
const BakedIcon& humidityDropFilled();
const BakedIcon& humidityDropEmpty();

inline uint8_t iconPixelIndex(const BakedIcon& icon, int i) {
    uint8_t byte = icon.pixels[i / 2];
    return (i % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
}

inline bool iconMaskBit(const BakedIcon& icon, int i) {
    uint8_t byte = icon.mask[i / 8];
    return (byte >> (7 - (i % 8))) & 1;
}

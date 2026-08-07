#!/usr/bin/env python3
"""Bakes assets/icons/*.png (vendored from InkyPiZero, see README attribution)
into src/generated/icons_data.{h,cpp} - indexed-palette bitmaps pre-quantized
to the 7-color ACeP palette, at each fixed on-screen size the firmware
actually needs. This is the module-mapping table's `tools/convert_assets.py`
row: no runtime resize, no runtime alpha blending against arbitrary RGBA -
the panel can only show 7 colors, so quantizing at bake time (against a
binary opacity mask, not smooth alpha) is strictly no less correct than
doing it per-frame on-device, and it's the only sane option on a
microcontroller.

Usage:
    python tools/convert_assets.py

Requires Pillow (not a firmware dependency - dev-time only, same as
InkyPiZero's own scripts/generate_icons.py).
"""
import os
import sys

try:
    from PIL import Image
except ImportError:
    print("convert_assets.py: Pillow not installed, skipping icon bake "
          "(committed src/generated/icons_data.* will be used as-is).", file=sys.stderr)
    sys.exit(0)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
ICON_DIR = os.path.join(REPO_ROOT, "assets", "icons")
OUT_DIR = os.path.join(REPO_ROOT, "src", "generated")

# Index-aligned with palette::Index in src/palette.h - keep in sync by hand,
# there are only 7 entries and they don't change.
PALETTE = [
    (0, 0, 0),        # Black
    (255, 255, 255),  # White
    (0, 255, 0),      # Green
    (0, 0, 255),       # Blue
    (255, 0, 0),       # Red
    (255, 255, 0),     # Yellow
    (255, 128, 0),     # Orange
]
ALPHA_THRESHOLD = 128  # binary opacity - no smooth alpha at bake time, see module docstring

# icon_key -> (C++ enum value name, [(context_name, w, h), ...])
WEATHER_ICONS = {
    "01d": "W01D", "01n": "W01N", "022d": "W022D", "022n": "W022N",
    "02d": "W02D", "02n": "W02N", "04d": "W04D", "50d": "W50D", "48d": "W48D",
    "51d": "W51D", "53d": "W53D", "09d": "W09D", "56d": "W56D", "57d": "W57D",
    "71d": "W71D", "73d": "W73D", "13d": "W13D", "77d": "W77D", "11d": "W11D",
}
MOON_ICONS = {
    "newmoon": "NewMoon", "waxingcrescent": "WaxingCrescent", "firstquarter": "FirstQuarter",
    "waxinggibbous": "WaxingGibbous", "fullmoon": "FullMoon", "waninggibbous": "WaningGibbous",
    "lastquarter": "LastQuarter", "waningcrescent": "WaningCrescent",
}
SUN_ICONS = {"sunrise": "Sunrise", "sunset": "Sunset"}

# Fixed on-screen sizes, derived from InkyPiZero's canvas.py/forecast.py/chart.py
# box math at this repo's fixed 800x480 canvas (see src/layout.h).
SIZE_CURRENT = (100, 100)     # current-conditions icon (canvas.py: int(TODAY.h * 0.62))
SIZE_FORECAST = (42, 42)      # forecast card icon
SIZE_CHART = (20, 20)         # hourly/sun-event chart strip icon
SIZE_MOON = (14, 14)          # forecast card moon badge
SIZE_VISIBILITY = (40, 40)    # data-point visibility icon (simplified from the original's 50x30 non-square fit)
SIZE_HUMIDITY = (19, 27)      # aspect-locked to the source art (28/38), see widgets/data_points.cpp


def nearest_palette_index(r, g, b):
    best_i, best_d = 0, None
    for i, (pr, pg, pb) in enumerate(PALETTE):
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if best_d is None or d < best_d:
            best_d, best_i = d, i
    return best_i


def load_icon(key):
    path = os.path.join(ICON_DIR, f"{key}.png")
    raw = Image.open(path).convert("RGBA")
    bbox = raw.getbbox()
    return raw.crop(bbox) if bbox else raw


def fit_into_box(img, target_w, target_h):
    """Aspect-preserving fit, centered, transparent padding - mirrors
    InkyPiZero's widgets/icons.py AssetStore.icon() resize behavior."""
    scale = min(target_w / img.width, target_h / img.height)
    fit_w, fit_h = max(1, round(img.width * scale)), max(1, round(img.height * scale))
    fitted = img.resize((fit_w, fit_h), Image.LANCZOS)
    canvas = Image.new("RGBA", (target_w, target_h), (0, 0, 0, 0))
    canvas.paste(fitted, ((target_w - fit_w) // 2, (target_h - fit_h) // 2), fitted)
    return canvas


def bake(img, target_w, target_h):
    """Returns (pixel_nibbles, mask_bits) as flat lists, raster order."""
    fitted = fit_into_box(img, target_w, target_h)
    pixels, mask = [], []
    for y in range(target_h):
        for x in range(target_w):
            r, g, b, a = fitted.getpixel((x, y))
            opaque = a >= ALPHA_THRESHOLD
            mask.append(1 if opaque else 0)
            pixels.append(nearest_palette_index(r, g, b) if opaque else 0)
    return pixels, mask


def pack_nibbles(values):
    out = bytearray()
    for i in range(0, len(values), 2):
        hi = values[i]
        lo = values[i + 1] if i + 1 < len(values) else 0
        out.append((hi << 4) | lo)
    return bytes(out)


def pack_bits(values):
    out = bytearray()
    for i in range(0, len(values), 8):
        chunk = values[i:i + 8]
        byte = 0
        for b_i, bit in enumerate(chunk):
            byte |= (bit & 1) << (7 - b_i)
        out.append(byte)
    return bytes(out)


def c_bytes(data):
    return ", ".join(str(b) for b in data)


class Baker:
    def __init__(self):
        self.entries = []  # (c_name, w, h, pixel_bytes, mask_bytes)

    def add(self, key, c_name, w, h):
        img = load_icon(key)
        pixels, mask = bake(img, w, h)
        self.entries.append((c_name, w, h, pack_nibbles(pixels), pack_bits(mask)))
        return c_name


def generate():
    os.makedirs(OUT_DIR, exist_ok=True)
    baker = Baker()

    weather_current, weather_forecast, weather_chart = {}, {}, {}
    for key, enum_name in WEATHER_ICONS.items():
        weather_current[enum_name] = baker.add(key, f"kIcon_{enum_name}_Current", *SIZE_CURRENT)
        weather_forecast[enum_name] = baker.add(key, f"kIcon_{enum_name}_Forecast", *SIZE_FORECAST)
        weather_chart[enum_name] = baker.add(key, f"kIcon_{enum_name}_Chart", *SIZE_CHART)

    sun_chart = {}
    for key, enum_name in SUN_ICONS.items():
        sun_chart[enum_name] = baker.add(key, f"kIcon_{enum_name}_Chart", *SIZE_CHART)

    moon = {}
    for key, enum_name in MOON_ICONS.items():
        moon[enum_name] = baker.add(key, f"kIcon_{enum_name}", *SIZE_MOON)

    visibility_name = baker.add("visibility", "kIcon_Visibility", *SIZE_VISIBILITY)
    humidity_filled_name = baker.add("humidity_drop_filled", "kIcon_HumidityFilled", *SIZE_HUMIDITY)
    humidity_empty_name = baker.add("humidity_drop_empty", "kIcon_HumidityEmpty", *SIZE_HUMIDITY)

    write_header(weather_current.keys(), MOON_ICONS.values(), SUN_ICONS.values())
    write_source(baker, weather_current, weather_forecast, weather_chart, sun_chart, moon,
                 visibility_name, humidity_filled_name, humidity_empty_name)
    print(f"Baked {len(baker.entries)} icon bitmaps -> {OUT_DIR}")


def write_header(weather_names, moon_names, sun_names):
    weather_enum = ",\n    ".join(weather_names)
    moon_enum = ",\n    ".join(MOON_ICONS.values())
    sun_enum = ",\n    ".join(SUN_ICONS.values())
    header = f"""// GENERATED FILE - do not edit by hand. Run tools/convert_assets.py to
// regenerate from assets/icons/*.png (vendored from InkyPiZero, see
// README attribution).
#pragma once

#include <cstdint>

struct BakedIcon {{
    uint8_t w, h;
    const uint8_t* pixels;  // 4-bit palette index per pixel, packed 2/byte, flat raster order
    const uint8_t* mask;    // 1 bit per pixel, packed 8/byte MSB-first, flat raster order
}};

enum class WeatherIcon : uint8_t {{
    {weather_enum},
    COUNT
}};

enum class MoonIcon : uint8_t {{
    {moon_enum},
    COUNT
}};

enum class SunIcon : uint8_t {{
    {sun_enum},
    COUNT
}};

const BakedIcon& weatherIconCurrent(WeatherIcon k);
const BakedIcon& weatherIconForecast(WeatherIcon k);
const BakedIcon& weatherIconChart(WeatherIcon k);
const BakedIcon& sunIconChart(SunIcon k);
const BakedIcon& moonIcon(MoonIcon k);
const BakedIcon& visibilityIcon();
const BakedIcon& humidityDropFilled();
const BakedIcon& humidityDropEmpty();

inline uint8_t iconPixelIndex(const BakedIcon& icon, int i) {{
    uint8_t byte = icon.pixels[i / 2];
    return (i % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
}}

inline bool iconMaskBit(const BakedIcon& icon, int i) {{
    uint8_t byte = icon.mask[i / 8];
    return (byte >> (7 - (i % 8))) & 1;
}}
"""
    with open(os.path.join(OUT_DIR, "icons_data.h"), "w", newline="\n") as f:
        f.write(header)


def write_source(baker, weather_current, weather_forecast, weather_chart, sun_chart, moon,
                  visibility_name, humidity_filled_name, humidity_empty_name):
    lines = [
        "// GENERATED FILE - do not edit by hand. Run tools/convert_assets.py to regenerate.",
        '#include "icons_data.h"',
        "",
    ]
    for c_name, w, h, pixel_bytes, mask_bytes in baker.entries:
        lines.append(f"static const uint8_t {c_name}_pixels[] = {{{c_bytes(pixel_bytes)}}};")
        lines.append(f"static const uint8_t {c_name}_mask[] = {{{c_bytes(mask_bytes)}}};")
        lines.append(f"static const BakedIcon {c_name} = {{{w}, {h}, {c_name}_pixels, {c_name}_mask}};")
        lines.append("")

    def table(name, type_, entries_dict):
        rows = ", ".join(f"&{entries_dict[n]}" for n in entries_dict)
        lines.append(f"static const BakedIcon* const {name}[] = {{{rows}}};")

    table("kWeatherCurrentTable", "WeatherIcon", weather_current)
    table("kWeatherForecastTable", "WeatherIcon", weather_forecast)
    table("kWeatherChartTable", "WeatherIcon", weather_chart)
    table("kSunChartTable", "SunIcon", sun_chart)
    table("kMoonTable", "MoonIcon", moon)

    lines += [
        "",
        "const BakedIcon& weatherIconCurrent(WeatherIcon k) { return *kWeatherCurrentTable[static_cast<uint8_t>(k)]; }",
        "const BakedIcon& weatherIconForecast(WeatherIcon k) { return *kWeatherForecastTable[static_cast<uint8_t>(k)]; }",
        "const BakedIcon& weatherIconChart(WeatherIcon k) { return *kWeatherChartTable[static_cast<uint8_t>(k)]; }",
        "const BakedIcon& sunIconChart(SunIcon k) { return *kSunChartTable[static_cast<uint8_t>(k)]; }",
        "const BakedIcon& moonIcon(MoonIcon k) { return *kMoonTable[static_cast<uint8_t>(k)]; }",
        f"const BakedIcon& visibilityIcon() {{ return {visibility_name}; }}",
        f"const BakedIcon& humidityDropFilled() {{ return {humidity_filled_name}; }}",
        f"const BakedIcon& humidityDropEmpty() {{ return {humidity_empty_name}; }}",
        "",
    ]
    with open(os.path.join(OUT_DIR, "icons_data.cpp"), "w", newline="\n") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    generate()

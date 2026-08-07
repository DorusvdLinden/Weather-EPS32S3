#!/usr/bin/env python3
"""Renders a preview of what the weather display actually looks like, using
real live Open-Meteo data and this project's real v1 design - not a
generic placeholder like dev_server.py's palette-swatch image.

This is a Python/Pillow reimplementation of the render pipeline
(weather/open_meteo.cpp -> canvas.cpp -> widgets/*.cpp -> palette.h/dither.cpp),
not the firmware itself - there's no host build of the ESP32-Arduino C++
(see tools/dev_server.py's docstring for why). It exists purely to answer
"what would this actually look like" without hardware:

- Layout regions, Dutch labels/formatting, weather-code->icon mapping,
  data-point content, and the v1 simplification (text data points instead
  of hand-drawn gauges, see README decision #3) all match the C++ exactly.
- Chart and forecast-card drawing are ported from InkyPiZero's own
  chart.py/forecast.py (this project's reference implementation) rather
  than re-derived, since chart.cpp/forecast.cpp are themselves faithful
  ports of those - not simplified like the gauges are.
- Text uses Arial/Arial Bold (whatever Pillow can find), not the firmware's
  actual U8g2 bitmap fonts or InkyPiZero's Jost - a font swap doesn't
  change the layout math, so this is a legitimate stand-in for "does the
  design work", not a fidelity claim about exact typography.
- The final image is quantized to the real 7-color ACeP palette with
  Floyd-Steinberg dithering (Pillow's own quantize(), the same algorithm
  dither.cpp reimplements and the same one Pimoroni's `inky` library uses),
  so what you see is what the panel would actually show.

Usage:
    python tools/mock_render.py [--lat 51.0004365] [--lon 5.8993687]

Saves two images into Mock-Display/ (never overwritten - see that folder's
README): "<ts>_true.png" (full RGB, before quantization) and
"<ts>_panel.png" (dithered to the 7-color palette - what the device
actually shows).
"""
import argparse
import json
import math
import os
import sys
import time
import urllib.request
from datetime import datetime

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("mock_render.py requires Pillow: pip install pillow", file=sys.stderr)
    sys.exit(1)

try:
    from astral import moon as astral_moon
except ImportError:
    astral_moon = None  # moon phase icons just get skipped if astral isn't installed

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
ICON_DIR = os.path.join(REPO_ROOT, "assets", "icons")
MOCK_DISPLAY_DIR = os.path.join(REPO_ROOT, "Mock-Display")

# ---- Config defaults, matching include/config.h ----
DEFAULT_LAT = 51.0004365
DEFAULT_LON = 5.8993687
UNITS = "metric"
TIME_FORMAT = "24h"
FORECAST_DAYS = 7
GRAPH_ICON_STEP = 2
SHOW_MOON_PHASE = False
BG_COLOR = (0xFF, 0xFF, 0xFF)  # white - matches include/config.h; dithers cleanly, unlike a pastel
TEXT_COLOR = (0, 0, 0)

# ---- Palette, matching src/palette.h's kRgb (Pimoroni's measured panel
# colors, not idealized primaries) ----
PALETTE = [
    (0, 0, 0), (217, 242, 255), (3, 124, 76), (27, 46, 198),
    (245, 80, 34), (255, 255, 68), (239, 121, 44),
]

# ---- Layout, matching src/layout.h ----
CANVAS_W, CANVAS_H = 800, 480
HEADER = (0, 6, 800, 34)
TODAY = (0, HEADER[1] + HEADER[3] + 6, 800, 165)
CURRENT_TEMPERATURE = (TODAY[0], TODAY[1], 320, TODAY[3])
DATA_POINTS = (320, TODAY[1], 480, TODAY[3])
CHART_AREA = (0, TODAY[1] + TODAY[3] + 6, 800, 150)
FORECAST_ROW = (0, CHART_AREA[1] + CHART_AREA[3] + 8, 800, 95)
DATA_POINT_COLS, DATA_POINT_ROWS = 2, 3
DATA_POINT_ICON_FRACTION = 0.38


def region_right(r):
    return r[0] + r[2]


def region_bottom(r):
    return r[1] + r[3]


def data_point_cell(i):
    col, row = i % DATA_POINT_COLS, i // DATA_POINT_COLS
    cw, ch = DATA_POINTS[2] // DATA_POINT_COLS, DATA_POINTS[3] // DATA_POINT_ROWS
    return (DATA_POINTS[0] + col * cw, DATA_POINTS[1] + row * ch, cw, ch)


def forecast_card(i, count):
    gap = 12
    cw = (FORECAST_ROW[2] - gap * (count - 1)) // count
    return (FORECAST_ROW[0] + i * (cw + gap), FORECAST_ROW[1], cw, FORECAST_ROW[3])


# ---- Dutch formatting, matching src/weather/open_meteo.cpp ----
WEEKDAYS = ["maandag", "dinsdag", "woensdag", "donderdag", "vrijdag", "zaterdag", "zondag"]
WEEKDAYS_ABBR = ["ma", "di", "wo", "do", "vr", "za", "zo"]
MONTHS = ["januari", "februari", "maart", "april", "mei", "juni",
          "juli", "augustus", "september", "oktober", "november", "december"]


def format_date_nl(dt):
    s = f"{WEEKDAYS[dt.weekday()]} {dt.day} {MONTHS[dt.month - 1]}"
    return s[0].upper() + s[1:]


def map_weather_code_to_icon(code, is_day):
    table = {
        0: "01d", 1: "022d", 2: "02d", 3: "04d",
        51: "51d", 61: "51d", 80: "51d",
        53: "53d", 63: "53d", 81: "53d",
        55: "09d", 65: "09d", 82: "09d",
        45: "50d", 48: "48d",
        56: "56d", 66: "56d", 57: "57d", 67: "57d",
        71: "71d", 85: "71d", 73: "73d",
        75: "13d", 86: "13d", 77: "77d",
        95: "11d", 96: "11d", 99: "11d",
    }
    icon = table.get(code, "01d")
    if not is_day:
        icon = {"01d": "01n", "022d": "022n", "02d": "02n"}.get(icon, icon)
    return icon


def wind_direction_nl(deg):
    dirs = ["N", "NO", "O", "ZO", "Z", "ZW", "W", "NW"]
    return dirs[round(deg / 45) % 8]


def beaufort_nl(speed_ms):
    levels = [
        (0.3, "Windstil"), (1.6, "Zwakke wind"), (3.4, "Zwakke wind"),
        (5.5, "Matige wind"), (8.0, "Matige wind"), (10.8, "Vrij krachtige wind"),
        (13.9, "Krachtige wind"), (17.2, "Harde wind"), (20.8, "Stormachtig"),
        (24.5, "Storm"), (28.5, "Zware storm"), (32.7, "Zeer zware storm"),
    ]
    for upper, label in levels:
        if speed_ms < upper:
            return label
    return "Orkaan"


def aqi_scale_nl(aqi):
    scale = ["Goed", "Redelijk", "Matig", "Slecht", "Zeer slecht", "Extreem slecht"]
    return scale[min(int(aqi // 20), 5)]


def humidity_drop_count(humidity):
    return min(5, max(1, math.ceil(humidity / 20)))


# ---- Fetch (same endpoints/params as weather/open_meteo.cpp) ----

def http_get_json(url, headers=None):
    req = urllib.request.Request(url, headers=headers or {"User-Agent": "Weather-EPS32S3-mock-render"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.load(resp)


def fetch_weather(lat, lon):
    url = (
        f"https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}"
        "&hourly=weather_code,temperature_2m,precipitation,relative_humidity_2m,surface_pressure,visibility"
        "&daily=weathercode,temperature_2m_max,temperature_2m_min,sunrise,sunset"
        "&current=temperature,windspeed,winddirection,is_day,weather_code,apparent_temperature"
        f"&timezone=auto&models=best_match&forecast_days={FORECAST_DAYS + 1}"
        "&temperature_unit=celsius&wind_speed_unit=ms&precipitation_unit=mm"
    )
    return http_get_json(url)


def fetch_aqi(lat, lon):
    url = (
        f"https://air-quality-api.open-meteo.com/v1/air-quality?latitude={lat}&longitude={lon}"
        "&hourly=european_aqi,uv_index&timezone=auto"
    )
    return http_get_json(url)


def fetch_location_name(lat, lon):
    try:
        url = f"https://nominatim.openstreetmap.org/reverse?lat={lat}&lon={lon}&format=jsonv2&accept-language=nl&zoom=14"
        data = http_get_json(url)
        addr = data.get("address", {})
        city = next((addr[k] for k in ("city", "town", "village", "municipality", "hamlet", "suburb", "county")
                     if addr.get(k)), "")
        country = addr.get("country", "")
        return f"{city}, {country}" if city and country else (city or country)
    except Exception as e:
        print(f"location lookup failed (non-fatal): {e}", file=sys.stderr)
        return ""


def value_at_hour(times, values, hour):
    for t, v in zip(times, values):
        if datetime.fromisoformat(t).hour == hour and v is not None:
            return v
    return None


def parse_snapshot(weather, aqi, lat):
    current = weather["current"]
    now = datetime.fromisoformat(current["time"])
    hourly = weather["hourly"]
    daily = weather["daily"]
    aqi_hourly = aqi["hourly"]

    current_icon = map_weather_code_to_icon(current["weather_code"], current["is_day"])

    # hourly slice: first 24 entries from "now"
    times = hourly["time"]
    start = 0
    for i, t in enumerate(times):
        dt = datetime.fromisoformat(t)
        if dt.date() == now.date() and dt.hour >= now.hour:
            start = i
            break
        if dt.date() > now.date():
            break
    sun_map = {}
    for sr, ss in zip(daily["sunrise"], daily["sunset"]):
        sr_dt, ss_dt = datetime.fromisoformat(sr), datetime.fromisoformat(ss)
        sun_map[sr_dt.date()] = (sr_dt, ss_dt)

    hourly_points = []
    for i in range(start, min(start + 24, len(times))):
        dt = datetime.fromisoformat(times[i])
        sunrise, sunset = sun_map.get(dt.date(), (None, None))
        is_day = bool(sunrise and sunset and sunrise <= dt < sunset)
        hourly_points.append({
            "time_label": dt.strftime("%H:00"),
            "temp": round(hourly["temperature_2m"][i]),
            "rain": hourly["precipitation"][i] or 0,
            "icon": map_weather_code_to_icon(hourly["weather_code"][i], is_day),
        })

    sun_events = []
    if hourly_points:
        start_dt = datetime.fromisoformat(times[start])
        end_dt = datetime.fromisoformat(times[start + len(hourly_points) - 1])
        for sr_dt, ss_dt in sun_map.values():
            for dt, key in ((sr_dt, "sunrise"), (ss_dt, "sunset")):
                if start_dt <= dt <= end_dt:
                    sun_events.append({"position": (dt - start_dt).total_seconds() / 3600, "icon": key})

    # daily forecast (skip today, matches daily_forecast[1:] in the original)
    daily_forecast = []
    for i in range(len(daily["time"])):
        dt = datetime.fromisoformat(daily["time"][i])
        entry = {
            "day_label": WEEKDAYS_ABBR[dt.weekday()],
            "icon": map_weather_code_to_icon(daily["weathercode"][i], True),
            "high": round(daily["temperature_2m_max"][i]),
            "low": round(daily["temperature_2m_min"][i]),
        }
        if astral_moon:
            target = dt.date()
            age = astral_moon.phase(target)
            entry["moon_pct"] = f"{(1 - math.cos(2 * math.pi * age / 29.530588853)) / 2 * 100:.0f}"
        daily_forecast.append(entry)
    forecast_high = daily_forecast[0]["high"] if daily_forecast else 0
    forecast_low = daily_forecast[0]["low"] if daily_forecast else 0
    daily_forecast = daily_forecast[1:FORECAST_DAYS + 1]

    # data points (v1-simplified: text only, matches widgets/data_points.cpp)
    wind_speed, wind_deg = current["windspeed"], current["winddirection"]
    humidity = value_at_hour(hourly["time"], hourly["relative_humidity_2m"], now.hour)
    pressure = value_at_hour(hourly["time"], hourly["surface_pressure"], now.hour)
    uv = value_at_hour(aqi_hourly["time"], aqi_hourly["uv_index"], now.hour)
    visibility_raw = value_at_hour(hourly["time"], hourly["visibility"], now.hour)
    aqi_val = value_at_hour(aqi_hourly["time"], aqi_hourly["european_aqi"], now.hour)

    visibility_km = visibility_raw * 0.001 if visibility_raw is not None else None
    data_points = [
        {"kind": "wind", "label": beaufort_nl(wind_speed), "direction": wind_direction_nl(wind_deg),
         "value": f"{wind_speed:.0f} m/s"},
        {"kind": "humidity", "label": "Vochtigheid", "value": f"{humidity:.0f} %" if humidity is not None else "N/A",
         "drop_count": humidity_drop_count(humidity) if humidity is not None else 1},
        {"kind": "pressure", "label": "Luchtdruk", "value": f"{pressure:.0f} hPa" if pressure is not None else "N/A"},
        {"kind": "uv", "label": "UV-index", "value": f"{uv:.1f}" if uv is not None else "N/A"},
        {"kind": "visibility", "label": "Zicht",
         "value": (f">={visibility_km:.1f} km" if visibility_km >= 10 else f"{visibility_km:.1f} km")
                  if visibility_km is not None else "N/A"},
        {"kind": "aqi", "label": "Luchtkwaliteit",
         "value": f"{aqi_val:.0f} {aqi_scale_nl(aqi_val)}" if aqi_val is not None else "N/A"},
    ]

    return {
        "current_date": format_date_nl(now),
        "current_icon": current_icon,
        "current_temp": round(current["temperature"]),
        "feels_like": round(current.get("apparent_temperature", current["temperature"])),
        "temp_unit": "C",
        "forecast_high": forecast_high,
        "forecast_low": forecast_low,
        "last_refresh": now.strftime("%H:%M"),
        "hourly": hourly_points,
        "sun_events": sun_events,
        "daily": daily_forecast,
        "data_points": data_points,
    }


# ---- Icons ----

_icon_cache = {}


def load_icon(key, size=None):
    cache_key = (key, size)
    if cache_key in _icon_cache:
        return _icon_cache[cache_key]
    path = os.path.join(ICON_DIR, f"{key}.png")
    if not os.path.exists(path):
        return None
    raw = Image.open(path).convert("RGBA")
    bbox = raw.getbbox()
    img = raw.crop(bbox) if bbox else raw
    if size:
        target_w, target_h = size
        scale = min(target_w / img.width, target_h / img.height)
        fit_w, fit_h = max(1, round(img.width * scale)), max(1, round(img.height * scale))
        fitted = img.resize((fit_w, fit_h), Image.LANCZOS)
        canvas = Image.new("RGBA", size, (0, 0, 0, 0))
        canvas.paste(fitted, ((target_w - fit_w) // 2, (target_h - fit_h) // 2), fitted)
        img = canvas
    _icon_cache[cache_key] = img
    return img


# ---- Fonts ----

def load_font(bold, size):
    candidates = (
        ["C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/segoeuib.ttf"] if bold
        else ["C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/segoeui.ttf"]
    )
    for path in candidates:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


# ---- Drawing: header + current conditions + data points (canvas.cpp / widgets/data_points.cpp) ----

def draw_header(draw, data):
    font_date = load_font(True, 22)
    font_refresh = load_font(True, 14)
    text = data["current_date"]
    if data.get("location"):
        text += f", {data['location']}"
    draw.text((HEADER[0], region_bottom(HEADER)), text, font=font_date, fill=TEXT_COLOR, anchor="lb")
    draw.text((region_right(HEADER) - 4, HEADER[1]), f"Laatste update: {data['last_refresh']}",
               font=font_refresh, fill=(51, 51, 51), anchor="ra")


def draw_current_conditions(image, draw, data):
    region = CURRENT_TEMPERATURE
    icon_size = int(region[3] * 0.62)
    icon = load_icon(data["current_icon"], (icon_size, icon_size))
    icon_cx = region[0] + int(region[2] * 0.22)
    icon_y = region[1] + int(region[3] * 0.12)
    if icon:
        image.paste(icon, (icon_cx - icon_size // 2, icon_y), icon)

    text_cx = region[0] + int(region[2] * 0.68)
    font_temp, font_unit, font_small = load_font(True, 64), load_font(True, 24), load_font(True, 15)
    temp_str = str(data["current_temp"])
    temp_y = region[1] + int(region[3] * 0.42)
    draw.text((text_cx, temp_y), temp_str, font=font_temp, fill=TEXT_COLOR, anchor="mm")
    temp_w = draw.textlength(temp_str, font=font_temp)
    draw.text((text_cx + temp_w / 2 + 4, temp_y - 22), f"\u00b0{data['temp_unit']}", font=font_unit,
               fill=TEXT_COLOR, anchor="lm")
    draw.text((text_cx, temp_y + 34), f"Gevoelstemp. {data['feels_like']}\u00b0", font=font_small,
               fill=TEXT_COLOR, anchor="mm")
    draw.text((text_cx, temp_y + 56), f"{data['forecast_high']}\u00b0 / {data['forecast_low']}\u00b0",
               font=font_small, fill=TEXT_COLOR, anchor="mm")


def draw_humidity_drops(image, box, filled_count):
    drop_h = max(1, int(box[3] * 0.5))
    drop_w = max(1, int(drop_h * 28 / 38))
    filled = load_icon("humidity_drop_filled", (drop_w, drop_h))
    empty = load_icon("humidity_drop_empty", (drop_w, drop_h))
    cx, cy = box[0] + box[2] // 2, box[1] + box[3] // 2
    spacing = drop_w
    row1_y, row2_y = cy - box[3] * 0.16, cy + box[3] * 0.22

    def row_positions(count, y):
        start_x = cx - spacing * (count - 1) / 2
        return [(start_x + i * spacing, y) for i in range(count)]

    positions = row_positions(3, row1_y) + row_positions(2, row2_y)
    for i, (x, y) in enumerate(positions):
        icon = filled if i < filled_count else empty
        if icon:
            image.paste(icon, (int(x - drop_w / 2), int(y - drop_h / 2)), icon)


def draw_data_points(image, draw, data):
    font_label, font_value = load_font(False, 13), load_font(True, 18)
    for i, dp in enumerate(data["data_points"]):
        cell = data_point_cell(i)
        icon_w = int(cell[2] * DATA_POINT_ICON_FRACTION)
        icon_box = (cell[0], cell[1], icon_w, cell[3])

        if dp["kind"] == "humidity":
            draw_humidity_drops(image, icon_box, dp["drop_count"])
        elif dp["kind"] == "visibility":
            w, h = int(icon_box[2] * 0.55), int(icon_box[3] * 0.55)
            icon = load_icon("visibility", (w, h))
            if icon:
                image.paste(icon, (icon_box[0] + (icon_box[2] - w) // 2, icon_box[1] + (icon_box[3] - h) // 2), icon)
        # wind/pressure/uv/aqi: no icon in v1 (matches widgets/data_points.cpp)

        text_x = cell[0] + icon_w + 8
        label_y = cell[1] + int(cell[3] * 0.28)
        value_y = cell[1] + int(cell[3] * 0.68)
        draw.text((text_x, label_y), dp["label"], font=font_label, fill=TEXT_COLOR, anchor="lm")
        value_text = f"{dp['direction']} {dp['value']}" if dp.get("direction") else dp["value"]
        draw.text((text_x, value_y), value_text, font=font_value, fill=TEXT_COLOR, anchor="lm")


# ---- Drawing: chart (ported from InkyPiZero's widgets/chart.py - see module docstring) ----

ORANGE, BLUE, FILL_YELLOW = (230, 81, 0), (13, 71, 161), (252, 204, 5)
CHART_LEFT, CHART_RIGHT, CHART_TOP, CHART_BOTTOM, CHART_ICON = 34, 42, 12, 34, 20


def _vertical_text(target, position, text, font, color):
    bbox = font.getbbox(text)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    if w <= 0 or h <= 0:
        return
    txt_img = Image.new("RGBA", (w + 2, h + 2), (0, 0, 0, 0))
    ImageDraw.Draw(txt_img).text((-bbox[0], -bbox[1]), text, font=font, fill=color)
    rotated = txt_img.rotate(90, expand=True)
    x, y = position
    target.paste(rotated, (int(x), int(y - rotated.height // 2)), rotated)


def draw_chart(image, data):
    draw = ImageDraw.Draw(image, "RGBA")
    hourly = data["hourly"]
    n = len(hourly)
    if n == 0:
        return

    plot_x0, plot_x1 = CHART_AREA[0] + CHART_LEFT, region_right(CHART_AREA) - CHART_RIGHT
    plot_y0, plot_y1 = CHART_AREA[1] + CHART_TOP, region_bottom(CHART_AREA) - CHART_BOTTOM
    plot_w, plot_h = plot_x1 - plot_x0, plot_y1 - plot_y0

    temps = [h["temp"] for h in hourly]
    rains = [h["rain"] for h in hourly]
    actual_min, actual_max = min(temps), max(temps)
    min_temp, max_temp = min(actual_min, 0), max(actual_max, 0)
    temp_span = (max_temp - min_temp) or 1
    rain_axis_max = max(1, math.ceil(max(rains, default=0) * 10) / 10)

    band = plot_w / n
    xs = [plot_x0 + band * (i + 0.5) for i in range(n)]

    def y_temp(v):
        return plot_y0 + (max_temp - v) / temp_span * plot_h

    def y_rain(v):
        return plot_y0 + (rain_axis_max - v) / rain_axis_max * plot_h

    y_zero = y_temp(0)

    for x, rain in zip(xs, rains):
        if rain < rain_axis_max * 0.03:
            continue
        top = y_rain(rain)
        w = band * 0.85
        draw.rectangle([x - w / 2, top, x + w / 2, plot_y1], fill=(*BLUE, 130))
        draw.rectangle([x - w / 2, top, x + w / 2, min(top + 3, plot_y1)], fill=(*BLUE, 230))

    curve = [(x, y_temp(t)) for x, t in zip(xs, temps)]
    draw.polygon(curve + [(xs[-1], y_zero), (xs[0], y_zero)], fill=(*FILL_YELLOW, 90))
    for (x1, y1), (x2, y2), t1, t2 in zip(curve, curve[1:], temps, temps[1:]):
        color = ORANGE if (t1 + t2) >= 0 else BLUE
        draw.line([(x1, y1), (x2, y2)], fill=color, width=4, joint="curve")

    font_bold12, font_small12 = load_font(True, 12), load_font(False, 12)
    for value, dy, color in [(actual_max, -14, ORANGE if actual_max >= 0 else BLUE),
                              (actual_min, 4, ORANGE if actual_min >= 0 else BLUE)]:
        y = y_temp(value)
        x = plot_x0
        while x < plot_x1:
            draw.line([(x, y), (min(x + 5, plot_x1), y)], fill=color, width=2)
            x += 9
        draw.text((plot_x0 + plot_w / 2, y + dy), f"{value}\u00b0", font=font_bold12, fill=color, anchor="mm")

    draw.line([(plot_x0, plot_y0), (plot_x0, plot_y1)], fill=TEXT_COLOR, width=2)
    draw.line([(plot_x1, plot_y0), (plot_x1, plot_y1)], fill=TEXT_COLOR, width=2)
    draw.line([(plot_x0, plot_y1), (plot_x1, plot_y1)], fill=TEXT_COLOR, width=2)
    draw.text((plot_x0 - 6, y_temp(max_temp)), f"{max_temp}\u00b0", font=font_bold12, fill=TEXT_COLOR, anchor="rm")
    draw.text((plot_x0 - 6, y_temp(min_temp)), f"{min_temp}\u00b0", font=font_bold12, fill=TEXT_COLOR, anchor="rm")
    draw.text((plot_x1 + 6, y_rain(rain_axis_max)), f"{rain_axis_max:g} mm", font=font_bold12, fill=TEXT_COLOR, anchor="lm")
    draw.text((plot_x1 + 6, y_rain(0)), "0 mm", font=font_bold12, fill=TEXT_COLOR, anchor="lm")

    _vertical_text(image, (CHART_AREA[0] + 4, (plot_y0 + plot_y1) // 2), "C", font_bold12, TEXT_COLOR)
    _vertical_text(image, (region_right(CHART_AREA) - 16, (plot_y0 + plot_y1) // 2), "Regen", font_bold12, TEXT_COLOR)

    for i in range(0, n, GRAPH_ICON_STEP):
        draw.text((xs[i], plot_y1 + 6), hourly[i]["time_label"], font=font_small12, fill=TEXT_COLOR, anchor="ma")

    sun_icon_by_index = {}
    for event in data["sun_events"]:
        idx = max(0, min(round(event["position"]), n - 1))
        sun_icon_by_index[idx] = event["icon"]
    icon_y = plot_y1 + 18
    for i in range(n):
        key = sun_icon_by_index.get(i)
        if key is None and i % GRAPH_ICON_STEP != 0:
            continue
        key = key or hourly[i]["icon"]
        icon = load_icon(key, (CHART_ICON, CHART_ICON))
        if icon:
            image.paste(icon, (int(xs[i] - CHART_ICON / 2), icon_y), icon)


# ---- Drawing: forecast row (ported from InkyPiZero's widgets/forecast.py) ----

CARD_BORDER, CARD_FILL = (90, 156, 90), (227, 241, 227)


def draw_forecast_row(image, data):
    draw = ImageDraw.Draw(image)
    count = len(data["daily"])
    if count == 0:
        return
    for i, day in enumerate(data["daily"]):
        region = forecast_card(i, count)
        draw.rounded_rectangle([region[0], region[1], region_right(region) - 1, region_bottom(region) - 1],
                                radius=8, outline=CARD_BORDER, width=2, fill=CARD_FILL)
        icon_size = int(min(region[2] * 0.85, region[3] * 0.45))
        icon = load_icon(day["icon"], (icon_size, icon_size))
        cx = region[0] + region[2] // 2
        top_y = region[1] + 6
        if icon:
            image.paste(icon, (cx - icon_size // 2, top_y), icon)
        font_bold = load_font(True, max(10, int(region[2] * 0.15)))
        temps_y = top_y + icon_size + 4
        draw.text((cx, temps_y), day["day_label"], font=font_bold, fill=TEXT_COLOR, anchor="ma")
        draw.text((cx, temps_y + font_bold.size + 1), f"{day['high']}\u00b0 / {day['low']}\u00b0",
                   font=font_bold, fill=TEXT_COLOR, anchor="ma")


# ---- Top-level render + palette quantization ----

def render(data):
    image = Image.new("RGB", (CANVAS_W, CANVAS_H), BG_COLOR)
    draw = ImageDraw.Draw(image)
    draw_header(draw, data)
    draw_current_conditions(image, draw, data)
    draw_data_points(image, draw, data)
    draw_chart(image, data)
    draw_forecast_row(image, data)
    return image


def quantize_to_panel(image):
    """Floyd-Steinberg dither to the 7-color ACeP palette - same algorithm
    as dither.cpp / Pillow's own quantize(), which is what Pimoroni's inky
    library uses too (see README's Dithering section)."""
    pal_img = Image.new("P", (1, 1))
    flat_palette = [c for rgb in PALETTE for c in rgb] + list(PALETTE[0]) * (256 - len(PALETTE))
    pal_img.putpalette(flat_palette)
    return image.convert("RGB").quantize(palette=pal_img, dither=Image.FLOYDSTEINBERG).convert("RGB")


def timestamped_path(prefix):
    os.makedirs(MOCK_DISPLAY_DIR, exist_ok=True)
    base = os.path.join(MOCK_DISPLAY_DIR, f"{prefix}_{time.strftime('%Y%m%d_%H%M%S')}")
    path = base + ".png"
    n = 1
    while os.path.exists(path):
        path = f"{base}_{n}.png"
        n += 1
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--lat", type=float, default=DEFAULT_LAT)
    parser.add_argument("--lon", type=float, default=DEFAULT_LON)
    parser.add_argument("--no-location", action="store_true", help="Skip the Nominatim reverse-geocode lookup")
    args = parser.parse_args()

    print(f"Fetching live weather for ({args.lat}, {args.lon})...")
    weather = fetch_weather(args.lat, args.lon)
    aqi = fetch_aqi(args.lat, args.lon)
    data = parse_snapshot(weather, aqi, args.lat)
    data["location"] = "" if args.no_location else fetch_location_name(args.lat, args.lon)

    print("Rendering...")
    true_image = render(data)
    panel_image = quantize_to_panel(true_image)

    true_path = timestamped_path("mockrender_true")
    panel_path = timestamped_path("mockrender_panel")
    true_image.save(true_path)
    panel_image.save(panel_path)
    print(f"Wrote {true_path}")
    print(f"Wrote {panel_path}")


if __name__ == "__main__":
    main()

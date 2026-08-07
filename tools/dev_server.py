#!/usr/bin/env python3
"""Local dev server standing in for the ESP32-S3's setup/debug web portal
(src/setup_portal.cpp) - lets you iterate on the settings page's look and
routes without flashing real hardware (todo.md item 2: "add a --dev option
to run the code on a local machine... including hosting of the website").

This does NOT run the real firmware. The weather fetch/render/dither
pipeline is ESP32-Arduino-specific C++ (WiFiClientSecure, Adafruit_GFX,
GxEPD2, PSRAM allocators) with no practical host build, so there's nothing
to simulate there. What IS reproduced here is the *website*: the same
routes, form fields, and page layout as setup_portal.cpp, backed by a local
JSON file instead of NVS and local files instead of LittleFS - close enough
to validate the UI/UX before flashing, not a functional/behavioral twin of
the firmware. Keep this file's form fields and setup_portal.cpp's in sync
by hand if either changes - there's no shared template between the two
languages.

Usage:
    python tools/dev_server.py [--port 8080]
    python tools/dev_server.py --snapshot   # generate one Mock-Display/ image and exit

Then browse http://127.0.0.1:8080/. Settings/log save to tools/dev_data/
(gitignored, ephemeral); preview images save to Mock-Display/ (tracked,
never overwritten or deleted - see Mock-Display/README.md).
"""
import argparse
import http.server
import json
import os
import socketserver
import sys
import time
import urllib.parse

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
MOCK_DISPLAY_DIR = os.path.join(REPO_ROOT, "Mock-Display")

DATA_DIR = os.path.join(SCRIPT_DIR, "dev_data")
SETTINGS_PATH = os.path.join(DATA_DIR, "settings.json")
LOG_PATH = os.path.join(DATA_DIR, "debug_log.txt")

# Mirrors DisplayConfig's default member initializers in include/config.h.
DEFAULT_SETTINGS = {
    "wifi_ssid": "", "wifi_password": "",
    "lat": 51.0004365, "lon": 5.8993687,
    "units": "metric", "timezone": "Europe/Amsterdam", "time_format": "24h",
    "forecast_days": 7, "graph_icon_step": 2, "show_moon": False,
    "bg_color": "#FFFFFF", "text_color": "#000000",
    "refresh_s": 600,
}

# Index-aligned with palette::Index in src/palette.h.
PALETTE = [(0, 0, 0), (217, 242, 255), (3, 124, 76), (27, 46, 198), (245, 80, 34), (255, 255, 68), (239, 121, 44)]


def load_settings():
    if os.path.exists(SETTINGS_PATH):
        with open(SETTINGS_PATH) as f:
            data = json.load(f)
        merged = dict(DEFAULT_SETTINGS)
        merged.update(data)
        return merged
    return dict(DEFAULT_SETTINGS)


def save_settings(settings):
    os.makedirs(DATA_DIR, exist_ok=True)
    with open(SETTINGS_PATH, "w") as f:
        json.dump(settings, f, indent=2)


def append_log(line):
    os.makedirs(DATA_DIR, exist_ok=True)
    with open(LOG_PATH, "a") as f:
        f.write(line + "\n")


def timestamped_mock_display_path(prefix):
    """A fresh, never-before-used path under Mock-Display/ - see that
    folder's README.md: nothing there is ever overwritten or deleted."""
    os.makedirs(MOCK_DISPLAY_DIR, exist_ok=True)
    base = os.path.join(MOCK_DISPLAY_DIR, f"{prefix}_{time.strftime('%Y%m%d_%H%M%S')}")
    path = base + ".png"
    n = 1
    while os.path.exists(path):
        path = f"{base}_{n}.png"
        n += 1
    return path


def latest_mock_display_image():
    if not os.path.isdir(MOCK_DISPLAY_DIR):
        return None
    pngs = [os.path.join(MOCK_DISPLAY_DIR, f) for f in os.listdir(MOCK_DISPLAY_DIR) if f.lower().endswith(".png")]
    return max(pngs, key=os.path.getmtime) if pngs else None


def generate_placeholder_image(path):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return False
    img = Image.new("RGB", (800, 480), (255, 255, 255))
    draw = ImageDraw.Draw(img)
    swatch_w = 800 // len(PALETTE)
    for i, color in enumerate(PALETTE):
        draw.rectangle([i * swatch_w, 0, (i + 1) * swatch_w, 120], fill=color)
    lines = [
        "Dev server placeholder - no real device capture found.",
        "Drop a real capture into Mock-Display/, e.g.:",
        "  python tools/dump_to_png.py --port COM5",
        f"Generated {time.strftime('%Y-%m-%d %H:%M:%S')}",
    ]
    for i, line in enumerate(lines):
        draw.text((20, 140 + i * 25), line, fill=(0, 0, 0))
    img.save(path)
    return True


def ensure_mock_display_image():
    """Returns a path to serve. Generates a first-ever placeholder only if
    Mock-Display/ has nothing in it yet; otherwise serves whatever's most
    recently there (a prior placeholder, a real capture, ...) - never
    generates a duplicate on every request."""
    existing = latest_mock_display_image()
    if existing:
        return existing
    path = timestamped_mock_display_path("dev_placeholder")
    return path if generate_placeholder_image(path) else None


def option(value, current, label):
    selected = " selected" if value == current else ""
    return f'<option value="{value}"{selected}>{label}</option>'


def render_page(settings):
    checked = " checked" if settings["show_moon"] else ""
    return f"""<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Weather-EPS32S3 setup (DEV)</title>
<style>body{{font-family:sans-serif;max-width:480px;margin:1.5em auto;padding:0 1em}}
fieldset{{margin-bottom:1em}}label{{display:block;margin-top:.5em}}
input,select{{width:100%;box-sizing:border-box;padding:.3em}}
img{{max-width:100%;border:1px solid #999}}
.row{{display:flex;gap:1em}}.row>div{{flex:1}}
.banner{{background:#fffae6;border:1px solid #e6c200;padding:.5em;margin-bottom:1em}}</style>
</head><body>
<div class="banner">DEV MODE - local simulator, not a real device. Mirrors src/setup_portal.cpp.</div>
<h1>Weather-EPS32S3</h1>
<p>Last render (see Mock-Display/ for every preview this has ever generated):</p>
<img src="/image" alt="last render">
<p><a href="/log">view step-by-step debug log</a></p>
<form method="POST" action="/save">
<fieldset><legend>WiFi</legend>
<label>SSID<input name="wifi_ssid" value="{settings['wifi_ssid']}"></label>
<label>Password (leave blank to keep current)<input type="password" name="wifi_password"></label>
</fieldset>
<fieldset><legend>Location &amp; units</legend>
<div class="row"><div><label>Latitude<input type="number" step="any" name="lat" value="{settings['lat']}"></label></div>
<div><label>Longitude<input type="number" step="any" name="lon" value="{settings['lon']}"></label></div></div>
<label>Units<select name="units">
{option("metric", settings["units"], "Metric (°C, m/s, km)")}
{option("imperial", settings["units"], "Imperial (°F, mph, mi)")}
{option("standard", settings["units"], "Standard (K, m/s, km)")}
</select></label>
<label>Timezone (informational only - Open-Meteo's timezone=auto drives rendered times)<input name="timezone" value="{settings['timezone']}"></label>
<label>Time format<select name="time_format">
{option("24h", settings["time_format"], "24-hour")}
{option("12h", settings["time_format"], "12-hour")}
</select></label>
<label>Forecast days (1-7)<input type="number" min="1" max="7" name="forecast_days" value="{settings['forecast_days']}"></label>
</fieldset>
<fieldset><legend>Display</legend>
<label><input type="checkbox" name="show_moon" value="1"{checked}> Show moon phase on forecast cards</label>
<div class="row"><div><label>Background color<input type="color" name="bg_color" value="{settings['bg_color']}"></label></div>
<div><label>Text color<input type="color" name="text_color" value="{settings['text_color']}"></label></div></div>
<label>Chart hour-label/icon step (1-6)<input type="number" min="1" max="6" name="graph_icon_step" value="{settings['graph_icon_step']}"></label>
<label>Refresh interval, seconds (deep-sleep wake cadence)<input type="number" min="60" name="refresh_s" value="{settings['refresh_s']}"></label>
</fieldset>
<button type="submit">Save (dev mode - no reboot)</button>
</form>
</body></html>"""


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"{self.address_string()} - {fmt % args}")

    def _respond(self, code, content_type, body_bytes):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body_bytes)))
        self.end_headers()
        self.wfile.write(body_bytes)

    def do_GET(self):
        if self.path in ("/", ""):
            self._respond(200, "text/html; charset=utf-8", render_page(load_settings()).encode("utf-8"))
        elif self.path == "/log":
            content = ""
            if os.path.exists(LOG_PATH):
                with open(LOG_PATH) as f:
                    content = f.read()
            if not content:
                content = "(no log yet - hit Save once to simulate a cycle)"
            self._respond(200, "text/plain; charset=utf-8", content.encode("utf-8"))
        elif self.path == "/image":
            path = ensure_mock_display_image()
            if path and os.path.exists(path):
                with open(path, "rb") as f:
                    self._respond(200, "image/png", f.read())
            else:
                self._respond(404, "text/plain", b"No image (Pillow not installed and Mock-Display/ is empty)")
        else:
            self._respond(404, "text/plain", b"Not found")

    def do_POST(self):
        if self.path != "/save":
            self._respond(404, "text/plain", b"Not found")
            return
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length).decode("utf-8")
        form = {k: v[0] for k, v in urllib.parse.parse_qs(raw).items()}

        settings = load_settings()
        for key in ("wifi_ssid", "units", "timezone", "time_format", "bg_color", "text_color"):
            if key in form:
                settings[key] = form[key]
        if form.get("wifi_password"):
            settings["wifi_password"] = form["wifi_password"]
        for key, cast in (("lat", float), ("lon", float), ("forecast_days", int),
                           ("graph_icon_step", int), ("refresh_s", int)):
            if key in form:
                try:
                    settings[key] = cast(form[key])
                except ValueError:
                    pass
        settings["show_moon"] = "show_moon" in form
        save_settings(settings)

        append_log(f"==== dev cycle @ {time.strftime('%Y-%m-%d %H:%M:%S')} ====")
        append_log("[OK  ] settings_saved - simulated in dev_server.py, no real device involved")

        self._respond(200, "text/html; charset=utf-8",
                       b'<html><body><p>Saved (dev mode - no reboot). <a href="/">Back</a></p></body></html>')


class ReusableTCPServer(socketserver.TCPServer):
    allow_reuse_address = True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--snapshot", action="store_true",
                         help="Generate one new timestamped placeholder into Mock-Display/ and exit, without starting the server.")
    args = parser.parse_args()

    if args.snapshot:
        path = timestamped_mock_display_path("dev_placeholder")
        if generate_placeholder_image(path):
            print(f"Saved {path}")
        else:
            print("Pillow not installed - cannot generate an image", file=sys.stderr)
            sys.exit(1)
        return

    ensure_mock_display_image()
    with ReusableTCPServer(("127.0.0.1", args.port), Handler) as httpd:
        print(f"Dev server running at http://127.0.0.1:{args.port}/  (Ctrl+C to stop)")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()

# Weather-EPS32S3

A standalone, battery-capable weather display firmware for the ESP32-S3,
driving a 7-color (ACeP) e-paper panel. No planning/repo has any code in it
yet — this is currently a design document only.

This project is a from-scratch firmware port of
[InkyPiZero](https://github.com/DorusvdLinden/InkyPiZero), a Raspberry
Pi + Python weather display for a Pimoroni Inky Impression panel. Rather than
extending that repo, the ESP32-S3 version lives here as its own project,
since it's a different language, toolchain, and hardware target end to end -
not a shared codebase.

## Context

InkyPiZero runs on a Raspberry Pi: a systemd timer fires a one-shot Python
process every 10 minutes that fetches weather from Open-Meteo, draws an
800x480 image with Pillow, and pushes it to a Pimoroni Inky Impression 7.3"
7-color (ACeP) e-paper panel via Pimoroni's `inky` library. That project's
own README documents a decision *against* an ESP32-S3 rewrite, for two
reasons: it reused Pimoroni's `inky` driver (SPI protocol + palette
dithering) unchanged, and it reused the Python fetch/parse logic almost
verbatim.

This repo revisits that decision. The motivation is a genuinely
self-contained single-chip device that can deep-sleep between updates for
real battery operation - something a Pi can't do between systemd timer
ticks. Three shape-defining decisions have already been made:

1. **Full standalone rewrite** - the ESP32-S3 does everything itself (WiFi,
   HTTPS fetch, JSON parse, rendering, ACeP dithering, panel SPI). No
   companion Pi/server.
2. **Display panel is undecided** - keep the exact Inky Impression 7.3"
   (confirmed AC073TC1A-driven) or swap to a different ESP32-friendly 7-color
   ACeP panel with existing library support. This plan flags it as an open
   decision rather than forcing a choice.
3. **Visual fidelity: simplified v1 first.** Don't try to pixel-match the
   hand-drawn gauges/chart initially - get the pipeline working end-to-end
   with simpler representations, then iterate toward the original design.

This document covers architecture, toolchain/library choices, a
module-by-module mapping from InkyPiZero's current Python code, a phased
build sequence, and the biggest risks - it does not write any code yet.

## Recommended architecture

**Toolchain: Arduino framework via PlatformIO**, not raw ESP-IDF or
MicroPython. Every library this port needs (color e-paper panel support,
JSON, HTTPS) is Arduino-ecosystem-first - ESP-IDF has no equivalent
color-e-paper library, so that path means writing a panel driver from scratch
on top of an already-hard problem. This costs nothing on the power-management
side: `arduino-esp32` sits directly on ESP-IDF and exposes the same
`esp_sleep.h` deep-sleep APIs unchanged. MicroPython is ruled out - no mature
ACeP driver exists for it, and GC pauses are a poor fit for repeatedly
manipulating a 187KB-1.1MB framebuffer.

**Board: ESP32-S3-DevKitC-1 N16R8** (16MB flash / 8MB PSRAM). The framebuffer
alone runs 187KB (4bpp packed, the panel's native format) to 1.1MB (24bpp
working buffer before dithering); the ~512KB of internal SRAM can't hold that
alongside WiFi/TLS buffers, so PSRAM is mandatory. 8MB gives headroom for the
framebuffer, ArduinoJson's parsed document, and baked icon/font assets
simultaneously, with room for a double-buffer during dithering. Avoid
non-PSRAM or 2MB-PSRAM S3 variants.

**Libraries:**
- **HTTPS**: `WiFiClientSecure` + `HTTPClient`, using `setCACertBundle()`
  (the Mozilla root-CA bundle built into `arduino-esp32`) rather than pinning
  individual certs - avoids a cert-expiry maintenance trap.
- **JSON**: ArduinoJson v7, with its `JsonDocument` bound to a PSRAM allocator
  (`ps_malloc`/`MALLOC_CAP_SPIRAM` - the default `malloc` will *not* reach
  PSRAM without this, an easy silent-OOM trap given the buffer sizes here).
  Open-Meteo's tens-of-KB responses fit comfortably; ArduinoJson's `Filter`
  feature can drop unused echoed fields to shrink parse time/RAM further.
- **2D graphics**: **Adafruit_GFX** (which GxEPD2 subclasses directly), not
  LVGL - LVGL has no native 7-color indexed format and its object-model
  overhead buys nothing for a simplified v1 layout. Adafruit_GFX's
  `getTextBounds()` maps directly to Pillow's `textbbox`/anchor-placement
  usage in InkyPiZero's `canvas.py`, and `fillRoundRect`/`drawLine`/
  `fillTriangle` cover forecast cards and (later) gauge needles, since
  InkyPiZero's `widgets/gauge.py` needle and dashed-circle math is already
  segment/polygon-based, not native-arc based - it ports mechanically. Thick
  arcs (pressure/AQI bands) need a small custom helper; deferred to the
  fidelity pass (Phase 5).
- **Font fallback**: `U8g2_for_Adafruit_GFX` - bridges u8g2's broad-Unicode
  fonts onto Adafruit_GFX targets. This directly fixes an existing open bug
  in InkyPiZero's `TODO.md` ("Missing font glyph fallback" for non-Latin
  Nominatim results) as a side effect of the port, rather than deferring it
  again.
- **Panel driving**: investigate `GxEPD2_730c_ACeP_730` (supports a Waveshare
  7.3" ACeP panel - same 800x480 resolution and 7-color technology as the
  Pimoroni panel) as the low-risk default. Keeping the exact Pimoroni
  AC073TC1A panel remains possible but means porting the SPI/waveform
  sequence from Pimoroni's Python source (`inky.inky_ac073tc1a`) with no
  existing C driver to lean on - a materially larger, separately-scoped task
  (see Phase 0b below).
- **Dithering**: Floyd-Steinberg error diffusion against the fixed 7-color
  ACeP palette. This is **not** a from-scratch algorithm-design problem -
  Pimoroni's `inky` library itself just calls Pillow's own
  `Image.quantize(palette=..., dither=Image.FLOYDSTEINBERG)` internally, and
  Pillow's dithering source (`libImaging`, MIT/HPND) is a legitimate, portable
  reference for both the error-diffusion loop and the palette-distance metric.

## Module mapping (InkyPiZero Python -> this repo's C++)

| InkyPiZero file | ESP32-S3 equivalent |
|---|---|
| `config.py` | `include/config.h` (constexpr struct) + a separate, gitignored `secrets.h` for WiFi credentials |
| `weather_data.py` | `src/weather/open_meteo.{h,cpp}` (fetch + parse); `src/weather/moon_phase.cpp` (hand-ported - `astral.moon.phase` has no C++ equivalent to reuse) |
| `layout.py` | `src/layout.h` (constexpr `Region`s - pure arithmetic, ports as-is) |
| `canvas.py` | `src/canvas.cpp` (render orchestrator) |
| `widgets/gauge.py`, `chart.py`, `forecast.py` | `src/widgets/*.cpp` - simplified per the v1 fidelity decision (numeric/bar readouts and a basic chart instead of the hand-drawn compass/barometer/sunburst/AQI arcs) |
| `widgets/icons.py` (`AssetStore`) | `src/assets/asset_store.cpp` + generated headers (no runtime resize - bake every icon at its exact on-screen size at build time) |
| `display/inky_driver.py` | `src/display/epd_driver.cpp` (GxEPD2 wrapper, or a from-scratch driver if the exact panel is kept) |
| `display/mock_driver.py` | A serial-framebuffer-dump debug driver (dump the buffer over UART, reassemble host-side into a PNG) - fills the same role `--mock-output` plays in InkyPiZero, letting Phase 2 be validated headless |
| `main.py` | `src/main.cpp` (`setup()`: WiFi -> fetch -> render -> dither -> push -> deep sleep) |
| `assets/` + `scripts/generate_icons.py` | `tools/convert_assets.py`, run as a PlatformIO `extra_scripts` pre-build hook, emitting `src/generated/icons_data.h` / `fonts_data.h`. Pre-quantize icons to the 7-color palette at build time - no point alpha-blending against arbitrary RGBA when the panel can only show 7 colors anyway |

## Phased build sequence

- **Phase 0a - Toolchain bring-up.** PlatformIO project, blink + serial
  "hello world." No panel involved. *Exit: board boots, prints over serial.*
- **Phase 0b - Panel decision spike.** Get GxEPD2 driving a candidate panel
  with a flat native-palette fill (no dithering yet - this deliberately
  decouples "SPI/wiring works" from "dithering works"). If keeping the exact
  Pimoroni panel, this phase's exit criterion becomes "SPI protocol
  reverse-engineered from `inky_ac073tc1a`," which is a real scope gate, not
  a formality. *Exit: a solid color renders correctly on the physical panel.*
- **Phase 1 - Data fetch only.** Port the three HTTP calls + JSON parsing;
  dump parsed values over serial, no rendering yet. Cache the reverse-geocoded
  location name in NVS (it never changes for a fixed lat/long) to avoid
  hitting Nominatim's rate limit on every wake. *Exit: serial log shows
  correct current/hourly/daily/AQI values matching a manual Open-Meteo query.*
- **Phase 2 - Simplified on-device rendering.** Build the framebuffer (PSRAM),
  port `layout.py`'s regions, draw simplified widgets + pre-baked icons/fonts.
  Validate via the serial-dump debug driver, compared against InkyPiZero's
  `mock_display_output/` renders. *Exit: dumped buffer visually matches the
  intended simplified layout.*
- **Phase 3 - Real panel output.** Implement Floyd-Steinberg dithering to the
  7-color palette, wire it into the panel driver from Phase 0b.
  *Exit: first full end-to-end render on real hardware.*
- **Phase 4 - Power/scheduling.** Move to deep-sleep + RTC-timer wake at the
  10-minute cadence, and actually measure active-time power draw (see Risks -
  the e-paper refresh itself, not WiFi, is likely the dominant cost).
  *Exit: measured sleep-cycle power budget, tuned wake interval if needed.*
- **Phase 5 - Fidelity iteration.** Replace simplified widgets with
  closer recreations of the original wind compass, barometer arc, UV
  sunburst, AQI band, and dual-axis chart, using InkyPiZero's
  `widgets/gauge.py`/`widgets/chart.py` as the exact behavioral spec.

## Key risks

1. **E-paper refresh time likely dominates the power budget, not WiFi.**
   AC073TC1A-family panels have a ~28s core refresh vs. a 2-5s WiFi/TLS
   handshake - budget deep-sleep math around ~30-45s active time per cycle.
2. **Panel-from-scratch effort** if the exact Pimoroni panel is kept
   (SPI + waveform reverse-engineering) vs. swapping to a GxEPD2-supported
   ACeP panel (confirmed existing class, much lower risk). This is the
   single biggest scope unknown in the whole plan - resolve it in Phase 0b
   before investing further.
3. **Moon-phase math** (`astral.moon`) has no C++ library to reuse - hand-port
   the algorithm.
4. **Font baking** - the ~10 fixed pixel sizes in use today each need a
   generated bitmap-font header (e.g. via `fontconvert`); non-Latin fallback
   should be solved with `U8g2_for_Adafruit_GFX` during the port rather than
   deferred again.
5. **PSRAM allocator misuse** - `malloc` silently stays in internal SRAM;
   framebuffer/JSON allocations must explicitly request PSRAM or this fails
   in a confusing way (subtle corruption/OOM, not a clean error).
6. **Nominatim rate limit** (1 req/sec, unique User-Agent) - mitigate by
   caching the resolved location name in NVS since lat/long are static
   per-device.

## Verification

Each phase has its own exit criterion above (serial logs -> headless
framebuffer dump compared against InkyPiZero's `mock_display_output/` -> real
panel photo -> measured power draw). There's no unit-test suite to run - this
is a hardware bring-up project, so "verification" means physically driving
the board at each milestone before moving to the next, the same
incremental-hardware-first approach InkyPiZero already uses today via
`--mock-output` for the Pi version.

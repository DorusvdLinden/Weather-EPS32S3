# Weather-EPS32S3

A standalone, battery-capable weather display firmware for the ESP32-S3,
driving a 7-color (ACeP) e-paper panel.

**Status:** Phases 0a, 1, and 2 are now confirmed end-to-end on real
hardware (see [Implementation status](#implementation-status) below) - a
real ESP32-S3-N16-class board boots, connects to WiFi, fetches live
Open-Meteo data, resolves its location, renders the full display (header,
current conditions, data points, chart, 7-day forecast), and dumps a
correct framebuffer, entirely correctly. Four real bugs were found and
fixed via this hardware bring-up (a stack overflow, an HTTP chunked-
encoding gap, a text-rendering transparency bug, and a date-parsing gap) -
none of them were visible from compiling or code review alone. Phase 3/4
(real panel, deep-sleep timing/power) are implemented and compile but
haven't run against real panel hardware yet. This document started as a
design-only plan; it now doubles as the record of that plan plus what's
actually been built and verified against
it.

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
  (see Phase 0b below). **Update:** both paths are now implemented, plus a
  third (a monochrome Waveshare panel) - see
  [Implementation status](#implementation-status) and
  [docs/hardware.md](docs/hardware.md). Phase 0b's actual *decision* between
  them is still open; this just means the decision no longer blocks writing
  code.
- **Dithering**: Floyd-Steinberg error diffusion against the fixed 7-color
  ACeP palette. This is **not** a from-scratch algorithm-design problem -
  Pimoroni's `inky` library itself just calls Pillow's own
  `Image.quantize(palette=..., dither=Image.FLOYDSTEINBERG)` internally, and
  Pillow's dithering source (`libImaging`, MIT/HPND) is a legitimate, portable
  reference for both the error-diffusion loop and the palette-distance metric.

## Module mapping (InkyPiZero Python -> this repo's C++)

| InkyPiZero file | ESP32-S3 equivalent |
|---|---|
| `config.py` | `include/config.h` (`DisplayConfig` struct) + `src/settings.{h,cpp}` (NVS-backed load/save, including WiFi credentials - superseded the originally-planned compile-time `secrets.h` once the [web setup/debug portal](#web-setupdebug-portal) made settings runtime-editable) |
| `weather_data.py` | `src/weather/open_meteo.{h,cpp}` (fetch + parse); `src/weather/moon_phase.cpp` (hand-ported - `astral.moon.phase` has no C++ equivalent to reuse) |
| `layout.py` | `src/layout.h` (constexpr `Region`s - pure arithmetic, ports as-is) |
| `canvas.py` | `src/canvas.cpp` (render orchestrator) |
| `widgets/gauge.py`, `chart.py`, `forecast.py` | `src/widgets/*.cpp` - simplified per the v1 fidelity decision (numeric/bar readouts and a basic chart instead of the hand-drawn compass/barometer/sunburst/AQI arcs) |
| `widgets/icons.py` (`AssetStore`) | `src/assets/asset_store.cpp` + generated headers (no runtime resize - bake every icon at its exact on-screen size at build time) |
| `display/inky_driver.py` | Three interchangeable `DisplaySink` implementations, one built at a time (see [docs/hardware.md](docs/hardware.md)): `src/display/epd_driver.cpp` and `src/display/waveshare_mono_driver.cpp` (GxEPD2 wrappers) and `src/display/inky_impression_driver.cpp` (from-scratch AC073TC1A port, since GxEPD2 has no driver for it) |
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
   AC073TC1A-family panels have a ~28-41s core refresh (Pimoroni's own
   driver measured ~41s) vs. a 2-5s WiFi/TLS handshake - budget deep-sleep
   math around ~30-45s active time per cycle for either 7-color panel. The
   monochrome Waveshare 7.5" V2 option added since (docs/hardware.md)
   refreshes in ~1.2s instead - likely the single biggest lever available
   for battery life, at the cost of color.
2. **Panel-from-scratch effort** if the exact Pimoroni panel is kept
   (SPI + waveform reverse-engineering) vs. swapping to a GxEPD2-supported
   ACeP panel (confirmed existing class, much lower risk). **Update:** the
   from-scratch effort is done - `InkyImpressionDriver` is a line-by-line
   port of Pimoroni's own Python driver (see docs/attribution.md) - but
   it's correspondingly the least hardware-verified of the three drivers
   (see docs/hardware.md's BUSY-polarity risk callout). The *choice*
   between panels is still open; only the "is it too big a task" question
   this risk originally flagged is resolved.
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

## Implementation status

The module-mapping table above is fully implemented in `src/`/`include/`,
and **compiles with zero errors and zero warnings** against the real
`espressif32` PlatformIO platform (`arduino-esp32` 3.20017, xtensa-esp32s3
toolchain). As of 2026-08-07, the headless build has also been **flashed to
and run on real hardware** - a genuine ESP32-S3 (QFN56, rev v0.2) with
embedded 8MB PSRAM, connected over USB (CH343 bridge, COM3) - the first
physical-hardware verification this project has had. Concretely:

- **Phase 0a, Phase 1, and Phase 2: confirmed end-to-end on real hardware**,
  not just compiled - flashed to a genuine ESP32-S3 (QFN56 rev v0.2, 8MB
  embedded PSRAM) over USB (CH343 bridge) on 2026-08-07. Full captured
  serial log for one complete wake cycle:
  `psramInit(): PSRAM enabled` -> `Weather-EPS32S3 boot` -> WiFi setup
  portal (first boot, no credentials saved) -> after WiFi credentials were
  entered via the portal on a real phone -> `[OK] boot` -> `[OK]
  wifi_connect - 192.168.1.140` -> `[OK] weather_fetch` -> `[OK]
  location_resolve - Sittard, Nederland` -> `[OK] canvas_render` -> a real
  192000-byte `SerialDumpDriver` framebuffer dump -> `[OK] panel_push` ->
  `[OK] sleep` -> `Render complete, entering deep sleep`. The dumped frame
  was reassembled with `tools/dump_to_png.py` into an actual rendered
  image - live weather, live-resolved location name, a correct 24h chart,
  and a correct 7-day forecast row, all real. That's every module in this
  table actually exercised together against live network data, not just
  individually plausible.

  **Four real bugs surfaced this way that no amount of compiling or code
  review had caught:**
  1. A **stack overflow** (`Guru Meditation Error... Stack canary watchpoint
     triggered (loopTask)`) triggered by WiFi's own internal auth-retry
     event processing on a bad password, crash-looping the device instead
     of failing gracefully into deep sleep. arduino-esp32's default
     `loopTask` stack is only 8KB; fixed with `SET_LOOP_TASK_STACK_SIZE(16384)`
     in `main.cpp` (that macro is arduino-esp32's own documented override).
  2. **`HTTPClient::getStream()` doesn't decode chunked transfer-encoding**
     (confirmed by reading the actual framework source after
     `deserializeJson` returned `InvalidInput` on live Open-Meteo
     responses) - only `getString()`/`writeToStream()` do. Fixed by
     buffering the response with `getString()` before parsing
     (`weather/open_meteo.cpp`'s `httpGetJson()`), trading the originally-
     intended streaming-parse memory savings for correctness.
  3. **All text rendered as solid black boxes** instead of legible glyphs.
     Root cause: `U8g2_for_Adafruit_GFX`'s `u8g2_SetFont()` silently resets
     font transparency to opaque on every font change; calling
     `setFontMode(1)` once at startup (as the code originally did) only
     survives until the first `setFont()` call anywhere. Fixed by
     re-asserting `setFontMode(1)` after every `setFont()` in
     `graphics/text.cpp`.
  4. **The entire forecast row silently rendered empty** (`0°/0°`,
     no cards) - `weather_fetch` itself reported success, so this wasn't
     caught by that check. Root cause: Open-Meteo's `daily` block returns
     *date-only* strings ("2026-08-07", no time component - daily
     granularity has nothing to put there), but `parseIsoLocal()` only
     accepted the full `current`/`hourly`-style datetime format, so every
     single daily entry silently failed to parse and `dailyCount` stayed
     0. Fixed with a date-only fallback in `open_meteo.cpp`.

  Every one of these was invisible from source review or compilation -
  they only showed up against a real chip, a real WiFi network with a
  transient auth hiccup, and real API responses. This is the concrete
  argument for why this README kept insisting "unverified without hardware"
  throughout: here is what four different unverified assumptions actually
  looked like once checked.
- **Phase 3 (real panel) and Phase 4 (power/scheduling):** the code paths
  exist for **three** panels now, not one - `EpdDriver` (`GxEPD2_730c_ACeP_730`),
  `WaveshareMonoDriver` (`GxEPD2_750_GDEY075T7`, monochrome), and
  `InkyImpressionDriver` (hand-written AC073TC1A raw-SPI port, since GxEPD2
  has no driver for it) - plus `esp_sleep_enable_timer_wakeup` deep sleep in
  `main.cpp`. All three build environments compile, but **the panel
  decision in Phase 0b still hasn't been made** - which of the three to
  actually build/wire is now a real choice with three working code paths
  behind it, not a choice blocked on writing code. `InkyImpressionDriver` in
  particular is unverified in a different way than "not wired up yet" - it
  was ported from a Python reference with no hardware to confirm the port
  against at all (see docs/hardware.md's BUSY-polarity risk callout, the
  single biggest specific unknown of the three). The *processor* board is
  confirmed, though: an ARCELI ESP32-S3-WROOM-1-N16R8 ESP32-S3-DevKitC-1
  clone (amazon.nl/dp/B0FQBYLL6S) matches exactly what `platformio.ini`
  already targets, and the EPD pins in `include/config.h`'s `pins`
  namespace (shared across all three panels) were cross-checked against
  Espressif's own DevKitC-1 pinout (accounting for the extra GPIO 33-37
  reservation octal-PSRAM N16R8 boards have that quad-PSRAM ones don't) -
  no conflicts, but still unconfirmed by actual continuity/wiring since
  there's no board here. Full bill of materials and wiring table:
  **[docs/hardware.md](docs/hardware.md)**.
- **Phase 5 (fidelity iteration):** not started, by design - v1 keeps the
  simplified widgets (decision #3).

### Notable implementation decisions not already covered above

- **PSRAM**: confirmed at *build* time only - `board_build.arduino.memory_type
  = qio_opi` correctly selects the octal-PSRAM SDK variant (checked by
  inspecting the actual compiler include paths PlatformIO resolved, since the
  board's own descriptive label misleadingly says "No PSRAM" - that label is
  just the base board JSON's default and is overridden by the memory_type
  setting). Whether PSRAM is actually detected and usable at *runtime*
  depends on the physical board being genuine N16R8 and correctly wired/
  strapped - unverifiable without hardware.
- **CA cert bundle**: `WiFiClientSecure::setCACertBundle()` is pointed at
  `x509_crt_bundle`, the default Mozilla bundle already compiled into
  arduino-esp32's prebuilt `libmbedtls.a`. This symbol isn't documented for
  direct use anywhere (arduino-esp32's own `WiFiClientSecure` README assumes
  you generate and embed your own bundle) - it was found by running `nm` on
  the actual downloaded toolchain and confirmed to link successfully, but
  its presence/name could change in a future arduino-esp32 core bump.
- **Fonts**: uses `U8g2_for_Adafruit_GFX`'s bundled bitmap fonts
  (Helvetica-style, fixed sizes 8/10/12/14/18/24px, plus a `logisoso`
  numeric font for the big temperature readout) instead of baking
  InkyPiZero's Jost typeface. Baking Jost would need Adafruit's
  `fontconvert` (a C tool requiring freetype, not available in this
  environment) and, more importantly, no way to visually check the result -
  U8g2's fonts are pre-tested and this was already the architecture's own
  font-fallback library choice, just applied to all text instead of only
  non-Latin fallback. Exact Jost pixel-matching is left for Phase 5.
- **Data-point gauges**: wind/pressure/UV/AQI render as plain label+value
  text in v1 (`src/widgets/data_points.cpp`) rather than InkyPiZero's
  hand-drawn compass/barometer/UV-sunburst/AQI-arc widgets - this is
  decision #3 (simplified v1) applied literally, not a new cut. Humidity
  drops and the visibility icon are kept, since those were already
  pre-rendered image assets in the original, not hand-drawn gauge geometry.
- **Icon baking**: `tools/convert_assets.py` pre-quantizes every icon to the
  7-color ACeP palette and bakes a 1-bit opacity mask at each fixed
  on-screen size the firmware needs (no runtime resize, no runtime alpha
  blending), then `assets::drawIcon()` does a plain masked blit. Regenerates
  automatically as a PlatformIO pre-build step if Pillow is available;
  otherwise the committed generated files in `src/generated/` are used as-is.
- **Chart vertical axis labels**: the temperature-unit and "Regen" labels
  rotate via u8g2's `setFontDirection(3)`, approximating InkyPiZero's
  PIL-image-rotation approach. The rotation direction and exact placement
  are unverified - check these first against a real render.
- **Timezone handling**: Open-Meteo's `timezone=auto` already returns local
  wall-clock timestamps (no UTC offset), so this port skips a
  timezone-conversion layer entirely instead of porting `pytz` - see
  `src/weather/open_meteo.h`'s docstring.
- **Palette accuracy**: `palette::kRgb` (the RGB888 values dithering
  decisions are made against) uses Pimoroni's own *measured* ACeP panel
  colors (`inky_ac073tc1a.SATURATED_PALETTE`) rather than idealized
  primaries - resolves a TODO left in `palette.h` from before that data was
  available. Deliberately does **not** change `kRgb565` (the values actually
  sent to GxEPD2-backed panels), which stay pure primaries since that's a
  wire-protocol constant, not a color approximation - see `palette.h`'s
  namespace comment for the full reasoning.
- **Three panels, one codebase**: adding two more panels (`WaveshareMonoDriver`,
  `InkyImpressionDriver`) needed surprisingly little new code, because
  `canvas.cpp`/widgets only ever draw into the RGB888 working canvas -
  panel-specific work is entirely isolated to *quantizing* that canvas
  (`dither::floydSteinberg` vs. the new `floydSteinbergMono`) and *pushing*
  the result (`DisplaySink` subclasses). Baked icons, fonts, and layout are
  100% shared across all three; the monochrome panel just looks flatter
  since its dither pass collapses color to a luminance threshold.

## Building

See **[docs/installation.md](docs/installation.md)** for the full walkthrough
(flashing, first-boot WiFi setup, checking it's working, troubleshooting).
Short version: `./tools/run.ps1` (Windows) or `./tools/run.sh` (macOS/Linux)
builds, flashes, and opens a serial monitor - installing PlatformIO first if
it isn't already. Add `-Dev`/`--dev` to skip hardware entirely and run a
local web server mirroring the setup portal's website at
`http://localhost:8080`, for iterating on the settings page's UI
(`tools/dev_server.py` - see its docstring for exactly what is and isn't a
faithful stand-in for the real firmware).

Equivalent plain PlatformIO commands, if you'd rather not use the scripts:

```
pio run                                          # headless build (SerialDumpDriver, no panel needed)
pio run -e esp32-s3-devkitc-1-panel-acep730       # Waveshare 7.3" ACeP (7-color)
pio run -e esp32-s3-devkitc-1-panel-mono-v2       # Waveshare 7.5" e-Paper V2 (monochrome)
pio run -e esp32-s3-devkitc-1-panel-inky          # Pimoroni Inky Impression 7.3" (7-color, raw SPI)
pio run -t upload                                 # flash whichever environment (add -e for a panel one)
pio device monitor                                # serial log
```

See [docs/hardware.md](docs/hardware.md) for wiring and how the three panel
options compare (color vs. refresh speed vs. how hardware-verified each
driver is). Setup: there's no compile-time secrets file anymore - flash any
environment with no configuration at all, and since a factory-fresh device
has no saved WiFi credentials, it boots straight into the **web setup
portal** described below to collect them (and anything else in
`DisplayConfig`). Requires [PlatformIO Core](https://platformio.org/)
(`pip install platformio`); the first build downloads the ESP32-S3
toolchain and all libraries automatically.

To validate a headless render (Phase 2's actual exit criterion) without a
panel: flash the default environment, run one cycle (see below - either let
it fail past WiFi setup, or use the portal's live image preview instead),
then reassemble what it dumps over serial into a PNG with
`python tools/dump_to_png.py --port <COM_PORT>` (saved into
**[Mock-Display/](Mock-Display/)**, timestamped and never overwritten - see
that folder's README) and compare against InkyPiZero's `mock_display_output/`
renders.

Icon assets are vendored from InkyPiZero - see `docs/attribution.md` for
licensing.

## Web setup/debug portal

Added on top of the original plan (not part of the InkyPiZero port): since
this device deep-sleeps ~10 minutes between ~30-45s awake windows, an
always-reachable web server would contradict the whole point of the
battery-powered design (see README intro / Key risk #1). Instead, a
SoftAP + web server only comes up in a deliberate **setup/debug mode**,
triggered on boot by either:

- no WiFi credentials saved yet (a factory-fresh device, or after a
  settings reset), or
- the ESP32-S3-DevKitC-1's onboard **BOOT button held down** at power-on/reset
  (`pins::kBootButton` in `include/config.h`, GPIO0 - no extra wiring needed).

In this mode the device does **not** deep-sleep or run a weather cycle - it
starts a password-protected access point (`WeatherEPS32S3-Setup`, see
`kApPassword` in `src/setup_portal.cpp`) with a captive-portal DNS redirect,
and serves:

- **`/`** - the settings form (WiFi credentials, latitude/longitude, units,
  timezone, time format, forecast days, background/text color, chart icon
  step, show-moon-phase, refresh interval), pre-filled with current values,
  plus an embedded preview of the last rendered frame, a link to the debug
  log, and a read-only **Display panel** field showing which of the three
  panel drivers (see [docs/hardware.md](docs/hardware.md)) is actually
  compiled into the currently-flashed firmware - panel choice itself still
  isn't a form field here (see above for why), but which one you're
  currently running is visible without checking the build command. Saving
  persists everything else to NVS (`src/settings.h`) and reboots into
  normal operation.
- **`/log`** - the step-by-step debug log (`src/debug_log.h`): every wake
  cycle logs boot, WiFi connect, weather fetch, location resolve, canvas
  render, panel push, and sleep, each with OK/FAIL and a short detail
  string. Persisted to LittleFS (bounded to 32KB, oldest entries dropped by
  clearing and starting fresh rather than trimmed line-by-line) so it
  survives deep sleep and reads back across many cycles, not just the last
  one. There's no RTC/NTP clock on this device (see the timezone note
  above), so entries are keyed by an RTC-memory cycle counter instead of a
  timestamp.
- **`/image`** - the last dithered frame, reconstructed as a plain 24-bit
  BMP from the packed buffer `FrameStore` saves to LittleFS after every
  render (`src/display/frame_store.h`) - populated regardless of which
  DisplaySink is actually driving hardware, so the preview works the same
  whether or not a panel is connected.

The portal reboots itself back into normal battery-optimized operation
after a 10-minute idle timeout if left unattended, so it can't accidentally
be left running and draining the battery.

**Unverified** (no hardware here to confirm): the BOOT-button read timing,
SoftAP/captive-portal behavior on real client devices, and whether
`default_16MB.csv`'s LittleFS partition actually formats and mounts cleanly
on first boot of a genuine board.

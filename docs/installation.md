# Installation

This is a hardware bring-up project, not an app with a package manager -
"installing" it means flashing firmware onto an ESP32-S3 board and then
configuring it over WiFi. There is no companion server/Pi involved (see
README's Context section for why).

## Prerequisites

- An **ESP32-S3-DevKitC-1 N16R8** board (16MB flash / 8MB octal PSRAM - see
  README's "Recommended architecture"; other S3 variants without PSRAM will
  not work) and a USB cable. See **[hardware.md](hardware.md)** for the
  confirmed board, bill of materials, and panel wiring table.
- **Python 3.8+** on the machine you're flashing from.
- [PlatformIO Core](https://platformio.org/) - `tools/run.ps1`/`tools/run.sh`
  (below) install it automatically via pip if it's missing, or install it
  yourself with `pip install platformio`.
- Nothing else - no compile-time secrets file, no manual credential editing.
  WiFi and every other setting are configured over the device's own web UI
  after flashing (see [Web setup/debug portal](../README.md#web-setupdebug-portal)
  in the README).

## Flash the firmware

From the repo root:

```powershell
# Windows (PowerShell)
./tools/run.ps1
```

```bash
# macOS/Linux
./tools/run.sh
```

This builds the default (`esp32-s3-devkitc-1`) environment, uploads it,
and opens a serial monitor. It targets the **headless** build - drives the
`SerialDumpDriver` debug driver instead of real panel hardware (see
README Phases 0a-2) - which is what you want until the Phase 0b panel
decision is made and wired up. Once it is, flash the panel build instead:

```powershell
./tools/run.ps1 -Env panel
```
```bash
./tools/run.sh --env panel
```

If autodetection picks the wrong serial port, pin it explicitly:
`./tools/run.ps1 -UploadPort COM5` / `./tools/run.sh --upload-port /dev/ttyUSB0`.

Prefer plain PlatformIO commands instead of the scripts? See the README's
[Building](../README.md#building) section.

## First boot: WiFi and settings

A freshly-flashed board has no WiFi credentials saved, so it boots straight
into **setup mode**: a password-protected access point rather than trying
(and failing) to connect to a network.

1. On your phone/laptop, connect to WiFi network **`WeatherEPS32S3-Setup`**
   (password `weather123` - see `kApPassword` in `src/setup_portal.cpp` if
   you want to change it before flashing).
2. Browse to **http://192.168.4.1** (most devices auto-open this via the
   captive-portal redirect on associating with the AP; if not, open it
   manually).
3. Fill in your WiFi SSID/password and location (latitude/longitude), adjust
   units/display settings if you like, and hit **Save**.
4. The device reboots and starts its normal cycle: connect to your WiFi,
   fetch weather, render, sleep for the configured interval (10 minutes by
   default), repeat.

To go back into setup mode later (change WiFi, change location, etc.), hold
the board's onboard **BOOT button** while powering on or resetting it.

## Checking it's working

- **Serial monitor** (`pio device monitor`, or left open by `tools/run.*`)
  prints each step as it happens.
- **The web portal's `/log` route** (reachable in setup mode, see above)
  shows the step-by-step debug log for every cycle - boot, WiFi connect,
  weather fetch, location resolve, render, panel push, sleep - each marked
  OK/FAIL, persisted across deep-sleep cycles.
- **The web portal's `/image` route** (also embedded directly on the setup
  page) shows the last rendered frame, regardless of whether a real panel
  is connected - useful for confirming the render pipeline works before any
  panel decision is made.
- With no panel connected (headless build), reassemble what's dumped over
  serial into a PNG on your machine: `python tools/dump_to_png.py --port
  COM5`. Saved into **[Mock-Display/](../Mock-Display/)**, timestamped -
  every capture you take stays there, nothing gets overwritten.

## Developing the web UI without hardware

`tools/run.ps1 -Dev` / `tools/run.sh --dev` runs a local Python server at
`http://localhost:8080` that mirrors the setup portal's pages and routes,
backed by local files instead of a real device's NVS/LittleFS - useful for
iterating on the settings page's look without flashing anything. It is a
UI mirror only, not a firmware simulator - see `tools/dev_server.py`'s
docstring for exactly what is and isn't reproduced. Preview images it
generates also land in `Mock-Display/`; run it with `--snapshot` to
generate just one and exit, without starting the server.

## Troubleshooting

- **"PSRAM allocation failed" in the serial log**: wrong board variant -
  this firmware requires N16R8 (octal PSRAM). See README risk #5.
- **Stuck trying to connect to WiFi**: double-check the SSID/password saved
  via the setup portal; 2.4GHz networks only (ESP32-S3 doesn't do 5GHz).
- **Setup portal doesn't appear**: hold BOOT at power-on to force it, or
  check the serial log for why WiFi credentials were considered "already
  saved" if that's not expected.
- **No panel yet**: expected for now - see README's Implementation status
  and Phase 0b. Use the headless build and the `/image` web preview or
  `tools/dump_to_png.py` to verify rendering in the meantime.

#!/usr/bin/env bash
# Build, flash, and monitor the Weather-EPS32S3 firmware - or, with --dev,
# skip hardware entirely and run a local web server that mirrors the
# setup/debug portal's website for UI iteration. See docs/installation.md.
#
# Usage:
#   ./tools/run.sh                        # build + upload + monitor (headless env)
#   ./tools/run.sh --env panel-acep730    # Waveshare 7.3" ACeP (7-color)
#   ./tools/run.sh --env panel-mono-v2    # Waveshare 7.5" e-Paper V2 (monochrome)
#   ./tools/run.sh --env panel-inky       # Pimoroni Inky Impression 7.3" (7-color, raw SPI)
#   ./tools/run.sh --upload-port /dev/ttyUSB0
#   ./tools/run.sh --dev                  # local website only, no hardware, http://localhost:8080
#   ./tools/run.sh --dev --port 8081
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEV=false
ENV_NAME="headless"
PORT=8080
UPLOAD_PORT=""

usage() {
    grep '^#' "${BASH_SOURCE[0]}" | grep -v '^#!/' | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dev) DEV=true; shift ;;
        --env) ENV_NAME="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --upload-port) UPLOAD_PORT="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
    esac
done

if $DEV; then
    echo "Starting local dev web server (no hardware needed)..."
    exec python3 "$REPO_ROOT/tools/dev_server.py" --port "$PORT"
fi

case "$ENV_NAME" in
    headless) PIO_ENV="esp32-s3-devkitc-1" ;;
    panel-acep730|panel-mono-v2|panel-inky) PIO_ENV="esp32-s3-devkitc-1-$ENV_NAME" ;;
    *) echo "Unknown --env '$ENV_NAME' (expected: headless, panel-acep730, panel-mono-v2, panel-inky)" >&2; exit 1 ;;
esac

if ! python3 -m platformio --version >/dev/null 2>&1; then
    echo "PlatformIO not found - installing via pip..."
    python3 -m pip install --user platformio
fi

echo "Building environment '$PIO_ENV'..."
python3 -m platformio run -e "$PIO_ENV"

echo "Uploading to board..."
upload_args=(-e "$PIO_ENV" -t upload)
if [[ -n "$UPLOAD_PORT" ]]; then
    upload_args+=(--upload-port "$UPLOAD_PORT")
fi
python3 -m platformio run "${upload_args[@]}"

echo
echo 'Flashed. If this is a factory-fresh board (no WiFi saved yet) or you are'
echo 'holding the BOOT button, it will come up as an access point:'
echo '  connect to WiFi "WeatherEPS32S3-Setup", then browse to http://192.168.4.1'
echo
echo "Opening serial monitor (Ctrl+C to exit)..."
monitor_args=(device monitor -e "$PIO_ENV")
if [[ -n "$UPLOAD_PORT" ]]; then
    monitor_args+=(--port "$UPLOAD_PORT")
fi
python3 -m platformio "${monitor_args[@]}"

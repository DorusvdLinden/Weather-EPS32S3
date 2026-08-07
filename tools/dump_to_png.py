#!/usr/bin/env python3
"""Reassembles a SerialDumpDriver frame dump into a viewable PNG - the
host-side half of the serial-framebuffer-dump debug driver (see
src/display/serial_dump_driver.h and the README module mapping's
display/mock_driver.py row). Lets Phase 2 (on-device rendering) be
validated headless, by comparing the result against InkyPiZero's
mock_display_output/ renders.

Usage:
    python tools/dump_to_png.py --port COM5                # saves into Mock-Display/, timestamped
    python tools/dump_to_png.py --file capture.bin --out frame.png
"""
import argparse
import os
import struct
import sys
import time

try:
    from PIL import Image
except ImportError:
    print("dump_to_png.py requires Pillow: pip install pillow", file=sys.stderr)
    sys.exit(1)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MOCK_DISPLAY_DIR = os.path.join(os.path.dirname(SCRIPT_DIR), "Mock-Display")

# Index-aligned with palette::kRgb in src/palette.h - Pimoroni's measured
# panel colors (inky_ac073tc1a.SATURATED_PALETTE), not idealized primaries.
PALETTE = [
    (0, 0, 0), (217, 242, 255), (3, 124, 76), (27, 46, 198),
    (245, 80, 34), (255, 255, 68), (239, 121, 44),
]
MAGIC = b"WXPD"


def read_frame(stream):
    header = stream.read(8)
    if len(header) < 8 or header[:4] != MAGIC:
        raise ValueError(f"Bad magic: {header[:4]!r} (expected {MAGIC!r})")
    w, h = struct.unpack("<HH", header[4:8])
    expected = w * h // 2
    data = stream.read(expected)
    if len(data) < expected:
        raise ValueError(f"Short read: got {len(data)} bytes, expected {expected}")
    return w, h, data


def to_image(w, h, data):
    img = Image.new("RGB", (w, h))
    pixels = img.load()
    for i in range(w * h):
        byte = data[i // 2]
        idx = (byte >> 4) if i % 2 == 0 else (byte & 0x0F)
        pixels[i % w, i // w] = PALETTE[idx % len(PALETTE)]
    return img


def default_out_path():
    """A fresh, never-before-used path under Mock-Display/ - see that
    folder's README.md: nothing there is ever overwritten or deleted."""
    os.makedirs(MOCK_DISPLAY_DIR, exist_ok=True)
    base = os.path.join(MOCK_DISPLAY_DIR, f"capture_{time.strftime('%Y%m%d_%H%M%S')}")
    path = base + ".png"
    n = 1
    while os.path.exists(path):
        path = f"{base}_{n}.png"
        n += 1
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="Serial port to read the dump from (e.g. COM5) - requires pyserial")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--file", help="Read a previously captured raw dump from this file instead of serial")
    parser.add_argument("--out", help="Output PNG path (default: a timestamped file under Mock-Display/)")
    args = parser.parse_args()

    if args.file:
        with open(args.file, "rb") as f:
            w, h, data = read_frame(f)
    elif args.port:
        import serial  # pip install pyserial
        with serial.Serial(args.port, args.baud, timeout=30) as ser:
            w, h, data = read_frame(ser)
    else:
        parser.error("must specify --port or --file")
        return

    out = args.out or default_out_path()
    to_image(w, h, data).save(out)
    print(f"Wrote {out} ({w}x{h})")


if __name__ == "__main__":
    main()

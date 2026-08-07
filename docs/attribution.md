# Attribution

Icon assets under `assets/icons/` are vendored unmodified from
[InkyPiZero](https://github.com/DorusvdLinden/InkyPiZero)'s own
`assets/icons/` (see that project's `docs/attribution.md` for the full
per-icon sourcing). Summary:

## Icons

Weather condition icons, moon phases, and sunrise/sunset icons are recolored
PNGs originally rendered from
[erikflowers/weather-icons](https://github.com/erikflowers/weather-icons) SVG
source. License: [SIL OFL 1.1](https://github.com/erikflowers/weather-icons/blob/master/SIL%20OFL.txt).
Covers: `01d`, `01n`, `022d`, `022n`, `02d`, `02n`, `04d`, `50d`, `48d`, `51d`,
`53d`, `09d`, `56d`, `57d`, `71d`, `73d`, `13d`, `77d`, `11d`, `newmoon`,
`waxingcrescent`, `firstquarter`, `waxinggibbous`, `fullmoon`, `waninggibbous`,
`lastquarter`, `waningcrescent`, `sunrise`, `sunset`.

`visibility.png`: [Observe icons created by meaicon - Flaticon](https://www.flaticon.com/free-icons/observe).

`humidity_drop_filled.png` / `humidity_drop_empty.png`: cropped from a
screenshot of InkyPiZero's own earlier Chromium/CSS-rendered version, no
external source.

These source PNGs are not used directly by the firmware - `tools/convert_assets.py`
bakes them into `src/generated/icons_data.{h,cpp}` at build time (pre-quantized
to the 7-color ACeP palette, at each fixed on-screen size the firmware needs).
The generated files carry the same license as their source icons.

## Fonts

Unlike InkyPiZero (which bakes the Jost typeface, SIL OFL 1.1, via Pillow),
this firmware uses the bitmap fonts bundled with
[U8g2_for_Adafruit_GFX](https://github.com/olikraus/U8g2_for_Adafruit_GFX)
(itself derived from [u8g2](https://github.com/olikraus/u8g2), 2-clause BSD)
instead - see README's "Font fallback" section and
`src/assets/asset_store.h` for why. No Jost assets are vendored in this repo.

## Pimoroni `inky` (AC073TC1A driver + palette)

`src/display/inky_impression_driver.cpp` is a line-by-line C++ port of
[Pimoroni's `inky` library](https://github.com/pimoroni/inky), specifically
`inky/inky_ac073tc1a.py` - the SPI command bytes, init sequence values, and
timing constants there are copied from that source (see the file's own top
comment for exactly what was and wasn't verified in porting it). License:
[MIT](https://github.com/pimoroni/inky/blob/main/LICENSE), Copyright (c)
2018 Pimoroni Ltd.

`src/palette.h`'s `kRgb` values (the RGB888 palette used for dithering
decisions) are Pimoroni's own measured panel colors
(`Inky.SATURATED_PALETTE` in the same file), not idealized primaries -
copied under the same MIT license.

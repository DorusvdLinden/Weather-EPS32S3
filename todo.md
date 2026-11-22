# TODO

- **Weather-EPS32S3**: battery-capable weather display firmware for an ESP32-S3 with a 7-colour e-paper panel, a port of InkyPiZero.
- Status (2026-11-22): WiFi, live Open-Meteo data and full rendering confirmed on real hardware (2026-08-07); real panel and deep sleep not yet run on hardware.
- Open: phases 3-4 on the real panel.
- Next: run on the real panel, then measure deep-sleep battery life.
- Details: README.md (implementation status).

## Ideas to do
## features
1 - [x] create an install manual / script 
2 - [x] add a --dev option to run the code on a local machine if possible, including hosting of the website

## processor
3 - [x] check software & propose a pin config for specific hardware: https://www.amazon.nl/dp/B0FQBYLL6S?ref=ppx_yo2ov_dt_b_fed_asin_title
4 - [x] connect remotely to a esp32S3

## display hardware
5 - [x] add support for the the waveshare mono color 800x480 7 inch V2, includign a setting in the gui and the documentation incudign wiring & pinout.
6 - [x] add support for the the Inky Impression by Pimoroni 7.3 inch includign a setting in the gui and the documentation incudign wiring & pinout.

## test
7 - [x] test local run and safe the generated image
8 - [x] test connect & deploy method to hardware
8 - [x] NA

## optical
9 - [x] move degC and regen axis labels up a bit
10 - [x] fix the missing icons for the details: UV, quality pressure & wind direction
11 - [ ] pick more smart / solid colors to support the dithering display
12 - [ ] 
13 - [ ] 
14 - [ ] 
15 - [ ] 
16 - [ ] Connect the display & run the application fully on ESP32S3


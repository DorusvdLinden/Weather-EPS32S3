#pragma once

#include <cstdint>

#include "graphics/rgb_canvas.h"
#include "weather/open_meteo.h"

// v1 simplification of InkyPiZero's widgets/gauge.py + the humidity/
// visibility parts of canvas.py's _draw_data_points/_draw_data_point_icon:
// wind/pressure/uv/aqi become plain label+value text (README decision #3 -
// "numeric readouts... instead of the hand-drawn compass/barometer/sunburst/
// AQI arcs"); humidity keeps its drop icons and visibility keeps its icon,
// since those were already pre-rendered assets, not hand-drawn gauge
// geometry, so there's no fidelity cut in keeping them. Phase 5 replaces
// this whole module with a port of widgets/gauge.py.
namespace widgets {

void drawDataPoints(RgbCanvas& canvas, const weather::WeatherSnapshot& data, uint16_t textColor565);

}  // namespace widgets

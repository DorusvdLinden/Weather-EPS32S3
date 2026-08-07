#pragma once

#include <cstdint>

#include "graphics/rgb_canvas.h"
#include "layout.h"
#include "weather/open_meteo.h"

// Ports InkyPiZero's widgets/forecast.py.
namespace widgets {

void drawForecastCard(RgbCanvas& canvas, const layout::Region& region, const weather::DayForecast& day,
                       uint16_t textColor565, bool showMoon);

}  // namespace widgets

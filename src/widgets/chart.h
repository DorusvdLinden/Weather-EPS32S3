#pragma once

#include <cstdint>

#include "graphics/rgb_canvas.h"
#include "layout.h"
#include "weather/open_meteo.h"

// Ports InkyPiZero's widgets/chart.py - the hourly temperature/rain chart.
namespace widgets {

void drawChart(RgbCanvas& canvas, const layout::Region& region, const weather::WeatherSnapshot& data,
                uint16_t textColor565, int graphIconStep, const char* tempUnitLabel, const char* rainUnitLabel);

}  // namespace widgets

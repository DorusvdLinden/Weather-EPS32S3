#pragma once

#include "config.h"
#include "graphics/rgb_canvas.h"
#include "weather/open_meteo.h"

// Ports InkyPiZero's canvas.py WeatherCanvas - the render orchestrator.
namespace canvas {

void render(RgbCanvas& target, const DisplayConfig& config, const weather::WeatherSnapshot& data);

}  // namespace canvas

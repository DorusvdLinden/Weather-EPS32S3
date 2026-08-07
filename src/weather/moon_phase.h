#pragma once

#include "generated/icons_data.h"

// Hand-ported from astral's `astral.moon.phase()` (MIT license, see
// https://github.com/sffjunkie/astral) - README risk #3: no C++ library
// exists to reuse here, so this is a from-scratch port of astral's
// low-precision lunar elongation formula, kept numerically identical
// (including its day-count convention of ~0..28 rather than the true
// 29.53-day synodic month - InkyPiZero's weather_data.py mixes that
// convention with the true synodic length for illumination %, and this port
// preserves that exactly rather than "fixing" behavior InkyPiZero relies on).
namespace moon {

// Phase "age" in astral's own ~0..28 unit convention, for the UTC calendar
// date (y, m, d). Matches `astral.moon.phase(date)`.
double phaseAge(int year, int month, int day);

// Buckets phaseAge() into one of the 8 named phases - matches
// weather_data.py's get_moon_phase_name() thresholds exactly.
MoonIcon phaseNameFromAge(double phaseAge);

// Mirrors waxing/waning and quarter phases for southern-hemisphere
// observers - matches weather_data.py's get_moon_phase_icon_key().
MoonIcon mirrorForLatitude(MoonIcon phase, double latitude);

// Illumination percentage (0-100), using the *true* synodic month length as
// the divisor - matches weather_data.py's inline calculation in
// _parse_forecast() (phase_age / 29.530588853).
double illuminationPercent(double phaseAge);

}  // namespace moon

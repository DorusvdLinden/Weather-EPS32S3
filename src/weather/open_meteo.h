#pragma once

#include <Arduino.h>

#include "config.h"
#include "generated/icons_data.h"

// Ports InkyPiZero's weather_data.py (Open-Meteo fetch + parse + the small
// pure derivation helpers it kept inline) - see README module mapping.
//
// One deliberate simplification vs. the Python version: with Open-Meteo's
// `timezone=auto`, every timestamp it returns (current/hourly/daily,
// sunrise/sunset) is already local wall-clock time for the target lat/long -
// no UTC offset suffix. The Python code still routes these through
// pytz/astimezone (largely a no-op given that). This port skips a
// timezone-conversion layer entirely and treats the returned strings as
// local time directly, and anchors "now" on the response's own
// `current.time` field rather than the device's RTC - this device never
// runs NTP, so the API's own current time is the only reliable "now" it has.
namespace weather {

enum class DataPointKind : uint8_t { Wind, Humidity, Pressure, Uv, Visibility, Aqi };

// v1 simplification (README decision #3): only the fields the simplified
// numeric/bar readouts need. Phase 5 re-adds gauge-rotation/beam-geometry
// fields here when widgets/gauge.py's hand-drawn compass/needle math is
// ported.
struct DataPoint {
    DataPointKind kind;
    String label;
    String valueText;  // fully formatted "<measurement> <unit>", e.g. "12 m/s"
    String direction;  // wind only, else empty
    int dropCount = 0;  // humidity only
};

struct HourPoint {
    String timeLabel;
    int temperature = 0;
    float rain = 0;
    WeatherIcon icon = WeatherIcon::W01D;
};

struct SunEvent {
    float position = 0;  // fractional hour index into the hourly series
    SunIcon icon = SunIcon::Sunrise;
};

struct DayForecast {
    String dayLabel;
    WeatherIcon icon = WeatherIcon::W01D;
    int high = 0;
    int low = 0;
    String moonPhasePct;
    MoonIcon moonIcon = MoonIcon::NewMoon;
};

struct WeatherSnapshot {
    static constexpr int kMaxHourly = 24;
    static constexpr int kMaxSunEvents = 8;
    static constexpr int kMaxDaily = 8;
    static constexpr int kDataPointCount = 6;

    String currentDate;
    String location;
    WeatherIcon currentIcon = WeatherIcon::W01D;
    int currentTemp = 0;
    int feelsLike = 0;
    String tempUnit;
    int forecastHigh = 0;
    int forecastLow = 0;

    DataPoint dataPoints[kDataPointCount] = {
        {DataPointKind::Wind, "", "", "", 0},     {DataPointKind::Humidity, "", "", "", 0},
        {DataPointKind::Pressure, "", "", "", 0},  {DataPointKind::Uv, "", "", "", 0},
        {DataPointKind::Visibility, "", "", "", 0}, {DataPointKind::Aqi, "", "", "", 0},
    };

    HourPoint hourly[kMaxHourly];
    int hourlyCount = 0;

    SunEvent sunEvents[kMaxSunEvents];
    int sunEventCount = 0;

    DayForecast daily[kMaxDaily];
    int dailyCount = 0;

    String lastRefreshTime;
};

// Fetches Open-Meteo forecast + air-quality data and fills `out`. Does NOT
// resolve the location name (see fetchLocationName below) - main.cpp decides
// whether to hit Nominatim based on its NVS cache, so that policy doesn't
// belong in this module. Returns false on any network/parse failure; `out`
// may be partially written in that case and should not be rendered.
bool fetchSnapshot(const DisplayConfig& config, WeatherSnapshot& out);

// Reverse-geocodes (lat, long) via Nominatim into "City, Country". Returns
// an empty string on failure (matches weather_data.py's
// get_nearest_location_name(), which treats "unknown" as non-fatal).
String fetchLocationName(double lat, double lon);

}  // namespace weather

#include "open_meteo.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include <cmath>
#include <cstdio>

#include "weather/moon_phase.h"

// Default Mozilla CA bundle baked into arduino-esp32's prebuilt libmbedtls.a
// (confirmed present via `nm` on the actual toolchain/framework this project
// resolves, since arduino-esp32 does not document this symbol for direct use
// - see WiFiClientSecure's own README, which otherwise assumes you generate
// and embed your own bundle). This avoids the cert-expiry maintenance trap
// of pinning individual certs (see README's HTTPS library note) without
// needing a separate gen_crt_bundle.py + embed_files build step. If a future
// arduino-esp32 core version renames or drops this symbol, the link will
// fail loudly here rather than silently falling back to an insecure
// connection.
extern "C" const uint8_t x509_crt_bundle[];

namespace weather {

namespace {

// ---- PSRAM allocator for the forecast/AQI JsonDocuments (README risk #5:
// plain malloc for something this size is not guaranteed to land in PSRAM). ----
class PsramAllocator : public ArduinoJson::Allocator {
   public:
    void* allocate(size_t size) override { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
    void deallocate(void* ptr) override { heap_caps_free(ptr); }
    void* reallocate(void* ptr, size_t new_size) override {
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
    }
};
PsramAllocator g_psramAllocator;

// ---- Small local-time helpers (see open_meteo.h docstring: Open-Meteo's
// timezone=auto already returns local wall-clock strings, so no tz library
// is needed on-device). ----
struct LocalTime {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
};

// Confirmed on real hardware: Open-Meteo's `daily` block returns
// *date-only* strings ("2026-08-07", no "T15:30" time component - daily
// granularity has nothing to put there), unlike current/hourly's full
// datetime strings. The full-format-only version of this function silently
// failed to parse every single daily.time entry, which zeroed out
// dailyCount without fetchSnapshot() ever seeing an error (the loop over
// daily entries just `continue`d every iteration) - the forecast row
// rendered as completely empty. Falls back to date-only (hour=minute=0)
// when the full format doesn't match.
bool parseIsoLocal(const char* s, LocalTime& out) {
    if (!s) return false;
    if (sscanf(s, "%d-%d-%dT%d:%d", &out.year, &out.month, &out.day, &out.hour, &out.minute) == 5) return true;
    out.hour = 0;
    out.minute = 0;
    return sscanf(s, "%d-%d-%d", &out.year, &out.month, &out.day) == 3;
}

bool sameDate(const LocalTime& a, const LocalTime& b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

bool afterDate(const LocalTime& a, const LocalTime& b) {  // a's date > b's date
    if (a.year != b.year) return a.year > b.year;
    if (a.month != b.month) return a.month > b.month;
    return a.day > b.day;
}

// Julian-day-based minute counter, used only for relative differences (sun
// event position within the hourly chart) - never treated as a real epoch.
// Same formula as weather::moon's julianDay(); duplicated rather than shared
// since it's 4 lines and pulling in moon_phase.h here would be a stranger
// coupling than the duplication.
double minutesSinceEpoch(const LocalTime& t) {
    int year = t.year, month = t.month;
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    int a = year / 100;
    int b = 2 - a + a / 4;
    double jd = std::floor(365.25 * (year + 4716)) + std::floor(30.6001 * (month + 1)) + t.day + b - 1524.5;
    return jd * 1440.0 + t.hour * 60 + t.minute;
}

const char* kMonths[] = {"januari",  "februari", "maart",     "april",   "mei",      "juni",
                          "juli",     "augustus", "september", "oktober", "november", "december"};
const char* kWeekdays[] = {"maandag", "dinsdag", "woensdag", "donderdag", "vrijdag", "zaterdag", "zondag"};
const char* kWeekdaysAbbr[] = {"ma", "di", "wo", "do", "vr", "za", "zo"};

// Zeller-ish day-of-week via the same Julian Day used above: JD 0 (proleptic)
// falls on a Monday, so (floor(jd + 0.5)) mod 7 gives 0=Monday.
int weekdayIndex(const LocalTime& t) {
    int year = t.year, month = t.month;
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    int a = year / 100;
    int b = 2 - a + a / 4;
    double jd = std::floor(365.25 * (year + 4716)) + std::floor(30.6001 * (month + 1)) + t.day + b - 1524.5;
    long day = static_cast<long>(std::floor(jd + 0.5));
    int idx = static_cast<int>(((day % 7) + 7) % 7);
    return idx;  // JD 0 = Monday = index 0, matches Python's dt.weekday()
}

String formatDateNl(const LocalTime& t) {
    String s = String(kWeekdays[weekdayIndex(t)]) + " " + String(t.day) + " " + kMonths[t.month - 1];
    s.setCharAt(0, toupper(s.charAt(0)));
    return s;
}

String formatDayAbbrNl(const LocalTime& t) { return kWeekdaysAbbr[weekdayIndex(t)]; }

String formatHourOnly(const LocalTime& t, const String& timeFormat) {
    char buf[8];
    if (timeFormat == "24h") {
        snprintf(buf, sizeof(buf), "%02d:00", t.hour);
        return String(buf);
    }
    int h12 = t.hour % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, sizeof(buf), "%d %s", h12, t.hour < 12 ? "AM" : "PM");
    return String(buf);
}

// ---- Weather-code / unit / locale helpers (weather_data.py's pure
// functions - see README module mapping). ----

WeatherIcon mapWeatherCodeToIcon(int code, bool isDay) {
    WeatherIcon icon;
    switch (code) {
        case 1: icon = WeatherIcon::W022D; break;
        case 2: icon = WeatherIcon::W02D; break;
        case 3: icon = WeatherIcon::W04D; break;
        case 51: case 61: case 80: icon = WeatherIcon::W51D; break;
        case 53: case 63: case 81: icon = WeatherIcon::W53D; break;
        case 55: case 65: case 82: icon = WeatherIcon::W09D; break;
        case 45: icon = WeatherIcon::W50D; break;
        case 48: icon = WeatherIcon::W48D; break;
        case 56: case 66: icon = WeatherIcon::W56D; break;
        case 57: case 67: icon = WeatherIcon::W57D; break;
        case 71: case 85: icon = WeatherIcon::W71D; break;
        case 73: icon = WeatherIcon::W73D; break;
        case 75: case 86: icon = WeatherIcon::W13D; break;
        case 77: icon = WeatherIcon::W77D; break;
        case 95: case 96: case 99: icon = WeatherIcon::W11D; break;
        case 0: default: icon = WeatherIcon::W01D; break;
    }
    if (!isDay) {
        if (icon == WeatherIcon::W01D) return WeatherIcon::W01N;
        if (icon == WeatherIcon::W022D) return WeatherIcon::W022N;
        if (icon == WeatherIcon::W02D) return WeatherIcon::W02N;
    }
    return icon;
}

String windDirectionAbbrNl(double deg) {
    static const char* dirs[8] = {"N", "NO", "O", "ZO", "Z", "ZW", "W", "NW"};
    long idx = lround(deg / 45.0) % 8;
    if (idx < 0) idx += 8;
    return dirs[idx];
}

double windSpeedMs(double speed, const String& units) {
    return units == "imperial" ? speed * 0.44704 : speed;
}

String beaufortDescriptionNl(double speedMs) {
    struct Level {
        double upper;
        const char* text;
    };
    static const Level levels[] = {
        {0.3, "Windstil"},          {1.6, "Zwakke wind"},     {3.4, "Zwakke wind"},
        {5.5, "Matige wind"},       {8.0, "Matige wind"},     {10.8, "Vrij krachtige wind"},
        {13.9, "Krachtige wind"},   {17.2, "Harde wind"},     {20.8, "Stormachtig"},
        {24.5, "Storm"},            {28.5, "Zware storm"},    {32.7, "Zeer zware storm"},
    };
    for (const auto& l : levels) {
        if (speedMs < l.upper) return l.text;
    }
    return "Orkaan";
}

int humidityDropCount(double humidity) {
    if (std::isnan(humidity)) return 1;
    int count = static_cast<int>(std::ceil(humidity / 20.0));
    if (count < 1) count = 1;
    if (count > 5) count = 5;
    return count;
}

String aqiScaleNl(double aqi) {
    static const char* scale[] = {"Goed", "Redelijk", "Matig", "Slecht", "Zeer slecht", "Extreem slecht"};
    int idx = static_cast<int>(aqi / 20.0);
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
    return scale[idx];
}

struct Units {
    const char* temperature;
    const char* speed;
    const char* distance;
};

Units unitsFor(const String& units) {
    if (units == "imperial") return {"F", "mph", "mi"};
    if (units == "standard") return {"K", "m/s", "km"};
    return {"C", "m/s", "km"};
}

// ---- HTTP + JSON fetch ----

bool httpGetJson(const String& url, JsonDocument& doc, JsonDocument* filter, uint32_t timeoutMs) {
    WiFiClientSecure client;
    client.setCACertBundle(x509_crt_bundle);
    HTTPClient http;
    http.setTimeout(timeoutMs);
    http.setConnectTimeout(timeoutMs);
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    // Deliberately not http.getStream(): confirmed on real hardware that
    // Open-Meteo's response is chunked-transfer-encoded, and
    // HTTPClient::getStream() returns the raw TCP stream with chunk-size
    // markers still embedded - deserializeJson() failed with "InvalidInput"
    // reading it directly. getString() routes through HTTPClient's own
    // writeToStream(), which does correctly de-chunk (see
    // HTTPClient.cpp's HTTPC_TE_CHUNKED handling) - the streaming-parse
    // memory savings this originally aimed for isn't available from this
    // library without reimplementing chunk decoding ourselves.
    String body = http.getString();
    http.end();
    DeserializationError err = filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
                                       : deserializeJson(doc, body);
    return !err;
}

String buildForecastUrl(const DisplayConfig& config) {
    const char* unitParams = config.units == "imperial"
                                  ? "temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch"
                                  : "temperature_unit=celsius&wind_speed_unit=ms&precipitation_unit=mm";
    String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(config.latitude, 7) +
                 "&longitude=" + String(config.longitude, 7) +
                 "&hourly=weather_code,temperature_2m,precipitation,relative_humidity_2m,surface_pressure,visibility"
                 "&daily=weathercode,temperature_2m_max,temperature_2m_min,sunrise,sunset"
                 "&current=temperature,windspeed,winddirection,is_day,weather_code,apparent_temperature"
                 "&timezone=auto&models=best_match&forecast_days=" +
                 String(config.forecast_days + 1) + "&" + unitParams;
    return url;
}

String buildAqiUrl(const DisplayConfig& config) {
    return "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + String(config.latitude, 7) +
           "&longitude=" + String(config.longitude, 7) + "&hourly=european_aqi,uv_index&timezone=auto";
}

void buildForecastFilter(JsonDocument& filter) {
    filter["timezone"] = true;
    JsonObject current = filter["current"].to<JsonObject>();
    current["time"] = true;
    current["temperature"] = true;
    current["windspeed"] = true;
    current["winddirection"] = true;
    current["is_day"] = true;
    current["weather_code"] = true;
    current["apparent_temperature"] = true;
    JsonObject hourly = filter["hourly"].to<JsonObject>();
    hourly["time"] = true;
    hourly["temperature_2m"] = true;
    hourly["precipitation"] = true;
    hourly["relative_humidity_2m"] = true;
    hourly["surface_pressure"] = true;
    hourly["visibility"] = true;
    hourly["weather_code"] = true;
    JsonObject daily = filter["daily"].to<JsonObject>();
    daily["time"] = true;
    daily["weathercode"] = true;
    daily["temperature_2m_max"] = true;
    daily["temperature_2m_min"] = true;
    daily["sunrise"] = true;
    daily["sunset"] = true;
}

void buildAqiFilter(JsonDocument& filter) {
    JsonObject hourly = filter["hourly"].to<JsonObject>();
    hourly["time"] = true;
    hourly["european_aqi"] = true;
    hourly["uv_index"] = true;
}

// Mirrors weather_data.py's _value_at_current_hour(): first hourly entry
// whose hour matches `currentHour`.
bool valueAtCurrentHour(JsonArrayConst times, JsonArrayConst values, int currentHour, double& out) {
    size_t i = 0;
    for (JsonVariantConst t : times) {
        LocalTime lt;
        if (parseIsoLocal(t.as<const char*>(), lt) && lt.hour == currentHour) {
            if (i < values.size() && !values[i].isNull()) {
                out = values[i].as<double>();
                return true;
            }
            return false;
        }
        ++i;
    }
    return false;
}

int findHourlyStartIndex(JsonArrayConst times, const LocalTime& now) {
    int idx = 0;
    for (JsonVariantConst t : times) {
        LocalTime lt;
        if (parseIsoLocal(t.as<const char*>(), lt)) {
            if (sameDate(lt, now) && lt.hour >= now.hour) return idx;
            if (afterDate(lt, now)) break;
        }
        ++idx;
    }
    return 0;
}

void parseDailyForecast(JsonObjectConst daily, const DisplayConfig& config, WeatherSnapshot& out) {
    JsonArrayConst times = daily["time"].as<JsonArrayConst>();
    JsonArrayConst codes = daily["weathercode"].as<JsonArrayConst>();
    JsonArrayConst tmax = daily["temperature_2m_max"].as<JsonArrayConst>();
    JsonArrayConst tmin = daily["temperature_2m_min"].as<JsonArrayConst>();
    double tempOffset = config.units == "standard" ? 273.15 : 0.0;

    out.dailyCount = 0;
    int n = times.size();
    for (int i = 0; i < n && out.dailyCount < WeatherSnapshot::kMaxDaily; ++i) {
        LocalTime dt;
        if (!parseIsoLocal(times[i].as<const char*>(), dt)) continue;
        int code = i < static_cast<int>(codes.size()) ? codes[i].as<int>() : 0;

        // Moon phase for "tomorrow relative to this forecast day" - matches
        // weather_data.py's `target_date = dt.date() + timedelta(days=1)`.
        LocalTime target = dt;
        target.day += 1;  // deliberately not normalized across month ends,
                            // matching moon::phaseAge()'s tolerance for any
                            // (y,m,d) triple - see its julianDay() helper.
        double age = moon::phaseAge(target.year, target.month, target.day);
        MoonIcon moonPhase = moon::mirrorForLatitude(moon::phaseNameFromAge(age), config.latitude);
        double illum = moon::illuminationPercent(age);

        DayForecast& day = out.daily[out.dailyCount++];
        day.dayLabel = formatDayAbbrNl(dt);
        day.icon = mapWeatherCodeToIcon(code, true);
        day.high = i < static_cast<int>(tmax.size()) ? static_cast<int>(tmax[i].as<double>() + tempOffset) : 0;
        day.low = i < static_cast<int>(tmin.size()) ? static_cast<int>(tmin[i].as<double>() + tempOffset) : 0;
        day.moonPhasePct = String(static_cast<int>(std::lround(illum)));
        day.moonIcon = moonPhase;
    }
}

void parseHourlyAndSun(JsonObjectConst hourlyObj, JsonObjectConst dailyObj, const DisplayConfig& config,
                        const LocalTime& now, WeatherSnapshot& out) {
    JsonArrayConst times = hourlyObj["time"].as<JsonArrayConst>();
    JsonArrayConst temps = hourlyObj["temperature_2m"].as<JsonArrayConst>();
    JsonArrayConst rains = hourlyObj["precipitation"].as<JsonArrayConst>();
    JsonArrayConst codes = hourlyObj["weather_code"].as<JsonArrayConst>();
    double tempOffset = config.units == "standard" ? 273.15 : 0.0;

    int startIndex = findHourlyStartIndex(times, now);

    JsonArrayConst sunrises = dailyObj["sunrise"].as<JsonArrayConst>();
    JsonArrayConst sunsets = dailyObj["sunset"].as<JsonArrayConst>();
    struct DaySun {
        int y, m, d;
        LocalTime sunrise, sunset;
    };
    DaySun sunMap[WeatherSnapshot::kMaxDaily];
    int sunMapCount = 0;
    for (size_t i = 0; i < sunrises.size() && i < sunsets.size() && sunMapCount < WeatherSnapshot::kMaxDaily; ++i) {
        LocalTime sr, ss;
        if (parseIsoLocal(sunrises[i].as<const char*>(), sr) && parseIsoLocal(sunsets[i].as<const char*>(), ss)) {
            sunMap[sunMapCount++] = {sr.year, sr.month, sr.day, sr, ss};
        }
    }
    auto findSun = [&](const LocalTime& date) -> DaySun* {
        for (int i = 0; i < sunMapCount; ++i) {
            if (sunMap[i].y == date.year && sunMap[i].m == date.month && sunMap[i].d == date.day) return &sunMap[i];
        }
        return nullptr;
    };

    out.hourlyCount = 0;
    int n = times.size();
    for (int i = startIndex; i < n && out.hourlyCount < WeatherSnapshot::kMaxHourly; ++i) {
        LocalTime dt;
        if (!parseIsoLocal(times[i].as<const char*>(), dt)) continue;
        DaySun* sun = findSun(dt);
        bool isDay = false;
        if (sun) {
            double m = minutesSinceEpoch(dt);
            isDay = minutesSinceEpoch(sun->sunrise) <= m && m < minutesSinceEpoch(sun->sunset);
        }
        int code = i < static_cast<int>(codes.size()) ? codes[i].as<int>() : 0;

        HourPoint& hp = out.hourly[out.hourlyCount++];
        hp.timeLabel = formatHourOnly(dt, config.time_format);
        hp.temperature = i < static_cast<int>(temps.size()) ? static_cast<int>(temps[i].as<double>() + tempOffset) : 0;
        hp.rain = i < static_cast<int>(rains.size()) ? rains[i].as<float>() : 0;
        hp.icon = mapWeatherCodeToIcon(code, isDay);
    }

    out.sunEventCount = 0;
    if (out.hourlyCount > 0) {
        LocalTime startDt, endDt;
        parseIsoLocal(times[startIndex].as<const char*>(), startDt);
        parseIsoLocal(times[startIndex + out.hourlyCount - 1].as<const char*>(), endDt);
        double startM = minutesSinceEpoch(startDt);
        double endM = minutesSinceEpoch(endDt);
        for (int i = 0; i < sunMapCount; ++i) {
            struct { LocalTime t; SunIcon icon; int slot; } events[2] = {
                {sunMap[i].sunrise, SunIcon::Sunrise, 0}, {sunMap[i].sunset, SunIcon::Sunset, 1}};
            for (auto& e : events) {
                double m = minutesSinceEpoch(e.t);
                if (m >= startM && m <= endM && out.sunEventCount < WeatherSnapshot::kMaxSunEvents) {
                    out.sunEvents[out.sunEventCount++] = {static_cast<float>((m - startM) / 60.0), e.icon};
                }
            }
        }
    }
}

void parseDataPoints(JsonObjectConst current, JsonObjectConst hourlyObj, JsonObjectConst aqiHourly,
                      const DisplayConfig& config, const LocalTime& now, WeatherSnapshot& out) {
    Units u = unitsFor(config.units);
    JsonArrayConst hourlyTimes = hourlyObj["time"].as<JsonArrayConst>();

    // Wind
    {
        double speed = current["windspeed"] | 0.0;
        double dir = current["winddirection"] | 0.0;
        DataPoint& dp = out.dataPoints[0];
        dp.kind = DataPointKind::Wind;
        dp.label = beaufortDescriptionNl(windSpeedMs(speed, config.units));
        dp.direction = windDirectionAbbrNl(dir);
        dp.rawValue = dir;
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f %s", speed, u.speed);
        dp.valueText = buf;
    }
    // Humidity
    {
        double humidity = NAN;
        bool ok = valueAtCurrentHour(hourlyTimes, hourlyObj["relative_humidity_2m"].as<JsonArrayConst>(),
                                      now.hour, humidity);
        DataPoint& dp = out.dataPoints[1];
        dp.kind = DataPointKind::Humidity;
        dp.label = "Vochtigheid";
        dp.valueText = ok ? String(static_cast<int>(humidity)) + " %" : "N/A";
        dp.dropCount = ok ? humidityDropCount(humidity) : 1;
    }
    // Pressure
    {
        double pressure = NAN;
        bool ok =
            valueAtCurrentHour(hourlyTimes, hourlyObj["surface_pressure"].as<JsonArrayConst>(), now.hour, pressure);
        DataPoint& dp = out.dataPoints[2];
        dp.kind = DataPointKind::Pressure;
        dp.label = "Luchtdruk";
        dp.valueText = ok ? String(static_cast<int>(pressure)) + " hPa" : "N/A";
        dp.rawValue = pressure;
    }
    // UV
    {
        double uv = NAN;
        JsonArrayConst aqiTimes = aqiHourly["time"].as<JsonArrayConst>();
        bool ok = valueAtCurrentHour(aqiTimes, aqiHourly["uv_index"].as<JsonArrayConst>(), now.hour, uv);
        DataPoint& dp = out.dataPoints[3];
        dp.kind = DataPointKind::Uv;
        dp.label = "UV-index";
        dp.rawValue = uv;
        char buf[8];
        if (ok) {
            snprintf(buf, sizeof(buf), "%.1f", uv);
            dp.valueText = buf;
        } else {
            dp.valueText = "N/A";
        }
    }
    // Visibility
    {
        double raw = NAN;
        bool ok = valueAtCurrentHour(hourlyTimes, hourlyObj["visibility"].as<JsonArrayConst>(), now.hour, raw);
        DataPoint& dp = out.dataPoints[4];
        dp.kind = DataPointKind::Visibility;
        dp.label = "Zicht";
        if (ok) {
            bool imperial = config.units == "imperial";
            double conv = imperial ? (1.0 / 5280.0) : 0.001;
            double vmax = imperial ? 6.2 : 10.0;
            double visibility = raw * conv;
            char buf[16];
            snprintf(buf, sizeof(buf), visibility >= vmax ? ">=%.1f %s" : "%.1f %s", visibility, u.distance);
            dp.valueText = buf;
        } else {
            dp.valueText = "N/A";
        }
    }
    // AQI
    {
        double aqi = NAN;
        JsonArrayConst aqiTimes = aqiHourly["time"].as<JsonArrayConst>();
        bool ok = valueAtCurrentHour(aqiTimes, aqiHourly["european_aqi"].as<JsonArrayConst>(), now.hour, aqi);
        DataPoint& dp = out.dataPoints[5];
        dp.kind = DataPointKind::Aqi;
        dp.label = "Luchtkwaliteit";
        dp.valueText = ok ? String(static_cast<int>(std::lround(aqi))) + " " + aqiScaleNl(aqi) : "N/A";
        dp.rawValue = aqi;
    }
}

}  // namespace

bool fetchSnapshot(const DisplayConfig& config, WeatherSnapshot& out) {
    JsonDocument forecastFilter, aqiFilter;
    buildForecastFilter(forecastFilter);
    buildAqiFilter(aqiFilter);

    JsonDocument weatherDoc(&g_psramAllocator);
    if (!httpGetJson(buildForecastUrl(config), weatherDoc, &forecastFilter, 30000)) return false;

    JsonDocument aqiDoc(&g_psramAllocator);
    if (!httpGetJson(buildAqiUrl(config), aqiDoc, &aqiFilter, 30000)) return false;

    JsonObjectConst current = weatherDoc["current"].as<JsonObjectConst>();
    JsonObjectConst dailyObj = weatherDoc["daily"].as<JsonObjectConst>();
    JsonObjectConst hourlyObj = weatherDoc["hourly"].as<JsonObjectConst>();
    JsonObjectConst aqiHourly = aqiDoc["hourly"].as<JsonObjectConst>();

    LocalTime now;
    if (!parseIsoLocal(current["time"] | "", now)) return false;

    int code = current["weather_code"] | 0;
    bool isDay = (current["is_day"] | 1) != 0;
    double tempOffset = config.units == "standard" ? 273.15 : 0.0;

    out.currentDate = formatDateNl(now);
    out.currentIcon = mapWeatherCodeToIcon(code, isDay);
    out.currentTemp = static_cast<int>(std::lround((current["temperature"] | 0.0) + tempOffset));
    double apparent = current["apparent_temperature"] | (current["temperature"] | 0.0);
    out.feelsLike = static_cast<int>(std::lround(apparent + tempOffset));
    Units u = unitsFor(config.units);
    out.tempUnit = u.temperature;

    parseDailyForecast(dailyObj, config, out);
    out.forecastHigh = out.dailyCount > 0 ? out.daily[0].high : 0;
    out.forecastLow = out.dailyCount > 0 ? out.daily[0].low : 0;

    parseDataPoints(current, hourlyObj, aqiHourly, config, now, out);
    parseHourlyAndSun(hourlyObj, dailyObj, config, now, out);

    // First forecast day is "today" (used only for high/low above, matching
    // weather_data.py's `daily_forecast[1:]` slice) - shift the rest down so
    // `out.daily` holds the *upcoming* days, same as the Python version's
    // forecast row.
    if (out.dailyCount > 0) {
        for (int i = 0; i < out.dailyCount - 1; ++i) out.daily[i] = out.daily[i + 1];
        out.dailyCount = min(out.dailyCount - 1, config.forecast_days);
    }

    // Not reusing formatHourOnly() here: it always zero-pads to the top of
    // the hour ("%02d:00") for chart labels, but the header wants minutes.
    char buf[8];
    if (config.time_format == "24h") {
        snprintf(buf, sizeof(buf), "%02d:%02d", now.hour, now.minute);
    } else {
        int h12 = now.hour % 12;
        if (h12 == 0) h12 = 12;
        snprintf(buf, sizeof(buf), "%d:%02d %s", h12, now.minute, now.hour < 12 ? "AM" : "PM");
    }
    out.lastRefreshTime = buf;

    return true;
}

String fetchLocationName(double lat, double lon) {
    String url = "https://nominatim.openstreetmap.org/reverse?lat=" + String(lat, 7) + "&lon=" + String(lon, 7) +
                 "&format=jsonv2&accept-language=nl&zoom=14";

    WiFiClientSecure client;
    client.setCACertBundle(x509_crt_bundle);
    HTTPClient http;
    http.setTimeout(10000);
    http.setConnectTimeout(10000);
    if (!http.begin(client, url)) return "";
    http.setUserAgent("WeatherEPS32S3");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return "";
    }

    JsonDocument doc;
    JsonDocument filter;
    filter["address"]["city"] = true;
    filter["address"]["town"] = true;
    filter["address"]["village"] = true;
    filter["address"]["municipality"] = true;
    filter["address"]["hamlet"] = true;
    filter["address"]["suburb"] = true;
    filter["address"]["county"] = true;
    filter["address"]["country"] = true;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) return "";

    JsonObjectConst address = doc["address"].as<JsonObjectConst>();
    static const char* kCityKeys[] = {"city", "town", "village", "municipality", "hamlet", "suburb", "county"};
    String city;
    for (const char* key : kCityKeys) {
        if (!address[key].isNull()) {
            city = address[key].as<const char*>();
            break;
        }
    }
    String country = address["country"].isNull() ? "" : address["country"].as<const char*>();
    if (city.length() && country.length()) return city + ", " + country;
    return city.length() ? city : country;
}

}  // namespace weather

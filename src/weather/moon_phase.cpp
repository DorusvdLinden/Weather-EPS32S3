#include "moon_phase.h"

#include <cmath>

namespace moon {

namespace {

double fmod360(double deg) {
    double m = std::fmod(deg, 360.0);
    return m < 0 ? m + 360.0 : m;
}

double toRadians(double deg) { return deg * M_PI / 180.0; }

// Julian Day at 00:00 UTC for a Gregorian calendar date - matches
// astral.julian.julianday() with day_fraction=0 (that function is called
// with a `date`, never a `datetime`, in weather_data.py's moon usage).
double julianDay(int year, int month, int day) {
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    int a = year / 100;
    int b = 2 - a + a / 4;
    return std::floor(365.25 * (year + 4716)) + std::floor(30.6001 * (month + 1)) + day + b - 1524.5;
}

}  // namespace

double phaseAge(int year, int month, int day) {
    double jd = julianDay(year, month, day);
    double dt = std::pow(jd - 2382148, 2) / (41048480.0 * 86400.0);
    double t = (jd + dt - 2451545.0) / 36525.0;
    double t2 = t * t;
    double t3 = t2 * t;

    double d = toRadians(fmod360(297.85 + (445267.1115 * t) - (0.0016300 * t2) + (t3 / 545868.0)));
    double m = toRadians(fmod360(357.53 + (35999.0503 * t)));
    double m1 = toRadians(fmod360(134.96 + (477198.8676 * t) + (0.0089970 * t2) + (t3 / 69699.0)));

    double elong = d * 180.0 / M_PI + 6.29 * std::sin(m1);
    elong -= 2.10 * std::sin(m);
    elong += 1.27 * std::sin(2 * d - m1);
    elong += 0.66 * std::sin(2 * d);
    elong = fmod360(elong);
    elong = std::floor(elong);

    double phase = ((elong + 6.43) / 360.0) * 28.0;
    if (phase >= 28.0) phase -= 28.0;
    return phase;
}

MoonIcon phaseNameFromAge(double age) {
    struct Threshold {
        double upTo;
        MoonIcon icon;
    };
    static const Threshold thresholds[] = {
        {1.0, MoonIcon::NewMoon},          {7.0, MoonIcon::WaxingCrescent},
        {8.5, MoonIcon::FirstQuarter},     {14.0, MoonIcon::WaxingGibbous},
        {15.5, MoonIcon::FullMoon},        {22.0, MoonIcon::WaningGibbous},
        {23.5, MoonIcon::LastQuarter},     {29.0, MoonIcon::WaningCrescent},
    };
    for (const auto& t : thresholds) {
        if (age <= t.upTo) return t.icon;
    }
    return MoonIcon::NewMoon;
}

MoonIcon mirrorForLatitude(MoonIcon phase, double latitude) {
    if (latitude >= 0) return phase;
    switch (phase) {
        case MoonIcon::WaxingCrescent: return MoonIcon::WaningCrescent;
        case MoonIcon::WaningCrescent: return MoonIcon::WaxingCrescent;
        case MoonIcon::WaxingGibbous: return MoonIcon::WaningGibbous;
        case MoonIcon::WaningGibbous: return MoonIcon::WaxingGibbous;
        case MoonIcon::FirstQuarter: return MoonIcon::LastQuarter;
        case MoonIcon::LastQuarter: return MoonIcon::FirstQuarter;
        default: return phase;
    }
}

double illuminationPercent(double age) {
    constexpr double kLunarCycleDays = 29.530588853;
    double fraction = age / kLunarCycleDays;
    return (1 - std::cos(2 * M_PI * fraction)) / 2 * 100.0;
}

}  // namespace moon

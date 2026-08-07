#include "settings.h"

#include <Preferences.h>

namespace settings {

namespace {
constexpr char kNamespace[] = "weather";
}  // namespace

DisplayConfig load() {
    DisplayConfig def;  // struct's own default member initializers
    Preferences prefs;
    prefs.begin(kNamespace, true);

    DisplayConfig cfg;
    cfg.wifiSsid = prefs.getString("wifi_ssid", def.wifiSsid);
    cfg.wifiPassword = prefs.getString("wifi_pass", def.wifiPassword);
    cfg.latitude = prefs.getDouble("lat", def.latitude);
    cfg.longitude = prefs.getDouble("lon", def.longitude);
    cfg.units = prefs.getString("units", def.units);
    cfg.timezone = prefs.getString("timezone", def.timezone);
    cfg.time_format = prefs.getString("time_fmt", def.time_format);
    cfg.forecast_days = prefs.getInt("fc_days", def.forecast_days);
    cfg.graph_icon_step = prefs.getInt("icon_step", def.graph_icon_step);
    cfg.show_moon_phase = prefs.getBool("show_moon", def.show_moon_phase);
    cfg.background_color = prefs.getUInt("bg_color", def.background_color);
    cfg.text_color = prefs.getUInt("text_color", def.text_color);
    cfg.refresh_interval_seconds = prefs.getUInt("refresh_s", def.refresh_interval_seconds);

    prefs.end();
    return cfg;
}

void save(const DisplayConfig& config) {
    Preferences prefs;
    prefs.begin(kNamespace, false);

    prefs.putString("wifi_ssid", config.wifiSsid);
    prefs.putString("wifi_pass", config.wifiPassword);
    prefs.putDouble("lat", config.latitude);
    prefs.putDouble("lon", config.longitude);
    prefs.putString("units", config.units);
    prefs.putString("timezone", config.timezone);
    prefs.putString("time_fmt", config.time_format);
    prefs.putInt("fc_days", config.forecast_days);
    prefs.putInt("icon_step", config.graph_icon_step);
    prefs.putBool("show_moon", config.show_moon_phase);
    prefs.putUInt("bg_color", config.background_color);
    prefs.putUInt("text_color", config.text_color);
    prefs.putUInt("refresh_s", config.refresh_interval_seconds);

    prefs.end();
}

bool hasWifiCredentials() {
    Preferences prefs;
    prefs.begin(kNamespace, true);
    String ssid = prefs.getString("wifi_ssid", "");
    prefs.end();
    return ssid.length() > 0;
}

}  // namespace settings

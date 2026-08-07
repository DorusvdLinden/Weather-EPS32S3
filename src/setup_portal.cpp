#include "setup_portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "debug_log.h"
#include "display/frame_store.h"
#include "settings.h"

namespace setup_portal {

namespace {

constexpr uint32_t kIdleTimeoutMs = 10 * 60 * 1000;  // reboot back to normal cycling if left unattended
constexpr const char* kApSsid = "WeatherEPS32S3-Setup";
constexpr const char* kApPassword = "weather123";  // >=8 chars required for WPA2 SoftAP

DNSServer g_dns;
WebServer g_server(80);
DisplayConfig g_config;
uint32_t g_lastActivity = 0;

String hexColor(uint32_t rgb) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%06X", static_cast<unsigned>(rgb & 0xFFFFFF));
    return String(buf);
}

uint32_t parseHexColor(const String& s, uint32_t fallback) {
    if (s.length() < 6) return fallback;
    const char* start = s.c_str();
    if (start[0] == '#') start++;
    char* end = nullptr;
    unsigned long v = strtoul(start, &end, 16);
    if (end == start) return fallback;
    return static_cast<uint32_t>(v);
}

String option(const String& value, const String& current, const String& label) {
    String sel = value == current ? " selected" : "";
    return "<option value=\"" + value + "\"" + sel + ">" + label + "</option>";
}

// Panel choice is a build-time decision (see README's "Web setup/debug
// portal" section for why - different panels need different compiled C++
// types and, for the monochrome panel, a different dither algorithm), so
// this is a read-only status line, not a form field. Matches the
// WEATHER_PANEL_* macro platformio.ini's environments define.
const char* activePanelName() {
#if defined(WEATHER_PANEL_ACEP730)
    return "Waveshare 7.3\" ACeP (GxEPD2_730c_ACeP_730) - 7-color, ~28-40s refresh";
#elif defined(WEATHER_PANEL_MONO_V2)
    return "Waveshare 7.5\" e-Paper V2 (GxEPD2_750_GDEY075T7) - monochrome, ~1.2s refresh";
#elif defined(WEATHER_PANEL_INKY_IMPRESSION)
    return "Pimoroni Inky Impression 7.3\" (AC073TC1A, hand-written raw-SPI driver) - 7-color, ~41s refresh";
#else
    return "None - headless build (SerialDumpDriver debug output only, no physical panel driven)";
#endif
}

String htmlPage() {
    String html;
    html.reserve(4096);
    html += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    html += "<title>Weather-EPS32S3 setup</title>";
    html += "<style>body{font-family:sans-serif;max-width:480px;margin:1.5em auto;padding:0 1em}"
            "fieldset{margin-bottom:1em}label{display:block;margin-top:.5em}"
            "input,select{width:100%;box-sizing:border-box;padding:.3em}"
            "img{max-width:100%;border:1px solid #999}"
            ".row{display:flex;gap:1em}.row>div{flex:1}</style></head><body>";

    html += "<h1>Weather-EPS32S3</h1>";
    html += "<p>Last render (may be stale or absent if no cycle has completed yet):</p>";
    html += "<img src=\"/image\" alt=\"last render\">";
    html += "<p><a href=\"/log\">view step-by-step debug log</a></p>";

    html += "<fieldset><legend>Display panel</legend>";
    html += "<p><strong>" + String(activePanelName()) + "</strong></p>";
    html += "<p style=\"color:#666;font-size:.9em;margin-top:.3em\">Panel choice is set at build time, not here"
            " - see <code>docs/hardware.md</code> for wiring and the three build environments"
            " (<code>-panel-acep730</code> / <code>-panel-mono-v2</code> / <code>-panel-inky</code>);"
            " flash a different one to switch panels.</p>";
    html += "</fieldset>";

    html += "<form method=\"POST\" action=\"/save\">";

    html += "<fieldset><legend>WiFi</legend>";
    html += "<label>SSID<input name=\"wifi_ssid\" value=\"" + g_config.wifiSsid + "\" required></label>";
    html += "<label>Password (leave blank to keep current)<input type=\"password\" name=\"wifi_password\"></label>";
    html += "</fieldset>";

    html += "<fieldset><legend>Location & units</legend>";
    html += "<div class=\"row\"><div><label>Latitude<input type=\"number\" step=\"any\" name=\"lat\" value=\"" +
            String(g_config.latitude, 7) + "\"></label></div>";
    html += "<div><label>Longitude<input type=\"number\" step=\"any\" name=\"lon\" value=\"" +
            String(g_config.longitude, 7) + "\"></label></div></div>";
    html += "<label>Units<select name=\"units\">";
    html += option("metric", g_config.units, "Metric (\xC2\xB0" "C, m/s, km)");
    html += option("imperial", g_config.units, "Imperial (\xC2\xB0" "F, mph, mi)");
    html += option("standard", g_config.units, "Standard (K, m/s, km)");
    html += "</select></label>";
    html += "<label>Timezone (IANA name, informational only - Open-Meteo's timezone=auto "
            "already drives all rendered times)<input name=\"timezone\" value=\"" + g_config.timezone + "\"></label>";
    html += "<label>Time format<select name=\"time_format\">";
    html += option("24h", g_config.time_format, "24-hour");
    html += option("12h", g_config.time_format, "12-hour");
    html += "</select></label>";
    html += "<label>Forecast days (1-7)<input type=\"number\" min=\"1\" max=\"7\" name=\"forecast_days\" value=\"" +
            String(g_config.forecast_days) + "\"></label>";
    html += "</fieldset>";

    html += "<fieldset><legend>Display</legend>";
    html += "<label><input type=\"checkbox\" name=\"show_moon\" value=\"1\"" +
            String(g_config.show_moon_phase ? " checked" : "") + "> Show moon phase on forecast cards</label>";
    html += "<div class=\"row\"><div><label>Background color<input type=\"color\" name=\"bg_color\" value=\"" +
            hexColor(g_config.background_color) + "\"></label></div>";
    html += "<div><label>Text color<input type=\"color\" name=\"text_color\" value=\"" +
            hexColor(g_config.text_color) + "\"></label></div></div>";
    html += "<label>Chart hour-label/icon step (1-6)<input type=\"number\" min=\"1\" max=\"6\" "
            "name=\"graph_icon_step\" value=\"" + String(g_config.graph_icon_step) + "\"></label>";
    html += "<label>Refresh interval, seconds (deep-sleep wake cadence)<input type=\"number\" min=\"60\" "
            "name=\"refresh_s\" value=\"" + String(g_config.refresh_interval_seconds) + "\"></label>";
    html += "</fieldset>";

    html += "<button type=\"submit\">Save and reboot</button></form>";
    html += "</body></html>";
    return html;
}

void handleRoot() {
    g_lastActivity = millis();
    g_server.send(200, "text/html", htmlPage());
}

void handleSave() {
    g_lastActivity = millis();

    if (g_server.hasArg("wifi_ssid")) g_config.wifiSsid = g_server.arg("wifi_ssid");
    if (g_server.hasArg("wifi_password") && g_server.arg("wifi_password").length() > 0) {
        g_config.wifiPassword = g_server.arg("wifi_password");
    }
    if (g_server.hasArg("lat")) g_config.latitude = g_server.arg("lat").toDouble();
    if (g_server.hasArg("lon")) g_config.longitude = g_server.arg("lon").toDouble();
    if (g_server.hasArg("units")) g_config.units = g_server.arg("units");
    if (g_server.hasArg("timezone")) g_config.timezone = g_server.arg("timezone");
    if (g_server.hasArg("time_format")) g_config.time_format = g_server.arg("time_format");
    if (g_server.hasArg("forecast_days")) {
        g_config.forecast_days = constrain(g_server.arg("forecast_days").toInt(), 1, 7);
    }
    if (g_server.hasArg("graph_icon_step")) {
        g_config.graph_icon_step = constrain(g_server.arg("graph_icon_step").toInt(), 1, 6);
    }
    g_config.show_moon_phase = g_server.hasArg("show_moon");
    if (g_server.hasArg("bg_color")) g_config.background_color = parseHexColor(g_server.arg("bg_color"), g_config.background_color);
    if (g_server.hasArg("text_color")) g_config.text_color = parseHexColor(g_server.arg("text_color"), g_config.text_color);
    if (g_server.hasArg("refresh_s")) {
        long v = g_server.arg("refresh_s").toInt();
        g_config.refresh_interval_seconds = v < 60 ? 60 : static_cast<unsigned>(v);
    }

    settings::save(g_config);
    g_server.send(200, "text/html",
                   "<html><body><p>Saved. Rebooting into normal operation...</p></body></html>");
    delay(1000);
    ESP.restart();
}

void handleLog() {
    g_lastActivity = millis();
    g_server.send(200, "text/plain", debuglog::readAll());
}

void handleImage() {
    g_lastActivity = millis();
    uint8_t* bmp = nullptr;
    size_t len = 0;
    if (!frame_store::renderBmp(&bmp, &len)) {
        g_server.send(404, "text/plain", "No frame rendered yet");
        return;
    }
    g_server.setContentLength(len);
    g_server.send(200, "image/bmp", "");
    g_server.sendContent(reinterpret_cast<const char*>(bmp), len);
    heap_caps_free(bmp);
}

// Captive-portal redirect: any request to an unrecognized host gets bounced
// to the settings page, so most phones/laptops auto-open it on associating
// with the AP instead of the user having to know to browse to 192.168.4.1.
void handleNotFound() {
    g_lastActivity = millis();
    g_server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    g_server.send(302, "text/plain", "");
}

}  // namespace

void run() {
    g_config = settings::load();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPassword);
    g_dns.start(53, "*", WiFi.softAPIP());

    g_server.on("/", HTTP_GET, handleRoot);
    g_server.on("/save", HTTP_POST, handleSave);
    g_server.on("/log", HTTP_GET, handleLog);
    g_server.on("/image", HTTP_GET, handleImage);
    g_server.onNotFound(handleNotFound);
    g_server.begin();

    g_lastActivity = millis();
    Serial.print("Setup portal up - connect to WiFi \"");
    Serial.print(kApSsid);
    Serial.print("\" (password ");
    Serial.print(kApPassword);
    Serial.print("), then browse to http://");
    Serial.println(WiFi.softAPIP());

    while (true) {
        g_dns.processNextRequest();
        g_server.handleClient();
        if (millis() - g_lastActivity > kIdleTimeoutMs) {
            Serial.println("Setup portal idle timeout, rebooting into normal operation");
            ESP.restart();
        }
        delay(1);  // guarantee a scheduler yield point - avoids relying on handleClient()/
                   // processNextRequest() alone to keep the idle task (and its watchdog feed) fed
    }
}

}  // namespace setup_portal

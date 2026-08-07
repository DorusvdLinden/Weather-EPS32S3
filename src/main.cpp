#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "canvas.h"
#include "config.h"
#include "debug_log.h"
#include "display/display_sink.h"
#include "display/frame_store.h"
#include "display/serial_dump_driver.h"
#include "graphics/dither.h"
#include "graphics/rgb_canvas.h"
#include "graphics/text.h"
#include "layout.h"
#include "settings.h"
#include "setup_portal.h"
#include "weather/open_meteo.h"

// Exactly one of these panel-select macros is defined per PlatformIO
// environment (see platformio.ini) - display selection is build-time, not
// a runtime/web-GUI choice, since each panel needs a different compiled
// C++ type (GxEPD2 is templated per-panel) and, for the monochrome panel,
// a different dither algorithm entirely (see graphics/dither.h). None
// defined -> the headless serial-dump debug driver, so Phases 1-2 can be
// validated without any panel wired up at all.
#if defined(WEATHER_PANEL_ACEP730)
#include "display/epd_driver.h"
#elif defined(WEATHER_PANEL_MONO_V2)
#include "display/waveshare_mono_driver.h"
#elif defined(WEATHER_PANEL_INKY_IMPRESSION)
#include "display/inky_impression_driver.h"
#endif

// arduino-esp32's default loopTask stack is only 8KB (ARDUINO_LOOP_STACK_SIZE
// in cores/esp32/main.cpp) - confirmed too small on real hardware: a WiFi
// auth failure (wrong password) triggered a genuine stack-canary panic
// ("Guru Meditation Error... Stack canary watchpoint triggered (loopTask)")
// during the WiFi driver's own internal auth-retry event processing, before
// this code even reached the TLS handshake (WiFiClientSecure/mbedTLS) or
// JSON parsing later in the same cycle - both also meaningfully stack-heavy.
// SET_LOOP_TASK_STACK_SIZE is arduino-esp32's own documented override (must
// be at file scope, not inside a namespace, so it correctly replaces the
// weak default symbol at link time). Must come before setup()/loop().
SET_LOOP_TASK_STACK_SIZE(16384);

// Survives deep sleep (unlike everything else, which resets) - gives the
// debug log a meaningful cycle identifier even though there's no RTC/NTP
// wall clock on this device (see weather/open_meteo.h's timezone note).
RTC_DATA_ATTR uint32_t g_cycleCount = 0;

namespace {

bool connectWifi(const DisplayConfig& config, uint32_t timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) return false;
        delay(250);
    }
    return true;
}

// NVS-cached reverse-geocoded location name (README risk #6 / Phase 1 exit
// criterion) - lat/long are fixed per device, and Nominatim rate-limits to
// 1 req/sec with a required unique User-Agent, so there's no reason to ever
// resolve this more than once per device.
String resolveLocationName(const DisplayConfig& config) {
    Preferences prefs;
    prefs.begin("weather", false);
    String cached = prefs.getString("location", "");
    if (cached.length()) {
        prefs.end();
        return cached;
    }
    String resolved = weather::fetchLocationName(config.latitude, config.longitude);
    if (resolved.length()) prefs.putString("location", resolved);
    prefs.end();
    return resolved;
}

// Fans the dither pass out to both whichever DisplaySink is actually
// driving hardware (EpdDriver/SerialDumpDriver) and a FrameStore, so the
// setup/debug portal's /image route always has a preview of the last
// render regardless of which sink is active.
struct DitherTargets {
    DisplaySink* primary;
    FrameStore* frameStore;
};

bool renderAndPresent(const DisplayConfig& config, const weather::WeatherSnapshot& snapshot) {
    RgbCanvas rgbCanvas(layout::kCanvasW, layout::kCanvasH);
    if (!rgbCanvas.begin()) {
        debuglog::logStep("canvas_alloc", false, "PSRAM allocation failed - is PSRAM enabled in the board config?");
        return false;
    }
    text::begin(rgbCanvas);
    canvas::render(rgbCanvas, config, snapshot);
    debuglog::logStep("canvas_render", true);

#if defined(WEATHER_PANEL_ACEP730)
    EpdDriver sink;
#elif defined(WEATHER_PANEL_MONO_V2)
    WaveshareMonoDriver sink;
#elif defined(WEATHER_PANEL_INKY_IMPRESSION)
    InkyImpressionDriver sink;
#else
    SerialDumpDriver sink;
#endif
    FrameStore frameStore;
    if (!sink.begin() || !frameStore.begin()) {
        debuglog::logStep("display_sink_begin", false);
        return false;
    }

    DitherTargets targets{&sink, &frameStore};
    auto emit = [](int16_t x, int16_t y, palette::Index idx, void* ctx) {
        auto* t = static_cast<DitherTargets*>(ctx);
        t->primary->setPixel(x, y, idx);
        t->frameStore->setPixel(x, y, idx);
    };
#if defined(WEATHER_PANEL_MONO_V2)
    dither::floydSteinbergMono(rgbCanvas, emit, &targets);
#else
    dither::floydSteinberg(rgbCanvas, emit, &targets);
#endif

    sink.present();
    frameStore.present();
    debuglog::logStep("panel_push", true);
    return true;
}

void goToSleep(const DisplayConfig& config) {
    esp_sleep_enable_timer_wakeup(wakeIntervalUs(config));
    Serial.flush();
    esp_deep_sleep_start();
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("Weather-EPS32S3 boot");

    LittleFS.begin(true);  // formatOnFail: true, so an unformatted first-ever partition still mounts

    pinMode(pins::kBootButton, INPUT_PULLUP);
    bool bootButtonHeld = digitalRead(pins::kBootButton) == LOW;

    g_cycleCount++;
    debuglog::beginCycle(g_cycleCount);
    debuglog::logStep("boot", true);

    DisplayConfig config = settings::load();

    if (bootButtonHeld || !settings::hasWifiCredentials()) {
        debuglog::logStep("setup_trigger", true, bootButtonHeld ? "BOOT button held" : "no WiFi credentials saved");
        debuglog::endCycle();
        setup_portal::run();  // never returns - reboots on save or idle timeout
    }

    bool wifiOk = connectWifi(config, 20000);
    debuglog::logStep("wifi_connect", wifiOk, wifiOk ? WiFi.localIP().toString() : "timed out after 20s");
    if (!wifiOk) {
        debuglog::endCycle();
        goToSleep(config);
        return;
    }

    weather::WeatherSnapshot snapshot;
    bool fetchOk = weather::fetchSnapshot(config, snapshot);
    debuglog::logStep("weather_fetch", fetchOk);
    if (!fetchOk) {
        debuglog::endCycle();
        WiFi.disconnect(true);
        goToSleep(config);
        return;
    }

    snapshot.location = resolveLocationName(config);
    debuglog::logStep("location_resolve", true, snapshot.location.length() ? snapshot.location : "(empty)");

    bool renderOk = renderAndPresent(config, snapshot);

    WiFi.disconnect(true);
    debuglog::logStep("sleep", true);
    debuglog::endCycle();
    Serial.println(renderOk ? "Render complete, entering deep sleep" : "Render failed, entering deep sleep anyway");
    goToSleep(config);
}

// Never reached - goToSleep() resets the board back into setup() on wake.
void loop() {}

#include "rgb_canvas.h"

#include <esp_heap_caps.h>

#include "palette.h"

RgbCanvas::RgbCanvas(int16_t w, int16_t h) : Adafruit_GFX(w, h) {}

RgbCanvas::~RgbCanvas() {
    if (buf_) {
        heap_caps_free(buf_);
    }
}

bool RgbCanvas::begin() {
    size_t bytes = static_cast<size_t>(WIDTH) * static_cast<size_t>(HEIGHT) * 3;
    buf_ = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    return buf_ != nullptr;
}

void RgbCanvas::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (!buf_ || x < 0 || y < 0 || x >= width() || y >= height()) return;
    palette::Rgb888 rgb = palette::unpackRgb565(color);
    setPixelRgb(x, y, rgb.r, rgb.g, rgb.b);
}

void RgbCanvas::setPixelRgb(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (!buf_ || x < 0 || y < 0 || x >= width() || y >= height()) return;
    size_t idx = (static_cast<size_t>(y) * WIDTH + x) * 3;
    buf_[idx] = r;
    buf_[idx + 1] = g;
    buf_[idx + 2] = b;
}

void RgbCanvas::blendPixelRgb(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    if (!buf_ || x < 0 || y < 0 || x >= width() || y >= height()) return;
    if (alpha == 255) {
        setPixelRgb(x, y, r, g, b);
        return;
    }
    if (alpha == 0) return;
    size_t idx = (static_cast<size_t>(y) * WIDTH + x) * 3;
    uint8_t dr = buf_[idx], dg = buf_[idx + 1], db = buf_[idx + 2];
    buf_[idx] = static_cast<uint8_t>((r * alpha + dr * (255 - alpha)) / 255);
    buf_[idx + 1] = static_cast<uint8_t>((g * alpha + dg * (255 - alpha)) / 255);
    buf_[idx + 2] = static_cast<uint8_t>((b * alpha + db * (255 - alpha)) / 255);
}

bool RgbCanvas::getPixelRgb(int16_t x, int16_t y, uint8_t* r, uint8_t* g, uint8_t* b) const {
    if (!buf_ || x < 0 || y < 0 || x >= width() || y >= height()) return false;
    size_t idx = (static_cast<size_t>(y) * WIDTH + x) * 3;
    *r = buf_[idx];
    *g = buf_[idx + 1];
    *b = buf_[idx + 2];
    return true;
}

#include "serial_dump_driver.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "layout.h"

namespace {
constexpr int kW = layout::kCanvasW;
constexpr int kH = layout::kCanvasH;
constexpr size_t kBufSize = static_cast<size_t>(kW) * kH / 2;
}  // namespace

bool SerialDumpDriver::begin() {
    buf_ = static_cast<uint8_t*>(heap_caps_malloc(kBufSize, MALLOC_CAP_SPIRAM));
    return buf_ != nullptr;
}

void SerialDumpDriver::setPixel(int16_t x, int16_t y, palette::Index color) {
    if (!buf_ || x < 0 || y < 0 || x >= kW || y >= kH) return;
    size_t pixelIndex = static_cast<size_t>(y) * kW + x;
    size_t byteIndex = pixelIndex / 2;
    if (pixelIndex % 2 == 0) {
        buf_[byteIndex] = (buf_[byteIndex] & 0x0F) | (static_cast<uint8_t>(color) << 4);
    } else {
        buf_[byteIndex] = (buf_[byteIndex] & 0xF0) | static_cast<uint8_t>(color);
    }
}

void SerialDumpDriver::present() {
    if (!buf_) return;
    // Tiny framing header so tools/dump_to_png.py can find the start of a
    // frame and knows its dimensions - see that script for the reader side.
    uint8_t header[8] = {'W', 'X', 'P', 'D', static_cast<uint8_t>(kW & 0xFF), static_cast<uint8_t>((kW >> 8) & 0xFF),
                          static_cast<uint8_t>(kH & 0xFF), static_cast<uint8_t>((kH >> 8) & 0xFF)};
    Serial.write(header, sizeof(header));
    Serial.write(buf_, kBufSize);
    Serial.flush();
}

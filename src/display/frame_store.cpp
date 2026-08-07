#include "frame_store.h"

#include <LittleFS.h>
#include <esp_heap_caps.h>

#include "layout.h"
#include "palette.h"

namespace {
constexpr int kW = layout::kCanvasW;
constexpr int kH = layout::kCanvasH;
constexpr size_t kPackedBytes = static_cast<size_t>(kW) * kH / 2;
constexpr const char* kFramePath = "/last_frame.bin";
}  // namespace

bool FrameStore::begin() {
    buf_ = static_cast<uint8_t*>(heap_caps_malloc(kPackedBytes, MALLOC_CAP_SPIRAM));
    return buf_ != nullptr;
}

void FrameStore::setPixel(int16_t x, int16_t y, palette::Index color) {
    if (!buf_ || x < 0 || y < 0 || x >= kW || y >= kH) return;
    size_t pixelIndex = static_cast<size_t>(y) * kW + x;
    size_t byteIndex = pixelIndex / 2;
    if (pixelIndex % 2 == 0) {
        buf_[byteIndex] = (buf_[byteIndex] & 0x0F) | (static_cast<uint8_t>(color) << 4);
    } else {
        buf_[byteIndex] = (buf_[byteIndex] & 0xF0) | static_cast<uint8_t>(color);
    }
}

void FrameStore::present() {
    if (!buf_) return;
    if (!LittleFS.begin(false)) return;
    File f = LittleFS.open(kFramePath, FILE_WRITE);
    if (!f) return;
    f.write(buf_, kPackedBytes);
    f.close();
}

namespace frame_store {

namespace {
void writeLE32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}
void writeLE16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}
}  // namespace

bool renderBmp(uint8_t** outBuf, size_t* outLen) {
    *outBuf = nullptr;
    *outLen = 0;
    if (!LittleFS.begin(false)) return false;

    File f = LittleFS.open(kFramePath, FILE_READ);
    if (!f || f.size() != kPackedBytes) {
        if (f) f.close();
        return false;
    }
    uint8_t* packed = static_cast<uint8_t*>(heap_caps_malloc(kPackedBytes, MALLOC_CAP_SPIRAM));
    if (!packed) {
        f.close();
        return false;
    }
    f.read(packed, kPackedBytes);
    f.close();

    size_t rowBytes = static_cast<size_t>(kW) * 3;  // 800*3=2400, already a multiple of 4: no row padding needed
    size_t pixelDataSize = rowBytes * kH;
    size_t total = 54 + pixelDataSize;
    uint8_t* bmp = static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM));
    if (!bmp) {
        heap_caps_free(packed);
        return false;
    }

    uint8_t* h = bmp;
    h[0] = 'B';
    h[1] = 'M';
    writeLE32(h + 2, static_cast<uint32_t>(total));
    writeLE32(h + 6, 0);
    writeLE32(h + 10, 54);
    writeLE32(h + 14, 40);  // DIB header size (BITMAPINFOHEADER)
    writeLE32(h + 18, static_cast<uint32_t>(kW));
    writeLE32(h + 22, static_cast<uint32_t>(kH));  // positive height = bottom-up rows
    writeLE16(h + 26, 1);                          // color planes
    writeLE16(h + 28, 24);                         // bits per pixel
    writeLE32(h + 30, 0);                          // no compression
    writeLE32(h + 34, static_cast<uint32_t>(pixelDataSize));
    writeLE32(h + 38, 2835);  // ~72 DPI
    writeLE32(h + 42, 2835);
    writeLE32(h + 46, 0);
    writeLE32(h + 50, 0);

    uint8_t* out = bmp + 54;
    for (int y = 0; y < kH; ++y) {
        int srcY = kH - 1 - y;  // BMP rows are bottom-up
        for (int x = 0; x < kW; ++x) {
            size_t pixelIndex = static_cast<size_t>(srcY) * kW + x;
            uint8_t byte = packed[pixelIndex / 2];
            uint8_t idx = (pixelIndex % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
            const palette::Rgb888& c = palette::kRgb[idx % palette::kCount];
            size_t o = (static_cast<size_t>(y) * kW + x) * 3;
            out[o] = c.b;
            out[o + 1] = c.g;
            out[o + 2] = c.r;
        }
    }

    heap_caps_free(packed);
    *outBuf = bmp;
    *outLen = total;
    return true;
}

}  // namespace frame_store

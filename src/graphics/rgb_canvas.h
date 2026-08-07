#pragma once

#include <Adafruit_GFX.h>
#include <cstdint>

// A full-frame RGB888 working buffer that Adafruit_GFX draws into, matching
// InkyPiZero's canvas.py (which draws into a Pillow RGB Image before any
// palette quantization happens). Kept as true 24bpp - not the panel's final
// 4bpp packed format - so error-diffusion dithering (src/graphics/dither.h)
// has full color precision to work with; converting straight to 4bpp while
// drawing would bake in banding before dithering ever ran.
//
// 800*480*3 = 1,152,000 bytes: must live in PSRAM (see README risk #5 -
// plain `new`/`malloc` for something this size is NOT guaranteed to land in
// PSRAM depending on board SPIRAM config, so allocation explicitly requests
// MALLOC_CAP_SPIRAM in the .cpp rather than relying on that).
class RgbCanvas : public Adafruit_GFX {
   public:
    RgbCanvas(int16_t w, int16_t h);
    ~RgbCanvas() override;

    // Allocates the backing buffer in PSRAM. Must be called once before any
    // drawing; returns false (and leaves the canvas unusable) if PSRAM
    // allocation fails.
    bool begin();

    // Adafruit_GFX's primitives (drawLine, fillRect, drawChar, ...) all
    // eventually call this with a standard RGB565-packed color - it is
    // unpacked to RGB888 and stored directly, no palette lookup yet.
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;

    // For code that needs true 8-bit-per-channel precision (alpha-blended
    // fills in widgets/chart.cpp) or needs to read back what's already
    // drawn (icon compositing in assets/asset_store.cpp) - Adafruit_GFX has
    // no equivalent of either, so these are the canvas's own small addition.
    void setPixelRgb(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b);
    void blendPixelRgb(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
    bool getPixelRgb(int16_t x, int16_t y, uint8_t* r, uint8_t* g, uint8_t* b) const;

    uint8_t* buffer() { return buf_; }
    const uint8_t* buffer() const { return buf_; }

   private:
    uint8_t* buf_ = nullptr;
};

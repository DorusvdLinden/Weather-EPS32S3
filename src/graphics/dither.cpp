#include "dither.h"

namespace dither {

namespace {

inline uint8_t clampToByte(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<uint8_t>(v);
}

void diffuse(RgbCanvas& canvas, int16_t x, int16_t y, int errR, int errG, int errB, int weight) {
    if (x < 0 || y < 0 || x >= canvas.width() || y >= canvas.height()) return;
    uint8_t r, g, b;
    if (!canvas.getPixelRgb(x, y, &r, &g, &b)) return;
    canvas.setPixelRgb(x, y, clampToByte(r + errR * weight / 16), clampToByte(g + errG * weight / 16),
                        clampToByte(b + errB * weight / 16));
}

}  // namespace

void floydSteinberg(RgbCanvas& canvas, EmitFn emit, void* ctx) {
    int16_t w = canvas.width();
    int16_t h = canvas.height();
    for (int16_t y = 0; y < h; ++y) {
        for (int16_t x = 0; x < w; ++x) {
            uint8_t r, g, b;
            canvas.getPixelRgb(x, y, &r, &g, &b);
            palette::Rgb888 oldColor{r, g, b};
            palette::Index idx = palette::nearest(oldColor);
            const palette::Rgb888& newColor = palette::kRgb[idx];

            int errR = int(oldColor.r) - int(newColor.r);
            int errG = int(oldColor.g) - int(newColor.g);
            int errB = int(oldColor.b) - int(newColor.b);

            emit(x, y, idx, ctx);

            diffuse(canvas, x + 1, y, errR, errG, errB, 7);
            diffuse(canvas, x - 1, y + 1, errR, errG, errB, 3);
            diffuse(canvas, x, y + 1, errR, errG, errB, 5);
            diffuse(canvas, x + 1, y + 1, errR, errG, errB, 1);
        }
    }
}

void floydSteinbergMono(RgbCanvas& canvas, EmitFn emit, void* ctx) {
    int16_t w = canvas.width();
    int16_t h = canvas.height();
    for (int16_t y = 0; y < h; ++y) {
        for (int16_t x = 0; x < w; ++x) {
            uint8_t r, g, b;
            canvas.getPixelRgb(x, y, &r, &g, &b);
            // ITU-R BT.601 luma - standard, well-documented grayscale weighting.
            int luma = (299 * r + 587 * g + 114 * b) / 1000;
            palette::Index idx = luma >= 128 ? palette::kWhite : palette::kBlack;
            int target = idx == palette::kWhite ? 255 : 0;
            int err = luma - target;

            emit(x, y, idx, ctx);

            // Diffuse the same luma error into all 3 channels equally - the
            // canvas naturally desaturates as this propagates, which is
            // fine (and expected): color no longer means anything once
            // headed for a monochrome panel.
            diffuse(canvas, x + 1, y, err, err, err, 7);
            diffuse(canvas, x - 1, y + 1, err, err, err, 3);
            diffuse(canvas, x, y + 1, err, err, err, 5);
            diffuse(canvas, x + 1, y + 1, err, err, err, 1);
        }
    }
}

}  // namespace dither

#pragma once

#include <cstdint>

#include "graphics/rgb_canvas.h"
#include "palette.h"

// Floyd-Steinberg error diffusion against the fixed 7-color ACeP palette -
// the same algorithm Pillow's `Image.quantize(dither=FLOYDSTEINBERG)` runs
// under the hood, which is what InkyPiZero relies on today (see README's
// "Dithering" section). Ported directly rather than reimplemented from a
// spec, since the reference behavior (weights 7/3/5/1 over 16, raster
// order) is standard and well-documented independent of Pillow's source.
namespace dither {

using EmitFn = void (*)(int16_t x, int16_t y, palette::Index index, void* ctx);

// Mutates `canvas` in place (diffused error is carried forward through the
// same buffer being quantized) and calls `emit` exactly once per pixel, in
// raster order, with the chosen palette index. See rgb_canvas.h for why this
// avoids needing a second full-frame buffer.
void floydSteinberg(RgbCanvas& canvas, EmitFn emit, void* ctx);

// Same algorithm, but quantizing to pure black/white via luminance instead
// of the 7-color palette - for monochrome panels (e.g. the Waveshare 7.5"
// V2, see WaveshareMonoDriver). Only ever emits palette::kBlack or
// palette::kWhite, so it's still compatible with any DisplaySink without
// that interface needing to know about a separate "mono" color type.
// Reuses the exact same canvas/widgets/icon rendering as the 7-color path -
// icons were baked against the color palette, not re-baked for mono, so
// they'll look flatter/less detailed here (their color information just
// collapses to whichever side of the luminance threshold it lands on) -
// an accepted simplification, not a bug.
void floydSteinbergMono(RgbCanvas& canvas, EmitFn emit, void* ctx);

}  // namespace dither

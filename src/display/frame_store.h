#pragma once

#include <cstddef>

#include "display/display_sink.h"

// Always-populated copy of the last dithered frame, saved to LittleFS
// regardless of which DisplaySink (EpdDriver/SerialDumpDriver) is actually
// driving hardware - so the setup/debug web portal's /image route always
// has something to show. Same packed-4bpp format as SerialDumpDriver (see
// that file's header comment for why).
class FrameStore : public DisplaySink {
   public:
    bool begin() override;
    void setPixel(int16_t x, int16_t y, palette::Index color) override;
    void present() override;  // writes the packed buffer to LittleFS

   private:
    uint8_t* buf_ = nullptr;
};

namespace frame_store {

// Reads the last saved frame and renders it as an uncomplicated 24-bit BMP
// (no compression, trivial for any browser to display) into a
// heap_caps_malloc'd (MALLOC_CAP_SPIRAM) buffer the caller owns and must
// free with heap_caps_free(). Returns false (leaving *outBuf null) if no
// frame has been saved yet or the read fails.
bool renderBmp(uint8_t** outBuf, size_t* outLen);

}  // namespace frame_store

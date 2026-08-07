// Ported from Pimoroni's `inky/inky_ac073tc1a.py` (MIT License, Copyright
// (c) 2018 Pimoroni Ltd - see docs/attribution.md), commit current as of
// 2026-08. Command bytes, init sequence values, and timing constants are
// copied as-is from that source, not derived from a datasheet - this is a
// faithful port of a working reference implementation, not new reverse
// engineering.
//
// HIGHEST-RISK UNVERIFIED ASSUMPTION: BUSY pin polarity. Pimoroni's driver
// configures the BUSY GPIO line for gpiod `Edge.RISING` detection and waits
// for that edge, which means their driver treats BUSY as LOW-while-busy,
// HIGH-when-ready. busyWait() below polls on that same assumption
// (`while (digitalRead(BUSY) == LOW) ...`). If a real panel turns out to be
// the opposite polarity, this will either hang for the full timeout on
// every operation (safe but broken) or - worse - proceed past busyWait()
// while the panel is still mid-refresh (not safe). Confirm this first
// against a real board, e.g. by logging the BUSY pin's raw level right
// after power-on/reset, before trusting this driver with real hardware.
//
// Everything else here (init command sequence, reset pulse timing, SPI CS/DC
// toggling per phase, 4bpp pixel packing) is copied 1:1 from the Python
// reference and cross-referenced against it in the comments below.
#include "inky_impression_driver.h"

#include <Arduino.h>
#include <SPI.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "layout.h"

namespace {

constexpr int kW = layout::kCanvasW;
constexpr int kH = layout::kCanvasH;
constexpr size_t kPackedBytes = static_cast<size_t>(kW) * kH / 2;

// AC073TC1 command bytes - names and values match inky_ac073tc1a.py exactly.
constexpr uint8_t kPSR = 0x00;
constexpr uint8_t kPWR = 0x01;
constexpr uint8_t kPOF = 0x02;
constexpr uint8_t kPOFS = 0x03;
constexpr uint8_t kPON = 0x04;
constexpr uint8_t kBTST1 = 0x05;
constexpr uint8_t kBTST2 = 0x06;
constexpr uint8_t kBTST3 = 0x08;
constexpr uint8_t kDTM = 0x10;
constexpr uint8_t kDRF = 0x12;
constexpr uint8_t kIPC = 0x13;
constexpr uint8_t kPLL = 0x30;
constexpr uint8_t kTSE = 0x41;
constexpr uint8_t kCDI = 0x50;
constexpr uint8_t kTCON = 0x60;
constexpr uint8_t kTRES = 0x61;
constexpr uint8_t kVDCS = 0x82;
constexpr uint8_t kT_VDCS = 0x84;
constexpr uint8_t kAGID = 0x86;
constexpr uint8_t kCMDH = 0xAA;
constexpr uint8_t kCCSET = 0xE0;
constexpr uint8_t kPWS = 0xE3;
constexpr uint8_t kTSSET = 0xE6;

constexpr uint32_t kSpiHz = 5000000;  // matches spidev's max_speed_hz = 5000000

void csAssert() { digitalWrite(pins::kEpdCs, LOW); }
void csRelease() { digitalWrite(pins::kEpdCs, HIGH); }
void dcCommand() { digitalWrite(pins::kEpdDc, LOW); }
void dcData() { digitalWrite(pins::kEpdDc, HIGH); }

// Mirrors _spi_write(): asserts CS, holds it for the whole burst, releases -
// called once for the command byte and, separately, once for its data (if
// any), matching Pimoroni's _send_command()/_send_data() split rather than
// holding CS low across both phases.
void spiWrite(bool isData, const uint8_t* data, size_t len) {
    if (isData) dcData(); else dcCommand();
    csAssert();
    SPI.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
    for (size_t i = 0; i < len; ++i) SPI.transfer(data[i]);
    SPI.endTransaction();
    csRelease();
}

void sendCommand(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0) {
    spiWrite(false, &cmd, 1);
    if (data && len) spiWrite(true, data, len);
}

// BUSY is LOW while busy, HIGH when ready - see the top-of-file risk note.
bool busyWait(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (digitalRead(pins::kEpdBusy) == LOW) {
        if (millis() - start > timeoutMs) return false;
        delay(1);
    }
    return true;
}

// Mirrors setup()'s double reset pulse (100ms low, 100ms high, twice) then
// a busy wait, run once per update cycle just like the Python reference
// (which calls setup() at the top of every _update()).
void resetAndInit() {
    pinMode(pins::kEpdRst, OUTPUT);
    digitalWrite(pins::kEpdRst, LOW);
    delay(100);
    digitalWrite(pins::kEpdRst, HIGH);
    delay(100);
    digitalWrite(pins::kEpdRst, LOW);
    delay(100);
    digitalWrite(pins::kEpdRst, HIGH);
    busyWait(1000);

    const uint8_t cmdh[] = {0x49, 0x55, 0x20, 0x08, 0x09, 0x18};
    sendCommand(kCMDH, cmdh, sizeof(cmdh));
    const uint8_t pwr[] = {0x3F, 0x00, 0x32, 0x2A, 0x0E, 0x2A};
    sendCommand(kPWR, pwr, sizeof(pwr));
    const uint8_t psr[] = {0x5F, 0x69};
    sendCommand(kPSR, psr, sizeof(psr));
    const uint8_t pofs[] = {0x00, 0x54, 0x00, 0x44};
    sendCommand(kPOFS, pofs, sizeof(pofs));
    const uint8_t btst1[] = {0x40, 0x1F, 0x1F, 0x2C};
    sendCommand(kBTST1, btst1, sizeof(btst1));
    const uint8_t btst2[] = {0x6F, 0x1F, 0x16, 0x25};
    sendCommand(kBTST2, btst2, sizeof(btst2));
    const uint8_t btst3[] = {0x6F, 0x1F, 0x1F, 0x22};
    sendCommand(kBTST3, btst3, sizeof(btst3));
    const uint8_t ipc[] = {0x00, 0x04};
    sendCommand(kIPC, ipc, sizeof(ipc));
    const uint8_t pll[] = {0x02};
    sendCommand(kPLL, pll, sizeof(pll));
    const uint8_t tse[] = {0x00};
    sendCommand(kTSE, tse, sizeof(tse));
    const uint8_t cdi[] = {0x3F};
    sendCommand(kCDI, cdi, sizeof(cdi));
    const uint8_t tcon[] = {0x02, 0x00};
    sendCommand(kTCON, tcon, sizeof(tcon));
    const uint8_t tres[] = {0x03, 0x20, 0x01, 0xE0};
    sendCommand(kTRES, tres, sizeof(tres));
    const uint8_t vdcs[] = {0x1E};
    sendCommand(kVDCS, vdcs, sizeof(vdcs));
    const uint8_t t_vdcs[] = {0x00};
    sendCommand(kT_VDCS, t_vdcs, sizeof(t_vdcs));
    const uint8_t agid[] = {0x00};
    sendCommand(kAGID, agid, sizeof(agid));
    const uint8_t pws[] = {0x2F};
    sendCommand(kPWS, pws, sizeof(pws));
    const uint8_t ccset[] = {0x00};
    sendCommand(kCCSET, ccset, sizeof(ccset));
    const uint8_t tsset[] = {0x00};
    sendCommand(kTSSET, tsset, sizeof(tsset));
}

}  // namespace

bool InkyImpressionDriver::begin() {
    pinMode(pins::kEpdCs, OUTPUT);
    pinMode(pins::kEpdDc, OUTPUT);
    pinMode(pins::kEpdBusy, INPUT);
    csRelease();
    SPI.begin(pins::kEpdSck, pins::kEpdMiso, pins::kEpdMosi, pins::kEpdCs);

    buf_ = static_cast<uint8_t*>(heap_caps_malloc(kPackedBytes, MALLOC_CAP_SPIRAM));
    return buf_ != nullptr;
}

void InkyImpressionDriver::setPixel(int16_t x, int16_t y, palette::Index color) {
    if (!buf_ || x < 0 || y < 0 || x >= kW || y >= kH) return;
    // Even/odd -> high/low nibble - matches inky_ac073tc1a.py's show():
    // `((buf[::2] << 4) & 0xF0) | (buf[1::2] & 0x0F)`.
    size_t pixelIndex = static_cast<size_t>(y) * kW + x;
    size_t byteIndex = pixelIndex / 2;
    if (pixelIndex % 2 == 0) {
        buf_[byteIndex] = (buf_[byteIndex] & 0x0F) | (static_cast<uint8_t>(color) << 4);
    } else {
        buf_[byteIndex] = (buf_[byteIndex] & 0xF0) | static_cast<uint8_t>(color);
    }
}

void InkyImpressionDriver::present() {
    if (!buf_) return;

    // _update() calls setup() (= resetAndInit here) at the start of every
    // update, not just once at power-on - matched here for the same reason:
    // this device deep-sleeps between cycles, so "once at startup" and
    // "once per cycle" are the same thing anyway.
    resetAndInit();

    sendCommand(kDTM, buf_, kPackedBytes);
    sendCommand(kPON);
    busyWait(400);

    const uint8_t drf[] = {0x00};
    sendCommand(kDRF, drf, sizeof(drf));
    busyWait(45000);  // ~41s measured in Pimoroni's own testing, per their comment

    const uint8_t pof[] = {0x00};
    sendCommand(kPOF, pof, sizeof(pof));
    busyWait(400);
}

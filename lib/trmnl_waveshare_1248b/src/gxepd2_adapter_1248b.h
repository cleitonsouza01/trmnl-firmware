// Phase 1 (MVP) adapter for the Waveshare 12.48" Module B (SKU 17299, 1304x984
// BWR, four onboard controllers M1/S1/M2/S2) driven by GxEPD2's GxEPD2_1248c
// driver class.
//
// Surface is intentionally tiny: init / width / height / sleep / powerOff
// plus an accessor to the underlying GxEPD2 instance for the PNG render
// path. Phase 2 will grow this with text / QR / MSG forwarding.
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_3C.h>
#include <epd3c/GxEPD2_1248c.h>

namespace trmnl {

// 400000 lets GxEPD2 buffer the full BWR frame in PSRAM in a single page:
//   2 planes (black + red) * 1304 * 984 / 8 = 320784 bytes.
// On a WROOM board (no PSRAM) GxEPD2 falls back to paged mode (~5 pages).
static constexpr unsigned long GXEPD2_1248B_MAX_DISPLAY_BUFFER_SIZE = 400000UL;

template <typename DRIVER>
inline constexpr int gxepd2_1248b_max_height() {
    // For 3-color, GxEPD2 allocates 2 * (WIDTH/8) bytes per row (B + R planes).
    return DRIVER::HEIGHT
        <= GXEPD2_1248B_MAX_DISPLAY_BUFFER_SIZE / (2UL * DRIVER::WIDTH / 8)
        ?  DRIVER::HEIGHT
        :  GXEPD2_1248B_MAX_DISPLAY_BUFFER_SIZE / (2UL * DRIVER::WIDTH / 8);
}

using GxEPD2_1248c_Full =
    GxEPD2_3C<GxEPD2_1248c, gxepd2_1248b_max_height<GxEPD2_1248c>()>;

class GxEPD2Adapter1248B {
public:
    // 14-pin constructor — first 12 pins map directly to GxEPD2_1248c's
    // constructor; the last 2 are the shared SPI bus.
    GxEPD2Adapter1248B(int8_t cs_m1, int8_t cs_s1, int8_t cs_m2, int8_t cs_s2,
                      int8_t dc1, int8_t dc2,
                      int8_t rst1, int8_t rst2,
                      int8_t busy_m1, int8_t busy_s1,
                      int8_t busy_m2, int8_t busy_s2,
                      int8_t sck, int8_t mosi);

    // ---- Phase 1 (MVP) surface ----
    bool init();
    int  width()  const;     // returns 1304
    int  height() const;     // returns 984
    void sleep();
    void powerOff();

    // Accessor for the PNG image-render path in display.cpp.
    GxEPD2_1248c_Full& gx();

private:
    GxEPD2_1248c_Full _gx;
    int8_t _sck;
    int8_t _mosi;
};

} // namespace trmnl

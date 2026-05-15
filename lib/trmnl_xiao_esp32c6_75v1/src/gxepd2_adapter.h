// Phase 1 (MVP) adapter for the Waveshare 7.5" V1 (SKU 13187, GDEW075T8)
// driven by GxEPD2's GxEPD2_750 driver class.
//
// Surface is intentionally tiny: init / width / height / sleep / powerOff
// plus an accessor to the underlying GxEPD2 instance for the PNG render
// path. Phase 2 will grow this with text / QR / MSG forwarding.
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>

namespace trmnl {

// 65 536 lets GxEPD2 buffer the full 640 x 384 / 8 = 30 720 byte frame in a
// single page — fastest refresh. We have ~250 KB free heap on the C6, so the
// 30 KB cost is comfortable.
static constexpr unsigned long GXEPD2_MAX_DISPLAY_BUFFER_SIZE = 65536UL;

template <typename DRIVER>
inline constexpr int gxepd2_max_height() {
    return DRIVER::HEIGHT
        <= GXEPD2_MAX_DISPLAY_BUFFER_SIZE / (DRIVER::WIDTH / 8)
        ?  DRIVER::HEIGHT
        :  GXEPD2_MAX_DISPLAY_BUFFER_SIZE / (DRIVER::WIDTH / 8);
}

using GxEPD2_750_Full =
    GxEPD2_BW<GxEPD2_750, gxepd2_max_height<GxEPD2_750>()>;

class GxEPD2Adapter {
public:
    GxEPD2Adapter(int cs, int dc, int rst, int busy, int sck, int mosi);

    // ---- Phase 1 (MVP) surface ----
    bool init();
    int  width()  const;
    int  height() const;
    void sleep();
    void powerOff();

    // Accessor for the PNG image-render path in display.cpp.
    GxEPD2_750_Full& gx();

private:
    GxEPD2_750_Full _gx;
    int _sck;
    int _mosi;
};

} // namespace trmnl

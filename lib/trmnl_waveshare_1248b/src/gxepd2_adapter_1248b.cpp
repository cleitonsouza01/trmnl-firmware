// ESP32-only translation unit. The native test env compiles with
// `framework =` empty, so this gate keeps GxEPD2 (Arduino-only) out of
// the native build entirely.
#if defined(ARDUINO_ARCH_ESP32)

#include "gxepd2_adapter_1248b.h"

namespace trmnl {

GxEPD2Adapter1248B::GxEPD2Adapter1248B(
    int8_t cs_m1, int8_t cs_s1, int8_t cs_m2, int8_t cs_s2,
    int8_t dc1, int8_t dc2,
    int8_t rst1, int8_t rst2,
    int8_t busy_m1, int8_t busy_s1,
    int8_t busy_m2, int8_t busy_s2,
    int8_t sck, int8_t mosi)
    // Use GxEPD2_1248c's 15-pin constructor (the one its source comment
    // calls out as "for Waveshare ESP32 driver board mounted on connection
    // board"). The 12-pin overload defaults _sck/_miso/_mosi to the
    // platform's SPI macros (VSPI 18/19/23 on ESP32), which means
    // GxEPD2::_initSPI() does a plain SPI.begin() and routes SPI to those
    // pins — colliding with our M2_BUSY (GPIO 18) and M1_CS (GPIO 23)
    // wiring and starving the panel of any actual SPI clock on GPIO 13.
    // Passing sck/mosi explicitly here lets GxEPD2 do SPI.begin(sck, miso,
    // mosi, cs_m1) internally and route SPI to our actual wiring.
    : _gx(GxEPD2_1248c(sck, /*miso*/ -1, mosi,
                       cs_m1, cs_s1, cs_m2, cs_s2,
                       dc1, dc2,
                       rst1, rst2,
                       busy_m1, busy_s1,
                       busy_m2, busy_s2)),
      _sck(sck),
      _mosi(mosi) {}

bool GxEPD2Adapter1248B::init() {
    // 115200 is the serial speed GxEPD2 uses for its optional debug prints;
    // matches the project's `monitor_speed`.
    _gx.init(115200);

    // GxEPD2's _initSPI() called SPI.begin(_sck, _miso, _mosi, _cs_m1),
    // passing M1_CS (GPIO 23) as the SPI hardware-SS pin. Arduino-ESP32's
    // SPI.begin() then enables hardware-managed SS, which makes the SPI
    // peripheral auto-toggle M1_CS on every transaction — even ones
    // targeted at S1/M2/S2. M1 ends up receiving data meant for the
    // other three controllers, its frame buffer overflows with garbage,
    // and the top-left quadrant renders blank.
    //
    // Re-init SPI here with ss = -1 so M1_CS is driven only by GxEPD2's
    // manual digitalWrite() calls. _initSPI() is only invoked again
    // during the init-time temperature read; once we override here, the
    // setting sticks for every refresh thereafter.
    SPI.end();
    SPI.begin(_sck, /*miso*/ -1, _mosi, /*ss*/ -1);
    return true;
}

int GxEPD2Adapter1248B::width()  const { return GxEPD2_1248c::WIDTH;  } // 1304
int GxEPD2Adapter1248B::height() const { return GxEPD2_1248c::HEIGHT; } // 984

void GxEPD2Adapter1248B::sleep()    { _gx.hibernate(); }
void GxEPD2Adapter1248B::powerOff() { _gx.powerOff();  }

GxEPD2_1248c_Full& GxEPD2Adapter1248B::gx() { return _gx; }

} // namespace trmnl

#endif // ARDUINO_ARCH_ESP32

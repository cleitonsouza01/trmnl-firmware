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
    : _gx(GxEPD2_1248c(cs_m1, cs_s1, cs_m2, cs_s2,
                       dc1, dc2,
                       rst1, rst2,
                       busy_m1, busy_s1,
                       busy_m2, busy_s2)),
      _sck(sck),
      _mosi(mosi) {}

bool GxEPD2Adapter1248B::init() {
    // Explicit SPI.begin so the bus is pinned to the Driver Board's stock
    // SCK/MOSI; survives any future board-default changes.
    SPI.begin(_sck, /*miso*/ -1, _mosi, /*ss*/ -1);

    // 115200 is the serial speed GxEPD2 uses for its optional debug prints;
    // matches the project's `monitor_speed`.
    _gx.init(115200);
    return true;
}

int GxEPD2Adapter1248B::width()  const { return GxEPD2_1248c::WIDTH;  } // 1304
int GxEPD2Adapter1248B::height() const { return GxEPD2_1248c::HEIGHT; } // 984

void GxEPD2Adapter1248B::sleep()    { _gx.hibernate(); }
void GxEPD2Adapter1248B::powerOff() { _gx.powerOff();  }

GxEPD2_1248c_Full& GxEPD2Adapter1248B::gx() { return _gx; }

} // namespace trmnl

#endif // ARDUINO_ARCH_ESP32

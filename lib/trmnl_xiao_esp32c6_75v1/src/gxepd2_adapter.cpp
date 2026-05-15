// ESP32-only translation unit. The native test env compiles with
// `framework =` empty, so this gate keeps GxEPD2 (Arduino-only) out of
// the native build entirely.
#if defined(ARDUINO_ARCH_ESP32)

#include "gxepd2_adapter.h"

namespace trmnl {

GxEPD2Adapter::GxEPD2Adapter(int cs, int dc, int rst, int busy,
                             int sck, int mosi)
    : _gx(GxEPD2_750(cs, dc, rst, busy)),
      _sck(sck),
      _mosi(mosi) {}

bool GxEPD2Adapter::init() {
    // The XIAO ESP32-C6's default SPI bus maps SCK = GPIO19 / MOSI = GPIO18,
    // which already matches the spec's pin assignment. We still call
    // SPI.begin() explicitly with our pin numbers so the call is
    // self-documenting and survives any future board-default changes.
    SPI.begin(_sck, /*miso*/ -1, _mosi, /*ss*/ -1);

    // 115200 is the serial speed GxEPD2 uses for its optional debug prints;
    // matches the project's `monitor_speed`.
    _gx.init(115200);
    return true;
}

int GxEPD2Adapter::width()  const { return GxEPD2_750::WIDTH;  }   // 640
int GxEPD2Adapter::height() const { return GxEPD2_750::HEIGHT; }   // 384

void GxEPD2Adapter::sleep()    { _gx.hibernate(); }
void GxEPD2Adapter::powerOff() { _gx.powerOff();  }

GxEPD2_750_Full& GxEPD2Adapter::gx() { return _gx; }

} // namespace trmnl

#endif // ARDUINO_ARCH_ESP32

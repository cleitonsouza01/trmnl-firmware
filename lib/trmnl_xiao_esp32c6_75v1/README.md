# trmnl_xiao_esp32c6_75v1

GxEPD2-based driver adapter for the Waveshare 7.5" V1 (SKU 13187, panel GDEW075T8, IC UC8159c / IL0371; 640×384 B/W) on a Seeed XIAO ESP32-C6.

## Status

Phase 1 (MVP). Provides:

- `trmnl::GxEPD2Adapter` — small wrapper around `GxEPD2_BW<GxEPD2_750, ...>` exposing `init() / width() / height() / sleep() / powerOff()` plus an accessor to the underlying GxEPD2 instance for the PNG image-render path.

Phase 2 (separate spec) will layer in text/QR/MSG rendering by forwarding to GxEPD2's Adafruit_GFX surface.

## Wiring (XIAO ESP32-C6 → Waveshare 7.5" V1)

| Panel       | XIAO pad | GPIO |
|-------------|----------|------|
| VCC         | 3V3      | —    |
| GND         | GND      | —    |
| DIN (MOSI)  | D10      | 18   |
| CLK (SCK)   | D8       | 19   |
| CS          | D3       | 21   |
| DC          | D6       | 16   |
| RST         | D7       | 17   |
| BUSY        | D2       |  2   |

I²C pads D4 (SDA) / D5 (SCL) are intentionally left free for future peripherals.

## Build

```
pio run -e xiao_esp32c6_75v1 -t upload
```

(The env block is in the project's root `platformio.ini`.)

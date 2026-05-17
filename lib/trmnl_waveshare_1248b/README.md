# trmnl_waveshare_1248b

GxEPD2-based driver adapter for the Waveshare 12.48" e-Paper Module B (SKU 17299; 1304×984 black/white/red, four onboard controllers) on the Waveshare ESP32 Driver Board (SKU 15823, **WROVER variant — PSRAM required**).

## Status

Phase 1 (MVP). Provides:

- `trmnl::GxEPD2Adapter1248B` — small wrapper around `GxEPD2_3C<GxEPD2_1248c, …>` exposing `init() / width() / height() / sleep() / powerOff()` plus an accessor to the underlying GxEPD2 instance for the PNG image-render path.

Phase 2 (separate spec) will layer in text/QR/MSG rendering by forwarding to GxEPD2's Adafruit_GFX surface.

## Wiring (Waveshare ESP32 Driver Board → 12.48" Module B)

| Panel signal | ESP32 GPIO | Notes                              |
|--------------|------------|------------------------------------|
| VCC          | 3V3        | < 50 mA average                    |
| GND          | GND        |                                    |
| DIN  (MOSI)  | 14         |                                    |
| CLK  (SCK)   | 13         |                                    |
| M1_CS        | 23         | top-left controller chip select    |
| S1_CS        | 22         | top-right controller chip select   |
| M2_CS        | 16         | bottom-left controller chip select |
| S2_CS        | 19         | bottom-right controller chip select|
| M1S1_DC      | 25         | top half data/command select       |
| M2S2_DC      | 17         | bottom half data/command select    |
| M1S1_RST     | 33         | top half reset                     |
| M2S2_RST     | 5          | bottom half reset                  |
| M1_BUSY      | 32         | top-left busy                      |
| S1_BUSY      | 26         | top-right busy                     |
| M2_BUSY      | 18         | bottom-left busy                   |
| S2_BUSY      | 4          | bottom-right busy                  |

The Waveshare ESP32 Driver Board's flat-flex socket for the panel is pre-wired to these pins via the on-board level shifter — no manual jumpers needed when using the stock cable.

## Hardware variant check

Some lots of the Waveshare ESP32 Driver Board ship with ESP32-WROOM-32 (no PSRAM); this env is built for the ESP32-WROVER-B variant (4 MB PSRAM) because the full BWR frame buffer is ~320 KB. The implementation logs `esp_psram_get_size()` during `display_init`; if it reports 0, the board is WROOM and rendering will either fall back to slow paged mode or run out of heap.

## Build

```
pio run -e waveshare_1248b -t upload
```

(The env block is in the project's root `platformio.ini`.)

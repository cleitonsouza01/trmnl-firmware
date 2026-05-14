# Seeed XIAO ESP32-C6 + Waveshare 7.5" V1 (SKU 13187) — MVP design

**Status:** Approved (brainstorming) — pending implementation plan
**Date:** 2026-05-14
**Target hardware:** Seeed Studio XIAO ESP32-C6 + Waveshare 7.5" V1 e-paper raw display (SKU 13187, part number 7.5inch e-Paper, panel GDEW075T8, IC UC8159c / IL0371, 640×384, B/W)
**Scope:** MVP — fetch + render only. Captive portal UX, on-panel error screens, BMP support, fonts, G5 images → deferred to a Phase 2 spec.

## 1. Goal

Add a new `[env:xiao_esp32c6_75v1]` build to `trmnl-firmware` that brings up the Seeed XIAO ESP32-C6 driving the Waveshare 7.5" V1 (640×384 B/W) panel. MVP behavior: boot → connect to a configured Wi-Fi → call `/api/setup` (if not already paired) → call `/api/display` → download the PNG image → render to the panel → deep-sleep. The existing TRMNL captive portal SoftAP + web UI continue to serve (the user can still configure Wi-Fi credentials by joining `TRMNL-xxxx` and visiting the portal), but `display_show_msg(...)` paths render nothing on the panel in Phase 1 — they log a warning instead.

The full TRMNL UX (on-panel captive-portal QR + setup instructions, on-panel error messages, friendly_id rendering, BMP support, font rendering) is layered in by a separate **Phase 2** spec once MVP is proven on real hardware.

## 2. Non-goals (Phase 1)

- On-panel captive-portal QR or "connect to TRMNL_xxxx" instructions.
- On-panel error/setup screens (`MSG_API_ERROR`, `WIFI_FAILED`, `READY_TO_SHIP`, …).
- Font rendering (text via `bbep.print` / `Paint_DrawMultilineText` / `display_show_msg_*`).
- G5-compressed image rendering (`bbep.loadG5Image`).
- BMP rendering (PNG only).
- Battery-voltage sensing (the XIAO C6 has no battery management onboard; `FAKE_BATTERY_VOLTAGE` is set).
- Light-sleep optimisations (this board uses `esp_deep_sleep` between refreshes — same flow as the smaller existing TRMNL boards).
- Other ESP32-C6 boards (Espressif DevKitC, Olimex, etc.).
- Other 640×384 panel variants (`EP74R_640x384` BWR, the 7.5" V1 B/W is the only one in scope).

## 3. Background

### 3.1 Why this work

The user has a XIAO ESP32-C6 + 7.5" V1 panel and wants TRMNL on it. The TRMNL firmware already supports several XIAO variants (`BOARD_SEEED_XIAO_ESP32C3`, `BOARD_SEEED_XIAO_ESP32S3`) and the 7.5" V2 (800×480) via bb_epaper, but **not the C6 board and not the 7.5" V1 panel** (`bb_epaper`'s panel table has only `EP74R_640x384` for 3-color BWR and `EP41_640x400` for a different chip — no BW 640×384 V1 entry).

### 3.2 Why GxEPD2 instead of porting Waveshare's C reference

The user found a working third-party reference at `/Users/cleiton/projects/eletronics/trmnl-custom-firmware-waveshare-esp32-epd/` (a hobbyist TRMNL firmware targeting this exact panel). It uses **Jean-Marc Zingg's GxEPD2 library** (`zinggjm/GxEPD2`), specifically the `GxEPD2_750` driver class for `GDEW075T8 / UC8159c (IL0371) / WF0583CZ09` (the V1 panel we have). The library handles the init/refresh/sleep sequences out of the box and uses a paged-drawing model that fits comfortably in the XIAO C6's 512 KB SRAM without PSRAM.

GxEPD2 replaces what would otherwise have been a 1–2 day port of Waveshare's reference C source into a new in-tree driver. It adds a third e-paper library to the project (alongside `bb_epaper` for most boards and `FastEPD` for X-class), but is `lib_deps`-gated to this env only.

### 3.3 Hardware: XIAO ESP32-C6 vs the ESP32-WROOM-32 the 12.48" branch uses

The XIAO ESP32-C6 has a different shape than the WROOM-32:

| | XIAO ESP32-C6 | ESP32-WROOM-32 (Waveshare Driver Board) |
|---|---|---|
| Architecture | RISC-V single-core @ 160 MHz | Xtensa LX6 dual-core @ 240 MHz |
| Internal SRAM | 512 KB | 320 KB |
| External PSRAM | none | none |
| Flash | 4 MB | 4 MB |
| Wi-Fi | WiFi 6 (802.11ax) | WiFi 4 (802.11n) |
| GPIO pads broken out | 11 (D0–D10) + GPIO 9 boot button | many (full ESP32 GPIO surface) |
| USB | native USB-C with USB-CDC | requires CP2102 USB-UART chip |
| PlatformIO platform | needs **pioarduino fork** ≥ 5.5.x for C6 support | base `espressif32@6.12.0` works |
| SPI: SCK / MOSI pins | D8 (GPIO 19) / D10 (GPIO 18) — default, no remapping | non-standard, requires HSPI remap (`hspi.begin(13, 12, 14, 15)`) |

The XIAO C6's larger internal SRAM is the lever that lets us run GxEPD2's full-frame paged buffer (~30 KB for 640×384 BW) without PSRAM. This wouldn't help on the 12.48" (~313 KB needed) but is plenty for the 7.5" V1.

## 4. Approved decisions (from brainstorming)

| # | Decision |
|---|---|
| 1 | Driver library: **GxEPD2** (`zinggjm/GxEPD2@^1.6.5`), class `GxEPD2_750` for the GDEW075T8 panel. |
| 2 | Scope: **MVP** — fetch + render only. Captive portal UX + on-panel MSG screens deferred to Phase 2. |
| 3 | Architecture: **small `GxEPD2Adapter` class** (Phase 1 surface: `init`, `width`, `height`, `sleep`, `powerOff`, plus an accessor to the underlying GxEPD2 instance for the image-render path) + a new `#elif defined(BOARD_XIAO_ESP32C6_75V1)` branch in `src/display.cpp` that drives `firstPage()/nextPage()/drawPixel` directly during PNG decode. |
| 4 | Image format: **PNG only** in Phase 1. If the server returns BMP, log a warning and skip rendering. |
| 5 | Pin map (user-supplied, fixed): SCK=D8/GPIO 19, MOSI=D10/GPIO 18, CS=D3/GPIO 21, DC=D6/GPIO 16, RST=D7/GPIO 17, BUSY=D2/GPIO 2; preserves I²C pads D4 (SDA) / D5 (SCL) for future peripherals. `PIN_INTERRUPT` = GPIO 9 (XIAO boot button). |
| 6 | Build: pioarduino platform fork (release `55.03.37`), `board = seeed_xiao_esp32c6`, `littlefs` filesystem, GxEPD2 in `lib_deps`, `bb_epaper` in `lib_ignore` for this env. |
| 7 | `MSG` paths: logged no-op in Phase 1. Each call logs `[W] display_show_msg(...) called on xiao_c6_75v1 — UX not implemented in Phase 1`. |
| 8 | The existing TRMNL captive-portal SoftAP + web UI continue to serve. The user configures Wi-Fi by joining `TRMNL-xxxx` and visiting the portal IP from their phone — there is no on-panel QR or instructions in Phase 1. |

## 5. Architecture

### 5.1 File structure

```
lib/trmnl_xiao_esp32c6_75v1/
├── library.properties                  metadata only
├── README.md                           describes the lib + flashing
└── src/
    └── gxepd2_adapter.{h,cpp}          small adapter (Phase 1: ~80 LoC of stubs;
                                        Phase 2: grows to expose text/QR via
                                        Adafruit_GFX forwarding)

src/
├── display.cpp                         new #elif BOARD_XIAO_ESP32C6_75V1 branch
└── DEV_Config.h                        new pin block

include/config.h                        new BOARD_XIAO_ESP32C6_75V1 branch

platformio.ini                          new [env:xiao_esp32c6_75v1]
```

No new test directories — there is no pure-logic unit worth native-testing in this MVP (GxEPD2 is 3rd-party; the adapter is mostly pass-through; the PNG-to-`drawPixel` callback is a verbatim 20-line copy from the reference firmware).

### 5.2 `GxEPD2Adapter` (the small adapter)

`lib/trmnl_xiao_esp32c6_75v1/src/gxepd2_adapter.h`:

```cpp
// ESP32-only translation unit; see ARDUINO_ARCH_ESP32 gate in .cpp.
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>

namespace trmnl {

// 65 536 lets GxEPD2 buffer the full 640×384/8 = 30 720 bytes in one page —
// fastest refresh. Can drop to 16 384 in Phase 2 if RAM pressure shows up.
static constexpr unsigned long MAX_DISPLAY_BUFFER_SIZE = 65536UL;

template <typename DRIVER>
inline constexpr int MAX_HEIGHT() {
    return DRIVER::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (DRIVER::WIDTH / 8)
         ? DRIVER::HEIGHT
         : MAX_DISPLAY_BUFFER_SIZE / (DRIVER::WIDTH / 8);
}

class GxEPD2Adapter {
public:
    GxEPD2Adapter(int cs, int dc, int rst, int busy, int sck, int mosi);

    // ---- Phase 1 (MVP) ----
    bool init();
    int  width()  const;
    int  height() const;
    void sleep();
    void powerOff();

    // Underlying GxEPD2 instance for the PNG-decode/drawPixel path.
    GxEPD2_BW<GxEPD2_750, MAX_HEIGHT<GxEPD2_750>()>& gx();

    // ---- Phase 2 placeholder ----
    // Future surface for text/QR/MSG rendering will be added here; not in MVP.

private:
    GxEPD2_BW<GxEPD2_750, MAX_HEIGHT<GxEPD2_750>()> _gx;
    int _sck, _mosi;
};

} // namespace trmnl
```

Implementation (~80 LoC):
- Constructor passes `cs/dc/rst/busy` to GxEPD2's `GxEPD2_750(cs, dc, rst, busy)` constructor.
- `init()` calls `SPI.begin(_sck, /*miso*/ -1, _mosi, /*ss*/ -1)` (uses default SPI bus — no HSPI remap needed for the XIAO C6, unlike the Waveshare ESP32 Driver Board), then `_gx.init(115200)` (the int is the serial speed for GxEPD2's optional debug; we pass the project's standard speed).
- `width()`/`height()` forward to the GxEPD2 template parameters: returns 640 / 384.
- `sleep()` calls `_gx.hibernate()` (GxEPD2's deep-sleep equivalent).
- `powerOff()` calls `_gx.powerOff()` (lower-power than hibernate; turns off the panel's power but keeps the controller responsive).
- `gx()` returns a reference for the image-render path.

ESP32-only TU — wrap the body of `gxepd2_adapter.cpp` in `#if defined(ARDUINO_ARCH_ESP32) ... #endif` so the native test env doesn't try to compile GxEPD2.

### 5.3 `display.cpp` `BOARD_XIAO_ESP32C6_75V1` branch

A new `#elif defined(BOARD_XIAO_ESP32C6_75V1)` arm (sibling to the existing bb_epaper and `BOARD_X_CLASS` arms). It does **not** define `BB_EPAPER` or `BOARD_X_CLASS`, so existing code paths under those macros are skipped for this board. A new macro `GXEPD2_DISPLAY` is defined so any later cross-cutting branches can target this code path.

```cpp
#elif defined(BOARD_XIAO_ESP32C6_75V1)
#include "gxepd2_adapter.h"
#define GXEPD2_DISPLAY 1

static trmnl::GxEPD2Adapter g_adapter(
    EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN,
    EPD_SCK_PIN, EPD_MOSI_PIN
);
```

The `display.h` public functions are reimplemented for this board:

| Function | Phase 1 behavior |
|---|---|
| `display_init()` | `g_adapter.init();` |
| `display_width()` / `display_height()` | forward to `g_adapter.width()` / `height()` (returns 640 / 384) |
| `display_show_image(buffer, size, wait)` | PNG decode + paged render — see §6 |
| `display_show_msg(...)` (all overloads) | `Log.warning("display_show_msg(%d) on xiao_c6_75v1 — UX not implemented in Phase 1", (int)message_type); return;` |
| `display_show_msg_api(...)` | same logged no-op |
| `display_show_msg_qa(...)` | same logged no-op |
| `display_show_battery(float)` | same logged no-op |
| `display_reset()` | `g_adapter.init();` (GxEPD2's init does a hard reset) |
| `display_sleep()` | `g_adapter.sleep();` |
| `display_set_light_sleep(uint8_t)` | no-op — this board uses `esp_deep_sleep` between refreshes |
| `Paint_DrawMultilineText(...)` | logged no-op (Phase 2) |

### 5.4 Image-render flow (the one path that actually matters for MVP)

```
api-client/display.cpp                                 (unchanged for this board)
   downloads PNG into a heap buffer (existing TRMNL flow)
        │
        ▼
display.cpp::display_show_image(buffer, size, wait)    (new branch)
   check magic bytes → if BMP, log a warning + skip; if PNG, continue
   adapter.gx().setFullWindow();
   adapter.gx().firstPage();
   do {
       adapter.gx().fillScreen(GxEPD_WHITE);
       decode_png_into_adapter(buffer, size);          // local helper
   } while (adapter.gx().nextPage());
   adapter.powerOff();
```

`decode_png_into_adapter(buffer, size)` is a small file-local helper inside the `#elif` arm:

```cpp
static PNG s_png;   // file-static; PNGdec state

static int pngDraw(PNGDRAW* pDraw) {
    const uint16_t y    = pDraw->y;
    const uint8_t* line = pDraw->pPixels;
    for (uint16_t x = 0; x < pDraw->iWidth; ++x) {
        const uint8_t byte_idx = x / 8;
        const uint8_t bit_idx  = 7 - (x % 8);
        const uint8_t pixel    = (line[byte_idx] >> bit_idx) & 0x01;
        // PNG: 0 = black, 1 = white; GxEPD2: same convention.
        g_adapter.gx().drawPixel(x, y, pixel ? GxEPD_WHITE : GxEPD_BLACK);
    }
    return 1;
}

static bool decode_png_into_adapter(const uint8_t* buf, int len) {
    int rc = s_png.openRAM(const_cast<uint8_t*>(buf), len, pngDraw);
    if (rc != PNG_SUCCESS) {
        Log.errorln(F("xiao_c6_75v1: PNG open failed rc=%d"), rc);
        return false;
    }
    rc = s_png.decode(nullptr, 0);
    s_png.close();
    if (rc != PNG_SUCCESS) {
        Log.errorln(F("xiao_c6_75v1: PNG decode failed rc=%d"), rc);
        return false;
    }
    return true;
}
```

Per-pixel `drawPixel` is slow (~245k calls per page) but works. With `MAX_DISPLAY_BUFFER_SIZE = 65536`, the whole frame fits in one page, so the loop runs exactly once. Total render time including SPI transfer: ~3–5 seconds (e-paper refresh itself takes another ~4 seconds). Comparable to other TRMNL boards.

### 5.5 What does **not** change

- `src/main.cpp` flow (boot → wifi → setup → display → sleep).
- `lib/wificaptive` — captive-portal SoftAP and web UI still serve. The user configures Wi-Fi by joining `TRMNL-<friendly_id>` and visiting the portal page; there is no on-panel guidance in Phase 1 (the panel will be blank from boot until the first `/api/display` succeeds).
- `src/api-client/setup.cpp`, `src/api-client/display.cpp`, `src/api-client/submit_log.cpp`.
- OTA path.
- All other boards' build behavior.

## 6. Data flow

The TRMNL flow for this board is unchanged from other small boards (TRMNL OG, Waveshare ESP32 Driver Board). The only board-specific work is in `display_show_image()`'s body. End-to-end:

```
boot (cold or wake-from-deep-sleep)
   ↓
preferences load (existing) → wifi connect (existing)
   ↓
[first-time only] /api/setup → store api_key + friendly_id
   ↓
/api/display → JSON envelope with image_url + refresh_rate
   ↓
download PNG into heap buffer (existing api-client)
   ↓
display_show_image(buffer, size, true)              ← new branch in display.cpp
   ↓
adapter.gx().setFullWindow() → firstPage()
  decode_png_into_adapter (PNGdec → pngDraw → drawPixel per pixel)
   ↓
adapter.gx().nextPage() loop (1 iteration with MAX_DISPLAY_BUFFER_SIZE=65536)
   ↓
adapter.powerOff()
   ↓
esp_deep_sleep_start() for refresh_rate seconds
   ↓
(wake by timer or by GPIO 9 button)
```

### Memory budget during a display update

| Component | Bytes |
|---|---|
| GxEPD2 full-frame buffer (640 × 384 / 8) | 30 720 |
| PNGdec workspace (`PNG_MAX_BUFFERED_PIXELS=6432`) | ~6 500 |
| Downloaded PNG buffer (typical TRMNL image) | ~30 000 |
| WiFi + TLS + HTTPClient (existing) | ~16 000 |
| **Total display-update overhead** | **~83 KB** |

XIAO ESP32-C6 has ~250 KB free heap after boot. Comfortable margin — leaves ~165 KB for Phase 2's QR encoder, font tables, etc.

## 7. Build configuration

### 7.1 PlatformIO env

```ini
[env:xiao_esp32c6_75v1]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/platform-espressif32.zip
board = seeed_xiao_esp32c6
framework = arduino
monitor_speed = 115200
upload_speed = 460800
board_build.f_cpu = 160000000L
board_build.f_flash = 80000000L
board_build.flash_mode = qio
board_build.partitions = min_spiffs.csv
board_build.filesystem = littlefs
build_flags =
    ${env:esp32_base.build_flags}
    -D BOARD_XIAO_ESP32C6_75V1
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=1
    -D PNG_MAX_BUFFERED_PIXELS=6432
lib_deps =
    ${deps_app.lib_deps}
    zinggjm/GxEPD2@^1.6.5
lib_ignore =
    BMA530_SensorAPI
    IQS323
    trmnl_x
    BQ27427
    bb_epaper
```

Notes:
- The **pioarduino fork** (already used by the X-class envs in this project, e.g. `[env:TRMNL_X_EPDIY]` on line 209) provides ESP32-C6 support that `espressif32@6.12.0` does not yet have. Release `55.03.37` matches what newer X-class envs use.
- `board_build.f_cpu = 160000000L` — the C6's RISC-V core runs at 160 MHz (not 240 MHz like the Xtensa cores).
- `bb_epaper` is in `lib_ignore` for this env — it would otherwise be pulled in by `deps_common` and bloat the image with unused code.
- `lib_deps` inherits `${deps_app.lib_deps}` so we keep ArduinoJson, PNGdec, JPEGDEC, ArduinoLog, WiFi captive portal, etc.

### 7.2 Pin definitions (`src/DEV_Config.h`)

A new `#elif` block before the existing `#elif defined (BOARD_X_CLASS)`:

```c
#elif defined(BOARD_XIAO_ESP32C6_75V1)
   // Pin definition for Seeed XIAO ESP32-C6 + Waveshare 7.5" V1 (SKU 13187, GDEW075T8)
   // Panel is driven via GxEPD2's GxEPD2_750 class on the C6's default SPI bus.
   #define EPD_SCK_PIN   19    // D8 — default SPI SCK
   #define EPD_MOSI_PIN  18    // D10 — default SPI MOSI
   #define EPD_CS_PIN    21    // D3
   #define EPD_DC_PIN    16    // D6
   #define EPD_RST_PIN   17    // D7
   #define EPD_BUSY_PIN   2    // D2
```

### 7.3 Config (`include/config.h`)

A new `#elif` after the existing `BOARD_WAVESHARE_397` branch:

```c
#elif defined(BOARD_XIAO_ESP32C6_75V1)
#define PIN_INTERRUPT 9         // XIAO ESP32-C6 internal boot button
#define DEVICE_MODEL "xiao_c6_75v1"
#define FAKE_BATTERY_VOLTAGE
```

`MAX_IMAGE_SIZE` stays at the default `90000` from the existing `#else` arm — a 640×384 1-bit BMP is ~31 KB and the typical TRMNL PNG much less; 90 KB is plenty.

### 7.4 Library skeleton (`lib/trmnl_xiao_esp32c6_75v1/library.properties`)

```properties
name=trmnl_xiao_esp32c6_75v1
version=0.1.0
author=TRMNL
maintainer=TRMNL
sentence=GxEPD2-based driver adapter for the Waveshare 7.5" V1 (SKU 13187) on a Seeed XIAO ESP32-C6.
paragraph=Phase 1 (MVP): exposes init/width/height/sleep + access to the underlying GxEPD2 instance for the PNG image-render path in display.cpp. Phase 2 layers in text/QR/MSG rendering by forwarding to GxEPD2's Adafruit_GFX surface.
category=Display
architectures=esp32
depends=GxEPD2
```

### 7.5 Wiring summary (for the README + user)

```
Panel        XIAO pad     Notes
─────────    ────────     ────────────────────────────
VCC          3V3          ~25 mA peak; XIAO's regulator handles it
GND          GND
DIN (MOSI)   D10          GPIO 18 — default SPI MOSI
CLK (SCK)    D8           GPIO 19 — default SPI SCK
CS           D3           GPIO 21
DC           D6           GPIO 16
RST          D7           GPIO 17
BUSY         D2           GPIO 2
```

Free pads after wiring: D0, D1, D4, D5, D9. D4 (SDA) / D5 (SCL) preserved for future I²C peripherals.

## 8. Error handling

| Failure | Detection | Phase 1 response |
|---|---|---|
| Wi-Fi never connects | existing TRMNL connect-retry | existing flow: log + deep-sleep with backoff; `display_show_msg(WIFI_FAILED)` calls log + no-op |
| `/api/setup` non-200 | existing api-client | existing flow: deep-sleep with retry; MSG call logged + no-op |
| `/api/display` non-200 or timeout | existing api-client | same |
| PNG download fails | existing api-client | same |
| PNG decode error (`png.openRAM` or `png.decode` non-`PNG_SUCCESS`) | new helper in §5.4 | `display_show_image` returns; serial logs the PNGdec error code; panel keeps whatever was previously displayed (e-paper is persistent); deep-sleep normally so the next cycle retries |
| GxEPD2 init silently fails (no panel detected) | no return code from `display.init()`; manifests as a blank panel | log on serial during init; continue to deep-sleep; next cycle retries init |
| Server returns BMP instead of PNG | magic-byte check in `display_show_image` (first 8 bytes: `\x89PNG\r\n\x1a\n` for PNG; `BM` for BMP) | log `[W] xiao_c6_75v1: BMP not supported in Phase 1; skipping render`; continue to sleep |
| GPIO 9 boot button pressed during sleep | normal flow | wake → re-enter `setup()` — same as other TRMNL boards |

The single new failure mode this board introduces is the **PNG-only constraint**. If the user's TRMNL server returns BMP for some reason (self-hosted servers can be configured for either; the SaaS backend's exact format depends on the device pairing), the user sees a blank or stale panel and a warning in serial. Phase 2 will add BMP support.

There is no on-panel error UX in Phase 1. All `display_show_msg(...)` paths log a warning and return without touching the panel. This is intentional and documented per the Phase 2 spec.

## 9. Testing

### 9.1 Native unit tests

Nothing new. The MVP touches:
- GxEPD2 (3rd-party — not our test target).
- A thin adapter (mostly pass-through; not worth its own native test).
- A 20-line PNG-to-`drawPixel` callback (verbatim from the reference firmware; native-testable would require mocking GxEPD2's `drawPixel`, which is more test scaffolding than the SUT).

The existing native test suite (37 tests, including `test_quadrant_splitter`) continues to run unaffected because the new lib's source is ESP32-gated via `#if defined(ARDUINO_ARCH_ESP32)` and the env is `lib_ignore`d under native via the existing pattern.

### 9.2 On-device smoke

No separate smoke sketch — the full firmware *is* the smoke test for an MVP this small. Procedure:

1. Wire the panel per §7.5.
2. `pio run -e xiao_esp32c6_75v1 -t upload`.
3. From a phone, find the SSID `TRMNL-<friendly_id>` in available Wi-Fi networks. Connect.
4. Open `http://192.168.4.1` (the captive-portal IP). Enter home Wi-Fi credentials. Submit. Device reboots.
5. Device hits `/api/setup` → `/api/display` → downloads PNG → renders → deep-sleeps.
6. **Visual check:** dashboard image appears on the panel within ~30–40 s of the WiFi-config reboot. No obvious artifacts, no inverted pixels (PNG black/white maps correctly to `GxEPD_BLACK`/`GxEPD_WHITE`).
7. Press the boot button (GPIO 9): device wakes, re-fetches, re-renders.

### 9.3 Build-sanity for other envs

After landing this work, build the major envs to confirm no regressions:

```bash
for env in trmnl trmnl_4clr waveshare-esp32-driver WAVESHARE_397 xiao_esp32c6_75v1; do
    pio run -e "$env" 2>&1 | tail -3
done
```

All must report `SUCCESS`. Also run the native test suite:

```bash
pio test -e native
```

Expected: 37 tests pass (same as the post-12.48b state).

### 9.4 Out of scope

- Captive portal UX tests (Phase 2).
- BMP rendering tests (Phase 2).
- Battery / deep-sleep current measurement (this board has no battery management onboard).
- Font / G5 / QR work (Phase 2).
- The 12.48" streaming work (separate branch, separate spec).

## 10. Risks & contingencies

| Risk | Mitigation |
|---|---|
| pioarduino release `55.03.37` doesn't have ESP32-C6 board definitions ready, or has a regression on C6. | Try `56.x` or a more recent release; fall back to building from the pioarduino main branch. If C6 support is genuinely broken, the alternative is to use a slightly older pioarduino release (the C6 has been supported since ~5.4.0) — pick whichever release is known-good. |
| GxEPD2 1.6.5's `GxEPD2_750` produces inverted output (some 7.5" V1 panels need pixel inversion). | The reference firmware's `pngDraw` notes "If colors are inverted, use: pixel = !pixel" — keep that as a one-line fallback we can toggle if hardware shows inverted output during smoke test. |
| Per-pixel `drawPixel` is too slow for the user's taste. | Acceptable for MVP. Phase 2 can introduce a packed-row write that uses GxEPD2's `_buffer` directly (the underlying frame buffer is accessible) for ~10× speedup. |
| The 7.5" V1's controller doesn't respond on the user's specific panel revision (rare — Waveshare has shipped revisions over years). | The smoke test catches this immediately (blank panel + serial silence from GxEPD2). Fix: pin `zinggjm/GxEPD2` to a specific known-good commit (the user's reference uses `^1.6.5` — we follow). |
| The XIAO C6 + 7.5" V1 + USB-CDC + PSRAM-less + WiFi 6 combo strains the regulator under load. | The 7.5" V1 draws ~25 mA peak; WiFi peaks ~250 mA. The XIAO C6's onboard regulator handles ~500 mA. Should be fine; flag if brownouts show up in serial. |

## 11. Phase 2 (separate spec, follow-up)

These are intentionally deferred from MVP:

- **On-panel captive-portal screen** with QR code containing `TRMNL-<friendly_id>` SSID + the portal URL. Requires a small QR encoder library (e.g. nayuki's `qrcodegen.c`) and GxEPD2-side rendering of the QR + accompanying text.
- **On-panel error/setup screens** (`MSG_API_ERROR`, `WIFI_FAILED`, `READY_TO_SHIP`, `MAC_NOT_REGISTERED`, etc.) rendered via GxEPD2's Adafruit_GFX text primitives + a small set of GFX-format fonts (converted from the project's existing bb_epaper-format fonts, or use Adafruit's `FreeFonts`).
- **G5-compressed image rendering** for the embedded WiFi setup QR (`loadG5Image` in bb_epaper) — re-render the G5 source as PNG, or implement a small G5 decoder and feed `drawPixel`.
- **BMP rendering** — port TRMNL's existing BMP decoder to feed GxEPD2 via `drawPixel` (or directly into the frame buffer for speed).
- **Battery voltage reporting** if the user adds a divider on D0 or D1.
- **Packed-row write path** instead of per-pixel `drawPixel` for faster refresh.

## 12. Summary of new / changed files

**New:**
- `platformio.ini` — append `[env:xiao_esp32c6_75v1]`
- `src/DEV_Config.h` — append `#elif defined(BOARD_XIAO_ESP32C6_75V1)` pin block
- `include/config.h` — append `#elif defined(BOARD_XIAO_ESP32C6_75V1)` config branch
- `src/display.cpp` — append `#elif defined(BOARD_XIAO_ESP32C6_75V1)` branch (including the file-static PNG helper)
- `lib/trmnl_xiao_esp32c6_75v1/library.properties`
- `lib/trmnl_xiao_esp32c6_75v1/README.md`
- `lib/trmnl_xiao_esp32c6_75v1/src/gxepd2_adapter.{h,cpp}`

**Modified:**
- `src/display.cpp` — small audit-and-gate work where existing `bbep.*` references are unconditional and would break compilation for our board; add `#ifndef BOARD_XIAO_ESP32C6_75V1` guards where needed (same audit pattern the 12.48b branch's Task 11 used).

**Untouched:**
- `src/main.cpp`, `lib/wificaptive`, `src/api-client/*`, `src/preferences_persistence.cpp`, OTA path, all other boards' behavior.

## 13. Acceptance criteria

The MVP is complete when:

1. `pio run -e xiao_esp32c6_75v1` returns `SUCCESS`.
2. `pio run -e trmnl` / `trmnl_4clr` / `waveshare-esp32-driver` / `WAVESHARE_397` still return `SUCCESS` (no regressions).
3. `pio test -e native` still passes all 37 tests.
4. On real hardware: a freshly-flashed device, configured via the captive portal, completes a `/setup` + `/display` cycle and renders the dashboard PNG on the panel within ~40 s.
5. Pressing the boot button during deep-sleep re-triggers a refresh cycle.
6. All `display_show_msg(...)` calls log a warning but do not crash or hang the device.

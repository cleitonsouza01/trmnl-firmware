# Waveshare 12.48" 1304×984 BWR (SKU 17299) — driver integration design

**Status:** Approved (brainstorming) — pending implementation plan
**Date:** 2026-05-10
**Revised:** 2026-05-16 (panel topology + library choice corrected — see §1.1); 2026-05-17 (paged-mode default for WROOM support — see §1.2)
**Target hardware:** Waveshare ESP32 Driver Board (SKU 15823, ESP32-WROVER variant — PSRAM required) + 12.48" e-Paper Module B (SKU 17299, part number `12.48inch e-paper Module (B)`, 1304×984, 3-color black/white/red, **four onboard controllers**)

## 1. Goal

Add support to `trmnl-firmware` for the Waveshare 12.48" 1304×984 black/white/red e-paper display, driven from the Waveshare ESP32 Driver Board. The display must integrate with the existing TRMNL update flow (`/api/setup`, `/api/display`, captive-portal Wi-Fi setup, OTA, sleep cycle) without changing those paths.

### 1.1 What changed in the 2026-05-16 revision

The original 2026-05-10 draft assumed a **dual-controller** panel (two halves M / S, 10 control GPIOs) and proposed porting Waveshare's reference C driver into an in-tree library with a custom `RowSplitter`. Analysis of Waveshare's reference at `/Users/cleiton/projects/eletronics/12.48inch-e-paper/esp32/esp32-epd-12in48/` corrects three facts:

1. **The panel has four controllers** (`M1`, `S1`, `M2`, `S2`) driving four quadrants — not two halves. This requires 12 control GPIOs (4× CS, 2× DC, 2× RST, 4× BUSY), not 10.
2. **Waveshare's reference uses bit-banged SPI** (`DEV_SPI_WriteByte` clocks bits with `delayMicroseconds(10)`); a faithful port would push one 320 KB BWR frame in ~10 minutes. Any port has to rewrite the SPI layer first.
3. **GxEPD2 already supports this exact panel** as `GxEPD2_1248c` — its 12-pin constructor maps directly to the four-controller topology, hardware-SPI is used internally, and the same library was just shipped in the May-14 XIAO-C6 / 7.5"V1 branch.

The revision therefore pivots from "port Waveshare reference + custom row splitter" to "**GxEPD2 + thin adapter**", mirroring the proven `lib/trmnl_xiao_esp32c6_75v1/` pattern. The custom `WS1248B` driver, `BBEPAdapter1248B`, and `RowSplitter` are removed; the in-tree library becomes a small adapter only.

### 1.2 What changed in the 2026-05-17 revision

Hardware-on-the-bench confirmed the user's Waveshare ESP32 Driver Board ships with the **ESP32-WROOM-32E** module (no external PSRAM), not the WROVER variant the Phase 1 design assumed. The Phase 1 binary boots but aborts in `display_init` when `heap_caps_get_total_size(MALLOC_CAP_SPIRAM)` returns 0. Rather than require the user to swap modules, the design pivots:

1. **Single-env paged-mode default.** Drop `MAX_DISPLAY_BUFFER_SIZE` from 400 000 to 65 536. `GxEPD2_3C<GxEPD2_1248c, …>` instantiates with ~200-row planes, so the full 1304×984 BWR frame renders in 5 passes. Total adapter sizeof ≈ 65 KB.
2. **DRAM-resident static adapter.** Revert from `heap_caps_malloc(MALLOC_CAP_SPIRAM)` + placement-new (the Phase 1 workaround for the dual-framework PSRAM-init fragility) back to a plain file-static instance. ~65 KB in static BSS is comfortable on both WROOM (~256 KB DRAM total) and WROVER variants — no PSRAM dependency at all.
3. **Drop `board_build.psram = enabled`, `BOARD_HAS_PSRAM`, `CONFIG_SPIRAM_USE_MALLOC=1`** from `[env:waveshare_1248b]`. The PSRAM detection check in `display_init` is also removed (no longer relevant).
4. **WROOM is no longer a non-goal.** §2's "Boards without PSRAM" exclusion is dropped (see implementation plan `docs/superpowers/plans/2026-05-17-waveshare-1248b-wroom-fallback.md`).

Sections §4 (decision #6), §5.3 (adapter header constant), §5.4 (display.cpp branch), §6.1 (memory budget), §7.1 (env), §10 (risks), and §13 (acceptance criteria) describe the Phase 1 PSRAM-required behavior — they remain accurate as historical record but the **shipped behavior is the §1.2 design**. A follow-up clean-up pass can fold §1.2 into those sections; for now the divergence is small and the §1.2 summary plus the implementation plan are authoritative.

## 2. Non-goals

- Implementing support for other Waveshare panels in the same spec. The 7.5" 640×384 BWR (SKU 14144), the 12.48" Module **A** (BW, single-driver variant), and the 13.3" Module B (960×680) are deferred to follow-up specs that will reuse the boundary defined here (see §11).
- Patching GxEPD2 upstream.
- Boards without PSRAM. Full-frame BWR buffering needs ~320 KB; the WROVER variant of the driver board ships with 4 MB PSRAM and is the supported target.
- Battery-life optimization for this board. The Waveshare ESP32 Driver Board is not power-optimized for battery use, so the existing `FAKE_BATTERY_VOLTAGE` pattern is reused.

## 3. Background

### 3.1 Project shape (relevant pieces)

- `src/display.cpp` is the single point of truth for display I/O. Existing branches: **`BB_EPAPER`** (most boards, instantiates `BBEPAPER bbep` from `bitbank2/bb_epaper`), **`BOARD_X_CLASS`** (instantiates `FASTEPD bbep` from `bitbank2/FastEPD`), and (since May 14) **`BOARD_XIAO_ESP32C6_75V1`** (uses `lib/trmnl_xiao_esp32c6_75v1/` and the GxEPD2-based PNG render path).
- `src/DEV_Config.h` declares per-board pin macros under `#if defined(BOARD_*)` blocks.
- `include/config.h` declares `PIN_INTERRUPT`, `DEVICE_MODEL`, `FAKE_BATTERY_VOLTAGE`, etc. per board.
- `platformio.ini` already contains a `[env:waveshare-esp32-driver]` env that targets `esp32dev` with a single-controller 6-pin pin layout for small panels. It stays as-is.
- The TRMNL update flow downloads a PNG and pushes it to the panel via `display_show_image()`. PNG decoding uses `bitbank2/PNGdec`, already a dependency.

### 3.2 The proven pattern (XIAO C6 / 7.5"V1)

The May-14 work for the XIAO C6 + Waveshare 7.5"V1 panel established this shape:

```
lib/trmnl_xiao_esp32c6_75v1/src/gxepd2_adapter.{h,cpp}    # ~80 LoC adapter
src/display.cpp  →  new #elif branch instantiating the adapter
src/DEV_Config.h →  new #elif pin block
include/config.h →  new #elif device-config block
platformio.ini   →  new env with GxEPD2 in lib_deps, bb_epaper in lib_ignore
```

The adapter exposes `init / width / height / sleep / powerOff` + an accessor to the underlying `GxEPD2_BW<DRIVER, …>` instance. `display.cpp`'s new `#elif` branch reimplements the public `display_*` functions in terms of the adapter; the PNG decode path runs `firstPage()` → per-pixel `drawPixel` inside the `pngDraw` callback → `nextPage()` loop. `display_show_msg(...)` paths log a warning and return (UX is deferred to Phase 2).

This spec follows the same shape, swapping `GxEPD2_BW<GxEPD2_750, …>` for `GxEPD2_3C<GxEPD2_1248c, …>`.

### 3.3 Why the 12.48" needed its own env (and not the bb_epaper path)

`bb_epaper`'s public API has a single CS/DC/BUSY trio per `BBEPAPER` instance and no panel constant for 1304×984 or any 4-controller panel. Accommodating the 12.48" inside `bb_epaper` would require invasive library changes (effectively a fork). GxEPD2 already does this work via `GxEPD2_1248c`, so the boundary stays clean: this env uses GxEPD2, the existing bb_epaper envs keep using bb_epaper, and the two libraries do not need to know about each other.

## 4. Approved decisions (from brainstorming, revised 2026-05-16)

| # | Decision |
|---|---|
| 1 | Scope is the 12.48" Module B only. Other Waveshare panels are deferred to per-panel specs that reuse the shape defined here (§11). |
| 2 | New build flag `BOARD_WAVESHARE_1248B` selects this driver path. The existing `BOARD_WAVESHARE_ESP32_DRIVER` env stays untouched (different pin map, different panel class). |
| 3 | Driver library: **`zinggjm/GxEPD2`** (`@^1.6.5`), class `GxEPD2_1248c` (the 3-color BWR 1304×984 driver). The library handles the four-controller init / refresh / sleep paired sequencing internally; the firmware never touches individual M1/S1/M2/S2 pins. |
| 4 | Architecture: small `GxEPD2Adapter1248B` class (Phase 1 surface: `init`, `width`, `height`, `sleep`, `powerOff`, plus an accessor to the underlying GxEPD2 instance) + a new `#elif defined(BOARD_WAVESHARE_1248B)` branch in `src/display.cpp` that drives `firstPage()/nextPage()/drawPixel` directly during PNG decode. Mirrors `lib/trmnl_xiao_esp32c6_75v1/`. |
| 5 | Color: PNG-driven, color-aware from day one. RGB → `{white, black, red}` quantization runs once per pixel inside the `pngDraw` callback (the same threshold rule the existing `BBEP_3COLOR` boards use in `display.cpp`, ported across as a small helper). |
| 6 | Frame buffer: GxEPD2 owns it. Full-frame mode (`MAX_HEIGHT == HEIGHT`) requires PSRAM (~320 KB for B + R planes). The env enables PSRAM and sets `BOARD_HAS_PSRAM`; if `ps_malloc` fails at runtime, GxEPD2 falls back to its default heap allocation and rendering still works at a lower page count. |
| 7 | Pin map: 12 GPIOs, locked to Waveshare's reference assignment for the 12.48" Module B + ESP32 Driver Board pairing (§7.2). |
| 8 | `PIN_INTERRUPT = 0` (BOOT button on the driver board) — the existing `BOARD_WAVESHARE_ESP32_DRIVER` env's GPIO 33 conflicts with `EPD_M1S1_RST_PIN` and cannot be reused for this env. |
| 9 | `MSG` paths (captive-portal QR, error screens, friendly_id, etc.): logged no-ops in Phase 1, identical to the XIAO C6 / 7.5"V1 MVP. The TRMNL captive-portal SoftAP and web UI continue to serve so Wi-Fi can be configured from a phone. On-panel UX is deferred to a Phase 2 spec. |
| 10 | Image format: PNG only in Phase 1. If the server returns BMP, the magic-byte check logs a warning and skips rendering. |
| 11 | Testing: rely on the same minimum set the C6/7.5"V1 MVP uses — build sanity across all envs, native suite still passes, on-device smoke is the end-to-end TRMNL flow. No new native unit tests (no in-tree row splitter, no in-tree quantizer worth its own test — the quantizer is the same one bb_epaper boards already use). |

## 5. Architecture

### 5.1 Panel geometry

```
              Cols 0…647        Cols 648…1303
            ┌─────────────────┬──────────────────┐
Rows 0…491  │  M1 (648×492)   │   S1 (656×492)   │   top half
            ├─────────────────┼──────────────────┤
Rows 492…983│  M2 (656×492)   │   S2 (648×492)   │   bottom half
            └─────────────────┴──────────────────┘
```

Four controllers, two halves stacked vertically. Each half's slave controller pads its source-line buffer from 648 to 656 columns; the visible area is 1304 px wide. GxEPD2 handles the per-controller geometry internally — `display.cpp` sees a single 1304×984 logical surface and writes `drawPixel(x, y, color)` calls.

### 5.2 File structure

```
lib/trmnl_waveshare_1248b/
├── library.properties              metadata only
├── README.md                       describes the lib + wiring
└── src/
    └── gxepd2_adapter_1248b.{h,cpp}   small adapter (Phase 1: ~90 LoC of stubs;
                                        Phase 2 grows to forward text/QR via
                                        Adafruit_GFX)

src/
├── display.cpp                     new #elif BOARD_WAVESHARE_1248B branch
└── DEV_Config.h                    new pin block (12 GPIOs)

include/config.h                    new BOARD_WAVESHARE_1248B branch

platformio.ini                      new [env:waveshare_1248b]
```

No new `test/` directories — there is no pure-logic unit worth native-testing in this MVP (GxEPD2 is 3rd-party; the adapter is mostly pass-through; the `pngDraw` callback is small).

### 5.3 `GxEPD2Adapter1248B` (the small adapter)

`lib/trmnl_waveshare_1248b/src/gxepd2_adapter_1248b.h`:

```cpp
// ESP32-only translation unit; see ARDUINO_ARCH_ESP32 gate in .cpp.
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_3C.h>
#include <epd3c/GxEPD2_1248c.h>

namespace trmnl {

// 400 000 lets GxEPD2 fit the full BWR frame (2 * 1304 * 984 / 8 = 320 784 bytes)
// in one page on PSRAM-equipped boards. Drop to e.g. 65536 for paged mode.
static constexpr unsigned long MAX_DISPLAY_BUFFER_SIZE = 400000UL;

template <typename DRIVER>
inline constexpr int MAX_HEIGHT() {
    // For 3-color, GxEPD2 allocates 2 * (WIDTH/8) bytes per row.
    return DRIVER::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (2UL * DRIVER::WIDTH / 8)
         ? DRIVER::HEIGHT
         : MAX_DISPLAY_BUFFER_SIZE / (2UL * DRIVER::WIDTH / 8);
}

class GxEPD2Adapter1248B {
public:
    // 12-pin constructor — order mirrors GxEPD2_1248c::GxEPD2_1248c(...)
    GxEPD2Adapter1248B(int8_t cs_m1, int8_t cs_s1, int8_t cs_m2, int8_t cs_s2,
                      int8_t dc1, int8_t dc2, int8_t rst1, int8_t rst2,
                      int8_t busy_m1, int8_t busy_s1, int8_t busy_m2, int8_t busy_s2,
                      int8_t sck, int8_t mosi);

    // ---- Phase 1 (MVP) ----
    bool init();
    int  width()  const;     // returns 1304
    int  height() const;     // returns 984
    void sleep();
    void powerOff();

    // Underlying GxEPD2 instance for the PNG-decode/drawPixel path.
    GxEPD2_3C<GxEPD2_1248c, MAX_HEIGHT<GxEPD2_1248c>()>& gx();

    // ---- Phase 2 placeholder ----
    // Text/QR/MSG forwarding to be added here later. Not in MVP.

private:
    GxEPD2_3C<GxEPD2_1248c, MAX_HEIGHT<GxEPD2_1248c>()> _gx;
    int8_t _sck, _mosi;
};

} // namespace trmnl
```

Implementation (~90 LoC):
- Constructor forwards the 12 panel pins to `GxEPD2_1248c(cs_m1, cs_s1, cs_m2, cs_s2, dc1, dc2, rst1, rst2, busy_m1, busy_s1, busy_m2, busy_s2)`, stores `sck` / `mosi`.
- `init()` calls `SPI.begin(_sck, /*miso*/ -1, _mosi, /*ss*/ -1)`, then `_gx.init(115200)`. Returns `true` if no exception (GxEPD2 init never returns a status code; the smoke test is the success signal).
- `width()` / `height()` forward to `GxEPD2_1248c::WIDTH` / `HEIGHT`.
- `sleep()` calls `_gx.hibernate()`.
- `powerOff()` calls `_gx.powerOff()`.
- `gx()` returns the embedded `GxEPD2_3C<>` reference.

ESP32-only TU — wrap the body of `gxepd2_adapter_1248b.cpp` in `#if defined(ARDUINO_ARCH_ESP32) ... #endif` so the native test env doesn't try to compile GxEPD2.

### 5.4 `display.cpp` `BOARD_WAVESHARE_1248B` branch

A new `#elif defined(BOARD_WAVESHARE_1248B)` arm (sibling to `BB_EPAPER`, `BOARD_X_CLASS`, and `BOARD_XIAO_ESP32C6_75V1`). It does **not** define `BB_EPAPER` or `BOARD_X_CLASS`. No cross-cutting "GXEPD2 board" umbrella macro is introduced — both GxEPD2-based arms (C6 and this one) stay independent for Phase 1.

```cpp
#elif defined(BOARD_WAVESHARE_1248B)
#include "gxepd2_adapter_1248b.h"

static trmnl::GxEPD2Adapter1248B g_adapter(
    EPD_M1_CS_PIN, EPD_S1_CS_PIN, EPD_M2_CS_PIN, EPD_S2_CS_PIN,
    EPD_M1S1_DC_PIN, EPD_M2S2_DC_PIN,
    EPD_M1S1_RST_PIN, EPD_M2S2_RST_PIN,
    EPD_M1_BUSY_PIN, EPD_S1_BUSY_PIN, EPD_M2_BUSY_PIN, EPD_S2_BUSY_PIN,
    EPD_SCK_PIN, EPD_MOSI_PIN
);
```

The `display.h` public functions are reimplemented for this board:

| Function | Phase 1 behavior |
|---|---|
| `display_init()` | `g_adapter.init();` |
| `display_width()` / `display_height()` | forward to `g_adapter.width()` / `height()` (1304 / 984) |
| `display_show_image(buffer, size, wait)` | PNG decode + paged render — see §6 |
| `display_show_msg(...)` (all overloads) | `Log.warning("display_show_msg(%d) on waveshare_1248b — UX not implemented in Phase 1", (int)message_type); return;` |
| `display_show_msg_api(...)` | logged no-op |
| `display_show_msg_qa(...)` | logged no-op |
| `display_show_battery(float)` | logged no-op |
| `display_reset()` | `g_adapter.init();` (GxEPD2's init does a hard reset) |
| `display_sleep()` | `g_adapter.sleep();` |
| `display_set_light_sleep(uint8_t)` | no-op — this board uses `esp_deep_sleep` between refreshes |
| `Paint_DrawMultilineText(...)` | logged no-op (Phase 2) |

### 5.5 Image-render flow (the one path that actually matters for MVP)

```
api-client/display.cpp                                 (unchanged for this board)
   downloads PNG into a heap buffer
        │
        ▼
display.cpp::display_show_image(buffer, size, wait)    (new branch)
   check magic bytes → if BMP, log a warning + skip; if PNG, continue
   adapter.gx().setFullWindow();
   adapter.gx().firstPage();
   do {
       adapter.gx().fillScreen(GxEPD_WHITE);
       decode_png_into_adapter(buffer, size);          // file-local helper
   } while (adapter.gx().nextPage());
   adapter.powerOff();
```

`decode_png_into_adapter(buffer, size)` is a small file-local helper inside the `#elif` arm. It uses `PNGdec` (already in `deps_common`):

```cpp
static PNG s_png;   // file-static; PNGdec state

// Same RGB→{WHITE, BLACK, RED} threshold rule used by the existing BBEP_3COLOR
// boards in display.cpp — small static helper, ported across by the implementation.
static uint16_t rgb_to_3color(uint8_t r, uint8_t g, uint8_t b);

static int pngDraw(PNGDRAW* pDraw) {
    const uint16_t y = pDraw->y;
    // Bounds check copied from the C6 branch (display.cpp f91efdc / 9856bab).
    if (y >= g_adapter.height()) return 1;
    uint16_t line[g_adapter.width()];   // RGB565 line buffer
    s_png.getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
    for (uint16_t x = 0; x < pDraw->iWidth && x < g_adapter.width(); ++x) {
        const uint16_t rgb565 = line[x];
        const uint8_t r = ((rgb565 >> 11) & 0x1f) << 3;
        const uint8_t g = ((rgb565 >> 5)  & 0x3f) << 2;
        const uint8_t b = ( rgb565        & 0x1f) << 3;
        g_adapter.gx().drawPixel(x, y, rgb_to_3color(r, g, b));
    }
    return 1;
}

static bool decode_png_into_adapter(const uint8_t* buf, int len) {
    int rc = s_png.openRAM(const_cast<uint8_t*>(buf), len, pngDraw);
    if (rc != PNG_SUCCESS) { Log.errorln(F("waveshare_1248b: PNG open failed rc=%d"), rc); return false; }
    rc = s_png.decode(nullptr, 0);
    s_png.close();
    if (rc != PNG_SUCCESS) { Log.errorln(F("waveshare_1248b: PNG decode failed rc=%d"), rc); return false; }
    return true;
}
```

The exact `pngDraw` body — `getLineAsRGB565` vs bit-shift on a 1bpp source vs other PNG color types — is finalized during implementation, ported from the existing `BBEP_3COLOR` display.cpp render path. The skeleton above shows the intent; the implementation step locks the conversion to whatever pixel formats the TRMNL backend actually serves to this board.

Per-pixel `drawPixel` is slow (~1.28M calls per page) but works. With `MAX_DISPLAY_BUFFER_SIZE = 400_000` on a PSRAM board, the loop runs once; without PSRAM, it runs ~5× with re-decode per page (acceptable for MVP — the panel refresh itself dominates the cycle time).

### 5.6 What does **not** change

- `src/main.cpp` flow (boot → wifi → setup → display → sleep).
- `lib/wificaptive` — captive-portal SoftAP and web UI still serve.
- `src/api-client/setup.cpp`, `src/api-client/display.cpp`, `src/api-client/submit_log.cpp`.
- OTA path.
- All other boards' build behavior.

## 6. Data flow (one display update)

End-to-end, identical to other small boards except for the body of `display_show_image()`:

```
boot (cold or wake-from-deep-sleep)
   ↓
preferences load → wifi connect
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
adapter.gx().nextPage() loop (1 iteration with PSRAM, ~5 without)
   ↓
adapter.powerOff()
   ↓
esp_deep_sleep_start() for refresh_rate seconds
   ↓
(wake by timer or by GPIO 0 BOOT button)
```

### 6.1 Memory & timing

| Component | Bytes | Notes |
|---|---|---|
| GxEPD2 BWR frame buffer (2 × 1304 × 984 / 8) | ~320 800 | full-frame mode; allocated in PSRAM (`ps_malloc`) |
| PNGdec workspace (`PNG_MAX_BUFFERED_PIXELS=14984`) | ~15 000 | heap |
| Downloaded PNG buffer (typical TRMNL image) | ~30 000 | heap |
| WiFi + TLS + HTTPClient (existing) | ~16 000 | heap |
| **Heap overhead during display update** | **~61 KB** | comfortable on the WROVER's ~250 KB free heap |

- SPI clock: GxEPD2 default (4 MHz). Conservative; the reference's bit-banged path was orders of magnitude slower.
- Full refresh time: ~25–30 s per Waveshare datasheet (panel-limited, not MCU-limited). The existing display flow already waits for refresh completion; nothing in `main.cpp` needs to change.

## 7. Build configuration

### 7.1 PlatformIO env

```ini
[env:waveshare_1248b]
extends = env:esp32_base
board = esp32dev
board_build.f_cpu = 240000000L
board_build.f_flash = 80000000L
board_build.flash_mode = qio
board_build.psram = enabled        ; required: ~320 KB BWR frame buffer
build_flags =
    ${env:esp32_base.build_flags}
    -D BOARD_WAVESHARE_1248B
    -D BOARD_HAS_PSRAM
    -D CONFIG_SPIRAM_USE_MALLOC=1
    -D PNG_MAX_BUFFERED_PIXELS=14984
    -D FAKE_BATTERY_VOLTAGE
lib_deps =
    ${deps_app.lib_deps}
    zinggjm/GxEPD2@^1.6.5
    adafruit/Adafruit GFX Library@^1.11.0
    adafruit/Adafruit BusIO@^1.16.0
lib_ignore =
    BMA530_SensorAPI
    IQS323
    trmnl_x
    BQ27427
    bb_epaper
    trmnl_xiao_esp32c6_75v1
```

> **Hardware verification step.** Before flashing, confirm the user's Waveshare ESP32 Driver Board is the WROVER variant (ESP32-WROVER-B, 4 MB PSRAM). The WROOM variant of the board exists in some lots and lacks PSRAM — flashing this env to a WROOM board would fail GxEPD2's PSRAM allocation and either fall back to slow paged mode or refuse to render large frames. Check the ESP module's silkscreen or run `esp_psram_get_size()` once on first boot.

The existing `[env:waveshare-esp32-driver]` env stays untouched.

### 7.2 Pin map (`src/DEV_Config.h`)

Per Waveshare's reference `DEV_Config.h` for the 12.48"B + ESP32 Driver Board pairing:

```c
#elif defined(BOARD_WAVESHARE_1248B)
   // Pin definition for Waveshare ESP32 Driver Board (SKU 15823) + 12.48" Module B (SKU 17299)
   // Four-controller panel driven via GxEPD2's GxEPD2_1248c class on hardware SPI.
   // Reference: /Users/cleiton/projects/eletronics/12.48inch-e-paper/esp32/esp32-epd-12in48/src/DEV_Config.h

   // Shared SPI bus
   #define EPD_SCK_PIN        13
   #define EPD_MOSI_PIN       14

   // Per-quadrant CS
   #define EPD_M1_CS_PIN      23      // top-left
   #define EPD_S1_CS_PIN      22      // top-right
   #define EPD_M2_CS_PIN      16      // bottom-left
   #define EPD_S2_CS_PIN      19      // bottom-right

   // DC shared per half (top row / bottom row)
   #define EPD_M1S1_DC_PIN    25      // top half DC
   #define EPD_M2S2_DC_PIN    17      // bottom half DC

   // RST shared per half
   #define EPD_M1S1_RST_PIN   33      // top half RST
   #define EPD_M2S2_RST_PIN    5      // bottom half RST

   // Per-quadrant BUSY
   #define EPD_M1_BUSY_PIN    32
   #define EPD_S1_BUSY_PIN    26
   #define EPD_M2_BUSY_PIN    18
   #define EPD_S2_BUSY_PIN     4
```

The single-controller macros (`EPD_CS_PIN`, `EPD_DC_PIN`, `EPD_RST_PIN`, `EPD_BUSY_PIN`) are intentionally **not** defined for this board. Code that references them is gated behind `#ifdef BB_EPAPER`, which this board does not define.

### 7.3 Config (`include/config.h`)

A new `#elif` after the existing `BOARD_XIAO_ESP32C6_75V1` branch:

```c
#elif defined(BOARD_WAVESHARE_1248B)
#define PIN_INTERRUPT 0          // BOOT button on the Waveshare ESP32 Driver Board.
                                 // GPIO 33 (used by the existing BOARD_WAVESHARE_ESP32_DRIVER env)
                                 // is taken by EPD_M1S1_RST_PIN on this wiring.
#define DEVICE_MODEL "waveshare_1248b"
#define FAKE_BATTERY_VOLTAGE
```

`MAX_IMAGE_SIZE` is bumped for this board (PSRAM is available, larger PNGs are plausible from a server tuned for a 1304×984 panel). Append to the existing `#if defined(BOARD_TRMNL_X) || …` block:

```c
#if defined(BOARD_TRMNL_X) || defined(BOARD_TRMNL_X_EPDIY) || defined(BOARD_WAVESHARE_1248B)
#define MAX_IMAGE_SIZE 750000 // Use PSRAM
#else
#define MAX_IMAGE_SIZE 90000
#endif
```

### 7.4 Library skeleton (`lib/trmnl_waveshare_1248b/library.properties`)

```properties
name=trmnl_waveshare_1248b
version=0.1.0
author=TRMNL
maintainer=TRMNL
sentence=GxEPD2-based driver adapter for the Waveshare 12.48" Module B (SKU 17299) on the Waveshare ESP32 Driver Board.
paragraph=Phase 1 (MVP): exposes init/width/height/sleep + access to the underlying GxEPD2_3C<GxEPD2_1248c, ...> instance for the PNG image-render path in display.cpp. Phase 2 layers in text/QR/MSG rendering by forwarding to GxEPD2's Adafruit_GFX surface.
category=Display
architectures=esp32
depends=GxEPD2,Adafruit GFX Library,Adafruit BusIO
```

### 7.5 Wiring summary (for the README + user)

```
Panel signal    ESP32 GPIO    Notes
─────────────   ──────────    ─────────────────────────────────
VCC             3V3           panel draws < 50 mA average
GND             GND
DIN  (MOSI)     14
CLK  (SCK)      13
M1_CS           23            top-left controller chip select
S1_CS           22            top-right controller chip select
M2_CS           16            bottom-left controller chip select
S2_CS           19            bottom-right controller chip select
M1S1_DC         25            top half data/command select
M2S2_DC         17            bottom half data/command select
M1S1_RST        33            top half reset
M2S2_RST         5            bottom half reset
M1_BUSY         32            top-left busy
S1_BUSY         26            top-right busy
M2_BUSY         18            bottom-left busy
S2_BUSY          4            bottom-right busy
```

Total: 14 GPIOs (12 control + SCK + MOSI). The Waveshare ESP32 Driver Board's flat-flex socket for the panel is pre-wired to these pins via the on-board level shifter — no manual jumpers needed when using the board's stock cable.

## 8. Error handling

### 8.1 BUSY timeouts

Handled inside GxEPD2. Each controller's BUSY is polled by the library; on timeout the library returns from the refresh call (no exception, no crash). The visible failure mode is a partial / unchanged panel; the cycle proceeds to deep-sleep and retries on next wake.

### 8.2 SPI write failures

Arduino `SPI.transfer()` does not surface errors. Per-call handling is not added.

### 8.3 PNG decode mid-frame failure

If `PNGdec` errors out partway through a frame, the helper returns `false`; `display_show_image` finishes the `nextPage()` loop (whatever was drawn so far is what gets refreshed) and proceeds to `powerOff` + deep-sleep. The decode error is logged so the existing `/api/log` upload picks it up.

### 8.4 PSRAM allocation failure

If `ps_malloc` for the GxEPD2 frame buffer fails (e.g. WROOM board flashed with this env by mistake), GxEPD2's default heap allocation kicks in and may itself fail. The implementation logs heap + PSRAM sizes during `display_init` so the failure mode is diagnosable from serial; firmware does not attempt to recover. Pairing this env with a WROOM board is unsupported.

### 8.5 BMP from server

Magic-byte check (first 8 bytes: `\x89PNG\r\n\x1a\n` for PNG; `BM` for BMP). On BMP, log `[W] waveshare_1248b: BMP not supported in Phase 1; skipping render` and continue to sleep.

### 8.6 Half / quadrant desync

GxEPD2's `GxEPD2_1248c::_PowerOn`, `_PowerOff`, `_setPartialRamArea`, and refresh sequences all coordinate the four controllers internally. The application code never addresses an individual quadrant. No new failure surface here.

### 8.7 Explicitly out of scope

- Partial-refresh windows that span any quadrant boundary.
- Hot-swap / display reconnect during runtime.
- Multiple panels of this type behind one MCU.

## 9. Testing

### 9.1 Native unit tests

Nothing new. The MVP touches:
- GxEPD2 (3rd-party — not our test target).
- A thin adapter (mostly pass-through; not worth its own native test).
- A PNG-to-`drawPixel` callback shaped like the C6/7.5"V1 branch's (also not unit-tested there).
- A small RGB→3-color quantizer (small enough that a native unit test is optional; the implementation step decides based on how much the rule differs from the existing `BBEP_3COLOR` path).

The existing native test suite continues to run unaffected because the new lib's source is ESP32-gated via `#if defined(ARDUINO_ARCH_ESP32)`.

### 9.2 On-device smoke

No separate smoke sketch — the full firmware *is* the smoke test for an MVP this small. Procedure:

1. Wire the panel per §7.5 (stock Waveshare cable handles this).
2. `pio run -e waveshare_1248b -t upload`.
3. From a phone, find the SSID `TRMNL-<friendly_id>` in available Wi-Fi networks. Connect.
4. Open `http://192.168.4.1` (the captive-portal IP). Enter home Wi-Fi credentials. Submit. Device reboots.
5. Device hits `/api/setup` → `/api/display` → downloads PNG → renders → deep-sleeps.
6. **Visual check:** dashboard image appears on the panel within ~60–90 s of the WiFi-config reboot (the larger panel + slower SPI than the C6/7.5V1 → longer total cycle). No obvious quadrant-seam artifacts. Black pixels render black; red pixels render red.
7. Press the BOOT button (GPIO 0): device wakes, re-fetches, re-renders.

### 9.3 Build-sanity for other envs

After landing this work, build the major envs to confirm no regressions:

```bash
for env in trmnl trmnl_4clr waveshare-esp32-driver WAVESHARE_397 xiao_esp32c6_75v1 waveshare_1248b; do
    pio run -e "$env" 2>&1 | tail -3
done
```

All must report `SUCCESS`. Also run the native test suite:

```bash
pio test -e native
```

Expected: same pass count as the pre-change baseline.

### 9.4 Out of scope

- Captive portal on-panel UX tests (Phase 2).
- BMP rendering tests (Phase 2).
- Battery / deep-sleep current measurement (this board has no battery management onboard).
- Font / G5 / QR work (Phase 2).

## 10. Risks & contingencies

| Risk | Mitigation |
|---|---|
| User's Waveshare ESP32 Driver Board ships as WROOM (no PSRAM) rather than WROVER. | Implementation step verifies via `esp_psram_get_size()` on first boot and logs the result. If WROOM: drop `MAX_DISPLAY_BUFFER_SIZE` to ~65 000 (paged mode, ~5 pages per frame, ~3× slower render) and remove the `BOARD_HAS_PSRAM` flag. |
| GxEPD2 `GxEPD2_1248c` defaults conflict with the Driver Board's stock pin map. | The 12-pin constructor takes our exact pins — no defaults are used. Verified against `/Users/cleiton/projects/eletronics/12.48inch-e-paper/esp32/esp32-epd-12in48/src/DEV_Config.h`. |
| Per-pixel `drawPixel` is too slow for the user's taste. | Acceptable for MVP — panel refresh (~25-30 s) dominates render time (~2-5 s on PSRAM). Phase 2 can write packed rows directly into GxEPD2's frame buffer for ~10× render speedup if needed. |
| GxEPD2 1.6.5's `GxEPD2_1248c` produces inverted output (some panel revisions invert one color plane). | If smoke test shows inverted red or inverted black, swap `GxEPD_RED ↔ GxEPD_BLACK` in `rgb_to_3color` as a one-line fix. |
| The TRMNL backend doesn't yet serve a 1304×984 PNG for this device model. | The BYOS adaptation pattern documented for `xiao_c6_75v1` (`docs/superpowers/specs/2026-05-14-byos-frontend-adaptation-xiao-c6-75v1.md`) applies — register a new device model `waveshare_1248b` server-side with the right resolution before the on-device smoke test. |
| pioarduino fork is needed (some recent C-class envs do). | Not needed here: `esp32dev` + `espressif32@6.12.0` (the project's default platform) fully supports ESP32-WROOM/WROVER. We keep the project's default platform. |

## 11. Future extensibility

Design preserves a clear path for other Waveshare panels. Each becomes a new spec + new env + (when GxEPD2 already supports the panel) a new ~80 LoC adapter.

| Display | Path |
|---|---|
| **12.48" Module A (BW, single driver, 1304×984)** | New `[env:waveshare_1248a]`, `BOARD_WAVESHARE_1248A` flag, adapter wrapping `GxEPD2_BW<GxEPD2_1248, …>`. Same shape as this spec, half the controllers. |
| **7.5" 640×384 BWR (SKU 14144, "C")** | Could use the bb_epaper path (`EP74R_640x384` is supported there) or the GxEPD2 path for consistency. Cheaper via bb_epaper. |
| **13.3" Module B (SKU 22894, 960×680)** | New `[env:waveshare_1333b]` if/when GxEPD2 adds the panel class; otherwise an in-tree port becomes worth considering. |

The boundary contract — `BOARD_WAVESHARE_*` flag + `lib/trmnl_waveshare_<panel>/` adapter + `#elif` branch in `display.cpp` — is the reusable shape, now established by both this spec and the XIAO-C6/7.5V1 spec.

## 12. Summary of new / changed files

**New:**
- `platformio.ini` — append `[env:waveshare_1248b]`
- `src/DEV_Config.h` — append `#elif defined(BOARD_WAVESHARE_1248B)` pin block (12 GPIOs)
- `include/config.h` — append `#elif defined(BOARD_WAVESHARE_1248B)` config branch + update the `MAX_IMAGE_SIZE` PSRAM clause
- `src/display.cpp` — append `#elif defined(BOARD_WAVESHARE_1248B)` branch (including the file-static PNG helper + `rgb_to_3color` ported from the existing `BBEP_3COLOR` path)
- `lib/trmnl_waveshare_1248b/library.properties`
- `lib/trmnl_waveshare_1248b/README.md`
- `lib/trmnl_waveshare_1248b/src/gxepd2_adapter_1248b.{h,cpp}`

**Modified:**
- `src/display.cpp` — audit-and-gate where existing `bbep.*` references are unconditional and would break compilation for our board; add `#ifndef BOARD_WAVESHARE_1248B` (or, where it makes sense, the existing `#ifdef BB_EPAPER` umbrella) where needed.

**Untouched:**
- `src/main.cpp`, `lib/wificaptive`, `src/api-client/*`, `src/preferences_persistence.cpp`, OTA path, all other boards' behavior.

## 13. Acceptance criteria

The MVP is complete when:

1. `pio run -e waveshare_1248b` returns `SUCCESS`.
2. `pio run -e trmnl` / `trmnl_4clr` / `waveshare-esp32-driver` / `WAVESHARE_397` / `xiao_esp32c6_75v1` still return `SUCCESS` (no regressions).
3. `pio test -e native` still passes the same number of tests as the pre-change baseline.
4. On real hardware (Waveshare ESP32 Driver Board WROVER + 12.48" Module B): a freshly-flashed device, configured via the captive portal, completes a `/setup` + `/display` cycle and renders the dashboard PNG on the panel within ~60–90 s of the WiFi-config reboot.
5. Pressing the BOOT button (GPIO 0) during deep-sleep re-triggers a refresh cycle.
6. All `display_show_msg(...)` calls log a warning but do not crash or hang the device.
7. Black pixels render as black, white as white, and red as red — no quadrant-seam artifacts.

## 14. Phase 2 (separate spec, follow-up)

Same shape as the C6/7.5V1 Phase 2:

- On-panel captive-portal screen with QR code containing `TRMNL-<friendly_id>` SSID + portal URL (QR encoder lib + GxEPD2 Adafruit_GFX rendering).
- On-panel error/setup screens (`MSG_API_ERROR`, `WIFI_FAILED`, `READY_TO_SHIP`, `MAC_NOT_REGISTERED`, etc.) via GxEPD2 text primitives + GFX-format fonts.
- G5-compressed image rendering for the embedded WiFi setup QR.
- BMP rendering — port TRMNL's existing BMP decoder to feed GxEPD2.
- Packed-row write path instead of per-pixel `drawPixel` for faster refresh.

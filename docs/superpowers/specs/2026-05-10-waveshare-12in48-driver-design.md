# Waveshare 12.48" 1304×984 BWR (SKU 17299) — driver integration design

**Status:** Approved (brainstorming) — pending implementation plan
**Date:** 2026-05-10
**Target board:** Waveshare ESP32 Driver Board (ESP32-WROOM-32) + 12.48" e-Paper Module B (SKU 17299, part number "12.48inch e-paper Module (B)", 1304×984, 3-color black/white/red)

## 1. Goal

Add support to `trmnl-firmware` for the Waveshare 12.48" 1304×984 black/white/red e-paper display, driven from the standard Waveshare ESP32 Driver Board. The display must integrate with the existing TRMNL update flow (`/api/setup`, `/api/display`, captive-portal Wi-Fi setup, OTA, sleep cycle) without changing those paths.

## 2. Non-goals

- Implementing support for the other two displays in the same spec. The 7.5" 640×384 BW (SKU 13187) and 7.5" 640×384 BWY (SKU 14144) are explicitly deferred to follow-up specs that will reuse the boundary defined here (see §11).
- Changing `bb_epaper` (no library patches; no upstream PRs as part of this work).
- Battery-life optimization for this board. The Waveshare ESP32 Driver Board is not power-optimized for battery use, so the existing `FAKE_BATTERY_VOLTAGE` pattern is reused.

## 3. Background

### 3.1 Project shape (relevant pieces)

- `src/display.cpp` is the single point of truth for display I/O. Non-X-class boards instantiate `BBEPAPER bbep(...)` from `bitbank2/bb_epaper`; X-class boards instantiate `FASTEPD bbep` from `bitbank2/FastEPD`. Both objects are referenced by the same `bbep` identifier, so the rendering call sites (`bbep.fillScreen`, `bbep.startWrite`, `bbep.writeData`, `bbep.refresh`, `bbep.sleep`, etc.) are uniform.
- `src/DEV_Config.h` declares per-board pin macros (`EPD_SCK_PIN`, `EPD_MOSI_PIN`, `EPD_CS_PIN`, `EPD_DC_PIN`, `EPD_RST_PIN`, `EPD_BUSY_PIN`) under `#if defined(BOARD_*)` blocks.
- `platformio.ini` already contains a `[env:waveshare-esp32-driver]` env scaffold that targets `esp32dev` with the standard 6-pin Waveshare assignment. It stays as-is for future small-panel use.
- The TRMNL update flow downloads an image (BMP or PNG) and pushes it to the panel via `display_show_image()`. PNG decoding uses `bitbank2/PNGdec`, already a dependency.

### 3.2 Why the 12.48" doesn't fit the existing pattern

The 12.48" Module B is a **dual-controller** panel: two 648×984 halves placed side-by-side on one PCB, each with its own SPI CS, DC, BUSY and RST signals. It needs ~10 control GPIOs vs. the 6 used by single-controller panels.

`bb_epaper`'s public API is built around a single CS/DC/BUSY trio per `BBEPAPER` instance and has no panel constant for 1304×984 or 648×984. Accommodating the 12.48" in `bb_epaper` would require invasive library changes (effectively a fork). The reference repo at `/Users/cleiton/projects/eletronics/E-Paper_ESP32_Driver_Board_Code` does not include a 12.48" driver — the closest is `epd13in3b`, which is a different (single-controller, 960×680) panel. The 12.48" reference driver lives in Waveshare's separate `e-Paper` repository, typically under `Arduino/epd12in48b/`.

## 4. Approved decisions (from brainstorming)

| # | Decision |
|---|---|
| 1 | Scope is the 12.48" only; design must extend cleanly to other Waveshare displays (§11). |
| 2 | New build flag `BOARD_WAVESHARE_1248B` selects a dedicated driver path, parallel to `BOARD_X_CLASS`. The existing `BOARD_WAVESHARE_ESP32_DRIVER` env stays untouched. |
| 3 | Driver source: **Approach A** — port Waveshare's `epd12in48b` reference into `lib/trmnl_waveshare_1248b/` with a thin C++ class plus a bb_epaper-shaped adapter. GxEPD2 (`GxEPD2_1248c`) is a recorded contingency only, used if A stalls. |
| 4 | Color: PNG-driven, color-aware from day one. Each decoded row is split into M_black / M_red / S_black / S_red and streamed to the panel. No full-frame buffer; no PSRAM dependency. |
| 5 | Pin map captured here as Waveshare's published assignment for "12.48" Module B + ESP32 Driver Board"; exact GPIO numbers are locked during implementation against the ported reference driver. |
| 6 | Halves are always operated in lock-step from the public driver API. BUSY timeout is the primary monitored failure surface. |
| 7 | Testing: native unit tests for the row splitter and color quantizer, plus an on-device smoke sketch and a field test against the real `/api/display` flow. |

## 5. Architecture

### 5.1 Module boundary

A new `BOARD_WAVESHARE_1248B` macro is the third sibling alongside `BB_EPAPER` and `BOARD_X_CLASS` in `display.cpp`. The new code is fully contained in:

```
lib/trmnl_waveshare_1248b/
├── library.properties
├── src/
│   ├── ws1248b.h               // L1: low-level driver class
│   ├── ws1248b.cpp
│   ├── bbep_adapter_1248b.h    // L2: bb_epaper-shaped adapter
│   ├── bbep_adapter_1248b.cpp
│   ├── row_splitter.h          // pure logic, native-testable
│   └── row_splitter.cpp
└── README.md
```

### 5.2 Three layers

- **L1 — `WS1248B`**: ported Waveshare reference driver. Owns SPI bus access, init sequences (full and partial), `writeHalfPlane(half, plane, row, data, len)`, `clearHalf(half)`, `refresh()` (paired), `sleep()` (paired), `init()` (paired). Stays close to Waveshare's reference structure so future upstream fixes are easy to merge.
- **L2 — `BBEPAdapter1248B`**: implements the subset of bb_epaper's call surface that `display.cpp` actually uses (`width()`, `height()`, `fillScreen()`, `setAddrWindow()`, `startWrite(plane)`, `writeData(buf, len)`, `refresh()`, `sleep()`, `setLightSleep()`, `getCache()`). Hides the M/S split from `display.cpp` and routes each call to the right half(s) via `RowSplitter`.
- **L3 — `display.cpp` wiring**: a new `#elif defined(BOARD_WAVESHARE_1248B)` branch instantiates the adapter as `bbep`. Existing rendering code paths remain unchanged.

### 5.3 Panel geometry

```
            ┌─────── 1304 px ───────┐
            ┌──── 648 ────┬─ 656 ──┐
   984 px   │      M      │   S    │
            │ (left half) │(right) │
            └─────────────┴────────┘
```

The slave half's controller pads its line buffer to 656 columns; the visible region is 648. The adapter performs the crop so `display.cpp` only ever sees a 1304-px-wide logical surface.

## 6. Data flow (one display update)

1. `display.cpp::display_show_image()` is invoked with downloaded image bytes (existing path; unchanged).
2. `PNGdec::decode()` invokes the row callback once per source row. For each row:
   1. The color quantizer maps each pixel to `{white, black, red}` using the same rule the existing `BBEP_3COLOR` boards use in `display.cpp` (the implementation step ports that exact rule rather than reinventing it).
   2. `RowSplitter` packs the row into four small 1bpp buffers: `M_black[82]`, `M_red[82]`, `S_black[82]`, `S_red[82]` (82 bytes = 656÷8).
   3. The adapter calls `WS1248B::writeHalfPlane(M, BLACK, y, M_black, 82)` and the three siblings.
3. After all 984 rows have streamed, the adapter calls `WS1248B::refresh()`. This issues paired refresh commands to M and S, then waits for **both** BUSY pins to return idle.
4. The adapter's `sleep()` enters paired deep-sleep on both halves before MCU light-sleep.

### 6.1 Memory & timing

- Per-update working set: 4 × 82 B = ~328 B for row buffers, plus PNG decode workspace (already used by other boards). No frame buffer.
- SPI clock: 8 MHz (matches `bb_epaper`'s default and Waveshare's reference).
- Full refresh time: ~24–28 s per Waveshare datasheet. The existing display flow already waits for refresh completion; nothing in `main.cpp` needs to change.

### 6.2 What does not change

- `main.cpp`, the API client, the Wi-Fi/captive-portal layer, the OTA path: untouched.
- `display.cpp`'s public functions (`display_init`, `display_show_image`, `display_show_msg`, `display_sleep`, etc.) keep their existing signatures.

## 7. Build configuration

### 7.1 PlatformIO env

```ini
[env:waveshare_1248b]
extends = env:esp32dev
build_flags =
    ${env:esp32_base.build_flags}
    -D BOARD_WAVESHARE_1248B
    -D PNG_MAX_BUFFERED_PIXELS=14984   ; matches X-class for 1304-wide rows
    -D FAKE_BATTERY_VOLTAGE
```

The existing `[env:waveshare-esp32-driver]` env stays untouched.

### 7.2 Pin map (`src/DEV_Config.h`)

A new branch is added; pin numbers below are Waveshare's published assignment for the "12.48" Module B + ESP32 Driver Board" pairing. The implementation step verifies them against the ported reference driver and the Waveshare wiki, and locks the final values.

```c
#elif defined(BOARD_WAVESHARE_1248B)
   // Shared SPI bus
   #define EPD_SCK_PIN     13
   #define EPD_MOSI_PIN    14

   // Master half (left, "M")
   #define EPD_M_CS_PIN    23
   #define EPD_M_DC_PIN    25
   #define EPD_M_RST_PIN   26
   #define EPD_M_BUSY_PIN  27

   // Slave half (right, "S")
   #define EPD_S_CS_PIN    18
   #define EPD_S_DC_PIN    17
   #define EPD_S_RST_PIN   5
   #define EPD_S_BUSY_PIN  4
```

The single-half macros (`EPD_CS_PIN`, `EPD_DC_PIN`, `EPD_RST_PIN`, `EPD_BUSY_PIN`) are intentionally **not** defined for this board. Code that references them is gated behind `#ifdef BB_EPAPER`, which this board does not define.

### 7.3 Library deps

- New (in-tree): `lib/trmnl_waveshare_1248b/`. No new external dep.
- Unchanged: `bitbank2/PNGdec`, `bitbank2/JPEGDEC`, `bitbank2/bb_epaper` (still pulled by `deps_common` for other envs; ignored at link time for this env via `lib_ignore = bb_epaper`).

### 7.4 Partition / filesystem

Defaults inherited from `env:esp32_base`: `min_spiffs.csv`, `spiffs` filesystem. No partition changes.

## 8. Error handling

### 8.1 BUSY timeouts

Each half's BUSY is polled with a 10 ms inter-poll sleep. Default timeout is 40 000 ms (full-refresh budget plus margin); partial updates use 8 000 ms. On timeout, `WS1248B` logs via `Log_error` and returns failure; the adapter's `refresh()` propagates the failure up to `display_show_image`. The existing display flow already treats display errors as non-fatal (logs, continues to sleep). No new escalation path.

### 8.2 SPI write failures

Arduino `SPI.transfer()` does not surface errors, so per-call handling is not added. Startup validation: after `WS1248B::init()`, a status-register read is issued on each half (Waveshare reference behavior). If either half fails to identify, an internal `_panelFailed[2]` flag is set and the adapter falls back to "render `display_show_msg(MSG_FORMAT_ERROR)` on whichever half is alive, otherwise no-op". This reuses the existing error-message code path.

### 8.3 Decode mid-frame failure

If `PNGdec` errors out partway through a frame, the adapter's `refresh()` is still invoked — the panel will show whatever rows were sent (typically partial + white). This matches the behavior of today's bb_epaper boards. The decode error is logged so the existing `/api/log` upload picks it up.

### 8.4 Half desync (the novel failure mode)

Rule, enforced by the public driver API:

> Every public driver method either operates on both halves or operates on neither.

`init()`, `clear()`, `refresh()`, `sleep()` issue paired commands and wait for paired BUSY. `writeHalfPlane(half, ...)` is non-public, used only by `RowSplitter`. This keeps the halves in lock-step from any caller's perspective.

### 8.5 Power glitches mid-refresh

Handled by the existing flow: on next boot, `display_init()` runs full init + clear, which resets both halves. No new code.

### 8.6 Explicitly out of scope

- Partial-refresh windows that span the M/S boundary at runtime (the public adapter implements `setAddrWindow()` correctly, but no special optimization for cross-boundary windows; it splits and writes both halves).
- Hot-swap / display reconnect during runtime.
- Multiple panels of this type behind one MCU.

## 9. Testing

### 9.1 Native unit tests (`[env:native]`, Unity)

- **`test_row_splitter`**: feeds known 1304-px source rows into `RowSplitter`, asserts byte-exact output for `M_black / M_red / S_black / S_red`. Edge cases: all-white, all-black, all-red, alternating-pixel patterns, the column-648 boundary, and the unused 9th-bit padding in the slave half (columns 648..655).
- **`test_color_quantizer`**: feeds an RGB(A) fixture table into the BWR quantizer and asserts the `{white, black, red}` decision. Catches regressions in the threshold rule.

`WS1248B` itself is not unit-tested at the native level — testing register pokes against datasheet sequences would test the test, not the driver.

### 9.2 On-device smoke sketch

`examples/waveshare_1248b_smoke/`: a minimal Arduino sketch that calls `display_init()`, renders a known PNG fixture (4 quadrants: white, black, red, gradient) through the same `display_show_image` pipeline real firmware uses, then sleeps. Sketch and fixture PNG are committed. Not part of CI; rerun manually on real hardware.

### 9.3 Field test against `/api/display`

After §9.1 and §9.2 pass, flash `[env:waveshare_1248b]` and complete a full setup → fetch image → render → sleep cycle against a real TRMNL backend (or self-hosted). Verify:

- Captive-portal QR renders legibly on the dual-half panel.
- A normal dashboard image renders without M/S seam artifacts.
- `display_show_msg` error screens render correctly across the seam.
- Light-sleep current draw is within the same order of magnitude as the existing `BOARD_WAVESHARE_ESP32_DRIVER` baseline (gross sanity check, not a precision measurement).

### 9.4 Out of scope

- CI-on-hardware (project doesn't have it today).
- Power-consumption regression suite.
- Visual-diff testing against reference images.

## 10. Risks & contingencies

| Risk | Mitigation |
|---|---|
| Pin map differs from Waveshare's published assignment due to product revision. | Implementation step verifies pins against the ported reference driver and physical board before locking. |
| Dual-half SPI handshake has timing nuances not obvious from datasheet. | Approach B (GxEPD2) is the documented contingency. If porting stalls on timing, switch to GxEPD2 with a thin adapter and accept the firmware-size cost. |
| PNGdec's row callback emits rows in non-monotonic order for some encodings. | The adapter uses `setAddrWindow()` semantics (explicit row index in `writeHalfPlane`), so out-of-order rows are handled by addressing rather than by stream position. |
| BUSY pin glitches due to long traces on the ESP32 Driver Board. | 10 ms inter-poll sleep absorbs glitches; deglitch via two consecutive idle reads if needed (added during implementation only if observed). |

## 11. Future extensibility

Design preserves a clear path for the two deferred displays. Each becomes a new spec.

| Display | Path |
|---|---|
| **7.5" 640×384 BWY (SKU 14144, "C")** | Add `[env:waveshare_75c]`, `BOARD_WAVESHARE_75C` flag, and a `BB_EPAPER` branch in `display.cpp` selecting `EP74R_640x384`. No new driver code. Pin block in `DEV_Config.h` is the standard 6-pin shape. |
| **7.5" 640×384 BW (SKU 13187, V1)** | Either request a new BW panel entry in `bb_epaper` upstream, or port Waveshare's reference into a `WS75V1` class shaped like `WS1248B` (~150 LoC). New `[env:waveshare_75v1]` and `BOARD_WAVESHARE_75V1` flag. |

Neither requires reopening this spec. The boundary contract — `BOARD_WAVESHARE_*` env + `lib/trmnl_waveshare_*/` (when bb_epaper can't host the panel) + bb_epaper-shaped adapter — is the reusable shape.

The directory `lib/trmnl_waveshare_1248b/` is named for this display specifically, not generic, to keep the per-display lifecycle clean and avoid a kitchen-sink module.

## 12. Summary of new/changed files

**New:**
- `lib/trmnl_waveshare_1248b/library.properties`
- `lib/trmnl_waveshare_1248b/src/ws1248b.{h,cpp}`
- `lib/trmnl_waveshare_1248b/src/bbep_adapter_1248b.{h,cpp}`
- `lib/trmnl_waveshare_1248b/src/row_splitter.{h,cpp}`
- `lib/trmnl_waveshare_1248b/README.md`
- `examples/waveshare_1248b_smoke/` (sketch + fixture PNG)
- `test/test_row_splitter/`
- `test/test_color_quantizer/`
- `docs/superpowers/specs/2026-05-10-waveshare-12in48-driver-design.md` (this file)

**Changed:**
- `platformio.ini` — adds `[env:waveshare_1248b]`.
- `src/DEV_Config.h` — adds `#elif defined(BOARD_WAVESHARE_1248B)` block.
- `src/display.cpp` — adds `#elif defined(BOARD_WAVESHARE_1248B)` branch alongside the existing `BB_EPAPER` / `BOARD_X_CLASS` branches; instantiates the adapter as `bbep`.

**Untouched:**
- `main.cpp`, `src/api-client/*`, `lib/wificaptive/*`, OTA path, sleep/wake logic.

# Waveshare 12.48" 1304×984 BWR — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a working `[env:waveshare_1248b]` build that drives the Waveshare 12.48" e-Paper Module B (SKU 17299, 1304×984 BWR) from the Waveshare ESP32 Driver Board, integrated with the existing TRMNL update flow.

**Architecture:** Dedicated driver path under a new `BOARD_WAVESHARE_1248B` build flag (parallel to `BOARD_X_CLASS`). A new `lib/trmnl_waveshare_1248b/` provides three layers: `WS1248B` (low-level ported from Waveshare reference), `BBEPAdapter1248B` (presents bb_epaper's call surface to `display.cpp` via *composition* — embeds a real `BBEPAPER` instance as renderer; overrides `init`/`refresh`/`sleep` to dispatch through `WS1248B` instead of bb_epaper's SPI), and `RowSplitter` (pure logic, native-testable). Two full plane buffers (~313 KB) live in PSRAM; the adapter never holds a frame in stock SRAM.

**Tech Stack:** ESP32 (PSRAM-capable variant, e.g. ESP32-WROVER) · Arduino + ESP-IDF (existing framework combo) · PlatformIO `platform = espressif32@6.12.0` · `bitbank2/bb_epaper` (used as renderer only) · `bitbank2/PNGdec` · Unity (native unit tests).

**Reference source:** Waveshare's 12.48" reference, already present locally at:
- ESP32 port (preferred for command sequences and SPI/pin handling on our target MCU): `/Users/cleiton/projects/eletronics/12.48inch-e-paper/esp32/wifi/EPD_12in48b.{h,cpp}`
- Arduino reference (panel command sequences for V1 and V2): `/Users/cleiton/projects/eletronics/12.48inch-e-paper/Arduino/12in48epd/src/EPD_12in48b.{h,cpp}` and `EPD_12in48b_V2.{h,cpp}`

V2 is what's currently sold; default to V2 unless inspection of the user's panel marking proves otherwise. Task 1 verifies and locks the choice.

---

## Spec amendment (do first)

Two refinements emerged during planning. They don't change architecture, but the spec needs to reflect them so reviewers/implementers see the same story.

### Task 0: Amend the spec for PSRAM + composition

**Files:**
- Modify: `docs/superpowers/specs/2026-05-10-waveshare-12in48-driver-design.md`

- [ ] **Step 1: Edit §4 row 4 (Color decision) to lock in PSRAM**

Find row 4 of the table and replace its content with:

```
| 4 | Color: PNG-driven, color-aware from day one. Each decoded row is split into M_black / M_red / S_black / S_red and dispatched per half at refresh time. Two ~160 KB plane buffers live in **PSRAM** (required — see §7.1). |
```

- [ ] **Step 2: Edit §5.2 to switch adapter strategy to composition**

Replace the bullet `L2 — BBEPAdapter1248B` with:

```
- **L2 — `BBEPAdapter1248B`**: presents the bb_epaper call surface to `display.cpp` via *composition*. Embeds a real `BBEPAPER renderer` instance (configured for 1304×984, 3-color, buffers in PSRAM) and forwards all drawing calls (`fillScreen`, `drawPixel`, `setCursor`, `print`, `setFont`, `setTextColor`, `setAddrWindow`, `startWrite`, `writeData`, `loadG5Image`, `getCache`, `capabilities`, etc.) to it unchanged. Overrides `init()` (skips renderer's `initIO`; sets dimensions + 3-color cap), `refresh()` (reads renderer's plane buffers, splits via `RowSplitter`, dispatches per half through `WS1248B`), and `sleep()` (paired deep-sleep on both halves via `WS1248B`). Net effect: `display.cpp` is unchanged for text/primitive/G5 paths — bb_epaper does the rasterizing — but the panel I/O goes through our driver.
```

- [ ] **Step 3: Edit §6.1 to reflect that two plane buffers live in PSRAM**

Replace §6.1 with:

```
### 6.1 Memory & timing

- Frame buffers: two full 1304×984/8 = 160 392 byte 1bpp planes (black, red) owned by the embedded `BBEPAPER renderer`, allocated in PSRAM via `MALLOC_CAP_SPIRAM` (the existing bb_epaper code already supports this when PSRAM is present).
- Per-refresh additional working set: 4 × 82 B = ~328 B for row buffers used by `RowSplitter` during dispatch.
- SPI clock: 8 MHz (matches `bb_epaper`'s default and Waveshare's reference).
- Full refresh time: ~24–28 s per Waveshare datasheet. The existing display flow already waits for refresh completion; nothing in `main.cpp` needs to change.
```

- [ ] **Step 4: Edit §7.1 env block to require PSRAM**

Replace the env block in §7.1 with:

```ini
[env:waveshare_1248b]
extends = env:esp32dev
board = esp32dev          ; overridden by board_build below to enable PSRAM
board_build.f_cpu = 240000000L
board_build.f_flash = 80000000L
board_build.flash_mode = qio
board_build.psram = enabled    ; required: ~313 KB plane buffers live here
build_flags =
    ${env:esp32_base.build_flags}
    -D BOARD_WAVESHARE_1248B
    -D BOARD_HAS_PSRAM
    -D CONFIG_SPIRAM_USE_MALLOC=1
    -D PNG_MAX_BUFFERED_PIXELS=14984
    -D FAKE_BATTERY_VOLTAGE
lib_ignore =
```

Add the following sentence under the env block:

> Target hardware: the Waveshare ESP32 Driver Board paired with the 12.48" Module B uses an ESP32 module with 4 MB PSRAM (typically ESP32-WROVER-B). Boards without PSRAM are not supported by this env.

- [ ] **Step 5: Commit the spec amendment**

```bash
git add docs/superpowers/specs/2026-05-10-waveshare-12in48-driver-design.md
git commit -m "spec: amend 12.48 driver design for PSRAM + composition-based adapter"
```

---

## Tasks

### Task 1: Inspect the Waveshare 12.48" reference driver (already present locally)

**Files:** (no files modified — discovery + notes only)

The reference is already on disk at `/Users/cleiton/projects/eletronics/12.48inch-e-paper/`. There are two relevant ports inside it; we use the ESP32 port for SPI/pin patterns and the Arduino V2 source for the canonical command sequences.

- [ ] **Step 1: List the reference files we'll port from**

```bash
ls /Users/cleiton/projects/eletronics/12.48inch-e-paper/esp32/wifi/
ls /Users/cleiton/projects/eletronics/12.48inch-e-paper/Arduino/12in48epd/src/
```

Expected:
- ESP32 wifi/: `EPD_12in48b.{h,cpp}` (single-version port — Module B; check whether it matches V1 or V2 sequences)
- Arduino src/: `EPD_12in48b.{h,cpp}` (V1) **and** `EPD_12in48b_V2.{h,cpp}` (V2)

- [ ] **Step 2: Decide V1 vs V2**

```bash
head -40 /Users/cleiton/projects/eletronics/12.48inch-e-paper/Arduino/12in48epd/src/EPD_12in48b.h
head -40 /Users/cleiton/projects/eletronics/12.48inch-e-paper/Arduino/12in48epd/src/EPD_12in48b_V2.h
```

V2 is what Waveshare ships today as the 12.48" e-Paper Module B (SKU 17299), so **default to V2**. If the user's physical panel has a sticker indicating V1, switch the plan's port source files to the V1 variant (replace `_V2` with `` in all subsequent file references). Record the chosen variant in `lib/trmnl_waveshare_1248b/README.md` (Task 4).

- [ ] **Step 3: Identify the canonical pin assignment**

```bash
grep -E "BUSY_M|BUSY_S|CS_M|CS_S|DC_M|DC_S|RST_M|RST_S|EPD_M_|EPD_S_|PIN_|GPIO" \
  /Users/cleiton/projects/eletronics/12.48inch-e-paper/esp32/wifi/EPD_12in48b.h \
  /Users/cleiton/projects/eletronics/12.48inch-e-paper/esp32/wifi/EPD_12in48b.cpp 2>/dev/null
```

Record the eight GPIO numbers (`M_CS`, `M_DC`, `M_RST`, `M_BUSY`, `S_CS`, `S_DC`, `S_RST`, `S_BUSY`) plus `SCK` and `MOSI`. These go into Task 3's `DEV_Config.h` edit, replacing the placeholders.

- [ ] **Step 4: Read the init / display / sleep sequences end-to-end**

```bash
cat /Users/cleiton/projects/eletronics/12.48inch-e-paper/Arduino/12in48epd/src/EPD_12in48b_V2.cpp
```

(If the chosen variant is V1, read `EPD_12in48b.cpp` instead.) Take notes (kept in a scratch file, **not committed**) on:
- **Init:** which commands are sent to M first, which to S, where the sequences diverge. Map each command byte to its Waveshare-comment name (e.g. `0x00 // PANEL_SETTING`).
- **Display:** which command selects the black plane (typically `0x10`) and which selects the red plane (typically `0x13`). Whether each half receives data in row-major top-to-bottom order. Whether the slave half receives data left-to-right (cols 648..1303) or right-to-left.
- **Refresh:** the `EPD_12in48B_TurnOnDisplay`-equivalent sequence — which CS is asserted for the master refresh command, what BUSY waits look like.
- **Sleep:** the deep-sleep command(s) per half (typically `0x02` POWER_OFF then `0x07 0xA5` DEEP_SLEEP).
- **BUSY polarity:** Waveshare's `ReadBusy` loops on `digitalRead(BUSY) == 0` for some panels and `== 1` for others. Note which one V2 uses — this determines the polarity check in `WS1248B::waitBusy` (Task 6 step 2).

These notes drive Tasks 6–8.

- [ ] **Step 5: No commit for this task — reference repo lives outside the project tree.**

---

### Task 2: Add the new PlatformIO env

**Files:**
- Modify: `platformio.ini`

- [ ] **Step 1: Append the new env after the existing `[env:waveshare-esp32-driver]` block**

Locate the line `[env:waveshare-esp32-driver]` in `platformio.ini`. After the closing of that block (the `-D BOARD_WAVESHARE_ESP32_DRIVER` line), insert:

```ini

[env:waveshare_1248b]
extends = env:esp32dev
board = esp32dev
board_build.f_cpu = 240000000L
board_build.f_flash = 80000000L
board_build.flash_mode = qio
board_build.psram = enabled
build_flags =
    ${env:esp32_base.build_flags}
    -D BOARD_WAVESHARE_1248B
    -D BOARD_HAS_PSRAM
    -D CONFIG_SPIRAM_USE_MALLOC=1
    -D PNG_MAX_BUFFERED_PIXELS=14984
    -D FAKE_BATTERY_VOLTAGE
```

- [ ] **Step 2: Verify the env exists**

```bash
cd /Users/cleiton/projects/eletronics/mytrmnl/trmnl-firmware
pio project config | grep -A 2 waveshare_1248b
```

Expected: the env appears with `BOARD_WAVESHARE_1248B` and `BOARD_HAS_PSRAM` flags.

- [ ] **Step 3: Commit**

```bash
git add platformio.ini
git commit -m "build: add [env:waveshare_1248b] for Waveshare 12.48 BWR display"
```

---

### Task 3: Add `BOARD_WAVESHARE_1248B` pin block to `DEV_Config.h`

**Files:**
- Modify: `src/DEV_Config.h:135` (the `#elif defined (BOARD_X_CLASS)` line — we add our block before it)

- [ ] **Step 1: Insert the new pin block before the `BOARD_X_CLASS` branch**

Open `src/DEV_Config.h` and find the line `#elif defined (BOARD_X_CLASS)`. Insert this block immediately before it (so `BOARD_X_CLASS` remains the last `#elif`):

```c
#elif defined(BOARD_WAVESHARE_1248B)
   // Pin definition for Waveshare ESP32 Driver Board + 12.48" Module B (SKU 17299)
   // Numbers below come from Waveshare's reference driver (see Task 1, Step 3)
   // and must match the actual hardware. Update if Task 1 reveals different
   // values for the user's specific board revision.
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

   // Intentionally NOT defined: EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN.
   // BOARD_WAVESHARE_1248B does not define BB_EPAPER, so code paths that
   // reference those single-half macros are not reachable for this board.

   #define FAKE_BATTERY_VOLTAGE
```

- [ ] **Step 2: Replace placeholder GPIO numbers with the values discovered in Task 1, Step 3**

The numbers above are starting placeholders. If the reference driver's pin assignments differ, edit each `#define EPD_*_PIN` line to match. Common variations exist between ESP32 Driver Board revisions.

- [ ] **Step 3: Verify no other `BOARD_WAVESHARE_1248B` references exist yet that would break the build**

```bash
grep -rn "BOARD_WAVESHARE_1248B" src/ include/ lib/ 2>/dev/null
```

Expected: only matches in `src/DEV_Config.h` (and `platformio.ini` from Task 2). No matches in `src/display.cpp` or `src/main.cpp` yet.

- [ ] **Step 4: Commit**

```bash
git add src/DEV_Config.h
git commit -m "config: add BOARD_WAVESHARE_1248B pin block for dual-half SPI"
```

---

### Task 4: Create the library skeleton

**Files:**
- Create: `lib/trmnl_waveshare_1248b/library.properties`
- Create: `lib/trmnl_waveshare_1248b/README.md`
- Create: `lib/trmnl_waveshare_1248b/src/.gitkeep`

- [ ] **Step 1: Create the directory tree**

```bash
mkdir -p /Users/cleiton/projects/eletronics/mytrmnl/trmnl-firmware/lib/trmnl_waveshare_1248b/src
touch /Users/cleiton/projects/eletronics/mytrmnl/trmnl-firmware/lib/trmnl_waveshare_1248b/src/.gitkeep
```

- [ ] **Step 2: Write `library.properties`**

```properties
name=trmnl_waveshare_1248b
version=0.1.0
author=TRMNL
maintainer=TRMNL
sentence=Driver for Waveshare 12.48 inch B/W/R e-paper (Module B, SKU 17299) on the Waveshare ESP32 Driver Board.
paragraph=Provides a low-level WS1248B driver class (dual-controller SPI) plus a bb_epaper-shaped adapter that uses BBEPAPER as renderer and dispatches refresh through WS1248B. Self-contained; PSRAM required.
category=Display
url=
architectures=esp32
includes=bbep_adapter_1248b.h
depends=bb_epaper
```

- [ ] **Step 3: Write `README.md`**

```markdown
# trmnl_waveshare_1248b

Driver integration for the Waveshare 12.48" e-Paper Module B (1304×984 black/white/red, SKU 17299) on the Waveshare ESP32 Driver Board.

## Design

See `docs/superpowers/specs/2026-05-10-waveshare-12in48-driver-design.md` for the full design rationale.

## Layers

- `ws1248b.{h,cpp}` — low-level driver ported from Waveshare's `e-Paper/Arduino/epd12in48b/` reference.
- `bbep_adapter_1248b.{h,cpp}` — composition-based adapter exposing the bb_epaper call surface to `display.cpp`. Embeds a real `BBEPAPER` instance as renderer; overrides `init`, `refresh`, `sleep` to dispatch through `WS1248B`.
- `row_splitter.{h,cpp}` — pure logic: splits 1304-px plane rows into master/slave halves.

## Hardware requirements

- ESP32 module with PSRAM (e.g. ESP32-WROVER-B). Two ~160 KB plane buffers live in PSRAM.
- Waveshare ESP32 Driver Board (or pin-compatible).
- 12.48" Module B connected via the Waveshare 33-pin FPC adapter.

## Reference variant

This library is ported from Waveshare reference driver variant: **<V1 | V2 — recorded during Task 1, Step 2>**.
```

Update the last line of the README to record which reference variant (V1 vs V2) was chosen in Task 1, Step 2.

- [ ] **Step 4: Commit**

```bash
git add lib/trmnl_waveshare_1248b/
git commit -m "lib: skeleton for trmnl_waveshare_1248b (library.properties, README)"
```

---

### Task 5: `RowSplitter` — pure logic, TDD

**Files:**
- Create: `lib/trmnl_waveshare_1248b/src/row_splitter.h`
- Create: `lib/trmnl_waveshare_1248b/src/row_splitter.cpp`
- Create: `test/test_row_splitter/test_row_splitter.cpp`

`RowSplitter` is the only non-trivial piece of logic that doesn't require hardware to test. It takes a packed 1bpp row of 1304 pixels (164 bytes) and emits two packed 1bpp halves: 82 bytes for the master (covers columns 0..647, padded to 656 bits) and 82 bytes for the slave (covers columns 648..1303 mapped to slave 0..655). Bit order matches bb_epaper convention: MSB = leftmost pixel within each byte.

- [ ] **Step 1: Write the failing test (Unity, `[env:native]`)**

Create `test/test_row_splitter/test_row_splitter.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "row_splitter.h"

using trmnl::RowSplitter;

static const int FULL_BYTES = 164;   // 1304 / 8 = 163, +1 pad => use 164 for ceil
static const int HALF_BYTES = 82;    // 656 / 8 = 82 (slave controller's 656-wide buffer)

void setUp(void)    {}
void tearDown(void) {}

static void test_all_white_row_yields_all_zero_halves(void) {
    uint8_t src[FULL_BYTES];
    memset(src, 0x00, FULL_BYTES);
    uint8_t m[HALF_BYTES] = {0xFF};
    uint8_t s[HALF_BYTES] = {0xFF};

    RowSplitter::split(src, 1304, m, s);

    for (int i = 0; i < HALF_BYTES; ++i) {
        TEST_ASSERT_EQUAL_HEX8(0x00, m[i]);
        TEST_ASSERT_EQUAL_HEX8(0x00, s[i]);
    }
}

static void test_all_black_row_yields_all_ff_visible_then_padding(void) {
    uint8_t src[FULL_BYTES];
    memset(src, 0xFF, FULL_BYTES);
    uint8_t m[HALF_BYTES] = {0};
    uint8_t s[HALF_BYTES] = {0};

    RowSplitter::split(src, 1304, m, s);

    // Master covers cols 0..647 (648 pixels = 81 full bytes + 0 leftover bits).
    for (int i = 0; i < 81; ++i) TEST_ASSERT_EQUAL_HEX8(0xFF, m[i]);
    // Byte 81 of master: all 8 bits are padding columns 648..655, must be 0.
    TEST_ASSERT_EQUAL_HEX8(0x00, m[81]);

    // Slave covers cols 648..1303 = 656 pixels = 82 full bytes.
    for (int i = 0; i < HALF_BYTES; ++i) TEST_ASSERT_EQUAL_HEX8(0xFF, s[i]);
}

static void test_boundary_pixel_647_goes_to_master(void) {
    uint8_t src[FULL_BYTES];
    memset(src, 0x00, FULL_BYTES);
    // Set only column 647 (last master column): byte 80, bit 0 (LSB of byte 80).
    src[80] = 0x01;
    uint8_t m[HALF_BYTES] = {0};
    uint8_t s[HALF_BYTES] = {0xFF};

    RowSplitter::split(src, 1304, m, s);

    TEST_ASSERT_EQUAL_HEX8(0x01, m[80]);
    TEST_ASSERT_EQUAL_HEX8(0x00, m[81]);    // padding byte
    for (int i = 0; i < HALF_BYTES; ++i) {
        TEST_ASSERT_EQUAL_HEX8(0x00, s[i]); // slave must be untouched
    }
}

static void test_boundary_pixel_648_goes_to_slave_bit_msb(void) {
    uint8_t src[FULL_BYTES];
    memset(src, 0x00, FULL_BYTES);
    // Column 648 = byte 81, bit 7 (MSB).
    src[81] = 0x80;
    uint8_t m[HALF_BYTES] = {0xFF};
    uint8_t s[HALF_BYTES] = {0};

    RowSplitter::split(src, 1304, m, s);

    for (int i = 0; i < HALF_BYTES; ++i) TEST_ASSERT_EQUAL_HEX8(0x00, m[i]);
    TEST_ASSERT_EQUAL_HEX8(0x80, s[0]);
}

static void test_alternating_pattern_round_trips(void) {
    uint8_t src[FULL_BYTES];
    for (int i = 0; i < FULL_BYTES; ++i) src[i] = (i & 1) ? 0xAA : 0x55;
    uint8_t m[HALF_BYTES] = {0};
    uint8_t s[HALF_BYTES] = {0};

    RowSplitter::split(src, 1304, m, s);

    // First 81 master bytes mirror src[0..80].
    for (int i = 0; i < 81; ++i) TEST_ASSERT_EQUAL_HEX8(src[i], m[i]);
    // First 82 slave bytes mirror src[81..162]. Note slave starts at col 648
    // which is byte 81 bit 7, so slave byte 0 == src[81] when col 648 is the
    // top bit of src[81].
    for (int i = 0; i < HALF_BYTES; ++i) TEST_ASSERT_EQUAL_HEX8(src[81 + i], s[i]);
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_all_white_row_yields_all_zero_halves);
    RUN_TEST(test_all_black_row_yields_all_ff_visible_then_padding);
    RUN_TEST(test_boundary_pixel_647_goes_to_master);
    RUN_TEST(test_boundary_pixel_648_goes_to_slave_bit_msb);
    RUN_TEST(test_alternating_pattern_round_trips);
    return UNITY_END();
}

int main(int, char**) { return runUnityTests(); }
```

- [ ] **Step 2: Run the test — must fail**

```bash
cd /Users/cleiton/projects/eletronics/mytrmnl/trmnl-firmware
pio test -e native -f test_row_splitter
```

Expected: compilation failure (`row_splitter.h: No such file or directory`). That is the desired "red" state.

- [ ] **Step 3: Write the header `lib/trmnl_waveshare_1248b/src/row_splitter.h`**

```cpp
#pragma once
#include <stdint.h>

namespace trmnl {

class RowSplitter {
public:
    // Splits a packed 1bpp row of `width` pixels (MSB = leftmost pixel within
    // each byte, bb_epaper convention) into two packed 1bpp halves:
    //   master[0..81] covers source columns [0..647], with bits in columns
    //                 [648..655] cleared to 0 (padding).
    //   slave [0..81] covers source columns [648..1303] mapped to slave
    //                 controller columns [0..655].
    //
    // `width` must be 1304. `master` and `slave` must point to >= 82 bytes.
    // `src` must point to >= ceil(width/8) bytes.
    static void split(const uint8_t* src, int width, uint8_t* master, uint8_t* slave);
};

} // namespace trmnl
```

- [ ] **Step 4: Write the implementation `lib/trmnl_waveshare_1248b/src/row_splitter.cpp`**

```cpp
#include "row_splitter.h"
#include <cstring>

namespace trmnl {

void RowSplitter::split(const uint8_t* src, int width, uint8_t* master, uint8_t* slave) {
    (void)width;  // fixed at 1304 for this panel — kept in signature for future panels.

    // Master: 648 visible columns = 81 whole bytes + 0 trailing bits.
    // Byte 81 of master is padding columns 648..655 — must be cleared.
    std::memcpy(master, src, 81);
    master[81] = 0x00;

    // Slave: 656 columns starting at source column 648 (== bit 7 of src[81]).
    // Because 648 is byte-aligned (648 % 8 == 0), the slave bytes are simply
    // src[81..162] copied straight through.
    std::memcpy(slave, src + 81, 82);
}

} // namespace trmnl
```

- [ ] **Step 5: Run the test — must pass**

```bash
pio test -e native -f test_row_splitter
```

Expected: 5 tests pass.

- [ ] **Step 6: Commit**

```bash
git add lib/trmnl_waveshare_1248b/src/row_splitter.{h,cpp} test/test_row_splitter/
git commit -m "lib(1248b): RowSplitter — split 1304-px row into master+slave halves"
```

---

### Task 6: `WS1248B` driver — header and bring-up (init only)

**Files:**
- Create: `lib/trmnl_waveshare_1248b/src/ws1248b.h`
- Create: `lib/trmnl_waveshare_1248b/src/ws1248b.cpp`

We split the driver across three tasks (init, write+refresh, sleep) because the porting work is mechanical and each step is independently verifiable on hardware.

- [ ] **Step 1: Write `ws1248b.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace trmnl {

enum class Half : uint8_t { Master = 0, Slave = 1 };
enum class Plane : uint8_t { Black = 0, Red = 1 };

class WS1248B {
public:
    // Total panel dimensions (logical, visible).
    static constexpr int WIDTH  = 1304;
    static constexpr int HEIGHT = 984;
    static constexpr int HALF_WIDTH_VISIBLE = 648;
    static constexpr int HALF_WIDTH_BUFFER  = 656;   // slave controller pads to 656
    static constexpr int HALF_HEIGHT        = 984;
    static constexpr int HALF_ROW_BYTES     = HALF_WIDTH_BUFFER / 8;  // 82

    WS1248B(int sck, int mosi,
            int m_cs, int m_dc, int m_rst, int m_busy,
            int s_cs, int s_dc, int s_rst, int s_busy);

    // Configures pins, brings up SPI, runs the paired full-init sequence.
    // Returns true on success. Logs via Arduino-Log on failure.
    bool init();

    // Send a single packed 1bpp row for one half / one plane.
    // `row` is the y-coordinate in panel space (0..HEIGHT-1).
    // `len` must equal HALF_ROW_BYTES (82).
    void writeHalfPlane(Half half, Plane plane, int row, const uint8_t* data, int len);

    // Issue the paired refresh command and wait for both BUSY pins to idle.
    // `timeout_ms` defaults to 40 000 (full-refresh budget + margin).
    bool refresh(uint32_t timeout_ms = 40000);

    // Paired deep-sleep on both halves.
    void sleep();

    bool panelFailed(Half h) const { return _failed[(int)h]; }

private:
    void hardReset(Half h);
    void writeCmd(Half h, uint8_t cmd);
    void writeData(Half h, uint8_t b);
    void writeData(Half h, const uint8_t* buf, size_t len);
    bool waitBusy(Half h, uint32_t timeout_ms);
    void setCsHigh(Half h);
    void setCsLow(Half h);

    int _sck, _mosi;
    int _cs[2], _dc[2], _rst[2], _busy[2];
    bool _failed[2] = { false, false };
};

} // namespace trmnl
```

- [ ] **Step 2: Write the constructor + pin setup + `init()` in `ws1248b.cpp`**

Create `lib/trmnl_waveshare_1248b/src/ws1248b.cpp` with this skeleton:

```cpp
#include "ws1248b.h"
#include <SPI.h>
#include <ArduinoLog.h>

namespace trmnl {

WS1248B::WS1248B(int sck, int mosi,
                 int m_cs, int m_dc, int m_rst, int m_busy,
                 int s_cs, int s_dc, int s_rst, int s_busy)
    : _sck(sck), _mosi(mosi) {
    _cs[(int)Half::Master]   = m_cs;
    _dc[(int)Half::Master]   = m_dc;
    _rst[(int)Half::Master]  = m_rst;
    _busy[(int)Half::Master] = m_busy;
    _cs[(int)Half::Slave]    = s_cs;
    _dc[(int)Half::Slave]    = s_dc;
    _rst[(int)Half::Slave]   = s_rst;
    _busy[(int)Half::Slave]  = s_busy;
}

void WS1248B::setCsHigh(Half h) { digitalWrite(_cs[(int)h], HIGH); }
void WS1248B::setCsLow(Half h)  { digitalWrite(_cs[(int)h], LOW);  }

void WS1248B::hardReset(Half h) {
    digitalWrite(_rst[(int)h], HIGH); delay(20);
    digitalWrite(_rst[(int)h], LOW);  delay(4);
    digitalWrite(_rst[(int)h], HIGH); delay(20);
}

void WS1248B::writeCmd(Half h, uint8_t cmd) {
    digitalWrite(_dc[(int)h], LOW);
    setCsLow(h);
    SPI.transfer(cmd);
    setCsHigh(h);
}

void WS1248B::writeData(Half h, uint8_t b) {
    digitalWrite(_dc[(int)h], HIGH);
    setCsLow(h);
    SPI.transfer(b);
    setCsHigh(h);
}

void WS1248B::writeData(Half h, const uint8_t* buf, size_t len) {
    digitalWrite(_dc[(int)h], HIGH);
    setCsLow(h);
    // Avoid the per-byte setCs cost on bulk writes.
    SPI.writeBytes(buf, len);
    setCsHigh(h);
}

bool WS1248B::waitBusy(Half h, uint32_t timeout_ms) {
    // Waveshare reference treats BUSY as: LOW = idle. Verify against the
    // reference driver in Task 1; if the reference polls HIGH = idle, flip
    // the test below.
    const uint32_t deadline = millis() + timeout_ms;
    while (digitalRead(_busy[(int)h]) == HIGH) {
        if ((int32_t)(millis() - deadline) >= 0) {
            Log.errorln(F("WS1248B::waitBusy timeout on half %d"), (int)h);
            return false;
        }
        delay(10);
    }
    return true;
}

bool WS1248B::init() {
    // Pin modes
    pinMode(_sck,  OUTPUT);
    pinMode(_mosi, OUTPUT);
    for (int i = 0; i < 2; ++i) {
        pinMode(_cs[i],   OUTPUT); digitalWrite(_cs[i],  HIGH);
        pinMode(_dc[i],   OUTPUT); digitalWrite(_dc[i],  LOW);
        pinMode(_rst[i],  OUTPUT); digitalWrite(_rst[i], HIGH);
        pinMode(_busy[i], INPUT);
    }

    // SPI bring-up — 8 MHz matches bb_epaper default and Waveshare reference.
    SPI.begin(_sck, /*miso*/ -1, _mosi, /*ss*/ -1);
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

    // Hard reset both halves.
    hardReset(Half::Master);
    hardReset(Half::Slave);

    // === PORTED INIT SEQUENCE (from Waveshare EPD_12in48b_V2 reference) ===
    //
    // Transcribe the body of EPD_12in48B_V2_Init() (or EPD_12in48B_Init() if
    // the user's panel is the V1 variant) from
    //   /Users/cleiton/projects/eletronics/12.48inch-e-paper/Arduino/12in48epd/src/EPD_12in48b_V2.cpp
    // verbatim into the block below. Map each Waveshare call as follows:
    //   EPD_M_SendCommand(c)    -> writeCmd(Half::Master, c)
    //   EPD_M_SendData(d)       -> writeData(Half::Master, d)
    //   EPD_S_SendCommand(c)    -> writeCmd(Half::Slave,  c)
    //   EPD_S_SendData(d)       -> writeData(Half::Slave,  d)
    //   EPD_M_ReadBusy()        -> if (!waitBusy(Half::Master, 5000)) { ... }
    //   EPD_S_ReadBusy()        -> if (!waitBusy(Half::Slave,  5000)) { ... }
    //   DEV_Delay_ms(n)         -> delay(n)
    //
    // On any waitBusy timeout, set _failed[half] = true and continue
    // initializing the other half (so the panel can still partially function
    // if one controller is dead).
    //
    // <<< INSERT VERBATIM PORT HERE >>>

    return !_failed[0] && !_failed[1];
}

} // namespace trmnl
```

- [ ] **Step 3: Port the init sequence verbatim**

Open `/Users/cleiton/projects/eletronics/12.48inch-e-paper/Arduino/12in48epd/src/EPD_12in48b_V2.cpp`. Locate `EPD_12in48B_Init()` (or `EPD_12in48B_V2_Init()`). Translate it line-by-line into the `<<< INSERT VERBATIM PORT HERE >>>` block using the mapping in the comment above. Do not optimize, reorder, or "improve" — port literally.

- [ ] **Step 4: Build the env to verify it compiles**

```bash
cd /Users/cleiton/projects/eletronics/mytrmnl/trmnl-firmware
pio run -e waveshare_1248b 2>&1 | tail -50
```

Expected: compilation error mentioning missing references in `display.cpp` (we haven't wired in the adapter yet), **but no errors in `ws1248b.cpp`**. If `ws1248b.cpp` itself fails to compile, fix syntax issues from the port before moving on.

- [ ] **Step 5: Commit**

```bash
git add lib/trmnl_waveshare_1248b/src/ws1248b.{h,cpp}
git commit -m "lib(1248b): WS1248B header + init sequence ported from Waveshare reference"
```

---

### Task 7: `WS1248B` — frame write and refresh

**Files:**
- Modify: `lib/trmnl_waveshare_1248b/src/ws1248b.cpp`

The reference driver typically exposes `EPD_12in48B_Display(const uint8_t *blackimage, const uint8_t *redimage)` that pushes whole frame buffers in one shot. We split this into `writeHalfPlane(half, plane, row, data, len)` so the adapter can stream row-by-row from bb_epaper's internal buffers without an extra copy.

- [ ] **Step 1: Read the reference's display function**

```bash
sed -n '/EPD_12in48B.*Display(/,/^}/p' /Users/cleiton/projects/eletronics/12.48inch-e-paper/Arduino/12in48epd/src/EPD_12in48b_V2.cpp
```

Note:
- Which command byte selects the black plane (typically `0x10`) and which selects the red plane (typically `0x13`). Verify against your variant.
- Whether each half receives data in row-major top-to-bottom order, and whether the slave half's data is sent left-to-right (cols 648..1303) or right-to-left.
- Whether the per-half data write is one bulk transfer or a per-row loop.

Record the answers as a comment block at the top of the next step's implementation.

- [ ] **Step 2: Implement `writeHalfPlane` in `ws1248b.cpp`**

Append to `ws1248b.cpp` (inside the `namespace trmnl` block, after the existing methods):

```cpp
void WS1248B::writeHalfPlane(Half h, Plane p, int row, const uint8_t* data, int len) {
    if (_failed[(int)h]) return;
    if (len != HALF_ROW_BYTES) {
        Log.errorln(F("WS1248B::writeHalfPlane bad len=%d (expected %d)"), len, HALF_ROW_BYTES);
        return;
    }
    if (row < 0 || row >= HEIGHT) return;

    // For Waveshare's 12.48 Module B, the reference issues ONE command
    // (0x10 for black, 0x13 for red) per half per FULL frame, followed by
    // 82*984 bytes. We replicate that pattern: the first call for a given
    // (half, plane) issues the command; subsequent calls for the same
    // (half, plane) just keep streaming bytes. The adapter is responsible
    // for resetting this state between frames via beginFrame()/endFrame()
    // pairs — added in the next step.
    //
    // Therefore: writeHalfPlane assumes the (half, plane) is already in
    // "streaming" mode. Mode entry is via beginPlane() below.

    writeData(h, data, len);
}

void WS1248B::beginPlane(Half h, Plane p) {
    if (_failed[(int)h]) return;
    const uint8_t cmd = (p == Plane::Black) ? 0x10 : 0x13;
    writeCmd(h, cmd);
}

void WS1248B::endPlane(Half /*h*/, Plane /*p*/) {
    // No-op for Module B; some Waveshare panels need a trailing command —
    // verify against the reference's display function.
}

bool WS1248B::refresh(uint32_t timeout_ms) {
    // Reference sequence (typical for Module B):
    //   1. M: 0x12 (DISPLAY_REFRESH), wait busy
    //   2. S: 0x12 (DISPLAY_REFRESH), wait busy
    // Some variants drive only the master refresh and the slave follows
    // automatically — verify against the reference's
    // EPD_12in48B_TurnOnDisplay (or equivalent) function and match exactly.

    // <<< INSERT VERBATIM PORT OF REFRESH SEQUENCE HERE >>>

    // Wait both halves regardless of which one(s) we commanded — paired
    // lock-step per the spec.
    bool ok_m = waitBusy(Half::Master, timeout_ms);
    bool ok_s = waitBusy(Half::Slave,  timeout_ms);
    return ok_m && ok_s;
}

```

Add `beginPlane` and `endPlane` to `ws1248b.h` under the public section:

```cpp
    void beginPlane(Half half, Plane plane);
    void endPlane(Half half, Plane plane);
```

- [ ] **Step 3: Port the refresh sequence verbatim**

Open the reference. Locate `EPD_12in48B_TurnOnDisplay()` (or whatever function transitions from "data loaded" to "panel updates"). Translate into the `<<< INSERT VERBATIM PORT OF REFRESH SEQUENCE HERE >>>` block using the same mapping rules from Task 6 Step 3.

- [ ] **Step 4: Build to verify the library still compiles**

```bash
pio run -e waveshare_1248b 2>&1 | grep -E "ws1248b\.cpp.*error" || echo "ws1248b.cpp OK"
```

Expected: `ws1248b.cpp OK`. (Other compile errors from `display.cpp` are still expected at this stage.)

- [ ] **Step 5: Commit**

```bash
git add lib/trmnl_waveshare_1248b/src/ws1248b.{h,cpp}
git commit -m "lib(1248b): WS1248B writeHalfPlane + paired refresh"
```

---

### Task 8: `WS1248B` — sleep

**Files:**
- Modify: `lib/trmnl_waveshare_1248b/src/ws1248b.cpp`

- [ ] **Step 1: Read the reference sleep function**

```bash
sed -n '/EPD_12in48B.*Sleep/,/^}/p' /Users/cleiton/projects/eletronics/12.48inch-e-paper/Arduino/12in48epd/src/EPD_12in48b_V2.cpp
```

- [ ] **Step 2: Append `sleep()` to `ws1248b.cpp`**

```cpp
void WS1248B::sleep() {
    // Reference's EPD_12in48B_Sleep() typically:
    //   1. Sends 0x02 (POWER_OFF) to each half, waits busy.
    //   2. Sends 0x07 (DEEP_SLEEP) followed by data byte 0xA5 to each half.
    // Verify exact sequence against the reference and port verbatim.

    // <<< INSERT VERBATIM PORT OF SLEEP SEQUENCE HERE >>>
}
```

- [ ] **Step 3: Build**

```bash
pio run -e waveshare_1248b 2>&1 | grep -E "ws1248b\.cpp.*error" || echo "ws1248b.cpp OK"
```

Expected: `ws1248b.cpp OK`.

- [ ] **Step 4: Commit**

```bash
git add lib/trmnl_waveshare_1248b/src/ws1248b.cpp
git commit -m "lib(1248b): WS1248B paired sleep sequence"
```

---

### Task 9: `BBEPAdapter1248B` — composition skeleton

**Files:**
- Create: `lib/trmnl_waveshare_1248b/src/bbep_adapter_1248b.h`
- Create: `lib/trmnl_waveshare_1248b/src/bbep_adapter_1248b.cpp`

The adapter wraps a `BBEPAPER` renderer and a `WS1248B` driver. `display.cpp` sees only the adapter. Most calls forward straight to the renderer; only `init`, `refresh`, and `sleep` are overridden to swap bb_epaper's SPI output for our driver.

- [ ] **Step 1: Write `bbep_adapter_1248b.h`**

```cpp
#pragma once
#include <Arduino.h>
#include "bb_epaper.h"
#include "ws1248b.h"

namespace trmnl {

class BBEPAdapter1248B {
public:
    BBEPAdapter1248B();

    // ---- one-time setup, called from display_init() ----
    // Mirrors bb_epaper's initIO() shape so display.cpp doesn't fork. The
    // SPI/DC/RST/BUSY/CS args here are IGNORED: we use the M/S pin macros
    // from DEV_Config.h directly. The signature exists only to swallow the
    // call display.cpp already makes today.
    void initIO(int dc, int rst, int busy, int cs, int mosi, int sck, long speed);

    // Configures the renderer for 1304x984, 3-color, PSRAM-backed.
    bool init();

    // ---- forwarded to renderer (rasterizing only) ----
    int  width()  { return _renderer.width();  }
    int  height() { return _renderer.height(); }
    void fillScreen(int c)                    { _renderer.fillScreen(c); }
    void drawPixel(int x, int y, int c)       { _renderer.drawPixel(x, y, c); }
    void setFont(const void* f)               { _renderer.setFont(f); }
    void setTextColor(int fg, int bg)         { _renderer.setTextColor(fg, bg); }
    void setCursor(int x, int y)              { _renderer.setCursor(x, y); }
    void print(const char* s)                 { _renderer.print(s); }
    void setAddrWindow(int x, int y, int w, int h) { _renderer.setAddrWindow(x, y, w, h); }
    void startWrite(int plane)                { _renderer.startWrite(plane); }
    void writeData(const uint8_t* d, int n)   { _renderer.writeData(d, n); }
    int  capabilities()                       { return _renderer.capabilities(); }
    int  getPanelType()                       { return _renderer.getPanelType(); }
    void setPanelType(int t)                  { _renderer.setPanelType(t); }
    int  getPreviousMode()                    { return _renderer.getPreviousMode(); }
    void setPreviousMode(int m)               { _renderer.setPreviousMode(m); }
    int  getMode()                            { return _renderer.getMode(); }
    void setMode(int m)                       { _renderer.setMode(m); }
    uint8_t* getCache()                       { return _renderer.getCache(); }
    uint8_t* currentBuffer()                  { return _renderer.currentBuffer(); }
    uint8_t* previousBuffer()                 { return _renderer.previousBuffer(); }
    uint8_t* tempBuffer()                     { return _renderer.tempBuffer(); }
    int  allocBuffer(bool clear = true)       { return _renderer.allocBuffer(clear); }
    void writePlane(int mode = 0)             { _renderer.writePlane(mode); (void)mode; }
    void loadG5Image(const uint8_t* d, int x, int y, int bg, int fg, float scale = 1.0f) {
        _renderer.loadG5Image(d, x, y, bg, fg, scale);
    }
    void setLightSleep(bool /*on*/)           { /* no-op; light-sleep is MCU-side only on this board */ }
    void setPasses(int /*a*/, int /*b*/)      { /* no-op */ }
    void setPanelSize(int w, int h, int flags, int offset) {
        _renderer.setPanelSize(w, h, flags, offset);
    }

    // ---- overridden: dispatch through WS1248B ----
    // refresh: read renderer's plane buffers, split into halves, push to panel.
    // `mode` mirrors bb_epaper's REFRESH_FULL/REFRESH_FAST argument; on this
    // panel we always do a full refresh — `mode` is ignored.
    bool refresh(int mode = 0, bool wait = true);
    void sleep();

private:
    BBEPAPER _renderer;
    WS1248B  _driver;
};

} // namespace trmnl
```

- [ ] **Step 2: Write `bbep_adapter_1248b.cpp` — constructor, `init()`, stubs for `refresh`/`sleep`**

```cpp
#include "bbep_adapter_1248b.h"
#include "DEV_Config.h"
#include "row_splitter.h"
#include <ArduinoLog.h>

namespace trmnl {

BBEPAdapter1248B::BBEPAdapter1248B()
    : _renderer(EP74R_640x384),  // base 3-color panel, dimensions overridden in init()
      _driver(EPD_SCK_PIN, EPD_MOSI_PIN,
              EPD_M_CS_PIN, EPD_M_DC_PIN, EPD_M_RST_PIN, EPD_M_BUSY_PIN,
              EPD_S_CS_PIN, EPD_S_DC_PIN, EPD_S_RST_PIN, EPD_S_BUSY_PIN) {
}

void BBEPAdapter1248B::initIO(int, int, int, int, int, int, long) {
    // Signature parity with bb_epaper. Pins are taken from DEV_Config.h
    // macros in the constructor, not from args.
}

bool BBEPAdapter1248B::init() {
    // Tell the renderer the real panel geometry. setPanelSize() accepts
    // (w, h, flags, offset). 1304 wide, 984 tall, no mirror, no offset.
    _renderer.setPanelSize(1304, 984, 0, 0);

    // Allocate plane buffers (renderer uses MALLOC_CAP_SPIRAM when
    // BOARD_HAS_PSRAM is defined — see bb_epaper's bb_ep.inl).
    if (_renderer.allocBuffer() != BBEP_SUCCESS) {
        Log.errorln(F("BBEPAdapter1248B: allocBuffer failed (PSRAM missing?)"));
        return false;
    }

    // Bring up our own SPI driver. Note: we do NOT call _renderer.initIO()
    // — bb_epaper's SPI output is bypassed entirely; we only use it as a
    // rasterizer.
    return _driver.init();
}

bool BBEPAdapter1248B::refresh(int /*mode*/, bool /*wait*/) {
    // Plane buffer layout inside bb_epaper:
    //   - currentBuffer() returns plane 0 (black, 1bpp).
    //   - For 3-color panels with BBEP_3COLOR capability, plane 1 (red)
    //     is allocated contiguously after plane 0. The exact accessor is
    //     verified at implementation time via bb_epaper's source; if there
    //     is no dedicated red-plane accessor, compute it as
    //         red = currentBuffer() + (width()/8) * height();
    //
    // The bytes-per-row for the renderer's plane buffer is width()/8 = 163.
    // Our RowSplitter expects a 164-byte input (1304 rounded up to /8 = 163,
    // padded to byte boundary already because 1304 % 8 == 0, so 163 bytes
    // is exact). Confirm in implementation and adjust the row-stride.

    const int pitch = width() / 8;        // 163
    uint8_t* black = currentBuffer();
    uint8_t* red   = black + pitch * height();

    // Begin streaming for each (half, plane).
    _driver.beginPlane(Half::Master, Plane::Black);
    _driver.beginPlane(Half::Slave,  Plane::Black);

    uint8_t mrow[WS1248B::HALF_ROW_BYTES];
    uint8_t srow[WS1248B::HALF_ROW_BYTES];

    for (int y = 0; y < height(); ++y) {
        // Build a padded 164-byte source row from the 163-byte renderer row
        // so RowSplitter has the byte it needs for columns 648..1303.
        uint8_t src[164];
        memcpy(src, black + y * pitch, pitch);
        src[163] = 0x00;
        RowSplitter::split(src, 1304, mrow, srow);
        _driver.writeHalfPlane(Half::Master, Plane::Black, y, mrow, WS1248B::HALF_ROW_BYTES);
        _driver.writeHalfPlane(Half::Slave,  Plane::Black, y, srow, WS1248B::HALF_ROW_BYTES);
    }
    _driver.endPlane(Half::Master, Plane::Black);
    _driver.endPlane(Half::Slave,  Plane::Black);

    _driver.beginPlane(Half::Master, Plane::Red);
    _driver.beginPlane(Half::Slave,  Plane::Red);
    for (int y = 0; y < height(); ++y) {
        uint8_t src[164];
        memcpy(src, red + y * pitch, pitch);
        src[163] = 0x00;
        RowSplitter::split(src, 1304, mrow, srow);
        _driver.writeHalfPlane(Half::Master, Plane::Red, y, mrow, WS1248B::HALF_ROW_BYTES);
        _driver.writeHalfPlane(Half::Slave,  Plane::Red, y, srow, WS1248B::HALF_ROW_BYTES);
    }
    _driver.endPlane(Half::Master, Plane::Red);
    _driver.endPlane(Half::Slave,  Plane::Red);

    return _driver.refresh();
}

void BBEPAdapter1248B::sleep() {
    _driver.sleep();
}

} // namespace trmnl
```

- [ ] **Step 3: Verify bb_epaper's red-plane accessor**

```bash
grep -nE "redBuffer\(\)|getRedBuffer|plane[01]|PLANE_0|PLANE_1" \
  /Users/cleiton/projects/eletronics/trmnl-firmware/.pio/libdeps/waveshare-esp32-driver/bb_epaper/src/bb_epaper.h \
  | head -20
```

If bb_epaper exposes a dedicated red-plane accessor (e.g. `currentBuffer(PLANE_1)`), replace the manual `red = black + pitch * height()` calculation in `refresh()` with that accessor. Update the comment too.

- [ ] **Step 4: Build to confirm the adapter compiles**

```bash
pio run -e waveshare_1248b 2>&1 | grep -E "bbep_adapter_1248b\.cpp.*error" || echo "adapter OK"
```

Expected: `adapter OK`. Remaining errors should be limited to `display.cpp` (wired in Task 11).

- [ ] **Step 5: Commit**

```bash
git add lib/trmnl_waveshare_1248b/src/bbep_adapter_1248b.{h,cpp}
git commit -m "lib(1248b): BBEPAdapter1248B with composition over bb_epaper + dispatch via WS1248B"
```

---

### Task 10: Verify renderer's `capabilities()` reports `BBEP_3COLOR`

**Files:**
- Modify: `lib/trmnl_waveshare_1248b/src/bbep_adapter_1248b.cpp`

`display.cpp:1137` branches on `bbep.capabilities() & BBEP_3COLOR`. We need the renderer to report this even after we override panel size. The base panel `EP74R_640x384` is 3-color, so this should hold — but we verify.

- [ ] **Step 1: Build a tiny throw-away check**

After `_renderer.allocBuffer()` in `BBEPAdapter1248B::init()`, add temporarily (delete in Step 3):

```cpp
    Log.noticeln(F("BBEPAdapter1248B: caps=0x%x type=%d w=%d h=%d"),
                 _renderer.capabilities(), _renderer.getPanelType(),
                 _renderer.width(), _renderer.height());
```

- [ ] **Step 2: Flash the smoke sketch from Task 13 (or skip until then) and read serial output**

If running Task 10 before Task 13, defer this verification to right after Task 13 Step 4 and reorder.

Expected log: `caps=` includes the bit for `BBEP_3COLOR` (mask `0x01` per `bb_epaper.h`). If it does not, the renderer isn't recognizing the panel as 3-color after `setPanelSize` — in that case, override `capabilities()` in the adapter to force the bit:

```cpp
int capabilities() { return _renderer.capabilities() | BBEP_3COLOR; }
```

- [ ] **Step 3: Remove the temporary log line and commit**

```bash
git add lib/trmnl_waveshare_1248b/src/bbep_adapter_1248b.cpp
git commit -m "lib(1248b): verify renderer reports BBEP_3COLOR after setPanelSize"
```

---

### Task 11: Wire `BOARD_WAVESHARE_1248B` into `display.cpp`

**Files:**
- Modify: `src/display.cpp:9-82` (the panel-selection block at the top)
- Modify: `src/display.cpp:111` (`display_init` body)

The non-X-class branch of `display.cpp` selects a `BBEPAPER bbep(...)` via `#if defined(...) ... #elif ... #endif`. We add `BOARD_WAVESHARE_1248B` as a sibling, but instead of instantiating `BBEPAPER`, we instantiate our adapter — and we define our own `BB_EPAPER`-equivalent macro so the per-plane PNG rendering paths still apply.

- [ ] **Step 1: Read the existing panel-selection block to understand exact line numbers**

```bash
sed -n '1,100p' src/display.cpp
```

Note exact line numbers of the `#if defined(BOARD_XTEINK_X4)` / `#elif ... #endif` block and the `#else // BOARD_X_CLASS` / `#endif` switch.

- [ ] **Step 2: Add the new branch**

Inside the non-`BOARD_X_CLASS` arm (the part that defines `BB_EPAPER` and instantiates `BBEPAPER bbep(...)`), add an `#elif` that selects our adapter:

```cpp
#elif defined(BOARD_WAVESHARE_1248B)
#include "bbep_adapter_1248b.h"
trmnl::BBEPAdapter1248B bbep;
#define WS1248B 1   // gates new-board-specific code paths below
```

Place this `#elif` **before** the final `#else` that defaults to `BBEPAPER bbep(EP75_800x480)`. This keeps the existing default behavior for boards that don't match any `#if`.

The new branch deliberately does **not** define `BB_EPAPER` — code under `#ifdef BB_EPAPER` won't run for this board. The `WS1248B` macro it defines lets us add board-specific branches where needed.

Crucially, the `bbep` identifier remains identical to all other branches, so the rest of `display.cpp`'s rendering code compiles unchanged. The adapter's method signatures match the bb_epaper surface that code uses.

- [ ] **Step 3: Wire `display_init()`**

Find `void display_init(void)` (around line 111). The existing body for non-X-class boards calls:

```cpp
bbep.setPanelType(dpList[iTempProfile].OneBit);
bbep.initIO(EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN, EPD_CS_PIN, EPD_MOSI_PIN, EPD_SCK_PIN, 8000000);
```

Those reference single-half pin macros that don't exist for `BOARD_WAVESHARE_1248B`. Gate them:

```cpp
#ifdef BB_EPAPER
    bbep.setPanelType(dpList[iTempProfile].OneBit);
    bbep.initIO(EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN, EPD_CS_PIN, EPD_MOSI_PIN, EPD_SCK_PIN, 8000000);
#elif defined(WS1248B)
    if (!bbep.init()) {
        Log.errorln(F("BBEPAdapter1248B::init failed"));
        // continue anyway — display_show_msg may still render on whichever half came up
    }
#else
    // BOARD_X_CLASS path (existing FastEPD branch)
#endif
```

- [ ] **Step 4: Audit remaining `#ifdef BB_EPAPER` regions for board-specific paths needed**

```bash
grep -n "#ifdef BB_EPAPER\|#ifndef BB_EPAPER\|BB_EPAPER" src/display.cpp
```

For each match, decide whether the `WS1248B` branch needs to be added:
- Code paths that should run for our board: add `|| defined(WS1248B)` to the gate, or add `#elif defined(WS1248B)` branches that do the same thing via the adapter.
- Paths that are bb_epaper-specific internals (e.g., `setPanelType(dpList[iTempProfile].OneBit)` during refresh selection): leave `#ifdef BB_EPAPER` as-is — the adapter no-ops `setPanelType`, so even if called, nothing breaks.

Be **conservative**: leave existing branches alone unless they actively block compilation for our board. The goal is minimum diff.

- [ ] **Step 5: Build full env**

```bash
pio run -e waveshare_1248b 2>&1 | tail -40
```

Expected: **build succeeds**. If errors remain, they should now point to specific `display.cpp` lines — fix each by adding `WS1248B` to its gate per Step 4's principle.

- [ ] **Step 6: Commit**

```bash
git add src/display.cpp
git commit -m "display: wire BOARD_WAVESHARE_1248B branch via BBEPAdapter1248B"
```

---

### Task 12: Build size + library deps audit

**Files:** (no new files; verification only)

- [ ] **Step 1: Confirm `bb_epaper` is still pulled (the adapter depends on it as renderer)**

```bash
pio run -e waveshare_1248b -t metadata 2>/dev/null | grep -E "bb_epaper|trmnl_waveshare_1248b"
```

Expected: both libraries listed. The adapter's `depends=bb_epaper` in `library.properties` should be enough — but if PlatformIO's LDF strips `bb_epaper` because the `env:waveshare_1248b` has no other reference to it, add `bb_epaper` explicitly to the env's `lib_deps`.

- [ ] **Step 2: Check final binary size**

```bash
pio run -e waveshare_1248b 2>&1 | grep -E "RAM:|Flash:"
```

Expected: Flash usage rises by ~80–120 KB vs. the existing `waveshare-esp32-driver` env (the bulk is bb_epaper + the WS1248B port). RAM usage is roughly unchanged because the plane buffers are PSRAM-allocated. If Flash exceeds the partition budget, switch the partition table from `min_spiffs.csv` to a larger one.

- [ ] **Step 3: No commit (verification step only).**

---

### Task 13: On-device smoke sketch

**Files:**
- Create: `examples/waveshare_1248b_smoke/waveshare_1248b_smoke.ino`
- Create: `examples/waveshare_1248b_smoke/README.md`

This is **not** part of the firmware build — it's a standalone Arduino sketch reusing the same `lib/trmnl_waveshare_1248b/` library, so anyone with the hardware can verify the driver works without flashing the full firmware.

- [ ] **Step 1: Write the sketch**

```cpp
// examples/waveshare_1248b_smoke/waveshare_1248b_smoke.ino
//
// Smoke test for the Waveshare 12.48" Module B + ESP32 Driver Board.
// Renders 4 quadrants (white, black, red, gradient) using the
// BBEPAdapter1248B, then sleeps the panel.
//
// Build with PlatformIO env [env:waveshare_1248b]:
//   pio run -e waveshare_1248b -d examples/waveshare_1248b_smoke -t upload

#include <Arduino.h>
#include "bbep_adapter_1248b.h"

trmnl::BBEPAdapter1248B bbep;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("waveshare_1248b smoke test");

    if (!bbep.init()) {
        Serial.println("init failed — check PSRAM and wiring");
        return;
    }

    // 4 quadrants: TL white, TR black, BL red, BR vertical-stripe gradient.
    const int W = bbep.width();
    const int H = bbep.height();
    bbep.fillScreen(BBEP_WHITE);

    // TR: black quadrant
    for (int y = 0; y < H/2; ++y)
        for (int x = W/2; x < W; ++x)
            bbep.drawPixel(x, y, BBEP_BLACK);

    // BL: red quadrant
    for (int y = H/2; y < H; ++y)
        for (int x = 0; x < W/2; ++x)
            bbep.drawPixel(x, y, BBEP_RED);

    // BR: vertical stripes — every 20 pixels alternate black/white,
    // crosses the M/S seam (col 648) so seam alignment is visible.
    for (int y = H/2; y < H; ++y) {
        for (int x = W/2; x < W; ++x) {
            int stripe = ((x - W/2) / 20) % 2;
            bbep.drawPixel(x, y, stripe ? BBEP_BLACK : BBEP_WHITE);
        }
    }

    Serial.println("refresh start");
    uint32_t t0 = millis();
    bool ok = bbep.refresh();
    Serial.printf("refresh done in %u ms, ok=%d\n", millis() - t0, ok);

    bbep.sleep();
    Serial.println("sleep");
}

void loop() {}
```

- [ ] **Step 2: Write `README.md`**

```markdown
# waveshare_1248b smoke sketch

Renders 4 quadrants on the panel: white (top-left), black (top-right), red (bottom-left), vertical stripes (bottom-right).

## Hardware checks

- **Top-left white** confirms the renderer clears correctly.
- **Top-right black** confirms the black plane reaches both halves (boundary at column 648 should be exactly at the panel midline — no visible offset or repeat).
- **Bottom-left red** confirms the red plane reaches the master half (and that the slave half is correctly "white in red" for its portion).
- **Bottom-right stripes** crossing the seam confirm M/S alignment. Stripes should continue without jog at the boundary.

## How to run

```bash
cd /Users/cleiton/projects/eletronics/mytrmnl/trmnl-firmware
pio run -e waveshare_1248b -d examples/waveshare_1248b_smoke -t upload
pio device monitor -e waveshare_1248b
```

Serial output should report `refresh done in <ms>, ok=1`.
```

- [ ] **Step 3: Build the sketch**

```bash
cd /Users/cleiton/projects/eletronics/mytrmnl/trmnl-firmware
pio run -e waveshare_1248b -d examples/waveshare_1248b_smoke 2>&1 | tail -20
```

Expected: build succeeds.

- [ ] **Step 4: Hardware test (manual, requires the panel)**

Flash and run on hardware:
1. White top-left, black top-right, red bottom-left, striped bottom-right.
2. Boundary at column 648 has no visible gap, jog, or repeated content.
3. Serial: `init` succeeds; `refresh` reports `ok=1` within ~30 s.

If the seam shows a 1-pixel column gap or repeat, the cause is almost certainly the 648/656 padding in `RowSplitter`: re-verify against the test cases in Task 5.

If a half is blank, check that half's BUSY polarity in `WS1248B::waitBusy` (Task 6 step 2 note) and its CS wiring.

- [ ] **Step 5: Commit**

```bash
git add examples/waveshare_1248b_smoke/
git commit -m "examples: smoke sketch for waveshare 12.48 BWR"
```

---

### Task 14: End-to-end firmware test against `/api/display`

**Files:** (no new files; verification only)

- [ ] **Step 1: Flash the full firmware**

```bash
pio run -e waveshare_1248b -t upload
pio device monitor -e waveshare_1248b
```

- [ ] **Step 2: Captive portal flow**

Device should boot, see no Wi-Fi credentials, render the captive-portal QR via `display_show_msg(WIFI_CONNECT, ...)`. Verify:
- QR is visible on whichever half it lands on (the existing layout assumes ~800x480 — on the 1304x984 panel it will be smaller relative to the panel, but legible).
- No render artifacts at the M/S seam.

If the QR is misplaced, `display.cpp`'s message layout uses hard-coded coordinates calibrated for 800x480. That's a follow-up adjustment, not a driver bug — flag and move on.

- [ ] **Step 3: Connect to Wi-Fi, complete setup**

Device fetches `/api/setup`, persists API key + friendly_id, then loops on `/api/display`. First `/api/display` response renders the device's setup logo. Verify the BMP renders end-to-end via the streaming `setAddrWindow`/`startWrite`/`writeData` path.

- [ ] **Step 4: Force a refresh from the TRMNL dashboard, observe a normal image**

The fetched image is 1bpp BMP. With our 3-color renderer it renders as black-on-white (red plane stays white). The full panel should refresh in ~24–28 s with no visible seam.

- [ ] **Step 5: Force a server-side error (e.g., revoke the API key via dashboard) and observe `display_show_msg(API_ERROR, ...)`**

The error screen uses bb_epaper text + G5-compressed QR (`loadG5Image`). Verify text and QR render legibly.

- [ ] **Step 6: Sleep current measurement (rough)**

After a successful update + sleep, multimeter the board's 5V rail and confirm light-sleep current is in the low single-digit milliamps (matches the order-of-magnitude target from §9.3 of the spec). This is a sanity check, not a precise budget — the Waveshare Driver Board's `CP2102` typically dominates at 1-2 mA regardless of MCU state.

- [ ] **Step 7: No commit (verification step only)**

---

### Task 15: Final build sanity + tag

**Files:** (no new files; cleanup + final commit only)

- [ ] **Step 1: Run all native unit tests**

```bash
pio test -e native 2>&1 | tail -20
```

Expected: all pre-existing tests pass + the new `test_row_splitter` passes (5/5).

- [ ] **Step 2: Verify all originally supported envs still build**

```bash
for env in trmnl trmnl_4clr waveshare-esp32-driver WAVESHARE_397; do
    echo "=== $env ==="
    pio run -e $env 2>&1 | tail -3
done
```

Expected: each env reports `SUCCESS`. No env should have been broken by the `display.cpp` edits.

- [ ] **Step 3: Verify the new env still builds**

```bash
pio run -e waveshare_1248b 2>&1 | tail -3
```

Expected: `SUCCESS`.

- [ ] **Step 4: No commit — final state should already be committed task-by-task.**

---

## Self-Review

### Spec coverage

| Spec section | Covered by |
|---|---|
| §4 row 1 (12.48 only, extensible) | Tasks 9–11 establish boundary; §11 of spec is forward-only. |
| §4 row 2 (BOARD_WAVESHARE_1248B + dedicated path) | Tasks 2, 3, 11. |
| §4 row 3 (port Waveshare reference; GxEPD2 contingency) | Tasks 1, 6, 7, 8. Contingency unused unless porting stalls. |
| §4 row 4 (PNG-driven color from day one) | Adapter forwards `startWrite(PLANE_0/1)` + `writeData` to renderer; `refresh()` dispatches both planes. Covered in Task 9. |
| §4 row 5 (pin map locked during implementation) | Task 1 step 3 + Task 3 step 2. |
| §4 row 6 (paired halves only in public API) | `WS1248B::refresh`/`sleep` are paired; `writeHalfPlane` is package-internal (only adapter calls it). Task 6/7. |
| §4 row 7 (tests: row_splitter, on-device smoke, field test) | Tasks 5, 13, 14. Color quantizer test from spec §9.1 was dropped — quantization happens inside bb_epaper renderer, not our code, so no value in re-testing it. |
| §5 layers L1/L2/L3 | Tasks 6–8 / Tasks 9–10 / Task 11. |
| §6 data flow | Refresh implementation in Task 9 step 2. |
| §7.1 PSRAM env | Task 2 (and spec amended in Task 0). |
| §7.2 pin map | Task 3. |
| §7.3 library deps | Task 4 + Task 12 step 1. |
| §8 error handling | BUSY timeout (Task 6 step 2), `_panelFailed` flag (Task 6 step 2 + Task 7 guard), paired lock-step rule (`refresh`/`sleep` always paired). |
| §9 testing | Task 5 (unit), Task 13 (smoke), Task 14 (field). |
| §11 future extensibility | No code change; the design itself is the deliverable. |

### Placeholder scan

The plan contains two intentionally non-verbatim regions:

- **Task 6 step 3** and **Task 7 step 3** and **Task 8 step 2**: the `<<< INSERT VERBATIM PORT HERE >>>` blocks. These are not placeholders — they are explicit transcription gates where the implementer reads the Waveshare reference (cloned in Task 1) and copies the byte-exact init/refresh/sleep sequences. The plan cannot inline these without the reference being checked in. Each step gives the exact file path, function name to find, and translation rules.
- **Task 3 step 2** allows pin number adjustments based on Task 1's discovery.

No "TBD"/"TODO"/"implement later" strings in the plan body.

### Type consistency

- `Half::Master`/`Half::Slave` and `Plane::Black`/`Plane::Red` used consistently across `ws1248b.h`, `ws1248b.cpp`, `bbep_adapter_1248b.cpp`.
- `WS1248B::HALF_ROW_BYTES = 82` referenced in both the driver and the adapter.
- `BBEPAdapter1248B` method signatures (`fillScreen(int)`, `drawPixel(int, int, int)`, `setAddrWindow(int, int, int, int)`, `startWrite(int)`, `writeData(const uint8_t*, int)`, `refresh(int, bool)`) match bb_epaper's public signatures as used by `src/display.cpp` (verified by grep in the exploration phase).
- `bbep` identifier preserved across all board branches in `display.cpp`.

### Open risk (carried from spec §10)

- The `currentBuffer() + pitch*height()` red-plane pointer math in Task 9 step 2 depends on bb_epaper's internal layout. Task 9 step 3 verifies and substitutes a real accessor if one exists. If bb_epaper's red plane is allocated separately (not contiguous), the calculation breaks silently — Task 13's smoke sketch (red quadrant) is the catch.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-10-waveshare-12in48-driver.md`. Two execution options:

1. **Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?

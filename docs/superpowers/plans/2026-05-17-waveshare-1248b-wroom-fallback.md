# Waveshare 12.48"B — WROOM fallback (paged mode) implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the `waveshare_1248b` env run on the ESP32-WROOM-32 variant of the Driver Board (the one without PSRAM) by switching from full-frame BWR rendering to paged mode. Same env, same code, just smaller frame-buffer pages — works on both WROOM and WROVER.

**Architecture:** Drop the PSRAM dependency entirely. Set `MAX_DISPLAY_BUFFER_SIZE = 65536` so `GxEPD2_3C<GxEPD2_1248c, …>` buffers ~200 rows per page (~65 KB total, fits comfortably in DRAM static BSS); the panel refreshes in 5 passes per frame. Revert the adapter from heap-allocated (`ps_malloc` + placement-new) back to a file-static instance — the whole machinery was a workaround for putting 320 KB in PSRAM, and once we're not using PSRAM, the workaround can go.

**Tech Stack:** Same as Phase 1 — Arduino + ESP-IDF, GxEPD2, PNGdec. No new dependencies.

**Prior context:**
- Phase 1 MVP plan: `docs/superpowers/plans/2026-05-16-waveshare-1248b.md` (complete; merged-pending)
- Phase 1 spec: `docs/superpowers/specs/2026-05-10-waveshare-12in48-driver-design.md` (revised 2026-05-16)
- Phase 1 was WROVER-only — confirmed on real hardware that the user's board is WROOM-32E, not WROVER. The Phase 1 binary boots but `display_init` aborts when PSRAM total is 0.

**Test posture:** Same as Phase 1 — build sanity sweep across all envs + native regression. On-hardware verification is the user's job (they have the actual board).

---

## File map

**Modify:**
- `lib/trmnl_waveshare_1248b/src/gxepd2_adapter_1248b.h` — drop `MAX_DISPLAY_BUFFER_SIZE` from 400 000 to 65 536; update inline comment
- `src/display.cpp` — revert the `BOARD_WAVESHARE_1248B` arm's `g_adapter` from `GxEPD2Adapter1248B*` heap-allocated back to a file-static value; drop `#include <new>`, `#include <esp_heap_caps.h>`; drop placement-new + PSRAM check from `display_init`; drop null-guards from every call site
- `platformio.ini` — drop `board_build.psram = enabled`, `-D BOARD_HAS_PSRAM`, `-D CONFIG_SPIRAM_USE_MALLOC=1` from `[env:waveshare_1248b]`
- `lib/trmnl_waveshare_1248b/README.md` — drop "WROVER required" warnings; explain paged mode
- `README.md` — drop "WROVER required" callout in the top-level Waveshare 12.48"B section
- `docs/superpowers/specs/2026-05-10-waveshare-12in48-driver-design.md` — add a "Revision 3 (2026-05-17)" note at the top documenting the design pivot
- `docs/superpowers/plans/2026-05-17-waveshare-1248b-wroom-fallback.md` — this file

**Untouched:**
- Everything else (other envs, main.cpp, api-client/, wificaptive/, all other boards' behavior).

---

### Task 1: Drop `MAX_DISPLAY_BUFFER_SIZE` in the adapter header

**Why first:** The template `gxepd2_1248b_max_height<DRIVER>()` resolves at compile time based on this constant. Lowering it from 400 000 to 65 536 changes `GxEPD2_3C<GxEPD2_1248c, gxepd2_1248b_max_height<…>()>` from a full-frame (984-row) instantiation to a paged (~200-row) one. Every other change cascades from this.

**Files:**
- Modify: `lib/trmnl_waveshare_1248b/src/gxepd2_adapter_1248b.h`

- [ ] **Step 1: Edit the constant + comment**

In `lib/trmnl_waveshare_1248b/src/gxepd2_adapter_1248b.h`, find:

```cpp
// 400000 lets GxEPD2 buffer the full BWR frame in PSRAM in a single page:
//   2 planes (black + red) * 1304 * 984 / 8 = 320784 bytes.
// On a WROOM board (no PSRAM) GxEPD2 falls back to paged mode (~5 pages).
static constexpr unsigned long GXEPD2_1248B_MAX_DISPLAY_BUFFER_SIZE = 400000UL;
```

Replace with:

```cpp
// 65536 buffers ~200 rows per page (2 planes * 1304/8 bytes/row * 200 rows =
// 65200 bytes), so the full 1304x984 BWR frame renders in 5 passes. Fits in
// the ESP32 DRAM static BSS on both WROOM (no PSRAM) and WROVER variants —
// no PSRAM dependency. Going larger would speed renders slightly but pushes
// us into PSRAM territory and re-introduces the platform fragility the
// Phase 1 MVP hit on WROOM hardware.
static constexpr unsigned long GXEPD2_1248B_MAX_DISPLAY_BUFFER_SIZE = 65536UL;
```

- [ ] **Step 2: Commit**

```bash
git add lib/trmnl_waveshare_1248b/src/gxepd2_adapter_1248b.h
git commit -m "adapter(1248b): drop MAX_DISPLAY_BUFFER_SIZE to 65 536 for paged mode"
```

---

### Task 2: Revert the `BOARD_WAVESHARE_1248B` arm in `src/display.cpp` to a static adapter

**Why second:** The Phase 1 implementation made `g_adapter` a pointer with placement-new into PSRAM specifically to put GxEPD2's ~320 KB of member arrays in PSRAM. Now that the adapter is ~65 KB and lives in DRAM static BSS, that whole machinery is dead weight. Revert it.

**Files:**
- Modify: `src/display.cpp` — `BOARD_WAVESHARE_1248B` arm only

- [ ] **Step 1: Drop the `<new>` and `<esp_heap_caps.h>` includes**

In `src/display.cpp`, inside the `#elif defined(BOARD_WAVESHARE_1248B)` arm, find:

```cpp
#include <SPIFFS.h>
#include <config.h>
#include <trmnl_log.h>
#include <esp_heap_caps.h>
#include "gxepd2_adapter_1248b.h"
#include <new>
```

Remove the two no-longer-needed includes so it reads:

```cpp
#include <SPIFFS.h>
#include <config.h>
#include <trmnl_log.h>
#include "gxepd2_adapter_1248b.h"
```

- [ ] **Step 2: Revert `g_adapter` to a file-static instance**

Find:

```cpp
// Adapter is heap-allocated in PSRAM during display_init() because GxEPD2_3C
// holds the ~320 KB BWR frame buffer as a member array. board_build.psram =
// enabled is silently ignored in this dual-framework env, so we can't rely on
// EXT_RAM_BSS_ATTR; we placement-new the whole adapter into PSRAM instead.
// Same pattern as commit f91efdc on the xiao_c6 path.
static trmnl::GxEPD2Adapter1248B* g_adapter = nullptr;
```

Replace with:

```cpp
// Adapter lives in DRAM static BSS. GxEPD2_3C's member buffers add up to
// ~65 KB at MAX_DISPLAY_BUFFER_SIZE = 65 536, which fits comfortably on both
// WROOM (no PSRAM, ~256 KB DRAM total) and WROVER (with PSRAM) variants of
// the driver board. Paged rendering does 5 passes per frame.
static trmnl::GxEPD2Adapter1248B g_adapter(
    EPD_M1_CS_PIN, EPD_S1_CS_PIN, EPD_M2_CS_PIN, EPD_S2_CS_PIN,
    EPD_M1S1_DC_PIN, EPD_M2S2_DC_PIN,
    EPD_M1S1_RST_PIN, EPD_M2S2_RST_PIN,
    EPD_M1_BUSY_PIN, EPD_S1_BUSY_PIN, EPD_M2_BUSY_PIN, EPD_S2_BUSY_PIN,
    EPD_SCK_PIN, EPD_MOSI_PIN);
```

- [ ] **Step 3: Simplify `display_init`**

Find the current `display_init` body (with PSRAM check + heap_caps_malloc + placement new + g_adapter->init()) and replace it with:

```cpp
void display_init(void) {
    Log_info("waveshare_1248b: display_init (paged mode, %u-byte buffer)",
             (unsigned)sizeof(trmnl::GxEPD2Adapter1248B));
    g_adapter.init();
}
```

- [ ] **Step 4: Convert every `g_adapter->foo()` back to `g_adapter.foo()` and drop the null-guards**

Within the `BOARD_WAVESHARE_1248B` arm only, find every call site that uses the pointer form (`g_adapter->foo()`) and the corresponding `if (g_adapter == nullptr) { … return …; }` guard. Replace with the value form and drop the guard.

The complete list of sites to update:

- In `waveshare_1248b_pngDraw`: `g_adapter->gx()` → `g_adapter.gx()`
- In `display_width()`: drop the null-guard (the `if (g_adapter == nullptr) return 0;` block); change `g_adapter->width()` → `g_adapter.width()`
- In `display_height()`: same — drop the guard; change `g_adapter->height()` → `g_adapter.height()`
- In `display_show_image()`:
  - Drop the null-guard at the top
  - `g_adapter->gx().setFullWindow()` → `g_adapter.gx().setFullWindow()`
  - `g_adapter->gx().firstPage()` → `g_adapter.gx().firstPage()`
  - `g_adapter->gx().fillScreen(GxEPD_WHITE)` → `g_adapter.gx().fillScreen(GxEPD_WHITE)`
  - `g_adapter->gx().nextPage()` → `g_adapter.gx().nextPage()`
  - `g_adapter->powerOff()` → `g_adapter.powerOff()`
- In `display_reset()`: drop the null-guard; `g_adapter->init()` → `g_adapter.init()`
- In `display_sleep()`: drop the null-guard; `g_adapter->sleep()` → `g_adapter.sleep()`

After this step, the only remaining function that returns early in error is `display_show_image()`'s nullptr/empty buffer + BMP/unknown-format checks (which are unrelated to the adapter null-guards we just dropped — those stay).

- [ ] **Step 5: Verify the build before committing**

```bash
pio run -e waveshare_1248b 2>&1 | tail -10
```

Expected: `[SUCCESS]`. RAM usage should be roughly 30–40% (the ~65 KB adapter now visible in static BSS).

If `[FAILED]`:
- Diagnose carefully; don't guess. Common causes are missed `->` → `.` conversions (would surface as "request for member ‘…’ in ‘g_adapter’, which is of non-class type ‘trmnl::GxEPD2Adapter1248B*’"), or a missed null-guard removal.

- [ ] **Step 6: Commit**

```bash
git add src/display.cpp
git commit -m "display(waveshare_1248b): static adapter in DRAM (paged mode, no PSRAM)"
```

---

### Task 3: Drop PSRAM machinery from `[env:waveshare_1248b]`

**Why now:** Without the placement-new-into-PSRAM path, the env's `psram=enabled` knob and PSRAM-related defines are no-ops at best, misleading at worst.

**Files:**
- Modify: `platformio.ini`

- [ ] **Step 1: Edit the `[env:waveshare_1248b]` block**

In `platformio.ini`, find the `[env:waveshare_1248b]` block. Remove:
- The line `board_build.psram = enabled        ; required: ~320 KB BWR frame buffer in PSRAM`
- The build flag `-D BOARD_HAS_PSRAM`
- The build flag `-D CONFIG_SPIRAM_USE_MALLOC=1`

The remaining `build_flags` for this env should be:

```ini
build_flags =
	${env:esp32_base.build_flags}
	-D BOARD_WAVESHARE_1248B
	-D PNG_MAX_BUFFERED_PIXELS=14984
	-D FAKE_BATTERY_VOLTAGE
```

Leave the rest of the env (lib_deps, lib_ignore, board, clocks, etc.) untouched.

- [ ] **Step 2: Verify the env still parses**

```bash
pio project config --json-output 2>&1 | tail -1 | head -c 80
```

Expected: JSON output starting with `[[`.

- [ ] **Step 3: Commit**

```bash
git add platformio.ini
git commit -m "platformio(waveshare_1248b): drop PSRAM knobs (paged mode runs in DRAM)"
```

---

### Task 4: Update README files to remove WROVER-only warnings

**Why now:** Code is correct; docs need to match. Both the top-level `README.md` and the in-tree `lib/trmnl_waveshare_1248b/README.md` carry "WROVER required" callouts that are now wrong.

**Files:**
- Modify: `README.md`
- Modify: `lib/trmnl_waveshare_1248b/README.md`

- [ ] **Step 1: Update top-level `README.md`**

In the `## **Waveshare ESP32 Driver Board + 12.48" Module B (community port)**` section near the top of `README.md`:

Find the intro paragraph that reads:

> This fork adds support for driving the Waveshare 12.48" e-Paper Module B (1304×984 black/white/red, SKU 17299, **four-controller panel**) from a Waveshare ESP32 Driver Board (SKU 15823, **WROVER variant — PSRAM required**) via [GxEPD2](https://github.com/ZinggJM/GxEPD2)'s `GxEPD2_1248c` driver class.

Replace with:

> This fork adds support for driving the Waveshare 12.48" e-Paper Module B (1304×984 black/white/red, SKU 17299, **four-controller panel**) from a Waveshare ESP32 Driver Board (SKU 15823) via [GxEPD2](https://github.com/ZinggJM/GxEPD2)'s `GxEPD2_1248c` driver class. Works on both the **WROOM-32** (no PSRAM) and **WROVER-B** (with PSRAM) variants of the driver board.

Then find the `### Hardware variant check` section and replace its entire content with:

```markdown
### Hardware variant check

Both WROOM-32 and WROVER-B variants of the driver board are supported. The firmware uses paged-mode rendering with a ~65 KB DRAM frame buffer — no PSRAM required. A full panel refresh takes roughly 35–45 s (the panel itself dominates; the 5 paged passes add ~5 s of decode work).
```

- [ ] **Step 2: Update `lib/trmnl_waveshare_1248b/README.md`**

Find the H1 lead paragraph:

> GxEPD2-based driver adapter for the Waveshare 12.48" e-Paper Module B (SKU 17299; 1304×984 black/white/red, four onboard controllers) on the Waveshare ESP32 Driver Board (SKU 15823, **WROVER variant — PSRAM required**).

Replace with:

> GxEPD2-based driver adapter for the Waveshare 12.48" e-Paper Module B (SKU 17299; 1304×984 black/white/red, four onboard controllers) on the Waveshare ESP32 Driver Board (SKU 15823). Works on both WROOM-32 and WROVER-B variants — paged-mode rendering keeps the frame buffer in DRAM, no PSRAM dependency.

Then find the `## Hardware variant check` section and replace its entire content with:

```markdown
## Rendering strategy

Paged mode: `GxEPD2_3C<GxEPD2_1248c, ~200>` buffers ~200 rows at a time (~65 KB in DRAM static BSS), rendering the full 1304×984 BWR frame in 5 passes. PNG decode runs once per pass; GxEPD2's `drawPixel` skips pixels outside the current page window so the per-pass cost is small. Total frame time: ~35–45 s (mostly panel refresh).
```

- [ ] **Step 3: Commit**

```bash
git add README.md lib/trmnl_waveshare_1248b/README.md
git commit -m "docs(waveshare_1248b): paged mode runs on WROOM + WROVER (no PSRAM)"
```

---

### Task 5: Append "Revision 3" note to the spec

**Why now:** The spec has been the source of truth for the design; an outside reader landing on it should see the WROOM-fallback decision. Don't rewrite every section — just append a header note that points at the change.

**Files:**
- Modify: `docs/superpowers/specs/2026-05-10-waveshare-12in48-driver-design.md`

- [ ] **Step 1: Update the header revision line + add a §1.2**

Find:

```markdown
**Revised:** 2026-05-16 (panel topology + library choice corrected — see §1.1)
```

Replace with:

```markdown
**Revised:** 2026-05-16 (panel topology + library choice corrected — see §1.1); 2026-05-17 (paged-mode default for WROOM support — see §1.2)
```

Then find the existing `### 1.1 What changed in the 2026-05-16 revision` subsection. Append a new `### 1.2` subsection immediately after it (and before `## 2. Non-goals`):

```markdown
### 1.2 What changed in the 2026-05-17 revision

Hardware-on-the-bench confirmed the user's Waveshare ESP32 Driver Board ships with the **ESP32-WROOM-32E** module (no external PSRAM), not the WROVER variant the Phase 1 design assumed. The Phase 1 binary boots but aborts in `display_init` when `heap_caps_get_total_size(MALLOC_CAP_SPIRAM)` returns 0. Rather than require the user to swap modules, the design pivots:

1. **Single-env paged-mode default.** Drop `MAX_DISPLAY_BUFFER_SIZE` from 400 000 to 65 536. `GxEPD2_3C<GxEPD2_1248c, …>` instantiates with ~200-row planes, so the full 1304×984 BWR frame renders in 5 passes. Total adapter sizeof ≈ 65 KB.
2. **DRAM-resident static adapter.** Revert from `heap_caps_malloc(MALLOC_CAP_SPIRAM)` + placement-new (the Phase 1 workaround for the dual-framework PSRAM-init fragility) back to a plain file-static instance. ~65 KB in static BSS is comfortable on both WROOM (~256 KB DRAM total) and WROVER variants — no PSRAM dependency at all.
3. **Drop `board_build.psram = enabled`, `BOARD_HAS_PSRAM`, `CONFIG_SPIRAM_USE_MALLOC=1`** from `[env:waveshare_1248b]`. The PSRAM detection check in `display_init` is also removed (no longer relevant).
4. **WROOM is no longer a non-goal.** §2's "Boards without PSRAM" exclusion is dropped (see implementation plan `docs/superpowers/plans/2026-05-17-waveshare-1248b-wroom-fallback.md`).

Sections §4 (decision #6), §5.3 (adapter header constant), §5.4 (display.cpp branch), §6.1 (memory budget), §7.1 (env), §10 (risks), and §13 (acceptance criteria) describe the Phase 1 PSRAM-required behavior — they remain accurate as historical record but the **shipped behavior is the §1.2 design**. A follow-up clean-up pass can fold §1.2 into those sections; for now the divergence is small and the §1.2 summary plus the implementation plan are authoritative.
```

- [ ] **Step 2: Commit**

```bash
git add docs/superpowers/specs/2026-05-10-waveshare-12in48-driver-design.md
git commit -m "spec(1248b): revision 3 — paged-mode default removes PSRAM requirement"
```

---

### Task 6: Build sanity sweep

**Why last:** Same gate as Phase 1 Task 8 — verify no regression across envs, and that the new paged-mode binary fits well within DRAM.

**Files:** none modified — verification only.

- [ ] **Step 1: Move pre-existing untracked IDF manifest aside**

(Same pre-flight as Phase 1. The user has untracked `src/idf_component.yml` files that poison builds.)

```bash
mkdir -p /tmp/trmnl-idf-stash
mv src/idf_component.yml src/idf_component.yml.orig dependencies.lock /tmp/trmnl-idf-stash/ 2>/dev/null
```

(If any of those files don't exist, the `mv` quietly skips them.)

- [ ] **Step 2: Build `waveshare_1248b` + report memory usage**

```bash
pio run -e waveshare_1248b 2>&1 | tail -10
```

Expected: `[SUCCESS]`. RAM usage should now be on the order of 30–40% (vs the heap-allocated Phase 1's 29.7% which excluded the BSS-resident buffer because it was in PSRAM). Flash usage similar to Phase 1 (~63%).

- [ ] **Step 3: Build other major envs to confirm no regressions**

```bash
for env in trmnl trmnl_4clr waveshare-esp32-driver WAVESHARE_397 xiao_esp32c6_75v1; do
    echo "=== $env ==="
    pio run -e "$env" 2>&1 | tail -2
done
```

Expected: each ends with `[SUCCESS]`. The only env where any of this change is observable is `waveshare_1248b` itself; the others should be unaffected.

- [ ] **Step 4: Run native tests**

```bash
pio test -e native 2>&1 | tail -5
```

Expected: 32/32 PASS (same as Phase 1 baseline).

- [ ] **Step 5: Restore the user's pre-existing untracked files**

```bash
mv /tmp/trmnl-idf-stash/idf_component.yml /tmp/trmnl-idf-stash/idf_component.yml.orig src/ 2>/dev/null
mv /tmp/trmnl-idf-stash/dependencies.lock . 2>/dev/null
rmdir /tmp/trmnl-idf-stash 2>/dev/null
```

- [ ] **Step 6: No commit unless fixes were needed**

If steps 2–4 all passed with no code edits, there's nothing to commit. If any env needed a fix, commit it with a clear message describing what was fixed.

---

## Done

The WROOM fallback is complete when:

1. `pio run -e waveshare_1248b` returns `[SUCCESS]` with the env's PSRAM knobs removed.
2. The new binary's RAM usage is roughly 30–40% (paged buffer visible in BSS instead of in PSRAM).
3. `pio run -e trmnl` / `trmnl_4clr` / `waveshare-esp32-driver` / `WAVESHARE_397` / `xiao_esp32c6_75v1` still return `SUCCESS`.
4. `pio test -e native` still passes 32/32.
5. Both READMEs (`README.md` and `lib/trmnl_waveshare_1248b/README.md`) describe paged-mode behavior and stop telling the user they need a WROVER board.

**Hand-off to user (on-device smoke):**
Tell the user:

> Build is green for `waveshare_1248b` (now paged mode, no PSRAM needed). Flash with `pio run -e waveshare_1248b -t upload -t monitor`. Configure WiFi via the captive portal, then watch serial: this time `display_init` should log `display_init (paged mode, NNNNN-byte buffer)` and NOT abort. Once `/api/display` succeeds, the panel should render in ~35–45 s (5 passes × ~1 s of decode work, plus the ~30 s panel refresh itself).

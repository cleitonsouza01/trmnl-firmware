# BYOS Server Adaptation for Waveshare ESP32 Driver Board + 12.48" Module B

**Date:** 2026-05-17
**Audience:** an LLM (or human) working on a self-hosted TRMNL BYOS server, asked to add support for the new device this firmware introduces.

This document tells you everything you need to know about the firmware so you can adapt the server. It does NOT modify the firmware — the firmware contract is fixed and shipping.

---

## 1. What's new

This firmware introduces a new device build:

- **Board:** Waveshare ESP32 Driver Board (SKU 15823) — Universal e-Paper Raw Panel Driver Board, ESP32-WROOM-32 or ESP32-WROVER variant. WiFi + Bluetooth. Wiki: <https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board>
- **Display:** Waveshare 12.48" e-Paper Module B (SKU 17299, part number "12.48inch e-paper Module (B)") — four-controller panel (M1 / S1 / M2 / S2 quadrants). Wiki: <https://www.waveshare.com/wiki/12.48inch_e-Paper_Module_(B)>
- **Resolution:** **1304 × 984, 3-color (black / white / red)**
- **`DEVICE_MODEL` (firmware constant):** `"waveshare_1248b"`
- **PIO env:** `[env:waveshare_1248b]`
- **Firmware refresh time per frame:** ~35–45 s (paged rendering does ~5 passes; the panel's own refresh dominates at ~25–30 s)

Every other existing device build (TRMNL OG 800×480, XIAO C6 + 7.5"V1, TRMNL-X, reTerminal, etc.) continues to work unchanged.

## 2. HTTP contract the firmware uses

The firmware speaks the **standard TRMNL BYOS protocol** — same one TRMNL OG and the XIAO C6 build use. Summary:

### 2.1 `GET /api/setup` (first boot, no API key yet)

Request headers:
```
ID: AA:BB:CC:DD:EE:FF   ← MAC address
```

Expected response (success):
```json
{
  "status": 200,
  "api_key": "<token>",
  "friendly_id": "917F0B",
  "image_url": "https://<server>/setup-logo.png",
  "filename": "empty_state"
}
```

### 2.2 `GET /api/display` (every refresh)

Request headers:
```
ID:              AA:BB:CC:DD:EE:FF
Access-Token:    <api_key from /setup>
Refresh-Rate:    1800
Battery-Voltage: 4.20         ← FAKE_BATTERY_VOLTAGE is defined for this device,
                                 so always 4.20 regardless of actual state
FW-Version:      <semver>
RSSI:            -67
```

Expected response (success):
```json
{
  "status": 0,
  "image_url": "https://<server>/path-to-img.png",
  "filename": "2026-05-17T12:00:00",
  "update_firmware": false,
  "firmware_url": null,
  "refresh_rate": 1800,
  "reset_firmware": false
}
```

### 2.3 Image download

The firmware does an unauthenticated `GET` on `image_url` and expects to receive raw PNG bytes.

**Constraints — these are hard requirements:**

- Image **must be PNG** (8-byte magic `\x89PNG\r\n\x1a\n`). BMP responses are detected and skipped with a warning log; the panel keeps showing whatever was there before.
- Image **must be exactly 1304 × 984 pixels**. Larger images are bounds-clamped (rows past 984 and columns past 1304 are silently dropped) so you won't crash the firmware, but content will be cut off.
- Image **can be any common PNG color type** — 1-bit grayscale, 8-bit indexed (palette), RGB888, or RGBA. The firmware uses PNGdec's `getLineAsRGB565` helper internally and quantizes each pixel to one of three palette entries (see §2.4).
- PNG should be **uncompressed enough that the decoded scanline buffer fits 14 984 pixels** (the firmware sets `PNG_MAX_BUFFERED_PIXELS=14984`). Standard PNG compression is fine.
- Filesize: keep under ~700 KB to stay safely under the firmware's `MAX_IMAGE_SIZE = 750 000` byte cap.

### 2.4 Color palette and quantization

The firmware reduces every pixel to one of three colors before drawing. The mapping (RGB888 input):

| Source RGB | Output color |
|---|---|
| `r > 180 && g < 100 && b < 100` | **RED** |
| `r < 100 && g < 100 && b < 100` | **BLACK** |
| everything else | **WHITE** |

The implication for the server: **emit images that quantize cleanly to those three buckets**. Anti-aliased text, grey gradients, and mid-saturation colors will collapse to white. The cleanest server-side strategy is to dither down to a fixed palette before encoding the PNG:

- **WHITE** = `#FFFFFF` (255, 255, 255)
- **BLACK** = `#000000` (0, 0, 0)
- **RED**   = `#FF0000` (255, 0, 0)

A 2-bit indexed PNG with that 3-entry palette is the most compact format the firmware decodes correctly.

## 3. How the server identifies this device

**Today: the firmware does not send a device-model identifier in HTTP headers.** The server only knows the device's MAC from the `ID` header. There are two ways to associate a MAC with the new model:

### Option A — admin-driven (recommended for v1)

After the device first registers via `/api/setup`, the server creates a record. The admin opens the dashboard, finds the new device, and picks `waveshare_1248b` (or equivalently "Waveshare 12.48" Module B, 1304×984 BWR") from a dropdown of supported device models. The server stores that association.

Subsequent `/api/display` responses produce a 1304×984 BWR PNG.

### Option B — firmware-supplied header (future-proof; requires both sides)

Add `Device-Model: waveshare_1248b` to the firmware's request headers in `src/api-client/`. Then the server can auto-detect on `/api/setup` and skip the manual step. This is a clean improvement but it requires a firmware change — out of scope for the current MVP.

**Tell the LLM:** start with Option A. It's the smaller change and works against the firmware as it ships. If you want Option B later, file a separate firmware PR.

## 4. Server-side checklist

What the BYOS server needs to do to render correctly for this device:

### 4.1 Add a device-model entry

Wherever your BYOS implementation enumerates supported devices (table, enum, config file, or Ruby/Python class), add:

```
key:              waveshare_1248b
display_name:     "Waveshare ESP32 Driver Board + 12.48\" Module B"
width_px:         1304
height_px:        984
bit_depth:        2                  # 4-entry palette, 3 used
output_format:    png
color_palette:    bwr                # white, black, red (no grey, no anti-alias)
palette_rgb:
  - "#FFFFFF"                        # 0
  - "#000000"                        # 1
  - "#FF0000"                        # 2
fake_battery:     true               # device always reports 4.20 V
refresh_rate_s:   1800               # default; tune to taste
```

### 4.2 Image rendering pipeline

Most BYOS implementations render plugin/dashboard screens by:
1. Running an HTML/Liquid template through a headless browser at a fixed viewport.
2. Capturing a screenshot.
3. Converting the screenshot to PNG with the device's bit depth.

You need to make sure the **viewport for `waveshare_1248b` is set to 1304 × 984** and the post-processing output is **a PNG with the 3-entry BWR palette described in §2.4**.

Concrete steps for the LLM:

1. Locate the rendering pipeline (search for callsites that take a device model and produce an image path).
2. Find where the existing TRMNL OG (800×480) or XIAO C6 (640×384) profile is handled and clone the branch for 1304×984.
3. Add the BWR palette step. If your pipeline already produces 1-bit PNG, you'll need a second code path: convert to indexed-PNG with the 3-color palette. Most image libraries do this in one line:
   - **ImageMagick:** `convert input.png -colors 3 -dither None -remap bwr_palette.png output.png`
   - **Pillow:** `img.convert("P", palette=Image.ADAPTIVE, colors=3).quantize(palette=bwr_palette_image)`
   - **Sharp/Node:** quantize to a custom palette via `sharp(input).png({ palette: true, colors: 3 })` after a remap step.
4. Make sure the output PNG passes `file output.png` showing `1304 x 984` and a 2- or 8-bit palette (`8-bit colormap` is fine).

### 4.3 Plugin / dashboard templates

This is the harder part. Most existing BYOS plugins were designed for the 800 × 480 TRMNL OG. They will **underflow** at 1304 × 984 (the dashboard will look small and lonely in a sea of white space) and may not use the red color at all.

Options, ordered from cheapest to most thorough:

- **Cheap:** render at 800×480 and upscale with nearest-neighbor to 1304×984 (the dashboard looks blocky but works; red plane is empty).
- **Better:** add a media-query / breakpoint in the plugin CSS to lay out at 1304×984 explicitly. Use the extra real estate. Red is still unused.
- **Best:** redesign templates to take advantage of the larger canvas AND use red as an accent color (e.g., red for alerts, red headers, red highlights on charts). This is what makes the 3-color panel worth the price.

The LLM should ask the human which one they want for v1. The cheap option is reasonable for a hobby setup; the best option is what the panel was designed for.

### 4.4 Dashboard UI

The "Devices" page (or equivalent in your BYOS) should show the new model in the device-model dropdown and display the correct resolution in any per-device readout. The dropdown label should make it clear this is a **3-color (BWR)** panel so the admin knows red-using templates are appropriate.

### 4.5 Setup image

`/api/setup` returns an `image_url` pointing at a "you're connected" placeholder image. Make sure your BYOS produces this at **1304 × 984, BWR-palette PNG** when the associated device is `waveshare_1248b`. (Otherwise the first render after pairing will skip with a warning log on the firmware side.)

The setup image is a great place to **use red** — a red TRMNL logo or accent confirms the BWR pipeline is working end-to-end before any plugin renders.

## 5. Testing checklist

After the LLM finishes, verify:

1. **Pair a device.** Power on a freshly-flashed Waveshare ESP32 Driver Board + 12.48"B, join the captive portal SSID `TRMNL-xxxx`, configure home WiFi. Device hits `/api/setup`. Confirm a new record appears in the BYOS admin.
2. **Associate the model.** In the BYOS admin, mark the device as `waveshare_1248b`.
3. **Force a refresh.** Trigger `/api/display` (or wait for the next poll).
4. **Verify the image URL** the server returns. `curl -I <url>` to confirm `Content-Type: image/png` and `Content-Length` < 750 000.
5. **Verify the image dimensions.** `curl <url> -o /tmp/x.png && file /tmp/x.png` should report `PNG image data, 1304 x 984, ...`.
6. **Verify the palette.** Open the PNG and confirm the only colors present are `#FFFFFF`, `#000000`, and `#FF0000`. Any other colors will quantize to white in the firmware.
7. **Check the panel.** Image should render to the e-paper within ~45 s of the `/api/display` poll. Black is black, white is white, red is red, no torn rows at the page boundaries (rows ~200, 400, 600, 800), no quadrant seams at row 492 (top/bottom half boundary) or column 648 (left/right boundary).
8. **Boot button refresh.** Press the BOOT button on the Driver Board (GPIO 0). Panel should re-poll and re-render.

### Known failure modes worth specifically watching for

| Symptom | Likely cause |
|---|---|
| Panel renders black-and-white only; red plane never lights up | Server is sending a grayscale PNG, or the palette doesn't include exactly `#FF0000` |
| Reds look pinkish or muddy | Anti-aliased red edges in source image (e.g. red text rendered with sub-pixel AA) — disable AA before quantization |
| Colors swapped (black appears red or vice-versa) | Firmware-side issue (one-line fix in `display.cpp::waveshare_1248b_rgb_to_3color`); report it so the firmware quantizer can be corrected |
| Visible horizontal seam every ~200 rows | Paged-mode rendering bug; report with the row number of the seam |
| Visible seam at row 492 OR column 648 | Quadrant-boundary issue (GxEPD2's coordination of the four controllers); report it |

## 6. Suggested LLM workflow

Hand this document plus the firmware repo URL to the next LLM, and ask:

> "Read `docs/byos-waveshare-1248b.md` to understand the new device. Then read the source of my BYOS server at `<path>`. Identify the files you'd need to change for §4 (device-model entry, rendering pipeline with BWR palette, plugin templates, dashboard UI, setup image). Produce a written plan before changing any code. After I approve the plan, execute it task-by-task with builds and tests after each step."

If the LLM tries to modify the firmware to make the server's life easier, push back — the firmware is shipped and the HTTP contract is fixed. The server adapts to the firmware, not the other way around (unless you explicitly opt in to the Option B firmware change in §3).

## 7. What this device intentionally does NOT need from the server

To keep the LLM from over-engineering:

- **No model-detection magic.** The firmware never sends `Device-Model` (today). Admin associates the MAC manually. Don't waste time on MAC-OUI lookups or other heuristics.
- **No G5 image format.** TRMNL has a G5-compressed image format for some plugins. This firmware uses standard PNG only. Don't bother adding G5 output for `waveshare_1248b`.
- **No BMP fallback.** Firmware skips BMP with a warning. Don't ship BMP responses to this device under any condition.
- **No firmware OTA.** The firmware accepts `update_firmware: true` responses but the user pushes new firmware over USB during MVP. The server can set `update_firmware: false` always for now.
- **No battery voltage tracking.** `FAKE_BATTERY_VOLTAGE` means the firmware always reports `4.20`. Don't draw battery graphs for this device — the data is fictitious.
- **No partial refreshes.** The firmware always does a full-frame BWR refresh. Don't send partial-update hints in the response.

## 8. Hardware reference: pin map

For someone reproducing the hardware setup. These pins are fixed in firmware (`src/DEV_Config.h`):

| Panel signal | ESP32 GPIO | Role |
|---|---|---|
| VCC          | 3V3 | Panel power (~50 mA average) |
| GND          | GND |  |
| DIN  (MOSI)  | 14  | Shared SPI MOSI |
| CLK  (SCK)   | 13  | Shared SPI SCK |
| M1_CS        | 23  | Top-left controller chip-select |
| S1_CS        | 22  | Top-right controller chip-select |
| M2_CS        | 16  | Bottom-left controller chip-select |
| S2_CS        | 19  | Bottom-right controller chip-select |
| M1S1_DC      | 25  | Top-half data/command select |
| M2S2_DC      | 17  | Bottom-half data/command select |
| M1S1_RST     | 33  | Top-half reset |
| M2S2_RST     | 5   | Bottom-half reset |
| M1_BUSY      | 32  | Top-left busy |
| S1_BUSY      | 26  | Top-right busy |
| M2_BUSY      | 18  | Bottom-left busy |
| S2_BUSY      | 4   | Bottom-right busy |

The Driver Board's flat-flex socket is pre-wired to these pins via the on-board level shifter — no manual jumpers needed when using the stock cable.

Panel quadrant layout (for understanding seam reports):

```
              Cols 0…647        Cols 648…1303
            ┌─────────────────┬──────────────────┐
Rows 0…491  │  M1 (648×492)   │   S1 (656×492)   │   top half
            ├─────────────────┼──────────────────┤
Rows 492…983│  M2 (656×492)   │   S2 (648×492)   │   bottom half
            └─────────────────┴──────────────────┘
```

## 9. Reference: firmware files the LLM may want to cross-reference

If the LLM wants to verify any claim in this document, here are the firmware files that ground the contract:

| Claim | Firmware file:line |
|---|---|
| `DEVICE_MODEL` constant | `include/config.h` (`#elif defined(BOARD_WAVESHARE_1248B)` arm) |
| Resolution 1304 × 984 | `lib/trmnl_waveshare_1248b/src/gxepd2_adapter_1248b.cpp` (`width()` / `height()`) and `GxEPD2_1248c::WIDTH / HEIGHT` in GxEPD2 |
| PNG magic byte check + BMP skip | `src/display.cpp` inside the `#elif defined(BOARD_WAVESHARE_1248B)` arm, in `display_show_image` |
| RGB → BWR quantizer | `src/display.cpp`, `waveshare_1248b_rgb_to_3color()` static helper |
| `MAX_IMAGE_SIZE = 750000` | `include/config.h` (the PSRAM-tier clause near the top) |
| `PNG_MAX_BUFFERED_PIXELS = 14984` | `platformio.ini`, `[env:waveshare_1248b]` `build_flags` |
| `FAKE_BATTERY_VOLTAGE` | `include/config.h` (same arm as `DEVICE_MODEL`) |
| Boot-button GPIO 0 | `include/config.h` (`PIN_INTERRUPT 0`) |
| Pin map | `src/DEV_Config.h` (`#elif defined(BOARD_WAVESHARE_1248B)` block) |
| Captive-portal SSID | `lib/wificaptive` (unchanged from upstream) |
| Setup / display HTTP calls | `src/api-client/setup.cpp` and `src/api-client/display.cpp` (unchanged from upstream) |
| Design + plan documents | `docs/superpowers/specs/2026-05-10-waveshare-12in48-driver-design.md` and `docs/superpowers/plans/2026-05-16-waveshare-1248b.md` + `docs/superpowers/plans/2026-05-17-waveshare-1248b-wroom-fallback.md` |

---

**End of document.** Hand this to the next LLM along with access to your BYOS codebase. The LLM should produce a plan in its first response, not a code dump.

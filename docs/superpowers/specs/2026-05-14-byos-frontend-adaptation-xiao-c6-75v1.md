# BYOS Server Adaptation for XIAO ESP32-C6 + Waveshare 7.5" V1

**Date:** 2026-05-14
**Audience:** an LLM (or human) working on a self-hosted TRMNL BYOS server, asked to add support for the new device this firmware introduces.

This document tells you everything you need to know about the firmware so you can adapt the server. It does NOT modify the firmware — the firmware contract is fixed and shipping.

---

## 1. What's new

This firmware introduces a new device build:

- **Board:** Seeed XIAO ESP32-C6
- **Display:** Waveshare 7.5" V1 (SKU 13187, panel GDEW075T8, IC UC8159c / IL0371)
- **Resolution:** **640 × 384, 1-bit B/W**
- **`DEVICE_MODEL` (firmware constant):** `"xiao_c6_75v1"`
- **PIO env:** `[env:xiao_esp32c6_75v1]`

Every other existing device build (TRMNL OG 800×480, 12.48", TRMNL-X, reTerminal, etc.) continues to work unchanged.

## 2. HTTP contract the firmware uses

The firmware speaks the **standard TRMNL BYOS protocol** — same one TRMNL OG uses. See the project's main README for the canonical reference. Summary:

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
  "filename": "2026-05-14T12:00:00",
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
- Image **must be exactly 640 × 384 pixels**.
- Image **must be 1-bit grayscale** (or any indexed/grayscale PNG that decodes to 1 bpp per scanline). GxEPD2 maps pixel `1` → white, pixel `0` → black.
- PNG should be **uncompressed enough that the decoded scanline buffer fits 6432 pixels** (the firmware sets `PNG_MAX_BUFFERED_PIXELS=6432`). Standard PNG compression is fine; only worry if you're outputting unusual PNG variants.
- Filesize: keep under ~85 KB to stay safely under the firmware's `MAX_IMAGE_SIZE = 90 000` byte cap.

## 3. How the server identifies this device

**Today: the firmware does not send a device-model identifier in HTTP headers.** The server only knows the device's MAC from the `ID` header. There are two ways to associate a MAC with the new model:

### Option A — admin-driven (recommended for v1)

After the device first registers via `/api/setup`, the server creates a record. The admin opens the dashboard, finds the new device, and picks `xiao_c6_75v1` (or equivalently "Waveshare 7.5" V1, 640×384 B/W") from a dropdown of supported device models. The server stores that association.

Subsequent `/api/display` responses produce a 640×384 PNG.

### Option B — firmware-supplied header (future-proof; requires both sides)

Add `Device-Model: xiao_c6_75v1` to the firmware's request headers in `src/api-client/`. Then the server can auto-detect on `/api/setup` and skip the manual step. This is a clean improvement but it requires a firmware change — out of scope for the current MVP.

**Tell the LLM:** start with Option A. It's the smaller change and works against the firmware as it ships. If you want Option B later, file a separate firmware PR.

## 4. Server-side checklist

What the BYOS server needs to do to render correctly for this device:

### 4.1 Add a device-model entry

Wherever your BYOS implementation enumerates supported devices (table, enum, config file, or Ruby/Python class), add:

```
key:              xiao_c6_75v1
display_name:     "Seeed XIAO ESP32-C6 + Waveshare 7.5\" V1"
width_px:         640
height_px:        384
bit_depth:        1
output_format:    png
color_palette:    bw           # white = 1, black = 0; no inversion
fake_battery:     true         # device always reports 4.20 V
```

### 4.2 Image rendering pipeline

Most BYOS implementations render plugin/dashboard screens by:
1. Running an HTML/Liquid template through a headless browser at a fixed viewport.
2. Capturing a screenshot.
3. Converting the screenshot to PNG with the device's bit depth.

You need to make sure the **viewport for `xiao_c6_75v1` is set to 640 × 384** and the post-processing output is **1-bit PNG**.

Concrete steps for the LLM:

1. Locate the rendering pipeline (search for callsites that take a device model and produce an image path).
2. Find where the existing TRMNL OG (800×480) profile is handled and clone the branch for 640×384.
3. Confirm the 1-bit PNG conversion path is the same — TRMNL OG is also 1-bit, so this likely needs zero new code.

### 4.3 Plugin / dashboard templates

This is the harder part. Most existing BYOS plugins were designed for the 800 × 480 TRMNL OG. They may **overflow or misalign at 640 × 384**.

Options, ordered from cheapest to most thorough:

- **Cheap:** render at 800×480 then resize to 640×384 with nearest-neighbor (preserves 1-bit). Quality is mediocre — text gets squished — but the dashboard works.
- **Better:** add a media-query / breakpoint in the plugin CSS to lay out at 640×384 explicitly.
- **Best:** make every plugin template responsive across the supported device widths.

The LLM should ask the human which one they want for v1. The cheap option is reasonable for a hobby setup.

### 4.4 Dashboard UI

The "Devices" page (or equivalent in your BYOS) should show the new model in the device-model dropdown and display the correct resolution in any per-device readout.

### 4.5 Setup image

`/api/setup` returns an `image_url` pointing at a "you're connected" placeholder image. Make sure your BYOS produces this at **640 × 384, 1-bit PNG** when the associated device is `xiao_c6_75v1`. (Otherwise the first render after pairing will be a no-op skipped with a warning log on the firmware side.)

## 5. Testing checklist

After the LLM finishes, verify:

1. **Pair a device.** Power on a freshly-flashed XIAO C6 + 7.5" V1, join the captive portal SSID `TRMNL-xxxx`, configure home WiFi. Device hits `/api/setup`. Confirm a new record appears in the BYOS admin.
2. **Associate the model.** In the BYOS admin, mark the device as `xiao_c6_75v1`.
3. **Force a refresh.** Trigger `/api/display` (or wait for the next poll).
4. **Verify the image URL** the server returns. `curl -I <url>` to confirm `Content-Type: image/png` and `Content-Length` < 90 000.
5. **Verify the image dimensions.** `curl <url> -o /tmp/x.png && file /tmp/x.png` should report `PNG image data, 640 x 384, 1-bit grayscale, non-interlaced` (or close — colortype 0 or 3 with 1 bit).
6. **Check the panel.** Image should render to the e-paper within ~30 s of the `/api/display` poll. No misaligned pixels, no inverted output, no torn rows.
7. **Boot button refresh.** Press the boot button on the XIAO (GPIO 9). Panel should re-poll and re-render.

## 6. Suggested LLM workflow

Hand this document plus the firmware repo URL to the next LLM, and ask:

> "Read `docs/superpowers/specs/2026-05-14-byos-frontend-adaptation-xiao-c6-75v1.md` to understand the new device. Then read the source of my BYOS server at `<path>`. Identify the files you'd need to change for §4 (device-model entry, rendering pipeline, plugin templates, dashboard UI, setup image). Produce a written plan before changing any code. After I approve the plan, execute it task-by-task with builds and tests after each step."

If the LLM tries to modify the firmware to make the server's life easier, push back — the firmware is shipped and the HTTP contract is fixed. The server adapts to the firmware, not the other way around (unless you explicitly opt in to the Option B firmware change in §3).

## 7. What this device intentionally does NOT need from the server

To keep the LLM from over-engineering:

- **No model-detection magic.** The firmware never sends `Device-Model` (today). Admin associates the MAC manually. Don't waste time on MAC-OUI lookups or other heuristics.
- **No G5 image format.** TRMNL has a G5-compressed image format for some plugins. This firmware uses standard PNG only. Don't bother adding G5 output for `xiao_c6_75v1`.
- **No BMP fallback.** Firmware skips BMP with a warning. Don't ship BMP responses to this device under any condition.
- **No firmware OTA.** The firmware accepts `update_firmware: true` responses but the user pushes new firmware over USB during MVP. The server can set `update_firmware: false` always for now.
- **No battery voltage tracking.** `FAKE_BATTERY_VOLTAGE` means the firmware always reports `4.20`. Don't draw battery graphs for this device — the data is fictitious.

## 8. Reference: firmware files the LLM may want to cross-reference

If the LLM wants to verify any claim in this document, here are the firmware files that ground the contract:

| Claim | Firmware file:line |
|---|---|
| `DEVICE_MODEL` constant | `include/config.h` (`#elif defined(BOARD_XIAO_ESP32C6_75V1)` arm) |
| Resolution 640 × 384 | `lib/trmnl_xiao_esp32c6_75v1/src/gxepd2_adapter.cpp` (`width()` / `height()`) and `GxEPD2_750::WIDTH / HEIGHT` in GxEPD2 |
| PNG magic byte check + BMP skip | `src/display.cpp` inside the `#if defined(BOARD_XIAO_ESP32C6_75V1)` arm, in `display_show_image` |
| `FAKE_BATTERY_VOLTAGE` | `include/config.h` (same arm as `DEVICE_MODEL`) |
| Boot-button GPIO 9 | `include/config.h` (`PIN_INTERRUPT 9`) |
| Captive-portal SSID | `lib/wificaptive` (unchanged from upstream) |
| Setup / display HTTP calls | `src/api-client/setup.cpp` and `src/api-client/display.cpp` (unchanged from upstream) |

---

**End of document.** Hand this to the next LLM along with access to your BYOS codebase. The LLM should produce a plan in its first response, not a code dump.

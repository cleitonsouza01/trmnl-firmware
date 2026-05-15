#!/usr/bin/env bash
# Wrapper for `pio run -e xiao_esp32c6_75v1 ...` that auto-recovers from the
# framework-arduinoespressif32 version conflict between the pioarduino fork
# (wants v3.3.x) and the upstream espressif32@6.x platform (wants v3.20017.x).
# Whichever env was built last wins the shared package slot, so we check the
# installed version and purge if it's not what the C6 env needs.
#
# Usage:
#   ./scripts/c6.sh                       # build only
#   ./scripts/c6.sh -t upload -t monitor  # build + flash + serial
#   ./scripts/c6.sh -t erase              # erase flash (for SPIFFS reformat)
set -e

FW_DIR="$HOME/.platformio/packages/framework-arduinoespressif32"
LIBS_DIR="${FW_DIR}-libs"
PKG_JSON="$FW_DIR/package.json"

if [ -f "$PKG_JSON" ]; then
    VERSION=$(grep '"version"' "$PKG_JSON" | head -1 | sed 's/.*"version": "\([^"]*\)".*/\1/')
    if [[ ! "$VERSION" =~ ^3\.[0-9]\. ]]; then
        echo "[c6.sh] Wrong framework version ($VERSION) — pioarduino needs 3.x; purging..."
        rm -rf "$FW_DIR" "$LIBS_DIR"
        echo "[c6.sh] Purged. PIO will re-fetch the pioarduino framework on next build."
    else
        echo "[c6.sh] Framework version $VERSION OK."
    fi
fi

exec pio run -e xiao_esp32c6_75v1 "$@"

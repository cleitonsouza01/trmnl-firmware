# Pre-build script for the xiao_esp32c6_75v1 env.
#
# Background:
# The pioarduino fork wants framework-arduinoespressif32 v3.x (pinned to
# v3.3.7 via URL in its platform.json), while the upstream espressif32@6.x
# platforms used by the other envs in this repo want v3.20017.x. Both land
# in ~/.platformio/packages/framework-arduinoespressif32/ and overwrite
# each other. If the other envs were built more recently, the C6 build
# fails with:
#
#   TypeError: expected str, bytes or os.PathLike object, not NoneType
#   File ".../arduino.py", line 997: build_script_path = str(Path(FRAMEWORK_DIR) / ...)
#
# Strategy: stash + swap.
# We keep a cached copy of each known-good version under
#   ~/.platformio/packages/framework-arduinoespressif32.cache_v3       (pioarduino)
#   ~/.platformio/packages/framework-arduinoespressif32.cache_v20017   (upstream)
# (and the matching `-libs` directories.)
#
# When this script runs for the C6 env, we:
#   1. Read the installed version.
#   2. If it's already pioarduino (3.x but not 3.20*), do nothing.
#   3. If it's upstream (3.20*), move the upstream dirs to cache_v20017
#      and restore cache_v3 to active. If cache_v3 doesn't exist yet,
#      delete the upstream copy so PIO refetches from URL on this run
#      and the new pioarduino version becomes the active one; next time
#      a swap happens, the post-build cache will catch it.

import json
import os
import shutil

Import("env")

PKG_ROOT = os.path.expanduser("~/.platformio/packages")
FW = os.path.join(PKG_ROOT, "framework-arduinoespressif32")
LIBS = FW + "-libs"
CACHE_V3 = FW + ".cache_v3"
CACHE_V3_LIBS = LIBS + ".cache_v3"
CACHE_V20017 = FW + ".cache_v20017"
CACHE_V20017_LIBS = LIBS + ".cache_v20017"


def read_version(path):
    pkg_json = os.path.join(path, "package.json")
    if not os.path.isfile(pkg_json):
        return None
    try:
        with open(pkg_json) as f:
            return json.load(f).get("version", "")
    except (OSError, ValueError):
        return None


def is_pioarduino(version):
    return version and version.startswith("3.") and not version.startswith("3.20")


def is_upstream(version):
    return version and version.startswith("3.20")


def swap_dirs(src, dst):
    """Move src -> dst. If dst exists, remove it first."""
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    if os.path.isdir(src):
        shutil.move(src, dst)


def main():
    version = read_version(FW)

    if version is None:
        # Nothing installed yet; if we have a stash, restore it.
        if os.path.isdir(CACHE_V3):
            print("[ensure_pioarduino_framework] No framework installed; "
                  "restoring pioarduino v3.3.7 from cache_v3.")
            shutil.move(CACHE_V3, FW)
            if os.path.isdir(CACHE_V3_LIBS):
                shutil.move(CACHE_V3_LIBS, LIBS)
            return
        print("[ensure_pioarduino_framework] No framework installed; "
              "PIO will fetch from URL.")
        return

    if is_pioarduino(version):
        print("[ensure_pioarduino_framework] Active framework is "
              "pioarduino %s — OK." % version)
        return

    if is_upstream(version):
        print("[ensure_pioarduino_framework] Active framework is upstream "
              "%s — stashing to cache_v20017." % version)
        swap_dirs(FW, CACHE_V20017)
        swap_dirs(LIBS, CACHE_V20017_LIBS)

        if os.path.isdir(CACHE_V3):
            print("[ensure_pioarduino_framework] Restoring pioarduino v3.x "
                  "from cache_v3 (instant — no download).")
            shutil.move(CACHE_V3, FW)
            if os.path.isdir(CACHE_V3_LIBS):
                shutil.move(CACHE_V3_LIBS, LIBS)
        else:
            print("[ensure_pioarduino_framework] No cache_v3 yet; PIO will "
                  "fetch pioarduino's framework. It will be cached after "
                  "this build for instant swaps next time.")
        return

    print("[ensure_pioarduino_framework] Unknown framework version "
          "(%s); leaving alone." % version)


def cache_pioarduino_on_exit():
    """After this build completes, snapshot the active framework into
    cache_v3 so the next upstream-build → C6-build swap can restore it
    without re-downloading."""
    version = read_version(FW)
    if not is_pioarduino(version):
        return  # don't cache an upstream version into the pioarduino slot
    # Make a copy (not a move) so the active install stays usable.
    if os.path.isdir(CACHE_V3):
        shutil.rmtree(CACHE_V3)
    shutil.copytree(FW, CACHE_V3)
    if os.path.isdir(LIBS):
        if os.path.isdir(CACHE_V3_LIBS):
            shutil.rmtree(CACHE_V3_LIBS)
        shutil.copytree(LIBS, CACHE_V3_LIBS)
    print("[ensure_pioarduino_framework] Cached pioarduino v%s to "
          "cache_v3 (will be restored on next swap)." % version)


main()

# Register a post-action so we snapshot after a successful build.
# We hook on the firmware.elf build action.
def post_action(source, target, env):
    cache_pioarduino_on_exit()


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", post_action)

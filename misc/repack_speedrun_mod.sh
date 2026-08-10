#!/bin/sh
# Repacks the speedrun mod pk3: full legacy mod content (from the official release
# pk3) with the freshly built speedrun client modules swapped in.
#
# The result is a self-contained mod package for the "speedrun" mod dir:
# clients connecting to a server running fs_game "speedrun" auto-download
# it into their homepath <mod>/ folder (no manual installation).
#
# Usage: misc/repack_speedrun_mod.sh [source_release_pk3] [build_dir] [dest_dir]
#   source_release_pk3  official mod pk3 providing the base content (default:
#                       /Applications/ETLegacy/legacy/legacy_v2.84.0.pk3)
#   build_dir           cmake build dir with fresh modules (default: cmake-build-arm64)
#   dest_dir            target mod dir (default: cmake-build-debug/speedrun)
#
# The pk3 is written as speedrun_v0.<N>.pk3 with the minor version
# auto-incremented on every repack (a new name makes clients re-download;
# a same-name repack would also re-download via pak checksum mismatch).

set -e

# the mod's own version, independent of the legacy base release (v2.84.0)
# that supplies the base content; the minor is auto-incremented on every
# repack (see below) — MOD_VERSION only sets the starting point
MOD_VERSION="v0.1"
SOURCE_PK3="${1:-/Applications/ETLegacy/legacy/legacy_v2.84.0.pk3}"
BUILD_DIR="${2:-cmake-build-arm64}"
DEST_DIR="${3:-cmake-build-debug/speedrun}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/$BUILD_DIR"
DEST_DIR="$ROOT/$DEST_DIR"

if [ ! -f "$SOURCE_PK3" ]; then
    echo "error: source pk3 not found: $SOURCE_PK3" >&2
    exit 1
fi

for mod in cgame_mac ui_mac; do
    if [ ! -f "$BUILD_DIR/legacy/$mod" ]; then
        echo "error: $BUILD_DIR/legacy/$mod not found (build the mod first)" >&2
        exit 1
    fi
done

mkdir -p "$DEST_DIR"

# next minor version: highest existing speedrun_v0.<N>.pk3 + 1, or the
# MOD_VERSION minor when no pk3 exists yet
MINOR="${MOD_VERSION#v0.}"
for f in "$DEST_DIR"/speedrun_v0.*.pk3; do
    [ -e "$f" ] || continue
    n="${f##*v0.}"
    n="${n%.pk3}"
    case "$n" in
        ''|*[!0-9]*) continue ;;
    esac
    if [ "$n" -ge "$MINOR" ] 2>/dev/null; then
        MINOR=$((n + 1))
    fi
done

OUT="$DEST_DIR/speedrun_v0.${MINOR}.pk3"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

unzip -qo "$SOURCE_PK3" -d "$TMP"
cp "$BUILD_DIR/legacy/cgame_mac" "$TMP/cgame_mac"
cp "$BUILD_DIR/legacy/ui_mac" "$TMP/ui_mac"

# speedrun mod: ship ONLY the repo's game configs (configs/ in the official
# release pk3 carries legacy1/3/5/6 which the user deleted). Strip them and
# inject etmain/configs/*.config (shortruns, fullmaprun).
rm -f "$TMP/configs/legacy"*.config
rm -f "$TMP/configs/defaultpublic.config"
mkdir -p "$TMP/configs"
cp "$ROOT/etmain/configs/"*.config "$TMP/configs/"

# speedrun mod: overlay the repo's ui/ menus + menudef.h (the official pk3's
# vote menus still reference the deleted configs; the repo etmain/ui is their
# source. menudef.h MUST be overlaid too: the menu parser expands its #defines
# at load time, and new CV_SVF_* flags only exist in the repo's copy)
cp "$ROOT/etmain/ui/"*.menu "$TMP/ui/"
cp "$ROOT/etmain/ui/menudef.h" "$TMP/ui/menudef.h"

# speedrun mod: ship the timerun shaders (debug zone boxes + zone point markers).
# (scripts/ is the classic renderer's shader dir; materials/ is renderer2-only)
mkdir -p "$TMP/scripts"
cp "$ROOT/etmain/scripts/"*.shader "$TMP/scripts/"


# cross-compiled Windows modules (optional: if the win64/win32 cross builds
# exist, replace the stock release modules so Windows clients get the same
# cgame/ui fixes as the Mac build)
for mod in cgame_mp_x64.dll ui_mp_x64.dll; do
    [ -f "$ROOT/build-win64/legacy/$mod" ] && cp "$ROOT/build-win64/legacy/$mod" "$TMP/$mod"
done
for mod in cgame_mp_x86.dll ui_mp_x86.dll; do
    [ -f "$ROOT/build-win32/legacy/$mod" ] && cp "$ROOT/build-win32/legacy/$mod" "$TMP/$mod"
done

# cross-compiled Linux modules (optional: if the linux cross builds exist,
# replace the stock release modules so Linux clients get the same cgame/ui fixes)
for mod in cgame.mp.x86_64.so ui.mp.x86_64.so; do
    [ -f "$ROOT/build-linux64/legacy/$mod" ] && cp "$ROOT/build-linux64/legacy/$mod" "$TMP/$mod"
done
for mod in cgame.mp.aarch64.so ui.mp.aarch64.so; do
    [ -f "$ROOT/build-linux-arm64/legacy/$mod" ] && cp "$ROOT/build-linux-arm64/legacy/$mod" "$TMP/$mod"
done

( cd "$TMP" && zip -qr "$OUT" . )

# purge other versions: a stale different-version pk3 sorts BEFORE/AFTER this
# one and would shadow its modules on the client (paks load alphabetically)
for f in "$DEST_DIR"/speedrun_*.pk3; do
    [ -e "$f" ] || continue
    if [ "$f" != "$OUT" ]; then
        rm -f "$f"
        echo "removed stale: $f"
    fi
done

echo "wrote: $OUT ($(du -h "$OUT" | awk '{print $1}'))"
echo "server: +set fs_game speedrun  (clients auto-download this pk3)"

#!/bin/sh
# Repacks the timerun mod pk3: full legacy mod content (from the official release
# pk3) with the freshly built timerun client modules swapped in.
#
# The result is a self-contained mod package for the "legacy-timerun" mod dir:
# clients connecting to a server running fs_game "legacy-timerun" auto-download
# it into their homepath <mod>/ folder (no manual installation).
#
# Usage: misc/repack_timerun_mod.sh [source_release_pk3] [build_dir] [dest_dir]
#   source_release_pk3  official mod pk3 providing the base content (default:
#                       /Applications/ETLegacy/legacy/legacy_v2.84.0.pk3)
#   build_dir           cmake build dir with fresh modules (default: cmake-build-arm64)
#   dest_dir            target mod dir (default: cmake-build-debug/legacy-timerun)
#
# The pk3 is written as legacy_timerun_<base>_r<N>.pk3 with an auto-incremented
# revision so clients that already downloaded an older revision re-download.

set -e

BASE_VERSION="v2.84.0"
SOURCE_PK3="${1:-/Applications/ETLegacy/legacy/legacy_v2.84.0.pk3}"
BUILD_DIR="${2:-cmake-build-arm64}"
DEST_DIR="${3:-cmake-build-debug/legacy-timerun}"

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

# next revision: scan existing legacy_timerun_<base>_r*.pk3
REV=1
for f in "$DEST_DIR"/legacy_timerun_${BASE_VERSION}_r*.pk3; do
    [ -e "$f" ] || continue
    n="${f##*_r}"
    n="${n%.pk3}"
    if [ "$n" -ge "$REV" ] 2>/dev/null; then
        REV=$((n + 1))
    fi
done

OUT="$DEST_DIR/legacy_timerun_${BASE_VERSION}_r${REV}.pk3"

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

# speedrun mod: ship the cull-none debug shader for the timerun zone boxes
# (scripts/ is the classic renderer's shader dir; materials/ is renderer2-only)
mkdir -p "$TMP/scripts"
cp "$ROOT/etmain/scripts/timerun_debug.shader" "$TMP/scripts/"


# cross-compiled Windows modules (optional: if the win64/win32 cross builds
# exist, replace the stock release modules so Windows clients get the same
# cgame/ui fixes as the Mac build)
for mod in cgame_mp_x64.dll ui_mp_x64.dll; do
    [ -f "$ROOT/build-win64/legacy/$mod" ] && cp "$ROOT/build-win64/legacy/$mod" "$TMP/$mod"
done
for mod in cgame_mp_x86.dll ui_mp_x86.dll; do
    [ -f "$ROOT/build-win32/legacy/$mod" ] && cp "$ROOT/build-win32/legacy/$mod" "$TMP/$mod"
done

( cd "$TMP" && zip -qr "$OUT" . )

# purge older revisions: a stale lower revision sorts BEFORE the new one and
# would shadow its modules on the client (paks load alphabetically)
for f in "$DEST_DIR"/legacy_timerun_${BASE_VERSION}_r*.pk3; do
    [ -e "$f" ] || continue
    if [ "$f" != "$OUT" ]; then
        rm -f "$f"
        echo "removed stale: $f"
    fi
done

echo "wrote: $OUT ($(du -h "$OUT" | awk '{print $1}'))"
echo "server: +set fs_game legacy-timerun  (clients auto-download this pk3)"

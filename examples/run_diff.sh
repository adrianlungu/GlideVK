#!/usr/bin/env bash
# Differential test: run a Glide 2 harness beside a REFERENCE glide2x.dll and
# beside GlideVK's, and diff their stdout. The harnesses in this set read pixels
# back through grLfbReadRegion (exported by every Glide 2 implementation) and
# print classified colours, so the reference DLL defines correct behaviour and
# GlideVK is judged against it rather than against its own assumptions.
#
# Usage:  examples/run_diff.sh [harness...]      (default: wine_texcoord)
# Env:    GLIDE_API=glide2x|glide3x   which shell to build and which DLL the
#           harness loads (default glide2x; glide3x harnesses are wine3_*.c)
#         GLIDE_REF=/path/to/reference/<api>.dll
#           (default: nGlide's copies in the Deus Ex install — glide2x from the
#           backup made by deusex_glidevk.sh, glide3x in place)
#         WINEPREFIX  honoured as usual; WINEDEBUG defaults to -all
set -euo pipefail
cd "$(dirname "$0")/.."

TARGET=i686-pc-windows-gnu
CC=i686-w64-mingw32-gcc
OUT="target/$TARGET/release"
API="${GLIDE_API:-glide2x}"
DIFF="target/diff/$API"
DEUSEX_SYS="/media/games/Games/gog/deus-ex/drive_c/GOG Games/Deus Ex GOTY/System"
case "$API" in
  glide2x) DEFAULT_REF="$DEUSEX_SYS/glide2x.dll.glidevk-orig" ;;
  glide3x) DEFAULT_REF="$DEUSEX_SYS/glide3x.dll" ;;
  *) echo "GLIDE_API must be glide2x or glide3x" >&2; exit 2 ;;
esac
REF="${GLIDE_REF:-$DEFAULT_REF}"
[ -f "$REF" ] || { echo "reference DLL not found: $REF (set GLIDE_REF)" >&2; exit 2; }

echo ">> building $API.dll ($TARGET, release)"
cargo build -p "$API" --target "$TARGET" --release

mkdir -p "$DIFF/ref" "$DIFF/ours"
cp "$REF" "$DIFF/ref/$API.dll"
cp "$OUT/$API.dll" "$DIFF/ours/$API.dll"

status=0
for h in "${@:-wine_texcoord}"; do
    echo ">> building $h"
    "$CC" -O2 -o "$DIFF/$h.exe" "examples/$h.c" -lgdi32
    for side in ref ours; do
        cp "$DIFF/$h.exe" "$DIFF/$side/"
        ( cd "$DIFF/$side" && GLIDEVK_WINDOWED=1 WINEDEBUG="${WINEDEBUG:--all}" timeout 60 wine "./$h.exe" ) \
            > "$DIFF/$h.$side.txt" 2>"$DIFF/$h.$side.err" || echo "!! $h ($side) exited $?" >&2
    done
    if diff -u "$DIFF/$h.ref.txt" "$DIFF/$h.ours.txt" > "$DIFF/$h.diff"; then
        echo "PASS $h"
        sed 's/^/    /' "$DIFF/$h.ours.txt"
    else
        echo "DIFF $h (- reference, + glidevk)"
        grep -E '^[-+][^-+]' "$DIFF/$h.diff" | sed 's/^/    /'
        status=1
    fi
done
exit $status

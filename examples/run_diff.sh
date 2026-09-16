#!/usr/bin/env bash
# Differential test: run a Glide 2 harness beside a REFERENCE glide2x.dll and
# beside GlideVK's, and diff their stdout. The harnesses in this set read pixels
# back through grLfbReadRegion (exported by every Glide 2 implementation) and
# print classified colours, so the reference DLL defines correct behaviour and
# GlideVK is judged against it rather than against its own assumptions.
#
# Usage:  examples/run_diff.sh [harness...]      (default: wine_texcoord)
# Env:    GLIDE2X_REF=/path/to/reference/glide2x.dll
#           (default: the nGlide copy backed up by deusex_glidevk.sh)
#         WINEPREFIX  honoured as usual; WINEDEBUG defaults to -all
set -euo pipefail
cd "$(dirname "$0")/.."

TARGET=i686-pc-windows-gnu
CC=i686-w64-mingw32-gcc
OUT="target/$TARGET/release"
DIFF="target/diff"
REF="${GLIDE2X_REF:-/media/games/Games/gog/deus-ex/drive_c/GOG Games/Deus Ex GOTY/System/glide2x.dll.glidevk-orig}"
[ -f "$REF" ] || { echo "reference DLL not found: $REF (set GLIDE2X_REF)" >&2; exit 2; }

echo ">> building glide2x.dll ($TARGET, release)"
cargo build -p glide2x --target "$TARGET" --release

mkdir -p "$DIFF/ref" "$DIFF/ours"
cp "$REF" "$DIFF/ref/glide2x.dll"
cp "$OUT/glide2x.dll" "$DIFF/ours/glide2x.dll"

status=0
for h in "${@:-wine_texcoord}"; do
    echo ">> building $h"
    "$CC" -O2 -o "$DIFF/$h.exe" "examples/$h.c" -lgdi32
    for side in ref ours; do
        cp "$DIFF/$h.exe" "$DIFF/$side/"
        ( cd "$DIFF/$side" && WINEDEBUG="${WINEDEBUG:--all}" timeout 60 wine "./$h.exe" ) \
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

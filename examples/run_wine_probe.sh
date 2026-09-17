#!/usr/bin/env bash
# Build glide3x.dll + the C harnesses and run the winevulkan smoke tests under
# Wine. Proves a game-style LoadLibrary of our PE DLL reaches the GPU through
# winevulkan, and that the windowed swapchain/present path works.
#
# Usage:  examples/run_wine_probe.sh [32|64]   (default: 32 — the ship target)
set -euo pipefail
cd "$(dirname "$0")/.."

case "${1:-32}" in
  32) TARGET=i686-pc-windows-gnu;   CC=i686-w64-mingw32-gcc ;;
  64) TARGET=x86_64-pc-windows-gnu; CC=x86_64-w64-mingw32-gcc ;;
  *)  echo "usage: $0 [32|64]" >&2; exit 2 ;;
esac
OUT="target/$TARGET/debug"

echo ">> building glide3x.dll ($TARGET)"
cargo build -p glide3x --target "$TARGET"

echo ">> building harnesses ($CC)"
"$CC" -O2 -o "$OUT/wine_probe.exe"  examples/wine_probe.c
"$CC" -O2 -o "$OUT/wine_window.exe" examples/wine_window.c -lgdi32

cd "$OUT"                                # so wine finds glide3x.dll beside the exe
echo ">> headless probe"
GLIDEVK_WINDOWED=1 WINEDEBUG=${WINEDEBUG:--all} wine ./wine_probe.exe
echo ">> windowed present (needs a display)"
GLIDEVK_WINDOWED=1 WINEDEBUG=${WINEDEBUG:--all} wine ./wine_window.exe

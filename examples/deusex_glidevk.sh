#!/usr/bin/env bash
# Install (or restore) GlideVK's glide2x.dll into Deus Ex GOTY and switch it to
# the Glide renderer, so you can launch it (via Lutris or wine) and see GlideVK
# render a real Unreal-engine game. Non-destructive: originals are backed up.
#
#   examples/deusex_glidevk.sh install   # swap in GlideVK + Glide renderer
#   examples/deusex_glidevk.sh run        # launch the game in its prefix
#   examples/deusex_glidevk.sh restore    # put the originals back
#
# 'run' launches the game directly in its prefix. GLIDEVK_* variables are passed
# through, e.g. GLIDEVK_LOG=/tmp/glidevk.log for the trace (with a 'perf' line
# every 120 frames) or GLIDEVK_CAPTURE=/tmp/frame.ppm GLIDEVK_CAPTURE_FRAME=-100
# for periodic frame dumps.
set -euo pipefail
SYS="/media/games/Games/gog/deus-ex/drive_c/GOG Games/Deus Ex GOTY/System"
PREFIX="/media/games/Games/gog/deus-ex"
OURS="$(dirname "$0")/../target/i686-pc-windows-gnu/release/glide2x.dll"

case "${1:-}" in
  install)
    [ -f "$SYS/glide2x.dll.glidevk-orig" ] || cp "$SYS/glide2x.dll" "$SYS/glide2x.dll.glidevk-orig"
    [ -f "$SYS/DeusEx.ini.glidevk-orig" ] || cp "$SYS/DeusEx.ini" "$SYS/DeusEx.ini.glidevk-orig"
    cp "$OURS" "$SYS/glide2x.dll"
    # Only the fullscreen device goes to Glide. WindowedRenderDevice must stay
    # SoftDrv: Unreal's GlideDrv is fullscreen-only (its SetRes returns 0 for a
    # windowed request), and on WM_KILLFOCUS the engine re-inits with the
    # *windowed* device. Pointing that at GlideDrv is what produced the
    # "3d hardware initialization failed" -> "Assertion failed: RenDev" crash on
    # any focus loss, with real Glide drivers too. With SoftDrv the game drops to
    # software while minimized and re-enters Glide fullscreen on restore.
    sed -i 's/^GameRenderDevice=.*/GameRenderDevice=GlideDrv.GlideRenderDevice/; s/^WindowedRenderDevice=.*/WindowedRenderDevice=SoftDrv.SoftwareRenderDevice/' "$SYS/DeusEx.ini"
    # `install windowed` sets StartupFullscreen=False, which with GlideDrv
    # (fullscreen-only) means the game starts on its software renderer; the
    # default (fullscreen) is what GlideVK renders.
    if [ "${2:-}" = "windowed" ]; then
        sed -i 's/^StartupFullscreen=.*/StartupFullscreen=False/' "$SYS/DeusEx.ini"
    else
        sed -i 's/^StartupFullscreen=.*/StartupFullscreen=True/' "$SYS/DeusEx.ini"
    fi
    echo "Installed GlideVK glide2x.dll + Glide renderer${2:+ ($2)}."
    echo "Launch with:  examples/deusex_glidevk.sh run"
    echo "Alt-tab crashes in fullscreen? Reinstall with:  examples/deusex_glidevk.sh install windowed"
    echo "(set GLIDEVK_LOG=1 for a trace; GLIDEVK_CAPTURE=<path.ppm> to dump a frame; GLIDEVK_WIREFRAME=1 for wireframe)"
    ;;
  run)
    # Unreal drops a Running.ini marker at boot and removes it on a clean exit.
    # A crash leaves it behind, so the next launch shows the "safe mode" dialog.
    # Clear it so we always boot straight into the game.
    rm -f "$SYS/Running.ini"
    cd "$SYS"
    # Wine paths for the GLIDEVK_* file variables: a Linux absolute path works
    # as-is under Wine's Z: drive mapping.
    exec env WINEPREFIX="$PREFIX" WINEDEBUG="${WINEDEBUG:--all}" wine DeusEx.exe
    ;;
  restore)
    [ -f "$SYS/glide2x.dll.glidevk-orig" ] && cp "$SYS/glide2x.dll.glidevk-orig" "$SYS/glide2x.dll"
    [ -f "$SYS/DeusEx.ini.glidevk-orig" ] && cp "$SYS/DeusEx.ini.glidevk-orig" "$SYS/DeusEx.ini"
    echo "Restored original glide2x.dll + DeusEx.ini."
    ;;
  *) echo "usage: $0 {install|run|restore}"; exit 2 ;;
esac

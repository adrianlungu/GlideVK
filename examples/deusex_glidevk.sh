#!/usr/bin/env bash
# Install (or restore) GlideVK's glide2x.dll into Deus Ex GOTY and switch it to
# the Glide renderer, so you can launch it (via Lutris or wine) and see GlideVK
# render a real Unreal-engine game. Non-destructive: originals are backed up.
#
#   examples/deusex_glidevk.sh install   # swap in GlideVK + Glide renderer
#   examples/deusex_glidevk.sh run        # launch in a Wine virtual desktop
#   examples/deusex_glidevk.sh restore    # put the originals back
#
# The 'run' target launches inside a Wine VIRTUAL DESKTOP. This is important:
# Unreal 1 crashes on focus loss (alt-tab / screenshot -> WM_KILLFOCUS ->
# EndFullscreen asserts a null RenDev — an engine bug, not the wrapper). Inside a
# virtual desktop the game window keeps focus, so alt-tab/screenshots don't crash.
set -euo pipefail
SYS="/media/games/Games/gog/deus-ex/drive_c/GOG Games/Deus Ex GOTY/System"
PREFIX="/media/games/Games/gog/deus-ex"
OURS="$(dirname "$0")/../target/i686-pc-windows-gnu/release/glide2x.dll"

case "${1:-}" in
  install)
    [ -f "$SYS/glide2x.dll.glidevk-orig" ] || cp "$SYS/glide2x.dll" "$SYS/glide2x.dll.glidevk-orig"
    [ -f "$SYS/DeusEx.ini.glidevk-orig" ] || cp "$SYS/DeusEx.ini" "$SYS/DeusEx.ini.glidevk-orig"
    cp "$OURS" "$SYS/glide2x.dll"
    sed -i 's/^GameRenderDevice=.*/GameRenderDevice=GlideDrv.GlideRenderDevice/; s/^WindowedRenderDevice=.*/WindowedRenderDevice=GlideDrv.GlideRenderDevice/' "$SYS/DeusEx.ini"
    # `install windowed` runs the game WINDOWED (StartupFullscreen=False). This is
    # the proper fix for the Unreal-1 alt-tab/focus-loss crash (an engine bug:
    # WM_KILLFOCUS -> EndFullscreen asserts a null RenDev) and, unlike a Wine
    # virtual desktop, it also works under Proton. Fullscreen otherwise.
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
    # Virtual desktop at the game's resolution avoids the Unreal focus-loss crash.
    cd "$SYS"
    exec env WINEPREFIX="$PREFIX" wine explorer /desktop=deusex,960x720 DeusEx.exe
    ;;
  restore)
    [ -f "$SYS/glide2x.dll.glidevk-orig" ] && cp "$SYS/glide2x.dll.glidevk-orig" "$SYS/glide2x.dll"
    [ -f "$SYS/DeusEx.ini.glidevk-orig" ] && cp "$SYS/DeusEx.ini.glidevk-orig" "$SYS/DeusEx.ini"
    echo "Restored original glide2x.dll + DeusEx.ini."
    ;;
  *) echo "usage: $0 {install|run|restore}"; exit 2 ;;
esac

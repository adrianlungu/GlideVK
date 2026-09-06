#!/usr/bin/env bash
# Regenerate the committed SPIR-V from the GLSL sources. Run after editing any
# .vert/.frag here. Requires glslc (Fedora: glslc / vulkan-tools).
set -euo pipefail
cd "$(dirname "$0")"
for src in *.vert *.frag; do
    [ -e "$src" ] || continue
    glslc -O "$src" -o "$src.spv"
    echo "compiled $src -> $src.spv"
done

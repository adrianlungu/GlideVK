# GlideVK — Feasibility Study

**A native Linux 3dfx Glide → Vulkan translation layer.**
The Glide equivalent of DXVK/D7VK: translate `glide2x` / `glide3x` API calls
directly to Vulkan, with no Direct3D or OpenGL hop in between.

Context: proposed by strycore in
[lutris/dgvoodoo2#5](https://github.com/lutris/dgvoodoo2/issues/5) as the
missing piece — "the equivalent of D7VK for Voodoo" — to sit alongside D7VK,
nGlide, and cncdraw for full legacy-API coverage on Linux/Wine.

---

## Verdict

**Feasible, and materially easier than DXVK.** Glide is a tiny, well-documented,
fixed-function API whose reference source is open. The whole Glide 3.x surface is
~80–100 exported functions with no shaders, no programmable pipeline, and a
rendering model that predates and is simpler than Direct3D 7. The real work is
not breadth — it is *pixel-fidelity* on a handful of quirks (the combine units,
linear-framebuffer access, 16-bit dithering, palettized/NCC textures).

This is a realistic solo-to-small-team project. A "runs a real game" milestone is
a matter of weeks, not the years DXVK represented — precisely because Glide is
1996-era fixed function, not a moving modern API.

---

## Why Glide is a small target

Glide is a thin abstraction over Voodoo Graphics / Voodoo2 / Voodoo3 hardware.
There is no scene graph, no state cache to speak of, no shader compiler. The
entire pipeline is:

```
vertices ──▶ triangle setup ──▶ per-pixel: [ texture fetch (TMU0..2) ]
                                            [ texture combine   ]
                                            [ color combine     ]
                                            [ alpha/fog/chroma  ]
                                            [ alpha blend       ]
                                            [ 16-bit dither     ] ──▶ framebuffer
```

Everything a game can configure maps to a small, enumerable state vector:

| Glide concept                        | Vulkan mapping                                            |
|--------------------------------------|-----------------------------------------------------------|
| `grDrawTriangle` / `grDrawVertexArray` | vertex buffer + `vkCmdDraw`                              |
| `grColorCombine` / `grAlphaCombine`  | generated fragment shader (combiner → GLSL/SPIR-V)        |
| `grTexCombine` (TMU0/1/2)            | fragment shader texture stages                            |
| `grTexSource` / `grTexDownloadMipMap`| `VkImage` + sampler; format conversion on upload          |
| `grAlphaBlendFunction`               | `VkPipelineColorBlendAttachmentState`                     |
| `grDepthBufferMode` / W-buffer       | depth attachment (+ compute/shader for W-buffer)          |
| `grFogMode` / fog table              | fragment shader + 1D fog LUT                              |
| `grChromakeyMode`                    | fragment `discard`                                        |
| `grLfbLock` / `grLfbUnlock`          | staging buffer read/write of the color/depth image       |
| `grBufferSwap`                       | swapchain present                                         |

The combiner state is finite and small, so the classic DXVK-style approach works:
**hash the pipeline/combine state → look up or JIT a fragment shader → cache it.**
There will only ever be a few dozen distinct shaders per game.

---

## Architecture

Two deployment targets, same core:

### 1. Wine PE DLLs (primary — this is what Lutris ships)

Glide games are Windows games. Under Wine they `LoadLibrary("glide3x.dll")` (or
`glide2x.dll`, or `glide.dll` for Glide 1/2). We provide drop-in PE DLLs, exactly
as DXVK provides `d3d9.dll` etc.:

```
game.exe
  └─ glide3x.dll  (GlideVK, PE build)   ← DLL override in the prefix
        └─ vulkan-1.dll → winevulkan     ← same path DXVK uses
              └─ host Vulkan ICD (RADV / NVIDIA / ANV)
```

- Built with the MinGW toolchain (or Rust `*-pc-windows-gnu`), linking the
  in-prefix Vulkan loader — **identical model to DXVK.** We can reuse DXVK's
  `winevulkan`/`native-vk` bridge patterns wholesale.
- Exports must match the real 3dfx DLLs exactly (undecorated names via a `.def`
  file, `__stdcall` / `FX_CALL` convention). The open Glide SDK gives us the
  authoritative export list and ordinals.
- Ships with an HUD/config story like DXVK (`GLIDEVK_HUD`, config file).

### 2. Native Linux `.so` (secondary — nice-to-have)

A handful of native Linux Glide ports exist (Loki-era titles, `libglide2.so`
consumers, some id/Epic ports). Same Vulkan core, ELF build, `libglide2.so` /
`libglide3.so` SONAMEs. Low priority but nearly free once the core exists.

### Core (shared, backend-agnostic)

```
glidevk-core
├─ api/           # glide2x + glide3x entry points (thin, per-DLL)
├─ state/         # the Glide state machine (combine, blend, tex, fog, depth)
├─ pipeline/      # state-hash → SPIR-V fragment shader cache
├─ texture/       # format conversion: P8, ARGB1555/4444, NCC/YIQ, mipmaps
├─ lfb/           # linear framebuffer lock/unlock via staging buffers
├─ present/       # swapchain, buffer swap, optional 3dfx post-filter/dither
└─ vk/            # Vulkan device/queue/allocator wrapper
```

---

## The hard parts (where the real time goes)

1. **Linear Frame Buffer (LFB) access.** Games `grLfbLock` and read/write raw
   16-bit pixels — menus, software blits, screenshots, some effects. Requires
   round-tripping the Vulkan color image through a staging buffer with correct
   pixel-format packing (RGB565 / ARGB1555). This is the #1 correctness and
   #1 performance pitfall in every Glide wrapper (OpenGLide, nGlide, dgVoodoo).

2. **Combine-unit fidelity.** `grColorCombine`/`grTexCombine`/`grAlphaCombine`
   are a configurable fixed-function fragment pipeline (think GL 1.x `TexEnv`
   combine, but 3dfx-specific: `LOCAL`, `OTHER`, `FACTOR`, invert bits, scale).
   Getting the exact arithmetic right is what separates "boots" from "looks
   correct." The open SDK documents the exact per-pixel math.

3. **Texture formats.** P8 palettized, ARGB1555/4444/8332, and NCC/YIQ
   (3dfx's 2-bit-per-texel compressed format) all need CPU/compute conversion
   to `VkImage`-compatible formats. Palette + NCC table state is stateful.

4. **The "3dfx look" (optional, but people care).** 16-bit rendering with the
   ordered dither + the 22-bit post-filter/gamma is the nostalgic Voodoo image.
   nGlide's headline feature. Emulate as an optional post-process pass; ship
   off-by-default, expose a toggle.

5. **Split/paletted edge cases:** guard-band clipping, `grLfbConstantDepth`,
   triple buffering, `gu*` legacy helpers (Glide2 utility layer), and the
   `grSst*` legacy entry points. Enumerable and small, just tedious.

---

## Reference material (all clean to use)

- **Open Glide SDK** — `glide2x` + `glide3x` source, headers, exact semantics.
  License: *3dfx Glide Source Code General Public License* (SPDX `Glide`) — a
  permissive, source-available, non-viral license that explicitly allows
  derivative works. Mirrors: `sezero/glide`, `hatarch/glide3x`,
  `Danaozhong/3dfx-Glide-API` (CMake, x64). **This is the authoritative spec.**
- **OpenGLide** (`fcbarros/openglide`, `voyageur/openglide`) — Glide→OpenGL,
  GPL. Reference for *how* to translate combine/LFB, not for copy-paste.
- **psVoodoo / d2gl** (`bayaraa/d2gl`) — modern Glide/DDraw→OpenGL wrappers,
  good structural references.
- **DXVK / D7VK** — not for Glide logic, but for the Wine PE-DLL build system,
  `winevulkan` bridging, HUD, config, state-hash→pipeline-cache patterns.
  Note: D7VK proxies D3D7→DXVK-D3D9; Glide has no analog, so we build a small
  real Vulkan backend instead.

---

## Language choice — the one real fork

- **C++** — matches DXVK/D7VK exactly; we can lift their build system,
  winevulkan bridge, meson cross-files, and pipeline-cache scaffolding almost
  verbatim. Lowest-friction path to a working PE DLL under Wine. Largest pool of
  prior art.
- **Rust** — cleaner core, `ash` for Vulkan, memory-safety on the format-
  conversion/LFB code that is historically where wrappers crash. Cost: PE-DLL
  cross-compile (`x86_64-pc-windows-gnu`), stdcall exports with a `.def`, and
  the winevulkan-loader linkage are less-trodden ground than DXVK's C++ path
  (doable — Rust produces PE DLLs fine — just less copy-pasteable prior art).

Recommendation: **Rust for the core, with the DXVK build model as the template
for the Wine PE shell.** The API surface is small enough that not inheriting
DXVK's C++ codebase costs little, and the safety wins land exactly on the
crash-prone code (LFB packing, texture format conversion, pointer-heavy Glide
vertex arrays).

---

## Phased roadmap

- **P0 — Skeleton.** `glide3x.dll` that exports the full API as stubs, loads
  under Wine, `grSstWinOpen` brings up a Vulkan swapchain, `grBufferSwap`
  presents a cleared frame. Proves the winevulkan plumbing end to end.
- **P1 — First triangle.** `grDrawTriangle` / `grDrawVertexArray` → Vulkan draw
  with a single hardcoded combine. Gouraud-shaded untextured geometry on screen.
- **P2 — Textures + combine.** `grTexSource`/`grTexDownloadMipMap`, format
  conversion, state-hash→fragment-shader cache covering the common combine
  modes. Target: a simple Glide title renders recognizably.
- **P3 — LFB + fog + alpha + depth.** The correctness pass. Menus, blends,
  chroma-key, W-buffer. Target: a real game (e.g. a GLQuake/Tomb Raider/Carmageddon
  Glide build, or a Glide test app) playable start to finish.
- **P4 — Fidelity + polish.** Optional 3dfx dither/post-filter, NCC textures,
  performance (delayed uploads / dirty tracking, à la D7VK), HUD, config, and a
  Lutris-friendly install path.

---

## Bottom line

The technical risk is low and the scope is bounded. The differentiators vs. the
Glide→OpenGL wrappers of the past are: (1) a modern Vulkan target that matches
the rest of the Linux stack, (2) no multi-hop translation, (3) a from-scratch
FOSS implementation with a clean license story — which is the entire point of
the thread that spawned this idea. The main investment is fidelity on ~5 quirks,
all of which are documented in source we are allowed to read.

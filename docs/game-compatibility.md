# Real-game compatibility survey

Survey of the Glide-capable titles in a real Lutris library (the games that had
`dgvoodoo2: true`), to ground the roadmap in what actual games require.

## Key finding: glide2x vs glide3x

GlideVK currently ships **`glide3x.dll`** only. But most classic Glide titles
link **`glide2x.dll`** (Glide 2.x, the 1996–1998 Voodoo1/2 era). Confirmed by
dumping each renderer's import table (`objdump -x ... | grep 'DLL Name: glide'`):

| Game | Renderer binary | Links |
|------|-----------------|-------|
| POD Gold | `PODX3Dfx.exe`, `donut.exe` | **glide2x** |
| Deus Ex (Unreal) | `GlideDrv.dll` | **glide2x** |
| Clive Barker's Undying (Unreal) | `GlideDrv.dll` | **glide2x** |
| **Diablo II** | `D2Glide.dll` | **glide3x** ✅ |

So a **`glide2x.dll` shell** (reusing `glidevk-core`) is the single biggest
unlock for real-game testing — it covers the Unreal-engine games, POD, and most
of the 1997–98 catalogue. Glide2 is the same pipeline with a fixed `GrVertex`
struct (no `grVertexLayout`) and a slightly different entry-point set.

The GOG builds already run these in Glide mode via **nGlide** (bundled
`glide.dll`/`glide2x.dll`/`glide3x.dll` + `nglide_config.exe`), so a GlideVK DLL
dropped into the game dir is a clean drop-in swap.

## Diablo II — the glide3x test target

`D2Glide.dll` imports **35** `glide3x` functions. GlideVK now exports all of
them (verified: `comm -23 d2_needs our_exports` is empty), so D2 can load our
DLL. The functions it needs:

```
grGlideInit grGlideShutdown grSstSelect grSstWinOpen grSstWinClose
grBufferClear grBufferSwap grFinish grGet
grColorCombine grAlphaCombine grAlphaBlendFunction grConstantColorValue
grColorMask grDepthMask grChromakeyMode grChromakeyValue grDitherMode
grVertexLayout grDrawVertexArray grDrawVertexArrayContiguous grDrawLine grDrawPoint
grTexSource grTexDownloadMipMap grTexDownloadTable grTexCombine grTexFilterMode
grTexMinAddress grTexMaxAddress
grLfbLock grLfbUnlock grLoadGammaTable guGammaCorrectionRGB
```

Added for D2 (P3a tail): `grGet` (reports a Voodoo-ish single-TMU / 16 MB / 256px
capability set), `grTexMin/MaxAddress`, a non-crashing `grLfbLock` (hands back a
real writable 16-bit staging buffer), and no-op stubs for the rest.

### What D2 still needs to render correctly
- **LFB upload** ✅ — done (P3b). `grLfbLock`/`grLfbUnlock` writes now composite
  into the presented image (verified via `examples/wine_lfb.c`).
- Real **ST scaling** (texel-space → normalized) and **Z range** — currently
  passed through; fine for the synthetic harness, not for a real game. Best
  driven by an actual D2 frame once the win32→win64 prefix migration is done.
- Read-locks and partial-region LFB (unwritten pixels currently keep the staging
  buffer's prior contents rather than the live framebuffer).

## Deus Ex — real-game render status (glide2x)

Deus Ex runs on GlideVK and renders its intro (Eidos logo, the 3D flying-logo
sequence). Getting there fixed a chain of real Glide-vs-Vulkan mismatches:

- **Decorated exports** — Glide2 games import `_grXxx@N` (stdcall-decorated);
  Glide3 (D2) imports undecorated. `glide2x.def` + `build.rs` add the decorated
  aliases.
- **Snap bias** — Unreal's GlideDrv adds `(3<<18)=786432` to vertex X/Y (the
  Glide fixed-point snapping bias); subtract it (`unsnap`).
- **Perspective `oow`** — Deus Ex routes W through the *vertex* `oow` (via
  `grHints`); the shader does `s = sow/oow` then normalizes by texture size.
  **Correction (was a real bug):** GlideDrv does not zero `tmuvtx[0].oow` — it
  leaves it **uninitialized**. That slot holds stack garbage that *varies run to
  run*: sometimes the denormal `1.345e-42` (the int `960` = screen width
  reinterpreted as f32), sometimes normal-looking junk like `5.9e3` or `-1.7e38`.
  `read_vertex` used to *prefer* the per-TMU slot (skipping only an exact `0.0`),
  so it divided `sow` by that garbage — exploding texture coordinates to ±inf and
  rendering the intro logos/menu as garbage that changed on every launch (the
  "invisible Eidos logo / badly-rendered Ion Storm & menu"). Fix: trust the
  vertex `oow` (= 1/z, the field GlideDrv actually populates); fall back to the
  per-TMU slot only if the vertex one is 0/denormal, else affine `1.0`. Verified:
  `inf`/`NaN` texture coordinates dropped 200 → 0 across an intro capture, and the
  result is now deterministic. Regression-tested in
  `uninitialized_tmu_oow_is_ignored_for_vertex_oow`.
- **ABGR color format** — `grSstWinOpen` declares `cformat=1` (ABGR); unpack
  `grConstantColorValue`/`grBufferClear`/`grChromakeyValue` accordingly (R/B
  were swapped).
- **Base-mip only** — `grTexDownloadMipMapLevel` sends one level per call; keep
  only `this_lod == large_lod` (base) to avoid over-reading sub-mip data.
- **Constant-color byte order** — Deus Ex declares an ABGR framebuffer
  (`grSstWinOpen` cformat=1) but supplies `grConstantColorValue` packed as
  `0xRRGGBBAA` (RGBA) regardless — a GlideDrv quirk real 3dfx/nGlide honour.
  Reading it as the declared ABGR swaps R↔B, rendering the green Ion Storm /
  Eidos accent sprites as dim blue. `unpack_constant_color` special-cases this
  (confirmed: the constant color ramps `0x004e2818`, whose dominant `0x4e` byte
  only lands in green under RGBA; forcing it green produced a grid of green
  accent dots, matching the intro). The framebuffer/clear/chroma paths stay
  ABGR. Note the **glow halo itself is genuinely white** — its per-vertex colors
  are a gray `R=G=B` ramp (0→255) in the game's own geometry, so a white glow is
  faithful; the green lives only in the constant-color accent sprites.
- **Logical resolution** — the game renders in its requested space (960×720);
  map geometry to that (push-constant viewport) and let the swapchain scale to
  the window, so 2D overlays center correctly. `grSstScreenWidth/Height` report
  it too.

**Focus-loss crash (alt-tab / minimize) — was a GlideVK bug, now fixed.** On
`WM_KILLFOCUS` in fullscreen, Unreal *resets* the render device with a
`grSstWinClose` → `grGlideShutdown` → `grSstWinOpen` → `grGlideShutdown` dance.
GlideVK used to destroy and recreate the entire Vulkan device on each of those,
and that thrash left Unreal's `RenDev` null so its `EndFullscreen` assert
(`WinViewport.cpp:2187`, seen in Wine as C++ exception `e06d7363`) fired. Real
Glide drivers (nGlide/dgVoodoo2) *reset* a persistent device, which is why they
never crashed. Fix: keep the Vulkan backend alive across that dance —
`grSstWinClose`/`grGlideShutdown` no longer tear it down, and `grSstWinOpen`
reuses the live backend (the swapchain re-creates itself on the next present when
the window size changed). Verified by reproducing the crash (launch on `:0`,
`xdotool windowminimize`) then confirming the game survives both minimize and
restore. Windowed mode (`examples/deusex_glidevk.sh install windowed`) remains a
belt-and-braces option but is no longer required.

**Intro logos rendered as grids of dark dots — texture tiling seams.** The flat
2D-overlay quads (logo/title bitmaps, the glow) arrive with affine texture coords
far outside [0,1] (`sow` up to ~1250 vs perspective world geometry's <~1 — a
coordinate bug in how un-divided ST is handled). Deus Ex sets `grTexClampMode` to
WRAP globally, so those out-of-range coords tile the texture and every tile seam
shows as a dark dot. Fix: a second CLAMP sampler + per-texture descriptors,
selected per-draw by `wants_clamp`, which clamps **affine** primitives (constant
`oow`/`1-w` across the corners = a flat overlay) and wraps **perspective** ones
(varying `oow` = a projected/3D surface, incl. gameplay walls that legitimately
tile). This makes the "Deus Ex" title, the emblem, and the Ion Storm glow render
as clean shapes on black, keeps gameplay tiling working, and avoids clamping the
perspective backdrop into a flat edge-texel fill. **Follow-up:** still a
workaround — the real fix is recovering correct affine ST so coords land in
[0,1] and honoring `grTexClampMode` directly.

**Eidos logo (the very first intro segment, ~first 7 s) still renders black —
unsolved.** Those frames draw fullscreen quads whose texture coordinates, after
the perspective divide, land in the tens of thousands and are heavily skewed
(non-parallel edges — a projected mapping, not a flat bitmap); they sample dark
texture data whether wrapped or clamped, so nothing visible appears. No brighter
Eidos element was found in that window. Decoding this needs a reference (how real
3dfx / nGlide renders that exact early frame) to calibrate the coordinate scheme.

**GPU crash (GPUVM/TCP fault) during the intro — texture re-upload race.** The P8
palette re-decode replaces a texture mid-frame; destroying the old `GpuTexture`
immediately faults the GPU because draws already recorded that frame still
reference it. Fixed by retiring replaced textures and destroying them only after
the frame's fence signals (`retired_textures`, drained in `present_inner`).

**Output gamma (`grGammaCorrectionValue`) — was stubbed, is the reason dim
content read as black.** Deus Ex sets `UseGammaCorrection=True` and requests
gamma **1.4**; real 3dfx applies this at the RAMDAC, lifting dim geometry into
the visible range. We now forward it to the backend and apply `pow(rgb, 1/gamma)`
in the combiner's final output (per-fragment; exact for single-layer geometry
over black, which is where it matters). This alone takes the flying Deus Ex
emblem from pure black to a visible dark-blue shape.

**Black intro logos = palettized (`GR_TEXFMT_P_8`) textures animated by palette
cycling.** Deus Ex uploads its intro-logo textures as 8-bit palettized (`fmt
0x5`) and fades them in by re-uploading the palette via `grTexDownloadTable`
**hundreds of times** (a gray ramp cycling against zero). Real 3dfx has a live
palette LUT, so the logo brightens as the palette ramps. We baked the palette at
*decode* time — and the texture is downloaded while the palette is still zero, so
it decoded to solid black and never updated. Fix: cache each P8 texture's raw
index data (`TextureCache::p8_sources`); on `grTexDownloadTable` just bump a
palette version; re-decode a P8 texture lazily when it's next bound
(`take_p8_redecode`), **capped to once per texture per frame** so the continuous
palette animation doesn't trigger a per-bind re-upload storm (an uncapped version
dropped the game to ~10 fps; the cap restores ~37 fps). The proper long-term fix
is a palette LUT in the shader (upload indices once, update only a 256-entry
palette buffer) — that would remove the re-upload cost entirely.

**Flat intro logos (Eidos mark, Deus Ex arcs, Ion Storm mark) render but are
nearly invisible without gamma.** They draw with
`grColorCombine(func=Local, local=Iterated)` (output = the iterated vertex
color) and blend `(One, OneMinusSrcColor)`. Forcing the vertex color bright
reveals the exact logo geometry, so position/combine/blend are all correct — the
emblem is simply drawn in the game's own *very dim* per-vertex colors (peak
≈`[0.04, 0.07, 0.15]`, most vertices `[0,0,0]`), which vanish on black. Same
blend as the glow; the glow is bright only because its texture is white. Since
this is the game's own Glide output. Untextured 3D geometry (the Deus Ex emblem
arcs, the pure-3D Eidos logo) is shaded the same way: its per-vertex colours are
read correctly at the standard `r@12/g@16/b@20` offsets, but the game lights them
dimly (`[0,0,0]` unlit → `~[0.03,0.06,0.11]` lit), so they're near-invisible on
black. **`GLIDEVK_GAMMA=<v>` overrides the output gamma as a brightness knob** —
e.g. `GLIDEVK_GAMMA=3.0` makes the emblem arcs and orb clearly visible (verified
against a normal-vs-boosted capture). It's the practical way to match a reference
renderer's brightness without hard-coding a value.

**Blend enum — now canonical, which made the Ion Storm logo appear.** For a
while `map_blend_factor` used a *non-canonical* table (reverse-engineered to make
Deus Ex's additive glow look right). It was wrong: Glide's `GrAlphaBlendFnc_t`
codes are fixed ABI. The real subtlety is Glide's src/dst operand rule — the
`_COLOR` codes name the *other* operand (`0x2` = DST_COLOR on the source factor
but SRC_COLOR on the dest factor; likewise `0x4`). Handling that (`map_blend_factor(v,
is_src)`) makes the canonical table correct for *both* standard Glide programs
(see `examples/wine_blend.c`, which failed under the old table) and Deus Ex.
Concretely, the Ion Storm logo — previously composited *away* by the wrong blend
and hidden inside its glow — now reads through it as "ION STORM". The controlled
`wine_blend`/`wine_depth`/`wine_texmap` harnesses are what surfaced this.

**Safe-mode dialog after a crash.** Unreal drops a 0-byte `System/Running.ini`
marker at boot and removes it only on a *clean* exit. Any crash (e.g. the
focus-loss bug above, or a level-transition re-init) leaves it behind, so the
next launch detects "we died last time" and shows the safe-mode/renderer picker
instead of booting. `examples/deusex_glidevk.sh run` now `rm -f`s it before every
launch, so the game always starts straight into the intro.

Remaining fidelity TODOs: mipmapping (the Eidos logo aliases into a glow +
dots), the flickering "Deus" letter, and the intermittent level-transition
re-init. The `GLIDEVK_CAPTURE=<file.ppm>` frame dump makes these debuggable
offline.

## Launch notes
- D2's prefix is a **win32** prefix — needs a 32-bit-capable wine
  (`wine-10.8-staging-i386` in the Lutris runners), not a WoW64-only build.
- Force Glide with `-3dfx` (Lutris's default launch does not).
- Drop `glide3x.dll` beside `Diablo II.exe` (the game dir has none; the loader
  finds it before the system32 nGlide copy) — non-destructive.

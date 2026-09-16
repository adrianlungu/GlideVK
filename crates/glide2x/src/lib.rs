//! `glide2x` — the 3dfx Glide 2.x API entry points.
//!
//! A thin shell over [`glidevk_core`], sibling to `glide3x`. The rendering core
//! (state machine, combine ubershader, textures, blend, depth, LFB) is shared;
//! this crate provides the Glide2-specific surface:
//!
//! * a **fixed `GrVertex`** struct read at hard-coded offsets (Glide2 has no
//!   `grVertexLayout`),
//! * Glide2's **inverted texture LOD** / aspect encoding,
//! * `grSstQueryHardware` board enumeration,
//! * the `gu*` high-level combine helpers most Glide2 games use.
//!
//! Targets the import sets of real games (Unreal-engine `GlideDrv.dll`, POD's
//! `PODX3Dfx.exe`). Signatures follow the Glide 2.x `glide.h` from the open SDK.
#![allow(non_snake_case, clippy::missing_safety_doc)]

use std::sync::Mutex;

use glidevk_core::types::*;
use glidevk_core::vk::{BlendConfig, CombineParams, DepthConfig, DrawVertex};
use glidevk_core::Context;

static CONTEXT: Mutex<Option<Context>> = Mutex::new(None);
/// Frame counter (bumped each `grBufferSwap`) for frame-gated debug logging.
static FRAME: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);

/// Append a diagnostic line to the log when `GLIDEVK_LOG` is set. Its value is
/// the log path; `GLIDEVK_LOG=1` uses the fixed absolute `C:\glidevk.log` (which
/// Wine maps to `$WINEPREFIX/drive_c/glidevk.log`) so the location doesn't depend
/// on the game's working directory. A no-op when unset.
fn glidevk_log(msg: &str) {
    use std::io::Write;
    use std::sync::OnceLock;
    static T0: OnceLock<std::time::Instant> = OnceLock::new();
    let path = match std::env::var("GLIDEVK_LOG") {
        Ok(v) if v == "1" || v.is_empty() => "glidevk.log".to_string(), // game's cwd
        Ok(v) => v,                                                     // explicit path
        Err(_) => return,
    };
    // Prefix with seconds since the first log line so lifecycle events
    // (re-opens, captures) can be placed against the game's own timeline.
    let t = T0
        .get_or_init(std::time::Instant::now)
        .elapsed()
        .as_secs_f32();
    if let Ok(mut f) = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)
    {
        let _ = writeln!(f, "[{t:8.3}] {msg}");
    }
}

use std::sync::atomic::{AtomicBool, Ordering};
static LOG_TS: AtomicBool = AtomicBool::new(false);

/// Log `msg` at most once (per `flag`), for tracing setup calls without spam.
fn log_once(flag: &AtomicBool, msg: &str) {
    if !flag.swap(true, Ordering::Relaxed) {
        glidevk_log(msg);
    }
}

/// Log `msg` the first time this exact `key` is seen — for enumerating all the
/// distinct render-state configurations a game uses without per-call spam.
/// `GLIDEVK_TRACE_FRAMES=<n>`: log every call to the traced entry points for
/// the first `n` frames, in order, so call sequencing (texture download vs
/// palette vs bind vs draw) can be read directly.
fn trace_on() -> bool {
    use std::sync::OnceLock;
    static LIMIT: OnceLock<u32> = OnceLock::new();
    let limit = *LIMIT.get_or_init(|| {
        std::env::var("GLIDEVK_TRACE_FRAMES")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(0)
    });
    limit > 0 && FRAME.load(Ordering::Relaxed) < limit
}

fn trace(msg: &str) {
    if trace_on() {
        glidevk_log(&format!("trace f{} {msg}", FRAME.load(Ordering::Relaxed)));
    }
}

fn log_distinct(key: u64, msg: &str) {
    use std::collections::HashSet;
    static SEEN: Mutex<Option<HashSet<u64>>> = Mutex::new(None);
    if std::env::var_os("GLIDEVK_LOG").is_none() {
        return;
    }
    if let Ok(mut g) = SEEN.lock() {
        if g.get_or_insert_with(HashSet::new).insert(key) {
            glidevk_log(msg);
        }
    }
}

/// Start address of the last texture bound via `grTexSource` — logged alongside
/// the texture-coordinate breakdown so a garbled overlay can be traced to its
/// exact TMU texture.
static LAST_TEX_ADDR: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
/// Distinct textured primitives logged so far by [`log_texcoords`], bounded by
/// `TEXLOG_MAX` so an animating overlay can't flood the log.
static TEXLOG_COUNT: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
/// Glide's texture-coordinate range on a texture's major axis (mirrors the
/// shader's `GLIDE_ST_RANGE`).
const GLIDE_ST_RANGE: f32 = 256.0;
const TEXLOG_MAX: u32 = 1500;

/// When `GLIDEVK_LOG` is set, log each *distinct* textured primitive once: the
/// raw Glide TMU0 coordinates (`sow`, `tow`), the `1/w` used for the perspective
/// divide, the bound texture size, and the resulting texel + normalized UV the
/// shader actually samples (`combine.frag::texCoord`). This is the ground-truth
/// data needed to root-cause the affine-overlay ST bug — coordinates landing far
/// outside `[0,1]` (the invisible Eidos logo, the dot-grid intro logos). A `1×1`
/// `texSize` here means `grTexSource` missed and the 1×1 white default is bound.
/// Compact one-line description of the combine/blend/const-colour state a draw
/// uses — appended to each `texdraw` line so a mis-shaded quad can be traced to
/// its exact pipeline state (e.g. is the green-dot backdrop modulating the green
/// constant colour, or is the texture itself green?).
fn shade_desc(s: &glidevk_core::state::State) -> String {
    let c = &s.color_combine;
    let a = &s.alpha_combine;
    format!(
        "colorC={:?}/{:?}/{:?}{} alphaC={:?}/{:?}/{:?} blend={:?}->{:?} const=({:.2},{:.2},{:.2},{:.2}) chroma={} atf={}",
        c.function, c.local, c.other,
        if c.invert { "!" } else { "" },
        a.function, a.local, a.other,
        s.blend_src_rgb, s.blend_dst_rgb,
        s.constant_color[0], s.constant_color[1], s.constant_color[2], s.constant_color[3],
        s.chromakey_enabled as u8, s.alpha_test_func
    )
}

fn log_texcoords(tris: &[DrawVertex], tex_size: [f32; 2], shade: &str) {
    use std::collections::HashSet;
    use std::hash::{Hash, Hasher};
    static SEEN: Mutex<Option<HashSet<u64>>> = Mutex::new(None);
    if std::env::var_os("GLIDEVK_LOG").is_none() || tris.len() < 3 {
        return;
    }
    if TEXLOG_COUNT.load(Ordering::Relaxed) >= TEXLOG_MAX {
        return;
    }
    let addr = LAST_TEX_ADDR.load(Ordering::Relaxed);
    // Stable key = bound texture + quantized coords + a coarse frame bucket, so a
    // repeated overlay logs once per ~2s window. The bucket lets us sample across
    // the whole session (intro AND menu) instead of exhausting the cap on the
    // first animated seconds.
    let mut h = std::collections::hash_map::DefaultHasher::new();
    (FRAME.load(Ordering::Relaxed) / 60).hash(&mut h);
    addr.hash(&mut h);
    tex_size[0].to_bits().hash(&mut h);
    tex_size[1].to_bits().hash(&mut h);
    for v in &tris[..3] {
        ((v.tex[0] * 16.0) as i64).hash(&mut h);
        ((v.tex[1] * 16.0) as i64).hash(&mut h);
        ((v.oow * 4096.0) as i64).hash(&mut h);
    }
    let key = h.finish();
    let fresh = match SEEN.lock() {
        Ok(mut g) => g.get_or_insert_with(HashSet::new).insert(key),
        Err(_) => return, // lock poisoned
    };
    if !fresh {
        return; // already logged this primitive
    }
    TEXLOG_COUNT.fetch_add(1, Ordering::Relaxed);

    let frame = FRAME.load(Ordering::Relaxed);
    let [tw, th] = tex_size;
    let mut msg = format!(
        "texdraw frame={frame} addr={addr:#x} texSize={tw}x{th} nverts={} {shade}",
        tris.len()
    );
    // Show the primitive's first three vertices, reproducing the shader's
    // mapping: st = sow/oow in Glide's [0,256) space (major axis), then
    // uv = st * major / (256 * texSize).
    for (i, v) in tris.iter().take(3).enumerate() {
        let w = if v.oow != 0.0 { v.oow } else { 1.0 };
        let (s, t) = (v.tex[0] / w, v.tex[1] / w);
        let major = tw.max(th).max(1.0);
        let (u, vcoord) = (
            s * major / (GLIDE_ST_RANGE * tw.max(1.0)),
            t * major / (GLIDE_ST_RANGE * th.max(1.0)),
        );
        msg.push_str(&format!(
            "\n  v{i} pos=({:.1},{:.1}) sow={:.3} tow={:.3} oow={:.6e} -> texel=({:.2},{:.2}) uv=({:.4},{:.4})",
            v.pos[0], v.pos[1], v.tex[0], v.tex[1], v.oow, s, t, u, vcoord
        ));
    }
    glidevk_log(&msg);
}

static RAWLOG_COUNT: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
const RAWLOG_MAX: u32 = 400;

/// Dump the *raw* `GrVertex` fields (before `read_vertex` normalizes anything)
/// for the first vertex of a primitive, deduped + capped. This is the ground
/// truth for the ST-coordinate investigation: it shows every candidate `1/w`
/// slot — the vertex `oow` (offset 32) vs each TMU's `oow` (offset 44/56) — so we
/// can tell which one carries a valid perspective W for the 2D overlays, plus
/// `ooz`, colour, and both TMU coordinate pairs.
unsafe fn log_raw_vertex(base: *const u8, nverts: i32) {
    use std::collections::HashSet;
    use std::hash::{Hash, Hasher};
    static SEEN: Mutex<Option<HashSet<u64>>> = Mutex::new(None);
    if std::env::var_os("GLIDEVK_LOG").is_none() {
        return;
    }
    if RAWLOG_COUNT.load(Ordering::Relaxed) >= RAWLOG_MAX {
        return;
    }
    let f = |off: usize| f32_at(base, off);
    let (x, y) = (f(V_X), f(V_Y));
    let addr = LAST_TEX_ADDR.load(Ordering::Relaxed);
    // Cap per bound texture so one busy texture cannot exhaust the budget
    // before the next one (e.g. a mesh skin) ever shows up.
    static PER_ADDR: Mutex<Option<std::collections::HashMap<u32, u32>>> = Mutex::new(None);
    const PER_ADDR_MAX: u32 = 4;
    match PER_ADDR.lock() {
        Ok(mut g) => {
            let n = g
                .get_or_insert_with(std::collections::HashMap::new)
                .entry(addr)
                .or_insert(0);
            if *n >= PER_ADDR_MAX {
                return;
            }
            *n += 1;
        }
        Err(_) => return,
    }
    let mut h = std::collections::hash_map::DefaultHasher::new();
    addr.hash(&mut h);
    ((x * 16.0) as i64).hash(&mut h);
    ((y * 16.0) as i64).hash(&mut h);
    ((f(V_SOW0) * 64.0) as i64).hash(&mut h);
    ((f(V_TOW0) * 64.0) as i64).hash(&mut h);
    let key = h.finish();
    let fresh = match SEEN.lock() {
        Ok(mut g) => g.get_or_insert_with(HashSet::new).insert(key),
        Err(_) => return,
    };
    if !fresh {
        return;
    }
    RAWLOG_COUNT.fetch_add(1, Ordering::Relaxed);
    let frame = FRAME.load(Ordering::Relaxed);
    // Layout: x y z r g b ooz a oow | tmu0 sow tow oow | tmu1 sow tow oow
    glidevk_log(&format!(
        "rawvert frame={frame} addr={addr:#x} nverts={nverts} \
         x={x:.2} y={y:.2} z={:.4} rgb=({:.1},{:.1},{:.1}) ooz={:.4} a={:.1} oow={:.6e} | \
         tmu0 sow={:.4} tow={:.4} oow={:.6e} | tmu1 sow={:.4} tow={:.4} oow={:.6e}",
        f(8), // z
        f(V_R),
        f(V_G),
        f(V_B),
        f(V_OOZ),
        f(V_A),
        f(V_OOW),
        f(V_SOW0),
        f(V_TOW0),
        f(V_TMU_OOW0),
        f(48),
        f(52),
        f(56), // tmu1 sow/tow/oow
    ));
}

/// `GrColorFormat_t` from `grSstWinOpen` (0=ARGB 1=ABGR 2=RGBA 3=BGRA). Governs
/// how `GrColor_t` words (constant/clear/chroma) are byte-ordered.
static COLOR_FORMAT: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(0);

/// Unpack a `GrColor_t` to normalized RGBA per the active color format.
fn unpack_color(c: u32) -> [f32; 4] {
    let byte = |shift: u32| ((c >> shift) & 0xff) as f32 / 255.0;
    match COLOR_FORMAT.load(Ordering::Relaxed) {
        1 => [byte(0), byte(8), byte(16), byte(24)], // ABGR: 0xAABBGGRR
        2 => [byte(24), byte(16), byte(8), byte(0)], // RGBA: 0xRRGGBBAA
        3 => [byte(8), byte(16), byte(24), byte(0)], // BGRA: 0xBBGGRRAA
        _ => [byte(16), byte(8), byte(0), byte(24)], // ARGB: 0xAARRGGBB
    }
}

/// Unpack the argument of `grConstantColorValue`. Deus Ex's GlideDrv declares an
/// ABGR framebuffer (`grSstWinOpen` cformat=1) but supplies the *constant color*
/// packed as `0xRRGGBBAA` (RGBA) regardless — a well-known GlideDrv quirk that
/// real 3dfx hardware / nGlide honour. Interpreting it with the declared ABGR
/// order swaps R↔B and renders the Ion Storm / Eidos accent sprites dim blue
/// instead of their intended green. Confirmed empirically: the constant color
/// ramps `0x004e2818`, whose dominant byte (`0x4e`) sits in the green slot only
/// under RGBA. When the game declares RGBA/BGRA already, defer to `unpack_color`.
fn unpack_constant_color(c: u32) -> [f32; 4] {
    let byte = |shift: u32| ((c >> shift) & 0xff) as f32 / 255.0;
    match COLOR_FORMAT.load(Ordering::Relaxed) {
        1 => [byte(24), byte(16), byte(8), byte(0)], // ABGR-declared → RGBA-packed const
        _ => unpack_color(c),
    }
}

fn with_ctx<R>(f: impl FnOnce(&mut Context) -> R) -> Option<R> {
    let mut guard = CONTEXT.lock().ok()?;
    guard.as_mut().map(f)
}

// ---------------------------------------------------------------------------
// Fixed GrVertex layout (Glide 2.x, GLIDE_NUM_TMU = 2)
// ---------------------------------------------------------------------------
// struct GrVertex { float x,y,z, r,g,b, ooz, a, oow; GrTmuVertex tmuvtx[2]; }
// where GrTmuVertex { float sow, tow, oow; }.
const V_X: usize = 0;
const V_Y: usize = 4;
const V_R: usize = 12;
const V_G: usize = 16;
const V_B: usize = 20;
const V_OOZ: usize = 24;
const V_A: usize = 28;
const V_OOW: usize = 32; // vertex 1/w
const V_SOW0: usize = 36;
const V_TOW0: usize = 40;
const V_TMU_OOW0: usize = 44; // tmuvtx[0].oow (0 when the W hint routes to vertex oow)
/// sizeof(GrVertex) with GLIDE_NUM_TMU = 2 (36 + 2*12). Stride for vertex lists.
const GRVERTEX_SIZE: usize = 60;

#[inline]
unsafe fn f32_at(base: *const u8, off: usize) -> f32 {
    (base.add(off) as *const f32).read_unaligned()
}

/// Glide's sub-pixel snapping bias, `(3 << 18)` = 786432.0. Many Glide drivers
/// (incl. Unreal's GlideDrv) add this to vertex X/Y so the float mantissa aligns
/// for the hardware's fixed-point conversion; the real screen coordinate is the
/// value minus the bias. Coordinates below the threshold are unbiased (raw).
const SNAP_BIAS: f32 = (3 << 18) as f32;

#[inline]
fn unsnap(v: f32) -> f32 {
    if v >= (1 << 18) as f32 {
        v - SNAP_BIAS
    } else {
        v
    }
}

/// Read a fixed `GrVertex` into the backend's normalized form. Colors are Glide
/// floats in [0, 255]; ST come from TMU0's (s/w, t/w). Depth is derived from
/// `ooz` (65535/w): larger ooz = nearer, mapped to a smaller [0,1] depth.
unsafe fn read_vertex(base: *const u8) -> DrawVertex {
    let ooz = f32_at(base, V_OOZ);
    DrawVertex {
        pos: [unsnap(f32_at(base, V_X)), unsnap(f32_at(base, V_Y))],
        color: [
            f32_at(base, V_R) / 255.0,
            f32_at(base, V_G) / 255.0,
            f32_at(base, V_B) / 255.0,
            f32_at(base, V_A) / 255.0,
        ],
        // sow/tow (= s/w, t/w) + oow (= 1/w); the shader recovers + normalizes.
        tex: [f32_at(base, V_SOW0), f32_at(base, V_TOW0)],
        // TODO: proper Glide2 Z/W-buffer range mapping.
        depth: (1.0 - ooz / 65535.0).clamp(0.0, 1.0),
        // Perspective W (1/w) for the texture divide. In single-texture Glide the
        // vertex `oow` and `tmuvtx[0].oow` are the *same* perspective 1/w — but
        // Deus Ex's GlideDrv leaves `tmuvtx[0].oow` **uninitialized** for its draws.
        // That slot holds whatever was on the stack: sometimes the denormal
        // `1.345e-42` (raw int 960 = screen width), sometimes normal-looking junk
        // like `5.9e3` or `-1.7e38` — and it *varies run to run*. The old code
        // preferred that slot (only skipping an exact `0.0`), so it divided `sow`
        // by random memory, exploding texture coordinates to ±inf and making the
        // intro/menu render as garbage that changed on every launch.
        //
        // The vertex `oow` (offset 32, = 1/z) is the field GlideDrv actually
        // populates, so trust it. Fall back to the per-TMU slot only if the vertex
        // one is unusable (0/denormal), and to `1.0` (affine, no divide) if neither
        // is a normal float. Since the two are equal whenever a game sets both,
        // this can't regress correctly-written Glide programs.
        oow: {
            let vertex = f32_at(base, V_OOW);
            let tmu = f32_at(base, V_TMU_OOW0);
            if vertex.is_normal() {
                vertex
            } else if tmu.is_normal() {
                tmu
            } else {
                1.0
            }
        },
    }
}

/// Bind the texture selected by `grTexSource` for a draw. Palettised textures
/// are decoded against the palette live *now* (Glide's TMU applies its LUT at
/// draw time), cached per palette content so alternating palettes don't churn.
fn bind_draw_texture(
    textures: &mut glidevk_core::texture::TextureCache,
    backend: &mut glidevk_core::vk::Backend,
) {
    let addr = LAST_TEX_ADDR.load(Ordering::Relaxed);
    let Some(resolved) = textures.p8_draw(addr) else {
        return; // plain texture: bound at grTexSource
    };
    for key in resolved.release {
        backend.release_texture(key);
    }
    if let Some(tex) = resolved.upload {
        let n = (tex.rgba.len() / 4).max(1) as u64;
        let avg = |c: usize| tex.rgba.chunks_exact(4).map(|p| p[c] as u64).sum::<u64>() / n;
        log_distinct(
            8 << 40 | (addr as u64) << 8 | avg(0),
            &format!(
                "p8 variant frame={} addr={addr:#x} palette={} {}x{} avgRGB=({},{},{})",
                FRAME.load(Ordering::Relaxed),
                textures.palette_id(),
                tex.width,
                tex.height,
                avg(0),
                avg(1),
                avg(2)
            ),
        );
        if let Ok(dir) = std::env::var("GLIDEVK_TEXDUMP") {
            write_ppm(
                &format!(
                    "{dir}/p8_{addr:#07x}_pal{}_{}x{}.ppm",
                    textures.palette_id(),
                    tex.width,
                    tex.height
                ),
                tex.width,
                tex.height,
                &tex.rgba,
            );
        }
        let _ = backend.upload_texture(resolved.key, tex.width, tex.height, &tex.rgba);
    }
    backend.set_texture(resolved.key);
}

fn submit_fan(verts: &[DrawVertex]) {
    if verts.len() < 3 {
        return;
    }
    if trace_on() {
        let v = &verts[0];
        trace(&format!(
            "draw n={} tex={:#x} v0=({:.0},{:.0}) rgb=({:.0},{:.0},{:.0})",
            verts.len(),
            LAST_TEX_ADDR.load(Ordering::Relaxed),
            v.pos[0],
            v.pos[1],
            v.color[0] * 255.0,
            v.color[1] * 255.0,
            v.color[2] * 255.0
        ));
    }
    let mut tris = Vec::with_capacity((verts.len() - 2) * 3);
    for i in 1..verts.len() - 1 {
        tris.extend_from_slice(&[verts[0], verts[i], verts[i + 1]]);
    }
    let mut tex_size = [1.0, 1.0];
    let mut shade = String::new();
    with_ctx(|ctx| {
        let combine = CombineParams::from_state(&ctx.state);
        let blend = BlendConfig::from_state(&ctx.state);
        let depth = DepthConfig::from_state(&ctx.state);
        let chroma_enable = ctx.state.chromakey_enabled;
        let chroma = [
            ctx.state.chroma_key[0],
            ctx.state.chroma_key[1],
            ctx.state.chroma_key[2],
        ];
        let (atf, atr) = (ctx.state.alpha_test_func, ctx.state.alpha_test_ref);
        if std::env::var_os("GLIDEVK_LOG").is_some() {
            shade = shade_desc(&ctx.state);
        }
        if let Some(b) = ctx.backend.as_mut() {
            bind_draw_texture(&mut ctx.textures, b);
            b.set_combine(combine);
            b.set_blend(blend);
            b.set_depth(depth);
            b.set_chroma(chroma_enable, chroma);
            b.set_alpha_test(atf, atr);
            b.set_clamp(ctx.state.tex_clamp);
            let _ = b.draw_triangles(&tris);
            tex_size = b.current_tex_size();
        }
    });
    // Diagnostics only (GLIDEVK_LOG); file I/O deliberately outside the lock.
    log_texcoords(&tris, tex_size, &shade);
}

// ---------------------------------------------------------------------------
// Lifecycle + hardware query
// ---------------------------------------------------------------------------

#[export_name = "grGlideInit"]
pub extern "system" fn grGlideInit() {}

#[export_name = "grGlideShutdown"]
pub extern "system" fn grGlideShutdown() {
    glidevk_log("grGlideShutdown");
    // Keep the Vulkan backend alive: Unreal calls this mid-way through its
    // focus-loss reset dance and immediately re-opens. Destroying the device
    // here (and again on re-open) is what crashed the game on alt-tab. The
    // device is reclaimed on process exit. See grSstWinOpen's reuse path.
}

#[export_name = "grGlideGetVersion"]
pub unsafe extern "system" fn grGlideGetVersion(version: *mut u8) {
    if !version.is_null() {
        let s = b"Glide2x\0";
        std::ptr::copy_nonoverlapping(s.as_ptr(), version, s.len());
    }
}

/// Fill a caller-owned `GrHwConfiguration` describing one Voodoo board with a
/// single 4 MB TMU. We write only the leading fields we're sure of (by offset),
/// never assuming the struct's full size, so we can't overflow the caller.
unsafe fn fill_hwconfig(config: *mut i32) {
    if config.is_null() {
        return;
    }
    // num_sst=1; SSTs[0].type=GR_SSTTYPE_VOODOO(0); then GrVoodooConfig:
    // fbRam, fbiRev, nTexelfx, sliDetect, tmuConfig[2]{tmuRev,tmuRam}.
    let fields = [1, 0, 4, 2, 1, 0, 1, 4, 1, 4];
    std::ptr::copy_nonoverlapping(fields.as_ptr(), config, fields.len());
}

#[export_name = "grSstQueryHardware"]
pub unsafe extern "system" fn grSstQueryHardware(config: *mut i32) -> FxBool {
    fill_hwconfig(config);
    1
}

#[export_name = "grSstQueryBoards"]
pub unsafe extern "system" fn grSstQueryBoards(config: *mut i32) -> FxBool {
    if !config.is_null() {
        *config = 1; // num_sst
    }
    1
}

#[export_name = "grSstSelect"]
pub extern "system" fn grSstSelect(_which: i32) {}

/// `grSstWinOpen(hwnd, res, refresh, cformat, origin, nColBuffers, nAuxBuffers)`
/// -> FxBool. Binds a winevulkan swapchain to `hwnd` on the Windows/Wine build.
#[export_name = "grSstWinOpen"]
pub extern "system" fn grSstWinOpen(
    hwnd: usize,
    screen_resolution: u32,
    _refresh_rate: i32,
    _color_format: i32,
    origin_location: i32,
    _num_color_buffers: i32,
    _num_aux_buffers: i32,
) -> FxBool {
    COLOR_FORMAT.store(_color_format, Ordering::Relaxed);
    let mut guard = match CONTEXT.lock() {
        Ok(g) => g,
        Err(_) => return 0,
    };
    // Unreal re-opens the render device on every focus loss / mode change
    // (grSstWinClose -> grGlideShutdown -> grSstWinOpen). Tearing down and
    // recreating the whole Vulkan device on that dance is what crashes the game
    // on alt-tab (real Glide drivers reset a persistent device). If a backend is
    // already live for this process, reuse it — its swapchain re-creates itself
    // on the next present when the window size changed.
    if let Some(existing) = guard.as_mut() {
        if let Some(b) = existing.backend.as_mut() {
            let (w, h) = resolution_to_size(screen_resolution);
            b.set_origin_upper_left(origin_location == 0);
            // The re-open may ask for a different mode (Unreal re-inits at the
            // windowed size after a focus loss); the coordinate space and what
            // grSstScreenWidth/Height report must follow it.
            b.set_logical_size(w, h);
            glidevk_log(&format!(
                "grSstWinOpen: reusing persistent backend (re-open) hwnd={hwnd:#x} res={screen_resolution} -> {w}x{h}"
            ));
            return 1;
        }
    }
    #[allow(unused_mut)]
    let mut ctx = Context::new();

    #[cfg(windows)]
    {
        let (w, h) = resolution_to_size(screen_resolution);
        // SAFETY: GetModuleHandleW(NULL) returns this process's base HINSTANCE.
        let hinstance = unsafe { GetModuleHandleW(core::ptr::null()) };
        glidevk_log(&format!(
            "grSstWinOpen: hwnd={hwnd:#x} res={screen_resolution} cformat={_color_format} -> {w}x{h} (0=ARGB 1=ABGR 2=RGBA 3=BGRA)"
        ));
        // The window may not be fully mapped the instant Glide is initialized;
        // retry briefly so a transient surface/swapchain failure doesn't abort
        // the whole render-device init ("3d hardware initialization failed").
        let mut attempt = 0;
        let result = loop {
            match glidevk_core::vk::Backend::new_windowed(hwnd as isize, hinstance, w, h) {
                Ok(b) => break Ok(b),
                Err(e) if attempt < 10 => {
                    glidevk_log(&format!(
                        "grSstWinOpen: attempt {attempt} failed ({e:?}), retrying"
                    ));
                    attempt += 1;
                    std::thread::sleep(std::time::Duration::from_millis(50));
                }
                Err(e) => break Err(e),
            }
        };
        match result {
            Ok(mut backend) => {
                glidevk_log(&format!(
                    "grSstWinOpen: backend OK extent={:?} ({})",
                    backend.extent(),
                    backend.device_name()
                ));
                backend.set_origin_upper_left(origin_location == 0);
                // GLIDEVK_CAPTURE=<path> dumps a rendered frame to a PPM for
                // offline verification. Frame index via GLIDEVK_CAPTURE_FRAME.
                if std::env::var_os("GLIDEVK_CAPTURE").is_some() {
                    let frame = std::env::var("GLIDEVK_CAPTURE_FRAME")
                        .ok()
                        .and_then(|s| s.parse().ok())
                        .unwrap_or(400);
                    backend.set_capture_frame(frame);
                }
                ctx.backend = Some(backend);
            }
            Err(e) => {
                glidevk_log(&format!("grSstWinOpen: new_windowed FAILED: {e:?}"));
                return 0;
            }
        }
    }
    #[cfg(not(windows))]
    let _ = (hwnd, screen_resolution, origin_location);

    *guard = Some(ctx);
    1
}

#[cfg(windows)]
extern "system" {
    fn GetModuleHandleW(module_name: *const u16) -> isize;
}

#[export_name = "grSstWinClose"]
pub extern "system" fn grSstWinClose() {
    glidevk_log("grSstWinClose");
    // Keep the backend alive across Unreal's close/re-open focus-loss dance
    // (see grSstWinOpen / grGlideShutdown). Full teardown here crashed alt-tab.
}

#[export_name = "grSstControl"]
pub extern "system" fn grSstControl(_code: i32) -> FxBool {
    1
}

#[export_name = "grSstScreenWidth"]
pub extern "system" fn grSstScreenWidth() -> u32 {
    with_ctx(|ctx| ctx.backend.as_ref().map(|b| b.logical_size().0))
        .flatten()
        .unwrap_or(640)
}

#[export_name = "grSstScreenHeight"]
pub extern "system" fn grSstScreenHeight() -> u32 {
    with_ctx(|ctx| ctx.backend.as_ref().map(|b| b.logical_size().1))
        .flatten()
        .unwrap_or(480)
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

#[export_name = "grBufferClear"]
pub extern "system" fn grBufferClear(color: u32, _alpha: u8, _depth: u32) {
    trace(&format!("grBufferClear {color:#010x}"));
    let c = unpack_color(color);
    with_ctx(|ctx| {
        ctx.clear_color = c;
        if let Some(b) = ctx.backend.as_mut() {
            b.set_clear_color(c);
        }
    });
}

#[export_name = "grBufferSwap"]
pub extern "system" fn grBufferSwap(_swap_interval: i32) {
    trace("grBufferSwap");
    FRAME.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    let capture = with_ctx(|ctx| {
        if let Some(b) = ctx.backend.as_mut() {
            if b.present().is_err() {
                ctx.present_errors += 1;
            }
            b.take_capture()
        } else {
            None
        }
    })
    .flatten();
    // Write the captured frame outside the context lock (file I/O). For interval
    // capture the frame index is appended so a whole intro can be scanned.
    if let Some((frame, w, h, rgba)) = capture {
        if let Ok(base) = std::env::var("GLIDEVK_CAPTURE") {
            let interval = std::env::var("GLIDEVK_CAPTURE_FRAME")
                .ok()
                .and_then(|s| s.parse::<i32>().ok())
                .unwrap_or(0);
            let path = if interval < 0 {
                base.replace(".ppm", &format!("_{frame:05}.ppm"))
            } else {
                base
            };
            write_ppm(&path, w, h, &rgba);
            glidevk_log(&format!("captured frame {frame} {w}x{h} -> {path}"));
        }
    }
}

/// Write an RGBA8 buffer as a binary PPM (P6, RGB) — trivial + viewable.
fn write_ppm(path: &str, w: u32, h: u32, rgba: &[u8]) {
    use std::io::Write;
    if let Ok(mut f) = std::fs::File::create(path) {
        let _ = write!(f, "P6\n{w} {h}\n255\n");
        let mut rgb = Vec::with_capacity((w * h * 3) as usize);
        for px in rgba.chunks_exact(4) {
            rgb.extend_from_slice(&px[..3]);
        }
        let _ = f.write_all(&rgb);
    }
}

#[export_name = "grBufferNumPending"]
pub extern "system" fn grBufferNumPending() -> i32 {
    0
}

// ---------------------------------------------------------------------------
// Combine / blend / test state
// ---------------------------------------------------------------------------

#[export_name = "grColorCombine"]
pub extern "system" fn grColorCombine(func: i32, factor: i32, local: i32, other: i32, inv: FxBool) {
    log_distinct(
        1 << 40 | (func as u64) << 24 | (factor as u64) << 16 | (local as u64) << 8 | other as u64,
        &format!(
            "grColorCombine func={func} factor={factor} local={local} other={other} inv={inv}"
        ),
    );
    with_ctx(|ctx| {
        let c = &mut ctx.state.color_combine;
        c.function = map_combine_function(func);
        c.factor = map_combine_factor(factor);
        c.local = map_combine_local(local);
        c.other = map_combine_other(other);
        c.invert = inv != 0;
    });
}

#[export_name = "grAlphaCombine"]
pub extern "system" fn grAlphaCombine(func: i32, factor: i32, local: i32, other: i32, inv: FxBool) {
    log_distinct(
        2 << 40 | (func as u64) << 24 | (factor as u64) << 16 | (local as u64) << 8 | other as u64,
        &format!("grAlphaCombine func={func} factor={factor} local={local} other={other}"),
    );
    with_ctx(|ctx| {
        let a = &mut ctx.state.alpha_combine;
        a.function = map_combine_function(func);
        a.factor = map_combine_factor(factor);
        a.local = map_combine_local(local);
        a.other = map_combine_other(other);
        a.invert = inv != 0;
    });
}

#[export_name = "grAlphaBlendFunction"]
pub extern "system" fn grAlphaBlendFunction(rs: i32, rd: i32, as_: i32, ad: i32) {
    log_distinct(
        3 << 40 | (rs as u64) << 8 | rd as u64,
        &format!("grAlphaBlendFunction rgb src={rs} dst={rd}"),
    );
    with_ctx(|ctx| {
        ctx.state.blend_src_rgb = BlendFactor::from_glide(rs, true);
        ctx.state.blend_dst_rgb = BlendFactor::from_glide(rd, false);
        ctx.state.blend_src_alpha = BlendFactor::from_glide(as_, true);
        ctx.state.blend_dst_alpha = BlendFactor::from_glide(ad, false);
    });
}

#[export_name = "grConstantColorValue"]
pub extern "system" fn grConstantColorValue(color: u32) {
    log_distinct(
        4 << 40 | color as u64,
        &format!("grConstantColorValue raw={color:#010x}"),
    );
    let c = unpack_constant_color(color);
    with_ctx(|ctx| ctx.state.constant_color = c);
}

#[export_name = "grDepthBufferMode"]
pub extern "system" fn grDepthBufferMode(mode: i32) {
    with_ctx(|ctx| {
        ctx.state.depth_mode = match mode {
            0 => glidevk_core::state::DepthMode::Disable,
            1 => glidevk_core::state::DepthMode::ZBuffer,
            2 => glidevk_core::state::DepthMode::WBuffer,
            _ => glidevk_core::state::DepthMode::Disable,
        };
    });
}

#[export_name = "grDepthBufferFunction"]
pub extern "system" fn grDepthBufferFunction(function: i32) {
    with_ctx(|ctx| ctx.state.depth_compare = function);
}

#[export_name = "grDepthMask"]
pub extern "system" fn grDepthMask(enable: FxBool) {
    with_ctx(|ctx| ctx.state.depth_write = enable != 0);
}

#[export_name = "grChromakeyMode"]
pub extern "system" fn grChromakeyMode(mode: i32) {
    with_ctx(|ctx| ctx.state.chromakey_enabled = mode != 0);
}

#[export_name = "grChromakeyValue"]
pub extern "system" fn grChromakeyValue(value: u32) {
    let c = unpack_color(value);
    with_ctx(|ctx| ctx.state.chroma_key = c);
}

#[export_name = "grAlphaTestFunction"]
pub extern "system" fn grAlphaTestFunction(function: i32) {
    with_ctx(|ctx| ctx.state.alpha_test_func = function);
}

#[export_name = "grAlphaTestReferenceValue"]
pub extern "system" fn grAlphaTestReferenceValue(value: u8) {
    with_ctx(|ctx| ctx.state.alpha_test_ref = value as f32 / 255.0);
}

#[export_name = "grFogMode"]
pub extern "system" fn grFogMode(mode: i32) {
    with_ctx(|ctx| ctx.state.fog_enabled = mode != 0);
}

// ---------------------------------------------------------------------------
// gu* high-level combine helpers (used by real Glide2 games)
// ---------------------------------------------------------------------------

/// `guColorCombineFunction(GrColorCombineFnc_t)` — map the high-level function
/// to the low-level color combine state.
#[export_name = "guColorCombineFunction"]
pub extern "system" fn guColorCombineFunction(func: i32) {
    log_distinct(
        5 << 40 | func as u64,
        &format!("guColorCombineFunction func={func}"),
    );
    use glidevk_core::types::{CombineFunction as F, CombineLocal as L, CombineOther as O};
    with_ctx(|ctx| {
        let c = &mut ctx.state.color_combine;
        c.invert = false;
        c.factor = CombineFactor::One;
        match func {
            2 => {
                // ITRGB
                c.function = F::Local;
                c.local = L::Iterated;
                c.other = O::Iterated;
            }
            4 => {
                // DECAL_TEXTURE
                c.function = F::ScaleOther;
                c.other = O::Texture;
            }
            6 => {
                // TEXTURE_TIMES_ITRGB (modulate)
                c.function = F::ScaleOther;
                c.factor = CombineFactor::Local;
                c.local = L::Iterated;
                c.other = O::Texture;
            }
            1 => {
                // CCRGB (constant)
                c.function = F::Local;
                c.local = L::Constant;
            }
            _ => {
                // Sensible default: modulate.
                c.function = F::ScaleOther;
                c.factor = CombineFactor::Local;
                c.local = L::Iterated;
                c.other = O::Texture;
            }
        }
    });
}

/// `guAlphaSource(GrAlphaSource_t)` — configure the alpha combine source.
#[export_name = "guAlphaSource"]
pub extern "system" fn guAlphaSource(src: i32) {
    log_distinct(6 << 40 | src as u64, &format!("guAlphaSource src={src}"));
    use glidevk_core::types::{CombineFunction as F, CombineLocal as L, CombineOther as O};
    with_ctx(|ctx| {
        let a = &mut ctx.state.alpha_combine;
        a.invert = false;
        a.factor = CombineFactor::One;
        match src {
            0 => {
                a.function = F::Local;
                a.local = L::Constant;
            } // CC_ALPHA
            1 => {
                a.function = F::Local;
                a.local = L::Iterated;
            } // ITERATED_ALPHA
            2 => {
                a.function = F::ScaleOther;
                a.other = O::Texture;
            } // TEXTURE_ALPHA
            _ => {
                a.function = F::ScaleOther;
                a.factor = CombineFactor::Local;
                a.local = L::Iterated;
                a.other = O::Texture;
            } // TEXTURE*ITER
        }
    });
}

/// `guFogTableIndexToW(int i)` -> float. Fog-table build helper; approximate.
#[export_name = "guFogTableIndexToW"]
pub extern "system" fn guFogTableIndexToW(i: i32) -> f32 {
    // Glide's canonical mapping is pow(2, i/4) / 8; adequate for table setup.
    let e = (i.max(0) as f32) / 4.0;
    2.0_f32.powf(e) / 8.0
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

/// Glide2 `GrTexInfo` — note LOD/aspect differ from Glide3 (see `tex_dims`).
#[repr(C)]
pub struct GrTexInfo {
    pub small_lod: u32,
    pub large_lod: u32,
    pub aspect_ratio: u32,
    pub format: u32,
    pub data: *const core::ffi::c_void,
}

fn map_texfmt(v: u32) -> Option<TexFormat> {
    match v {
        0x02 => Some(TexFormat::Alpha8),
        0x03 => Some(TexFormat::Intensity8),
        0x05 => Some(TexFormat::P8),
        0x0a => Some(TexFormat::Rgb565),
        0x0b => Some(TexFormat::Argb1555),
        0x0c => Some(TexFormat::Argb4444),
        _ => None,
    }
}

fn texfmt_bpp(f: TexFormat) -> usize {
    match f {
        TexFormat::Rgb565
        | TexFormat::Argb1555
        | TexFormat::Argb4444
        | TexFormat::Argb8332
        | TexFormat::AlphaIntensity88 => 2,
        _ => 1,
    }
}

/// Glide2 texture dimensions. Unlike Glide3, `large_lod` counts DOWN from 256
/// (`GR_LOD_256`=0 … `GR_LOD_1`=8) and aspect runs `8x1`=0 … `1x1`=3 … `1x8`=6.
fn tex_dims(info: &GrTexInfo) -> (u32, u32) {
    let dim = (256u32 >> info.large_lod.min(8)).max(1);
    let aspect = info.aspect_ratio.min(6) as i32;
    if aspect <= 3 {
        (dim, (dim >> (3 - aspect)).max(1)) // square or wide
    } else {
        ((dim >> (aspect - 3)).max(1), dim) // tall
    }
}

#[export_name = "grTexDownloadMipMap"]
pub unsafe extern "system" fn grTexDownloadMipMap(
    _tmu: u32,
    start_address: u32,
    _even_odd: u32,
    info: *const GrTexInfo,
) {
    download_texture(start_address, info);
}

#[export_name = "grTexDownloadMipMapLevel"]
pub unsafe extern "system" fn grTexDownloadMipMapLevel(
    _tmu: u32,
    start_address: u32,
    this_lod: u32,
    large_lod: u32,
    aspect: u32,
    format: u32,
    _even_odd: u32,
    data: *const core::ffi::c_void,
) {
    // We don't mipmap; keep only the base (largest) level. In Glide2 LOD counts
    // DOWN from 256, so the base level is `this_lod == large_lod`. Uploading a
    // smaller level with base-level dimensions would over-read `data`.
    if this_lod != large_lod {
        return;
    }
    let info = GrTexInfo {
        small_lod: large_lod,
        large_lod,
        aspect_ratio: aspect,
        format,
        data,
    };
    download_texture(start_address, &info);
}

#[export_name = "grTexDownloadMipMapLevelPartial"]
pub unsafe extern "system" fn grTexDownloadMipMapLevelPartial(
    tmu: u32,
    start_address: u32,
    lod: u32,
    large_lod: u32,
    aspect: u32,
    format: u32,
    even_odd: u32,
    data: *const core::ffi::c_void,
    start_row: i32,
    end_row: i32,
) {
    // Unreal uploads lightmaps this way with rows [0, VClamp-1] only. We still
    // decode the full level (the source buffer is level-sized); log the range so
    // a partial-row mismatch can be recognised.
    log_distinct(
        7 << 40 | (large_lod as u64) << 32 | (aspect as u64) << 24 | (start_row as u64) << 12 | (end_row as u64 & 0xfff),
        &format!(
            "grTexDownloadMipMapLevelPartial lod={lod} largeLod={large_lod} aspect={aspect} fmt={format:#x} rows={start_row}..{end_row}"
        ),
    );
    grTexDownloadMipMapLevel(
        tmu,
        start_address,
        lod,
        large_lod,
        aspect,
        format,
        even_odd,
        data,
    );
}

unsafe fn download_texture(start_address: u32, info: *const GrTexInfo) {
    if info.is_null() {
        return;
    }
    let info = &*info;
    trace(&format!(
        "grTexDownload addr={start_address:#x} fmt={:#x} lod={} aspect={}",
        info.format, info.large_lod, info.aspect_ratio
    ));
    if info.data.is_null() {
        return;
    }
    let fmt = match map_texfmt(info.format) {
        Some(f) => f,
        None => {
            glidevk_log(&format!(
                "grTexDownload: UNHANDLED format={:#x} largeLod={} aspect={}",
                info.format, info.large_lod, info.aspect_ratio
            ));
            return;
        }
    };
    let (w, h) = tex_dims(info);
    let src = std::slice::from_raw_parts(
        info.data as *const u8,
        (w as usize) * (h as usize) * texfmt_bpp(fmt),
    );
    with_ctx(|ctx| {
        // Palettised textures keep only their indices here: Glide applies the
        // palette at draw time (Unreal binds, THEN uploads the texture's palette,
        // then draws), so they are decoded per draw against the live palette —
        // see `bind_draw_texture`. Any GPU variants of a previous texture at this
        // address are stale.
        if matches!(fmt, TexFormat::P8) {
            let stale = ctx
                .textures
                .store_p8_source(start_address, w, h, src.to_vec());
            if let Some(b) = ctx.backend.as_mut() {
                for key in stale {
                    b.release_texture(key);
                }
            }
        }
        match ctx.textures.decode(fmt, w, h, src) {
            Ok(tex) => {
                // Log each distinct texture's decoded content once: its format,
                // dims, and average RGBA + alpha coverage. Tells us whether a
                // garbled overlay (e.g. the green-dot backdrop at some addr) comes
                // from the texture itself being green/masked vs. the combine.
                log_texture_content(start_address, info.format, w, h, &tex.rgba, src);
                if !matches!(fmt, TexFormat::P8) {
                    if let Some(b) = ctx.backend.as_mut() {
                        let _ = b.upload_texture(start_address as u64, w, h, &tex.rgba);
                    }
                }
            }
            Err(e) => glidevk_log(&format!(
                "grTexDownload: decode failed {e:?} fmt={:#x}",
                info.format
            )),
        }
    });
}

/// Log a decoded texture's average colour + alpha coverage (deduped by address),
/// so we can tell what a mis-rendered quad's texture actually contains.
fn log_texture_content(addr: u32, format: u32, w: u32, h: u32, rgba: &[u8], raw: &[u8]) {
    use std::collections::HashSet;
    use std::hash::{Hash, Hasher};
    static SEEN: Mutex<Option<HashSet<u64>>> = Mutex::new(None);
    static DUMPED: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
    const DUMP_MAX: u32 = 400;
    if std::env::var_os("GLIDEVK_LOG").is_none() || rgba.len() < 4 {
        return;
    }
    // Unreal's texture cache reuses TMU addresses, so key on the content too
    // (a coarse hash of the raw bytes), not just the address.
    let mut hs = std::collections::hash_map::DefaultHasher::new();
    (addr, format, w, h).hash(&mut hs);
    raw.hash(&mut hs);
    let fresh = match SEEN.lock() {
        Ok(mut g) => g.get_or_insert_with(HashSet::new).insert(hs.finish()),
        Err(_) => return,
    };
    if !fresh {
        return;
    }
    let frame = FRAME.load(Ordering::Relaxed);
    // GLIDEVK_TEXDUMP=<dir>: write each distinct decoded texture as a PPM so
    // what the game uploaded can be inspected directly.
    if let Ok(dir) = std::env::var("GLIDEVK_TEXDUMP") {
        if DUMPED.fetch_add(1, Ordering::Relaxed) < DUMP_MAX {
            write_ppm(
                &format!("{dir}/tex_f{frame:05}_{addr:#07x}_{w}x{h}_fmt{format:x}.ppm"),
                w,
                h,
                rgba,
            );
        }
    }
    // Palettised textures: show the index range so an all-black decode can be
    // attributed to the indices vs the palette.
    let idx = if format == 0x5 && !raw.is_empty() {
        let (mn, mx) = raw
            .iter()
            .fold((255u8, 0u8), |(a, b), &v| (a.min(v), b.max(v)));
        format!(" idx={mn}..{mx}")
    } else {
        String::new()
    };
    let (mut r, mut gg, mut b, mut a, mut opaque) = (0u64, 0u64, 0u64, 0u64, 0u64);
    let n = rgba.len() / 4;
    for px in rgba.chunks_exact(4) {
        r += px[0] as u64;
        gg += px[1] as u64;
        b += px[2] as u64;
        a += px[3] as u64;
        if px[3] >= 128 {
            opaque += 1;
        }
    }
    let d = n.max(1) as u64;
    glidevk_log(&format!(
        "texcontent frame={frame} addr={addr:#x} fmt={format:#x} {w}x{h} avgRGBA=({},{},{},{}) opaque={}%{idx}",
        r / d,
        gg / d,
        b / d,
        a / d,
        opaque * 100 / d
    ));
}

#[export_name = "grTexSource"]
pub extern "system" fn grTexSource(
    _tmu: u32,
    start_address: u32,
    _even_odd: u32,
    _info: *const GrTexInfo,
) {
    LAST_TEX_ADDR.store(start_address, Ordering::Relaxed);
    trace(&format!("grTexSource addr={start_address:#x}"));
    with_ctx(|ctx| {
        // Palettised textures are resolved per draw (`bind_draw_texture`).
        if ctx.textures.is_p8(start_address) {
            return;
        }
        if let Some(b) = ctx.backend.as_mut() {
            let found = b.set_texture(start_address as u64);
            if !found {
                log_once(
                    &LOG_TS,
                    &format!("grTexSource addr={start_address:#x} -> NO texture (default white)"),
                );
            }
        }
    });
}

#[export_name = "grTexDownloadTable"]
pub unsafe extern "system" fn grTexDownloadTable(
    _tmu: u32,
    _table_type: i32,
    data: *const core::ffi::c_void,
) {
    if data.is_null() {
        return;
    }
    let src = std::slice::from_raw_parts(data as *const u32, 256);
    let mut palette = [0u32; 256];
    palette.copy_from_slice(src);
    let nonzero = palette.iter().filter(|&&c| c & 0x00ff_ffff != 0).count();
    trace(&format!(
        "grTexDownloadTable nonzero={nonzero} p[1]={:#08x} p[29]={:#08x}",
        palette[1] & 0xff_ffff,
        palette[29] & 0xff_ffff
    ));
    log_distinct(
        6 << 40 | (nonzero as u64) << 8 | (palette[1] & 0xff) as u64,
        &format!(
            "grTexDownloadTable type={_table_type} nonzero={nonzero}/256 p[0..8]={:06x?} p[16]={:#08x} p[29]={:#08x} p[255]={:#08x}",
            palette[..8].iter().map(|c| c & 0xff_ffff).collect::<Vec<_>>(),
            palette[16] & 0xff_ffff,
            palette[29] & 0xff_ffff,
            palette[255] & 0xff_ffff
        ),
    );
    // Deus Ex animates its intro logos by cycling this palette. Just record it
    // (a cheap version bump); the affected P8 textures re-decode lazily when
    // next bound for drawing, so the hundreds of redundant per-frame uploads
    // collapse to at most one re-decode per texture per frame.
    with_ctx(|ctx| ctx.textures.set_palette(palette));
}

/// `grTexCalcMemRequired(smallLod, largeLod, aspect, format)` -> bytes.
#[export_name = "grTexCalcMemRequired"]
pub extern "system" fn grTexCalcMemRequired(
    _small_lod: u32,
    large_lod: u32,
    aspect: u32,
    format: u32,
) -> u32 {
    let info = GrTexInfo {
        small_lod: large_lod,
        large_lod,
        aspect_ratio: aspect,
        format,
        data: core::ptr::null(),
    };
    let (w, h) = tex_dims(&info);
    let bpp = map_texfmt(format).map(texfmt_bpp).unwrap_or(2);
    (w * h) * bpp as u32
}

const TMU_MEMORY_BYTES: u32 = 4 * 1024 * 1024;

#[export_name = "grTexMinAddress"]
pub extern "system" fn grTexMinAddress(_tmu: u32) -> u32 {
    0
}

#[export_name = "grTexMaxAddress"]
pub extern "system" fn grTexMaxAddress(_tmu: u32) -> u32 {
    TMU_MEMORY_BYTES - 1
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

#[export_name = "grDrawTriangle"]
pub unsafe extern "system" fn grDrawTriangle(
    a: *const core::ffi::c_void,
    b: *const core::ffi::c_void,
    c: *const core::ffi::c_void,
) {
    log_raw_vertex(a as *const u8, 3);
    let tris = [
        read_vertex(a as *const u8),
        read_vertex(b as *const u8),
        read_vertex(c as *const u8),
    ];
    let mut tex_size = [1.0, 1.0];
    let mut shade = String::new();
    with_ctx(|ctx| {
        let combine = CombineParams::from_state(&ctx.state);
        let blend = BlendConfig::from_state(&ctx.state);
        let depth = DepthConfig::from_state(&ctx.state);
        let ce = ctx.state.chromakey_enabled;
        let ck = [
            ctx.state.chroma_key[0],
            ctx.state.chroma_key[1],
            ctx.state.chroma_key[2],
        ];
        let (atf, atr) = (ctx.state.alpha_test_func, ctx.state.alpha_test_ref);
        if std::env::var_os("GLIDEVK_LOG").is_some() {
            shade = shade_desc(&ctx.state);
        }
        if let Some(bk) = ctx.backend.as_mut() {
            bind_draw_texture(&mut ctx.textures, bk);
            bk.set_combine(combine);
            bk.set_blend(blend);
            bk.set_depth(depth);
            bk.set_chroma(ce, ck);
            bk.set_alpha_test(atf, atr);
            bk.set_clamp(ctx.state.tex_clamp);
            let _ = bk.draw_triangles(&tris);
            tex_size = bk.current_tex_size();
        }
    });
    log_texcoords(&tris, tex_size, &shade);
}

#[export_name = "grDrawPolygonVertexList"]
pub unsafe extern "system" fn grDrawPolygonVertexList(
    nverts: i32,
    vlist: *const core::ffi::c_void,
) {
    if nverts < 3 || vlist.is_null() {
        return;
    }
    let base = vlist as *const u8;
    log_raw_vertex(base, nverts);
    let verts: Vec<DrawVertex> = (0..nverts as usize)
        .map(|i| read_vertex(base.add(i * GRVERTEX_SIZE)))
        .collect();
    submit_fan(&verts);
}

#[export_name = "grDrawPlanarPolygonVertexList"]
pub unsafe extern "system" fn grDrawPlanarPolygonVertexList(
    nverts: i32,
    vlist: *const core::ffi::c_void,
) {
    grDrawPolygonVertexList(nverts, vlist);
}

// ---------------------------------------------------------------------------
// LFB
// ---------------------------------------------------------------------------

/// Glide2 `GrLfbInfo_t`.
#[repr(C)]
pub struct GrLfbInfo {
    pub size: i32,
    pub lfb_ptr: *mut core::ffi::c_void,
    pub stride_in_bytes: u32,
    pub write_mode: i32,
    pub origin: i32,
}

#[export_name = "grLfbLock"]
pub unsafe extern "system" fn grLfbLock(
    _lock_type: u32,
    _buffer: u32,
    write_mode: i32,
    origin: i32,
    _pixel_pipeline: FxBool,
    info: *mut GrLfbInfo,
) -> FxBool {
    glidevk_log(&format!("grLfbLock mode={write_mode}"));
    if info.is_null() {
        return 0;
    }
    with_ctx(|ctx| {
        if let Some(b) = ctx.backend.as_mut() {
            let (ptr, stride) = b.lfb_lock(write_mode);
            (*info).lfb_ptr = ptr as *mut core::ffi::c_void;
            (*info).stride_in_bytes = stride;
            (*info).write_mode = write_mode;
            (*info).origin = origin;
            1
        } else {
            0
        }
    })
    .unwrap_or(0)
}

#[export_name = "grLfbUnlock"]
pub extern "system" fn grLfbUnlock(_lock_type: u32, _buffer: u32) -> FxBool {
    with_ctx(|ctx| {
        if let Some(b) = ctx.backend.as_mut() {
            b.lfb_unlock();
        }
    });
    1
}

/// `grLfbReadRegion`: copy a rectangle of the framebuffer to caller memory as
/// 16-bit RGB565 (the only format Glide 2 reads back in).
///
/// Reads come from the **last presented frame**: the first call enables the
/// per-frame host copy, so a read made before any frame has been presented
/// with readback on returns zeros. Coordinates are in the game's logical space
/// and scaled to the swapchain extent when the two differ.
///
/// # Safety
/// `dst_data` must point to at least `src_h * dst_stride` writable bytes.
#[export_name = "grLfbReadRegion"]
pub unsafe extern "system" fn grLfbReadRegion(
    _src_buffer: u32,
    src_x: u32,
    src_y: u32,
    src_w: u32,
    src_h: u32,
    dst_stride: u32,
    dst_data: *mut core::ffi::c_void,
) -> FxBool {
    if dst_data.is_null() || src_w == 0 || src_h == 0 {
        return 0;
    }
    let dst_len = (src_h as usize) * (dst_stride as usize);
    let dst = std::slice::from_raw_parts_mut(dst_data as *mut u8, dst_len);
    with_ctx(|ctx| {
        let Some(b) = ctx.backend.as_mut() else {
            return 0;
        };
        b.set_full_readback(true);
        let (lw, lh) = b.logical_size();
        let Some((fw, fh, rgba)) = b.last_frame() else {
            glidevk_log("grLfbReadRegion: no frame presented since readback enabled");
            dst.fill(0);
            return 1;
        };
        for row in 0..src_h {
            let fy = ((src_y + row) * fh / lh.max(1)).min(fh - 1) as usize;
            for col in 0..src_w {
                let fx = ((src_x + col) * fw / lw.max(1)).min(fw - 1) as usize;
                let i = (fy * fw as usize + fx) * 4;
                let px = pack_rgb565(rgba[i], rgba[i + 1], rgba[i + 2]);
                let o = (row * dst_stride + col * 2) as usize;
                if o + 2 <= dst.len() {
                    dst[o..o + 2].copy_from_slice(&px.to_le_bytes());
                }
            }
        }
        1
    })
    .unwrap_or(0)
}

/// Pack 8-bit RGB into the Glide framebuffer's RGB565.
fn pack_rgb565(r: u8, g: u8, b: u8) -> u16 {
    ((r as u16 >> 3) << 11) | ((g as u16 >> 2) << 5) | (b as u16 >> 3)
}

#[export_name = "grLfbWriteRegion"]
pub extern "system" fn grLfbWriteRegion(
    _dst_buffer: u32,
    _dst_x: u32,
    _dst_y: u32,
    _fmt: u32,
    _src_w: u32,
    _src_h: u32,
    _src_stride: u32,
    _src_data: *const core::ffi::c_void,
) -> FxBool {
    1
}

// ---------------------------------------------------------------------------
// GR_* -> core enum maps (values shared with Glide3)
// ---------------------------------------------------------------------------

fn map_combine_function(v: i32) -> CombineFunction {
    use CombineFunction::*;
    match v {
        0x0 => Zero,
        0x1 => Local,
        0x2 => LocalAlpha,
        0x3 => ScaleOther,
        0x4 => ScaleOtherAddLocal,
        0x5 => ScaleOtherAddLocalAlpha,
        0x6 => ScaleOtherMinusLocal,
        0x7 => ScaleOtherMinusLocalAddLocal,
        0x8 => ScaleOtherMinusLocalAddLocalAlpha,
        0x9 => ScaleMinusLocalAddLocal,
        0xa => ScaleMinusLocalAddLocalAlpha,
        _ => Zero,
    }
}

fn map_combine_factor(v: i32) -> CombineFactor {
    use CombineFactor::*;
    match v {
        0x0 => Zero,
        0x1 => Local,
        0x2 => OtherAlpha,
        0x3 => LocalAlpha,
        0x4 => TextureAlpha,
        0x8 => One,
        0x9 => OneMinusLocal,
        0xa => OneMinusOtherAlpha,
        0xb => OneMinusLocalAlpha,
        0xc => OneMinusTextureAlpha,
        _ => Zero,
    }
}

fn map_combine_local(v: i32) -> CombineLocal {
    use CombineLocal::*;
    match v {
        0x0 => Iterated,
        0x1 => Constant,
        0x2 => None,
        0x3 => Depth,
        _ => Iterated,
    }
}

fn map_combine_other(v: i32) -> CombineOther {
    use CombineOther::*;
    match v {
        0x0 => Iterated,
        0x1 => Texture,
        0x2 => Constant,
        0x3 => None,
        _ => Iterated,
    }
}

// ---------------------------------------------------------------------------
// Accepted-but-minimal entry points (state stored elsewhere or not yet used)
// ---------------------------------------------------------------------------

macro_rules! stub {
    ($name:literal, $rust:ident ( $($arg:ident : $ty:ty),* ) $(-> $ret:ty)?) => {
        #[export_name = $name]
        pub extern "system" fn $rust($(_: $ty),*) $(-> $ret)? { Default::default() }
    };
}

/// `grColorMask(rgb, alpha)` — enable/disable writing the colour / alpha channels
/// to the framebuffer (e.g. depth-only passes, preserving destination alpha).
#[export_name = "grColorMask"]
pub extern "system" fn grColorMask(rgb: FxBool, alpha: FxBool) {
    with_ctx(|ctx| {
        ctx.state.color_mask_rgb = rgb != 0;
        ctx.state.color_mask_alpha = alpha != 0;
    });
}
stub!("grDitherMode", gr_dither_mode(mode: i32));
stub!("grClipWindow", gr_clip_window(minx: u32, miny: u32, maxx: u32, maxy: u32));
stub!("grDepthBiasLevel", gr_depth_bias(level: i16));
stub!("grDrawLine", gr_draw_line(a: *const core::ffi::c_void, b: *const core::ffi::c_void));
stub!("grErrorSetCallback", gr_error_cb(cb: *const core::ffi::c_void));
stub!("grFogColorValue", gr_fog_color(color: u32));
stub!("grFogTable", gr_fog_table(table: *const u8));
/// `grGammaCorrectionValue(float)` — the 3dfx RAMDAC output gamma. Real Voodoo
/// hardware applies this to the whole framebuffer at scan-out, which lifts dim
/// content (Unreal's flat-shaded intro logos) into the visible range; without it
/// they stay near-black. We forward it to the backend, which applies it in the
/// combiner's final output.
#[export_name = "grGammaCorrectionValue"]
pub extern "system" fn grGammaCorrectionValue(value: f32) {
    log_distinct(
        8 << 40 | value.to_bits() as u64,
        &format!("grGammaCorrectionValue value={value}"),
    );
    with_ctx(|ctx| {
        if let Some(b) = ctx.backend.as_mut() {
            b.set_gamma(value);
        }
    });
}
stub!("grHints", gr_hints(hint: i32, mask: u32));
/// `grTexClampMode(tmu, s_mode, t_mode)`: GR_TEXTURECLAMP_WRAP (0) / _CLAMP (1)
/// (Glide 3 adds _MIRROR_EXTEND, mapped to clamp). Either axis non-wrap selects
/// the clamped sampler for subsequent draws.
#[export_name = "grTexClampMode"]
pub extern "system" fn grTexClampMode(_tmu: u32, s_mode: i32, t_mode: i32) {
    with_ctx(|ctx| ctx.state.tex_clamp = s_mode != 0 || t_mode != 0);
}
stub!("grTexCombine", gr_tex_combine(tmu: u32, rf: i32, rfa: i32, af: i32, afa: i32, ri: FxBool, ai: FxBool));
stub!("grTexCombineFunction", gr_tex_combine_fn(tmu: u32, func: i32));
stub!("grTexFilterMode", gr_tex_filter(tmu: u32, minf: i32, magf: i32));
stub!("grTexLodBiasValue", gr_tex_lod_bias(tmu: u32, bias: f32));
stub!("grTexMipMapMode", gr_tex_mipmap(tmu: u32, mode: i32, lod_blend: FxBool));
/// `grAlphaControlsITRGBLighting(FxBool)` — when enabled, the MSB of the texture
/// alpha selects the color-combine local source per texel (MSB=1 → constant
/// color, MSB=0 → iterated vertex RGB), a "specular alpha paint" trick. The
/// combiner shader applies it via the `alphaLighting` push constant.
#[export_name = "grAlphaControlsITRGBLighting"]
pub extern "system" fn grAlphaControlsITRGBLighting(enable: FxBool) {
    with_ctx(|ctx| ctx.state.alpha_it_lighting = enable != 0);
}
stub!("guTexCombineFunction", gu_tex_combine_fn(tmu: u32, func: i32));

// ---------------------------------------------------------------------------
// Diagnostics (not part of the Glide API) — shared with the glide3x harnesses.
// ---------------------------------------------------------------------------

#[export_name = "glidevkFrameResult"]
pub extern "system" fn glidevkFrameResult() -> i32 {
    with_ctx(|ctx| ctx.present_errors as i32).unwrap_or(-1)
}

#[export_name = "glidevkCenterPixel"]
pub extern "system" fn glidevkCenterPixel() -> u32 {
    with_ctx(|ctx| {
        ctx.backend
            .as_ref()
            .map(|b| {
                let [r, g, bl, a] = b.center_pixel_rgba();
                (r as u32) << 24 | (g as u32) << 16 | (bl as u32) << 8 | a as u32
            })
            .unwrap_or(0)
    })
    .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fixed_vertex_read() {
        // Build a GrVertex: x=100, y=50, rgb=(255,0,0), a=255, sow=0.5, tow=0.25.
        let mut v = [0f32; 16];
        v[0] = 100.0; // x
        v[1] = 50.0; // y
        v[3] = 255.0; // r  (offset 12 = index 3)
        v[7] = 255.0; // a  (offset 28 = index 7)
        v[9] = 0.5; // sow (offset 36 = index 9)
        v[10] = 0.25; // tow (offset 40 = index 10)
        let dv = unsafe { read_vertex(v.as_ptr() as *const u8) };
        assert_eq!(dv.pos, [100.0, 50.0]);
        assert_eq!(dv.color, [1.0, 0.0, 0.0, 1.0]);
        assert_eq!(dv.tex, [0.5, 0.25]);
    }

    #[test]
    fn uninitialized_tmu_oow_is_ignored_for_vertex_oow() {
        // Regression for the Deus Ex intro/menu garble: GlideDrv leaves
        // tmuvtx[0].oow (offset 44 = idx 11) uninitialized, so read_vertex must
        // take the vertex oow (offset 32 = idx 8, = 1/w) and never divide texture
        // coordinates by that stack garbage.
        let mut v = [0f32; 16];
        v[0] = 100.0; // x
        v[1] = 50.0; // y
        v[8] = 0.5; // vertex oow = 1/w
        v[9] = 2.0; // sow

        // The exact denormal garbage seen in the wild: the int 960 (screen width)
        // reinterpreted as f32 (~1.345e-42). Must be ignored.
        v[11] = f32::from_bits(960);
        assert_eq!(unsafe { read_vertex(v.as_ptr() as *const u8) }.oow, 0.5);

        // Normal-looking garbage (5919.0 was also observed) must lose to the
        // vertex oow too — the whole per-TMU slot is untrustworthy for GlideDrv.
        v[11] = 5919.0;
        assert_eq!(unsafe { read_vertex(v.as_ptr() as *const u8) }.oow, 0.5);

        // Vertex oow unset -> fall back to a usable per-TMU oow.
        v[8] = 0.0;
        v[11] = 4.0;
        assert_eq!(unsafe { read_vertex(v.as_ptr() as *const u8) }.oow, 4.0);

        // Neither usable -> affine (1.0, no perspective divide), never inf.
        v[11] = 0.0;
        assert_eq!(unsafe { read_vertex(v.as_ptr() as *const u8) }.oow, 1.0);
    }

    #[test]
    fn glide2_lod_is_inverted() {
        // GR_LOD_256 = 0 -> 256px; aspect 1x1 = 3 -> square.
        let info = GrTexInfo {
            small_lod: 0,
            large_lod: 0,
            aspect_ratio: 3,
            format: 0x0a,
            data: core::ptr::null(),
        };
        assert_eq!(tex_dims(&info), (256, 256));
        let info8 = GrTexInfo {
            large_lod: 5,
            ..info
        };
        assert_eq!(tex_dims(&info8), (8, 8)); // GR_LOD_8 = 5
    }
}

#[cfg(test)]
mod lfb_tests {
    use super::pack_rgb565;

    #[test]
    fn rgb565_packing_matches_glide_framebuffer_layout() {
        assert_eq!(pack_rgb565(255, 0, 0), 0xF800);
        assert_eq!(pack_rgb565(0, 255, 0), 0x07E0);
        assert_eq!(pack_rgb565(0, 0, 255), 0x001F);
        assert_eq!(pack_rgb565(255, 255, 255), 0xFFFF);
        assert_eq!(pack_rgb565(0, 0, 0), 0x0000);
    }
}

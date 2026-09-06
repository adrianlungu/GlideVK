//! Glide primitive types and enums.
//!
//! Names and numeric values mirror `glide.h` / `g3ext.h` from the open 3dfx
//! Glide SDK so the DLL shells can pass values straight through. Only the
//! subset needed for P0–P2 is defined; extend from `sst1vid.h` / `glide.h`.

/// Glide's boolean (`FxBool`).
pub type FxBool = i32;
/// Handle to a Glide context returned by `grSstWinOpen`.
pub type GrContext = u32;

/// Vertex as consumed by `grDrawVertexArray` (Glide3 `GrVertex`, common layout).
///
/// Glide vertices are plain structs of floats; the exact stride is set by the
/// game via `grVertexLayout`, so the real path reads offsets dynamically. This
/// canonical form is what we normalize to before uploading to a vertex buffer.
#[derive(Clone, Copy, Debug, Default)]
#[repr(C)]
pub struct Vertex {
    pub x: f32,
    pub y: f32,
    pub ooz: f32, // 1/z
    pub oow: f32, // 1/w
    pub r: f32,
    pub g: f32,
    pub b: f32,
    pub a: f32,
    /// Per-TMU texture coords: (s, w) pairs, pre-divide.
    pub tmu: [[f32; 2]; 3],
}

/// Color combine / texture combine function selector (`GR_COMBINE_FUNCTION_*`).
///
/// `#[repr(i32)]` pins the discriminants: they are the integer codes the P2
/// combiner shader (`combine.frag`) switches on, so declaration order here is
/// load-bearing — keep it in sync with the shader.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
#[repr(i32)]
pub enum CombineFunction {
    #[default]
    Zero,
    Local,
    LocalAlpha,
    ScaleOther,
    ScaleOtherAddLocal,
    ScaleOtherAddLocalAlpha,
    ScaleOtherMinusLocal,
    ScaleOtherMinusLocalAddLocal,
    ScaleOtherMinusLocalAddLocalAlpha,
    ScaleMinusLocalAddLocal,
    ScaleMinusLocalAddLocalAlpha,
}

/// Combine factor selector (`GR_COMBINE_FACTOR_*`). Discriminants are the
/// combiner-shader codes; keep in sync with `combine.frag`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
#[repr(i32)]
pub enum CombineFactor {
    #[default]
    Zero,
    Local,
    OtherAlpha,
    LocalAlpha,
    TextureAlpha,
    DetailFactor,
    LodFraction,
    One,
    OneMinusLocal,
    OneMinusOtherAlpha,
    OneMinusLocalAlpha,
    OneMinusTextureAlpha,
    OneMinusDetailFactor,
    OneMinusLodFraction,
}

/// Combine local color source (`GR_COMBINE_LOCAL_*`). Discriminants match
/// `combine.frag`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
#[repr(i32)]
pub enum CombineLocal {
    #[default]
    Iterated,
    Constant,
    None,
    Depth,
}

/// Combine "other" color source (`GR_COMBINE_OTHER_*`). Discriminants match
/// `combine.frag`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
#[repr(i32)]
pub enum CombineOther {
    #[default]
    Iterated,
    Texture,
    Constant,
    None,
}

/// Alpha blend factor (`GR_BLEND_*`). Maps to `VkBlendFactor`; discriminants
/// are the codes the backend's `map_blend_factor` translates.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
#[repr(i32)]
pub enum BlendFactor {
    #[default]
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstColor,
    OneMinusDstColor,
    DstAlpha,
    OneMinusDstAlpha,
    AlphaSaturate,
}

/// Texture internal formats (`GR_TEXFMT_*`).
///
/// These are what games hand to `grTexDownloadMipMap`; the texture cache
/// converts each to a `VkFormat`-compatible layout on upload.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
pub enum TexFormat {
    #[default]
    Rgb565,
    Argb1555,
    Argb4444,
    Argb8332,
    AlphaIntensity88,
    Alpha8,
    Intensity8,
    P8,       // palettized
    Yiq422,   // NCC-compressed
    Ayiq8422, // NCC-compressed with alpha
}

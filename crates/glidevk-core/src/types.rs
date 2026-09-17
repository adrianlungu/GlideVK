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
    /// `GR_COMBINE_FUNCTION_SCALE_MINUS_OTHER_ADD_LOCAL` (0x9, a.k.a.
    /// `BLEND_LOCAL`): `f * (-other) + local`.
    ScaleMinusOtherAddLocal,
    /// `GR_COMBINE_FUNCTION_SCALE_MINUS_OTHER_ADD_LOCAL_ALPHA` (0x10).
    ScaleMinusOtherAddLocalAlpha,
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

impl BlendFactor {
    /// Map a `GrAlphaBlendFnc_t` code, per `glide.h`: 0 ZERO, 1 SRC_ALPHA,
    /// 2 SRC_COLOR/DST_COLOR, 3 DST_ALPHA, 4 ONE, 5 ONE_MINUS_SRC_ALPHA,
    /// 6 ONE_MINUS_SRC_COLOR/ONE_MINUS_DST_COLOR, 7 ONE_MINUS_DST_ALPHA,
    /// 0xf ALPHA_SATURATE (source side) / PREFOG_COLOR (destination side).
    ///
    /// The `_COLOR` codes name the *other* operand, so `is_src` selects the
    /// side; with fog off the pre-fog colour is the source colour. Verified
    /// against nGlide by `examples/wine_blendmode.c` via `run_diff.sh`, which
    /// walks every code on both sides.
    pub fn from_glide(code: i32, is_src: bool) -> Self {
        use BlendFactor::*;
        match code {
            0x0 => Zero,
            0x1 => SrcAlpha,
            0x2 => {
                if is_src {
                    DstColor
                } else {
                    SrcColor
                }
            }
            0x3 => DstAlpha,
            0x4 => One,
            0x5 => OneMinusSrcAlpha,
            0x6 => {
                if is_src {
                    OneMinusDstColor
                } else {
                    OneMinusSrcColor
                }
            }
            0x7 => OneMinusDstAlpha,
            0xf => {
                if is_src {
                    AlphaSaturate
                } else {
                    SrcColor
                }
            }
            _ => Zero,
        }
    }
}

impl CombineFunction {
    /// `GR_COMBINE_FUNCTION_*` per `glide.h`: ZERO 0, LOCAL 1, LOCAL_ALPHA 2,
    /// SCALE_OTHER 3, SCALE_OTHER_ADD_LOCAL 4, SCALE_OTHER_ADD_LOCAL_ALPHA 5,
    /// SCALE_OTHER_MINUS_LOCAL 6, SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL (BLEND) 7,
    /// SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA 8, SCALE_MINUS_OTHER_ADD_LOCAL
    /// (BLEND_LOCAL) 9, SCALE_MINUS_OTHER_ADD_LOCAL_ALPHA 0x10.
    pub fn from_glide(code: i32) -> Self {
        use CombineFunction::*;
        match code {
            0x0 => Zero,
            0x1 => Local,
            0x2 => LocalAlpha,
            0x3 => ScaleOther,
            0x4 => ScaleOtherAddLocal,
            0x5 => ScaleOtherAddLocalAlpha,
            0x6 => ScaleOtherMinusLocal,
            0x7 => ScaleOtherMinusLocalAddLocal,
            0x8 => ScaleOtherMinusLocalAddLocalAlpha,
            0x9 => ScaleMinusOtherAddLocal,
            0x10 => ScaleMinusOtherAddLocalAlpha,
            _ => Zero,
        }
    }
}

impl CombineFactor {
    /// `GR_COMBINE_FACTOR_*` per `glide.h`: ZERO 0, LOCAL 1, OTHER_ALPHA 2,
    /// LOCAL_ALPHA 3, TEXTURE_ALPHA (DETAIL_FACTOR) 4, TEXTURE_RGB (LOD_FRACTION)
    /// 5, ONE 8, ONE_MINUS_LOCAL 9, ONE_MINUS_OTHER_ALPHA 0xa,
    /// ONE_MINUS_LOCAL_ALPHA 0xb, ONE_MINUS_TEXTURE_ALPHA 0xc,
    /// ONE_MINUS_LOD_FRACTION 0xd.
    pub fn from_glide(code: i32) -> Self {
        use CombineFactor::*;
        match code {
            0x0 => Zero,
            0x1 => Local,
            0x2 => OtherAlpha,
            0x3 => LocalAlpha,
            0x4 => TextureAlpha,
            0x5 => LodFraction,
            0x8 => One,
            0x9 => OneMinusLocal,
            0xa => OneMinusOtherAlpha,
            0xb => OneMinusLocalAlpha,
            0xc => OneMinusTextureAlpha,
            0xd => OneMinusLodFraction,
            _ => Zero,
        }
    }
}

impl CombineLocal {
    /// `GR_COMBINE_LOCAL_*`: ITERATED 0, CONSTANT 1 (NONE is its alias), DEPTH 2.
    pub fn from_glide(code: i32) -> Self {
        match code {
            0x1 => CombineLocal::Constant,
            0x2 => CombineLocal::Depth,
            _ => CombineLocal::Iterated,
        }
    }
}

impl CombineOther {
    /// `GR_COMBINE_OTHER_*`: ITERATED 0, TEXTURE 1, CONSTANT 2 (NONE is its alias).
    pub fn from_glide(code: i32) -> Self {
        match code {
            0x1 => CombineOther::Texture,
            0x2 => CombineOther::Constant,
            _ => CombineOther::Iterated,
        }
    }
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

/// Screen resolutions (`GrScreenResolution_t`, `GR_RESOLUTION_*`).
///
/// Discriminants are the exact `glide.h` codes that `grSstWinOpen` receives, so
/// the shells can convert with [`ScreenResolution::from_code`]. `None` (0xff)
/// means "use the window size" in Glide 3.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum ScreenResolution {
    R320x200 = 0x0,
    R320x240 = 0x1,
    R400x256 = 0x2,
    R512x384 = 0x3,
    R640x200 = 0x4,
    R640x350 = 0x5,
    R640x400 = 0x6,
    R640x480 = 0x7,
    R800x600 = 0x8,
    R960x720 = 0x9,
    R856x480 = 0xa,
    R512x256 = 0xb,
    R1024x768 = 0xc,
    R1280x1024 = 0xd,
    R1600x1200 = 0xe,
    R400x300 = 0xf,
    None = 0xff,
}

impl ScreenResolution {
    /// Decode a raw `GR_RESOLUTION_*` code; unknown codes decode to `None`.
    pub fn from_code(code: u32) -> Self {
        match code {
            0x0 => Self::R320x200,
            0x1 => Self::R320x240,
            0x2 => Self::R400x256,
            0x3 => Self::R512x384,
            0x4 => Self::R640x200,
            0x5 => Self::R640x350,
            0x6 => Self::R640x400,
            0x7 => Self::R640x480,
            0x8 => Self::R800x600,
            0x9 => Self::R960x720,
            0xa => Self::R856x480,
            0xb => Self::R512x256,
            0xc => Self::R1024x768,
            0xd => Self::R1280x1024,
            0xe => Self::R1600x1200,
            0xf => Self::R400x300,
            _ => Self::None,
        }
    }

    /// Pixel dimensions, or `None` for [`ScreenResolution::None`].
    pub fn dimensions(self) -> Option<(u32, u32)> {
        Some(match self {
            Self::R320x200 => (320, 200),
            Self::R320x240 => (320, 240),
            Self::R400x256 => (400, 256),
            Self::R512x384 => (512, 384),
            Self::R640x200 => (640, 200),
            Self::R640x350 => (640, 350),
            Self::R640x400 => (640, 400),
            Self::R640x480 => (640, 480),
            Self::R800x600 => (800, 600),
            Self::R960x720 => (960, 720),
            Self::R856x480 => (856, 480),
            Self::R512x256 => (512, 256),
            Self::R1024x768 => (1024, 768),
            Self::R1280x1024 => (1280, 1024),
            Self::R1600x1200 => (1600, 1200),
            Self::R400x300 => (400, 300),
            Self::None => return None,
        })
    }
}

/// Pixel size for a raw `grSstWinOpen` resolution code, defaulting to 640×480
/// for `GR_RESOLUTION_NONE` / unknown codes (the Voodoo1 baseline).
pub fn resolution_to_size(code: u32) -> (u32, u32) {
    ScreenResolution::from_code(code)
        .dimensions()
        .unwrap_or_else(|| ScreenResolution::R640x480.dimensions().unwrap())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn combine_codes_match_glide_h() {
        assert_eq!(
            CombineFunction::from_glide(0x9),
            CombineFunction::ScaleMinusOtherAddLocal
        );
        assert_eq!(
            CombineFunction::from_glide(0x10),
            CombineFunction::ScaleMinusOtherAddLocalAlpha
        );
        assert_eq!(
            CombineFunction::from_glide(0x7),
            CombineFunction::ScaleOtherMinusLocalAddLocal
        );
        assert_eq!(CombineLocal::from_glide(0x2), CombineLocal::Depth);
        assert_eq!(CombineOther::from_glide(0x2), CombineOther::Constant);
        assert_eq!(CombineFactor::from_glide(0x8), CombineFactor::One);
    }

    #[test]
    fn blend_codes_match_glide_h() {
        use BlendFactor::*;
        // Unreal's SetBlending: default (4,0), translucent (4,6), modulated
        // (2,2), masked (1,5).
        assert_eq!(BlendFactor::from_glide(4, true), One);
        assert_eq!(BlendFactor::from_glide(0, false), Zero);
        assert_eq!(BlendFactor::from_glide(6, false), OneMinusSrcColor);
        assert_eq!(BlendFactor::from_glide(6, true), OneMinusDstColor);
        assert_eq!(BlendFactor::from_glide(2, true), DstColor);
        assert_eq!(BlendFactor::from_glide(2, false), SrcColor);
        assert_eq!(BlendFactor::from_glide(1, true), SrcAlpha);
        assert_eq!(BlendFactor::from_glide(5, false), OneMinusSrcAlpha);
        assert_eq!(BlendFactor::from_glide(3, true), DstAlpha);
        assert_eq!(BlendFactor::from_glide(0xf, true), AlphaSaturate);
        assert_eq!(BlendFactor::from_glide(0xf, false), SrcColor);
    }

    #[test]
    fn resolution_codes_match_glide_h() {
        // Unreal-engine games ask for 7 (640x480) and 12 (1024x768); the old
        // table had 7 => 960x720, which mis-sized every Deus Ex viewport.
        assert_eq!(resolution_to_size(0x7), (640, 480));
        assert_eq!(resolution_to_size(0x9), (960, 720));
        assert_eq!(resolution_to_size(0xc), (1024, 768));
        assert_eq!(ScreenResolution::from_code(0xff), ScreenResolution::None);
        assert_eq!(resolution_to_size(0xff), (640, 480));
        for code in 0..=0xf {
            assert_eq!(ScreenResolution::from_code(code) as u32, code);
            assert!(ScreenResolution::from_code(code).dimensions().is_some());
        }
    }
}

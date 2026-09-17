//! The Glide state machine.
//!
//! Glide is a stateful, fixed-function API: the game sets combine/blend/fog/
//! depth/texture state via `grXxx` calls, then issues draws. We accumulate that
//! state here, and its hashable subset (`PipelineKey`) selects a Vulkan
//! pipeline + fragment shader from the cache. This mirrors how DXVK derives
//! pipeline objects from D3D fixed-function state.

use crate::types::*;
use std::hash::{Hash, Hasher};

/// Configuration of one combine unit (color or alpha).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
pub struct Combine {
    pub function: CombineFunction,
    pub factor: CombineFactor,
    pub local: CombineLocal,
    pub other: CombineOther,
    pub invert: bool,
}

/// Per-TMU texture combine configuration (`grTexCombine`).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
pub struct TexCombine {
    pub rgb: Combine,
    pub alpha: Combine,
    pub enabled: bool,
}

/// Depth-buffer mode (`grDepthBufferMode`): Glide supports both a classic Z
/// buffer and a W buffer; the latter needs per-fragment reconstruction.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
pub enum DepthMode {
    #[default]
    Disable,
    ZBuffer,
    WBuffer,
}

/// Largest W the Glide W-buffer represents (`GR_WDEPTHVALUE_FARTHEST`); the
/// Z-buffer likewise spans `ooz` in 0..65535.
pub const GLIDE_DEPTH_RANGE: f32 = 65535.0;

impl DepthMode {
    /// Normalised [0,1] depth for a vertex, smaller = nearer (so `GR_CMP_LESS`
    /// / `LEQUAL` keep their Glide meaning).
    ///
    /// * W-buffer: from `oow = 1/w` — `w / 65535`. Games like Unreal never set
    ///   `ooz`, so using it here would put everything at the same depth and let
    ///   later draws overwrite nearer ones (Deus Ex's Ion Storm glow covering
    ///   its text). A non-positive or non-finite `oow` is treated as farthest.
    /// * Z-buffer: from `ooz` (65535/z, larger = nearer).
    /// * Disabled: farthest; the test is off anyway.
    pub fn vertex_depth(self, ooz: f32, oow: f32) -> f32 {
        match self {
            DepthMode::WBuffer => {
                if oow.is_finite() && oow > 0.0 {
                    ((1.0 / oow) / GLIDE_DEPTH_RANGE).clamp(0.0, 1.0)
                } else {
                    1.0
                }
            }
            DepthMode::ZBuffer => (1.0 - ooz / GLIDE_DEPTH_RANGE).clamp(0.0, 1.0),
            DepthMode::Disable => 1.0,
        }
    }
}

/// The full Glide render state for a context.
#[derive(Clone, Debug)]
pub struct State {
    pub color_combine: Combine,
    pub alpha_combine: Combine,
    pub tex_combine: [TexCombine; 3],

    pub blend_src_rgb: BlendFactor,
    pub blend_dst_rgb: BlendFactor,
    pub blend_src_alpha: BlendFactor,
    pub blend_dst_alpha: BlendFactor,

    pub depth_mode: DepthMode,
    pub depth_write: bool,
    /// `GR_CMP_*` depth compare (`grDepthBufferFunction`). 7 = ALWAYS.
    pub depth_compare: i32,

    pub fog_enabled: bool,
    pub chromakey_enabled: bool,
    /// Chroma-key color (RGBA 0..1), from `grChromakeyValue`.
    pub chroma_key: [f32; 4],

    /// Alpha test (`grAlphaTestFunction`/`ReferenceValue`). Func is `GR_CMP_*`
    /// (7 = ALWAYS = disabled); reference in 0..1.
    pub alpha_test_func: i32,
    pub alpha_test_ref: f32,

    /// Constant color used by `GR_COMBINE_LOCAL_CONSTANT` etc.
    pub constant_color: [f32; 4],

    /// `grAlphaControlsITRGBLighting`: when set, the texture alpha MSB selects the
    /// color-combine *local* source per texel — MSB=1 → constant color, MSB=0 →
    /// iterated vertex RGB (specular "alpha paint").
    pub alpha_it_lighting: bool,

    /// `grColorMask(rgb, alpha)`: whether RGB / alpha channels are written to the
    /// framebuffer. Both default on.
    pub color_mask_rgb: bool,
    pub color_mask_alpha: bool,

    /// `grTexClampMode`: true when either axis is CLAMP (or MIRROR, approximated
    /// as clamp); false = WRAP, the Glide default.
    pub tex_clamp: bool,
}

impl Default for State {
    fn default() -> Self {
        // Default to a passthrough combine that emits the iterated Gouraud
        // color (function = LOCAL, local = ITERATED), so geometry drawn before a
        // game sets grColorCombine is visible rather than black.
        let passthrough = Combine {
            function: CombineFunction::Local,
            factor: CombineFactor::One,
            local: CombineLocal::Iterated,
            other: CombineOther::Iterated,
            invert: false,
        };
        Self {
            color_combine: passthrough,
            alpha_combine: passthrough,
            tex_combine: Default::default(),
            // Glide's default blend is the opaque identity (GR_BLEND_ONE src,
            // GR_BLEND_ZERO dst) — NOT BlendFactor::default() (Zero), which would
            // blend every fragment to (0,0,0,0) before a game sets a blend func.
            blend_src_rgb: BlendFactor::One,
            blend_dst_rgb: BlendFactor::Zero,
            blend_src_alpha: BlendFactor::One,
            blend_dst_alpha: BlendFactor::Zero,
            depth_mode: DepthMode::default(),
            depth_write: false,
            depth_compare: 7, // GR_CMP_ALWAYS
            fog_enabled: false,
            chromakey_enabled: false,
            chroma_key: [0.0; 4],
            alpha_test_func: 7, // GR_CMP_ALWAYS (no test)
            alpha_test_ref: 0.0,
            constant_color: [0.0, 0.0, 0.0, 0.0],
            alpha_it_lighting: false,
            color_mask_rgb: true,
            color_mask_alpha: true,
            tex_clamp: false,
        }
    }
}

/// The hashable slice of `State` that determines which Vulkan pipeline +
/// fragment shader we need. Excludes values that are pushed as uniforms
/// (constant color, fog table) rather than baked into the shader.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct PipelineKey {
    pub color_combine: Combine,
    pub alpha_combine: Combine,
    pub tex_combine: [TexCombine; 3],
    pub blend_src_rgb: BlendFactor,
    pub blend_dst_rgb: BlendFactor,
    pub blend_src_alpha: BlendFactor,
    pub blend_dst_alpha: BlendFactor,
    pub depth_mode: DepthMode,
    pub depth_write: bool,
    pub fog_enabled: bool,
    pub chromakey_enabled: bool,
}

impl State {
    /// Derive the pipeline key for the current draw.
    pub fn pipeline_key(&self) -> PipelineKey {
        PipelineKey {
            color_combine: self.color_combine,
            alpha_combine: self.alpha_combine,
            tex_combine: self.tex_combine,
            blend_src_rgb: self.blend_src_rgb,
            blend_dst_rgb: self.blend_dst_rgb,
            blend_src_alpha: self.blend_src_alpha,
            blend_dst_alpha: self.blend_dst_alpha,
            depth_mode: self.depth_mode,
            depth_write: self.depth_write,
            fog_enabled: self.fog_enabled,
            chromakey_enabled: self.chromakey_enabled,
        }
    }

    /// Stable 64-bit hash of the pipeline key, used as the cache index.
    pub fn pipeline_hash(&self) -> u64 {
        let mut h = std::collections::hash_map::DefaultHasher::new();
        self.pipeline_key().hash(&mut h);
        h.finish()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wbuffer_depth_comes_from_oow_and_nearer_is_smaller() {
        let m = DepthMode::WBuffer;
        let near = m.vertex_depth(0.0, 1.0 / 100.0); // w = 100
        let far = m.vertex_depth(0.0, 1.0 / 1000.0); // w = 1000
        assert!(near < far);
        assert_eq!(m.vertex_depth(0.0, 0.0), 1.0); // unset -> farthest
        assert_eq!(m.vertex_depth(0.0, f32::NAN), 1.0);
        // Z-buffer uses ooz, larger = nearer.
        let z = DepthMode::ZBuffer;
        assert!(z.vertex_depth(60000.0, 0.0) < z.vertex_depth(100.0, 0.0));
    }
}

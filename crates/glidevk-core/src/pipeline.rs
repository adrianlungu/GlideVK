//! State-hash → pipeline/shader cache.
//!
//! Every distinct combine/blend/fog/depth configuration the game uses becomes a
//! `PipelineKey` (see [`crate::state`]). The first time we see a key we
//! synthesize a fragment shader that implements exactly that combiner math,
//! compile it to SPIR-V, build the `VkPipeline`, and cache it. Real games only
//! ever exercise a few dozen keys, so the cache stays tiny.
//!
//! This is the same "derive shader from fixed-function state" trick DXVK uses
//! for D3D fixed-function; Glide's combiner is small enough that the generated
//! GLSL is a short, readable expression tree.

use crate::state::PipelineKey;
use std::collections::HashMap;

/// One cached, ready-to-bind pipeline.
pub struct Pipeline {
    pub key: PipelineKey,
    pub glsl_frag: String, // SPIR-V once the compiler is wired (P1/P2)
                           // pub vk_pipeline: vk::Pipeline,
}

#[derive(Default)]
pub struct PipelineCache {
    map: HashMap<u64, Pipeline>,
}

impl PipelineCache {
    /// Return the cached pipeline for `key`, building it on first use.
    pub fn get_or_build(&mut self, hash: u64, key: PipelineKey) -> &Pipeline {
        self.map.entry(hash).or_insert_with(|| Pipeline {
            key,
            glsl_frag: generate_fragment_shader(&key),
        })
    }

    pub fn len(&self) -> usize {
        self.map.len()
    }

    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }
}

/// Synthesize the fragment-shader body implementing a combiner configuration.
///
/// P0 emits a documented placeholder; P1/P2 expand this into the real combine
/// expression tree (texture fetch per TMU → `grTexCombine` → `grColorCombine`
/// / `grAlphaCombine` → fog → chroma-key `discard`) and hand it to a
/// GLSL→SPIR-V compiler (e.g. `shaderc`/`naga`).
fn generate_fragment_shader(key: &PipelineKey) -> String {
    format!(
        "// GlideVK generated fragment shader\n\
         // color_combine = {:?}\n\
         // alpha_combine = {:?}\n\
         // tmu           = {:?}\n\
         // fog={} chroma={} depth={:?}\n\
         // TODO(P1/P2): emit real combiner expression tree.\n",
        key.color_combine,
        key.alpha_combine,
        key.tex_combine,
        key.fog_enabled,
        key.chromakey_enabled,
        key.depth_mode,
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::state::State;

    #[test]
    fn identical_state_hits_cache_once() {
        let mut cache = PipelineCache::default();
        let s = State::default();
        cache.get_or_build(s.pipeline_hash(), s.pipeline_key());
        cache.get_or_build(s.pipeline_hash(), s.pipeline_key());
        assert_eq!(cache.len(), 1);
    }
}

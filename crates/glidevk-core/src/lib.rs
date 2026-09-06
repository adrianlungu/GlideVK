//! GlideVK core — backend-agnostic translation of the 3dfx Glide pipeline.
//!
//! This crate holds everything that is *not* tied to the `glide2x` / `glide3x`
//! DLL entry points or to a particular OS: the Glide state machine, texture
//! format conversion, linear-framebuffer emulation, and the
//! state-hash -> fragment-shader pipeline cache that targets Vulkan.
//!
//! The thin per-API shells (`glide3x`, and later `glide2x`) call into here.
//!
//! ## Architecture
//!
//! ```text
//! glide3x.dll entry ──▶ Context ──▶ State ──▶ PipelineCache ──▶ Vulkan
//!                                     │
//!                                     ├─ TextureCache  (format conversion)
//!                                     └─ Lfb           (framebuffer r/w)
//! ```
//!
//! Reference semantics come from the open 3dfx Glide SDK (SPDX: `Glide`).

pub mod lfb;
pub mod pipeline;
pub mod state;
pub mod texture;
pub mod types;

#[cfg(feature = "vulkan")]
pub mod vk;

pub use state::State;
pub use types::*;

/// A single Glide rendering context — one per `grSstWinOpen`.
///
/// Owns the Glide state machine plus the caches that turn that state into
/// Vulkan work. The Vulkan device itself lives behind the `vulkan` feature so
/// the core can be exercised (state transitions, format conversion) on hosts
/// without a Vulkan loader.
#[derive(Default)]
pub struct Context {
    pub state: State,
    pub textures: texture::TextureCache,
    pub pipelines: pipeline::PipelineCache,
    /// Last `grBufferClear` color; presented by the next `grBufferSwap`.
    pub clear_color: [f32; 4],
    /// Count of failed presents — 0 means every `grBufferSwap` succeeded.
    /// Exposed for diagnostics/self-test.
    pub present_errors: u32,
    #[cfg(feature = "vulkan")]
    pub backend: Option<vk::Backend>,
}

impl Context {
    pub fn new() -> Self {
        Self::default()
    }
}

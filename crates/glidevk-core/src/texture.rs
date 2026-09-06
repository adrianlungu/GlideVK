//! Texture format conversion and caching.
//!
//! Games upload textures via `grTexDownloadMipMap` in one of the `GR_TEXFMT_*`
//! formats. Several of these have no direct `VkFormat` equivalent (P8
//! palettized, NCC/YIQ compressed) and must be decoded on the CPU (or via a
//! compute shader) into an RGBA image before upload. This is one of the
//! historically crash-prone areas — hence it lives in safe Rust with explicit
//! bounds.

use crate::types::TexFormat;
use std::collections::HashMap;

/// A decoded, upload-ready texture (RGBA8), keyed by its Glide TMU address.
pub struct Texture {
    pub width: u32,
    pub height: u32,
    pub rgba: Vec<u8>,
    // pub image: Option<vk::Image>,  // populated on upload once VK is wired
}

/// Cache of decoded textures, indexed by the TMU start address the game uses as
/// a texture handle in `grTexSource`.
pub struct TextureCache {
    textures: HashMap<u32, Texture>,
    /// Current palette for `GR_TEXFMT_P_8` (set by `grTexDownloadTable`).
    palette: [u32; 256],
    /// Raw index data of downloaded P8 textures, keyed by TMU address. Kept so
    /// they can be re-decoded when the palette changes — games (e.g. Deus Ex's
    /// intro logos) animate palettized textures by cycling the palette, which
    /// the 3dfx palette LUT applies live. Without re-decode they'd be frozen at
    /// whatever palette was set when first downloaded.
    p8_sources: HashMap<u32, (u32, u32, Vec<u8>)>,
    /// Bumped whenever the palette changes; a P8 texture is re-decoded lazily
    /// (at bind time) when its `p8_uploaded` stamp lags this. Collapses the
    /// hundreds of redundant per-frame palette uploads into cheap version bumps.
    palette_version: u64,
    p8_uploaded: HashMap<u32, u64>,
    /// Frame in which each P8 texture was last re-decoded, to cap re-decodes at
    /// one per texture per frame (uploads are expensive; the palette animates
    /// continuously, so an uncapped re-decode-on-bind tanks the framerate).
    p8_last_frame: HashMap<u32, u64>,
}

impl Default for TextureCache {
    fn default() -> Self {
        Self {
            textures: HashMap::new(),
            palette: [0; 256],
            p8_sources: HashMap::new(),
            palette_version: 0,
            p8_uploaded: HashMap::new(),
            p8_last_frame: HashMap::new(),
        }
    }
}

impl TextureCache {
    /// Decode a source mip level into RGBA8. Returns the decoded texture without
    /// inserting it, so callers can associate it with a TMU address.
    ///
    /// Only the linear (non-compressed) formats are handled here for P0–P2;
    /// NCC/YIQ decode is a TODO gated behind P4 fidelity work.
    pub fn decode(
        &self,
        fmt: TexFormat,
        width: u32,
        height: u32,
        src: &[u8],
    ) -> Result<Texture, DecodeError> {
        let texels = (width as usize) * (height as usize);
        let mut rgba = vec![0u8; texels * 4];

        match fmt {
            TexFormat::Rgb565 => {
                let src16 = as_u16_slice(src, texels)?;
                for (i, &p) in src16.iter().enumerate() {
                    let r = ((p >> 11) & 0x1f) as u8;
                    let g = ((p >> 5) & 0x3f) as u8;
                    let b = (p & 0x1f) as u8;
                    rgba[i * 4] = (r << 3) | (r >> 2);
                    rgba[i * 4 + 1] = (g << 2) | (g >> 4);
                    rgba[i * 4 + 2] = (b << 3) | (b >> 2);
                    rgba[i * 4 + 3] = 0xff;
                }
            }
            TexFormat::Argb1555 => {
                let src16 = as_u16_slice(src, texels)?;
                for (i, &p) in src16.iter().enumerate() {
                    let a = if p & 0x8000 != 0 { 0xff } else { 0 };
                    let r = ((p >> 10) & 0x1f) as u8;
                    let g = ((p >> 5) & 0x1f) as u8;
                    let b = (p & 0x1f) as u8;
                    rgba[i * 4] = (r << 3) | (r >> 2);
                    rgba[i * 4 + 1] = (g << 3) | (g >> 2);
                    rgba[i * 4 + 2] = (b << 3) | (b >> 2);
                    rgba[i * 4 + 3] = a;
                }
            }
            TexFormat::Argb4444 => {
                let src16 = as_u16_slice(src, texels)?;
                for (i, &p) in src16.iter().enumerate() {
                    let a = ((p >> 12) & 0xf) as u8;
                    let r = ((p >> 8) & 0xf) as u8;
                    let g = ((p >> 4) & 0xf) as u8;
                    let b = (p & 0xf) as u8;
                    rgba[i * 4] = (r << 4) | r;
                    rgba[i * 4 + 1] = (g << 4) | g;
                    rgba[i * 4 + 2] = (b << 4) | b;
                    rgba[i * 4 + 3] = (a << 4) | a;
                }
            }
            TexFormat::P8 => {
                if src.len() < texels {
                    return Err(DecodeError::ShortBuffer);
                }
                for (i, &idx) in src[..texels].iter().enumerate() {
                    let c = self.palette[idx as usize];
                    rgba[i * 4] = (c >> 16) as u8;
                    rgba[i * 4 + 1] = (c >> 8) as u8;
                    rgba[i * 4 + 2] = c as u8;
                    // GR_TEXFMT_P_8 is 24-bit RGB indices; the palette's high
                    // byte is unused, so treat texels as opaque.
                    rgba[i * 4 + 3] = 0xff;
                }
            }
            TexFormat::Alpha8 | TexFormat::Intensity8 => {
                if src.len() < texels {
                    return Err(DecodeError::ShortBuffer);
                }
                for (i, &v) in src[..texels].iter().enumerate() {
                    let alpha = matches!(fmt, TexFormat::Alpha8);
                    rgba[i * 4] = if alpha { 0xff } else { v };
                    rgba[i * 4 + 1] = if alpha { 0xff } else { v };
                    rgba[i * 4 + 2] = if alpha { 0xff } else { v };
                    rgba[i * 4 + 3] = if alpha { v } else { 0xff };
                }
            }
            // TODO(P4): Argb8332, AlphaIntensity88, NCC (Yiq422 / Ayiq8422).
            other => return Err(DecodeError::Unsupported(other)),
        }

        Ok(Texture {
            width,
            height,
            rgba,
        })
    }

    /// Store the 256-entry ARGB palette from `grTexDownloadTable`. Bumps the
    /// palette version when it actually changes (games upload it redundantly
    /// hundreds of times per frame). Cheap — no texture work happens here; P8
    /// textures re-decode lazily at bind time.
    pub fn set_palette(&mut self, palette: [u32; 256]) {
        if self.palette != palette {
            self.palette = palette;
            self.palette_version += 1;
        }
    }

    /// Remember a P8 texture's raw index data so it can be re-decoded when the
    /// palette changes; stamp it as current so it isn't redundantly re-decoded.
    pub fn store_p8_source(&mut self, addr: u32, width: u32, height: u32, src: Vec<u8>) {
        self.p8_sources.insert(addr, (width, height, src));
        self.p8_uploaded.insert(addr, self.palette_version);
    }

    /// If `addr` is a P8 texture whose GPU copy predates the current palette,
    /// re-decode it against the live palette and stamp it current. Returns the
    /// freshly decoded texture for the caller to upload, or `None` if it's not
    /// a palettized texture or is already up to date.
    pub fn take_p8_redecode(&mut self, addr: u32, frame: u64) -> Option<Texture> {
        let cur = self.palette_version;
        if self.p8_uploaded.get(&addr) == Some(&cur) {
            return None; // already current
        }
        if self.p8_last_frame.get(&addr) == Some(&frame) {
            return None; // already re-decoded this frame; cap the churn
        }
        let (w, h, src) = self.p8_sources.get(&addr)?.clone();
        let tex = self.decode(TexFormat::P8, w, h, &src).ok()?;
        self.p8_uploaded.insert(addr, cur);
        self.p8_last_frame.insert(addr, frame);
        Some(tex)
    }

    pub fn insert(&mut self, tmu_addr: u32, tex: Texture) {
        self.textures.insert(tmu_addr, tex);
    }

    pub fn get(&self, tmu_addr: u32) -> Option<&Texture> {
        self.textures.get(&tmu_addr)
    }
}

fn as_u16_slice(src: &[u8], texels: usize) -> Result<Vec<u16>, DecodeError> {
    if src.len() < texels * 2 {
        return Err(DecodeError::ShortBuffer);
    }
    Ok(src[..texels * 2]
        .chunks_exact(2)
        .map(|c| u16::from_le_bytes([c[0], c[1]]))
        .collect())
}

#[derive(Debug, PartialEq, Eq)]
pub enum DecodeError {
    ShortBuffer,
    Unsupported(TexFormat),
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rgb565_opaque_white() {
        let cache = TextureCache::default();
        let src = 0xffff_u16.to_le_bytes();
        let t = cache.decode(TexFormat::Rgb565, 1, 1, &src).unwrap();
        assert_eq!(t.rgba, [0xff, 0xff, 0xff, 0xff]);
    }

    #[test]
    fn argb1555_transparent_bit() {
        let cache = TextureCache::default();
        let src = 0x0000_u16.to_le_bytes(); // top bit clear => alpha 0
        let t = cache.decode(TexFormat::Argb1555, 1, 1, &src).unwrap();
        assert_eq!(t.rgba[3], 0x00);
    }

    #[test]
    fn p8_uses_palette() {
        let mut cache = TextureCache::default();
        let mut pal = [0u32; 256];
        pal[5] = 0xff_11_22_33; // A R G B
        cache.set_palette(pal);
        let t = cache.decode(TexFormat::P8, 1, 1, &[5]).unwrap();
        assert_eq!(t.rgba, [0x11, 0x22, 0x33, 0xff]);
    }

    #[test]
    fn short_buffer_is_error() {
        let cache = TextureCache::default();
        let err = cache.decode(TexFormat::Rgb565, 4, 4, &[0, 0]).err();
        assert_eq!(err, Some(DecodeError::ShortBuffer));
    }
}

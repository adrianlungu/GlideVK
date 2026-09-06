//! Linear Frame Buffer (LFB) emulation.
//!
//! `grLfbLock` hands the game a raw pointer + stride into the color or depth
//! buffer, which it then reads or writes directly (menus, software blits,
//! screenshots, procedural effects). There is no GPU equivalent of "here is a
//! pointer to the framebuffer," so we back each lock with a CPU-side staging
//! buffer:
//!
//! * **Read lock**  — copy the Vulkan color/depth image into the staging buffer
//!   (packing to the requested 16-bit format), hand out `&mut staging`.
//! * **Write lock** — hand out the staging buffer; on unlock, upload it back
//!   into the Vulkan image.
//!
//! This is the single biggest correctness *and* performance hazard in every
//! Glide wrapper, so the packing lives in safe, unit-tested Rust.

/// Pixel format a lock exposes (`GR_LFBWRITEMODE_*` / `GR_LFB_*`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LfbFormat {
    Rgb565,
    Argb1555,
    Argb8888,
    /// 16-bit depth (Z or W).
    Depth16,
}

/// An active LFB lock over a staging region.
pub struct LfbLock {
    pub format: LfbFormat,
    pub width: u32,
    pub height: u32,
    /// Bytes per row (may be padded beyond `width * bpp`).
    pub stride: u32,
    pub writable: bool,
    pub data: Vec<u8>,
}

impl LfbLock {
    /// Pack an RGBA8 source frame into this lock's 16-bit layout (read path).
    pub fn pack_from_rgba8(&mut self, rgba: &[u8]) {
        let bpp = self.bytes_per_pixel();
        for y in 0..self.height as usize {
            for x in 0..self.width as usize {
                let si = (y * self.width as usize + x) * 4;
                let di = y * self.stride as usize + x * bpp;
                let (r, g, b, a) = (rgba[si], rgba[si + 1], rgba[si + 2], rgba[si + 3]);
                match self.format {
                    LfbFormat::Rgb565 => {
                        let p = ((r as u16 >> 3) << 11) | ((g as u16 >> 2) << 5) | (b as u16 >> 3);
                        self.data[di..di + 2].copy_from_slice(&p.to_le_bytes());
                    }
                    LfbFormat::Argb1555 => {
                        let p = (((a > 127) as u16) << 15)
                            | ((r as u16 >> 3) << 10)
                            | ((g as u16 >> 3) << 5)
                            | (b as u16 >> 3);
                        self.data[di..di + 2].copy_from_slice(&p.to_le_bytes());
                    }
                    LfbFormat::Argb8888 => {
                        self.data[di..di + 4].copy_from_slice(&[b, g, r, a]);
                    }
                    LfbFormat::Depth16 => {} // color pack is a no-op for depth
                }
            }
        }
    }

    fn bytes_per_pixel(&self) -> usize {
        match self.format {
            LfbFormat::Argb8888 => 4,
            _ => 2,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pack_rgb565_red() {
        let mut lock = LfbLock {
            format: LfbFormat::Rgb565,
            width: 1,
            height: 1,
            stride: 2,
            writable: false,
            data: vec![0; 2],
        };
        lock.pack_from_rgba8(&[0xff, 0x00, 0x00, 0xff]);
        assert_eq!(u16::from_le_bytes([lock.data[0], lock.data[1]]), 0xF800);
    }
}

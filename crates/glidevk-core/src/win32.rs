//! Thin Win32 bindings the shells need outside Vulkan.
//!
//! Glide is a fullscreen API: on 3dfx hardware `grSstWinOpen` switched the
//! display to the requested mode and games size their window to the screen
//! afterwards. Under Wine a display-mode change is emulated by scaling the
//! fullscreen window and its mouse input, which is what makes "fullscreen" and
//! resolution changes work for Glide games there — the same path the D3D-based
//! wrappers get from Direct3D fullscreen.

/// `DEVMODEW` from `wingdi.h`, exact layout (220 bytes).
#[repr(C)]
pub struct DevModeW {
    pub device_name: [u16; 32],
    pub spec_version: u16,
    pub driver_version: u16,
    pub size: u16,
    pub driver_extra: u16,
    pub fields: u32,
    pub position_x: i32,
    pub position_y: i32,
    pub display_orientation: u32,
    pub display_fixed_output: u32,
    pub color: i16,
    pub duplex: i16,
    pub y_resolution: i16,
    pub tt_option: i16,
    pub collate: i16,
    pub form_name: [u16; 32],
    pub log_pixels: u16,
    pub bits_per_pel: u32,
    pub pels_width: u32,
    pub pels_height: u32,
    pub display_flags: u32,
    pub display_frequency: u32,
    pub icm_method: u32,
    pub icm_intent: u32,
    pub media_type: u32,
    pub dither_type: u32,
    pub reserved1: u32,
    pub reserved2: u32,
    pub panning_width: u32,
    pub panning_height: u32,
}

/// `sizeof(DEVMODEW)`.
pub const DEVMODEW_SIZE: usize = 220;
const DM_BITSPERPEL: u32 = 0x0004_0000;
const DM_PELSWIDTH: u32 = 0x0008_0000;
const DM_PELSHEIGHT: u32 = 0x0010_0000;
#[cfg(windows)]
const CDS_FULLSCREEN: u32 = 0x4;
#[cfg(not(windows))]
#[allow(dead_code)]
const _UNUSED_ON_HOST: (i32, u32, u32, isize, u32, u32) = (
    GWL_STYLE,
    WS_POPUP,
    WS_VISIBLE,
    HWND_TOP,
    SWP_FRAMECHANGED,
    SWP_SHOWWINDOW,
);
#[cfg(windows)]
const DISP_CHANGE_SUCCESSFUL: i32 = 0;
/// Colour depth requested with the mode; Wine's emulated modes are 32-bit.
const MODE_BITS_PER_PEL: u32 = 32;

impl DevModeW {
    /// A mode request for `width`x`height` at 32 bpp.
    pub fn mode(width: u32, height: u32) -> Self {
        // SAFETY: an all-zero DEVMODEW is a valid (empty) value.
        let mut dm: DevModeW = unsafe { core::mem::zeroed() };
        dm.size = DEVMODEW_SIZE as u16;
        dm.fields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
        dm.bits_per_pel = MODE_BITS_PER_PEL;
        dm.pels_width = width;
        dm.pels_height = height;
        dm
    }
}

#[cfg(windows)]
extern "system" {
    fn ChangeDisplaySettingsExW(
        device_name: *const u16,
        dev_mode: *mut DevModeW,
        hwnd: isize,
        flags: u32,
        param: *mut core::ffi::c_void,
    ) -> i32;
    #[cfg(target_pointer_width = "32")]
    fn SetWindowLongW(hwnd: isize, index: i32, value: i32) -> i32;
    #[cfg(target_pointer_width = "64")]
    fn SetWindowLongPtrW(hwnd: isize, index: i32, value: isize) -> isize;
    fn SetWindowPos(hwnd: isize, after: isize, x: i32, y: i32, cx: i32, cy: i32, flags: u32)
        -> i32;
}

const GWL_STYLE: i32 = -16;
const WS_POPUP: u32 = 0x8000_0000;
const WS_VISIBLE: u32 = 0x1000_0000;
const HWND_TOP: isize = 0;
const SWP_FRAMECHANGED: u32 = 0x20;
const SWP_SHOWWINDOW: u32 = 0x40;

/// Make `hwnd` a borderless popup covering the (possibly emulated) screen at
/// the origin: what a fullscreen Glide game's window must look like for Wine to
/// treat it as fullscreen and scale it. Games do this themselves on real
/// hardware only if their engine bothers (Deus Ex does not).
#[cfg(windows)]
pub fn make_fullscreen_window(hwnd: isize, width: u32, height: u32) {
    if hwnd == 0 {
        return;
    }
    let style = (WS_POPUP | WS_VISIBLE) as i32;
    // SAFETY: `hwnd` is the game's window handle passed to grSstWinOpen.
    unsafe {
        #[cfg(target_pointer_width = "32")]
        SetWindowLongW(hwnd, GWL_STYLE, style);
        #[cfg(target_pointer_width = "64")]
        SetWindowLongPtrW(hwnd, GWL_STYLE, style as isize);
        SetWindowPos(
            hwnd,
            HWND_TOP,
            0,
            0,
            width as i32,
            height as i32,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW,
        );
    }
}

/// Switch the primary display to `width`x`height` for the calling process
/// (`CDS_FULLSCREEN`: restored when the process exits or on
/// [`restore_display_mode`]). Returns the `DISP_CHANGE_*` code on failure.
#[cfg(windows)]
pub fn set_display_mode(width: u32, height: u32) -> Result<(), i32> {
    let mut dm = DevModeW::mode(width, height);
    // SAFETY: `dm` is a fully initialised DEVMODEW with a correct dmSize.
    let r = unsafe {
        ChangeDisplaySettingsExW(
            core::ptr::null(),
            &mut dm,
            0,
            CDS_FULLSCREEN,
            core::ptr::null_mut(),
        )
    };
    if r == DISP_CHANGE_SUCCESSFUL {
        Ok(())
    } else {
        Err(r)
    }
}

/// Return the display to the desktop mode.
#[cfg(windows)]
pub fn restore_display_mode() {
    // SAFETY: a null DEVMODE means "registry/default mode".
    unsafe {
        ChangeDisplaySettingsExW(
            core::ptr::null(),
            core::ptr::null_mut(),
            0,
            0,
            core::ptr::null_mut(),
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn devmodew_layout_matches_wingdi() {
        assert_eq!(core::mem::size_of::<DevModeW>(), DEVMODEW_SIZE);
        assert_eq!(core::mem::offset_of!(DevModeW, fields), 72);
        assert_eq!(core::mem::offset_of!(DevModeW, bits_per_pel), 168);
        assert_eq!(core::mem::offset_of!(DevModeW, pels_width), 172);
        assert_eq!(core::mem::offset_of!(DevModeW, pels_height), 176);
        let dm = DevModeW::mode(800, 600);
        assert_eq!(dm.size as usize, DEVMODEW_SIZE);
        assert_eq!((dm.pels_width, dm.pels_height), (800, 600));
    }
}

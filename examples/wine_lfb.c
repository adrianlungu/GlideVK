/* GlideVK LFB smoke test (P3b) — proves grLfbLock/Unlock writes reach the screen.
 *
 * Each frame: clear to blue, then LOCK the linear framebuffer and write RGB565
 * yellow over the whole surface, UNLOCK, and swap. If LFB upload works the
 * center reads yellow (the LFB write overwrote the blue clear); if it were still
 * a no-op the center would stay blue.
 *
 * Uses glide2x (has grSstScreenWidth/Height); the LFB path itself is in the
 * shared core, so this equally validates glide3x / Diablo II's LFB needs.
 *
 * Build:  i686-w64-mingw32-gcc -o wine_lfb.exe examples/wine_lfb.c -lgdi32
 */
#include <windows.h>
#include <stdio.h>

typedef struct { int size; void *lfbPtr; unsigned int strideInBytes; int writeMode; int origin; } GrLfbInfo;

typedef void(__stdcall *v_fn)(void);
typedef int(__stdcall *query_fn)(void *);
typedef void(__stdcall *sel_fn)(int);
typedef int(__stdcall *winopen_fn)(size_t, unsigned int, int, int, int, int, int);
typedef void(__stdcall *clear_fn)(unsigned int, unsigned char, unsigned int);
typedef void(__stdcall *i_fn)(int);
typedef unsigned int(__stdcall *u_fn)(void);
typedef int(__stdcall *lock_fn)(unsigned int, unsigned int, int, int, int, GrLfbInfo *);
typedef int(__stdcall *unlock_fn)(unsigned int, unsigned int);
typedef int(__stdcall *result_fn)(void);
typedef unsigned int(__stdcall *pixel_fn)(void);

#define GR_RES_640x480 5
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_LFB_WRITE_ONLY 1
#define GR_BUFFER_BACKBUFFER 1
#define GR_LFBWRITEMODE_565 0
#define FXFALSE 0

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

int main(void) {
    HMODULE dll = LoadLibraryA("glide2x.dll");
    if (!dll) { fprintf(stderr, "FAIL: load glide2x.dll (%lu)\n", GetLastError()); return 1; }

    v_fn       grGlideInit        = (v_fn)      GetProcAddress(dll, "grGlideInit");
    query_fn   grSstQueryHardware = (query_fn)  GetProcAddress(dll, "grSstQueryHardware");
    sel_fn     grSstSelect        = (sel_fn)    GetProcAddress(dll, "grSstSelect");
    winopen_fn grSstWinOpen       = (winopen_fn)GetProcAddress(dll, "grSstWinOpen");
    clear_fn   grBufferClear      = (clear_fn)  GetProcAddress(dll, "grBufferClear");
    i_fn       grBufferSwap       = (i_fn)      GetProcAddress(dll, "grBufferSwap");
    u_fn       grSstScreenWidth   = (u_fn)      GetProcAddress(dll, "grSstScreenWidth");
    u_fn       grSstScreenHeight  = (u_fn)      GetProcAddress(dll, "grSstScreenHeight");
    lock_fn    grLfbLock          = (lock_fn)   GetProcAddress(dll, "grLfbLock");
    unlock_fn  grLfbUnlock        = (unlock_fn) GetProcAddress(dll, "grLfbUnlock");
    result_fn  frameResult        = (result_fn) GetProcAddress(dll, "glidevkFrameResult");
    pixel_fn   centerPixel        = (pixel_fn)  GetProcAddress(dll, "glidevkCenterPixel");
    if (!grGlideInit || !grSstWinOpen || !grBufferClear || !grBufferSwap || !grSstScreenWidth ||
        !grSstScreenHeight || !grLfbLock || !grLfbUnlock || !frameResult || !centerPixel) {
        fprintf(stderr, "FAIL: missing exports\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GlideVKLfb";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GlideVKLfb", "GlideVK LFB", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) { fprintf(stderr, "FAIL: CreateWindow\n"); return 1; }

    grGlideInit();
    char hw[256] = {0};
    if (grSstQueryHardware) grSstQueryHardware(hw);
    if (grSstSelect) grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, 0, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }
    unsigned int w = grSstScreenWidth(), h = grSstScreenHeight();
    printf("surface %ux%u\n", w, h);

    for (int f = 0; f < 30; ++f) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        grBufferClear(0x0000FFFF, 0xff, 0xffff);   /* blue */

        GrLfbInfo info; info.size = sizeof(info);
        if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565,
                      GR_ORIGIN_UPPER_LEFT, FXFALSE, &info)) {
            unsigned short *px = (unsigned short *)info.lfbPtr;
            unsigned int stride16 = info.strideInBytes / 2;
            for (unsigned int y = 0; y < h; ++y)
                for (unsigned int x = 0; x < w; ++x)
                    px[y * stride16 + x] = 0xFFE0; /* RGB565 yellow */
            grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
        }
        grBufferSwap(1);
        Sleep(12);
    }

    int errors = frameResult();
    unsigned int c = centerPixel();
    DestroyWindow(hwnd);
    printf("center pixel = 0x%08X (expect 0xFFFF00FF yellow via LFB), present errors = %d\n", c, errors);
    if (errors == 0 && c == 0xFFFF00FFu) {
        printf("PASS: LFB write (grLfbLock/Unlock) composited to the screen\n");
        return 0;
    }
    fprintf(stderr, "FAIL\n");
    return 3;
}

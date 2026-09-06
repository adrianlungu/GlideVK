/* GlideVK glide2x grColorMask test.
 *
 * grColorMask(rgb, alpha) gates writes to the colour / alpha channels. We draw
 * a red background, then a green quad — but with RGB writes MASKED OFF, so the
 * green must be discarded and the pixel stays RED. A second band draws blue with
 * the mask back ON, which shows BLUE. So:
 *   top    band: red bg + green(masked)  -> RED   (green write blocked)
 *   bottom band: red bg + blue(unmasked) -> BLUE  (normal write)
 *
 * Capture:  GLIDEVK_CAPTURE=cmask.ppm i686 ... wine wine_colormask.exe
 * Build:    i686-w64-mingw32-gcc -o wine_colormask.exe examples/wine_colormask.c -lgdi32
 */
#include <windows.h>
#include <stdio.h>

typedef struct { float sow, tow, oow; } GrTmuVertex;
typedef struct {
    float x, y, z, r, g, b, ooz, a, oow;
    GrTmuVertex tmuvtx[2];
} GrVertex;

typedef void(__stdcall *v_fn)(void);
typedef int(__stdcall *query_fn)(void *);
typedef void(__stdcall *sel_fn)(int);
typedef int(__stdcall *winopen_fn)(size_t, unsigned int, int, int, int, int, int);
typedef void(__stdcall *clear_fn)(unsigned int, unsigned char, unsigned int);
typedef void(__stdcall *swap_fn)(int);
typedef void(__stdcall *combine_fn)(int, int, int, int, int);
typedef void(__stdcall *mask_fn)(int, int);
typedef void(__stdcall *tri_fn)(void *, void *, void *);

#define GR_RES_640x480 5
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_COMBINE_FUNCTION_LOCAL 1
#define GR_COMBINE_LOCAL_ITERATED 0
#define FXTRUE 1
#define FXFALSE 0

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

static void band(tri_fn draw, float y0, float y1, float r, float g, float b) {
    GrVertex q[4] = {0};
    float PX[4][2] = {{0, y0}, {640, y0}, {640, y1}, {0, y1}};
    for (int i = 0; i < 4; ++i) {
        q[i].x = PX[i][0]; q[i].y = PX[i][1];
        q[i].r = r; q[i].g = g; q[i].b = b; q[i].a = 255; q[i].oow = 1.0f;
    }
    draw(&q[0], &q[1], &q[2]);
    draw(&q[0], &q[2], &q[3]);
}

int main(void) {
    HMODULE dll = LoadLibraryA("glide2x.dll");
    if (!dll) { fprintf(stderr, "FAIL: no glide2x.dll\n"); return 1; }
    v_fn       grGlideInit    = (v_fn)      GetProcAddress(dll, "grGlideInit");
    query_fn   grSstQueryHardware=(query_fn)GetProcAddress(dll, "grSstQueryHardware");
    sel_fn     grSstSelect    = (sel_fn)    GetProcAddress(dll, "grSstSelect");
    winopen_fn grSstWinOpen   = (winopen_fn)GetProcAddress(dll, "grSstWinOpen");
    clear_fn   grBufferClear  = (clear_fn)  GetProcAddress(dll, "grBufferClear");
    swap_fn    grBufferSwap   = (swap_fn)   GetProcAddress(dll, "grBufferSwap");
    combine_fn grColorCombine = (combine_fn)GetProcAddress(dll, "grColorCombine");
    mask_fn    grColorMask    = (mask_fn)   GetProcAddress(dll, "grColorMask");
    tri_fn     grDrawTriangle = (tri_fn)    GetProcAddress(dll, "grDrawTriangle");
    if (!grGlideInit || !grSstWinOpen || !grColorMask || !grDrawTriangle) {
        fprintf(stderr, "FAIL: missing exports\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVKmask";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVKmask", "GlideVK colormask", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, wc.hInstance, NULL);
    grGlideInit();
    char hw[256] = {0}; grSstQueryHardware(hw); grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, 0, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, 0, GR_COMBINE_LOCAL_ITERATED, 0, 0);

    for (int f = 0; f < 20; ++f) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        grBufferClear(0x000000FFu, 0xff, 0xffff);
        /* Top: red, then green with RGB writes masked -> should stay RED. */
        grColorMask(FXTRUE, FXTRUE);
        band(grDrawTriangle, 0, 240, 255, 0, 0);
        grColorMask(FXFALSE, FXTRUE);          /* block colour writes */
        band(grDrawTriangle, 0, 240, 0, 255, 0);
        /* Bottom: red, then blue with mask back on -> should be BLUE. */
        grColorMask(FXTRUE, FXTRUE);
        band(grDrawTriangle, 240, 480, 255, 0, 0);
        band(grDrawTriangle, 240, 480, 0, 0, 255);
        grBufferSwap(1);
        Sleep(12);
    }
    DestroyWindow(hwnd);
    printf("expect: top=RED (green blocked by color mask) bottom=BLUE\n");
    return 0;
}

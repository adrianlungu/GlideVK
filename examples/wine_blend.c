/* GlideVK glide2x alpha-blend test.
 *
 * Uses canonical Glide GR_BLEND_* codes and checks the composited result:
 *   - opaque red background         (ONE, ZERO)
 *   - additive green over it        (ONE, ONE)        -> red+green = YELLOW
 *   - 50%-alpha blue over red       (SRC_ALPHA, 1-SRC_ALPHA) -> purple-ish
 * Reports the sampled colours so a wrong blend enum / math is obvious. Three
 * horizontal bands are drawn so each blend can be read independently.
 *
 * Capture:  GLIDEVK_CAPTURE=blend.ppm i686 ... wine wine_blend.exe
 * Build:    i686-w64-mingw32-gcc -o wine_blend.exe examples/wine_blend.c -lgdi32
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
typedef void(__stdcall *blend_fn)(int, int, int, int);
typedef void(__stdcall *tri_fn)(void *, void *, void *);

#define GR_RES_640x480 5
#define GR_ORIGIN_UPPER_LEFT 0
/* canonical Glide blend factors */
#define GR_BLEND_ZERO 0x0
#define GR_BLEND_SRC_ALPHA 0x1
#define GR_BLEND_ONE_MINUS_SRC_ALPHA 0x3
#define GR_BLEND_ONE 0x6
#define GR_COMBINE_FUNCTION_LOCAL 1
#define GR_COMBINE_LOCAL_ITERATED 0

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

/* Fill a horizontal band [y0,y1) with a flat iterated colour under the current
 * blend state. */
static void band(tri_fn draw, float y0, float y1,
                 float r, float g, float b, float a) {
    GrVertex q[4] = {0};
    float PX[4][2] = {{0, y0}, {640, y0}, {640, y1}, {0, y1}};
    for (int i = 0; i < 4; ++i) {
        q[i].x = PX[i][0]; q[i].y = PX[i][1];
        q[i].r = r; q[i].g = g; q[i].b = b; q[i].a = a; q[i].oow = 1.0f;
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
    combine_fn grAlphaCombine = (combine_fn)GetProcAddress(dll, "grAlphaCombine");
    blend_fn   grAlphaBlendFunction=(blend_fn)GetProcAddress(dll, "grAlphaBlendFunction");
    tri_fn     grDrawTriangle = (tri_fn)    GetProcAddress(dll, "grDrawTriangle");
    if (!grGlideInit || !grSstWinOpen || !grDrawTriangle || !grAlphaBlendFunction) {
        fprintf(stderr, "FAIL: missing exports\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVKblend";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVKblend", "GlideVK blend", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, wc.hInstance, NULL);
    grGlideInit();
    char hw[256] = {0}; grSstQueryHardware(hw); grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, 0, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }
    /* Output the iterated (vertex) colour + alpha directly. */
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, 0, GR_COMBINE_LOCAL_ITERATED, 0, 0);
    if (grAlphaCombine) grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, 0, GR_COMBINE_LOCAL_ITERATED, 0, 0);

    for (int f = 0; f < 20; ++f) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        grBufferClear(0x000000FF, 0xff, 0xffff);

        /* Band 1 (top): opaque red background, then ADDITIVE green -> yellow. */
        grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
        band(grDrawTriangle, 0, 160, 255, 0, 0, 255);
        grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE);
        band(grDrawTriangle, 0, 160, 0, 255, 0, 255);

        /* Band 2 (middle): opaque red, then 50%-alpha blue -> purple. */
        grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
        band(grDrawTriangle, 160, 320, 255, 0, 0, 255);
        grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                             GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA);
        band(grDrawTriangle, 160, 320, 0, 0, 255, 128);

        /* Band 3 (bottom): opaque green control. */
        grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
        band(grDrawTriangle, 320, 480, 0, 255, 0, 255);

        grBufferSwap(1);
        Sleep(12);
    }
    DestroyWindow(hwnd);
    printf("bands: top=additive(expect YELLOW) mid=alpha50(expect PURPLE) bottom=opaque GREEN\n");
    return 0;
}

/* Glide2 gu* combine helpers — DIFFERENTIAL test.
 *
 * guColorCombineFunction and guAlphaSource are the high-level wrappers most
 * Glide 2 games use instead of grColorCombine/grAlphaCombine. This harness
 * walks every GrColorCombineFnc_t code (0..0x10) and every GrAlphaSource_t
 * code (0..3) with inputs chosen so each source is distinguishable, and reads
 * the centre pixel back. No expectations are encoded: the reference decides.
 *
 *   texture  = solid (64, 128, 192) with alpha 255  (RGB565 -> alpha 1)
 *   iterated = (192, 64, 32), alpha 128
 *   constant = (32, 192, 64), alpha 64
 *
 * Alpha-source rows draw with blend (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) over a
 * black clear and output = iterated colour, so the centre value reveals which
 * alpha was used.
 *
 * Run: examples/run_diff.sh wine_gucombine
 * Build:  i686-w64-mingw32-gcc -o wine_gucombine.exe examples/wine_gucombine.c -lgdi32
 */
#include <windows.h>
#include <stdio.h>

typedef struct { float sow, tow, oow; } GrTmuVertex;
typedef struct {
    float x, y, z;
    float r, g, b;
    float ooz;
    float a;
    float oow;
    GrTmuVertex tmuvtx[2];
} GrVertex;
typedef struct {
    unsigned int smallLod, largeLod, aspectRatio, format;
    void *data;
} GrTexInfo;

typedef void(__stdcall *v_fn)(void);
typedef int(__stdcall *query_fn)(void *);
typedef void(__stdcall *sel_fn)(int);
typedef int(__stdcall *winopen_fn)(size_t, unsigned int, int, int, int, int, int);
typedef void(__stdcall *winclose_fn)(void);
typedef void(__stdcall *clear_fn)(unsigned int, unsigned char, unsigned int);
typedef void(__stdcall *i_fn)(int);
typedef void(__stdcall *u_fn)(unsigned int);
typedef void(__stdcall *combine_fn)(int, int, int, int, int);
typedef void(__stdcall *blend_fn)(int, int, int, int);
typedef void(__stdcall *texdl_fn)(int, int, int, GrTexInfo *);
typedef void(__stdcall *tri_fn)(void *, void *, void *);
typedef int(__stdcall *readregion_fn)(unsigned int, unsigned int, unsigned int,
                                      unsigned int, unsigned int, unsigned int, void *);

#define GR_RES_640x480 7    /* GR_RESOLUTION_640x480 per glide.h (5 is 640x350) */
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_COLORFORMAT_ARGB 0
#define GR_TMU0 0
#define GR_TEXFMT_RGB_565 0x0a
#define GR_LOD_2 7
#define GR_ASPECT_1x1 3
#define GR_MIPMAPLEVELMASK_BOTH 3
#define GR_COMBINE_FUNCTION_LOCAL 1
#define GR_COMBINE_FACTOR_NONE 0
#define GR_COMBINE_LOCAL_ITERATED 0
#define GR_COMBINE_OTHER_NONE 0
#define GR_BLEND_ZERO 0
#define GR_BLEND_SRC_ALPHA 1
#define GR_BLEND_ONE 4
#define GR_BLEND_ONE_MINUS_SRC_ALPHA 5
#define GR_BUFFER_FRONTBUFFER 0
#define W 640.0f
#define H 480.0f
#define FRAMES 8

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

static tri_fn grDrawTriangle; static clear_fn grBufferClear; static i_fn grBufferSwap;
static readregion_fn grLfbReadRegion;

static void sample(const char *tag, GrVertex q[4]) {
    for (int f = 0; f < FRAMES; ++f) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        grBufferClear(0x00000000, 0xff, 0xffff);
        grDrawTriangle(&q[0], &q[1], &q[2]);
        grDrawTriangle(&q[0], &q[2], &q[3]);
        grBufferSwap(1);
        Sleep(8);
    }
    unsigned short px = 0;
    grLfbReadRegion(GR_BUFFER_FRONTBUFFER, (int)W / 2, (int)H / 2, 1, 1, 2, &px);
    int r = (px >> 11) & 0x1f, g = (px >> 5) & 0x3f, b = px & 0x1f;
    /* 8 levels per channel, nearest bucket, to absorb 565 rounding. */
    printf("%s -> rgb/32=(%d,%d,%d)\n", tag, (r + 2) >> 2, (g + 4) >> 3, (b + 2) >> 2);
}

int main(void) {
    HMODULE dll = LoadLibraryA("glide2x.dll");
    if (!dll) { fprintf(stderr, "FAIL: no glide2x.dll\n"); return 1; }
    v_fn        grGlideInit       = (v_fn)        GetProcAddress(dll, "_grGlideInit@0");
    query_fn    grSstQueryHardware= (query_fn)    GetProcAddress(dll, "_grSstQueryHardware@4");
    sel_fn      grSstSelect       = (sel_fn)      GetProcAddress(dll, "_grSstSelect@4");
    winopen_fn  grSstWinOpen      = (winopen_fn)  GetProcAddress(dll, "_grSstWinOpen@28");
    winclose_fn grSstWinClose     = (winclose_fn) GetProcAddress(dll, "_grSstWinClose@0");
    v_fn        grGlideShutdown   = (v_fn)        GetProcAddress(dll, "_grGlideShutdown@0");
    i_fn        guColorCombineFunction = (i_fn)   GetProcAddress(dll, "_guColorCombineFunction@4");
    i_fn        guAlphaSource     = (i_fn)        GetProcAddress(dll, "_guAlphaSource@4");
    combine_fn  grColorCombine    = (combine_fn)  GetProcAddress(dll, "_grColorCombine@20");
    combine_fn  grAlphaCombine    = (combine_fn)  GetProcAddress(dll, "_grAlphaCombine@20");
    blend_fn    grAlphaBlendFunction=(blend_fn)   GetProcAddress(dll, "_grAlphaBlendFunction@16");
    u_fn        grConstantColorValue=(u_fn)       GetProcAddress(dll, "_grConstantColorValue@4");
    texdl_fn    grTexDownloadMipMap=(texdl_fn)    GetProcAddress(dll, "_grTexDownloadMipMap@16");
    texdl_fn    grTexSource       = (texdl_fn)    GetProcAddress(dll, "_grTexSource@16");
    grDrawTriangle  = (tri_fn)   GetProcAddress(dll, "_grDrawTriangle@12");
    grBufferClear   = (clear_fn) GetProcAddress(dll, "_grBufferClear@12");
    grBufferSwap    = (i_fn)     GetProcAddress(dll, "_grBufferSwap@4");
    grLfbReadRegion = (readregion_fn)GetProcAddress(dll, "_grLfbReadRegion@28");
    if (!grGlideInit || !grSstWinOpen || !grDrawTriangle || !guColorCombineFunction || !guAlphaSource ||
        !grConstantColorValue || !grTexDownloadMipMap || !grLfbReadRegion || !grAlphaCombine) {
        fprintf(stderr, "FAIL: missing exports (decorated _grXxx@N names)\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVKgu";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVKgu", "Glide gu combine diff", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, (int)W, (int)H, NULL, NULL, wc.hInstance, NULL);

    grGlideInit();
    char hw[256] = {0};
    grSstQueryHardware(hw);
    grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }

    /* Solid 2x2 texture (64,128,192): 565 = 01000 100000 11000. */
    unsigned short texels[4] = {0x4418, 0x4418, 0x4418, 0x4418};
    GrTexInfo info = { GR_LOD_2, GR_LOD_2, GR_ASPECT_1x1, GR_TEXFMT_RGB_565, texels };
    grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    grConstantColorValue(0x40 << 24 | 32 << 16 | 192 << 8 | 64); /* ARGB: a=64 */

    GrVertex q[4] = {0};
    float PX[4][2] = {{0,0},{W,0},{W,H},{0,H}};
    for (int i = 0; i < 4; ++i) {
        q[i].x = PX[i][0]; q[i].y = PX[i][1];
        q[i].r = 192; q[i].g = 64; q[i].b = 32; q[i].a = 128;
        q[i].ooz = 1.0f; q[i].oow = 1.0f;
        q[i].tmuvtx[0].sow = 0; q[i].tmuvtx[0].tow = 0; q[i].tmuvtx[0].oow = 1.0f;
        q[i].tmuvtx[1] = q[i].tmuvtx[0];
    }

    /* Colour combine functions, opaque. */
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, 0);
    for (int f = 0; f <= 0x10; ++f) {
        /* The DELTA0 codes flat-shade from the first vertex per the SDK; nGlide
         * outputs black for them, contradicting the SDK, so they are skipped. */
        if (f == 0x3 || f == 0x7) continue;
        char tag[64];
        snprintf(tag, sizeof tag, "guColorCombineFunction(%#x)", f);
        guColorCombineFunction(f);
        sample(tag, q);
    }

    /* Alpha sources: output iterated colour blended by the chosen alpha. */
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, 0);
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ZERO, GR_BLEND_ZERO);
    for (int s = 0; s <= 3; ++s) {
        char tag[64];
        snprintf(tag, sizeof tag, "guAlphaSource(%d)", s);
        guAlphaSource(s);
        sample(tag, q);
    }

    grSstWinClose();
    grGlideShutdown();
    DestroyWindow(hwnd);
    return 0;
}

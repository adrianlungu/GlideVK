/* Glide3 palette timing — DIFFERENTIAL test (glide3x).
 *
 * Glide's TMU applies its palette LUT when drawing, so a P8 texture must
 * pick up a palette uploaded AFTER grTexSource — which is exactly the order
 * Unreal uses. This harness encodes no expectation: run it beside a reference
 * glide3x.dll and beside GlideVK's and diff:
 *
 *   GLIDE_API=glide3x examples/run_diff.sh wine3_palette
 *
 *   A: download P8 (all index 7), palette with [7]=red, bind, palette with
 *      [7]=green, draw            -> the reference says which palette applies
 *   B: palette [7]=red again, draw, no rebind
 *   C: palette [7]=blue, draw
 *
 * Reads back via grLfbReadRegion. Symbols are looked up undecorated first,
 * then as _name@N (nGlide's glide3x exports the decorated form).
 *
 * Build:  i686-w64-mingw32-gcc -o wine3_palette.exe examples/wine3_palette.c -lgdi32
 */
#include <windows.h>
#include <stdio.h>

typedef struct { float x, y, z; float r, g, b, a; float s, t; } Vtx;
typedef struct {
    int smallLodLog2, largeLodLog2, aspectRatioLog2, format;
    void *data;
} GrTexInfo;

typedef void(__stdcall *v_fn)(void);
typedef unsigned int(__stdcall *winopen_fn)(size_t, unsigned int, int, int, int, int, int);
typedef void(__stdcall *close_fn)(unsigned int);
typedef void(__stdcall *clear_fn)(unsigned int, unsigned char, unsigned int);
typedef void(__stdcall *i_fn)(int);
typedef void(__stdcall *vlayout_fn)(int, int, int);
typedef void(__stdcall *tri_fn)(const void *, const void *, const void *);
typedef void(__stdcall *combine_fn)(int, int, int, int, int);
typedef void(__stdcall *texdl_fn)(int, unsigned int, int, GrTexInfo *);
typedef void(__stdcall *table_fn)(int, void *);
typedef int(__stdcall *readregion_fn)(int, unsigned int, unsigned int, unsigned int,
                                      unsigned int, unsigned int, void *);

#define GR_RESOLUTION_640x480 7    /* per glide.h (5 is 640x350) */
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_PARAM_XY 0x01
#define GR_PARAM_Z 0x02
#define GR_PARAM_A 0x06
#define GR_PARAM_RGB 0x07
#define GR_PARAM_ST0 0x10
#define GR_PARAM_ENABLE 1
#define GR_COMBINE_FUNCTION_SCALE_OTHER 3
#define GR_COMBINE_FACTOR_ONE 8
#define GR_COMBINE_LOCAL_ITERATED 0
#define GR_COMBINE_OTHER_TEXTURE 1
#define GR_TMU0 0
#define GR_TEXFMT_P_8 0x5
#define GR_LOD_LOG2_32 5
#define GR_ASPECT_LOG2_1x1 0
#define GR_MIPMAPLEVELMASK_BOTH 3
#define GR_TEXTABLE_PALETTE 2
#define GR_BUFFER_FRONTBUFFER 0
#define ST_MAX 256.0f
#define W 640.0f
#define H 480.0f
#define FRAMES 12
#define INDEX 7

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

static FARPROC sym(HMODULE dll, const char *name, int argbytes) {
    FARPROC p = GetProcAddress(dll, name);
    if (p) return p;
    char dec[128];
    snprintf(dec, sizeof dec, "_%s@%d", name, argbytes);
    return GetProcAddress(dll, dec);
}

static const char *classify(unsigned short p) {
    int r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
    int R = r > 15, G = g > 31, B = b > 15;
    if (R && G && B) return "white";
    if (R && !G && !B) return "red";
    if (!R && G && !B) return "green";
    if (!R && !G && B) return "blue";
    if (!R && !G && !B) return "black";
    return "mixed";
}

static tri_fn grDrawTriangle; static clear_fn grBufferClear; static i_fn grBufferSwap;
static readregion_fn grLfbReadRegion;

static void draw_and_sample(HWND hwnd, Vtx q[4], const char *tag) {
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
    printf("%s: center=%s\n", tag, classify(px));
}

int main(void) {
    HMODULE dll = LoadLibraryA("glide3x.dll");
    if (!dll) { fprintf(stderr, "FAIL: no glide3x.dll\n"); return 1; }
    v_fn       grGlideInit     = (v_fn)      sym(dll, "grGlideInit", 0);
    winopen_fn grSstWinOpen    = (winopen_fn)sym(dll, "grSstWinOpen", 28);
    close_fn   grSstWinClose   = (close_fn)  sym(dll, "grSstWinClose", 4);
    v_fn       grGlideShutdown = (v_fn)      sym(dll, "grGlideShutdown", 0);
    vlayout_fn grVertexLayout  = (vlayout_fn)sym(dll, "grVertexLayout", 12);
    combine_fn grColorCombine  = (combine_fn)sym(dll, "grColorCombine", 20);
    texdl_fn   grTexDownloadMipMap = (texdl_fn)sym(dll, "grTexDownloadMipMap", 16);
    texdl_fn   grTexSource     = (texdl_fn)  sym(dll, "grTexSource", 16);
    table_fn   grTexDownloadTable = (table_fn)sym(dll, "grTexDownloadTable", 8);
    grDrawTriangle  = (tri_fn)   sym(dll, "grDrawTriangle", 12);
    grBufferClear   = (clear_fn) sym(dll, "grBufferClear", 12);
    grBufferSwap    = (i_fn)     sym(dll, "grBufferSwap", 4);
    grLfbReadRegion = (readregion_fn)sym(dll, "grLfbReadRegion", 28);
    if (!grGlideInit || !grSstWinOpen || !grVertexLayout || !grColorCombine || !grTexDownloadMipMap ||
        !grTexSource || !grTexDownloadTable || !grDrawTriangle || !grBufferClear || !grBufferSwap ||
        !grLfbReadRegion) {
        fprintf(stderr, "FAIL: missing exports\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVK3pal";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVK3pal", "Glide3 palette diff", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, (int)W, (int)H, NULL, NULL, wc.hInstance, NULL);

    grGlideInit();
    if (!grSstWinOpen((size_t)hwnd, GR_RESOLUTION_640x480, -1, 0, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }
    grVertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
    grVertexLayout(GR_PARAM_Z, 8, GR_PARAM_ENABLE);
    grVertexLayout(GR_PARAM_RGB, 12, GR_PARAM_ENABLE);
    grVertexLayout(GR_PARAM_A, 24, GR_PARAM_ENABLE);
    grVertexLayout(GR_PARAM_ST0, 28, GR_PARAM_ENABLE);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);

    Vtx q[4] = {0};
    float PX[4][2] = {{0,0},{W,0},{W,H},{0,H}};
    float ST[4][2] = {{0,0},{ST_MAX,0},{ST_MAX,ST_MAX},{0,ST_MAX}};
    for (int i = 0; i < 4; ++i) {
        q[i].x = PX[i][0]; q[i].y = PX[i][1]; q[i].z = 0;
        q[i].r = q[i].g = q[i].b = q[i].a = 255;
        q[i].s = ST[i][0]; q[i].t = ST[i][1];
    }

    static unsigned char indices[32 * 32];
    memset(indices, INDEX, sizeof indices);
    GrTexInfo info = { GR_LOD_LOG2_32, GR_LOD_LOG2_32, GR_ASPECT_LOG2_1x1, GR_TEXFMT_P_8, indices };
    static unsigned int pal[256];

    /* A: texture, palette(red), bind, palette(green), draw. */
    grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    memset(pal, 0, sizeof pal); pal[INDEX] = 0x00FF0000;
    grTexDownloadTable(GR_TEXTABLE_PALETTE, pal);
    grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    memset(pal, 0, sizeof pal); pal[INDEX] = 0x0000FF00;
    grTexDownloadTable(GR_TEXTABLE_PALETTE, pal);
    draw_and_sample(hwnd, q, "A palette uploaded after bind (red then green)");

    /* B: back to red without rebinding. */
    memset(pal, 0, sizeof pal); pal[INDEX] = 0x00FF0000;
    grTexDownloadTable(GR_TEXTABLE_PALETTE, pal);
    draw_and_sample(hwnd, q, "B palette red again, no rebind");

    /* C: blue. */
    memset(pal, 0, sizeof pal); pal[INDEX] = 0x000000FF;
    grTexDownloadTable(GR_TEXTABLE_PALETTE, pal);
    draw_and_sample(hwnd, q, "C palette blue, no rebind");

    grSstWinClose(1);
    grGlideShutdown();
    DestroyWindow(hwnd);
    return 0;
}

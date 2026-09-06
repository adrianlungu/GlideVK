/* GlideVK glide2x chroma-key test.
 *
 * Chroma-keying makes a chosen texel colour transparent (the background shows
 * through) — the classic Glide way to cut out sprites/decals. We draw a quad
 * textured with a 2x2 texture: two texels magenta (the key), two green. With
 * chroma-key = magenta over a blue background, the magenta texels must drop out
 * to blue and the green texels stay green.
 *
 *   TL magenta -> BLUE (keyed)     TR green -> GREEN
 *   BL green   -> GREEN            BR magenta -> BLUE (keyed)
 *
 * Capture:  GLIDEVK_CAPTURE=chroma.ppm i686 ... wine wine_chroma.exe
 * Build:    i686-w64-mingw32-gcc -o wine_chroma.exe examples/wine_chroma.c -lgdi32
 */
#include <windows.h>
#include <stdio.h>

typedef struct { float sow, tow, oow; } GrTmuVertex;
typedef struct {
    float x, y, z, r, g, b, ooz, a, oow;
    GrTmuVertex tmuvtx[2];
} GrVertex;
typedef struct { unsigned int smallLod, largeLod, aspectRatio, format; void *data; } GrTexInfo;

typedef void(__stdcall *v_fn)(void);
typedef int(__stdcall *query_fn)(void *);
typedef void(__stdcall *sel_fn)(int);
typedef int(__stdcall *winopen_fn)(size_t, unsigned int, int, int, int, int, int);
typedef void(__stdcall *clear_fn)(unsigned int, unsigned char, unsigned int);
typedef void(__stdcall *swap_fn)(int);
typedef void(__stdcall *combine_fn)(int, int, int, int, int);
typedef void(__stdcall *texdl_fn)(int, int, int, GrTexInfo *);
typedef void(__stdcall *i_fn)(int);
typedef void(__stdcall *u_fn)(unsigned int);
typedef void(__stdcall *tri_fn)(void *, void *, void *);

#define GR_RES_640x480 5
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_LOD_2 7
#define GR_ASPECT_1x1 3
#define GR_TEXFMT_RGB_565 0x0a
#define GR_TMU0 0
#define GR_MIPMAPLEVELMASK_BOTH 3
#define GR_COMBINE_FUNCTION_SCALE_OTHER 3
#define GR_COMBINE_FACTOR_ONE 8
#define GR_COMBINE_LOCAL_ITERATED 0
#define GR_COMBINE_OTHER_TEXTURE 1
#define GR_CHROMAKEY_ENABLE 1

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
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
    texdl_fn   grTexDownloadMipMap=(texdl_fn)GetProcAddress(dll, "grTexDownloadMipMap");
    texdl_fn   grTexSource    = (texdl_fn)  GetProcAddress(dll, "grTexSource");
    i_fn       grChromakeyMode= (i_fn)      GetProcAddress(dll, "grChromakeyMode");
    u_fn       grChromakeyValue=(u_fn)      GetProcAddress(dll, "grChromakeyValue");
    tri_fn     grDrawTriangle = (tri_fn)    GetProcAddress(dll, "grDrawTriangle");
    if (!grGlideInit || !grSstWinOpen || !grChromakeyMode || !grChromakeyValue) {
        fprintf(stderr, "FAIL: missing exports\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVKchroma";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVKchroma", "GlideVK chroma", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, wc.hInstance, NULL);
    grGlideInit();
    char hw[256] = {0}; grSstQueryHardware(hw); grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, 0, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }

    /* 2x2 RGB565: TL magenta, TR green, BL green, BR magenta. */
    unsigned short texels[4] = {0xF81F, 0x07E0, 0x07E0, 0xF81F};
    GrTexInfo info;
    info.smallLod = GR_LOD_2; info.largeLod = GR_LOD_2;
    info.aspectRatio = GR_ASPECT_1x1; info.format = GR_TEXFMT_RGB_565; info.data = texels;
    grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);

    /* Key out magenta. Default colour format is ARGB (0x00RRGGBB). */
    grChromakeyValue(0x00FF00FFu);
    grChromakeyMode(GR_CHROMAKEY_ENABLE);

    const float W = 640, H = 480, T = 2.0f;
    GrVertex q[4] = {0};
    float PX[4][2] = {{0,0},{W,0},{W,H},{0,H}};
    float ST[4][2] = {{0,0},{T,0},{T,T},{0,T}};
    for (int i = 0; i < 4; ++i) {
        q[i].x = PX[i][0]; q[i].y = PX[i][1];
        q[i].r = q[i].g = q[i].b = q[i].a = 255; q[i].oow = 1.0f;
        q[i].tmuvtx[0].sow = ST[i][0]; q[i].tmuvtx[0].tow = ST[i][1]; q[i].tmuvtx[0].oow = 1.0f;
    }
    for (int f = 0; f < 20; ++f) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        grBufferClear(0x000000FFu, 0xff, 0xffff);   /* blue background */
        grDrawTriangle(&q[0], &q[1], &q[2]);
        grDrawTriangle(&q[0], &q[2], &q[3]);
        grBufferSwap(1);
        Sleep(12);
    }
    DestroyWindow(hwnd);
    printf("expect: TL/BR=BLUE (magenta keyed out) TR/BL=GREEN\n");
    return 0;
}

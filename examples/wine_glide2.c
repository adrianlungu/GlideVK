/* GlideVK glide2x smoke test — proves the Glide 2.x shell renders.
 *
 * Exercises the Glide2 API (fixed GrVertex, grSstQueryHardware, inverted
 * texture LOD, modulate combine) end to end: uploads an 8x8 yellow texture and
 * draws a screen-covering triangle textured with iterated white -> yellow.
 * Verified via center-pixel readback (0xFFFF00FF).
 *
 * Build:  i686-w64-mingw32-gcc -o wine_glide2.exe examples/wine_glide2.c -lgdi32
 */
#include <windows.h>
#include <stdio.h>

/* Glide2 fixed vertex (GLIDE_NUM_TMU = 2 -> sizeof 60). */
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
    const void *data;
} GrTexInfo;

typedef void(__stdcall *v_fn)(void);
typedef int(__stdcall *query_fn)(void *);
typedef void(__stdcall *sel_fn)(int);
typedef int(__stdcall *winopen_fn)(size_t, unsigned int, int, int, int, int, int);
typedef void(__stdcall *clear_fn)(unsigned int, unsigned char, unsigned int);
typedef void(__stdcall *i_fn)(int);
typedef void(__stdcall *combine_fn)(int, int, int, int, int);
typedef void(__stdcall *texdl_fn)(unsigned int, unsigned int, unsigned int, const void *);
typedef void(__stdcall *tri_fn)(const void *, const void *, const void *);
typedef int(__stdcall *result_fn)(void);
typedef unsigned int(__stdcall *pixel_fn)(void);

#define GR_RES_640x480 7    /* GR_RESOLUTION_640x480 per glide.h (5 is 640x350) */
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_TMU0 0
#define GR_TEXFMT_RGB_565 0x0a
#define GR_LOD_8 5          /* inverted: 256=0 .. 8=5 */
#define GR_ASPECT_1x1 3
#define GR_MIPMAPLEVELMASK_BOTH 0x03
#define GR_COMBINE_FUNCTION_SCALE_OTHER 0x03
#define GR_COMBINE_FACTOR_LOCAL 0x01
#define GR_COMBINE_LOCAL_ITERATED 0x00
#define GR_COMBINE_OTHER_TEXTURE 0x01

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
    combine_fn grColorCombine     = (combine_fn)GetProcAddress(dll, "grColorCombine");
    texdl_fn   grTexDownloadMipMap= (texdl_fn)  GetProcAddress(dll, "grTexDownloadMipMap");
    texdl_fn   grTexSource        = (texdl_fn)  GetProcAddress(dll, "grTexSource");
    tri_fn     grDrawTriangle     = (tri_fn)    GetProcAddress(dll, "grDrawTriangle");
    result_fn  frameResult        = (result_fn) GetProcAddress(dll, "glidevkFrameResult");
    pixel_fn   centerPixel        = (pixel_fn)  GetProcAddress(dll, "glidevkCenterPixel");
    if (!grGlideInit || !grSstQueryHardware || !grSstSelect || !grSstWinOpen || !grBufferClear ||
        !grBufferSwap || !grColorCombine || !grTexDownloadMipMap || !grTexSource ||
        !grDrawTriangle || !frameResult || !centerPixel) {
        fprintf(stderr, "FAIL: missing exports\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GlideVK2";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GlideVK2", "GlideVK glide2x", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) { fprintf(stderr, "FAIL: CreateWindow\n"); return 1; }

    /* Glide2 init sequence */
    grGlideInit();
    char hw[256] = {0};
    grSstQueryHardware(hw);
    printf("grSstQueryHardware -> num_sst=%d type=%d nTexelfx=%d\n",
           ((int *)hw)[0], ((int *)hw)[1], ((int *)hw)[4]);
    grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, 0, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen returned 0\n"); return 2;
    }


    /* 8x8 uniformly-yellow RGB565 texture (0xFFE0) */
    unsigned short texels[64];
    for (int i = 0; i < 64; ++i) texels[i] = 0xFFE0;
    GrTexInfo info;
    info.smallLod = GR_LOD_8; info.largeLod = GR_LOD_8;
    info.aspectRatio = GR_ASPECT_1x1; info.format = GR_TEXFMT_RGB_565; info.data = texels;
    grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);

    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);

    /* White iterated triangle covering the center; ST spans the texture. */
    GrVertex v[3] = {0};
    float P[3][4] = {{0,0,0,0}, {1280,0,2,0}, {0,960,0,2}};
    for (int i = 0; i < 3; ++i) {
        v[i].x = P[i][0]; v[i].y = P[i][1];
        v[i].r = 255; v[i].g = 255; v[i].b = 255; v[i].a = 255;
        v[i].tmuvtx[0].sow = P[i][2]; v[i].tmuvtx[0].tow = P[i][3];
    }

    for (int f = 0; f < 30; ++f) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        grBufferClear(0x0000FFFF, 0xff, 0xffff);
        grDrawTriangle(&v[0], &v[1], &v[2]);
        grBufferSwap(1);
        Sleep(12);
    }

    int errors = frameResult();
    unsigned int px = centerPixel();
    DestroyWindow(hwnd);
    printf("center pixel = 0x%08X (expect 0xFFFF00FF yellow), present errors = %d\n", px, errors);
    if (errors == 0 && px == 0xFFFF00FFu) {
        printf("PASS: glide2x textured triangle (fixed GrVertex, modulate) rendered\n");
        return 0;
    }
    fprintf(stderr, "FAIL\n");
    return 3;
}

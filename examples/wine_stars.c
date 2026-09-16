/* GlideVK glide2x demo: an additive-blended starfield warp tunnel.
 *
 * Hundreds of glowing sprites fly toward the camera and respawn at the back — a
 * staple 3dfx effect. Each star is a radial-glow texture drawn as a screen quad
 * sized and brightened by 1/z (perspective), composited with ADDITIVE blending
 * (GR_BLEND_ONE, GR_BLEND_ONE) so overlapping glows accumulate. Exercises
 * additive transparency, per-sprite iterated colour, and perspective scaling.
 *
 * Run and watch, or dump a frame:
 *   GLIDEVK_CAPTURE=stars.ppm GLIDEVK_CAPTURE_FRAME=80 wine wine_stars.exe
 * Build:  i686-w64-mingw32-gcc -O2 -o wine_stars.exe examples/wine_stars.c -lgdi32 -lm
 */
#include <windows.h>
#include <math.h>
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
typedef void(__stdcall *blend_fn)(int, int, int, int);
typedef void(__stdcall *texdl_fn)(int, int, int, GrTexInfo *);
typedef void(__stdcall *tri_fn)(void *, void *, void *);

#define GR_RES_640x480 7    /* GR_RESOLUTION_640x480 per glide.h (5 is 640x350) */
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_LOD_32 3
#define GR_ASPECT_1x1 3
#define GR_TEXFMT_RGB_565 0x0a
#define GR_TMU0 0
#define GR_MIPMAPLEVELMASK_BOTH 3
#define GR_COMBINE_FUNCTION_SCALE_OTHER 3
#define GR_COMBINE_FACTOR_LOCAL 1
#define GR_COMBINE_LOCAL_ITERATED 0
#define GR_COMBINE_OTHER_TEXTURE 1
#define GR_BLEND_ONE 0x4
#define NSTARS 260
#define TW 32

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
    blend_fn   grAlphaBlendFunction=(blend_fn)GetProcAddress(dll, "grAlphaBlendFunction");
    texdl_fn   grTexDownloadMipMap=(texdl_fn)GetProcAddress(dll, "grTexDownloadMipMap");
    texdl_fn   grTexSource    = (texdl_fn)  GetProcAddress(dll, "grTexSource");
    tri_fn     grDrawTriangle = (tri_fn)    GetProcAddress(dll, "grDrawTriangle");
    if (!grGlideInit || !grSstWinOpen || !grDrawTriangle || !grAlphaBlendFunction) {
        fprintf(stderr, "FAIL: missing exports\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVKstars";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVKstars", "GlideVK stars", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, wc.hInstance, NULL);
    grGlideInit();
    char hw[256] = {0}; grSstQueryHardware(hw); grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, 0, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }

    /* 32x32 radial glow (bright centre -> black edge), RGB565 white. */
    static unsigned short tex[TW*TW];
    for (int y = 0; y < TW; ++y) for (int x = 0; x < TW; ++x) {
        float dx = (x - TW/2 + 0.5f) / (TW/2), dy = (y - TW/2 + 0.5f) / (TW/2);
        float d = 1.0f - sqrtf(dx*dx + dy*dy);
        if (d < 0) d = 0; d *= d;
        int c = (int)(d * 31.0f);
        tex[y*TW+x] = (c << 11) | ((c*2) << 5) | c;  /* white-ish */
    }
    GrTexInfo info = { GR_LOD_32, GR_LOD_32, GR_ASPECT_1x1, GR_TEXFMT_RGB_565, tex };
    grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE);

    /* Deterministic star field (no RNG): spiral spread. */
    static float sx[NSTARS], sy[NSTARS], sz[NSTARS], sc[NSTARS][3];
    for (int i = 0; i < NSTARS; ++i) {
        float a = i * 2.39996323f, rad = 0.3f + fmodf(i * 0.37f, 3.0f);
        sx[i] = cosf(a) * rad; sy[i] = sinf(a) * rad;
        sz[i] = 0.4f + fmodf(i * 0.613f, 7.6f);
        float t = fmodf(i * 0.111f, 1.0f);   /* per-star tint */
        sc[i][0] = 0.6f + 0.4f*t; sc[i][1] = 0.7f; sc[i][2] = 1.0f - 0.4f*t;
    }

    const float W = 640, H = 480, FOC = 300.0f;
    for (int frame = 0; frame < 100000; ++frame) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        grBufferClear(0x00000000u, 0xff, 0xffff);
        for (int i = 0; i < NSTARS; ++i) {
            sz[i] -= 0.04f;
            if (sz[i] < 0.3f) sz[i] += 8.0f;
            float oow = 1.0f / sz[i];
            float px = W*0.5f + sx[i]*FOC*oow;
            float py = H*0.5f - sy[i]*FOC*oow;
            float sz2 = 26.0f * oow;                 /* sprite half-size */
            float br = oow * 1.4f; if (br > 1.0f) br = 1.0f;
            GrVertex v[4];
            float P[4][2] = {{px-sz2,py-sz2},{px+sz2,py-sz2},{px+sz2,py+sz2},{px-sz2,py+sz2}};
            float S[4][2] = {{0,0},{TW,0},{TW,TW},{0,TW}};
            for (int k = 0; k < 4; ++k) {
                v[k].x = P[k][0]; v[k].y = P[k][1]; v[k].z = 0; v[k].ooz = 0; v[k].oow = 1.0f;
                v[k].r = sc[i][0]*br*255; v[k].g = sc[i][1]*br*255; v[k].b = sc[i][2]*br*255; v[k].a = 255;
                v[k].tmuvtx[0].sow = S[k][0]; v[k].tmuvtx[0].tow = S[k][1]; v[k].tmuvtx[0].oow = 1.0f;
            }
            grDrawTriangle(&v[0], &v[1], &v[2]);
            grDrawTriangle(&v[0], &v[2], &v[3]);
        }
        grBufferSwap(1);
        Sleep(10);
    }
done:
    DestroyWindow(hwnd);
    return 0;
}

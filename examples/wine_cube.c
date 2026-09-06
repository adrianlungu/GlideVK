/* GlideVK glide2x demo: a spinning, lit, texture-mapped cube.
 *
 * A classic 3dfx-style demo. Glide is a rasteriser, not a T&L engine, so the
 * demo does the 3D itself — rotate the cube, perspective-project each vertex to
 * screen space, and hand Glide `sow=s/w, tow=t/w, oow=1/w` for perspective-
 * correct texturing plus `ooz=65535/z` for the Z-buffer. Each face is flat-lit
 * by its rotated normal and its texture is modulated by that shade
 * (texture * iterated), so it reads as a solid, lit, spinning cube.
 *
 * Run it and watch (it animates); or dump a frame:
 *   GLIDEVK_CAPTURE=cube.ppm GLIDEVK_CAPTURE_FRAME=40 wine wine_cube.exe
 * Build:  i686-w64-mingw32-gcc -O2 -o wine_cube.exe examples/wine_cube.c -lgdi32 -lm
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
typedef void(__stdcall *texdl_fn)(int, int, int, GrTexInfo *);
typedef void(__stdcall *i_fn)(int);
typedef void(__stdcall *tri_fn)(void *, void *, void *);

#define GR_RES_640x480 5
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_LOD_64 2
#define GR_ASPECT_1x1 3
#define GR_TEXFMT_RGB_565 0x0a
#define GR_TMU0 0
#define GR_MIPMAPLEVELMASK_BOTH 3
#define GR_COMBINE_FUNCTION_SCALE_OTHER 3
#define GR_COMBINE_FACTOR_LOCAL 1
#define GR_COMBINE_LOCAL_ITERATED 0
#define GR_COMBINE_OTHER_TEXTURE 1
#define GR_DEPTHBUFFER_ZBUFFER 1
#define GR_CMP_LESS 1
#define TW 64

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

/* 8 corners, 6 faces (4 corner indices each), 6 face normals. */
static const float CORNER[8][3] = {
    {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
};
static const int FACE[6][4] = {
    {4,5,6,7},{1,0,3,2},{5,1,2,6},{0,4,7,3},{7,6,2,3},{0,1,5,4}
};
static const float NORMAL[6][3] = {
    {0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}
};

static void rot(const float p[3], float cx, float sx, float cy, float sy, float o[3]) {
    float x=p[0], y=p[1], z=p[2];
    float x1 =  x*cy + z*sy;
    float z1 = -x*sy + z*cy;
    o[0] = x1;
    o[1] = y*cx - z1*sx;
    o[2] = y*sx + z1*cx;
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
    i_fn       grDepthBufferMode=(i_fn)     GetProcAddress(dll, "grDepthBufferMode");
    i_fn       grDepthBufferFunction=(i_fn) GetProcAddress(dll, "grDepthBufferFunction");
    i_fn       grDepthMask    = (i_fn)      GetProcAddress(dll, "grDepthMask");
    tri_fn     grDrawTriangle = (tri_fn)    GetProcAddress(dll, "grDrawTriangle");
    if (!grGlideInit || !grSstWinOpen || !grDrawTriangle) {
        fprintf(stderr, "FAIL: missing exports\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVKcube";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVKcube", "GlideVK cube", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, wc.hInstance, NULL);
    grGlideInit();
    char hw[256] = {0}; grSstQueryHardware(hw); grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, 0, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }

    /* 64x64 checkerboard texture (cyan / magenta blocks). */
    static unsigned short tex[TW*TW];
    for (int y = 0; y < TW; ++y)
        for (int x = 0; x < TW; ++x)
            tex[y*TW+x] = ((x>>3) ^ (y>>3)) & 1 ? 0x07FF /*cyan*/ : 0xF81F /*magenta*/;
    GrTexInfo info = { GR_LOD_64, GR_LOD_64, GR_ASPECT_1x1, GR_TEXFMT_RGB_565, tex };
    grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    /* out = texture * iterated(shade). */
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
    if (grDepthBufferMode) { grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
                             grDepthBufferFunction(GR_CMP_LESS); grDepthMask(1); }

    const float W = 640, H = 480, FOC = 380.0f, DIST = 4.5f;
    const float ST[4][2] = {{0,0},{TW,0},{TW,TW},{0,TW}};
    float L[3] = {0.40f, 0.55f, -0.73f};  /* light dir (normalised-ish) */

    for (int frame = 0; frame < 100000; ++frame) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        float ax = frame * 0.021f, ay = frame * 0.033f;
        float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay);

        grBufferClear(0x101018FFu, 0xff, 0xffff);   /* dark blue-grey, depth far */

        for (int f = 0; f < 6; ++f) {
            float n[3]; rot(NORMAL[f], cx, sx, cy, sy, n);
            float shade = n[0]*L[0] + n[1]*L[1] + n[2]*L[2];
            if (shade < 0) shade = 0;
            shade = 0.25f + 0.75f*shade;            /* ambient + diffuse */
            float col = shade * 255.0f;

            GrVertex v[4];
            for (int i = 0; i < 4; ++i) {
                float p[3]; rot(CORNER[FACE[f][i]], cx, sx, cy, sy, p);
                float z = p[2] + DIST;
                float oow = 1.0f / z;
                v[i].x = W*0.5f + p[0]*FOC*oow;
                v[i].y = H*0.5f - p[1]*FOC*oow;
                v[i].z = 0; v[i].ooz = 65535.0f * oow; v[i].oow = oow;
                v[i].r = v[i].g = v[i].b = col; v[i].a = 255;
                v[i].tmuvtx[0].sow = ST[i][0]*oow;
                v[i].tmuvtx[0].tow = ST[i][1]*oow;
                v[i].tmuvtx[0].oow = oow;
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

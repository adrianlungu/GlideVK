/* GlideVK glide2x demo: a spinning Gouraud-lit donut (torus).
 *
 * The iconic 3dfx demo. A tessellated torus is rotated, perspective-projected,
 * and smoothly shaded by lighting each vertex with its surface normal (Gouraud):
 * the per-vertex colour interpolates across each triangle, so the lit highlight
 * glides over the surface. Depth-buffered, untextured — a pure iterated-colour /
 * Z-buffer showcase (complements the textured cube and additive starfield).
 *
 * Run and watch, or dump a frame:
 *   GLIDEVK_CAPTURE=donut.ppm GLIDEVK_CAPTURE_FRAME=50 wine wine_donut.exe
 * Build:  i686-w64-mingw32-gcc -O2 -o wine_donut.exe examples/wine_donut.c -lgdi32 -lm
 */
#include <windows.h>
#include <math.h>
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
typedef void(__stdcall *i_fn)(int);
typedef void(__stdcall *tri_fn)(void *, void *, void *);

#define GR_RES_640x480 7    /* GR_RESOLUTION_640x480 per glide.h (5 is 640x350) */
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_COMBINE_FUNCTION_LOCAL 1
#define GR_COMBINE_LOCAL_ITERATED 0
#define GR_DEPTHBUFFER_ZBUFFER 1
#define GR_CMP_LESS 1
#define RINGS 30
#define SIDES 15

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

static void rot(const float p[3], float cx, float sx, float cy, float sy, float o[3]) {
    float x=p[0], y=p[1], z=p[2];
    float x1 =  x*cy + z*sy, z1 = -x*sy + z*cy;
    o[0] = x1; o[1] = y*cx - z1*sx; o[2] = y*sx + z1*cx;
}
static void torus_pt(float u, float v, float R, float r, float p[3], float n[3]) {
    float cu=cosf(u), su=sinf(u), cv=cosf(v), sv=sinf(v);
    p[0] = (R + r*cv)*cu; p[1] = (R + r*cv)*su; p[2] = r*sv;
    n[0] = cv*cu; n[1] = cv*su; n[2] = sv;
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
    i_fn       grDepthBufferMode=(i_fn)     GetProcAddress(dll, "grDepthBufferMode");
    i_fn       grDepthBufferFunction=(i_fn) GetProcAddress(dll, "grDepthBufferFunction");
    i_fn       grDepthMask    = (i_fn)      GetProcAddress(dll, "grDepthMask");
    tri_fn     grDrawTriangle = (tri_fn)    GetProcAddress(dll, "grDrawTriangle");
    if (!grGlideInit || !grSstWinOpen || !grDrawTriangle) {
        fprintf(stderr, "FAIL: missing exports\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVKdonut";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVKdonut", "GlideVK donut", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, wc.hInstance, NULL);
    grGlideInit();
    char hw[256] = {0}; grSstQueryHardware(hw); grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, 0, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, 0, GR_COMBINE_LOCAL_ITERATED, 0, 0);
    if (grDepthBufferMode) { grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
                             grDepthBufferFunction(GR_CMP_LESS); grDepthMask(1); }

    const float W = 640, H = 480, FOC = 340.0f, DIST = 5.0f, R = 1.7f, r = 0.65f;
    const float TWO_PI = 6.2831853f;
    float L[3] = {0.35f, 0.50f, -0.79f};      /* light direction */
    float tint[3] = {1.0f, 0.78f, 0.30f};     /* warm gold */

    for (int frame = 0; frame < 100000; ++frame) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        float ax = frame * 0.017f, ay = frame * 0.026f;
        float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay);
        grBufferClear(0x00141826u, 0xff, 0xffff);   /* deep blue */

        for (int i = 0; i < RINGS; ++i) {
            for (int j = 0; j < SIDES; ++j) {
                float U[2] = { i*TWO_PI/RINGS, (i+1)*TWO_PI/RINGS };
                float V[2] = { j*TWO_PI/SIDES, (j+1)*TWO_PI/SIDES };
                int uu[4] = {0,1,1,0}, vv[4] = {0,0,1,1};
                GrVertex q[4];
                for (int k = 0; k < 4; ++k) {
                    float p0[3], n0[3]; torus_pt(U[uu[k]], V[vv[k]], R, r, p0, n0);
                    float p[3], n[3]; rot(p0, cx,sx,cy,sy, p); rot(n0, cx,sx,cy,sy, n);
                    float z = p[2] + DIST, oow = 1.0f / z;
                    float sh = n[0]*L[0] + n[1]*L[1] + n[2]*L[2];
                    if (sh < 0) sh = 0;
                    sh = 0.18f + 0.82f*sh;           /* ambient + diffuse */
                    float spec = powf(sh, 6.0f) * 0.6f;   /* subtle highlight */
                    q[k].x = W*0.5f + p[0]*FOC*oow;
                    q[k].y = H*0.5f - p[1]*FOC*oow;
                    q[k].z = 0; q[k].ooz = 65535.0f * oow; q[k].oow = oow;
                    q[k].r = (tint[0]*sh + spec) * 255; if (q[k].r>255) q[k].r=255;
                    q[k].g = (tint[1]*sh + spec) * 255; if (q[k].g>255) q[k].g=255;
                    q[k].b = (tint[2]*sh + spec) * 255; if (q[k].b>255) q[k].b=255;
                    q[k].a = 255;
                    q[k].tmuvtx[0].sow = 0; q[k].tmuvtx[0].tow = 0; q[k].tmuvtx[0].oow = oow;
                }
                grDrawTriangle(&q[0], &q[1], &q[2]);
                grDrawTriangle(&q[0], &q[2], &q[3]);
            }
        }
        grBufferSwap(1);
        Sleep(10);
    }
done:
    DestroyWindow(hwnd);
    return 0;
}

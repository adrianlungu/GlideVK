/* GlideVK glide2x depth-buffer / occlusion test.
 *
 * With Z-buffering on, draws a NEAR green quad first, then a FAR red quad over
 * the same area. Depth testing must reject the later far quad, so the overlap
 * stays GREEN despite red being drawn last. (If depth were ignored, the last
 * draw — red — would win.) This is exactly the "is the logo behind the glow"
 * question: correct occlusion is draw-order independent.
 *
 * Glide delivers depth as ooz = 65535/w (larger = nearer); GlideVK maps a large
 * ooz to a small [0,1] depth. So near green uses ooz=65535 (depth 0) and far
 * red uses ooz=1 (depth ~1).
 *
 * Capture:  GLIDEVK_CAPTURE=depth.ppm i686 ... wine wine_depth.exe
 * Build:    i686-w64-mingw32-gcc -o wine_depth.exe examples/wine_depth.c -lgdi32
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
typedef void(__stdcall *i_fn)(int);
typedef void(__stdcall *tri_fn)(void *, void *, void *);

#define GR_RES_640x480 7    /* GR_RESOLUTION_640x480 per glide.h (5 is 640x350) */
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_DEPTHBUFFER_ZBUFFER 1
#define GR_CMP_LESS 1
#define GR_COMBINE_FUNCTION_LOCAL 1
#define GR_COMBINE_LOCAL_ITERATED 0

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

/* A quad over [x0,x1)x[y0,y1) at a uniform depth `ooz`, flat iterated colour. */
static void quad(tri_fn draw, float x0, float y0, float x1, float y1,
                 float ooz, float r, float g, float b) {
    GrVertex q[4] = {0};
    float PX[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    for (int i = 0; i < 4; ++i) {
        q[i].x = PX[i][0]; q[i].y = PX[i][1];
        q[i].r = r; q[i].g = g; q[i].b = b; q[i].a = 255;
        q[i].ooz = ooz; q[i].oow = 1.0f;
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
    i_fn       grDepthBufferMode=(i_fn)     GetProcAddress(dll, "grDepthBufferMode");
    i_fn       grDepthBufferFunction=(i_fn) GetProcAddress(dll, "grDepthBufferFunction");
    i_fn       grDepthMask    = (i_fn)      GetProcAddress(dll, "grDepthMask");
    tri_fn     grDrawTriangle = (tri_fn)    GetProcAddress(dll, "grDrawTriangle");
    if (!grGlideInit || !grSstWinOpen || !grDrawTriangle || !grDepthBufferMode) {
        fprintf(stderr, "FAIL: missing exports\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVKdepth";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVKdepth", "GlideVK depth", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, wc.hInstance, NULL);
    grGlideInit();
    char hw[256] = {0}; grSstQueryHardware(hw); grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, 0, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, 0, GR_COMBINE_LOCAL_ITERATED, 0, 0);
    grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
    grDepthBufferFunction(GR_CMP_LESS);
    grDepthMask(1);

    for (int f = 0; f < 20; ++f) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        grBufferClear(0x000000FF, 0xff, 0xffff); /* also clears depth to far */

        /* NEAR green (ooz=65535 -> depth 0) drawn FIRST, left-biased box. */
        quad(grDrawTriangle, 160, 120, 480, 360, 65535.0f, 0, 255, 0);
        /* FAR red (ooz=1 -> depth ~1) drawn SECOND, same box. Must be rejected. */
        quad(grDrawTriangle, 160, 120, 480, 360, 1.0f, 255, 0, 0);

        grBufferSwap(1);
        Sleep(12);
    }
    DestroyWindow(hwnd);
    printf("center: expect GREEN (near quad drawn first occludes the later far red)\n");
    return 0;
}

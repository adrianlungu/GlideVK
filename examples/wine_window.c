/* GlideVK windowed smoke test — P3 raster-state correctness.
 *
 * Loads glide3x.dll like a game would, opens a context, and runs three
 * sub-tests, each verified by reading the presented frame's center pixel:
 *   A. additive blend   — red tri (GR_BLEND_ONE/ONE) over blue clear -> magenta
 *   B. depth occlusion  — near green drawn BEFORE far red; depth test keeps
 *                         green (painter's order alone would show red)
 *   C. alpha test       — GREATER 0.5 on a zero-alpha tri -> all discarded,
 *                         center stays the blue clear
 *
 * Build:  {i686,x86_64}-w64-mingw32-gcc -o wine_window.exe examples/wine_window.c -lgdi32
 */
#include <windows.h>
#include <stdio.h>

typedef void(__stdcall *v_fn)(void);
typedef unsigned int(__stdcall *winopen_fn)(size_t, unsigned int, int, int, int, int, int);
typedef void(__stdcall *clear_fn)(unsigned int, unsigned char, unsigned int);
typedef void(__stdcall *i_fn)(int);
typedef void(__stdcall *close_fn)(unsigned int);
typedef void(__stdcall *vlayout_fn)(int, int, int);
typedef void(__stdcall *tri_fn)(const void *, const void *, const void *);
typedef void(__stdcall *combine_fn)(int, int, int, int, int);
typedef void(__stdcall *blend_fn)(int, int, int, int);
typedef void(__stdcall *u8_fn)(unsigned char);
typedef int(__stdcall *result_fn)(void);
typedef unsigned int(__stdcall *pixel_fn)(void);

#define GR_RESOLUTION_640x480 5
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_PARAM_XY 0x01
#define GR_PARAM_Z 0x02
#define GR_PARAM_A 0x06
#define GR_PARAM_RGB 0x07
#define GR_PARAM_ST0 0x10
#define GR_PARAM_ENABLE 1
/* combine: rgb = iterated (LOCAL) */
#define GR_COMBINE_FUNCTION_LOCAL 0x01
#define GR_COMBINE_LOCAL_ITERATED 0x00
#define GR_COMBINE_OTHER_ITERATED 0x00
/* blend factors */
#define GR_BLEND_ZERO 0x00
#define GR_BLEND_ONE 0x04
/* depth / alpha compare (GR_CMP_*) */
#define GR_CMP_LESS 1
#define GR_CMP_GREATER 4
#define GR_CMP_ALWAYS 7
#define FXTRUE 1
#define FXFALSE 0

typedef struct { float x, y, z; float r, g, b, a; float s, t; } Vtx;

static tri_fn grDrawTriangle;
static clear_fn grBufferClear;
static i_fn grBufferSwap;
static result_fn frameResult;
static pixel_fn centerPixel;

static void fillTri(Vtx v[3], float r, float g, float b, float a, float z) {
    float P[3][2] = {{0.f, 0.f}, {1280.f, 0.f}, {0.f, 960.f}};
    for (int i = 0; i < 3; ++i) {
        v[i].x = P[i][0]; v[i].y = P[i][1]; v[i].z = z;
        v[i].r = r; v[i].g = g; v[i].b = b; v[i].a = a;
        v[i].s = 0.f; v[i].t = 0.f;
    }
}

/* Run `n` frames; `second` (optional) is drawn after `first` each frame. */
static unsigned int runFrames(int n, unsigned int clear, Vtx *first, Vtx *second) {
    for (int f = 0; f < n; ++f) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        grBufferClear(clear, 0xff, 0xffff);
        grDrawTriangle(&first[0], &first[1], &first[2]);
        if (second) grDrawTriangle(&second[0], &second[1], &second[2]);
        grBufferSwap(1);
        Sleep(8);
    }
    return centerPixel();
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

int main(void) {
    HMODULE dll = LoadLibraryA("glide3x.dll");
    if (!dll) { fprintf(stderr, "FAIL: load glide3x.dll (%lu)\n", GetLastError()); return 1; }

    v_fn        grGlideInit               = (v_fn)      GetProcAddress(dll, "grGlideInit");
    winopen_fn  grSstWinOpen              = (winopen_fn)GetProcAddress(dll, "grSstWinOpen");
    close_fn    grSstWinClose             = (close_fn)  GetProcAddress(dll, "grSstWinClose");
    vlayout_fn  grVertexLayout            = (vlayout_fn)GetProcAddress(dll, "grVertexLayout");
    combine_fn  grColorCombine            = (combine_fn)GetProcAddress(dll, "grColorCombine");
    blend_fn    grAlphaBlendFunction      = (blend_fn)  GetProcAddress(dll, "grAlphaBlendFunction");
    i_fn        grDepthBufferMode         = (i_fn)      GetProcAddress(dll, "grDepthBufferMode");
    i_fn        grDepthBufferFunction     = (i_fn)      GetProcAddress(dll, "grDepthBufferFunction");
    i_fn        grDepthMask               = (i_fn)      GetProcAddress(dll, "grDepthMask");
    i_fn        grAlphaTestFunction       = (i_fn)      GetProcAddress(dll, "grAlphaTestFunction");
    u8_fn       grAlphaTestReferenceValue = (u8_fn)     GetProcAddress(dll, "grAlphaTestReferenceValue");
    grDrawTriangle = (tri_fn)   GetProcAddress(dll, "grDrawTriangle");
    grBufferClear  = (clear_fn) GetProcAddress(dll, "grBufferClear");
    grBufferSwap   = (i_fn)     GetProcAddress(dll, "grBufferSwap");
    frameResult    = (result_fn)GetProcAddress(dll, "glidevkFrameResult");
    centerPixel    = (pixel_fn) GetProcAddress(dll, "glidevkCenterPixel");
    if (!grGlideInit || !grSstWinOpen || !grVertexLayout || !grColorCombine ||
        !grAlphaBlendFunction || !grDepthBufferMode || !grDepthBufferFunction || !grDepthMask ||
        !grAlphaTestFunction || !grAlphaTestReferenceValue || !grDrawTriangle || !grBufferClear ||
        !grBufferSwap || !frameResult || !centerPixel) {
        fprintf(stderr, "FAIL: missing exports\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GlideVKWnd";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GlideVKWnd", "GlideVK P3", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) { fprintf(stderr, "FAIL: CreateWindow (%lu)\n", GetLastError()); return 1; }

    grGlideInit();
    unsigned int ctx = grSstWinOpen((size_t)hwnd, GR_RESOLUTION_640x480, -1, 0, GR_ORIGIN_UPPER_LEFT, 2, 1);
    if (ctx == 0) { fprintf(stderr, "FAIL: grSstWinOpen returned 0\n"); return 2; }

    grVertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
    grVertexLayout(GR_PARAM_Z, 8, GR_PARAM_ENABLE);
    grVertexLayout(GR_PARAM_RGB, 12, GR_PARAM_ENABLE);
    grVertexLayout(GR_PARAM_A, 24, GR_PARAM_ENABLE);
    grVertexLayout(GR_PARAM_ST0, 28, GR_PARAM_ENABLE);
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, 0, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_ITERATED, 0);

    int ok = 1;
    Vtx a[3], b[3];

    /* A. additive blend: red over blue -> magenta */
    grDepthBufferMode(0);
    grAlphaTestFunction(GR_CMP_ALWAYS);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE);
    fillTri(a, 255.f, 0.f, 0.f, 255.f, 0.f);
    unsigned int pa = runFrames(12, 0x0000FFFF, a, NULL);
    int passA = (pa == 0xFF00FFFFu);
    printf("A blend    : center=0x%08X expect 0xFF00FFFF -> %s\n", pa, passA ? "PASS" : "FAIL");
    ok &= passA;

    /* B. depth: near green drawn before far red; depth keeps green */
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grDepthBufferMode(1);
    grDepthBufferFunction(GR_CMP_LESS);
    grDepthMask(FXTRUE);
    fillTri(a, 0.f, 255.f, 0.f, 255.f, 0.2f); /* near green */
    fillTri(b, 255.f, 0.f, 0.f, 255.f, 0.8f); /* far red */
    unsigned int pb = runFrames(12, 0x0000FFFF, a, b);
    int passB = (pb == 0x00FF00FFu);
    printf("B depth    : center=0x%08X expect 0x00FF00FF -> %s\n", pb, passB ? "PASS" : "FAIL");
    ok &= passB;

    /* C. alpha test: zero-alpha tri, GREATER 0.5 -> discarded, stays blue */
    grDepthBufferMode(0);
    grAlphaTestFunction(GR_CMP_GREATER);
    grAlphaTestReferenceValue(128);
    fillTri(a, 0.f, 255.f, 0.f, 0.f, 0.f); /* green, alpha 0 */
    unsigned int pc = runFrames(12, 0x0000FFFF, a, NULL);
    int passC = (pc == 0x0000FFFFu);
    printf("C alphatest: center=0x%08X expect 0x0000FFFF -> %s\n", pc, passC ? "PASS" : "FAIL");
    ok &= passC;
    grAlphaTestFunction(GR_CMP_ALWAYS);

    int errors = frameResult();
    if (grSstWinClose) grSstWinClose(ctx);
    DestroyWindow(hwnd);

    if (ok && errors == 0) { printf("PASS: blend + depth + alpha-test all correct (0 present errors)\n"); return 0; }
    fprintf(stderr, "FAIL: ok=%d errors=%d\n", ok, errors);
    return 3;
}

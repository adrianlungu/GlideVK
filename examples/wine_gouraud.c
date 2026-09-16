/* GlideVK glide2x Gouraud / iterated-lighting test.
 *
 * Glide "lighting" is per-vertex iterated RGBA smoothly interpolated across the
 * triangle. We draw one big triangle with red / green / blue corners and check
 * that each corner reads its pure colour and the centroid reads a roughly equal
 * blend (~85,85,85). A flat or wrongly-interpolated result reveals a problem in
 * the iterated-colour path (the basis of vertex lighting in Glide games).
 *
 * Capture:  GLIDEVK_CAPTURE=gouraud.ppm i686 ... wine wine_gouraud.exe
 * Build:    i686-w64-mingw32-gcc -o wine_gouraud.exe examples/wine_gouraud.c -lgdi32
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
typedef void(__stdcall *tri_fn)(void *, void *, void *);

#define GR_RES_640x480 7    /* GR_RESOLUTION_640x480 per glide.h (5 is 640x350) */
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_COMBINE_FUNCTION_LOCAL 1
#define GR_COMBINE_LOCAL_ITERATED 0

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
    tri_fn     grDrawTriangle = (tri_fn)    GetProcAddress(dll, "grDrawTriangle");
    if (!grGlideInit || !grSstWinOpen || !grDrawTriangle) {
        fprintf(stderr, "FAIL: missing exports\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVKgou";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVKgou", "GlideVK gouraud", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, wc.hInstance, NULL);
    grGlideInit();
    char hw[256] = {0}; grSstQueryHardware(hw); grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, 0, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, 0, GR_COMBINE_LOCAL_ITERATED, 0, 0);

    /* Big triangle: top=red, bottom-left=green, bottom-right=blue. */
    GrVertex v[3] = {0};
    float P[3][5] = {           /* x, y, r, g, b */
        {320,  40, 255, 0,   0  },
        { 40, 440, 0,   255, 0  },
        {600, 440, 0,   0,   255},
    };
    for (int i = 0; i < 3; ++i) {
        v[i].x = P[i][0]; v[i].y = P[i][1];
        v[i].r = P[i][2]; v[i].g = P[i][3]; v[i].b = P[i][4];
        v[i].a = 255; v[i].oow = 1.0f;
    }
    for (int f = 0; f < 20; ++f) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        grBufferClear(0x000000FFu, 0xff, 0xffff);
        grDrawTriangle(&v[0], &v[1], &v[2]);
        grBufferSwap(1);
        Sleep(12);
    }
    DestroyWindow(hwnd);
    printf("expect: near-top~RED, near-BL~GREEN, near-BR~BLUE, centroid~even blend\n");
    return 0;
}

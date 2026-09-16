/* Glide2 alpha-blend factor codes — DIFFERENTIAL test.
 *
 * Walks the GrAlphaBlendFnc_t codes with no assumption about what each means:
 * for every (src, dst) pair of interest it clears the buffer to a known colour,
 * draws a screen quad with a known iterated colour + alpha, and reads the
 * centre pixel back through grLfbReadRegion. Run beside a reference glide2x.dll
 * and beside GlideVK's and diff (examples/run_diff.sh wine_blendmode): the
 * reference defines the table.
 *
 * Colours are chosen so every factor gives a distinct result:
 *   dst (clear) = (128, 64, 32)  src (vertex) = (64, 128, 192)  src alpha = 128
 * Output is quantised to 8 levels per channel to absorb 565 rounding.
 *
 * Build:  i686-w64-mingw32-gcc -o wine_blendmode.exe examples/wine_blendmode.c -lgdi32
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

typedef void(__stdcall *v_fn)(void);
typedef int(__stdcall *query_fn)(void *);
typedef void(__stdcall *sel_fn)(int);
typedef int(__stdcall *winopen_fn)(size_t, unsigned int, int, int, int, int, int);
typedef void(__stdcall *winclose_fn)(void);
typedef void(__stdcall *clear_fn)(unsigned int, unsigned char, unsigned int);
typedef void(__stdcall *swap_fn)(int);
typedef void(__stdcall *combine_fn)(int, int, int, int, int);
typedef void(__stdcall *blend_fn)(int, int, int, int);
typedef void(__stdcall *tri_fn)(void *, void *, void *);
typedef int(__stdcall *readregion_fn)(unsigned int, unsigned int, unsigned int,
                                      unsigned int, unsigned int, unsigned int, void *);

#define GR_RES_640x480 7    /* GR_RESOLUTION_640x480 per glide.h (5 is 640x350) */
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_COLORFORMAT_ARGB 0
#define GR_COMBINE_FUNCTION_LOCAL 1
#define GR_COMBINE_FACTOR_NONE 0
#define GR_COMBINE_LOCAL_ITERATED 0
#define GR_COMBINE_OTHER_NONE 0
#define GR_BUFFER_FRONTBUFFER 0

#define W 640.0f
#define H 480.0f
#define FRAMES 12

/* ARGB packing for grBufferClear with GR_COLORFORMAT_ARGB. */
#define DST_R 128
#define DST_G 64
#define DST_B 32
#define SRC_R 64
#define SRC_G 128
#define SRC_B 192
#define SRC_A 128

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
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
    clear_fn    grBufferClear     = (clear_fn)    GetProcAddress(dll, "_grBufferClear@12");
    swap_fn     grBufferSwap      = (swap_fn)     GetProcAddress(dll, "_grBufferSwap@4");
    combine_fn  grColorCombine    = (combine_fn)  GetProcAddress(dll, "_grColorCombine@20");
    combine_fn  grAlphaCombine    = (combine_fn)  GetProcAddress(dll, "_grAlphaCombine@20");
    blend_fn    grAlphaBlendFunction=(blend_fn)   GetProcAddress(dll, "_grAlphaBlendFunction@16");
    tri_fn      grDrawTriangle    = (tri_fn)      GetProcAddress(dll, "_grDrawTriangle@12");
    readregion_fn grLfbReadRegion = (readregion_fn)GetProcAddress(dll, "_grLfbReadRegion@28");
    if (!grGlideInit || !grSstWinOpen || !grDrawTriangle || !grAlphaBlendFunction ||
        !grAlphaCombine || !grLfbReadRegion) {
        fprintf(stderr, "FAIL: missing exports (decorated _grXxx@N names)\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVKblendmode";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVKblendmode", "Glide blend diff", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, (int)W, (int)H, NULL, NULL, wc.hInstance, NULL);

    grGlideInit();
    char hw[256] = {0};
    grSstQueryHardware(hw);
    grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }
    /* Output = iterated vertex colour; alpha = iterated vertex alpha. */
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, 0);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, 0);

    GrVertex q[4] = {0};
    float PX[4][2] = {{0,0},{W,0},{W,H},{0,H}};
    for (int i = 0; i < 4; ++i) {
        q[i].x = PX[i][0]; q[i].y = PX[i][1];
        q[i].r = SRC_R; q[i].g = SRC_G; q[i].b = SRC_B; q[i].a = SRC_A;
        q[i].ooz = 1.0f; q[i].oow = 1.0f;
        q[i].tmuvtx[0].oow = 1.0f; q[i].tmuvtx[1].oow = 1.0f;
    }
    const unsigned int clearColor = (0xFFu << 24) | (DST_R << 16) | (DST_G << 8) | DST_B;

    /* Every code as the source factor with dst=ZERO, then every code as the
     * destination factor with src=ZERO. */
    static const int codes[] = {0, 1, 2, 3, 4, 5, 6, 7, 0xf};
    const int ncodes = sizeof(codes) / sizeof(codes[0]);
    for (int side = 0; side < 2; ++side) {
        for (int c = 0; c < ncodes; ++c) {
            int rs = side == 0 ? codes[c] : 0;
            int rd = side == 0 ? 0 : codes[c];
            grAlphaBlendFunction(rs, rd, 0, 0);
            for (int f = 0; f < FRAMES; ++f) {
                MSG msg;
                while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
                grBufferClear(clearColor, 0xff, 0xffff);
                grDrawTriangle(&q[0], &q[1], &q[2]);
                grDrawTriangle(&q[0], &q[2], &q[3]);
                grBufferSwap(1);
                Sleep(8);
            }
            unsigned short px = 0;
            grLfbReadRegion(GR_BUFFER_FRONTBUFFER, (int)W / 2, (int)H / 2, 1, 1, 2, &px);
            int r = (px >> 11) & 0x1f, g = (px >> 5) & 0x3f, b = px & 0x1f;
            /* 8 levels per channel, rounded to the nearest bucket so an
             * implementation's 565 rounding direction cannot flip a value that
             * sits on a bucket edge. */
            printf("src=%#x dst=%#x -> rgb/32=(%d,%d,%d)\n", rs, rd, (r + 2) >> 2, (g + 4) >> 3, (b + 2) >> 2);
        }
    }

    grSstWinClose();
    grGlideShutdown();
    DestroyWindow(hwnd);
    return 0;
}

/* Glide2 W-buffer depth — DIFFERENTIAL test.
 *
 * Which of two overlapping quads wins under GR_DEPTHBUFFER_WBUFFER with
 * GR_CMP_LEQUAL when the NEARER one (larger oow = 1/w) is drawn FIRST and the
 * farther one second? On hardware the depth test keeps the nearq quad; a
 * wrapper that ignores oow for depth lets the second draw overwrite it (Deus
 * Ex: the Ion Storm glow covering its text). ooz is left at 0 like Unreal.
 *
 *   A: wbuffer, nearq green (w=100) then farq red (w=1000)   -> reference decides
 *   B: same with the farq one drawn first
 *   C: depth disabled, nearq green then farq red             -> last wins
 *
 * Run: examples/run_diff.sh wine_wbuffer
 * Build:  i686-w64-mingw32-gcc -o wine_wbuffer.exe examples/wine_wbuffer.c -lgdi32
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
typedef void(__stdcall *i_fn)(int);
typedef void(__stdcall *combine_fn)(int, int, int, int, int);
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
#define GR_DEPTHBUFFER_DISABLE 0
#define GR_DEPTHBUFFER_WBUFFER 2
#define GR_CMP_LEQUAL 3
#define GR_WDEPTHVALUE_FARTHEST 0xffff
#define GR_BUFFER_FRONTBUFFER 0
#define W 640.0f
#define H 480.0f
#define FRAMES 10

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

static const char *classify(unsigned short p) {
    int r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
    int R = r > 15, G = g > 31, B = b > 15;
    if (R && !G && !B) return "red";
    if (!R && G && !B) return "green";
    if (!R && !G && !B) return "black";
    return "mixed";
}

static tri_fn grDrawTriangle; static clear_fn grBufferClear; static swap_fn grBufferSwap;
static readregion_fn grLfbReadRegion;

static void quad(GrVertex q[4], float w, float r, float g, float b) {
    float PX[4][2] = {{0,0},{W,0},{W,H},{0,H}};
    for (int i = 0; i < 4; ++i) {
        q[i].x = PX[i][0]; q[i].y = PX[i][1]; q[i].z = 0;
        q[i].r = r; q[i].g = g; q[i].b = b; q[i].a = 255;
        q[i].ooz = 0.0f;           /* unset, like Unreal */
        q[i].oow = 1.0f / w;
        q[i].tmuvtx[0].oow = q[i].oow; q[i].tmuvtx[1].oow = q[i].oow;
    }
}

static void run(const char *tag, GrVertex first[4], GrVertex second[4]) {
    for (int f = 0; f < FRAMES; ++f) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        grBufferClear(0x00000000, 0xff, GR_WDEPTHVALUE_FARTHEST);
        grDrawTriangle(&first[0], &first[1], &first[2]);
        grDrawTriangle(&first[0], &first[2], &first[3]);
        grDrawTriangle(&second[0], &second[1], &second[2]);
        grDrawTriangle(&second[0], &second[2], &second[3]);
        grBufferSwap(1);
        Sleep(8);
    }
    unsigned short px = 0;
    grLfbReadRegion(GR_BUFFER_FRONTBUFFER, (int)W / 2, (int)H / 2, 1, 1, 2, &px);
    printf("%s: center=%s\n", tag, classify(px));
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
    combine_fn  grColorCombine    = (combine_fn)  GetProcAddress(dll, "_grColorCombine@20");
    i_fn        grDepthBufferMode = (i_fn)        GetProcAddress(dll, "_grDepthBufferMode@4");
    i_fn        grDepthBufferFunction=(i_fn)      GetProcAddress(dll, "_grDepthBufferFunction@4");
    i_fn        grDepthMask       = (i_fn)        GetProcAddress(dll, "_grDepthMask@4");
    grDrawTriangle  = (tri_fn)   GetProcAddress(dll, "_grDrawTriangle@12");
    grBufferClear   = (clear_fn) GetProcAddress(dll, "_grBufferClear@12");
    grBufferSwap    = (swap_fn)  GetProcAddress(dll, "_grBufferSwap@4");
    grLfbReadRegion = (readregion_fn)GetProcAddress(dll, "_grLfbReadRegion@28");
    if (!grGlideInit || !grSstWinOpen || !grDrawTriangle || !grDepthBufferMode || !grDepthMask ||
        !grDepthBufferFunction || !grLfbReadRegion) {
        fprintf(stderr, "FAIL: missing exports (decorated _grXxx@N names)\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVKwbuf";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVKwbuf", "Glide wbuffer diff", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, (int)W, (int)H, NULL, NULL, wc.hInstance, NULL);

    grGlideInit();
    char hw[256] = {0};
    grSstQueryHardware(hw);
    grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, 0);

    GrVertex nearq[4] = {0}, farq[4] = {0};
    quad(nearq, 100.0f, 0, 255, 0);
    quad(farq, 1000.0f, 255, 0, 0);

    grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
    grDepthBufferFunction(GR_CMP_LEQUAL);
    grDepthMask(1);
    run("A wbuffer nearq(green,w=100) then farq(red,w=1000)", nearq, farq);
    run("B wbuffer farq(red) then nearq(green)", farq, nearq);

    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    run("C depth off nearq(green) then farq(red)", nearq, farq);

    grSstWinClose();
    grGlideShutdown();
    DestroyWindow(hwnd);
    return 0;
}

/* Glide2 texture-coordinate space — DIFFERENTIAL test.
 *
 * Unlike the other harnesses this one encodes no assumption about how GlideVK
 * maps s/t to texels. It draws in the Glide 2.x convention as documented by
 * the 3dfx programming guide — texture coordinates span [0,256) on the
 * texture's LARGER axis regardless of its pixel size, and [0,256/aspect) on
 * the smaller one — then reads pixels back through grLfbReadRegion, which
 * every Glide 2 implementation exports. Run it beside a reference glide2x.dll
 * (nGlide, dgVoodoo) and beside GlideVK's and diff the output: the reference
 * defines what is correct. See examples/run_diff.sh.
 *
 *   A: 32x32 texture in four 16x16 quadrants (TL red, TR green, BL blue, BR
 *      white), quad with s,t in 0..256. Samples one point per screen quadrant.
 *   B: 64x16 texture (aspect 4:1), left half red / right half green, quad with
 *      s in 0..256 and t in 0..64. Samples left and right.
 *
 * A wrapper that normalises by pixel size instead tiles A 8x and B 4x, so the
 * sample points land in the wrong colour — that is the discriminator.
 *
 * Build:  i686-w64-mingw32-gcc -o wine_texcoord.exe examples/wine_texcoord.c -lgdi32
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

typedef struct {
    unsigned int smallLod, largeLod, aspectRatio, format;
    void *data;
} GrTexInfo;

typedef void(__stdcall *v_fn)(void);
typedef int(__stdcall *query_fn)(void *);
typedef void(__stdcall *sel_fn)(int);
typedef int(__stdcall *winopen_fn)(size_t, unsigned int, int, int, int, int, int);
typedef void(__stdcall *winclose_fn)(void);
typedef void(__stdcall *clear_fn)(unsigned int, unsigned char, unsigned int);
typedef void(__stdcall *swap_fn)(int);
typedef void(__stdcall *combine_fn)(int, int, int, int, int);
typedef void(__stdcall *texdl_fn)(int, int, int, GrTexInfo *);
typedef void(__stdcall *tri_fn)(void *, void *, void *);
typedef void(__stdcall *clamp_fn)(int, int, int);
typedef void(__stdcall *filter_fn)(int, int, int);
typedef int(__stdcall *readregion_fn)(unsigned int, unsigned int, unsigned int,
                                      unsigned int, unsigned int, unsigned int, void *);

#define GR_RES_640x480 7    /* GR_RESOLUTION_640x480 per glide.h (5 is 640x350) */
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_COLORFORMAT_ARGB 0
#define GR_TMU0 0
#define GR_TEXFMT_RGB_565 0x0a
#define GR_LOD_64 2         /* inverted: 256=0 .. 1=8 */
#define GR_LOD_32 3
#define GR_ASPECT_4x1 1
#define GR_ASPECT_1x1 3
#define GR_MIPMAPLEVELMASK_BOTH 3
#define GR_COMBINE_FUNCTION_SCALE_OTHER 3
#define GR_COMBINE_FACTOR_ONE 8
#define GR_COMBINE_LOCAL_ITERATED 0
#define GR_COMBINE_OTHER_TEXTURE 1
#define GR_TEXTURECLAMP_WRAP 0
#define GR_TEXTUREFILTER_POINT_SAMPLED 0
#define GR_BUFFER_FRONTBUFFER 0

#define ST_MAX 256.0f       /* Glide 2 texture-coordinate range on the large axis */
#define W 640.0f
#define H 480.0f
#define FRAMES 30           /* enough for lazy readback paths to have a frame */

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

static const char *classify(unsigned short p) {
    int r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
    int R = r > 15, G = g > 31, B = b > 15;
    if (R && G && B) return "white";
    if (R && !G && !B) return "red";
    if (!R && G && !B) return "green";
    if (!R && !G && B) return "blue";
    if (!R && !G && !B) return "black";
    return "mixed";
}

typedef struct {
    tri_fn tri; clear_fn clear; swap_fn swap; readregion_fn read;
} Api;

static void quad(GrVertex q[4], float smax, float tmax) {
    float PX[4][2] = {{0,0},{W,0},{W,H},{0,H}};
    float ST[4][2] = {{0,0},{smax,0},{smax,tmax},{0,tmax}};
    for (int i = 0; i < 4; ++i) {
        q[i].x = PX[i][0]; q[i].y = PX[i][1]; q[i].z = 0;
        q[i].r = q[i].g = q[i].b = q[i].a = 255;
        q[i].ooz = 1.0f; q[i].oow = 1.0f;
        q[i].tmuvtx[0].sow = ST[i][0];
        q[i].tmuvtx[0].tow = ST[i][1];
        q[i].tmuvtx[0].oow = 1.0f;
        q[i].tmuvtx[1] = q[i].tmuvtx[0];
    }
}

static void render(const Api *a, HWND hwnd, GrVertex q[4]) {
    for (int f = 0; f < FRAMES; ++f) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        a->clear(0x00000000, 0xff, 0xffff);
        a->tri(&q[0], &q[1], &q[2]);
        a->tri(&q[0], &q[2], &q[3]);
        a->swap(1);
        Sleep(10);
    }
}

static void sample(const Api *a, const char *tag, const int pts[][2], int n) {
    printf("%s:", tag);
    for (int i = 0; i < n; ++i) {
        unsigned short px = 0x1234;
        a->read(GR_BUFFER_FRONTBUFFER, pts[i][0], pts[i][1], 1, 1, 2, &px);
        printf(" (%d,%d)=%s", pts[i][0], pts[i][1], classify(px));
    }
    printf("\n");
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
    texdl_fn    grTexDownloadMipMap=(texdl_fn)    GetProcAddress(dll, "_grTexDownloadMipMap@16");
    texdl_fn    grTexSource       = (texdl_fn)    GetProcAddress(dll, "_grTexSource@16");
    clamp_fn    grTexClampMode    = (clamp_fn)    GetProcAddress(dll, "_grTexClampMode@12");
    filter_fn   grTexFilterMode   = (filter_fn)   GetProcAddress(dll, "_grTexFilterMode@12");
    tri_fn      grDrawTriangle    = (tri_fn)      GetProcAddress(dll, "_grDrawTriangle@12");
    readregion_fn grLfbReadRegion = (readregion_fn)GetProcAddress(dll, "_grLfbReadRegion@28");
    if (!grGlideInit || !grSstWinOpen || !grDrawTriangle || !grTexDownloadMipMap ||
        !grLfbReadRegion || !grTexClampMode || !grTexFilterMode) {
        fprintf(stderr, "FAIL: missing exports (decorated _grXxx@N names)\n"); return 1;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "GVKtexcoord";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "GVKtexcoord", "Glide texcoord diff", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, (int)W, (int)H, NULL, NULL, wc.hInstance, NULL);

    grGlideInit();
    char hw[256] = {0};
    grSstQueryHardware(hw);
    grSstSelect(0);
    if (!grSstWinOpen((size_t)hwnd, GR_RES_640x480, -1, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        fprintf(stderr, "FAIL: grSstWinOpen\n"); return 2;
    }
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
    grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    Api api = { grDrawTriangle, grBufferClear, grBufferSwap, grLfbReadRegion };
    GrVertex q[4] = {0};

    /* A: 32x32 quadrants. */
    static unsigned short texA[32 * 32];
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            texA[y * 32 + x] = (y < 16) ? (x < 16 ? 0xF800 : 0x07E0) : (x < 16 ? 0x001F : 0xFFFF);
    GrTexInfo infoA = { GR_LOD_32, GR_LOD_32, GR_ASPECT_1x1, GR_TEXFMT_RGB_565, texA };
    grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &infoA);
    grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &infoA);
    quad(q, ST_MAX, ST_MAX);
    render(&api, hwnd, q);
    /* Off-centre samples so an 8x tiling lands in a different quadrant. */
    static const int ptsA[4][2] = {{100, 90}, {540, 90}, {100, 390}, {540, 390}};
    sample(&api, "A 32x32 st0..256 expect red green blue white", ptsA, 4);

    /* B: 64x16, aspect 4:1, left red / right green. */
    static unsigned short texB[64 * 16];
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 64; ++x)
            texB[y * 64 + x] = x < 32 ? 0xF800 : 0x07E0;
    GrTexInfo infoB = { GR_LOD_64, GR_LOD_64, GR_ASPECT_4x1, GR_TEXFMT_RGB_565, texB };
    grTexDownloadMipMap(GR_TMU0, 0x10000, GR_MIPMAPLEVELMASK_BOTH, &infoB);
    grTexSource(GR_TMU0, 0x10000, GR_MIPMAPLEVELMASK_BOTH, &infoB);
    quad(q, ST_MAX, ST_MAX / 4.0f);
    render(&api, hwnd, q);
    static const int ptsB[2][2] = {{100, 240}, {540, 240}};
    sample(&api, "B 64x16 s0..256 t0..64 expect red green", ptsB, 2);

    grSstWinClose();
    grGlideShutdown();
    DestroyWindow(hwnd);
    return 0;
}

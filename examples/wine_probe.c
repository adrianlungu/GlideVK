/* GlideVK smoke test — proves the PE DLL can reach Vulkan through Wine.
 *
 * Loads glide3x.dll exactly as a game would (LoadLibrary + GetProcAddress),
 * then calls the diagnostic `glidevkProbeDevice` export, which brings up a
 * headless Vulkan device via winevulkan and returns the GPU name. No window is
 * created, so this runs on a headless host.
 *
 * Build (64-bit, matching the toolchain we have):
 *   x86_64-w64-mingw32-gcc -o wine_probe.exe examples/wine_probe.c
 * Run:
 *   WINEPATH=target/x86_64-pc-windows-gnu/debug wine wine_probe.exe
 */
#include <windows.h>
#include <stdio.h>

typedef int(__stdcall * probe_fn)(char *out, int len);
typedef void(__stdcall *init_fn)(void);
typedef void(__stdcall *ver_fn)(char *out);

int main(void) {
    HMODULE dll = LoadLibraryA("glide3x.dll");
    if (!dll) {
        fprintf(stderr, "FAIL: could not load glide3x.dll (err %lu)\n", GetLastError());
        return 1;
    }

    ver_fn grGlideGetVersion = (ver_fn)GetProcAddress(dll, "grGlideGetVersion");
    init_fn grGlideInit = (init_fn)GetProcAddress(dll, "grGlideInit");
    probe_fn probe = (probe_fn)GetProcAddress(dll, "glidevkProbeDevice");

    if (!grGlideGetVersion || !grGlideInit || !probe) {
        fprintf(stderr, "FAIL: missing exports (version=%p init=%p probe=%p)\n",
                (void *)grGlideGetVersion, (void *)grGlideInit, (void *)probe);
        return 1;
    }

    char version[64] = {0};
    grGlideGetVersion(version);
    printf("glide3x.dll loaded; grGlideGetVersion -> \"%s\"\n", version);

    grGlideInit();

    char name[256] = {0};
    int n = probe(name, sizeof(name));
    if (n <= 0) {
        fprintf(stderr, "FAIL: glidevkProbeDevice returned %d "
                        "(winevulkan unreachable or no device)\n", n);
        return 2;
    }

    printf("PASS: Vulkan device via winevulkan -> \"%s\"\n", name);
    return 0;
}

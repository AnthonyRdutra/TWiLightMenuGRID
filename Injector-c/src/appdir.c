#include "appdir.h"

#include <string.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdlib.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

static void strip_to_dir(char *path) {
    char *slash = strrchr(path, '/');
#if defined(_WIN32)
    char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    if (slash) *slash = '\0';
    else path[0] = '\0';
}

int app_dir(char *out, size_t out_size) {
    out[0] = '\0';

#if defined(__APPLE__)
    char raw[4096];
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0) return -1;
    char *resolved = realpath(raw, NULL);
    if (!resolved) return -1;
    strncpy(out, resolved, out_size - 1);
    out[out_size - 1] = '\0';
    free(resolved);
    strip_to_dir(out);
    return 0;

#elif defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", out, out_size - 1);
    if (n < 0) return -1;
    out[n] = '\0';
    strip_to_dir(out);
    return 0;

#elif defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)out_size);
    if (n == 0 || n == out_size) return -1;
    strip_to_dir(out);
    return 0;

#else
    return -1;
#endif
}

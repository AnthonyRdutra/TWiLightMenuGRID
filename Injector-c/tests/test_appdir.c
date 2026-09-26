/* Tests for src/appdir.c. Exact expected path is environment-dependent (wherever the build
 * directory lives), so this asserts the one thing that's actually knowable: app_dir() succeeds,
 * returns a real directory, and -- since ctest binaries and the injector binary all land
 * directly in build/ per this project's CMakeLists (no per-target subdirectories) -- that
 * directory is the same one containing CMakeCache.txt (always written straight into the build
 * dir by `cmake -S . -B build`). */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "appdir.h"

static int all_ok = 1;

static void check(const char *what, int cond) {
    printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
    all_ok &= cond;
}

int main(void) {
    char dir[1024];
    int rc = app_dir(dir, sizeof(dir));
    check("app_dir: rc==0", rc == 0);
    check("app_dir: non-empty", dir[0] != '\0');

    struct stat st;
    check("app_dir: result is a real directory", stat(dir, &st) == 0 && S_ISDIR(st.st_mode));

    char marker[1100];
    snprintf(marker, sizeof(marker), "%s/CMakeCache.txt", dir);
    check("app_dir: matches the build dir (CMakeCache.txt found there)", stat(marker, &st) == 0);

    printf(all_ok ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    return all_ok ? 0 : 1;
}

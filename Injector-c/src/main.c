/* Entry point: a native webview window loading assets/ui/index.html (copied next to the binary
 * as ui/, see CMakeLists.txt), driving src/ui/wizard.c's state machine through src/ui/bridge.c.
 *
 * The old Nuklear+SDL3 UI drew every widget as raster triangles into a pixel framebuffer; this
 * one renders real HTML/CSS (vector, resolution-independent, no baked font atlas to size for
 * HiDPI) inside the OS's own web engine -- WebKit on macOS/Linux, WebView2 on Windows. */
#include "webview.h"
#include "ui/wizard.h"
#include "ui/bridge.h"
#include "appdir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

/* Builds a file:// URL for `path`, percent-encoding spaces (the one character bundled install
 * paths realistically contain, e.g. "Program Files") since WebKit/WebView2 otherwise misparse
 * the URL at the first one. */
static void build_file_url(const char *path, char *out, size_t out_size) {
    size_t pos = 0;
#define APPEND(s) do { \
        size_t n = strlen(s); \
        if (pos + n < out_size) { memcpy(out + pos, s, n); pos += n; } \
    } while (0)

    APPEND("file://");
#if defined(_WIN32)
    /* Windows paths ("C:\foo\bar") need a leading slash and forward slashes to become a valid
     * file URL ("file:///C:/foo/bar"). */
    APPEND("/");
#endif
    for (const char *p = path; *p; p++) {
        char c = *p;
#if defined(_WIN32)
        if (c == '\\') c = '/';
#endif
        if (c == ' ') {
            APPEND("%20");
        } else if (pos + 1 < out_size) {
            out[pos++] = c;
        }
    }
    if (pos < out_size) out[pos] = '\0';
    out[out_size - 1] = '\0';
#undef APPEND
}

/* Headless/CI smoke test: if INJECTOR_SMOKE_TEST_MS is set, terminate the webview loop after
 * that many milliseconds instead of waiting for the window to be closed -- lets this binary be
 * used as an automated "did the GUI stack actually come up" check, not just a manual click-test. */
typedef struct {
    webview_t w;
    int ms;
} SmokeTestArgs;

#if defined(_WIN32)
static DWORD WINAPI smoke_test_thread(LPVOID arg) {
    SmokeTestArgs *args = (SmokeTestArgs *)arg;
    Sleep((DWORD)args->ms);
    webview_terminate(args->w);
    free(args);
    return 0;
}
#else
static void *smoke_test_thread(void *arg) {
    SmokeTestArgs *args = (SmokeTestArgs *)arg;
    usleep((useconds_t)args->ms * 1000);
    webview_terminate(args->w);
    free(args);
    return NULL;
}
#endif

static void start_smoke_test_timer_if_requested(webview_t w) {
    const char *ms_env = getenv("INJECTOR_SMOKE_TEST_MS");
    if (!ms_env) return;
    int ms = atoi(ms_env);
    if (ms <= 0) return;

    SmokeTestArgs *args = (SmokeTestArgs *)malloc(sizeof(SmokeTestArgs));
    args->w = w;
    args->ms = ms;
#if defined(_WIN32)
    HANDLE t = CreateThread(NULL, 0, smoke_test_thread, args, 0, NULL);
    if (t) CloseHandle(t);
#else
    pthread_t t;
    if (pthread_create(&t, NULL, smoke_test_thread, args) == 0) pthread_detach(t);
#endif
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char dir[1024];
    if (app_dir(dir, sizeof(dir)) != 0) snprintf(dir, sizeof(dir), ".");

    char index_path[1200];
    snprintf(index_path, sizeof(index_path), "%s/ui/index.html", dir);

    char url[1400];
    build_file_url(index_path, url, sizeof(url));

    webview_t w = webview_create(0, NULL);
    if (!w) {
        fprintf(stderr, "webview_create failed\n");
        return 1;
    }
    webview_set_title(w, "TWiLightMenuGRID Injector");
    webview_set_size(w, 760, 520, WEBVIEW_HINT_NONE);

    WizardState *wizard = wizard_create();
    bridge_install(w, wizard);

    webview_navigate(w, url);
    start_smoke_test_timer_if_requested(w);
    webview_run(w);

    wizard_destroy(wizard);
    webview_destroy(w);
    return 0;
}

#include "bridge.h"
#include "cJSON.h"
#include "platform_dialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

#define SCREENSCRAPER_SIGNUP_URL "https://www.screenscraper.fr/membreinscription.php"

/* Launches the user's default browser at a fixed, hardcoded URL -- never anything JS-supplied, so
 * there's no shell-injection surface even though POSIX does it via system(). */
static void open_external_url(const char *url) {
#if defined(_WIN32)
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "open '%s'", url);
    system(cmd);
#else
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", url);
    system(cmd);
#endif
}

typedef struct {
    webview_t w;
    WizardState *wiz;
} BridgeCtx;

/* Every binding resolves with the wizard's full state -- the frontend always re-renders the
 * whole screen from this rather than patching the DOM incrementally, so there's nothing
 * screen-specific to compute here. */
static void resolve_with_state(BridgeCtx *ctx, const char *seq) {
    char *json = wizard_state_json(ctx->wiz);
    webview_return(ctx->w, seq, 0, json);
    free(json);
}

/* `req` is always a JSON array string, e.g. ["/Volumes/SD"] or [] -- these helpers pull out the
 * one argument each binding below cares about (or a safe default if the frontend sent none). */
static char *arg_string_dup(const char *req, int index) {
    cJSON *args = cJSON_Parse(req);
    char *out = NULL;
    if (args) {
        cJSON *item = cJSON_GetArrayItem(args, index);
        if (item && cJSON_IsString(item)) {
            size_t n = strlen(item->valuestring);
            out = (char *)malloc(n + 1);
            memcpy(out, item->valuestring, n + 1);
        }
        cJSON_Delete(args);
    }
    if (!out) out = strdup("");
    return out;
}

static int arg_int(const char *req, int index, int fallback) {
    cJSON *args = cJSON_Parse(req);
    int out = fallback;
    if (args) {
        cJSON *item = cJSON_GetArrayItem(args, index);
        if (item && cJSON_IsNumber(item)) out = item->valueint;
        cJSON_Delete(args);
    }
    return out;
}

static int arg_bool(const char *req, int index, int fallback) {
    cJSON *args = cJSON_Parse(req);
    int out = fallback;
    if (args) {
        cJSON *item = cJSON_GetArrayItem(args, index);
        if (item && cJSON_IsBool(item)) out = cJSON_IsTrue(item);
        cJSON_Delete(args);
    }
    return out;
}

static void on_get_state(const char *seq, const char *req, void *arg) {
    (void)req;
    resolve_with_state((BridgeCtx *)arg, seq);
}

static void on_rescan_sd(const char *seq, const char *req, void *arg) {
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_rescan_sd(ctx->wiz);
    resolve_with_state(ctx, seq);
}

static void on_select_sd(const char *seq, const char *req, void *arg) {
    BridgeCtx *ctx = (BridgeCtx *)arg;
    char *path = arg_string_dup(req, 0);
    wizard_select_sd(ctx->wiz, path);
    free(path);
    resolve_with_state(ctx, seq);
}

static void on_pick_manual_sd(const char *seq, const char *req, void *arg) {
    BridgeCtx *ctx = (BridgeCtx *)arg;
    char *raw = arg_string_dup(req, 0);
    wizard_pick_manual_sd(ctx->wiz, raw);
    free(raw);
    resolve_with_state(ctx, seq);
}

static void on_confirm_already_installed(const char *seq, const char *req, void *arg) {
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_confirm_already_installed(ctx->wiz, arg_bool(req, 0, 0));
    resolve_with_state(ctx, seq);
}

static void on_select_theme(const char *seq, const char *req, void *arg) {
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_select_theme(ctx->wiz, arg_int(req, 0, -1));
    resolve_with_state(ctx, seq);
}

static void on_install(const char *seq, const char *req, void *arg) {
    BridgeCtx *ctx = (BridgeCtx *)arg;
    char *manual_srldr_path = arg_string_dup(req, 0);
    wizard_install(ctx->wiz, manual_srldr_path);
    free(manual_srldr_path);
    resolve_with_state(ctx, seq);
}

/* Doesn't touch wizard state at all -- resolves with the chosen path (or "" if the user
 * cancelled) directly instead of going through resolve_with_state(), so the frontend can drop it
 * straight into the manual-path input without re-rendering the whole screen from wizard state. */
static void on_pick_srldr_file(const char *seq, const char *req, void *arg) {
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    char path[1200];
    path[0] = '\0';
    platform_pick_file("Select dsimenu.srldr", path, sizeof(path));

    cJSON *result = cJSON_CreateString(path);
    char *json = cJSON_PrintUnformatted(result);
    webview_return(ctx->w, seq, 0, json);
    free(json);
    cJSON_Delete(result);
}

static void on_scrape_ask_yes(const char *seq, const char *req, void *arg) {
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_scrape_ask_yes(ctx->wiz);
    resolve_with_state(ctx, seq);
}

static void on_scrape_skip(const char *seq, const char *req, void *arg) {
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_scrape_skip(ctx->wiz);
    resolve_with_state(ctx, seq);
}

static void on_open_screenscraper_signup(const char *seq, const char *req, void *arg) {
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    open_external_url(SCREENSCRAPER_SIGNUP_URL);
    resolve_with_state(ctx, seq);
}

static void on_scrape_login_continue(const char *seq, const char *req, void *arg) {
    BridgeCtx *ctx = (BridgeCtx *)arg;
    char *user = arg_string_dup(req, 0);
    char *pass = arg_string_dup(req, 1);
    wizard_scrape_login_continue(ctx->wiz, user, pass, arg_bool(req, 2, 0));
    free(user);
    free(pass);
    resolve_with_state(ctx, seq);
}

static void on_scrape_login_skip(const char *seq, const char *req, void *arg) {
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_scrape_login_skip(ctx->wiz);
    resolve_with_state(ctx, seq);
}

static void on_scrape_login_forget(const char *seq, const char *req, void *arg) {
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_scrape_login_forget(ctx->wiz);
    resolve_with_state(ctx, seq);
}

static void on_scrape_start(const char *seq, const char *req, void *arg) {
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_scrape_start(ctx->wiz, arg_bool(req, 0, 0));
    resolve_with_state(ctx, seq);
}

static void on_scrape_cancel(const char *seq, const char *req, void *arg) {
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_scrape_cancel(ctx->wiz);
    resolve_with_state(ctx, seq);
}

static void on_scrape_done_ok(const char *seq, const char *req, void *arg) {
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_scrape_done_ok(ctx->wiz);
    resolve_with_state(ctx, seq);
}

static void on_go_back_to_sd_pick(const char *seq, const char *req, void *arg) {
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_go_back_to_sd_pick(ctx->wiz);
    resolve_with_state(ctx, seq);
}

static void on_go_back_to_already_installed(const char *seq, const char *req, void *arg) {
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_go_back_to_already_installed(ctx->wiz);
    resolve_with_state(ctx, seq);
}

static void on_restart(const char *seq, const char *req, void *arg) {
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    wizard_restart(ctx->wiz);
    resolve_with_state(ctx, seq);
}

/* "Finish" on the done screen -- stops webview_run()'s event loop (see src/main.c), which then
 * falls through to wizard_destroy()/webview_destroy() and returns from main() normally. No
 * resolve_with_state(): the window is on its way down, nothing is left to re-render. */
static void on_quit(const char *seq, const char *req, void *arg) {
    (void)seq;
    (void)req;
    BridgeCtx *ctx = (BridgeCtx *)arg;
    webview_terminate(ctx->w);
}

void bridge_install(webview_t w, WizardState *wiz) {
    /* Leaked intentionally: lives for the whole process, same lifetime as `w`/`wiz` themselves. */
    BridgeCtx *ctx = (BridgeCtx *)malloc(sizeof(BridgeCtx));
    ctx->w = w;
    ctx->wiz = wiz;

    webview_bind(w, "getState", on_get_state, ctx);
    webview_bind(w, "rescanSd", on_rescan_sd, ctx);
    webview_bind(w, "selectSd", on_select_sd, ctx);
    webview_bind(w, "pickManualSd", on_pick_manual_sd, ctx);
    webview_bind(w, "confirmAlreadyInstalled", on_confirm_already_installed, ctx);
    webview_bind(w, "selectTheme", on_select_theme, ctx);
    webview_bind(w, "install", on_install, ctx);
    webview_bind(w, "pickSrldrFile", on_pick_srldr_file, ctx);
    webview_bind(w, "scrapeAskYes", on_scrape_ask_yes, ctx);
    webview_bind(w, "scrapeSkip", on_scrape_skip, ctx);
    webview_bind(w, "openScreenScraperSignup", on_open_screenscraper_signup, ctx);
    webview_bind(w, "scrapeLoginContinue", on_scrape_login_continue, ctx);
    webview_bind(w, "scrapeLoginSkip", on_scrape_login_skip, ctx);
    webview_bind(w, "scrapeLoginForget", on_scrape_login_forget, ctx);
    webview_bind(w, "scrapeStart", on_scrape_start, ctx);
    webview_bind(w, "scrapeCancel", on_scrape_cancel, ctx);
    webview_bind(w, "scrapeDoneOk", on_scrape_done_ok, ctx);
    webview_bind(w, "goBackToSdPick", on_go_back_to_sd_pick, ctx);
    webview_bind(w, "goBackToAlreadyInstalled", on_go_back_to_already_installed, ctx);
    webview_bind(w, "restart", on_restart, ctx);
    webview_bind(w, "quit", on_quit, ctx);
}

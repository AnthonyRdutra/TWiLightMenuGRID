/* Regression test for the background-job plumbing added to src/ui/wizard.c: the SD scan+hash
 * (SCRAPE_JOB_SCAN) and the login-check fast path (SCRAPE_JOB_LOGIN_CHECK when no
 * INJECTOR_SS_DEVID/DEVPASSWORD is configured) both used to run synchronously on whatever thread
 * called into the wizard, freezing the UI; now they run on a background thread and
 * wizard_state_json() is responsible for noticing completion and advancing the step. This exists
 * to catch state-machine bugs (wrong step, deadlock, or a data race reading scrape_preview while
 * the scan thread is still mutating it) without ever touching the network -- SCRAPE_JOB_DOWNLOAD
 * itself is NOT exercised here since that needs a real ScreenScraper request, out of bounds for
 * this "no GUI/network dependency" test suite (see CMakeLists.txt).
 *
 * wizard.c has no GUI dependency of its own (see its header comment), so it's compiled directly
 * into this test binary instead of going through bridge.c/webview. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

#include "wizard.h"
#include "cJSON.h"

static int all_ok = 1;

static void check(const char *what, int cond) {
    printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
    all_ok &= cond;
}

/* Polls wizard_state_json(w) (sleeping sleep_ms between each try, up to max_iters times) until
 * "step" equals want_step. Returns the parsed root right at that moment (caller must
 * cJSON_Delete() it) so it can inspect other fields from the same snapshot, or NULL if it never
 * got there -- bounding this loop is what keeps a stuck job from hanging the test suite forever
 * instead of just failing this one check. */
static cJSON *poll_until_step(WizardState *w, const char *want_step, int max_iters, int sleep_ms) {
    for (int i = 0; i < max_iters; i++) {
        char *json = wizard_state_json(w);
        cJSON *root = cJSON_Parse(json);
        free(json);
        if (!root) return NULL;
        cJSON *step = cJSON_GetObjectItemCaseSensitive(root, "step");
        if (cJSON_IsString(step) && strcmp(step->valuestring, want_step) == 0) return root;
        cJSON_Delete(root);
        SLEEP_MS(sleep_ms);
    }
    return NULL;
}

static const char *json_step(char *json_buf, cJSON **out_root) {
    *out_root = cJSON_Parse(json_buf);
    cJSON *step = *out_root ? cJSON_GetObjectItemCaseSensitive(*out_root, "step") : NULL;
    return cJSON_IsString(step) ? step->valuestring : NULL;
}

static void make_fixture_sd(const char *dir, int rom_count) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s'", dir, dir);
    system(cmd);
    for (int i = 0; i < rom_count; i++) {
        snprintf(cmd, sizeof(cmd), "printf 'rom bytes #%d' > '%s/Game%d.nds'", i, dir, i);
        system(cmd);
    }
}

/* Drives the wizard from a freshly created SD path up to (but not including) the scrape_login
 * step, exactly like every real path into wizard_scrape_login_continue()/_skip() does. */
static void reach_scrape_login(WizardState *w, const char *sd_dir) {
    wizard_pick_manual_sd(w, sd_dir);
    wizard_confirm_already_installed(w, /*already_installed=*/1);
    wizard_scrape_ask_yes(w);
}

int main(void) {
    /* Never let a real ScreenScraper account leak into this test from the developer's own shell,
     * and make sure the login-check fast path (no devid configured) is the one actually exercised
     * -- this test is not allowed to touch the network. */
    unsetenv("INJECTOR_SS_DEVID");
    unsetenv("INJECTOR_SS_DEVPASSWORD");

    const char *sd_dir = "/tmp/injector_test_wizard_sd";

    /* --- scenario 1: anonymous ("Continue without login") path into the scan job ------------- */
    make_fixture_sd(sd_dir, 3);
    {
        WizardState *w = wizard_create();
        reach_scrape_login(w, sd_dir);

        char *json0 = wizard_state_json(w);
        cJSON *root0;
        const char *step0 = json_step(json0, &root0);
        check("reaches scrape_login before skipping", step0 && strcmp(step0, "scrape_login") == 0);
        cJSON_Delete(root0);
        free(json0);

        wizard_scrape_login_skip(w);

        /* Right after launching the scan job, state must already reflect it -- either still
         * scanning (the common case: thread scheduling alone takes longer than returning from
         * wizard_scrape_login_skip()) or, on an implausibly fast/lucky scheduler, already past it.
         * Anything else would mean the job kicked off into the wrong step. */
        char *json1 = wizard_state_json(w);
        cJSON *root1;
        const char *step1 = json_step(json1, &root1);
        check("scan job starts in scrape_scanning (or finishes immediately)",
              step1 && (strcmp(step1, "scrape_scanning") == 0 || strcmp(step1, "scrape_preview") == 0));
        cJSON_Delete(root1);
        free(json1);

        cJSON *done = poll_until_step(w, "scrape_preview", /*max_iters=*/200, /*sleep_ms=*/25);
        check("scan job finishes and reaches scrape_preview", done != NULL);
        if (done) {
            cJSON *scrape = cJSON_GetObjectItemCaseSensitive(done, "scrape");
            cJSON *games = scrape ? cJSON_GetObjectItemCaseSensitive(scrape, "games") : NULL;
            cJSON *totalRoms = scrape ? cJSON_GetObjectItemCaseSensitive(scrape, "totalRoms") : NULL;
            check("found all 3 fixture ROMs", cJSON_IsArray(games) && cJSON_GetArraySize(games) == 3);
            check("totalRoms matches", cJSON_IsNumber(totalRoms) && totalRoms->valueint == 3);
            cJSON_Delete(done);
        }

        wizard_destroy(w);
    }

    /* --- scenario 2: logged-in path (no devid configured -> fast path, no network) ------------ */
    make_fixture_sd(sd_dir, 2);
    {
        WizardState *w = wizard_create();
        reach_scrape_login(w, sd_dir);

        wizard_scrape_login_continue(w, "someuser", "somepass", /*remember=*/0);

        /* No devid/devpassword means there's nothing to validate against, so this must NOT go
         * through scrape_login_checking at all -- straight to the scan job, same as skip. */
        char *json1 = wizard_state_json(w);
        cJSON *root1;
        const char *step1 = json_step(json1, &root1);
        check("no devid configured -> skips scrape_login_checking entirely",
              step1 && strcmp(step1, "scrape_login_checking") != 0);
        cJSON_Delete(root1);
        free(json1);

        check("SS_USER was exported for the (fast-pathed) login",
              getenv("SS_USER") && strcmp(getenv("SS_USER"), "someuser") == 0);

        cJSON *done = poll_until_step(w, "scrape_preview", 200, 25);
        check("scan job (from login_continue) finishes and reaches scrape_preview", done != NULL);
        if (done) {
            cJSON *scrape = cJSON_GetObjectItemCaseSensitive(done, "scrape");
            cJSON *games = scrape ? cJSON_GetObjectItemCaseSensitive(scrape, "games") : NULL;
            check("found both fixture ROMs", cJSON_IsArray(games) && cJSON_GetArraySize(games) == 2);
            cJSON_Delete(done);
        }

        wizard_destroy(w);
    }
    unsetenv("SS_USER");
    unsetenv("SS_PASS");

    char rm_cmd[256];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", sd_dir);
    system(rm_cmd);

    printf(all_ok ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    return all_ok ? 0 : 1;
}

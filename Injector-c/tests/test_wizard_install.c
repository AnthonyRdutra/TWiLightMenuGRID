/* Regression test for the "select a dsimenu.srldr manually" override added to
 * src/ui/wizard.c's do_install()/wizard_install(): a manual path, if given, must be used INSTEAD
 * of the auto-detected "<app_dir>/build/dsimenu.srldr", and a manual path that doesn't resolve to
 * a real file must be a hard, logged error rather than silently falling back to auto-detect.
 * Entirely local (no network, no real theme/SD content needed) -- wizard.c has no GUI dependency
 * of its own (see its header comment), so it's compiled directly into this test binary. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wizard.h"
#include "cJSON.h"

static int all_ok = 1;

static void check(const char *what, int cond) {
    printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
    all_ok &= cond;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int main(void) {
    const char *sd_dir = "/tmp/injector_test_install_sd";
    const char *dst_srldr = "/tmp/injector_test_install_sd/_nds/TWiLightMenu/dsimenu.srldr";
    const char *good_src = "/tmp/injector_test_install_custom.srldr";

    system("rm -rf /tmp/injector_test_install_sd /tmp/injector_test_install_custom.srldr");
    system("mkdir -p /tmp/injector_test_install_sd");
    system("printf 'custom srldr bytes' > /tmp/injector_test_install_custom.srldr");

    WizardState *w = wizard_create();
    wizard_pick_manual_sd(w, sd_dir);
    wizard_confirm_already_installed(w, /*already_installed=*/0); /* -> theme_pick */

    /* --- a bad manual path is a hard error, and installs nothing ----------------------------- */
    wizard_install(w, "/tmp/injector_test_install_does_not_exist.srldr");

    char *json1 = wizard_state_json(w);
    cJSON *root1 = cJSON_Parse(json1);
    free(json1);
    cJSON *step1 = root1 ? cJSON_GetObjectItemCaseSensitive(root1, "step") : NULL;
    cJSON *log1 = root1 ? cJSON_GetObjectItemCaseSensitive(root1, "log") : NULL;
    check("bad manual path -> still advances to scrape_ask",
          cJSON_IsString(step1) && strcmp(step1->valuestring, "scrape_ask") == 0);
    check("bad manual path -> logs an ERROR",
          cJSON_IsString(log1) && strstr(log1->valuestring, "ERROR") != NULL);
    check("bad manual path -> nothing installed on the card", !file_exists(dst_srldr));
    cJSON_Delete(root1);

    /* --- a good manual path is used instead of build/dsimenu.srldr --------------------------- */
    wizard_install(w, good_src);

    char *json2 = wizard_state_json(w);
    cJSON *root2 = cJSON_Parse(json2);
    free(json2);
    cJSON *log2 = root2 ? cJSON_GetObjectItemCaseSensitive(root2, "log") : NULL;
    check("good manual path -> logs which file it used",
          cJSON_IsString(log2) && strstr(log2->valuestring, "Using manually selected dsimenu.srldr") != NULL);
    check("good manual path -> installed on the card", file_exists(dst_srldr));
    if (file_exists(dst_srldr)) {
        FILE *f = fopen(dst_srldr, "rb");
        char buf[64] = {0};
        size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
        if (f) fclose(f);
        check("installed file has the custom source's content", strcmp(buf, "custom srldr bytes") == 0);
    }
    cJSON_Delete(root2);

    /* --- re-installing backs up the previous one instead of silently overwriting it ----------- */
    system("printf 'custom srldr bytes v2' > /tmp/injector_test_install_custom.srldr");
    wizard_install(w, good_src);
    check("re-install backs up the previous dsimenu.srldr",
          file_exists("/tmp/injector_test_install_sd/_nds/TWiLightMenu/dsimenu.srldr.bak"));

    wizard_destroy(w);
    system("rm -rf /tmp/injector_test_install_sd /tmp/injector_test_install_custom.srldr");

    printf(all_ok ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    return all_ok ? 0 : 1;
}

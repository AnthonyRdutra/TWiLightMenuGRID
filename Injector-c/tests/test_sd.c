/* Tests for src/sd.c. sd_list_mount_candidates()/sd_find_twilightmenu_cards() depend on the real
 * host's mounted volumes, so those two only get a "doesn't crash, returns something sane" smoke
 * test here -- sd_looks_like_twilightmenu() and sd_eject_hint() are the fully-controllable,
 * precisely-asserted parts (and are exactly the two pieces of logic the Python original had that
 * are actually testable in isolation, per deploy.py's own find_sd_card()/eject_hint()). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "sd.h"

static int all_ok = 1;

static void check(const char *what, int cond) {
    printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
    all_ok &= cond;
}

int main(void) {
    const char *base = "/tmp/injector_test_sd";
    char nds[512];
    snprintf(nds, sizeof(nds), "%s/_nds", base);

    system("rm -rf /tmp/injector_test_sd");
    mkdir(base, 0755);

    check("no _nds dir -> not a TWiLightMenu SD", !sd_looks_like_twilightmenu(base));

    mkdir(nds, 0755);
    check("with _nds dir -> looks like a TWiLightMenu SD", sd_looks_like_twilightmenu(base));

    /* A file named "_nds" (not a directory) must not count. */
    system("rm -rf /tmp/injector_test_sd");
    mkdir(base, 0755);
    FILE *f = fopen(nds, "w");
    fputs("not a directory", f);
    fclose(f);
    check("_nds as a plain file -> not a TWiLightMenu SD", !sd_looks_like_twilightmenu(base));

    system("rm -rf /tmp/injector_test_sd");

    /* Smoke test: must not crash, must return a non-negative count either way. */
    {
        SdCandidates all = {0};
        sd_list_mount_candidates(&all);
        check("sd_list_mount_candidates: ran without crashing", 1);
        sd_candidates_free(&all);
        check("sd_candidates_free: paths cleared", all.paths == NULL && all.count == 0);
    }
    {
        SdCandidates found = {0};
        sd_find_twilightmenu_cards(&found);
        check("sd_find_twilightmenu_cards: ran without crashing", 1);
        sd_candidates_free(&found);
    }

    /* eject_hint must at least produce a non-empty, NUL-terminated string that mentions the
     * path somewhere on a platform that includes it (macOS/Linux do; Windows/other don't). */
    {
        char hint[256];
        sd_eject_hint("/Volumes/TESTSD", hint, sizeof(hint));
        check("sd_eject_hint: non-empty", strlen(hint) > 0);
#if defined(__APPLE__) || defined(__linux__)
        check("sd_eject_hint: mentions the path on macOS/Linux", strstr(hint, "/Volumes/TESTSD") != NULL);
#endif
    }

    printf(all_ok ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    return all_ok ? 0 : 1;
}

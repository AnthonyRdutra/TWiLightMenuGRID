/* Tests for src/blocklist.c against a controlled system_blocklist.txt fixture, mirroring
 * assetbind/scan_and_bind.py's load_blocklist()/name_blocked(). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "blocklist.h"

static int all_ok = 1;

static void check(const char *what, int cond) {
    printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
    all_ok &= cond;
}

int main(void) {
    const char *path = "/tmp/injector_test_blocklist.txt";
    FILE *f = fopen(path, "w");
    fprintf(f,
        "# comment line, ignored\n"
        "\n"
        "sha1:D7ACF1BE345B87BB3863E599725C8956D5CE87D4   # dsimenu.nds\n"
        "name:pictochat\n"
        "nds-bootstrap\n" /* bare line -- shorthand for name: */
    );
    fclose(f);

    Blocklist b;
    blocklist_load(path, &b);

    check("loads exactly 1 sha1 entry", b.sha1_count == 1);
    check("loads exactly 2 name entries", b.name_count == 2);

    check("sha1 match is case-insensitive",
          blocklist_has_sha1(&b, "d7acf1be345b87bb3863e599725c8956d5ce87d4"));
    check("sha1 match works on the exact stored case too",
          blocklist_has_sha1(&b, "D7ACF1BE345B87BB3863E599725C8956D5CE87D4"));
    check("unrelated sha1 doesn't match", !blocklist_has_sha1(&b, "0000000000000000000000000000000000000000"));

    check("name: prefix matches a full path's basename",
          blocklist_name_blocked(&b, "/Volumes/SD/roms/PictoChat.nds"));
    check("bare-line shorthand matches too",
          blocklist_name_blocked(&b, "nds-bootstrap-hb-release.nds"));
    check("a real game name doesn't match", !blocklist_name_blocked(&b, "Mario Kart DS.nds"));

    blocklist_free(&b);
    check("blocklist_free clears the struct", b.sha1s == NULL && b.names == NULL);

    Blocklist missing;
    blocklist_load("/tmp/injector_test_blocklist_does_not_exist.txt", &missing);
    check("missing file: empty (blocks nothing), no crash",
          missing.sha1_count == 0 && missing.name_count == 0);
    blocklist_free(&missing);

    printf(all_ok ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    remove(path);
    return all_ok ? 0 : 1;
}

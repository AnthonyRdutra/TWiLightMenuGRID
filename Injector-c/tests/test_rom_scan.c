/* Tests for src/rom_scan.c against a controlled fixture tree: real ROMs (including a
 * subdirectory), AppleDouble sidecars (both by name and by magic-number content), and a
 * non-ROM file -- mirrors assetbind/scan_and_bind.py's find_roms()/_is_appledouble(). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "rom_scan.h"

static int all_ok = 1;

static void check(const char *what, int cond) {
    printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
    all_ok &= cond;
}

static int list_contains(const RomList *l, const char *needle) {
    for (size_t i = 0; i < l->count; i++) {
        if (strstr(l->paths[i], needle)) return 1;
    }
    return 0;
}

int main(void) {
    system("rm -rf /tmp/injector_test_roms");
    system("mkdir -p /tmp/injector_test_roms/sub");
    system("printf 'nds-rom-bytes' > /tmp/injector_test_roms/Game1.nds");
    system("printf 'nds-rom-bytes' > /tmp/injector_test_roms/sub/Game2.NDS"); /* uppercase ext */
    system("printf 'not a rom' > /tmp/injector_test_roms/readme.txt");
    system("printf 'no magic here' > /tmp/injector_test_roms/._NamedSidecar.nds"); /* by name */
    /* AppleDouble by content: magic 0x00051607 followed by junk, normal-looking filename. */
    system("printf '\\x00\\x05\\x16\\x07junk' > /tmp/injector_test_roms/ContentSidecar.nds");

    RomList roms;
    memset(&roms, 0, sizeof(roms));
    rom_scan_find("/tmp/injector_test_roms", &roms);

    check("finds exactly the 2 real ROMs", roms.count == 2);
    check("finds the top-level ROM", list_contains(&roms, "Game1.nds"));
    check("finds the ROM in a subdirectory (recursive)", list_contains(&roms, "sub/Game2.NDS"));
    check("skips the non-ROM file", !list_contains(&roms, "readme.txt"));
    check("skips the AppleDouble sidecar (by name)", !list_contains(&roms, "NamedSidecar"));
    check("skips the AppleDouble sidecar (by magic number)", !list_contains(&roms, "ContentSidecar"));

    if (roms.count == 2) {
        check("sorted alphabetically", strcmp(roms.paths[0], roms.paths[1]) < 0);
    }

    rom_scan_free(&roms);
    check("rom_scan_free clears the list", roms.paths == NULL && roms.count == 0);

    RomList empty;
    memset(&empty, 0, sizeof(empty));
    rom_scan_find("/tmp/injector_test_roms_does_not_exist", &empty);
    check("missing base dir: empty list, no crash", empty.count == 0);
    rom_scan_free(&empty);

    printf(all_ok ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    system("rm -rf /tmp/injector_test_roms");
    return all_ok ? 0 : 1;
}

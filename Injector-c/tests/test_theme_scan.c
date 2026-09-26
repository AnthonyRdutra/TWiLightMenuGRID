/* Tests for src/theme_scan.c against a controlled fixture directory: a folder with a real
 * theme.ini, a folder with only theme.json (the merged theme.ini+layout.json format -- see
 * FRONTEND.md §19), a folder without either, and a plain file -- mirrors deploy.py's
 * find_bundled_theme_dirs() ("immediate subfolders that directly contain theme.ini or
 * theme.json"). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "theme_scan.h"

static int all_ok = 1;

static void check(const char *what, int cond) {
    printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
    all_ok &= cond;
}

int main(void) {
    system("rm -rf /tmp/injector_test_themes");
    system("mkdir -p '/tmp/injector_test_themes/Zebra Theme'");
    system("mkdir -p '/tmp/injector_test_themes/Alpha Theme'");
    system("mkdir -p '/tmp/injector_test_themes/Json Only Theme'");
    system("mkdir -p /tmp/injector_test_themes/no_ini_here");
    system("touch '/tmp/injector_test_themes/Zebra Theme/theme.ini'");
    system("touch '/tmp/injector_test_themes/Alpha Theme/theme.ini'");
    system("touch '/tmp/injector_test_themes/Json Only Theme/theme.json'"); /* no theme.ini at all */
    system("touch /tmp/injector_test_themes/not_a_dir_theme.ini"); /* plain file, not a folder */

    BundledThemeList list;
    memset(&list, 0, sizeof(list));
    theme_scan_bundled("/tmp/injector_test_themes", &list);

    check("finds all 3 theme folders (theme.ini or theme.json)", list.count == 3);
    if (list.count == 3) {
        /* Must be sorted alphabetically. */
        check("sorted: Alpha Theme first", strcmp(list.items[0].name, "Alpha Theme") == 0);
        check("sorted: Json Only Theme second", strcmp(list.items[1].name, "Json Only Theme") == 0);
        check("sorted: Zebra Theme third", strcmp(list.items[2].name, "Zebra Theme") == 0);
        check("path is the full directory path", strstr(list.items[0].path, "Alpha Theme") != NULL);
    }

    theme_scan_free(&list);
    check("theme_scan_free clears the list", list.items == NULL && list.count == 0);

    /* Missing base dir: must not crash, just returns an empty list. */
    BundledThemeList empty;
    memset(&empty, 0, sizeof(empty));
    theme_scan_bundled("/tmp/injector_test_themes_does_not_exist", &empty);
    check("missing base dir: empty list, no crash", empty.count == 0);
    theme_scan_free(&empty);

    printf(all_ok ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    system("rm -rf /tmp/injector_test_themes");
    return all_ok ? 0 : 1;
}

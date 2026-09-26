/* Round-trip tests for src/ini.c against the exact behaviors documented in ini.h -- ported from
 * (and checked against) Injector/deploy.py's set_ini_keys()/_read_ini_lines()/_write_ini_lines(). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ini.h"

static int all_ok = 1;

static void check(const char *what, int cond) {
    printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
    all_ok &= cond;
}

static const char *tmp_path(void) {
    return "/tmp/injector_test_settings.ini";
}

static void write_raw(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    fwrite(content, 1, strlen(content), f);
    fclose(f);
}

static char *read_raw(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    fread(buf, 1, (size_t)n, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(void) {
    const char *path = tmp_path();
    remove(path);

    /* 1. Missing file reads as empty, no error. */
    {
        IniLines l;
        int rc = ini_read(path, &l);
        check("missing file: rc==0", rc == 0);
        check("missing file: 0 lines", l.count == 0);
        ini_free(&l);
    }

    /* 2. Setting keys with no existing file/section appends [SECTION] + keys at EOF. */
    {
        IniLines l;
        ini_read(path, &l);
        IniKV kv[2] = {{"THEME", "0"}, {"DSI_THEME", "Default grid theme"}};
        ini_set_keys(&l, "SRLOADER", kv, 2);
        ini_write(path, &l);
        ini_free(&l);

        char *raw = read_raw(path);
        check("fresh section: has header",
              strstr(raw, "[SRLOADER]\r\n") != NULL);
        check("fresh section: has THEME",
              strstr(raw, "THEME = 0\r\n") != NULL);
        check("fresh section: has DSI_THEME",
              strstr(raw, "DSI_THEME = Default grid theme\r\n") != NULL);
        /* Header must come before both keys, keys in kv[] order right after it. */
        char *hdr = strstr(raw, "[SRLOADER]");
        char *k1 = strstr(raw, "THEME = 0");
        char *k2 = strstr(raw, "DSI_THEME =");
        check("fresh section: header before THEME before DSI_THEME", hdr && k1 && k2 && hdr < k1 && k1 < k2);
        free(raw);
    }

    /* 3. Re-running with a changed value updates the existing key in place, leaves everything
     *    else (including an unrelated section and a comment) untouched, and preserves the
     *    comment -- the one deliberate behavior change from the Python original. */
    {
        write_raw(path,
            "; user comment above the section\r\n"
            "[OTHER]\r\n"
            "FOO = bar\r\n"
            "\r\n"
            "[SRLOADER]\r\n"
            "THEME = 0\r\n"
            "; a commented-out override, must not be touched\r\n"
            "; DSI_THEME = should_not_be_uncommented\r\n"
            "SOME_OTHER_KEY = keepme\r\n"
            "[NEXT]\r\n"
            "X = 1\r\n");

        IniLines l;
        ini_read(path, &l);
        IniKV kv[2] = {{"THEME", "1"}, {"DSI_THEME", "Custom Theme"}};
        ini_set_keys(&l, "SRLOADER", kv, 2);
        ini_write(path, &l);
        ini_free(&l);

        char *raw = read_raw(path);
        check("update: THEME changed to 1", strstr(raw, "THEME = 1\r\n") != NULL);
        check("update: old THEME=0 gone", strstr(raw, "THEME = 0\r\n") == NULL);
        check("update: DSI_THEME inserted", strstr(raw, "DSI_THEME = Custom Theme\r\n") != NULL);
        check("update: comment above section preserved",
              strstr(raw, "; user comment above the section\r\n") != NULL);
        check("update: commented-out DSI_THEME left alone, not uncommented",
              strstr(raw, "; DSI_THEME = should_not_be_uncommented\r\n") != NULL);
        check("update: unrelated OTHER section untouched", strstr(raw, "[OTHER]\r\nFOO = bar\r\n") != NULL);
        check("update: unrelated key in SRLOADER untouched", strstr(raw, "SOME_OTHER_KEY = keepme\r\n") != NULL);
        check("update: NEXT section untouched", strstr(raw, "[NEXT]\r\nX = 1\r\n") != NULL);

        /* DSI_THEME must land at the END of the section's existing content -- after every
         * pre-existing line in SRLOADER (including the trailing comment), before [NEXT] starts
         * (matches ini_set_keys' documented insertion point: end of section, not right after the
         * header). */
        char *hdr = strstr(raw, "[SRLOADER]");
        char *some_other = strstr(raw, "SOME_OTHER_KEY");
        char *dsi = strstr(raw, "DSI_THEME = Custom Theme");
        char *next = strstr(raw, "[NEXT]");
        check("update: DSI_THEME inserted at end of section, after pre-existing keys, before [NEXT]",
              hdr && some_other && dsi && next && hdr < some_other && some_other < dsi && dsi < next);
        free(raw);
    }

    printf(all_ok ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    remove(path);
    return all_ok ? 0 : 1;
}

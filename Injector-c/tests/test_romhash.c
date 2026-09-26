/* Tests for src/romhash.c: hash correctness against Python hashlib/zlib ground truth, and the
 * size+mtime JSON cache round-trip/invalidation behavior from scan_and_bind.py's
 * hash_file_cached()/load_hash_cache()/save_hash_cache(). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <utime.h>

#include "romhash.h"

static int all_ok = 1;

static void check(const char *what, int cond) {
    printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
    all_ok &= cond;
}

int main(void) {
    system("rm -rf /tmp/injector_test_romhash");
    system("mkdir -p /tmp/injector_test_romhash/cache");

    const char *rom_path = "/tmp/injector_test_romhash/rom.nds";
    FILE *f = fopen(rom_path, "wb");
    const char *content = "TWiLightMenuGRID test ROM content 1234567890";
    fwrite(content, 1, strlen(content), f);
    fclose(f);

    /* 1. Hash correctness, verified against Python's hashlib.sha1/md5 + zlib.crc32 for the same
     * bytes (see conversation -- computed once via `python3 -c ...` as ground truth). */
    {
        RomHash h;
        int rc = romhash_file(rom_path, &h);
        check("romhash_file: rc==0", rc == 0);
        check("romhash_file: sha1 matches Python ground truth",
              strcmp(h.sha1, "a93d887bac54340720063d4f3b7e88bacf7d6df0") == 0);
        check("romhash_file: md5 matches Python ground truth",
              strcmp(h.md5, "5bb427465e2801e769d8f37f09798b3a") == 0);
        check("romhash_file: crc32 matches Python ground truth",
              strcmp(h.crc32, "0c344585") == 0);
        check("romhash_file: size matches", h.size == (long long)strlen(content));
    }

    /* 2. Cache: first lookup is a miss (fresh hash), gets recorded; save+reload; second lookup
     * (same file, untouched) is a cache hit with an identical result. */
    {
        RomHashCache *cache = romhash_cache_load("/tmp/injector_test_romhash/cache");
        check("cache_load on empty dir: non-NULL", cache != NULL);

        RomHash h1;
        int was_cached = -1;
        int rc = romhash_file_cached(cache, rom_path, &h1, &was_cached);
        check("first lookup: rc==0", rc == 0);
        check("first lookup: was a miss", was_cached == 0);
        check("first lookup: hash correct",
              strcmp(h1.sha1, "a93d887bac54340720063d4f3b7e88bacf7d6df0") == 0);

        rc = romhash_cache_save(cache, "/tmp/injector_test_romhash/cache");
        check("cache_save: rc==0", rc == 0);
        romhash_cache_free(cache);

        RomHashCache *reloaded = romhash_cache_load("/tmp/injector_test_romhash/cache");
        check("cache_load after save: non-NULL", reloaded != NULL);

        RomHash h2;
        was_cached = -1;
        rc = romhash_file_cached(reloaded, rom_path, &h2, &was_cached);
        check("second lookup (after reload): rc==0", rc == 0);
        check("second lookup: was a cache hit", was_cached == 1);
        check("second lookup: hash matches first", strcmp(h1.sha1, h2.sha1) == 0 &&
                                                     strcmp(h1.md5, h2.md5) == 0 &&
                                                     strcmp(h1.crc32, h2.crc32) == 0 &&
                                                     h1.size == h2.size);

        /* 3. Invalidation: touching the file's mtime must force a fresh hash next time. */
        struct utimbuf new_time;
        new_time.actime = 2000000000;
        new_time.modtime = 2000000000;
        utime(rom_path, &new_time);

        RomHash h3;
        was_cached = -1;
        rc = romhash_file_cached(reloaded, rom_path, &h3, &was_cached);
        check("after mtime change: rc==0", rc == 0);
        check("after mtime change: cache correctly treated as stale (miss)", was_cached == 0);
        check("after mtime change: hash still correct (content unchanged)",
              strcmp(h3.sha1, "a93d887bac54340720063d4f3b7e88bacf7d6df0") == 0);

        romhash_cache_free(reloaded);
    }

    printf(all_ok ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    system("rm -rf /tmp/injector_test_romhash");
    return all_ok ? 0 : 1;
}

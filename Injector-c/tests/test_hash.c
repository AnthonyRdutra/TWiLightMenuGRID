/* Sanity check for the vendored SHA1/MD5/CRC32 against known test vectors, before anything else
 * in the project depends on them being correct (romhash.c is the single source of truth for game
 * identity -- a silent hashing bug there would corrupt the whole asset-binding pipeline). */
#include <stdio.h>
#include <string.h>

#include "../vendor/sha1.h"
#include "../vendor/md5.h"
#include "../vendor/crc32.h"

static void to_hex(const unsigned char *buf, size_t len, char *out) {
    static const char *hexd = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hexd[buf[i] >> 4];
        out[i * 2 + 1] = hexd[buf[i] & 0xF];
    }
    out[len * 2] = '\0';
}

static int check(const char *what, const char *got, const char *want) {
    int ok = strcmp(got, want) == 0;
    printf("[%s] %s: got=%s want=%s\n", ok ? "OK" : "FAIL", what, got, want);
    return ok;
}

int main(void) {
    int all_ok = 1;
    const char *msg = "abc";

    /* SHA1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d (FIPS 180-2 test vector, verified against Python hashlib.sha1) */
    {
        SHA1_CTX ctx;
        BYTE digest[SHA1_BLOCK_SIZE];
        char hex[SHA1_BLOCK_SIZE * 2 + 1];
        sha1_init(&ctx);
        sha1_update(&ctx, (const BYTE *)msg, strlen(msg));
        sha1_final(&ctx, digest);
        to_hex(digest, SHA1_BLOCK_SIZE, hex);
        all_ok &= check("sha1(abc)", hex, "a9993e364706816aba3e25717850c26c9cd0d89d");
    }

    /* MD5("abc") = 900150983cd24fb0d6963f7d28e17f72 (RFC 1321 test vector) */
    {
        MD5_CTX ctx;
        BYTE digest[MD5_BLOCK_SIZE];
        char hex[MD5_BLOCK_SIZE * 2 + 1];
        md5_init(&ctx);
        md5_update(&ctx, (const BYTE *)msg, strlen(msg));
        md5_final(&ctx, digest);
        to_hex(digest, MD5_BLOCK_SIZE, hex);
        all_ok &= check("md5(abc)", hex, "900150983cd24fb0d6963f7d28e17f72");
    }

    /* CRC32("abc") = 0x352441c2 (standard zlib/PNG CRC-32, matches Python's zlib.crc32(b"abc")) */
    {
        uint32_t crc = crc32_update(0, msg, strlen(msg));
        char hex[9];
        snprintf(hex, sizeof(hex), "%08x", crc);
        all_ok &= check("crc32(abc)", hex, "352441c2");
    }

    /* Streaming CRC32 across two chunks must equal the one-shot result over the concatenation. */
    {
        uint32_t crc = crc32_update(0, "ab", 2);
        crc = crc32_update(crc, "c", 1);
        char hex[9];
        snprintf(hex, sizeof(hex), "%08x", crc);
        all_ok &= check("crc32(ab+c streamed)", hex, "352441c2");
    }

    /* Streaming SHA1/MD5 across two update() calls must equal a single update() over the whole. */
    {
        SHA1_CTX ctx;
        BYTE digest[SHA1_BLOCK_SIZE];
        char hex[SHA1_BLOCK_SIZE * 2 + 1];
        sha1_init(&ctx);
        sha1_update(&ctx, (const BYTE *)"ab", 2);
        sha1_update(&ctx, (const BYTE *)"c", 1);
        sha1_final(&ctx, digest);
        to_hex(digest, SHA1_BLOCK_SIZE, hex);
        all_ok &= check("sha1(ab+c streamed)", hex, "a9993e364706816aba3e25717850c26c9cd0d89d");
    }

    printf(all_ok ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    return all_ok ? 0 : 1;
}

#include "romhash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

#include "sha1.h"
#include "md5.h"
#include "crc32.h"
#include "cJSON.h"

#define HASH_CACHE_FILENAME "rom_hash_cache.json"

static void to_hex(const unsigned char *buf, size_t len, char *out) {
    static const char *hexd = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hexd[buf[i] >> 4];
        out[i * 2 + 1] = hexd[buf[i] & 0xF];
    }
    out[len * 2] = '\0';
}

int romhash_file(const char *path, RomHash *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    SHA1_CTX sha1_ctx;
    MD5_CTX md5_ctx;
    sha1_init(&sha1_ctx);
    md5_init(&md5_ctx);
    uint32_t crc = 0;
    long long size = 0;

    static unsigned char chunk[1 << 20]; /* 1 MiB, matches rom_hash.py's default chunk size */
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        sha1_update(&sha1_ctx, chunk, n);
        md5_update(&md5_ctx, chunk, n);
        crc = crc32_update(crc, chunk, n);
        size += (long long)n;
    }
    int had_error = ferror(f);
    fclose(f);
    if (had_error) return -1;

    unsigned char sha1_digest[SHA1_BLOCK_SIZE];
    unsigned char md5_digest[MD5_BLOCK_SIZE];
    sha1_final(&sha1_ctx, sha1_digest);
    md5_final(&md5_ctx, md5_digest);

    to_hex(sha1_digest, SHA1_BLOCK_SIZE, out->sha1);
    to_hex(md5_digest, MD5_BLOCK_SIZE, out->md5);
    snprintf(out->crc32, sizeof(out->crc32), "%08x", crc);
    out->size = size;
    return 0;
}

/* Extracts a Python-os.stat()-compatible mtime (seconds + fractional nanoseconds as a double) so
 * cache comparisons behave the same way Python's `entry.get("mtime") == st.st_mtime` does. */
static double stat_mtime(const struct stat *st) {
#if defined(__APPLE__)
    return (double)st->st_mtimespec.tv_sec + (double)st->st_mtimespec.tv_nsec / 1e9;
#elif defined(__linux__)
    return (double)st->st_mtim.tv_sec + (double)st->st_mtim.tv_nsec / 1e9;
#else
    return (double)st->st_mtime;
#endif
}

typedef struct {
    char *path;
    long long size;
    double mtime;
    RomHash hash;
} CacheEntry;

struct RomHashCache {
    CacheEntry *entries;
    size_t count;
    size_t capacity;
};

static CacheEntry *cache_find(RomHashCache *c, const char *path) {
    for (size_t i = 0; i < c->count; i++) {
        if (strcmp(c->entries[i].path, path) == 0) return &c->entries[i];
    }
    return NULL;
}

static CacheEntry *cache_upsert(RomHashCache *c, const char *path) {
    CacheEntry *e = cache_find(c, path);
    if (e) return e;
    if (c->count >= c->capacity) {
        size_t new_cap = c->capacity ? c->capacity * 2 : 64;
        c->entries = (CacheEntry *)realloc(c->entries, new_cap * sizeof(CacheEntry));
        c->capacity = new_cap;
    }
    e = &c->entries[c->count++];
    memset(e, 0, sizeof(*e));
    e->path = strdup(path);
    return e;
}

static char *read_whole_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

RomHashCache *romhash_cache_load(const char *cache_dir) {
    RomHashCache *c = (RomHashCache *)calloc(1, sizeof(RomHashCache));

    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", cache_dir, HASH_CACHE_FILENAME);

    char *text = read_whole_file(path);
    if (!text) return c; /* missing file: empty cache, matches load_hash_cache()'s except-return-{} */

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root || !cJSON_IsObject(root)) {
        /* Unparseable JSON: also tolerated, matches the Python original. */
        if (root) cJSON_Delete(root);
        return c;
    }

    cJSON *entry_json;
    cJSON_ArrayForEach(entry_json, root) {
        if (!cJSON_IsObject(entry_json)) continue;
        cJSON *size_j = cJSON_GetObjectItemCaseSensitive(entry_json, "size");
        cJSON *mtime_j = cJSON_GetObjectItemCaseSensitive(entry_json, "mtime");
        cJSON *hash_j = cJSON_GetObjectItemCaseSensitive(entry_json, "hash");
        if (!cJSON_IsNumber(size_j) || !cJSON_IsNumber(mtime_j) || !cJSON_IsObject(hash_j)) continue;

        cJSON *h_sha1 = cJSON_GetObjectItemCaseSensitive(hash_j, "sha1");
        cJSON *h_md5 = cJSON_GetObjectItemCaseSensitive(hash_j, "md5");
        cJSON *h_crc32 = cJSON_GetObjectItemCaseSensitive(hash_j, "crc32");
        cJSON *h_size = cJSON_GetObjectItemCaseSensitive(hash_j, "size");
        if (!cJSON_IsString(h_sha1) || !cJSON_IsString(h_md5) || !cJSON_IsString(h_crc32) ||
            !cJSON_IsNumber(h_size)) {
            continue;
        }

        CacheEntry *e = cache_upsert(c, entry_json->string);
        e->size = (long long)size_j->valuedouble;
        e->mtime = mtime_j->valuedouble;
        snprintf(e->hash.sha1, sizeof(e->hash.sha1), "%s", h_sha1->valuestring);
        snprintf(e->hash.md5, sizeof(e->hash.md5), "%s", h_md5->valuestring);
        snprintf(e->hash.crc32, sizeof(e->hash.crc32), "%s", h_crc32->valuestring);
        e->hash.size = (long long)h_size->valuedouble;
    }

    cJSON_Delete(root);
    return c;
}

int romhash_cache_save(const RomHashCache *cache, const char *cache_dir) {
#if defined(_WIN32)
    _mkdir(cache_dir);
#else
    mkdir(cache_dir, 0755);
#endif

    cJSON *root = cJSON_CreateObject();
    for (size_t i = 0; i < cache->count; i++) {
        const CacheEntry *e = &cache->entries[i];
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "size", (double)e->size);
        cJSON_AddNumberToObject(entry, "mtime", e->mtime);
        cJSON *hash = cJSON_CreateObject();
        cJSON_AddStringToObject(hash, "sha1", e->hash.sha1);
        cJSON_AddStringToObject(hash, "md5", e->hash.md5);
        cJSON_AddStringToObject(hash, "crc32", e->hash.crc32);
        cJSON_AddNumberToObject(hash, "size", (double)e->hash.size);
        cJSON_AddItemToObject(entry, "hash", hash);
        cJSON_AddItemToObject(root, e->path, entry);
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return -1;

    char path[4096], tmp[4200];
    snprintf(path, sizeof(path), "%s/%s", cache_dir, HASH_CACHE_FILENAME);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "wb");
    if (!f) { free(text); return -1; }
    size_t len = strlen(text);
    size_t written = fwrite(text, 1, len, f);
    free(text);
    if (fclose(f) != 0 || written != len) return -1;

    if (rename(tmp, path) != 0) return -1;
    return 0;
}

void romhash_cache_free(RomHashCache *cache) {
    if (!cache) return;
    for (size_t i = 0; i < cache->count; i++) free(cache->entries[i].path);
    free(cache->entries);
    free(cache);
}

int romhash_file_cached(RomHashCache *cache, const char *path, RomHash *out, int *was_cached) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    CacheEntry *e = cache_find(cache, path);
    if (e && e->size == (long long)st.st_size && e->mtime == stat_mtime(&st)) {
        *out = e->hash;
        if (was_cached) *was_cached = 1;
        return 0;
    }

    RomHash h;
    if (romhash_file(path, &h) != 0) return -1;

    CacheEntry *ne = cache_upsert(cache, path);
    ne->size = (long long)st.st_size;
    ne->mtime = stat_mtime(&st);
    ne->hash = h;

    *out = h;
    if (was_cached) *was_cached = 0;
    return 0;
}

/* ROM content identity: single-pass sha1+md5+crc32+size, plus a size+mtime JSON cache so a
 * re-scan of an already-processed library only costs a stat() per file. Ported 1:1 from
 * assetbind/rom_hash.py (hash_file) and assetbind/scan_and_bind.py (load_hash_cache /
 * save_hash_cache / hash_file_cached). ROM files aren't edited in place, so cache correctness
 * only needs "size+mtime still match" -- not a content re-check. */
#ifndef INJECTOR_ROMHASH_H
#define INJECTOR_ROMHASH_H

typedef struct {
    char sha1[41];  /* 40 hex chars + NUL */
    char md5[33];   /* 32 hex chars + NUL */
    char crc32[9];  /* 8 hex chars + NUL */
    long long size;
} RomHash;

/* Hashes the WHOLE file (no header stripping -- ROM headers vary by system/mapper and aren't
 * reliably strippable in general, so content identity is defined as "every byte"). Streams in
 * 1 MiB chunks. Returns 0 on success, -1 if the file can't be opened/read. */
int romhash_file(const char *path, RomHash *out);

typedef struct RomHashCache RomHashCache; /* opaque */

/* Loads "<cache_dir>/rom_hash_cache.json". A missing or unparseable file is not an error -- an
 * empty (but valid) cache is returned, matching load_hash_cache()'s try/except-and-return-{}. */
RomHashCache *romhash_cache_load(const char *cache_dir);

/* Writes the cache back to "<cache_dir>/rom_hash_cache.json" atomically (write to a .tmp file,
 * then rename over the real path) -- matches save_hash_cache(). Returns 0 on success, -1 on
 * I/O error. */
int romhash_cache_save(const RomHashCache *cache, const char *cache_dir);

void romhash_cache_free(RomHashCache *cache);

/* hash_file_cached(): looks up `path` in `cache` by exact string match (same key deploy.py's
 * callers use -- whatever path string they pass in, verbatim, not normalized). If a cached entry
 * exists whose recorded size+mtime still match the file's current stat(), returns that cached
 * hash without reading the file's contents at all; otherwise hashes it fresh and updates `cache`
 * in memory (call romhash_cache_save() afterward to persist). `was_cached` (if non-NULL) is set
 * to 1 if the cache hit, 0 if it was freshly hashed. Returns 0 on success, -1 on I/O error. */
int romhash_file_cached(RomHashCache *cache, const char *path, RomHash *out, int *was_cached);

#endif

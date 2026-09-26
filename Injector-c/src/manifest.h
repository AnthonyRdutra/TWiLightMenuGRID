/* manifest.yml / assets_index.yml reader-writer, ported 1:1 from assetbind/yaml_io.py's
 * dump_manifest()/dump_assets_index()/_load_scoped()/load_assets_index_text() fallback parser
 * (the *only* parser that matters for this rewrite -- the PyYAML path existed solely so a human
 * could hand-edit the file with a real YAML tool; this C tool only ever reads what it itself
 * wrote, so there's no need to support general YAML here, same as the Python fallback already
 * proved sufficient for -- see docs/asset-structure-changes.md and assetbind/test_binder.py).
 *
 * Known gap, out of scope for this pass: assets_index.yml keys are supposed to be Unicode-NFC-
 * normalized ROM base names (macOS lists filenames in NFD; the DS side needs NFC to match) --
 * this port does not implement Unicode normalization (it would need real composition tables, a
 * separate chunk of work) and stores whatever string the caller passes in verbatim. Flag this
 * before wiring up real SD scanning on macOS. */
#ifndef INJECTOR_MANIFEST_H
#define INJECTOR_MANIFEST_H

#include <stddef.h>

typedef struct {
    char sha1[41];
    char md5[33];
    char crc32[9];
    long long size;
} ManifestIdentity;

typedef struct {
    char *game_id;   /* malloc'd, always present */
    ManifestIdentity identity;
    char *rom_name;  /* malloc'd, or NULL (Python: key omitted entirely when empty/falsy) */
    char *logo;         /* malloc'd path, or NULL (-> "null" on write) */
    char *video;         /* legacy field, always NULL in practice but round-tripped if present */
    char *video_top;     /* malloc'd path, or NULL */
    char *video_bottom;  /* malloc'd path, or NULL */
} ManifestGame;

typedef struct {
    int version;            /* Python default: 1 */
    int allow_name_match;   /* bool; Python: key omitted entirely on write when false */
    ManifestGame *games;
    size_t game_count;
    size_t game_capacity;
} Manifest;

void manifest_init(Manifest *m);
void manifest_free(Manifest *m);

/* Appends a new (zero-initialized) game to `m` and returns a pointer to it for the caller to
 * fill in directly (game_id/identity are mandatory; rom_name/logo/video fields may be left
 * NULL). */
ManifestGame *manifest_add_game(Manifest *m);

/* Serializes `m` exactly like dump_manifest() -- fixed field order/indentation, double-quoted
 * scalars with backslash/quote escaping, "null" (unquoted) for absent asset paths. Returns a
 * malloc'd NUL-terminated string the caller must free(). */
char *manifest_dump(const Manifest *m);

/* Parses text produced by manifest_dump() (or an actual manifest.yml on disk) into `out`
 * (zero-initialize `out` before calling, or call manifest_free() on a previous value first).
 * Matches _load_scoped()'s fixed-indentation grammar for exactly this schema. Malformed/
 * unexpected lines are skipped rather than treated as fatal, matching the tolerant style of the
 * Python original. */
void manifest_load(const char *text, Manifest *out);

/* --- assets_index.yml: ROM base name (no extension) -> game_id --------------------------- */

typedef struct {
    char *rom_name; /* malloc'd key */
    char *game_id;  /* malloc'd value */
} AssetsIndexEntry;

typedef struct {
    AssetsIndexEntry *entries;
    size_t count;
    size_t capacity;
} AssetsIndex;

void assets_index_init(AssetsIndex *idx);
void assets_index_free(AssetsIndex *idx);

/* Adds/overwrites the mapping rom_name -> game_id (duplicate rom_names from renamed/copied ROMs
 * that hash the same are expected -- each gets its own entry pointing at the same game_id, same
 * as dump_assets_index()'s "includes ALL names"). */
void assets_index_set(AssetsIndex *idx, const char *rom_name, const char *game_id);

/* Serializes exactly like dump_assets_index(): the two fixed header comment lines, "version: 1",
 * "roms:", then one "  \"name\": \"game_id\"" line per entry sorted alphabetically by rom_name.
 * Returns a malloc'd NUL-terminated string the caller must free(). */
char *assets_index_dump(const AssetsIndex *idx);

/* Parses text produced by assets_index_dump() into `out`. Matches load_assets_index_text()'s
 * fallback parser. */
void assets_index_load(const char *text, AssetsIndex *out);

#endif

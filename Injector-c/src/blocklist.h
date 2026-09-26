/* system_blocklist.txt reader + matcher: excludes TWiLightMenu system/homebrew .nds apps
 * (dsimenu.nds, pictochat.nds, ...) from scraping. Ported from assetbind/scan_and_bind.py's
 * load_blocklist()/name_blocked(). */
#ifndef INJECTOR_BLOCKLIST_H
#define INJECTOR_BLOCKLIST_H

#include <stddef.h>

typedef struct {
    char **sha1s; /* malloc'd, lowercase hex */
    size_t sha1_count;
    size_t sha1_capacity;

    char **names; /* malloc'd, lowercase substrings */
    size_t name_count;
    size_t name_capacity;
} Blocklist;

/* Reads `path` (accepted line formats, case-insensitive, '#' starts a comment:
 *   sha1:<hex>        -> exclude by content hash
 *   name:<substring>  -> exclude if the substring appears in the file name
 *   <substring>       -> shorthand for name:<substring>
 * A missing or unreadable file yields an empty (blocks nothing) list, not an error -- same as
 * the Python original. Zero-initialize `out` before calling, or blocklist_free() a previous
 * value first. */
void blocklist_load(const char *path, Blocklist *out);

void blocklist_free(Blocklist *out);

/* True if `sha1` (any case) is in the blocklist's hash set. */
int blocklist_has_sha1(const Blocklist *b, const char *sha1);

/* True if any blocklisted name substring appears in the lowercased basename of `filename`. */
int blocklist_name_blocked(const Blocklist *b, const char *filename);

#endif

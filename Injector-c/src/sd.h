/* Cross-platform SD-card (or any drive) detection, ported 1:1 from deploy.py's
 * list_mount_candidates()/looks_like_twilightmenu_sd()/find_sd_card()/eject_hint(). */
#ifndef INJECTOR_SD_H
#define INJECTOR_SD_H

#include <stddef.h>

typedef struct {
    char **paths; /* malloc'd absolute paths, caller-owned via sd_candidates_free() */
    size_t count;
    size_t capacity;
} SdCandidates;

/* True if `path`/_nds is a directory -- the same "has TWiLightMenu already been installed here"
 * check deploy.py uses. */
int sd_looks_like_twilightmenu(const char *path);

/* Scans OS-specific mount locations (macOS: entries under /Volumes; Linux: /media/$USER, /run/media/$USER,
 * /media, /mnt; Windows: GetLogicalDrives()) and appends every existing directory found to
 * `out` (zero-initialize `out` before the first call). Mirrors list_mount_candidates(). */
void sd_list_mount_candidates(SdCandidates *out);

/* Same as sd_list_mount_candidates(), filtered to only those that pass
 * sd_looks_like_twilightmenu() -- mirrors find_sd_card(). */
void sd_find_twilightmenu_cards(SdCandidates *out);

void sd_candidates_free(SdCandidates *c);

/* Writes an OS-appropriate safe-eject hint for `sd_root` into `out` (truncated to `out_size`).
 * Mirrors eject_hint(). */
void sd_eject_hint(const char *sd_root, char *out, size_t out_size);

#endif

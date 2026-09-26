/* Recursive .nds ROM discovery on an SD card, ported from assetbind/scan_and_bind.py's
 * find_roms()/_is_appledouble(). */
#ifndef INJECTOR_ROM_SCAN_H
#define INJECTOR_ROM_SCAN_H

#include <stddef.h>

typedef struct {
    char **paths; /* malloc'd absolute paths, caller-owned via rom_scan_free() */
    size_t count;
    size_t capacity;
} RomList;

/* Recursively scans `sd_dir` for ".nds" files (case-insensitive extension match), skipping
 * macOS AppleDouble sidecar files ("._foo", or any file whose first 4 bytes are the AppleDouble
 * magic number 0x00051607 -- see _is_appledouble()'s docstring in the Python original for why
 * these matter even when not currently running on a Mac). Appends sorted absolute paths to
 * `out` (zero-initialize `out` before the first call). */
void rom_scan_find(const char *sd_dir, RomList *out);

void rom_scan_free(RomList *out);

#endif

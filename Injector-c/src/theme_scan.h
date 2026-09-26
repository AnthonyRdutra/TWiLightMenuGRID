/* Bundled single-theme folder discovery, ported from deploy.py's find_bundled_theme_dirs():
 * immediate subfolders of a base directory that directly contain a theme.ini. */
#ifndef INJECTOR_THEME_SCAN_H
#define INJECTOR_THEME_SCAN_H

#include <stddef.h>

typedef struct {
    char *path; /* malloc'd, full path to the theme folder */
    char *name; /* malloc'd, basename(path) -- what gets written as DSI_THEME */
} BundledTheme;

typedef struct {
    BundledTheme *items;
    size_t count;
    size_t capacity;
} BundledThemeList;

/* Scans `base_dir` (non-recursive) for immediate subdirectories that directly contain a
 * theme.ini file, appending each to `out` (zero-initialize `out` before the first call),
 * sorted alphabetically by name -- matches sorted(os.listdir(HERE)) + the theme.ini check. */
void theme_scan_bundled(const char *base_dir, BundledThemeList *out);

void theme_scan_free(BundledThemeList *list);

#endif

#include "theme_scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

static int is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
#if defined(_WIN32)
    return (st.st_mode & S_IFMT) == S_IFDIR;
#else
    return S_ISDIR(st.st_mode);
#endif
}

static int is_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
#if defined(_WIN32)
    return (st.st_mode & S_IFMT) == S_IFREG;
#else
    return S_ISREG(st.st_mode);
#endif
}

static void list_append(BundledThemeList *out, const char *path, const char *name) {
    if (out->count >= out->capacity) {
        size_t new_cap = out->capacity ? out->capacity * 2 : 8;
        out->items = (BundledTheme *)realloc(out->items, new_cap * sizeof(BundledTheme));
        out->capacity = new_cap;
    }
    out->items[out->count].path = strdup(path);
    out->items[out->count].name = strdup(name);
    out->count++;
}

static int cmp_by_name(const void *a, const void *b) {
    return strcmp(((const BundledTheme *)a)->name, ((const BundledTheme *)b)->name);
}

void theme_scan_bundled(const char *base_dir, BundledThemeList *out) {
#if defined(_WIN32)
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", base_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        char path[4096], ini[4200], json[4200];
        snprintf(path, sizeof(path), "%s\\%s", base_dir, fd.cFileName);
        snprintf(ini, sizeof(ini), "%s\\theme.ini", path);
        snprintf(json, sizeof(json), "%s\\theme.json", path);
        if (is_file(ini) || is_file(json)) list_append(out, path, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(base_dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char path[4096], ini[4200], json[4200];
        snprintf(path, sizeof(path), "%s/%s", base_dir, ent->d_name);
        if (!is_dir(path)) continue;
        snprintf(ini, sizeof(ini), "%s/theme.ini", path);
        snprintf(json, sizeof(json), "%s/theme.json", path);
        if (is_file(ini) || is_file(json)) list_append(out, path, ent->d_name);
    }
    closedir(d);
#endif

    if (out->count > 1) qsort(out->items, out->count, sizeof(BundledTheme), cmp_by_name);
}

void theme_scan_free(BundledThemeList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].path);
        free(list->items[i].name);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

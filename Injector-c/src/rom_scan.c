#include "rom_scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

/* AppleDouble magic number (big-endian u32 0x00051607), at the start of every "._foo" sidecar
 * file macOS writes next to "foo" whenever it touches a non-HFS+ filesystem (any FAT32/exFAT SD
 * card qualifies). These aren't ROMs, but they DO match a ".nds" extension filter if the
 * original file was itself an .nds ROM -- filtered out on every OS, not just when running on a
 * Mac, since a card that ever touched one keeps these regardless of what's scanning it now. */
static int is_appledouble(const char *path, const char *filename) {
    if (filename[0] == '.' && filename[1] == '_') return 1;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char magic[4];
    size_t n = fread(magic, 1, 4, f);
    fclose(f);
    return n == 4 && magic[0] == 0x00 && magic[1] == 0x05 && magic[2] == 0x16 && magic[3] == 0x07;
}

static int has_nds_extension(const char *filename) {
    size_t len = strlen(filename);
    if (len < 4) return 0;
    const char *ext = filename + len - 4;
    return (ext[0] == '.') &&
           (ext[1] == 'n' || ext[1] == 'N') &&
           (ext[2] == 'd' || ext[2] == 'D') &&
           (ext[3] == 's' || ext[3] == 'S');
}

static void list_append(RomList *out, const char *path) {
    if (out->count >= out->capacity) {
        size_t new_cap = out->capacity ? out->capacity * 2 : 16;
        out->paths = (char **)realloc(out->paths, new_cap * sizeof(char *));
        out->capacity = new_cap;
    }
    out->paths[out->count++] = strdup(path);
}

static int cmp_path(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

#if defined(_WIN32)
static void walk(const char *dir, RomList *out) {
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            walk(path, out);
        } else if (has_nds_extension(fd.cFileName) && !is_appledouble(path, fd.cFileName)) {
            list_append(out, path);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
#else
static void walk(const char *dir, RomList *out) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            walk(path, out);
        } else if (S_ISREG(st.st_mode) && has_nds_extension(ent->d_name) &&
                   !is_appledouble(path, ent->d_name)) {
            list_append(out, path);
        }
    }
    closedir(d);
}
#endif

void rom_scan_find(const char *sd_dir, RomList *out) {
    walk(sd_dir, out);
    if (out->count > 1) qsort(out->paths, out->count, sizeof(char *), cmp_path);
}

void rom_scan_free(RomList *out) {
    if (!out) return;
    for (size_t i = 0; i < out->count; i++) free(out->paths[i]);
    free(out->paths);
    memset(out, 0, sizeof(*out));
}

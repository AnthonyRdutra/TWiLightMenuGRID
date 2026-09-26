#include "sd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
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

static void candidates_append(SdCandidates *c, const char *path) {
    if (c->count >= c->capacity) {
        size_t new_cap = c->capacity ? c->capacity * 2 : 16;
        c->paths = (char **)realloc(c->paths, new_cap * sizeof(char *));
        c->capacity = new_cap;
    }
    c->paths[c->count++] = strdup(path);
}

void sd_candidates_free(SdCandidates *c) {
    if (!c) return;
    for (size_t i = 0; i < c->count; i++) free(c->paths[i]);
    free(c->paths);
    c->paths = NULL;
    c->count = 0;
    c->capacity = 0;
}

int sd_looks_like_twilightmenu(const char *path) {
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s/_nds", path);
    return is_dir(buf);
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

#if !defined(_WIN32)
/* Lists every entry of `base` (skipping "." and "..") as "<base>/<name>", sorted alphabetically
 * to match Python's `sorted(os.listdir(base))`, appending directories only to `out`. */
static void list_dir_entries_sorted(const char *base, SdCandidates *out) {
    DIR *d = opendir(base);
    if (!d) return;

    char **names = NULL;
    size_t n = 0, cap = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (n >= cap) {
            cap = cap ? cap * 2 : 16;
            names = (char **)realloc(names, cap * sizeof(char *));
        }
        names[n++] = strdup(ent->d_name);
    }
    closedir(d);

    qsort(names, n, sizeof(char *), cmp_str);

    char pathbuf[4096];
    for (size_t i = 0; i < n; i++) {
        snprintf(pathbuf, sizeof(pathbuf), "%s/%s", base, names[i]);
        if (is_dir(pathbuf)) candidates_append(out, pathbuf);
        free(names[i]);
    }
    free(names);
}
#endif

void sd_list_mount_candidates(SdCandidates *out) {
#if defined(__APPLE__)
    if (is_dir("/Volumes")) list_dir_entries_sorted("/Volumes", out);

#elif defined(__linux__)
    const char *user = getenv("USER");
    if (!user || !*user) user = getenv("LOGNAME");

    char bases_buf[4][512];
    const char *bases[4];
    int nbases = 0;
    if (user && *user) {
        snprintf(bases_buf[nbases], sizeof(bases_buf[0]), "/media/%s", user);
        bases[nbases] = bases_buf[nbases]; nbases++;
        snprintf(bases_buf[nbases], sizeof(bases_buf[0]), "/run/media/%s", user);
        bases[nbases] = bases_buf[nbases]; nbases++;
    }
    bases[nbases++] = "/media";
    bases[nbases++] = "/mnt";

    /* Dedup by exact base-path string, matching deploy.py's `seen` set (no realpath resolution). */
    for (int i = 0; i < nbases; i++) {
        int dup = 0;
        for (int j = 0; j < i; j++) {
            if (strcmp(bases[i], bases[j]) == 0) { dup = 1; break; }
        }
        if (dup || !is_dir(bases[i])) continue;
        list_dir_entries_sorted(bases[i], out);
    }

#elif defined(_WIN32)
    DWORD mask = GetLogicalDrives();
    if (mask != 0) {
        for (int i = 0; i < 26; i++) {
            if (mask & (1u << i)) {
                char path[4] = { (char)('A' + i), ':', '\\', '\0' };
                if (is_dir(path)) candidates_append(out, path);
            }
        }
    } else {
        /* Fallback matching the Python original: try every letter blindly if the API call fails. */
        for (int i = 0; i < 26; i++) {
            char path[4] = { (char)('A' + i), ':', '\\', '\0' };
            if (is_dir(path)) candidates_append(out, path);
        }
    }
#endif
}

void sd_find_twilightmenu_cards(SdCandidates *out) {
    SdCandidates all;
    memset(&all, 0, sizeof(all));
    sd_list_mount_candidates(&all);
    for (size_t i = 0; i < all.count; i++) {
        if (sd_looks_like_twilightmenu(all.paths[i])) candidates_append(out, all.paths[i]);
    }
    sd_candidates_free(&all);
}

void sd_eject_hint(const char *sd_root, char *out, size_t out_size) {
#if defined(__APPLE__)
    snprintf(out, out_size, "diskutil eject \"%s\"", sd_root);
#elif defined(__linux__)
    snprintf(out, out_size,
             "udisksctl unmount -b \"%s\"   # or use your file manager's \"Eject\"", sd_root);
#elif defined(_WIN32)
    snprintf(out, out_size, "Use \"Safely Remove Hardware\" in the taskbar before pulling the card.");
#else
    snprintf(out, out_size, "Safely unmount the SD card before removing it.");
#endif
}

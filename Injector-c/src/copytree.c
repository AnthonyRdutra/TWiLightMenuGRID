#include "copytree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#if defined(_WIN32)
#include <windows.h>
#include <sys/utime.h>
#else
#include <dirent.h>
#include <utime.h>
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

static int mkdir_p_one(const char *path) {
#if defined(_WIN32)
    if (CreateDirectoryA(path, NULL)) return 0;
    return (GetLastError() == ERROR_ALREADY_EXISTS) ? 0 : -1;
#else
    if (mkdir(path, 0755) == 0) return 0;
    return (errno == EEXIST) ? 0 : -1;
#endif
}

/* Creates every path component of `dir`, tolerating components that already exist -- like
 * os.makedirs(exist_ok=True). */
int fs_makedirs(const char *dir) {
    char buf[4096];
    size_t len = strlen(dir);
    if (len == 0 || len >= sizeof(buf)) return -1;
    memcpy(buf, dir, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char saved = buf[i];
            buf[i] = '\0';
            if (buf[0] != '\0' && !is_dir(buf)) {
                if (mkdir_p_one(buf) != 0) return -1;
            }
            buf[i] = saved;
        }
    }
    if (!is_dir(buf)) {
        if (mkdir_p_one(buf) != 0) return -1;
    }
    return 0;
}

static int should_skip_name(const char *name) {
    if (strcmp(name, ".DS_Store") == 0) return 1;
    if (strncmp(name, "._", 2) == 0) return 1;
    return 0;
}

/* Copies file contents + mtime (best-effort, like shutil.copy2 -- mtime failure doesn't fail the
 * whole copy, matching how Python only cares that the bytes made it over). */
int copytree_copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }

    char buf[65536];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    }
    if (ferror(in)) ok = 0;
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    if (!ok) return -1;

    struct stat st;
    if (stat(src, &st) == 0) {
#if defined(_WIN32)
        struct _utimbuf times;
        times.actime = st.st_atime;
        times.modtime = st.st_mtime;
        _utime(dst, &times);
#else
        struct utimbuf times;
        times.actime = st.st_atime;
        times.modtime = st.st_mtime;
        utime(dst, &times);
#endif
    }
    return 0;
}

#if defined(_WIN32)
static int walk_and_copy(const char *src, const char *dst) {
    if (fs_makedirs(dst) != 0) return -1;

    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", src);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0; /* empty/unreadable dir: nothing to copy */

    int rc = 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char child_src[4096], child_dst[4096];
        snprintf(child_src, sizeof(child_src), "%s\\%s", src, fd.cFileName);
        snprintf(child_dst, sizeof(child_dst), "%s\\%s", dst, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (walk_and_copy(child_src, child_dst) != 0) rc = -1;
        } else {
            if (should_skip_name(fd.cFileName)) continue;
            if (copytree_copy_file(child_src, child_dst) != 0) rc = -1;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return rc;
}
#else
static int walk_and_copy(const char *src, const char *dst) {
    if (fs_makedirs(dst) != 0) return -1;

    DIR *d = opendir(src);
    if (!d) return 0; /* unreadable/empty dir: nothing to copy, matches os.walk's tolerance */

    int rc = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        char child_src[4096], child_dst[4096];
        snprintf(child_src, sizeof(child_src), "%s/%s", src, ent->d_name);
        snprintf(child_dst, sizeof(child_dst), "%s/%s", dst, ent->d_name);

        if (is_dir(child_src)) {
            if (walk_and_copy(child_src, child_dst) != 0) rc = -1;
        } else {
            if (should_skip_name(ent->d_name)) continue;
            if (copytree_copy_file(child_src, child_dst) != 0) rc = -1;
        }
    }
    closedir(d);
    return rc;
}
#endif

int copytree_merge(const char *src, const char *dst) {
    if (!is_dir(src)) return -1;
    return walk_and_copy(src, dst);
}

#include "blocklist.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void to_lower_inplace(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static void trim_inplace(char *s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
}

static void sha1_append(Blocklist *out, const char *sha1) {
    if (out->sha1_count >= out->sha1_capacity) {
        size_t new_cap = out->sha1_capacity ? out->sha1_capacity * 2 : 8;
        out->sha1s = (char **)realloc(out->sha1s, new_cap * sizeof(char *));
        out->sha1_capacity = new_cap;
    }
    out->sha1s[out->sha1_count++] = strdup(sha1);
}

static void name_append(Blocklist *out, const char *name) {
    if (out->name_count >= out->name_capacity) {
        size_t new_cap = out->name_capacity ? out->name_capacity * 2 : 8;
        out->names = (char **)realloc(out->names, new_cap * sizeof(char *));
        out->name_capacity = new_cap;
    }
    out->names[out->name_count++] = strdup(name);
}

void blocklist_load(const char *path, Blocklist *out) {
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        trim_inplace(line);
        if (line[0] == '\0') continue;
        to_lower_inplace(line);

        if (strncmp(line, "sha1:", 5) == 0) {
            char *v = line + 5;
            trim_inplace(v);
            if (v[0]) sha1_append(out, v);
        } else if (strncmp(line, "name:", 5) == 0) {
            char *v = line + 5;
            trim_inplace(v);
            if (v[0]) name_append(out, v);
        } else {
            name_append(out, line);
        }
    }
    fclose(f);
}

void blocklist_free(Blocklist *out) {
    if (!out) return;
    for (size_t i = 0; i < out->sha1_count; i++) free(out->sha1s[i]);
    free(out->sha1s);
    for (size_t i = 0; i < out->name_count; i++) free(out->names[i]);
    free(out->names);
    memset(out, 0, sizeof(*out));
}

int blocklist_has_sha1(const Blocklist *b, const char *sha1) {
    char lower[64];
    size_t n = strlen(sha1);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    memcpy(lower, sha1, n);
    lower[n] = '\0';
    to_lower_inplace(lower);

    for (size_t i = 0; i < b->sha1_count; i++) {
        if (strcmp(b->sha1s[i], lower) == 0) return 1;
    }
    return 0;
}

int blocklist_name_blocked(const Blocklist *b, const char *filename) {
    const char *slash = strrchr(filename, '/');
    const char *bslash = strrchr(filename, '\\');
    const char *base = filename;
    if (slash && slash + 1 > base) base = slash + 1;
    if (bslash && bslash + 1 > base) base = bslash + 1;

    char lower[512];
    size_t n = strlen(base);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    memcpy(lower, base, n);
    lower[n] = '\0';
    to_lower_inplace(lower);

    for (size_t i = 0; i < b->name_count; i++) {
        if (strstr(lower, b->names[i]) != NULL) return 1;
    }
    return 0;
}

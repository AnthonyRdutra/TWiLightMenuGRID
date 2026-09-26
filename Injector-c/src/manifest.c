#include "manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------- tiny growable string buffer ------------------------------ */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static void sb_init(StrBuf *sb) {
    sb->cap = 256;
    sb->data = (char *)malloc(sb->cap);
    sb->data[0] = '\0';
    sb->len = 0;
}

static void sb_append(StrBuf *sb, const char *s) {
    size_t n = strlen(s);
    if (sb->len + n + 1 > sb->cap) {
        size_t new_cap = sb->cap * 2;
        while (new_cap < sb->len + n + 1) new_cap *= 2;
        sb->data = (char *)realloc(sb->data, new_cap);
        sb->cap = new_cap;
    }
    memcpy(sb->data + sb->len, s, n + 1);
    sb->len += n;
}

/* ------------------------------------ quoting (writer) ------------------------------------ */

/* Mirrors yaml_io.py's _q(): backslash-escape '\' and '"', wrap in double quotes. Returns a
 * malloc'd string the caller must free(). */
static char *quote(const char *s) {
    size_t extra = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '\\' || *p == '"') extra++;
    }
    size_t n = strlen(s);
    char *out = (char *)malloc(n + extra + 3);
    char *w = out;
    *w++ = '"';
    for (const char *p = s; *p; p++) {
        if (*p == '\\' || *p == '"') *w++ = '\\';
        *w++ = *p;
    }
    *w++ = '"';
    *w = '\0';
    return out;
}

static void append_quoted_or_null(StrBuf *sb, const char *prefix, const char *value) {
    char line[4200];
    if (value && value[0]) {
        char *q = quote(value);
        snprintf(line, sizeof(line), "%s%s\n", prefix, q);
        free(q);
    } else {
        snprintf(line, sizeof(line), "%snull\n", prefix);
    }
    sb_append(sb, line);
}

/* ------------------------------------- Manifest API ---------------------------------------- */

void manifest_init(Manifest *m) {
    m->version = 1;
    m->allow_name_match = 0;
    m->games = NULL;
    m->game_count = 0;
    m->game_capacity = 0;
}

void manifest_free(Manifest *m) {
    if (!m) return;
    for (size_t i = 0; i < m->game_count; i++) {
        ManifestGame *g = &m->games[i];
        free(g->game_id);
        free(g->rom_name);
        free(g->logo);
        free(g->video);
        free(g->video_top);
        free(g->video_bottom);
    }
    free(m->games);
    memset(m, 0, sizeof(*m));
}

ManifestGame *manifest_add_game(Manifest *m) {
    if (m->game_count >= m->game_capacity) {
        size_t new_cap = m->game_capacity ? m->game_capacity * 2 : 16;
        m->games = (ManifestGame *)realloc(m->games, new_cap * sizeof(ManifestGame));
        m->game_capacity = new_cap;
    }
    ManifestGame *g = &m->games[m->game_count++];
    memset(g, 0, sizeof(*g));
    return g;
}

char *manifest_dump(const Manifest *m) {
    StrBuf sb;
    sb_init(&sb);
    char line[4200];

    snprintf(line, sizeof(line), "version: %d\n", m->version ? m->version : 1);
    sb_append(&sb, line);
    if (m->allow_name_match) sb_append(&sb, "allow_name_match: true\n");
    sb_append(&sb, "games:\n");

    for (size_t i = 0; i < m->game_count; i++) {
        const ManifestGame *g = &m->games[i];

        char *q = quote(g->game_id ? g->game_id : "");
        snprintf(line, sizeof(line), "  - game_id: %s\n", q);
        sb_append(&sb, line);
        free(q);

        sb_append(&sb, "    identity:\n");
        q = quote(g->identity.sha1);
        snprintf(line, sizeof(line), "      sha1: %s\n", q);
        sb_append(&sb, line);
        free(q);
        q = quote(g->identity.md5);
        snprintf(line, sizeof(line), "      md5: %s\n", q);
        sb_append(&sb, line);
        free(q);
        q = quote(g->identity.crc32);
        snprintf(line, sizeof(line), "      crc32: %s\n", q);
        sb_append(&sb, line);
        free(q);
        snprintf(line, sizeof(line), "      size: %lld\n", g->identity.size);
        sb_append(&sb, line);

        if (g->rom_name && g->rom_name[0]) {
            q = quote(g->rom_name);
            snprintf(line, sizeof(line), "    rom_name: %s\n", q);
            sb_append(&sb, line);
            free(q);
        }

        sb_append(&sb, "    assets:\n");
        append_quoted_or_null(&sb, "      logo: ", g->logo);
        append_quoted_or_null(&sb, "      video: ", g->video);
        append_quoted_or_null(&sb, "      video_top: ", g->video_top);
        append_quoted_or_null(&sb, "      video_bottom: ", g->video_bottom);
    }

    return sb.data;
}

/* --------------------------------------- reading -------------------------------------------
 * Faithful, schema-specific port of yaml_io.py's _load_scoped(): fixed indentation levels
 * (0/2/4/6), tolerant of anything unexpected (skips rather than errors). Not a general parser --
 * see the "known gap" note in manifest.h for why that's fine here. */

static char *trim_dup(const char *start, const char *end) {
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
    size_t n = (size_t)(end - start);
    char *out = (char *)malloc(n + 1);
    memcpy(out, start, n);
    out[n] = '\0';
    return out;
}

/* Mirrors yaml_io.py's _split_kv(). */
static void split_kv(const char *line, char **key_out, char **val_out) {
    const char *colon_space = strstr(line, ": ");
    if (colon_space) {
        *key_out = trim_dup(line, colon_space);
        *val_out = trim_dup(colon_space + 2, line + strlen(line));
        return;
    }
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == ':') {
        *key_out = trim_dup(line, line + len - 1);
        *val_out = strdup("");
        return;
    }
    const char *colon = strchr(line, ':');
    if (colon) {
        *key_out = trim_dup(line, colon);
        *val_out = trim_dup(colon + 1, line + strlen(line));
        return;
    }
    *key_out = strdup(line);
    *val_out = strdup("");
}

/* Mirrors yaml_io.py's _parse_scalar(), restricted to the string/null cases this schema ever
 * actually emits (no bool/int scalars appear inside quoted-string fields here). Returns NULL
 * for "", "null" or "~"; an unescaped malloc'd copy for a `"quoted"` value; otherwise a raw
 * malloc'd copy of `v` as-is. */
static char *parse_scalar_str(const char *v) {
    if (v[0] == '\0' || strcmp(v, "null") == 0 || strcmp(v, "~") == 0) return NULL;
    size_t n = strlen(v);
    if (n >= 2 && v[0] == '"' && v[n - 1] == '"') {
        char *out = (char *)malloc(n); /* upper bound; unescaping only ever shrinks */
        char *w = out;
        for (size_t i = 1; i < n - 1; i++) {
            if (v[i] == '\\' && i + 1 < n - 1 && (v[i + 1] == '"' || v[i + 1] == '\\')) {
                *w++ = v[i + 1];
                i++;
            } else {
                *w++ = v[i];
            }
        }
        *w = '\0';
        return out;
    }
    return strdup(v);
}

void manifest_load(const char *text, Manifest *out) {
    manifest_init(out);

    enum { SUB_NONE, SUB_IDENTITY, SUB_ASSETS } sub = SUB_NONE;
    ManifestGame *cur = NULL;

    const char *p = text;
    while (*p) {
        const char *raw_start = p;
        while (*p && *p != '\n') p++;
        size_t raw_len = (size_t)(p - raw_start);
        if (*p == '\n') p++;
        if (raw_len > 0 && raw_start[raw_len - 1] == '\r') raw_len--;

        size_t indent = 0;
        while (indent < raw_len && raw_start[indent] == ' ') indent++;
        if (indent >= raw_len) continue;      /* blank line */
        if (raw_start[indent] == '#') continue; /* comment */

        char *line = trim_dup(raw_start + indent, raw_start + raw_len);

        if (indent == 0) {
            char *key, *val;
            split_kv(line, &key, &val);
            if (strcmp(key, "games") == 0) {
                /* no-op marker */
            } else if (strcmp(key, "version") == 0) {
                out->version = atoi(val);
            } else if (strcmp(key, "allow_name_match") == 0) {
                out->allow_name_match = strcmp(val, "true") == 0;
            }
            cur = NULL;
            sub = SUB_NONE;
            free(key);
            free(val);
        } else if (indent == 2 && strncmp(line, "- ", 2) == 0) {
            cur = manifest_add_game(out);
            sub = SUB_NONE;
            char *key, *val;
            split_kv(line + 2, &key, &val);
            if (strcmp(key, "game_id") == 0) cur->game_id = parse_scalar_str(val);
            free(key);
            free(val);
        } else if (indent == 4 && cur != NULL) {
            char *key, *val;
            split_kv(line, &key, &val);
            if (val[0] == '\0') {
                if (strcmp(key, "identity") == 0) sub = SUB_IDENTITY;
                else if (strcmp(key, "assets") == 0) sub = SUB_ASSETS;
                else sub = SUB_NONE;
            } else {
                if (strcmp(key, "rom_name") == 0) cur->rom_name = parse_scalar_str(val);
                sub = SUB_NONE;
            }
            free(key);
            free(val);
        } else if (indent == 6 && sub != SUB_NONE && cur != NULL) {
            char *key, *val;
            split_kv(line, &key, &val);
            if (sub == SUB_IDENTITY) {
                char *s = parse_scalar_str(val);
                if (strcmp(key, "sha1") == 0) snprintf(cur->identity.sha1, sizeof(cur->identity.sha1), "%s", s ? s : "");
                else if (strcmp(key, "md5") == 0) snprintf(cur->identity.md5, sizeof(cur->identity.md5), "%s", s ? s : "");
                else if (strcmp(key, "crc32") == 0) snprintf(cur->identity.crc32, sizeof(cur->identity.crc32), "%s", s ? s : "");
                else if (strcmp(key, "size") == 0) cur->identity.size = atoll(val);
                free(s);
            } else { /* SUB_ASSETS */
                char *s = parse_scalar_str(val);
                if (strcmp(key, "logo") == 0) cur->logo = s;
                else if (strcmp(key, "video") == 0) cur->video = s;
                else if (strcmp(key, "video_top") == 0) cur->video_top = s;
                else if (strcmp(key, "video_bottom") == 0) cur->video_bottom = s;
                else free(s);
            }
            free(key);
            free(val);
        }

        free(line);
    }
}

/* ------------------------------------- AssetsIndex API -------------------------------------- */

void assets_index_init(AssetsIndex *idx) {
    idx->entries = NULL;
    idx->count = 0;
    idx->capacity = 0;
}

void assets_index_free(AssetsIndex *idx) {
    if (!idx) return;
    for (size_t i = 0; i < idx->count; i++) {
        free(idx->entries[i].rom_name);
        free(idx->entries[i].game_id);
    }
    free(idx->entries);
    memset(idx, 0, sizeof(*idx));
}

void assets_index_set(AssetsIndex *idx, const char *rom_name, const char *game_id) {
    for (size_t i = 0; i < idx->count; i++) {
        if (strcmp(idx->entries[i].rom_name, rom_name) == 0) {
            free(idx->entries[i].game_id);
            idx->entries[i].game_id = strdup(game_id);
            return;
        }
    }
    if (idx->count >= idx->capacity) {
        size_t new_cap = idx->capacity ? idx->capacity * 2 : 32;
        idx->entries = (AssetsIndexEntry *)realloc(idx->entries, new_cap * sizeof(AssetsIndexEntry));
        idx->capacity = new_cap;
    }
    idx->entries[idx->count].rom_name = strdup(rom_name);
    idx->entries[idx->count].game_id = strdup(game_id);
    idx->count++;
}

static int cmp_entry_by_name(const void *a, const void *b) {
    const AssetsIndexEntry *ea = (const AssetsIndexEntry *)a;
    const AssetsIndexEntry *eb = (const AssetsIndexEntry *)b;
    return strcmp(ea->rom_name, eb->rom_name);
}

char *assets_index_dump(const AssetsIndex *idx) {
    StrBuf sb;
    sb_init(&sb);
    sb_append(&sb, "# GENERATED by the host: base name on the SD (no extension) -> game_id.\n");
    sb_append(&sb, "# The DS matches the focused ROM here and loads assets/<game_id>/.\n");
    sb_append(&sb, "version: 1\n");
    sb_append(&sb, "roms:\n");

    AssetsIndexEntry *sorted = NULL;
    if (idx->count) {
        sorted = (AssetsIndexEntry *)malloc(idx->count * sizeof(AssetsIndexEntry));
        memcpy(sorted, idx->entries, idx->count * sizeof(AssetsIndexEntry));
        qsort(sorted, idx->count, sizeof(AssetsIndexEntry), cmp_entry_by_name);
    }

    char line[4200];
    for (size_t i = 0; i < idx->count; i++) {
        char *qk = quote(sorted[i].rom_name);
        char *qv = quote(sorted[i].game_id);
        snprintf(line, sizeof(line), "  %s: %s\n", qk, qv);
        sb_append(&sb, line);
        free(qk);
        free(qv);
    }
    free(sorted);

    return sb.data;
}

/* Mirrors yaml_io.py's _parse_quoted_key_line() -- parses `"key with \" and \\ escapes": value`. */
static void parse_quoted_key_line(const char *line, char **key_out, char **val_out) {
    if (line[0] == '"') {
        size_t n = strlen(line);
        size_t end = 1;
        while (end < n) {
            if (line[end] == '\\') { end += 2; continue; }
            if (line[end] == '"') break;
            end++;
        }
        if (end > n) end = n;
        char *raw_key = trim_dup(line + 1, line + (end <= n ? end : n));
        /* unescape \" and \\ in the key */
        size_t kn = strlen(raw_key);
        char *key = (char *)malloc(kn + 1);
        char *w = key;
        for (size_t i = 0; i < kn; i++) {
            if (raw_key[i] == '\\' && i + 1 < kn && (raw_key[i + 1] == '"' || raw_key[i + 1] == '\\')) {
                *w++ = raw_key[i + 1];
                i++;
            } else {
                *w++ = raw_key[i];
            }
        }
        *w = '\0';
        free(raw_key);
        *key_out = key;

        const char *rest = (end < n) ? line + end + 1 : line + n;
        while (*rest == ' ') rest++;
        if (*rest == ':') {
            const char *v = rest + 1;
            while (*v == ' ') v++;
            *val_out = parse_scalar_str(v);
        } else {
            *val_out = NULL;
        }
        return;
    }
    char *key, *val;
    split_kv(line, &key, &val);
    *key_out = key;
    *val_out = parse_scalar_str(val);
    free(val);
}

void assets_index_load(const char *text, AssetsIndex *out) {
    assets_index_init(out);

    const char *p = text;
    while (*p) {
        const char *raw_start = p;
        while (*p && *p != '\n') p++;
        size_t raw_len = (size_t)(p - raw_start);
        if (*p == '\n') p++;
        if (raw_len > 0 && raw_start[raw_len - 1] == '\r') raw_len--;

        size_t indent = 0;
        while (indent < raw_len && raw_start[indent] == ' ') indent++;
        if (indent >= raw_len) continue;
        if (raw_start[indent] == '#') continue;

        char *line = trim_dup(raw_start + indent, raw_start + raw_len);

        if (indent == 2) {
            char *key, *val;
            parse_quoted_key_line(line, &key, &val);
            if (key && val) assets_index_set(out, key, val);
            free(key);
            free(val);
        }
        /* indent 0 lines ("version:", "roms:") carry nothing this reader needs to keep. */

        free(line);
    }
}

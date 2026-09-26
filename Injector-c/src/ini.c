#include "ini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void lines_ensure_capacity(IniLines *l, size_t need) {
    if (need <= l->capacity) return;
    size_t new_cap = l->capacity ? l->capacity * 2 : 16;
    while (new_cap < need) new_cap *= 2;
    l->lines = (char **)realloc(l->lines, new_cap * sizeof(char *));
    l->capacity = new_cap;
}

static void lines_append_owned(IniLines *l, char *owned_str) {
    lines_ensure_capacity(l, l->count + 1);
    l->lines[l->count++] = owned_str;
}

static void lines_insert_owned(IniLines *l, size_t at, char *owned_str) {
    lines_ensure_capacity(l, l->count + 1);
    memmove(&l->lines[at + 1], &l->lines[at], (l->count - at) * sizeof(char *));
    l->lines[at] = owned_str;
    l->count++;
}

static char *dup_range(const char *start, const char *end) {
    /* Trims leading/trailing ' ' and '\t' from [start, end), returns a malloc'd NUL-terminated
     * copy of what remains (possibly empty). */
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
    size_t n = (size_t)(end - start);
    char *out = (char *)malloc(n + 1);
    memcpy(out, start, n);
    out[n] = '\0';
    return out;
}

void ini_free(IniLines *lines) {
    if (!lines) return;
    for (size_t i = 0; i < lines->count; i++) free(lines->lines[i]);
    free(lines->lines);
    lines->lines = NULL;
    lines->count = 0;
    lines->capacity = 0;
}

int ini_read(const char *path, IniLines *out) {
    out->lines = NULL;
    out->count = 0;
    out->capacity = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        /* Missing file: not an error -- caller (ini_set_keys) creates the section fresh. */
        return 0;
    }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';

    const char *p = buf;
    const char *bufend = buf + got;

    /* Strip a UTF-8 BOM if present. */
    if (got >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF) {
        p += 3;
    }

    while (p < bufend) {
        const char *line_start = p;
        while (p < bufend && *p != '\n' && *p != '\r') p++;
        const char *line_end = p;
        lines_append_owned(out, dup_range(line_start, line_end));

        /* Consume the line ending: \r\n, \r, or \n all count as one break. */
        if (p < bufend && *p == '\r') p++;
        if (p < bufend && *p == '\n') p++;
    }

    free(buf);
    return 0;
}

static int line_is_comment(const char *line) {
    /* Lines are already trimmed by ini_read(), so the comment marker (if any) is line[0]. */
    return line[0] == ';' || line[0] == '/' || line[0] == '!';
}

/* Matches the Python original's `line.startswith("[") and line[1:line.find("]")] == section`:
 * the FIRST "]" on the line closes the header (trailing content past it, if any, is ignored),
 * and any line lacking a "]" entirely is never a match. */
static int line_is_section_header(const char *line, const char *section) {
    if (line[0] != '[') return 0;
    const char *close = strchr(line, ']');
    if (!close) return 0;
    size_t inner_len = (size_t)(close - line - 1);
    return strlen(section) == inner_len && memcmp(line + 1, section, inner_len) == 0;
}

/* Matches the Python original's `line.startswith("[")`, used to find "the next section" while
 * scanning -- deliberately looser than line_is_section_header() (no "]" required), since that's
 * exactly what upstream checks at this point. */
static int starts_with_bracket(const char *line) {
    return line[0] == '[';
}

/* If `line` is a "key = value" line (not a comment) whose trimmed key matches one of `kv`,
 * returns its index into `kv`; otherwise -1. `matched` marks entries already replaced once,
 * matching the Python original's "first match wins" behavior for duplicate keys. */
static int line_matches_pending_key(const char *line, const IniKV *kv, size_t count,
                                     const int *matched) {
    if (line_is_comment(line)) return -1;
    const char *eq = strchr(line, '=');
    if (!eq) return -1;
    char *key = dup_range(line, eq);
    int found = -1;
    for (size_t i = 0; i < count; i++) {
        if (matched[i]) continue;
        if (strcmp(key, kv[i].key) == 0) {
            found = (int)i;
            break;
        }
    }
    free(key);
    return found;
}

void ini_set_keys(IniLines *lines, const char *section, const IniKV *kv, size_t count) {
    if (count == 0) return;
    int *matched = (int *)calloc(count, sizeof(int));

    /* Locate the section header, if any. */
    size_t header_idx = (size_t)-1;
    for (size_t i = 0; i < lines->count; i++) {
        if (line_is_section_header(lines->lines[i], section)) {
            header_idx = i;
            break;
        }
    }

    if (header_idx != (size_t)-1) {
        /* Rewrite any existing "key = value" lines within [header+1, next section or EOF). */
        size_t section_end = lines->count;
        for (size_t i = header_idx + 1; i < lines->count; i++) {
            if (starts_with_bracket(lines->lines[i])) { section_end = i; break; }
        }
        for (size_t i = header_idx + 1; i < section_end; i++) {
            int idx = line_matches_pending_key(lines->lines[i], kv, count, matched);
            if (idx >= 0) {
                char buf[512];
                snprintf(buf, sizeof(buf), "%s = %s", kv[idx].key, kv[idx].value);
                free(lines->lines[i]);
                lines->lines[i] = strdup(buf);
                matched[idx] = 1;
            }
        }

        /* Any keys not found get inserted at the END of the section's existing content (right
         * before the next section header or EOF) -- matches set_ini_keys()'s own insert_at scan
         * in deploy.py exactly (NOT right after the header -- an earlier draft of this port got
         * that backwards). */
        size_t insert_at = section_end;
        for (size_t i = 0; i < count; i++) {
            if (matched[i]) continue;
            char buf[512];
            snprintf(buf, sizeof(buf), "%s = %s", kv[i].key, kv[i].value);
            lines_insert_owned(lines, insert_at, strdup(buf));
            insert_at++;
        }
    } else {
        /* Section doesn't exist at all: append "[section]" + every key at EOF. */
        char buf[512];
        snprintf(buf, sizeof(buf), "[%s]", section);
        lines_append_owned(lines, strdup(buf));
        for (size_t i = 0; i < count; i++) {
            snprintf(buf, sizeof(buf), "%s = %s", kv[i].key, kv[i].value);
            lines_append_owned(lines, strdup(buf));
        }
    }

    free(matched);
}

int ini_write(const char *path, const IniLines *lines) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    for (size_t i = 0; i < lines->count; i++) {
        if (fputs(lines->lines[i], f) < 0) { fclose(f); return -1; }
        if (fputs("\r\n", f) < 0) { fclose(f); return -1; }
    }
    if (fclose(f) != 0) return -1;
    return 0;
}

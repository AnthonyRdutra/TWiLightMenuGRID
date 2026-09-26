/* settings.ini reader/writer -- byte-compatible with TWiLightMenu's own ad-hoc C++ CIniFile
 * (universal/source/common/inifile.cpp), the same file the DS itself reads at boot. Ported from
 * Injector/deploy.py's _read_ini_lines/_write_ini_lines/set_ini_keys.
 *
 * One deliberate behavior change from the Python original: that version silently dropped every
 * comment line (';'/'/'/'!'  as first char) on every write, because it filtered them out at read
 * time and never wrote them back. This port preserves comment lines instead (see project plan --
 * flagged there as a latent bug worth fixing while this logic was already being touched). */
#ifndef INJECTOR_INI_H
#define INJECTOR_INI_H

#include <stddef.h>

typedef struct {
    char **lines;    /* malloc'd strings, no line-ending characters, blank/comment lines kept */
    size_t count;
    size_t capacity;
} IniLines;

/* Reads `path` into `out` (zero-initialized by this call). Normalizes CRLF/CR to logical line
 * breaks, strips a UTF-8 BOM if present, trims leading/trailing spaces and tabs from each line.
 * A missing file is not an error -- `out` is left empty, ready for ini_set_keys() to append a
 * brand new [section] into (matches the Python original's behavior of tolerating a fresh SD).
 * Returns 0 on success, -1 on a real I/O error (unreadable existing file). */
int ini_read(const char *path, IniLines *out);

/* Frees every line and the array itself; safe to call on a zero-initialized or already-freed
 * IniLines. */
void ini_free(IniLines *lines);

typedef struct {
    const char *key;
    const char *value;
} IniKV;

/* Sets `count` keys under [section] in `lines`, in place, matching TWiLightMenu's own CIniFile
 * semantics: case-sensitive `[section]` header match; existing `key = value` lines inside the
 * section are rewritten in place (first match only -- a duplicate key line further down is left
 * untouched, matching upstream); keys not already present are inserted as new lines at the END
 * of the section's existing content (right before the next section header or EOF); a wholly
 * missing section is appended (header + all keys) at EOF.
 * Lines outside the target section (including comments, now preserved -- see file header) are
 * never touched. */
void ini_set_keys(IniLines *lines, const char *section, const IniKV *kv, size_t count);

/* Writes `lines` back to `path`, one per line, always CRLF-terminated (matches what the DS-side
 * reader expects, regardless of host OS -- not a bug, kept identical to the Python original).
 * Returns 0 on success, -1 on I/O error. */
int ini_write(const char *path, const IniLines *lines);

#endif

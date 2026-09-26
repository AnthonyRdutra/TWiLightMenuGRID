#pragma once
#ifndef _JSONWALK_H_
#define _JSONWALK_H_

// Small jsmn token-walk helpers shared by ThemeConfig.cpp (theme.json's "theme"/"macro" objects,
// the JSON counterpart to theme.ini's [THEME]/[MACRO] sections) and ThemeLayout.cpp (theme.json's/
// layout.json's "grid"/"assets"/"sprites" objects) -- no DOM, same fixed-buffer/no-heap posture as
// the rest of this theme config code (see ThemeLayout.h's class comment for why). Header-only +
// `inline` so both .cpp files can include this without an ODR violation or a new Makefile entry.
//
// jsmn itself is pulled in declarations-only here (JSMN_HEADER) -- its one implementation TU is
// universal/source/common/jsmn.c, already linked into every frontend that uses it.
#define JSMN_HEADER
#include "common/jsmn.h"

#include <cstring>
#include <cstdlib>
#include <string>

// True if `tok` is the JSON string `s` (case-sensitive, exact length match).
inline bool jsoneq(const char *json, const jsmntok_t &tok, const char *s) {
	int len = tok.end - tok.start;
	return tok.type == JSMN_STRING && (int)strlen(s) == len && strncmp(json + tok.start, s, len) == 0;
}

// Number of tokens occupied by the value at `idx`, including its descendants -- needed to skip
// over a nested object/array we don't recognize instead of misreading its contents as siblings.
inline int jsmnTokenSpan(const jsmntok_t *tokens, int idx) {
	const jsmntok_t &t = tokens[idx];
	int span = 1;
	if (t.type == JSMN_OBJECT) {
		for (int i = 0; i < t.size; i++) {
			span += 1; // key (always a plain string, no children of its own)
			span += jsmnTokenSpan(tokens, idx + span);
		}
	} else if (t.type == JSMN_ARRAY) {
		for (int i = 0; i < t.size; i++)
			span += jsmnTokenSpan(tokens, idx + span);
	}
	return span;
}

// Decimal by default; hex if 0x/0X-prefixed -- mirrors CIniFile::GetInt's exact parsing (see
// universal/source/common/inifile.cpp) so a number means the same thing whether it came from
// theme.ini or theme.json. Accepts a bare JSON primitive (number/true/false -- layout.json's
// existing convention) or a quoted string (theme.json's convention for hex colors, since a bare
// 0x-prefixed literal isn't valid JSON number syntax) -- either works for any key.
inline int jsonInt(const char *json, const jsmntok_t &tok, int defVal) {
	if (tok.type != JSMN_PRIMITIVE && tok.type != JSMN_STRING)
		return defVal;
	int len = tok.end - tok.start;
	if (len <= 0 || len >= 16)
		return defVal;
	char tmp[16];
	memcpy(tmp, json + tok.start, len);
	tmp[len] = '\0';
	if (tok.type == JSMN_PRIMITIVE) {
		if (tmp[0] == 't') return 1;  // true
		if (tmp[0] == 'f') return 0;  // false
	}
	if (len > 2 && tmp[0] == '0' && (tmp[1] == 'x' || tmp[1] == 'X'))
		return (int)strtol(tmp, NULL, 16);
	return (int)strtol(tmp, NULL, 10);
}

inline bool jsonBool(const char *json, const jsmntok_t &tok, bool defVal) {
	if (tok.type != JSMN_PRIMITIVE || tok.end - tok.start <= 0)
		return defVal;
	return json[tok.start] == 't';
}

inline std::string jsonString(const char *json, const jsmntok_t &tok) {
	if (tok.type != JSMN_STRING)
		return std::string();
	return std::string(json + tok.start, tok.end - tok.start);
}

// Looks up `key` as a DIRECT child of the object at `objIdx` (skips over grandchildren/nested
// objects wholesale via jsmnTokenSpan). Returns the value token's index, or -1 if `objIdx` isn't
// an object or doesn't have that key. Pass 0 for `objIdx` to look up a top-level (root) key.
// O(children of objIdx) -- fine for a config file with, at most, a few dozen keys, parsed once
// at startup.
inline int findJsonKey(const char *json, const jsmntok_t *tokens, int n, int objIdx, const char *key) {
	if (objIdx < 0 || objIdx >= n || tokens[objIdx].type != JSMN_OBJECT)
		return -1;
	int i = objIdx + 1;
	for (int p = 0; p < tokens[objIdx].size && i < n; p++) {
		const jsmntok_t &k = tokens[i];
		int valIdx = i + 1;
		if (valIdx >= n) break;
		if (jsoneq(json, k, key)) return valIdx;
		i = valIdx + jsmnTokenSpan(tokens, valIdx);
	}
	return -1;
}

#endif //_JSONWALK_H_

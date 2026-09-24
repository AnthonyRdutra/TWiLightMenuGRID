// Single translation unit that compiles jsmn's implementation (vendored, unmodified upstream
// header at common/jsmn.h from https://github.com/zserge/jsmn, MIT licensed).
//
// jsmn.h's functions default to extern linkage, and by default (JSMN_HEADER undefined) including
// the header also emits their bodies — fine for exactly one TU, but every other file that needs
// jsmn's declarations must `#define JSMN_HEADER` before including it, or the implementation would
// be duplicated and fail to link. This file is that one TU; see ThemeLayout.cpp for the
// declarations-only usage.
#include "common/jsmn.h"

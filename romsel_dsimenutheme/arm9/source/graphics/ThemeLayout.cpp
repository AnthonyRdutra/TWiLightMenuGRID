#include "ThemeLayout.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "themefilenames.h"
#include "jsonwalk.h"

// Generous but bounded: layout.json/theme.json are small, hand-written config files, not user
// data. Anything bigger than this or with more tokens than fit is treated the same as "absent" --
// log and keep defaults, never crash. Both are function-static (not stack locals) since
// loadConfig() runs once at startup and there's no reason to spend ARM9 stack space on a one-shot
// buffer. theme.json is the bigger of the two (it also carries everything theme.ini used to), so
// both limits size this file, not layout.json alone.
static constexpr size_t LAYOUT_JSON_MAX_BYTES = 8192;
static constexpr int LAYOUT_JSON_MAX_TOKENS = 320;

const std::string ThemeLayout::EMPTY_STRING;

// Magic numbers here are the DSi grid's former hardcoded constants (graphics.cpp's old
// NROWS/colSpacing/rowCY/ACTIVE_SCALE/INACTIVE_SCALE/GRID_ZOOM_STEP locals, and fileBrowse.cpp's
// ROW3_COLS_LEFT/RIGHT/titleboxXspeed) -- a theme that ships no layout.json renders identically to
// before this class existed.
ThemeLayout::ThemeLayout()
	: _gridRows(3),
	  _gridColsLeft(4),
	  _gridColsRight(3),
	  _gridColSpacing(48),
	  _gridRowSpacing(52),
	  _gridRowOffsetY(36),
	  _gridActiveScale(4096),   // 1.0x -> box 64px / icon 32px, in the animation's 12-bit fixed point
	  _gridInactiveScale(2048), // 0.5x -> box 32px / icon 16px
	  _gridZoomStep(585),       // linear step (~7 frames 0->4096, no front-load)
	  _gridScrollSpeedX(3), // divisor, not px/frame -- see the getter doc in ThemeLayout.h
	  _gridScrollSpeedY(3),
	  _gridCursorEnabled(false) {
}

// jsoneq/jsmnTokenSpan/jsonInt/jsonBool/jsonString now live in jsonwalk.h, shared with
// ThemeConfig.cpp's theme.json reader.

// Same body regardless of which file it's reading -- theme.json's root object carries "grid"/
// "assets"/"sprites" alongside "theme"/"macro" (ThemeConfig.cpp's concern, ignored here the same
// way any OTHER unrecognized top-level key already is), so this needs no changes beyond the path
// it's given. Returns true if `path` existed and was at least parseable enough to read from
// (even if some/all of its keys were themselves malformed -- those just keep their defaults).
bool ThemeLayout::loadFromFile(const std::string &path) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false; // file doesn't exist -- caller tries the next fallback, if any

	fseek(f, 0, SEEK_END);
	long fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fileSize <= 0 || (size_t)fileSize > LAYOUT_JSON_MAX_BYTES) {
		fclose(f); // empty, unreadable, or bigger than our fixed buffer -- treat as absent
		return false;
	}

	static char buf[LAYOUT_JSON_MAX_BYTES + 1];
	size_t readCount = fread(buf, 1, (size_t)fileSize, f);
	fclose(f);
	if (readCount != (size_t)fileSize)
		return false;
	buf[readCount] = '\0';

	static jsmntok_t tokens[LAYOUT_JSON_MAX_TOKENS];
	jsmn_parser parser;
	jsmn_init(&parser);
	int n = jsmn_parse(&parser, buf, readCount, tokens, LAYOUT_JSON_MAX_TOKENS);
	if (n < 1 || tokens[0].type != JSMN_OBJECT)
		return false; // malformed / truncated / not an object at the root -- keep defaults

	int i = 1;
	for (int obj = 0; obj < tokens[0].size && i < n; obj++) {
		const jsmntok_t &key = tokens[i];
		int valIdx = i + 1;
		if (valIdx >= n) break;
		const jsmntok_t &val = tokens[valIdx];

		if (jsoneq(buf, key, "grid") && val.type == JSMN_OBJECT) {
			int gi = valIdx + 1;
			for (int p = 0; p < val.size; p++) {
				const jsmntok_t &gkey = tokens[gi];
				int gvalIdx = gi + 1;
				const jsmntok_t &gval = tokens[gvalIdx];
				if (jsoneq(buf, gkey, "rows")) _gridRows = jsonInt(buf, gval, _gridRows);
				else if (jsoneq(buf, gkey, "colsLeft")) _gridColsLeft = jsonInt(buf, gval, _gridColsLeft);
				else if (jsoneq(buf, gkey, "colsRight")) _gridColsRight = jsonInt(buf, gval, _gridColsRight);
				else if (jsoneq(buf, gkey, "colSpacing")) _gridColSpacing = jsonInt(buf, gval, _gridColSpacing);
				else if (jsoneq(buf, gkey, "rowSpacing")) _gridRowSpacing = jsonInt(buf, gval, _gridRowSpacing);
				else if (jsoneq(buf, gkey, "rowOffsetY")) _gridRowOffsetY = jsonInt(buf, gval, _gridRowOffsetY);
				else if (jsoneq(buf, gkey, "activeScale")) _gridActiveScale = jsonInt(buf, gval, _gridActiveScale);
				else if (jsoneq(buf, gkey, "inactiveScale")) _gridInactiveScale = jsonInt(buf, gval, _gridInactiveScale);
				else if (jsoneq(buf, gkey, "zoomStep")) _gridZoomStep = jsonInt(buf, gval, _gridZoomStep);
				else if (jsoneq(buf, gkey, "scrollSpeedX")) _gridScrollSpeedX = jsonInt(buf, gval, _gridScrollSpeedX);
				else if (jsoneq(buf, gkey, "scrollSpeedY")) _gridScrollSpeedY = jsonInt(buf, gval, _gridScrollSpeedY);
				else if (jsoneq(buf, gkey, "cursor") && gval.type == JSMN_OBJECT) {
					int ci = gvalIdx + 1;
					for (int cp = 0; cp < gval.size; cp++) {
						const jsmntok_t &ckey = tokens[ci];
						int cvalIdx = ci + 1;
						if (jsoneq(buf, ckey, "enabled"))
							_gridCursorEnabled = jsonBool(buf, tokens[cvalIdx], _gridCursorEnabled);
						else if (jsoneq(buf, ckey, "asset") && _assetOverrideCount < MAX_ASSET_OVERRIDES) {
							_assetOverrides[_assetOverrideCount].key = "gridCursor";
							_assetOverrides[_assetOverrideCount].path = jsonString(buf, tokens[cvalIdx]);
							_assetOverrideCount++;
						}
						ci = cvalIdx + jsmnTokenSpan(tokens, cvalIdx);
					}
				}
				gi = gvalIdx + jsmnTokenSpan(tokens, gvalIdx);
			}
		} else if (jsoneq(buf, key, "assets") && val.type == JSMN_OBJECT) {
			int ai = valIdx + 1;
			for (int p = 0; p < val.size && _assetOverrideCount < MAX_ASSET_OVERRIDES; p++) {
				const jsmntok_t &akey = tokens[ai];
				int avalIdx = ai + 1;
				_assetOverrides[_assetOverrideCount].key = jsonString(buf, akey);
				_assetOverrides[_assetOverrideCount].path = jsonString(buf, tokens[avalIdx]);
				_assetOverrideCount++;
				ai = avalIdx + jsmnTokenSpan(tokens, avalIdx);
			}
		} else if (jsoneq(buf, key, "sprites") && val.type == JSMN_OBJECT) {
			int si = valIdx + 1;
			for (int p = 0; p < val.size && _spriteTableCount < MAX_SPRITE_TABLES; p++) {
				const jsmntok_t &skey = tokens[si];
				int svalIdx = si + 1;
				const jsmntok_t &sval = tokens[svalIdx];
				if (sval.type == JSMN_OBJECT) {
					SpriteTable table{0, 0};
					int ti = svalIdx + 1;
					for (int tp = 0; tp < sval.size; tp++) {
						const jsmntok_t &tkey = tokens[ti];
						int tvalIdx = ti + 1;
						if (jsoneq(buf, tkey, "frameCount")) table.frameCount = jsonInt(buf, tokens[tvalIdx], 0);
						else if (jsoneq(buf, tkey, "frameDelayVBlanks")) table.frameDelayVBlanks = jsonInt(buf, tokens[tvalIdx], 0);
						ti = tvalIdx + jsmnTokenSpan(tokens, tvalIdx);
					}
					_spriteTables[_spriteTableCount].name = jsonString(buf, skey);
					_spriteTables[_spriteTableCount].table = table;
					_spriteTableCount++;
				}
				si = svalIdx + jsmnTokenSpan(tokens, svalIdx);
			}
		}

		i = valIdx + jsmnTokenSpan(tokens, valIdx);
	}
	return true;
}

void ThemeLayout::loadConfig() {
	// theme.json (if this theme ships one) replaces BOTH theme.ini and layout.json outright --
	// see TFN_THEME_JSON's comment in themefilenames.h. Falls back to the legacy layout.json path
	// unchanged for every theme that doesn't have one.
	if (loadFromFile(TFN_THEME_JSON))
		return;
	loadFromFile(TFN_THEME_LAYOUT);
}

const std::string &ThemeLayout::assetPath(const std::string &key) const {
	for (int i = 0; i < _assetOverrideCount; i++)
		if (_assetOverrides[i].key == key)
			return _assetOverrides[i].path;
	return EMPTY_STRING;
}

const ThemeLayout::SpriteTable *ThemeLayout::spriteTable(const std::string &name) const {
	for (int i = 0; i < _spriteTableCount; i++)
		if (_spriteTables[i].name == name)
			return &_spriteTables[i].table;
	return nullptr;
}

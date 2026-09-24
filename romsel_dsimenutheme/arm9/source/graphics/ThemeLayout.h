#pragma once
#ifndef _THEMELAYOUT_H_
#define _THEMELAYOUT_H_

#include <string>
#include "common/singleton.h"

// Per-theme grid layout, driven by an optional <ui-dir>/layout.json (see TFN_THEME_LAYOUT), the
// JSON counterpart to theme.ini/ThemeConfig — but for the DSi theme's icon grid, which theme.ini
// has never covered (grid geometry has always been hardcoded C++ constants, unreachable by theme
// authors). Modeled directly on ThemeConfig: hardcoded ctor defaults reproduce today's behavior
// exactly, loadConfig() overrides them from JSON if the file exists and parses, and any missing
// key/file/parse failure just leaves the corresponding default in place (same fail-open posture
// as ThemeConfig's CIniFile lookups) — so a theme that ships no layout.json is unaffected.
//
// Storage is fixed-capacity, no heap allocation: this runs on DSi-class hardware with only a few
// MB of RAM, and every other runtime config reader in this codebase (CIniFile, ymlLookup) avoids
// building a full in-memory document tree. jsmn itself only tokenizes (spans into the raw file
// buffer); ThemeLayout::loadConfig() walks those tokens directly rather than building a DOM.
class ThemeLayout {
public:
	static constexpr int MAX_ASSET_OVERRIDES = 32;
	static constexpr int MAX_SPRITE_TABLES = 16;
	static constexpr int MAX_KEY_LEN = 24;
	static constexpr int MAX_PATH_LEN = 96;

	struct SpriteTable {
		int frameCount;
		int frameDelayVBlanks;
	};

	ThemeLayout();

	// Parses <ui-dir>/layout.json (TFN_THEME_LAYOUT) with jsmn, if present. On any error (file
	// missing, too large, malformed JSON) every field is left at its constructor default.
	void loadConfig();

	// ---- Grid geometry (defaults reproduce the DSi grid's former hardcoded constants) ----
	int gridRows() const { return _gridRows; }
	int gridColsLeft() const { return _gridColsLeft; }
	int gridColsRight() const { return _gridColsRight; }
	int gridColSpacing() const { return _gridColSpacing; }
	int gridRowSpacing() const { return _gridRowSpacing; }
	int gridRowOffsetY() const { return _gridRowOffsetY; }
	int gridActiveScale() const { return _gridActiveScale; }
	int gridInactiveScale() const { return _gridInactiveScale; }
	int gridZoomStep() const { return _gridZoomStep; }
	// Divisor for the horizontal scroll-chase's proportional step (see GridView::update()) --
	// higher is SLOWER (each frame closes 1/N of the remaining distance, floored at 1px), same
	// convention as graphics.cpp's titleboxXspeed. Not a fixed pixels-per-frame speed: that would
	// let a fast-moving destination (e.g. held-key repeat) outrun the chase indefinitely.
	int gridScrollSpeedX() const { return _gridScrollSpeedX; }
	int gridScrollSpeedY() const { return _gridScrollSpeedY; }
	bool gridCursorEnabled() const { return _gridCursorEnabled; }

	// Named asset path override (e.g. "box", "folder", "gridCursor"). Returns an empty string if
	// the theme's layout.json doesn't override this key — callers should fall back to the
	// existing TFN_* hardcoded path in that case.
	const std::string &assetPath(const std::string &key) const;

	// Named multi-frame animation timing, or nullptr if the theme doesn't define one by this name.
	const SpriteTable *spriteTable(const std::string &name) const;

private:
	int _gridRows;
	int _gridColsLeft;
	int _gridColsRight;
	int _gridColSpacing;
	int _gridRowSpacing;
	int _gridRowOffsetY;
	int _gridActiveScale;
	int _gridInactiveScale;
	int _gridZoomStep;
	int _gridScrollSpeedX;
	int _gridScrollSpeedY;
	bool _gridCursorEnabled;

	struct AssetOverride {
		std::string key;
		std::string path;
	};
	AssetOverride _assetOverrides[MAX_ASSET_OVERRIDES];
	int _assetOverrideCount = 0;

	struct NamedSpriteTable {
		std::string name;
		SpriteTable table;
	};
	NamedSpriteTable _spriteTables[MAX_SPRITE_TABLES];
	int _spriteTableCount = 0;

	static const std::string EMPTY_STRING;
};

typedef singleton<ThemeLayout> themeLayout_s;
inline ThemeLayout &tl() { return themeLayout_s::instance(); }

#endif //_THEMELAYOUT_H_

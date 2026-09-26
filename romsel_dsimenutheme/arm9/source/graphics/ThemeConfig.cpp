#include "ThemeConfig.h"
#include "ThemeTextures.h"
#include "themefilenames.h"
#include "common/twlmenusettings.h"
#include "common/singleton.h"
#include "jsonwalk.h"

#include <nds.h>
#include <string>
#include <cstdio>

// Generous but bounded, same reasoning as ThemeLayout.cpp's LAYOUT_JSON_MAX_BYTES/TOKENS (which
// this mirrors): theme.json is a small, hand-written config file, not user data. Sized for ALL of
// theme.ini's ~90 keys plus a macro override section, comfortably -- anything bigger or with more
// tokens than fit is treated the same as "absent" (fall back to theme.ini), never crash.
static constexpr size_t THEME_JSON_MAX_BYTES = 8192;
static constexpr int THEME_JSON_MAX_TOKENS = 320;

// Magic numbers derived from default dark theme
ThemeConfig::ThemeConfig()
	: _startBorderRenderY(81), _startBorderSpriteW(32), _startBorderSpriteH(80), _startTextRenderY(143),
	_titleboxRenderY(85), _titleboxMaxLines(4), _titleboxTextY(30), _titleboxTextW(240), _titleboxTextLarge(true),
	_bubbleTipRenderY(80), _bubbleTipRenderX(122), _bubbleTipSpriteH(8), _bubbleTipSpriteW(11), _rotatingCubesRenderY(78),
	_shoulderLRenderY(172), _shoulderLRenderX(0), _shoulderLTextY(257), _shoulderLTextX(18), _shoulderLTextAlign(1),
	_shoulderRRenderY(172), _shoulderRRenderX(178), _shoulderRTextY(257), _shoulderRTextX(238), _shoulderRTextAlign(-1),
	_volumeRenderY(4), _volumeRenderX(16), _batteryRenderY(5), _batteryRenderX(235),
	_statusBarContentOffsetX(0), _statusBarContentOffsetY(0),
	_logoZoomPercent(100), _logoOffsetX(0), _logoOffsetY(0),
	_usernameRenderY(3), _usernameRenderX(28),
	_usernameRenderXDS(4), _usernameEdgeAlpha(true), _dateRenderY(5), _dateRenderX(162), _timeRenderY(5), _timeRenderX(200),
	// _photoRenderY(24), _photoRenderX(179),
	_bipsUserPalette(false), _boxUserPalette(false), _boxEmptyUserPalette(false), _boxFullUserPalette(false),
	_braceUserPalette(false), _bubbleUserPalette(false), _buttonArrowUserPalette(true), _cornerButtonUserPalette(false),
	_cursorUserPalette(false), _dialogBoxUserPalette(true), _folderUserPalette(false), _launchDotsUserPalette(true),
	_movingArrowUserPalette(true), _progressUserPalette(true), _scrollWindowUserPalette(false), _smallCartUserPalette(false),
	_startBorderUserPalette(true), _startTextUserPalette(true), _wirelessIconsUserPalette(false),
	_iconA26UserPalette(false), _iconCPCUserPalette(false), _iconCOLUserPalette(false), _iconGBUserPalette(false),
	_iconGBAUserPalette(false), _iconGBAModeUserPalette(false), _iconGGUserPalette(false),
	_iconHBUserPalette(false), _iconIMGUserPalette(false),
	_iconINTUserPalette(false), _iconM5UserPalette(false), _iconManualUserPalette(false), _iconMDUserPalette(false),
	_iconMINIUserPalette(false), _iconMSXUserPalette(false), _iconNESUserPalette(false), _iconNGPUserPalette(false),
	_iconPCEUserPalette(false), _iconPLGUserPalette(false), _iconSettingsUserPalette(false), _iconSGUserPalette(false),
	_iconSMSUserPalette(false), _iconSNESUserPalette(false), _iconUnknownUserPalette(false), _iconVIDUserPalette(false),
	_iconWSUserPalette(false), _usernameUserPalette(true), _progressBarUserPalette(true),
	_purpleBatteryAvailable(false), _renderPhoto(true), _darkLoading(false), _useAlphaBlend(true),
	_playStopSound(true),
	_playStartupJingle(false), _startupJingleDelayAdjust(0), _progressBarColor(0x7C00),
	_fontPalette1(0x0000), _fontPalette2(0xDEF7), _fontPalette3(0xC631), _fontPalette4(0xA108),
	_fontPaletteDisabled1(0x0000), _fontPaletteDisabled2(0xDEF7), _fontPaletteDisabled3(0xC631), _fontPaletteDisabled4(0xA108),
	_fontPaletteTitlebox1(0x0000), _fontPaletteTitlebox2(0xDEF7), _fontPaletteTitlebox3(0xC631), _fontPaletteTitlebox4(0xA108),
	_fontPaletteDialog1(0x0000), _fontPaletteDialog2(0xDEF7), _fontPaletteDialog3(0xC631), _fontPaletteDialog4(0xA108),
	_fontPaletteOverlay1(0x0000), _fontPaletteOverlay2(0xDEF7), _fontPaletteOverlay3(0xC631), _fontPaletteOverlay4(0xA108),
	_fontPaletteUsername1(0x0000), _fontPaletteUsername2(0xDEF7), _fontPaletteUsername3(0xC631), _fontPaletteUsername4(0xA108),
	_fontPaletteDateTime1(0x0000), _fontPaletteDateTime2(0xDEF7), _fontPaletteDateTime3(0xC631), _fontPaletteDateTime4(0xA108)
{
	if (ms().theme != TWLSettings::EThemeDSi) {
		_playStopSound = false;
	}

	if (ms().theme == TWLSettings::ETheme3DS) {
		_startBorderUserPalette = false;
		_dialogBoxUserPalette = false;
	}

	if (ms().theme == TWLSettings::EThemeSaturn || ms().theme == TWLSettings::EThemeHBL) {
		_renderPhoto = false;
		_darkLoading = true;
	}
}

int ThemeConfig::getInt(CIniFile &ini, const std::string &item, int defaultVal) {
	if (ms().macroMode)
		return ini.GetInt("MACRO", item, ini.GetInt("THEME", item, defaultVal));

	return ini.GetInt("THEME", item, defaultVal);
}

int ThemeConfig::getJsonInt(const char *json, const jsmntok_t *tokens, int n, int themeIdx,
                             int macroIdx, const char *item, int defaultVal) {
	int themeVal = defaultVal;
	int idx = findJsonKey(json, tokens, n, themeIdx, item);
	if (idx >= 0) themeVal = jsonInt(json, tokens[idx], defaultVal);

	if (ms().macroMode && macroIdx >= 0) {
		int midx = findJsonKey(json, tokens, n, macroIdx, item);
		if (midx >= 0) return jsonInt(json, tokens[midx], themeVal);
	}
	return themeVal;
}

// theme.json counterpart to loadConfig()'s CIniFile-based reading below -- same ~90 keys, same
// per-key defaults, same macro-mode override semantics (getJsonInt() mirrors getInt() exactly),
// just camelCase keys under theme.json's "theme"/"macro" objects instead of PascalCase keys under
// theme.ini's [THEME]/[MACRO] sections (matching the camelCase convention layout.json's own
// "grid"/"assets"/"sprites" objects already used). Kept as one big flat function on purpose,
// mirroring loadConfig()'s own shape below line for line, rather than introducing a key/pointer
// table -- easier to eyeball against loadConfig() for "did I translate every key" than a lookup
// table would be, and this only ever runs once at startup.
bool ThemeConfig::loadFromJson() {
	std::string path = TFN_THEME_JSON;

	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false; // no theme.json shipped by this theme -- caller falls back to theme.ini

	fseek(f, 0, SEEK_END);
	long fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fileSize <= 0 || (size_t)fileSize > THEME_JSON_MAX_BYTES) {
		fclose(f); // empty, unreadable, or bigger than our fixed buffer -- treat as absent
		return false;
	}

	static char buf[THEME_JSON_MAX_BYTES + 1];
	size_t readCount = fread(buf, 1, (size_t)fileSize, f);
	fclose(f);
	if (readCount != (size_t)fileSize)
		return false;
	buf[readCount] = '\0';

	static jsmntok_t tokens[THEME_JSON_MAX_TOKENS];
	jsmn_parser parser;
	jsmn_init(&parser);
	int n = jsmn_parse(&parser, buf, readCount, tokens, THEME_JSON_MAX_TOKENS);
	if (n < 1 || tokens[0].type != JSMN_OBJECT)
		return false; // malformed / truncated / not an object at the root

	int themeIdx = findJsonKey(buf, tokens, n, 0, "theme");
	int macroIdx = findJsonKey(buf, tokens, n, 0, "macro"); // may be -1: optional, same as [MACRO]
	if (themeIdx < 0)
		return false; // no "theme" object at all -- nothing here to read, same as no file

	int macroY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "macroTitleboxTextY", -1);
	int macroW = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "macroTitleboxTextW", -1);

	_startBorderRenderY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "startBorderRenderY", _startBorderRenderY);
	_startBorderSpriteW = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "startBorderSpriteW", _startBorderSpriteW);
	_startBorderSpriteH = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "startBorderSpriteH", _startBorderSpriteH);
	_startTextRenderY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "startTextRenderY", _startTextRenderY);

	_bubbleTipRenderY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "bubbleTipRenderY", _bubbleTipRenderY);
	_bubbleTipRenderX = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "bubbleTipRenderX", _bubbleTipRenderX);
	_bubbleTipSpriteW = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "bubbleTipSpriteW", _bubbleTipSpriteW);
	_bubbleTipSpriteH = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "bubbleTipSpriteH", _bubbleTipSpriteH);

	_titleboxRenderY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "titleboxRenderY", _titleboxRenderY);
	_titleboxMaxLines = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "titleboxMaxLines", _titleboxMaxLines);
	_titleboxTextY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "titleboxTextY", _titleboxTextY);
	_titleboxTextW = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "titleboxTextW", _titleboxTextW);
	if (ms().macroMode) {
		if (macroY != -1) _titleboxTextY = macroY;
		if (macroW != -1) _titleboxTextW = macroW;
	}
	_titleboxTextLarge = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "titleboxTextLarge", _titleboxTextLarge);

	_volumeRenderX = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "volumeRenderX", _volumeRenderX);
	_volumeRenderY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "volumeRenderY", _volumeRenderY);
	_shoulderLRenderY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "shoulderLRenderY", _shoulderLRenderY);
	_shoulderLRenderX = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "shoulderLRenderX", _shoulderLRenderX);
	_shoulderLTextY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "shoulderLTextY", _shoulderLTextY);
	_shoulderLTextX = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "shoulderLTextX", _shoulderLTextX);
	_shoulderLTextAlign = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "shoulderLTextAlign", _shoulderLTextAlign);
	_shoulderRRenderY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "shoulderRRenderY", _shoulderRRenderY);
	_shoulderRRenderX = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "shoulderRRenderX", _shoulderRRenderX);
	_shoulderRTextY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "shoulderRTextY", _shoulderRTextY);
	_shoulderRTextX = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "shoulderRTextX", _shoulderRTextX);
	_shoulderRTextAlign = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "shoulderRTextAlign", _shoulderRTextAlign);
	_batteryRenderY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "batteryRenderY", _batteryRenderY);
	_batteryRenderX = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "batteryRenderX", _batteryRenderX);
	_statusBarContentOffsetX = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "statusBarContentOffsetX", _statusBarContentOffsetX);
	_statusBarContentOffsetY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "statusBarContentOffsetY", _statusBarContentOffsetY);
	_logoZoomPercent = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "logoZoomPercent", _logoZoomPercent);
	_logoOffsetX = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "logoOffsetX", _logoOffsetX);
	_logoOffsetY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "logoOffsetY", _logoOffsetY);
	_usernameRenderY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "usernameRenderY", _usernameRenderY);
	_usernameRenderX = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "usernameRenderX", _usernameRenderX);
	_usernameRenderXDS = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "usernameRenderXDS", _usernameRenderXDS);
	_dateRenderY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "dateRenderY", _dateRenderY);
	_dateRenderX = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "dateRenderX", _dateRenderX);
	_timeRenderY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "timeRenderY", _timeRenderY);
	_timeRenderX = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "timeRenderX", _timeRenderX);

	_bipsUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "bipsUserPalette", _bipsUserPalette);
	_boxUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "boxUserPalette", _boxUserPalette);
	_boxEmptyUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "boxEmptyUserPalette", _boxEmptyUserPalette);
	_boxFullUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "boxFullUserPalette", _boxFullUserPalette);
	_braceUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "braceUserPalette", _braceUserPalette);
	_bubbleUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "bubbleUserPalette", _bubbleUserPalette);
	_buttonArrowUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "buttonArrowUserPalette", _buttonArrowUserPalette);
	_cornerButtonUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "cornerButtonUserPalette", _cornerButtonUserPalette);
	_cursorUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "cursorUserPalette", _cursorUserPalette);
	_dialogBoxUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "dialogBoxUserPalette", _dialogBoxUserPalette);
	_folderUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "folderUserPalette", _folderUserPalette);
	_launchDotsUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "launchDotsUserPalette", _launchDotsUserPalette);
	_movingArrowUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "movingArrowUserPalette", _movingArrowUserPalette);
	_progressUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "progressUserPalette", _progressUserPalette);
	_scrollWindowUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "scrollWindowUserPalette", _scrollWindowUserPalette);
	_smallCartUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "smallCartUserPalette", _smallCartUserPalette);
	_startBorderUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "startBorderUserPalette", _startBorderUserPalette);
	_startTextUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "startTextUserPalette", _startTextUserPalette);
	_wirelessIconsUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "wirelessIconsUserPalette", _wirelessIconsUserPalette);

	_iconA26UserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconA26UserPalette", _iconA26UserPalette);
	_iconCPCUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconCPCUserPalette", _iconCPCUserPalette);
	_iconCOLUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconCOLUserPalette", _iconCOLUserPalette);
	_iconGBUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconGBUserPalette", _iconGBUserPalette);
	_iconGBAUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconGBAUserPalette", _iconGBAUserPalette);
	_iconGBAModeUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconGBAModeUserPalette", _iconGBAModeUserPalette);
	_iconGGUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconGGUserPalette", _iconGGUserPalette);
	_iconHBUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconHBUserPalette", _iconHBUserPalette);
	_iconIMGUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconIMGUserPalette", _iconIMGUserPalette);
	_iconINTUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconINTUserPalette", _iconINTUserPalette);
	_iconM5UserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconM5UserPalette", _iconM5UserPalette);
	_iconManualUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconManualUserPalette", _iconManualUserPalette);
	_iconMDUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconMDUserPalette", _iconMDUserPalette);
	_iconMINIUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconMINIUserPalette", _iconMINIUserPalette);
	_iconMSXUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconMSXUserPalette", _iconMSXUserPalette);
	_iconNESUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconNESUserPalette", _iconNESUserPalette);
	_iconNGPUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconNGPUserPalette", _iconNGPUserPalette);
	_iconPCEUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconPCEUserPalette", _iconPCEUserPalette);
	_iconPLGUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconPLGUserPalette", _iconPLGUserPalette);
	_iconSettingsUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconSettingsUserPalette", _iconSettingsUserPalette);
	_iconSGUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconSGUserPalette", _iconSGUserPalette);
	_iconSMSUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconSMSUserPalette", _iconSMSUserPalette);
	_iconSNESUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconSNESUserPalette", _iconSNESUserPalette);
	_iconUnknownUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconUnknownUserPalette", _iconUnknownUserPalette);
	_iconVIDUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconVIDUserPalette", _iconVIDUserPalette);
	_iconWSUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "iconWSUserPalette", _iconWSUserPalette);

	_usernameUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "usernameUserPalette", _usernameUserPalette);
	_usernameEdgeAlpha = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "usernameEdgeAlpha", _usernameEdgeAlpha);
	_progressBarUserPalette = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "progressBarUserPalette", _progressBarUserPalette);

	_purpleBatteryAvailable = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "purpleBatteryAvailable", _purpleBatteryAvailable);
	_rotatingCubesRenderY = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "rotatingCubesRenderY", _rotatingCubesRenderY);
	_renderPhoto = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "renderPhoto", _renderPhoto);
	_darkLoading = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "darkLoading", _darkLoading);
	_useAlphaBlend = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "useAlphaBlend", _useAlphaBlend);

	_playStopSound = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "playStopSound", _playStopSound);
	_playStartupJingle = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "playStartupJingle", _playStartupJingle);
	_startupJingleDelayAdjust = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "startupJingleDelayAdjust", _startupJingleDelayAdjust);
	_progressBarColor = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "progressBarColor", _progressBarColor);

	_fontPalette1 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPalette1", _fontPalette1);
	_fontPalette2 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPalette2", _fontPalette2);
	_fontPalette3 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPalette3", _fontPalette3);
	_fontPalette4 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPalette4", _fontPalette4);
	_fontPaletteDisabled1 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteDisabled1", _fontPalette1);
	_fontPaletteDisabled2 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteDisabled2", _fontPalette2);
	_fontPaletteDisabled3 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteDisabled3", _fontPalette3);
	_fontPaletteDisabled4 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteDisabled4", _fontPalette4);
	_fontPaletteTitlebox1 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteTitlebox1", _fontPalette1);
	_fontPaletteTitlebox2 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteTitlebox2", _fontPalette2);
	_fontPaletteTitlebox3 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteTitlebox3", _fontPalette3);
	_fontPaletteTitlebox4 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteTitlebox4", _fontPalette4);
	_fontPaletteDialog1 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteDialog1", _fontPalette1);
	_fontPaletteDialog2 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteDialog2", _fontPalette2);
	_fontPaletteDialog3 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteDialog3", _fontPalette3);
	_fontPaletteDialog4 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteDialog4", _fontPalette4);
	_fontPaletteOverlay1 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteOverlay1", _fontPalette1);
	_fontPaletteOverlay2 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteOverlay2", _fontPalette2);
	_fontPaletteOverlay3 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteOverlay3", _fontPalette3);
	_fontPaletteOverlay4 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteOverlay4", _fontPalette4);
	_fontPaletteUsername1 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteUsername1", _fontPalette1);
	_fontPaletteUsername2 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteUsername2", _fontPalette2);
	_fontPaletteUsername3 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteUsername3", _fontPalette3);
	_fontPaletteUsername4 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteUsername4", _fontPalette4);
	_fontPaletteDateTime1 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteDateTime1", _fontPalette1);
	_fontPaletteDateTime2 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteDateTime2", _fontPalette2);
	_fontPaletteDateTime3 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteDateTime3", _fontPalette3);
	_fontPaletteDateTime4 = getJsonInt(buf, tokens, n, themeIdx, macroIdx, "fontPaletteDateTime4", _fontPalette4);

	return true;
}

void ThemeConfig::loadConfig() {
	//iprintf("tc().loadConfig()\n");
	if (loadFromJson()) return; // merged theme.json, if this theme ships one -- see loadFromJson()

	int macroY = 0;
	int macroW = 0;

	CIniFile themeConfig(TFN_THEME_SETTINGS);
	_startBorderRenderY = getInt(themeConfig, "StartBorderRenderY", _startBorderRenderY);
	_startBorderSpriteW = getInt(themeConfig, "StartBorderSpriteW", _startBorderSpriteW);
	_startBorderSpriteH = getInt(themeConfig, "StartBorderSpriteH", _startBorderSpriteH);
	_startTextRenderY = getInt(themeConfig, "StartTextRenderY", _startTextRenderY);

	_bubbleTipRenderY = getInt(themeConfig, "BubbleTipRenderY", _bubbleTipRenderY);
	_bubbleTipRenderX = getInt(themeConfig, "BubbleTipRenderX", _bubbleTipRenderX);
	_bubbleTipSpriteW = getInt(themeConfig, "BubbleTipSpriteW", _bubbleTipSpriteW);
	_bubbleTipSpriteH = getInt(themeConfig, "BubbleTipSpriteH", _bubbleTipSpriteH);

	_titleboxRenderY = getInt(themeConfig, "TitleboxRenderY", _titleboxRenderY);
	_titleboxMaxLines = getInt(themeConfig, "TitleboxMaxLines", _titleboxMaxLines);
	macroY = getInt(themeConfig, "MacroTitleboxTextY", -1);
	macroW = getInt(themeConfig, "MacroTitleboxTextW", -1);
	_titleboxTextY = getInt(themeConfig, "TitleboxTextY", _titleboxTextY);
	_titleboxTextW = getInt(themeConfig, "TitleboxTextW", _titleboxTextW);
	if (ms().macroMode) {
		if (macroY != -1) _titleboxTextY = macroY;
		if (macroW != -1) _titleboxTextW = macroW;
	}
	_titleboxTextLarge = getInt(themeConfig, "TitleboxTextLarge", _titleboxTextLarge);

	_volumeRenderX = getInt(themeConfig, "VolumeRenderX", _volumeRenderX);
	_volumeRenderY = getInt(themeConfig, "VolumeRenderY", _volumeRenderY);
	// _photoRenderX = getInt(themeConfig, "PhotoRenderX", _photoRenderX);
	// _photoRenderY = getInt(themeConfig, "PhotoRenderY", _photoRenderY);
	_shoulderLRenderY = getInt(themeConfig, "ShoulderLRenderY", _shoulderLRenderY);
	_shoulderLRenderX = getInt(themeConfig, "ShoulderLRenderX", _shoulderLRenderX);
	_shoulderLTextY = getInt(themeConfig, "ShoulderLTextY", _shoulderLTextY);
	_shoulderLTextX = getInt(themeConfig, "ShoulderLTextX", _shoulderLTextX);
	_shoulderLTextAlign = getInt(themeConfig, "ShoulderLTextAlign", _shoulderLTextAlign);
	_shoulderRRenderY = getInt(themeConfig, "ShoulderRRenderY", _shoulderRRenderY);
	_shoulderRRenderX = getInt(themeConfig, "ShoulderRRenderX", _shoulderRRenderX);
	_shoulderRTextY = getInt(themeConfig, "ShoulderRTextY", _shoulderRTextY);
	_shoulderRTextX = getInt(themeConfig, "ShoulderRTextX", _shoulderRTextX);
	_shoulderRTextAlign = getInt(themeConfig, "ShoulderRTextAlign", _shoulderRTextAlign);
	_batteryRenderY = getInt(themeConfig, "BatteryRenderY", _batteryRenderY);
	_batteryRenderX = getInt(themeConfig, "BatteryRenderX", _batteryRenderX);
	_statusBarContentOffsetX = getInt(themeConfig, "StatusBarContentOffsetX", _statusBarContentOffsetX);
	_statusBarContentOffsetY = getInt(themeConfig, "StatusBarContentOffsetY", _statusBarContentOffsetY);
	_logoZoomPercent = getInt(themeConfig, "LogoZoomPercent", _logoZoomPercent);
	_logoOffsetX = getInt(themeConfig, "LogoOffsetX", _logoOffsetX);
	_logoOffsetY = getInt(themeConfig, "LogoOffsetY", _logoOffsetY);
	_usernameRenderY = getInt(themeConfig, "UsernameRenderY", _usernameRenderY);
	_usernameRenderX = getInt(themeConfig, "UsernameRenderX", _usernameRenderX);
	_usernameRenderXDS = getInt(themeConfig, "UsernameRenderXDS", _usernameRenderXDS);
	_dateRenderY = getInt(themeConfig, "DateRenderY", _dateRenderY);
	_dateRenderX = getInt(themeConfig, "DateRenderX", _dateRenderX);
	_timeRenderY = getInt(themeConfig, "TimeRenderY", _timeRenderY);
	_timeRenderX = getInt(themeConfig, "TimeRenderX", _timeRenderX);

	_bipsUserPalette = getInt(themeConfig, "BipsUserPalette", _bipsUserPalette);
	_boxUserPalette = getInt(themeConfig, "BoxUserPalette", _boxUserPalette);
	_boxEmptyUserPalette = getInt(themeConfig, "BoxEmptyUserPalette", _boxEmptyUserPalette);
	_boxFullUserPalette = getInt(themeConfig, "BoxFullUserPalette", _boxFullUserPalette);
	_braceUserPalette = getInt(themeConfig, "BraceUserPalette", _braceUserPalette);
	_bubbleUserPalette = getInt(themeConfig, "BubbleUserPalette", _bubbleUserPalette);
	_buttonArrowUserPalette = getInt(themeConfig, "ButtonArrowUserPalette", _buttonArrowUserPalette);
	_cornerButtonUserPalette = getInt(themeConfig, "CornerButtonUserPalette", _cornerButtonUserPalette);
	_cursorUserPalette = getInt(themeConfig, "CursorUserPalette", _cursorUserPalette);
	_dialogBoxUserPalette = getInt(themeConfig, "DialogBoxUserPalette", _dialogBoxUserPalette);
	_folderUserPalette = getInt(themeConfig, "FolderUserPalette", _folderUserPalette);
	_launchDotsUserPalette = getInt(themeConfig, "LaunchDotsUserPalette", _launchDotsUserPalette);
	_movingArrowUserPalette = getInt(themeConfig, "MovingArrowUserPalette", _movingArrowUserPalette);
	_progressUserPalette = getInt(themeConfig, "ProgressUserPalette", _progressUserPalette);
	_scrollWindowUserPalette = getInt(themeConfig, "ScrollWindowUserPalette", _scrollWindowUserPalette);
	_smallCartUserPalette = getInt(themeConfig, "SmallCartUserPalette", _smallCartUserPalette);
	_startBorderUserPalette = getInt(themeConfig, "StartBorderUserPalette", _startBorderUserPalette);
	_startTextUserPalette = getInt(themeConfig, "StartTextUserPalette", _startTextUserPalette);
	_wirelessIconsUserPalette = getInt(themeConfig, "WirelessIconsUserPalette", _wirelessIconsUserPalette);

	_iconA26UserPalette = getInt(themeConfig, "IconA26UserPalette", _iconA26UserPalette);
	_iconCPCUserPalette = getInt(themeConfig, "IconCPCUserPalette", _iconCPCUserPalette);
	_iconCOLUserPalette = getInt(themeConfig, "IconCOLUserPalette", _iconCOLUserPalette);
	_iconGBUserPalette = getInt(themeConfig, "IconGBUserPalette", _iconGBUserPalette);
	_iconGBAUserPalette = getInt(themeConfig, "IconGBAUserPalette", _iconGBAUserPalette);
	_iconGBAModeUserPalette = getInt(themeConfig, "IconGBAModeUserPalette", _iconGBAModeUserPalette);
	_iconGGUserPalette = getInt(themeConfig, "IconGGUserPalette", _iconGGUserPalette);
	_iconHBUserPalette = getInt(themeConfig, "IconHBUserPalette", _iconHBUserPalette);
	_iconIMGUserPalette = getInt(themeConfig, "IconIMGUserPalette", _iconIMGUserPalette);
	_iconINTUserPalette = getInt(themeConfig, "IconINTUserPalette", _iconINTUserPalette);
	_iconM5UserPalette = getInt(themeConfig, "IconM5UserPalette", _iconM5UserPalette);
	_iconManualUserPalette = getInt(themeConfig, "IconManualUserPalette", _iconManualUserPalette);
	_iconMDUserPalette = getInt(themeConfig, "IconMDUserPalette", _iconMDUserPalette);
	_iconMINIUserPalette = getInt(themeConfig, "IconMINIUserPalette", _iconMINIUserPalette);
	_iconMSXUserPalette = getInt(themeConfig, "IconMSXUserPalette", _iconMSXUserPalette);
	_iconNESUserPalette = getInt(themeConfig, "IconNESUserPalette", _iconNESUserPalette);
	_iconNGPUserPalette = getInt(themeConfig, "IconNGPUserPalette", _iconNGPUserPalette);
	_iconPCEUserPalette = getInt(themeConfig, "IconPCEUserPalette", _iconPCEUserPalette);
	_iconPLGUserPalette = getInt(themeConfig, "IconPLGUserPalette", _iconPLGUserPalette);
	_iconSettingsUserPalette = getInt(themeConfig, "IconSettingsUserPalette", _iconSettingsUserPalette);
	_iconSGUserPalette = getInt(themeConfig, "IconSGUserPalette", _iconSGUserPalette);
	_iconSMSUserPalette = getInt(themeConfig, "IconSMSUserPalette", _iconSMSUserPalette);
	_iconSNESUserPalette = getInt(themeConfig, "IconSNESUserPalette", _iconSNESUserPalette);
	_iconUnknownUserPalette = getInt(themeConfig, "IconUnknownUserPalette", _iconUnknownUserPalette);
	_iconVIDUserPalette = getInt(themeConfig, "IconVIDUserPalette", _iconVIDUserPalette);
	_iconWSUserPalette = getInt(themeConfig, "IconWSUserPalette", _iconWSUserPalette);

	_usernameUserPalette = getInt(themeConfig, "UsernameUserPalette", _usernameUserPalette);
	_usernameEdgeAlpha = getInt(themeConfig, "UsernameEdgeAlpha", _usernameEdgeAlpha);
	_progressBarUserPalette = getInt(themeConfig, "ProgressBarUserPalette", _progressBarUserPalette);

	_purpleBatteryAvailable = getInt(themeConfig, "PurpleBatteryAvailable", _purpleBatteryAvailable);
	_rotatingCubesRenderY = getInt(themeConfig, "RotatingCubesRenderY", _rotatingCubesRenderY);
	_renderPhoto = getInt(themeConfig, "RenderPhoto", _renderPhoto);
	_darkLoading = getInt(themeConfig, "DarkLoading", _darkLoading);
	_useAlphaBlend = getInt(themeConfig, "UseAlphaBlend", _useAlphaBlend);

	_playStopSound = getInt(themeConfig, "PlayStopSound", _playStopSound);
	_playStartupJingle = getInt(themeConfig, "PlayStartupJingle", _playStartupJingle);
	_startupJingleDelayAdjust = getInt(themeConfig, "StartupJingleDelayAdjust", _startupJingleDelayAdjust);
	_progressBarColor = getInt(themeConfig, "ProgressBarColor", _progressBarColor);

	_fontPalette1 = getInt(themeConfig, "FontPalette1", _fontPalette1);
	_fontPalette2 = getInt(themeConfig, "FontPalette2", _fontPalette2);
	_fontPalette3 = getInt(themeConfig, "FontPalette3", _fontPalette3);
	_fontPalette4 = getInt(themeConfig, "FontPalette4", _fontPalette4);
	_fontPaletteDisabled1 = getInt(themeConfig, "FontPaletteDisabled1", _fontPalette1);
	_fontPaletteDisabled2 = getInt(themeConfig, "FontPaletteDisabled2", _fontPalette2);
	_fontPaletteDisabled3 = getInt(themeConfig, "FontPaletteDisabled3", _fontPalette3);
	_fontPaletteDisabled4 = getInt(themeConfig, "FontPaletteDisabled4", _fontPalette4);
	_fontPaletteTitlebox1 = getInt(themeConfig, "FontPaletteTitlebox1", _fontPalette1);
	_fontPaletteTitlebox2 = getInt(themeConfig, "FontPaletteTitlebox2", _fontPalette2);
	_fontPaletteTitlebox3 = getInt(themeConfig, "FontPaletteTitlebox3", _fontPalette3);
	_fontPaletteTitlebox4 = getInt(themeConfig, "FontPaletteTitlebox4", _fontPalette4);
	_fontPaletteDialog1 = getInt(themeConfig, "FontPaletteDialog1", _fontPalette1);
	_fontPaletteDialog2 = getInt(themeConfig, "FontPaletteDialog2", _fontPalette2);
	_fontPaletteDialog3 = getInt(themeConfig, "FontPaletteDialog3", _fontPalette3);
	_fontPaletteDialog4 = getInt(themeConfig, "FontPaletteDialog4", _fontPalette4);
	_fontPaletteOverlay1 = getInt(themeConfig, "FontPaletteOverlay1", _fontPalette1);
	_fontPaletteOverlay2 = getInt(themeConfig, "FontPaletteOverlay2", _fontPalette2);
	_fontPaletteOverlay3 = getInt(themeConfig, "FontPaletteOverlay3", _fontPalette3);
	_fontPaletteOverlay4 = getInt(themeConfig, "FontPaletteOverlay4", _fontPalette4);
	_fontPaletteUsername1 = getInt(themeConfig, "FontPaletteUsername1", _fontPalette1);
	_fontPaletteUsername2 = getInt(themeConfig, "FontPaletteUsername2", _fontPalette2);
	_fontPaletteUsername3 = getInt(themeConfig, "FontPaletteUsername3", _fontPalette3);
	_fontPaletteUsername4 = getInt(themeConfig, "FontPaletteUsername4", _fontPalette4);
	_fontPaletteDateTime1 = getInt(themeConfig, "FontPaletteDateTime1", _fontPalette1);
	_fontPaletteDateTime2 = getInt(themeConfig, "FontPaletteDateTime2", _fontPalette2);
	_fontPaletteDateTime3 = getInt(themeConfig, "FontPaletteDateTime3", _fontPalette3);
	_fontPaletteDateTime4 = getInt(themeConfig, "FontPaletteDateTime4", _fontPalette4);
}

#include "GameTitleComponent.h"

#include <algorithm>
#include <nds.h>

#include "TopScreenBoxBmp.h"
#include "common/tonccpy.h" // toncset16
#include "graphics/FontGraphic.h"
#include "graphics/ThemeTextures.h" // tex()
#include "graphics/themefilenames.h" // tfn()

void GameTitleComponent::ensureLoaded() {
	if (!_titleboxLoaded) {
		_titleboxLoaded = true;
		loadTopScreenBoxBmp(tfn().uiDirectory() + "/grf/topscreen_titlebox.bmp",
		                    _titleboxPix, _titleboxW, _titleboxH, _tbBoxX, _tbBoxY, _tbBoxW, _tbBoxH,
		                    MAX_W, MAX_H);
	}
	if (!_startboxLoaded) {
		_startboxLoaded = true;
		_startboxHas = loadTopScreenBoxBmp(tfn().uiDirectory() + "/grf/topscreen_startbox.bmp",
		                                   _startboxPix, _startboxW, _startboxH, _sbBoxX, _sbBoxY,
		                                   _sbBoxW, _sbBoxH, MAX_W, MAX_H);
	}
}

void GameTitleComponent::onItemChanged() {
	_boxSwapTimer = 0; // restart the countdown to the "press start" prompt for this new item
}

bool GameTitleComponent::update() {
	bool dirty = false;

	// The box that's leaving "falls" (slides down and disappears); the one entering "rises" (comes
	// up from below). The swap is driven purely by the idle timer (independent of e.g. the video
	// background) -- the timer itself resets on item change, in onItemChanged().
	if (_boxSwapTimer < SWAP_DELAY) _boxSwapTimer++;
	int wantKind = (_startboxHas && _boxSwapTimer >= SWAP_DELAY) ? 1 : 0;
	int curBoxH = (_boxKind == 1) ? _sbBoxH : _tbBoxH;
	if (_boxKind != wantKind) {
		_boxSlide += SLIDE_STEP; // slide the current box down
		if (_boxSlide >= curBoxH + MARGIN + 1) {
			_boxKind = wantKind; // fully gone -- swap boxes
			int newBoxH = (_boxKind == 1) ? _sbBoxH : _tbBoxH;
			_boxSlide = newBoxH + MARGIN + 1; // position the new one fully below, ready to rise
		}
		dirty = true;
	} else if (_boxSlide > 0) {
		_boxSlide -= SLIDE_STEP; // rise the new box into place
		if (_boxSlide < 0) _boxSlide = 0;
		dirty = true;
	}

	return dirty;
}

void GameTitleComponent::compose(u16 *dst, std::u16string_view text) {
	ensureLoaded();

	// Anchored to the bottom of the top screen, centred horizontally; _boxSlide offsets it
	// vertically during the swap animation.
	const int sx = (SCREEN_WIDTH - _tbBoxW) / 2;
	const int sy = SCREEN_HEIGHT - _tbBoxH - MARGIN + _boxSlide;

	if (_boxKind == 1 && _startboxHas) {
		// "Press start" prompt: replaces the title+text while idle, anchored/centred the same way.
		const int bx = (SCREEN_WIDTH - _sbBoxW) / 2;
		const int by = SCREEN_HEIGHT - _sbBoxH - MARGIN + _boxSlide;
		for (int y = 0; y < _sbBoxH; y++) {
			int dy = by + y;
			if (dy < 0 || dy >= SCREEN_HEIGHT) continue; // clip sliding off the bottom
			for (int x = 0; x < _sbBoxW; x++) {
				u16 p = _startboxPix[(_sbBoxY + y) * _startboxW + (_sbBoxX + x)];
				if (p)
					dst[dy * SCREEN_WIDTH + bx + x] = p;
			}
		}
		return;
	}

	// Blit the title box (from its location in the asset) to the bottom of the top screen. Opaque
	// pixels overwrite (clearing any previous text inside), transparent shows the brick/logo below.
	for (int y = 0; y < _tbBoxH; y++) {
		int dy = sy + y;
		if (dy < 0 || dy >= SCREEN_HEIGHT) continue; // clip sliding off the bottom
		for (int x = 0; x < _tbBoxW; x++) {
			u16 p = _titleboxPix[(_tbBoxY + y) * _titleboxW + (_tbBoxX + x)];
			if (p)
				dst[dy * SCREEN_WIDTH + sx + x] = p;
		}
	}

	FontGraphic *font = tex().smallFont();
	if (!font)
		return;
	const int lineH = font->height();

	int nLines = 1;
	for (size_t p = 0; p < text.size(); p++)
		if (text[p] == u'\n') nLines++;
	// Centre the text block vertically inside the box.
	int posY = sy + _tbBoxH / 2 - (nLines * lineH) / 2;

	// Draw each line centred, in black.
	size_t start = 0;
	int line = 0;
	while (true) {
		size_t nl = text.find(u'\n', start);
		std::u16string_view ln = text.substr(start, (nl == std::u16string_view::npos) ? text.size() - start : nl - start);
		int y0 = posY + line * lineH;
		toncset16(FontGraphic::textBuf[1], 0, SCREEN_WIDTH * lineH);
		font->print(0, 0, true, ln, Alignment::center, FontPalette::regular);
		for (int y = 0; y < lineH && y0 + y < SCREEN_HEIGHT; y++) {
			if (y0 + y < 0) continue;
			for (int x = 0; x < SCREEN_WIDTH; x++)
				if (FontGraphic::textBuf[1][y * SCREEN_WIDTH + x])
					dst[(y0 + y) * SCREEN_WIDTH + x] = RGB15(0, 0, 0) | BIT(15);
		}
		if (nl == std::u16string_view::npos) break;
		start = nl + 1;
		line++;
	}
}

GameTitleComponent &gameTitle() {
	static GameTitleComponent instance;
	return instance;
}

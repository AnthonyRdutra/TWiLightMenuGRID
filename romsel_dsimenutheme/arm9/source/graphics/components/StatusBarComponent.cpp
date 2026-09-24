#include "StatusBarComponent.h"

#include <nds.h>

#include "BatteryComponent.h"
#include "ClockComponent.h"
#include "TopScreenBoxBmp.h"
#include "date.h"
#include "graphics/ThemeConfig.h" // tc(), statusBarContentOffsetX/Y()
#include "graphics/ThemeTextures.h" // getBatteryLevel()
#include "graphics/themefilenames.h" // tfn()

void StatusBarComponent::ensureLoaded() {
	if (_loaded)
		return;
	_loaded = true;
	_has = loadTopScreenBoxBmp(tfn().uiDirectory() + "/grf/status_bar.bmp", _pix, _w, _h,
	                          _boundX, _boundY, _boundW, _boundH, MAX_W, MAX_H);
}

bool StatusBarComponent::needsRedraw() {
	ensureLoaded();
	if (!_has)
		return false; // theme has no status bar asset -- nothing to keep redrawing for
	return retTime() != _lastTime || ThemeTextures::getBatteryLevel() != _lastBattery;
}

void StatusBarComponent::compose(u16 *dst) {
	ensureLoaded();
	if (!_has)
		return;

	const int barX = SCREEN_WIDTH - _w; // anchored to the right, at the top
	const int barY = 0;

	// Bar background (0 = transparent -> skip, shows whatever was already composed underneath).
	for (int y = 0; y < _h; y++) {
		int dy = barY + y;
		if ((unsigned)dy >= SCREEN_HEIGHT) continue;
		for (int x = 0; x < _w; x++) {
			u16 p = _pix[y * _w + x];
			if (!p) continue;
			int dx = barX + x;
			if ((unsigned)dx >= SCREEN_WIDTH) continue;
			dst[dy * SCREEN_WIDTH + dx] = p;
		}
	}

	// Battery is laid out first (fixed position); the clock anchors off its left edge. Both shift
	// together by the theme's StatusBarContentOffsetX/Y (theme.ini) -- a fine-tune knob for themes
	// whose status_bar.bmp art doesn't line up with the hardcoded margins/centering below.
	const int offX = tc().statusBarContentOffsetX();
	const int offY = tc().statusBarContentOffsetY();
	int battLeftEdge = battery().compose(dst, barX + offX, _w, barY + offY, _h);
	hudClock().compose(dst, battLeftEdge, barY + offY, _h);

	// Record what's now on screen, so needsRedraw() can tell when it's stale.
	_lastTime = retTime();
	_lastBattery = ThemeTextures::getBatteryLevel();
}

StatusBarComponent &statusBar() {
	static StatusBarComponent instance;
	return instance;
}

#pragma once

#include <cstddef>
#include <string>
#include <nds/ndstypes.h>

// The status bar in the top-right corner of the DSi theme's top screen: background
// (grf/status_bar.bmp) plus BatteryComponent and ClockComponent composed on top of it. Owns the
// two components' shared layout (battery is right-anchored first; the clock lays its text out
// against the battery's left edge) and the "did anything actually change" cache that lets the
// idle loop skip a full top-screen recompose when the time/battery haven't moved.
class StatusBarComponent {
public:
	// True if the clock or battery has changed since the last compose() -- i.e. a redraw is due.
	// Cheap: reads the clock/battery source values, doesn't touch any pixels. Always false if the
	// theme doesn't ship a status_bar.bmp (nothing to redraw for).
	bool needsRedraw();

	// Composes the bar (background + battery + clock) into `dst`, anchored to the top screen's
	// top-right corner. No-op if the theme doesn't ship a status_bar.bmp.
	void compose(u16 *dst);

	// Bytes of static RAM this component's own background-bar pixel buffer occupies (not counting
	// BatteryComponent/ClockComponent, which report themselves) -- used by
	// ThemeTextures::drawTopDebug()'s memory-usage ranking.
	static constexpr size_t ramFootprintBytes() { return sizeof(_pix); }

private:
	void ensureLoaded();

	static constexpr int MAX_W = 256, MAX_H = 64;

	bool _loaded = false, _has = false;
	u16 _pix[MAX_W * MAX_H];
	int _w = 0, _h = 0;
	int _boundX = 0, _boundY = 0, _boundW = 0, _boundH = 0; // unused for the blit, kept from the loader

	// Last-composed values, so needsRedraw() can tell the idle loop whether anything changed.
	std::string _lastTime;
	int _lastBattery = -999;
};

StatusBarComponent &statusBar();

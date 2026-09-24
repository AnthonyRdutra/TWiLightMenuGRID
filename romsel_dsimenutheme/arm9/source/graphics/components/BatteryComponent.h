#pragma once

#include <cstddef>
#include <nds/ndstypes.h>

// The battery icon in the DSi theme's top-screen status bar. Position is fixed relative to the
// status bar's right edge (so it never moves as the clock's text width changes -- see
// ClockComponent, which anchors off this component's left edge instead of the other way around).
class BatteryComponent {
public:
	// Composes the battery icon into `dst`, right-anchored inside the bar rect [barX, barX+barW)
	// x [barY, barY+barH). Returns the icon's left edge in screen coordinates (or barX+barW if no
	// icon could be drawn), for ClockComponent to lay its text out against. Lazily loads the
	// battery icon assets on first use.
	int compose(u16 *dst, int barX, int barW, int barY, int barH);

	// Bytes of static RAM this component's pixel buffer occupies (all STATES, decoded, whether or
	// not currently visible) -- used by ThemeTextures::drawTopDebug()'s memory-usage ranking.
	static constexpr size_t ramFootprintBytes() { return sizeof(_pix); }

	static constexpr int MAX_W = 32, MAX_H = 24, STATES = 6;

private:
	void ensureLoaded();

	static constexpr int RIGHT_INSET = 10; // px from the bar's right edge to the icon

	bool _loaded = false;
	u16 _pix[STATES][MAX_W * MAX_H];
	int _w[STATES] = {0}, _h[STATES] = {0};
};

BatteryComponent &battery();

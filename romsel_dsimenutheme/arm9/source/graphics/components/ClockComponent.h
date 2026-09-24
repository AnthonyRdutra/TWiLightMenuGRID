#pragma once

#include <nds/ndstypes.h>

// The time text in the DSi theme's top-screen status bar. Right-aligned, growing leftward from a
// point anchored to BatteryComponent's left edge (see StatusBarComponent, which composes the two
// together) -- so changes in the text's width never push the battery icon around, only the clock
// itself shifts.
class ClockComponent {
public:
	// Composes the current time into `dst`, right-aligned so its right edge sits `gap` px to the
	// left of `rightEdge` (typically BatteryComponent::compose()'s return value), vertically
	// centred within [barY, barY+barH).
	void compose(u16 *dst, int rightEdge, int barY, int barH, int gap = 5) const;

private:
	// Scale of the bar's time text (nearest-neighbor). NUM/DEN < 1 shrinks it. E.g. 3/4 = 75%.
	static constexpr int SCALE_NUM = 3;
	static constexpr int SCALE_DEN = 4;
	// Extra spacing (px, native scale) between time characters (tracking).
	static constexpr int TRACKING = 3;
};

// Named hudClock(), not clock() -- collides with <time.h>'s global clock_t clock(void) otherwise.
ClockComponent &hudClock();

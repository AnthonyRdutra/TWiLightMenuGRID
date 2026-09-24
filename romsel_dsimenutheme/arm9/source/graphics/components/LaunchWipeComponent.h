#pragma once

#include "Component.h"

// Full-screen circular wipe played while a game is launching (the `applaunchprep` window between
// pressing START and nds-bootstrap taking over). A dark circle spawns at the selected grid icon,
// on the bottom screen, and expands until it has swallowed both screens.
//
// The two physical screens are separate framebuffers with no shared coordinate space, but the
// wipe is meant to read as *one* circle spilling from the bottom screen up into the top one — so
// both draw methods measure distance in the same imaginary combined space: the top screen sits
// above the bottom screen, offset by SCREEN_GAP (a stand-in for the physical bezel). The origin is
// captured once per launch (not every frame) from wherever the grid's selection actually is, so
// the wipe starts exactly on the icon that was launched.
//
// The two screens need two different draw paths: the bottom screen's grid/carousel items are
// gl2d sprites, so the wipe has to be gl2d geometry (a triangle fan approximating a filled circle)
// to actually render on top of them; the top screen has no gl2d layer at all (see
// romsel_dsimenutheme/FRONTEND.md §1) and is a plain pixel buffer (BG_GFX_SUB), so its wipe is a
// direct scanline fill.
class LaunchWipeComponent : public Component {
public:
	// Grows the radius while applaunchprep is true, capturing the spawn origin and target radius
	// on the first frame of each launch. Resets everything once applaunchprep clears (so the next
	// launch starts over, from wherever the selection is at that time).
	bool update() override;

	// Draws the bottom-screen circle. Must be called from within glBegin2D()/glEnd2D() (it issues
	// gl2d calls), in place of the grid/carousel's old launch-icon draw.
	void draw() override;

	// Draws the top-screen circle. Independent of gl2d/glBegin2D -- writes BG_GFX_SUB directly --
	// so call this anywhere in the frame, whether or not the bottom screen is mid-glBegin2D.
	void drawTopScreen() const;

private:
	void captureOrigin();

	int _radius = 0;
	int _maxRadius = 0;      // 0 means "not currently launching" (see captureOrigin())
	int _growthPerFrame = 1;
	int _originX = 0;
	int _originY = 0; // bottom-screen-local Y of the spawn point
};

LaunchWipeComponent &launchWipe();

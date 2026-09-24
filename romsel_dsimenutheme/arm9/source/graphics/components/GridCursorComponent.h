#pragma once

#include "Component.h"

// Optional highlight drawn around the selected grid cell.
//
// Off by default (ThemeLayout::gridCursorEnabled() == false): no theme ships a dedicated cursor
// asset today, and selection is already conveyed by GridView's zoom animation. When
// a theme opts in via layout.json ("grid":{"cursor":{"enabled":true}}), this draws a plain gl2d
// outline (glBox -- no new texture/VRAM cost) around the selected box instead of/in addition to
// the zoom. ThemeLayout::assetPath("gridCursor") is reserved plumbing for a future dedicated
// sprite asset once one is designed; this class doesn't need it to draw a correct, safe default.
//
// Also doubles as this codebase's first consumer of ThemeLayout's "sprites" table: if the theme
// defines a "gridCursorBlink" sprite table ({"frameCount", "frameDelayVBlanks"}), the highlight
// blinks at that rate instead of staying solid -- proof that sprite_tables can drive an animation
// end to end, the way the goal's per-theme JSON is meant to.
class GridCursorComponent : public Component {
public:
	// Advances the optional blink animation. Returns true only on the frame the phase changes, so
	// idle (non-blinking, or disabled) cursors don't force extra redraws.
	bool update() override;

	// No-op: this component's real draw entrypoint is drawAt(), which needs the caller's already-
	// computed cell geometry (GridView knows cx/cy/boxPx for the selected cell already).
	void draw() override {}

	// Draws the highlight around a cell of size boxPx x boxPx centred at (cx, cy). Safe to call
	// unconditionally (it checks gridCursorEnabled()/the blink phase itself and no-ops otherwise).
	void drawAt(int cx, int cy, int boxPx) const;

private:
	int _frame = 0;
	int _delayCounter = 0;
};

GridCursorComponent &gridCursor();

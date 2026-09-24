#pragma once

// Draws one grid cell: the box/folder background plus the game icon on top, at the given scale.
// A small, single-purpose helper -- takes fully-resolved position/scale so it has no dependency
// on selection state, scroll position, or any other piece of the grid (see GridView, which
// computes those and calls this once per visible cell).
class GridItemComponent {
public:
	// itemIndex: flat item index (col*rows + row). cx/cy: cell centre, in screen pixels.
	// scale: fixed point, 4096 = 1.0x (ThemeLayout::gridActiveScale() at rest).
	static void draw(int itemIndex, int cx, int cy, int scale);
};

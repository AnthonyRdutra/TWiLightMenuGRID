#pragma once

#include "Component.h"

// The DSi theme's icon grid, as a single self-contained component.
//
// Rebuilt from scratch (replacing the earlier GridLayoutComponent/GridSelectionComponent/
// GridRenderer split) to fix a real, recurring class of bug in this codebase: pieces of the grid
// kept sharing mutable state and helper functions with the single-row carousel that the grid
// replaced for the DSi theme (titleboxXpos/titleboxXdest, titleboxXspacing, moveCursor()...), and
// carousel-only code paths that were never taught about the grid kept stomping on that shared
// state. See romsel_dsimenutheme/FRONTEND.md §12 for the specific spots this class and its
// fileBrowse.cpp callers now replace. GridView owns its OWN scroll position, entirely separate
// from titleboxXpos/titleboxXdest, so no carousel code path can ever desync it again.
//
// update() must run once per vblank, before the frame's redraw-needed decision is computed
// (mirrors where the selection-zoom/scroll-chase logic ran historically). draw() must run once,
// only when the caller has already decided a redraw is due.
class GridView : public Component {
public:
	GridView();

	bool update() override;
	void draw() override;

	// ---- Geometry (theme-driven, see ThemeLayout) -- also used by fileBrowse.cpp for
	// hit-testing, so touch input can never drift from what's actually drawn. ----
	int rows() const;
	int colsLeft() const;
	int colsRight() const;
	int columnCenterX(int col, int screen) const;
	int rowCenterY(int row) const;

	// ---- Navigation entry points for fileBrowse.cpp. These are the grid's *only* horizontal
	// scroll state -- callers must never write titleboxXpos/titleboxXdest expecting it to affect
	// the grid. ----
	// Smoothly scrolls column `col` into view (normal left/right navigation).
	void scrollToColumn(int col, int screen);
	// Instantly snaps to column `col` with no animation (page/directory changes, boot).
	void jumpToColumn(int col, int screen);
	// Instantly snaps to whichever column holds flat item index `item` (a CURPOS-style index) --
	// a convenience for callers that only know the flat position, e.g. the persisted cursor
	// position restored at startup.
	void jumpToItem(int item, int screen);

private:
	// The animation's own 12-bit fixed-point progress unit (4096 = "whole") -- not a
	// theme-tunable quantity, so it stays a literal rather than a ThemeLayout getter.
	static constexpr int PROGRESS_FULL = 4096;

	int _scrollPos[2];
	int _scrollDest[2];
	int _selCur;
	int _selPrev;
	int _zoomFP;
};

GridView &gridView();

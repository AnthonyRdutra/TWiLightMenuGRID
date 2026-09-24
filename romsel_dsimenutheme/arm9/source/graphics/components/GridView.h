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

	// ---- Touch-drag scrolling (hold + move horizontally on the grid). ----
	// Nudges the scroll position by `dxPx` screen pixels of finger movement this frame (positive
	// = finger moved right, content follows the finger like any touchscreen scroll surface).
	// Writes _scrollPos and _scrollDest together so update()'s destination-chase never fights the
	// touch -- callers still don't touch _scrollPos/_scrollDest directly. Clamped to the valid
	// column range, so a fast/long swipe can't run the camera off the end of the list.
	void dragScrollBy(int dxPx, int screen);
	// Column nearest the current scroll position -- the snap target once a touch-drag ends
	// (fileBrowse.cpp resolves CURPOS from this plus whichever row was selected at drag start).
	int nearestColumn(int screen) const;

private:
	// The animation's own 12-bit fixed-point progress unit (4096 = "whole") -- not a
	// theme-tunable quantity, so it stays a literal rather than a ThemeLayout getter.
	static constexpr int PROGRESS_FULL = 4096;

	int _scrollPos[2];
	int _scrollDest[2];
	// _scrollPos as of the last update() call -- lets update() notice a change made directly by
	// dragScrollBy() (bypasses the chase below) so it can still mark the frame dirty every vblank.
	int _lastScrollPos[2];
	int _selCur;
	int _selPrev;
	int _zoomFP;
};

GridView &gridView();

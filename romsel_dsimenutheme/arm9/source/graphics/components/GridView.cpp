#include "GridView.h"

#include <algorithm>
#include <gl2d.h>

#include "GridCursorComponent.h"
#include "GridItemComponent.h"
#include "common/twlmenusettings.h" // ms()
#include "fileBrowse.h"             // CURPOS
#include "graphics/ThemeLayout.h"
#include "graphics/color.h"
#include "graphics/menubar.h" // menuBarDraw

// Defined in fileBrowse.cpp; true only while a carousel-only drag gesture is in progress. These
// never actually go true while the DSi grid is the active theme (its own touch handling doesn't
// use them), but the check is kept to match this codebase's existing "don't fight a drag in
// progress" convention at zero cost.
extern bool draggingIcons;
extern bool scrollWindowTouched;

// Defined in fileBrowse.cpp; true only while a *grid* touch-drag (hold + move horizontally) is in
// progress. dragScrollBy() already keeps _scrollPos/_scrollDest in lockstep so there's no gap for
// the chase below to act on mid-drag, but the guard is kept anyway to match the convention above.
extern bool draggingGrid;

// Defined elsewhere (fileBrowse.cpp/main.cpp); count of items currently spawned in the list.
extern int spawnedtitleboxes;

// Defined in fileBrowse.cpp; last valid flat item index on the current page -- needed here to
// clamp the drag's scroll range to the actual last column instead of running off the list.
extern int last_used_box;

GridView::GridView()
	: _scrollPos{0, 0}, _scrollDest{0, 0}, _lastScrollPos{0, 0}, _selCur(CURPOS), _selPrev(-1),
	  _zoomFP(PROGRESS_FULL) {
	// _selCur starts at the already-persisted cursor position, at full scale, so the item that's
	// already selected when the grid first appears doesn't play a phantom zoom-in animation (a
	// carryover from the old carousel's selection-change bookkeeping, which used a -1 sentinel
	// for "nothing selected yet" and so always animated item 0 in on first render).
}

int GridView::rows() const { return tl().gridRows(); }
int GridView::colsLeft() const { return tl().gridColsLeft(); }
int GridView::colsRight() const { return tl().gridColsRight(); }

int GridView::columnCenterX(int col, int screen) const {
	return 128 + col * tl().gridColSpacing() - _scrollPos[screen];
}

int GridView::rowCenterY(int row) const {
	return tl().gridRowOffsetY() + row * tl().gridRowSpacing();
}

void GridView::scrollToColumn(int col, int screen) {
	_scrollDest[screen] = col * tl().gridColSpacing();
}

void GridView::jumpToColumn(int col, int screen) {
	_scrollPos[screen] = _scrollDest[screen] = col * tl().gridColSpacing();
}

void GridView::jumpToItem(int item, int screen) {
	jumpToColumn(item / rows(), screen);
}

void GridView::dragScrollBy(int dxPx, int screen) {
	int maxCol = last_used_box / rows();
	int maxScroll = maxCol * tl().gridColSpacing();
	// Content follows the finger: dragging right (dxPx > 0) reveals earlier columns, i.e. scroll
	// position decreases -- same sign convention the single-row carousel's own live-drag uses
	// (titleboxXdest -= touch.px delta in fileBrowse.cpp).
	_scrollPos[screen] = std::clamp(_scrollPos[screen] - dxPx, 0, maxScroll);
	_scrollDest[screen] = _scrollPos[screen];
}

int GridView::nearestColumn(int screen) const {
	int maxCol = last_used_box / rows();
	int spacing = tl().gridColSpacing();
	int col = (_scrollPos[screen] + spacing / 2) / spacing;
	return std::clamp(col, 0, maxCol);
}

bool GridView::update() {
	bool dirty = false;

	// Selection zoom: grow the newly-selected item, shrink the previously-selected one.
	if (_selCur != CURPOS) {
		_selPrev = _selCur;
		_selCur = CURPOS;
		_zoomFP = 0;
	}
	if (_zoomFP < PROGRESS_FULL) {
		// Linear step (no front-loaded ease) -- a front-loaded curve jumps ~half the distance on
		// the first frame, which reads as a jolt when the top edge of a row-1 box snaps upward.
		_zoomFP += tl().gridZoomStep();
		if (_zoomFP > PROGRESS_FULL) _zoomFP = PROGRESS_FULL;
		dirty = true;
	}

	// Horizontal scroll-chase: slide toward the destination column set by scrollToColumn()/
	// jumpToColumn(). This is the grid's own scroll state -- entirely separate from the single-
	// row carousel's titleboxXpos/titleboxXdest, which other themes' code keeps writing to
	// unconditionally in a dozen places throughout fileBrowse.cpp.
	//
	// The step is proportional to the remaining distance (min 1px), not a fixed pixel amount --
	// holding left/right fires moveCursorGrid() on every key-repeat tick, which can push the
	// destination forward by a whole column every couple of frames. A fixed step can't keep up
	// with that (the gap grows for as long as the key is held, then takes a very long time to
	// close), so the drawn column window -- which always follows CURPOS instantly -- ends up
	// showing columns at positions far from where the lagging scroll thinks they are, making
	// items appear to vanish until the camera eventually crawls back into sync. A proportional
	// step (same pattern the single-row carousel already uses for this exact problem, see
	// graphics.cpp's non-DSi scroll-chase) closes a big gap fast and a small one precisely.
	if (!draggingIcons && !scrollWindowTouched && !draggingGrid) {
		int sd = ms().secondaryDevice;
		if (_scrollPos[sd] != _scrollDest[sd]) {
			int diff = _scrollDest[sd] - _scrollPos[sd];
			int step = std::max(std::abs(diff) / tl().gridScrollSpeedX(), 1);
			if (diff < 0) {
				_scrollPos[sd] = std::max(_scrollPos[sd] - step, _scrollDest[sd]);
			} else {
				_scrollPos[sd] = std::min(_scrollPos[sd] + step, _scrollDest[sd]);
			}
			dirty = true;
		}
	}

	// Detect any change to _scrollPos since the last update() call -- covers both the chase above
	// and direct writes from dragScrollBy() (fileBrowse.cpp), which bypasses the chase (guarded
	// off via draggingGrid) and writes _scrollPos straight from touch input, once per vblank while
	// a finger is dragging the grid. Without this, CURPOS crossing into a new column was the only
	// thing that marked a frame dirty (see vBlankHandler's curposPrev check in graphics.cpp), so
	// the grid only appeared to move in per-column jumps instead of tracking the finger smoothly.
	for (int s = 0; s < 2; s++) {
		if (_scrollPos[s] != _lastScrollPos[s]) {
			_lastScrollPos[s] = _scrollPos[s];
			dirty = true;
		}
	}

	return dirty;
}

void GridView::draw() {
	const int sd = ms().secondaryDevice;
	const int rowCount = rows();
	const int selCol = CURPOS / rowCount;
	const int active = tl().gridActiveScale();
	const int inactive = tl().gridInactiveScale();

	// Draw up to (colsLeft + 1 + colsRight) columns so items enter/leave smoothly.
	for (int c = std::max(selCol - colsLeft(), 0); c <= selCol + colsRight(); c++) {
		int cx = columnCenterX(c, sd);
		for (int r = 0; r < rowCount; r++) {
			int i = c * rowCount + r;
			if (i >= spawnedtitleboxes)
				continue;

			int boxScale;
			if (i == _selCur)
				boxScale = inactive + ((active - inactive) * _zoomFP >> 12);
			else if (i == _selPrev)
				boxScale = active - ((active - inactive) * _zoomFP >> 12);
			else
				boxScale = inactive;

			GridItemComponent::draw(i, cx, rowCenterY(r), boxScale);
		}
	}

	// Optional highlight around the selected cell (off unless a theme opts in -- see
	// GridCursorComponent). Drawn after every cell so it isn't covered by a neighbour's box art.
	if (tl().gridCursorEnabled()) {
		int selRow = CURPOS % rowCount;
		int selScale = (_selCur == CURPOS) ? (inactive + ((active - inactive) * _zoomFP >> 12)) : active;
		int boxPx = (selScale * 64) >> 12;
		gridCursor().drawAt(columnCenterX(selCol, sd), rowCenterY(selRow), boxPx);
	}

	// Menu bar on top of the items (drawn last = upper layer).
	menuBarDraw();
	glColor(RGB15(31, 31, 31));
}

GridView &gridView() {
	static GridView instance;
	return instance;
}

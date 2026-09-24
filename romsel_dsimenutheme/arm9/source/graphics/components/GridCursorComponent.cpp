#include "GridCursorComponent.h"

#include <gl2d.h>

#include "graphics/ThemeLayout.h"
#include "graphics/color.h"

bool GridCursorComponent::update() {
	if (!tl().gridCursorEnabled())
		return false;

	const ThemeLayout::SpriteTable *blink = tl().spriteTable("gridCursorBlink");
	if (!blink || blink->frameCount <= 1 || blink->frameDelayVBlanks <= 0)
		return false; // no blink configured -- steady highlight, nothing to animate per frame

	if (++_delayCounter < blink->frameDelayVBlanks)
		return false;

	_delayCounter = 0;
	_frame = (_frame + 1) % blink->frameCount;
	return true; // visibility phase just changed -- keep this frame's redraw happening
}

void GridCursorComponent::drawAt(int cx, int cy, int boxPx) const {
	if (!tl().gridCursorEnabled())
		return;

	const ThemeLayout::SpriteTable *blink = tl().spriteTable("gridCursorBlink");
	if (blink && blink->frameCount > 1 && (_frame % 2) == 1)
		return; // blinking "off" phase this cycle -- skip drawing

	int half = boxPx / 2 + 2; // a couple of px outside the box art
	glBox(cx - half, cy - half, cx + half, cy + half, RGB15(31, 31, 31));
}

GridCursorComponent &gridCursor() {
	static GridCursorComponent instance;
	return instance;
}

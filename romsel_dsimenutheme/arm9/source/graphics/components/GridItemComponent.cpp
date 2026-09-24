#include "GridItemComponent.h"

#include <gl2d.h>

#include "graphics/ThemeLayout.h"   // tl()
#include "graphics/ThemeTextures.h" // tex()
#include "iconTitle.h"              // drawIconScaled
#include "ndsheaderbanner.h"        // isDirectory, customIcon

void GridItemComponent::draw(int itemIndex, int cx, int cy, int scale) {
	int boxPx = (scale * 64) >> 12; // px do box p/ centralizar (art 64x64)
	int bx = cx - boxPx / 2;
	int by = cy - boxPx / 2;

	// Frame/background from the theme: folder for directories, icon box otherwise.
	if (isDirectory[itemIndex]) {
		glSpriteScale(bx, by, scale, GL_FLIP_NONE, &tex().folderImage()[0]);
		if (!customIcon[itemIndex])
			return; // folder art already shows the folder; no game icon
	} else {
		glSpriteScale(bx, by, scale, GL_FLIP_NONE, &tex().boxfullImage()[0]);
	}

	// Game icon (32px art) on top of the box; its on-screen size is half the box, so its scale
	// tracks boxScale directly. During the fractional zoom, glSpriteScale truncates the
	// destination and drops the sprite's outer texel (a missing 1px border that only fills in
	// once the zoom settles at 1.0), so bias the scale up by ~1 texel (128 = one 32px texel) to
	// ceil the size and keep the edge covered throughout; clamp to native 1:1.
	int iconScale = scale + 127;
	if (iconScale > tl().gridActiveScale())
		iconScale = tl().gridActiveScale();
	int iconPx = (32 * iconScale) >> 12;
	drawIconScaled(cx - iconPx / 2, cy - iconPx / 2, itemIndex, iconScale);
}

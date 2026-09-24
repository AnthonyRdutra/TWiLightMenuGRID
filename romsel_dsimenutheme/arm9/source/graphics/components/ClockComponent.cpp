#include "ClockComponent.h"

#include <nds.h>

#include "common/tonccpy.h" // toncset16
#include "date.h"
#include "graphics/FontGraphic.h"
#include "graphics/ThemeTextures.h" // tex()

void ClockComponent::compose(u16 *dst, int rightEdge, int barY, int barH, int gap) const {
	FontGraphic *font = tex().smallFont();
	if (!font)
		return;
	const int lineH = font->height();

	std::string timeStr = retTime();
	std::u16string t = FontGraphic::utf8to16(timeStr);

	int tw = 0; // native width of the time text (with inter-character tracking)
	for (size_t i = 0; i < t.size(); i++) {
		tw += font->calcWidth(std::u16string(1, t[i]));
		if (i + 1 < t.size()) tw += TRACKING;
	}
	int twS = tw * SCALE_NUM / SCALE_DEN;   // scaled-down width
	int thS = lineH * SCALE_NUM / SCALE_DEN; // scaled-down height
	if (twS <= 0)
		return;

	// Render each character (with tracking) at native size into the scratch buffer first.
	toncset16(FontGraphic::textBuf[1], 0, SCREEN_WIDTH * lineH);
	int penX = 0;
	for (size_t i = 0; i < t.size(); i++) {
		std::u16string ch(1, t[i]);
		font->print(penX, 0, true, ch, Alignment::left, FontPalette::regular);
		penX += font->calcWidth(ch) + TRACKING;
		if (penX >= SCREEN_WIDTH) break;
	}

	// Right edge of the time field is fixed (rightEdge - gap); the text grows leftward from there.
	// Rendered in black, downsampled via OR (a destination pixel lights up if ANY source pixel in
	// its scale-mapped cell is lit), which preserves thin font strokes better than averaging would.
	int txRight = rightEdge - gap;
	int tx = txRight - twS;
	int ty = barY + (barH - thS) / 2;
	for (int y = 0; y < thS; y++) {
		int dy = ty + y;
		if ((unsigned)dy >= SCREEN_HEIGHT) continue;
		int sy0 = y * lineH / thS, sy1 = (y + 1) * lineH / thS;
		if (sy1 <= sy0) sy1 = sy0 + 1;
		for (int x = 0; x < twS; x++) {
			int sx0 = x * tw / twS, sx1 = (x + 1) * tw / twS;
			if (sx1 <= sx0) sx1 = sx0 + 1;
			bool on = false;
			for (int sy = sy0; sy < sy1 && !on; sy++)
				for (int sx = sx0; sx < sx1; sx++)
					if (FontGraphic::textBuf[1][sy * SCREEN_WIDTH + sx]) { on = true; break; }
			if (!on) continue;
			int dx = tx + x;
			if ((unsigned)dx >= SCREEN_WIDTH) continue;
			dst[dy * SCREEN_WIDTH + dx] = RGB15(0, 0, 0) | BIT(15);
		}
	}
}

ClockComponent &hudClock() {
	static ClockComponent instance;
	return instance;
}

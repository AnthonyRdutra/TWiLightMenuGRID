#include "BatteryComponent.h"

#include <nds.h>
#include <vector>

#include "common/lodepng.h"
#include "graphics/ThemeTextures.h" // tex(), getBatteryLevel()
#include "graphics/themefilenames.h" // tfn()

namespace {
// Battery states mapped from ThemeTextures::getBatteryLevel(): 0=empty .. 4=full, 5=charging.
constexpr const char *FILES[BatteryComponent::STATES] = {
	"/battery/battery0.png", "/battery/battery1.png", "/battery/battery2.png",
	"/battery/battery3.png", "/battery/battery4.png", "/battery/batterycharge.png",
};
} // namespace

void BatteryComponent::ensureLoaded() {
	if (_loaded)
		return;
	_loaded = true;
	for (int i = 0; i < STATES; i++) {
		std::vector<unsigned char> img;
		unsigned w = 0, h = 0;
		if (lodepng::decode(img, w, h, tfn().uiDirectory() + FILES[i]) != 0)
			continue;
		if (w == 0 || h == 0 || w > MAX_W || h > MAX_H)
			continue;
		_w[i] = w;
		_h[i] = h;
		for (unsigned y = 0; y < h; y++) {
			for (unsigned x = 0; x < w; x++) {
				unsigned o = (y * w + x) * 4;
				u8 r = img[o], g = img[o + 1], b = img[o + 2], a = img[o + 3];
				_pix[i][y * MAX_W + x] = (a >= 128)
					? ((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | BIT(15)) : 0;
			}
		}
	}
}

int BatteryComponent::compose(u16 *dst, int barX, int barW, int barY, int barH) {
	ensureLoaded();

	int lvl = ThemeTextures::getBatteryLevel();
	int st = (lvl >= 7) ? 5 : (lvl < 0 ? 0 : (lvl > 4 ? 4 : lvl));
	int iw = _w[st], ih = _h[st];

	int ix = barX + barW - RIGHT_INSET - (iw > 0 ? iw : 0);
	int iy = barY + (barH - ih) / 2;

	if (iw > 0) {
		for (int y = 0; y < ih; y++) {
			int dy = iy + y;
			if ((unsigned)dy >= SCREEN_HEIGHT) continue;
			for (int x = 0; x < iw; x++) {
				u16 p = _pix[st][y * MAX_W + x];
				if (!p) continue;
				int dx = ix + x;
				if ((unsigned)dx >= SCREEN_WIDTH) continue;
				dst[dy * SCREEN_WIDTH + dx] = p;
			}
		}
	}

	return ix;
}

BatteryComponent &battery() {
	static BatteryComponent instance;
	return instance;
}

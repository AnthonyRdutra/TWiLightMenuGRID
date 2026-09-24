#include "GameLogoComponent.h"

#include <nds.h>
#include <vector>

#include "common/lodepng.h"
#include "graphics/ThemeConfig.h" // tc(), logoZoomPercent/OffsetX/OffsetY()
#include "graphics/color.h"

bool GameLogoComponent::beginItemChange(const std::string &romName) {
	if (romName == _logoKey)
		return false; // already resolved for this item
	_logoKey = romName;
	_logoScaleDest = 0.0f;    // zoom-out the previous logo (pixels kept so it can shrink away)
	_pendingLogoPath.clear(); // cancel any decode still scheduled for the previous item
	return true;
}

void GameLogoComponent::scheduleDecode(const std::string &logoPath) {
	_pendingLogoPath = logoPath;
	_pendingLogoDelay = LOAD_DELAY;
}

// Decodes (lodepng) + downscales the PNG into _logoPix. COSTLY -- only ever called from update(),
// after the selection has been stable for LOAD_DELAY frames.
void GameLogoComponent::decodeLogoFile(const std::string &logoPath) {
	_logoPresent = false;
	std::vector<unsigned char> img;
	unsigned w = 0, h = 0;
	if (lodepng::decode(img, w, h, logoPath) != 0 || w == 0 || h == 0)
		return;

	// Integer scaling: pick the smallest integer factor 1/N that fits (uniform N-to-1 sampling).
	const int maxW = 240, maxH = 120;
	int N = 1;
	while ((int)w / N > maxW || (int)h / N > maxH)
		N++;
	int tw = (int)w / N, th = (int)h / N;
	if (tw < 1) tw = 1;
	if (th < 1) th = 1;

	for (int y = 0; y < th; y++) {
		int syi = y * N; // exact step N = uniform 1/N downscale
		for (int x = 0; x < tw; x++) {
			int sxi = x * N;
			int i = (syi * (int)w + sxi) * 4;
			u8 r = img[i], g = img[i + 1], b = img[i + 2], a = img[i + 3];
			_logoPix[y * 256 + x] = (a >= 128) ? ((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | BIT(15)) : 0;
		}
	}
	_logoW = tw;
	_logoH = th;
	_logoPresent = true;
}

bool GameLogoComponent::update() {
	bool dirty = false;

	// Deferred logo decode: once the item has settled, decode and arm the zoom-in.
	if (!_pendingLogoPath.empty() && --_pendingLogoDelay <= 0) {
		std::string path = _pendingLogoPath;
		_pendingLogoPath.clear();
		decodeLogoFile(path); // costly, but only runs once the item is stable (user stopped)
		if (_logoPresent) {
			_logoScale = 0.0f;     // starts tiny...
			_logoScaleDest = 1.0f; // ...and grows (zoom-in on appearing)
		}
	}

	// Animate the scale towards the target.
	if (_logoScale != _logoScaleDest) {
		if (_logoScale < _logoScaleDest) {
			_logoScale += ZOOM_IN_STEP;
			if (_logoScale > _logoScaleDest) _logoScale = _logoScaleDest;
		} else {
			_logoScale -= ZOOM_OUT_STEP;
			if (_logoScale < _logoScaleDest) _logoScale = _logoScaleDest;
		}
		dirty = true;
	}

	return dirty;
}

void GameLogoComponent::compose(u16 *dst) const {
	if (!_logoPresent || _logoScale <= 0.01f)
		return;

	// LogoZoomPercent (theme.ini, 100 = tamanho normal) multiplica o tamanho-alvo por cima da
	// animação de zoom-in/out da seleção (_logoScale continua indo de 0 a este tamanho, não de
	// 0 a 100%). LogoOffsetX/Y deslocam a partir do centro da tela superior (px, pode ser negativo).
	const float zoom = tc().logoZoomPercent() / 100.0f;
	int dw = (int)(_logoW * _logoScale * zoom);
	int dh = (int)(_logoH * _logoScale * zoom);
	if (dw < 1) dw = 1;
	if (dh < 1) dh = 1;
	int lx = (SCREEN_WIDTH - dw) / 2;
	int ly = (SCREEN_HEIGHT - dh) / 2;
	if (ly < 0) ly = 0;
	lx += tc().logoOffsetX();
	ly += tc().logoOffsetY();

	// Drop shadow: the logo's silhouette, offset and alpha-blended over the already-composed
	// background. Drawn BEFORE the logo, so the logo ends up on top.
	for (int y = 0; y < dh; y++) {
		int sy = y * _logoH / dh;
		if (sy >= _logoH) sy = _logoH - 1;
		int py = ly + y + SHADOW_DY;
		if (py < 0 || py >= SCREEN_HEIGHT) continue;
		for (int x = 0; x < dw; x++) {
			int sx = x * _logoW / dw;
			if (sx >= _logoW) sx = _logoW - 1;
			if (!_logoPix[sy * 256 + sx]) continue; // only where the logo is opaque
			int px = lx + x + SHADOW_DX;
			if ((unsigned)px >= SCREEN_WIDTH) continue;
			u16 &d = dst[py * SCREEN_WIDTH + px];
			d = alphablend(RGB15(0, 0, 0) | BIT(15), d, SHADOW_ALPHA);
		}
	}

	// Logo on top of the shadow.
	for (int y = 0; y < dh; y++) {
		int sy = y * _logoH / dh; // nearest-neighbor on the Y axis
		if (sy >= _logoH) sy = _logoH - 1;
		int py = ly + y;
		// `continue`, not `break`: with LogoOffsetY, py can start negative and grow back into
		// range as y increases (it isn't guaranteed >= 0 like it was when ly was always clamped).
		if ((unsigned)py >= SCREEN_HEIGHT) continue;
		for (int x = 0; x < dw; x++) {
			int sx = x * _logoW / dw; // nearest-neighbor on the X axis
			if (sx >= _logoW) sx = _logoW - 1;
			u16 p = _logoPix[sy * 256 + sx];
			if (!p) continue;
			int px = lx + x;
			if ((unsigned)px < SCREEN_WIDTH)
				dst[py * SCREEN_WIDTH + px] = p;
		}
	}
}

GameLogoComponent &gameLogo() {
	static GameLogoComponent instance;
	return instance;
}

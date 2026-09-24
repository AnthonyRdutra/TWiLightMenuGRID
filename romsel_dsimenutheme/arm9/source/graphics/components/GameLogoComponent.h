#pragma once

#include <cstddef>
#include <string>
#include <nds/ndstypes.h>

// The selected game's logo on the DSi theme's top screen: decode, zoom in/out animation, and
// composition (with a drop shadow) into the shared top-screen compose buffer.
//
// Decoding a PNG (lodepng) is too costly to do on every selection change while scrolling, so
// beginItemChange()/scheduleDecode() only ever record *what* to decode; the actual decode happens
// inside update(), deferred until the selection has stayed put for LOGO_LOAD_DELAY frames (see
// ThemeTextures::loadGameLogo(), which still owns resolving *which* file to decode -- asset-index
// lookups are shared with the per-game video background, so that resolution stays there).
class GameLogoComponent {
public:
	// Called once per selection change (from ThemeTextures::loadGameLogo()). `romName` is the
	// bare rom name used as the "is this actually a different item" key. Returns false (and does
	// nothing else) if `romName` is the item already current -- callers must skip their own
	// per-item-changed work too in that case (matches the original combined guard this replaces).
	bool beginItemChange(const std::string &romName);

	// Schedules the deferred decode of `logoPath` (called by ThemeTextures::loadGameLogo() once it
	// has resolved a path, only when beginItemChange() returned true and a path was found).
	void scheduleDecode(const std::string &logoPath);

	// Advances the deferred-decode countdown and the zoom animation. Returns true if a redraw is
	// needed (animation stepped, or a decode just completed).
	bool update();

	// Composes the logo (with drop shadow) into `dst`, centred on the top screen. No-op if no
	// logo is loaded or fully zoomed out.
	void compose(u16 *dst) const;

	// Bytes of static RAM this component's pixel buffer occupies -- used by
	// ThemeTextures::drawTopDebug()'s memory-usage ranking.
	static constexpr size_t ramFootprintBytes() { return sizeof(_logoPix); }

private:
	void decodeLogoFile(const std::string &logoPath);

	static constexpr int LOAD_DELAY = 8;         // frames of stability before decoding (debounce)
	static constexpr float ZOOM_IN_STEP = 0.14f;  // speed of zoom-in (appearing)
	static constexpr float ZOOM_OUT_STEP = 0.22f; // speed of zoom-out (leaving, on item change) --
	                                               // faster than the decode debounce, so the old
	                                               // logo can't "become" the new one mid-animation
	static constexpr int SHADOW_DX = 2;
	static constexpr int SHADOW_DY = 2;
	static constexpr int SHADOW_ALPHA = 128; // 0..255; 128 = ~50% black

	u16 _logoPix[256 * 128];
	int _logoW = 0, _logoH = 0;
	bool _logoPresent = false;
	std::string _logoKey; // resolved rom base name (cache -- avoids reprocessing every redraw)

	std::string _pendingLogoPath; // "" = nothing pending
	int _pendingLogoDelay = 0;

	float _logoScale = 0.0f;     // currently rendered scale (0..1)
	float _logoScaleDest = 0.0f; // animation target (0 = hidden, 1 = full size)
};

GameLogoComponent &gameLogo();

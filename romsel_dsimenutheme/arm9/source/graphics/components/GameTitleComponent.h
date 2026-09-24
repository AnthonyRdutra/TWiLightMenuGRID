#pragma once

#include <cstddef>
#include <nds/ndstypes.h>
#include <string>
#include <string_view>

// The box anchored to the bottom of the DSi theme's top screen: shows the selected item's
// name/details (the "game title" box) until the selection has sat idle for a while, then swaps to
// a themed "press start" prompt box (the "start box") -- and back again as soon as the selection
// changes. The two are mutually exclusive faces of one animated slot (a vertical slide swaps
// between them), not independent elements, so they're one component.
class GameTitleComponent {
public:
	// Called once per selection change (from ThemeTextures::loadGameLogo(), after it determines
	// via GameLogoComponent::beginItemChange() that this really is a new item). Restarts the idle
	// countdown so the prompt box doesn't appear until the new selection has sat for a while.
	void onItemChanged();

	// Advances the idle countdown and the slide animation between title/start box. Returns true if
	// a redraw is needed.
	bool update();

	// Composes whichever box is currently showing (plus, for the title box, `text` centred inside
	// it) into `dst`, anchored to the bottom of the top screen.
	void compose(u16 *dst, std::u16string_view text);

	// Bytes of static RAM this component's pixel buffers occupy (titlebox + startbox, whichever is
	// showing or not) -- used by ThemeTextures::drawTopDebug()'s memory-usage ranking.
	static constexpr size_t ramFootprintBytes() { return sizeof(_titleboxPix) + sizeof(_startboxPix); }

private:
	void ensureLoaded();

	// MAX_H used to be the full screen height (192) "just in case" a theme's canvas was that tall.
	// Real assets are far smaller (the bundled theme's titlebox is 256x47) and only the opaque
	// bounding box within the canvas is ever drawn (see compose()) -- 64 is a generous margin above
	// that, not a hard requirement; a theme shipping a taller box would just fail to load (falls
	// back to no titlebox) rather than corrupt memory. Kept static (not heap): see TopScreenBoxBmp.h.
	static constexpr int MAX_W = 256, MAX_H = 64;
	static constexpr int MARGIN = 2;        // gap from the screen's bottom edge
	static constexpr int SLIDE_STEP = 8;    // px/frame of the swap slide
	static constexpr int SWAP_DELAY = 90;   // frames after selecting an item until title->start (~1.5s)

	bool _titleboxLoaded = false;
	u16 _titleboxPix[MAX_W * MAX_H];
	int _titleboxW = 0, _titleboxH = 0;
	int _tbBoxX = 0, _tbBoxY = 0, _tbBoxW = 0, _tbBoxH = 0; // opaque box within the asset

	bool _startboxLoaded = false, _startboxHas = false;
	u16 _startboxPix[MAX_W * MAX_H];
	int _startboxW = 0, _startboxH = 0;
	int _sbBoxX = 0, _sbBoxY = 0, _sbBoxW = 0, _sbBoxH = 0;

	int _boxKind = 0;      // which box is showing now: 0 = title(+text), 1 = start prompt
	int _boxSlide = 0;     // vertical offset (0 = in place; >0 = sliding away/in)
	int _boxSwapTimer = 0; // frames since the item was selected (counts up to SWAP_DELAY)
};

GameTitleComponent &gameTitle();

#include <gl2d.h>

#pragma once

// Banks 0..(iconActiveBankCount()-1) hold on-screen icons (index % iconActiveBankCount()); the
// bank at iconActiveBankCount() itself is the "moving app" icon. Each bank is a 32x256 4bpp
// texture (~4KB) in VRAM_A (128KB, shared with theme textures).
//
// The on-screen bank count used to be a fixed 8 columns x 3 rows = 24 -- it's now
// iconActiveBankCount(), sized from the DSi theme's grid geometry (rows * visible columns, see
// graphics/ThemeLayout.h) so a theme's layout.json can grow/shrink the grid. NDS_ICON_MAX_BANKS
// is only a compile-time ceiling for the static arrays below and for clamping
// iconActiveBankCount() to the VRAM budget -- it is NOT how many banks actually get VRAM
// allocated (that number is always iconActiveBankCount(), computed once ThemeLayout has loaded).
#define NDS_ICON_MAX_BANKS 25
#define NDS_ICON_BANK_CAPACITY (NDS_ICON_MAX_BANKS + 1)
#define TWL_ICON_FRAMES 8
#define TWL_TEX_HEIGHT 256

/**
 * Number of on-screen icon banks in use this run: gridRows() * (colsLeft+1+colsRight) from
 * ThemeLayout, clamped to NDS_ICON_MAX_BANKS so a theme can never request more banks than the
 * VRAM budget allows (clamping is logged, never a crash). Computed once and cached -- ThemeLayout
 * must already be loaded (main.cpp calls tl().loadConfig() before iconManagerInit()).
 */
int iconActiveBankCount();

// Checks if the icon is a bad index (out of the *active* pool -- see iconActiveBankCount()).
#define BAD_ICON_IDX(i) (i < 0 || i > iconActiveBankCount())

/**
 * Gets the current icon stored at the specified index.
 * If the index is out of bounds or the icon manager is not
 * initialized, returns null.
 */
const glImage* getIcon(int num);

/**
 * Allocates and initializes the VRAM locations for
 * icons. Must be called before the icon manager is used.
 */
void iconManagerInit();

/**
 * Loads an icon into one of 6 existing banks, overwritting 
 * the previous data.
 * num must be in the range [0, 5] else this function
 * does nothing.
 * 
 * If init is true, then the palettes will be copied into
 * texture memory before being bound with
 * glColorTableEXT. 
 * 
 * Otherwise, they will be replacing the existing palette
 * using glColorTableSubEXT at the same memory location.
 * 
 * texHeight must be a power of two, or bad things will happen.
 */
void glLoadIcon(int num, const u16 *palette, const u8 *tiles, int texHeight = 32);

/**
 * Loads an icon's palette into one of 6 existing banks,
 * overwritting the previous data.
 * num must be in the range [0, 5] else this function
 * does nothing.
 */
void glLoadPalette(int num, const u16 *palette);


/**
 * Clears an icon in the bank.
 */
void glClearIcon(int num);

/**
 * Reloads the palette of the icon in the 
 * numth slot, if it has been corrupted.
 */
void glReloadIconPalette(int num);

/**
 * Reloads the palette of all the icons in a slot, if
 * they have been corrupted.
 */
void reloadIconPalettes();
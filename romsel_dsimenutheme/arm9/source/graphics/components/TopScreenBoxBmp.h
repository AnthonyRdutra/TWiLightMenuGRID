#pragma once

#include <nds/ndstypes.h>
#include <string>

// Loads a 4/8bpp BMP from the active theme (magenta #FF00FF = transparent) into `pix` (caller-owned,
// static, at least maxW*maxH u16s -- NOT heap: see the note below), converting to BG-format u16
// pixels (0 = transparent) and locating the opaque bounding box within it (bx,by,bw,bh) -- themes
// ship these boxes inside a full-screen-sized canvas so the box itself can be positioned/sized
// freely without the .bmp dimensions dictating layout. Returns false if the file doesn't open or
// isn't a plausible box asset (wrong bpp, or bigger than `pix` can hold).
//
// Static, not heap: an earlier version of this function allocated the exact cropped size on the
// heap (new u16[bw*bh]) instead of asking the caller for a fixed buffer. That shrank each asset's
// footprint on paper, but moved it out of .bss and into malloc's arena -- on a heap that's already
// under real pressure (see FRONTEND.md §16), a few more allocations (this runs for 3 assets, 2
// allocations each) measurably slow down *every* other allocation in the app, not just these. A
// fixed, generously-sized static buffer costs more RAM on paper but zero allocator contention.
//
// Shared by every top-screen box asset (title box, start-prompt box, status bar background) --
// see GameTitleComponent and StatusBarComponent.
bool loadTopScreenBoxBmp(const std::string &path, u16 *pix, int &outW, int &outH,
                         int &bx, int &by, int &bw, int &bh, int maxW, int maxH);

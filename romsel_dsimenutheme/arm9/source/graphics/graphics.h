/*-----------------------------------------------------------------
 Copyright (C) 2015
	Matthew Scholefield

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

------------------------------------------------------------------*/
#pragma once
#define REFRESH_EVERY_VBLANKS 60

#include <nds.h>

bool screenFadedIn(void);
bool screenFadedOut(void);
void SetBrightness(u8 screen, s8 bright);

void drawCurrentDate();
void drawCurrentTime();

bool loadPhotoList();
void reloadPhoto();
void clearBoxArt();
void graphicsInit();
extern u16* colorTable;

// % of the ~16.7ms frame budget vBlankHandler's own body took to run, measured last frame (see
// graphics.cpp). Used by ThemeTextures::drawTopDebug()'s "render CPU cost" readout -- this is
// specifically the rendering work's share of the frame, not a whole-system CPU utilization
// (swiWaitForVBlank() is called from dozens of places outside vBlankHandler, so there's no single
// clean point to measure the latter without a much more invasive refactor).
int vblankWorkPercent();

template<typename T> inline const T abs(T const & x)
{
	return ( x < 0) ? -x : x;
}
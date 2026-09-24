#include "LaunchWipeComponent.h"

#include <algorithm>
#include <cmath>
#include <gl2d.h>
#include <nds.h>

#include "GridView.h"
#include "common/twlmenusettings.h" // ms()
#include "fileBrowse.h"             // CURPOS

// Defined in main.cpp; true for the brief window between pressing START on a game and
// nds-bootstrap actually taking over.
extern bool applaunchprep;

namespace {

constexpr int SCREEN_CX = SCREEN_WIDTH / 2;  // 128
constexpr int SCREEN_CY = SCREEN_HEIGHT / 2; // 96

// Stand-in for the physical bezel between the two screens, in the same pixel units, used only to
// place the top screen "above" the bottom one in the imaginary combined space the wipe expands
// through (see LaunchWipeComponent.h). Not meant to be physically exact -- just big enough that
// the wipe visibly starts on the bottom screen before reaching the top one.
constexpr int SCREEN_GAP = 32;

constexpr int TOTAL_FRAMES = 40; // ~0.66s at 60fps, regardless of how big the wipe ends up being

// Unit circle, 20 points, fixed-point (x4096) -- used to fan out gl2d triangles for the bottom
// screen without needing runtime trig (no FPU on the ARM9) or a libnds trig-LUT dependency.
constexpr int CIRCLE_SEGMENTS = 20;
constexpr int UNIT_CIRCLE[CIRCLE_SEGMENTS][2] = {
	{4096, 0}, {3896, 1266}, {3314, 2408}, {2408, 3314}, {1266, 3896}, {0, 4096},
	{-1266, 3896}, {-2408, 3314}, {-3314, 2408}, {-3896, 1266}, {-4096, 0}, {-3896, -1266},
	{-3314, -2408}, {-2408, -3314}, {-1266, -3896}, {0, -4096}, {1266, -3896}, {2408, -3314},
	{3314, -2408}, {3896, -1266},
};

} // namespace

void LaunchWipeComponent::captureOrigin() {
	// Spawn at the icon that was actually launched (the grid's current selection), on the bottom
	// screen -- not the screen centre. Other themes don't have a grid to ask, so they keep the
	// old screen-centre spawn.
	if (ms().theme == TWLSettings::EThemeDSi) {
		int rows = gridView().rows();
		_originX = gridView().columnCenterX(CURPOS / rows, ms().secondaryDevice);
		_originY = gridView().rowCenterY(CURPOS % rows);
	} else {
		_originX = SCREEN_CX;
		_originY = SCREEN_CY;
	}

	// Combined virtual space: top screen occupies y in [0, SCREEN_HEIGHT); the bottom screen sits
	// below it, past the bezel gap, at y in [SCREEN_HEIGHT+SCREEN_GAP, 2*SCREEN_HEIGHT+SCREEN_GAP).
	// The wipe needs to grow far enough to reach the farthest corner of that whole space from the
	// origin -- computed fresh per launch since the origin (and so the farthest corner) varies
	// with where in the grid the launched icon happens to be.
	const int originVY = _originY + SCREEN_HEIGHT + SCREEN_GAP;
	const int combinedHeight = 2 * SCREEN_HEIGHT + SCREEN_GAP;
	const int corners[4][2] = {
		{0, 0}, {SCREEN_WIDTH, 0}, {0, combinedHeight}, {SCREEN_WIDTH, combinedHeight},
	};
	int maxDistSq = 0;
	for (const auto &corner : corners) {
		int dx = corner[0] - _originX;
		int dy = corner[1] - originVY;
		maxDistSq = std::max(maxDistSq, dx * dx + dy * dy);
	}
	_maxRadius = (int)sqrtf((float)maxDistSq) + 1;
	_growthPerFrame = std::max(_maxRadius / TOTAL_FRAMES, 1);
}

bool LaunchWipeComponent::update() {
	if (!applaunchprep) {
		if (_radius != 0 || _maxRadius != 0) {
			_radius = 0;
			_maxRadius = 0; // next launch re-captures the origin from scratch
			return true;
		}
		return false;
	}
	if (_maxRadius == 0)
		captureOrigin();
	if (_radius < _maxRadius) {
		_radius = std::min(_radius + _growthPerFrame, _maxRadius);
		return true;
	}
	return false;
}

void LaunchWipeComponent::draw() {
	if (_radius <= 0)
		return;

	for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
		int j = (i + 1) % CIRCLE_SEGMENTS;
		int x1 = _originX + (UNIT_CIRCLE[i][0] * _radius) / 4096;
		int y1 = _originY + (UNIT_CIRCLE[i][1] * _radius) / 4096;
		int x2 = _originX + (UNIT_CIRCLE[j][0] * _radius) / 4096;
		int y2 = _originY + (UNIT_CIRCLE[j][1] * _radius) / 4096;
		glTriangleFilled(_originX, _originY, x1, y1, x2, y2, RGB15(0, 0, 0));
	}
}

void LaunchWipeComponent::drawTopScreen() const {
	if (_radius <= 0)
		return;

	// The origin lives on the bottom screen, i.e. below the top screen in the combined space (see
	// captureOrigin()) -- so dy here is always negative-ish, and only rows within `_radius` of the
	// origin (starting from the bottom edge, closest to the gap) get filled. That's what makes the
	// wipe visibly reach up into the top screen from below instead of appearing there uniformly.
	const int originVY = _originY + SCREEN_HEIGHT + SCREEN_GAP;
	const u16 black = RGB15(0, 0, 0) | BIT(15);
	for (int y = 0; y < SCREEN_HEIGHT; y++) {
		int dy = y - originVY;
		long distSqAvail = (long)_radius * _radius - (long)dy * dy;
		if (distSqAvail < 0)
			continue; // this row hasn't been reached by the wipe yet
		int dx = (int)sqrtf((float)distSqAvail);
		int xStart = std::max(_originX - dx, 0);
		int xEnd = std::min(_originX + dx, SCREEN_WIDTH - 1);
		if (xStart > xEnd)
			continue;
		u16 *row = &BG_GFX_SUB[y * SCREEN_WIDTH];
		for (int x = xStart; x <= xEnd; x++) {
			row[x] = black;
		}
	}
}

LaunchWipeComponent &launchWipe() {
	static LaunchWipeComponent instance;
	return instance;
}

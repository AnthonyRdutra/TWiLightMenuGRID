#pragma once

// Minimal base for the DSi theme's grid render components.
//
// This is deliberately NOT a virtual-DOM/diff engine — on this hardware the "scene" is small
// (one icon grid, redrawn immediate-mode via gl2d) and a reconciler would just add cycles for no
// benefit. The React-like part that *is* worth taking is composition: small, self-contained,
// stateful units instead of one long function, each with a clear update/draw split so the
// existing per-frame dirty-flag optimization (vBlankHandler's `updateFrame` bool) can be computed
// from each component's own `update()` instead of one shared blob of shadow "...Prev" variables.
//
// update(): mutate this component's own state from current game state (CURPOS, touch input,
//           etc). Returns true if something changed that requires a redraw this frame.
// draw():   issue gl2d calls. Only called by the renderer when a redraw is already due — a
//           component never decides on its own whether *it* gets drawn.
//
// No dynamic allocation: components are owned as function-local `static` instances (see each
// component's accessor, e.g. gridView()/gridCursor()), matching this codebase's existing
// avoidance of heap churn in hot paths.
class Component {
public:
	virtual ~Component() = default;
	virtual bool update() = 0;
	virtual void draw() = 0;
};

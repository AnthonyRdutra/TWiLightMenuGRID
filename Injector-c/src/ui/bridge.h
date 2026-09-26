/* Wires the wizard's actions (src/ui/wizard.h) up to the HTML/JS frontend (assets/ui/) over a
 * webview instance: JS calls the global functions bound here (window.getState(), etc.), each of
 * which runs the matching wizard_* action and resolves with the wizard's full JSON state so the
 * frontend can just re-render from scratch every time. */
#ifndef INJECTOR_UI_BRIDGE_H
#define INJECTOR_UI_BRIDGE_H

#include "webview.h"
#include "wizard.h"

/* Registers every window.* binding the frontend calls. `w` and `wiz` must outlive the webview. */
void bridge_install(webview_t w, WizardState *wiz);

#endif

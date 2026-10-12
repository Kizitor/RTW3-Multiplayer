#pragma once
#include "common.h"

namespace ui {

void Init(HINSTANCE inst);
void Tick();  // ~4x per second on the main thread
void ShowMpWindow();
void ToggleMpWindow();
void Refresh();
void Notice(const std::string& text);
// Lets the dialog manager handle Tab/Enter for our window; returns true if consumed.
bool PreTranslate(MSG* msg);
// Test hook (bridge `claimui`): "state", or "toggle <i>" ticks/unticks row i like a click on its box.
std::string DebugClaimWindow(const std::string& action, int index);

}  // namespace ui

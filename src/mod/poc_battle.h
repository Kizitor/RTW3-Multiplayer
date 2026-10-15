#pragma once
#include "common.h"

// Research proof of concept for tactical battles (investigation 2026-10-14): a seeded battle RNG, a per-minute
// battle state hash, minute gating and test entry points. Inactive unless a `poc*` bridge command is used.
namespace poc {
bool IsAsync(const std::string& cmd);                 // commands that may open the game's modal dialogs
std::string Command(const std::vector<std::string>& args);  // runs on the game's main thread
}

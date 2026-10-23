#pragma once
#include "common.h"

// The player's economy rules for the joined players' nations on the host (relations v2, B5: money; economy.cpp).
namespace economy {

// After game::InstallHooks (MinHook is set up there): finds the game's money and budget rules for the nations it plays
// and hooks them for the joined nations. False if none could be installed.
bool Install();

// Bridge `econ <K>` (host): nation K's Funds, BudgetModifier and monthly balance (the player's prices and the computer's)
// and the record of its last processed month (what was withheld or applied).
std::string BridgeCommand(const std::vector<std::string>& args);

// Host: each joined nation's own state of the player's rules (ParliamentDelay, army offensive months, the smaller share's
// notice), kept in the session file with the month ("K:pd:ao:notice,...").
std::string EncodeState();
void DecodeState(const std::string& s);
void ClearState();  // also the messages not yet sent
// Host: the messages of the rules for nation K's player since the last call (cleared), and K's army offensive months.
std::vector<std::wstring> TakeNews(int K);
int ArmyOffensiveOf(int K);
// Host: K's last war ended (its war counter followed to 0): its army offensive months end, as the game's ArmyOffensive at
// DoPeace (the reset in WarStartsWithNation is unreachable: it runs only while the war counter is 0 or less, after it
// was set to 1).
void OnWarChanged(int K);
// A joined player's PC, after the month is loaded: its nation's army offensive months as the game's ArmyOffensive when
// the loaded one is 0 (the budget figures show K's x 0.9). What it did, for the log.
std::string ClientArmyOffensive(int months);
// TEST ONLY (bridge `econrand <v>`, host): the rules' Random(n) draws min(v, n - 1); -1 (the default) the game's Random.
std::string TestRandom(const std::vector<std::string>& args);

}  // namespace economy

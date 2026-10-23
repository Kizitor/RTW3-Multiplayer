#pragma once
#include "common.h"
#include "saveio.h"

// The parts of the game that it keeps for its player (nation 0) only, given to the joined players' nations:
// - intelligence: each player's own spending, effort and report list, its own monthly spying check, its own cost;
// - the Turn's trade protection and foreign station questions, and the month's consequences of missing ones.
// The game keeps a player's intelligence on the TARGET nations (`IntelligenceSpending`, `IntelEffort`: what nation 0
// spends on / has achieved against that nation) and in one global report list, so on a joined player's PC every
// value and report was the host's. The host keeps each joined nation's own values in `rtw3mp_nations.ini` in its slot,
// puts them into that player's month package, takes them back from the player's turn and charges the cost in the
// month. See docs/ARCHITECTURE.md ("Per-player intelligence, trade protection and foreign stations").
namespace nplay {

bool Resolve();       // bindings; logs what is missing (each feature turns itself off)
void InstallHooks();  // creates its MinHook hooks; game::InstallHooks enables them with the others

// True while the game temporarily sees a joined player's nation K as "the player" (nation list and relations swapped).
bool ViewActive();

// ---- host
void HostAttach(const std::wstring& slotDir, const std::string& campaignKey);  // loads the records (slot/campaign)
void HostDetach();
void HostTransformState(Bundle& b, int K);          // K's month package: K's own intelligence instead of the host's
void HostTakeSubmission(const Bundle& sub, int K);  // K's intelligence from its turn
void HostTakeIntelEvent(const KV& kv, int K, std::string& applied);  // K's spying results (sent right away)
void SetNewsSink(std::function<void(int nation, const std::string& line)> fn);  // the month's news for a player
std::string HostDebug();

// ---- client
void ClientSetMonth(int K, bool hostIntel, int clientSlot);  // after a month package was loaded
void ClientReset();
bool ClientIntelActive();  // this PC plays nation K of a host that keeps per-player intelligence
// The monthly spying check as the game does it for its player (CheckIntelligence with K seen as the player); the game's
// own dialogs show on this PC. Fills the MSG_EVENT keys (tension, delta, intel=1, ireports, ieffort). Returns true if
// anything is to be sent.
bool ClientRunIntel(int K, KV& out, std::vector<std::string>& newReports, std::string& summary);
bool ClientTurnWarnings(int K);  // the Turn's questions; false = the player chose to go back
void ClientAddSubmission(Bundle& b, const std::wstring& slotDir, int slot, int K);

// ---- tests (bridge)
std::string IntelInfo();
std::string TradeCheck(int K);
std::string ShipStatus(int nation, int ship, int status);  // status < 0: read only
std::string MonthCheckRun(int K, int count);  // host: the month's trade protection/foreign stations check for K, count times
std::string IntelButton(int tag);  // the politics panel's +/- button with that Tag (1..16), through the game's handler

}  // namespace nplay

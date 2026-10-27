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
// Campaign divisions (0.6.1; the strategic screen's Fleet organization). The game keeps one division list, its
// player's, saved in the slot's .off and loaded against the player's nation. The host keeps each joined nation's own
// list (from the nation's turns, "divisions.ini") in its records and puts it into the nation's packages instead of
// the host's own divisions.
// K's list from its turn (validated); a turn without one keeps it. fresh (0.7.0, a legacy turn: all of K's ships are
// new): a turn without a valid list leaves none, as the stored one would name the replaced fleet's ships.
void HostTakeDivisions(const Bundle& sub, int K, bool fresh = false);
// The package's .off gets K's stored divisions pruned to K's ships of the package's .bcs (own = false: an empty list,
// for a player whose mod would look K's ships up among the host's). `what` names the package in the log. Returns the
// divisions written, -1 if the package has no .off.
int HostPutDivisions(Bundle& b, int K, bool own, const char* what);
std::string HostDivisionsInfo(int K);  // bridge `divstore`: "ok n=<n> list=<CDivId>:<name>:<ships>:<id.id...>|..."
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

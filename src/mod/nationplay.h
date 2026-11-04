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
// Officers (0.8.0; officers.h). The host keeps each joined nation's corps (its [Officers] lines, the game's keys) and
// its area commands ("MapArea<i>Commander=<id>") in its records ([Nation<K>Officers], [Nation<K>AreaCmd]).
bool HostHasOfficers(int K);
// The officers.ini of K's turn (validated strictly) into K's record. False: none sent (an older mod) or rejected (the
// stored record stays; `why`).
bool HostTakeOfficers(const Bundle& sub, int K, std::string& why);
// The [Officers] of K's battle result (officers.ini; the area commands stay). False: none or rejected. sent (0.8.0, with
// simultaneous battles): the [Officers] lines its package carried (HostPutOfficers' `put`): the result is merged per
// officer id into the record (saveio::MergeOfficers: the host's month since the send stays), else it replaces it.
bool HostTakeBattleOfficers(const Bundle& res, int K, const std::vector<std::string>* sent = nullptr);
// 0.8.1 (a live battle between two players with both sides' commanders): the second player's nation K's officers after
// the battle (the result's lsoff<K>.ini, from the first player's PC) merged into K's record against `sent` (what the
// package carried, `of2`), only what a battle changes (saveio::BattleOnlyOfficers). False: none or rejected.
bool HostTakeBattleOfficers2(const Bundle& res, int K, const std::vector<std::string>& sent);
// The package's .off [Officers] := K's record (own = false or no record: OfficerNo=0), its MapData's area commanders :=
// K's (-1 elsewhere). Returns true if K has a record (false: the player's PC makes the corps, `offgen`). put: the
// [Officers] lines the package got (own only; else left empty).
bool HostPutOfficers(Bundle& b, int K, bool own, const char* what, std::vector<std::string>* put = nullptr);
// K's record's [Officers] lines (empty: none); tests (`battles simulkeep`).
std::vector<std::string> HostRecordOfficers(int K);
// After a load of the host's slot: every joined nation's record into its own list (officers.h LoadIntoNation).
void HostOfficersToMemory(const char* why);
// After a save of the host's slot (not during the battle phase) and after the month: the lists the last load filled
// back into the records.
void HostOfficersFromMemory(const char* why);
// The host's save after K's turn was merged: K's ships whose commander is not in K's record lose it (-1); `keep`
// (a turn without officers.ini: an older mod) puts the host's previous ids back first. Returns the ids reset.
int HostFixShipCommanders(const std::wstring& bcsPath, int K, const std::map<std::string, int>* keep);
std::string HostOfficersInfo(int K);  // bridge `ofstore`: "ok n=<n> active=<a> areas=<m> ids=<min>-<max> first=..."
int FleetSizeValue();  // [General] FleetSize of the open campaign (-1 unknown)
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

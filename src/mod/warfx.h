#pragma once
#include "common.h"

// The player's war effects for the joined players' nations under the player's rules (relations v2, B5; warfx.cpp):
// blockades (their victory points, the nations' Blockaded flags, the effects the game gives the player's nation and its
// enemies) for each such nation K at war, on the host during the month.
namespace warfx {

// After game::InstallHooks (MinHook is set up there): finds the routines and hooks them. False if unavailable.
bool Install();

// Host: the messages of the war effects for nation K's player since the last call (cleared).
std::vector<std::wstring> TakeNews(int K);
// Host: the nations whose Blockaded flag is set now ("1,3,"), for the players' screens (header `kblk`).
std::string BlockadedList();
// A joined player's PC, after the month is loaded: the host's Blockaded flags (header `kblk`). What changed, for the log.
std::string ClientApplyBlockade(const std::string& list);
// Host: a new session (the messages not yet sent, the last month's record).
void ClearState();

// Bridge `warfx [K]` (host): the frames of the last blockade check and K's record of the last month.
std::string BridgeCommand(const std::vector<std::string>& args);
// TEST ONLY (bridge `warfxblk <n> <-1|0|1>`): the frames' blockade test of nation n answers 1 / 0; -1 the game's test.
std::string TestBlockade(const std::vector<std::string>& args);
// TEST ONLY (bridge `warfxrand <v>`): the war effects' own Random(n) draws (the blockade points, the unrest) min(v, n - 1);
// -1 the game's Random.
std::string TestRandom(const std::vector<std::string>& args);
// TEST ONLY (bridge `shipbuilder <n> [<ship> <builder>]`, host or client): nation n's ships under construction
// (`index:CShipId:BuildingNationIdx`), after setting one ship's BuildingNationIdx (a ship built in another nation's yard).
std::string TestBuilder(const std::vector<std::string>& args);
// TEST ONLY (bridge `warfxsea <R> <n>`, host): in the next sea war frame with nation R (a joined nation K, or a computer
// enemy of K), up to n of R's operational ships in active service raid in the defender's build area (R = K: against its
// first enemy; else against K) for that frame; put back afterwards. n <= 0: off.
std::string TestSea(const std::vector<std::string>& args);
// 0.7.2, bridge `aibuilds [clear | patrol <J> [<K> [set]] | keep <0|1>]` (any PC): the computer's ships laid down
// (LayDownAIShip) since the last `aibuilds clear`, oldest first (the last 300): "ok n=<count> keep=<0|1> | <yyyy-mm>:
// <nation>:<type>:<cause>[:K<k>]:<on trade protection>/<required> ..." (cause: monthly = AIMonthlyBuild, escorts =
// CheckSubmarineActivity, initial, other; K<k>: in joined nation k's view). `patrol`: J's ships on trade protection and
// its patrol requirement on the host's frame and in K's view (TEST ONLY `set`: put some on trade protection up to the
// latter). `keep 0` (TEST ONLY): 0.7.1, the relations mask doesn't put back the trade protection and raiders of a
// computer nation whose wars are all with nations people play, and the sea war frames run no ASW effort for it.
std::string AIBuildsCommand(const std::vector<std::string>& args);
// 0.7.3, bridge `aiwar` (host): the computer's war against joined nations this month: "ok on=<0|1>
// players=<J:K,...|-> | nation=J K=k routines=<the routines run in K's view> natwar=n colonies=c moves=m
// [before=a/r/m/t/r/b/f after=...] ... | patrols <J:K=k war=w patrols=a->b/r raiders=x->y calls=c faults=f> ... |
// cp=<0|1>" (statuses 0-6 of J's ships in play around its PutAIShipsInStatus; CheckPatrols' part in K's view). TEST
// ONLY `aiwar 0|1` (0: 0.7.2) and `aiwar status <J> <from> <to> [n]` (J's operational capital ships, types 0-2, in play
// from one status to another, those in a home area first; their CShipIds and how many are outside a home area).
std::string AIWarCommand(const std::vector<std::string>& args);

}  // namespace warfx

#pragma once
#include "common.h"

// Each joined player's officers (0.8.0). The game keeps one officer corps, its player's: PlayerNation's OfficerList,
// saved in the slot's .off ([Officers]) and connected to the player's ships and the map areas at the load
// (TBuilderNation.ConnectOfficersToShips). The mod gives each joined nation K its own corps in K's own OfficerList:
// - on the host, K's corps (kept in the joined nations' records, nationplay.cpp) is loaded into K's list after every
//   load of the host's slot and connected to K's ships, so the month treats K's commanders as the game treats any
//   nation's (sinkings, maintenance, crew training); it is read back from memory after the host's saves;
// - on K's PC, the month's load trades the lists of nation 0 and K (the month's .off holds K's corps), so the game's
//   own screens, actions and saves work on K's corps; the monthly officer routine, the new campaign's pool and the
//   Turn's question and auto-assignment run there as the game runs them for its player.
// See docs/ARCHITECTURE.md ("Each joined player's officers (0.8.0)").
namespace ofc {

bool Resolve();       // bindings; logs what is missing (the feature turns itself off)
void InstallHooks();  // creates its MinHook hooks; game::InstallHooks enables them with the others
bool Available();     // the officer lists can be loaded, read and traded (hello `off`)
bool MonthAvailable();  // ... and the month's officer routine, the pool and the Turn's question can run here

// ---- every load (game.cpp)
void OnLoadStart();  // LoadBuildCampaign is about to load: no nation's corps of the mod is in memory
// game::LoadCampaign(slot, playerIdx): while a joined player's own month loads with its host's officers (`offs`), the
// lists of nation 0 and K trade places in ConnectOfficersToShips.
struct LoadScope {
    LoadScope(int slot, int playerIdx);
    ~LoadScope();
};

// ---- host
// The nation's corps from the record's [Officers] lines into its (empty) list, then its ships connected (the game's
// ConnectOfficersToShips; the map areas stay the host's). Returns the officers loaded, -1 if not possible.
int LoadIntoNation(int K, const std::vector<std::string>& lines, int* commanded = nullptr);
bool LoadedThisLoad(int K);  // LoadIntoNation ran for K since the last load
// The nation's list as the game's writer saves it ([Officers] lines without the header).
bool ReadNation(int K, std::vector<std::string>& lines);
// The month (host): the game's AdjustCrewQuality for a joined nation's ship shows no picture message on the host; the
// texts go to the nation's player (`news`). The ship sheet shows no commander for a ship that is not PlayerNation's.
void SetHostGuards(std::function<bool(int nation)> isJoined, std::function<void(int nation, const std::string&)> news);
void BeforeSave();  // a save while a ship sheet hides a commander: put back first

// ---- joined player's PC
void ClientSetMonth(int K, bool hostOfficers);  // before the month's load: `offs` of the month's header
void ClientReset();
bool ClientActive();  // this PC plays nation K with its own officers (a host that keeps them)
// The new campaign's officers (the game's recipe for its player): the pool, the area commands, the ships' commanders.
bool GenerateCorps(int K, std::string& summary);
void ClearFreePromotion();  // the one free promotion a month (DoTurn2 clears it in the game)
// The Turn's question about ships and divisions without a commander (false: the player chose to go back), then the
// three auto-assignments of DoTurn2.
bool AskCommanders();
void AutoAssign(std::string& summary);
// How many officers / commanded ships / commanded areas PlayerNation has now.
void Counts(int& officers, int& active, int& ships, int& areas);

// ---- tests (bridge)
std::string Info();                                 // `officers`
std::string FreePromotionState();                   // `ofpromo`
std::string TestKill(int nation, int ship, bool pow);  // TEST ONLY `ofkill`: the game's KillCommander on that ship
std::string TestAdjustCrew(int nation, int ship);   // TEST ONLY `ofcrew`: the game's AdjustCrewQuality on that ship
// TEST ONLY `ofset <nation> <officer id|-1> <field> <value>`: a TOfficer field (Special1, Special2, Status, AbilityKnown,
// Ability, Rank, YearsInRank, BattleStars) of an officer in the nation's list (-1: the first)
std::string TestSet(int nation, int id, const std::string& field, int value);
std::string ShipCommander(int nation, int ship);    // `ofship <nation> <ship|-1>`: a ship's commander
// `ofget <nation> <id|-1> [any|ship|pool]`: an officer of the nation's list (-1: the first active one below admiral
// commanding a ship / in the pool)
std::string OfficerText(int nation, int id, const std::string& pick);

}  // namespace ofc

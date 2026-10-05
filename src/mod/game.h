#pragma once
#include "common.h"
#include "delphi.h"

// Rule the Waves 3 bindings: resolved by RTTI name at startup, then thin wrappers around game routines.
namespace game {

bool Resolve();
bool InstallHooks();

void* BuildCamp();     // TfrmBuildCamp instance (strategic screen)
void* StartForm();     // TfrmSelectNation2 instance (title screen)
void* Application();   // TApplication instance
HWND FormHwnd(void* form);
bool FormVisible(void* form);

int NationCount();
void* Nation(int idx);
int NationIndex(void* nation);
std::wstring NationName(int idx);
int NationFunds(int idx);
void* PlayerNation();
int PlayerIndex();

int StartBuildPhase();  // 0/1 = initial fleet phases, 2 = normal play
int CampaignStartYear();
int CurrentSlot();
void SetCurrentSlot(int slot);
std::wstring SaveDir(int slot);  // "...\Save\Game<slot>\"

bool SaveGame();                             // same as the Save button (normal save into the current slot)
bool LoadCampaign(int slot, int playerIdx);  // full load like the title screen, then optionally switch nation
void SetPlayerNation(int idx);               // switch the human-controlled nation and refresh the UI
void RefreshUI();
bool IsBusy();          // modal dialog open, turn running, or form disabled by the game
bool InTurnProcessing();
bool BattleActive();    // tactical battle window is open
void RunOriginalTurn();  // the real "Turn" button handler (host)
void ClickTurnButton();  // posts a click to the Turn button (as if pressed)
void* TurnButton();
void SetTurnButtonCaption(const std::wstring& caption);

// Ships of a nation (TCampaignShip objects).
int ShipCount(void* nation);
void* Ship(void* nation, int i);

// Ship orders the AI strategic-move routine may overwrite (restored for human nations).
struct ShipSnap {
    void* ship;
    void* ordered;
    void* location;
    uint8_t status;
};
std::vector<ShipSnap> SnapshotShips(int nationIdx);
int RestoreShips(const std::vector<ShipSnap>& snaps);

// A fingerprint of the game build derived from RTTI addresses (host and clients must match).
std::string BuildFingerprint();

// Diplomacy between nations (campaign indices; 0 = the nation the host plays). The game keeps one tension
// value per pair (the relation bar); above 40 means war. Alliances are month countdowns: `Allied` for an
// alliance with nation 0, `AIAlliance[j]` between two other nations.
bool DiplomacyAvailable();
int Tension(int a, int b);
void SetTensionRaw(int a, int b, int value);  // both directions, no side effects
bool AtWar(int a, int b);
bool AtWarWithAnyAI(int idx);                 // any war of `idx` with nations 1..8
int AllianceMonths(int a, int b);
int WarCounter();                             // months the host's nation has been at war (<= 0: peace)
int CountAtWarWithHost();
void StartWar(int a, int b);                  // what the game does when a war breaks out
void MakePeace(int a, int b);                 // ends only the war between a and b (compromise peace)
void SetAlliance(int a, int b, int months);   // 0 ends it

}  // namespace game

// Callbacks from hooks (implemented by the session layer). All run on the game's main thread.
namespace hookcb {
bool OnTurnClick(void* self, void* sender);  // true => swallow the click
bool OnResignClick(void* self);              // true => swallow
void OnAfterEndOfTurn(void* self);
bool IsHumanRemoteNation(int idx);  // host: AI must not act for this nation
void OnBeforeAIMoves(void* self);
void OnAfterAIMoves(void* self);
bool OnBeforeAIPeace(int nationIdx, bool badLoss);  // true => skip the game's AI peace for this nation
void OnAfterAIPeace();
}  // namespace hookcb

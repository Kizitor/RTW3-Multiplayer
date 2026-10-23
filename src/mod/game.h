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
// Full load like the title screen, then optionally switch nation. modReload: the mod's own reload (its load keeps the
// saved victory points even outside a multiplayer session); false = as the player's own load (the test bridge).
bool LoadCampaign(int slot, int playerIdx, bool modReload = true);
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

// Ship designs of a nation (TDesignShip). ReadyForBuild = months left in the design study (0 = can be built,
// MAXINT = not a study). The game's monthly AdvanceDesignStudies only counts down the host's own nation.
int DesignCount(int nationIdx);
std::wstring DesignName(int nationIdx, int i);
int DesignReadyForBuild(int nationIdx, int i);  // -1 if there is no such design
void SetDesignReadyForBuild(int nationIdx, int i, int months);

// Entries in the campaign's intel report list (-1 if unavailable); `unique` = distinct non-empty entries.
int IntelReportCount(int* unique = nullptr);
// Air units in the game's global list (all nations); -1 if the list was not found. A load drops the units from before it.
int AirUnitCount();

// Doctrine (Doctrine button): training priorities and the missile storage policy change only after a number of
// months (Pending* values). The game's monthly ReduceTimeLimits applies them for the host's nation only.
struct DoctrineState {
    bool valid = false;
    uint8_t training[4] = {}, pending[4] = {};
    int pendingMonths = 0;
    int missileStorage = 0, pendingMissileStorage = 0, missileMonths = 0;
};
struct DoctrineChange {
    bool trainingApplied = false, missilesApplied = false;
};
bool DoctrineAvailable();
DoctrineState GetDoctrine(int nationIdx);
void SetPendingTraining(int nationIdx, const uint8_t pending[4], int months);  // as the dialog's Apply (tests)
void SetPendingMissileStorage(int nationIdx, int policy, int months);          // tests
DoctrineChange AdvanceDoctrine(int nationIdx);  // one month of the game's countdown

// Random events for a joined player's nation K, rolled on the player's PC with K seen as "the player"
// (everything is put back afterwards). `used` = the player's own event cooldowns (in/out). The relation and
// nation-field changes are returned for the host to apply.
struct ClientEventResult {
    bool shown = false;
    int eventIdx = -1;
    std::wstring caption;
    std::string error;
    int tension[9] = {};  // change of the relation between K and nation i
    int alliance[9];      // new alliance months between K and nation i, -1 = unchanged
    std::map<std::string, int> fieldDelta;  // changes of K's own fields (Prestige, Funds, ...)
    std::map<std::string, int> fieldSet;    // new values (BuildConstraint, BuildConstraintTime, BuildConstraintType)
};
bool EventsAvailable();
bool EventActive();  // an event dialog for a joined player is open (nation list reordered: don't load/save)
bool GetEventInfo(int idx, int& cond, std::wstring& caption, bool& hostOnly);  // false if the slot is empty
ClientEventResult RunClientEvent(int playerNation, int forcedIdx, int used[100]);  // forcedIdx -1 = random
bool AdjustNationField(int nationIdx, const std::string& field, int value, bool isDelta);
int NationIntField(int nationIdx, const std::string& field);  // INT_MIN if unknown

// Peace treaties between players, with the game's own rules and routines (the winner's claimable possessions,
// ships handed over as reparations, prestige, the treaty report dialog).
struct PossessionInfo {
    std::wstring name, area;
    int value = 0;
    bool oil = false;
    int owner = -1;
};
struct TreatyResult {
    std::vector<std::wstring> report;  // lines of the peace treaty
    std::vector<std::wstring> ceded;
    int ships = 0, resourcesCut = 0;
    bool automatic = false;  // possessions chosen by the game (no claims)
};
bool TreatiesAvailable();
int WarScale();   // the game's scale value G in its war-progress thresholds (100*G + 300 slightly ahead, + 500 winning)
int NationVP(int idx);
int EnemyVP();    // the victory points of the host nation's enemies
// Victory points of the joined players' nations. The game credits whatever the host's enemies achieve in the host's
// wars to EnemyVP only; the host gives a human enemy its share in its own VP (the "VP" of that player's screen).
void SetNationVP(int idx, int vp);
bool AtWarWithAnyone(int idx);
void ShowEnemyVP(int vp);  // this PC's "Enemy VP" (a joined player's view: the points of its own enemies)
// The game's "Copy game" from the open slot to another one (shows the game's own confirmations; tests).
void CopyGame(int toSlot);
// Tactical battles of joined players (see game.cpp). A battle of a joined player's nation against an AI nation is
// deferred by hookcb::OnAIBattle; the host resolves it later with the game's own routine (ResolveAIBattle) or the
// player fights it in a battle slot (StartPlayerBattle: the game's CheckAttackLocation -> battle preview -> battle).
bool BattlesAvailable();
void SetBattleSlotMode(bool on);  // a battle slot: the battle screen's close doesn't run the rest of a month
bool BattleSlotMode();
bool BattleAftermathSeen();       // the battle ended with the game's aftermath (VP, prestige) in battle slot mode
bool ResolveAIBattle(int a, int b, int countA, int countB, const std::wstring& area);
bool StartPlayerBattle(int attacker, int defender, const std::wstring& area);
std::vector<std::wstring> AreaNames();
bool ModalOpen();                  // a modal game dialog is open
void SetStrategicEnabled(bool on); // the host's strategic screen accepts input (off while battles are fought)
int DoBattleCalls();               // how often the game's battle setup (DoBattle) was entered
// Tests: a nation's research budget (% of the total) and its research priorities; the Research button's handler.
std::string DebugResearch(int idx);
void OpenResearch();
void DebugDoResearch(int idx, int count);  // tests: the game's monthly research of a nation, `count` times (logged)
// Test hook: the game's end-of-battle routine against `opp`, as after a tactical battle (VP before the log scale).
std::string DebugBattleAftermath(int opp, int vp0, int vp1, const std::string& level);
std::vector<PossessionInfo> Possessions(int ownerIdx);
std::vector<PossessionInfo> ClaimablePossessions(int winnerIdx, int loserIdx);
// level 1 = minor win (land worth 4, one ship hand-over), 2 = major win (land worth 8, three hand-overs). Call
// while the two nations are still at war.
TreatyResult ApplyTreaty(int winnerIdx, int loserIdx, int level, const std::vector<std::wstring>& claims, unsigned seed);
void ShowPeaceTreaty(const std::vector<std::wstring>& lines);  // the game's modal treaty dialog

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
void MakePeace(int a, int b, int hostOutcome = 3);  // ends only the war between a and b; hostOutcome 1 won, 2 lost, 3 compromise
void SetAlliance(int a, int b, int months);   // 0 ends it

// The relations view: on a PC that plays nation K > 0 as the player (a joined player's PC) the game's tension panel,
// war and alliance marks, flags, map strength bars and war state show nation K's relations (game.cpp, KViewBegin).
int ShownNationIndex();     // K when PlayerNation is BuilderNations[K] with K in 1..8, else 0
std::string DebugRelView();  // tests: what the strategic screen's tension panel and war state show now

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
void OnAfterDesignStudies(void* self);  // the game advanced the host nation's design studies
void OnAfterReduceTimeLimits(void* self);  // the game counted down the host nation's pending changes this month
void OnBattleVP(int a, int b, int vpA, int vpB);  // victory points a battle between nations a and b gave each side
void OnHostPeace(bool after);  // the game's end of the host's last war (DoPeace), before and after
void OnAfterCopyGame(int fromSlot, int toSlot);  // the game's "Copy game" finished (or was cancelled)
// The game resolves a battle between two nations it runs (DoBattleBetweenAINations); true = defer it (host).
bool OnAIBattle(int a, int b, int countA, int countB, const std::wstring& area);
// The game's DoBattle returned (opp = the opponent's nation index); the battle screen is open if it was fought.
void OnDoBattleDone(int opp, bool result);
// The game's LoadBuildCampaign returned (a battle in progress in that save is resumed inside it: the screen is open).
void OnAfterCampaignLoad();
void OnBattleSlotSkippedTurn();  // a battle slot's close skipped DoStrategicTurn
void OnStrategicTurnRuns();      // DoStrategicTurn runs (outside a battle slot)
void OnCampaignLoading();        // LoadBuildCampaign is about to load a campaign
bool InSession();                // a multiplayer session is up on this PC (hosting or joined)
}  // namespace hookcb

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
// The legacy fleet of a joined player (0.7.0): the game's manual build of the starting fleet for the PlayerNation of a
// loaded campaign (see game.cpp). EnterLegacyBuild starts it (StartBuild 0), LegacyShowStartMessage shows the game's
// picture message (modal), LegacyCheckDone ends it once the game is back in normal play, FinishLegacyBuild ends it at
// once without the game's questions.
bool LegacyAvailable();
bool LegacyActive();
bool EnterLegacyBuild(std::string& info);
void LegacyShowStartMessage();
bool LegacyCheckDone(int* ships = nullptr, int* funds = nullptr);
bool FinishLegacyBuild(int* ships = nullptr, int* funds = nullptr);
void LegacyAbort();
std::string LegacyFleetInfo();
std::string DescribeControl(const char* formCls, const char* field);  // tests (bridge `vcl`)
int CampaignStartYear();
int CurrentSlot();
void SetCurrentSlot(int slot);
std::wstring SaveDir(int slot);  // "...\Save\Game<slot>\"

// Same as the Save button (normal save into the current slot); false: it failed. quietInUse (0.7.1): a box of the game
// that reports a file of the save in use by another program stays off the screen (the caller saves again).
bool SaveGame(bool quietInUse = false);
// 0.7.1: every SaveBuildCampaign (the game's own and the mod's) is checked: a box of the game during the save (its
// "Error in Save... procedure!" handlers show the error and carry on) or a file it had to write that is missing, old or
// cut short makes it a failed save. Counters of the saves that worked and failed; whether the last save (of `slot`, -1
// any) failed and why; the bridge's `savestate`.
uint32_t SaveOkCount();
uint32_t SaveFailCount();
bool SaveInProgress();  // inside SaveBuildCampaign (its box's message loop runs the mod's timer and network messages)
bool LastSaveFailed(int slot = -1);
std::string LastSaveError();
std::string SaveState();
// Why the last failed save failed: a full disk (the game's text "not enough space on the disk", or less free space
// than the slot's files take), a file of the save in use by another program (OneDrive's sync, an antivirus scan: a
// sharing violation, "being used by another process", "Cannot create file"), or another error. The texts are matched
// in English and in the system's own words (FormatMessage, as Delphi's SysErrorMessage puts them into the message).
enum class SaveCause { None, DiskFull, InUse, Other };
SaveCause LastSaveCause();  // the last save's (None: it worked)
const char* SaveCauseName(SaveCause c);  // "none", "full", "inuse", "other" (`savestate` cause=)
std::string LastSaveText();  // the game's text of the last failed save, without folders (for the players' messages)
std::string LastSaveFile();  // the file a box of the last failed save names ("RTWGame1.bcs"), or ""
// TEST ONLY (the bridge's `savelock <seconds> [file]`): the next save finds a file of its slot (default
// RTWGame<slot>.bcs) held open without sharing for <seconds> s, as OneDrive's sync or an antivirus scan holds it.
std::string SetTestSaveLock(int seconds, const std::wstring& file);
// pocHeapCheck (diagnosis only): RTW3MP_DIAG checks the campaign's objects before every save and after every load
// (dl::HeapCheck; also the bridge's `pocheapcheck`), the summary and the first problems to the log.
bool PocDiag();
std::string PocHeapCheck(const char* where);
bool KeepBattlePointersTest();  // TEST ONLY (RTW3MP_TEST_KEEPBATTLEPTRS=1): loads keep BattleArea/BattlePossession
// The last battle's area and possession (TfrmBuildCamp.BattleArea/BattlePossession): every load starts with both nil
// (0.6.0); the bridge's `battleptrs`: "ok area=<0|1> poss=<0|1> names=<area>/<possession>".
std::string BattlePointersState();
// Full load like the title screen, then optionally switch nation. modReload: the mod's own reload (its load keeps the
// saved victory points even outside a multiplayer session); false = as the player's own load (the test bridge).
bool LoadCampaign(int slot, int playerIdx, bool modReload = true);
// 0.7.1: nine nations, none nil, and PlayerNation one of them (a failed load of a save cut short leaves fewer); `why`
// says what is missing.
bool CampaignComplete(std::string* why = nullptr);
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
// Campaign divisions (the Fleet organization tab; TfrmBuildCamp.CampaignDivisions, the player's list). A load replaces
// the list (the game's own load appends to it) and drops repeated divisions and ships the nation doesn't have; a joined
// player's own slot (LoadCampaign with playerIdx > 0) loads them against the player's nation. False if unavailable.
bool DivisionsAvailable();
// The bridge's `divs` (read only): "ok n=<count> nation=<PlayerNation's index> list=<CDivId>:<name>:<ship count>:
// <ship ids joined by '.'>|..." ('|' and ':' in names become '_').
std::string CampaignDivisionsInfo();

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
// countA/countB: the ships when the battle was found (logged); the routine gets both nations' ships that can fight in
// the area now, and is not called (false, no battle) if either has none.
bool ResolveAIBattle(int a, int b, int countA, int countB, const std::wstring& area,
                     std::vector<std::wstring>* texts = nullptr);  // texts: its result box, captured (not shown)
bool StartPlayerBattle(int attacker, int defender, const std::wstring& area);  // area "": the game picks one
// 0.6.0 (B6 piece 2): a surprise attack at a war's start in a battle slot: the game's CheckSurpriseAttack(attacker,
// defender) (one of them the player nation; its year decides: missions 1896-1924, a submarine attack without a battle
// 1925-1934, carriers from 1935). True: a battle was set up, or the player let the chance go.
bool StartSurpriseAttack(int attacker, int defender);
std::vector<std::wstring> AreaNames();
bool ModalOpen();                  // a modal game dialog is open
void SetStrategicEnabled(bool on); // the host's strategic screen accepts input (off while battles are fought)
int DoBattleCalls();               // how often the game's battle setup (DoBattle) was entered
// B6 (0.6.0), host: the player's battle generator (CheckForBattles2) for a joined nation K, run in K's view (K as the
// player nation, War := `war`; the nations people play, human[real index], read Tension <= 40). Its CheckAttackLocation
// calls are recorded in order as attempts (attacker, defender in real indices; area "" = the game picks it) and not run.
struct BattleAttempt {
    int a = -1, b = -1;
    std::wstring area;
    int type = 0;  // 0.7.0: 0 CheckAttackLocation(a, b, area); 1 the raider check (CheckRaiderVsPatrols with a = K);
                   // 2 the invasion check (CheckForInvasionBattles with a = K, K's invasions in its battle slot)
};
bool BattleGeneratorAvailable();
bool BattleReplayAvailable();      // a battle slot here can replay the generator's attempts and report declines
// raiders (0.7.0): the view records the game's raider checks too (type 1; only for a PC that replays them, hello gen 3)
// invasions (0.7.0): the view records the game's invasion check (type 2; a PC that replays it, hello gen 4, for a
// nation with invasions or an invasion plan)
bool RunBattleGenerator(int K, int war, const bool human[9], std::vector<BattleAttempt>& out, std::string& err,
                        bool raiders = false, bool invasions = false);
// 0.7.0: raiders against patrols for a joined nation, in its battle slot (K = nation 0): the game's CheckRaiderVsPatrols
// (CheckRaiderInterceptions for its raiders and the enemies', the "thwarts" lines, at most one interception, then
// CheckPendingRaiderBattle: the player's choice between a tactical battle and QuickResolveRaiderBattle). `mask`: nations
// (this game's indices) that read Tension <= 40 meanwhile (the nations people play). TEST ONLY: testSide 0/1 with
// testEnemy: an interception of that side is forced (TestForcedRaid).
struct RaiderCheck {
    bool ran = false, result = false;  // the game's routine ran; its answer (an interception was fought or resolved)
    bool pending = false, quick = false;  // an interception was recorded; QuickResolveRaiderBattle ran (auto resolve)
    int enemy = -1, side = -1;  // the enemy nation (this game's index); 0 the player's raider intercepted, 1 its patrol
    std::wstring area, caption;
    std::vector<std::wstring> news;  // the turn messages it added (the "thwarts" lines)
    std::string test;                // the forced test setup (log text)
};
bool RaiderReplayAvailable();
RaiderCheck StartRaiderCheck(const std::vector<int>& mask, int testSide = -1, int testEnemy = -1);
// 0.7.0 (invasions, P3): the game's CheckForInvasionBattles in a joined nation's battle slot (K = nation 0, its
// invasions put in by inv::SlotPut): the enemy's pending landing, K's own invasion battle at its target, the battles
// for invaded possessions (the player's question, the computer's decline, the player's: DoPlayerDecline, the enemy
// lands). `mask` as for StartRaiderCheck.
struct InvasionCheck {
    bool ran = false, result = false;  // the game's routine ran; its answer (a battle was fought)
    std::vector<std::wstring> news;    // the turn messages it added
    int plain = -1;                    // TEST ONLY: a plain check (`battles testinv 1`): how often its hooks answered
};
bool InvasionReplayAvailable();
InvasionCheck StartInvasionCheck(const std::vector<int>& mask);
// TEST ONLY: the next invasion check is plain (the computer does not decline, the player is not caught out); false if
// the hooks for it are not available.
bool SetTestInvasionPlain(bool on);
// 0.7.0 (fix-afb): the game's AdjustForcesForBattle gives the reinforcements of an intelligence coup's ambush the
// player's nation (it left them an unset local: its access-violation box). TEST ONLY (`battles testcoup <0|1|2>`): the
// next AdjustForcesForBattle on this PC runs as an ambush that finds the player's side the weaker (2: without the mod's
// nation, the game's box); false if unavailable.
bool AmbushFixAvailable();
bool SetTestCoup(int mode);
long TestCoupRuns();  // TEST ONLY: the setups the switch made an ambush
// 0.7.1 (fix-selforce): the battle setup's force builder SelectForce (its ally fallback and its no-force rule) is hooked;
// the guard: where the game's ally fallback would read a missing division ("Error in SelectForce procedure! Access
// violation ... Read of address 0000000D"), the force build ends with no force (DoBattle 0) without the box. Counted on
// this PC: the guard's ends and the game's own no-force rule ("Friendly ships not available ..." / "Enemy unable to
// challenge ...", its VP to the other side), each with the side of the last one.
struct NoForceCounts {
    long guard = 0, rule = 0;
    int guardSide = -1, ruleSide = -1;
    std::string guardWhy;  // "<nation> had no ship that could fight in <area>" (the last guard's)
};
NoForceCounts NoForce();
bool SelectForceGuardAvailable();
// TEST ONLY (`battles testnoforce <0|1|2> [ally] [n]`): the next n force builds of side 0 on this PC run without the
// side's own ships in the area and with an ally's ships there, the ally fallback's draws high (2: the mod's guard off);
// StartPlayerBattle's attempts meanwhile plain (no computer decline, not caught out); false if unavailable.
bool SelectForceHooksAvailable();
bool SetTestNoForce(int mode, int ally, int count = 1);
long TestNoForceRuns();  // TEST ONLY: the force builds the switch ran
int RunHostInvasionCheck();  // TEST ONLY (host): the game's CheckForInvasionBattles for the host (1/0, -1 exception)
int QuickRaiderCalls();            // how often the game's QuickResolveRaiderBattle ran
// 0.6.0 (B6's view, shared with B5), host: K's view of the campaign (K at index 0 and as PlayerNation, the host's nation
// at index K, relation and alliance cells permuted, War := `war`; human[real index] nations read Tension <= 40 with K).
// Not nested; false (nothing changed) when the campaign is not in place. No game exception may unwind through it:
// game calls inside go under __try. HostViewLeave puts everything back. Nation(i) answers view indices meanwhile.
bool HostViewEnter(int K, int war, const bool human[9]);
void HostViewLeave();
// 0.7.0 (invasions): the game's ShowMessage boxes recorded instead of shown (on); off returns the last text ("" none).
std::wstring SetBoxCapture(bool on);
int PlayerDeclineCalls();          // how often the game's DoPlayerDecline ran (the player declined a battle)
// 0.6.0: the player's decline counts (form DeclineCount, saved with the campaign, and EnemyDeclineCount, which the game
// saves but never loads: a battle slot holds the host player's and this PC's last): a joined nation's own are put in
// its battle slot and read back afterwards.
bool GetDeclineCounts(int& own, int& enemy);
bool SetDeclineCounts(int own, int enemy);
// 0.6.0 (B6 round 3): the month's state a joined player's game lacks (it never processes a month). The ship-type flags
// from the loaded campaign by the game's CheckShipTypesExist ("before -> after", "" if unavailable); for the battle slot
// also the month's resets (the surprise-attack and possession battle flags, the aftermath's prestige fields): a log text.
std::string RefreshShipTypeFlags();
std::string PrepareBattleSlotMonth();
// "flags=<6 hex> surprise=n possession=n heroic=n loss=n convoy=n" ("" if unavailable); setForTest (TEST ONLY) first
// sets the five month fields to 1, as an earlier battle or the host's surprise attack would leave them.
std::string MonthStateText(bool setForTest);
void TestGeneratorFault();         // tests: the next generator run in a joined nation's view meets a game exception
std::wstring LastBattleArea();     // the sea area of the last battle the game set up (DoBattle)
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
// TEST ONLY (bridge `econtreaty <chance> blind`): the arms-control conference's masked call without its view of the wars
// of the nations people play (the behaviour before 0.6.0: their wars hidden by B2's mask; MaskedCall's TreatyWarsEnter).
void TestTreatyBlind(bool on);
bool AtWar(int a, int b);
bool AtWarWithAnyAI(int idx);                 // any war of `idx` with nations 1..8
int AllianceMonths(int a, int b);
int WarCounter();                             // months the host's nation has been at war (<= 0: peace)
// A joined player's PC: its own nation's war counter as the host keeps it (months at war > 0, at peace <= 0), set
// before the month is loaded; LoadCampaign with a player nation puts it in place of the save's (the host's). INT_MIN:
// unknown (an older host).
void SetOwnWarCounter(int months);
int OwnWarCounter();
int CountAtWarWithHost();
void StartWar(int a, int b);                  // what the game does when a war breaks out
void MakePeace(int a, int b, int hostOutcome = 3);  // ends only the war between a and b; hostOutcome 1 won, 2 lost, 3 compromise
void SetAlliance(int a, int b, int months);   // 0 ends it

// 0.6.0: the disarmament treaty (CreateArmsControlTreaty) and the submarine ban, campaign-wide ([General]). Any war's
// outbreak ends both in the game (CancelTreaty: nation 0's war at the screen's refresh, two computer nations' war when
// it breaks out). On a joined player's PC CancelTreaty runs without its box when the host ends them for a joined
// nation's war too (SetHostEndsArmsTreaty, the month's header `armstreaty`), else it leaves them as the host has them,
// as it does in a view on the host.
struct ArmsTreaty {
    int months = 0;  // TreatyTimeRemaining (> 0: in force)
    int disp = 0;    // TreatyDisplacementLimit (MaxInt: none)
    int cal = 0;     // TreatyCalibreLimit (MaxInt: none)
    int subBan = 0;  // SubBanTime (> 0: submarines banned)
    int base = 0;    // TreatyTonnageBasePercentage
    bool ok = false;
};
ArmsTreaty ArmsTreatyNow();
bool ArmsTreatyAvailable();
// Host: a joined nation's war ends the treaty and the submarine ban, as a war's outbreak does in the game (its
// CancelTreaty, without the box). True if either was in force. Also the TEST ends (bridge `econtreaty cancel`, `blind`).
bool EndArmsTreatyForWar();
void SetHostEndsArmsTreaty(bool on);  // a joined PC: the host ends them for a joined nation's war (header `armstreaty`)
bool HostEndsArmsTreaty();
int ArmsTreatyKept();       // CancelTreaty calls left to the host on this PC (bridge `armstreaty`)
int ArmsTreatyEndedHere();  // ends on this joined PC with the host's rule (bridge `armstreaty`)
void TestSetArmsTreaty(int months, int disp, int cal);  // TEST ONLY (bridge `armstreaty set`)
void TestArmsConference(int chance);                    // TEST ONLY (bridge `armstreaty conference`, `econtreaty`):
                                                        // the game's CreateArmsControlTreaty(chance), through B2's mask
void TestSetWarCounter(int months);                     // TEST ONLY (bridge `armstreaty war`): this PC's game War

// The relations view: on a PC that plays nation K > 0 as the player (a joined player's PC) the game's tension panel,
// war and alliance marks, flags, map strength bars and war state show nation K's relations (game.cpp, KViewBegin).
int ShownNationIndex();     // K when PlayerNation is BuilderNations[K] with K in 1..8, else 0
// 0.7.1: the last ship data window ("View data"): `ok calls=<n> ship=<name> owner=<nation> asked=<nation the game passed>
// used=<nation shown> design=<class of the design it finds>|- dnation=<n>` (bridge `shipdata`, read only)
std::string DebugShipData();
std::string DebugRelView();  // tests: what the strategic screen's tension panel and war state show now
// Relations v2, B2: the computer's relation rules leave the pairs of the nations under the player's rules alone
// (hookcb::PlayerRulesNation; game.cpp, PlayerRules). A mask a game exception left in place is undone here (month end).
void ResetRelationMasks();

// Relations v2, B3: the player's relation routines for a joined player's nation K, run on K's own PC in K's full view
// (game.cpp, RunPlayerTurn). Real nation indices throughout; the dialogs are the player's.
struct PastWar {
    int opp = -1;        // the other nation
    int monthsAgo = 0;   // since the war ended
    int winner = 0;      // 1 K won, 2 K lost, 0 compromise or unknown
};
// Relations v2, B4: K's peace with the computer's nations, after the relation routines in the same view (game.cpp,
// PeacePart): the player's peace decision (CheckTurnEvents' block) -> the game's peace event (answered by K's player) ->
// NegotiatePeace -> DoPeace, or a proposal / test: NegotiatePeace(chance, lossGain, force) at once. The people's nations
// stay out of it (their wars with K are their own). K's own effects stay in this PC's campaign (they reach the host with
// K's turn); the rest is returned in real indices for the host.
struct PeaceSetup {
    bool run = false;           // run the peace part (K at war with a computer nation)
    bool decision = true;       // the turn's decision (false in a month with another event: one event a month)
    int forcedCode = 0;         // tests: this peace event code (33..36) past the decision
    int forcedChance = 0;       // tests: the forced event's NegotiatePeace chance (0: its answer's)
    bool negotiate = false;     // a proposal or test: NegotiatePeace(chance, lossGain, force) without the decision
    int chance = 40, lossGain = 0;
    bool force = false;
    int vp = 0, enemyVP = 0;    // K's war score in its wars with the computer's nations (the host's ledger)
    int* used = nullptr;        // K's own event cooldowns (100 values, in/out); nullptr: the save's
    // B5: the enemy-revolution rule before the decision (a 0.6.0 host applies its separate peace: `krevo`), with K's own
    // RevolutionPending (real index, 0 none; the host keeps it)
    bool revolution = false;
    int revoPending = 0;
};
struct PeaceLand {  // ChangePossessionOwner
    std::wstring name;
    int from = -1, to = -1;  // to -1: independent
    bool removeCA = false;
};
struct PeaceShip {  // TransferShipAsReparation
    int id = 0, from = -1, to = -1;
    std::wstring name, reason, renamed;  // the name afterwards, the reason, the ship log's "Renamed" line (or empty)
};
struct PeaceTaken {  // a possession's war state afterwards (the possessions of K's war)
    std::wstring name;
    int owner = -1, takenFrom = -1, invaded = -1, support = 0, rebellion = 0;
};
struct PeaceResult {
    bool ran = false;           // K was at war with a computer nation: the peace part ran
    int code = 0;               // the decision's peace event code (33..36), 0 none, -1 CheckTotalDefeat decided
    bool eventShown = false;    // a peace event was shown (K's player answered it)
    bool negotiated = false;    // NegotiatePeace ran (the first call's arguments below)
    int chance = 0, lossGain = 0;
    bool force = false;
    bool accepted = false;      // the game's DoPeace ran: K's wars with the computer's nations ended
    int winner = 0;             // 1 K won, 2 K lost, 0 compromise (the game's war record)
    std::vector<int> enemies;   // K's computer enemies before
    std::vector<PeaceLand> lands;
    std::vector<PeaceShip> ships;
    std::vector<PeaceTaken> taken;
    std::map<int, std::map<std::string, int>> delta;  // other nations: BaseResources, Prestige, BudgetModifier changes
    std::map<int, int> lostWar;                       // other nations: HasLostWar afterwards (changed ones)
    std::vector<std::wstring> treaty;                 // the "Peace treaty" lines
    // B5: the enemy-revolution rule ran; the enemies whose war with K it ended without the game's DoPeace (a separate
    // peace: the others fight on), K's RevolutionPending afterwards (real index)
    bool revolution = false;
    std::vector<int> separate;
    int revoPending = 0;
};
struct PlayerTurnSetup {
    int K = -1;
    bool relations = true;       // the relation routines (false: the peace part alone, a proposal)
    bool tensionChanges = true;  // RandomTensionChanges (the game runs it in a month without another player event)
    int will[9] = {};            // K's own per-relation state: nation j's war plan against K (WillGoToWar)
    int techSharing[9] = {};     // K's tech-sharing deal with nation j
    bool human[9] = {};          // nations played by people (the host's nation 0 and the joined ones)
    std::vector<PastWar> history;  // K's past wars (TensionEffectOfPreviousWars)
    PeaceSetup peace;
    // Relations v2, B5 (d): the war help of K's allies (WarHelpFromAllies) before the relation routines, in the same view
    // (the host runs it: `khelp`; K at war with a computer nation, its war counter above 1 as the game's War > 1), with K's
    // own LendLeaseDone (the host keeps it: `klend`).
    bool allyHelpHost = false;
    bool allyHelp = false;
    bool lendDone = false;
    // Relations v2, B5 (e): the ship seizures of a war (CheckBuildingShipsForWar) right after, in the same view (the host
    // applies them: `kbuild`; K's war counter above 0 as the game's War > 0).
    bool buildWarHost = false;
    bool buildWar = false;
    // 0.6.0 (B6 piece 2): surprise attacks and takeovers at a war's start are made and recorded (a host with welcome
    // `gen` 2; else B3's skips). Tests: nation testSaNation (a computer nation) gets the SurpriseAttack trait for the
    // turn (its war plan comes in `will`), testSaK: K too (K attacks), testTakeover: the takeover's pick is the area's
    // first unowned possession; testSaDry: only DoSurpriseAttack(testSaNation) in the view (no routines).
    bool surprise = false;
    int testSaNation = -1;
    bool testSaK = false, testTakeover = false, testSaDry = false;
    // 0.7.0 (invasions, P4): CheckForWar's surprise landing in K's possession runs as in single player and is recorded
    // (a host with welcome `gen` 4, which keeps K's invasions); else it stays out (B6 piece 2). TEST ONLY: testLanding:
    // the landing's pick is K's first possession worth < 200 in the area and its strength test passes.
    bool landings = false;
    bool testLanding = false;
};
struct PairChange {
    int a, b, before, after;  // a < b
};
struct PlayerTurnResult {
    std::string error;
    bool ranTension = false, ranPreviousWars = false, ranWar = false, ranSurprise = false;
    std::vector<PairChange> tension, alliance;  // every pair that changed (real indices)
    int will[9] = {}, techSharing[9] = {};      // K's per-relation state afterwards
    std::map<int, int> budget;                  // BudgetModifier changes of the other nations (relation routines)
    int vpDelta = 0;                            // K's victory points (kept by the host)
    int warCounter = 0;                         // K's war counter afterwards
    std::map<std::string, int> self;            // K's own fields the turn or proposal changed (field -> delta)
    std::vector<std::wstring> messages;         // the turn messages the routines wrote
    PeaceResult peace;
    // B5 (d): WarHelpFromAllies ran; K's VP from it (also in vpDelta), the other nations' Funds it changed, the ships it
    // handed over (lend-lease: TransferShipAsReparation), K's LendLeaseDone afterwards.
    bool ranAllyHelp = false;
    int allyVP = 0;
    std::map<int, int> allyFunds;
    std::vector<PeaceShip> allyShips;
    bool lendDone = false;
    // B5 (e): CheckBuildingShipsForWar ran; the ships it handed over (TransferShipAsReparation), between K and a computer
    // nation.
    bool ranBuildWar = false;
    std::vector<PeaceShip> buildShips;
    // 0.6.0: CheckSurpriseAttack's pairs in DoSurpriseAttack's order (attacker, defender; K and a computer nation; area
    // ""), and the unowned possessions taken over (ChangePossessionOwner, from -1)
    std::vector<BattleAttempt> surprise;
    std::vector<PeaceLand> takeovers;
    // 0.7.0 (P4): the surprise landings in K's possessions (name; `to` = the landing computer nation, real index)
    std::vector<PeaceLand> landings;
};
bool AllyHelpAvailable();  // B5 (d): WarHelpFromAllies for K in its view
bool BuildWarAvailable();  // B5 (e): CheckBuildingShipsForWar for K in its view
std::string TestAllyRandom(int v);  // TEST ONLY (bridge `allyrand <0|-1>`): 0 = the allies' war help draws 0 throughout
// TEST ONLY (bridge `revotest <0|-1> [n:unrest ...]`): the next enemy-revolution run on this PC: its draws 0 (-1: the
// game's) and these nations' UnrestLevel (real indices) in K's copy first
std::string TestRevolution(const std::vector<std::string>& args);
bool PlayerTurnAvailable();
bool PeaceAvailable();  // B4: the peace part's routines and hooks
bool SurpriseAvailable();  // 0.6.0: surprise attacks and takeovers at a war's start in the view (hooks, call sites)
PlayerTurnResult RunPlayerTurn(const PlayerTurnSetup& setup);
// Relations v2, B4 (host): a joined nation's peace, applied with the game's own routines. "" or why not.
std::string MovePossession(const std::wstring& name, int from, int to, bool removeCA);  // `from` must own it
std::string HandOverShip(int id, int from, int to, const std::wstring& name, const std::wstring& reason,
                         const std::wstring& renamedLine);  // by CShipId in `from`'s list
std::string SetPossessionWarState(const PeaceTaken& t);  // by name; `t.owner` must own it
bool SetNationFlag(int idx, const std::string& field, bool value);  // a Boolean field (HasLostWar, Blockaded)
int NationFlag(int idx, const std::string& field);                  // -1 if unknown
// B5 (warfx.cpp): told after every call of the game's CheckBlockadeStatus (the load's, after its victory points were
// put back, `load` true; the month's), on the main thread. The game's [General] War cell (the host's nation's war
// counter), or nullptr.
void SetBlockadeCheckedHook(void(__stdcall* fn)(void* form, bool load));
int* WarCell();
// The turn messages of RunPlayerTurn in the game's own turn report dialog (modal), on this PC.
void ShowTurnMessagesOf(const std::vector<std::wstring>& lines);

}  // namespace game

// Callbacks from hooks (implemented by the session layer). All run on the game's main thread.
namespace hookcb {
bool OnTurnClick(void* self, void* sender);  // true => swallow the click
bool OnResignClick(void* self);              // true => swallow
void OnAfterEndOfTurn(void* self);
bool IsHumanRemoteNation(int idx);  // host: AI must not act for this nation
// Host (relations v2): this joined nation's relations follow the player's rules (its player's PC runs the player's
// relation routines), so the computer's relation rules leave its pairs alone.
bool PlayerRulesNation(int idx);
// Host (relations v2): a joined nation's own war counter, as the game's War for the host's nation (months at war > 0,
// at peace < 0; stepped after the month). The player's economy rules for that nation read it (economy.cpp).
int KWarCounter(int idx);
// Host (relations v2, B5: warfx.cpp): `vp` victory points of a war effect (a blockade, ...) for `gainer` in its war with
// `loser`: the nation's VP and the pair's war score (the B4 ledger of a nation under the player's rules with a computer
// nation, or a war between two players).
void CreditWarVP(int gainer, int loser, int vp, const char* why);
// Host (B5: warfx.cpp): nation K's war score over all its wars now (the B4 ledger with the computer's nations, the score
// of a war between two players with the host's or another joined nation): K's points and its enemies'.
void KWarScore(int K, int& vp, int& evp);
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
// B6 (host): the game's CheckForBattles2 is about to look for the host's battle of the month (the joined nations' first).
void OnBattleGenerator();
// The game's DoBattle returned (opp = the opponent's nation index); the battle screen is open if it was fought.
void OnDoBattleDone(int opp, bool result);
// The game's LoadBuildCampaign returned (a battle in progress in that save is resumed inside it: the screen is open).
void OnAfterCampaignLoad();
void OnBattleSlotSkippedTurn();  // a battle slot's close skipped DoStrategicTurn
void OnStrategicTurnRuns();      // DoStrategicTurn runs (outside a battle slot)
void OnCampaignLoading();        // LoadBuildCampaign is about to load a campaign
bool InSession();                // a multiplayer session is up on this PC (hosting or joined)
bool JoinedPlayerPC();           // this PC plays a joined nation in a session: its campaign is the host's copy
// 0.6.1: nation idx (a real index) is played by a person and the game never declares a war between it and this PC's
// nation (relations v2: wars between players come only from the players). Host: a joined nation under the player's
// rules; a joined player's PC: the host's nation and the other nations people play, while the host runs the player's
// rules for this PC's nation.
bool WarOnlyByPlayers(int idx);
}  // namespace hookcb

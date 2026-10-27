// 0.7.0: invasions for joined nations (the game's invasions of single player for a nation K a person plays).
#pragma once
#include <string>
#include <vector>

namespace inv {

// A joined nation's invasion plan: the TfrmBuildCamp fields single player keeps for its player (InvasionTarget,
// InvasionCount, EnemyInvasionTarget, InvasionBattlePending) and the possessions whose invasion began this month
// (TPossession.InvadedThisTurn, which the game never saves). Possessions by name; `pending` a real nation index
// (K = K's own invasion battle, a computer nation = its invasion battle against K, -1 none).
struct KState {
    std::wstring target, enemyTarget;
    int count = 0, pending = -1;
    std::vector<std::wstring> itt;
    bool Empty() const { return target.empty() && enemyTarget.empty() && pending < 0 && itt.empty() && !count; }
};

bool Install();    // hooks (CheckRebellionAndInvasions, its rebellion part, the test random cell); after game hooks
bool Available();  // K's month resolution can run here
KState& State(int K);  // the host's record of joined nation K's plan (session file `kinv`)

// The host's own routines hide every invasion that does not involve nation 0 (a joined nation's) while they run
// (CheckRebellionAndInvasions, CheckForInvasionBattles, DoPeace, HandleBattleAftermath); restored on every path.
// `peace`: DoPeace (it clears every possession's war state: the hidden ones get Invaded, InvasionSupport and TakenFrom
// back). Not nested; no effect inside a view.
void HostHide(const char* what, bool peace = false);
void HostUnhide();
std::string TakeHideLog();  // the month's hide counts ("" when none)

// Host: K's month resolution, the game's CheckRebellionAndInvasions in K's view (K = PlayerNation, War := `war`;
// `human`: real indices of the nations people play), with K's invasions only, K's plan in the form fields, the
// base-expansion countdown and the rebellions left out. `st` is updated; `news` gets the texts for K's player.
struct RunResult {
    bool ran = false;
    std::string error, summary;
    std::vector<std::wstring> news;
    int enemyVP = 0;  // what the routine added to the form's EnemyVP (K's enemies' points)
    int owners = 0;   // possessions that changed hands
};
RunResult RunForNation(int K, int war, const bool human[9], KState& st, int armyOffensive, int enemyDecline);

// K's invasions on this PC's campaign, real indices: "name:owner:invader:support" joined by ';' (bridge, logs).
std::string Describe(int K);
// Does nation K have anything to resolve (a plan or a possession with a K invasion)?
bool HasWork(int K, const KState& st);

// InvadedThisTurn across a reload of the same month (the game never saves it): the possessions marked now, and set
// them again after the load. Returns how many were set.
std::vector<std::wstring> MarkedThisTurn();
int RestoreMarked(const std::vector<std::wstring>& names);

// Client (P2): K's plan in this PC's form (slot 77, real indices, PlayerNation = K's record). `clear`: no plan (an
// older host: the host player's own plan loaded with the save is taken away). Returns a log text.
std::string ClientSetForm(const KState& st, bool clear);
std::wstring FormTarget();  // the form's InvasionTarget name ("" none)

// The game's monthly cost of an invasion target for nation n's record (GetModifiedBudget / 300 + 100).
int TargetCost(void* nationRecord);

// Possession helpers (host tests and the order's checks): -2 no such possession.
int PossessionOwner(const std::wstring& name);
int PossessionValue(const std::wstring& name);
int PossessionInvader(const std::wstring& name);
std::string SetPossession(const std::wstring& name, int invader, int support, bool itt);  // TEST ONLY ("" ok)

// TEST ONLY: the random draws of CheckRebellionAndInvasions forced (0 off, 1 low = Random(n) answers 0, 2 high = n-1).
void SetTestRandom(int mode);
int TestRandom();

std::string EncodeStates(const KState st[9]);  // "K\x1ftarget\x1fcount\x1fenemy\x1fpending\x1fitt/itt\x1e"
void DecodeStates(const std::string& s, KState st[9]);

// P3, invasion battles: a joined nation's invasions go with each of its battles (its battle slot, K = nation 0 there)
// and come back with the result. A possession of a K invasion by name, real indices (the invader: K, or the computer
// nation invading K's possession).
struct PossState {
    std::wstring name;
    int invader = -1, support = 0;
};
std::vector<PossState> KPossessions(int K);  // host: K's invasions on this campaign
std::string EncodePoss(const std::vector<PossState>& v);  // "name\x1finvader\x1fsupport\x1e"
std::vector<PossState> DecodePoss(const std::string& s);
// Client, battle slot (nation 0 = K): every possession's invasion cleared (the host's and the others' are not K's),
// K's set (real -> slot indices), K's plan in the form. Returns a log text.
std::string SlotPut(int K, const KState& st, const std::vector<PossState>& ps);
// Client, battle slot: K's plan and invasions as the game left them (real indices).
void SlotRead(int K, KState& st, std::vector<PossState>& ps);
// Host: the changes of K's battles (from `base`, what the first battle took, to `now`, what the last one returned)
// applied to this campaign by name. Returns a log text ("" nothing changed).
std::string ApplyPossessions(int K, const std::vector<PossState>& base, const std::vector<PossState>& now);
// The game's DoPossessionBattle calls from CheckForInvasionBattles (any PC): how many so far, and the last one's sides
// (this game's indices), possession and sea area.
struct PossBattle {
    int attacker = -1, defender = -1;
    std::wstring poss, area;
};
long PossessionBattles(PossBattle* last);
std::string PossessionBattlesText(long since);  // the calls after number `since` ("poss (a attacks d), ...")
// P7 (host): the computer's fleets see a joined nation's invasions (GetMostImportantArea). TEST: J's most important area
// without and with them, and the areas' points ("without=<area> with=<area> shown=n points(without)=... points(with)=...").
std::string EscortTest(int J);

}  // namespace inv

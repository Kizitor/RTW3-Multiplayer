// Relations v2, B5 (money): the player's economy rules for a joined player's nation K on the host.
//
// The host runs the month of every joined player's nation K, and the game treats K as one of its computer nations (RTW3
// 1.01.44). The mod keeps the computer's monthly budget, building and research routines away from K (game.cpp, g_skips);
// this file takes away the money and budget rules the game gives only the nations it plays, and gives K the player's rule
// where the player has one:
//  - ComputeExpenses: K's month is Funds += GetMonthlyBalance(K), as the player's (no subsidy of 25 x FleetSize, twice when
//    K's budget is below the host's and K is tense; no BaseResources floor at 1.5 x FleetSize; no BaseResources x 0.9 at a
//    BudgetModifier below 5).
//  - CheckBBNumbers (a capital-ship race: +6000 x FleetSize and BudgetModifier +1 for the computer) does not run for K: the
//    game never runs it for the player (its player branch is dead code).
//  - BudgetModifier (BM): the "keep up" rule (+1 toward the host's BM) is skipped; the "preeminence" rule takes the
//    player's branch (unrest below 3, half the chance, two below the highest BM); the January cuts, a coup's +1 (the player
//    answers a coup in a dialog), CheckAITensionEvents' +1s (at war with a computer nation, arms race, a war declared
//    between computer nations) and CheckForWar's +1 for the nations not at war when the host's nation goes to war are left
//    out; when the host's nation goes to war with K, K gets the player's increments (+1 below 30, then +1 below 25 when
//    Random(30) > BM) instead of the computer's (+2 up to 30, then +1 below 22).
//  - SpySuccess: the host's spies don't find K "secretly spending an additional 10%" (which also gave K ModBudget / 5).
//  - The computer's deficit relief: DeHaltAllShips (every ship resumed each month) and HaltMostExpensiveShip (halts while the
//    balance is negative) leave K's ships alone: K's player halts and resumes them.
//  - Maintenance at the player's prices: the naval academy (free for the computer), aircraft (x 0.8 for the computer), light
//    carriers with aircraft (x 0.6 for the computer).
//  - CheckPlayerFunds (the player's, after the game's own for the host's nation): the debt messages with the prestige loss
//    on K's prestige, the game's MoraleLoss(K) and Prestige -1; corruption's 1000 that disappear (the question is the
//    player's choice, which K's player can't answer on the host: its first answer, no further effect); the hoarding cuts
//    (BM -1, the opera's 2000 x FleetSize, a tenth of the funds) with K's own army offensive months: the game's
//    ArmyOffensive is one value for every nation and is not written for K. K's budget is x 0.9 while K's months last
//    (GetModifiedBudget's test of ArmyOffensive answers for K), counted down where ReduceTimeLimits counts the game's (no
//    victory points of an army offensive for K) and ended when K's last war ends, as the game's at DoPeace (OnWarChanged).
//  - CheckTurnEvents (after the game's own): the war loan (BM +1) and, at peace, SmallFleetCheck's complaints (prestige -1),
//    budget rules (BM +1/-1 or K's tension with its own most tense nation, with K's own ParliamentDelay) and the notice of
//    the smaller share. The war counter is K's own (session.cpp, hookcb::KWarCounter); the routines' texts, with K's
//    leader and parliament, go to K's player with the next month (TakeNews).
//  - ReduceTimeLimits' treaty cut: the player's second -1 and its message instead of the computer's tension message.
//  - The computer's decisions about K that the player makes himself are left out: escorts, ASW patrols and the ASW research
//    priority when K's merchant ships are sunk (CheckSubmarineActivity takes K for the player), K as BuyTechnology's
//    partner, K as the buyer of another nation's scrapped ship, the scrapping of K's armed merchant cruisers after the
//    host's peace (the player's "No": K's player scraps them on K's PC), and China's computer upkeep when K is China.
// Left as they are: the computer's unrest cuts (BM -1 with unrest -1: the player's unrest rules show the host's dialogs and
// follow the host's war counter), the rules all nations share (HandleEconomicGrowth, AdvanceShipConstruction, DoPeace, the
// treaty rules besides the cut above, the nationalist rules), the intel part of the balance (a change of its own), and, of
// the player's rules for K:
// SmallFleetCheck's battlecruiser minister and early events (the host player's dialogs and events), the Air Force's budget
// event (a choice, not run for K), the global-economy event (a world event), ReduceTimeLimits' nationalist and
// GGP rules (R2/R4 read the host's ParliamentDelay) and GetModifiedBudget's x 0.97 (the host's war counter). The computer
// still develops aircraft prototypes and licences for K (K's own aircraft requests stay on K's PC and are not carried),
// gives K's new carriers air groups at launch (AdjustShipValuesUponLaunch), and keeps CheckForRevolutionInEnemyNation and
// WarHelpFromAllies for K (war effects, later work).
//
// Each rule is found by its routine's RTTI name and its instruction bytes (field offsets from RTTI), unique in the routine;
// a rule whose code does not match stays the game's (the reproduced routines: their code as expected, their texts found).
// Host only (hookcb::IsHumanRemoteNation): nation 0 and the computer's nations are unchanged; on a joined player's PC only
// the budget figures follow K's army offensive (session.cpp). Each joined nation's month is logged after the month's
// CheckForWar (DoEndOfTurn): `economy: nation K ...`; the bridge's `econ K` shows it.
#include "economy.h"
#include "delphi.h"
#include "game.h"
#include "../../third_party/minhook/include/MinHook.h"
#include <algorithm>
#include <cstring>

namespace economy {
namespace {

int fBM = -1, fBR = -1, fFunds = -1, fTension = -1, fUnrest = -1, fPlayer = -1, fHalted = -1;
void* mBalance = nullptr;    // TBuilderNation.GetMonthlyBalance
void* mModBudget = nullptr;  // TBuilderNation.GetModifiedBudget
void* mRandom = nullptr;     // System.Random (unnamed: WarStartsWithNation's call)
int* fsCell = nullptr;       // the campaign's fleet-size multiplier (ComputeExpenses' 25 x FleetSize)
int** yearCell = nullptr;    // cells holding the addresses of the game's Year and Month
int** monthCell = nullptr;
bool aiPrices = false;       // the maintenance tests answer as for a computer nation (the record's comparison)
bool installed = false;

// The player's rules the mod runs for K (CheckPlayerFunds, CheckTurnEvents with SmallFleetCheck): fields, the game's
// helpers (conventions from their call sites in these routines), its cells and constants.
int fPrestige = -1, fCorruption = -1, fGov = -1, fBombastic = -1, fDispLimit = -1, fNationName = -1, fName2 = -1,
    fParlName = -1, fArmyOff = -1, fTreatyTime = -1, fTreatyDisp = -1;
void* mMoraleLoss = nullptr;    // TfrmBuildCamp.MoraleLoss(Self, aNation = EDX): any nation
void* mLeaderName = nullptr;    // TdlgEventAnswer.InsertLeaderName(Self, aCaption = EDX, aNationIndex = ECX; Result on stack)
void* mLeaderTitle = nullptr;   // TBuilderNation.GetLeaderTitle(Self, Result = EDX)
void* mRating = nullptr;        // TfrmBuildCamp.GetNationEconomicRating(Self, aNation = EDX)
void* mSmallestBB = nullptr;    // TfrmBuildCamp.SmallestBBFleet(Self, aNation = EDX, IncludeBuilding = CL)
void* mSmallestCA = nullptr;    // TfrmBuildCamp.SmallestCAFleet(Self, aNation = EDX, IncludeBuilding = CL)
void* mStrength = nullptr;      // TBuilderNation.GetTotalStrength(Self)
void* mAdjYear = nullptr;       // the year shifted by the tech-pace option (EAX = year; unnamed: SmallFleetCheck's call)
void* mUStrClr = nullptr;       // System._UStrClr (unnamed: GetModifiedBudget's epilogue)
void** dlgCell = nullptr;       // the TdlgEventAnswer instance's cell
const void* c06 = nullptr;      // CheckPlayerFunds' 0.6 (10-byte) and 20000 (4-byte), SmallFleetCheck's 0.7 (10-byte)
const void* c20000 = nullptr;
const void* c07 = nullptr;
bool g_fundsOn = false, g_eventsOn = false;  // the two routines' code and texts as expected
void* g_oFunds = nullptr;       // MinHook's trampolines of CheckPlayerFunds and CheckTurnEvents
void* g_oEvents = nullptr;
int g_testRandom = -1;          // TEST ONLY (bridge `econrand`): the rules' Random(n) draws min(v, n - 1)
bool g_aoOff = false;           // GetModifiedBudget without K's army offensive (the record's comparison)

enum Kind {
    kGuard,    // the instruction is left out for K (or K takes a branch: skipTo is its target)
    kForce,    // a `cmp nation, [form+PlayerNation]` answers "the player's nation" for K (ZF = 1)
    kCount,    // the instruction runs; K's cases are counted
    kSkipRet,  // a routine returns at once (False) for K
};
enum Rule {
    rExpenses, rBB, rKeepUp, rPreem, rPreemInc, rJanuary, rCoup, rAIWar, rArmsRace, rAIWarStart, rHostWar, rSpy, rDeHalt,
    rHalt, rAcademy, rAir, rLightCarrier, rWarStart, rMonthEnd, rArmyStep, rArmyBudget, rASW, rTechPartner, rShipBuyer,
    rAMC, rTreatyCut, rChina, rFunds, rEvents, rRules
};
const char* const kRuleNames[rRules] = {"expenses",   "bbnumbers",  "keepup",      "preeminence", "preeminence+",
                                        "january",    "coup",       "aiwar",       "armsrace",    "aiwarstart",
                                        "hostwar",    "spy",        "dehalt",      "halt",        "academy",
                                        "air",        "lightcarrier", "warstart",  "monthend",    "armyoffensive",
                                        "aobudget",   "asw",        "techpartner", "shipbuyer",   "amc",
                                        "treatycut",  "china",      "playerfunds", "turnevents"};

struct Site {
    const char* name;
    Rule rule;
    Kind kind;
    int reg;                // the register holding the nation (x86 numbering: 0 eax, 2 edx, 3 ebx, 6 esi, 7 edi)
    bool index;             // ... as a nation index (else a TBuilderNation)
    uint8_t* at;            // the instruction (or the routine)
    void* skipTo;           // where the code goes on when the instruction is left out or answered (kGuard, kForce)
    void* orig;             // MinHook's trampoline: the game's instruction, then back
    bool on;
};
enum SiteIdx {
    sExpenses, sBB, sKeepUp, sPreem, sPreemInc, sJanuary1, sJanuary2, sCoup, sAIWar, sArmsRace, sAIWarStart1,
    sAIWarStart2, sHostWar, sSpy, sDeHalt, sHalt, sAcademy, sAir, sLightCarrier, sArmyStep, sArmyBudget, sASW1, sASW2,
    sASW3, sTechPartner, sShipBuyer, sAMC, sTreatyCut, sChina, sSites
};
Site g_sites[sSites] = {
    {"ComputeExpenses (the computer's part)", rExpenses, kGuard, 3},
    {"CheckBBNumbers", rBB, kSkipRet, 2},
    {"ReduceTimeLimits keep-up", rKeepUp, kForce, 7},
    {"ReduceTimeLimits preeminence", rPreem, kForce, 7},
    {"ReduceTimeLimits preeminence +1 (player's branch)", rPreemInc, kCount, 7},
    {"ReduceTimeLimits January cut (governments 3-5)", rJanuary, kGuard, 0},
    {"ReduceTimeLimits January cut (governments 0-2)", rJanuary, kGuard, 0},
    {"CheckForCoups +1", rCoup, kGuard, 0},
    {"CheckAITensionEvents at war +1", rAIWar, kGuard, 6},
    {"CheckAITensionEvents arms race +1", rArmsRace, kGuard, 6},
    {"CheckAITensionEvents war declared +1", rAIWarStart, kGuard, 6},
    {"CheckAITensionEvents war declared on +1", rAIWarStart, kGuard, 0},
    {"CheckForWar +1 of the nations at peace", rHostWar, kGuard, 0},
    {"SpySuccess secret spending", rSpy, kForce, 0},
    {"DeHaltAllShips", rDeHalt, kSkipRet, 0},
    {"HaltMostExpensiveShip", rHalt, kSkipRet, 2},
    {"GetNavalAcademyCost", rAcademy, kForce, 6},
    {"ApplyAirMaintenanceModifiers", rAir, kForce, 6},
    {"GetShipMaintenanceCost light carrier", rLightCarrier, kForce, 7},
    {"ReduceTimeLimits army offensive (K's months counted down)", rArmyStep, kCount, 3},
    {"GetModifiedBudget army offensive x0.9 (K's own)", rArmyBudget, kGuard, 3},
    {"CheckSubmarineActivity escorts and patrols", rASW, kForce, 3},
    {"CheckSubmarineActivity escorts and ASW priority", rASW, kForce, 3},
    {"CheckSubmarineActivity escorts, patrols and ASW priority at war", rASW, kForce, 3},
    {"BuyTechnology partner", rTechPartner, kGuard, 0},
    {"ScrapShip sell-on buyer", rShipBuyer, kGuard, 0},
    {"ScrapShipsAfterPeace armed merchant cruisers", rAMC, kGuard, 3, true},
    {"ReduceTimeLimits treaty cut (the player's)", rTreatyCut, kGuard, 0},
    {"CheckChineseConcessions China's upkeep", rChina, kForce, 6},
};

// WarStartsWithNation (entry) and the CheckForWar call of DoEndOfTurn (the month's end).
void* g_oWarStarts = nullptr;
bool g_warStartsOn = false;
void* g_cfwEntry = nullptr;   // CheckForWar (its entry: another hook may be there)
void* g_eomAfter = nullptr;   // the instruction after DoEndOfTurn's call of it
uint8_t* g_eomAt = nullptr;
void* g_eomOrig = nullptr;
bool g_eomOn = false;

// ---------------------------------------------------------------------------------------------
// The month's record of each joined nation

struct Month {
    int date = -1;  // Year * 12 + Month - 1 when it was recorded (-1: unknown)
    bool expenses = false;
    int fundsBefore = 0, fundsAfter = 0, balance = 0, balanceAI = 0;  // ComputeExpenses: the player's prices, the computer's
    int subsidy = 0, brFloor = 0, brDecay = 0;                         // what the computer's part would have done
    int n[rRules] = {};   // how often each rule was left out (preeminence, preeminence+: the player's branch, its +1s)
    int spyFunds = 0;     // what the left-out spy findings would have paid
    int warStarts = 0, warGame = 0, warPlayer = 0;  // the host's wars with K: the computer's BM increments, the player's
    int shared = 0;       // AdvanceShipConstruction's change of Funds (rules every nation shares: delays, the reclaim)
    // The player's rules after ComputeExpenses (CheckPlayerFunds, CheckTurnEvents)
    bool rules = false;   // they ran for K
    int kwar = 0;         // K's war counter they saw
    int debt = 0;         // 1 in the red, 2 the finance ministry, 3 losing confidence, 4 outraged, 5 late pay (morale)
    int debtPrestige = 0;
    bool morale = false;  // the game's MoraleLoss(K) ran
    int corruption = 0;   // 1 the 1000 disappeared, 2 the corruption question (both answered with the first answer)
    int hoard = 0;        // 1 army offensive (at war), 2 opera, 3 parliament, 4 defence committee, 5 reallocation
    int loan = 0;         // the war loan (BM +1)
    int complaint = 0;    // 1 battleships, 2 displeased (battleships), 3 armoured cruisers, 4 displeased (cruisers)
    int complaintPrestige = 0;
    int budget = 0;       // SmallFleetCheck's budget rule (1..4) that applied
    int budgetBM = 0;
    std::string tension;  // ... and the tension changes it made: "nation:delta,..."
    int notice = 0;       // the smaller share's notice
    int ruleFunds = 0, ruleBM = 0, rulePrestige = 0;  // these rules' changes of K's Funds, BudgetModifier, Prestige
    int news = 0;         // messages for K's player
    int kao = 0, kpd = 0; // K's army offensive months and ParliamentDelay afterwards
    int aoCut = 0;        // ComputeExpenses: the budget K's own army offensive took (0: none, or the game's own applied)
    bool ended = false;   // the month's end was seen (K's funds then)
    int fundsEnd = 0;
};
std::map<int, Month> g_cur, g_last;  // nation -> this month's record, the last complete one

int GameDate() {
    if (!yearCell || !monthCell || !*yearCell || !*monthCell) return -1;
    return **yearCell * 12 + **monthCell - 1;
}

std::string DateText(int d) {
    if (d < 0) return "unknown";
    char b[16];
    snprintf(b, sizeof(b), "%04d-%02d", d / 12, d % 12 + 1);
    return b;
}

std::string Line(int K, const Month& r) {
    char b[1024];
    std::string s = "economy: nation " + std::to_string(K) + " (" + W2U(game::NationName(K)) + ") " + DateText(r.date) + ": ";
    if (r.expenses)
        snprintf(b, sizeof(b), "funds %d -> %d (balance %+d at the player's prices, maintenance %+d); ", r.fundsBefore,
                 r.fundsAfter, r.balance, r.balanceAI - r.balance);
    else
        snprintf(b, sizeof(b), "no ComputeExpenses; ");
    s += b;
    int up = r.n[rAIWar] + r.n[rArmsRace] + r.n[rAIWarStart] + r.n[rHostWar] + r.n[rCoup];
    snprintf(b, sizeof(b),
             "withheld: subsidy %d, BB check %d, spy %d (funds %d), BM +%d (AI war %d, arms race %d, AI war start %d, "
             "host's war %d, coup %d), January -%d, keep-up %d, BR floor %d, BR x0.9 %d, auto-halt %d, resume %d; the "
             "player's rules: preeminence %d (+%d), war start %+d (computer's %+d); shared construction rules %+d",
             r.subsidy, r.n[rBB], r.n[rSpy], r.spyFunds, up, r.n[rAIWar], r.n[rArmsRace], r.n[rAIWarStart], r.n[rHostWar],
             r.n[rCoup], r.n[rJanuary], r.n[rKeepUp], r.brFloor, r.brDecay, r.n[rHalt], r.n[rDeHalt], r.n[rPreem],
             r.n[rPreemInc], r.warPlayer, r.warGame, r.shared);
    s += b;
    static const char* const kDebt[] = {"none", "in the red", "finance ministry", "losing confidence", "outraged", "late pay"};
    static const char* const kCorr[] = {"none", "1000 disappeared", "question"};
    static const char* const kHoard[] = {"none", "army offensive", "opera", "parliament", "defence committee", "reallocation"};
    if (r.rules)
        snprintf(b, sizeof(b),
                 "; the player's money rules (war counter %d): debt %s (prestige -%d, morale %s), corruption %s, hoarding "
                 "%s, war loan %d, complaint %d (prestige -%d), budget rule %d (BM %+d, tension %s), notice %d; funds %+d, BM "
                 "%+d, prestige %+d, %d message(s)",
                 r.kwar, kDebt[r.debt], r.debtPrestige, r.morale ? "yes" : "no", kCorr[r.corruption], kHoard[r.hoard],
                 r.loan, r.complaint, r.complaintPrestige, r.budget, r.budgetBM, r.tension.empty() ? "-" : r.tension.c_str(),
                 r.notice, r.ruleFunds, r.ruleBM, r.rulePrestige, r.news);
    else
        snprintf(b, sizeof(b), "; the player's money rules: not run");
    s += b;
    snprintf(b, sizeof(b), "; treaty cut %d; army offensive %d month(s) (budget -%d), ParliamentDelay %d; ", r.n[rTreatyCut],
             r.kao, r.aoCut, r.kpd);
    s += b;
    if (r.ended && r.expenses)
        snprintf(b, sizeof(b), "month's end funds %d (other changes %+d); ", r.fundsEnd,
                 r.fundsEnd - r.fundsAfter - r.ruleFunds);
    else
        snprintf(b, sizeof(b), "month's end funds %s; ", r.ended ? std::to_string(r.fundsEnd).c_str() : "unknown");
    s += b;
    snprintf(b, sizeof(b), "left out for the player: escorts/ASW %d, tech partner %d, ship purchase %d, AMCs kept %d, China %d",
             r.n[rASW], r.n[rTechPartner], r.n[rShipBuyer], r.n[rAMC], r.n[rChina]);
    return s + b;
}

void Flush(int K) {
    auto it = g_cur.find(K);
    if (it == g_cur.end()) return;
    Log("%s", Line(K, it->second).c_str());
    g_last[K] = it->second;
    g_cur.erase(it);
}

void FlushAll() {
    while (!g_cur.empty()) Flush(g_cur.begin()->first);
}

// This month's record of K (a record of an earlier month, without the month's end, is logged first).
Month& Rec(int K) {
    int d = GameDate();
    auto it = g_cur.find(K);
    if (it != g_cur.end() && it->second.date != d) {
        Flush(K);
        it = g_cur.end();
    }
    if (it == g_cur.end()) {
        it = g_cur.emplace(K, Month()).first;
        it->second.date = d;
    }
    return it->second;
}

// ---------------------------------------------------------------------------------------------
// The rules

// K in 1..8 when `nation` is a joined player's nation on the host, else 0.
int Joined(void* nation) {
    if (!nation || !hookcb::InSession()) return 0;
    for (int K = 1; K <= 8; K++)
        if (hookcb::IsHumanRemoteNation(K) && game::Nation(K) == nation) return K;
    return 0;
}

int JoinedIndex(int idx) {  // the same for a nation index
    return idx >= 1 && idx <= 8 && hookcb::InSession() && hookcb::IsHumanRemoteNation(idx) ? idx : 0;
}

int& Field(void* n, int off) { return dl::At<int>(n, off); }
int ModBudget(void* n) { return (int)dl::Call(mModBudget, (uint32_t)(uintptr_t)n); }
int Random(int range) { return (int)dl::Call(mRandom, (uint32_t)range); }

// K's own state of the player's rules (in the session file with the month: EncodeState) and the month's messages of the
// rules for K's player (sent with the next month: TakeNews).
struct KState {
    int pd = 0;           // ParliamentDelay: SmallFleetCheck's budget rules wait while it is above 0
    int ao = 0;           // army offensive months: K's budget is x 0.9 while above 0
    bool notice = false;  // the smaller share's notice was shown (once)
};
std::map<int, KState> g_k;
std::map<int, std::vector<std::wstring>> g_news;
const size_t kMaxNews = 60;  // a nation whose player is away keeps the latest ones

int KArmy(int K) {
    auto it = g_k.find(K);
    return it == g_k.end() ? 0 : it->second.ao;
}

// The game's texts of these rules: Delphi literals their routines reference, found by their start (or the whole text).
enum TextId {
    tDebt1, tDebt2, tDebt3, tDebt4, tDebt5, tCorrupt1, tCorrupt1a, tCorrupt2, tCorrupt2a, tHoardWar, tHoardOpera,
    tHoardParl, tHoard2a, tHoard2b, tLoanThe, tLoanLeader, tLoanRubber, tLoanPX, tPre1, tPre2, tPre3, tMid1, tMid2, tMid3,
    tBB, tBBWar, tBBPeace, tDispleasedP, tCA, tCASmall, tDispleased, tPrestige, tHawk1, tHawk2, tScare1, tScare2,
    tBuild1, tBuild2, tPress1, tPressPX, tPressThe, tPressAuth, tTreaty, tWelfareThe, tWelfare, tCivil, tInfra, tSocial,
    tShare, tCut1, tCut2, tTexts
};
enum TextIn { inFunds, inEvents, inFleet, inLimits };  // CheckPlayerFunds, CheckTurnEvents, SmallFleetCheck, ReduceTimeLimits
struct TextSpec {
    TextIn in;
    const wchar_t* match;
    bool exact;
};
const TextSpec kTexts[tTexts] = {
    {inFunds, L"Navy finances are in the red!", false},
    {inFunds, L"The finance ministry is raising serious concerns", false},
    {inFunds, L"The minister of the navy is letting you know", false},
    {inFunds, L"The minister of the navy is outraged", false},
    {inFunds, L"With pay for the men being late again", false},
    {inFunds, L"It seems 1 000 has mysteriously disappeared", false},
    {inFunds, L"Well, these things will happen in any large organization.", false},
    {inFunds, L"There are indications that corruption is rife", false},
    {inFunds, L"Not sure there is much to do.", false},
    {inFunds, L"With the army is in dire need of equipment", false},
    {inFunds, L"With the navy literally awash in money and no immediate threat of war", false},
    {inFunds, L"With the navy not using up its budget allocations", false},
    {inFunds, L"With the navy evidently having more funding than it can use", false},
    {inFunds, L"With the navy literally awash in money and the army in dire need", false},
    {inEvents, L"The ", true},
    {inEvents, L" pledges additional funding for the navy!", false},
    {inEvents, L"PX rubber stamps additional war loan!", false},
    {inEvents, L"PX authorizes additional war loan!", false},
    {inFleet, L"The navy league is complaining that ", true},
    {inFleet, L"Conservative newspapers are reporting that ", true},
    {inFleet, L"Hawk party politicians attack the government saying that ", true},
    {inFleet, L"our navy is pathetically weak in ", true},
    {inFleet, L"we have too few ", true},
    {inFleet, L"other nations laugh at our weakness in ", true},
    {inFleet, L"battleships!", true},
    {inFleet, L"You are held responsible for the navy's weakness in battleships", false},
    {inFleet, L"It appears that we have the smallest total battleship tonnage", false},
    {inFleet, L" is displeased! Your prestige suffers!", true},
    {inFleet, L"armoured cruisers!", true},
    {inFleet, L"It seems that we have the smallest total tonnage of large cruisers", false},
    {inFleet, L" is displeased!", true},
    {inFleet, L" Your prestige suffers!", true},
    {inFleet, L"With the threat from ", true},
    {inFleet, L" rising, the hawk party", false},
    {inFleet, L"Battleship scare! The rising threat from ", true},
    {inFleet, L" persuades PX to authorize additional funding for the navy.", false},
    {inFleet, L"The buildup of the ", true},
    {inFleet, L" navy persuades PX to increase funding for the navy.", false},
    {inFleet, L"With the press doing headlines on the threat from ", true},
    {inFleet, L", PX passes a bill authorizing increased naval spending.", false},
    {inFleet, L", the ", true},
    {inFleet, L" authorizes increased naval spending.", true},
    {inFleet, L"With tensions low and a disarmament treaty in force", false},
    {inFleet, L"The ", true},
    {inFleet, L" wants to strengthen the welfare state.", false},
    {inFleet, L"Resources are diverted to the civilian sector", false},
    {inFleet, L"National resources are diverted to civilian infrastructure projects.", false},
    {inFleet, L"PX passes a bill increasing spending on social programmes", false},
    {inFleet, L"As government spending increases, the military will receive a smaller share", false},
    {inLimits, L"With naval armaments restricted by treaty, ", true},
    {inLimits, L" reduces funding for the Navy!", true},
};
std::wstring g_text[tTexts];
const std::wstring& Text(int id) { return g_text[id]; }

void News(int K, const std::wstring& text) {
    auto& v = g_news[K];
    v.push_back(text);
    if (v.size() > kMaxNews) v.erase(v.begin());
    Rec(K).news++;
    std::string line = W2U(text);
    for (char& c : line)
        if (c == '\r' || c == '\n') c = ' ';
    Log("economy: nation %d news: %s", K, line.c_str());
}

// The month's step of K's army offensive months, one toward 0 where ReduceTimeLimits steps the game's (before the
// month's ComputeExpenses reads the budget).
void StepArmy() {
    for (auto& e : g_k) {
        int& ao = e.second.ao;
        if (!ao || !JoinedIndex(e.first)) continue;
        int before = ao;
        ao += ao > 0 ? -1 : 1;
        Log("economy: nation %d's army offensive: %d -> %d month(s)", e.first, before, ao);
    }
}

// ReduceTimeLimits' treaty cut (high funds and balance, a calm most tense nation): the player's BudgetModifier -2 and its
// message with K's parliament instead of the computer's -1 and tension message (the code goes on at the rule's end).
void TreatyCut(int K, void* n) {
    Field(n, fBM) -= 2;
    Rec(K).n[rTreatyCut]++;
    void* pn = fParlName >= 0 ? dl::At<void*>(n, fParlName) : nullptr;
    News(K, Text(tCut1) + (pn ? dl::ReadUStr(pn) : std::wstring()) + Text(tCut2));
}

// GetMonthlyBalance at the player's prices, or at the computer's (the record's comparison).
int BalanceAt(void* n, bool computersPrices) {
    int v = 0;
    aiPrices = computersPrices;
    __try {
        v = (int)dl::Call(mBalance, (uint32_t)(uintptr_t)n);
    } __finally {
        aiPrices = false;
    }
    return v;
}

int RoundHalfEven(long long num, int den) {  // num / den rounded as the FPU's Round (num >= 0)
    long long q = num / den, r = num % den;
    if (2 * r > den || (2 * r == den && (q & 1))) q++;
    return (int)q;
}

int ModBudgetWithoutK(void* n) {  // GetModifiedBudget without K's own army offensive (the record's comparison)
    int v = 0;
    g_aoOff = true;
    __try {
        v = ModBudget(n);
    } __finally {
        g_aoOff = false;
    }
    return v;
}

// ComputeExpenses, nation K (ebx) of its loop: the player's month (Funds += GetMonthlyBalance) instead of the computer's
// part (the BaseResources floor and x 0.9, the balance, the subsidies); 1 = the loop goes on with the next nation.
int Expenses(int K, void* n) {
    void* f = game::BuildCamp();
    void* P = f ? dl::At<void*>(f, fPlayer) : nullptr;
    int fs = fsCell ? *fsCell : 0;
    int balAI = BalanceAt(n, true);
    int bal = BalanceAt(n, false);
    Month& r = Rec(K);
    int br = Field(n, fBR);
    if (2LL * br < 3LL * fs) {
        int floor = RoundHalfEven(3LL * fs, 2);
        r.brFloor += floor - br;
        br = floor;
    }
    if (Field(n, fBM) < 5 && br > 0) r.brDecay += br - RoundHalfEven(9LL * br, 10);
    int sub = 25 * fs;
    if (P && ModBudget(n) < ModBudget(P) && Field(n, fTension) > 6) sub += 25 * fs;
    r.subsidy += sub;
    int& funds = Field(n, fFunds);
    r.fundsBefore = funds;
    funds += bal;
    r.fundsAfter = funds;
    r.balance = bal;
    r.balanceAI = balAI;
    r.expenses = true;
    r.kao = KArmy(K);  // the budget K's own army offensive took from this balance
    r.aoCut = r.kao > 0 && g_sites[sArmyBudget].on ? ModBudgetWithoutK(n) - ModBudget(n) : 0;
    return 1;
}

int HaltedShips(void* n) {
    if (fHalted < 0) return 1;
    int c = 0;
    for (int i = 0; i < game::ShipCount(n); i++)
        if (void* s = game::Ship(n, i))
            if (dl::At<uint8_t>(s, fHalted)) c++;
    return c;
}

// 1 = K's case: the instruction is left out (kGuard), answered as the player's (kForce), the routine returns (kSkipRet).
int Decide(int idx, uint32_t reg) {
    const Site& s = g_sites[idx];
    if (s.rule == rArmyStep) {  // reg: the form
        StepArmy();
        return 0;
    }
    int K = s.index ? JoinedIndex((int)reg) : Joined((void*)(uintptr_t)reg);
    if (!K) return 0;
    void* nation = s.index ? game::Nation(K) : (void*)(uintptr_t)reg;
    switch (s.rule) {
        case rExpenses:
            return Expenses(K, nation);
        case rAcademy:
        case rAir:
        case rLightCarrier:
            return aiPrices ? 0 : 1;
        case rArmyBudget:  // GetModifiedBudget's x 0.9 while K's own army offensive lasts (also outside the month)
            return !g_aoOff && KArmy(K) > 0 ? 1 : 0;
        case rTreatyCut:
            TreatyCut(K, nation);
            return 1;
        case rPreemInc:
            Rec(K).n[rPreemInc]++;
            return 0;
        case rKeepUp: {  // counted when it would have rolled (below the host's BM, unrest below 6)
            void* f = game::BuildCamp();
            void* P = f ? dl::At<void*>(f, fPlayer) : nullptr;
            if (P && Field(nation, fBM) < Field(P, fBM) && Field(nation, fUnrest) < 6) Rec(K).n[rKeepUp]++;
            return 1;
        }
        case rSpy:
            if (Field(nation, fTension) > 7) {
                Month& r = Rec(K);
                r.n[rSpy]++;
                r.spyFunds += ModBudget(nation) / 5;
            }
            return 1;
        case rDeHalt:
            Rec(K).n[rDeHalt] += HaltedShips(nation);
            return 1;
        default:
            Rec(K).n[s.rule]++;
            return 1;
    }
}

int __stdcall SiteDecider(int idx, uint32_t reg) {
    __try {
        return Decide(idx, reg);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// WarStartsWithNation(Self = EAX, aNation = EDX): the host's nation goes to war with aNation. A joined nation gets the
// player's increments of its BudgetModifier instead of the computer's (the routine's other effects stay).
void WarStartsAfter(int K, void* n, int bm0) {
    int& bm = Field(n, fBM);
    int computers = bm - bm0;
    int v = bm0;
    if (v < 30) v++;
    if (v < 25 && Random(30) > v) v++;
    bm = v;
    Month& r = Rec(K);
    r.warStarts++;
    r.warGame += computers;
    r.warPlayer += v - bm0;
    Log("economy: nation %d's war with the host's nation: BudgetModifier %d -> %d (the player's rule; the computer's %+d)",
        K, bm0, v, computers);
}

int SafeJoinedBM(void* n, int* bm) {
    __try {
        int K = Joined(n);
        *bm = K ? Field(n, fBM) : 0;
        return K;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void SafeWarStartsAfter(int K, void* n, int bm0) {
    __try {
        WarStartsAfter(K, n, bm0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __stdcall WarStartsCB(void* self, void* nation) {
    int bm0 = 0;
    int K = SafeJoinedBM(nation, &bm0);
    dl::Call(g_oWarStarts, (uint32_t)(uintptr_t)self, (uint32_t)(uintptr_t)nation);
    if (K) SafeWarStartsAfter(K, nation, bm0);
}

void MonthEnd() {  // K's funds at the month's end, then the records
    for (auto& e : g_cur) {
        void* n = e.first < game::NationCount() ? game::Nation(e.first) : nullptr;
        if (!n) continue;
        e.second.fundsEnd = Field(n, fFunds);
        e.second.ended = true;
    }
    FlushAll();
}

void __stdcall MonthEndCB() {
    __try {
        MonthEnd();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// AdvanceShipConstruction (ReduceTimeLimits, after the date's step, before ComputeExpenses): how much the rules every
// nation shares changed each joined nation's Funds (a construction delay pays the month's cost back, the hoarding reclaim
// takes 5 or 10 %), for the month's record.
void* g_oAdvance = nullptr;
bool g_advanceOn = false;
int g_advFunds[9] = {};
bool g_advHave[9] = {};

void AdvanceFundsBefore() {
    for (int K = 1; K <= 8; K++) {
        g_advHave[K] = false;
        if (!hookcb::InSession() || !hookcb::IsHumanRemoteNation(K) || K >= game::NationCount()) continue;
        if (void* n = game::Nation(K)) {
            g_advFunds[K] = Field(n, fFunds);
            g_advHave[K] = true;
        }
    }
}

void AdvanceFundsAfter() {
    for (int K = 1; K <= 8; K++) {
        if (!g_advHave[K]) continue;
        g_advHave[K] = false;
        void* n = game::Nation(K);
        int d = n ? Field(n, fFunds) - g_advFunds[K] : 0;
        if (d) Rec(K).shared += d;
    }
}

void __stdcall AdvancePreCB() {
    __try {
        AdvanceFundsBefore();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __stdcall AdvancePostCB() {
    __try {
        AdvanceFundsAfter();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// ---------------------------------------------------------------------------------------------
// The player's monthly rules for K: CheckPlayerFunds, CheckTurnEvents (with SmallFleetCheck)
//
// They run after the game's own routine (the host's nation) for each joined nation, with the game's tests and Random calls
// in the game's order, K's own war counter (session.cpp: the game's War for K, stepped after the month), ParliamentDelay
// and army offensive months, K's tension cells and the game's helpers. What the host's player would see in a message or a
// picture goes to K's player (TakeNews).

int KWar(int K) { return hookcb::KWarCounter(K); }
int Year() { return yearCell && *yearCell ? **yearCell : 0; }
int MonthOfYear() { return monthCell && *monthCell ? **monthCell : 0; }
int FS() { return fsCell ? *fsCell : 0; }

int R(int n) {  // the game's Random(n), or (TEST ONLY) the fixed draw
    if (g_testRandom >= 0) return n > 0 ? std::min(g_testRandom, n - 1) : 0;
    return Random(n);
}

void DecMin(int& x, int a, int mn) {  // the game's DecMin: x -= a, not below mn
    if (a < 0) return;
    x -= a;
    if (mn > x) x = mn;
}

// ProportionalPrestigeLoss(f), which the game applies to the host's nation, on K's prestige; the prestige lost.
int PrestigeLoss(void* n, int f) {
    int& p = Field(n, fPrestige);
    int before = p;
    DecMin(p, f >= p || p < 19 ? 1 : (p >= 2 * f ? p / f : 1), 0);
    return before - p;
}

// K's tension towards nation j; a change writes both cells of the pair as the game's SetAITension (K's Tension alone for
// nation 0).
int TensionOf(void* n, int j) { return Field(n, fTension + 4 * j); }

void SetTensionOf(int K, void* n, int j, int v, Month& r) {
    int d = v - TensionOf(n, j);
    if (v < 0 || !d) return;
    Field(n, fTension + 4 * j) = v;
    if (j >= 1)
        if (void* o = game::Nation(j)) Field(o, fTension + 4 * K) = v;
    r.tension += (r.tension.empty() ? "" : ",") + std::to_string(j) + ":" + (d > 0 ? "+" : "") + std::to_string(d);
}

// GetMostTenseNationIndex for K: of the other nations, the first with the highest tension above 0 and below 40, else the
// first of them (the game looks at nations 1..8 for the host's nation).
int MostTense(int K, void* n) {
    int best = 0, idx = -1;
    for (int j = 0; j <= 8 && j < game::NationCount(); j++) {
        if (j == K) continue;
        if (idx < 0) idx = j;
        int t = TensionOf(n, j);
        if (best < t && t < 40) {
            best = t;
            idx = j;
        }
    }
    return idx;
}

// A string a game routine returned (its Result variable): read, then released with the game's _UStrClr.
std::wstring TakeString(uint32_t& var) {
    std::wstring s = var ? dl::ReadUStr((const void*)(uintptr_t)var) : std::wstring();
    if (var && mUStrClr) dl::Call(mUStrClr, (uint32_t)(uintptr_t)&var);
    var = 0;
    return s;
}

std::wstring LeaderTitle(void* n) {  // GetLeaderTitle (by K's government)
    uint32_t var = 0;
    dl::Call(mLeaderTitle, (uint32_t)(uintptr_t)n, (uint32_t)(uintptr_t)&var);
    return TakeString(var);
}

// InsertLeaderName(text, K): YY -> K's leader, PX -> K's parliament (the game passes its nation 0).
std::wstring WithNames(const std::wstring& text, int K) {
    void* dlg = dlgCell ? *dlgCell : nullptr;
    if (!dlg) return text;
    dl::ConstUStr s(text);
    uint32_t var = 0, ref = (uint32_t)(uintptr_t)&var;
    dl::Call(mLeaderName, (uint32_t)(uintptr_t)dlg, s.ptr(), (uint32_t)K, 1, &ref);
    return TakeString(var);
}

std::wstring NameOf(void* n, int off) {  // NationName, Name2
    void* p = n && off >= 0 ? dl::At<void*>(n, off) : nullptr;
    return p ? dl::ReadUStr(p) : std::wstring();
}

int Rating(void* form, void* n) { return (int)dl::Call(mRating, (uint32_t)(uintptr_t)form, (uint32_t)(uintptr_t)n); }
bool Smallest(void* fn, void* form, void* n) {  // SmallestBBFleet / SmallestCAFleet(n, IncludeBuilding = True)
    return (uint8_t)dl::Call(fn, (uint32_t)(uintptr_t)form, (uint32_t)(uintptr_t)n, 1) != 0;
}
int Strength(void* n) { return (int)dl::Call(mStrength, (uint32_t)(uintptr_t)n); }
int AdjYear(int y) { return (int)dl::Call(mAdjYear, (uint32_t)y); }

// a > b * c + d as the game's x87 code computes it: its constants (c 10 bytes, d 4 bytes or none) and its FPU control
// word (the game's thread).
bool X87Greater(int a, int b, const void* c, const void* d) {
    unsigned short sw = 0;
    __asm {
        mov eax, c
        mov edx, d
        fild b
        fld tbyte ptr [eax]
        fmulp st(1), st
        test edx, edx
        jz nod
        fadd dword ptr [edx]
    nod:
        fild a
        fucompp
        fnstsw sw
    }
    return (sw & 0x4500) == 0;  // C0, C2, C3 clear: a is greater (the game's jbe not taken)
}

// CheckPlayerFunds for K, with K's funds after ComputeExpenses.
void PlayerFundsK(void* form, int K, void* n) {
    KState& ks = g_k[K];
    Month& r = Rec(K);
    r.rules = true;
    int kwar = KWar(K);
    r.kwar = kwar;
    int& funds = Field(n, fFunds);
    int& bm = Field(n, fBM);
    // debt
    if (funds < 0) {
        int kind = 0, lost = 0;
        if (funds > -50 || R(100) < 10) {
            kind = 1;
        } else if (funds > -1000) {
            if (R(100) < 40) {
                kind = 2;
            } else if (R(100) < 40 && kwar < -2) {
                kind = 3;
                lost = PrestigeLoss(n, 10);
            }
        } else if (!(BalanceAt(n, false) >= 100 && R(100) >= 20)) {
            if (R(100) < 50 && kwar < -4) {
                kind = 4;
                lost = PrestigeLoss(n, 10);
            } else {
                kind = 5;
                dl::Call(mMoraleLoss, (uint32_t)(uintptr_t)form, (uint32_t)(uintptr_t)n);
                r.morale = true;
                int& p = Field(n, fPrestige);
                int before = p;
                DecMin(p, 1, 0);
                lost = before - p;
            }
        }
        if (kind) {
            static const TextId kDebtText[] = {tDebt1, tDebt1, tDebt2, tDebt3, tDebt4, tDebt5};
            News(K, Text(kDebtText[kind]));
            r.debt = kind;
            r.debtPrestige += lost;
            r.rulePrestige -= lost;
        }
    }
    // corruption: its question is the player's choice, which K's player can't answer on the host; K takes the first
    // answer ("these things will happen", "not much to do"), which changes nothing more
    int& c = Field(n, fCorruption);
    if (c > 100) c = 100;
    if (c < 0) c = 0;
    if (c > 0 && R(300 - c) < 10 && (kwar <= 0 || R(100) < kwar + 70)) {
        if (funds > 4000 && R(300) < c) {
            funds -= 1000;
            r.ruleFunds -= 1000;
            r.corruption = 1;
            News(K, Text(tCorrupt1) + L" - " + Text(tCorrupt1a));
        } else if (R(300) < c) {
            r.corruption = 2;
            News(K, Text(tCorrupt2) + L" - " + Text(tCorrupt2a));
        }
    }
    // The Air Force's budget request (1950-1975, 1 in 1000) is a choice of the player: not run for K.
    // Hoarding: K's own army offensive months stand for the game's ArmyOffensive (one value for every nation).
    bool fired = false;
    if (X87Greater(funds, ModBudget(n), c06, c20000)) {
        int j = MostTense(K, n);
        if (j >= 0 && TensionOf(n, j) < 6 && BalanceAt(n, false) > -100 && ks.ao == 0 &&
            (R(10) == 1 || (R(10) == 1 && Year() < 1903))) {
            fired = true;
            if (kwar > 0) {
                News(K, Text(tHoardWar));
                ks.ao = R(3) + 4;
                r.hoard = 1;
            } else if (dl::At<uint8_t>(n, fBombastic) || dl::At<uint8_t>(n, fGov) == 2) {
                News(K, WithNames(Text(tHoardOpera), K));
                int x = 2 * FS() * 1000;
                funds -= x;
                bm--;
                ks.ao = 12;
                r.ruleFunds -= x;
                r.ruleBM--;
                r.hoard = 2;
            } else {
                News(K, WithNames(Text(tHoardParl), K));
                bm--;
                ks.ao = 12;
                r.ruleBM--;
                r.hoard = 3;
            }
        }
    }
    if (!fired && funds > 40000 * FS() && ks.ao == 0 && (R(10) == 1 || R(5) == 1)) {
        bool committee = R(100) < 50;
        News(K, WithNames(Text(committee ? tHoard2a : tHoard2b), K));
        int x = funds / 10;
        funds -= x;
        bm--;
        ks.ao = 8;
        r.ruleFunds -= x;
        r.ruleBM--;
        r.hoard = committee ? 4 : 5;
    }
    r.kao = ks.ao;
    r.kpd = ks.pd;
}

// SmallFleetCheck's budget rules for K (with K's ParliamentDelay at 0): the first that applies. The most tense nation is
// looked up again at every use, as the game does.
void BudgetK(void* form, int K, void* n, int kwar, KState& ks, Month& r) {
    int& bm = Field(n, fBM);
    int funds = Field(n, fFunds), fs = FS();
    uint8_t gov = dl::At<uint8_t>(n, fGov);
    // a tense rival that is not much weaker
    int j = MostTense(K, n);
    void* mt = game::Nation(j);
    if (mt && TensionOf(n, j) > 7 && bm < 18 && R(20) > bm && funds < 5000 * fs) {
        int strengthMT = Strength(mt);
        if (X87Greater(strengthMT, Strength(n), c07, nullptr)) {
            r.budget = 1;
            if (kwar > -36) {
                SetTensionOf(K, n, j, TensionOf(n, j) - 1, r);
                return;
            }
            std::wstring s;
            if (R(100) < 60)
                s = Text(tHawk1) + NameOf(mt, fNationName) + Text(tHawk2);
            else if (AdjYear(1942) > Year())
                s = Text(tScare1) + NameOf(mt, fNationName) + Text(tScare2);
            else
                s = Text(tBuild1) + NameOf(mt, fName2) + Text(tBuild2);
            News(K, WithNames(s, K));
            bm++;
            ks.pd = R(4) + 10;
            r.budgetBM++;
            r.ruleBM++;
            return;
        }
    }
    // the press (governments 0)
    if (gov == 0) {
        j = MostTense(K, n);
        mt = game::Nation(j);
        if (TensionOf(n, j) > 6 && bm < 20 && R(25) > bm && funds < 5000 * fs) {
            if (kwar > -36) {
                int t = TensionOf(n, j);
                if (t > R(10)) SetTensionOf(K, n, j, std::max(0, t - 1), r);
            } else if (R(100) >= 50) {  // below 50 the game builds the opposition's text and doesn't show it
                std::wstring s = Text(tPress1) + NameOf(mt, fNationName);
                if (gov == 2 || dl::At<uint8_t>(n, fBombastic))
                    s = WithNames(s + Text(tPressPX), K);
                else
                    s += Text(tPressThe) + LeaderTitle(n) + Text(tPressAuth);
                News(K, s);
            }
            bm++;
            int j2 = MostTense(K, n);
            SetTensionOf(K, n, j2, TensionOf(n, j2) + 1, r);
            ks.pd = R(4) + 8;
            r.budget = 2;
            r.budgetBM++;
            r.ruleBM++;
            return;
        }
    }
    // low tension under a disarmament treaty
    j = MostTense(K, n);
    if (TensionOf(n, j) + 50 > R(100) && R(40000) > Field(form, fTreatyDisp) && funds > 10000 * fs && TensionOf(n, j) < 6) {
        News(K, WithNames(Text(tTreaty), K));
        bm--;
        ks.pd = R(4) + 10;
        r.budget = 3;
        r.budgetBM--;
        r.ruleBM--;
        return;
    }
    // the welfare state (after 1950)
    if (Year() > 1950 && bm > 14 && kwar <= 0 && TensionOf(n, MostTense(K, n)) < 7 && R(40) < bm) {
        std::wstring s;
        if (R(100) < 30)
            s = Text(tWelfareThe) + LeaderTitle(n) + Text(tWelfare);
        else if (R(100) < 30)
            s = Text(tCivil);
        else if (R(100) < 30)
            s = Text(tInfra);
        else
            s = Text(tSocial);
        News(K, WithNames(s, K));
        bm--;
        ks.pd = R(4) + 12;
        r.budget = 4;
        r.budgetBM--;
        r.ruleBM--;
    }
}

// SmallFleetCheck for K. Its battlecruiser minister (a choice of the player) and its early events (the host's events and
// relations) are the host player's: not run for K.
void SmallFleetK(void* form, int K, void* n, int kwar) {
    KState& ks = g_k[K];
    Month& r = Rec(K);
    int& prestige = Field(n, fPrestige);
    // complaints about a small fleet
    if (R(8) == 1) {
        int a = R(10);
        std::wstring s = Text(a < 4 ? tPre1 : a < 8 ? tPre2 : tPre3);
        int b = R(10);
        s += Text(b < 4 ? tMid1 : b < 8 ? tMid2 : tMid3);
        bool battleships = false;
        if (Rating(form, n) < 4 && Smallest(mSmallestBB, form, n)) {
            int rating = Rating(form, n);
            if (rating < R(4) + 1 && Field(n, fDispLimit) == 0 && Field(form, fTreatyTime) <= 0) {
                battleships = true;
                if (R(100) < 60 && kwar <= 0) {
                    News(K, s + Text(tBB));
                    r.complaint = 1;
                } else {
                    News(K, kwar > 0 ? Text(tBBWar) : Text(tBBPeace) + LeaderTitle(n) + Text(tDispleasedP));
                    prestige--;
                    r.complaint = 2;
                    r.complaintPrestige++;
                    r.rulePrestige--;
                }
            }
        }
        if (!battleships && Rating(form, n) < 3 && Smallest(mSmallestCA, form, n) && kwar <= 0) {
            int rating = Rating(form, n);
            if (rating < R(4) + 1 && Year() < 1918 && Field(form, fTreatyTime) <= 0) {
                if (R(100) < 60) {
                    News(K, s + Text(tCA));
                    r.complaint = 3;
                } else {
                    std::wstring t = Text(tCASmall) + LeaderTitle(n) + Text(tDispleased);
                    if (R(100) < 50) {
                        prestige--;
                        t += Text(tPrestige);
                        r.complaintPrestige++;
                        r.rulePrestige--;
                    }
                    News(K, t);
                    r.complaint = 4;
                }
            }
        }
    }
    if (ks.pd <= 0) BudgetK(form, K, n, kwar, ks, r);
    // the smaller share of the budget (GetModifiedBudget's x 0.97 after 1920 at peace): told once
    uint8_t gov = dl::At<uint8_t>(n, fGov);
    if (Year() > 1920 && kwar < -6 && !(gov >= 2 && gov <= 5) && !ks.notice) {
        News(K, Text(tShare));
        ks.notice = true;
        r.notice = 1;
    }
}

// CheckTurnEvents for K: the war loan at war, SmallFleetCheck at peace (its other parts are the host player's events and
// dialogs, or the computer's routines for every nation).
void TurnEventsK(void* form, int K, void* n) {
    KState& ks = g_k[K];
    DecMin(ks.pd, 1, 0);  // where the game counts its ParliamentDelay down (ReduceTimeLimits): every month
    Month& r = Rec(K);
    r.rules = true;
    int kwar = KWar(K);
    r.kwar = kwar;
    int y = Year(), m = MonthOfYear();
    bool skip = ((y == 1900 || y == 1890 || y == 1920) && m == 2) || kwar == 0 || kwar == -1;
    if (!skip) {
        int at = R(4) + 2;
        if (at < kwar) {
            int& bm = Field(n, fBM);
            if (kwar % 3 == 1 && Field(n, fUnrest) < 8 && bm < 22 && R(16) + 10 > bm) {
                uint8_t gov = dl::At<uint8_t>(n, fGov);
                std::wstring t;
                if (gov == 4 || gov == 5)
                    t = R(100) < 80 ? Text(tLoanThe) + LeaderTitle(n) + Text(tLoanLeader) : WithNames(Text(tLoanRubber), K);
                else
                    t = WithNames(Text(tLoanPX), K);
                News(K, t);
                bm++;
                r.loan = 1;
                r.ruleBM++;
            }
            // From the fifth month the game decides about peace here: relations v2's peace part on K's PC.
        } else if (kwar <= 0) {
            SmallFleetK(form, K, n, kwar);
        }
    }
    r.kao = ks.ao;
    r.kpd = ks.pd;
}

// After the game's CheckPlayerFunds / CheckTurnEvents: each joined nation's (a nation the computer plays keeps nothing).
void ForJoined(void* form, void (*rule)(void*, int, void*)) {
    if (!hookcb::InSession()) return;
    for (int K = 1; K <= 8 && K < game::NationCount(); K++) {
        if (!hookcb::IsHumanRemoteNation(K)) {
            g_k.erase(K);
            g_news.erase(K);
            continue;
        }
        if (void* n = game::Nation(K)) rule(form, K, n);
    }
}

void __stdcall PlayerFundsCB(void* form) {
    __try {
        if (g_fundsOn) ForJoined(form, PlayerFundsK);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("economy: the player's CheckPlayerFunds for the joined nations: exception");
    }
}

void __stdcall TurnEventsCB(void* form) {
    __try {
        if (g_eventsOn) ForJoined(form, TurnEventsK);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("economy: the player's CheckTurnEvents for the joined nations: exception");
    }
}

// ---------------------------------------------------------------------------------------------
// Code

uint8_t* g_code = nullptr;
size_t g_codeUsed = 0;

uint8_t* CodeSpace(size_t n) {
    if (!g_code) g_code = (uint8_t*)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_code || g_codeUsed + n > 4096) return nullptr;
    uint8_t* p = g_code + g_codeUsed;
    g_codeUsed += (n + 15) & ~(size_t)15;
    return p;
}

void Put32(uint8_t*& p, uint32_t v) {
    memcpy(p, &v, 4);
    p += 4;
}

// pushad; push <reg>; push idx; call SiteDecider; test eax,eax; popad; jnz K; jmp [orig]
// K: kGuard/kCount: jmp [skipTo]; kForce: cmp esp,esp (ZF = 1); jmp [skipTo]; kSkipRet: xor eax,eax; ret
void* EmitSiteThunk(int idx) {
    Site& s = g_sites[idx];
    uint8_t* p0 = CodeSpace(48);
    if (!p0) return nullptr;
    uint8_t* p = p0;
    *p++ = 0x60;
    *p++ = (uint8_t)(0x50 + s.reg);
    *p++ = 0x68;
    Put32(p, (uint32_t)idx);
    *p++ = 0xB8;
    Put32(p, (uint32_t)(uintptr_t)&SiteDecider);
    *p++ = 0xFF;
    *p++ = 0xD0;
    *p++ = 0x85;
    *p++ = 0xC0;
    *p++ = 0x61;
    *p++ = 0x75;
    *p++ = 0x06;
    *p++ = 0xFF;
    *p++ = 0x25;
    Put32(p, (uint32_t)(uintptr_t)&s.orig);
    if (s.kind == kSkipRet) {
        *p++ = 0x31;
        *p++ = 0xC0;
        *p++ = 0xC3;
    } else {
        if (s.kind == kForce) {
            *p++ = 0x39;
            *p++ = 0xE4;
        }
        *p++ = 0xFF;
        *p++ = 0x25;
        Put32(p, (uint32_t)(uintptr_t)&s.skipTo);
    }
    FlushInstructionCache(GetCurrentProcess(), p0, (SIZE_T)(p - p0));
    return p0;
}

// WarStartsWithNation: push edx; push eax; call WarStartsCB; ret
void* EmitWarStartsThunk() {
    uint8_t* p0 = CodeSpace(16);
    if (!p0) return nullptr;
    uint8_t* p = p0;
    *p++ = 0x52;
    *p++ = 0x50;
    *p++ = 0xB8;
    Put32(p, (uint32_t)(uintptr_t)&WarStartsCB);
    *p++ = 0xFF;
    *p++ = 0xD0;
    *p++ = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), p0, (SIZE_T)(p - p0));
    return p0;
}

// DoEndOfTurn's `call CheckForWar`: call [CheckForWar]; pushad; call MonthEndCB; popad; jmp [after]
void* EmitMonthEndThunk() {
    uint8_t* p0 = CodeSpace(32);
    if (!p0) return nullptr;
    uint8_t* p = p0;
    *p++ = 0xFF;
    *p++ = 0x15;
    Put32(p, (uint32_t)(uintptr_t)&g_cfwEntry);
    *p++ = 0x60;
    *p++ = 0xB8;
    Put32(p, (uint32_t)(uintptr_t)&MonthEndCB);
    *p++ = 0xFF;
    *p++ = 0xD0;
    *p++ = 0x61;
    *p++ = 0xFF;
    *p++ = 0x25;
    Put32(p, (uint32_t)(uintptr_t)&g_eomAfter);
    FlushInstructionCache(GetCurrentProcess(), p0, (SIZE_T)(p - p0));
    return p0;
}

// AdvanceShipConstruction (Self in eax, no other argument): pushad; call AdvancePreCB; popad; call [g_oAdvance];
// pushad; call AdvancePostCB; popad; ret
void* EmitAdvanceThunk() {
    uint8_t* p0 = CodeSpace(32);
    if (!p0) return nullptr;
    uint8_t* p = p0;
    *p++ = 0x60;
    *p++ = 0xB8;
    Put32(p, (uint32_t)(uintptr_t)&AdvancePreCB);
    *p++ = 0xFF;
    *p++ = 0xD0;
    *p++ = 0x61;
    *p++ = 0xFF;
    *p++ = 0x15;
    Put32(p, (uint32_t)(uintptr_t)&g_oAdvance);
    *p++ = 0x60;
    *p++ = 0xB8;
    Put32(p, (uint32_t)(uintptr_t)&AdvancePostCB);
    *p++ = 0xFF;
    *p++ = 0xD0;
    *p++ = 0x61;
    *p++ = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), p0, (SIZE_T)(p - p0));
    return p0;
}

// CheckPlayerFunds / CheckTurnEvents (Self in eax, no other argument): the game's routine, then cb(Self).
// push eax; call [orig]; pop eax; pushad; push eax; call cb; popad; ret
void* EmitAfterThunk(void** orig, void(__stdcall* cb)(void*)) {
    uint8_t* p0 = CodeSpace(32);
    if (!p0) return nullptr;
    uint8_t* p = p0;
    *p++ = 0x50;
    *p++ = 0xFF;
    *p++ = 0x15;
    Put32(p, (uint32_t)(uintptr_t)orig);
    *p++ = 0x58;
    *p++ = 0x60;
    *p++ = 0x50;
    *p++ = 0xB8;
    Put32(p, (uint32_t)(uintptr_t)cb);
    *p++ = 0xFF;
    *p++ = 0xD0;
    *p++ = 0x61;
    *p++ = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), p0, (SIZE_T)(p - p0));
    return p0;
}

bool HookAt(void* target, void* detour, void** orig, const char* name) {
    if (!target || !detour) return false;
    MH_STATUS st = MH_CreateHook(target, detour, orig);
    if (st == MH_OK) {
        st = MH_EnableHook(target);
        if (st != MH_OK) MH_RemoveHook(target);
    }
    if (st != MH_OK) {
        Log("economy: %s: hook failed (%d)", name, (int)st);
        *orig = nullptr;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Finding the rules

const int X = -1;  // any byte

struct Pat {
    std::vector<int> b;
    Pat& o(std::initializer_list<int> x) {
        b.insert(b.end(), x.begin(), x.end());
        return *this;
    }
    Pat& d(int v) {  // a 32-bit value (a field offset, an address)
        for (int i = 0; i < 4; i++) b.push_back((int)(((uint32_t)v >> (8 * i)) & 0xFF));
        return *this;
    }
};

bool InImage(uintptr_t a) { return a >= dl::g_base && a < dl::g_base + dl::g_size; }

// The only match of `p` in [fn, fn + len), else nullptr.
uint8_t* Unique(void* fn, size_t len, const Pat& p) {
    if (!fn) return nullptr;
    uint8_t* a = (uint8_t*)fn;
    uint8_t* hit = dl::FindPattern(a, len, p.b);
    if (!hit) return nullptr;
    size_t rest = len - (size_t)(hit - a) - 1;
    return dl::FindPattern(hit + 1, rest, p.b) ? nullptr : hit;
}

void* CallTo(uint8_t* e8) { return e8[0] == 0xE8 ? e8 + 5 + *(int32_t*)(e8 + 1) : nullptr; }

void* JumpTo(uint8_t* j) {  // the target of a jcc/jmp (rel32 or rel8)
    if (j[0] == 0x0F && (j[1] & 0xF0) == 0x80) return j + 6 + *(int32_t*)(j + 2);
    if ((j[0] & 0xF0) == 0x70 || j[0] == 0xEB) return j + 2 + *(int8_t*)(j + 1);
    if (j[0] == 0xE9) return j + 5 + *(int32_t*)(j + 1);
    return nullptr;
}

void SetSite(int idx, uint8_t* at, int len) {
    g_sites[idx].at = at;
    g_sites[idx].skipTo = at ? at + len : nullptr;
}

void SetSiteTo(int idx, uint8_t* at, void* to) {  // K goes on at `to` (a branch's target)
    g_sites[idx].at = at && to && InImage((uintptr_t)to) ? at : nullptr;
    g_sites[idx].skipTo = to;
}

void FindSites() {
    void* mBNation = dl::Method("TfrmBuildCamp", "GetBNation");
    // ComputeExpenses: `call TList.Get; mov ebx,eax; fild [FS]; fmul [1.5]; fild [ebx+BaseResources]` opens the loop body of
    // nation esi (in ebx); `mov eax,[FS]; lea eax,[eax+eax*4] x2; add [ebx+Funds],eax; inc esi; cmp esi,9; jne` closes it;
    // `mov eax,ebx; call GetMonthlyBalance; add [ebx+Funds],eax` is in between.
    void* ce = dl::Method("TfrmBuildCamp", "ComputeExpenses");
    if (uint8_t* h = Unique(ce, 0x200, Pat().o({0x8B, 0xD8, 0xDB, 0x05, X, X, X, X, 0xD8, 0x0D, X, X, X, X, 0xDB, 0x83}).d(fBR))) {
        uint8_t* at = h + 2;
        uint32_t fs = *(uint32_t*)(at + 2);
        uint8_t* e = Unique(at, 0x100, Pat().o({0xA1}).d((int)fs).o({0x8D, 0x04, 0x80, 0x8D, 0x04, 0x80, 0x01, 0x83}).d(fFunds).o(
                                              {0x46, 0x83, 0xFE, 0x09, 0x0F, 0x85}));
        uint8_t* c = e ? Unique(at, (size_t)(e - at), Pat().o({0x8B, 0xC3, 0xE8, X, X, X, X, 0x01, 0x83}).d(fFunds)) : nullptr;
        if (InImage(fs) && e && c && CallTo(c + 2) == mBalance) {
            fsCell = (int*)(uintptr_t)fs;
            g_sites[sExpenses].at = at;
            g_sites[sExpenses].skipTo = e + 17;  // inc esi
        }
    }
    // CheckBBNumbers(Self, aNation = EDX); its only caller is CheckTurnEvents' loop over nations 1..8.
    g_sites[sBB].at = (uint8_t*)dl::Method("TfrmBuildCamp", "CheckBBNumbers");
    // ReduceTimeLimits' "AI funding" loop (nation edi), January cuts (nation eax) and the date.
    void* rtl = dl::Method("TfrmBuildCamp", "ReduceTimeLimits");
    // keep-up: `jmp; cmp edi,[ebx+PlayerNation]; je (the player: none); mov eax,[edi+BM]; mov edx,[ebx+PlayerNation];
    // cmp eax,[edx+BM]`
    if (uint8_t* h = Unique(rtl, 0x2000, Pat().o({0xE9, X, X, X, X, 0x3B, 0xBB}).d(fPlayer).o({0x0F, 0x84, X, X, X, X, 0x8B, 0x87})
                                             .d(fBM).o({0x8B, 0x93}).d(fPlayer).o({0x3B, 0x82}).d(fBM)))
        SetSite(sKeepUp, h + 5, 6);
    // preeminence: `jg; cmp edi,[ebx+PlayerNation]; jne (the computer's branch); cmp [edi+UnrestLevel],3`
    if (uint8_t* h = Unique(rtl, 0x2000, Pat().o({0x0F, 0x8F, X, X, X, X, 0x3B, 0xBB}).d(fPlayer).o({0x75, X, 0x83, 0xBF}).d(fUnrest).o({0x03})))
        SetSite(sPreem, h + 6, 6);
    // the player's branch's +1: `cmp eax,edx; jge; inc [edi+BM]; mov ecx,<its intel report>`
    if (uint8_t* h = Unique(rtl, 0x2000, Pat().o({0x3B, 0xC2, 0x0F, 0x8D, X, X, X, X, 0xFF, 0x87}).d(fBM).o({0xB9})))
        SetSite(sPreemInc, h + 8, 6);
    // January (Month 1), nations 1..8: `cmp eax,6|9; jge; mov edx,esi; mov eax,ebx; call GetBNation; dec [eax+BM]`
    for (int k = 0; k < 2; k++) {
        Pat p = Pat().o({0x83, 0xF8, k ? 0x09 : 0x06, 0x7D, X, 0x8B, 0xD6, 0x8B, 0xC3, 0xE8, X, X, X, X, 0xFF, 0x88}).d(fBM);
        if (k) p.o({0x46, 0x83, 0xFE, 0x09});
        uint8_t* h = Unique(rtl, 0x2000, p);
        if (h && CallTo(h + 9) == mBNation) SetSite(k ? sJanuary2 : sJanuary1, h + 14, 6);
    }
    // the date: `mov eax,[MonthCell]; cmp dword [eax],1; jne; mov esi,1` and `mov eax,[YearCell]; cmp dword [eax],1898`
    if (uint8_t* h = Unique(rtl, 0x2000, Pat().o({0xA1, X, X, X, X, 0x83, 0x38, 0x01, 0x0F, 0x85, X, X, X, X, 0xBE, 0x01, 0x00, 0x00, 0x00})))
        if (InImage(*(uint32_t*)(h + 1))) monthCell = (int**)(uintptr_t) * (uint32_t*)(h + 1);
    if (uint8_t* h = Unique(rtl, 0x2000, Pat().o({0xA1, X, X, X, X, 0x81, 0x38, 0x6A, 0x07, 0x00, 0x00})))
        if (InImage(*(uint32_t*)(h + 1))) yearCell = (int**)(uintptr_t) * (uint32_t*)(h + 1);
    // CheckForCoups, a computer nation's coup: `mov edx,ebx; mov eax,edi; call GetBNation; inc [eax+BM]; mov eax,edi;
    // call SetValues`
    if (uint8_t* h = Unique(dl::Method("TfrmBuildCamp", "CheckForCoups"), 0x1000,
                            Pat().o({0x8B, 0xD3, 0x8B, 0xC7, 0xE8, X, X, X, X, 0xFF, 0x80}).d(fBM).o({0x8B, 0xC7, 0xE8})))
        if (CallTo(h + 4) == mBNation && CallTo(h + 17) == dl::Method("TfrmBuildCamp", "SetValues")) SetSite(sCoup, h + 9, 6);
    // CheckAITensionEvents (nation esi; the declared-on nation eax)
    void* ate = dl::Method("TfrmBuildCamp", "CheckAITensionEvents");
    // at war with a computer nation: `add edx,[esi+UnrestLevel]; cmp eax,edx; jle +6; inc [esi+BM]`
    if (uint8_t* h = Unique(ate, 0x1000, Pat().o({0x03, 0x96}).d(fUnrest).o({0x3B, 0xC2, 0x7E, 0x06, 0xFF, 0x86}).d(fBM)))
        SetSite(sAIWar, h + 10, 6);
    // arms race: `cmp ebx,eax; jge +6; inc [esi+BM]`
    if (uint8_t* h = Unique(ate, 0x1000, Pat().o({0x3B, 0xD8, 0x7D, 0x06, 0xFF, 0x86}).d(fBM))) SetSite(sArmsRace, h + 4, 6);
    // a war declared between computer nations: `cmp [esi+BM],20; jge +6; inc [esi+BM]`, then `cmp [eax+BM],20; jge;
    // mov edx,ebx; mov eax,[form]; call GetBNation; inc [eax+BM]`
    if (uint8_t* h = Unique(ate, 0x1000, Pat().o({0x83, 0xBE}).d(fBM).o({0x14, 0x7D, 0x06, 0xFF, 0x86}).d(fBM)))
        SetSite(sAIWarStart1, h + 9, 6);
    if (uint8_t* h = Unique(ate, 0x1000, Pat().o({0x83, 0xB8}).d(fBM).o({0x14, 0x7D, X, 0x8B, 0xD3, 0xA1, X, X, X, X, 0xE8, X, X, X, X, 0xFF, 0x80}).d(fBM)))
        if (CallTo(h + 16) == mBNation) SetSite(sAIWarStart2, h + 21, 6);
    // CheckForWar, the host's nation went to war: `cmp [eax+Tension],40; jge; mov edx,[ebp-0x10]; mov eax,esi;
    // call GetBNation; inc [eax+BM]` for nations 1..8
    void* cfw = dl::Method("TfrmBuildCamp", "CheckForWar");
    if (uint8_t* h = Unique(cfw, 0x1000, Pat().o({0x83, 0xB8}).d(fTension).o({0x28, 0x7D, X, 0x8B, 0x55, 0xF0, 0x8B, 0xC6, 0xE8, X, X, X, X, 0xFF, 0x80}).d(fBM)))
        if (CallTo(h + 14) == mBNation) SetSite(sHostWar, h + 19, 6);
    // SpySuccess, the host's spies (SpyingNationIdx 0): `mov edx,edi; mov eax,ebx; call GetBNation; cmp [eax+Tension],7;
    // jle` -> the report "secretly spending an additional 10%" and `add [eax+Funds],esi` (ModBudget / 5)
    void* spy = dl::Method("TfrmBuildCamp", "SpySuccess");
    if (uint8_t* h = Unique(spy, 0x1A00, Pat().o({0x8B, 0xD7, 0x8B, 0xC3, 0xE8, X, X, X, X, 0x83, 0xB8}).d(fTension).o({0x07, 0x7E, X})))
        if (CallTo(h + 4) == mBNation && Unique(h, 0x80, Pat().o({0x01, 0xB0}).d(fFunds))) SetSite(sSpy, h + 9, 7);
    // the computer's deficit relief (ReduceTimeLimits' loop of the computer's nations): DeHaltAllShips(Self = the nation),
    // HaltMostExpensiveShip(Self, aNation = EDX)
    g_sites[sDeHalt].at = (uint8_t*)dl::Method("TBuilderNation", "DeHaltAllShips");
    g_sites[sHalt].at = (uint8_t*)dl::Method("TfrmBuildCamp", "HaltMostExpensiveShip");
    // maintenance: `mov eax,[form]; cmp <nation>,[eax+PlayerNation]; je` (the player pays the full price)
    if (uint8_t* h = Unique(dl::Method("TBuilderNation", "GetNavalAcademyCost"), 0x80,
                            Pat().o({0xA1, X, X, X, X, 0x3B, 0xB0}).d(fPlayer).o({0x74, 0x02, 0x33, 0xDB})))
        SetSite(sAcademy, h + 5, 6);
    if (uint8_t* h = Unique(dl::Method("TBuilderNation", "ApplyAirMaintenanceModifiers"), 0x100,
                            Pat().o({0xA1, X, X, X, X, 0x3B, 0xB0}).d(fPlayer).o({0x74, X, 0xDB, 0x03})))
        SetSite(sAir, h + 5, 6);
    // a light carrier (TShipType 12) with aircraft: `cmp byte [ebx+Type],12; jne; cmp [ebx+AircraftCapacity],0; jle;
    // mov eax,[form]; cmp edi,[eax+PlayerNation]; je`
    if (uint8_t* h = Unique(dl::Method("TBuilderNation", "GetShipMaintenanceCost"), 0x500,
                            Pat().o({0x80, 0x7B, X, 0x0C, 0x75, X, 0x83, 0xBB, X, X, X, X, 0x00, 0x7E, X, 0xA1, X, X, X, X, 0x3B, 0xB8})
                                .d(fPlayer).o({0x74})))
        SetSite(sLightCarrier, h + 20, 6);
    // K's army offensive: ReduceTimeLimits' `cmp [ebx+ArmyOffensive],0; je; mov eax,ebx; call ResolveArmyOffensive` (K's
    // months are counted down there, then the instruction runs); GetModifiedBudget's `mov eax,[form];
    // cmp [eax+ArmyOffensive],0; jle; fild [ebp-8]; fld tbyte [0.9]` (the nation in ebx): K takes the x 0.9 after the jle.
    if (uint8_t* h = Unique(rtl, 0x2000, Pat().o({0x83, 0xBB}).d(fArmyOff).o({0x00, 0x74, X, 0x8B, 0xC3, 0xE8})))
        if (CallTo(h + 11) == dl::Method("TfrmBuildCamp", "ResolveArmyOffensive")) SetSite(sArmyStep, h, 7);
    if (uint8_t* h = Unique(mModBudget, 0x200,
                            Pat().o({0xA1, X, X, X, X, 0x83, 0xB8}).d(fArmyOff).o({0x00, 0x7E, X, 0xDB, 0x45, 0xF8, 0xDB, 0x2D})))
        if (JumpTo(h + 12) == h + 33 && h[23] == 0xDE && h[25] == 0xE8) SetSite(sArmyBudget, h + 5, 9);
    // CheckSubmarineActivity, K's merchant ships sunk (the victim in ebx): `mov eax,[ebp-4]; cmp ebx,[eax+PlayerNation]; je`
    // past the computer's escorts (AIBuildShip: K would pay), ASW patrols and ASW research priority, three times
    void* csa = dl::Method("TfrmBuildCamp", "CheckSubmarineActivity");
    Pat asw = Pat().o({0x8B, 0x45, 0xFC, 0x3B, 0x98}).d(fPlayer).o({0x0F, 0x84, X, X, X, X});
    if (uint8_t* h = Unique(csa, 0x2400, Pat(asw).o({0x33, 0xD2, 0x8B, 0xC3, 0xE8}))) SetSite(sASW1, h + 3, 6);
    if (uint8_t* h = Unique(csa, 0x2400, Pat(asw).o({0xB8, 0x0A, 0x00, 0x00, 0x00, 0xE8}))) SetSite(sASW2, h + 3, 6);
    if (uint8_t* h = Unique(csa, 0x2400, Pat(asw).o({0xA1, X, X, X, X, 0xBA, 0x56, 0x55, 0x55, 0x55, 0xF7, 0xEA})))
        SetSite(sASW3, h + 3, 6);
    // BuyTechnology's partner (up to 4 tries of a random nation): `mov edi,eax; inc edi; mov edx,edi; mov eax,esi;
    // call GetBNation; cmp [eax+Tension],7; jge` (the partner in eax): K goes on with the next try, the target of the jle
    // after `cmp [eax+Allied],0` (`dec [ebp-0x14]; jne`).
    void* btc = dl::Method("TfrmBuildCamp", "BuyTechnology");
    int fAllied = dl::Field("TBuilderNation", "Allied");
    uint8_t* bh = Unique(btc, 0xE00, Pat().o({0x8B, 0xF8, 0x47, 0x8B, 0xD7, 0x8B, 0xC6, 0xE8, X, X, X, X, 0x83, 0xB8})
                                          .d(fTension).o({0x07, 0x7D}));
    uint8_t* bn = fAllied < 0 ? nullptr : Unique(btc, 0xE00, Pat().o({0x83, 0xB8}).d(fAllied).o(
                                                                  {0x00, 0x0F, 0x8E, X, X, X, X, 0xB8, 0xDC, 0x00, 0x00, 0x00, 0xE8}));
    if (bh && bn && CallTo(bh + 7) == mBNation) {
        uint8_t* next = (uint8_t*)JumpTo(bn + 7);
        if (next && InImage((uintptr_t)next) && next[0] == 0xFF && next[1] == 0x4D && next[3] == 0x0F && next[4] == 0x85)
            SetSiteTo(sTechPartner, bh + 12, next);
    }
    // A scrapped ship sold on (ScrapShip's second check, unnamed): the buyers 8..1, `mov eax,[ebp-4]; cmp [eax+Funds],10;
    // jle next` (the buyer in eax): K buys nothing (the next buyer).
    if (uint8_t* h = Unique(dl::Method("TfrmBuildCamp", "ScrapShip"), 0x400,
                            Pat().o({0x8B, 0xCB, 0x8B, 0xD7, 0x8B, 0xC6, 0xE8, X, X, X, X, 0x84, 0xC0, 0x74, 0x0D, 0x33, 0xC0,
                                     0x5A, 0x59, 0x59, 0x64, 0x89, 0x10, 0xE9, X, X, X, X, 0x8B, 0xCB, 0x8B, 0xD7, 0x8B, 0xC6,
                                     0xE8}))) {
        uint8_t* sell = (uint8_t*)CallTo(h + 34);
        if (InImage((uintptr_t)sell))
            if (uint8_t* s = Unique(sell, 0x400, Pat().o({0x8B, 0x45, 0xFC, 0x83, 0xB8}).d(fFunds).o(
                                                     {0x0A, 0x0F, 0x8E, X, X, X, X, 0xBF, 0x0A, 0x00, 0x00, 0x00})))
                SetSiteTo(sShipBuyer, s + 3, JumpTo(s + 10));
    }
    // ScrapShipsAfterPeace (the host's peace), an armed merchant cruiser of nation ebx: `test ebx,ebx; setg al;
    // or al,[ebp-9] (the player's "Yes"); je` past ScrapShip: K keeps them (the player's "No").
    if (uint8_t* h = Unique(dl::Method("TfrmBuildCamp", "ScrapShipsAfterPeace"), 0x300,
                            Pat().o({0x85, 0xDB, 0x0F, 0x9F, 0xC0, 0x0A, 0x45, 0xF7, 0x0F, 0x84})))
        if (Unique(h - 0x20, 0x20, Pat().o({0x80, 0x78, X, 0x07, 0x75}))) SetSiteTo(sAMC, h, JumpTo(h + 8));
    // ReduceTimeLimits' treaty cut of nation esi: `dec [eax+BM]; test esi,esi; jne; mov edx,esi; mov eax,ebx;
    // call GetBNation; dec [eax+BM]` (the player's second) and the player's message, or the computer's
    // TensionChangeWithMessage; both jump to the rule's end.
    void* tcwm = dl::Method("TfrmBuildCamp", "TensionChangeWithMessage");
    uint8_t* th = Unique(rtl, 0x2000, Pat().o({0xFF, 0x88}).d(fBM).o({0x85, 0xF6, 0x75, 0x0F, 0x8B, 0xD6, 0x8B, 0xC3, 0xE8, X, X,
                                                                     X, X, 0xFF, 0x88}).d(fBM).o({0x85, 0xF6, 0x75, X, 0x6A, 0x01, 0x68}));
    uint8_t* te = Unique(rtl, 0x2000, Pat().o({0x68, X, X, X, X, 0x8B, 0xD6, 0x8B, 0xC3, 0xE8, X, X, X, X, 0x8B, 0xD0, 0x83, 0xC9,
                                               0xFF, 0xA1, X, X, X, X, 0xE8, X, X, X, X, 0xE9}));
    if (th && te && te > th && te - th < 0x100 && CallTo(th + 14) == mBNation && tcwm && CallTo(te + 24) == tcwm)
        SetSiteTo(sTreatyCut, th, JumpTo(te + 29));
    // CheckChineseConcessions, China (esi) and the computer's upkeep of it: `test esi,esi; je; mov eax,[ebp-4];
    // cmp esi,[eax+PlayerNation]; je` past BaseResources x 0.99 and BudgetModifier -1: K as China is the player there.
    if (uint8_t* h = Unique(dl::Method("TfrmBuildCamp", "CheckChineseConcessions"), 0x1000,
                            Pat().o({0x85, 0xF6, 0x0F, 0x84, X, X, X, X, 0x8B, 0x45, 0xFC, 0x3B, 0xB0}).d(fPlayer).o(
                                {0x0F, 0x84, X, X, X, X, 0xA1, X, X, X, X, 0x8B, 0x00, 0x83, 0xE8, 0x04})))
        SetSite(sChina, h + 11, 6);
}

// A Delphi string literal (code page 1200, 2-byte characters, reference count -1) an instruction in [fn, fn + len) refers
// to, whose text starts with (or is) `match`.
const wchar_t* FindLiteral(uint8_t* fn, size_t len, const wchar_t* match, bool exact, int* outLen) {
    size_t ml = wcslen(match);
    __try {
        for (size_t i = 0; fn && i + 4 <= len; i++) {
            uint32_t v = *(uint32_t*)(fn + i);
            if (v < 12 || !InImage(v - 12) || !InImage(v)) continue;
            const uint8_t* h = (const uint8_t*)(uintptr_t)(v - 12);
            if (*(const uint32_t*)h != 0x000204B0 || *(const int32_t*)(h + 4) != -1) continue;
            int32_t n = *(const int32_t*)(h + 8);
            if (n <= 0 || n > 4000 || (size_t)n < ml || (exact && (size_t)n != ml) || !InImage(v + 2 * n)) continue;
            if (wmemcmp((const wchar_t*)(uintptr_t)v, match, ml) != 0) continue;
            *outLen = n;
            return (const wchar_t*)(uintptr_t)v;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// Each pattern found in [fn, fn + len) (the first hit of each in `hits`); the first one missing named in `why`.
bool AllFound(void* fn, size_t len, const std::vector<Pat>& pats, std::vector<uint8_t*>* hits, std::string& why,
              const char* routine) {
    for (size_t i = 0; i < pats.size(); i++) {
        uint8_t* h = fn ? dl::FindPattern((uint8_t*)fn, len, pats[i].b) : nullptr;
        if (hits) hits->push_back(h);
        if (!h) {
            why += std::string(why.empty() ? "" : ", ") + routine + " code " + std::to_string(i + 1);
            return false;
        }
    }
    return true;
}

// The reproduced routines (CheckPlayerFunds, CheckTurnEvents, SmallFleetCheck): their code as these rules expect it, the
// helpers they call (from their call sites), the cells and constants, and the texts. Sets g_fundsOn / g_eventsOn ready
// (the hooks follow in Install).
void FindRules(std::string& why, int& texts) {
    void* cpf = dl::Method("TfrmBuildCamp", "CheckPlayerFunds");
    void* cte = dl::Method("TfrmBuildCamp", "CheckTurnEvents");
    void* sfc = dl::Method("TfrmBuildCamp", "SmallFleetCheck");
    void* rtl = dl::Method("TfrmBuildCamp", "ReduceTimeLimits");
    void* mBNation = dl::Method("TfrmBuildCamp", "GetBNation");
    void* mMostTense = dl::Method("TfrmBuildCamp", "GetMostTenseNationIndex");
    mMoraleLoss = dl::Method("TfrmBuildCamp", "MoraleLoss");
    mLeaderName = dl::Method("TdlgEventAnswer", "InsertLeaderName");
    mLeaderTitle = dl::Method("TBuilderNation", "GetLeaderTitle");
    mRating = dl::Method("TfrmBuildCamp", "GetNationEconomicRating");
    mSmallestBB = dl::Method("TfrmBuildCamp", "SmallestBBFleet");
    mSmallestCA = dl::Method("TfrmBuildCamp", "SmallestCAFleet");
    mStrength = dl::Method("TBuilderNation", "GetTotalStrength");
    int fPD = dl::Field("TfrmBuildCamp", "ParliamentDelay");
    int fNotice = dl::Field("TfrmBuildCamp", "ReductionInArmamentsMessage");
    bool fields = fPrestige >= 0 && fCorruption >= 0 && fGov >= 0 && fBombastic >= 0 && fDispLimit >= 0 &&
                  fNationName >= 0 && fName2 >= 0 && fArmyOff >= 0 && fTreatyTime >= 0 && fTreatyDisp >= 0 && fPD >= 0 &&
                  fNotice >= 0;
    if (!fields) why += "fields";
    // System._UStrClr: GetModifiedBudget's epilogue `lea eax,[ebp-4]; call _UStrClr; pop eax; jmp eax`, by its code
    if (uint8_t* h = Unique(mModBudget, 0x220, Pat().o({0x8D, 0x45, 0xFC, 0xE8, X, X, X, X, 0x58, 0xFF, 0xE0}))) {
        uint8_t* c = (uint8_t*)CallTo(h + 3);
        if (InImage((uintptr_t)c) && Unique(c, 24, Pat().o({0x8B, 0x10, 0x85, 0xD2, 0x74, X, 0xC7, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                            0x8B, 0x4A, 0xF8, 0x49, 0x7C, X, 0xF0, 0xFF, 0x4A, 0xF8})))
            mUStrClr = c;
    }
    if (!mUStrClr) Log("economy: _UStrClr not found (the rules' strings are not released)");
    const int F = fFunds, P = fPlayer, AO = fArmyOff, BM = fBM, T = fTension;
    std::vector<uint8_t*> h, he, hs;
    // CheckPlayerFunds
    bool funds = fields && mMoraleLoss && mLeaderName && fsCell &&
                 AllFound(cpf, 0x800, {
                     Pat().o({0x8B, 0x83}).d(P).o({0x83, 0xB8}).d(F).o({0x00, 0x0F, 0x8D}),  // Funds < 0
                     Pat().o({0x83, 0xB8}).d(F).o({0xCE, 0x7F, X, 0xB8, 0x64, 0, 0, 0, 0xE8}),  // > -50, R(100)
                     Pat().o({0x81, 0xB8}).d(F).o({0x18, 0xFC, 0xFF, 0xFF, 0x7E, X, 0xB8, 0x64, 0, 0, 0, 0xE8}),  // > -1000
                     Pat().o({0x83, 0x3D, X, X, X, X, 0xFE, 0x0F, 0x8D}),  // War < -2
                     Pat().o({0x83, 0x3D, X, X, X, X, 0xFC, 0x7D}),        // War < -4
                     Pat().o({0x8B, 0x93}).d(P).o({0x8B, 0xC3, 0xE8}),     // MoraleLoss(P)
                     Pat().o({0x05}).d(fPrestige).o({0x33, 0xC9, 0xBA, 0x01, 0, 0, 0, 0xE8}),  // DecMin(Prestige, 1, 0)
                     Pat().o({0x83, 0xB8}).d(fCorruption).o({0x64, 0x7E, X, 0xC7, 0x80}).d(fCorruption).o({0x64, 0, 0, 0}),
                     Pat().o({0xB8, 0x2C, 0x01, 0, 0, 0x2B, 0xC6, 0xE8, X, X, X, X, 0x83, 0xF8, 0x0A, 0x0F, 0x8D}),  // R(300-C)
                     Pat().o({0x83, 0x3D, X, X, X, X, 0x00, 0x7E, X, 0xB8, 0x64, 0, 0, 0, 0xE8, X, X, X, X, 0x8B, 0x15, X, X,
                              X, X, 0x83, 0xC2, 0x46, 0x3B, 0xC2, 0x0F, 0x8D}),  // R(100) < War + 70
                     Pat().o({0x81, 0xB8}).d(F).o({0xA0, 0x0F, 0, 0, 0x7E, X, 0xB8, 0x2C, 0x01, 0, 0, 0xE8}),  // > 4000; R(300)
                     Pat().o({0x81, 0xA8}).d(F).o({0xE8, 0x03, 0, 0}),  // Funds -= 1000
                     Pat().o({0xDB, 0x45, 0xEC, 0xDB, 0x2D, X, X, X, X, 0xDE, 0xC9, 0xD8, 0x05, X, X, X, X, 0xDB, 0x86}).d(F).o(
                         {0xDA, 0xE9, 0x9B, 0xDF, 0xE0, 0x9E, 0x0F, 0x86}),  // Funds > budget * 0.6 + 20000
                     Pat().o({0x8B, 0xC3, 0xE8, X, X, X, X, 0x8B, 0xD0, 0x8B, 0xC3, 0xE8, X, X, X, X, 0x83, 0xB8}).d(T).o(
                         {0x06, 0x0F, 0x8D}),  // the most tense nation's tension < 6
                     Pat().o({0x83, 0xBB}).d(AO).o({0x00, 0x0F, 0x85, X, X, X, X, 0xB8, 0x0A, 0, 0, 0, 0xE8, X, X, X, X, 0x48, 0x74}),
                     Pat().o({0xA1, X, X, X, X, 0x81, 0x38, 0x6F, 0x07, 0x00, 0x00, 0x0F, 0x8D}),  // Year < 1903
                     Pat().o({0xB8, 0x03, 0, 0, 0, 0xE8, X, X, X, X, 0x83, 0xC0, 0x04, 0x89, 0x83}).d(AO),  // AO := R(3)+4
                     Pat().o({0x80, 0xB8}).d(fBombastic).o({0x00, 0x75, X, 0x80, 0xB8}).d(fGov).o({0x02, 0x75}),
                     Pat().o({0xA1, X, X, X, X, 0x03, 0xC0, 0x69, 0xF0, 0xE8, 0x03, 0x00, 0x00}),  // 2000 x FS
                     Pat().o({0x29, 0xB0}).d(F).o({0x8B, 0x83}).d(P).o({0xFF, 0x88}).d(BM).o({0xC7, 0x83}).d(AO).o({0x0C, 0, 0, 0}),
                     Pat().o({0x8B, 0x15}).d((int)(uintptr_t)fsCell).o({0xC1, 0xE2, 0x02, 0x69, 0xD2, 0x10, 0x27, 0, 0, 0x3B, 0xC2}),
                     Pat().o({0xB8, 0x05, 0, 0, 0, 0xE8, X, X, X, X, 0x48, 0x0F, 0x85}),  // R(5) == 1
                     Pat().o({0xBA, 0x67, 0x66, 0x66, 0x66, 0xF7, 0xEA, 0xC1, 0xFA, 0x02, 0x8B, 0xC2, 0xC1, 0xE8, 0x1F, 0x03,
                              0xC2, 0x29, 0x81}).d(F).o({0x8B, 0x83}).d(P).o({0xFF, 0x88}).d(BM).o({0xC7, 0x83}).d(AO).o({0x08, 0, 0, 0}),
                     Pat().o({0xA1, X, X, X, X, 0x8B, 0x00, 0x33, 0xC9, 0xBA, X, X, X, X, 0xE8}),  // InsertLeaderName(text, 0)
                 }, &h, why, "CheckPlayerFunds");
    if (funds) {
        c06 = (const void*)(uintptr_t) * (uint32_t*)(h[12] + 5);
        c20000 = (const void*)(uintptr_t) * (uint32_t*)(h[12] + 13);
        dlgCell = (void**)(uintptr_t) * (uint32_t*)(h[23] + 1);
        funds = CallTo(h[5] + 8) == mMoraleLoss && CallTo(h[12] - 8) == mModBudget && CallTo(h[13] + 2) == mMostTense &&
                CallTo(h[13] + 11) == mBNation && *(uint32_t*)(h[18] + 1) == (uint32_t)(uintptr_t)fsCell &&
                CallTo(h[23] + 14) == mLeaderName && InImage((uintptr_t)c06) && InImage((uintptr_t)c20000) &&
                InImage((uintptr_t)dlgCell) && *(const float*)c20000 == 20000.0f;
        if (!funds) why += std::string(why.empty() ? "" : ", ") + "CheckPlayerFunds helpers";
    }
    // CheckTurnEvents and SmallFleetCheck
    bool events = fields && mLeaderName && mLeaderTitle && mRating && mSmallestBB && mSmallestCA && mStrength && fsCell &&
                  AllFound(cte, 0x900, {
                      Pat().o({0x81, 0x38, 0x6C, 0x07, 0x00, 0x00, 0x75, X, 0xA1, X, X, X, X, 0x83, 0x38, 0x02, 0x74}),  // 1900
                      Pat().o({0x81, 0x38, 0x62, 0x07, 0x00, 0x00, 0x75, X, 0xA1, X, X, X, X, 0x83, 0x38, 0x02, 0x74}),  // 1890
                      Pat().o({0x81, 0x38, 0x80, 0x07, 0x00, 0x00, 0x75, X, 0xA1, X, X, X, X, 0x83, 0x38, 0x02, 0x75}),  // 1920
                      Pat().o({0x83, 0x3D, X, X, X, X, 0x00, 0x74, X, 0x83, 0x3D, X, X, X, X, 0xFF, 0x75}),  // War 0, -1
                      Pat().o({0xB8, 0x04, 0, 0, 0, 0xE8, X, X, X, X, 0x83, 0xC0, 0x02, 0x3B, 0x05, X, X, X, X, 0x0F, 0x8D}),
                      Pat().o({0x8B, 0x0D, X, X, X, X, 0xB8, 0x56, 0x55, 0x55, 0x55, 0xF7, 0xE9, 0x8B, 0xC2, 0xC1, 0xE8, 0x1F,
                               0x03, 0xD0, 0x8D, 0x14, 0x52, 0x2B, 0xCA, 0x49, 0x0F, 0x85}),  // War mod 3 = 1
                      Pat().o({0x83, 0xB8}).d(fUnrest).o({0x08, 0x0F, 0x8D, X, X, X, X, 0x8B, 0x83}).d(P).o({0x83, 0xB8}).d(BM).o(
                          {0x16, 0x0F, 0x8D, X, X, X, X, 0xB8, 0x10, 0, 0, 0, 0xE8, X, X, X, X, 0x83, 0xC0, 0x0A, 0x8B, 0x93}).d(P).o(
                          {0x3B, 0x82}).d(BM).o({0x0F, 0x8E}),
                      Pat().o({0x0F, 0xB6, 0x80}).d(fGov).o({0x04, 0xFC, 0x2C, 0x02, 0x73, X, 0xB8, 0x64, 0, 0, 0, 0xE8, X, X, X,
                                                             X, 0x83, 0xF8, 0x50, 0x7D}),  // governments 4, 5; R(100) < 80
                      Pat().o({0x68, X, X, X, X, 0x8D, 0x55, 0xF0, 0x8B, 0x83}).d(P).o({0xE8}),  // "The " + GetLeaderTitle
                      Pat().o({0x8B, 0x83}).d(P).o({0xFF, 0x80}).d(BM).o({0x83, 0x3D, X, X, X, X, 0x04, 0x0F, 0x8E}),  // BM++
                  }, &he, why, "CheckTurnEvents");
    if (events) {
        bool sfcCall = false;  // the peace branch: `call War>0; test al,al; jne; mov eax,ebx; call SmallFleetCheck`
        Pat call = Pat().o({0x84, 0xC0, 0x0F, 0x85, X, X, X, X, 0x8B, 0xC3, 0xE8});
        for (uint8_t* a = (uint8_t*)cte; a && !sfcCall && (a = dl::FindPattern(a, 0x900 - (size_t)(a - (uint8_t*)cte), call.b));
             a++)
            sfcCall = CallTo(a + 10) == sfc;
        events = sfcCall && CallTo(he[8] + 14) == mLeaderTitle;
        if (!events) why += std::string(why.empty() ? "" : ", ") + "CheckTurnEvents helpers";
    }
    if (events) {
        events = AllFound(sfc, 0x1000, {
            Pat().o({0xB8, 0x08, 0, 0, 0, 0xE8, X, X, X, X, 0x48, 0x0F, 0x85}),  // R(8) == 1
            Pat().o({0x8B, 0x93}).d(P).o({0x8B, 0xC3, 0xE8, X, X, X, X, 0x83, 0xF8, 0x04, 0x0F, 0x8D}),  // rating < 4
            Pat().o({0x8B, 0x93}).d(P).o({0xB1, 0x01, 0x8B, 0xC3, 0xE8, X, X, X, X, 0x84, 0xC0, 0x0F, 0x84}),  // SmallestBBFleet
            Pat().o({0x83, 0xB8}).d(fDispLimit).o({0x00, 0x0F, 0x85, X, X, X, X, 0x83, 0xBB}).d(fTreatyTime).o({0x00, 0x0F, 0x8F}),
            Pat().o({0x8B, 0x83}).d(P).o({0xFF, 0x88}).d(fPrestige).o({0xC6, 0x45, 0xEF, 0x01}),
            Pat().o({0xA1, X, X, X, X, 0x81, 0x38, 0x7E, 0x07, 0, 0, 0x0F, 0x8D, X, X, X, X, 0x83, 0xBB}).d(fTreatyTime).o(
                {0x00, 0x0F, 0x8F}),  // Year < 1918
            Pat().o({0xB8, 0x64, 0, 0, 0, 0xE8, X, X, X, X, 0x83, 0xF8, 0x32, 0x7D, X, 0x8B, 0x83}).d(P).o({0xFF, 0x88}).d(fPrestige),
            Pat().o({0x83, 0xBB}).d(fPD).o({0x00, 0x0F, 0x8F}),
            Pat().o({0x83, 0xB8}).d(T).o({0x07, 0x0F, 0x8E, X, X, X, X, 0x8B, 0x83}).d(P).o({0x83, 0xB8}).d(BM).o(
                {0x12, 0x0F, 0x8D, X, X, X, X, 0xB8, 0x14, 0, 0, 0, 0xE8, X, X, X, X, 0x8B, 0x93}).d(P).o({0x3B, 0x82}).d(BM).o(
                {0x0F, 0x8E, X, X, X, X, 0x8B, 0x83}).d(P).o({0x8B, 0x80}).d(F).o({0x69, 0x15}).d((int)(uintptr_t)fsCell).o(
                {0x88, 0x13, 0, 0, 0x3B, 0xC2, 0x0F, 0x8D}),  // D1
            Pat().o({0xE8, X, X, X, X, 0x89, 0x45, 0xE8, 0xDB, 0x45, 0xE8, 0xDB, 0x7D, 0xDC, 0x9B, 0x8B, 0x83}).d(P).o(
                {0xE8, X, X, X, X, 0x89, 0x45, 0xD8, 0xDB, 0x45, 0xD8, 0xDB, 0x2D, X, X, X, X, 0xDE, 0xC9, 0xDB, 0x6D, 0xDC, 0xDA,
                 0xE9, 0x9B, 0xDF, 0xE0, 0x9E, 0x0F, 0x86}),  // GetTotalStrength(MT) > 0.7 x GetTotalStrength(P)
            Pat().o({0x83, 0x3D, X, X, X, X, 0xDC, 0x7E, X, 0x8B, 0xC3, 0xE8, X, X, X, X, 0x8B, 0xD0, 0x8B, 0xC3, 0xE8, X, X,
                     X, X, 0xFF, 0x88}).d(T).o({0xE9}),  // War > -36: MT.Tension--
            Pat().o({0xB8, 0x96, 0x07, 0, 0, 0xE8, X, X, X, X, 0x8B, 0x15, X, X, X, X, 0x3B, 0x02, 0x7E}),  // AdjYear(1942)
            Pat().o({0xFF, 0x80}).d(BM).o({0xB8, 0x04, 0, 0, 0, 0xE8, X, X, X, X, 0x83, 0xC0, 0x0A, 0x89, 0x83}).d(fPD),
            Pat().o({0x80, 0xB8}).d(fGov).o({0x00, 0x0F, 0x85, X, X, X, X, 0x8B, 0xC3, 0xE8, X, X, X, X, 0x8B, 0xD0, 0x8B, 0xC3,
                                              0xE8, X, X, X, X, 0x83, 0xB8}).d(T).o({0x06, 0x0F, 0x8E, X, X, X, X, 0x8B, 0x83}).d(P).o(
                {0x83, 0xB8}).d(BM).o({0x14, 0x0F, 0x8D, X, X, X, X, 0xB8, 0x19, 0, 0, 0, 0xE8}),  // D2
            Pat().o({0x8B, 0xB0}).d(T).o({0xB8, 0x0A, 0, 0, 0, 0xE8, X, X, X, X, 0x3B, 0xF0, 0x0F, 0x8E}),  // T > R(10)
            Pat().o({0x80, 0xB8}).d(fGov).o({0x02, 0x74, X, 0x80, 0xB8}).d(fBombastic).o({0x00, 0x74}),
            Pat().o({0xFF, 0x80}).d(BM).o({0x8B, 0xC3, 0xE8, X, X, X, X, 0x8B, 0xD0, 0x8B, 0xC3, 0xE8, X, X, X, X, 0xFF, 0x80}).d(T).o(
                {0xB8, 0x04, 0, 0, 0, 0xE8, X, X, X, X, 0x83, 0xC0, 0x08, 0x89, 0x83}).d(fPD),  // BM++; MT'.Tension++
            Pat().o({0x8B, 0xB0}).d(T).o({0x83, 0xC6, 0x32, 0xB8, 0x64, 0, 0, 0, 0xE8, X, X, X, X, 0x3B, 0xF0, 0x0F, 0x8E, X, X, X,
                                          X, 0xB8, 0x40, 0x9C, 0, 0, 0xE8, X, X, X, X, 0x3B, 0x83}).d(fTreatyDisp).o({0x0F, 0x8E}),
            Pat().o({0x83, 0xB8}).d(T).o({0x06, 0x7D, X, 0x8D, 0x45, 0xFC, 0xBA}),  // D3: T < 6
            Pat().o({0xFF, 0x88}).d(BM).o({0xB8, 0x04, 0, 0, 0, 0xE8, X, X, X, X, 0x83, 0xC0, 0x0A, 0x89, 0x83}).d(fPD),
            Pat().o({0xA1, X, X, X, X, 0x81, 0x38, 0x9E, 0x07, 0, 0, 0x0F, 0x8E, X, X, X, X, 0x8B, 0x83}).d(P).o({0x83, 0xB8}).d(BM).o(
                {0x0E, 0x0F, 0x8E}),  // D4: Year > 1950, BM > 14
            Pat().o({0x83, 0xB8}).d(T).o({0x07, 0x0F, 0x8D, X, X, X, X, 0xB8, 0x28, 0, 0, 0, 0xE8, X, X, X, X, 0x8B, 0x93}).d(P).o(
                {0x3B, 0x82}).d(BM).o({0x0F, 0x8D}),  // T < 7; R(40) < BM
            Pat().o({0xFF, 0x88}).d(BM).o({0xB8, 0x04, 0, 0, 0, 0xE8, X, X, X, X, 0x83, 0xC0, 0x0C, 0x89, 0x83}).d(fPD),
            Pat().o({0xA1, X, X, X, X, 0x81, 0x38, 0x80, 0x07, 0, 0, 0x7E, X, 0x83, 0x3D, X, X, X, X, 0xFA, 0x7D, X, 0x8B, 0x83}).d(P).o(
                {0x0F, 0xB6, 0x80}).d(fGov).o({0x04, 0xFE, 0x2C, 0x04, 0x0F, 0x92, 0xC0, 0x84, 0xC0, 0x75, X, 0x80, 0xBB}).d(fNotice).o(
                {0x00, 0x75}),  // E
            Pat().o({0xA1, X, X, X, X, 0x8B, 0x00, 0x33, 0xC9, 0x8B, 0x55, 0xFC, 0xE8}),  // InsertLeaderName(t, 0)
        }, &hs, why, "SmallFleetCheck");
    }
    if (events) {
        uint8_t* second = dl::FindPattern(hs[2] + 1, 0x400, Pat().o({0x8B, 0x93}).d(P).o({0xB1, 0x01, 0x8B, 0xC3, 0xE8}).b);
        mAdjYear = CallTo(hs[11] + 5);
        c07 = (const void*)(uintptr_t) * (uint32_t*)(hs[9] + 34);
        events = CallTo(hs[1] + 8) == mRating && CallTo(hs[2] + 10) == mSmallestBB && second && CallTo(second + 10) == mSmallestCA &&
                 CallTo(hs[9]) == mStrength && CallTo(hs[9] + 21) == mStrength && InImage((uintptr_t)c07) &&
                 InImage((uintptr_t)mAdjYear) && CallTo(hs[24] + 12) == mLeaderName &&
                 (!dlgCell || *(uint32_t*)(hs[24] + 1) == (uint32_t)(uintptr_t)dlgCell);
        if (!dlgCell) dlgCell = (void**)(uintptr_t) * (uint32_t*)(hs[24] + 1);
        if (!events) why += std::string(why.empty() ? "" : ", ") + "SmallFleetCheck helpers";
    }
    // the texts
    void* const in[4] = {cpf, cte, sfc, rtl};
    static const size_t kLen[4] = {0x800, 0x900, 0x1000, 0x1A00};
    bool have[4] = {true, true, true, true};
    texts = 0;
    for (int i = 0; i < tTexts; i++) {
        int n = 0;
        const wchar_t* p = FindLiteral((uint8_t*)in[kTexts[i].in], kLen[kTexts[i].in], kTexts[i].match, kTexts[i].exact, &n);
        g_text[i] = p ? std::wstring(p, n) : std::wstring();
        if (p)
            texts++;
        else
            have[kTexts[i].in] = false;
    }
    if (!have[inFunds]) why += std::string(why.empty() ? "" : ", ") + "CheckPlayerFunds texts";
    if (!have[inEvents] || !have[inFleet]) why += std::string(why.empty() ? "" : ", ") + "CheckTurnEvents texts";
    if (!have[inLimits]) {
        why += std::string(why.empty() ? "" : ", ") + "ReduceTimeLimits texts";
        g_sites[sTreatyCut].at = nullptr;
    }
    g_fundsOn = funds && have[inFunds];
    g_eventsOn = events && have[inEvents] && have[inFleet];
}

}  // namespace

bool Install() {
    fBM = dl::Field("TBuilderNation", "BudgetModifier");
    fBR = dl::Field("TBuilderNation", "BaseResources");
    fFunds = dl::Field("TBuilderNation", "Funds");
    fTension = dl::Field("TBuilderNation", "Tension");
    fUnrest = dl::Field("TBuilderNation", "UnrestLevel");
    fPlayer = dl::Field("TfrmBuildCamp", "PlayerNation");
    fHalted = dl::Field("TCampaignShip", "Halted");
    mBalance = dl::Method("TBuilderNation", "GetMonthlyBalance");
    mModBudget = dl::Method("TBuilderNation", "GetModifiedBudget");
    fPrestige = dl::Field("TBuilderNation", "Prestige");
    fCorruption = dl::Field("TBuilderNation", "Corruption");
    fGov = dl::Field("TBuilderNation", "GovernmentType");
    fBombastic = dl::Field("TBuilderNation", "Bombastic");
    fDispLimit = dl::Field("TBuilderNation", "DisplacementLimit");
    fNationName = dl::Field("TBuilderNation", "NationName");
    fName2 = dl::Field("TBuilderNation", "Name2");
    fParlName = dl::Field("TBuilderNation", "ParliamentName");
    fArmyOff = dl::Field("TfrmBuildCamp", "ArmyOffensive");
    fTreatyTime = dl::Field("TfrmBuildCamp", "TreatyTimeRemaining");
    fTreatyDisp = dl::Field("TfrmBuildCamp", "TreatyDisplacementLimit");
    if (fBM < 0 || fBR < 0 || fFunds < 0 || fTension < 0 || fUnrest < 0 || fPlayer < 0 || !mBalance || !mModBudget ||
        !dl::Method("TfrmBuildCamp", "GetBNation")) {
        Log("economy: the player's economy rules for joined nations: UNAVAILABLE (fields or routines)");
        return false;
    }
    MH_STATUS st = MH_Initialize();  // normally done by game::InstallHooks
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        Log("economy: the player's economy rules for joined nations: UNAVAILABLE (MinHook %d)", (int)st);
        return false;
    }
    FindSites();
    std::string why;  // what of the reproduced routines did not match
    int texts = 0;
    FindRules(why, texts);
    std::string on, missing, where;
    char b[64];
    for (int i = 0; i < sSites; i++) {
        Site& s = g_sites[i];
        s.on = s.at && HookAt(s.at, EmitSiteThunk(i), &s.orig, s.name);
        if (s.on) {
            snprintf(b, sizeof(b), " %#x", (unsigned)((uintptr_t)s.at - dl::g_base));
            where += b;
        } else {
            missing += std::string(missing.empty() ? "" : ", ") + s.name;
        }
    }
    // WarStartsWithNation: the enemy's increments (IncMax(BM, 2, 30) below 30, then +1 below 22) and System.Random
    // (`mov eax,30; call Random`, `push eax; call [RandProc]` at its start)
    void* ws = dl::Method("TfrmBuildCamp", "WarStartsWithNation");
    uint8_t* inc2 = Unique(ws, 0x100, Pat().o({0x83, 0xBE}).d(fBM).o({0x1E, 0x7D, X, 0x8D, 0x86}).d(fBM).o(
                                          {0xB9, 0x1E, 0x00, 0x00, 0x00, 0xBA, 0x02, 0x00, 0x00, 0x00, 0xE8}));
    uint8_t* inc1 = Unique(ws, 0x100, Pat().o({0x83, 0xBE}).d(fBM).o({0x16, 0x7D, 0x06, 0xFF, 0x86}).d(fBM));
    uint8_t* rnd = Unique(ws, 0x100, Pat().o({0xB8, 0x1E, 0x00, 0x00, 0x00, 0xE8}));
    uint8_t* r = rnd ? (uint8_t*)CallTo(rnd + 5) : nullptr;
    if (inc2 && inc1 && r && InImage((uintptr_t)r) && r[0] == 0x50 && r[1] == 0xFF && r[2] == 0x15) {
        mRandom = r;
        g_warStartsOn = HookAt(ws, EmitWarStartsThunk(), &g_oWarStarts, "WarStartsWithNation");
    }
    if (g_warStartsOn) {
        snprintf(b, sizeof(b), " %#x", (unsigned)((uintptr_t)ws - dl::g_base));
        where += b;
    } else {
        missing += std::string(missing.empty() ? "" : ", ") + "WarStartsWithNation";
    }
    // The player's CheckPlayerFunds and CheckTurnEvents for K: their entries, after the game's own routine
    struct {
        const char* method;
        bool* ready;
        void** orig;
        void(__stdcall* cb)(void*);
    } after[2] = {{"CheckPlayerFunds", &g_fundsOn, &g_oFunds, &PlayerFundsCB},
                  {"CheckTurnEvents", &g_eventsOn, &g_oEvents, &TurnEventsCB}};
    int afterOn = 0;
    for (auto& a : after) {
        void* fn = dl::Method("TfrmBuildCamp", a.method);
        *a.ready = *a.ready && mRandom && HookAt(fn, EmitAfterThunk(a.orig, a.cb), a.orig, a.method);
        if (*a.ready) {
            afterOn++;
            snprintf(b, sizeof(b), " %#x", (unsigned)((uintptr_t)fn - dl::g_base));
            where += b;
        } else {
            missing += std::string(missing.empty() ? "" : ", ") + "the player's " + a.method;
        }
    }
    // AdvanceShipConstruction: only the record of the rules every nation shares (the game's code runs as it is)
    void* adv = dl::Method("TfrmBuildCamp", "AdvanceShipConstruction");
    g_advanceOn = adv && HookAt(adv, EmitAdvanceThunk(), &g_oAdvance, "AdvanceShipConstruction");
    if (!g_advanceOn) missing += std::string(missing.empty() ? "" : ", ") + "AdvanceShipConstruction (record only)";
    // The month's end: DoEndOfTurn's one `mov eax,ebx; call CheckForWar` (the last of these rules in a month).
    void* eot = dl::Method("TfrmBuildCamp", "DoEndOfTurn");
    void* cfw = dl::Method("TfrmBuildCamp", "CheckForWar");
    int calls = 0;
    for (int i = 2; eot && cfw && i < 0x80; i++) {
        uint8_t* p = (uint8_t*)eot + i;
        if (CallTo(p) == cfw) {
            calls++;
            g_eomAt = p;
        }
    }
    if (calls == 1 && g_eomAt[-2] == 0x8B && g_eomAt[-1] == 0xC3) {
        g_cfwEntry = cfw;
        g_eomAfter = g_eomAt + 5;
        g_eomOn = HookAt(g_eomAt, EmitMonthEndThunk(), &g_eomOrig, "DoEndOfTurn's CheckForWar");
    }
    if (!g_eomOn) missing += std::string(missing.empty() ? "" : ", ") + "the month's end (records logged later)";
    int n = 0;
    for (auto& s : g_sites) n += s.on ? 1 : 0;
    n += (g_warStartsOn ? 1 : 0) + afterOn;
    installed = n > 0;
    Log("economy: the player's economy rules for joined nations: %s (%d of %d; texts %d/%d; fleet size cell %p, date %s)%s%s"
        "%s%s",
        missing.empty() ? "ok" : (installed ? "PARTLY" : "UNAVAILABLE"), n, (int)sSites + 3, texts, (int)tTexts,
        (void*)fsCell, DateText(GameDate()).c_str(), missing.empty() ? "" : "; missing: ", missing.c_str(),
        why.empty() ? "" : "; not as expected: ", why.c_str());
    if (installed) Log("economy: hooked at rva%s", where.c_str());
    return installed;
}

namespace {

bool SafeBalances(void* n, int* bal, int* balAI) {
    __try {
        *balAI = BalanceAt(n, true);
        *bal = BalanceAt(n, false);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        aiPrices = false;
        return false;
    }
}

std::string RecordKV(const Month& r) {
    char b[700];
    snprintf(b, sizeof(b),
             " expenses=%d before=%d after=%d applied=%d appliedAI=%d subsidy=%d brFloor=%d brDecay=%d bb=%d spy=%d "
             "spyFunds=%d aiWar=%d armsRace=%d aiWarStart=%d hostWar=%d coup=%d january=%d keepUp=%d preem=%d "
             "preemApplied=%d halt=%d resume=%d warStarts=%d warGame=%d warPlayer=%d shared=%d",
             r.expenses ? 1 : 0, r.fundsBefore, r.fundsAfter, r.balance, r.balanceAI, r.subsidy, r.brFloor, r.brDecay,
             r.n[rBB], r.n[rSpy], r.spyFunds, r.n[rAIWar], r.n[rArmsRace], r.n[rAIWarStart], r.n[rHostWar], r.n[rCoup],
             r.n[rJanuary], r.n[rKeepUp], r.n[rPreem], r.n[rPreemInc], r.n[rHalt], r.n[rDeHalt], r.warStarts, r.warGame,
             r.warPlayer, r.shared);
    std::string s = b;
    // the player's rules after ComputeExpenses, K's own army offensive, the month's end, the computer's acts left out
    snprintf(b, sizeof(b),
             " ran=%d kwar=%d debt=%d debtPrestige=%d morale=%d corruption=%d hoard=%d loan=%d complaint=%d "
             "complaintPrestige=%d budget=%d budgetBM=%d tensionEdits=%s notice=%d treatyCut=%d ruleFunds=%d ruleBM=%d "
             "rulePrestige=%d news=%d kao=%d kpd=%d aoCut=%d ended=%d fundsEnd=%d otherFunds=%d asw=%d techPartner=%d "
             "shipBuyer=%d amcKept=%d china=%d",
             r.rules ? 1 : 0, r.kwar, r.debt, r.debtPrestige, r.morale ? 1 : 0, r.corruption, r.hoard, r.loan, r.complaint,
             r.complaintPrestige, r.budget, r.budgetBM, r.tension.empty() ? "-" : r.tension.c_str(), r.notice,
             r.n[rTreatyCut], r.ruleFunds, r.ruleBM, r.rulePrestige, r.news, r.kao, r.kpd, r.aoCut, r.ended ? 1 : 0,
             r.fundsEnd, r.ended && r.expenses ? r.fundsEnd - r.fundsAfter - r.ruleFunds : 0, r.n[rASW], r.n[rTechPartner],
             r.n[rShipBuyer], r.n[rAMC], r.n[rChina]);
    return s + b;
}

std::string RuleList() {
    std::set<std::string> on;
    for (auto& s : g_sites)
        if (s.on) on.insert(kRuleNames[s.rule]);
    if (g_warStartsOn) on.insert(kRuleNames[rWarStart]);
    if (g_eomOn) on.insert(kRuleNames[rMonthEnd]);
    if (g_fundsOn) on.insert(kRuleNames[rFunds]);
    if (g_eventsOn) on.insert(kRuleNames[rEvents]);
    std::string out;
    for (int i = 0; i < rRules; i++)
        if (on.count(kRuleNames[i])) out += std::string(out.empty() ? "" : ",") + kRuleNames[i];
    return out.empty() ? "none" : out;
}

}  // namespace

std::string BridgeCommand(const std::vector<std::string>& args) {
    if (!installed) return "error the economy rules are not installed";
    int K = args.size() > 1 ? atoi(args[1].c_str()) : 0;
    void* n = K >= 1 && K <= 8 && game::BuildCamp() && K < game::NationCount() ? game::Nation(K) : nullptr;
    if (!n) return "error usage: econ <nation 1..8> (a campaign open)";
    int bal = 0, balAI = 0;
    if (!SafeBalances(n, &bal, &balAI)) return "error GetMonthlyBalance failed";
    char b[400];
    snprintf(b, sizeof(b), "ok nation=%d joined=%d funds=%d bm=%d br=%d balance=%d balanceAI=%d maintenance=%d fs=%d date=%s",
             K, hookcb::IsHumanRemoteNation(K) ? 1 : 0, Field(n, fFunds), Field(n, fBM), Field(n, fBR), bal, balAI,
             balAI - bal, fsCell ? *fsCell : -1, DateText(GameDate()).c_str());
    std::string s = b;
    auto cur = g_cur.find(K);
    auto last = g_last.find(K);
    const Month* r = cur != g_cur.end() ? &cur->second : last != g_last.end() ? &last->second : nullptr;
    s += " rec=" + (r ? DateText(r->date) : std::string("none")) + " pending=" + (cur != g_cur.end() ? "1" : "0");
    if (r) s += RecordKV(*r);
    // K's own state now (the session file's), its messages not yet sent, the test switch
    auto k = g_k.find(K);
    auto nw = g_news.find(K);
    snprintf(b, sizeof(b), " aoNow=%d pdNow=%d noticeShown=%d newsPending=%d prestige=%d corruptionNow=%d econrand=%d",
             k == g_k.end() ? 0 : k->second.ao, k == g_k.end() ? 0 : k->second.pd,
             k != g_k.end() && k->second.notice ? 1 : 0, nw == g_news.end() ? 0 : (int)nw->second.size(),
             fPrestige >= 0 ? Field(n, fPrestige) : 0, fCorruption >= 0 ? Field(n, fCorruption) : 0, g_testRandom);
    return s + b + " rules=" + RuleList();
}

// "K:pd:ao:notice,..." (nations with any state)
std::string EncodeState() {
    std::string o;
    for (auto& e : g_k)
        if (e.second.pd || e.second.ao || e.second.notice)
            o += std::to_string(e.first) + ":" + std::to_string(e.second.pd) + ":" + std::to_string(e.second.ao) + ":" +
                 (e.second.notice ? "1" : "0") + ",";
    return o;
}

void DecodeState(const std::string& s) {
    g_k.clear();
    for (auto& e : Split(s, ',')) {
        auto f = Split(e, ':');
        if (f.size() != 4 || f[0].empty()) continue;
        int K = atoi(f[0].c_str());
        if (K < 1 || K > 8) continue;
        KState& k = g_k[K];
        k.pd = std::max(0, atoi(f[1].c_str()));
        k.ao = atoi(f[2].c_str());
        k.notice = f[3] == "1";
    }
}

void ClearState() {
    g_k.clear();
    g_news.clear();
}

std::vector<std::wstring> TakeNews(int K) {
    std::vector<std::wstring> out;
    auto it = g_news.find(K);
    if (it != g_news.end()) {
        out.swap(it->second);
        g_news.erase(it);
    }
    return out;
}

int ArmyOffensiveOf(int K) { return KArmy(K); }

void OnWarChanged(int K) {
    auto it = g_k.find(K);
    if (it == g_k.end() || !it->second.ao) return;
    Log("economy: nation %d's army offensive ends with its war (%d month(s) left)", K, it->second.ao);
    it->second.ao = 0;
}

std::string ClientArmyOffensive(int months) {
    void* f = game::BuildCamp();
    int off = dl::Field("TfrmBuildCamp", "ArmyOffensive");
    if (!f || off < 0) return "unavailable";
    int& ao = dl::At<int>(f, off);
    if (ao != 0 || months <= 0) return "kept " + std::to_string(ao);
    ao = months;
    return "set " + std::to_string(months);
}

std::string TestRandom(const std::vector<std::string>& args) {
    if (args.size() > 1) g_testRandom = std::max(-1, atoi(args[1].c_str()));
    Log("economy: TEST econrand %d (the player's rules for joined nations draw %s)", g_testRandom,
        g_testRandom < 0 ? "the game's Random" : "min(v, n - 1)");
    return "ok econrand=" + std::to_string(g_testRandom);
}

}  // namespace economy

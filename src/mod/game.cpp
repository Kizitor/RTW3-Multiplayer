#include "game.h"
#include "economy.h"
#include "nationplay.h"
#include "lockstep.h"
#include "../../third_party/minhook/include/MinHook.h"
#include <algorithm>
#include <climits>
#include <intrin.h>
#include <random>

namespace game {

struct Api {
    // fields
    int fBuilderNations = -1, fPlayerNation = -1, fStartBuild = -1, fBtbnTurn = -1, fBattleOpponentIdx = -1,
        fCampaignStartYear = -1;
    int fNationName = -1, fShipList = -1, fFunds = -1;
    int fShipOrderedArea = -1, fShipLocationArea = -1, fShipStatus = -1;
    int fHandle = -1, fVisible = -1, fOwner = -1, fModalLevel = -1;
    // methods
    void *mTurnClick = nullptr, *mResignClick = nullptr, *mDoEndOfTurn = nullptr, *mDoTurn2 = nullptr,
         *mStopBlockadedMoves = nullptr, *mSave = nullptr, *mLoad = nullptr, *mAdjustTechs = nullptr,
         *mEnableButtons = nullptr, *mSetNationColors = nullptr, *mUpdateUIValues = nullptr, *mFillMapTree = nullptr,
         *mLoadWarData = nullptr, *mShow = nullptr, *mHide = nullptr, *mGetSaveDirectory = nullptr,
         *mAIMoves = nullptr, *mSetValues = nullptr, *mTabChange = nullptr;
    int fTabControl1 = -1;
    int* slotVar = nullptr;
    // diplomacy
    int fTension = -1, fAIAlliance = -1, fAllied = -1;
    void *mWarStarts = nullptr, *mSetAITension = nullptr, *mSetAIAlliance = nullptr, *mDoPeace = nullptr,
         *mCountAtWar = nullptr, *mHandleAIPeace = nullptr;
    int* warVar = nullptr;                        // months at war of the host's nation (save key General/War)
    int **recYearCell = nullptr, **recMonthCell = nullptr;  // monthly history record: form + Y*1200 + M*100 + disp
    int recDisp = 0;
    bool diplomacy = false;
    // ship designs
    int fDesignList = -1, fReadyForBuild = -1, fShipName = -1;
    void* mAdvanceDesignStudies = nullptr;
    // intel reports (TStringList the game appends to on every load and never clears)
    int fIntelReports = -1, fSLList = -1, fSLCount = -1;
    void *mSLClear = nullptr, *mSLAdd = nullptr, *mSLInsert = nullptr;
    // air units (TList of TAirUnit the game appends to on every load and never clears): the cell of its unit variable
    uint32_t airUnitsCell = 0;
    void* mObjFree = nullptr;
    // the load's blockade check (credits victory points on every load): the routine, the return address of
    // LoadBuildCampaign's call of it, the nations' Blockaded flag
    void* mCheckBlockade = nullptr;
    uintptr_t loadBlockadeRet = 0;
    int fBlockaded = -1;
    // pending doctrine changes (counted down by ReduceTimeLimits for the host's nation only)
    int fTraining = -1, fPendingTraining = -1, fPendingTrainingTime = -1;
    int fMissileStorage = -1, fPendingMissileStorage = -1, fMissileStorageTime = -1;
    void* mReduceTimeLimits = nullptr;
    bool doctrine = false;
    // random events (TfrmBuildCamp.Events: 100 records of 100 bytes, loaded from Data\Events.dat)
    int fEvents = -1, fTechSharing = -1;
    void *mHandleEvent = nullptr, *mLikelihood = nullptr;
    bool events = false;
    // peace treaties between players
    int fMapAreas = -1, fAreaName = -1, fAreaPossessions = -1, fPossName = -1, fPossValue = -1, fPossOil = -1,
        fPossOwner = -1, fBaseResources = -1, fPrestige = -1, fVP = -1, fEnemyVP = -1, fBattleOpp = -1;
    void *mChangeOwner = nullptr, *mAdjacent = nullptr, *mRemoveShipInPeace = nullptr, *mShowTreaty = nullptr,
         *mSLCreate = nullptr, *mFree = nullptr, *mBattleAftermath = nullptr,
         *mAIBattle = nullptr, *mRaiderBattle = nullptr, *mUpdateVPNumbers = nullptr, *mCopyGame = nullptr,
         *mResearchExecute = nullptr, *mResearchClick = nullptr, *mDoResearch = nullptr;
    int fResearchPct = -1, fResearchAreas = -1, fBtbnResearch = -1;
    // Tactical battles of joined players
    void *mCheckAttack = nullptr, *mStrategicTurn = nullptr, *mDoBattle = nullptr;
void *mCheckForBattles = nullptr, *mInvasionBattles = nullptr, *mRaiderVsPatrols = nullptr, *mPlayerDecline = nullptr;
bool battleGen = false;  // B6: the player's battle generator for joined nations (host-side view of CheckForBattles2)
    void** frmMainVar = nullptr;  // the battle form (TfrmMain) unit variable
    bool battles = false;
    int* scaleVar = nullptr;  // the game's scale value used in its war-progress thresholds (100*G + 300/500)
    bool treaties = false;
    // A joined player's screen shows its own nation's relations (the relations view, see KViewBegin). VCL and RTL
    // routines without RTTI names (TControl.SetVisible/SetText, System._UStrAsg) come from call sites.
    void *mShowTension = nullptr, *mStrengthBars = nullptr, *mNationData = nullptr, *mFlagFile = nullptr,
         *mPictureLoad = nullptr, *mSetVisible = nullptr, *mSetText = nullptr, *mUStrAsg = nullptr;
    int fHint = -1, fText = -1, fPicture = -1, fGbWar = -1, fLbWar = -1, fLbPlayer = -1;
    int fFlag[9] = {}, fShape[9] = {}, fSlotWar[9] = {}, fSlotAlly[9] = {}, fSlotBlock[9] = {};
    bool relView = false;
    // Relations v2 (B2): the computer's relation routines leave the pairs of nations under the player's rules alone
    void *mChangeAITension = nullptr, *mCheckForWar = nullptr, *mRandomTension = nullptr;
    int fWillGoToWar = -1;
    bool playerRules = false;
    // Relations v2 (B3): the player's relation routines in K's view on K's PC
    void *mPreviousWars = nullptr, *mShowTurnMessages = nullptr, *mWarsWithNation = nullptr, *mRandPossArea = nullptr,
         *mRandPoss = nullptr, *mSurpriseAttack = nullptr, *mSurpriseInvasion = nullptr;
    int fBudget = -1, fTurnMessages = -1;
    bool playerTurn = false;
    // 0.6.0 (B6 piece 2): surprise attacks and takeovers at a war's start for K (CheckForWar's call sites)
    void* mCheckSurprise = nullptr;
    int fSurpriseTrait = -1;
    bool surprise = false;
    // Relations v2 (B4): K's peace with the computer's nations in K's view on K's PC; System.Random (unnamed)
    void *mNegotiatePeace = nullptr, *mGetPeaceEvent = nullptr, *mCheckTotalDefeat = nullptr, *mForceRatio = nullptr,
         *mRandomEnemy = nullptr, *mSameAreas = nullptr, *mCountTakenFrom = nullptr, *mEcoRating = nullptr,
         *mTransferShip = nullptr, *mRandom = nullptr;
    int fTotalWar = -1, fHasBeenBattle = -1, fAutoSubs = -1, fTFW = -1, fHasLostWar = -1, fGovernment = -1,
        fUnrest = -1, fShipId = -1, fCShipName = -1, fShipLog = -1, fTakenFrom = -1, fInvaded = -1, fInvSupport = -1,
        fRebellion = -1, fSubPolicy = -1, fCorruption = -1;
    bool peace = false;
    // Relations v2, B5 (d): WarHelpFromAllies for K in K's view on K's PC
    void* mWarHelp = nullptr;
    int fLendLease = -1, fCoBel = -1;
    bool allyHelp = false;
    void* mBuildWar = nullptr;  // B5 (e): CheckBuildingShipsForWar(NewWar)
    void* mIsBuilding = nullptr;
    int fBuildNat = -1, fRebuild = -1;
    bool buildWar = false;
    void* mRevolution = nullptr;  // B5: CheckForRevolutionInEnemyNation (B2's mask hook bypassed: its original)
    int fRevoPending = -1;        // TfrmBuildCamp.RevolutionPending
    bool revolution = false;
    // 0.6.0: the disarmament treaty and the joined nations' wars (CancelTreaty hooked; ArmsTreatyNow)
    void *mCancelTreaty = nullptr, *mCreateArms = nullptr;
    int fTreatyTime = -1, fTreatyDisp = -1, fTreatyCal = -1, fSubBan = -1, fTreatyBase = -1;
    bool armsTreaty = false;
    // 0.6.0: the last battle's area and possession across a load (TfrmBuildCamp.BattleArea/BattlePossession, the
    // campaign's MapAreas, TMapArea.Possessions)
    int fBattleArea = -1, fBattlePoss = -1;
    bool battlePtrs = false;
} A;

static void* g_buildCamp = nullptr;
static void* g_startForm = nullptr;
static bool g_inTurn = false;
static bool g_noDefer = false;         // ResolveAIBattle: the game's own resolution runs
static bool g_battleSlotMode = false;  // this PC fights a joined player's battle in its battle slot
static bool g_battleAftermath = false; // HandleBattleAftermath ran in battle slot mode
static int g_ownWar = INT_MIN;          // a joined player's PC: its own nation's war counter from the host (or unknown)
extern "C" int __stdcall CB_SkipCampaignStep(int which);

#define REQ_FIELD(dst, cls, name)                              \
    do {                                                       \
        dst = dl::Field(cls, name);                            \
        if (dst < 0) {                                         \
            Log("MISSING field %s.%s", cls, name);             \
            ok = false;                                        \
        }                                                      \
    } while (0)
#define REQ_METHOD(dst, cls, name)                             \
    do {                                                       \
        dst = dl::Method(cls, name);                           \
        if (!dst) {                                            \
            Log("MISSING method %s.%s", cls, name);            \
            ok = false;                                        \
        }                                                      \
    } while (0)

static bool InImage(uintptr_t a) { return a >= dl::g_base && a < dl::g_base + dl::g_size; }

static void* CallTarget(uint8_t* e8) {  // target of an E8 rel32 call
    return e8 + 5 + *(int32_t*)(e8 + 1);
}

// Peace treaties between players: the game's own building blocks (possession owner change, reparation ships,
// treaty report), found by name or, for unnamed routines, from the instructions around their calls.
static void ResolveTreaties() {
    A.fMapAreas = dl::Field("TfrmBuildCamp", "MapAreas");
    A.fAreaName = dl::Field("TMapArea", "Name");
    A.fAreaPossessions = dl::Field("TMapArea", "Possessions");
    A.fPossName = dl::Field("TPossession", "Name");
    A.fPossValue = dl::Field("TPossession", "Value");
    A.fPossOil = dl::Field("TPossession", "Oil");
    A.fPossOwner = dl::Field("TPossession", "Owner");
    A.fBaseResources = dl::Field("TBuilderNation", "BaseResources");
    A.fPrestige = dl::Field("TBuilderNation", "Prestige");
    A.fVP = dl::Field("TBuilderNation", "VP");
    A.fEnemyVP = dl::Field("TfrmBuildCamp", "EnemyVP");
    A.fBattleOpp = dl::Field("TfrmBuildCamp", "BattleOpponentIdx");
    A.mChangeOwner = dl::Method("TfrmBuildCamp", "ChangePossessionOwner");
    A.mAdjacent = dl::Method("TfrmBuildCamp", "AreaIsAdjacentToOwnPossessions");
    A.mShowTreaty = dl::Method("TfrmBuildCamp", "ShowPeaceTreaty");
    A.mSLCreate = dl::MethodFewestParams("TStringList", "Create");
    A.mFree = dl::Method("TObject", "Free");
    A.mBattleAftermath = dl::Method("TfrmBuildCamp", "HandleBattleAftermath");
    A.mAIBattle = dl::Method("TfrmBuildCamp", "DoBattleBetweenAINations");
    A.mRaiderBattle = dl::Method("TfrmBuildCamp", "QuickResolveRaiderBattle");
    A.mUpdateVPNumbers = dl::Method("TfrmBuildCamp", "UpdateVPNumbers");
    A.mCopyGame = dl::Method("TfrmBuildCamp", "CopyGameToOtherSlot");
    A.mResearchExecute = dl::Method("TfrmResearch", "Execute");
    A.mResearchClick = dl::Method("TfrmBuildCamp", "btbnResearchClick");
    A.mDoResearch = dl::Method("TfrmBuildCamp", "DoResearch");
    A.fResearchPct = dl::Field("TBuilderNation", "ResearchPct");
    A.fResearchAreas = dl::Field("TBuilderNation", "ResearchAreas");
    A.fBtbnResearch = dl::Field("TfrmBuildCamp", "btbnResearch");
    A.mCheckAttack = dl::Method("TfrmBuildCamp", "CheckAttackLocation");
    A.mCheckForBattles = dl::Method("TfrmBuildCamp", "CheckForBattles2");
    A.mInvasionBattles = dl::Method("TfrmBuildCamp", "CheckForInvasionBattles");
    A.mRaiderVsPatrols = dl::Method("TfrmBuildCamp", "CheckRaiderVsPatrols");
    A.mPlayerDecline = dl::Method("TfrmBuildCamp", "DoPlayerDecline");
    A.mStrategicTurn = dl::Method("TfrmBuildCamp", "DoStrategicTurn");
    // DoBattle: mov ecx, [frmMain cell]; mov eax, [Application]; mov eax, [eax]; mov edx, [TfrmMain]; call CreateForm
    if (void* doBattle = dl::Method("TfrmBuildCamp", "DoBattle")) {
        A.mDoBattle = doBattle;
        uint8_t* hit = dl::FindPattern((uint8_t*)doBattle, 0x200,
                                       {0x8B, 0x0D, -1, -1, -1, -1, 0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0x8B, 0x15});
        uint32_t cell = hit ? *(uint32_t*)(hit + 2) : 0;
        uint32_t var = cell && InImage(cell) ? *(uint32_t*)(uintptr_t)cell : 0;
        if (var && InImage(var)) A.frmMainVar = (void**)(uintptr_t)var;
    }
    A.battles = A.mAIBattle && A.mCheckAttack && A.mStrategicTurn && A.frmMainVar && A.fMapAreas >= 0 &&
                A.fAreaName >= 0 && A.mBattleAftermath;
    Log("Joined players' battles: %s (frmMain=%p checkAttack=%p)", A.battles ? "ok" : "UNAVAILABLE", A.frmMainVar,
        A.mCheckAttack);
    // RemoveShipInPeace (unnamed): NegotiatePeace hands the player's ships to an enemy with
    // `call GetRandomEnemy; mov ecx,eax; mov eax,[ebp-4]; mov edx,[eax+PlayerNation]; mov eax,[ebp-4]; call X`.
    void* negotiate = dl::Method("TfrmBuildCamp", "NegotiatePeace");
    if (negotiate) {
        uint8_t* hit = dl::FindPattern((uint8_t*)negotiate, 0x1400,
                                       {0x8B, 0xC8, 0x8B, 0x45, 0xFC, 0x8B, 0x90, -1, -1, -1, -1, 0x8B, 0x45, 0xFC, 0xE8});
        if (hit && *(int32_t*)(hit + 7) == A.fPlayerNation) {
            void* t = CallTarget(hit + 14);
            if (InImage((uintptr_t)t)) A.mRemoveShipInPeace = t;
        }
    }
    // The scale value in the war-progress checks of EventLikelihoodCheck: `imul edx, dword ptr [G], 100` (8-bit
    // immediate `6B 15 [G] 64` in 1.01.44; the 32-bit form `69 15 [G] 64 00 00 00` is accepted too).
    if (A.mLikelihood) {
        uint8_t* hit = dl::FindPattern((uint8_t*)A.mLikelihood, 0x1000, {0x6B, 0x15, -1, -1, -1, -1, 0x64});
        if (!hit) hit = dl::FindPattern((uint8_t*)A.mLikelihood, 0x1000, {0x69, 0x15, -1, -1, -1, -1, 0x64, 0x00, 0x00, 0x00});
        if (hit && InImage(*(uint32_t*)(hit + 2))) A.scaleVar = (int*)(uintptr_t) * (uint32_t*)(hit + 2);
    }
    A.treaties = A.diplomacy && A.fMapAreas >= 0 && A.fAreaName >= 0 && A.fAreaPossessions >= 0 && A.fPossName >= 0 &&
                 A.fPossValue >= 0 && A.fPossOil >= 0 && A.fPossOwner >= 0 && A.fBaseResources >= 0 && A.fPrestige >= 0 &&
                 A.fVP >= 0 && A.fEnemyVP >= 0 && A.fBattleOpp >= 0 && A.mChangeOwner && A.mAdjacent && A.mShowTreaty &&
                 A.mSLCreate && A.mFree && A.mRemoveShipInPeace && A.scaleVar && A.mSLAdd;
    Log("Peace treaties: %s (removeShip=%p scale=%p slCreate=%p battleHooks=%d/%d/%d)", A.treaties ? "ok" : "UNAVAILABLE",
        A.mRemoveShipInPeace, A.scaleVar, A.mSLCreate, A.mBattleAftermath != nullptr, A.mAIBattle != nullptr,
        A.mRaiderBattle != nullptr);
}

// Optional: diplomacy between human players. Missing pieces only disable that feature.
static void ResolveDiplomacy() {
    A.fTension = dl::Field("TBuilderNation", "Tension");
    A.fAIAlliance = dl::Field("TBuilderNation", "AIAlliance");
    A.fAllied = dl::Field("TBuilderNation", "Allied");
    int fAITension = dl::Field("TBuilderNation", "AITension");
    A.mWarStarts = dl::Method("TfrmBuildCamp", "WarStartsWithNation");
    A.mSetAITension = dl::Method("TBuilderNation", "SetAITension");
    A.mSetAIAlliance = dl::Method("TBuilderNation", "SetAIAlliance");
    A.mDoPeace = dl::Method("TfrmBuildCamp", "DoPeace");
    A.mCountAtWar = dl::Method("TfrmBuildCamp", "CountNationsAtWarWithPlayer");
    A.mHandleAIPeace = dl::Method("TfrmBuildCamp", "HandleAIPeace");
    // The tension array is indexed by nation: [0] is `Tension` (with nation 0), AITension[1..8] follows.
    bool layout = A.fTension >= 0 && fAITension == A.fTension + 4 && A.fAIAlliance == A.fTension + 0x24 &&
                  A.fAllied == A.fAIAlliance + 0x20;
    // WarStartsWithNation: `cmp dword ptr [War],0; jg +n; mov dword ptr [War],1` (same address twice)
    if (A.mWarStarts) {
        uint8_t* hit = dl::FindPattern((uint8_t*)A.mWarStarts, 0x60,
                                       {0x83, 0x3D, -1, -1, -1, -1, 0x00, 0x7F, -1, 0xC7, 0x05, -1, -1, -1, -1, 0x01,
                                        0x00, 0x00, 0x00});
        if (hit && *(uint32_t*)(hit + 2) == *(uint32_t*)(hit + 11) && InImage(*(uint32_t*)(hit + 2)))
            A.warVar = (int*)(uintptr_t) * (uint32_t*)(hit + 2);
    }
    // DoPeace: `mov eax,[YearCell]; imul eax,[eax],0x96; lea eax,[esi+eax*8]; mov edx,[MonthCell]; mov edx,[edx];
    //           lea edx,[edx+edx*4]; lea edx,[edx+edx*4]; cmp dword ptr [eax+edx*4+disp],0`
    if (A.mDoPeace) {
        uint8_t* hit = dl::FindPattern((uint8_t*)A.mDoPeace, 0x80,
                                       {0xA1, -1, -1, -1, -1, 0x69, 0x00, 0x96, 0x00, 0x00, 0x00, 0x8D, 0x04, 0xC6,
                                        0x8B, 0x15, -1, -1, -1, -1, 0x8B, 0x12, 0x8D, 0x14, 0x92, 0x8D, 0x14, 0x92,
                                        0x83, 0xBC, 0x90, -1, -1, -1, -1, 0x00});
        if (hit) {
            uint32_t y = *(uint32_t*)(hit + 1), m = *(uint32_t*)(hit + 16);
            if (InImage(y) && InImage(m)) {
                A.recYearCell = (int**)(uintptr_t)y;
                A.recMonthCell = (int**)(uintptr_t)m;
                A.recDisp = *(int32_t*)(hit + 31);
            }
        }
    }
    A.diplomacy = layout && A.mWarStarts && A.mSetAITension && A.mSetAIAlliance && A.mCountAtWar && A.warVar;
    Log("Diplomacy: %s (layout=%d warVar=%p peaceRecord=%p doPeace=%p aiPeace=%p)", A.diplomacy ? "ok" : "UNAVAILABLE",
        layout, A.warVar, A.recYearCell, A.mDoPeace, A.mHandleAIPeace);
}

// The load's blockade check (see CB_BeforeBlockade): LoadBuildCampaign's own call of CheckBlockadeStatus (one E8 call
// in 1.01.44, 0x1327 bytes in), told apart from the month's calls (DoTurn, DoStrategicTurn) by its return address.
static void ResolveLoadBlockade() {
    A.mCheckBlockade = dl::Method("TfrmBuildCamp", "CheckBlockadeStatus");
    A.fBlockaded = dl::Field("TBuilderNation", "Blockaded");
    int calls = 0;
    uint8_t* load = (uint8_t*)A.mLoad;
    for (int i = 0; load && A.mCheckBlockade && i < 0x2000 && InImage((uintptr_t)(load + i + 5)); i++)
        if (load[i] == 0xE8 && CallTarget(load + i) == A.mCheckBlockade) {
            calls++;
            A.loadBlockadeRet = (uintptr_t)(load + i + 5);
        }
    if (calls != 1) A.loadBlockadeRet = 0;
    bool ok = A.loadBlockadeRet && A.fVP >= 0 && A.fEnemyVP >= 0 && A.fBuilderNations >= 0;
    Log("Load VP guard: %s (CheckBlockadeStatus=%p, %d call(s) in LoadBuildCampaign%s)", ok ? "ok" : "UNAVAILABLE",
        A.mCheckBlockade, calls,
        ok ? "; loads in a multiplayer session and the mod's own reloads keep the saved victory points, a load without a "
             "session is the game's own"
           : "; every load in a multiplayer session of a campaign with a blockade will add victory points the save "
             "doesn't have");
    if (!ok) A.loadBlockadeRet = 0;
}

// The first call site in [start, start+len) that matches `pat` (bytes < 0 are wildcards) and `ok`.
template <class Pred>
static uint8_t* FindSite(uint8_t* start, size_t len, const std::vector<int>& pat, Pred ok) {
    for (uint8_t* p = start; p && len > pat.size();) {
        uint8_t* hit = dl::FindPattern(p, len, pat);
        if (!hit) return nullptr;
        if (ok(hit)) return hit;
        len -= (size_t)(hit + 1 - p);
        p = hit + 1;
    }
    return nullptr;
}

// The relations view (see KViewBegin): the strategic screen's tension panel (TfrmBuildCamp.ShowTensionLevels, slots 1-8:
// imgFlag<i>, Tension<i>, lbWar<i>, imgAlly<i>, lbBlock<i>), the flags (SetValues), the nation data dialog of a flag
// click (TdlgNationData.Execute), the map's strength bars (DrawAreaStrengthBars) and the war state (gbWar, lbWar,
// lbPlayerNation's hint, set by UpdateUIValues from the host's war counter).
static void ResolveRelationsView() {
    A.mShowTension = dl::Method("TfrmBuildCamp", "ShowTensionLevels");
    A.mStrengthBars = dl::Method("TfrmBuildCamp", "DrawAreaStrengthBars");
    A.mNationData = dl::Method("TdlgNationData", "Execute");
    A.mFlagFile = dl::Method("TBuilderNation", "GetFlagFileName");
    A.mPictureLoad = dl::Method("TPicture", "LoadFromFile");
    A.fHint = dl::Field("TControl", "FHint");
    A.fText = dl::Field("TControl", "FText");
    A.fPicture = dl::Field("TImage", "FPicture");
    A.fGbWar = dl::Field("TfrmBuildCamp", "gbWar");
    A.fLbWar = dl::Field("TfrmBuildCamp", "lbWar");
    A.fLbPlayer = dl::Field("TfrmBuildCamp", "lbPlayerNation");
    bool slots = true;
    for (int i = 1; i <= 8; i++) {
        char n[24];
        auto f = [&](const char* pre) {
            snprintf(n, sizeof(n), "%s%d", pre, i);
            int o = dl::Field("TfrmBuildCamp", n);
            slots &= o > 0;
            return o;
        };
        A.fFlag[i] = f("imgFlag");
        A.fShape[i] = f("Tension");
        A.fSlotWar[i] = f("lbWar");
        A.fSlotAlly[i] = f("imgAlly");
        A.fSlotBlock[i] = f("lbBlock");
    }
    int fIntel1 = dl::Field("TfrmBuildCamp", "stIntel1");
    // TControl.SetVisible: ShowTensionLevels `setg dl; mov eax, [ebx+lbWar1]; call X` (lbWar1.Visible := Tension > 40)
    if (A.mShowTension && slots) {
        uint8_t* hit = FindSite((uint8_t*)A.mShowTension, 0x800, {0x0F, 0x9F, 0xC2, 0x8B, 0x83, -1, -1, -1, -1, 0xE8},
                                [](uint8_t* h) { return *(int32_t*)(h + 5) == A.fSlotWar[1]; });
        if (hit && InImage((uintptr_t)CallTarget(hit + 9))) A.mSetVisible = CallTarget(hit + 9);
    }
    // TControl.SetText: ShowTensionLevels `mov edx, [ebp-4]; mov eax, [ebx+stIntel1]; call X` (the intel label's text)
    if (A.mShowTension && fIntel1 > 0) {
        uint8_t* hit = FindSite((uint8_t*)A.mShowTension, 0x800, {0x8B, 0x55, 0xFC, 0x8B, 0x83, -1, -1, -1, -1, 0xE8},
                                [fIntel1](uint8_t* h) { return *(int32_t*)(h + 5) == fIntel1; });
        if (hit && InImage((uintptr_t)CallTarget(hit + 9))) A.mSetText = CallTarget(hit + 9);
    }
    // System._UStrAsg: UpdateUIValues `mov eax, [ebx+imgFlag1]; add eax, FHint; call X` (imgFlag1.Hint := the name)
    if (A.mUpdateUIValues && slots && A.fHint > 0) {
        uint8_t* hit = FindSite((uint8_t*)A.mUpdateUIValues, 0x600, {0x8B, 0x83, -1, -1, -1, -1, 0x05, -1, -1, -1, -1, 0xE8},
                                [](uint8_t* h) { return *(int32_t*)(h + 2) == A.fFlag[1] && *(int32_t*)(h + 7) == A.fHint; });
        if (hit && InImage((uintptr_t)CallTarget(hit + 11))) A.mUStrAsg = CallTarget(hit + 11);
    }
    bool distinct = A.mSetVisible != A.mSetText && A.mSetText != A.mUStrAsg && A.mSetVisible != A.mUStrAsg;
    A.relView = A.diplomacy && slots && distinct && A.mShowTension && A.mSetVisible && A.mSetText && A.mUStrAsg &&
                A.fHint > 0 && A.fText > 0 && A.fPicture > 0 && A.fGbWar > 0 && A.fLbWar > 0 && A.fLbPlayer > 0 &&
                A.mFlagFile && A.mPictureLoad && A.fVisible >= 0;
    Log("Relations view: %s (showTension=%p setVisible=%p setText=%p ustrAsg=%p strengthBars=%p nationData=%p)",
        A.relView ? "ok" : "UNAVAILABLE; a joined player's screen shows the host's relations", A.mShowTension,
        A.mSetVisible, A.mSetText, A.mUStrAsg, A.mStrengthBars, A.mNationData);
}

// Relations v2, B4: the player's peace (CheckTurnEvents' peace decision, GetPeaceEvent, NegotiatePeace, DoPeace) for a
// joined nation in its view, and the fields the peace part masks or reports. System.Random has no RTTI name: it is the
// call of the decision's first instructions in CheckTurnEvents, `mov eax, 4; call Random; add eax, 2; cmp eax, [War]`
// (Random = `push eax; call [RandomProc]; ...`); the decision's `imul edi, [G], 250` must name the scale value G that
// EventLikelihoodCheck's thresholds use (A.scaleVar, the game's FleetSize setting).
static void ResolvePeace() {
    A.mNegotiatePeace = dl::Method("TfrmBuildCamp", "NegotiatePeace");
    A.mGetPeaceEvent = dl::Method("TfrmBuildCamp", "GetPeaceEvent");
    A.mCheckTotalDefeat = dl::Method("TfrmBuildCamp", "CheckTotalDefeat");
    A.mForceRatio = dl::Method("TfrmBuildCamp", "GetTotalForceRatio");
    A.mRandomEnemy = dl::Method("TfrmBuildCamp", "GetRandomEnemy");
    A.mSameAreas = dl::Method("TfrmBuildCamp", "HasPossessionsInTheSameAreas");
    A.mCountTakenFrom = dl::Method("TfrmBuildCamp", "CountTakenFrom");
    A.mEcoRating = dl::Method("TfrmBuildCamp", "GetNationEconomicRating");
    A.mTransferShip = dl::Method("TfrmBuildCamp", "TransferShipAsReparation");
    A.fTotalWar = dl::Field("TfrmBuildCamp", "TotalWar");
    A.fHasBeenBattle = dl::Field("TfrmBuildCamp", "HasBeenBattle");
    A.fAutoSubs = dl::Field("TfrmBuildCamp", "AutoBuildSubs");
    A.fTFW = dl::Field("TfrmBuildCamp", "TFW");
    A.fHasLostWar = dl::Field("TBuilderNation", "HasLostWar");
    A.fGovernment = dl::Field("TBuilderNation", "GovernmentType");
    A.fUnrest = dl::Field("TBuilderNation", "UnrestLevel");
    A.fShipId = dl::Field("TCampaignShip", "CShipId");
    A.fCShipName = dl::Field("TCampaignShip", "ShipName");
    A.fShipLog = dl::Field("TCampaignShip", "myLogEntries");
    A.fTakenFrom = dl::Field("TPossession", "TakenFrom");
    A.fInvaded = dl::Field("TPossession", "Invaded");
    A.fInvSupport = dl::Field("TPossession", "InvasionSupport");
    A.fRebellion = dl::Field("TPossession", "Rebellion");
    A.fSubPolicy = dl::Field("TBuilderNation", "SubmarinePolicy");
    A.fCorruption = dl::Field("TBuilderNation", "Corruption");
    void* cte = dl::Method("TfrmBuildCamp", "CheckTurnEvents");
    bool scaleOk = false;
    if (cte && A.warVar) {
        uint8_t* hit = FindSite((uint8_t*)cte, 0x1000,
                                {0xB8, 0x04, 0x00, 0x00, 0x00, 0xE8, -1, -1, -1, -1, 0x83, 0xC0, 0x02, 0x3B, 0x05, -1, -1, -1, -1},
                                [](uint8_t* h) { return *(uint32_t*)(h + 15) == (uint32_t)(uintptr_t)A.warVar; });
        uint8_t* r = hit ? (uint8_t*)CallTarget(hit + 5) : nullptr;
        if (r && InImage((uintptr_t)r) && r[0] == 0x50 && r[1] == 0xFF && r[2] == 0x15) A.mRandom = r;
        uint8_t* g = dl::FindPattern((uint8_t*)cte, 0x1000, {0x69, 0x3D, -1, -1, -1, -1, 0xFA, 0x00, 0x00, 0x00});
        scaleOk = g && A.scaleVar && *(uint32_t*)(g + 2) == (uint32_t)(uintptr_t)A.scaleVar;
    }
    A.peace = A.treaties && A.events && A.mRandom && scaleOk && A.mNegotiatePeace && A.mGetPeaceEvent &&
              A.mCheckTotalDefeat && A.mForceRatio && A.mRandomEnemy && A.mSameAreas && A.mCountTakenFrom &&
              A.mEcoRating && A.mTransferShip && A.mDoPeace && A.recYearCell && A.recMonthCell && A.fTotalWar >= 0 &&
              A.fHasBeenBattle >= 0 && A.fAutoSubs >= 0 && A.fTFW >= 0 && A.fHasLostWar >= 0 && A.fGovernment >= 0 &&
              A.fUnrest >= 0 && A.fShipId >= 0 && A.fCShipName >= 0 && A.fShipLog >= 0 && A.fTakenFrom >= 0 &&
              A.fInvaded >= 0 && A.fInvSupport >= 0 && A.fRebellion >= 0 && A.fBlockaded >= 0 && A.fPlayerNation >= 0 &&
              A.fSubPolicy >= 0 && A.fCorruption >= 0;
    Log("Peace with the computer's nations for a joined nation (routines): %s (random=%p scale=%d negotiate=%p "
        "transfer=%p)",
        A.peace ? "ok" : "UNAVAILABLE", A.mRandom, scaleOk ? 1 : 0, A.mNegotiatePeace, A.mTransferShip);
}

bool Resolve() {
    bool ok = true;
    REQ_FIELD(A.fBuilderNations, "TfrmBuildCamp", "BuilderNations");
    REQ_FIELD(A.fPlayerNation, "TfrmBuildCamp", "PlayerNation");
    REQ_FIELD(A.fStartBuild, "TfrmBuildCamp", "StartBuild");
    REQ_FIELD(A.fBtbnTurn, "TfrmBuildCamp", "btbnTurn");
    REQ_FIELD(A.fBattleOpponentIdx, "TfrmBuildCamp", "BattleOpponentIdx");
    REQ_FIELD(A.fCampaignStartYear, "TfrmBuildCamp", "CampaignStartYear");
    REQ_FIELD(A.fNationName, "TBuilderNation", "NationName");
    REQ_FIELD(A.fShipList, "TBuilderNation", "ShipList");
    REQ_FIELD(A.fFunds, "TBuilderNation", "Funds");
    REQ_FIELD(A.fShipOrderedArea, "TCampaignShip", "OrderedArea");
    REQ_FIELD(A.fShipLocationArea, "TCampaignShip", "LocationArea");
    REQ_FIELD(A.fShipStatus, "TCampaignShip", "Status");
    REQ_FIELD(A.fHandle, "TWinControl", "FHandle");
    REQ_FIELD(A.fVisible, "TControl", "FVisible");
    REQ_FIELD(A.fOwner, "TComponent", "FOwner");
    REQ_FIELD(A.fModalLevel, "TApplication", "FModalLevel");

    REQ_METHOD(A.mTurnClick, "TfrmBuildCamp", "btbnTurnClick");
    REQ_METHOD(A.mResignClick, "TfrmBuildCamp", "btbnResignClick");
    REQ_METHOD(A.mDoEndOfTurn, "TfrmBuildCamp", "DoEndOfTurn");
    REQ_METHOD(A.mDoTurn2, "TfrmBuildCamp", "DoTurn2");
    REQ_METHOD(A.mStopBlockadedMoves, "TfrmBuildCamp", "StopBlockadedMoves");
    REQ_METHOD(A.mSave, "TfrmBuildCamp", "SaveBuildCampaign");
    REQ_METHOD(A.mLoad, "TfrmBuildCamp", "LoadBuildCampaign");
    REQ_METHOD(A.mAdjustTechs, "TfrmBuildCamp", "AdjustTechs");
    REQ_METHOD(A.mEnableButtons, "TfrmBuildCamp", "EnableButtons");
    REQ_METHOD(A.mSetNationColors, "TfrmBuildCamp", "SetNationColors");
    REQ_METHOD(A.mUpdateUIValues, "TfrmBuildCamp", "UpdateUIValues");
    REQ_METHOD(A.mFillMapTree, "TfrmBuildCamp", "FillMapTree");
    REQ_METHOD(A.mGetSaveDirectory, "TfrmBuildCamp", "GetSaveDirectory");
    REQ_METHOD(A.mLoadWarData, "TBuilderNation", "LoadWarData");
    REQ_METHOD(A.mShow, "TCustomForm", "Show");
    REQ_METHOD(A.mHide, "TCustomForm", "Hide");
    // UI refresh helpers (optional: missing ones only make the client view refresh less complete)
    A.mSetValues = dl::Method("TfrmBuildCamp", "SetValues");
    A.mTabChange = dl::Method("TfrmBuildCamp", "TabControl1Change");
    A.fTabControl1 = dl::Field("TfrmBuildCamp", "TabControl1");

    // Current save slot global: SaveBuildCampaign starts with `mov edx,[ptr]; mov edx,[edx]; mov eax,esi;
    // call GetSaveDirectory`. The pointer cell holds the address of the slot variable.
    if (A.mSave) {
        uint8_t* hit = dl::FindPattern((uint8_t*)A.mSave, 0x100, {0x8B, 0x15, -1, -1, -1, -1, 0x8B, 0x12});
        if (hit) {
            uint32_t cell = *(uint32_t*)(hit + 2);
            if (cell >= dl::g_base && cell < dl::g_base + dl::g_size) A.slotVar = *(int**)cell;
        }
        if (!A.slotVar) {
            Log("MISSING save slot variable");
            ok = false;
        }
    }
    // AI strategic moves: the unnamed routine DoTurn2 calls right before StopBlockadedMoves.
    if (A.mDoTurn2 && A.mStopBlockadedMoves) {
        auto calls = dl::CallTargets((uint8_t*)A.mDoTurn2, 0x300);
        for (size_t i = 1; i < calls.size(); i++) {
            if (calls[i] == A.mStopBlockadedMoves) {
                uint8_t* c = calls[i - 1];
                if (c[0] == 0x55 && c[1] == 0x8B && c[2] == 0xEC) A.mAIMoves = c;
                break;
            }
        }
        if (!A.mAIMoves) Log("WARNING: AI strategic move routine not found; human fleets may be moved by AI");
    }
    ResolveDiplomacy();
    A.fDesignList = dl::Field("TBuilderNation", "DesignList");
    A.fReadyForBuild = dl::Field("TDesignShip", "ReadyForBuild");
    A.fShipName = dl::Field("Tship", "Name");
    A.mAdvanceDesignStudies = dl::Method("TfrmBuildCamp", "AdvanceDesignStudies");
    if (A.fDesignList < 0 || A.fReadyForBuild < 0 || A.fShipName < 0 || !A.mAdvanceDesignStudies)
        Log("WARNING: design study fields not found; joined players' design studies won't advance");
    A.fIntelReports = dl::Field("TfrmBuildCamp", "IntelReports");
    A.fSLList = dl::Field("TStringList", "FList");
    A.fSLCount = dl::Field("TStringList", "FCount");
    A.mSLClear = dl::Method("TStringList", "Clear");
    A.mSLAdd = dl::Method("TStringList", "Add");
    A.mSLInsert = dl::Method("TStringList", "Insert");
    if (A.fIntelReports < 0 || A.fSLList < 0 || A.fSLCount < 0 || !A.mSLClear || !A.mSLAdd)
        Log("WARNING: intel report list not found; reloading a campaign will duplicate its intel reports");
    // The air units' list: LoadAircraftData (mov eax, [cell]; mov eax, [eax]; mov edx, ebx; call TList.Add) and the
    // game's own ClearAirData (push ebx; push esi; mov esi, [cell]) name the same cell.
    {
        uint8_t* lad = (uint8_t*)dl::Method("TfrmBuildCamp", "LoadAircraftData");
        uint8_t* cad = (uint8_t*)dl::Method("TfrmBuildCamp", "ClearAirData");
        uint32_t c1 = 0, c2 = 0;
        for (int i = 0; lad && i < 0x100; i++)
            if (lad[i] == 0xA1 && lad[i + 5] == 0x8B && lad[i + 6] == 0x00 && lad[i + 7] == 0x8B && lad[i + 8] == 0xD3 &&
                lad[i + 9] == 0xE8) {
                c1 = *(uint32_t*)(lad + i + 1);
                break;
            }
        if (cad && cad[0] == 0x53 && cad[1] == 0x56 && cad[2] == 0x8B && cad[3] == 0x35) c2 = *(uint32_t*)(cad + 4);
        A.mObjFree = dl::Method("TObject", "Free");
        if (c1 && c1 == c2 && A.mObjFree)
            A.airUnitsCell = c1;
        else
            Log("WARNING: air unit list not found; reloading a campaign will duplicate its air units");
    }
    A.fTraining = dl::Field("TBuilderNation", "TrainingPriorities");
    A.fPendingTraining = dl::Field("TBuilderNation", "PendingTrainingPriorities");
    A.fPendingTrainingTime = dl::Field("TBuilderNation", "PendingTrainingTime");
    A.fMissileStorage = dl::Field("TBuilderNation", "MissileStorage");
    A.fPendingMissileStorage = dl::Field("TBuilderNation", "PendingMissileStorage");
    A.fMissileStorageTime = dl::Field("TBuilderNation", "MissileStorageTime");
    A.mReduceTimeLimits = dl::Method("TfrmBuildCamp", "ReduceTimeLimits");
    A.doctrine = A.fTraining >= 0 && A.fPendingTraining == A.fTraining + 4 && A.fPendingTrainingTime == A.fTraining + 8 &&
                 A.fMissileStorage >= 0 && A.fPendingMissileStorage >= 0 && A.fMissileStorageTime >= 0 &&
                 A.mReduceTimeLimits;
    if (!A.doctrine) Log("WARNING: doctrine fields not found; joined players' doctrine changes won't take effect");
    A.fEvents = dl::Field("TfrmBuildCamp", "Events");
    A.fTechSharing = dl::Field("TBuilderNation", "TechSharing");
    A.mHandleEvent = dl::Method("TfrmBuildCamp", "HandleEvent");
    A.mLikelihood = dl::Method("TfrmBuildCamp", "EventLikelihoodCheck");
    A.events = A.diplomacy && A.fEvents >= 0 && A.fTechSharing >= 0 && A.mHandleEvent && A.mLikelihood;
    if (!A.events) Log("WARNING: event data not found; joined players won't get random events");
    ResolveTreaties();
    ResolveLoadBlockade();
    ResolveRelationsView();
    A.mChangeAITension = dl::Method("TBuilderNation", "ChangeAITension");
    A.mCheckForWar = dl::Method("TfrmBuildCamp", "CheckForWar");
    A.mRandomTension = dl::Method("TfrmBuildCamp", "RandomTensionChanges");
    A.fWillGoToWar = dl::Field("TBuilderNation", "WillGoToWar");
    A.playerRules = A.diplomacy && A.mChangeAITension && A.mCheckForWar && A.mRandomTension && A.fWillGoToWar >= 0;
    A.mPreviousWars = dl::Method("TfrmBuildCamp", "TensionEffectOfPreviousWars");
    A.mShowTurnMessages = dl::Method("TfrmBuildCamp", "ShowTurnMessages");
    A.mWarsWithNation = dl::Method("TfrmBuildCamp", "CheckWarsWithNation");
    A.mRandPossArea = dl::Method("TBuilderNation", "GetRandomPossessionInArea");
    A.mRandPoss = dl::Method("TfrmBuildCamp", "GetRandomPossession");
    A.mSurpriseAttack = dl::Method("TfrmBuildCamp", "DoSurpriseAttack");
    A.mSurpriseInvasion = dl::Method("TfrmBuildCamp", "CheckForPlayerSurpriseInvasion");
    A.mCheckSurprise = dl::Method("TfrmBuildCamp", "CheckSurpriseAttack");
    A.fSurpriseTrait = dl::Field("TBuilderNation", "SurpriseAttack");
    A.fBudget = dl::Field("TBuilderNation", "BudgetModifier");
    A.fTurnMessages = dl::Field("TfrmBuildCamp", "TurnMessages");
    ResolvePeace();
    nplay::Resolve();
    Log("Resolve: %s (slotVar=%p aiMoves=%p)", ok ? "ok" : "FAILED", A.slotVar, A.mAIMoves);
    return ok;
}

// ---------------------------------------------------------------------------------------------
// Instances

static bool IsAppOwned(void* obj) {
    void* owner = dl::At<void*>(obj, A.fOwner);
    return owner && dl::IsInstanceOf(owner, "TApplication");
}

static void* ScanHeapFor(const char* cls) {
    uintptr_t vmt = (uintptr_t)dl::ClassVmt(cls);
    if (!vmt) return nullptr;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t end = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi;
    while (addr < end && VirtualQuery((void*)addr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect & PAGE_READWRITE) &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            uint32_t* p = (uint32_t*)mbi.BaseAddress;
            size_t n = mbi.RegionSize / 4;
            for (size_t i = 0; i < n; i++) {
                if (p[i] == vmt && IsAppOwned(&p[i])) return &p[i];
            }
        }
        addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return nullptr;
}

static bool StillValid(void* obj, const char* cls) {
    return obj && *(uint32_t*)obj == (uint32_t)(uintptr_t)dl::ClassVmt(cls);
}

void* BuildCamp() {
    if (!StillValid(g_buildCamp, "TfrmBuildCamp")) g_buildCamp = ScanHeapFor("TfrmBuildCamp");
    return g_buildCamp;
}

void* StartForm() {
    if (!StillValid(g_startForm, "TfrmSelectNation2")) g_startForm = ScanHeapFor("TfrmSelectNation2");
    return g_startForm;
}

void* Application() {
    void* f = StartForm();
    if (!f) f = BuildCamp();
    return f ? dl::At<void*>(f, A.fOwner) : nullptr;
}

HWND FormHwnd(void* form) { return form ? dl::At<HWND>(form, A.fHandle) : nullptr; }

bool FormVisible(void* form) {
    HWND h = FormHwnd(form);
    return h && IsWindowVisible(h);
}

// ---------------------------------------------------------------------------------------------
// Nations and ships

static void* NationList() {
    void* f = BuildCamp();
    return f ? dl::At<void*>(f, A.fBuilderNations) : nullptr;
}

int NationCount() { return dl::ListCount(NationList()); }
void* Nation(int idx) { return dl::ListItem(NationList(), idx); }

int NationIndex(void* nation) {
    void* l = NationList();
    int n = dl::ListCount(l);
    for (int i = 0; i < n; i++)
        if (dl::ListItem(l, i) == nation) return i;
    return -1;
}

std::wstring NationName(int idx) {
    void* n = Nation(idx);
    return n ? dl::ReadUStr(dl::At<void*>(n, A.fNationName)) : L"";
}

int NationFunds(int idx) {
    void* n = Nation(idx);
    return n ? dl::At<int>(n, A.fFunds) : 0;
}

void* PlayerNation() {
    void* f = BuildCamp();
    return f ? dl::At<void*>(f, A.fPlayerNation) : nullptr;
}

int PlayerIndex() { return NationIndex(PlayerNation()); }

int ShipCount(void* nation) { return nation ? dl::ListCount(dl::At<void*>(nation, A.fShipList)) : 0; }
void* Ship(void* nation, int i) { return nation ? dl::ListItem(dl::At<void*>(nation, A.fShipList), i) : nullptr; }

static void* DesignAt(int nationIdx, int i) {
    void* n = Nation(nationIdx);
    if (!n || A.fDesignList < 0 || A.fReadyForBuild < 0) return nullptr;
    void* d = dl::ListItem(dl::At<void*>(n, A.fDesignList), i);
    return d && dl::IsInstanceOf(d, "TDesignShip") ? d : nullptr;
}

int DesignCount(int nationIdx) {
    void* n = Nation(nationIdx);
    return n && A.fDesignList >= 0 ? dl::ListCount(dl::At<void*>(n, A.fDesignList)) : 0;
}

std::wstring DesignName(int nationIdx, int i) {
    void* d = DesignAt(nationIdx, i);
    return d && A.fShipName >= 0 ? dl::ReadUStr(dl::At<void*>(d, A.fShipName)) : L"";
}

int DesignReadyForBuild(int nationIdx, int i) {
    void* d = DesignAt(nationIdx, i);
    return d ? dl::At<int>(d, A.fReadyForBuild) : -1;
}

void SetDesignReadyForBuild(int nationIdx, int i, int months) {
    void* d = DesignAt(nationIdx, i);
    if (d) dl::At<int>(d, A.fReadyForBuild) = months;
}

// ---------------------------------------------------------------------------------------------
// Intel reports. LoadBuildCampaign appends the save's [IntelReports] (plus one empty line) to the list without
// clearing it. The game itself always loads into a fresh strategic screen, but the mod reloads the campaign every
// month (client: the new month; host: after merging the players' turns), so the list doubled every month and
// bloated the save until it no longer fit in memory or in a network message.

static void* IntelList(void* form) {
    if (!form || A.fIntelReports < 0 || A.fSLList < 0 || A.fSLCount < 0 || !A.mSLClear || !A.mSLAdd) return nullptr;
    void* l = dl::At<void*>(form, A.fIntelReports);
    const char* cls = l ? dl::ClassNameOf(l) : nullptr;
    return cls && strcmp(cls, "TStringList") == 0 ? l : nullptr;
}

static void ClearIntelReports(void* form) {
    void* l = IntelList(form);
    if (l && dl::At<int>(l, A.fSLCount) > 0) dl::Call(A.mSLClear, (uint32_t)(uintptr_t)l);
}

// Removes empty and repeated entries (keeps the first of each), which also repairs campaigns saved before the fix.
static void DedupIntelReports(void* form) {
    void* l = IntelList(form);
    if (!l) return;
    int count = dl::At<int>(l, A.fSLCount);
    void** items = dl::At<void**>(l, A.fSLList);  // TStringItem = {FString, FObject}
    if (count <= 0 || !items) return;
    std::vector<std::wstring> keep;
    std::set<std::wstring> seen;
    for (int i = 0; i < count; i++) {
        std::wstring s = dl::ReadUStr(items[i * 2]);
        if (!s.empty() && seen.insert(s).second) keep.push_back(s);
    }
    if ((int)keep.size() == count) return;
    dl::Call(A.mSLClear, (uint32_t)(uintptr_t)l);
    for (auto& s : keep) {
        dl::ConstUStr u(s);
        dl::Call(A.mSLAdd, (uint32_t)(uintptr_t)l, u.ptr());
    }
    if (count - (int)keep.size() > 1)  // the game itself adds one empty entry on every load
        Log("intel reports: %d entries -> %d (empty and repeated entries removed)", count, (int)keep.size());
}

// Air units. LoadAircraftData (called by LoadBuildCampaign) appends the save's [AirUnits] to the global TAirUnit list
// without clearing it (the game's own ClearAirData is called only by the quick battle). The mod reloads the campaign in
// the same process every month (host: after merging the players' turns; client: the new month) and at every live
// battle start, resync and rejoin, so the list doubled with each save and load: the host's carriers soon failed the
// Turn's check ("CV ... aircraft capacity exceeded!") and the air maintenance costs ran away. The units the list held
// before a load are dropped and freed after the load succeeded (nothing else keeps TAirUnit pointers: the battle's
// formations name aircraft types, the air group dialog clears its copy pointer when it opens).
static std::vector<void*> g_airBefore;

static void* AirUnitList() {
    if (!A.airUnitsCell) return nullptr;
    void* var = *(void**)(uintptr_t)A.airUnitsCell;
    void* l = var ? *(void**)var : nullptr;
    return l && dl::IsInstanceOf(l, "TList") ? l : nullptr;
}

int AirUnitCount() { return AirUnitList() ? dl::ListCount(AirUnitList()) : -1; }

static void RememberAirUnits() {
    g_airBefore.clear();
    void* l = AirUnitList();
    for (int i = 0, n = dl::ListCount(l); i < n; i++) g_airBefore.push_back(dl::ListItem(l, i));
}

static void DropOldAirUnits(bool loaded) {
    std::vector<void*> old;
    old.swap(g_airBefore);
    void* l = AirUnitList();
    if (!l || old.empty()) return;
    int n = dl::ListCount(l), n0 = (int)old.size();
    void** items = *(void***)((uint8_t*)l + 4);  // TList.FList, FCount at +8 (as dl::ListCount)
    if (!loaded) {
        Log("air units: the load failed; the %d units from before it stay (the list has %d)", n0, n);
        return;
    }
    if (!items || n < n0 || !std::equal(old.begin(), old.end(), items)) {
        Log("WARNING: air units: the list changed during the load (%d before, %d now); left as it is", n0, n);
        return;
    }
    memmove(items, items + n0, (size_t)(n - n0) * sizeof(void*));
    *(int*)((uint8_t*)l + 8) = n - n0;
    for (void* o : old) dl::Call(A.mObjFree, (uint32_t)(uintptr_t)o);
    Log("air units: %d from before the load dropped; the campaign's %d kept", n0, n - n0);
}

// A campaign saved by an earlier version may hold the same air units several times (AirUnitId is unique in a campaign
// the game itself saved): the first of each id stays, the repeats are dropped and freed. The first copy is the one the
// game treats as that unit: its only lookup by AirUnitId (0xe9f570) returns the first match from index 0, and the end
// of every battle (0xea7abc) writes the battle's losses, aircrew experience, kills and hits to that copy (or deletes it
// when its ship was sunk). The later copies, from later loads in the same game, differ only by what code that matches
// a unit's base object changed after the first reload (desired sizes, transfers, the computer's air group growth):
// orders that can be given again, unlike a battle's results.
static void DedupAirUnits() {
    static int fId = dl::Field("TAirUnit", "AirUnitId");
    void* l = AirUnitList();
    int n = dl::ListCount(l);
    if (!l || n <= 0 || fId < 0 || !A.mObjFree) return;
    void** items = *(void***)((uint8_t*)l + 4);
    std::set<int> seen;
    std::vector<void*> drop;
    int k = 0;
    for (int i = 0; i < n; i++) {
        void* u = items[i];
        if (u && !seen.insert(dl::At<int>(u, fId)).second) {
            drop.push_back(u);
            continue;
        }
        items[k++] = u;
    }
    if (drop.empty()) return;
    *(int*)((uint8_t*)l + 8) = k;
    for (void* o : drop) dl::Call(A.mObjFree, (uint32_t)(uintptr_t)o);
    Log("air units: %d repeated units of the save dropped (%d -> %d; a save of an earlier version)", (int)drop.size(), n, k);
}

int IntelReportCount(int* unique) {
    void* l = IntelList(BuildCamp());
    if (!l) return -1;
    int count = dl::At<int>(l, A.fSLCount);
    if (unique) {
        std::set<std::wstring> u;
        void** items = dl::At<void**>(l, A.fSLList);
        for (int i = 0; items && i < count; i++) {
            std::wstring s = dl::ReadUStr(items[i * 2]);
            if (!s.empty()) u.insert(s);
        }
        *unique = (int)u.size();
    }
    return count;
}

// ---------------------------------------------------------------------------------------------
// Victory points on every load. LoadBuildCampaign sets PlayerNation := BuilderNations[0] (1.01.44: 0xc9a69d) and ends
// its "Setting data" step with CheckBlockadeStatus (0xccc1c4, called at 0xc9abeb) to set the nations' Blockaded flags,
// which the save doesn't keep. The routine also credits blockades, as in the month (its other two callers are DoTurn
// and DoStrategicTurn): while the war counter is above 0, for each nation 1..8 at war with PlayerNation (Tension > 40),
// PlayerNation.VP += (Random(3) + FleetSize) * 20 when that nation is blockaded, and EnemyVP += the same when
// PlayerNation is. So every load of a campaign with a blockade gave nation 0 (or the host's enemies) points the save
// never had, 60-100 per blockade with FleetSize 3. The game loads once per process; the mod reloads the campaign in the
// same process every month (host: after merging the turns; client: the new month) and at every live battle load, so the
// host's VP grew with each reload and a joined player saw nation 0 above the host's own numbers. The load's call still
// runs (its flags, and its Random draws: the same dice as before); the VP and EnemyVP it changed are put back. The
// month's own calls are not touched. Only in multiplayer: while a session is up (hosting or joined) and for the mod's
// own reloads (game::LoadCampaign; also outside a session, e.g. a joined player back to its slot after a live battle
// that outlived the session). A load without a session (the title screen, the host's first load before hosting, the
// resume after a restart) stays the game's own: one credit per start, as in single player.
static const int kBlockadeSlots = 4;
static int g_blockadeDepth = 0;
static int g_modReload = 0;  // inside game::LoadCampaign for one of the mod's own reloads
static struct BlockadeCtx {
    bool load;
    bool guard;  // put back (a session is up, or the mod's own reload); else only logged
    bool seen;   // a call of the routine (load or month): BlockadeChecked is told afterwards
    bool isLoad;
    int n;
    void* nation[16];
    int vp[16];
    int enemyVP;
} g_blockade[kBlockadeSlots];
static void(__stdcall* g_blockadeChecked)(void* form, bool load) = nullptr;

void SetBlockadeCheckedHook(void(__stdcall* fn)(void* form, bool load)) { g_blockadeChecked = fn; }

extern "C" int __stdcall CB_BeforeBlockade(void* self, uintptr_t ret) {
    int d = g_blockadeDepth++;
    if (d < 0 || d >= kBlockadeSlots) return d;
    BlockadeCtx& c = g_blockade[d];
    c.load = false;
    c.seen = self != nullptr;
    c.isLoad = ret && ret == A.loadBlockadeRet;
    if (!self || !ret || ret != A.loadBlockadeRet) return d;
    c.guard = g_modReload > 0 || hookcb::InSession();
    __try {
        void* l = dl::At<void*>(self, A.fBuilderNations);
        int n = dl::ListCount(l);
        c.n = n < 0 ? 0 : n > 16 ? 16 : n;
        for (int i = 0; i < c.n; i++) {
            c.nation[i] = dl::ListItem(l, i);
            c.vp[i] = c.nation[i] ? dl::At<int>(c.nation[i], A.fVP) : 0;
        }
        c.enemyVP = dl::At<int>(self, A.fEnemyVP);
        c.load = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        c.load = false;
    }
    return d;
}

// What the load's blockade check changed (put back by RestoreLoadVP when `restore`), for the log.
struct LoadVPChange {
    int n = 0, nb = 0;
    int nation[16], before[16], after[16], blockaded[16];
    bool enemy = false;
    int enemyBefore = 0, enemyAfter = 0;
};

static bool RestoreLoadVP(const BlockadeCtx& c, void* self, bool restore, LoadVPChange& r) {
    __try {
        for (int i = 0; i < c.n; i++) {
            if (!c.nation[i]) continue;
            int& v = dl::At<int>(c.nation[i], A.fVP);
            if (v != c.vp[i]) {
                r.nation[r.n] = i;
                r.before[r.n] = c.vp[i];
                r.after[r.n++] = v;
                if (restore) v = c.vp[i];
            }
            if (A.fBlockaded >= 0 && dl::At<uint8_t>(c.nation[i], A.fBlockaded)) r.blockaded[r.nb++] = i;
        }
        int& e = dl::At<int>(self, A.fEnemyVP);
        if (e != c.enemyVP) {
            r.enemy = true;
            r.enemyBefore = c.enemyVP;
            r.enemyAfter = e;
            if (restore) e = c.enemyVP;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

static void AfterBlockadeLoad(int slot, void* self);

// B5 (warfx): every call of the routine is told afterwards, the load's after its victory points were put back.
extern "C" void __stdcall CB_AfterBlockade(int slot, void* self) {
    if (slot < 0) return;
    g_blockadeDepth = slot;  // also drops frames left behind by an exception
    if (slot >= kBlockadeSlots) return;
    bool seen = g_blockade[slot].seen, isLoad = g_blockade[slot].isLoad;
    g_blockade[slot].seen = false;
    AfterBlockadeLoad(slot, self);
    if (seen && g_blockadeChecked) {
        __try {
            g_blockadeChecked(self, isLoad);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("WARNING: the war effects after the game's blockade check failed (exception)");
        }
    }
}

static void AfterBlockadeLoad(int slot, void* self) {
    if (!g_blockade[slot].load) return;
    g_blockade[slot].load = false;
    bool guard = g_blockade[slot].guard;
    LoadVPChange r;
    if (!RestoreLoadVP(g_blockade[slot], self, guard, r)) {
        Log("WARNING: load: the victory points after the game's blockade check could not be read");
        return;
    }
    if (!r.n && !r.enemy) return;
    try {
        // put back: "<after the check> -> <the save's>"; kept: "<the save's> -> <after the check>"
        auto pair = [guard](int saved, int now) {
            return guard ? std::to_string(now) + " -> " + std::to_string(saved)
                         : std::to_string(saved) + " -> " + std::to_string(now);
        };
        std::string s;
        for (int i = 0; i < r.n; i++)
            s += (s.empty() ? "" : ", ") + std::string("nation ") + std::to_string(r.nation[i]) + " " +
                 pair(r.before[i], r.after[i]);
        if (r.enemy) s += (s.empty() ? "" : ", ") + std::string("EnemyVP ") + pair(r.enemyBefore, r.enemyAfter);
        std::string b;
        for (int i = 0; i < r.nb; i++) b += " " + std::to_string(r.blockaded[i]);
        if (guard)
            Log("load: the game's blockade check gave victory points the save doesn't have; put back: %s (blockaded:%s)",
                s.c_str(), b.empty() ? " none" : b.c_str());
        else
            Log("load: the game's blockade check gave victory points: %s (blockaded:%s); kept: no multiplayer session (a "
                "load outside a session is the game's own)",
                s.c_str(), b.empty() ? " none" : b.c_str());
    } catch (...) {
    }
}

extern "C" void* o_CheckBlockade = nullptr;

// CheckBlockadeStatus(Self=EAX): a register procedure without stack arguments. The caller's return address tells the
// load's call apart from the month's.
extern "C" __declspec(naked) void d_CheckBlockade() {
    __asm {
        push eax
        push dword ptr [esp + 4]
        push eax
        call CB_BeforeBlockade
        push eax
        mov eax, [esp + 4]
        call dword ptr [o_CheckBlockade]
        call CB_AfterBlockade
        ret
    }
}

// ---------------------------------------------------------------------------------------------
// Doctrine (TdlgDoctrine): new training priorities and missile storage policies wait in Pending* fields for a
// number of months. The game's ReduceTimeLimits counts them down and applies them for the host's nation only.

bool DoctrineAvailable() { return A.doctrine && BuildCamp(); }

DoctrineState GetDoctrine(int nationIdx) {
    DoctrineState d;
    void* n = Nation(nationIdx);
    if (!A.doctrine || !n) return d;
    d.valid = true;
    for (int i = 0; i < 4; i++) {
        d.training[i] = dl::At<uint8_t>(n, A.fTraining + i);
        d.pending[i] = dl::At<uint8_t>(n, A.fPendingTraining + i);
    }
    d.pendingMonths = dl::At<int>(n, A.fPendingTrainingTime);
    d.missileStorage = dl::At<int>(n, A.fMissileStorage);
    d.pendingMissileStorage = dl::At<int>(n, A.fPendingMissileStorage);
    d.missileMonths = dl::At<int>(n, A.fMissileStorageTime);
    return d;
}

void SetPendingTraining(int nationIdx, const uint8_t pending[4], int months) {
    void* n = Nation(nationIdx);
    if (!A.doctrine || !n) return;
    for (int i = 0; i < 4; i++) dl::At<uint8_t>(n, A.fPendingTraining + i) = pending[i] ? 1 : 0;
    dl::At<int>(n, A.fPendingTrainingTime) = months;
}

void SetPendingMissileStorage(int nationIdx, int policy, int months) {
    void* n = Nation(nationIdx);
    if (!A.doctrine || !n) return;
    dl::At<int>(n, A.fPendingMissileStorage) = policy;
    dl::At<int>(n, A.fMissileStorageTime) = months;
}

// One month of the game's own countdown (ReduceTimeLimits), for any nation.
DoctrineChange AdvanceDoctrine(int nationIdx) {
    DoctrineChange c;
    void* n = Nation(nationIdx);
    if (!A.doctrine || !n) return c;
    int& trainingTime = dl::At<int>(n, A.fPendingTrainingTime);
    if (trainingTime == 1) {
        bool any = false;
        for (int i = 0; i < 4; i++) any |= dl::At<uint8_t>(n, A.fPendingTraining + i) != 0;
        if (any) {
            for (int i = 0; i < 4; i++) {
                dl::At<uint8_t>(n, A.fTraining + i) = dl::At<uint8_t>(n, A.fPendingTraining + i);
                dl::At<uint8_t>(n, A.fPendingTraining + i) = 0;
            }
            c.trainingApplied = true;
        }
    }
    if (trainingTime > 0) trainingTime--;
    int& missileTime = dl::At<int>(n, A.fMissileStorageTime);
    if (missileTime == 1) {
        dl::At<int>(n, A.fMissileStorage) = dl::At<int>(n, A.fPendingMissileStorage);
        dl::At<int>(n, A.fPendingMissileStorage) = 0;
        c.missilesApplied = true;
    }
    if (missileTime > 0) missileTime--;
    return c;
}

// ---------------------------------------------------------------------------------------------
// Random events (the "Event - <month>" dialogs). The game rolls them for the host's nation only and treats
// nation 0 as "the player": effects change the player's own fields, and each other nation's `Tension`/`Allied`/
// `TechSharing` (its relation with nation 0). To run an event for a joined player's nation K on the player's PC,
// the nation list is temporarily reordered so K is at index 0 (and nation 0 at index K), and every nation's
// relation fields temporarily hold its relation with K. Afterwards everything is put back and the relation
// changes are returned, for the host to apply to the K pairs.

static const int kEventSlots = 100, kEventRecSize = 100;
static const int kEvCond = 0x50, kEvUsed = 0x54, kEvAnswer = 8, kEvAnswerSize = 24, kEvAnswerCond = 0x14;

static uint8_t* EventRec(void* form, int i) { return (uint8_t*)form + A.fEvents + i * kEventRecSize; }

bool EventsAvailable() { return A.events && BuildCamp() && NationCount() >= 9 && A.warVar; }

// Events whose outcome depends on the host's own war, on campaign-wide treaties or on other nations' ships and
// colonies cannot be resolved on a joined player's PC.
static bool EventNeedsHost(void* form, int i) {
    uint8_t* r = EventRec(form, i);
    int cond = *(int*)(r + kEvCond);
    if (cond == 25 || (cond >= 30 && cond <= 40)) return true;
    for (int a = 0; a < 3; a++) {
        int c = *(int*)(r + kEvAnswer + a * kEvAnswerSize + kEvAnswerCond);
        if (c == 20 || c == 25 || c == 26 || c == 27 || c == 40 || c == 41 || (c >= 50 && c <= 56) || c == 60 ||
            c == 61 || c == 65)
            return true;
    }
    return false;
}

bool GetEventInfo(int i, int& cond, std::wstring& caption, bool& hostOnly) {
    void* f = BuildCamp();
    if (!A.events || !f || i < 0 || i >= kEventSlots) return false;
    uint8_t* r = EventRec(f, i);
    caption = dl::ReadUStr(*(void**)r);
    if (caption.empty()) return false;
    cond = *(int*)(r + kEvCond);
    hostOnly = EventNeedsHost(f, i);
    return true;
}

static bool g_evActive = false;  // a joined player's event is being rolled (likelihood veto active)

struct EvState {
    void** arr;
    void* obj[9];
    int K;
    int origT[9], origA[9], origTS[9];
    int war;
    int hostUsed[kEventSlots];
    bool swapped;
};
static EvState g_ev;
static bool g_pvOn = false;  // relations v2, B3: the player's routines run in K's view (RunPlayerTurn)
static bool g_pvSkip = false;  // ... and the relation routines run (surprise landings and attacks are left out)
static bool g_pvSurprise = false;  // ... 0.6.0: with a gen 2 host the surprise attacks and takeovers are recorded
static bool g_pvSkipSA = false;    // ... without it DoSurpriseAttack does nothing (B3)
static bool g_pvHuman[9];    // ... and the view indices of the other nations people play (the host's nation at K)

bool EventActive() { return g_ev.swapped || g_pvOn || nplay::ViewActive(); }

static void EvRestore() {
    if (!g_ev.swapped) return;
    g_ev.arr[0] = g_ev.obj[0];
    g_ev.arr[g_ev.K] = g_ev.obj[g_ev.K];
    for (int i = 0; i < 9; i++) {
        dl::At<int>(g_ev.obj[i], A.fTension) = g_ev.origT[i];
        dl::At<int>(g_ev.obj[i], A.fAllied) = g_ev.origA[i];
        dl::At<int>(g_ev.obj[i], A.fTechSharing) = g_ev.origTS[i];
    }
    *A.warVar = g_ev.war;
    void* f = BuildCamp();
    for (int j = 0; f && j < kEventSlots; j++) *(int*)(EventRec(f, j) + kEvUsed) = g_ev.hostUsed[j];
    g_ev.swapped = false;
    g_evActive = false;
}

// Runs the game's HandleEvent; if a game exception unwinds through here, the nation list is put back first.
static int EvRollGuarded(void* form, int forced, const int* order, int n) {
    int shown = -1;
    __try {
        if (forced >= 0) {
            if (dl::Call(A.mHandleEvent, (uint32_t)(uintptr_t)form, (uint32_t)forced, 1) & 0xFF) shown = forced;
        } else {
            for (int t = 0; t < n && shown < 0; t++)
                if (dl::Call(A.mHandleEvent, (uint32_t)(uintptr_t)form, (uint32_t)order[t], 0) & 0xFF) shown = order[t];
        }
    } __finally {
        if (AbnormalTermination()) EvRestore();
    }
    return shown;
}

// Player-nation fields an event answer may change (all saved in the player's [NationK] section).
static const char* kEvDeltaFields[] = {"Prestige", "BudgetModifier", "BaseResources", "Funds",
                                       "UnrestLevel", "TechLeakRisk", "Corruption"};
static const char* kEvSetFields[] = {"BuildConstraint", "BuildConstraintTime"};

ClientEventResult RunClientEvent(int K, int forcedIdx, int used[100]) {
    ClientEventResult res;
    void* f = BuildCamp();
    void* list = NationList();
    if (!EventsAvailable() || !f || !list || K < 1 || K > 8 || PlayerNation() != Nation(K) || EventActive()) {
        res.error = "events unavailable";
        return res;
    }
    if (forcedIdx >= 0) {
        int cond;
        std::wstring cap;
        bool hostOnly;
        if (!GetEventInfo(forcedIdx, cond, cap, hostOnly)) {
            res.error = "no such event";
            return res;
        }
        if (hostOnly) {
            res.error = "event needs the host";
            return res;
        }
    }
    g_ev.arr = dl::At<void**>(list, 4);
    g_ev.K = K;
    int tmpT[9], tmpA[9];
    bool kAtWar = false;
    for (int i = 0; i < 9; i++) {
        g_ev.obj[i] = g_ev.arr[i];
        g_ev.origT[i] = dl::At<int>(g_ev.obj[i], A.fTension);
        g_ev.origA[i] = dl::At<int>(g_ev.obj[i], A.fAllied);
        g_ev.origTS[i] = dl::At<int>(g_ev.obj[i], A.fTechSharing);
        tmpT[i] = i == K ? 0 : Tension(K, i);
        tmpA[i] = i == K ? 0 : AllianceMonths(K, i);
        if (i != K && tmpT[i] > 40) kAtWar = true;
    }
    g_ev.war = *A.warVar;
    for (int j = 0; j < kEventSlots; j++) g_ev.hostUsed[j] = *(int*)(EventRec(f, j) + kEvUsed);
    std::map<std::string, int> before;
    for (const char* n : kEvDeltaFields) before[n] = dl::At<int>(g_ev.obj[K], dl::Field("TBuilderNation", n));
    for (const char* n : kEvSetFields) before[n] = dl::At<int>(g_ev.obj[K], dl::Field("TBuilderNation", n));
    int fType = dl::Field("TBuilderNation", "BuildConstraintType");
    int typeBefore = fType >= 0 ? dl::At<uint8_t>(g_ev.obj[K], fType) : 0;

    // The player's view: relations with K, K's war state, K's own event cooldowns, K at index 0.
    for (int i = 0; i < 9; i++) {
        dl::At<int>(g_ev.obj[i], A.fTension) = tmpT[i];
        dl::At<int>(g_ev.obj[i], A.fAllied) = tmpA[i];
        dl::At<int>(g_ev.obj[i], A.fTechSharing) = 0;
    }
    // K's own war counter when the host sent it (it is in place already, see LoadCampaign), else a guess from the host's.
    bool ownKnown = g_ownWar != INT_MIN && (g_ownWar > 0) == kAtWar;
    *A.warVar = ownKnown ? g_ownWar : kAtWar ? (g_ev.war > 0 ? g_ev.war : 1) : (g_ev.war > 0 ? -12 : g_ev.war);
    for (int j = 0; j < kEventSlots; j++) *(int*)(EventRec(f, j) + kEvUsed) = used[j];
    g_ev.swapped = true;
    g_ev.arr[0] = g_ev.obj[K];
    g_ev.arr[K] = g_ev.obj[0];
    g_evActive = true;

    static std::mt19937 rng((unsigned)(GetTickCount() * 2654435761u));
    int order[101];
    for (int& o : order) o = std::uniform_int_distribution<int>(0, kEventSlots - 1)(rng);
    int shown = EvRollGuarded(f, forcedIdx, order, 101);

    int afterT[9], afterA[9];
    for (int i = 0; i < 9; i++) {
        afterT[i] = dl::At<int>(g_ev.obj[i], A.fTension);
        afterA[i] = dl::At<int>(g_ev.obj[i], A.fAllied);
    }
    for (int j = 0; j < kEventSlots; j++) used[j] = *(int*)(EventRec(f, j) + kEvUsed);
    EvRestore();

    res.shown = shown >= 0;
    res.eventIdx = shown;
    if (shown >= 0) res.caption = dl::ReadUStr(*(void**)EventRec(f, shown));
    for (int i = 0; i < 9; i++) {
        res.alliance[i] = -1;
        if (i == K) {
            if (afterT[i] != tmpT[i]) Log("event: own relation entry changed by %d (ignored)", afterT[i] - tmpT[i]);
            continue;
        }
        res.tension[i] = afterT[i] - tmpT[i];
        if (afterA[i] != tmpA[i]) res.alliance[i] = afterA[i];
    }
    for (const char* n : kEvDeltaFields) {
        int d = dl::At<int>(g_ev.obj[K], dl::Field("TBuilderNation", n)) - before[n];
        if (d) res.fieldDelta[n] = d;
    }
    for (const char* n : kEvSetFields) {
        int v = dl::At<int>(g_ev.obj[K], dl::Field("TBuilderNation", n));
        if (v != before[n]) res.fieldSet[n] = v;
    }
    if (fType >= 0 && dl::At<uint8_t>(g_ev.obj[K], fType) != typeBefore)
        res.fieldSet["BuildConstraintType"] = dl::At<uint8_t>(g_ev.obj[K], fType);
    return res;
}

bool AdjustNationField(int nationIdx, const std::string& field, int value, bool isDelta) {
    void* n = Nation(nationIdx);
    int off = dl::Field("TBuilderNation", field.c_str());
    if (!n || off < 0) return false;
    if (field == "BuildConstraintType")
        dl::At<uint8_t>(n, off) = (uint8_t)(isDelta ? dl::At<uint8_t>(n, off) + value : value);
    else
        dl::At<int>(n, off) = isDelta ? dl::At<int>(n, off) + value : value;
    return true;
}

int NationIntField(int nationIdx, const std::string& field) {
    void* n = Nation(nationIdx);
    int off = dl::Field("TBuilderNation", field.c_str());
    if (!n || off < 0) return INT_MIN;
    return field == "BuildConstraintType" ? dl::At<uint8_t>(n, off) : dl::At<int>(n, off);
}

// ---------------------------------------------------------------------------------------------
// Peace treaties between players

bool TreatiesAvailable() { return A.treaties && BuildCamp() && NationCount() >= 9; }

int WarScale() { return A.scaleVar ? *A.scaleVar : 0; }

int NationVP(int idx) {
    void* n = Nation(idx);
    return A.treaties && n ? dl::At<int>(n, A.fVP) : 0;
}

int EnemyVP() {
    void* f = BuildCamp();
    return A.treaties && f ? dl::At<int>(f, A.fEnemyVP) : 0;
}

void SetNationVP(int idx, int vp) {
    void* n = Nation(idx);
    if (A.treaties && n) dl::At<int>(n, A.fVP) = vp;
}

bool AtWarWithAnyone(int idx) {
    for (int j = 0; j <= 8 && j < NationCount(); j++)
        if (j != idx && AtWar(idx, j)) return true;
    return false;
}

void ShowEnemyVP(int vp) {
    void* f = BuildCamp();
    if (!A.treaties || !f) return;
    dl::At<int>(f, A.fEnemyVP) = vp;
    if (A.mUpdateVPNumbers) dl::Call(A.mUpdateVPNumbers, (uint32_t)(uintptr_t)f);
}

std::string DebugBattleAftermath(int opp, int vp0, int vp1, const std::string& level) {
    void* f = BuildCamp();
    if (!TreatiesAvailable() || !A.mBattleAftermath || !f) return "error treaties unavailable";
    if (opp < 1 || opp > 8 || EventActive() || BattleActive()) return "error not now";
    int h0 = NationVP(0), e0 = EnemyVP(), o0 = NationVP(opp);
    dl::At<int>(f, A.fBattleOpp) = opp;
    dl::ConstUStr lv(U2W(level));
    uint32_t stack[1] = {lv.ptr()};
    dl::Call(A.mBattleAftermath, (uint32_t)(uintptr_t)f, (uint32_t)vp0, (uint32_t)vp1, 1, stack);  // through the hook
    char buf[160];
    snprintf(buf, sizeof(buf), "ok host=%d->%d enemy=%d->%d opp=%d->%d", h0, NationVP(0), e0, EnemyVP(), o0,
             NationVP(opp));
    return buf;
}

template <class Fn>
static void ForEachPossession(Fn fn) {
    void* f = BuildCamp();
    if (!A.treaties || !f) return;
    void* areas = dl::At<void*>(f, A.fMapAreas);
    for (int a = 0; a < dl::ListCount(areas); a++) {
        void* area = dl::ListItem(areas, a);
        if (!area) continue;
        void* poss = dl::At<void*>(area, A.fAreaPossessions);
        for (int p = 0; p < dl::ListCount(poss); p++) {
            void* ps = dl::ListItem(poss, p);
            if (ps) fn(area, ps);
        }
    }
}

static PossessionInfo PossInfo(void* area, void* ps) {
    PossessionInfo i;
    i.name = dl::ReadUStr(dl::At<void*>(ps, A.fPossName));
    i.area = dl::ReadUStr(dl::At<void*>(area, A.fAreaName));
    i.value = dl::At<int>(ps, A.fPossValue);
    i.oil = dl::At<uint8_t>(ps, A.fPossOil) != 0;
    i.owner = NationIndex(dl::At<void*>(ps, A.fPossOwner));
    return i;
}

std::vector<PossessionInfo> Possessions(int ownerIdx) {
    std::vector<PossessionInfo> out;
    void* owner = Nation(ownerIdx);
    if (!owner) return out;
    ForEachPossession([&](void* area, void* ps) {
        if (dl::At<void*>(ps, A.fPossOwner) == owner) out.push_back(PossInfo(area, ps));
    });
    return out;
}

// The possessions a winner may claim, with the rules of the game's own peace dialog (SelectPossessionsAfterWar):
// owned by the loser, value below 11, in a sea area next to the winner's own possessions.
std::vector<PossessionInfo> ClaimablePossessions(int winnerIdx, int loserIdx) {
    std::vector<PossessionInfo> out;
    void* f = BuildCamp();
    void* winner = Nation(winnerIdx);
    void* loser = Nation(loserIdx);
    if (!TreatiesAvailable() || !winner || !loser || winner == loser) return out;
    ForEachPossession([&](void* area, void* ps) {
        if (dl::At<void*>(ps, A.fPossOwner) != loser || dl::At<int>(ps, A.fPossValue) >= 11) return;
        if (!(dl::Call(A.mAdjacent, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)area, (uint32_t)(uintptr_t)winner) & 0xFF))
            return;
        out.push_back(PossInfo(area, ps));
    });
    return out;
}

static void* StringListCreate() {
    return (void*)(uintptr_t)dl::Call(A.mSLCreate, (uint32_t)(uintptr_t)dl::ClassVmt("TStringList"), 1);
}

static void StringListAdd(void* sl, const std::wstring& s) {
    dl::ConstUStr u(s);
    dl::Call(A.mSLAdd, (uint32_t)(uintptr_t)sl, u.ptr());
}

static void ShowTreatyGuarded(void* form, void* sl) {
    __try {
        dl::Call(A.mShowTreaty, (uint32_t)(uintptr_t)form, (uint32_t)(uintptr_t)sl);
    } __finally {
        dl::Call(A.mFree, (uint32_t)(uintptr_t)sl);
    }
}

void ShowPeaceTreaty(const std::vector<std::wstring>& lines) {
    void* f = BuildCamp();
    if (!TreatiesAvailable() || !f || lines.empty() || EventActive()) return;
    void* sl = StringListCreate();
    if (!sl) return;
    for (auto& l : lines) StringListAdd(sl, l);
    ShowTreatyGuarded(f, sl);
}

static std::wstring ShipTransferText(void* f, void* from, void* to) {
    void* text = nullptr;  // Delphi string result (var parameter)
    uint32_t stack[1] = {(uint32_t)(uintptr_t)&text};
    dl::Call(A.mRemoveShipInPeace, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)from, (uint32_t)(uintptr_t)to, 1, stack);
    return dl::ReadUStr(text);
}

TreatyResult ApplyTreaty(int winnerIdx, int loserIdx, int level, const std::vector<std::wstring>& claims, unsigned seed) {
    TreatyResult r;
    void* f = BuildCamp();
    void* winner = Nation(winnerIdx);
    void* loser = Nation(loserIdx);
    if (!TreatiesAvailable() || !winner || !loser || winner == loser || level <= 0) return r;
    // Land: the winner's claims (still valid ones), otherwise the game's rules with a random choice.
    int budget = level >= 2 ? 8 : 4;  // the game's dialog budget is 2 x its win level (minor 2, major 4)
    auto claimable = ClaimablePossessions(winnerIdx, loserIdx);
    std::vector<std::wstring> take;
    int used = 0;
    if (!claims.empty()) {
        for (auto& c : claims)
            for (auto& p : claimable)
                if (p.name == c && used + p.value <= budget) {
                    take.push_back(p.name);
                    used += p.value;
                    break;
                }
    } else {
        std::mt19937 rng(seed);
        std::shuffle(claimable.begin(), claimable.end(), rng);
        std::stable_sort(claimable.begin(), claimable.end(),
                         [](const PossessionInfo& a, const PossessionInfo& b) { return a.value > b.value; });
        for (auto& p : claimable)
            if (used + p.value <= budget) {
                take.push_back(p.name);
                used += p.value;
            }
        r.automatic = true;
    }
    std::wstring wName = NationName(winnerIdx), lName = NationName(loserIdx);
    for (auto& name : take) {
        void* found = nullptr;
        ForEachPossession([&](void*, void* ps) {
            if (!found && dl::At<void*>(ps, A.fPossOwner) == loser && dl::ReadUStr(dl::At<void*>(ps, A.fPossName)) == name)
                found = ps;
        });
        if (!found) continue;
        uint32_t stack[1] = {0};  // RemoveAllCoastalArtillery = False (as the game's peace dialog)
        dl::Call(A.mChangeOwner, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)found, (uint32_t)(uintptr_t)winner, 1, stack);
        r.ceded.push_back(name);
        r.report.push_back(name + L" is ceded from " + lName + L" to " + wName + L".");
    }
    // Ships: the loser hands recent capital ships over as war reparations (the game's own selection).
    int handovers = level >= 2 ? 3 : 1;
    for (int i = 0; i < handovers; i++) {
        std::wstring t = ShipTransferText(f, loser, winner);
        if (t.empty()) continue;
        r.ships++;
        // The game runs two sentences together: "<ship> handed over to <winner> as war reparation She is renamed
        // <name> in <adjective> service." Report them as two lines.
        for (auto& ch : t)
            if (ch == L'\r' || ch == L'\n') ch = L' ';
        size_t cut = t.find(L" She is renamed ");
        std::wstring parts[2] = {cut == std::wstring::npos ? t : t.substr(0, cut),
                                 cut == std::wstring::npos ? L"" : t.substr(cut + 1)};
        for (auto& part : parts) {
            while (!part.empty() && part.back() == L' ') part.pop_back();
            if (!part.empty()) r.report.push_back(part.back() == L'.' ? part : part + L".");
        }
    }
    // Prestige and economy, as the game's peace treaties. The loser's loss is the game's PrestigeWarLoss, which
    // can't be called for another nation (its proportional part always hits PlayerNation, i.e. the host):
    // ProportionalPrestigeLoss(10) = P div 10 when P >= 20, otherwise 1 (not below 0); a major defeat costs 1 more,
    // and 1 more again above 20.
    dl::At<int>(winner, A.fPrestige) += level >= 2 ? 2 : 1;
    int& prestige = dl::At<int>(loser, A.fPrestige);
    int loss = prestige >= 20 ? prestige / 10 : 1;
    prestige = std::max(0, prestige - loss);
    if (level >= 2) {
        prestige--;
        if (prestige > 20) prestige--;
    }
    int& res = dl::At<int>(loser, A.fBaseResources);
    int cut = level >= 2 ? 500 : 100;
    r.resourcesCut = std::min(cut, std::max(0, res / 2));
    res -= r.resourcesCut;
    Log("treaty %d over %d (level %d): %d possession(s)%s, %d ship(s), prestige +%d, resources -%d", winnerIdx, loserIdx,
        level, (int)r.ceded.size(), r.automatic ? " (automatic)" : " (claimed)", r.ships, level >= 2 ? 2 : 1,
        r.resourcesCut);
    return r;
}

// ---------------------------------------------------------------------------------------------
// Victory points of each battle, for the war score of player wars. The game adds a battle's points to the two
// nations' VP (AI wars) or to the host nation's VP and EnemyVP (the host's wars); these hooks measure the change
// around each battle and report it with the two nations that fought. Battles can run inside other hooked battles
// (a raider battle of the host's ships ends with HandleBattleAftermath; the month can go on inside the aftermath
// of a tactical battle): each frame carries its own slot, and points reported by an inner battle are taken out of
// the outer frames, so every battle counts once.

static int g_vpDepth = 0;
static const int kVPSlots = 32;
static struct VPCtx {
    int kind, a, b;
    int vp[9];
    int enemyVP;
} g_vpStack[kVPSlots];

static void VPSnapshot(VPCtx& c) {
    for (int i = 0; i < 9; i++) c.vp[i] = NationVP(i);
    c.enemyVP = EnemyVP();
}

static bool VPReport(const VPCtx& c) {
    if (c.a < 0 || c.b < 0 || c.a > 8 || c.b > 8 || c.a == c.b) return false;
    int da, db;
    if (c.a == 0 || c.b == 0) {  // the host's side: its own VP; the enemy side: EnemyVP
        int host = NationVP(0) - c.vp[0], enemy = EnemyVP() - c.enemyVP;
        da = c.a == 0 ? host : enemy;
        db = c.b == 0 ? host : enemy;
    } else {
        da = NationVP(c.a) - c.vp[c.a];
        db = NationVP(c.b) - c.vp[c.b];
    }
    da = std::max(0, da);  // points only grow in a battle; a drop is a reset (peace) inside the frame
    db = std::max(0, db);
    if (da || db) hookcb::OnBattleVP(c.a, c.b, da, db);
    return true;
}

extern "C" int __stdcall CB_BeforeVP(int kind, uint32_t edx, uint32_t ecx, void* self) {
    int d = g_vpDepth++;
    if (d < 0 || d >= kVPSlots) return d;
    VPCtx& c = g_vpStack[d];
    c.kind = kind;
    c.a = c.b = -1;
    if (kind == 0 && g_battleSlotMode) g_battleAftermath = true;
    __try {
        if (!TreatiesAvailable()) return d;
        if (kind == 0) {  // HandleBattleAftermath: the host's battle against BattleOpponentIdx
            bool insideBattle = false;  // then the outer battle knows the two sides; it reports
            for (int k = 0; k < d; k++) insideBattle |= g_vpStack[k].kind != 0 && g_vpStack[k].a >= 0;
            if (!insideBattle) {
                c.a = 0;
                c.b = dl::At<int>(self, A.fBattleOpp);
            }
        } else if (kind == 1) {  // DoBattleBetweenAINations(NationA, NationB, ...)
            c.a = NationIndex((void*)(uintptr_t)edx);
            c.b = NationIndex((void*)(uintptr_t)ecx);
        } else {  // QuickResolveRaiderBattle(InterceptingShip, InterceptedShip, ...)
            int fShipNation = dl::Field("TCampaignShip", "Nation");
            if (fShipNation >= 0 && edx && ecx) {
                c.a = dl::At<int>((void*)(uintptr_t)edx, fShipNation);
                c.b = dl::At<int>((void*)(uintptr_t)ecx, fShipNation);
            }
        }
        VPSnapshot(c);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        c.a = c.b = -1;
    }
    return d;
}

extern "C" void __stdcall CB_AfterVP(int slot) {
    if (slot < 0) return;
    g_vpDepth = slot;  // also drops frames left behind by an exception
    if (slot >= kVPSlots) return;
    __try {
        VPCtx& c = g_vpStack[slot];
        if (c.a < 0 || c.b < 0 || !VPReport(c)) return;
        int now[9];
        for (int i = 0; i < 9; i++) now[i] = NationVP(i);
        int e = EnemyVP();
        for (int k = 0; k < slot; k++) {
            for (int i = 0; i < 9; i++) g_vpStack[k].vp[i] += now[i] - c.vp[i];
            g_vpStack[k].enemyVP += e - c.enemyVP;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void* o_BattleAftermath = nullptr;
extern "C" void* o_AIBattle = nullptr;
extern "C" void* o_RaiderBattle = nullptr;

// Register-convention methods with stack arguments (removed by the callee): report the registers and keep the
// returned slot in a local, call the game's routine with a copy of the stack arguments, report the slot, then
// return removing the caller's arguments. EAX/EDX/ECX reach the game unchanged; EAX (the result) is kept.
extern "C" __declspec(naked) void d_BattleAftermath() {  // (Self, VP0, VP1, aVictoryLevel)
    __asm {
        sub esp, 4
        push eax
        push ecx
        push edx
        push eax
        push ecx
        push edx
        push 0
        call CB_BeforeVP
        mov [esp + 12], eax
        pop edx
        pop ecx
        pop eax
        push dword ptr [esp + 8]
        call dword ptr [o_BattleAftermath]
        push eax
        push dword ptr [esp + 4]
        call CB_AfterVP
        pop eax
        add esp, 4
        ret 4
    }
}

extern "C" __declspec(naked) void d_AIBattle() {  // (Self, NationA, NationB, ShipCountA, ShipCountB, anArea): Boolean
    __asm {
        sub esp, 4
        push eax
        push ecx
        push edx
        push eax
        push ecx
        push edx
        push 1
        call CB_BeforeVP
        mov [esp + 12], eax
        pop edx
        pop ecx
        pop eax
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call dword ptr [o_AIBattle]
        push eax
        push dword ptr [esp + 4]
        call CB_AfterVP
        pop eax
        add esp, 4
        ret 12
    }
}

extern "C" __declspec(naked) void d_RaiderBattle() {  // (Self, InterceptingShip, InterceptedShip, SideIntercepted)
    __asm {
        sub esp, 4
        push eax
        push ecx
        push edx
        push eax
        push ecx
        push edx
        push 2
        call CB_BeforeVP
        mov [esp + 12], eax
        pop edx
        pop ecx
        pop eax
        push dword ptr [esp + 8]
        call dword ptr [o_RaiderBattle]
        push eax
        push dword ptr [esp + 4]
        call CB_AfterVP
        pop eax
        add esp, 4
        ret 4
    }
}

std::vector<ShipSnap> SnapshotShips(int nationIdx) {
    std::vector<ShipSnap> out;
    void* n = Nation(nationIdx);
    int c = ShipCount(n);
    for (int i = 0; i < c; i++) {
        void* s = Ship(n, i);
        if (!s) continue;
        out.push_back({s, dl::At<void*>(s, A.fShipOrderedArea), dl::At<void*>(s, A.fShipLocationArea),
                       dl::At<uint8_t>(s, A.fShipStatus)});
    }
    return out;
}

int RestoreShips(const std::vector<ShipSnap>& snaps) {
    int changed = 0;
    for (auto& s : snaps) {
        if (dl::At<void*>(s.ship, A.fShipOrderedArea) != s.ordered ||
            dl::At<void*>(s.ship, A.fShipLocationArea) != s.location ||
            dl::At<uint8_t>(s.ship, A.fShipStatus) != s.status)
            changed++;
        dl::At<void*>(s.ship, A.fShipOrderedArea) = s.ordered;
        dl::At<void*>(s.ship, A.fShipLocationArea) = s.location;
        dl::At<uint8_t>(s.ship, A.fShipStatus) = s.status;
    }
    return changed;
}

std::string BuildFingerprint() {
    uint32_t h = 2166136261u;
    auto mix = [&](uintptr_t v) {
        for (int i = 0; i < 4; i++) {
            h ^= (uint8_t)(v >> (i * 8));
            h *= 16777619u;
        }
    };
    mix((uintptr_t)dl::ClassVmt("TfrmBuildCamp") - dl::g_base);
    mix((uintptr_t)A.mTurnClick - dl::g_base);
    mix((uintptr_t)A.mSave - dl::g_base);
    mix((uintptr_t)A.mLoad - dl::g_base);
    mix(dl::g_size);
    char buf[16];
    snprintf(buf, sizeof(buf), "%08x", h);
    return buf;
}

// ---------------------------------------------------------------------------------------------
// Diplomacy

bool DiplomacyAvailable() { return A.diplomacy && BuildCamp() && NationCount() >= 9; }

static bool ValidPair(int a, int b) { return a != b && a >= 0 && b >= 0 && a <= 8 && b <= 8 && a < NationCount() && b < NationCount(); }

static int& TensionCell(int from, int to) {  // from's tension towards `to`
    return dl::At<int>(Nation(from), A.fTension + 4 * to);
}

int Tension(int a, int b) {
    if (!A.diplomacy || !ValidPair(a, b)) return 0;
    return a == 0 ? TensionCell(b, 0) : TensionCell(a, b);
}

void SetTensionRaw(int a, int b, int value) {
    if (!A.diplomacy || !ValidPair(a, b)) return;
    if (a == 0 || b == 0) {
        TensionCell(a == 0 ? b : a, 0) = value;
    } else {
        TensionCell(a, b) = value;
        TensionCell(b, a) = value;
    }
}

bool AtWar(int a, int b) { return Tension(a, b) > 40; }

bool AtWarWithAnyAI(int idx) {
    if (!A.diplomacy || idx <= 0 || idx > 8) return false;
    for (int j = 1; j <= 8 && j < NationCount(); j++)
        if (j != idx && TensionCell(idx, j) > 40) return true;
    return false;
}

int AllianceMonths(int a, int b) {
    if (!A.diplomacy || !ValidPair(a, b)) return 0;
    if (a == 0 || b == 0) return dl::At<int>(Nation(a == 0 ? b : a), A.fAllied);
    return dl::At<int>(Nation(a), A.fAIAlliance + 4 * (b - 1));
}

int WarCounter() { return A.warVar ? *A.warVar : 0; }
int* WarCell() { return A.warVar; }

void SetOwnWarCounter(int months) { g_ownWar = months; }
int OwnWarCounter() { return g_ownWar; }

int CountAtWarWithHost() {
    void* f = BuildCamp();
    return f && A.mCountAtWar ? (int)dl::Call(A.mCountAtWar, (uint32_t)(uintptr_t)f) : 0;
}

void StartWar(int a, int b) {
    void* f = BuildCamp();
    if (!f || !A.diplomacy || !ValidPair(a, b) || AtWar(a, b)) return;
    if (a == 0 || b == 0) {
        // Same as a tension-driven war: Tension=50, war counter, alliance with the host's nation cancelled.
        dl::Call(A.mWarStarts, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)Nation(a == 0 ? b : a));
    } else {
        dl::Call(A.mSetAIAlliance, (uint32_t)(uintptr_t)Nation(a), (uint32_t)b, 0);
        dl::Call(A.mSetAITension, (uint32_t)(uintptr_t)Nation(a), (uint32_t)b, 50);
    }
    Log("diplomacy: war %d-%d (tension %d, war counter %d)", a, b, Tension(a, b), WarCounter());
}

void MakePeace(int a, int b, int hostOutcome) {
    void* f = BuildCamp();
    if (!f || !A.diplomacy || !ValidPair(a, b) || !AtWar(a, b)) return;
    const int kPeaceTension = 5;
    if (a == 0 || b == 0) {
        int other = a == 0 ? b : a;
        if (CountAtWarWithHost() == 1 && A.mDoPeace && A.recYearCell && A.recMonthCell) {
            // The host's last war ends: run the game's own end-of-war routine, with this month's war record set to
            // the outcome (1 won, 2 lost, 3 compromise), as the game's own peace treaties do.
            int* rec = (int*)((uint8_t*)f + **A.recYearCell * 0x4B0 + **A.recMonthCell * 100 + A.recDisp);
            *rec = hostOutcome >= 1 && hostOutcome <= 3 ? hostOutcome : 3;
            dl::Call(A.mDoPeace, (uint32_t)(uintptr_t)f);
        }
        if (AtWar(0, other)) SetTensionRaw(0, other, kPeaceTension);  // separate peace while other wars go on
        if (CountAtWarWithHost() == 0 && A.warVar && *A.warVar > 0) *A.warVar = 0;
    } else {
        dl::Call(A.mSetAITension, (uint32_t)(uintptr_t)Nation(a), (uint32_t)b, kPeaceTension);
    }
    Log("diplomacy: peace %d-%d (tension %d, war counter %d)", a, b, Tension(a, b), WarCounter());
}

void SetAlliance(int a, int b, int months) {
    if (!A.diplomacy || !ValidPair(a, b)) return;
    if (a == 0 || b == 0)
        dl::At<int>(Nation(a == 0 ? b : a), A.fAllied) = months;
    else
        dl::Call(A.mSetAIAlliance, (uint32_t)(uintptr_t)Nation(a), (uint32_t)b, (uint32_t)months);
    Log("diplomacy: alliance %d-%d = %d", a, b, AllianceMonths(a, b));
}

// ---------------------------------------------------------------------------------------------
// Each player's own relations on the screen (the relations view)
//
// The game keeps relations from nation 0's point of view (every nation's `Tension`/`Allied` is its relation with
// nation 0; AITension/AIAlliance hold the rest) and its strategic screen shows BuilderNations[1..8] with those fields.
// On a joined player's PC PlayerNation is nation K, so the tension panel, its war and alliance marks, the flags and the
// war state showed the host's relations, and slot K showed K's own relation with the host under K's name. While one of
// these display routines runs, the view puts nation K at index 0 and the host's nation at index K, and every nation's
// Tension/Allied hold its relation with K (what RunClientEvent does for an event); everything is put back right after
// the routine, also when a game exception unwinds through it. Only display routines run in the view: the month and the
// player's orders never see it.

int ShownNationIndex() {
    void* l = NationList();
    void* pn = PlayerNation();
    if (!l || !pn || dl::ListCount(l) < 9 || dl::ListItem(l, 0) == pn) return 0;
    for (int i = 1; i <= 8; i++)
        if (dl::ListItem(l, i) == pn) return i;
    return 0;
}

static struct {
    void** arr;
    void* obj[9];
    int K;
    int T[9], Al[9];
    bool on;
} g_kv;
static int g_kvDepth = 0;

// Puts the view in place unless it is already there (nested display routines) or not wanted: the PC shows nation 0 as
// the player (the host, single player, a battle slot), or an event dialog has its own view. Pair with KViewEnd.
static bool KViewBegin() {
    if (g_kvDepth++ > 0 || !A.relView || EventActive()) return false;
    int K = ShownNationIndex();
    void* l = NationList();
    if (K < 1 || !l) return false;
    int rel[9], ally[9];
    for (int i = 0; i < 9; i++) {
        rel[i] = i == K ? 0 : Tension(K, i);
        ally[i] = i == K ? 0 : AllianceMonths(K, i);
    }
    g_kv.arr = dl::At<void**>(l, 4);
    g_kv.K = K;
    for (int i = 0; i < 9; i++) {
        g_kv.obj[i] = g_kv.arr[i];
        g_kv.T[i] = dl::At<int>(g_kv.obj[i], A.fTension);
        g_kv.Al[i] = dl::At<int>(g_kv.obj[i], A.fAllied);
    }
    for (int i = 0; i < 9; i++) {
        dl::At<int>(g_kv.obj[i], A.fTension) = rel[i];
        dl::At<int>(g_kv.obj[i], A.fAllied) = ally[i];
    }
    g_kv.arr[0] = g_kv.obj[K];
    g_kv.arr[K] = g_kv.obj[0];
    g_kv.on = true;
    return true;
}

static void KViewEnd() {
    if (g_kvDepth > 0) g_kvDepth--;
    if (g_kvDepth > 0 || !g_kv.on) return;
    g_kv.arr[0] = g_kv.obj[0];
    g_kv.arr[g_kv.K] = g_kv.obj[g_kv.K];
    for (int i = 0; i < 9; i++) {
        dl::At<int>(g_kv.obj[i], A.fTension) = g_kv.T[i];
        dl::At<int>(g_kv.obj[i], A.fAllied) = g_kv.Al[i];
    }
    g_kv.on = false;
}

static void* FormCtl(void* f, int off) { return f && off > 0 ? dl::At<void*>(f, off) : nullptr; }

static void SetCtlHint(void* ctl, const std::wstring& s) {  // System._UStrAsg copies the literal
    if (!ctl) return;
    dl::ConstUStr u(s);
    dl::Call(A.mUStrAsg, (uint32_t)(uintptr_t)((uint8_t*)ctl + A.fHint), u.ptr());
}

static void SetCtlVisible(void* ctl, bool v) {
    if (ctl) dl::Call(A.mSetVisible, (uint32_t)(uintptr_t)ctl, v ? 1 : 0);
}

static void SetCtlText(void* ctl, const std::wstring& s) {  // SetText -> SetTextBuf copies it
    if (!ctl) return;
    dl::ConstUStr u(s);
    dl::Call(A.mSetText, (uint32_t)(uintptr_t)ctl, u.ptr());
}

// In the view: the flags' hints, which name the panel's slots (UpdateUIValues sets them from nations 1-8 right before
// it shows the panel): the view's nation of each slot, " (Treaty)" while it is allied with the player, as the game does.
static void KViewFlagHints(void* f) {
    for (int i = 1; i <= 8; i++) {
        void* n = Nation(i);
        if (!n) continue;
        std::wstring h = dl::ReadUStr(dl::At<void*>(n, A.fNationName));
        if (dl::At<int>(n, A.fAllied) > 0) h += L" (Treaty)";
        SetCtlHint(FormCtl(f, A.fFlag[i]), h);
    }
}

static void ShowTensionInView(void* f, void* orig) {
    __try {
        if (KViewBegin()) KViewFlagHints(f);
        dl::Call(orig, (uint32_t)(uintptr_t)f);
    } __finally {
        KViewEnd();
    }
}

static void StrengthBarsInView(void* f, void* area, void* orig) {
    __try {
        KViewBegin();
        dl::Call(orig, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)area);
    } __finally {
        KViewEnd();
    }
}

// After SetValues (the flags): slot K shows the host's flag (the player's own flag already shows PlayerNation).
static void KViewFlagPicture(void* f) {
    int K = A.relView && !EventActive() ? ShownNationIndex() : 0;
    void* img = K > 0 ? FormCtl(f, A.fFlag[K]) : nullptr;
    void* host = Nation(0);
    if (!img || !host) return;
    void* file = nullptr;  // Delphi string result (small leak per call, acceptable)
    dl::Call(A.mFlagFile, (uint32_t)(uintptr_t)host, (uint32_t)(uintptr_t)&file);
    std::wstring path = dl::ReadUStr(file);
    void* pic = dl::At<void*>(img, A.fPicture);
    if (!path.empty() && pic && FileExists(path)) dl::Call(A.mPictureLoad, (uint32_t)(uintptr_t)pic, (uint32_t)(uintptr_t)file);
}

// After UpdateUIValues: the war state the game shows from the war counter (the "War results" box, the "At war for N
// months." line under the player's name, the name's "At peace for ..." hint) follows the player's own wars. Since the
// host sends the player's own war counter with the month (LoadCampaign puts it in place) the game's own lines are
// right; from an older host only the host's counter is known, so the line names the enemy instead.
static std::string g_kvLogged;

static void KViewWarState(void* f) {
    int K = A.relView && !EventActive() ? ShownNationIndex() : 0;
    if (K < 1) return;
    std::vector<int> enemies;
    std::string sig = "nation " + std::to_string(K) + " sees its own relations:";
    for (int j = 0; j <= 8; j++) {
        if (j == K) continue;
        int t = Tension(K, j);
        if (t > 40) enemies.push_back(j);
        sig += " " + W2U(NationName(j)) + " " + std::to_string(t) + (AllianceMonths(K, j) > 0 ? " (ally)" : "");
    }
    bool war = !enemies.empty();
    bool counted = g_ownWar != INT_MIN && A.warVar && (*A.warVar > 0) == war;
    if (counted) {
        sig += "; war counter " + std::to_string(*A.warVar);
    } else {
        std::wstring line;
        if (war)
            line = enemies.size() == 1 ? L"At war with " + NationName(enemies[0]) + L"."
                                       : L"At war with " + std::to_wstring(enemies.size()) + L" nations.";
        SetCtlVisible(FormCtl(f, A.fGbWar), war);
        SetCtlVisible(FormCtl(f, A.fLbWar), war);
        if (war) SetCtlText(FormCtl(f, A.fLbWar), line);
        SetCtlHint(FormCtl(f, A.fLbPlayer), war ? L"" : L"At peace.");
        sig += war ? "; " + W2U(line) : "; at peace";
    }
    if (sig != g_kvLogged) {
        g_kvLogged = sig;
        Log("relations view: %s", sig.c_str());
    }
}

std::string DebugRelView() {
    void* f = BuildCamp();
    if (!f || !A.relView) return "error relations view unavailable";
    auto hint = [](void* c) { return c ? W2U(dl::ReadUStr(dl::At<void*>(c, A.fHint))) : std::string("-"); };
    auto vis = [](void* c) { return c && dl::At<uint8_t>(c, A.fVisible) ? "1" : "0"; };
    auto text = [](void* c) {
        const wchar_t* p = c ? dl::At<const wchar_t*>(c, A.fText) : nullptr;
        return p ? W2U(std::wstring(p, wcsnlen(p, 200))) : std::string();
    };
    std::string s = "ok shown=" + std::to_string(ShownNationIndex()) + " slots=";
    for (int i = 1; i <= 8; i++)
        s += std::to_string(i) + ":" + hint(FormCtl(f, A.fFlag[i])) + "~" + hint(FormCtl(f, A.fShape[i])) + "~" +
             vis(FormCtl(f, A.fSlotWar[i])) + vis(FormCtl(f, A.fSlotAlly[i])) + vis(FormCtl(f, A.fSlotBlock[i])) + "; ";
    s += "war=" + std::string(vis(FormCtl(f, A.fGbWar))) + vis(FormCtl(f, A.fLbWar)) + "~" + text(FormCtl(f, A.fLbWar)) +
         "~" + hint(FormCtl(f, A.fLbPlayer));
    return s;
}

int StartBuildPhase() {
    void* f = BuildCamp();
    return f ? dl::At<uint8_t>(f, A.fStartBuild) : -1;
}

// ---------------------------------------------------------------------------------------------
// Relations v2, B2: the pairs of a joined nation K under the player's rules are left alone by the computer's rules.
//
// The game runs the relations between nations 1..8 by its AI rules (GAME-INTERNALS): CheckAITensionEvents (AI wars and
// peace, alliances, offers to the player), DoAITensionChanges, ChangeAITension, and the AI side of CheckStopPlayerAgression,
// CheckAllyTrouble, CheckBlitzkrieg, CheckChineseConcessions, CheckForceOnForeignStations, CreateArmsControlTreaty,
// CheckForRevolutionInEnemyNation, ReduceTimeLimits ("has negotiated a separate peace") and HandleAIPeace. While one of
// them runs on the host, every pair (K, j) looks neutral (tension 5: no war, no outbreak, no common-enemy or most-tense
// logic; no alliance) and is put back afterwards, so these routines neither change K's relations nor show messages or
// questions about them; ChangeAITension skips K's pairs at any time. The host's own player routines treat K as a
// player, not as a computer nation: during CheckForWar K's tension with the host reads 5 when they are at peace, K
// plans no war and has no alliances of its own (no declaration, surprise landing, volunteering, war plan or alliance
// quarrel in K's name; the other nations' decisions about K stay), and during RandomTensionChanges at least 6 (no
// alliance offer in K's name), the routine's own changes kept. K's relations then follow the player's routines on K's
// PC (B3), the players' diplomacy and the events.

static const int kMaskTension = 5;

static bool PlayerRules(int i) { return A.playerRules && i >= 1 && i <= 8 && hookcb::PlayerRulesNation(i); }

static int& AllianceCell(int from, int to) {  // from's alliance months with `to`, both in 1..8
    return dl::At<int>(Nation(from), A.fAIAlliance + 4 * (to - 1));
}

static std::set<std::string> g_maskLogged;  // routines already logged this month

static int g_aiMaskDepth = 0;
static struct {
    bool on;
    bool k[9];
    int t[9][9], al[9][9];
} g_aiMask;

static void AIMaskEnter(const char* routine) {
    if (g_aiMaskDepth++ > 0 || NationCount() < 9) return;
    g_aiMask.on = false;
    std::string ks;
    for (int k = 1; k <= 8; k++) {
        g_aiMask.k[k] = PlayerRules(k);
        if (g_aiMask.k[k]) ks += " " + std::to_string(k);
    }
    if (ks.empty()) return;
    for (int a = 1; a <= 8; a++)
        for (int b = 1; b <= 8; b++) {
            if (a == b || !(g_aiMask.k[a] || g_aiMask.k[b])) continue;
            g_aiMask.t[a][b] = TensionCell(a, b);
            g_aiMask.al[a][b] = AllianceCell(a, b);
            TensionCell(a, b) = kMaskTension;
            AllianceCell(a, b) = 0;
        }
    g_aiMask.on = true;
    if (g_maskLogged.insert(routine).second)
        Log("relations v2: %s runs without the pairs of nation(s)%s (player's rules)", routine, ks.c_str());
}

static void AIMaskLeave() {
    if (g_aiMaskDepth <= 0 || --g_aiMaskDepth > 0 || !g_aiMask.on) return;
    int dropped = 0;
    for (int a = 1; a <= 8; a++)
        for (int b = 1; b <= 8; b++) {
            if (a == b || !(g_aiMask.k[a] || g_aiMask.k[b])) continue;
            if (TensionCell(a, b) != kMaskTension || AllianceCell(a, b) != 0) dropped++;
            TensionCell(a, b) = g_aiMask.t[a][b];
            AllianceCell(a, b) = g_aiMask.al[a][b];
        }
    g_aiMask.on = false;
    if (dropped) Log("relations v2: %d change(s) of the computer's rules to the masked pairs dropped", dropped);
}

// The host's CheckForWar (kind 1) and RandomTensionChanges (kind 2) with K seen as a player; on a joined player's PC
// (B3, the routines in K's view) the same for the other nations people play: the host's nation and the other joined
// ones, so no war or alliance between players comes from these routines.
static struct {
    bool on;
    bool k[9];
    int T[9], W[9], al[9][9];
} g_pw;
static int g_pwKind = 0;

static bool MaskAsPlayer(int k) { return g_pvOn ? g_pvHuman[k] : PlayerRules(k); }

static void PlayerMaskEnter(int kind, const char* routine) {
    g_pw.on = false;
    g_pwKind = kind;
    if (NationCount() < 9) return;
    std::string ks;
    for (int k = 1; k <= 8; k++) {
        g_pw.k[k] = MaskAsPlayer(k);
        if (!g_pw.k[k]) continue;
        ks += " " + std::to_string(k);
        int& T = TensionCell(k, 0);
        int& W = dl::At<int>(Nation(k), A.fWillGoToWar);
        g_pw.T[k] = T;
        g_pw.W[k] = W;
        if (kind == 1) {
            if (T <= 40) T = kMaskTension;
            W = 0;
            for (int j = 1; j <= 8; j++)
                if (j != k) {
                    g_pw.al[k][j] = AllianceCell(k, j);
                    AllianceCell(k, j) = 0;
                }
        } else if (T <= 40 && T < 6) {
            T = 6;
        }
    }
    g_pw.on = !ks.empty();
    if (g_pw.on && g_maskLogged.insert(routine).second)
        Log("relations v2: %s sees nation(s)%s as players (player's rules)", routine, ks.c_str());
}

static void PlayerMaskLeave() {
    if (!g_pw.on) return;
    g_pw.on = false;
    for (int k = 1; k <= 8; k++) {
        if (!g_pw.k[k]) continue;
        int& T = TensionCell(k, 0);
        int base = g_pwKind == 1 ? kMaskTension : std::max(g_pw.T[k], 6);
        if (g_pw.T[k] <= 40 && T <= 40) {
            int now = std::max(0, std::min(40, g_pw.T[k] + (T - base)));  // the routine's own changes kept
            if (now != g_pw.T[k]) Log("relations v2: tension 0-%d %d -> %d (the host's routine)", k, g_pw.T[k], now);
            T = now;
        } else if (g_pw.T[k] <= 40) {
            Log("relations v2: WARNING: war 0-%d started inside the host's routine despite the mask", k);
        }
        if (g_pwKind == 1) {
            dl::At<int>(Nation(k), A.fWillGoToWar) = g_pw.W[k];
            for (int j = 1; j <= 8; j++)
                if (j != k) AllianceCell(k, j) = AllianceCell(j, k);  // the other nations' decisions about K stay
        }
    }
}

// A game exception that unwound through a masked routine would leave the mask in place: undone at the month's end.
static int g_aiChangesSkipped = 0;  // ChangeAITension calls left out this month

void ResetRelationMasks() {
    if (g_aiMaskDepth > 0 || g_aiMask.on) {
        Log("relations v2: mask reset (depth %d)", g_aiMaskDepth);
        g_aiMaskDepth = 1;
        AIMaskLeave();
        g_aiMaskDepth = 0;
    }
    if (g_pw.on) {
        Log("relations v2: player mask reset");
        PlayerMaskLeave();
    }
    if (g_aiChangesSkipped)
        Log("relations v2: %d tension change(s) of the computer's rules left out (ChangeAITension)", g_aiChangesSkipped);
    g_aiChangesSkipped = 0;
    g_maskLogged.clear();
}

static void MaskEnterSafe(int kind, const char* routine) {
    try {
        if (kind == 0)
            AIMaskEnter(routine);
        else
            PlayerMaskEnter(kind, routine);
    } catch (...) {
        Log("exception entering the relations mask (%s)", routine);
    }
}

static void MaskLeaveSafe(int kind) {
    try {
        if (kind == 0)
            AIMaskLeave();
        else
            PlayerMaskLeave();
    } catch (...) {
        Log("exception leaving the relations mask");
    }
}

// ---------------------------------------------------------------------------------------------
// Relations v2, B3: the player's relation routines for a joined player's nation K, on K's own PC.
//
// The game runs RandomTensionChanges, TensionEffectOfPreviousWars and CheckForWar for nation 0, reading relations from
// nation 0's point of view. On K's PC, at the start of K's month, they run in K's full view: K at index 0 and the host's
// nation at index K in the nation list (PlayerNation is K already), every record's relation cells permuted the same way
// (its `Tension` = its relation with K, `AITension[w]` = its relation with the view's nation w; alliances alike; K's own
// `Tension` and `Allied` read 0), K's own per-relation state in the other records (`WillGoToWar`: their war plans
// against K, `TechSharing`: K's deals, both kept by the host for K), K's war counter (B1, in place since the load), K's
// past wars for `CheckWarsWithNation`.
// The routines' dialogs (questions, declarations, spy reports) and turn messages are K's player's; the other nations
// people play are masked as players (MaskAsPlayer: no war or alliance between players from these routines).
// Afterwards the relation cells and the per-relation fields are put back, the changes are returned in real indices
// (the host applies them, MSG_RELATIONS) and written into this PC's campaign for K's screen; other fields keep what the
// routines did (K's own budget, prestige, corruption go to the host with K's turn; the host applies the others').
// Surprise landings and attacks are left out (`GetRandomPossession*`, `DoSurpriseAttack`,
// `CheckForPlayerSurpriseInvasion` return nothing in the view): they move possessions and ships (B5). 0.6.0 (B6 piece
// 2), with a host that takes them (welcome `gen` 2): DoSurpriseAttack runs and CheckSurpriseAttack records its pairs
// (K's battle slot plays them after the month), the takeover of an unowned possession runs and is recorded (the host
// applies it by name); the surprise landing in K's own possession stays out (see "Surprise attacks and takeovers").

static struct PvState {
    int K;
    void** arr;
    void* obj[9];
    int cells[9][9], acells[9][9];  // raw relation and alliance cells of every record (cell 0 = Tension / Allied)
    int will[9], ts[9];             // raw WillGoToWar, TechSharing
    int budget[9], vp;              // BudgetModifier of every record, K's VP
    int rel[9][9], ally[9][9];      // real relations before
    int histMonths[9], histWinner[9], histCount[9];  // K's past wars by real opponent (months 0x7fffffff: none)
    int warCounter;
} g_pv;

static int PvReal(int v) { return v == 0 ? g_pv.K : v == g_pv.K ? 0 : v; }  // view index -> real (and back)

static int& AllyCellAt(void* rec, int w) {  // alliance cell w of a record: 0 = Allied, else AIAlliance[w]
    return dl::At<int>(rec, w == 0 ? A.fAllied : A.fAIAlliance + 4 * (w - 1));
}
static int& RelCellAt(void* rec, int w) { return dl::At<int>(rec, A.fTension + 4 * w); }

bool PlayerTurnAvailable() { return A.playerTurn; }

static void PvEnter() {
    int K = g_pv.K;
    for (int v = 0; v < 9; v++) {
        void* rec = g_pv.obj[PvReal(v)];
        int r = PvReal(v);
        for (int w = 0; w < 9; w++) {
            if (w == v) continue;
            int o = PvReal(w);
            if (v == 0 && w == 0) continue;
            RelCellAt(rec, w) = g_pv.rel[r][o];
            AllyCellAt(rec, w) = g_pv.ally[r][o];
        }
    }
    // The swapped records' own cells read 0 like nation 0's own Tension and Allied (as in the event view): K's record's
    // cell 0 holds its real relation with the host, which the game's loops over nations 0..8 would take for a war or an
    // alliance of K with itself (CheckForWar's CountNationsAtWarWithPlayer, RandomTensionChanges' count of nations at
    // peace, NegotiatePeace's gains of enemies and allies, TerminateAlliances); the host's record's cell K (nation 0's
    // AITension[K] and AIAlliance[K]) likewise. PvLeave puts the raw cells back.
    RelCellAt(g_pv.obj[K], 0) = 0;
    AllyCellAt(g_pv.obj[K], 0) = 0;
    RelCellAt(g_pv.obj[0], K) = 0;
    AllyCellAt(g_pv.obj[0], K) = 0;
    g_pv.arr[0] = g_pv.obj[g_pv.K];
    g_pv.arr[g_pv.K] = g_pv.obj[0];
    g_pvOn = true;
}

static void PvLeave() {
    if (!g_pvOn) return;
    g_pv.arr[0] = g_pv.obj[0];
    g_pv.arr[g_pv.K] = g_pv.obj[g_pv.K];
    for (int r = 0; r < 9; r++) {
        void* rec = g_pv.obj[r];
        for (int w = 0; w < 9; w++) {
            RelCellAt(rec, w) = g_pv.cells[r][w];
            AllyCellAt(rec, w) = g_pv.acells[r][w];
        }
        dl::At<int>(rec, A.fWillGoToWar) = g_pv.will[r];
        dl::At<int>(rec, A.fTechSharing) = g_pv.ts[r];
    }
    g_pvOn = false;
}

// Runs the routines; a game exception that unwinds through here puts the view back first. Returns the routines run.
// `surprise` (0.6.0): DoSurpriseAttack and the takeover branch run, recorded (else B3's skips). `dryTarget` (tests,
// `battles sadry`): only DoSurpriseAttack(target) instead of the routines.
static std::vector<BattleAttempt>* g_saRec = nullptr;  // the surprise attacks of the turn (real indices)
static std::vector<PeaceLand>* g_takeRec = nullptr;    // the takeovers of the turn
static bool g_saNoTakeover = false;  // the attacker's landing pick found a small possession of K (single player: a landing)
static int PvRunGuarded(void* form, bool tension, bool surprise = false, void* dryTarget = nullptr) {
    int ran = 0;
    g_pvSkip = true;
    g_pvSurprise = surprise;
    g_pvSkipSA = !surprise;
    g_saNoTakeover = false;
    __try {
        if (dryTarget) {
            dl::Call(A.mSurpriseAttack, (uint32_t)(uintptr_t)form, 0, (uint32_t)(uintptr_t)dryTarget);
            ran |= 8;
        } else {
            if (tension) {
                dl::Call(A.mRandomTension, (uint32_t)(uintptr_t)form);
                ran |= 1;
            }
            dl::Call(A.mPreviousWars, (uint32_t)(uintptr_t)form);
            ran |= 2;
            dl::Call(A.mCheckForWar, (uint32_t)(uintptr_t)form);
            ran |= 4;
        }
    } __finally {
        g_pvSkip = g_pvSurprise = g_pvSkipSA = g_saNoTakeover = false;
        if (AbnormalTermination()) {
            g_saRec = nullptr;
            g_takeRec = nullptr;
            PvLeave();
        }
    }
    return ran;
}

static std::vector<std::wstring> ReadStringList(void* sl) {
    std::vector<std::wstring> out;
    int n = sl ? dl::At<int>(sl, A.fSLCount) : 0;
    void** items = sl ? dl::At<void**>(sl, A.fSLList) : nullptr;
    for (int i = 0; items && i < n; i++) out.push_back(dl::ReadUStr(items[i * 2]));
    return out;
}

static void WriteStringList(void* sl, const std::vector<std::wstring>& lines) {
    dl::Call(A.mSLClear, (uint32_t)(uintptr_t)sl);
    for (auto& s : lines) StringListAdd(sl, s);
}

// ---------------------------------------------------------------------------------------------
// Relations v2, B4: K's peace with the computer's nations, on K's own PC, in K's full view (right after the relation
// routines of B3, or alone for a proposal).
//
// The game ends the player's (nation 0's) wars in CheckTurnEvents: at war, when Random(4) + 2 < War (months at war), its
// peace decision weighs the war score (the player's VP and EnemyVP), FleetSize, the enemies' unrest, economies and
// colonies, and picks a peace event (condition 33..36) that GetPeaceEvent shows (TdlgEventAnswer). Its answers call
// NegotiatePeace(chance, lossGain, ForceBadPeace), which makes peace with every enemy at once: hawks, the enemies' refusals,
// the terms (prestige, resources, possessions chosen in TdlgPossessions or by the game, ships handed over, alliances
// ended, a build limit), the "Peace treaty" report, then DoPeace (each enemy's tension 1..4, every war of the enemies
// ends, everyone's WillGoToWar/Blockaded reset, budgets and unrest by the war's length, Prestige of nations 1..8 := 0,
// War := 0). PeaceDecision mirrors the decision (the war loans before it belong to B5); the rest is the game's own code in
// K's view, with K's player answering its dialogs:
// - the people's nations stay out of it: their relations and alliances with K read 40 and 0 while it runs, and all their
//   cells are put back afterwards (DoPeace would end their wars with K's enemies too); their wars with K are their own;
// - K's war score with its computer enemies is in place (VP, EnemyVP: the host's ledger); the save's TotalWar (the host's),
//   HasBeenBattle (K's: its war score has points) and AutoBuildSubs (0) are put aside; the month's war record of the save
//   is cleared (NegotiatePeace writes K's outcome there: the winner) and put back;
// - the possessions' TakenFrom and Invaded hold nation indices: the view's while the part runs;
// - K's own event cooldowns are in place (GetPeaceEvent picks an unused event and marks it);
// - hooks record what goes to the host: possession owner changes (ChangePossessionOwner), ships handed over
//   (TransferShipAsReparation: id, nations, the new name and the ship log's "Renamed" line), NegotiatePeace's arguments,
//   the treaty's lines (ShowPeaceTreaty), DoPeace (its zeroing of the other nations' Prestige is undone).
// Afterwards the other nations' BaseResources, Prestige, BudgetModifier and HasLostWar changes and the war state of the
// possessions of K's war are returned with the relation changes (B3's diff); DoPeace's clearing of the other possessions'
// war state is put back. K's own effects stay in this PC's campaign and reach the host with K's turn (the merge).

static bool g_peaceOn = false;          // the peace part runs: the hooks below record into g_peaceRec
static PeaceResult* g_peaceRec = nullptr;
static PeaceResult* g_allyRec = nullptr;  // B5 (d): the allies' war help runs: TransferShipAsReparation records here
static bool g_peaceDoPeace = false;     // DoPeace ran in the part
static int g_pvPrestige[9];             // every record's Prestige before DoPeace (real indices)
// K's own fields before DoPeace that its end of every war resets (demobilization); put back while K's war with a
// people's nation goes on (DoPeace ends all of the player's wars; K's peace ends only those with the computer's nations).
static struct {
    int budget, sub, unrest, corruption;
    uint8_t blockaded;
} g_pvKeep;

static struct {
    bool on;
    int cells[9][9], acells[9][9];  // the view's relation and alliance cells before the part (view indices)
    int vp, enemyVP, autoSubs, war;
    uint8_t totalWar, hasBeenBattle, tfw;
    int* rec;                       // this month's war record (the cell) of the save
    int recSave[3];                 // its enemy slots and the cell
    int* used;                      // K's event cooldowns (nullptr: the save's)
    int hostUsed[kEventSlots];
    int base[9], prestige[9], budget[9], unrest[9];  // real indices
    uint8_t lost[9];
} g_pm;

struct PossSnap {
    void* ps;
    void* owner;
    int taken, invaded, support, rebellion;
};
static std::vector<PossSnap> g_pmPoss;

bool PeaceAvailable() { return A.peace && A.playerTurn; }

static int PvRealOf(void* nation) {  // a record's real index (also while the view is in place)
    for (int r = 0; r < 9; r++)
        if (g_pv.obj[r] == nation) return r;
    return -1;
}

static void* PvRec(int v) { return g_pv.obj[PvReal(v)]; }  // the record at view index v

static int& EvUsedAt(void* f, int j) { return *(int*)(EventRec(f, j) + kEvUsed); }

static void PeaceMaskEnter(void* f, const PeaceSetup& s) {
    int K = g_pv.K;
    for (int v = 0; v < 9; v++)
        for (int w = 0; w < 9; w++) {
            g_pm.cells[v][w] = RelCellAt(PvRec(v), w);
            g_pm.acells[v][w] = AllyCellAt(PvRec(v), w);
        }
    for (int v = 1; v <= 8; v++) {
        if (!g_pvHuman[v]) continue;
        RelCellAt(PvRec(v), 0) = 40;
        RelCellAt(PvRec(0), v) = 40;
        AllyCellAt(PvRec(v), 0) = 0;
        AllyCellAt(PvRec(0), v) = 0;
    }
    void* k = g_pv.obj[K];
    g_pm.vp = dl::At<int>(k, A.fVP);
    dl::At<int>(k, A.fVP) = s.vp;
    g_pm.enemyVP = dl::At<int>(f, A.fEnemyVP);
    dl::At<int>(f, A.fEnemyVP) = s.enemyVP;
    g_pm.totalWar = dl::At<uint8_t>(f, A.fTotalWar);
    dl::At<uint8_t>(f, A.fTotalWar) = 0;
    g_pm.hasBeenBattle = dl::At<uint8_t>(f, A.fHasBeenBattle);
    dl::At<uint8_t>(f, A.fHasBeenBattle) = s.vp + s.enemyVP > 0 ? 1 : 0;
    g_pm.tfw = dl::At<uint8_t>(f, A.fTFW);
    g_pm.autoSubs = dl::At<int>(f, A.fAutoSubs);
    dl::At<int>(f, A.fAutoSubs) = 0;
    g_pm.war = *A.warVar;
    g_pm.rec = (int*)((uint8_t*)f + **A.recYearCell * 0x4B0 + **A.recMonthCell * 100 + A.recDisp);
    for (int i = 0; i < 3; i++) {
        g_pm.recSave[i] = g_pm.rec[i - 2];
        g_pm.rec[i - 2] = 0;
    }
    for (int r = 0; r < 9; r++) {
        void* n = g_pv.obj[r];
        g_pm.base[r] = dl::At<int>(n, A.fBaseResources);
        g_pm.prestige[r] = dl::At<int>(n, A.fPrestige);
        g_pm.budget[r] = dl::At<int>(n, A.fBudget);
        g_pm.unrest[r] = dl::At<int>(n, A.fUnrest);
        g_pm.lost[r] = dl::At<uint8_t>(n, A.fHasLostWar);
    }
    g_pmPoss.clear();
    ForEachPossession([](void*, void* ps) {
        PossSnap p{ps, dl::At<void*>(ps, A.fPossOwner), dl::At<int>(ps, A.fTakenFrom), dl::At<int>(ps, A.fInvaded),
                   dl::At<int>(ps, A.fInvSupport), dl::At<int>(ps, A.fRebellion)};
        g_pmPoss.push_back(p);
        dl::At<int>(ps, A.fTakenFrom) = PvReal(p.taken);
        dl::At<int>(ps, A.fInvaded) = PvReal(p.invaded);
    });
    g_pm.used = s.used;
    if (s.used)
        for (int j = 0; j < kEventSlots; j++) {
            g_pm.hostUsed[j] = EvUsedAt(f, j);
            EvUsedAt(f, j) = s.used[j];
        }
    g_pm.on = true;
}

// Puts back what the part masked (also when a game exception unwinds through it): the people's nations' cells and
// fields, K's VP, the form's fields, the war record, the cooldowns, the possessions' nation indices.
static void PeaceMaskLeave() {
    if (!g_pm.on) return;
    g_pm.on = false;
    void* f = BuildCamp();
    for (int v = 1; v <= 8; v++) {
        if (!g_pvHuman[v]) continue;
        for (int w = 0; w < 9; w++) {
            if (w == v) continue;
            RelCellAt(PvRec(v), w) = g_pm.cells[v][w];
            AllyCellAt(PvRec(v), w) = g_pm.acells[v][w];
            RelCellAt(PvRec(w), v) = g_pm.cells[w][v];
            AllyCellAt(PvRec(w), v) = g_pm.acells[w][v];
        }
        int r = PvReal(v);
        void* n = g_pv.obj[r];
        dl::At<int>(n, A.fBaseResources) = g_pm.base[r];
        dl::At<int>(n, A.fPrestige) = g_pm.prestige[r];
        dl::At<int>(n, A.fBudget) = g_pm.budget[r];
        dl::At<uint8_t>(n, A.fHasLostWar) = g_pm.lost[r];
    }
    dl::At<int>(g_pv.obj[g_pv.K], A.fVP) = g_pm.vp;
    if (f) {
        dl::At<int>(f, A.fEnemyVP) = g_pm.enemyVP;
        dl::At<uint8_t>(f, A.fTotalWar) = g_pm.totalWar;
        dl::At<uint8_t>(f, A.fHasBeenBattle) = g_pm.hasBeenBattle;
        dl::At<uint8_t>(f, A.fTFW) = g_pm.tfw;
        dl::At<int>(f, A.fAutoSubs) = g_pm.autoSubs;
        if (g_pm.used)
            for (int j = 0; j < kEventSlots; j++) {
                g_pm.used[j] = EvUsedAt(f, j);
                EvUsedAt(f, j) = g_pm.hostUsed[j];
            }
    }
    if (g_pm.rec)
        for (int i = 0; i < 3; i++) g_pm.rec[i - 2] = g_pm.recSave[i];
    for (auto& p : g_pmPoss) {
        dl::At<int>(p.ps, A.fTakenFrom) = PvReal(dl::At<int>(p.ps, A.fTakenFrom));
        dl::At<int>(p.ps, A.fInvaded) = PvReal(dl::At<int>(p.ps, A.fInvaded));
    }
}

// The hooks' records (only while the part runs; elsewhere the routines run as they are).
static void RecordLand(void* ps, void* owner, bool removeCA) {
    void* cur = dl::At<void*>(ps, A.fPossOwner);
    if (cur == owner) return;
    PeaceLand l;
    l.name = dl::ReadUStr(dl::At<void*>(ps, A.fPossName));
    l.from = cur ? PvRealOf(cur) : -1;
    l.to = owner ? PvRealOf(owner) : -1;
    l.removeCA = removeCA;
    g_peaceRec->lands.push_back(l);
    Log("relations v2: peace: possession %s from nation %d to %d", W2U(l.name).c_str(), l.from, l.to);
}

static void RecordShip(void* ship, int id, int from, int to, bool rename, void* reason, PeaceResult* into) {
    PeaceShip s;
    s.id = id;
    s.from = from;
    s.to = to;
    s.name = dl::ReadUStr(dl::At<void*>(ship, A.fCShipName));
    s.reason = dl::ReadUStr(reason);
    if (rename) {  // the game's log entry ends "<date> Renamed <name>", then an empty line
        std::vector<std::wstring> log = ReadStringList(dl::At<void*>(ship, A.fShipLog));
        for (size_t i = log.size(), n = 0; i > 0 && n < 3; i--, n++)
            if (log[i - 1].find(L"Renamed ") != std::wstring::npos) {
                s.renamed = log[i - 1];
                break;
            }
    }
    into->ships.push_back(s);
    Log("relations v2: %s: ship %d (%s) from nation %d to %d", into == g_peaceRec ? "peace" : "war help", id,
        W2U(s.name).c_str(), from, to);
}

static void RecordTreaty(void* report) {
    std::vector<std::wstring> lines = ReadStringList(report);
    if (!g_peaceRec->treaty.empty() && !lines.empty()) g_peaceRec->treaty.push_back(L" ");
    g_peaceRec->treaty.insert(g_peaceRec->treaty.end(), lines.begin(), lines.end());
}

extern "C" void* o_NegotiatePeace = nullptr;
extern "C" void* o_ChangeOwner = nullptr;
extern "C" void* o_TransferShip = nullptr;
extern "C" void* o_ShowPeaceTreaty = nullptr;

static int g_peaceChance = 0;  // tests (`peaceevent <code> <chance>`): the forced event's NegotiatePeace chance

// Returns the chance NegotiatePeace runs with (the caller's, or a test's for the forced peace event's answer).
extern "C" int __stdcall CB_NegotiatePeace(int chance, int lossGain, uint32_t force) {
    if (!g_peaceOn || !g_peaceRec) return chance;
    if (g_peaceChance > 0 && !g_peaceRec->negotiated) {
        Log("relations v2: peace: test: the event's NegotiatePeace runs with the chance %d (its answer's %d)",
            g_peaceChance, chance);
        chance = g_peaceChance;
    }
    Log("relations v2: peace: NegotiatePeace(%d, %d, %d)", chance, lossGain, (int)(force & 0xFF));
    if (g_peaceRec->negotiated) return chance;
    g_peaceRec->negotiated = true;
    g_peaceRec->chance = chance;
    g_peaceRec->lossGain = lossGain;
    g_peaceRec->force = (force & 0xFF) != 0;
    return chance;
}

extern "C" __declspec(naked) void d_NegotiatePeace() {  // (Self, aChance, aLossGain; ForceBadPeace on the stack)
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [esp + 16]
        push ecx
        push edx
        call CB_NegotiatePeace
        pop edx
        mov edx, eax
        pop ecx
        pop eax
        jmp dword ptr [o_NegotiatePeace]
    }
}

static void RecordTakeover(void* ps, void* owner, uintptr_t ret);

extern "C" void __stdcall CB_ChangeOwner(void* ps, void* owner, uint32_t removeCA, uintptr_t ret) {
    if (!ps) return;
    if (g_pvSkip && g_pvSurprise) {  // 0.6.0: the relation routines in K's view (CheckForWar's takeover)
        __try {
            RecordTakeover(ps, owner, ret);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return;
    }
    if (!g_peaceOn || !g_peaceRec) return;
    __try {
        RecordLand(ps, owner, (removeCA & 0xFF) != 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" __declspec(naked) void d_ChangeOwner() {  // (Self, aPossession, aNewOwner; RemoveAllCoastalArtillery on the stack)
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [esp + 12]  // the return address
        push dword ptr [esp + 20]  // RemoveAllCoastalArtillery
        push ecx
        push edx
        call CB_ChangeOwner
        pop edx
        pop ecx
        pop eax
        jmp dword ptr [o_ChangeOwner]
    }
}

extern "C" void __stdcall CB_TransferShip(void* self, void* from, void* to, void* ship, uint32_t rename, void* reason) {
    PeaceResult* into = g_peaceOn && g_peaceRec ? g_peaceRec : g_allyRec;  // the peace part's, or B5's war help's
    bool rec = into && ship;
    int id = rec ? dl::At<int>(ship, A.fShipId) : 0;
    int rf = rec ? PvRealOf(from) : -1, rt = rec ? PvRealOf(to) : -1;
    uint32_t st[3] = {(uint32_t)(uintptr_t)ship, rename, (uint32_t)(uintptr_t)reason};
    dl::Call(o_TransferShip, (uint32_t)(uintptr_t)self, (uint32_t)(uintptr_t)from, (uint32_t)(uintptr_t)to, 3, st);
    if (!rec) return;
    __try {
        RecordShip(ship, id, rf, rt, (rename & 0xFF) != 0, reason, into);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" __declspec(naked) void d_TransferShip() {  // (Self, aFromNation, aToNation; aShip, Rename, aReason on the stack)
    __asm {
        push dword ptr [esp + 4]
        push dword ptr [esp + 12]
        push dword ptr [esp + 20]
        push ecx
        push edx
        push eax
        call CB_TransferShip
        ret 12
    }
}

extern "C" void __stdcall CB_ShowPeaceTreaty(void* report) {
    if (!g_peaceOn || !g_peaceRec || !report) return;
    __try {
        RecordTreaty(report);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" __declspec(naked) void d_ShowPeaceTreaty() {  // (Self, aPeaceReport)
    __asm {
        push eax
        push ecx
        push edx
        push edx
        call CB_ShowPeaceTreaty
        pop edx
        pop ecx
        pop eax
        jmp dword ptr [o_ShowPeaceTreaty]
    }
}

// ---------------------------------------------------------------------------------------------
// Surprise attacks and takeovers at a war's start for K (0.6.0, B6 piece 2), while the relation routines run in K's view
// on K's PC with a host that takes them (welcome `gen` 2; else B3's skips stay).
//
// CheckForWar (1.01.44, 0xc84f04), for a computer nation B_i at peace with K (Tension < 40) while K is at peace (War <= 0),
// with the SurpriseAttack trait (or a fascist government, or a communist one 30 %) and a war plan (WillGoToWar 1, or
// Random(300) < Tension >= 10): Tension := 14, then in 50 % (with the trait 95 %) K.GetRandomPossessionInArea(B_i's
// build area); a possession of K there worth < 200 and B_i stronger there than half of K: the surprise landing ("...
// troops have made a surprise landing in our possession ...": Invaded := B_i, InvadedThisTurn := 1, resolved by the
// month's player-centric invasion code, so not for K: left out); otherwise up to 3 GetRandomPossession(nil, @area) until
// an unowned one: the takeover ("In a surprise move, ... troops have taken control of ...!", ChangePossessionOwner(p, B_i,
// False)). The war follows (Tension 14), and at the end DoSurpriseAttack runs when a war started from peace in the call
// and B_i or K has the trait: K's trait -> CheckSurpriseAttack(K, B_i), its False -> "No opportunity found for surprise
// attack!"; else every nation at war with K with the trait -> CheckSurpriseAttack(it, K) until one is True.
// In the view: CheckSurpriseAttack does not run; its (attacker, defender) is recorded and it answers True for K (no box)
// or False (the next nation is asked, as after a refusal); K's battle slot plays them after the month (session.cpp, a
// battle request of kind 2). The landing pick runs and answers nil; a possession worth < 200 means no takeover for this
// nation (single player's landing; its strength test is not evaluated: a guess that slightly under-counts takeovers),
// else the takeover branch runs and its ChangePossessionOwner is recorded (the host moves the possession by name).
// Each detour acts only on the call sites in CheckForWar (found at start-up); elsewhere in the view they keep B3's nil.
static uintptr_t g_retInArea = 0, g_retTake[3] = {}, g_retTakeOwner = 0;
static bool g_testTakeover = false;  // tests: the takeover takes the area's first unowned possession (no landing pick)
extern "C" void* o_RandPossArea;
extern "C" void* o_RandPoss;
extern "C" void* o_CheckSurprise = nullptr;

// The return addresses of the E8 calls to `target` in the first `len` bytes of `fn`.
static std::vector<uintptr_t> CallSitesTo(void* fn, void* target, int len) {
    std::vector<uintptr_t> out;
    uint8_t* p = (uint8_t*)fn;
    for (int i = 0; p && target && i + 5 <= len; i++)
        if (p[i] == 0xE8 && CallTarget(p + i) == target) out.push_back((uintptr_t)(p + i + 5));
    return out;
}

static std::wstring PossName(void* ps) { return dl::ReadUStr(dl::At<void*>(ps, A.fPossName)); }

static void RecordTakeover(void* ps, void* owner, uintptr_t ret) {
    if (!g_takeRec || ret != g_retTakeOwner) return;
    void* cur = dl::At<void*>(ps, A.fPossOwner);
    PeaceLand l;
    l.name = PossName(ps);
    l.from = cur ? PvRealOf(cur) : -1;
    l.to = owner ? PvRealOf(owner) : -1;
    g_takeRec->push_back(l);
    Log("relations v2: surprise move: nation %d takes over %s (owner %d; recorded for the host)", l.to,
        W2U(l.name).c_str(), l.from);
}

static void LogLandingLeftOut(void* p) {
    Log("relations v2: surprise move: the landing in %s (value %d) is left out; no takeover by this nation",
        W2U(PossName(p)).c_str(), dl::At<int>(p, A.fPossValue));
}

static void LogTestPick(void* p) {
    Log("relations v2: surprise move: test: the takeover picks %s", p ? W2U(PossName(p)).c_str() : "(none in the area)");
}

static void* FirstUnownedIn(void* area) {  // tests
    void* found = nullptr;
    ForEachPossession([&](void* a, void* ps) {
        if (!found && a == area && !dl::At<void*>(ps, A.fPossOwner)) found = ps;
    });
    return found;
}

static int RecordSurprise(void* attacker, void* defender) {
    BattleAttempt t;
    t.a = PvRealOf(attacker);
    t.b = PvRealOf(defender);
    g_saRec->push_back(t);
    Log("relations v2: surprise attack: nation %d may strike nation %d at the war's start (recorded for the battle slot)",
        t.a, t.b);
    return attacker == g_pv.obj[g_pv.K] ? 1 : 0;
}

extern "C" void* __stdcall CB_RandPossArea(void* self, void* area, uintptr_t ret) {
    if (!g_pvSurprise || ret != g_retInArea) return nullptr;  // B3: nothing in the view
    __try {
        g_saNoTakeover = false;
        if (g_testTakeover) return nullptr;
        void* p = (void*)(uintptr_t)dl::Call(o_RandPossArea, (uint32_t)(uintptr_t)self, (uint32_t)(uintptr_t)area);
        if (p && dl::At<int>(p, A.fPossValue) < 200) {
            g_saNoTakeover = true;
            LogLandingLeftOut(p);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_saNoTakeover = true;
    }
    return nullptr;
}

// TBuilderNation.GetRandomPossessionInArea(Self=EAX, anArea=EDX): TPossession (register-only).
extern "C" __declspec(naked) void d_RandPossArea() {
    __asm {
        cmp byte ptr [g_pvSkip], 0
        jne view
        jmp dword ptr [o_RandPossArea]
    view:
        push dword ptr [esp]
        push edx
        push eax
        call CB_RandPossArea
        ret
    }
}

extern "C" void* __stdcall CB_RandPoss(void* form, void* nation, void** areaRef, uintptr_t ret) {
    bool site = ret == g_retTake[0] || ret == g_retTake[1] || ret == g_retTake[2];
    if (!g_pvSurprise || !site || g_saNoTakeover) return nullptr;  // B3 (or this nation's landing): nothing
    __try {
        if (g_testTakeover && areaRef) {
            void* p = FirstUnownedIn(*areaRef);
            LogTestPick(p);
            if (p) return p;
        }
        return (void*)(uintptr_t)dl::Call(o_RandPoss, (uint32_t)(uintptr_t)form, (uint32_t)(uintptr_t)nation,
                                          (uint32_t)(uintptr_t)areaRef);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// TfrmBuildCamp.GetRandomPossession(Self=EAX, aNation=EDX, var anArea=ECX): TPossession (register-only).
extern "C" __declspec(naked) void d_RandPoss() {
    __asm {
        cmp byte ptr [g_pvSkip], 0
        jne view
        jmp dword ptr [o_RandPoss]
    view:
        push dword ptr [esp]
        push ecx
        push edx
        push eax
        call CB_RandPoss
        ret
    }
}

extern "C" int __stdcall CB_SurpriseRec(void* attacker, void* defender) {
    if (!g_pvSurprise || !g_saRec) return 0;
    __try {
        return RecordSurprise(attacker, defender);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// TfrmBuildCamp.CheckSurpriseAttack(Self=EAX, anAttacker=EDX, aDefender=ECX): Boolean (register-only). In K's view it
// is recorded and does not run; elsewhere (the battle slot, the host) it is the game's.
extern "C" __declspec(naked) void d_CheckSurprise() {
    __asm {
        cmp byte ptr [g_pvSkip], 0
        jne view
        jmp dword ptr [o_CheckSurprise]
    view:
        push ecx
        push edx
        call CB_SurpriseRec
        ret
    }
}

bool SurpriseAvailable() { return A.surprise && A.peace && A.playerTurn; }
static int Rnd(int n) { return (int)dl::Call(A.mRandom, (uint32_t)n); }  // the game's System.Random: 0..n-1

// CheckTurnEvents' peace decision (1.01.44, 0xce32c7..0xce37d6), in the same order of tests and Random calls; the war
// loans at War mod 3 == 1 before it are left out (the player's budget: B5). War is the game's value there (see
// PeaceRunGuarded). Returns the peace event's condition code (33..36), 0 (none) or -1 (CheckTotalDefeat decided: it
// negotiated itself).
static int PeaceDecision(void* f) {
    const int War = *A.warVar;
    void* P = dl::At<void*>(f, A.fPlayerNation);
    uint32_t F = (uint32_t)(uintptr_t)f;
    auto PVP = [&] { return dl::At<int>(P, A.fVP); };
    auto EVP = [&] { return dl::At<int>(f, A.fEnemyVP); };
    auto enemy = [&] { return (void*)(uintptr_t)dl::Call(A.mRandomEnemy, F); };
    auto sameAreas = [&](void* e) {
        return (dl::Call(A.mSameAreas, F, (uint32_t)(uintptr_t)P, (uint32_t)(uintptr_t)e) & 0xFF) != 0;
    };
    auto eco = [&](void* n) { return (int)dl::Call(A.mEcoRating, F, (uint32_t)(uintptr_t)n); };
    if (Rnd(4) + 2 >= War) return 0;  // (the game: no event this month at war)
    int code = 0;
    if (War > 4) {
        int t = *A.scaleVar * 250;
        if ((PVP() > t || EVP() > t) && (Rnd(30000) < PVP() + EVP() || Rnd(100) < War)) {
            if (Rnd(5) == 1 && EVP() > 300 && EVP() > 2 * PVP()) {
                code = 36;
            } else if (Rnd(5) == 1 && PVP() > 300 && PVP() > 2 * EVP()) {
                code = 34;
            } else if (Rnd(4) == 1 && PVP() > 300 && PVP() > 3 * EVP()) {
                code = 34;
            } else {
                if (Rnd(30) < War && Rnd(3) == 1) {
                    void* e = enemy();
                    if (e && !sameAreas(e) && ((int)dl::Call(A.mCountTakenFrom, F) == 0 || Rnd(100) < 30)) code = 33;
                }
                if (!code && Rnd(3) == 1) {
                    void* e = enemy();
                    if (e && !sameAreas(e) && 2 * PVP() > EVP()) code = 33;
                }
                if (!code && Rnd(10) == 1 && PVP() > 300 && (double)PVP() > EVP() * 1.5) {
                    void* e = enemy();
                    if (e && dl::At<int>(e, A.fUnrest) > 6) {
                        int mine = eco(P);
                        void* e2 = enemy();
                        int theirs = e2 ? eco(e2) : 0;
                        if (mine >= theirs || Rnd(100) > 50) code = 34;
                    }
                }
                if (!code && Rnd(15) == 1) {
                    int mine = eco(P);
                    void* e2 = enemy();
                    int theirs = e2 ? eco(e2) : 0;
                    if (mine >= theirs || Rnd(100) > 50) {
                        int p = PVP(), q = EVP();
                        if (p > 200 && q > 200 && (p > q ? p - q : q - p) < 100 && War > 6)
                            code = 33;
                        else if (p > 300 && (double)p < q * 1.5 && q > 300 && (double)q < p * 1.5 && War > 12)
                            code = 33;
                    }
                }
            }
            if (code) {
                int ratio = (int)dl::Call(A.mForceRatio, F);
                if (code == 34 && ratio < 95) code = 33;
                if (code == 36 && ratio > 110) code = 35;
            }
        }
    }
    if (dl::At<uint8_t>(f, A.fTotalWar)) {
        int gov = dl::At<uint8_t>(P, A.fGovernment);
        if ((gov == 4 || gov == 5) && code != 34 && Rnd(200) > War) code = 0;
        if (dl::Call(A.mCheckTotalDefeat, F) & 0xFF) return -1;
    }
    return code;
}

// The decision and its peace event, or NegotiatePeace at once; a game exception that unwinds through here puts the
// masks and the view back first. Returns the decision's code.
// The war counter: the game decides in CheckTurnEvents, before DoStrategicTurn steps the counter (0xce1f69), and its
// peace code there (the decision, CheckTotalDefeat, the event's NegotiatePeace, DoPeace) reads the value from before the
// step; on K's PC the month starts with the counter the host has stepped already (B1, KWarMonthStep). So the turn's
// decision and its peace event (also a test's forced one) see one month less while at war (> 1). A proposal (the
// Diplomacy list's "Propose peace" or a test, while planning; single player has none) sees the counter as shown. The
// shown value goes back afterwards on every path, unless DoPeace ended the war (its 0 stays; PeacePart puts the shown
// value back while a war with a people's nation goes on).
static int PeaceRunGuarded(void* f, const PeaceSetup* s) {
    int code = 0;
    const int shown = *A.warVar;
    const bool turn = !s->negotiate && (s->forcedCode > 0 || s->decision);
    const int war = turn && shown > 1 ? shown - 1 : shown;
    if (war != shown)
        Log("relations v2: peace: the turn's decision sees the war counter %d (the game's, before the month's step; "
            "shown %d)", war, shown);
    *A.warVar = war;
    g_peaceOn = true;
    g_peaceChance = !s->negotiate && s->forcedCode > 0 ? std::max(0, s->forcedChance) : 0;
    __try {
        if (s->negotiate) {
            uint32_t st[1] = {s->force ? 1u : 0u};
            dl::Call(A.mNegotiatePeace, (uint32_t)(uintptr_t)f, (uint32_t)s->chance, (uint32_t)s->lossGain, 1, st);
        } else {
            code = s->forcedCode > 0 ? s->forcedCode : s->decision ? PeaceDecision(f) : 0;
            if (code > 0) dl::Call(A.mGetPeaceEvent, (uint32_t)(uintptr_t)f, (uint32_t)code);
        }
    } __finally {
        g_peaceOn = false;
        g_peaceChance = 0;
        if (AbnormalTermination() || !g_peaceDoPeace) *A.warVar = shown;
        if (AbnormalTermination()) {
            g_peaceRec = nullptr;
            PeaceMaskLeave();
            PvLeave();
        }
    }
    return code;
}

// In the view (PvEnter done), after the relation routines: the peace part when K is at war with a computer nation.
// Relations v2, B5: the enemy-revolution rule first, as the game's month runs it (ReduceTimeLimits ->
// CheckForRevolutionInEnemyNation, before CheckTurnEvents' decision): the game's own routine with the peace part's masks
// and records (the people's nations read as at peace with K: their wars with K never end by it), the war counter as the
// decision's (the game's, before the month's step) and K's own RevolutionPending (a view index in the form's field while
// it runs; the host's put back). Each computer enemy at war with K: its unrest relief, a revolution (its peace: harsh,
// territories or compromise; possessions, a ship, K's resources and prestige, the enemy's wars and alliances ended), then
// either a separate peace (the others fight on: no DoPeace) or the general peace (DoPeace: the decision is skipped).
static int g_revoRandTest = -1;          // TEST ONLY (bridge `revotest`): the next run's draws 0
static std::map<int, int> g_revoUnrest;  // TEST ONLY: real index -> UnrestLevel in K's copy before the next run
static uint32_t __cdecl ZeroRand() { return 0; }

std::string TestRevolution(const std::vector<std::string>& args) {
    g_revoRandTest = args.size() > 1 && atoi(args[1].c_str()) == 0 ? 0 : -1;
    g_revoUnrest.clear();
    std::string s;
    for (size_t i = 2; i < args.size(); i++) {
        int n = -1, u = 0;
        if (sscanf(args[i].c_str(), "%d:%d", &n, &u) == 2 && n >= 0 && n <= 8) {
            g_revoUnrest[n] = u;
            s += " " + std::to_string(n) + ":" + std::to_string(u);
        }
    }
    Log("relations v2: TEST: the next enemy-revolution run: draws %s, unrest%s", g_revoRandTest == 0 ? "0" : "the game's",
        s.empty() ? " as it is" : s.c_str());
    return "ok revotest=" + std::to_string(g_revoRandTest) + s;
}

static void RevolutionGuarded(void* f, const PeaceSetup& s, PeaceResult& out) {
    const int shown = *A.warVar;
    int& pend = dl::At<int>(f, A.fRevoPending);
    const int hostPend = pend;
    const int kp = s.revoPending;
    pend = kp >= 1 && kp <= 8 && kp != g_pv.K && !g_pvHuman[PvReal(kp)] ? PvReal(kp) : 0;
    uint32_t* cell = nullptr;  // TEST: System.Random's RandProc (`push eax; call [RandProc]`)
    uint32_t saved = 0;
    uint8_t* rp = (uint8_t*)A.mRandom;
    if (g_revoRandTest == 0 && rp && rp[0] == 0x50 && rp[1] == 0xFF && rp[2] == 0x15) {
        cell = *(uint32_t**)(rp + 3);
        saved = *cell;
        *cell = (uint32_t)(uintptr_t)&ZeroRand;
        Log("relations v2: TEST: the enemy-revolution rule runs with its draws 0");
    }
    g_revoRandTest = -1;
    *A.warVar = shown > 1 ? shown - 1 : shown;
    g_peaceOn = true;
    __try {
        dl::Call(A.mRevolution, (uint32_t)(uintptr_t)f);
        out.revolution = true;
    } __finally {
        if (cell) *cell = saved;
        g_peaceOn = false;
        out.revoPending = pend >= 1 && pend <= 8 ? PvReal(pend) : 0;
        pend = hostPend;
        if (AbnormalTermination() || !g_peaceDoPeace) *A.warVar = shown;
        if (AbnormalTermination()) {
            g_peaceRec = nullptr;
            PeaceMaskLeave();
            PvLeave();
        }
    }
}

static void RevolutionPart(void* f, const PlayerTurnSetup& s, PeaceResult& out) {
    int K = g_pv.K;
    int before[9] = {}, unrest[9] = {};
    std::string cand, out1;
    for (int v = 1; v <= 8; v++) {
        before[v] = RelCellAt(PvRec(v), 0);
        unrest[v] = dl::At<int>(PvRec(v), A.fUnrest);
        if (g_pvHuman[v] && g_pm.cells[v][0] > 40)  // masked (40) by PeaceMaskEnter: a war between people
            out1 += " " + std::to_string(PvReal(v)) + " (unrest " + std::to_string(unrest[v]) +
                    (unrest[v] > 10 ? ": it would be a candidate" : "") + ")";
        else if (before[v] > 40)
            cand += " " + std::to_string(PvReal(v)) + ":" + std::to_string(unrest[v]);
    }
    if (!out1.empty())
        Log("relations v2: revolution: the wars of nation %d with the people's nations stay out of the enemy-revolution "
            "rule (no forced peace between players):%s", K, out1.c_str());
    void* msgs = dl::At<void*>(f, A.fTurnMessages);
    size_t m0 = ReadStringList(msgs).size();
    RevolutionGuarded(f, s.peace, out);
    std::vector<std::wstring> ml = ReadStringList(msgs);
    std::string mt;  // the turn messages it wrote (K's report; its pictures copy theirs there too)
    for (size_t i = m0; i < ml.size(); i++) mt += " | " + W2U(ml[i]).substr(0, 200);
    std::string ended, un;
    for (int v = 1; v <= 8; v++) {
        int u = dl::At<int>(PvRec(v), A.fUnrest);
        if (!g_pvHuman[v] && u != unrest[v])
            un += " " + std::to_string(PvReal(v)) + ":" + std::to_string(unrest[v]) + "->" + std::to_string(u);
        if (g_pvHuman[v] || before[v] <= 40 || RelCellAt(PvRec(v), 0) > 40) continue;
        ended += " " + std::to_string(PvReal(v));
        if (!g_peaceDoPeace) out.separate.push_back(PvReal(v));
    }
    Log("relations v2: revolution: nation %d (war counter %d), its computer enemies (nation:unrest)%s; unrest changes%s; "
        "wars ended%s (%s); RevolutionPending %d -> %d; messages%s",
        K, *A.warVar, cand.empty() ? " none" : cand.c_str(), un.empty() ? " none" : un.c_str(),
        ended.empty() ? " none" : ended.c_str(),
        g_peaceDoPeace ? "the general peace: DoPeace" : out.separate.empty() ? "-" : "a separate peace: the others fight on",
        s.peace.revoPending, out.revoPending, mt.empty() ? " none" : mt.c_str());
}

static void PeacePart(void* f, const PlayerTurnSetup& s, PeaceResult& out) {
    int K = g_pv.K;
    for (int v = 1; v <= 8; v++)
        if (!g_pvHuman[v] && RelCellAt(PvRec(v), 0) > 40) out.enemies.push_back(PvReal(v));
    if (out.enemies.empty()) return;
    out.ran = true;
    std::map<void*, int> forcedHuman;  // TEST (revotest): a people's nation's unrest in K's copy, put back afterwards
    if (s.peace.revolution && !s.peace.negotiate)
        for (auto& e : g_revoUnrest) {
            void* n = g_pv.obj[e.first];
            if (s.human[e.first]) forcedHuman[n] = dl::At<int>(n, A.fUnrest);
            dl::At<int>(n, A.fUnrest) = e.second;
        }
    g_revoUnrest.clear();
    PeaceMaskEnter(f, s.peace);
    int usedBefore[kEventSlots];
    for (int j = 0; j < kEventSlots; j++) usedBefore[j] = EvUsedAt(f, j);
    g_peaceRec = &out;
    g_peaceDoPeace = false;
    if (s.peace.revolution && !s.peace.negotiate && A.revolution) RevolutionPart(f, s, out);
    bool left = false;  // a computer enemy is left (the revolutions' separate peace)
    for (int v = 1; v <= 8; v++) left |= !g_pvHuman[v] && RelCellAt(PvRec(v), 0) > 40;
    if (!g_peaceDoPeace && left) out.code = PeaceRunGuarded(f, &s.peace);
    for (auto& h : forcedHuman) dl::At<int>(h.first, A.fUnrest) = h.second;
    g_peaceRec = nullptr;
    out.accepted = g_peaceDoPeace;
    for (int j = 0; j < kEventSlots; j++)
        if (usedBefore[j] == 0 && EvUsedAt(f, j) != 0) out.eventShown = true;
    int cell = g_pm.rec ? *g_pm.rec : 0;
    out.winner = !out.accepted ? 0 : cell == 1 ? 1 : cell == 2 ? 2 : 0;
    // The nations of K's war: K, its enemies, and the computer's nations whose war with one of them ended.
    bool inWar[9] = {};
    inWar[K] = true;
    for (int e : out.enemies) inWar[e] = true;
    for (int e : out.enemies) {
        int ve = PvReal(e);
        for (int v = 1; v <= 8; v++)
            if (v != ve && !g_pvHuman[v] && g_pm.cells[v][ve] > 40 && RelCellAt(PvRec(v), ve) <= 40) inWar[PvReal(v)] = true;
    }
    for (int r = 0; r < 9; r++) {
        if (r == K || s.human[r]) continue;
        void* n = g_pv.obj[r];
        int d = dl::At<int>(n, A.fBaseResources) - g_pm.base[r];
        if (d) out.delta[r]["BaseResources"] = d;
        d = dl::At<int>(n, A.fPrestige) - g_pm.prestige[r];
        if (d) out.delta[r]["Prestige"] = d;
        d = dl::At<int>(n, A.fBudget) - g_pm.budget[r];
        if (d) out.delta[r]["BudgetModifier"] = d;
        d = dl::At<int>(n, A.fUnrest) - g_pm.unrest[r];  // B5: the enemy-revolution rule's and DoPeace's
        if (d) out.delta[r]["UnrestLevel"] = d;
        uint8_t lost = dl::At<uint8_t>(n, A.fHasLostWar);
        if (lost != g_pm.lost[r]) out.lostWar[r] = lost;
    }
    PeaceMaskLeave();
    // The war state of the possessions of K's war; DoPeace cleared every possession's: the others get theirs back.
    auto inWarOf = [&](int n) { return n >= 0 && n <= 8 && inWar[n]; };
    for (auto& p : g_pmPoss) {
        void* owner = dl::At<void*>(p.ps, A.fPossOwner);
        int& tk = dl::At<int>(p.ps, A.fTakenFrom);
        int& inv = dl::At<int>(p.ps, A.fInvaded);
        int& sup = dl::At<int>(p.ps, A.fInvSupport);
        int& reb = dl::At<int>(p.ps, A.fRebellion);
        bool moved = owner != p.owner;
        int o0 = p.owner ? PvRealOf(p.owner) : -1;
        if (!moved && !(inWarOf(o0) && (inWarOf(p.taken) || p.taken == 99 || inWarOf(p.invaded)))) {
            tk = p.taken;
            inv = p.invaded;
            sup = p.support;
            reb = p.rebellion;
            continue;
        }
        if (moved || tk != p.taken || inv != p.invaded || sup != p.support || reb != p.rebellion) {
            PeaceTaken x;
            x.name = dl::ReadUStr(dl::At<void*>(p.ps, A.fPossName));
            x.owner = owner ? PvRealOf(owner) : -1;
            x.takenFrom = tk;
            x.invaded = inv;
            x.support = sup;
            x.rebellion = reb;
            out.taken.push_back(x);
        }
    }
    g_pmPoss.clear();
    if (out.accepted) {  // DoPeace set War := 0; a war with a people's nation goes on (its months count on)
        bool still = false;
        for (int v = 1; v <= 8; v++) still |= RelCellAt(PvRec(v), 0) > 40;
        if (still) {
            void* k = g_pv.obj[K];
            *A.warVar = g_pm.war;
            dl::At<int>(k, A.fBudget) = g_pvKeep.budget;
            dl::At<int>(k, A.fSubPolicy) = g_pvKeep.sub;
            dl::At<int>(k, A.fUnrest) = g_pvKeep.unrest;
            dl::At<int>(k, A.fCorruption) = g_pvKeep.corruption;
            dl::At<uint8_t>(k, A.fBlockaded) = g_pvKeep.blockaded;
            Log("relations v2: peace: nation %d's war with a people's nation goes on: its war counter, budget, submarine "
                "policy, unrest, corruption and blockade stay", K);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Relations v2, B5 (d): the war help of K's allies, on K's PC in K's view (RunPlayerTurn, before the relation routines).
//
// The game runs WarHelpFromAllies for nation 0 in DoStrategicTurn after the month's war step while War > 1: each ally
// (Allied > 0) at war with a random enemy of the player adds 10 * (Random(20) + FleetSize) VP, scaled by its economy, to
// the player's VP ("The operations of our ally X adds n VP", a picture; only 10% when the player's tonnage is not below
// the enemy's), or quarrels with the player's other ally, or joins the war, or (the war's second month) says it is not
// ready: the player's answer (ExecuteGeneral) may revoke the alliance, cost prestige, bring lend-lease ships
// (TransferShipAsReparation, once: LendLeaseDone) or 2000 from the ally's funds; a nation at war with the player's enemy
// may become an ally (its CoBelligerentDelay counts down first). For K the same runs here with its dialogs: K's war
// counter is the War (above 1: the host sends `khelp` and K fights a computer nation), K's own LendLeaseDone (kept by the
// host) and CoBelligerentDelay values (kept on this PC in memory: the game never saves them) are in place; the nations
// people play read as neither at war nor allied with anyone while it runs (their help and their wars are their own).
// Relations and alliances it changed reach the host with the turn's diff; K's VP (vpDelta, and `allyvp` for the war
// score), the other nations' Funds and the lend-lease ships are returned for the host.
static struct AllyMask {
    bool on;
    int cells[9][9], acells[9][9];  // view-indexed raw cells (the people's nations' put back afterwards)
    int funds[9];                   // by real index
    int vp;
    uint8_t lend;
    int cob[9];  // the records' own CoBelligerentDelay (the host's), by real index
} g_am;
static int g_kCob[9];     // K's own CoBelligerentDelay of each real nation (this PC's memory)
static int g_kCobK = -1;  // the nation they belong to

bool AllyHelpAvailable() { return A.allyHelp && A.playerTurn; }

static void AllyMaskEnter(void* f, const PlayerTurnSetup& s) {
    int K = g_pv.K;
    for (int v = 0; v < 9; v++)
        for (int w = 0; w < 9; w++) {
            g_am.cells[v][w] = RelCellAt(PvRec(v), w);
            g_am.acells[v][w] = AllyCellAt(PvRec(v), w);
        }
    for (int v = 1; v <= 8; v++) {
        if (!g_pvHuman[v]) continue;
        for (int w = 0; w < 9; w++) {
            if (w == v) continue;
            if (RelCellAt(PvRec(v), w) > 40) RelCellAt(PvRec(v), w) = 40;
            if (RelCellAt(PvRec(w), v) > 40) RelCellAt(PvRec(w), v) = 40;
            AllyCellAt(PvRec(v), w) = 0;
            AllyCellAt(PvRec(w), v) = 0;
        }
    }
    g_am.lend = dl::At<uint8_t>(f, A.fLendLease);
    dl::At<uint8_t>(f, A.fLendLease) = s.lendDone ? 1 : 0;
    for (int r = 0; r < 9; r++) {
        void* n = g_pv.obj[r];
        g_am.cob[r] = dl::At<int>(n, A.fCoBel);
        dl::At<int>(n, A.fCoBel) = g_kCobK == K ? g_kCob[r] : 0;  // 0: the game's start (TBuilderNation.Create)
        g_am.funds[r] = dl::At<int>(n, A.fFunds);
    }
    g_am.vp = dl::At<int>(g_pv.obj[K], A.fVP);
    g_am.on = true;
}

// Puts back what the part masked (also when a game exception unwinds through it) and reads K's results.
static void AllyMaskLeave(PlayerTurnResult* res) {
    if (!g_am.on) return;
    g_am.on = false;
    int K = g_pv.K;
    for (int v = 1; v <= 8; v++) {
        if (!g_pvHuman[v]) continue;
        for (int w = 0; w < 9; w++) {
            if (w == v) continue;
            RelCellAt(PvRec(v), w) = g_am.cells[v][w];
            AllyCellAt(PvRec(v), w) = g_am.acells[v][w];
            RelCellAt(PvRec(w), v) = g_am.cells[w][v];
            AllyCellAt(PvRec(w), v) = g_am.acells[w][v];
        }
    }
    void* f = BuildCamp();
    if (f) {
        res->lendDone = dl::At<uint8_t>(f, A.fLendLease) != 0;
        dl::At<uint8_t>(f, A.fLendLease) = g_am.lend;
    }
    for (int r = 0; r < 9; r++) {
        void* n = g_pv.obj[r];
        g_kCob[r] = dl::At<int>(n, A.fCoBel);
        dl::At<int>(n, A.fCoBel) = g_am.cob[r];
        int d = dl::At<int>(n, A.fFunds) - g_am.funds[r];
        if (r != K && d) res->allyFunds[r] = d;
    }
    g_kCobK = K;
    res->allyVP = dl::At<int>(g_pv.obj[K], A.fVP) - g_am.vp;
}

static int g_allyRandTest = -1;  // TEST ONLY (bridge `allyrand 0`): the allies' war help draws 0 (see AllyRandTest)
static uint32_t g_allyRandTurn = 0;
// System.Random(n) = high dword of RandProc() * n (`push eax; call [RandProc]; pop edx; mul edx`): 0 for every draw but
// GetRandomEnemy's Random(8), which goes round 0..7 so it finds an enemy (its 201 tries of the same index find none).
static uint32_t __cdecl AllyRandTest() {
    uint32_t* frame = (uint32_t*)_AddressOfReturnAddress();  // [0] back into Random, [1] its pushed range
    if (frame[1] == 8) return (g_allyRandTurn++ % 8) * 0x20000000u + 0x10000000u;
    return 0;
}

std::string TestAllyRandom(int v) {
    g_allyRandTest = v == 0 ? 0 : -1;
    Log("relations v2: TEST: the allies' war help draws %s", g_allyRandTest == 0 ? "0 for every Random" : "the game's");
    return "ok allyrand=" + std::to_string(g_allyRandTest);
}

static int AllyHelpGuarded(void* f, PlayerTurnResult* res) {
    int ran = 0;
    uint32_t* cell = nullptr;  // TEST: System.Random's RandProc (`push eax; call [RandProc]`)
    uint32_t saved = 0;
    uint8_t* rp = (uint8_t*)A.mRandom;
    if (g_allyRandTest == 0 && rp && rp[0] == 0x50 && rp[1] == 0xFF && rp[2] == 0x15) {
        cell = *(uint32_t**)(rp + 3);
        saved = *cell;
        *cell = (uint32_t)(uintptr_t)&AllyRandTest;
        g_allyRandTurn = 0;
        Log("relations v2: TEST: the allies' war help runs with its draws 0 (RandProc %p)", cell);
    }
    g_pvSkip = true;
    __try {
        dl::Call(A.mWarHelp, (uint32_t)(uintptr_t)f);
        ran = 1;
    } __finally {
        if (cell) *cell = saved;
        g_pvSkip = false;
        g_allyRec = nullptr;
        AllyMaskLeave(res);
        if (AbnormalTermination()) PvLeave();
    }
    return ran;
}

static void AllyHelpPart(void* f, const PlayerTurnSetup& s, PlayerTurnResult& res) {
    bool enemy = false;  // the game's GetRandomEnemy needs one (the people's nations read as at peace here)
    for (int v = 1; v <= 8; v++)
        if (!g_pvHuman[v] && RelCellAt(PvRec(v), 0) > 40) enemy = true;
    if (!enemy) return;
    PeaceResult rec;
    AllyMaskEnter(f, s);
    g_allyRec = &rec;
    res.ranAllyHelp = AllyHelpGuarded(f, &res) != 0;
    res.allyShips = rec.ships;
    std::string fu;
    for (auto& d : res.allyFunds) fu += " " + std::to_string(d.first) + ":" + std::to_string(d.second);
    Log("relations v2: war help from allies for nation %d (war counter %d): VP %d, funds of others%s, %d lend-lease "
        "ship(s), LendLeaseDone %d",
        g_pv.K, *A.warVar, res.allyVP, fu.empty() ? " none" : fu.c_str(), (int)res.allyShips.size(), res.lendDone ? 1 : 0);
}

// Relations v2, B5 (e): CheckBuildingShipsForWar for K in its view (the game runs it for its player after the month's war
// step while War > 0, NewWar = War 1): K's ships in a computer nation's yard are seized by that nation at its tension 7..9
// with K; a computer nation's ships in K's yards are seized at war, or at the answer of K's player. The routine compares
// the ships' BuildingNationIdx (an index) with the view's: the ships between K and a computer nation get theirs in view
// terms, every other ship reads as built at home (left alone: the people's yards and ships are decided by their own
// PCs or the host's game). Put back afterwards (also when an exception unwinds).
bool BuildWarAvailable() { return A.buildWar && A.playerTurn; }

static void RestoreBuilders(std::vector<std::pair<void*, int>>* saved) {
    for (auto& sv : *saved) dl::At<int>(sv.first, A.fBuildNat) = sv.second;
    saved->clear();
}

static int BuildWarGuarded(void* f, bool newWar, std::vector<std::pair<void*, int>>* saved) {
    int ran = 0;
    g_pvSkip = true;
    __try {
        dl::Call(A.mBuildWar, (uint32_t)(uintptr_t)f, newWar ? 1u : 0u);
        ran = 1;
    } __finally {
        g_pvSkip = false;
        g_allyRec = nullptr;
        RestoreBuilders(saved);
        if (AbnormalTermination()) PvLeave();
    }
    return ran;
}

static void BuildWarPart(void* f, const PlayerTurnSetup& s, PlayerTurnResult& res) {
    int war = *A.warVar;  // K's counter here (the host's, after the month's step)
    if (war <= 0) return;
    int K = g_pv.K;
    std::vector<std::pair<void*, int>> saved;
    std::string cand;
    for (int r = 0; r < 9; r++) {
        void* n = g_pv.obj[r];
        for (int i = 0; i < ShipCount(n); i++) {
            void* sh = Ship(n, i);
            if (!sh) continue;
            int& b = dl::At<int>(sh, A.fBuildNat);
            saved.push_back({sh, b});
            bool pair = b >= 0 && b <= 8 && b != r && (r == K ? !s.human[b] : (b == K && !s.human[r]));
            if (pair && (dl::Call(A.mIsBuilding, (uint32_t)(uintptr_t)sh) & 0xFF) && !dl::At<uint8_t>(sh, A.fRebuild))
                cand += " " + std::to_string(dl::At<int>(sh, A.fShipId)) + ":" + std::to_string(r) + "@" + std::to_string(b);
            b = pair ? PvReal(b) : PvReal(r);
        }
    }
    PeaceResult rec;
    g_allyRec = &rec;
    res.ranBuildWar = BuildWarGuarded(f, war == 1, &saved) != 0;
    res.buildShips = rec.ships;
    std::string sh;
    for (auto& x : rec.ships) sh += " " + std::to_string(x.id) + ":" + std::to_string(x.from) + ">" + std::to_string(x.to);
    Log("relations v2: building for war for nation %d (war counter %d%s): ships under construction between it and a "
        "computer nation (id:owner@yard)%s; handed over%s",
        K, war, war == 1 ? ", a new war" : "", cand.empty() ? " none" : cand.c_str(), sh.empty() ? " none" : sh.c_str());
}

PlayerTurnResult RunPlayerTurn(const PlayerTurnSetup& s) {
    PlayerTurnResult res;
    void* f = BuildCamp();
    void* list = NationList();
    int K = s.K;
    if (!PlayerTurnAvailable() || !f || !list || NationCount() < 9 || K < 1 || K > 8 || PlayerNation() != Nation(K) ||
        EventActive() || !A.warVar) {
        res.error = "the player's routines are not available here";
        return res;
    }
    void* msgs = dl::At<void*>(f, A.fTurnMessages);
    if (!msgs) {
        res.error = "no turn message list";
        return res;
    }
    g_pv.K = K;
    g_pv.arr = dl::At<void**>(list, 4);
    for (int r = 0; r < 9; r++) {
        g_pv.obj[r] = g_pv.arr[r];
        for (int w = 0; w < 9; w++) {
            g_pv.cells[r][w] = RelCellAt(g_pv.obj[r], w);
            g_pv.acells[r][w] = AllyCellAt(g_pv.obj[r], w);
        }
        g_pv.will[r] = dl::At<int>(g_pv.obj[r], A.fWillGoToWar);
        g_pv.ts[r] = dl::At<int>(g_pv.obj[r], A.fTechSharing);
        g_pv.budget[r] = dl::At<int>(g_pv.obj[r], A.fBudget);
        g_pvHuman[r] = false;
    }
    for (int a = 0; a < 9; a++)
        for (int b = 0; b < 9; b++) {
            g_pv.rel[a][b] = a == b ? 0 : Tension(a, b);
            g_pv.ally[a][b] = a == b ? 0 : AllianceMonths(a, b);
        }
    g_pv.vp = dl::At<int>(g_pv.obj[K], A.fVP);
    for (int o = 0; o < 9; o++) {
        g_pv.histMonths[o] = 0x7fffffff;
        g_pv.histWinner[o] = 0;
        g_pv.histCount[o] = 0;
    }
    for (auto& h : s.history) {
        if (h.opp < 0 || h.opp > 8 || h.opp == K) continue;
        g_pv.histCount[h.opp]++;
        if (h.monthsAgo < g_pv.histMonths[h.opp]) {
            g_pv.histMonths[h.opp] = std::max(0, h.monthsAgo);
            g_pv.histWinner[h.opp] = h.winner;
        }
    }
    for (int v = 1; v <= 8; v++) g_pvHuman[v] = s.human[PvReal(v)];
    // K's own per-relation state in the other records (the host's nation plans no war: a player).
    for (int r = 0; r < 9; r++) {
        if (r == K) continue;
        dl::At<int>(g_pv.obj[r], A.fWillGoToWar) = s.human[r] ? 0 : std::max(0, s.will[r]);
        dl::At<int>(g_pv.obj[r], A.fTechSharing) = s.human[r] ? 0 : std::max(0, s.techSharing[r]);
    }
    // K's own fields: what the turn or the proposal changes in them goes to the host too, so the host's campaign (saved
    // right after it applies them) holds them when the host's game is started again in the month.
    struct SelfField {
        const char* name;
        int off;
        int before;
    } self[] = {{"BudgetModifier", A.fBudget, 0},
                {"Prestige", A.fPrestige, 0},
                {"BaseResources", A.fBaseResources, 0},
                {"UnrestLevel", A.fUnrest, 0}};  // not Funds: a peace's scraps on this PC pay refunds the host never scraps
    for (auto& sf : self)
        if (sf.off >= 0) sf.before = dl::At<int>(g_pv.obj[K], sf.off);
    std::vector<std::wstring> oldMsgs = ReadStringList(msgs);
    dl::Call(A.mSLClear, (uint32_t)(uintptr_t)msgs);

    // 0.6.0 (B6 piece 2): surprise attacks and takeovers at a war's start, recorded (a gen 2 host). Tests: nation J plans
    // its war on K this turn (WillGoToWar 1 from the caller) with the SurpriseAttack trait, K with it too (K attacks),
    // the takeover's pick is the area's first unowned possession; `testSaDry`: only DoSurpriseAttack(J) in the view.
    const bool surprise = SurpriseAvailable() && (s.surprise || s.testSaDry);
    const int tJ = s.testSaNation;
    const bool forced = surprise && tJ >= 1 && tJ <= 8 && tJ != K && !s.human[tJ] && A.fSurpriseTrait >= 0;
    uint8_t traitJ = 0, traitK = 0;
    if (forced) {
        traitJ = dl::At<uint8_t>(g_pv.obj[tJ], A.fSurpriseTrait);
        traitK = dl::At<uint8_t>(g_pv.obj[K], A.fSurpriseTrait);
        dl::At<uint8_t>(g_pv.obj[tJ], A.fSurpriseTrait) = 1;
        if (s.testSaK) dl::At<uint8_t>(g_pv.obj[K], A.fSurpriseTrait) = 1;
        g_testTakeover = s.testTakeover;
        Log("relations v2: surprise: test: nation %d plans its war on nation %d (war plan %d, trait %d -> 1)%s%s", tJ, K,
            dl::At<int>(g_pv.obj[tJ], A.fWillGoToWar), traitJ,
            s.testSaK ? (", nation " + std::to_string(K) + "'s trait " + std::to_string(traitK) + " -> 1").c_str() : "",
            g_testTakeover ? ", the takeover picks the area's first unowned possession" : "");
    }
    g_saRec = surprise ? &res.surprise : nullptr;
    g_takeRec = surprise ? &res.takeovers : nullptr;

    PvEnter();
    if (s.allyHelp && AllyHelpAvailable()) AllyHelpPart(f, s, res);  // B5 (d): before CheckForWar, as in the game's month
    if (s.buildWar && BuildWarAvailable()) BuildWarPart(f, s, res);  // B5 (e): after it, as in the game's month
    int ran = 0;
    if (s.testSaDry && forced)
        ran = PvRunGuarded(f, false, true, g_pv.obj[tJ]);
    else if (s.relations)
        ran = PvRunGuarded(f, s.tensionChanges, surprise);
    g_saRec = nullptr;
    g_takeRec = nullptr;
    g_testTakeover = false;
    int budgetPre[9];  // after the relation routines: the peace part's budget changes are its own (res.peace.delta)
    for (int r = 0; r < 9; r++) budgetPre[r] = dl::At<int>(g_pv.obj[r], A.fBudget);
    if (s.peace.run && PeaceAvailable()) PeacePart(f, s, res.peace);
    // Read the view: K's relation with the view's nation v is v's cell 0; other pairs from both records' cells.
    int now[9][9], nowAlly[9][9];
    for (int a = 0; a < 9; a++)
        for (int b = 0; b < 9; b++) now[a][b] = nowAlly[a][b] = 0;
    for (int v = 1; v <= 8; v++) {
        int o = PvReal(v);
        void* rec = g_pv.obj[o];
        now[K][o] = now[o][K] = RelCellAt(rec, 0);
        nowAlly[K][o] = nowAlly[o][K] = AllyCellAt(rec, 0);
        for (int w = 1; w <= 8; w++) {
            if (w == v) continue;
            int p = PvReal(w);
            if (p == K) continue;
            int mine = RelCellAt(rec, w), theirs = RelCellAt(g_pv.obj[p], v);
            int before = g_pv.rel[o][p];
            now[o][p] = mine != before ? mine : theirs;  // a one-sided write (the game writes both) wins
            int am = AllyCellAt(rec, w), at = AllyCellAt(g_pv.obj[p], v);
            nowAlly[o][p] = am != g_pv.ally[o][p] ? am : at;
        }
    }
    for (int r = 0; r < 9; r++) {
        if (r == K) continue;
        res.will[r] = dl::At<int>(g_pv.obj[r], A.fWillGoToWar);
        res.techSharing[r] = dl::At<int>(g_pv.obj[r], A.fTechSharing);
        int d = budgetPre[r] - g_pv.budget[r];
        if (d) res.budget[r] = d;
    }
    res.vpDelta = dl::At<int>(g_pv.obj[K], A.fVP) - g_pv.vp;
    res.warCounter = *A.warVar;
    PvLeave();
    if (forced) {
        dl::At<uint8_t>(g_pv.obj[tJ], A.fSurpriseTrait) = traitJ;
        dl::At<uint8_t>(g_pv.obj[K], A.fSurpriseTrait) = traitK;
    }
    // Only K's own surprise attacks on the computer's nations and theirs on K (the people's nations read as players).
    for (size_t i = 0; i < res.surprise.size();) {
        const BattleAttempt& t = res.surprise[i];
        int o = t.a == K ? t.b : t.b == K ? t.a : -1;
        if (o >= 1 && o <= 8 && !s.human[o]) {
            i++;
            continue;
        }
        Log("relations v2: surprise attack %d > %d dropped (only between this nation and a computer nation)", t.a, t.b);
        res.surprise.erase(res.surprise.begin() + i);
    }
    res.ranSurprise = (ran & 8) != 0;
    for (auto& sf : self)
        if (sf.off >= 0 && dl::At<int>(g_pv.obj[K], sf.off) != sf.before)
            res.self[sf.name] = dl::At<int>(g_pv.obj[K], sf.off) - sf.before;
    res.ranTension = (ran & 1) != 0;
    res.ranPreviousWars = (ran & 2) != 0;
    res.ranWar = (ran & 4) != 0;
    res.messages = ReadStringList(msgs);
    WriteStringList(msgs, oldMsgs);

    // The changes, written into this PC's campaign too (K's screen until the next month comes).
    for (int a = 0; a < 9; a++)
        for (int b = a + 1; b < 9; b++) {
            int t0 = g_pv.rel[a][b], t1 = now[a][b], a0 = g_pv.ally[a][b], a1 = nowAlly[a][b];
            if (t1 != t0) {
                res.tension.push_back({a, b, t0, t1});
                SetTensionRaw(a, b, t1);
            }
            if (a1 != a0) {
                res.alliance.push_back({a, b, a0, a1});
                if (a == 0)
                    dl::At<int>(Nation(b), A.fAllied) = a1;
                else {
                    AllianceCell(a, b) = a1;
                    AllianceCell(b, a) = a1;
                }
            }
        }
    // K's peace: its screen follows (K's VP is the host's, 0 without a war; "Enemy VP" = its enemies' points).
    if (res.peace.accepted) {
        if (!AtWarWithAnyone(K)) SetNationVP(K, 0);
        int e = 0;
        for (int j = 0; j < 9; j++)
            if (j != K && AtWar(K, j)) e += std::max(0, NationVP(j));
        ShowEnemyVP(e);
    }
    std::string sum = "relations v2: nation " + std::to_string(K) +
                      (res.ranSurprise ? std::string("'s surprise attack test (DoSurpriseAttack alone):")
                       : s.relations   ? std::string("'s turn (") + (res.ranTension ? "tension, " : "") + "previous wars, war):"
                                       : std::string("'s peace proposal:"));
    for (auto& c : res.tension)
        sum += " " + std::to_string(c.a) + "-" + std::to_string(c.b) + " " + std::to_string(c.before) + "->" +
               std::to_string(c.after);
    for (auto& c : res.alliance)
        sum += " ally " + std::to_string(c.a) + "-" + std::to_string(c.b) + " " + std::to_string(c.before) + "->" +
               std::to_string(c.after);
    if (res.vpDelta) sum += " VP " + std::to_string(res.vpDelta);
    for (auto& d : res.self) sum += std::string(" own ") + d.first + (d.second > 0 ? "+" : "") + std::to_string(d.second);
    sum += "; war counter " + std::to_string(res.warCounter) + "; " + std::to_string(res.messages.size()) + " message(s)";
    if (surprise) {
        sum += "; surprise attacks:";
        for (auto& t : res.surprise) sum += " " + std::to_string(t.a) + ">" + std::to_string(t.b);
        if (res.surprise.empty()) sum += " none";
        sum += "; takeovers:";
        for (auto& l : res.takeovers) sum += " " + W2U(l.name) + " " + std::to_string(l.from) + ">" + std::to_string(l.to);
        if (res.takeovers.empty()) sum += " none";
    }
    if (res.peace.ran) {
        const PeaceResult& p = res.peace;
        char buf[320];
        snprintf(buf, sizeof(buf),
                 "; peace part: code %d, event %d, NegotiatePeace %s(%d, %d, %d), %s, winner %d, %d possession(s), %d "
                 "ship(s), %d war state(s), %d nation(s) changed, %d treaty line(s)",
                 p.code, p.eventShown ? 1 : 0, p.negotiated ? "" : "not run ", p.chance, p.lossGain, p.force ? 1 : 0,
                 p.accepted ? "peace" : "no peace", p.winner, (int)p.lands.size(), (int)p.ships.size(),
                 (int)p.taken.size(), (int)(p.delta.size() + p.lostWar.size()), (int)p.treaty.size());
        sum += buf;
    }
    Log("%s", sum.c_str());
    return res;
}

void ShowTurnMessagesOf(const std::vector<std::wstring>& lines) {
    void* f = BuildCamp();
    void* msgs = f && A.fTurnMessages >= 0 ? dl::At<void*>(f, A.fTurnMessages) : nullptr;
    if (!msgs || lines.empty() || !A.mShowTurnMessages || EventActive()) return;
    std::vector<std::wstring> old = ReadStringList(msgs);
    WriteStringList(msgs, lines);
    dl::Call(A.mShowTurnMessages, (uint32_t)(uintptr_t)f);  // the game's turn report dialog (modal)
    WriteStringList(msgs, old);
}

// ---------------------------------------------------------------------------------------------
// Relations v2, B4 (host): a joined nation's peace with the computer's nations, applied with the game's own routines.

static void* FindPossessionOf(const std::wstring& name, void* owner, bool* other) {
    void* found = nullptr;
    *other = false;
    ForEachPossession([&](void*, void* ps) {
        if (found || dl::ReadUStr(dl::At<void*>(ps, A.fPossName)) != name) return;
        if (dl::At<void*>(ps, A.fPossOwner) == owner)
            found = ps;
        else
            *other = true;
    });
    return found;
}

std::string MovePossession(const std::wstring& name, int from, int to, bool removeCA) {
    void* f = BuildCamp();
    if (!PeaceAvailable() || !f || from == to) return "unavailable";
    void* owner = from >= 0 ? Nation(from) : nullptr;
    void* newOwner = to >= 0 ? Nation(to) : nullptr;
    if ((from >= 0 && !owner) || (to >= 0 && !newOwner)) return "no such nation";
    bool other = false;
    void* ps = FindPossessionOf(name, owner, &other);
    if (!ps) return other ? "it has another owner now" : "no such possession";
    uint32_t st[1] = {removeCA ? 1u : 0u};
    dl::Call(A.mChangeOwner, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)ps, (uint32_t)(uintptr_t)newOwner, 1, st);
    return "";
}

// The game's hand-over (without its own rename: a second name suggestion would differ from the player's), then the name
// and the ship log's "Renamed" line from the player's PC.
std::string HandOverShip(int id, int from, int to, const std::wstring& name, const std::wstring& reason,
                         const std::wstring& renamedLine) {
    void* f = BuildCamp();
    void* nf = Nation(from);
    void* nt = Nation(to);
    if (!PeaceAvailable() || !f || !nf || !nt || from == to) return "unavailable";
    void* ship = nullptr;
    for (int i = 0; i < ShipCount(nf) && !ship; i++) {
        void* s = Ship(nf, i);
        if (s && dl::At<int>(s, A.fShipId) == id) ship = s;
    }
    if (!ship) return "not among nation " + std::to_string(from) + "'s ships";
    dl::ConstUStr why(reason);
    uint32_t st[3] = {(uint32_t)(uintptr_t)ship, 0, why.ptr()};
    dl::Call(A.mTransferShip, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)nf, (uint32_t)(uintptr_t)nt, 3, st);
    if (!name.empty() && A.mUStrAsg && dl::ReadUStr(dl::At<void*>(ship, A.fCShipName)) != name) {
        dl::ConstUStr n(name);
        dl::Call(A.mUStrAsg, (uint32_t)(uintptr_t)((uint8_t*)ship + A.fCShipName), n.ptr());
    }
    void* log = dl::At<void*>(ship, A.fShipLog);
    if (!renamedLine.empty() && log) {  // before the empty line that closes the game's log entry, as on the player's PC
        std::vector<std::wstring> lines = ReadStringList(log);
        if (A.mSLInsert && !lines.empty() && lines.back().empty()) {
            dl::ConstUStr u(renamedLine);
            dl::Call(A.mSLInsert, (uint32_t)(uintptr_t)log, (uint32_t)(lines.size() - 1), u.ptr());
        } else {
            StringListAdd(log, renamedLine);
        }
    }
    return "";
}

std::string SetPossessionWarState(const PeaceTaken& t) {
    if (!PeaceAvailable()) return "unavailable";
    void* owner = t.owner >= 0 ? Nation(t.owner) : nullptr;
    if (t.owner >= 0 && !owner) return "no such nation";
    bool other = false;
    void* ps = FindPossessionOf(t.name, owner, &other);
    if (!ps) return other ? "it has another owner now" : "no such possession";
    dl::At<int>(ps, A.fTakenFrom) = t.takenFrom;
    dl::At<int>(ps, A.fInvaded) = t.invaded;
    dl::At<int>(ps, A.fInvSupport) = t.support;
    dl::At<int>(ps, A.fRebellion) = t.rebellion;
    return "";
}

bool SetNationFlag(int idx, const std::string& field, bool value) {
    void* n = Nation(idx);
    int off = dl::Field("TBuilderNation", field.c_str());
    if (!n || off < 0) return false;
    dl::At<uint8_t>(n, off) = value ? 1 : 0;
    return true;
}

int NationFlag(int idx, const std::string& field) {
    void* n = Nation(idx);
    int off = dl::Field("TBuilderNation", field.c_str());
    return n && off >= 0 ? dl::At<uint8_t>(n, off) : -1;
}

int CampaignStartYear() {
    void* f = BuildCamp();
    return f ? dl::At<int>(f, A.fCampaignStartYear) : 0;
}

int CurrentSlot() { return A.slotVar ? *A.slotVar : -1; }
void SetCurrentSlot(int slot) {
    if (A.slotVar) *A.slotVar = slot;
}

std::wstring SaveDir(int slot) {
    void* f = BuildCamp();
    if (!f) return L"";
    void* res = nullptr;  // Delphi string result (small leak per call, acceptable)
    dl::Call(A.mGetSaveDirectory, (uint32_t)(uintptr_t)f, (uint32_t)slot, (uint32_t)(uintptr_t)&res);
    std::wstring d = dl::ReadUStr(res);
    if (!d.empty() && d.back() != L'\\') d += L'\\';
    return d;
}

bool SaveGame() {
    void* f = BuildCamp();
    if (!f) return false;
    if (EventActive()) {
        Log("SaveGame refused: an event dialog is open");
        return false;
    }
    if (PocDiag()) PocHeapCheck("before SaveBuildCampaign");
    uint32_t beforeBattle = 0;
    dl::Call(A.mSave, (uint32_t)(uintptr_t)f, 0xFFFFFFFFu, 0, 1, &beforeBattle);
    return true;
}

// pocHeapCheck (diagnosis only, RTW3MP_DIAG or the bridge's `pocheapcheck`): the campaign's objects from frmBuildCamp
// and the air unit list, checked by dl::HeapCheck; the result and the first problems go to the log.
bool PocDiag() {
    static int on = -1;
    if (on < 0) {
        char b[8];
        on = GetEnvironmentVariableA("RTW3MP_DIAG", b, sizeof(b)) > 0 ? 1 : 0;
    }
    return on == 1;
}

static std::string BattlePointersDiag(void* f);

std::string PocHeapCheck(const char* where) {
    DWORD t0 = GetTickCount();
    dl::HeapCheckResult r = dl::HeapCheck({{BuildCamp(), "frmBuildCamp"}, {AirUnitList(), "AirUnits"}});
    std::string s = "objects=" + std::to_string(r.objects) + " strings=" + std::to_string(r.strings) +
                    " bad=" + std::to_string(r.bad) + BattlePointersDiag(BuildCamp());
    Log("pocheapcheck %s: %s (%lu ms)%s", where, s.c_str(), GetTickCount() - t0, r.bad ? ":" : "");
    for (const std::string& p : r.problems) Log("pocheapcheck   %s", p.c_str());
    for (const std::string& p : r.problems) s += "; " + p;
    return s;
}

void RefreshUI() {
    void* f = BuildCamp();
    if (!f) return;
    uint32_t self = (uint32_t)(uintptr_t)f;
    if (A.mSetValues) dl::Call(A.mSetValues, self);  // player flag + name label
    dl::Call(A.mSetNationColors, self);
    dl::Call(A.mFillMapTree, self);
    // Same flags the game uses after loading; (True, True) assumes the "ships in service" columns and
    // raises "List index out of bounds" when another ship-list tab is active.
    dl::Call(A.mUpdateUIValues, self, 0, 0);
    if (A.mTabChange && A.fTabControl1 >= 0)  // refill the ship lists like a tab change does
        dl::Call(A.mTabChange, self, (uint32_t)(uintptr_t)dl::At<void*>(f, A.fTabControl1));
}

void SetPlayerNation(int idx) {
    void* f = BuildCamp();
    void* n = Nation(idx);
    if (!f || !n || EventActive()) return;
    dl::At<void*>(f, A.fPlayerNation) = n;
    Log("PlayerNation -> %d (%s)", idx, W2U(NationName(idx)).c_str());
    RefreshUI();
}

bool LoadCampaign(int slot, int playerIdx, bool modReload) {
    void* f = BuildCamp();
    if (!f) {
        Log("LoadCampaign: no frmBuildCamp");
        return false;
    }
    if (EventActive()) {
        Log("LoadCampaign refused: an event dialog is open");
        return false;
    }
    std::wstring path = SaveDir(slot) + L"RTWGame" + std::to_wstring(slot) + L".bcs";
    if (!FileExists(path)) {
        Log("LoadCampaign: missing %s", W2U(path).c_str());
        return false;
    }
    uint32_t self = (uint32_t)(uintptr_t)f;
    void* start = StartForm();
    if (start && FormVisible(start)) dl::Call(A.mHide, (uint32_t)(uintptr_t)start);
    SetCurrentSlot(slot);
    dl::ConstUStr s(path);
    struct ModReload {  // the load's blockade check (CB_BeforeBlockade) keeps the saved VP of the mod's own reloads
        bool on;
        explicit ModReload(bool b) : on(b) { g_modReload += on; }
        ~ModReload() { g_modReload -= on; }
    } mr(modReload);
    bool ok = (dl::Call(A.mLoad, self, s.ptr()) & 0xFF) != 0;
    if (dl::At<int>(f, A.fBattleOpponentIdx) == -1) dl::Call(A.mShow, self);
    if (ok) {
        dl::Call(A.mAdjustTechs, self);
        dl::Call(A.mEnableButtons, self);
        dl::Call(A.mSetNationColors, self);
        dl::Call(A.mUpdateUIValues, self, 0, 0);
        bool use1920 = dl::At<int>(f, A.fCampaignStartYear) >= 1920;
        for (int i = 0; i < 9 && i < NationCount(); i++)
            dl::Call(A.mLoadWarData, (uint32_t)(uintptr_t)Nation(i), use1920 ? 1 : 0);
        // A joined player's month: the save's war counter is the host nation's. The player's own one (sent by the host
        // with the month) replaces it before the screen is drawn: the war state, budgets and the player's own routines
        // read it. A client never sends [General] back (its turn holds its own nation's sections only).
        if (playerIdx > 0 && g_ownWar != INT_MIN && A.warVar) {
            Log("war counter: %d (the host's nation) -> %d (nation %d's own)", *A.warVar, g_ownWar, playerIdx);
            *A.warVar = g_ownWar;
        }
        if (playerIdx > 0) SetPlayerNation(playerIdx);
    }
    Log("LoadCampaign slot %d player %d -> %s", slot, playerIdx, ok ? "ok" : "FAILED");
    if (ok && PocDiag()) PocHeapCheck("after LoadCampaign");
    return ok;
}

bool InTurnProcessing() { return g_inTurn; }

static bool SafeHasVmt(void* obj, uint32_t vmt) {
    __try {
        return obj && vmt && *(uint32_t*)obj == vmt;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool BattleActive() {
    if (A.frmMainVar) {  // the battle form's unit variable (DoBattle creates the form into it)
        void* form = *A.frmMainVar;
        return SafeHasVmt(form, (uint32_t)(uintptr_t)dl::ClassVmt("TfrmMain")) && FormVisible(form);
    }
    static void* frmMain = nullptr;
    static DWORD lastScan = 0;
    if (!StillValid(frmMain, "TfrmMain")) {
        frmMain = nullptr;
        if (GetTickCount() - lastScan < 5000) return false;
        lastScan = GetTickCount();
        frmMain = ScanHeapFor("TfrmMain");
    }
    return frmMain && FormVisible(frmMain);
}

bool IsBusy() {
    if (g_inTurn) return true;
    void* app = Application();
    if (app && dl::At<int>(app, A.fModalLevel) > 0) return true;
    void* f = BuildCamp();
    HWND h = FormHwnd(f);
    if (h && IsWindowVisible(h) && !IsWindowEnabled(h)) return true;
    return false;
}

void* TurnButton() {
    void* f = BuildCamp();
    return f ? dl::At<void*>(f, A.fBtbnTurn) : nullptr;
}

void SetTurnButtonCaption(const std::wstring& caption) {
    HWND h = FormHwnd(TurnButton());
    if (h) {
        SetWindowTextW(h, caption.c_str());
        InvalidateRect(h, nullptr, TRUE);
    }
}

void ClickTurnButton() {
    HWND h = FormHwnd(TurnButton());
    if (h) PostMessageW(h, BM_CLICK, 0, 0);
}

// ---------------------------------------------------------------------------------------------
// Hooks

extern "C" void* o_TurnClick = nullptr;
extern "C" void* o_ResignClick = nullptr;
extern "C" void* o_EndOfTurn = nullptr;
extern "C" void* o_AIMoves = nullptr;
extern "C" void* o_AIPeace = nullptr;
extern "C" void* o_DesignStudies = nullptr;
extern "C" void* o_Load = nullptr;
extern "C" void* o_ReduceTimeLimits = nullptr;
extern "C" void* o_Likelihood = nullptr;

// EventLikelihoodCheck(Self, EventIndex, AffectedNation): while a joined player's event is rolled, events that
// need the host are never picked. Returns 1 to veto.
extern "C" BOOL __stdcall CB_Likelihood(void* self, int idx, int affected) {
    __try {
        return g_evActive && idx >= 0 && idx < kEventSlots && EventNeedsHost(self, idx) ? TRUE : FALSE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

extern "C" __declspec(naked) void d_Likelihood() {
    __asm {
        push eax
        push ecx
        push edx
        push ecx
        push edx
        push eax
        call CB_Likelihood
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz veto
        jmp dword ptr [o_Likelihood]
    veto:
        xor eax, eax
        ret
    }
}

void RunOriginalTurn() {
    void* f = BuildCamp();
    if (!f || !o_TurnClick) return;
    g_inTurn = true;
    dl::Call(o_TurnClick, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)TurnButton());
    g_inTurn = false;
}

extern "C" BOOL __stdcall CB_TurnClick(void* self, void* sender) {
    try {
        g_buildCamp = self;
        return hookcb::OnTurnClick(self, sender) ? TRUE : FALSE;
    } catch (...) {
        Log("exception in OnTurnClick");
        return FALSE;
    }
}
extern "C" BOOL __stdcall CB_ResignClick(void* self) {
    try {
        return hookcb::OnResignClick(self) ? TRUE : FALSE;
    } catch (...) {
        return FALSE;
    }
}
extern "C" void __stdcall CB_AfterEndOfTurn(void* self) {
    try {
        hookcb::OnAfterEndOfTurn(self);
    } catch (...) {
        Log("exception in OnAfterEndOfTurn");
    }
}
extern "C" void __stdcall CB_BeforeAIMoves(void* self) {
    try {
        hookcb::OnBeforeAIMoves(self);
    } catch (...) {
    }
}
extern "C" void __stdcall CB_AfterAIMoves(void* self) {
    try {
        hookcb::OnAfterAIMoves(self);
    } catch (...) {
    }
}
// HandleAIPeace ends with VP = 0 for every nation 1..8 after any AI peace (AI nations don't need it afterwards). A
// joined player's nation keeps its VP while it is still at war; the depth counts nested calls.
static int g_aiPeaceDepth = 0;
static int g_aiPeaceVP[9];
static bool g_aiPeaceKeep[9];

static void AIPeaceKeepVP() {
    if (g_aiPeaceDepth++ > 0 || !A.treaties) return;
    for (int i = 1; i < 9; i++) {
        g_aiPeaceKeep[i] = i < NationCount() && hookcb::IsHumanRemoteNation(i);
        g_aiPeaceVP[i] = g_aiPeaceKeep[i] ? NationVP(i) : 0;
    }
}

static void AIPeaceRestoreVP() {
    if (g_aiPeaceDepth <= 0 || --g_aiPeaceDepth > 0 || !A.treaties) return;
    for (int i = 1; i < 9; i++) {
        if (!g_aiPeaceKeep[i]) continue;
        g_aiPeaceKeep[i] = false;
        if (NationVP(i) == g_aiPeaceVP[i] || !AtWarWithAnyone(i)) continue;
        Log("AI peace: VP of player nation %d kept at %d (the game set %d)", i, g_aiPeaceVP[i], NationVP(i));
        SetNationVP(i, g_aiPeaceVP[i]);
    }
}

// The order matters: the protections below read wars (the skip of a nation whose wars are all hidden, and the VP of
// player nations still at war), so they run with every relation visible, outside the relations-v2 mask.
extern "C" BOOL __stdcall CB_BeforeAIPeace(void* self, void* nation, uint32_t badLoss) {
    BOOL skip = FALSE;
    int idx = -1;
    bool wasAtWar = false;
    try {
        idx = NationIndex(nation);
        wasAtWar = idx >= 1 && idx <= 8 && AtWarWithAnyone(idx);
        AIPeaceKeepVP();
    } catch (...) {
    }
    MaskEnterSafe(0, "HandleAIPeace");  // relations v2: the wars of nations under the player's rules stay out of it
    try {
        skip = hookcb::OnBeforeAIPeace(idx, (badLoss & 0xFF) != 0) ? TRUE : FALSE;
        if (!skip && wasAtWar && !AtWarWithAnyone(idx)) {
            // Every war of this nation is hidden (with nations under the player's rules, or between players): the
            // game's routine would look for an enemy it can't see (an access violation in 1.01.44), and these wars
            // end only by the players' own peace.
            Log("AI peace (badLoss=%d) skipped for nation %d: its wars are with players' nations", (badLoss & 0xFF) != 0,
                idx);
            hookcb::OnAfterAIPeace();
            skip = TRUE;
        }
    } catch (...) {
    }
    if (skip) {  // the game's routine doesn't run
        MaskLeaveSafe(0);
        try {
            AIPeaceRestoreVP();
        } catch (...) {
        }
    }
    return skip;
}
extern "C" void __stdcall CB_AfterAIPeace() {
    try {
        hookcb::OnAfterAIPeace();
    } catch (...) {
    }
    MaskLeaveSafe(0);
    try {
        AIPeaceRestoreVP();
    } catch (...) {
    }
}
extern "C" void __stdcall CB_AfterDesignStudies(void* self) {
    try {
        hookcb::OnAfterDesignStudies(self);
    } catch (...) {
        Log("exception in OnAfterDesignStudies");
    }
}

// AdvanceDesignStudies(Self=EAX): run the game's routine, then the callback with Self (skipped in a battle slot).
extern "C" __declspec(naked) void d_DesignStudies() {
    __asm {
        push eax
        push 2
        call CB_SkipCampaignStep
        test eax, eax
        pop eax
        jnz skipds
        push eax
        call dword ptr [o_DesignStudies]
        call CB_AfterDesignStudies
    skipds:
        ret
    }
}

extern "C" void __stdcall CB_AfterReduceTimeLimits(void* self) {
    try {
        hookcb::OnAfterReduceTimeLimits(self);
    } catch (...) {
        Log("exception in OnAfterReduceTimeLimits");
    }
}

// ReduceTimeLimits(Self=EAX): the game's routine with the pairs of nations under the player's rules masked (relations
// v2: its "separate peace" of a computer nation), then the callback with Self.
extern "C" void __stdcall CB_ReduceTimeLimits(void* self) {
    __try {
        MaskEnterSafe(0, "ReduceTimeLimits");
        dl::Call(o_ReduceTimeLimits, (uint32_t)(uintptr_t)self);
    } __finally {
        MaskLeaveSafe(0);
    }
    CB_AfterReduceTimeLimits(self);
}

extern "C" __declspec(naked) void d_ReduceTimeLimits() {
    __asm {
        push eax
        call CB_ReduceTimeLimits
        ret
    }
}

// The last battle's area and possession (0.6.0 fix). DoBattle sets TfrmBuildCamp.BattleArea (+0x4a3b) and
// BattlePossession (+0x4a36); only the battle screen's close clears the possession, nothing clears the area (it stays
// for DoStrategicTurn's "You may retire the CO <area>, ..." question). LoadBuildCampaign frees every TMapArea with its
// possessions (LoadMapData -> ClearMapData) and makes new ones, but sets the two fields again only for a save with a
// battle in progress ([BATTLE INFO]; ScenarioInProgress or BattleIndex > -1). After a battle and a load they point
// into freed memory: SaveBuildCampaign passes their Name ([+0x54]) straight to its "Ident=Value" writer (0xab6bc8,
// _UStrAddRef in its prologue), so the save's error box comes when the freed block holds something else there
// ("Access violation ... Read of address 00000029": a pointer 0x31 = the characters of a string "1"; "Integer
// overflow": the line's concatenation with a garbage length), and DoStrategicTurn's question can write into it. The
// mod loads every month and around every battle (slots 77 and 87, live battles), so every load now starts with both
// nil, as FormCreate leaves them; the load sets them again for a battle in progress.
static void* g_battleBefore[2] = {};  // BattleArea, BattlePossession before the load

static bool IsCampaignArea(void* f, void* a) {
    void* l = a && f && A.battlePtrs ? dl::At<void*>(f, A.fMapAreas) : nullptr;
    for (int i = 0, n = dl::ListCount(l); i < n; i++)
        if (dl::ListItem(l, i) == a) return true;
    return false;
}

static bool IsCampaignPossession(void* f, void* p) {
    void* l = p && f && A.battlePtrs ? dl::At<void*>(f, A.fMapAreas) : nullptr;
    for (int i = 0, n = dl::ListCount(l); i < n; i++) {
        void* a = dl::ListItem(l, i);
        void* pl = a ? dl::At<void*>(a, A.fAreaPossessions) : nullptr;
        for (int j = 0, m = dl::ListCount(pl); j < m; j++)
            if (dl::ListItem(pl, j) == p) return true;
    }
    return false;
}

bool KeepBattlePointersTest() {  // TEST ONLY (RTW3MP_TEST_KEEPBATTLEPTRS=1): the game's behaviour, for the experiment
    static int on = -1;
    if (on < 0) {
        char b[8];
        on = GetEnvironmentVariableA("RTW3MP_TEST_KEEPBATTLEPTRS", b, sizeof(b)) > 0 && b[0] == '1' ? 1 : 0;
    }
    return on == 1;
}

static std::string BattleObjName(void* o, int off) {
    return o && dl::ClassNameOf(o)[0] != '?' ? W2U(dl::ReadUStr(dl::At<void*>(o, off))) : std::string("?");
}

static void BattlePointersBeforeLoad(void* self) {
    g_battleBefore[0] = g_battleBefore[1] = nullptr;
    if (!A.battlePtrs || !self) return;
    void*& area = dl::At<void*>(self, A.fBattleArea);
    void*& poss = dl::At<void*>(self, A.fBattlePoss);
    g_battleBefore[0] = area;
    g_battleBefore[1] = poss;
    if (!area && !poss) return;
    if (KeepBattlePointersTest()) {
        Log("load: TEST: the last battle's area %s and possession %s kept across the load (the game's behaviour)",
            area ? BattleObjName(area, A.fAreaName).c_str() : "none",
            poss ? BattleObjName(poss, A.fPossName).c_str() : "none");
        return;
    }
    Log("load: the last battle's area %s and possession %s cleared (the load sets them again only for a battle in "
        "progress)", area ? BattleObjName(area, A.fAreaName).c_str() : "none",
        poss ? BattleObjName(poss, A.fPossName).c_str() : "none");
    area = nullptr;
    poss = nullptr;
}

static void BattlePointersAfterLoad(void* self, bool loaded) {
    if (!A.battlePtrs || !self || loaded || KeepBattlePointersTest()) return;
    // A failed load: what is still the campaign in memory keeps its own last battle.
    void*& area = dl::At<void*>(self, A.fBattleArea);
    void*& poss = dl::At<void*>(self, A.fBattlePoss);
    if (!area && IsCampaignArea(self, g_battleBefore[0])) area = g_battleBefore[0];
    if (!poss && IsCampaignPossession(self, g_battleBefore[1])) poss = g_battleBefore[1];
}

// pocHeapCheck's part (diagnosis): the two pointers against the campaign in memory, with what their Name slot holds.
static std::string BattlePointersDiag(void* f) {
    if (!A.battlePtrs || !f) return "";
    std::string s;
    const char* names[2] = {"BattleArea", "BattlePossession"};
    int fields[2] = {A.fBattleArea, A.fBattlePoss}, nameOff[2] = {A.fAreaName, A.fPossName};
    for (int k = 0; k < 2; k++) {
        void* p = dl::At<void*>(f, fields[k]);
        if (!p) continue;
        bool cur = k == 0 ? IsCampaignArea(f, p) : IsCampaignPossession(f, p);
        const char* cls = dl::ClassNameOf(p);
        char b[200];
        snprintf(b, sizeof(b), " %s=%p(%s, %s, Name '%s')", names[k], p, cur ? "of this campaign" : "STALE", cls,
                 cls[0] != '?' ? W2U(dl::ReadUStr(dl::At<void*>(p, nameOff[k]))).substr(0, 40).c_str() : "-");
        s += b;
    }
    return s;
}

// The bridge's `battleptrs` (read only): "ok area=<0|1> poss=<0|1> names=<area>/<possession>".
std::string BattlePointersState() {
    void* f = BuildCamp();
    if (!A.battlePtrs || !f) return "error unavailable";
    void* area = dl::At<void*>(f, A.fBattleArea);
    void* poss = dl::At<void*>(f, A.fBattlePoss);
    return std::string("ok area=") + (area ? "1" : "0") + " poss=" + (poss ? "1" : "0") + " names=" +
           (area ? BattleObjName(area, A.fAreaName) : "-") + "/" + (poss ? BattleObjName(poss, A.fPossName) : "-");
}

extern "C" void __stdcall CB_BeforeLoad(void* self) {
    try {
        ClearIntelReports(self);
        RememberAirUnits();
        BattlePointersBeforeLoad(self);
        hookcb::OnCampaignLoading();
    } catch (...) {
        Log("exception clearing intel reports");
    }
}
extern "C" void __stdcall CB_AfterLoad(void* self, uint32_t result) {
    try {
        DedupIntelReports(self);
    } catch (...) {
        Log("exception repairing intel reports");
    }
    try {
        bool loaded = (result & 0xFF) != 0;
        DropOldAirUnits(loaded);
        if (loaded) DedupAirUnits();
        BattlePointersAfterLoad(self, loaded);
    } catch (...) {
        Log("exception dropping the air units from before the load");
    }
    try {
        hookcb::OnAfterCampaignLoad();
    } catch (...) {
        Log("exception after a campaign load");
    }
}

// LoadBuildCampaign(Self=EAX, aFileName=EDX): Boolean in AL.
extern "C" __declspec(naked) void d_Load() {
    __asm {
        push eax
        push edx
        push eax
        call CB_BeforeLoad
        pop edx
        mov eax, [esp]
        call dword ptr [o_Load]
        push eax
        push eax
        push dword ptr [esp + 8]
        call CB_AfterLoad
        pop eax
        add esp, 4
        ret
    }
}

// HandleAIPeace(Self=EAX, aNation=EDX, BadLoss=CL): register-only procedure.
extern "C" __declspec(naked) void d_AIPeace() {
    __asm {
        push eax
        push edx
        push ecx
        push ecx
        push edx
        push eax
        call CB_BeforeAIPeace
        test eax, eax
        pop ecx
        pop edx
        pop eax
        jnz skip
        call dword ptr [o_AIPeace]
        call CB_AfterAIPeace
    skip:
        ret
    }
}

extern "C" __declspec(naked) void d_TurnClick() {
    __asm {
        push eax
        push ecx
        push edx
        push edx
        push eax
        call CB_TurnClick
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz handled
        jmp dword ptr [o_TurnClick]
    handled:
        ret
    }
}

extern "C" __declspec(naked) void d_ResignClick() {
    __asm {
        push eax
        push ecx
        push edx
        push eax
        call CB_ResignClick
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz handled
        jmp dword ptr [o_ResignClick]
    handled:
        ret
    }
}

extern "C" __declspec(naked) void d_EndOfTurn() {
    __asm {
        push eax
        call dword ptr [o_EndOfTurn]
        call CB_AfterEndOfTurn
        ret
    }
}

// DoPeace (the end of the host's last war) sets Prestige of nations 1..8 to 0: AI nations don't use it, but the
// joined players' nations do, so theirs is put back afterwards.
extern "C" void* o_DoPeace = nullptr;
static int g_peacePrestige[9];
static bool g_peaceKeep[9];

extern "C" void __stdcall CB_BeforeDoPeace() {
    if (g_peaceOn) {  // relations v2, B4: K's peace in K's view (see PeacePart): only the other nations' Prestige is kept
        for (int r = 0; r < 9; r++) g_pvPrestige[r] = g_pv.obj[r] ? dl::At<int>(g_pv.obj[r], A.fPrestige) : 0;
        void* k = g_pv.obj[g_pv.K];
        g_pvKeep.budget = dl::At<int>(k, A.fBudget);
        g_pvKeep.sub = dl::At<int>(k, A.fSubPolicy);
        g_pvKeep.unrest = dl::At<int>(k, A.fUnrest);
        g_pvKeep.corruption = dl::At<int>(k, A.fCorruption);
        g_pvKeep.blockaded = dl::At<uint8_t>(k, A.fBlockaded);
        g_peaceDoPeace = true;
        Log("relations v2: peace: DoPeace (nation %d's wars with the computer's nations end)", g_pv.K);
        return;
    }
    for (int i = 1; i < 9; i++) {
        g_peaceKeep[i] = false;
        void* n = i < NationCount() && hookcb::IsHumanRemoteNation(i) ? Nation(i) : nullptr;
        if (n) {
            g_peacePrestige[i] = dl::At<int>(n, A.fPrestige);
            g_peaceKeep[i] = true;
        }
    }
    try {
        hookcb::OnHostPeace(false);
    } catch (...) {
    }
}

extern "C" void __stdcall CB_AfterDoPeace() {
    if (g_peaceOn) {
        for (int r = 0; r < 9; r++)
            if (r != g_pv.K && g_pv.obj[r]) dl::At<int>(g_pv.obj[r], A.fPrestige) = g_pvPrestige[r];
        return;
    }
    for (int i = 1; i < 9; i++) {
        if (!g_peaceKeep[i]) continue;
        g_peaceKeep[i] = false;
        void* n = i < NationCount() ? Nation(i) : nullptr;
        if (!n || dl::At<int>(n, A.fPrestige) == g_peacePrestige[i]) continue;
        Log("end of war: prestige of player nation %d kept at %d (the game set %d)", i, g_peacePrestige[i],
            dl::At<int>(n, A.fPrestige));
        dl::At<int>(n, A.fPrestige) = g_peacePrestige[i];
    }
    try {
        hookcb::OnHostPeace(true);
    } catch (...) {
    }
}

extern "C" __declspec(naked) void d_DoPeace() {  // (Self)
    __asm {
        push eax
        call CB_BeforeDoPeace
        pop eax
        call dword ptr [o_DoPeace]
        call CB_AfterDoPeace
        ret
    }
}

// CopyGameToOtherSlot(Self, aFromSlot, aToSlot): the "Copy game" button (register arguments only).
extern "C" void* o_CopyGame = nullptr;

extern "C" void __stdcall CB_AfterCopyGame(int from, int to) {
    try {
        hookcb::OnAfterCopyGame(from, to);
    } catch (...) {
        Log("exception in OnAfterCopyGame");
    }
}

extern "C" __declspec(naked) void d_CopyGame() {
    __asm {
        push ecx
        push edx
        call dword ptr [o_CopyGame]
        call CB_AfterCopyGame
        ret
    }
}

void CopyGame(int toSlot) {
    void* f = BuildCamp();
    if (!f || !A.mCopyGame || EventActive()) return;
    dl::Call(A.mCopyGame, (uint32_t)(uintptr_t)f, (uint32_t)CurrentSlot(), (uint32_t)toSlot);  // through the hook
}

// TfrmResearch.Execute(Self, aNation): the research screen. The game's Research button opens it for nation 0
// (BuilderNations[0]), not for PlayerNation; on a joined player's PC that is the host's nation, so the player saw and
// edited the host's research priorities and research budget. It opens the player's own nation instead.
extern "C" void* o_ResearchExecute = nullptr;

extern "C" void* __stdcall CB_ResearchNation(void* nation) {
    __try {
        void* player = PlayerNation();
        if (nation && player && nation != player && nation == Nation(0)) {
            Log("research screen: opened for the player's nation %d instead of nation 0", NationIndex(player));
            return player;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nation;
}

extern "C" __declspec(naked) void d_ResearchExecute() {
    __asm {
        push eax
        push ecx
        push edx
        call CB_ResearchNation
        mov edx, eax
        pop ecx
        pop eax
        jmp dword ptr [o_ResearchExecute]
    }
}

// The relations view's display routines (see KViewBegin).
extern "C" void* o_ShowTension = nullptr;
extern "C" void* o_StrengthBars = nullptr;
extern "C" void* o_NationData = nullptr;
extern "C" void* o_SetValues = nullptr;
extern "C" void* o_UpdateUI = nullptr;

extern "C" void __stdcall CB_ShowTension(void* self) { ShowTensionInView(self, o_ShowTension); }
extern "C" void __stdcall CB_StrengthBars(void* self, void* area) { StrengthBarsInView(self, area, o_StrengthBars); }

// A flag's click opens the nation data dialog for its slot's nation index (the player's own flag: 0). In the view slot
// K shows the host's nation and the player's flag nation K.
extern "C" int __stdcall CB_NationDataIdx(int idx) {
    __try {
        int K = A.relView && !EventActive() ? ShownNationIndex() : 0;
        int to = K > 0 && idx == 0 ? K : K > 0 && idx == K ? 0 : idx;
        if (K > 0) Log("relations view: nation data of slot %d opens nation %d", idx, to);
        return to;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return idx;
}

static void SafeFlagPicture(void* self) {
    __try {
        KViewFlagPicture(self);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void SafeWarState(void* self) {
    __try {
        KViewWarState(self);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __stdcall CB_AfterSetValues(void* self) { SafeFlagPicture(self); }
extern "C" void __stdcall CB_AfterUpdateUI(void* self) { SafeWarState(self); }

extern "C" __declspec(naked) void d_ShowTension() {  // ShowTensionLevels(Self)
    __asm {
        push eax
        call CB_ShowTension
        ret
    }
}

extern "C" __declspec(naked) void d_StrengthBars() {  // DrawAreaStrengthBars(Self, anArea)
    __asm {
        push edx
        push eax
        call CB_StrengthBars
        ret
    }
}

extern "C" __declspec(naked) void d_NationData() {  // TdlgNationData.Execute(Self, aNationIdx)
    __asm {
        push eax
        push ecx
        push edx
        call CB_NationDataIdx
        mov edx, eax
        pop ecx
        pop eax
        jmp dword ptr [o_NationData]
    }
}

extern "C" __declspec(naked) void d_SetValues() {  // SetValues(Self); no stack arguments
    __asm {
        push eax
        call dword ptr [o_SetValues]
        call CB_AfterSetValues
        ret
    }
}

extern "C" __declspec(naked) void d_UpdateUI() {  // UpdateUIValues(Self, ReFillShipList, UpdateShipListOK); no stack arguments
    __asm {
        push eax
        call dword ptr [o_UpdateUI]
        call CB_AfterUpdateUI
        ret
    }
}

// DoResearch(Self, aNationIdx, NoNonGunmessages), register arguments only: the monthly research of one nation. For
// every nation above 0 it also runs the game's AI research choices: some priorities are set to High by chance, and with a
// 5 % chance a month every priority is re-rolled (mostly Medium). A joined player's own priorities are put back.
extern "C" void* o_DoResearch = nullptr;
static int g_resPrio[25];
static int g_resNation = -1;

extern "C" void __stdcall CB_BeforeResearch(int idx) {
    g_resNation = -1;
    if (idx <= 0 || idx > 8 || A.fResearchAreas < 0) return;
    try {
        if (!hookcb::IsHumanRemoteNation(idx)) return;
    } catch (...) {
        return;
    }
    void* n = Nation(idx);
    if (!n) return;
    for (int i = 0; i < 25; i++) g_resPrio[i] = *(int*)((uint8_t*)n + A.fResearchAreas + i * 0x35 + 1);
    g_resNation = idx;
}

extern "C" void __stdcall CB_AfterResearch() {
    if (g_resNation < 0) return;
    void* n = Nation(g_resNation);
    int changed = 0;
    for (int i = 0; n && i < 25; i++) {
        int* p = (int*)((uint8_t*)n + A.fResearchAreas + i * 0x35 + 1);
        if (*p != g_resPrio[i]) {
            *p = g_resPrio[i];
            changed++;
        }
    }
    if (changed) Log("research priorities of player nation %d kept (%d changed by the game's AI choices)", g_resNation, changed);
    g_resNation = -1;
}

extern "C" __declspec(naked) void d_DoResearch() {
    __asm {
        push eax
        push ecx
        push edx
        push edx  // aNationIdx
        call CB_BeforeResearch
        pop edx
        pop ecx
        pop eax
        call dword ptr [o_DoResearch]
        push eax
        call CB_AfterResearch
        pop eax
        ret
    }
}

std::string DebugResearch(int idx) {
    void* n = Nation(idx);
    if (!n || A.fResearchAreas < 0 || A.fResearchPct < 0) return "error research unavailable";
    // ResearchAreas: 25 records of 0x35 bytes (Advantage byte, Prio integer, ...), as TBuilderNation.SaveMe writes them.
    std::string prio;
    for (int i = 0; i < 25; i++)
        prio += (i ? "," : "") + std::to_string(*(int*)((uint8_t*)n + A.fResearchAreas + i * 0x35 + 1));
    return "ok pct=" + std::to_string(dl::At<int>(n, A.fResearchPct)) + " prio=" + prio;
}

// Tests: the game's monthly research of a nation `count` times (through the hook), logged as
// "research test nation N x count: before <prio> after <prio>". It can show the game's research messages.
void DebugDoResearch(int idx, int count) {
    void* f = BuildCamp();
    if (!f || !A.mDoResearch || idx < 1 || idx > 8 || count < 1 || EventActive()) {
        Log("research test: not possible");
        return;
    }
    std::string before = DebugResearch(idx);
    for (int i = 0; i < count; i++) dl::Call(A.mDoResearch, (uint32_t)(uintptr_t)f, (uint32_t)idx, 1);
    Log("research test nation %d x %d: before %s after %s", idx, count, before.c_str(), DebugResearch(idx).c_str());
}

void OpenResearch() {
    void* f = BuildCamp();
    void* btn = f && A.fBtbnResearch >= 0 ? dl::At<void*>(f, A.fBtbnResearch) : nullptr;
    if (!f || !A.mResearchClick || EventActive()) return;
    dl::Call(A.mResearchClick, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)btn);  // the button's handler
}

// ---------------------------------------------------------------------------------------------
// Tactical battles of joined players. A battle between a joined player's nation and an AI nation is one the game
// resolves without a battle screen (DoBattleBetweenAINations, from CheckAIvsAIBattles). The host can defer it: the
// player then fights it on their own PC, in a battle slot where their nation is the player nation, with the game's
// own battle code (CheckAttackLocation -> DoBattle -> the battle screen -> the aftermath). The battle screen's
// close runs the rest of a month (DoStrategicTurn), which a battle slot must not do.


bool BattlesAvailable() { return A.battles && BuildCamp(); }
void SetBattleSlotMode(bool on) {
    g_battleSlotMode = on;
    g_battleAftermath = false;
}
bool BattleSlotMode() { return g_battleSlotMode; }
bool BattleAftermathSeen() { return g_battleAftermath; }

static void* AreaByName(const std::wstring& name) {
    void* f = BuildCamp();
    if (!f || A.fMapAreas < 0 || A.fAreaName < 0 || name.empty()) return nullptr;
    void* areas = dl::At<void*>(f, A.fMapAreas);
    for (int i = 0; i < dl::ListCount(areas); i++) {
        void* a = dl::ListItem(areas, i);
        if (a && dl::ReadUStr(dl::At<void*>(a, A.fAreaName)) == name) return a;
    }
    return nullptr;
}

static std::wstring AreaNameOf(void* area) {
    return area && A.fAreaName >= 0 ? dl::ReadUStr(dl::At<void*>(area, A.fAreaName)) : std::wstring();
}

extern "C" int __stdcall CB_DeferAIBattle(void* a, void* b, int countA, int countB, void* area) {
    if (g_noDefer || !A.battles) return 0;
    try {
        return hookcb::OnAIBattle(NationIndex(a), NationIndex(b), countA, countB, AreaNameOf(area)) ? 1 : 0;
    } catch (...) {
        Log("exception in OnAIBattle");
        return 0;
    }
}

// DoBattleBetweenAINations(Self, NationA, NationB, ShipCountA, ShipCountB, anArea): Boolean, ret 12. A deferred
// battle returns True like a fought one (CheckAIvsAIBattles then stops looking for another battle of the pair).
extern "C" __declspec(naked) void d_AIBattleDefer() {
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [esp + 16]  // anArea
        push dword ptr [esp + 24]  // ShipCountB
        push dword ptr [esp + 32]  // ShipCountA
        push ecx                   // NationB
        push edx                   // NationA
        call CB_DeferAIBattle
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz deferred
        jmp d_AIBattle
    deferred:
        mov eax, 1
        ret 12
    }
}

// A game call from the battle code; a Delphi exception escaping it counts as a failure (ok = 0).
static uint32_t GuardedCall(void* fn, uint32_t a, uint32_t d, uint32_t c, int n, const uint32_t* s, int* ok) {
    __try {
        uint32_t r = dl::Call(fn, a, d, c, n, s);
        *ok = 1;
        return r;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *ok = 0;
        return 0;
    }
}

bool ResolveAIBattle(int a, int b, int countA, int countB, const std::wstring& areaName,
                     std::vector<std::wstring>* texts) {
    void* f = BuildCamp();
    void* na = Nation(a);
    void* nb = Nation(b);
    void* area = AreaByName(areaName);
    if (!f || !na || !nb || !area || !A.mAIBattle || EventActive()) {
        Log("ResolveAIBattle %d-%d in %s: not possible", a, b, W2U(areaName).c_str());
        return false;
    }
    uint32_t stack[3] = {(uint32_t)countA, (uint32_t)countB, (uint32_t)(uintptr_t)area};
    int ok = 0;
    // 0.6.0 (risk 4): its result box ("In a battle in <area> between ...") is the joined player's news, not a dialog
    // on the host that holds the new month until the host's player answers it.
    bool capture = texts && lockstep::BeginTextCapture();
    g_noDefer = true;
    uint32_t r = GuardedCall(A.mAIBattle, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)na, (uint32_t)(uintptr_t)nb, 3,
                             stack, &ok);
    g_noDefer = false;
    if (capture) *texts = lockstep::EndTextCapture();
    if (!ok) Log("ResolveAIBattle %d-%d: exception in the game's routine", a, b);
    return ok && (r & 0xFF) != 0;
}

bool StartPlayerBattle(int attacker, int defender, const std::wstring& areaName) {
    void* f = BuildCamp();
    void* na = Nation(attacker);
    void* nd = Nation(defender);
    void* area = areaName.empty() ? nullptr : AreaByName(areaName);  // B6: "" = the game picks the area
    if (!f || !na || !nd || (!area && !areaName.empty()) || !A.mCheckAttack || EventActive()) {
        Log("StartPlayerBattle %d-%d in %s: not possible", attacker, defender, W2U(areaName).c_str());
        return false;
    }
    uint32_t stack[1] = {(uint32_t)(uintptr_t)area};
    int ok = 0;
    bool r = (GuardedCall(A.mCheckAttack, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)na, (uint32_t)(uintptr_t)nd, 1,
                          stack, &ok) & 0xFF) != 0 && ok;
    Log("StartPlayerBattle %d attacks %d in %s -> %s", attacker, defender, W2U(areaName).c_str(),
        !ok ? "exception" : r ? "battle" : "none");
    return r;
}

// 0.6.0 (B6 piece 2): a surprise attack at a war's start in a battle slot: the game's CheckSurpriseAttack(attacker,
// defender), one of them the player nation (its dialogs, mobilization and battle as in single player).
bool StartSurpriseAttack(int attacker, int defender) {
    void* f = BuildCamp();
    void* na = Nation(attacker);
    void* nd = Nation(defender);
    if (!f || !na || !nd || !A.mCheckSurprise || EventActive()) {
        Log("StartSurpriseAttack %d-%d: not possible", attacker, defender);
        return false;
    }
    int ok = 0;
    bool r = (GuardedCall(A.mCheckSurprise, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)na, (uint32_t)(uintptr_t)nd, 0,
                          nullptr, &ok) & 0xFF) != 0 && ok;
    Log("StartSurpriseAttack %d attacks %d -> %s", attacker, defender, !ok ? "exception" : r ? "True" : "False");
    return r;
}

// ---------------------------------------------------------------------------------------------
// B6 (0.6.0): the player's battle generator for a joined nation K, on the host.
//
// In single player the game looks for the player's battle of the month in CheckForBattles2 (DoTurn2, before the rest of
// the month): at war (War > 0) invasion battles, raiders against patrols (30 %), then passes over sea areas and enemies
// (Tension > 40 in nation 0's view) calling CheckAttackLocation(attacker, defender, area or nil = it picks one), which
// picks the mission and the possession, asks the player and fights; the first True ends the search (at most one battle
// a month), a False goes on. For K the host runs it in K's view: the B3 view on the host (K at index 0, permuted relation
// cells), PlayerNation := K's record, War := K's own counter, and the nations people play read Tension <= 40 (their
// battles come from the host's own generator, and between joined players from the computer's as live battles).
// CheckAttackLocation does not run there: each call is recorded as an attempt and answers False like a call that found
// no battle, so the attempts are the ones single player would make until one gives a battle; K's PC replays them in its
// battle slot (session.cpp) until one does. Invasion battles and raiders against patrols answer False in the view (they
// read the host player's invasion and raider state, form fields).
// The attempts are recorded in the generator's order until it has none left (0.6.0: the cap only bounds a runaway
// generator). The generator's own error box ("Error in CheckForBattles procedure!", its except handler's ShowMessage)
// is recorded in the view instead of shown: the host's player never sees it and K gets no request that month.
static volatile bool g_b6View = false;
static std::vector<BattleAttempt> g_b6Attempts;
static const size_t kMaxAttempts = 64;
static std::wstring g_b6Error;           // the generator's error box in the view
static volatile bool g_b6Fault = false;  // tests: the next CheckAttackLocation in the view faults (an access violation)
extern "C" void* o_ShowMessage = nullptr;
extern "C" void* o_CheckForBattles = nullptr;
extern "C" void* o_CheckAttack = nullptr;
extern "C" void* o_InvasionBattles = nullptr;
extern "C" void* o_RaiderVsPatrols = nullptr;
extern "C" void* o_PlayerDecline = nullptr;
static volatile long g_declineCalls = 0;
static std::wstring g_lastBattleArea;

bool BattleGeneratorAvailable() { return A.battleGen; }
bool BattleReplayAvailable() { return A.battles && o_PlayerDecline; }
int PlayerDeclineCalls() { return (int)g_declineCalls; }

static bool DeclineFields(int& own, int& enemy) {
    static int fOwn = dl::Field("TfrmBuildCamp", "DeclineCount"), fEnemy = dl::Field("TfrmBuildCamp", "EnemyDeclineCount");
    own = fOwn;
    enemy = fEnemy;
    return BuildCamp() && fOwn > 0 && fEnemy > 0;
}

bool GetDeclineCounts(int& own, int& enemy) {
    int fo, fe;
    if (!DeclineFields(fo, fe)) return false;
    own = dl::At<int>(BuildCamp(), fo);
    enemy = dl::At<int>(BuildCamp(), fe);
    return true;
}

bool SetDeclineCounts(int own, int enemy) {
    int fo, fe;
    if (!DeclineFields(fo, fe)) return false;
    dl::At<int>(BuildCamp(), fo) = own;
    dl::At<int>(BuildCamp(), fe) = enemy;
    return true;
}

// 0.6.0 (B6 round 3): the month's own state for a joined player's battles. The player's game never processes a month
// (the host's does) and its battle slot is the host's month-end save. A static audit of what the month writes and the
// battle path reads (MODLOG "B6 round 3") found:
// - the ship-type flags (a battleship, a battlecruiser, a carrier exists; TFightingShip.SetMistakenType and IdentifyType
//   read them), set only by the month's ReduceTimeLimits (CheckShipTypesExist): this PC still had 0 for all three, or an
//   older campaign's. The game's routine computes them from the loaded campaign (it only counts every nation's ships in
//   play), after every month load and in the battle slot;
// - what DoTurn2 and DoStrategicTurn reset for a month: the surprise-attack flag (saved with the campaign: the host's
//   own surprise attack of the month made the player's battles partly surprise attacks: the setup's objectives, the
//   minute-10 step, the close's AdvanceDesignStudies; CheckSurpriseAttack sets it itself), the possession battle's flag
//   (DoPossessionBattle and DoHomeAreaBattle clear it, CheckSurpriseAttack's DoBattle reads it as left), and
//   HeroicVictory, tmpBattlePrestigeLoss and ImportantConvoy (left by an earlier battle on this PC; the aftermath's
//   DoBattlePrestigeLoss reads them: prestige and unrest messages and changes). The battle slot gets the reset values.
static bool CallSelfGuarded(void* m, void* self) {
    __try {
        dl::Call(m, (uint32_t)(uintptr_t)self);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string RefreshShipTypeFlags() {
    static void* m = dl::Method("TfrmBuildCamp", "CheckShipTypesExist");
    void* f = BuildCamp();
    std::string before = lockstep::ShipTypeFlags();
    if (!m || !f || before.empty()) return "";
    if (!CallSelfGuarded(m, f)) {
        Log("ship-type flags: the game's CheckShipTypesExist failed (an exception); this PC's %s stay", before.c_str());
        return "";
    }
    return before + " -> " + lockstep::ShipTypeFlags();
}

static uint8_t* g_surpriseFlag = nullptr;     // DoTurn2: mov byte ptr [var], 0; ... SurpriseAttackPossession := nil
static uint8_t* g_possessionFlag = nullptr;   // DoTurn2: PendingRaiderBattle := 0; mov eax, [cell]; mov byte ptr [eax], 0
static bool ResolveMonthResetVars() {
    static bool tried = false;
    if (tried) return g_surpriseFlag && g_possessionFlag;
    tried = true;
    uint8_t* fn = (uint8_t*)dl::Method("TfrmBuildCamp", "DoTurn2");
    int sap = dl::Field("TfrmBuildCamp", "SurpriseAttackPossession"), prb = dl::Field("TfrmBuildCamp", "PendingRaiderBattle");
    if (fn && sap > 0 && prb > 0) {
        std::vector<int> p1 = {0xC6, 0x05, -1, -1, -1, -1, 0x00, 0x8B, 0x45, 0xFC, 0x33, 0xD2, 0x89, 0x90};
        std::vector<int> p2 = {0xC6, 0x80};
        for (int i = 0; i < 4; i++) {
            p1.push_back((sap >> (8 * i)) & 0xFF);
            p2.push_back((prb >> (8 * i)) & 0xFF);
        }
        for (int b : {0x00, 0xA1, -1, -1, -1, -1, 0xC6, 0x00, 0x00}) p2.push_back(b);
        uint8_t* h1 = dl::FindPattern(fn, 0x200, p1);
        uint8_t* h2 = dl::FindPattern(fn, 0x200, p2);
        if (h1) g_surpriseFlag = *(uint8_t**)(h1 + 2);
        if (h2) g_possessionFlag = *(uint8_t**)*(uint8_t***)(h2 + 8);
    }
    Log("month reset variables for the battle slot (DoTurn2): surprise attack %p, possession battle %p", g_surpriseFlag,
        g_possessionFlag);
    return g_surpriseFlag && g_possessionFlag;
}

struct MonthFields {
    int heroic = -1, loss = -1, convoy = -1;
    MonthFields() {
        heroic = dl::Field("TfrmBuildCamp", "HeroicVictory");
        loss = dl::Field("TfrmBuildCamp", "tmpBattlePrestigeLoss");
        convoy = dl::Field("TfrmBuildCamp", "ImportantConvoy");
    }
    bool ok() const { return heroic > 0 && loss > 0 && convoy > 0; }
};
static const MonthFields& MF() {
    static MonthFields m;
    return m;
}

std::string PrepareBattleSlotMonth() {
    void* f = BuildCamp();
    if (!f) return "no campaign";
    std::string out, flags = RefreshShipTypeFlags();
    out = "ship-type flags " + (flags.empty() ? std::string("unavailable") : flags);
    if (ResolveMonthResetVars()) {
        out += ", surprise attack " + std::to_string(*g_surpriseFlag) + " -> 0, possession battle " +
               std::to_string(*g_possessionFlag) + " -> 0";
        *g_surpriseFlag = 0;
        *g_possessionFlag = 0;
    }
    const MonthFields& m = MF();
    if (m.ok()) {
        out += ", HeroicVictory " + std::to_string(dl::At<int>(f, m.heroic)) + " tmpBattlePrestigeLoss " +
               std::to_string(dl::At<uint8_t>(f, m.loss)) + " ImportantConvoy " +
               std::to_string(dl::At<uint8_t>(f, m.convoy)) + " -> 0";
        dl::At<int>(f, m.heroic) = 0;
        dl::At<uint8_t>(f, m.loss) = 0;
        dl::At<uint8_t>(f, m.convoy) = 0;
    }
    return out;
}

std::string MonthStateText(bool setForTest) {
    void* f = BuildCamp();
    const MonthFields& m = MF();
    if (!f || !ResolveMonthResetVars() || !m.ok()) return "";
    if (setForTest) {  // TEST: as an earlier battle or the host's surprise attack would leave them
        *g_surpriseFlag = *g_possessionFlag = 1;
        dl::At<int>(f, m.heroic) = 1;
        dl::At<uint8_t>(f, m.loss) = dl::At<uint8_t>(f, m.convoy) = 1;
        Log("tests: the month's battle fields set as an earlier battle would leave them");
    }
    char b[160];
    snprintf(b, sizeof(b), "flags=%s surprise=%d possession=%d heroic=%d loss=%d convoy=%d",
             lockstep::ShipTypeFlags().c_str(), *g_surpriseFlag, *g_possessionFlag, dl::At<int>(f, m.heroic),
             dl::At<uint8_t>(f, m.loss), dl::At<uint8_t>(f, m.convoy));
    return b;
}
std::wstring LastBattleArea() { return g_lastBattleArea; }

extern "C" void __stdcall CB_BeforeBattles() {
    if (g_b6View || g_battleSlotMode) return;
    try {
        hookcb::OnBattleGenerator();
    } catch (...) {
        Log("exception in OnBattleGenerator");
    }
}

// CheckForBattles2(Self=EAX): Boolean. The joined nations' generators run first (they only record).
extern "C" __declspec(naked) void d_CheckForBattles() {
    __asm {
        push eax
        call CB_BeforeBattles
        pop eax
        jmp dword ptr [o_CheckForBattles]
    }
}

extern "C" int __stdcall CB_B6Attack(void* attacker, void* defender, void* area) {
    try {
        BattleAttempt t;
        t.a = PvRealOf(attacker);
        t.b = PvRealOf(defender);
        t.area = AreaNameOf(area);
        g_b6Attempts.push_back(t);
    } catch (...) {
        return 1;
    }
    return g_b6Attempts.size() >= kMaxAttempts ? 1 : 0;
}

// CheckAttackLocation(Self=EAX, anAttacker=EDX, aDefender=ECX, anArea): Boolean, ret 4. Recorded in the view only.
extern "C" __declspec(naked) void d_CheckAttack() {
    __asm {
        cmp byte ptr [g_b6View], 0
        jne record
        jmp dword ptr [o_CheckAttack]
    record:
        cmp byte ptr [g_b6Fault], 0
        je rec2
        mov byte ptr [g_b6Fault], 0
        xor eax, eax
        mov dword ptr [eax], eax  // tests: a game exception inside the generator (its except handler catches it)
    rec2:
        push dword ptr [esp + 4]  // anArea
        push ecx
        push edx
        call CB_B6Attack
        ret 4
    }
}

// DoPlayerDecline(Self=EAX, aPossession=EDX): the player declined a battle (the battle's question or its preview).
extern "C" __declspec(naked) void d_PlayerDecline() {
    __asm {
        lock inc dword ptr [g_declineCalls]
        jmp dword ptr [o_PlayerDecline]
    }
}

extern "C" void __stdcall CB_B6Box(const void* text) {
    try {
        g_b6Error = text ? dl::ReadUStr(text) : L"";
        if (g_b6Error.empty()) g_b6Error = L"(no text)";
    } catch (...) {
        g_b6Error = L"(unreadable)";
    }
}

// ShowMessage(Msg=EAX): recorded in B6's view (the generator's error box), shown elsewhere.
extern "C" __declspec(naked) void d_ShowMessage() {
    __asm {
        cmp byte ptr [g_b6View], 0
        jne record
        jmp dword ptr [o_ShowMessage]
    record:
        push eax
        call CB_B6Box
        ret
    }
}

// CheckForBattles2's except handler: mov edx, 'Error in CheckForBattles procedure!'; call <concat>; mov eax, [ebp-0x14];
// call ShowMessage.
static void* FindGeneratorErrorBox(uint8_t* fn) {
    for (int i = 0; fn && i < 0x800; i++) {
        uint8_t* p = fn + i;
        uintptr_t s = *(uint32_t*)(p + 1);
        if (p[0] != 0xBA || !InImage(s) || wcsncmp((const wchar_t*)s, L"Error in CheckForBattles", 24) != 0) continue;
        if (p[5] == 0xE8 && p[10] == 0x8B && p[11] == 0x45 && p[12] == 0xEC && p[13] == 0xE8 &&
            InImage((uintptr_t)CallTarget(p + 13)))
            return CallTarget(p + 13);
    }
    return nullptr;
}

void TestGeneratorFault() { g_b6Fault = true; }

static int CallGenerator(void* f) {  // 1 = done, 0 = a game exception unwound through it
    __try {
        dl::Call(o_CheckForBattles, (uint32_t)(uintptr_t)f);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// The host-side view of a joined nation K (B6's view of RunBattleGenerator, shared with B5's war effects): on the HOST,
// B3's view (K at index 0, the host's nation at index K, every record's relation and alliance cells permuted: `Tension`
// = the relation with K, `Allied` = the alliance with K) plus form.PlayerNation := K's record and War := `war`; the
// nations people play (`human`, real indices, the host's nation is 0) read as not at war with K (cell 40). Not nested;
// false when the campaign is not in place (nothing changed). Code inside must not let a game exception unwind through
// the view (call the game under __try); HostViewLeave puts everything back.
static void* g_hvP0 = nullptr;
static int g_hvWar0 = 0;

bool HostViewEnter(int K, int war, const bool human[9]) {
    void* f = BuildCamp();
    void* list = NationList();
    if (!f || !list || NationCount() < 9 || K < 1 || K > 8 || PlayerNation() != Nation(0) || !A.warVar || g_pvOn)
        return false;
    g_pv.K = K;
    g_pv.arr = dl::At<void**>(list, 4);
    for (int r = 0; r < 9; r++) {
        g_pv.obj[r] = g_pv.arr[r];
        for (int w = 0; w < 9; w++) {
            g_pv.cells[r][w] = RelCellAt(g_pv.obj[r], w);
            g_pv.acells[r][w] = AllyCellAt(g_pv.obj[r], w);
        }
        g_pv.will[r] = dl::At<int>(g_pv.obj[r], A.fWillGoToWar);
        g_pv.ts[r] = dl::At<int>(g_pv.obj[r], A.fTechSharing);
    }
    for (int a = 0; a < 9; a++)
        for (int b = 0; b < 9; b++) {
            g_pv.rel[a][b] = a == b ? 0 : Tension(a, b);
            g_pv.ally[a][b] = a == b ? 0 : AllianceMonths(a, b);
        }
    for (int v = 0; v < 9; v++) g_pvHuman[v] = v > 0 && human[PvReal(v)];
    g_hvP0 = dl::At<void*>(f, A.fPlayerNation);
    g_hvWar0 = *A.warVar;
    PvEnter();
    for (int v = 1; v <= 8; v++)  // the nations people play: no war seen with them here
        if (g_pvHuman[v] && RelCellAt(PvRec(v), 0) > 40) RelCellAt(PvRec(v), 0) = 40;
    dl::At<void*>(f, A.fPlayerNation) = g_pv.obj[K];
    *A.warVar = war;
    return true;
}

void HostViewLeave() {
    if (!g_pvOn) return;
    *A.warVar = g_hvWar0;
    dl::At<void*>(BuildCamp(), A.fPlayerNation) = g_hvP0;
    PvLeave();
}

bool RunBattleGenerator(int K, int war, const bool human[9], std::vector<BattleAttempt>& out, std::string& err) {
    out.clear();
    void* f = BuildCamp();
    void* list = NationList();
    if (!A.battleGen || !f || !list || NationCount() < 9 || K < 1 || K > 8 || PlayerNation() != Nation(0) ||
        EventActive() || !A.warVar || g_b6View || g_battleSlotMode || !HostViewEnter(K, war, human)) {
        if (!g_b6View) g_b6Fault = false;
        err = "the player's battle generator is not available here";
        return false;
    }
    g_b6Attempts.clear();
    g_b6Error.clear();
    g_b6View = true;
    int ok = CallGenerator(f);
    g_b6View = false;
    g_b6Fault = false;
    HostViewLeave();
    out = g_b6Attempts;
    g_b6Attempts.clear();
    if (!ok || !g_b6Error.empty()) {  // no request from a generator that failed (its attempts may be partial)
        std::string box = W2U(g_b6Error);
        for (char& c : box)
            if (c == '\r' || c == '\n') c = ' ';
        err = "exception in the game's battle generator" + (box.empty() ? std::string() : ": " + box);
        out.clear();
        g_b6Error.clear();
        return false;
    }
    return true;
}

bool ModalOpen() {
    void* app = Application();
    return app && dl::At<int>(app, A.fModalLevel) > 0;
}

void SetStrategicEnabled(bool on) {
    HWND h = FormHwnd(BuildCamp());
    if (h) EnableWindow(h, on ? TRUE : FALSE);
}

std::vector<std::wstring> AreaNames() {
    std::vector<std::wstring> out;
    void* f = BuildCamp();
    if (!f || A.fMapAreas < 0 || A.fAreaName < 0) return out;
    void* areas = dl::At<void*>(f, A.fMapAreas);
    for (int i = 0; i < dl::ListCount(areas); i++)
        if (void* a = dl::ListItem(areas, i)) out.push_back(AreaNameOf(a));
    return out;
}

// Steps of the campaign the battle screen's close runs; skipped in a battle slot (which = 1 DoStrategicTurn, 2
// AdvanceDesignStudies).
extern "C" int __stdcall CB_SkipCampaignStep(int which) {
    if (!g_battleSlotMode) {
        if (which == 1) {
            __try {
                hookcb::OnStrategicTurnRuns();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        return 0;
    }
    Log("battle slot: %s skipped", which == 1 ? "DoStrategicTurn" : "AdvanceDesignStudies");
    if (which == 1) {
        __try {
            hookcb::OnBattleSlotSkippedTurn();  // the month that would have reset the campaign date doesn't run
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return 1;
}

extern "C" void* o_StrategicTurn = nullptr;

// DoBattle(Self, anOpponentIndex, aMission, anArea, aPossession), ret 8: the game's battle setup (forces, the battle
// preview, the battle screen). Counted, so a battle the player declined in the preview can be told from no battle.
extern "C" void* o_DoBattle = nullptr;
static volatile long g_doBattleCalls = 0;

extern "C" void __stdcall CB_AfterDoBattle(int opp, int result, void* area) {
    try {
        if (area) g_lastBattleArea = AreaNameOf(area);
        hookcb::OnDoBattleDone(opp, (result & 0xFF) != 0);
    } catch (...) {
        Log("exception after DoBattle");
    }
}

// Calls through (the callback sees the battle screen DoBattle opened, e.g. for a live battle against a player).
extern "C" __declspec(naked) void d_DoBattle() {
    __asm {
        lock inc dword ptr [g_doBattleCalls]
        push edx                   // anOpponentIndex
        push dword ptr [esp + 12]  // anArea
        push dword ptr [esp + 12]  // aPossession
        call dword ptr [o_DoBattle]
        pop edx
        push eax
        push dword ptr [esp + 12]  // anArea
        push eax
        push edx
        call CB_AfterDoBattle
        pop eax
        ret 8
    }
}

int DoBattleCalls() { return (int)g_doBattleCalls; }

extern "C" __declspec(naked) void d_StrategicTurn() {  // DoStrategicTurn(Self)
    __asm {
        push eax
        push 1
        call CB_SkipCampaignStep
        test eax, eax
        pop eax
        jnz skip
        jmp dword ptr [o_StrategicTurn]
    skip:
        ret
    }
}

extern "C" __declspec(naked) void d_AIMoves() {
    __asm {
        push eax
        push eax
        call CB_BeforeAIMoves
        mov eax, [esp]
        call dword ptr [o_AIMoves]
        call CB_AfterAIMoves
        ret
    }
}

// Relations v2, B2 (see PlayerRules): the routines that run with the pairs of nations under the player's rules masked.
// All are register-only (Self and at most one more argument); MaskedCall runs them through dl::Call inside __try, so a
// game exception unwinding through one still puts the mask back. kind 0: the computer's rules (AIMaskEnter); 1: the
// host's CheckForWar, 2: the host's RandomTensionChanges (PlayerMaskEnter).
struct MaskSpec {
    const char* cls;
    const char* method;
    int kind;
    void* orig;
};
static MaskSpec g_maskSpecs[] = {
    {"TfrmBuildCamp", "CheckAITensionEvents", 0, nullptr},
    {"TBuilderNation", "DoAITensionChanges", 0, nullptr},
    {"TfrmBuildCamp", "CheckStopPlayerAgression", 0, nullptr},
    {"TfrmBuildCamp", "CheckAllyTrouble", 0, nullptr},
    {"TfrmBuildCamp", "CheckBlitzkrieg", 0, nullptr},
    {"TfrmBuildCamp", "CheckChineseConcessions", 0, nullptr},
    {"TfrmBuildCamp", "CheckForceOnForeignStations", 0, nullptr},
    {"TfrmBuildCamp", "CreateArmsControlTreaty", 0, nullptr},
    {"TfrmBuildCamp", "CheckForRevolutionInEnemyNation", 0, nullptr},
    {"TfrmBuildCamp", "CheckForWar", 1, nullptr},
    {"TfrmBuildCamp", "RandomTensionChanges", 2, nullptr},
};

// B5 (round 4): the host's own enemy-revolution rule (CheckForRevolutionInEnemyNation through the thunk, P = the host's
// nation) never picks a nation people play: its revolution would force a peace between players, which only their own
// peace makes. A joined nation at war with the host reads at most unrest 10 for the call (the rule's test, 0xcc5d6d:
// above 10 and Random(10) < unrest - 10); the rule's own changes (its relief above 8 at War 1 and 2) are kept on top of
// the real level. The host's view of a joined nation (B6) never runs it.
static int g_revoCap[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};

static void RevoCapEnter() {
    if (g_pvOn || A.fUnrest < 0 || NationCount() < 9) return;
    std::string s;
    for (int i = 1; i <= 8; i++) {
        int& u = dl::At<int>(Nation(i), A.fUnrest);
        if (!hookcb::IsHumanRemoteNation(i) || TensionCell(i, 0) <= 40 || u <= 10) continue;
        g_revoCap[i] = u;
        s += " " + std::to_string(i) + " (unrest " + std::to_string(u) + ": it would be a candidate)";
        u = 10;
    }
    if (!s.empty())
        Log("relations v2: revolution (host): the nations people play at war with the host stay out of its "
            "enemy-revolution rule (no forced peace between players):%s", s.c_str());
}

static void RevoCapLeave() {
    for (int i = 1; i <= 8; i++) {
        if (g_revoCap[i] < 0) continue;
        int& u = dl::At<int>(Nation(i), A.fUnrest);
        u = g_revoCap[i] + (u - 10);
        g_revoCap[i] = -1;
    }
}

// 0.6.0: the arms-control conference sees the wars of the nations people play. CreateArmsControlTreaty's first test is
// AnyNationIsAtAIWar (0xcb6c26; IsAtAIWar 0xc68ecc: a tension above 40 between two of nations 1..8): a war in progress
// ends the conference with the game's own message ("With a war already in progress, the conference ends in solemn
// declarations and no action.") before the routine reads or changes anything else. Under B2's mask (its own, or a
// caller's such as CheckAllyTrouble's) a joined nation's pairs read 5, so the conference went on during its war (battles
// B1: a treaty while nation 1 was at war with Spain): the treaty scrapped its ships under construction over the limits
// (ScrapShip's refunds after ComputeExpenses), cut every BudgetModifier by 2, and the nation's own PC then cancelled it
// with a box ("The outbreak of war cancels the disarmament treaty!", CancelTreaty from UpdateUIValues). The masked pairs
// at war (above 40 before the mask) show their own tension for the call; put back to the mask's 5 afterwards (the masks
// then restore them as before). Bit (a - 1) * 8 + (b - 1) for the pair (a, b). A treaty already in force when a joined
// nation's war breaks out is ended once by the host (EndArmsTreatyForWar, session.cpp's HostArmsTreatyCheck); a treaty
// the conference makes at peace is measured for each joined nation's month (economy::TreatyBefore/TreatyAfter).
static uint64_t TreatyWarsEnter() {
    uint64_t shown = 0;
    if (!g_aiMask.on || NationCount() < 9) return 0;
    std::string s;
    for (int a = 1; a <= 8; a++)
        for (int b = 1; b <= 8; b++) {
            if (a == b || !(g_aiMask.k[a] || g_aiMask.k[b]) || g_aiMask.t[a][b] <= 40) continue;
            if (TensionCell(a, b) != kMaskTension) continue;
            TensionCell(a, b) = g_aiMask.t[a][b];
            shown |= 1ull << ((a - 1) * 8 + (b - 1));
            s += " " + std::to_string(a) + "-" + std::to_string(b);
        }
    if (shown)
        Log("relations v2: the arms-control conference (CreateArmsControlTreaty) sees the war(s) of the nations people "
            "play:%s (a war in progress ends it, as in single play)", s.c_str());
    return shown;
}

static void TreatyWarsLeave(uint64_t shown) {
    for (int a = 1; a <= 8 && shown; a++)
        for (int b = 1; b <= 8; b++)
            if (a != b && (shown >> ((a - 1) * 8 + (b - 1)) & 1)) TensionCell(a, b) = kMaskTension;
}

static bool g_treatyBlind = false;  // TEST ONLY (TestTreatyBlind)
void TestTreatyBlind(bool on) { g_treatyBlind = on; }

extern "C" uint32_t __stdcall MaskedCall(int idx, uint32_t eax, uint32_t edx) {
    const MaskSpec& s = g_maskSpecs[idx];
    const bool revo = strcmp(s.method, "CheckForRevolutionInEnemyNation") == 0;
    const bool treaty = strcmp(s.method, "CreateArmsControlTreaty") == 0;
    uint64_t wars = 0;
    uint32_t r = 0;
    __try {
        MaskEnterSafe(s.kind, s.method);
        if (revo) RevoCapEnter();
        if (treaty) {
            if (!g_treatyBlind) wars = TreatyWarsEnter();
            economy::TreatyBefore();
        }
        r = dl::Call(s.orig, eax, edx);
        if (treaty) economy::TreatyAfter();
    } __finally {
        if (revo) RevoCapLeave();
        if (treaty) TreatyWarsLeave(wars);
        MaskLeaveSafe(s.kind);
    }
    return r;
}

// A masked routine's own code (without B2's mask: e.g. run in a joined nation's view on its PC).
static void* MaskOrig(const char* method) {
    for (auto& s : g_maskSpecs)
        if (strcmp(s.method, method) == 0) return A.playerRules && s.orig ? s.orig : dl::Method(s.cls, s.method);
    return nullptr;
}

static void* EmitMaskThunk(int idx) {  // push edx; push eax; push idx; mov eax, MaskedCall; call eax; ret
    uint8_t* mem = (uint8_t*)VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) return nullptr;
    uint8_t* p = mem;
    *p++ = 0x52;
    *p++ = 0x50;
    *p++ = 0x68;
    *(int32_t*)p = idx;
    p += 4;
    *p++ = 0xB8;
    *(uint32_t*)p = (uint32_t)(uintptr_t)&MaskedCall;
    p += 4;
    *p++ = 0xFF;
    *p++ = 0xD0;
    *p++ = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), mem, p - mem);
    return mem;
}

// ChangeAITension(Self=EAX, Idx=EDX, anAmount=ECX, aCause on the stack): a pair with a nation under the player's rules
// is left alone (the change and its turn message).
extern "C" void* o_ChangeAITension = nullptr;

extern "C" BOOL __stdcall CB_SkipChangeAITension(void* self, int idx) {
    __try {
        if (A.playerRules && (PlayerRules(NationIndex(self)) || PlayerRules(idx))) {
            g_aiChangesSkipped++;
            return TRUE;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return FALSE;
}

extern "C" __declspec(naked) void d_ChangeAITension() {
    __asm {
        push eax
        push ecx
        push edx
        push edx
        push eax
        call CB_SkipChangeAITension
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz skip
        jmp dword ptr [o_ChangeAITension]
    skip:
        ret 4
    }
}

// 0.6.0: the disarmament treaty (CreateArmsControlTreaty: TreatyTimeRemaining, the displacement and calibre limits, each
// nation's tonnage limit; also the submarine ban, SubBanTime) is campaign-wide: form fields in [General]. CancelTreaty
// (Self) ends both ("The outbreak of war cancels the disarmament treaty!" while months remain): the game calls it at
// every screen refresh while nation 0 is at war (UpdateUIValues) and when two computer nations go to war
// (CheckAITensionEvents). A joined player's PC holds the host's copy with the nation's own war counter in place (relations
// v2, B1) and never sends [General] back. There the game's end runs without its box when the host ends the treaty for a
// joined nation's war too (its month header `armstreaty`; EndArmsTreatyForWar, session.cpp): at the first refresh at
// war, e.g. right after the nation's relation turn started a war, as the player's war ends it in single player. With an
// older host the treaty stays as the host has it, and so it does on the host while a view puts another war counter in
// place.
extern "C" void* o_CancelTreaty = nullptr;
static bool g_hostEndsTreaty = false;                          // a joined PC: the month's header `armstreaty`
static int g_treatyKept = 0;                                   // calls left to the host (bridge `armstreaty`)
static int g_treatyEndedHere = 0;                              // ends on a joined PC with the host's rule
static int g_treatyKeptLog[3] = {INT_MIN, INT_MIN, INT_MIN};  // the state of the last one logged

extern "C" BOOL __stdcall CB_KeepArmsTreaty(void* self) {
    __try {
        bool joined = hookcb::JoinedPlayerPC(), view = EventActive();
        if (!joined && !view) return FALSE;
        int& months = dl::At<int>(self, A.fTreatyTime);
        int ban = dl::At<int>(self, A.fSubBan), war = A.warVar ? *A.warVar : 0;
        if (joined && !view && g_hostEndsTreaty) {
            if (months > 0 || ban > 0) {
                g_treatyEndedHere++;
                Log("arms treaty: ended on this PC for its nation's war, as the host ends it (%d month(s) left, submarine "
                    "ban %d; war counter %d)", months, ban, war);
            }
            months = 0;  // the game's end without its box (the host's month brings the game's line)
            return FALSE;
        }
        g_treatyKept++;
        if (months != g_treatyKeptLog[0] || ban != g_treatyKeptLog[1] || war != g_treatyKeptLog[2]) {
            g_treatyKeptLog[0] = months;
            g_treatyKeptLog[1] = ban;
            g_treatyKeptLog[2] = war;
            Log("arms treaty: CancelTreaty left to the host (%s; treaty %d month(s), submarine ban %d; war counter %d)",
                view ? "a view" : "a joined player's PC, an older host", months, ban, war);
        }
        return TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return FALSE;
}

extern "C" __declspec(naked) void d_CancelTreaty() {  // CancelTreaty(Self); no other arguments, no result
    __asm {
        push eax
        push eax
        call CB_KeepArmsTreaty
        test eax, eax
        pop eax
        jnz keep
        jmp dword ptr [o_CancelTreaty]
    keep:
        ret
    }
}

ArmsTreaty ArmsTreatyNow() {
    ArmsTreaty t;
    void* f = BuildCamp();
    if (!A.armsTreaty || !f) return t;
    t.months = dl::At<int>(f, A.fTreatyTime);
    t.disp = dl::At<int>(f, A.fTreatyDisp);
    t.cal = dl::At<int>(f, A.fTreatyCal);
    t.subBan = dl::At<int>(f, A.fSubBan);
    t.base = dl::At<int>(f, A.fTreatyBase);
    t.ok = true;
    return t;
}

bool EndArmsTreatyForWar() {
    void* f = BuildCamp();
    if (!A.armsTreaty || !f) return false;
    int& months = dl::At<int>(f, A.fTreatyTime);
    if (months <= 0 && dl::At<int>(f, A.fSubBan) <= 0) return false;
    months = 0;  // the game's box shows only while months remain: the players are told by the session instead
    dl::Call(o_CancelTreaty ? o_CancelTreaty : A.mCancelTreaty, (uint32_t)(uintptr_t)f);
    return true;
}

int ArmsTreatyKept() { return g_treatyKept; }
int ArmsTreatyEndedHere() { return g_treatyEndedHere; }
bool ArmsTreatyAvailable() { return A.armsTreaty; }
void SetHostEndsArmsTreaty(bool on) { g_hostEndsTreaty = on && A.armsTreaty; }
bool HostEndsArmsTreaty() { return g_hostEndsTreaty; }

void TestSetArmsTreaty(int months, int disp, int cal) {
    void* f = BuildCamp();
    int fTon = dl::Field("TBuilderNation", "TreatyTonnageLimit");
    if (!A.armsTreaty || !f || fTon < 0) return;
    dl::At<int>(f, A.fTreatyTime) = months;
    dl::At<int>(f, A.fTreatyDisp) = disp;
    dl::At<int>(f, A.fTreatyCal) = cal;
    // no tonnage limits (MaxInt: GetTreatyMaxBuildTonnage and CheckTreatyTonnageLimit let everything pass), so the
    // test treaty never stops the host's Turn ("Due to treaty build restrictions, ...")
    for (int i = 0; i < 9 && i < NationCount(); i++)
        if (Nation(i)) dl::At<int>(Nation(i), fTon) = INT_MAX;
    Log("arms treaty: set by a test: %d month(s), %d t, %d in (no tonnage limits)", months, disp, cal);
}

void TestArmsConference(int chance) {
    void* f = BuildCamp();
    if (!A.armsTreaty || !f || EventActive()) return;
    Log("arms treaty: the game's conference by a test (CreateArmsControlTreaty(%d))", chance);
    dl::Call(A.mCreateArms, (uint32_t)(uintptr_t)f, (uint32_t)chance);  // its hook: B2's mask, as in the month
    ArmsTreaty t = ArmsTreatyNow();
    Log("arms treaty: after the conference: %d month(s), %d t, %d in", t.months, t.disp, t.cal);
}

void TestSetWarCounter(int months) {
    if (!A.warVar) return;
    Log("war counter: %d -> %d by a test", *A.warVar, months);
    *A.warVar = months;
}

// Relations v2, B3, in K's view (RunPlayerTurn): CheckWarsWithNation(Self=EAX, anOpponentIdx=EDX, var LastWinner=ECX,
// var NumberOfWars on the stack): months since the last war with that nation (MaxInt: none) from K's own past wars,
// not from the month records (nation 0's).
extern "C" void* o_WarsWithNation = nullptr;
extern "C" int __stdcall CB_WarsWithNation(void* self, int idx, int* lastWinner, int* number) {
    if (g_pvOn && idx >= 0 && idx <= 8) {
        int o = PvReal(idx);
        if (lastWinner) *lastWinner = g_pv.histWinner[o];
        if (number) *number = g_pv.histCount[o];
        return g_pv.histMonths[o];
    }
    uint32_t st = (uint32_t)(uintptr_t)number;
    return (int)dl::Call(o_WarsWithNation, (uint32_t)(uintptr_t)self, (uint32_t)idx, (uint32_t)(uintptr_t)lastWinner, 1,
                         &st);
}

extern "C" __declspec(naked) void d_WarsWithNation() {
    __asm {
        push dword ptr [esp + 4]
        push ecx
        push edx
        push eax
        call CB_WarsWithNation
        ret 4
    }
}

// While the relation routines run in K's view these return nothing (nil / False) without running: GetRandomPossessionInArea,
// GetRandomPossession, DoSurpriseAttack, CheckForPlayerSurpriseInvasion (all register-only). Elsewhere (also in the peace
// part of B4, where the game's possession choice after a war uses GetRandomPossession) the original runs.
extern "C" void* o_RandPossArea = nullptr;
extern "C" void* o_RandPoss = nullptr;
extern "C" void* o_SurpriseAttack = nullptr;
extern "C" void* o_SurpriseInvasion = nullptr;

// cmp byte [flag],0; je +3; xor eax,eax; ret; jmp [orig]  (flag: g_pvSkip, or B6's host-side view flag)
static void* EmitViewSkipThunk(void** origCell, const volatile bool* flag = nullptr) {
    uint8_t* mem = (uint8_t*)VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) return nullptr;
    uint8_t* p = mem;
    *p++ = 0x80;
    *p++ = 0x3D;
    *(uint32_t*)p = (uint32_t)(uintptr_t)(flag ? flag : &g_pvSkip);
    p += 4;
    *p++ = 0x00;
    *p++ = 0x74;
    *p++ = 0x03;
    *p++ = 0x31;
    *p++ = 0xC0;
    *p++ = 0xC3;
    *p++ = 0xFF;
    *p++ = 0x25;
    *(uint32_t*)p = (uint32_t)(uintptr_t)origCell;
    p += 4;
    FlushInstructionCache(GetCurrentProcess(), mem, p - mem);
    return mem;
}

// Generated thunks: skip an AI routine when its nation argument belongs to a remote human player.
struct SkipSpec {
    const char* cls;
    const char* method;
    int argReg;   // 0 = EAX (Self), 1 = EDX, 2 = ECX
    bool isIndex; // argument is a nation index instead of a TBuilderNation
    uint16_t retBytes;
    void* orig;
};
static SkipSpec g_skips[] = {
    {"TfrmBuildCamp", "AIMonthlyBuild", 1, false, 0, nullptr},
    {"TfrmBuildCamp", "AINationAdjustBudget", 1, false, 0, nullptr},
    {"TfrmBuildCamp", "PutAIShipsInStatus", 1, false, 0, nullptr},
    {"TfrmBuildCamp", "DetermineAINationResearch", 1, false, 0, nullptr},
    {"TfrmBuildCamp", "CheckAIRebuilds", 1, false, 0, nullptr},
    {"TfrmBuildCamp", "CheckAINationShipsOnTradePatrol", 1, false, 0, nullptr},
    {"TfrmBuildCamp", "AINationAssignRaiders", 1, true, 0, nullptr},
    {"TfrmBuildCamp", "AISendShipsToForeignStations", 1, false, 0, nullptr},
    {"TfrmBuildCamp", "AISendShipToColonies", 1, false, 4, nullptr},
    {"TBuilderNation", "SetAITraining", 0, false, 0, nullptr},
    // The AI's spying (SpySuccess for an AI spy: gun qualities copied silently); a joined player's nation spies as the
    // player does, on the player's PC (nationplay.cpp).
    {"TfrmBuildCamp", "CheckAISpying", 1, true, 0, nullptr},
    // The computer's decisions about a joined nation that its player makes on the player's PC (all register-only, no
    // result): the doctrine a finished research item brings (oxygen torpedoes, AP shell priority, diving missiles), the
    // January ammunition loadout, ships sent home from the colonies, ships put on ASW patrol (CheckPatrols,
    // CheckSubmarineActivity), air groups for carriers and bases (and the 6-monthly adds and deletes).
    {"TfrmBuildCamp", "CheckNewResearchEffectsForAI", 1, false, 0, nullptr},
    {"TBuilderNation", "SetAmmoLoadoutAndUsage", 0, false, 0, nullptr},
    {"TBuilderNation", "MaybeSendHomeShipInColonies", 0, false, 0, nullptr},
    {"TBuilderNation", "PutShipOnASWPatrol", 0, false, 0, nullptr},
    {"TfrmBuildCamp", "CheckOneShipsOrBaseHasAircraft", 2, true, 0, nullptr},
};

extern "C" int __stdcall SkipDecider(int idx, uint32_t arg) {
    try {
        const SkipSpec& s = g_skips[idx];
        int nation = s.isIndex ? (int)arg : NationIndex((void*)(uintptr_t)arg);
        if (nation > 0 && hookcb::IsHumanRemoteNation(nation)) {
            Log("AI suppressed: %s for nation %d", s.method, nation);
            return 1;
        }
    } catch (...) {
    }
    return 0;
}

static void* EmitSkipThunk(int idx) {
    uint8_t* mem = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) return nullptr;
    uint8_t* p = mem;
    const SkipSpec& s = g_skips[idx];
    *p++ = 0x50;  // push eax
    *p++ = 0x51;  // push ecx
    *p++ = 0x52;  // push edx
    *p++ = s.argReg == 0 ? 0x50 : s.argReg == 1 ? 0x52 : 0x51;  // push arg register
    *p++ = 0x68;  // push imm32 (hook index)
    *(int32_t*)p = idx;
    p += 4;
    *p++ = 0xB8;  // mov eax, SkipDecider
    *(uint32_t*)p = (uint32_t)(uintptr_t)&SkipDecider;
    p += 4;
    *p++ = 0xFF;  // call eax
    *p++ = 0xD0;
    *p++ = 0x85;  // test eax, eax
    *p++ = 0xC0;
    *p++ = 0x5A;  // pop edx
    *p++ = 0x59;  // pop ecx
    *p++ = 0x58;  // pop eax
    *p++ = 0x75;  // jnz skip
    *p++ = 0x06;
    *p++ = 0xFF;  // jmp [orig]
    *p++ = 0x25;
    *(uint32_t*)p = (uint32_t)(uintptr_t)&g_skips[idx].orig;
    p += 4;
    if (s.retBytes) {  // skip: ret n
        *p++ = 0xC2;
        *(uint16_t*)p = s.retBytes;
        p += 2;
    } else {
        *p++ = 0xC3;
    }
    FlushInstructionCache(GetCurrentProcess(), mem, p - mem);
    return mem;
}

static bool Hook(void* target, void* detour, void** orig, const char* name) {
    if (!target) {
        Log("hook %s: no target", name);
        return false;
    }
    MH_STATUS st = MH_CreateHook(target, detour, orig);
    if (st != MH_OK) {
        Log("hook %s: MH_CreateHook failed %d", name, (int)st);
        return false;
    }
    Log("hook %s at rva %#x", name, (unsigned)((uintptr_t)target - dl::g_base));
    return true;
}

// TEST ONLY (env RTW3MP_TEST_PREFS, docs/TESTING.md "Two-PC conditions"): this copy's own preferences without touching
// the shared Preferences file. The game's reader (TdlgPrefs.LoadPrefs(Self, DoSetStyle), register arguments only) is
// followed by the overrides; its writer (TdlgPrefs.SavePrefs(Self)) does nothing.
extern "C" void* o_LoadPrefs = nullptr;
extern "C" void* o_SavePrefs = nullptr;
extern "C" void __stdcall CB_AfterLoadPrefs() {
    try {
        lockstep::ApplyTestSwitches("the game read its Preferences");
    } catch (...) {
    }
}
extern "C" void __stdcall CB_SavePrefsSkipped() {
    Log("TEST: the game's Preferences writer skipped (RTW3MP_TEST_PREFS: this copy never writes the shared file)");
}
extern "C" __declspec(naked) void d_LoadPrefs() {
    __asm {
        call dword ptr [o_LoadPrefs]
        push eax
        call CB_AfterLoadPrefs
        pop eax
        ret
    }
}
extern "C" __declspec(naked) void d_SavePrefs() {
    __asm {
        push eax
        call CB_SavePrefsSkipped
        pop eax
        ret
    }
}

bool InstallHooks() {
    MH_STATUS mh = MH_Initialize();  // (already initialized: the TEST file-open probe, datafiles::PocFileLog)
    if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) {
        Log("MH_Initialize failed");
        return false;
    }
    bool ok = true;
    ok &= Hook(A.mTurnClick, (void*)&d_TurnClick, &o_TurnClick, "btbnTurnClick");
    ok &= Hook(A.mResignClick, (void*)&d_ResignClick, &o_ResignClick, "btbnResignClick");
    ok &= Hook(A.mDoEndOfTurn, (void*)&d_EndOfTurn, &o_EndOfTurn, "DoEndOfTurn");
    if (A.mAIMoves) Hook(A.mAIMoves, (void*)&d_AIMoves, &o_AIMoves, "AIStrategicMoves");
    if (A.diplomacy && A.mHandleAIPeace) Hook(A.mHandleAIPeace, (void*)&d_AIPeace, &o_AIPeace, "HandleAIPeace");
    if (A.diplomacy && A.mDoPeace && A.fPrestige >= 0) Hook(A.mDoPeace, (void*)&d_DoPeace, &o_DoPeace, "DoPeace");
    if (A.mCopyGame) Hook(A.mCopyGame, (void*)&d_CopyGame, &o_CopyGame, "CopyGameToOtherSlot");
    if (A.mResearchExecute) Hook(A.mResearchExecute, (void*)&d_ResearchExecute, &o_ResearchExecute, "TfrmResearch.Execute");
    if (A.mDoResearch && A.fResearchAreas >= 0) Hook(A.mDoResearch, (void*)&d_DoResearch, &o_DoResearch, "DoResearch");
    if (A.mAdvanceDesignStudies && A.fDesignList >= 0 && A.fReadyForBuild >= 0)
        Hook(A.mAdvanceDesignStudies, (void*)&d_DesignStudies, &o_DesignStudies, "AdvanceDesignStudies");
    if (A.fIntelReports >= 0 && A.fSLList >= 0 && A.fSLCount >= 0 && A.mSLClear && A.mSLAdd)
        Hook(A.mLoad, (void*)&d_Load, &o_Load, "LoadBuildCampaign");
    if (A.loadBlockadeRet) Hook(A.mCheckBlockade, (void*)&d_CheckBlockade, &o_CheckBlockade, "CheckBlockadeStatus");
    if (A.doctrine || A.playerRules)
        Hook(A.mReduceTimeLimits, (void*)&d_ReduceTimeLimits, &o_ReduceTimeLimits, "ReduceTimeLimits");
    if (A.events) Hook(A.mLikelihood, (void*)&d_Likelihood, &o_Likelihood, "EventLikelihoodCheck");
    bool aftermathHook = false, aiBattleHook = false;
    if (A.treaties) {
        if (A.mBattleAftermath)
            aftermathHook = Hook(A.mBattleAftermath, (void*)&d_BattleAftermath, &o_BattleAftermath, "HandleBattleAftermath");
        if (A.mAIBattle) aiBattleHook = Hook(A.mAIBattle, (void*)&d_AIBattleDefer, &o_AIBattle, "DoBattleBetweenAINations");
        if (A.mRaiderBattle) Hook(A.mRaiderBattle, (void*)&d_RaiderBattle, &o_RaiderBattle, "QuickResolveRaiderBattle");
    }
    // Joined players' battles need the aftermath and AI-battle hooks and the month-step skip in a battle slot.
    A.battles = A.battles && aftermathHook && aiBattleHook &&
                Hook(A.mStrategicTurn, (void*)&d_StrategicTurn, &o_StrategicTurn, "DoStrategicTurn");
    if (A.battles && A.mDoBattle) Hook(A.mDoBattle, (void*)&d_DoBattle, &o_DoBattle, "DoBattle");
    // 0.6.0: the player's declines in a battle slot (risk 4: merged as the game does for the player).
    if (A.battles && A.mPlayerDecline) Hook(A.mPlayerDecline, (void*)&d_PlayerDecline, &o_PlayerDecline, "DoPlayerDecline");
    if (!A.battles) Log("Joined players' battles: UNAVAILABLE (hooks)");
    if (A.relView) {  // a joined player's screen shows its own nation's relations
        bool core = Hook(A.mShowTension, (void*)&d_ShowTension, &o_ShowTension, "ShowTensionLevels") &&
                    Hook(A.mUpdateUIValues, (void*)&d_UpdateUI, &o_UpdateUI, "UpdateUIValues");
        if (A.mSetValues) Hook(A.mSetValues, (void*)&d_SetValues, &o_SetValues, "SetValues");
        if (A.mStrengthBars) Hook(A.mStrengthBars, (void*)&d_StrengthBars, &o_StrengthBars, "DrawAreaStrengthBars");
        if (A.mNationData) Hook(A.mNationData, (void*)&d_NationData, &o_NationData, "TdlgNationData.Execute");
        if (!core) {
            A.relView = false;
            Log("Relations view: UNAVAILABLE (hooks)");
        }
    }
    for (int i = 0; i < (int)(sizeof(g_skips) / sizeof(g_skips[0])); i++) {
        void* target = dl::Method(g_skips[i].cls, g_skips[i].method);
        void* thunk = target ? EmitSkipThunk(i) : nullptr;
        if (!target || !thunk) {
            Log("skip hook %s unavailable", g_skips[i].method);
            continue;
        }
        Hook(target, thunk, &g_skips[i].orig, g_skips[i].method);
    }
    // Relations v2, B2: the computer's relation rules leave the pairs of nations under the player's rules alone. All or
    // nothing: without one of the routines a nation under the player's rules would get half of the computer's rules.
    if (A.playerRules) {
        bool all = A.mReduceTimeLimits && A.mHandleAIPeace;
        void* thunks[sizeof(g_maskSpecs) / sizeof(g_maskSpecs[0])] = {};
        for (int i = 0; all && i < (int)(sizeof(g_maskSpecs) / sizeof(g_maskSpecs[0])); i++)
            all = dl::Method(g_maskSpecs[i].cls, g_maskSpecs[i].method) && (thunks[i] = EmitMaskThunk(i)) != nullptr;
        all = all && Hook(A.mChangeAITension, (void*)&d_ChangeAITension, &o_ChangeAITension, "ChangeAITension");
        for (int i = 0; all && i < (int)(sizeof(g_maskSpecs) / sizeof(g_maskSpecs[0])); i++)
            all = Hook(dl::Method(g_maskSpecs[i].cls, g_maskSpecs[i].method), thunks[i], &g_maskSpecs[i].orig,
                       g_maskSpecs[i].method);
        if (!all) {
            A.playerRules = false;
            MH_RemoveHook(A.mChangeAITension);
            for (auto& s : g_maskSpecs)
                if (s.orig) MH_RemoveHook(dl::Method(s.cls, s.method));
        }
        Log("Player's rules for joined nations: %s", A.playerRules ? "ok" : "UNAVAILABLE (hooks)");
    } else {
        Log("Player's rules for joined nations: UNAVAILABLE (routines or fields)");
    }
    // Relations v2, B3: the player's relation routines in a joined nation's view (on its player's PC).
    if (A.playerRules) {
        bool pt = A.events && A.fVP >= 0 && A.fBudget >= 0 && A.mPreviousWars && A.mShowTurnMessages &&
                  A.fTurnMessages >= 0 && A.mSLClear && A.mSLAdd && A.fSLList >= 0 && A.fSLCount >= 0 &&
                  A.mWarsWithNation && A.mRandPossArea && A.mRandPoss && A.mSurpriseAttack && A.mSurpriseInvasion;
        struct ViewSkip {
            void* target;
            void** orig;
            const char* name;
            void* detour;               // 0.6.0: the view's own detour (else a skip thunk)
            const volatile bool* flag;  // ... or the skip thunk's flag (nullptr: g_pvSkip)
        } skips[] = {{A.mRandPossArea, &o_RandPossArea, "GetRandomPossessionInArea", (void*)&d_RandPossArea, nullptr},
                     {A.mRandPoss, &o_RandPoss, "GetRandomPossession", (void*)&d_RandPoss, nullptr},
                     {A.mSurpriseAttack, &o_SurpriseAttack, "DoSurpriseAttack", nullptr, &g_pvSkipSA},
                     {A.mSurpriseInvasion, &o_SurpriseInvasion, "CheckForPlayerSurpriseInvasion", nullptr, nullptr}};
        pt = pt && Hook(A.mWarsWithNation, (void*)&d_WarsWithNation, &o_WarsWithNation, "CheckWarsWithNation");
        for (auto& v : skips) {
            void* thunk = !pt ? nullptr : v.detour ? v.detour : EmitViewSkipThunk(v.orig, v.flag);
            pt = pt && thunk && Hook(v.target, thunk, v.orig, v.name);
        }
        if (!pt) {
            if (o_WarsWithNation) MH_RemoveHook(A.mWarsWithNation);
            for (auto& v : skips)
                if (*v.orig) MH_RemoveHook(v.target);
        }
        A.playerTurn = pt;
        Log("Player's relation routines for a joined nation: %s", pt ? "ok" : "UNAVAILABLE");
        // 0.6.0 (B6 piece 2): surprise attacks and takeovers at a war's start in the view: CheckForWar's call sites of
        // the landing pick (1), the takeover's picks (3) and its owner change (1); CheckSurpriseAttack recorded there.
        if (pt && A.mCheckSurprise && A.fSurpriseTrait >= 0 && A.mChangeOwner && A.fPossValue >= 0) {
            auto inArea = CallSitesTo(A.mCheckForWar, A.mRandPossArea, 0x1000);
            auto take = CallSitesTo(A.mCheckForWar, A.mRandPoss, 0x1000);
            auto owner = CallSitesTo(A.mCheckForWar, A.mChangeOwner, 0x1000);
            bool sites = inArea.size() == 1 && take.size() == 3 && owner.size() == 1;
            if (sites) {
                g_retInArea = inArea[0];
                for (int i = 0; i < 3; i++) g_retTake[i] = take[i];
                g_retTakeOwner = owner[0];
            }
            A.surprise = sites && Hook(A.mCheckSurprise, (void*)&d_CheckSurprise, &o_CheckSurprise, "CheckSurpriseAttack");
            Log("Surprise attacks and takeovers at a war's start for a joined nation: %s (CheckForWar's call sites %d/%d/%d)",
                A.surprise ? "ok" : "UNAVAILABLE", (int)inArea.size(), (int)take.size(), (int)owner.size());
        } else {
            Log("Surprise attacks and takeovers at a war's start for a joined nation: UNAVAILABLE (routines or fields)");
        }
    }
    // B6 (0.6.0): the player's battle generator for joined nations (the host runs CheckForBattles2 in K's view).
    if (A.playerTurn && A.battles && A.mCheckForBattles && A.mInvasionBattles && A.mRaiderVsPatrols && o_PlayerDecline) {
        void* invThunk = EmitViewSkipThunk(&o_InvasionBattles, &g_b6View);
        void* raidThunk = EmitViewSkipThunk(&o_RaiderVsPatrols, &g_b6View);
        bool gen = invThunk && raidThunk && Hook(A.mCheckAttack, (void*)&d_CheckAttack, &o_CheckAttack, "CheckAttackLocation");
        gen = gen && Hook(A.mInvasionBattles, invThunk, &o_InvasionBattles, "CheckForInvasionBattles") &&
              Hook(A.mRaiderVsPatrols, raidThunk, &o_RaiderVsPatrols, "CheckRaiderVsPatrols") &&
              Hook(A.mCheckForBattles, (void*)&d_CheckForBattles, &o_CheckForBattles, "CheckForBattles2");
        if (!gen) {
            for (void* t : {A.mCheckAttack, A.mInvasionBattles, A.mRaiderVsPatrols, A.mCheckForBattles}) MH_RemoveHook(t);
            o_CheckAttack = o_InvasionBattles = o_RaiderVsPatrols = o_CheckForBattles = nullptr;
        }
        A.battleGen = gen;
        if (gen) {  // decision 3: the generator's error box in the view goes to the log (else it shows, as before)
            void* box = FindGeneratorErrorBox((uint8_t*)A.mCheckForBattles);
            bool hooked = box && Hook(box, (void*)&d_ShowMessage, &o_ShowMessage, "ShowMessage");
            Log("Player's battle generator: its error box in a joined nation's view %s",
                hooked ? "goes to the log" : "NOT CAPTURED (the pattern was not found)");
        }
    }
    Log("Player's battle generator for joined nations: %s", A.battleGen ? "ok" : "UNAVAILABLE");
    // Relations v2, B4: the records of a joined nation's peace in its view (elsewhere these routines run as they are).
    if (A.playerTurn && A.peace && o_DoPeace) {
        struct PeaceHook {
            void* target;
            void* detour;
            void** orig;
            const char* name;
        } hooks[] = {{A.mNegotiatePeace, (void*)&d_NegotiatePeace, &o_NegotiatePeace, "NegotiatePeace"},
                     {A.mChangeOwner, (void*)&d_ChangeOwner, &o_ChangeOwner, "ChangePossessionOwner"},
                     {A.mTransferShip, (void*)&d_TransferShip, &o_TransferShip, "TransferShipAsReparation"},
                     {A.mShowTreaty, (void*)&d_ShowPeaceTreaty, &o_ShowPeaceTreaty, "ShowPeaceTreaty"}};
        bool all = true;
        for (auto& h : hooks) all = all && Hook(h.target, h.detour, h.orig, h.name);
        if (!all)
            for (auto& h : hooks)
                if (*h.orig) {
                    MH_RemoveHook(h.target);
                    *h.orig = nullptr;
                }
        A.peace = all;
    } else {
        A.peace = false;
    }
    Log("Peace with the computer's nations for a joined nation: %s", PeaceAvailable() ? "ok" : "UNAVAILABLE");
    // Relations v2, B5 (d): the allies' war help for K (its lend-lease ships are recorded by the peace part's hook)
    A.mWarHelp = dl::Method("TfrmBuildCamp", "WarHelpFromAllies");
    A.fLendLease = dl::Field("TfrmBuildCamp", "LendLeaseDone");
    A.fCoBel = dl::Field("TBuilderNation", "CoBelligerentDelay");
    A.allyHelp = PeaceAvailable() && A.mWarHelp && A.fLendLease >= 0 && A.fCoBel >= 0 && A.fFunds >= 0 && A.fVP >= 0;
    Log("War help from allies for a joined nation: %s", A.allyHelp ? "ok" : "UNAVAILABLE");
    // Relations v2, B5 (e): the ship seizures of a war for K (recorded by the same hook)
    A.mBuildWar = dl::Method("TfrmBuildCamp", "CheckBuildingShipsForWar");
    A.mIsBuilding = dl::Method("TCampaignShip", "IsBuilding");
    A.fBuildNat = dl::Field("TCampaignShip", "BuildingNationIdx");
    A.fRebuild = dl::Field("TCampaignShip", "Rebuild");
    A.buildWar = PeaceAvailable() && A.mBuildWar && A.mIsBuilding && A.fBuildNat >= 0 && A.fRebuild >= 0 &&
                 A.fShipList >= 0;
    Log("Building for war (ship seizures) for a joined nation: %s", A.buildWar ? "ok" : "UNAVAILABLE");
    // Relations v2, B5: the enemy-revolution rule for K (its peace and ships are recorded by the peace part's hooks)
    A.mRevolution = MaskOrig("CheckForRevolutionInEnemyNation");
    A.fRevoPending = dl::Field("TfrmBuildCamp", "RevolutionPending");
    A.revolution = PeaceAvailable() && A.mRevolution && A.fRevoPending >= 0 && A.fUnrest >= 0;
    Log("Enemy revolutions for a joined nation: %s", A.revolution ? "ok" : "UNAVAILABLE");
    // 0.6.0: the disarmament treaty with the joined nations' wars (CancelTreaty is the host's; the conference sees their
    // wars through B2's mask, MaskedCall)
    A.mCancelTreaty = dl::Method("TfrmBuildCamp", "CancelTreaty");
    A.mCreateArms = dl::Method("TfrmBuildCamp", "CreateArmsControlTreaty");
    A.fTreatyTime = dl::Field("TfrmBuildCamp", "TreatyTimeRemaining");
    A.fTreatyDisp = dl::Field("TfrmBuildCamp", "TreatyDisplacementLimit");
    A.fTreatyCal = dl::Field("TfrmBuildCamp", "TreatyCalibreLimit");
    A.fSubBan = dl::Field("TfrmBuildCamp", "SubBanTime");
    A.fTreatyBase = dl::Field("TfrmBuildCamp", "TreatyTonnageBasePercentage");
    A.armsTreaty = A.mCancelTreaty && A.mCreateArms && A.fTreatyTime >= 0 && A.fTreatyDisp >= 0 && A.fTreatyCal >= 0 &&
                   A.fSubBan >= 0 && A.fTreatyBase >= 0 &&
                   Hook(A.mCancelTreaty, (void*)&d_CancelTreaty, &o_CancelTreaty, "CancelTreaty");
    Log("Disarmament treaty with joined nations' wars: %s (cancel=%p conference=%p masked=%d)",
        A.armsTreaty ? "ok" : "UNAVAILABLE", A.mCancelTreaty, A.mCreateArms, A.playerRules ? 1 : 0);
    // 0.6.0: the last battle's area and possession never outlive a load (CB_BeforeLoad)
    A.fBattleArea = dl::Field("TfrmBuildCamp", "BattleArea");
    A.fBattlePoss = dl::Field("TfrmBuildCamp", "BattlePossession");
    A.battlePtrs = A.fBattleArea >= 0 && A.fBattlePoss >= 0 && A.fMapAreas >= 0 && A.fAreaPossessions >= 0 &&
                   A.fAreaName >= 0 && A.fPossName >= 0;
    Log("The last battle's area across a load: %s%s", A.battlePtrs ? "ok" : "UNAVAILABLE",
        KeepBattlePointersTest() ? " (TEST: kept, as the game does)" : "");
    nplay::InstallHooks();
    if (lockstep::TestPrefsWanted()) {  // TEST ONLY
        Hook(dl::Method("TdlgPrefs", "LoadPrefs"), (void*)&d_LoadPrefs, &o_LoadPrefs, "TdlgPrefs.LoadPrefs (TEST)");
        Hook(dl::Method("TdlgPrefs", "SavePrefs"), (void*)&d_SavePrefs, &o_SavePrefs, "TdlgPrefs.SavePrefs (TEST)");
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        Log("MH_EnableHook failed");
        return false;
    }
    return ok;
}

}  // namespace game

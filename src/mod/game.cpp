#include "game.h"
#include "../../third_party/minhook/include/MinHook.h"
#include <algorithm>
#include <climits>
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
    void *mSLClear = nullptr, *mSLAdd = nullptr;
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
    void** frmMainVar = nullptr;  // the battle form (TfrmMain) unit variable
    bool battles = false;
    int* scaleVar = nullptr;  // the game's scale value used in its war-progress thresholds (100*G + 300/500)
    bool treaties = false;
} A;

static void* g_buildCamp = nullptr;
static void* g_startForm = nullptr;
static bool g_inTurn = false;
static bool g_noDefer = false;         // ResolveAIBattle: the game's own resolution runs
static bool g_battleSlotMode = false;  // this PC fights a joined player's battle in its battle slot
static bool g_battleAftermath = false; // HandleBattleAftermath ran in battle slot mode
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
    int n;
    void* nation[16];
    int vp[16];
    int enemyVP;
} g_blockade[kBlockadeSlots];

extern "C" int __stdcall CB_BeforeBlockade(void* self, uintptr_t ret) {
    int d = g_blockadeDepth++;
    if (d < 0 || d >= kBlockadeSlots) return d;
    BlockadeCtx& c = g_blockade[d];
    c.load = false;
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

extern "C" void __stdcall CB_AfterBlockade(int slot, void* self) {
    if (slot < 0) return;
    g_blockadeDepth = slot;  // also drops frames left behind by an exception
    if (slot >= kBlockadeSlots || !g_blockade[slot].load) return;
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

bool EventActive() { return g_ev.swapped; }

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
    if (!EventsAvailable() || !f || !list || K < 1 || K > 8 || PlayerNation() != Nation(K) || g_ev.swapped) {
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
    *A.warVar = kAtWar ? (g_ev.war > 0 ? g_ev.war : 1) : (g_ev.war > 0 ? -12 : g_ev.war);
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

int StartBuildPhase() {
    void* f = BuildCamp();
    return f ? dl::At<uint8_t>(f, A.fStartBuild) : -1;
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
    uint32_t beforeBattle = 0;
    dl::Call(A.mSave, (uint32_t)(uintptr_t)f, 0xFFFFFFFFu, 0, 1, &beforeBattle);
    return true;
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
        if (playerIdx > 0) SetPlayerNation(playerIdx);
    }
    Log("LoadCampaign slot %d player %d -> %s", slot, playerIdx, ok ? "ok" : "FAILED");
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

extern "C" BOOL __stdcall CB_BeforeAIPeace(void* self, void* nation, uint32_t badLoss) {
    BOOL skip = FALSE;
    try {
        AIPeaceKeepVP();
        skip = hookcb::OnBeforeAIPeace(NationIndex(nation), (badLoss & 0xFF) != 0) ? TRUE : FALSE;
        if (skip) AIPeaceRestoreVP();  // the game's routine doesn't run
    } catch (...) {
    }
    return skip;
}
extern "C" void __stdcall CB_AfterAIPeace() {
    try {
        hookcb::OnAfterAIPeace();
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

// ReduceTimeLimits(Self=EAX): run the game's routine, then the callback with Self.
extern "C" __declspec(naked) void d_ReduceTimeLimits() {
    __asm {
        push eax
        call dword ptr [o_ReduceTimeLimits]
        call CB_AfterReduceTimeLimits
        ret
    }
}

extern "C" void __stdcall CB_BeforeLoad(void* self) {
    try {
        ClearIntelReports(self);
        RememberAirUnits();
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

bool ResolveAIBattle(int a, int b, int countA, int countB, const std::wstring& areaName) {
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
    g_noDefer = true;
    uint32_t r = GuardedCall(A.mAIBattle, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)na, (uint32_t)(uintptr_t)nb, 3,
                             stack, &ok);
    g_noDefer = false;
    if (!ok) Log("ResolveAIBattle %d-%d: exception in the game's routine", a, b);
    return ok && (r & 0xFF) != 0;
}

bool StartPlayerBattle(int attacker, int defender, const std::wstring& areaName) {
    void* f = BuildCamp();
    void* na = Nation(attacker);
    void* nd = Nation(defender);
    void* area = AreaByName(areaName);
    if (!f || !na || !nd || !area || !A.mCheckAttack || EventActive()) {
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

extern "C" void __stdcall CB_AfterDoBattle(int opp, int result) {
    try {
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

bool InstallHooks() {
    if (MH_Initialize() != MH_OK) {
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
    if (A.doctrine) Hook(A.mReduceTimeLimits, (void*)&d_ReduceTimeLimits, &o_ReduceTimeLimits, "ReduceTimeLimits");
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
    if (!A.battles) Log("Joined players' battles: UNAVAILABLE (hooks)");
    for (int i = 0; i < (int)(sizeof(g_skips) / sizeof(g_skips[0])); i++) {
        void* target = dl::Method(g_skips[i].cls, g_skips[i].method);
        void* thunk = target ? EmitSkipThunk(i) : nullptr;
        if (!target || !thunk) {
            Log("skip hook %s unavailable", g_skips[i].method);
            continue;
        }
        Hook(target, thunk, &g_skips[i].orig, g_skips[i].method);
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        Log("MH_EnableHook failed");
        return false;
    }
    return ok;
}

}  // namespace game

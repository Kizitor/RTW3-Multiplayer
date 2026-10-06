#include "game.h"
#include "../../third_party/minhook/include/MinHook.h"

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
} A;

static void* g_buildCamp = nullptr;
static void* g_startForm = nullptr;
static bool g_inTurn = false;

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

void MakePeace(int a, int b) {
    void* f = BuildCamp();
    if (!f || !A.diplomacy || !ValidPair(a, b) || !AtWar(a, b)) return;
    const int kPeaceTension = 5;
    if (a == 0 || b == 0) {
        int other = a == 0 ? b : a;
        if (CountAtWarWithHost() == 1 && A.mDoPeace && A.recYearCell && A.recMonthCell) {
            // The host's last war ends: run the game's own end-of-war routine, recorded as a compromise peace.
            int* rec = (int*)((uint8_t*)f + **A.recYearCell * 0x4B0 + **A.recMonthCell * 100 + A.recDisp);
            *rec = 3;
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
    if (!f || !n) return;
    dl::At<void*>(f, A.fPlayerNation) = n;
    Log("PlayerNation -> %d (%s)", idx, W2U(NationName(idx)).c_str());
    RefreshUI();
}

bool LoadCampaign(int slot, int playerIdx) {
    void* f = BuildCamp();
    if (!f) {
        Log("LoadCampaign: no frmBuildCamp");
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

bool BattleActive() {
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
extern "C" BOOL __stdcall CB_BeforeAIPeace(void* self, void* nation, uint32_t badLoss) {
    try {
        return hookcb::OnBeforeAIPeace(NationIndex(nation), (badLoss & 0xFF) != 0) ? TRUE : FALSE;
    } catch (...) {
        return FALSE;
    }
}
extern "C" void __stdcall CB_AfterAIPeace() {
    try {
        hookcb::OnAfterAIPeace();
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

// AdvanceDesignStudies(Self=EAX): run the game's routine, then the callback with Self.
extern "C" __declspec(naked) void d_DesignStudies() {
    __asm {
        push eax
        call dword ptr [o_DesignStudies]
        call CB_AfterDesignStudies
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
    if (A.mAdvanceDesignStudies && A.fDesignList >= 0 && A.fReadyForBuild >= 0)
        Hook(A.mAdvanceDesignStudies, (void*)&d_DesignStudies, &o_DesignStudies, "AdvanceDesignStudies");
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

#include "officers.h"
#include "game.h"
#include "lockstep.h"
#include "nationplay.h"
#include "../../third_party/minhook/include/MinHook.h"
#include <algorithm>
#include <climits>

// See officers.h. Game facts (1.01.44; docs/GAME-INTERNALS.md "Officers"):
// - TBuilderNation.OfficerList (+0x115): the nation's officers; only PlayerNation's is filled by the game.
// - LoadBuildCampaign reads the .off into a TStringList, calls the unnamed loader (0xe100f4: EAX = the list, EDX = the
//   TStringList, ECX = 'Officers'; it appends) for PlayerNation (= BuilderNations[0] at that point) and then
//   PlayerNation.ConnectOfficersToShips: Self's ships' Commander from Self.GetOfficerWithId(CommanderId), and only when
//   Self is PlayerNation the map areas' AreaCommander. SaveBuildCampaign writes [Officers] with the unnamed writer
//   (0xe0fc78, the same arguments) right before SaveCampaignDivisions.
// - The Turn (DoTurn2): PlayerChecksBeforeTurn's question about uncommanded ships and divisions (0xd33ed0, Self =
//   frmBuildCamp, AL: False = the player goes back), then it clears the free-promotion byte (a cell holding its
//   address) and runs AssignofficerstoUncommandedAreas, ...Divisions and PlayerNation.AssignOfficersToUncommandedShips.
// - The new campaign (0xbc98c2): AddOfficerToPool(0) FleetSize+3 times, (2) FleetSize+1 times, (3) FleetSize +
//   GlobalNavalPower + 1 times, AssignofficerstoUncommandedAreas, PlayerNation.AssignOfficersToUncommandedShips.
// - AdjustCrewQuality(frm, ship, nation) shows picture messages for any nation's commanded ship.

namespace ofc {

static struct Api {
    int fOfficers = -1, fPlayer = -1, fMapAreas = -1, fDivisions = -1, fGNP = -1;
    int fOName = -1, fOId = -1, fORank = -1, fOStatus = -1;
    int fCmd = -1, fCmdId = -1, fACmd = -1, fACmdId = -1, fDCmd = -1;
    int fSLList = -1, fSLCount = -1;
    void *mConnect = nullptr, *mLoader = nullptr, *mWriter = nullptr, *mSLCreate = nullptr, *mSLAdd = nullptr,
         *mFree = nullptr, *mAddPool = nullptr, *mAssignAreas = nullptr, *mAssignDivs = nullptr,
         *mAssignShips = nullptr, *mQuestion = nullptr, *mKill = nullptr, *mAdjustCrew = nullptr,
         *mIsCommanded = nullptr;
    uint8_t** freePromo = nullptr;  // the cell holding the address of the free-promotion byte
    bool lists = false, month = false;
} A;

static bool InImage(uintptr_t a) { return a >= dl::g_base && a < dl::g_base + dl::g_size; }
static void* CallTarget(uint8_t* e8) { return e8 + 5 + *(int32_t*)(e8 + 1); }

// The E8 call right before the first call of `next` inside `fn` (within maxLen bytes).
static void* CallBefore(void* fn, void* next, size_t maxLen) {
    if (!fn || !next) return nullptr;
    std::vector<uint8_t*> calls = dl::CallTargets((uint8_t*)fn, maxLen);
    for (size_t i = 1; i < calls.size(); i++)
        if (calls[i] == next) return InImage((uintptr_t)calls[i - 1]) ? calls[i - 1] : nullptr;
    return nullptr;
}

// Does `fn` load the UTF-16 literal `text` (`mov edx, <literal>`) within its first maxLen bytes?
static bool LoadsLiteral(uint8_t* fn, size_t maxLen, const wchar_t* text) {
    size_t n = wcslen(text) * 2;
    for (size_t i = 0; i + 5 <= maxLen; i++) {
        if (fn[i] != 0xBA) continue;
        uintptr_t a = *(uint32_t*)(fn + i + 1);
        if (InImage(a) && InImage(a + n) && memcmp((void*)a, text, n) == 0) return true;
    }
    return false;
}

bool Resolve() {
    A.fOfficers = dl::Field("TBuilderNation", "OfficerList");
    A.fGNP = dl::Field("TBuilderNation", "GlobalNavalPower");
    A.fPlayer = dl::Field("TfrmBuildCamp", "PlayerNation");
    A.fMapAreas = dl::Field("TfrmBuildCamp", "MapAreas");
    A.fDivisions = dl::Field("TfrmBuildCamp", "CampaignDivisions");
    A.fOName = dl::Field("TOfficer", "Name");
    A.fOId = dl::Field("TOfficer", "Id");
    A.fORank = dl::Field("TOfficer", "Rank");
    A.fOStatus = dl::Field("TOfficer", "Status");
    A.fCmd = dl::Field("TCampaignShip", "Commander");
    A.fCmdId = dl::Field("TCampaignShip", "CommanderId");
    A.fACmd = dl::Field("TMapArea", "AreaCommander");
    A.fACmdId = dl::Field("TMapArea", "CommanderId");
    A.fDCmd = dl::Field("TCampaignDivision", "Commander");
    A.fSLList = dl::Field("TStringList", "FList");
    A.fSLCount = dl::Field("TStringList", "FCount");
    A.mConnect = dl::Method("TBuilderNation", "ConnectOfficersToShips");
    A.mSLCreate = dl::MethodFewestParams("TStringList", "Create");
    A.mSLAdd = dl::Method("TStringList", "Add");
    A.mFree = dl::Method("TObject", "Free");
    A.mAddPool = dl::Method("TfrmBuildCamp", "AddOfficerToPool");
    A.mAssignAreas = dl::Method("TfrmBuildCamp", "AssignofficerstoUncommandedAreas");
    A.mAssignDivs = dl::Method("TfrmBuildCamp", "AssignofficerstoUncommandedDivisions");
    A.mAssignShips = dl::Method("TBuilderNation", "AssignOfficersToUncommandedShips");
    A.mKill = dl::Method("TfrmBuildCamp", "KillCommander");
    A.mAdjustCrew = dl::Method("TfrmBuildCamp", "AdjustCrewQuality");
    A.mIsCommanded = dl::Method("TCampaignShip", "IsCommandedShip");
    void* load = dl::Method("TfrmBuildCamp", "LoadBuildCampaign");
    void* save = dl::Method("TfrmBuildCamp", "SaveBuildCampaign");
    // The loader: LoadBuildCampaign's call right before PlayerNation.ConnectOfficersToShips; the writer:
    // SaveBuildCampaign's call right before SaveCampaignDivisions.
    A.mLoader = CallBefore(load, A.mConnect, 0x2400);
    A.mWriter = CallBefore(save, dl::Method("TfrmBuildCamp", "SaveCampaignDivisions"), 0x1400);
    // The Turn's question: the call in PlayerChecksBeforeTurn whose routine loads the literal 'Some ships or divisions
    // have no commander assigned.' (`mov edx, <literal>`).
    if (void* pc = dl::Method("TfrmBuildCamp", "PlayerChecksBeforeTurn"))
        for (uint8_t* t : dl::CallTargets((uint8_t*)pc, 0x200))
            if (InImage((uintptr_t)t) && InImage((uintptr_t)t + 0x200) &&
                LoadsLiteral(t, 0x180, L"Some ships or divisions have no commander assigned.")) {
                A.mQuestion = t;
                break;
            }
    // The free promotion's byte: DoTurn2 `mov eax,[cell]; mov byte ptr [eax],0; mov eax,[ebp-4]; call
    // AssignofficerstoUncommandedAreas`.
    if (void* dt = dl::Method("TfrmBuildCamp", "DoTurn2")) {
        uint8_t* p = (uint8_t*)dt;
        for (int tries = 0; tries < 8 && A.mAssignAreas; tries++) {
            uint8_t* hit = dl::FindPattern(p, (uint8_t*)dt + 0x300 - p, {0xA1, -1, -1, -1, -1, 0xC6, 0x00, 0x00, 0x8B, 0x45,
                                                                       0xFC, 0xE8});
            if (!hit) break;
            if (CallTarget(hit + 11) == A.mAssignAreas && InImage(*(uint32_t*)(hit + 1))) {
                A.freePromo = (uint8_t**)(uintptr_t) * (uint32_t*)(hit + 1);
                break;
            }
            p = hit + 1;
        }
    }
    A.lists = A.fOfficers >= 0 && A.fPlayer >= 0 && A.fMapAreas >= 0 && A.fOId >= 0 && A.fOStatus >= 0 &&
              A.fCmd >= 0 && A.fCmdId >= 0 && A.fACmd >= 0 && A.fACmdId >= 0 && A.fSLList >= 0 && A.fSLCount >= 0 &&
              A.mConnect && A.mLoader && A.mWriter && A.mSLCreate && A.mSLAdd && A.mFree;
    A.month = A.lists && A.fGNP >= 0 && A.mAddPool && A.mAssignAreas && A.mAssignDivs && A.mAssignShips;
    Log("Officers of the joined players: %s; their month (the pool, the auto-assignment): %s; the Turn's question: %s; "
        "the free promotion: %s (loader rva %#x, writer rva %#x)",
        A.lists ? "ok" : "UNAVAILABLE", A.month ? "ok" : "UNAVAILABLE",
        A.mQuestion ? "ok" : "not found (not asked)", A.freePromo ? "ok" : "not found (not reset)",
        A.mLoader ? (unsigned)((uintptr_t)A.mLoader - dl::g_base) : 0u,
        A.mWriter ? (unsigned)((uintptr_t)A.mWriter - dl::g_base) : 0u);
    return A.lists;
}

bool Available() { return A.lists; }
bool MonthAvailable() { return A.month; }

// ---------------------------------------------------------------------------------------------
// TStringList helpers

static void* NewStringList() {
    return (void*)(uintptr_t)dl::Call(A.mSLCreate, (uint32_t)(uintptr_t)dl::ClassVmt("TStringList"), 1);
}
static void SLAdd(void* sl, const std::wstring& s) {
    dl::ConstUStr u(s);
    dl::Call(A.mSLAdd, (uint32_t)(uintptr_t)sl, u.ptr());
}
static std::vector<std::wstring> SLRead(void* sl) {
    std::vector<std::wstring> out;
    int n = sl ? dl::At<int>(sl, A.fSLCount) : 0;
    void** items = sl ? dl::At<void**>(sl, A.fSLList) : nullptr;
    for (int i = 0; items && i < n; i++) out.push_back(dl::ReadUStr(items[i * 2]));
    return out;
}
static void Free(void* o) {
    if (o) dl::Call(A.mFree, (uint32_t)(uintptr_t)o);
}

// The game's routines raise Delphi exceptions on errors: false then (the caller's state is put back first).
static bool Guarded3(void* fn, uint32_t eax, uint32_t edx, uint32_t ecx) {
    __try {
        dl::Call(fn, eax, edx, ecx);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static uint32_t U(void* p) { return (uint32_t)(uintptr_t)p; }

// ---------------------------------------------------------------------------------------------
// ConnectOfficersToShips: the joined player's month trades the lists of nation 0 and K

extern "C" void* o_ofConnect = nullptr;
static int g_tradeK = 0;  // LoadScope: the load of a joined player's own month (offs)
static bool g_tradeDone = false;
static int g_lastTraded = 0, g_lastTradeOfficers = -1, g_lastTradeShips = -1, g_lastTradeAreas = -1;
static std::set<int> g_loaded;  // nations whose corps LoadIntoNation put in memory since the last load
static int g_clientK = 0;
static bool g_clientOffs = false;

static void* ConnectFn() { return o_ofConnect ? o_ofConnect : A.mConnect; }

static int CommandedShips(void* nation) {
    int n = 0;
    for (int j = 0, c = game::ShipCount(nation); j < c; j++)
        if (void* sh = game::Ship(nation, j)) n += dl::At<void*>(sh, A.fCmd) != nullptr;
    return n;
}

static int CommandedAreas() {
    void* f = game::BuildCamp();
    void* areas = f ? dl::At<void*>(f, A.fMapAreas) : nullptr;
    int n = 0;
    for (int j = 0, c = dl::ListCount(areas); j < c; j++)
        if (void* a = dl::ListItem(areas, j)) n += dl::At<void*>(a, A.fACmd) != nullptr;
    return n;
}

// Both calls with PlayerNation = K (the areas against K's list; nation 0's ships against its now empty list);
// PlayerNation put back also when the game raises.
static bool ConnectTraded(void** player, void* nk, void* n0, void* prev) {
    bool ok = true;
    __try {
        *player = nk;
        dl::Call(ConnectFn(), U(nk));
        dl::Call(ConnectFn(), U(n0));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    *player = prev;
    return ok;
}

static bool TradeAndConnect(void* nation, int K) {
    void* f = game::BuildCamp();
    void* n0 = game::Nation(0);
    void* nk = game::Nation(K);
    if (!f || !n0 || !nk || nation != n0 || dl::At<void*>(f, A.fPlayer) != n0 || !dl::At<void*>(nk, A.fOfficers)) {
        Log("officers: nation %d's month NOT traded (unexpected state); the game's own connection", K);
        return false;
    }
    std::swap(dl::At<void*>(n0, A.fOfficers), dl::At<void*>(nk, A.fOfficers));
    void** player = &dl::At<void*>(f, A.fPlayer);
    bool ok = ConnectTraded(player, nk, n0, n0);
    g_lastTraded = K;
    g_lastTradeOfficers = dl::ListCount(dl::At<void*>(nk, A.fOfficers));
    g_lastTradeShips = CommandedShips(nk);
    g_lastTradeAreas = CommandedAreas();
    Log("officers: nation %d's own %d officers in its list (the month's load); %d of its ships and %d areas "
        "commanded%s",
        K, g_lastTradeOfficers, g_lastTradeShips, g_lastTradeAreas, ok ? "" : " (the game raised an exception)");
    return true;
}

extern "C" void __stdcall OF_Connect(void* nation) {
    if (g_tradeK > 0 && !g_tradeDone) {
        g_tradeDone = true;
        bool done = false;
        try {
            done = TradeAndConnect(nation, g_tradeK);
        } catch (...) {
            Log("officers: exception trading the officer lists");
        }
        if (done) return;
    }
    dl::Call(ConnectFn(), U(nation));
}

// ConnectOfficersToShips(Self=EAX): a register procedure without stack arguments.
extern "C" __declspec(naked) void OF_dConnect() {
    __asm {
        push eax
        call OF_Connect
        ret
    }
}

void OnLoadStart() {
    g_loaded.clear();
    g_tradeDone = false;
}

LoadScope::LoadScope(int slot, int playerIdx) {
    (void)slot;
    g_tradeK = A.lists && g_clientOffs && playerIdx >= 1 && playerIdx <= 8 && playerIdx == g_clientK ? playerIdx : 0;
    g_tradeDone = false;
}
LoadScope::~LoadScope() { g_tradeK = 0; }

// ---------------------------------------------------------------------------------------------
// Host: a joined nation's corps in its own list

static bool LoadGuarded(void* list, void* sl, uint32_t sec) {
    __try {
        dl::Call(A.mLoader, U(list), U(sl), sec);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int LoadIntoNation(int K, const std::vector<std::string>& lines, int* commanded) {
    if (commanded) *commanded = 0;
    if (!A.lists || K < 1 || K > 8 || game::EventActive()) return -1;
    void* nat = game::Nation(K);
    void* list = nat ? dl::At<void*>(nat, A.fOfficers) : nullptr;
    if (!list || nat == game::PlayerNation()) return -1;
    if (g_loaded.count(K) || dl::ListCount(list) > 0) {
        Log("officers: nation %d's list holds %d already; not loaded again", K, dl::ListCount(list));
        return -1;
    }
    void* sl = NewStringList();
    if (!sl) return -1;
    SLAdd(sl, L"[Officers]");
    for (auto& l : lines) SLAdd(sl, U2W(l));
    dl::ConstUStr sec(L"Officers");
    bool ok = LoadGuarded(list, sl, sec.ptr());
    Free(sl);
    if (!ok) {
        Log("officers: loading nation %d's corps raised an exception (%d in its list)", K, dl::ListCount(list));
        return -1;
    }
    g_loaded.insert(K);
    if (!Guarded3(ConnectFn(), U(nat), 0, 0)) Log("officers: connecting nation %d's ships raised an exception", K);
    if (commanded) *commanded = CommandedShips(nat);
    return dl::ListCount(list);
}

bool LoadedThisLoad(int K) { return g_loaded.count(K) > 0; }

static bool WriteGuarded(void* list, void* sl, uint32_t sec) {
    __try {
        dl::Call(A.mWriter, U(list), U(sl), sec);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadNation(int K, std::vector<std::string>& lines) {
    lines.clear();
    if (!A.lists || K < 0 || K > 8 || game::EventActive()) return false;
    void* nat = game::Nation(K);
    void* list = nat ? dl::At<void*>(nat, A.fOfficers) : nullptr;
    if (!list) return false;
    void* sl = NewStringList();
    if (!sl) return false;
    dl::ConstUStr sec(L"Officers");
    bool ok = WriteGuarded(list, sl, sec.ptr());
    if (ok)
        for (auto& w : SLRead(sl)) {
            if (w.empty() || w[0] == L'[') continue;  // the section's header
            lines.push_back(W2U(w));
        }
    Free(sl);
    return ok;
}

// ---------------------------------------------------------------------------------------------
// Host guards: AdjustCrewQuality's picture messages about a joined nation's ships

static std::function<bool(int)> g_isJoined;
static std::function<void(int, const std::string&)> g_news;
extern "C" void* o_ofAdjust = nullptr;
static std::vector<std::wstring> g_adjTexts;

void SetHostGuards(std::function<bool(int)> isJoined, std::function<void(int, const std::string&)> news) {
    g_isJoined = isJoined;
    g_news = news;
}

static void EndAdjustCapture() { g_adjTexts = lockstep::EndTextCapture(); }

static void AdjustGuarded(void* self, void* ship, void* nation, bool capture) {
    __try {
        dl::Call(o_ofAdjust, U(self), U(ship), U(nation));
    } __finally {
        if (capture) EndAdjustCapture();
    }
}

extern "C" void __stdcall OF_Adjust(void* self, void* ship, void* nation) {
    int k = -1;
    bool capture = false;
    try {
        k = g_isJoined ? game::NationIndex(nation) : -1;
        capture = k >= 1 && k <= 8 && g_isJoined(k) && lockstep::BeginTextCapture();
    } catch (...) {
    }
    g_adjTexts.clear();
    AdjustGuarded(self, ship, nation, capture);
    if (!capture) return;
    try {
        for (auto& t : g_adjTexts) {
            std::string u = W2U(t);
            for (char& c : u)
                if (c == '\r' || c == '\n') c = ' ';
            Log("officers: the crew training message about nation %d's ship kept from the host's screen: %s", k,
                u.substr(0, 200).c_str());
            if (g_news) g_news(k, "Officers: " + u);
        }
    } catch (...) {
    }
}

// AdjustCrewQuality(Self=EAX, aShip=EDX, aNation=ECX): a register procedure without stack arguments.
extern "C" __declspec(naked) void OF_dAdjust() {
    __asm {
        push ecx
        push edx
        push eax
        call OF_Adjust
        ret
    }
}

// ---------------------------------------------------------------------------------------------
// Host guards: the ship sheet (TdlgShipStatus.ExecuteShipStatusStrategic) shows "<officer> commanding" for any ship with
// a commander unless called with Enemy; on the host a joined nation's ships have theirs now. A ship that is not
// PlayerNation's shows none (the game's AI ships never have one): its Commander is nil while the sheet is filled and
// shown, put back afterwards and before any save meanwhile (the sheet is modal).

extern "C" void* o_ofSheet = nullptr;
static void* g_sheetShip = nullptr;
static void* g_sheetOfficer = nullptr;

static void SheetRestore() {
    if (!g_sheetShip) return;
    dl::At<void*>(g_sheetShip, A.fCmd) = g_sheetOfficer;
    g_sheetShip = g_sheetOfficer = nullptr;
}

void BeforeSave() { SheetRestore(); }

static bool PlayersShip(void* ship) {
    void* pn = game::PlayerNation();
    for (int j = 0, c = game::ShipCount(pn); j < c; j++)
        if (game::Ship(pn, j) == ship) return true;
    return false;
}

static void SheetGuarded(uint32_t self, uint32_t ship, uint32_t dship, const uint32_t* args) {
    __try {
        dl::Call(o_ofSheet, self, ship, dship, 9, args);
    } __finally {
        SheetRestore();
    }
}

extern "C" void __stdcall OF_Sheet(uint32_t self, uint32_t ship, uint32_t dship, const uint32_t* entry) {
    uint32_t args[9];  // the caller's stack arguments, the first pushed (aName) first
    for (int i = 0; i < 9; i++) args[i] = entry[9 - i];
    void* sh = (void*)(uintptr_t)ship;
    if (sh && !g_sheetShip && A.fCmd >= 0) {
        try {
            void* o = dl::At<void*>(sh, A.fCmd);
            if (o && game::BuildCamp() && !PlayersShip(sh)) {
                g_sheetShip = sh;
                g_sheetOfficer = o;
                dl::At<void*>(sh, A.fCmd) = nullptr;
            }
        } catch (...) {
        }
    }
    SheetGuarded(self, ship, dship, args);
}

// ExecuteShipStatusStrategic(Self=EAX, aCampaignShip=EDX, aDShip=ECX, nine stack arguments): ret 0x24. `push esp` puts
// the entry's stack pointer (the return address's slot) as the fourth argument.
extern "C" __declspec(naked) void OF_dSheet() {
    __asm {
        push esp
        push ecx
        push edx
        push eax
        call OF_Sheet
        ret 0x24
    }
}

static void HookOne(void* target, void* detour, void** orig, const char* name) {
    if (!target) return;
    MH_STATUS st = MH_CreateHook(target, detour, orig);
    if (st != MH_OK)
        Log("hook %s: MH_CreateHook failed %d", name, (int)st);
    else
        Log("hook %s at rva %#x", name, (unsigned)((uintptr_t)target - dl::g_base));
}

void InstallHooks() {
    if (!A.lists) return;
    HookOne(A.mConnect, (void*)&OF_dConnect, &o_ofConnect, "ConnectOfficersToShips");
    HookOne(A.mAdjustCrew, (void*)&OF_dAdjust, &o_ofAdjust, "AdjustCrewQuality");
    // the ship sheet: only when its argument count is the one the detour copies (ret 0x24 at its end)
    void* sheet = dl::Method("TdlgShipStatus", "ExecuteShipStatusStrategic");
    if (sheet && dl::FindPattern((uint8_t*)sheet, 0x1000, {0xC2, 0x24, 0x00}))
        HookOne(sheet, (void*)&OF_dSheet, &o_ofSheet, "ExecuteShipStatusStrategic");
    else
        Log("officers: the ship sheet's guard UNAVAILABLE (another game version?)");
}

// ---------------------------------------------------------------------------------------------
// Joined player's PC

void ClientSetMonth(int K, bool hostOfficers) {
    g_clientK = K;
    g_clientOffs = hostOfficers && A.lists && K >= 1 && K <= 8;
}
void ClientReset() {
    g_clientK = 0;
    g_clientOffs = false;
}
bool ClientActive() {
    return g_clientOffs && game::PlayerNation() && game::PlayerNation() == game::Nation(g_clientK) &&
           !game::EventActive();
}

static bool PlayerIs(int K) {
    void* f = game::BuildCamp();
    return f && K >= 1 && K <= 8 && game::Nation(K) && dl::At<void*>(f, A.fPlayer) == game::Nation(K) &&
           !game::EventActive();
}

static int ListSize(void* nation) { return nation ? dl::ListCount(dl::At<void*>(nation, A.fOfficers)) : 0; }

static bool GenerateGuarded(void* f, void* pn, int n0, int n2, int n3) {
    __try {
        for (int i = 0; i < n0; i++) dl::Call(A.mAddPool, U(f), 0);
        for (int i = 0; i < n2; i++) dl::Call(A.mAddPool, U(f), 2);
        for (int i = 0; i < n3; i++) dl::Call(A.mAddPool, U(f), 3);
        dl::Call(A.mAssignAreas, U(f));
        dl::Call(A.mAssignShips, U(pn));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GenerateCorps(int K, std::string& summary) {
    void* f = game::BuildCamp();
    if (!A.month || !PlayerIs(K)) {
        summary = "unavailable";
        return false;
    }
    void* pn = game::Nation(K);
    if (ListSize(pn) > 0) {
        summary = "the nation has " + std::to_string(ListSize(pn)) + " officers already";
        return false;
    }
    int fs = std::max(0, nplay::FleetSizeValue());
    int gnp = dl::At<uint8_t>(pn, A.fGNP) ? 1 : 0;
    int n0 = fs + 3, n2 = fs + 1, n3 = fs + gnp + 1;
    bool ok = GenerateGuarded(f, pn, n0, n2, n3);
    int officers = 0, active = 0, ships = 0, areas = 0;
    Counts(officers, active, ships, areas);
    summary = std::to_string(officers) + " officers (pool " + std::to_string(n0) + " commanders, " + std::to_string(n2) +
              " rear admirals, " + std::to_string(n3) + " admirals: FleetSize " + std::to_string(fs) +
              ", global naval power " + std::to_string(gnp) + "), " + std::to_string(ships) + " ships and " +
              std::to_string(areas) + " areas commanded" + (ok ? "" : " (the game raised an exception)");
    return ok;
}

void ClearFreePromotion() {
    if (A.freePromo && *A.freePromo) **A.freePromo = 0;
}

static int QuestionGuarded(void* f) {
    __try {
        return (dl::Call(A.mQuestion, U(f)) & 0xFF) != 0 ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool AskCommanders() {
    void* f = game::BuildCamp();
    if (!A.month || !A.mQuestion || !f || !ClientActive()) return true;
    int r = QuestionGuarded(f);
    if (r < 0) Log("officers: the Turn's commander question raised an exception (submitted anyway)");
    return r != 0;
}

static bool AssignGuarded(void* f, void* pn) {
    __try {
        dl::Call(A.mAssignAreas, U(f));
        dl::Call(A.mAssignDivs, U(f));
        dl::Call(A.mAssignShips, U(pn));
        dl::Call(A.mAssignShips, U(pn));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void AutoAssign(std::string& summary) {
    void* f = game::BuildCamp();
    if (!A.month || !f || !ClientActive()) {
        summary = "not run";
        return;
    }
    void* pn = game::PlayerNation();
    int o0, a0, s0, r0, o1, a1, s1, r1;
    Counts(o0, a0, s0, r0);
    bool ok = AssignGuarded(f, pn);
    Counts(o1, a1, s1, r1);
    summary = "officers " + std::to_string(o0) + " -> " + std::to_string(o1) + ", commanded ships " + std::to_string(s0) +
              " -> " + std::to_string(s1) + ", areas " + std::to_string(r0) + " -> " + std::to_string(r1) +
              (ok ? "" : " (the game raised an exception)");
}

void Counts(int& officers, int& active, int& ships, int& areas) {
    officers = active = ships = areas = 0;
    void* pn = game::PlayerNation();
    if (!A.lists || !pn) return;
    void* list = dl::At<void*>(pn, A.fOfficers);
    officers = dl::ListCount(list);
    for (int j = 0; j < officers; j++)
        if (void* o = dl::ListItem(list, j)) active += dl::At<uint8_t>(o, A.fOStatus) == 0;
    ships = CommandedShips(pn);
    areas = CommandedAreas();
}

// ---------------------------------------------------------------------------------------------
// Tests (bridge)

// "ok pn=<PlayerNation's index> client=<K>/<offs> traded=<K>:<officers>/<ships>/<areas> nations=<i>:<officers>/
// <active>/<ships>/<commanded>/<commander ids>/<commanders from its own list>;... areas=<commanded>/<ids>/<owner>x<n>,
// divs=<n>/<commanded> ids=<min>-<max>/<unique>/<total> first=<id>:<name>:<rank>:<status>|..."
std::string Info() {
    void* f = game::BuildCamp();
    if (!A.lists || !f || game::NationCount() < 9) return "error unavailable";
    int n = game::NationCount();
    std::map<void*, int> owner;
    std::set<int> ids;
    int total = 0, minId = INT_MAX, maxId = INT_MIN;
    for (int i = 0; i < n; i++) {
        void* l = dl::At<void*>(game::Nation(i), A.fOfficers);
        for (int j = 0, c = dl::ListCount(l); j < c; j++) {
            void* o = dl::ListItem(l, j);
            owner[o] = i;
            int id = dl::At<int>(o, A.fOId);
            ids.insert(id);
            total++;
            minId = std::min(minId, id);
            maxId = std::max(maxId, id);
        }
    }
    auto who = [&](void* o) {
        auto it = owner.find(o);
        return it == owner.end() ? std::string("?") : std::to_string(it->second);
    };
    std::string s = "ok pn=" + std::to_string(game::PlayerIndex()) + " client=" + std::to_string(g_clientK) + "/" +
                    (g_clientOffs ? "1" : "0") + " traded=" + std::to_string(g_lastTraded) + ":" +
                    std::to_string(g_lastTradeOfficers) + "/" + std::to_string(g_lastTradeShips) + "/" +
                    std::to_string(g_lastTradeAreas) + " nations=";
    for (int i = 0; i < n; i++) {
        void* nat = game::Nation(i);
        void* l = dl::At<void*>(nat, A.fOfficers);
        int c = dl::ListCount(l), active = 0, ships = game::ShipCount(nat), cmd = 0, cmdId = 0, own = 0;
        for (int j = 0; j < c; j++) active += dl::At<uint8_t>(dl::ListItem(l, j), A.fOStatus) == 0;
        for (int j = 0; j < ships; j++) {
            void* sh = game::Ship(nat, j);
            void* o = sh ? dl::At<void*>(sh, A.fCmd) : nullptr;
            int id = sh ? dl::At<int>(sh, A.fCmdId) : -1;
            cmd += o != nullptr;
            cmdId += id != -1 && id != 0;
            own += o && who(o) == std::to_string(i);
        }
        s += std::to_string(i) + ":" + std::to_string(c) + "/" + std::to_string(active) + "/" + std::to_string(ships) +
             "/" + std::to_string(cmd) + "/" + std::to_string(cmdId) + "/" + std::to_string(own) + ";";
    }
    void* areas = dl::At<void*>(f, A.fMapAreas);
    int ac = 0, aid = 0;
    std::map<std::string, int> aOwn;
    for (int j = 0, c = dl::ListCount(areas); j < c; j++) {
        void* a = dl::ListItem(areas, j);
        void* o = a ? dl::At<void*>(a, A.fACmd) : nullptr;
        ac += o != nullptr;
        aid += a && dl::At<int>(a, A.fACmdId) != -1;
        if (o) aOwn[who(o)]++;
    }
    s += " areas=" + std::to_string(ac) + "/" + std::to_string(aid) + "/";
    for (auto& e : aOwn) s += e.first + "x" + std::to_string(e.second) + ",";
    void* dv = A.fDivisions >= 0 ? dl::At<void*>(f, A.fDivisions) : nullptr;
    int dc = 0, dcmd = 0;
    for (int j = 0, c = dl::ListCount(dv); A.fDCmd >= 0 && j < c; j++) {
        void* d = dl::ListItem(dv, j);
        dc++;
        dcmd += d && dl::At<void*>(d, A.fDCmd) != nullptr;
    }
    s += " divs=" + std::to_string(dc) + "/" + std::to_string(dcmd);
    s += " ids=" + (total ? std::to_string(minId) + "-" + std::to_string(maxId) : std::string("-")) + "/" +
         std::to_string(ids.size()) + "/" + std::to_string(total);
    s += " first=";
    void* pl = dl::At<void*>(game::PlayerNation(), A.fOfficers);
    for (int j = 0, c = std::min(3, dl::ListCount(pl)); j < c; j++) {
        void* o = dl::ListItem(pl, j);
        std::string nm = A.fOName >= 0 ? W2U(dl::ReadUStr(dl::At<void*>(o, A.fOName))) : "";
        for (char& ch : nm)
            if (ch == ' ' || ch == '|' || ch == ':') ch = '_';
        s += std::to_string(dl::At<int>(o, A.fOId)) + ":" + nm + ":" +
             std::to_string(A.fORank >= 0 ? dl::At<int>(o, A.fORank) : -1) + ":" +
             std::to_string(dl::At<uint8_t>(o, A.fOStatus)) + "|";
    }
    return s;
}

std::string FreePromotionState() {
    if (!A.freePromo || !*A.freePromo) return "error unavailable";
    return "ok used=" + std::to_string((int)**A.freePromo);
}

static void* ShipOf(int nation, int ship) {
    void* nat = game::Nation(nation);
    return nat && ship >= 0 && ship < game::ShipCount(nat) ? game::Ship(nat, ship) : nullptr;
}

std::string TestKill(int nation, int ship, bool pow) {
    void* f = game::BuildCamp();
    if (ship < 0)  // the first of the nation's ships with a commander
        for (int j = 0, c = game::ShipCount(game::Nation(nation)); j < c && ship < 0; j++)
            if (void* s = game::Ship(game::Nation(nation), j))
                if (A.fCmd >= 0 && dl::At<void*>(s, A.fCmd)) ship = j;
    void* sh = ShipOf(nation, ship);
    if (!A.lists || !A.mKill || !f || !sh || game::EventActive()) return "error no such ship";
    void* o = dl::At<void*>(sh, A.fCmd);
    if (!o) return "error the ship has no commander";
    int id = dl::At<int>(o, A.fOId);
    if (!Guarded3(A.mKill, U(f), U(sh), pow ? 1 : 0)) return "error the game raised an exception";
    Log("TEST: officers: KillCommander on nation %d's ship %d: officer %d status %d", nation, ship, id,
        (int)dl::At<uint8_t>(o, A.fOStatus));
    return "ok ship=" + std::to_string(ship) + " id=" + std::to_string(id) + " status=" +
           std::to_string((int)dl::At<uint8_t>(o, A.fOStatus)) + " cmd=" + (dl::At<void*>(sh, A.fCmd) ? "1" : "0");
}

std::string TestAdjustCrew(int nation, int ship) {
    void* f = game::BuildCamp();
    void* sh = ShipOf(nation, ship);
    if (!A.lists || !A.mAdjustCrew || !f || !sh || game::EventActive()) return "error no such ship";
    if (!Guarded3(A.mAdjustCrew, U(f), U(sh), U(game::Nation(nation)))) return "error the game raised an exception";
    return "ok";
}

std::string TestSet(int nation, int id, const std::string& field, int value) {
    static const char* bytes[] = {"Special1", "Special2", "Status", "AbilityKnown"};
    static const char* ints[] = {"Ability", "Rank", "YearsInRank", "BattleStars"};
    bool isByte = false, known = false;
    for (const char* b : bytes) isByte = isByte || field == b;
    for (const char* i : ints) known = known || field == i;
    int off = dl::Field("TOfficer", field.c_str());
    void* nat = game::Nation(nation);
    void* list = nat && A.fOfficers >= 0 ? dl::At<void*>(nat, A.fOfficers) : nullptr;
    if (!list || off < 0 || !(isByte || known)) return "error no such nation or field";
    for (int j = 0, c = dl::ListCount(list); j < c; j++) {
        void* o = dl::ListItem(list, j);
        if (!o || (id >= 0 && dl::At<int>(o, A.fOId) != id)) continue;
        int before = isByte ? dl::At<uint8_t>(o, off) : dl::At<int>(o, off);
        if (isByte)
            dl::At<uint8_t>(o, off) = (uint8_t)value;
        else
            dl::At<int>(o, off) = value;
        Log("TEST: officers: nation %d's officer %d %s %d -> %d", nation, dl::At<int>(o, A.fOId), field.c_str(), before,
            value);
        return "ok id=" + std::to_string(dl::At<int>(o, A.fOId)) + " " + field + "=" + std::to_string(before) + "->" +
               std::to_string(value);
    }
    return "error no such officer";
}

// The commander of a nation's ship: "ok ship=<i> id=<officer id or -1> special=<s1>,<s2> rank=<r>", or the first
// commanded ship of the nation (ship -1).
std::string ShipCommander(int nation, int ship) {
    void* nat = game::Nation(nation);
    if (!A.lists || !nat) return "error no such nation";
    for (int j = ship < 0 ? 0 : ship, c = game::ShipCount(nat); j < c; j++) {
        void* sh = game::Ship(nat, j);
        void* o = sh ? dl::At<void*>(sh, A.fCmd) : nullptr;
        if (ship >= 0 || o) {
            if (!o) return "ok ship=" + std::to_string(j) + " id=-1";
            static int fS1 = dl::Field("TOfficer", "Special1"), fS2 = dl::Field("TOfficer", "Special2");
            return "ok ship=" + std::to_string(j) + " id=" + std::to_string(dl::At<int>(o, A.fOId)) + " special=" +
                   std::to_string(fS1 >= 0 ? dl::At<uint8_t>(o, fS1) : -1) + "," +
                   std::to_string(fS2 >= 0 ? dl::At<uint8_t>(o, fS2) : -1) + " rank=" +
                   std::to_string(A.fORank >= 0 ? dl::At<int>(o, A.fORank) : -1);
        }
    }
    return "error no commanded ship";
}

// "ok id=<id> name=<name> rank=<r> years=<y> status=<s> stars=<n> special=<s1>,<s2> ship=<index or -1> area=<index or
// -1>" of an officer in the nation's list (the first with ship >= 0 ... by `pick`: "any", "ship", "pool"; id -1)
std::string OfficerText(int nation, int id, const std::string& pick) {
    void* f = game::BuildCamp();
    void* nat = game::Nation(nation);
    void* list = nat && A.fOfficers >= 0 ? dl::At<void*>(nat, A.fOfficers) : nullptr;
    if (!f || !list) return "error no such nation";
    static int fS1 = dl::Field("TOfficer", "Special1"), fS2 = dl::Field("TOfficer", "Special2"),
               fY = dl::Field("TOfficer", "YearsInRank"), fB = dl::Field("TOfficer", "BattleStars");
    void* areas = dl::At<void*>(f, A.fMapAreas);
    for (int j = 0, c = dl::ListCount(list); j < c; j++) {
        void* o = dl::ListItem(list, j);
        if (!o || (id >= 0 && dl::At<int>(o, A.fOId) != id)) continue;
        int ship = -1, area = -1;
        for (int k = 0, n = game::ShipCount(nat); k < n && ship < 0; k++)
            if (void* s = game::Ship(nat, k))
                if (dl::At<void*>(s, A.fCmd) == o) ship = k;
        for (int k = 0, n = dl::ListCount(areas); k < n && area < 0; k++)
            if (void* a = dl::ListItem(areas, k))
                if (dl::At<void*>(a, A.fACmd) == o) area = k;
        int st = dl::At<uint8_t>(o, A.fOStatus);
        if (id < 0 && (st != 0 || (pick == "ship" && ship < 0) || (pick == "pool" && (ship >= 0 || area >= 0)) ||
                       (pick != "any" && A.fORank >= 0 && dl::At<int>(o, A.fORank) >= 3)))
            continue;
        std::string nm = A.fOName >= 0 ? W2U(dl::ReadUStr(dl::At<void*>(o, A.fOName))) : "";
        for (char& ch : nm)
            if (ch == ' ') ch = '_';
        return "ok id=" + std::to_string(dl::At<int>(o, A.fOId)) + " name=" + nm + " rank=" +
               std::to_string(A.fORank >= 0 ? dl::At<int>(o, A.fORank) : -1) + " years=" +
               std::to_string(fY >= 0 ? dl::At<int>(o, fY) : -1) + " status=" + std::to_string(st) + " stars=" +
               std::to_string(fB >= 0 ? dl::At<int>(o, fB) : -1) + " special=" +
               std::to_string(fS1 >= 0 ? dl::At<uint8_t>(o, fS1) : -1) + "," +
               std::to_string(fS2 >= 0 ? dl::At<uint8_t>(o, fS2) : -1) + " ship=" + std::to_string(ship) +
               " area=" + std::to_string(area);
    }
    return "error no such officer";
}

}  // namespace ofc

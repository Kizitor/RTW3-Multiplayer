// 0.9.0: each joined player's own aircraft (aircraft.h; docs/ARCHITECTURE.md "Each joined player's aircraft (0.9.0)").
//
// What the game gives its player (nation 0) and the mod gives a joined nation K:
//  - On K's PC the Aircraft types screen (TfrmAircraftTypes.Execute: FillAircraftTypeList for nation 0, the Request
//    dialog writing the form's ACRequestRole/ACRequest and nation 0's priorities, Buy copying a type for nation 0) and the
//    air group screen (TdlgCAG.Execute: new units and Auto air groups for nation 0) run in K's view: the events' view of
//    game.cpp (entries 0 and K of the nation list swapped, the relations and the war counter K's) and every
//    TAircraftType.Nation and TAirUnit.Nation 0 <-> K swapped. Put back after the screen, also when the game raises.
//  - At the start of each month K's PC runs the player's part of the game's CheckAircraftDevelopment in that view: the
//    request's countdown (ResolveAircraftRequest at 0: the prototypes and the player's choice), the private venture
//    (its chance reproduced from the routine; the game's venture routine then asks) and CheckOldAircraftTypes (the old
//    types' reminders; its computer branch, AIDevelopAircraftType for the other nations, muted). The type loop of that
//    routine (development, entering service, improved models, obsolescence, reliability) runs on the host for all.
//  - K's Submit runs the Turn's air checks; the turn carries aircraft.ini (saveio.cpp); the host merges it.
//  - The host leaves the computer's aircraft decisions for K out (game.cpp g_skips: AIDevelopAircraftType,
//    AIAddAirGroupToShip) and its air month (AircraftExperienceAndReplacement, unnamed) treats K's units as its player's at
//    the two tests of the unit's nation: no 30 % disbanding of reserve units without a base, no role, jet or night
//    changes of the computer.
#include "aircraft.h"
#include "delphi.h"
#include "game.h"
#include "../../third_party/minhook/include/MinHook.h"
#include <algorithm>
#include <climits>
#include <map>
#include <random>
#include <set>

namespace acft {
namespace {

struct Api {
    bool resolved = false, ok = false, hostOk = false;
    // fields
    int fTNation = -1, fTId = -1, fTName = -1, fTMaker = -1, fTRole = -1, fTDev = -1, fTVersion = -1, fTRelKnown = -1,
        fTRel = -1;
    int fUNation = -1, fUId = -1, fUName = -1, fUTypeId = -1, fUBase = -1, fURole = -1;
    int fReq = -1, fReqRole = -1, fPlayer = -1, fTension = -1;
    int fNoRemind = -1, fShipId = -1, fShipList = -1, fFunds = -1;
    // methods
    void *mTypes = nullptr, *mCAG = nullptr, *mResolve = nullptr, *mOldTypes = nullptr, *mVenture = nullptr,
         *mCapacity = nullptr, *mCarriers = nullptr, *mMostTense = nullptr, *mHasCap = nullptr, *mMaker = nullptr,
         *mRoleName = nullptr;
    uint32_t typeCell = 0;  // the cell holding the address of the aircraft type list's variable
    int** yearCell = nullptr;
    int ventureField = -1;  // the nation field the venture's chance takes off (2 x (value - 1); +0x6ab in 1.01.44)
    // the air month (AircraftExperienceAndReplacement): `mov eax,[esi]; cmp dword ptr [eax+Nation],0; jle` at the reserve
    // units' disbanding (short jle) and at the computer's changes (near jle); after = the jle's target
    uint8_t *siteReserve = nullptr, *reserveAfter = nullptr, *siteAI = nullptr, *aiAfter = nullptr;
} A;

bool InImage(uintptr_t a) { return a >= dl::g_base && a < dl::g_base + dl::g_size; }
void* CallTarget(uint8_t* e8) { return e8 + 5 + *(int32_t*)(e8 + 1); }

std::vector<int> Le(int v) {
    std::vector<int> p;
    for (int i = 0; i < 4; i++) p.push_back((v >> (8 * i)) & 0xFF);
    return p;
}

// Does `fn` push (68 imm32) or load into EDX (BA imm32) the UTF-16 literal `text` within maxLen bytes?
bool UsesLiteral(uint8_t* fn, size_t maxLen, const wchar_t* text) {
    size_t n = wcslen(text) * 2;
    for (size_t i = 0; fn && i + 5 <= maxLen; i++) {
        if (fn[i] != 0x68 && fn[i] != 0xBA) continue;
        uintptr_t a = *(uint32_t*)(fn + i + 1);
        if (InImage(a) && InImage(a + n) && memcmp((void*)a, text, n) == 0) return true;
    }
    return false;
}

// The unique hit of `pat` in [fn, fn + len), or nullptr.
uint8_t* Unique(void* fn, size_t len, const std::vector<int>& pat) {
    uint8_t* p = (uint8_t*)fn;
    uint8_t* hit = p ? dl::FindPattern(p, len, pat) : nullptr;
    if (!hit || dl::FindPattern(hit + 1, (size_t)(p + len - (hit + 1)), pat)) return nullptr;
    return hit;
}

void* TypeList() {
    if (!A.typeCell) return nullptr;
    void* var = *(void**)(uintptr_t)A.typeCell;
    void* l = var ? *(void**)var : nullptr;
    return l && dl::IsInstanceOf(l, "TList") ? l : nullptr;
}

int GameYear() { return A.yearCell && *A.yearCell ? **A.yearCell : 0; }

std::string Narrow(const std::wstring& w) {
    std::string s = W2U(w);
    for (char& c : s)
        if (c == ' ' || c == ',' || c == ';' || c == '~') c = '_';
    return s;
}

// ---------------------------------------------------------------------------------------------
// The view

bool g_view = false;
int g_viewK = 0;

// Every type's and air unit's nation index 0 <-> K (an involution: what the view made for "nation 0" becomes K's).
void SwapAirNations(int K) {
    if (void* tl = TypeList())
        for (int i = 0, n = dl::ListCount(tl); i < n; i++) {
            void* t = dl::ListItem(tl, i);
            int& v = dl::At<int>(t, A.fTNation);
            v = v == 0 ? K : v == K ? 0 : v;
        }
    if (void* ul = game::AirUnitList())
        for (int i = 0, n = dl::ListCount(ul); i < n; i++) {
            void* u = dl::ListItem(ul, i);
            int& v = dl::At<int>(u, A.fUNation);
            v = v == 0 ? K : v == K ? 0 : v;
        }
}

bool ViewEnter(int K) {
    if (g_view || !A.ok || !TypeList() || !game::AirUnitList() || !game::PlayerViewEnter(K)) return false;
    SwapAirNations(K);
    g_view = true;
    g_viewK = K;
    return true;
}

void ViewLeave() {
    if (!g_view) return;
    SwapAirNations(g_viewK);
    game::PlayerViewLeave();
    g_view = false;
}

uint32_t CallInView(void* fn, uint32_t eax, uint32_t edx, uint32_t ecx, int n, const uint32_t* st) {
    uint32_t r = 0;
    __try {
        r = dl::Call(fn, eax, edx, ecx, n, st);
    } __finally {
        ViewLeave();
    }
    return r;
}

// ---------------------------------------------------------------------------------------------
// The joined player's PC

int g_clientK = 0;
bool g_clientHost = false;  // the month's header `acft`
bool g_month = false;       // the month's routines run
std::set<int> g_ignored;    // carriers whose "no air units assigned" reminder the player ignored (ship ids)
int g_viewsLogged = 0;
bool g_testCapOff = false, g_testHostOff = false;

bool WantView() {
    return g_clientHost && g_clientK >= 1 && g_clientK <= 8 && !g_view && A.ok && !game::EventActive() &&
           !game::BattleActive() && game::PlayerNation() && game::PlayerNation() == game::Nation(g_clientK);
}

int CountOf(int K, bool units, int* dev = nullptr, int* reserve = nullptr) {
    int n = 0;
    if (dev) *dev = 0;
    if (reserve) *reserve = 0;
    void* l = units ? game::AirUnitList() : TypeList();
    for (int i = 0, c = l ? dl::ListCount(l) : 0; i < c; i++) {
        void* e = dl::ListItem(l, i);
        if (dl::At<int>(e, units ? A.fUNation : A.fTNation) != K) continue;
        n++;
        if (dev && !units && dl::At<int>(e, A.fTDev) > 0) (*dev)++;
        if (reserve && units && !dl::At<void*>(e, A.fUBase)) (*reserve)++;
    }
    return n;
}

void LogView(const char* what) {
    if (g_viewsLogged++ < 40)
        Log("aircraft: %s in nation %d's view (its %d aircraft types, %d air units)", what, g_clientK,
            CountOf(0, false), CountOf(0, true));
}

}  // namespace

// TfrmAircraftTypes.Execute(Self=EAX, aTypeList=EDX, aSelectedType=ECX, ShowRequestButton, aRoleToConsider): ret 8,
// the type in EAX.
extern "C" void* o_acTypes = nullptr;
extern "C" uint32_t __stdcall AC_Types(uint32_t self, uint32_t list, uint32_t sel, const uint32_t* entry) {
    uint32_t args[2] = {entry[2], entry[1]};  // the caller's stack arguments, the first pushed first
    bool view = false;
    try {
        view = WantView() && ViewEnter(g_clientK);
        if (view) LogView("the Aircraft types screen");
    } catch (...) {
        view = false;
    }
    return view ? CallInView(o_acTypes, self, list, sel, 2, args) : dl::Call(o_acTypes, self, list, sel, 2, args);
}
extern "C" __declspec(naked) void AC_dTypes() {
    __asm {
        push esp
        push ecx
        push edx
        push eax
        call AC_Types
        ret 8
    }
}

// TdlgCAG.Execute(Self=EAX, aBase=EDX): the air group screen.
extern "C" void* o_acCAG = nullptr;
extern "C" void __stdcall AC_CAG(uint32_t self, uint32_t base) {
    bool view = false;
    try {
        view = WantView() && ViewEnter(g_clientK);
        if (view) LogView("the air group screen");
    } catch (...) {
        view = false;
    }
    if (view)
        CallInView(o_acCAG, self, base, 0, 0, nullptr);
    else
        dl::Call(o_acCAG, self, base);
}
extern "C" __declspec(naked) void AC_dCAG() {
    __asm {
        push edx
        push eax
        call AC_CAG
        ret
    }
}

// ---------------------------------------------------------------------------------------------
// The host's air month

namespace {
struct AirCount {
    int reserve = 0, kept = 0;
};
AirCount g_airMonth[9];
}  // namespace

extern "C" void* o_acReserve = nullptr;
extern "C" void* o_acAI = nullptr;
extern "C" void* g_acReserveAfter = nullptr;
extern "C" void* g_acAIAfter = nullptr;

// 1: the unit's nation is a joined nation whose player plays its aircraft (take the game's branch for its player).
extern "C" int __stdcall AC_PlayerUnit(void* unit, int site) {
    __try {
        int K = unit ? dl::At<int>(unit, A.fUNation) : -1;
        if (K < 1 || K > 8 || !hookcb::AircraftByPlayer(K)) return 0;
        if (site == 0) {
            if (!dl::At<void*>(unit, A.fUBase)) g_airMonth[K].reserve++;
        } else {
            g_airMonth[K].kept++;
        }
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// At `mov eax,[esi]; cmp dword ptr [eax+Nation],0` (ESI: the address of the routine's local holding the unit): a joined
// nation's unit -> the jle's target (as for the game's player, nation 0); else the game's code (the trampoline). popad
// keeps the flags of `test`.
extern "C" __declspec(naked) void AC_dReserve() {
    __asm {
        pushad
        push 0
        push dword ptr [esi]
        call AC_PlayerUnit
        test eax, eax
        popad
        jnz player
        jmp dword ptr [o_acReserve]
    player:
        jmp dword ptr [g_acReserveAfter]
    }
}
extern "C" __declspec(naked) void AC_dAI() {
    __asm {
        pushad
        push 1
        push dword ptr [esi]
        call AC_PlayerUnit
        test eax, eax
        popad
        jnz player
        jmp dword ptr [o_acAI]
    player:
        jmp dword ptr [g_acAIAfter]
    }
}

// ---------------------------------------------------------------------------------------------
// Resolution and hooks

bool Resolve() {
    if (A.resolved) return A.ok;
    A.resolved = true;
    A.fTNation = dl::Field("TAircraftType", "Nation");
    A.fTId = dl::Field("TAircraftType", "Id");
    A.fTName = dl::Field("TAircraftType", "Name");
    A.fTMaker = dl::Field("TAircraftType", "Manufacturer");
    A.fTRole = dl::Field("TAircraftType", "Role");
    A.fTDev = dl::Field("TAircraftType", "DevelopmentTime");
    A.fTVersion = dl::Field("TAircraftType", "Version");
    A.fTRelKnown = dl::Field("TAircraftType", "ReliabilityKnown");
    A.fTRel = dl::Field("TAircraftType", "Reliability");
    A.fUNation = dl::Field("TAirUnit", "Nation");
    A.fUId = dl::Field("TAirUnit", "AirUnitId");
    A.fUName = dl::Field("TAirUnit", "Name");
    A.fUTypeId = dl::Field("TAirUnit", "AircraftTypeId");
    A.fUBase = dl::Field("TAirUnit", "HomeBase");
    A.fURole = dl::Field("TAirUnit", "Role");
    A.fReq = dl::Field("TfrmBuildCamp", "ACRequest");
    A.fReqRole = dl::Field("TfrmBuildCamp", "ACRequestRole");
    A.fPlayer = dl::Field("TfrmBuildCamp", "PlayerNation");
    A.fTension = dl::Field("TBuilderNation", "Tension");
    A.fShipList = dl::Field("TBuilderNation", "ShipList");
    A.fFunds = dl::Field("TBuilderNation", "Funds");  // (the month's log line only)
    A.fNoRemind = dl::Field("TCampaignShip", "DoNotRemindOfAirGroup");
    A.fShipId = dl::Field("TCampaignShip", "CShipId");
    A.mTypes = dl::Method("TfrmAircraftTypes", "Execute");
    A.mCAG = dl::Method("TdlgCAG", "Execute");
    A.mResolve = dl::Method("TfrmBuildCamp", "ResolveAircraftRequest");
    A.mOldTypes = dl::Method("TfrmBuildCamp", "CheckOldAircraftTypes");
    A.mCapacity = dl::Method("TfrmBuildCamp", "CheckAirBaseCapacity");
    A.mCarriers = dl::Method("TfrmBuildCamp", "CheckPlayerCarriersHaveAircraft");
    A.mMostTense = dl::Method("TfrmBuildCamp", "GetMostTenseNationIndex");
    A.mHasCap = dl::Method("TBuilderNation", "HasAircraftCapability");
    A.mMaker = dl::Method("TfrmBuildCamp", "GetAircraftManufacturerSuggestion");
    void* dev = dl::Method("TfrmBuildCamp", "CheckAircraftDevelopment");
    // the private venture: Privateventure1Click = `call <venture>; ret`, the venture's text pushed in it
    if (uint8_t* pv = (uint8_t*)dl::Method("TfrmBuildCamp", "Privateventure1Click"))
        if (pv[0] == 0xE8 && pv[5] == 0xC3 && InImage((uintptr_t)CallTarget(pv)) &&
            UsesLiteral((uint8_t*)CallTarget(pv), 0x300, L" as a private venture. They want to know if we are interested"))
            A.mVenture = CallTarget(pv);
    // the type list: btbnAircraftTypesClick `push 1; push 0Fh; mov edx,[cell]; mov edx,[edx]` before Execute
    if (uint8_t* h = Unique(dl::Method("TfrmBuildCamp", "btbnAircraftTypesClick"), 0x60,
                            {0x6A, 0x01, 0x6A, 0x0F, 0x8B, 0x15, -1, -1, -1, -1, 0x8B, 0x12}))
        if (InImage(*(uint32_t*)(h + 6))) A.typeCell = *(uint32_t*)(h + 6);
    // the year: TdlgAircraftRequest.bnOKClick `mov eax,[cell]; mov eax,[eax]; cmp eax,1940`
    if (uint8_t* h = Unique(dl::Method("TdlgAircraftRequest", "bnOKClick"), 0x300,
                            {0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0x3D, 0x94, 0x07, 0x00, 0x00}))
        if (InImage(*(uint32_t*)(h + 1))) A.yearCell = (int**)(uintptr_t) * (uint32_t*)(h + 1);
    // the venture's chance: CheckAircraftDevelopment `mov eax,[eax+PlayerNation]; mov eax,[eax+F]; dec eax; add eax,eax;
    // sub edi,eax` (F: the nation field it takes off)
    if (A.fPlayer > 0 && dev) {
        std::vector<int> p = {0x8B, 0x80};
        for (int b : Le(A.fPlayer)) p.push_back(b);
        p.insert(p.end(), {0x8B, 0x80, -1, -1, -1, -1, 0x48, 0x03, 0xC0, 0x2B, 0xF8});
        if (uint8_t* h = Unique(dev, 0xC00, p)) {
            int f = *(int*)(h + 8);
            if (f > 0 && f < 0x10000) A.ventureField = f;
        }
    }
    // the role's name: CheckAircraftDevelopment `movzx eax,byte ptr [eax+Role]; lea edx,[ebp+x]; call <name>` (every hit
    // the same routine)
    if (A.fTRole > 0 && dev) {
        std::vector<int> p = {0x0F, 0xB6, 0x80};
        for (int b : Le(A.fTRole)) p.push_back(b);
        p.insert(p.end(), {0x8D, 0x55, -1, 0xE8});
        void* t = nullptr;
        int hits = 0;
        bool same = true;
        for (uint8_t* q = (uint8_t*)dev; (q = dl::FindPattern(q, 0xC00 - (size_t)(q - (uint8_t*)dev), p)) != nullptr; q++) {
            void* c = CallTarget(q + 10);
            same = same && (!t || c == t);
            t = c;
            hits++;
        }
        if (hits >= 2 && same && InImage((uintptr_t)t)) A.mRoleName = t;
    }
    // the air month: DoStrategicTurn's call whose routine loads "Error in AircraftExperienceAndReplacement procedure!"
    uint8_t* month = nullptr;
    if (void* dst = dl::Method("TfrmBuildCamp", "DoStrategicTurn"))
        for (uint8_t* t : dl::CallTargets((uint8_t*)dst, 0x2000))
            if (InImage((uintptr_t)t) && InImage((uintptr_t)t + 0x1000) &&
                UsesLiteral(t, 0x1000, L"Error in AircraftExperienceAndReplacement procedure!")) {
                month = t;
                break;
            }
    if (month && A.fUNation > 0 && A.fUNation < 0x80) {
        uint8_t n = (uint8_t)A.fUNation;
        if (uint8_t* h = Unique(month, 0xC00, {0x8B, 0x06, 0x83, 0x78, n, 0x00, 0x7E})) {
            A.siteReserve = h;
            A.reserveAfter = h + 8 + (int8_t)h[7];
        }
        if (uint8_t* h = Unique(month, 0xC00, {0x8B, 0x06, 0x83, 0x78, n, 0x00, 0x0F, 0x8E})) {
            A.siteAI = h;
            A.aiAfter = h + 12 + *(int32_t*)(h + 8);
        }
        bool inside = A.reserveAfter && A.aiAfter && A.reserveAfter > month && A.reserveAfter < month + 0xC00 &&
                      A.aiAfter > month && A.aiAfter < month + 0xC00 && A.reserveAfter[0] == 0x8B && A.aiAfter[0] == 0x8B;
        if (!inside) A.siteReserve = A.siteAI = A.reserveAfter = A.aiAfter = nullptr;
    }
    bool fields = A.fTNation >= 0 && A.fTId >= 0 && A.fTName >= 0 && A.fTMaker >= 0 && A.fTRole >= 0 && A.fTDev >= 0 &&
                  A.fTVersion >= 0 && A.fTRelKnown >= 0 && A.fTRel >= 0 && A.fUNation >= 0 && A.fUId >= 0 &&
                  A.fUName >= 0 && A.fUTypeId >= 0 && A.fUBase >= 0 && A.fReq >= 0 && A.fReqRole >= 0 && A.fPlayer >= 0 &&
                  A.fTension >= 0 && A.fNoRemind >= 0 && A.fShipId >= 0;
    A.ok = fields && A.mTypes && A.mCAG && A.mResolve && A.mOldTypes && A.mVenture && A.mCapacity && A.mCarriers &&
           A.mMostTense && A.mHasCap && A.mMaker && A.typeCell && A.yearCell && A.ventureField > 0;
    A.hostOk = A.ok && A.siteReserve && A.siteAI;
    Log("aircraft: %s (fields %d, screens %p/%p, request %p, old types %p, venture %p (field %#x), checks %p/%p, type list "
        "%08x, year %p, role names %p); host: %s (air month sites rva %#x/%#x)",
        A.ok ? "available" : "UNAVAILABLE", fields ? 1 : 0, A.mTypes, A.mCAG, A.mResolve, A.mOldTypes, A.mVenture,
        A.ventureField, A.mCapacity, A.mCarriers, A.typeCell, (void*)A.yearCell, A.mRoleName,
        A.hostOk ? "available" : "UNAVAILABLE",
        A.siteReserve ? (unsigned)((uintptr_t)A.siteReserve - dl::g_base) : 0u,
        A.siteAI ? (unsigned)((uintptr_t)A.siteAI - dl::g_base) : 0u);
    return A.ok;
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
    if (!A.ok) return;
    HookOne(A.mTypes, (void*)&AC_dTypes, &o_acTypes, "TfrmAircraftTypes.Execute");
    HookOne(A.mCAG, (void*)&AC_dCAG, &o_acCAG, "TdlgCAG.Execute");
    if (A.hostOk) {
        g_acReserveAfter = A.reserveAfter;
        g_acAIAfter = A.aiAfter;
        HookOne(A.siteReserve, (void*)&AC_dReserve, &o_acReserve, "the air month's reserve units");
        HookOne(A.siteAI, (void*)&AC_dAI, &o_acAI, "the air month's computer changes");
        if (!o_acReserve || !o_acAI) A.hostOk = false;
    }
    if (!o_acTypes || !o_acCAG) A.ok = A.hostOk = false;
}

bool Available() { return A.ok; }
bool HostAvailable() { return A.hostOk; }
bool InView() { return g_view; }

// ---------------------------------------------------------------------------------------------
// The joined player's PC

static void ApplyIgnored() {
    void* pn = game::PlayerNation();
    for (int i = 0, n = pn && !g_ignored.empty() ? game::ShipCount(pn) : 0; i < n; i++)
        if (void* s = game::Ship(pn, i))
            if (g_ignored.count(dl::At<int>(s, A.fShipId))) dl::At<uint8_t>(s, A.fNoRemind) = 1;
}

static void RememberIgnored() {
    void* pn = game::PlayerNation();
    for (int i = 0, n = pn ? game::ShipCount(pn) : 0; i < n; i++)
        if (void* s = game::Ship(pn, i))
            if (dl::At<uint8_t>(s, A.fNoRemind)) g_ignored.insert(dl::At<int>(s, A.fShipId));
}

void ClientSetMonth(int K, bool hostAircraft) {
    if (K != g_clientK) g_ignored.clear();
    g_clientK = K;
    g_clientHost = hostAircraft && A.ok && K >= 1 && K <= 8;
    if (g_clientHost) {
        try {
            ApplyIgnored();
        } catch (...) {
        }
    }
    Log("aircraft: month of nation %d: %s", K,
        g_clientHost ? "the host keeps the nation's aircraft (its screens in its view, the month's routines, its turn)"
        : hostAircraft ? "the host keeps them, this PC can't (UNAVAILABLE)"
                       : "the host decides the nation's aircraft (an older host or none)");
}

void ClientReset() {
    g_clientK = 0;
    g_clientHost = false;
    g_ignored.clear();
}

bool ClientActive() {
    return g_clientHost && g_clientK >= 1 && game::PlayerNation() && game::PlayerNation() == game::Nation(g_clientK);
}

bool ClientMonthRunning() { return g_month; }

namespace {
std::mt19937 g_rng((unsigned)GetTickCount() * 2654435761u);

// The private venture's chance in CheckAircraftDevelopment (1.01.44 0xd1c857): Random(n) = 1 with n = 20, -4 when the
// most tense nation's tension is above 7, -4 at war, +5/+10/+15 in 1940-45/1946-49/1950-55, +25/+40/+80 in
// 1956-60/1961-80/1981-99, -2 x (PlayerNation's ventureField - 1). In the view: K's relations and war counter.
int VentureChance(void* f, void* pn) {
    int n = 20;
    int mt = (int)dl::Call(A.mMostTense, (uint32_t)(uintptr_t)f);
    if (void* m = game::Nation(mt))
        if (dl::At<int>(m, A.fTension) > 7) n -= 4;
    if (game::WarCounter() > 0) n -= 4;
    int y = GameYear();
    if (y >= 1956) {
        int d = y - 1956;
        n += d < 5 ? 25 : d < 25 ? 40 : d < 44 ? 80 : 0;
    } else if (y >= 1940) {
        int d = y - 1940;
        n += d < 6 ? 5 : d < 10 ? 10 : d < 16 ? 15 : 0;
    }
    n -= 2 * (dl::At<int>(pn, A.ventureField) - 1);
    return n;
}

// PlayerNation.HasAircraftCapability and an industry of its own (GetAircraftManufacturerSuggestion's name not '-').
bool VentureAllowed(void* f, void* pn) {
    if (!(dl::Call(A.mHasCap, (uint32_t)(uintptr_t)pn) & 0xFF)) return false;
    uint32_t rec[8] = {};  // the managed record the routine returns (its first field the maker's name)
    uint32_t st[1] = {(uint32_t)(uintptr_t)rec};
    dl::Call(A.mMaker, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)pn, 0, 1, st);
    std::wstring name = rec[0] ? dl::ReadUStr((void*)(uintptr_t)rec[0]) : L"";
    return !name.empty() && name != L"-";
}

struct MonthRun {
    int req0 = 0, role = 0;
    bool resolved = false, ventureRolled = false, venture = false;
    int chance = 0;
};

void MonthBody(void* f, MonthRun& r) {
    void* pn = game::PlayerNation();
    int req = dl::At<int>(f, A.fReq);
    if (req > 0) {
        dl::At<int>(f, A.fReq) = req - 1;
        if (req == 1) {
            dl::Call(A.mResolve, (uint32_t)(uintptr_t)f);
            r.resolved = true;
        }
    }
    r.chance = VentureChance(f, pn);
    if (r.chance > 0 && std::uniform_int_distribution<int>(0, r.chance - 1)(g_rng) == 1) {
        r.ventureRolled = true;
        if (VentureAllowed(f, pn)) {
            dl::Call(A.mVenture, (uint32_t)(uintptr_t)f);
            r.venture = true;
        }
    }
    dl::Call(A.mOldTypes, (uint32_t)(uintptr_t)f);
}

bool MonthGuarded(void* f, MonthRun& r) {
    bool ok = false;
    g_month = true;
    __try {
        MonthBody(f, r);
        ok = true;
    } __finally {
        g_month = false;
        ViewLeave();
    }
    return ok;
}
}  // namespace

std::string ClientMonth(int K) {
    void* f = game::BuildCamp();
    if (!ClientActive() || K != g_clientK || !f) return "";
    MonthRun r;
    r.req0 = dl::At<int>(f, A.fReq);
    r.role = dl::At<uint8_t>(f, A.fReqRole);
    int dev0 = 0, dev1 = 0;
    int types0 = CountOf(K, false, &dev0);
    void* pn = game::PlayerNation();
    int funds0 = pn && A.fFunds >= 0 ? dl::At<int>(pn, A.fFunds) : 0;
    if (!ViewEnter(K)) return "not run: the view is unavailable";
    bool ok = false;
    try {
        ok = MonthGuarded(f, r);
    } catch (...) {
        ok = false;
    }
    int types1 = CountOf(K, false, &dev1);
    int funds1 = pn && A.fFunds >= 0 ? dl::At<int>(pn, A.fFunds) : 0;
    char buf[340];
    snprintf(buf, sizeof(buf),
             "request %d:%d -> %d:%d%s; private venture %s (1 in %d); old types checked%s; types %d -> %d (%d -> %d in "
             "development); funds %d -> %d",
             r.role, r.req0, (int)dl::At<uint8_t>(f, A.fReqRole), dl::At<int>(f, A.fReq),
             r.resolved ? " (prototypes resolved)" : "", r.venture ? "offered" : r.ventureRolled ? "rolled, no industry" : "no",
             r.chance, ok ? "" : " (the game raised an exception)", types0, types1, dev0, dev1, funds0, funds1);
    return buf;
}

bool ClientSubmitChecks(std::string& why) {
    void* f = game::BuildCamp();
    void* pn = game::PlayerNation();
    if (!ClientActive() || !f || !pn) return true;
    ApplyIgnored();
    if (!(dl::Call(A.mCapacity, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)pn) & 0xFF)) {
        why = "a carrier or base holds more aircraft than it has room for (the game's message names it): change its air "
              "group first";
        return false;
    }
    bool ok = (dl::Call(A.mCarriers, (uint32_t)(uintptr_t)f) & 0xFF) != 0;
    RememberIgnored();
    if (!ok) {
        why = "you chose to go to the air groups screen";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// The host: the month's news of the joined nations' types and units

namespace {
struct TypeSnap {
    int nation = 0, dev = 0, version = 0, relKnown = 0, rel = 0, role = 0;
    std::wstring name, maker;
};
struct UnitSnap {
    int nation = 0, type = 0, role = 0;
    std::wstring name;
};
std::map<int, TypeSnap> g_types0;
std::map<int, UnitSnap> g_units0;
bool g_snap = false;
std::map<int, std::vector<std::wstring>> g_news;

std::map<int, TypeSnap> TypesNow() {
    std::map<int, TypeSnap> m;
    void* l = TypeList();
    for (int i = 0, n = l ? dl::ListCount(l) : 0; i < n; i++) {
        void* t = dl::ListItem(l, i);
        int K = dl::At<int>(t, A.fTNation);
        if (K < 1 || K > 8 || !hookcb::AircraftByPlayer(K)) continue;
        TypeSnap s;
        s.nation = K;
        s.dev = dl::At<int>(t, A.fTDev);
        s.version = dl::At<int>(t, A.fTVersion);
        s.relKnown = dl::At<uint8_t>(t, A.fTRelKnown);
        s.rel = dl::At<int>(t, A.fTRel);
        s.role = dl::At<uint8_t>(t, A.fTRole);
        s.name = dl::ReadUStr(dl::At<void*>(t, A.fTName));
        s.maker = dl::ReadUStr(dl::At<void*>(t, A.fTMaker));
        m[dl::At<int>(t, A.fTId)] = s;
    }
    return m;
}

std::map<int, UnitSnap> UnitsNow() {
    std::map<int, UnitSnap> m;
    void* l = game::AirUnitList();
    for (int i = 0, n = l ? dl::ListCount(l) : 0; i < n; i++) {
        void* u = dl::ListItem(l, i);
        int K = dl::At<int>(u, A.fUNation);
        if (K < 1 || K > 8 || !hookcb::AircraftByPlayer(K)) continue;
        UnitSnap s;
        s.nation = K;
        s.type = dl::At<int>(u, A.fUTypeId);
        s.role = A.fURole >= 0 ? dl::At<uint8_t>(u, A.fURole) : 0;
        s.name = dl::ReadUStr(dl::At<void*>(u, A.fUName));
        m[dl::At<int>(u, A.fUId)] = s;
    }
    return m;
}

// The game's name of a role (the routine CheckAircraftDevelopment uses for its texts), "" if unavailable.
void* RoleNameRaw(int role) {
    void* s = nullptr;
    __try {
        dl::Call(A.mRoleName, (uint32_t)role, (uint32_t)(uintptr_t)&s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s = nullptr;
    }
    return s;
}

std::wstring RoleName(int role) {
    void* s = A.mRoleName ? RoleNameRaw(role) : nullptr;
    return s ? dl::ReadUStr(s) : L"";
}

std::wstring Spaced(const std::wstring& a, const std::wstring& b) { return a.empty() ? b : b.empty() ? a : a + L" " + b; }

// TAircraftType.GetCompleteName(False): the name alone when it starts with the manufacturer's ("Loire 40"), else both.
std::wstring Complete(const std::wstring& maker, const std::wstring& name) {
    return !maker.empty() && name.compare(0, maker.size(), maker) == 0 ? name : Spaced(maker, name);
}
}  // namespace

void HostMonthStart() {
    g_snap = false;
    if (!A.hostOk) return;
    try {
        g_types0 = TypesNow();
        g_units0 = UnitsNow();
        g_snap = true;
    } catch (...) {
    }
}

void HostMonthEnd() {
    if (!g_snap) return;
    g_snap = false;
    try {
        std::map<int, TypeSnap> t1 = TypesNow();
        std::map<int, UnitSnap> u1 = UnitsNow();
        std::map<int, int> counts;
        for (auto& e : t1) {
            const TypeSnap& now = e.second;
            std::wstring full = Complete(now.maker, now.name), role = RoleName(now.role);
            auto was = g_types0.find(e.first);
            std::vector<std::wstring>& out = g_news[now.nation];
            size_t before = out.size();
            if (was == g_types0.end()) {
                if (now.version > 0)  // DoImprovedModel: a new version of one of the nation's types
                    out.push_back(L"The " + now.maker + L" company has developed an improved model: the " +
                                  Spaced(role, now.name) + L".");
            } else {
                const TypeSnap& old = was->second;
                if (old.dev > 0 && now.dev == 0)
                    out.push_back(L"Our new " + Spaced(role, full) + L" is ready for operational service.");
                else if (old.dev > 0 && now.dev >= old.dev)
                    out.push_back(L"Unexpected problems have delayed the development of our new " + Spaced(role, full) + L".");
                if (!old.relKnown && now.relKnown) {
                    if (now.rel == 1)
                        out.push_back(L"Our aircraft type " + full + L" seems to have rather poor reliability!");
                    else if (now.rel == 3)
                        out.push_back(L"Our " + Spaced(role, L"aircraft type ") + full + L" seems to have good reliability!");
                }
            }
            counts[now.nation] += (int)(out.size() - before);
        }
        for (auto& e : u1) {
            auto was = g_units0.find(e.first);
            if (was == g_units0.end() || was->second.type == e.second.type || e.second.type < 0) continue;
            std::wstring tn;
            for (auto& t : t1)
                if (t.first == e.second.type) tn = Complete(t.second.maker, t.second.name);
            if (tn.empty()) continue;
            g_news[e.second.nation].push_back(Spaced(RoleName(e.second.role), L"squadron ") + e.second.name +
                                              L" has reequipped with " + tn + L".");
            counts[e.second.nation]++;
        }
        for (auto& c : counts)
            if (c.second) Log("aircraft: %d line(s) of nation %d's aircraft news for its next month", c.second, c.first);
    } catch (...) {
        Log("aircraft: the month's news failed");
    }
    g_types0.clear();
    g_units0.clear();
}

std::vector<std::wstring> HostTakeNews(int K) {
    std::vector<std::wstring> v;
    auto it = g_news.find(K);
    if (it != g_news.end()) {
        v.swap(it->second);
        g_news.erase(it);
    }
    return v;
}

std::string HostTakeAirMonth() {
    std::string s;
    for (int k = 1; k <= 8; k++) {
        if (g_airMonth[k].reserve || g_airMonth[k].kept)
            s += " nation " + std::to_string(k) + ": " + std::to_string(g_airMonth[k].reserve) + " reserve unit(s) kept, " +
                 std::to_string(g_airMonth[k].kept) + " unit(s) without the computer's changes;";
        g_airMonth[k] = AirCount();
    }
    return s;
}

// ---------------------------------------------------------------------------------------------
// Tests

std::string Info(int nation) {
    if (!A.resolved) return "error unavailable";
    void* f = game::BuildCamp();
    int K = nation >= 0 ? nation : game::PlayerIndex();
    if (!f || K < 0) return "error no campaign";
    int dev = 0, reserve = 0;
    int nt = CountOf(K, false, &dev), nu = CountOf(K, true, nullptr, &reserve);
    std::string types, ids;
    int minId = INT_MAX, maxId = 0;
    if (void* l = TypeList())
        for (int i = 0, n = dl::ListCount(l); i < n; i++) {
            void* t = dl::ListItem(l, i);
            if (dl::At<int>(t, A.fTNation) != K) continue;
            types += std::to_string(dl::At<int>(t, A.fTId)) + "~" + Narrow(dl::ReadUStr(dl::At<void*>(t, A.fTName))) + "~" +
                     std::to_string(dl::At<int>(t, A.fTDev)) + ",";
        }
    if (void* l = game::AirUnitList())
        for (int i = 0, n = dl::ListCount(l); i < n; i++) {
            void* u = dl::ListItem(l, i);
            if (dl::At<int>(u, A.fUNation) != K) continue;
            int id = dl::At<int>(u, A.fUId);
            minId = std::min(minId, id);
            maxId = std::max(maxId, id);
        }
    char buf[200];
    snprintf(buf, sizeof(buf), "ok nation=%d view=%d client=%d active=%d host=%d month=%d req=%d:%d types=%d dev=%d units=%d "
             "reserve=%d unitIds=%d-%d ignored=%d", K, g_view ? 1 : 0, A.ok ? 1 : 0, ClientActive() ? 1 : 0, A.hostOk ? 1 : 0,
             g_month ? 1 : 0, (int)dl::At<uint8_t>(f, A.fReqRole), dl::At<int>(f, A.fReq), nt, dev, nu, reserve,
             nu ? minId : 0, nu ? maxId : 0, (int)g_ignored.size());
    return std::string(buf) + " list=" + types;
}

std::string TestSet(const std::vector<std::string>& a) {
    auto arg = [&](size_t i, const char* d = "") { return i < a.size() ? a[i] : std::string(d); };
    std::string what = arg(1);
    void* f = game::BuildCamp();
    if (what == "cap" || what == "host") {
        bool off = arg(2, "1") == "0";
        (what == "cap" ? g_testCapOff : g_testHostOff) = off;
        Log("TEST: aircraft: %s %s", what == "cap" ? "this PC's hello announces acft" : "this host keeps nations' aircraft",
            off ? "OFF (as an older version)" : "on");
        return std::string("ok ") + what + "=" + (off ? "0" : "1");
    }
    if (!A.ok || !f) return "error unavailable";
    if (what == "req") {
        int m = atoi(arg(2, "0").c_str());
        dl::At<int>(f, A.fReq) = std::max(0, m);
        if (!arg(3).empty()) dl::At<uint8_t>(f, A.fReqRole) = (uint8_t)atoi(arg(3).c_str());
        Log("TEST: aircraft: the form's request now %d:%d", (int)dl::At<uint8_t>(f, A.fReqRole), dl::At<int>(f, A.fReq));
        return "ok req=" + std::to_string(dl::At<uint8_t>(f, A.fReqRole)) + ":" + std::to_string(dl::At<int>(f, A.fReq));
    }
    if (what == "dev") {
        int id = atoi(arg(2, "0").c_str()), m = atoi(arg(3, "0").c_str());
        if (void* l = TypeList())
            for (int i = 0, n = dl::ListCount(l); i < n; i++) {
                void* t = dl::ListItem(l, i);
                if (dl::At<int>(t, A.fTId) != id) continue;
                int was = dl::At<int>(t, A.fTDev);
                dl::At<int>(t, A.fTDev) = std::max(0, m);
                Log("TEST: aircraft: type %d's DevelopmentTime %d -> %d", id, was, std::max(0, m));
                return "ok dev=" + std::to_string(was) + "->" + std::to_string(std::max(0, m));
            }
        return "error no such type";
    }
    if (what == "aidev") {  // the computer's development for a nation, called as CheckAircraftDevelopment does (role -1)
        int K = atoi(arg(2, "0").c_str());
        void* m = dl::Method("TfrmBuildCamp", "AIDevelopAircraftType");
        if (!m || K < 1 || K > 8) return "error usage: acset aidev <nation 1-8>";
        int n0 = CountOf(K, false);
        dl::Call(m, (uint32_t)(uintptr_t)f, (uint32_t)K, 0xFFFFFFFFu);
        int n1 = CountOf(K, false);
        Log("TEST: aircraft: AIDevelopAircraftType(%d, -1) called: nation %d's types %d -> %d", K, K, n0, n1);
        return "ok types=" + std::to_string(n0) + "->" + std::to_string(n1);
    }
    return "error usage: acset req <months> [role] | dev <type id> <months> | aidev <nation> | cap 0|1 | host 0|1";
}

bool TestCapOff() { return g_testCapOff; }
bool TestHostOff() { return g_testHostOff; }

}  // namespace acft

// 0.7.0: invasions for joined nations. RTW3 1.01.44 keeps invasions for its player only (nation 0):
//  - the month's resolution is TfrmBuildCamp's unnamed routine its own error text calls CheckRebellionAndInvasions
//    (DoStrategicTurn's call after 'Checking rebellions'): every possession with Invaded <> -1 is resolved as nation 0's
//    (Invaded > 0 = an enemy invades nation 0's possession, Invaded = 0 = nation 0 invades), nation 0's strength
//    against a random enemy, its prestige, TakenFrom := 0, every invasion cancelled at peace; the computer starts
//    invasions only against nation 0's possessions; the player's plan is in the form (InvasionTarget, InvasionCount,
//    EnemyInvasionTarget, InvasionBattlePending); InvadedThisTurn is never saved.
//  - The same routine counts every possession's base expansion down (BuildingBase) and runs the rebellions of the
//    possessions nation 0 does not own (the part from the possession's Rebellion test to the loop's next possession).
// A joined nation K's invasions are kept in the possession fields as single player keeps nation 0's, in the real
// numbering (Invaded = the invading nation): on the host the game's own routines see only the invasions that involve
// nation 0 (HostHide), and K's month runs the routine in K's view (RunForNation): K's invasions only, K's plan in the
// form fields, the countdown and the rebellions left out (they ran in the host's own call).
#include "invasion.h"
#include "common.h"
#include "delphi.h"
#include "game.h"
#include "lockstep.h"
#include "../../third_party/minhook/include/MinHook.h"
#include <windows.h>
#include <algorithm>
#include <map>

namespace inv {
bool RefuseBattleAt(void* ps);
bool InstallEscorts();
}

extern "C" {
void* o_InvResolve = nullptr;   // CheckRebellionAndInvasions
void* o_InvTail = nullptr;      // its rebellion part (the possession's Rebellion test)
void* o_InvPossBattle = nullptr;  // DoPossessionBattle
uintptr_t g_invTailJoin = 0;    // the possession loop's next possession
uintptr_t g_invCfibStart = 0, g_invCfibEnd = 0;  // CheckForInvasionBattles' code
volatile uint8_t g_invSkipTail = 0;
uintptr_t g_invRndOrig = 0, g_invRStart = 0, g_invREnd = 0;
uint32_t g_invRndVal = 0;
}

namespace inv {
namespace {
bool g_ok = false;
void* g_resolve = nullptr;
uint32_t* g_randCell = nullptr;
int g_testRnd = 0;
int fName = -1, fValue = -1, fOwner = -1, fInvaded = -1, fSupport = -1, fITT = -1, fTaken = -1, fBuilding = -1;
int fAreas = -1, fAreaPoss = -1, fTarget = -1, fCount = -1, fEnemyTarget = -1, fPending = -1, fArmyOff = -1,
    fEnemyDecl = -1, fEVP = -1, fSAPoss = -1, fMsgs = -1, fPlayer = -1, fSLItems = -1, fSLCount = -1, fAreaName = -1;
void *mSLNew = nullptr, *mObjFree = nullptr, *slVmt = nullptr, *mModBudget = nullptr;
bool g_kRun = false;

int& I(void* o, int off) { return dl::At<int>(o, off); }

template <class Fn>
void ForEachPoss(Fn fn) {
    void* f = game::BuildCamp();
    void* areas = f ? dl::At<void*>(f, fAreas) : nullptr;
    for (int a = 0; areas && a < dl::ListCount(areas); a++) {
        void* area = dl::ListItem(areas, a);
        void* poss = area ? dl::At<void*>(area, fAreaPoss) : nullptr;
        for (int p = 0; poss && p < dl::ListCount(poss); p++)
            if (void* ps = dl::ListItem(poss, p)) fn(ps);
    }
}

std::wstring NameOf(void* ps) { return ps ? dl::ReadUStr(dl::At<void*>(ps, fName)) : std::wstring(); }

void* FindPoss(const std::wstring& name) {
    void* hit = nullptr;
    if (!name.empty()) ForEachPoss([&](void* ps) {
            if (!hit && NameOf(ps) == name) hit = ps;
        });
    return hit;
}

int OwnerIdx(void* ps) {
    void* o = dl::At<void*>(ps, fOwner);
    return o ? game::NationIndex(o) : -1;
}

bool Human(int n) { return n == 0 || (n > 0 && n <= 8 && hookcb::IsHumanRemoteNation(n)); }

std::vector<std::wstring> ListLines(void* sl) {
    std::vector<std::wstring> out;
    int n = sl ? dl::At<int>(sl, fSLCount) : 0;
    void** items = n > 0 ? dl::At<void**>(sl, fSLItems) : nullptr;
    for (int i = 0; items && i < n; i++) out.push_back(dl::ReadUStr(items[i * 2]));
    return out;
}

// --- the host's own routines: the invasions without nation 0 hidden while they run ---
struct Hid {
    void* ps;
    int inv, sup, taken;
    bool hidInv, keepTaken;
};
std::vector<Hid> g_hid;
int g_hideDepth = 0;
const char* g_hideWhat = "";
std::map<std::string, std::pair<int, int>> g_hideLog;  // what -> (calls, possessions hidden)
bool g_hidePeace = false;

void HideScan(bool peace) {
    ForEachPoss([peace](void* ps) {
        int inv = I(ps, fInvaded), taken = I(ps, fTaken), owner = OwnerIdx(ps);
        bool k = inv > 0 && owner != 0;  // an invasion that does not involve nation 0
        bool keepTaken = peace && taken > 0 && (Human(taken) || (owner > 0 && Human(owner)));
        if (!k && !keepTaken) return;
        g_hid.push_back({ps, inv, I(ps, fSupport), taken, k, keepTaken});
        if (k) I(ps, fInvaded) = -1;
    });
}

int HideScanGuarded(bool peace) {
    __try {
        HideScan(peace);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
}  // namespace

void HostHide(const char* what, bool peace) {
    if (!g_ok || g_kRun || game::EventActive()) return;
    if (g_hideDepth++ > 0) return;
    g_hid.clear();
    g_hideWhat = what;
    g_hidePeace = peace;
    if (!HideScanGuarded(peace)) Log("invasions: %s: the possession scan failed", what);
    try {
        auto& l = g_hideLog[what];
        l.first++;
        l.second += (int)g_hid.size();
    } catch (...) {
    }
}

void HostUnhide() {
    if (g_hideDepth <= 0 || --g_hideDepth > 0) return;
    for (auto& h : g_hid) {
        int& cur = I(h.ps, fInvaded);
        if (g_hidePeace) {  // DoPeace cleared every possession's war state
            if (h.hidInv) {
                cur = h.inv;
                I(h.ps, fSupport) = h.sup;
            }
            if (h.keepTaken) I(h.ps, fTaken) = h.taken;
        } else if (h.hidInv) {
            if (cur == -1)
                cur = h.inv;
            else
                Log("invasions: %s started nation %d's invasion of %s while nation %d's was hidden: nation %d's ends",
                    g_hideWhat, cur, W2U(NameOf(h.ps)).c_str(), h.inv, h.inv);
        }
    }
    g_hid.clear();
}

bool RefuseBattleAt(void* ps) {
    if (!g_ok || !ps || g_kRun || game::EventActive()) return false;
    int inv = I(ps, fInvaded), owner = OwnerIdx(ps);
    if (inv <= 0 || owner == 0) return false;
    auto& l = g_hideLog["DoPossessionBattle refused"];
    l.first++;
    l.second++;
    Log("invasions: the host's invasion battles: none at %s (nation %d's invasion of nation %d's possession)",
        W2U(NameOf(ps)).c_str(), inv, owner);
    return true;
}

std::string TakeHideLog() {
    std::string s;
    for (auto& e : g_hideLog)
        s += (s.empty() ? "" : ", ") + e.first + " " + std::to_string(e.first.empty() ? 0 : e.second.first) + "x (" +
             std::to_string(e.second.second) + " hidden)";
    g_hideLog.clear();
    return s;
}

}  // namespace inv

// CheckRebellionAndInvasions(Self=EAX): the host's own call sees only nation 0's invasions.
extern "C" void __stdcall CB_InvResolve(void* form) {
    inv::HostHide("CheckRebellionAndInvasions");
    __try {
        dl::Call(o_InvResolve, (uint32_t)(uintptr_t)form);
    } __finally {
        inv::HostUnhide();
    }
}

extern "C" __declspec(naked) void d_InvResolve() {
    __asm {
        push eax
        call CB_InvResolve
        ret
    }
}

// DoPossessionBattle(Self=EAX, anAttacker=EDX, aDefender=ECX, anArea, aPossession): Boolean, ret 8. The host's own
// CheckForInvasionBattles picks invaded possessions without asking whose invasion it is ("Owner = player: the invader
// attacks the player, else the player attacks the owner"): a joined nation's invasion gives no battle there.
// 0.7.0 (P3): every call from CheckForInvasionBattles is noted (a joined nation's battle slot reports the battle's enemy).
namespace inv {
void NotePossBattle(void* ps, void* attacker, void* defender, void* area);
}

extern "C" int __stdcall CB_InvPossBattle(void* ps, void* attacker, void* defender, void* area) {
    __try {
        inv::NotePossBattle(ps, attacker, defender, area);
        return inv::RefuseBattleAt(ps) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

extern "C" __declspec(naked) void d_InvPossBattle() {
    __asm {
        push eax
        push edx
        push ecx
        mov eax, [esp + 12]
        cmp eax, [g_invCfibStart]
        jb pass
        cmp eax, [g_invCfibEnd]
        jae pass
        push dword ptr [esp + 20]  // anArea
        push dword ptr [esp + 4]   // aDefender (the saved ECX)
        push dword ptr [esp + 12]  // anAttacker (the saved EDX)
        push dword ptr [esp + 28]  // aPossession
        call CB_InvPossBattle
        test eax, eax
        jnz refuse
    pass:
        pop ecx
        pop edx
        pop eax
        jmp dword ptr [o_InvPossBattle]
    refuse:
        pop ecx
        pop edx
        pop eax
        xor eax, eax
        ret 8
    }
}

// The rebellion part of the routine (per possession): skipped in K's run (the host's own call ran it).
extern "C" __declspec(naked) void d_InvTail() {
    __asm {
        cmp byte ptr [g_invSkipTail], 0
        jne skip
        jmp dword ptr [o_InvTail]
    skip:
        jmp dword ptr [g_invTailJoin]
    }
}

// TEST ONLY: Random32Proc for the draws of CheckRebellionAndInvasions itself (Random's return address at [esp + 8]).
extern "C" __declspec(naked) void InvRand32() {
    __asm {
        mov eax, [esp + 8]
        cmp eax, [g_invRStart]
        jb orig
        cmp eax, [g_invREnd]
        jae orig
        mov eax, [g_invRndVal]
        ret
    orig:
        jmp dword ptr [g_invRndOrig]
    }
}

// 0.7.0 (P7): the computer's fleets and the joined nations' invasions. TBuilderNation.GetMostImportantArea(Self=EAX,
// var aValue=EDX, ForSubs=ECX): TMapArea (register-only) scores each sea area for the nation's fleet: its possession
// invaded by the player (Invaded = 0: +80, "is invaded by player"), its own invasion (Invaded = its index), and, for a
// nation at war with the player (Tension > 40), the area of the form's EnemyInvasionTarget (the computer's planned
// invasion of the player's possession: Random(15) + 6, the escort). For a computer nation J at war with a joined nation
// K the call sees K's invasions of J's possessions as the player's, and K's possession J's side plans to invade as the
// form's target, its Tension test passed for that branch only (a detour at the test: g_escortPass).
namespace inv {
int EscortsEnter(void* self);
void EscortsLeave();
}

extern "C" {
void* o_MostImportant = nullptr;
void* o_EscortTest = nullptr;  // the escort branch's `mov eax, [ebp-4]; cmp [eax+Tension], 40; jle`
uintptr_t g_escortJoin = 0;    // ... past it
volatile uint8_t g_escortPass = 0;
}

extern "C" __declspec(naked) void d_EscortTest() {
    __asm {
        cmp byte ptr [g_escortPass], 0
        jne pass
        jmp dword ptr [o_EscortTest]
    pass:
        jmp dword ptr [g_escortJoin]
    }
}

static int EscortsEnterGuarded(void* self) {
    __try {
        return inv::EscortsEnter(self);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

extern "C" void* __stdcall CB_MostImportant(void* self, void* valueRef, uint32_t forSubs) {
    int shown = EscortsEnterGuarded(self);
    void* r = nullptr;
    __try {
        r = (void*)(uintptr_t)dl::Call(o_MostImportant, (uint32_t)(uintptr_t)self, (uint32_t)(uintptr_t)valueRef, forSubs);
    } __finally {
        if (shown) inv::EscortsLeave();
    }
    return r;
}

extern "C" __declspec(naked) void d_MostImportant() {
    __asm {
        push ecx
        push edx
        push eax
        call CB_MostImportant
        ret
    }
}

namespace inv {
namespace {

bool HookAt(void* target, void* detour, void** orig, const char* name) {
    MH_STATUS st = MH_CreateHook(target, detour, orig);
    if (st == MH_OK) {
        st = MH_EnableHook(target);
        if (st != MH_OK) MH_RemoveHook(target);
    }
    if (st != MH_OK) {
        Log("invasions: %s: hook failed (%d)", name, (int)st);
        *orig = nullptr;
    }
    return st == MH_OK;
}

bool CodeIs(const uint8_t* p, const char* hex) {
    __try {
        for (size_t i = 0; hex[i * 2]; i++) {
            unsigned v = 0;
            sscanf_s(hex + i * 2, "%2x", &v);
            if (p[i] != (uint8_t)v) return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// `mov edx, <the routine's error text>` inside the call target: the except handler's message.
bool HasErrorText(uint8_t* fn, size_t len, const wchar_t* text) {
    __try {
        size_t n = wcslen(text);
        for (size_t i = 0; i + 5 < len; i++) {
            if (fn[i] != 0xBA) continue;
            const wchar_t* s = *(const wchar_t**)(fn + i + 1);
            MEMORY_BASIC_INFORMATION mi;
            if (!VirtualQuery(s, &mi, sizeof(mi)) || mi.State != MEM_COMMIT || (mi.Protect & PAGE_NOACCESS)) continue;
            if (wcsncmp(s, text, n) == 0) return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

int CallGuarded(void* fn, void* form) {
    __try {
        dl::Call(fn, (uint32_t)(uintptr_t)form);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

KState g_state[9];
}  // namespace

KState& State(int K) { return g_state[K >= 0 && K < 9 ? K : 0]; }

bool Install() {
    fName = dl::Field("TPossession", "Name");
    fValue = dl::Field("TPossession", "Value");
    fOwner = dl::Field("TPossession", "Owner");
    fInvaded = dl::Field("TPossession", "Invaded");
    fSupport = dl::Field("TPossession", "InvasionSupport");
    fITT = dl::Field("TPossession", "InvadedThisTurn");
    fTaken = dl::Field("TPossession", "TakenFrom");
    fBuilding = dl::Field("TPossession", "BuildingBase");
    fAreas = dl::Field("TfrmBuildCamp", "MapAreas");
    fAreaPoss = dl::Field("TMapArea", "Possessions");
    fAreaName = dl::Field("TMapArea", "Name");
    fTarget = dl::Field("TfrmBuildCamp", "InvasionTarget");
    fCount = dl::Field("TfrmBuildCamp", "InvasionCount");
    fEnemyTarget = dl::Field("TfrmBuildCamp", "EnemyInvasionTarget");
    fPending = dl::Field("TfrmBuildCamp", "InvasionBattlePending");
    fArmyOff = dl::Field("TfrmBuildCamp", "ArmyOffensive");
    fEnemyDecl = dl::Field("TfrmBuildCamp", "EnemyDeclineCount");
    fEVP = dl::Field("TfrmBuildCamp", "EnemyVP");
    fSAPoss = dl::Field("TfrmBuildCamp", "SurpriseAttackPossession");
    fMsgs = dl::Field("TfrmBuildCamp", "TurnMessages");
    fPlayer = dl::Field("TfrmBuildCamp", "PlayerNation");
    fSLItems = dl::Field("TStringList", "FList");
    fSLCount = dl::Field("TStringList", "FCount");
    mSLNew = dl::MethodFewestParams("TStringList", "Create");
    mObjFree = dl::Method("TObject", "Free");
    slVmt = dl::ClassVmt("TStringList");
    mModBudget = dl::Method("TBuilderNation", "GetModifiedBudget");
    int fields[] = {fName, fValue, fOwner, fInvaded, fSupport, fITT, fTaken, fBuilding, fAreas, fAreaPoss, fTarget,
                    fCount, fEnemyTarget, fPending, fArmyOff, fEnemyDecl, fEVP, fSAPoss, fMsgs, fPlayer, fSLItems,
                    fSLCount};
    bool fieldsOk = std::all_of(std::begin(fields), std::end(fields), [](int x) { return x >= 0; }) && mSLNew &&
                    mObjFree && slVmt && mModBudget;
    // The routine: DoStrategicTurn's call target whose except handler names it.
    uint8_t* st = (uint8_t*)dl::Method("TfrmBuildCamp", "DoStrategicTurn");
    if (st)
        for (uint8_t* t : dl::CallTargets(st, 0x1000))
            if (HasErrorText(t, 0x1a00, L"Error in CheckRebellionAndInvasions")) {
                g_resolve = t;
                break;
            }
    uint8_t* r = (uint8_t*)g_resolve;
    // checked against 1.01.44: the rebellion part's head, the loop's next possession, Random's call at the start
    bool code = r && CodeIs(r + 0x10e3, "8b068b786d") && CodeIs(r + 0x1920, "ff45f0ff4dcc") && CodeIs(r + 0x47, "e8");
    uint8_t* rnd = code ? r + 0x4c + *(int32_t*)(r + 0x48) : nullptr;
    if (rnd && CodeIs(rnd, "50ff15")) g_randCell = *(uint32_t**)(rnd + 3);
    // (its nested weather test, right before it: `push ebp; mov ebp, esp; push ebx; mov bl, 1`, counts as its own)
    g_invRStart = (uintptr_t)r - (r && CodeIs(r - 0x60, "558bec53b301") ? 0x60 : 0);
    g_invREnd = r ? (uintptr_t)r + 0x19ae : 0;
    g_invTailJoin = r ? (uintptr_t)r + 0x1920 : 0;
    g_ok = fieldsOk && code && g_randCell && HookAt(r, (void*)&d_InvResolve, &o_InvResolve, "CheckRebellionAndInvasions");
    if (g_ok && !HookAt(r + 0x10e3, (void*)&d_InvTail, &o_InvTail, "CheckRebellionAndInvasions (rebellions)")) {
        MH_DisableHook(r);
        MH_RemoveHook(r);
        o_InvResolve = nullptr;
        g_ok = false;
    }
    void* cfib = dl::Method("TfrmBuildCamp", "CheckForInvasionBattles");
    void* dpb = dl::Method("TfrmBuildCamp", "DoPossessionBattle");
    g_invCfibStart = (uintptr_t)cfib;
    g_invCfibEnd = cfib ? (uintptr_t)cfib + 0x4d0 : 0;  // its code (its string literals follow at +0x4d0)
    if (g_ok && !(cfib && dpb && HookAt(dpb, (void*)&d_InvPossBattle, &o_InvPossBattle, "DoPossessionBattle"))) {
        for (void* t : {(void*)r, (void*)(r + 0x10e3)}) {
            MH_DisableHook(t);
            MH_RemoveHook(t);
        }
        o_InvResolve = o_InvTail = nullptr;
        g_ok = false;
    }
    bool esc = g_ok && InstallEscorts();  // P7
    Log("Invasions for joined nations: %s (fields %s, routine %p%s; the computer's escorts %s)",
        g_ok ? "ok" : "UNAVAILABLE", fieldsOk ? "ok" : "missing", g_resolve, code ? "" : ", code differs",
        esc ? "ok" : "UNAVAILABLE");
    return g_ok;
}

bool Available() { return g_ok; }

std::string Describe(int K) {
    std::string s;
    if (!g_ok || !game::BuildCamp()) return s;
    ForEachPoss([&](void* ps) {
        int inv = I(ps, fInvaded), owner = OwnerIdx(ps);
        if (inv < 0 || (K >= 0 && inv != K && owner != K)) return;
        s += W2U(NameOf(ps)) + ":" + std::to_string(owner) + ":" + std::to_string(inv) + ":" +
             std::to_string(I(ps, fSupport)) + (dl::At<uint8_t>(ps, fITT) ? ":itt" : "") + ";";
    });
    return s;
}

bool HasWork(int K, const KState& st) {
    if (!st.Empty()) return true;
    bool any = false;
    ForEachPoss([&](void* ps) {
        int inv = I(ps, fInvaded), owner = OwnerIdx(ps);
        any |= inv >= 0 && ((inv == K && !Human(owner)) || (owner == K && !Human(inv)));
    });
    return any;
}

std::vector<std::wstring> MarkedThisTurn() {
    std::vector<std::wstring> out;
    if (g_ok)
        ForEachPoss([&](void* ps) {
            if (dl::At<uint8_t>(ps, fITT)) out.push_back(NameOf(ps));
        });
    return out;
}

int RestoreMarked(const std::vector<std::wstring>& names) {
    int n = 0;
    if (!g_ok || names.empty()) return 0;
    ForEachPoss([&](void* ps) {
        if (!dl::At<uint8_t>(ps, fITT) && std::find(names.begin(), names.end(), NameOf(ps)) != names.end()) {
            dl::At<uint8_t>(ps, fITT) = 1;
            n++;
        }
    });
    return n;
}

int TargetCost(void* n) {
    if (!g_ok || !n) return 0;
    int b = (int)dl::Call(mModBudget, (uint32_t)(uintptr_t)n);
    return b / 300 + 100;  // the game's imul 0x1b4e81b5 / sar 5: a division by 300 toward zero
}

int PossessionOwner(const std::wstring& name) {
    void* ps = g_ok ? FindPoss(name) : nullptr;
    return ps ? OwnerIdx(ps) : -2;
}
int PossessionValue(const std::wstring& name) {
    void* ps = g_ok ? FindPoss(name) : nullptr;
    return ps ? I(ps, fValue) : -2;
}
int PossessionInvader(const std::wstring& name) {
    void* ps = g_ok ? FindPoss(name) : nullptr;
    return ps ? I(ps, fInvaded) : -2;
}

std::string SetPossession(const std::wstring& name, int invader, int support, bool itt) {
    void* ps = g_ok ? FindPoss(name) : nullptr;
    if (!ps) return "no such possession";
    if (invader < -1 || invader > 8) return "bad nation";
    I(ps, fInvaded) = invader;
    I(ps, fSupport) = support;
    dl::At<uint8_t>(ps, fITT) = itt ? 1 : 0;
    return "";
}

void SetTestRandom(int mode) { g_testRnd = mode >= 0 && mode <= 2 ? mode : 0; }
int TestRandom() { return g_testRnd; }

namespace {
struct PS {
    void* ps;
    void* owner;
    int inv, taken, building;
    uint8_t itt;
    bool vis;
};

struct FormSave {
    void *target, *enemy, *sap, *msgs;
    int count, pending, armyOff, enemyDecl, evp;
};

std::vector<PS> g_ps;

// Everything between the view's enter and leave that can throw a C++ exception; the game call is guarded itself.
void RunInView(int K, const bool hv[9], KState& st, int armyOff, int enemyDecl, RunResult& r) {
    void* f = game::BuildCamp();
    auto sw = [K](int i) { return i == 0 ? K : i == K ? 0 : i; };
    std::map<std::wstring, void*> byName;
    g_ps.clear();
    ForEachPoss([&](void* ps) {
        PS s{ps, dl::At<void*>(ps, fOwner), I(ps, fInvaded), I(ps, fTaken), I(ps, fBuilding), dl::At<uint8_t>(ps, fITT),
             false};
        int vi = s.inv >= 0 ? sw(s.inv) : -1, ov = OwnerIdx(ps);  // view indices (K's record is index 0 now)
        s.vis = vi >= 0 && ((vi == 0 && ov > 0 && !hv[ov]) || (ov == 0 && vi > 0 && !hv[vi]));
        I(ps, fInvaded) = s.vis ? vi : -1;
        if (s.taken >= 0) I(ps, fTaken) = sw(s.taken);
        I(ps, fBuilding) = 0;
        dl::At<uint8_t>(ps, fITT) = 0;
        byName[NameOf(ps)] = ps;
        g_ps.push_back(s);
    });
    for (auto& n : st.itt) {
        auto it = byName.find(n);
        if (it != byName.end() && I(it->second, fInvaded) >= 0) dl::At<uint8_t>(it->second, fITT) = 1;
    }
    FormSave fs{dl::At<void*>(f, fTarget), dl::At<void*>(f, fEnemyTarget), dl::At<void*>(f, fSAPoss),
                dl::At<void*>(f, fMsgs),   I(f, fCount),                    I(f, fPending),
                I(f, fArmyOff),            I(f, fEnemyDecl),                I(f, fEVP)};
    auto find = [&](const std::wstring& n) -> void* {
        auto it = byName.find(n);
        return n.empty() || it == byName.end() ? nullptr : it->second;
    };
    void* msgs = (void*)(uintptr_t)dl::Call(mSLNew, (uint32_t)(uintptr_t)slVmt, 1);
    dl::At<void*>(f, fTarget) = find(st.target);
    dl::At<void*>(f, fEnemyTarget) = find(st.enemyTarget);
    dl::At<void*>(f, fSAPoss) = nullptr;
    dl::At<void*>(f, fMsgs) = msgs;
    I(f, fCount) = st.count;
    int pv = st.pending >= 0 ? sw(st.pending) : -1;
    I(f, fPending) = pv >= 0 && (pv == 0 || !hv[pv]) ? pv : -1;
    I(f, fArmyOff) = armyOff;
    I(f, fEnemyDecl) = enemyDecl;
    I(f, fEVP) = 0;
    uint32_t randKeep = 0;
    if (g_testRnd) {
        randKeep = *g_randCell;
        g_invRndOrig = randKeep;
        g_invRndVal = g_testRnd == 1 ? 0u : 0xFFFFFFFFu;
        *g_randCell = (uint32_t)(uintptr_t)&InvRand32;
    }
    g_invSkipTail = 1;
    game::SetBoxCapture(true);
    const bool pics = lockstep::BeginTextCapture();
    int ok = CallGuarded(o_InvResolve, f);
    std::vector<std::wstring> texts = pics ? lockstep::EndTextCapture() : std::vector<std::wstring>();
    std::wstring box = game::SetBoxCapture(false);
    g_invSkipTail = 0;
    if (g_testRnd) *g_randCell = randKeep;
    if (!ok) r.error = "exception in the game's routine";
    if (!box.empty()) r.error = "the game's routine failed: " + W2U(box);
    // K's plan back (real indices)
    st.target = NameOf(dl::At<void*>(f, fTarget));
    st.enemyTarget = NameOf(dl::At<void*>(f, fEnemyTarget));
    st.count = I(f, fCount);
    int p = I(f, fPending);
    st.pending = p >= 0 ? sw(p) : -1;
    r.enemyVP = I(f, fEVP);
    std::vector<std::wstring> tm = ListLines(msgs);
    dl::At<void*>(f, fTarget) = fs.target;
    dl::At<void*>(f, fEnemyTarget) = fs.enemy;
    dl::At<void*>(f, fSAPoss) = fs.sap;
    dl::At<void*>(f, fMsgs) = fs.msgs;
    I(f, fCount) = fs.count;
    I(f, fPending) = fs.pending;
    I(f, fArmyOff) = fs.armyOff;
    I(f, fEnemyDecl) = fs.enemyDecl;
    I(f, fEVP) = fs.evp;
    dl::Call(mObjFree, (uint32_t)(uintptr_t)msgs);
    for (auto& t : texts) r.news.push_back(t);
    for (auto& t : tm)
        if (std::find(r.news.begin(), r.news.end(), t) == r.news.end()) r.news.push_back(t);
    // the possessions back in the real numbering; K's marks of this month into its plan
    st.itt.clear();
    int kinv = 0;
    for (auto& s : g_ps) {
        int cur = I(s.ps, fInvaded);
        if (s.vis || cur != -1) {
            if (!s.vis && s.inv != -1)
                Log("invasions: nation %d's month started an invasion of %s, where nation %d's was hidden: it ends", K,
                    W2U(NameOf(s.ps)).c_str(), s.inv);
            I(s.ps, fInvaded) = cur >= 0 ? sw(cur) : -1;
            if (cur >= 0) {
                kinv++;
                if (dl::At<uint8_t>(s.ps, fITT)) st.itt.push_back(NameOf(s.ps));
            }
        } else {
            I(s.ps, fInvaded) = s.inv;
        }
        int t = I(s.ps, fTaken);
        I(s.ps, fTaken) = t >= 0 ? sw(t) : t;
        I(s.ps, fBuilding) = s.building;
        dl::At<uint8_t>(s.ps, fITT) = s.itt;
        void* now = dl::At<void*>(s.ps, fOwner);
        if (now != s.owner) {
            r.owners++;
            Log("invasions: nation %d's month: %s passes from nation %d to nation %d", K, W2U(NameOf(s.ps)).c_str(),
                s.owner ? sw(game::NationIndex(s.owner)) : -1, now ? sw(game::NationIndex(now)) : -1);
        }
    }
    r.summary = std::to_string(kinv) + " invasion(s) after it, plan " + W2U(st.target.empty() ? L"-" : st.target) +
                "/" + std::to_string(st.count) + ", enemy plan " + W2U(st.enemyTarget.empty() ? L"-" : st.enemyTarget) +
                "/" + std::to_string(st.pending) + ", " + std::to_string(st.itt.size()) + " mark(s), " +
                std::to_string(r.news.size()) + " message(s), owners changed " + std::to_string(r.owners) +
                ", EnemyVP +" + std::to_string(r.enemyVP) + (g_testRnd ? g_testRnd == 1 ? ", TEST: draws low" : ", TEST: draws high" : "");
    r.ran = true;
}

int RunInViewGuarded(int K, const bool hv[9], KState& st, int armyOff, int enemyDecl, RunResult& r) {
    try {
        RunInView(K, hv, st, armyOff, enemyDecl, r);
        return 1;
    } catch (...) {
        return 0;
    }
}

// K's view left on every path (a game exception the routine's own handler did not take included).
int RunInViewSEH(int K, const bool* hv, KState* st, int armyOff, int enemyDecl, RunResult* r) {
    int ok = 0;
    __try {
        ok = RunInViewGuarded(K, hv, *st, armyOff, enemyDecl, *r);
    } __finally {
        g_kRun = false;
        g_invSkipTail = 0;
        game::HostViewLeave();
    }
    return ok;
}
}  // namespace

RunResult RunForNation(int K, int war, const bool human[9], KState& st, int armyOff, int enemyDecl) {
    RunResult r;
    if (!g_ok || K < 1 || K > 8 || g_kRun || g_hideDepth > 0) {
        r.error = "unavailable";
        return r;
    }
    if (!game::HostViewEnter(K, war, human)) {
        r.error = "K's view is not available here";
        return r;
    }
    bool hv[9];
    for (int v = 0; v < 9; v++) hv[v] = human[v == 0 ? K : v == K ? 0 : v];
    hv[0] = false;  // K itself
    g_kRun = true;
    int ok = RunInViewSEH(K, hv, &st, armyOff, enemyDecl, &r);
    if (!ok && r.error.empty()) r.error = "exception in the mod";
    return r;
}

std::string ClientSetForm(const KState& st, bool clear) {
    void* f = game::BuildCamp();
    if (!g_ok || !f) return "unavailable";
    void* t = clear ? nullptr : FindPoss(st.target);
    void* e = clear ? nullptr : FindPoss(st.enemyTarget);
    int me = game::NationIndex(game::PlayerNation());
    std::string was = W2U(NameOf(dl::At<void*>(f, fTarget)));
    dl::At<void*>(f, fTarget) = t;
    dl::At<void*>(f, fEnemyTarget) = e;
    I(f, fCount) = clear ? 0 : st.count;
    I(f, fPending) = clear || st.pending < 0 ? -1 : st.pending == me ? 0 : st.pending;
    return "target " + (t ? W2U(NameOf(t)) : std::string("-")) + " (the loaded save's: " + (was.empty() ? "-" : was) +
           "), enemy target " + (e ? W2U(NameOf(e)) : std::string("-")) + ", months " + std::to_string(I(f, fCount)) +
           ", pending " + std::to_string(I(f, fPending));
}

std::wstring FormTarget() {
    void* f = game::BuildCamp();
    return g_ok && f ? NameOf(dl::At<void*>(f, fTarget)) : std::wstring();
}

// --- P3: a joined nation's invasions in its battle slot and back ---
namespace {
PossBattle g_lastPB;
volatile long g_pbCalls = 0;
std::vector<std::pair<long, PossBattle>> g_pbList;  // the latest calls (their number, the call)

// A K invasion: exactly one side is K (the invader, or the owner of the invaded possession), the other a computer
// nation's (`human`: this game's indices of the nations people play).
template <class H>
bool KSides(int K, int invader, int owner, H human) {
    return invader >= 0 && ((invader == K && owner != K && !human(owner)) || (owner == K && invader != K && !human(invader)));
}

const PossState* FindIn(const std::vector<PossState>& v, const std::wstring& n) {
    for (auto& p : v)
        if (p.name == n) return &p;
    return nullptr;
}
}  // namespace

void NotePossBattle(void* ps, void* attacker, void* defender, void* area) {
    if (!g_ok) return;
    PossBattle b;
    b.attacker = attacker ? game::NationIndex(attacker) : -1;
    b.defender = defender ? game::NationIndex(defender) : -1;
    b.poss = NameOf(ps);
    if (area && fAreaName >= 0) b.area = dl::ReadUStr(dl::At<void*>(area, fAreaName));
    g_lastPB = b;
    long n = InterlockedIncrement(&g_pbCalls);
    try {
        if (g_pbList.size() >= 32) g_pbList.erase(g_pbList.begin());
        g_pbList.push_back({n, b});
    } catch (...) {
    }
}

long PossessionBattles(PossBattle* last) {
    if (last) *last = g_lastPB;
    return g_pbCalls;
}

std::string PossessionBattlesText(long since) {
    std::string s;
    for (auto& e : g_pbList)
        if (e.first > since)
            s += (s.empty() ? "" : ", ") + W2U(e.second.poss) + " (" + std::to_string(e.second.attacker) + " attacks " +
                 std::to_string(e.second.defender) + ")";
    return s;
}

std::vector<PossState> KPossessions(int K) {
    std::vector<PossState> out;
    if (!g_ok || K < 1 || K > 8 || !game::BuildCamp()) return out;
    ForEachPoss([&](void* ps) {
        int v = I(ps, fInvaded), owner = OwnerIdx(ps);
        if (KSides(K, v, owner, Human)) out.push_back({NameOf(ps), v, I(ps, fSupport)});
    });
    return out;
}

std::string EncodePoss(const std::vector<PossState>& v) {
    std::string o;
    for (auto& p : v) o += W2U(p.name) + "\x1f" + std::to_string(p.invader) + "\x1f" + std::to_string(p.support) + "\x1e";
    return o;
}

std::vector<PossState> DecodePoss(const std::string& s) {
    std::vector<PossState> out;
    size_t a = 0;
    while (a < s.size()) {
        size_t e = s.find('\x1e', a);
        if (e == std::string::npos) e = s.size();
        std::string rec = s.substr(a, e - a);
        size_t c1 = rec.find('\x1f'), c2 = c1 == std::string::npos ? c1 : rec.find('\x1f', c1 + 1);
        if (c2 != std::string::npos && c1 > 0) {
            PossState p;
            p.name = U2W(rec.substr(0, c1));
            p.invader = atoi(rec.c_str() + c1 + 1);
            p.support = atoi(rec.c_str() + c2 + 1);
            if (p.invader >= 0 && p.invader <= 8) out.push_back(p);
        }
        a = e + 1;
    }
    return out;
}

std::string SlotPut(int K, const KState& st, const std::vector<PossState>& ps) {
    void* f = game::BuildCamp();
    if (!g_ok || !f || K < 1 || K > 8) return "unavailable";
    auto sw = [K](int i) { return i == 0 ? K : i == K ? 0 : i; };
    std::map<std::wstring, void*> byName;
    int cleared = 0, set = 0, bad = 0;
    ForEachPoss([&](void* p) {
        if (I(p, fInvaded) != -1) cleared++;
        I(p, fInvaded) = -1;
        I(p, fSupport) = 0;
        dl::At<uint8_t>(p, fITT) = 0;
        byName[NameOf(p)] = p;
    });
    auto find = [&](const std::wstring& n) -> void* {
        auto it = byName.find(n);
        return n.empty() || it == byName.end() ? nullptr : it->second;
    };
    std::string list;
    for (auto& s : ps) {
        void* p = find(s.name);
        int v = sw(s.invader), owner = p ? OwnerIdx(p) : -1;
        if (!p || (v == 0) == (owner == 0)) {  // not K's invasion in this slot
            bad++;
            continue;
        }
        I(p, fInvaded) = v;
        I(p, fSupport) = s.support;
        bool itt = std::find(st.itt.begin(), st.itt.end(), s.name) != st.itt.end();
        dl::At<uint8_t>(p, fITT) = itt ? 1 : 0;
        list += (list.empty() ? "" : ", ") + W2U(s.name) + " " + std::to_string(s.invader) + "/" +
                std::to_string(s.support) + (itt ? "/itt" : "");
        set++;
    }
    dl::At<void*>(f, fTarget) = find(st.target);
    dl::At<void*>(f, fEnemyTarget) = find(st.enemyTarget);
    I(f, fCount) = st.count;
    I(f, fPending) = st.pending >= 0 && st.pending <= 8 ? sw(st.pending) : -1;
    return std::to_string(cleared) + " invasion(s) of the save cleared, " + std::to_string(set) + " of the nation's set" +
           (list.empty() ? "" : " (" + list + ")") + (bad ? ", " + std::to_string(bad) + " not taken" : "") + "; plan " +
           W2U(st.target.empty() ? L"-" : st.target) + "/" + std::to_string(st.count) + ", enemy plan " +
           W2U(st.enemyTarget.empty() ? L"-" : st.enemyTarget) + "/" + std::to_string(st.pending);
}

void SlotRead(int K, KState& st, std::vector<PossState>& ps) {
    st = KState();
    ps.clear();
    void* f = game::BuildCamp();
    if (!g_ok || !f || K < 1 || K > 8) return;
    auto sw = [K](int i) { return i == 0 ? K : i == K ? 0 : i; };
    ForEachPoss([&](void* p) {
        int v = I(p, fInvaded), owner = OwnerIdx(p);
        if (v < 0 || (v == 0) == (owner == 0)) return;  // (SlotPut left only K's: K is nation 0 here)
        ps.push_back({NameOf(p), sw(v), I(p, fSupport)});
        if (dl::At<uint8_t>(p, fITT)) st.itt.push_back(NameOf(p));
    });
    st.target = NameOf(dl::At<void*>(f, fTarget));
    st.enemyTarget = NameOf(dl::At<void*>(f, fEnemyTarget));
    st.count = I(f, fCount);
    int p = I(f, fPending);
    st.pending = p >= 0 && p <= 8 ? sw(p) : -1;
}

std::string ApplyPossessions(int K, const std::vector<PossState>& base, const std::vector<PossState>& now) {
    if (!g_ok || K < 1 || K > 8) return "unavailable";
    std::vector<std::wstring> names;
    for (auto* v : {&base, &now})
        for (auto& p : *v)
            if (std::find(names.begin(), names.end(), p.name) == names.end()) names.push_back(p.name);
    std::string s;
    for (auto& n : names) {
        const PossState* b = FindIn(base, n);
        const PossState* a = FindIn(now, n);
        if (b && a && b->invader == a->invader && b->support == a->support) continue;
        void* ps = FindPoss(n);
        std::string what = W2U(n) + " ";
        if (!ps) {
            s += what + "(no such possession); ";
            continue;
        }
        int owner = OwnerIdx(ps), cur = I(ps, fInvaded);
        if (a && !KSides(K, a->invader, owner, Human)) {
            s += what + "left (owner " + std::to_string(owner) + ", invader " + std::to_string(a->invader) +
                 ": not the nation's invasion here); ";
            continue;
        }
        if (cur != -1 && !(b && cur == b->invader) && !(a && cur == a->invader)) {
            s += what + "left (nation " + std::to_string(cur) + "'s invasion there now); ";
            continue;
        }
        if (a) {
            I(ps, fInvaded) = a->invader;
            I(ps, fSupport) = a->support;
            s += what + (b ? "invader " + std::to_string(b->invader) + " support " + std::to_string(b->support) + " -> "
                           : std::string("new: invader ")) +
                 std::to_string(a->invader) + " support " + std::to_string(a->support) + "; ";
        } else if (cur != -1) {
            I(ps, fInvaded) = -1;
            I(ps, fSupport) = 0;
            s += what + "invasion over; ";
        }
    }
    return s;
}

// --- P7: the computer's fleets and the joined nations' invasions (see d_MostImportant) ---
namespace {
struct EscSave {
    void* ps;
    int inv;
};
std::vector<EscSave> g_esc;
void* g_escTargetWas = nullptr;
bool g_escTarget = false;
int g_escDepth = 0;
int fTension = -1, fAIPoints = -1;
bool g_escOk = false;
}  // namespace

int EscortsEnter(void* self) {
    if (!g_escOk || g_escDepth > 0 || g_kRun || g_hideDepth > 0 || game::EventActive()) return 0;
    void* f = game::BuildCamp();
    int J = self ? game::NationIndex(self) : -1;
    if (!f || J < 1 || J > 8 || Human(J)) return 0;
    std::vector<int> ks;  // the joined nations at war with J (hookcb::PlayerRulesNation: the host only)
    for (int K = 1; K <= 8; K++)
        if (K != J && hookcb::PlayerRulesNation(K) && game::AtWar(J, K)) ks.push_back(K);
    if (ks.empty()) return 0;
    g_esc.clear();
    g_escTarget = false;
    ForEachPoss([&](void* ps) {  // J's possessions a joined nation at war with J invades: the player's invasion
        int v = I(ps, fInvaded);
        if (v >= 1 && OwnerIdx(ps) == J && std::find(ks.begin(), ks.end(), v) != ks.end()) {
            g_esc.push_back({ps, v});
            I(ps, fInvaded) = 0;
        }
    });
    void* host = dl::At<void*>(f, fEnemyTarget);
    if (!(host && I(self, fTension) > 40))  // (the host's own planned invasion counts for J at war with the host)
        for (int K : ks) {
            void* t = FindPoss(State(K).enemyTarget);
            if (!t || OwnerIdx(t) != K) continue;
            g_escTargetWas = host;
            dl::At<void*>(f, fEnemyTarget) = t;
            g_escTarget = true;
            g_escortPass = 1;
            break;
        }
    int n = (int)g_esc.size() + (g_escTarget ? 1 : 0);
    if (n) {
        g_escDepth = 1;
        auto& l = g_hideLog["GetMostImportantArea: joined nations' invasions shown"];
        l.first++;
        l.second += n;
    }
    return n;
}

void EscortsLeave() {
    if (g_escDepth <= 0) return;
    g_escDepth = 0;
    for (auto& e : g_esc)
        if (I(e.ps, fInvaded) == 0) I(e.ps, fInvaded) = e.inv;
    g_esc.clear();
    if (g_escTarget) {
        if (void* f = game::BuildCamp()) dl::At<void*>(f, fEnemyTarget) = g_escTargetWas;
        g_escTarget = false;
    }
    g_escortPass = 0;
}

bool InstallEscorts() {
    fTension = dl::Field("TBuilderNation", "Tension");
    fAIPoints = dl::Field("TMapArea", "AIPriorityPoints");
    uint8_t* gm = (uint8_t*)dl::Method("TBuilderNation", "GetMostImportantArea");
    if (!g_ok || !gm || fTension <= 0 || fEnemyTarget <= 0) return false;
    // cmp dword ptr [eax+EnemyInvasionTarget], 0; je ...; mov eax, [ebp-4]; cmp dword ptr [eax+Tension], 40; jle ...
    std::vector<int> pat = {0x83, 0xB8};
    for (int i = 0; i < 4; i++) pat.push_back((fEnemyTarget >> (8 * i)) & 0xFF);
    for (int b : {0x00, 0x0F, 0x84, -1, -1, -1, -1, 0x8B, 0x45, 0xFC, 0x83, 0xB8}) pat.push_back(b);
    for (int i = 0; i < 4; i++) pat.push_back((fTension >> (8 * i)) & 0xFF);
    for (int b : {0x28, 0x0F, 0x8E}) pat.push_back(b);
    uint8_t* hit = dl::FindPattern(gm, 0x1800, pat);
    uint8_t* site = hit ? hit + 13 : nullptr;
    if (!site || !HookAt(site, (void*)&d_EscortTest, &o_EscortTest, "GetMostImportantArea (escort test)")) {
        Log("invasions: the computer's escorts for the joined nations' invasions: UNAVAILABLE (pattern %s)",
            hit ? "found" : "not found");
        return false;
    }
    g_escortJoin = (uintptr_t)site + 16;
    if (!HookAt(gm, (void*)&d_MostImportant, &o_MostImportant, "GetMostImportantArea")) {
        MH_DisableHook(site);
        MH_RemoveHook(site);
        o_EscortTest = nullptr;
        return false;
    }
    g_escOk = true;
    return true;
}

namespace {
// RandSeed, from the default Random32Proc (`imul r32, [RandSeed], 0x08088405`) the game's Random cell points to.
uint32_t* RandSeedCell() {
    static uint32_t* seed = nullptr;
    static bool tried = false;
    if (tried) return seed;
    tried = true;
    uint8_t* p = g_randCell ? (uint8_t*)(uintptr_t)*g_randCell : nullptr;
    __try {
        for (int i = 0; p && i < 0x30; i++)
            if (p[i] == 0x69 && (p[i + 1] & 0xC7) == 0x05 && *(uint32_t*)(p + i + 6) == 0x08088405u) {
                seed = *(uint32_t**)(p + i + 2);
                break;
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        seed = nullptr;
    }
    return seed;
}
}  // namespace

std::string EscortTest(int J) {
    void* f = game::BuildCamp();
    void* n = J >= 1 && J <= 8 ? game::Nation(J) : nullptr;
    if (!g_escOk || !f || !n || fAIPoints <= 0) return "unavailable";
    void* entry = dl::Method("TBuilderNation", "GetMostImportantArea");
    uint32_t* seed = RandSeedCell();  // both calls with the same draws: the differences are the shown invasions'
    int v1 = 0, v2 = 0;
    // which areas the joined nations' invasions concern for J (their possessions' areas)
    std::vector<void*> areas;
    void* fa = dl::At<void*>(f, fAreas);
    for (int a = 0; fa && a < dl::ListCount(fa); a++) {
        void* area = dl::ListItem(fa, a);
        void* poss = area ? dl::At<void*>(area, fAreaPoss) : nullptr;
        for (int p = 0; poss && p < dl::ListCount(poss); p++) {
            void* ps = dl::ListItem(poss, p);
            int v = ps ? I(ps, fInvaded) : -1, o = ps ? OwnerIdx(ps) : -1;
            bool kInv = v >= 1 && o == J && hookcb::PlayerRulesNation(v);
            bool kTarget = false;
            for (int K = 1; K <= 8; K++)
                if (ps && hookcb::PlayerRulesNation(K) && NameOf(ps) == State(K).enemyTarget && o == K) kTarget = true;
            if ((kInv || kTarget) && std::find(areas.begin(), areas.end(), area) == areas.end()) areas.push_back(area);
        }
    }
    auto pts = [&]() {
        std::string s;
        for (void* a : areas)
            s += (s.empty() ? "" : ",") + W2U(dl::ReadUStr(dl::At<void*>(a, fAreaName))) + ":" + std::to_string(I(a, fAIPoints));
        return s;
    };
    auto name = [&](void* a) { return a ? W2U(dl::ReadUStr(dl::At<void*>(a, fAreaName))) : std::string("-"); };
    uint32_t s0 = seed ? *seed : 0;
    void* without = (void*)(uintptr_t)dl::Call(o_MostImportant, (uint32_t)(uintptr_t)n, (uint32_t)(uintptr_t)&v1, 0);
    std::string p1 = pts();
    long calls = g_hideLog["GetMostImportantArea: joined nations' invasions shown"].first;
    if (seed) *seed = s0;
    void* with = (void*)(uintptr_t)dl::Call(entry, (uint32_t)(uintptr_t)n, (uint32_t)(uintptr_t)&v2, 0);
    std::string p2 = pts();
    long shown = g_hideLog["GetMostImportantArea: joined nations' invasions shown"].first - calls;
    return "without=" + name(without) + " with=" + name(with) + " shown=" + std::to_string(shown) + " points(without)=" +
           p1 + " points(with)=" + p2 + (seed ? " draws=same" : " draws=own");
}

std::string EncodeStates(const KState st[9]) {
    std::string o;
    for (int K = 1; K <= 8; K++) {
        const KState& s = st[K];
        if (s.Empty()) continue;
        std::string itt;
        for (auto& n : s.itt) itt += (itt.empty() ? "" : "\x1d") + W2U(n);
        o += std::to_string(K) + "\x1f" + W2U(s.target) + "\x1f" + std::to_string(s.count) + "\x1f" + W2U(s.enemyTarget) +
             "\x1f" + std::to_string(s.pending) + "\x1f" + itt + "\x1e";
    }
    return o;
}

void DecodeStates(const std::string& s, KState st[9]) {
    for (int K = 0; K < 9; K++) st[K] = KState();
    size_t a = 0;
    while (a < s.size()) {
        size_t e = s.find('\x1e', a);
        if (e == std::string::npos) e = s.size();
        std::vector<std::string> f;
        size_t b = a;
        while (b <= e) {
            size_t c = s.find('\x1f', b);
            if (c == std::string::npos || c > e) c = e;
            f.push_back(s.substr(b, c - b));
            b = c + 1;
        }
        int K = f.size() == 6 ? atoi(f[0].c_str()) : 0;
        if (K >= 1 && K <= 8) {
            KState& k = st[K];
            k.target = U2W(f[1]);
            k.count = atoi(f[2].c_str());
            k.enemyTarget = U2W(f[3]);
            k.pending = atoi(f[4].c_str());
            size_t p = 0;
            while (p < f[5].size()) {
                size_t q = f[5].find('\x1d', p);
                if (q == std::string::npos) q = f[5].size();
                if (q > p) k.itt.push_back(U2W(f[5].substr(p, q - p)));
                p = q + 1;
            }
        }
        a = e + 1;
    }
}

}  // namespace inv

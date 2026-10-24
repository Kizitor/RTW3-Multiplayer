// Relations v2, B5: the player's war effects for each joined player's nation K under the player's rules, on the host
// during the month (blockades).
//
// The game decides blockades for the player's nation (nation 0, the host's) only. CheckBlockadeStatus (the month's:
// DoStrategicTurn; the load's: LoadBuildCampaign) clears every Blockaded flag and, while the host is at war (War > 0), for
// each nation i at war with nation 0: nation 0 blockaded (IsBlockaded: one enemy alone above the minimum and 1.1 x its
// own strength in its build or blockade area) -> EnemyVP += 20 * (Random(3) + FleetSize) for each such enemy; nation i
// blockaded -> its flag, nation 0's VP += 20 * (Random(3) + FleetSize). CheckUnrestChanges then calls DoBlockadeEffects
// first in each nation's turn of its loop: a flagged nation gives x = 20 * (Random(3) + FS) + 10 * (Random(2) + FS) (the
// player blockaded: EnemyVP, "The enemy gains x VP for blockading us"; an enemy blockaded: the game adds it to that
// enemy's own VP, "Gain x VP for blockade of enemy"), and unrest: Random(100) < War / 2 + 1 and Random(40) <
// BudgetModifier -> UnrestLevel + 1, above 5 a 50% "The blockade is causing hardship and food shortages in <nation>!"
// picture. The loop's own rule for a flagged nation: Random(100) < 5 or (Random(100) < War / 2 + 12 and UnrestLevel < 7)
// -> UnrestLevel + 1, at 7 the picture "The blockade is causing food shortages and privations in <nation>!", else the
// turn message "Shortages due to the blockade are causing hardship for our people!". StopBlockadedMoves (DoTurn2, with
// the flags of the last check) stops the moves out of a flagged nation's build area while the host is at war. The flag
// also stops productivity growth and cuts shipyard capacity, fuel and raiding (the game reads it for every nation).
//
// Here each K under the player's rules at war (its own war counter > 0) gets the same after each of the game's checks
// (the load's: the flags only): K blockaded by any enemy -> its flag, each computer enemy 20 * (Random(3) + FS); each
// enemy j blockaded -> its flag, K 20 * (Random(3) + FS). The points go to the nation's VP and to the pair's war score
// (hookcb::CreditWarVP: the B4 ledger, or a war between two players' score). The host's wars stay the game's (its own
// check covers them); another player's nation under the player's rules credits its own gains, so nothing counts twice.
// DoBlockadeEffects of K is the player's: x to the enemies that blockade K on their own (split; the host's share to its
// VP), unrest by K's war counter, K's messages. Of a computer enemy flagged by these checks only: x to the nations at war
// with it under the player's rules (the message's meaning), unrest by their war counter. Of an enemy the host's check
// flagged too: the game's own, and each such K its own x. The loop's rule for these nations runs here with the war
// counter of K (their flag reads 0 for the rest of the nation's turn of the loop). StopBlockadedMoves runs with War 1
// while the host is at peace and such a flag is set. The messages go to K's player (the turn report: `econnews`), the
// flags to every player's screen (`kblk`).
#include "warfx.h"
#include "delphi.h"
#include "game.h"
#include "lockstep.h"
#include "../../third_party/minhook/include/MinHook.h"
#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace warfx {
namespace {

int fBlockaded = -1, fBM = -1, fUnrest = -1, fTension = -1, fShips = -1, fDest = -1;
void* mIsBlockaded = nullptr;
void* mRandom = nullptr;
int* fsCell = nullptr;
uintptr_t retEffects = 0;  // DoBlockadeEffects' call in CheckUnrestChanges: its return address
bool g_on = false;

int g_testBlk[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};  // TEST ONLY (bridge `warfxblk`)
int g_testRand = -1;  // TEST ONLY (bridge `warfxrand`): the war effects' Random(n) draws min(v, n - 1)

struct Frame {
    int K = 0, kwar = 0;
    bool hostEnemy = false;     // at war with the host's nation too
    bool blockaded = false;
    std::vector<int> enemies;   // the nations 1..8 at war with K
    std::vector<int> blockers;  // the enemies (0: the host's nation) that blockade K on their own
};
std::map<int, Frame> g_frames;  // the last check's
bool g_frameFlag[9] = {};       // set by a frame
bool g_hostFlag[9] = {};        // set by the game's own check
bool g_month = false;           // the month's check ran: CheckUnrestChanges' DoBlockadeEffects calls are the frames'
int g_cleared = -1;             // the nation whose flag reads 0 for the rest of its turn of the loop
std::map<int, std::vector<std::wstring>> g_news;
std::map<int, std::string> g_rec;  // K -> the last month's record
std::string g_lastCheck, g_lastStop;
int g_viewMissLogged = 0;  // log lines of blockade tests that ran on the host's frame (capped)

// War weariness: CheckUnrestChanges' rule for every nation, hooked at its `call` of the War > 0 test (warfx_dWeary).
int fGov = -1;
bool g_wearyOn = false;
std::wstring g_wearyText;            // the game's own "Our people is getting dissatisfied with the bad war situation!"
std::map<int, std::string> g_weary;  // K -> the last month's record
std::string g_wearyGame;             // the nations whose turn of the loop ran the game's own rule (the last call)

// Building for war (B5 (e)): the War > 0 tests of the build rules every nation shares, per joined nation by its own counter.
bool g_buildOn = false;
struct BuildRec {
    int tests[3] = {}, war[3] = {};  // sites: 0 submarines (AdvanceShipConstruction), 1 delays, 2 war budget (growth)
    int grants = 0;
};
std::map<int, BuildRec> g_build;  // joined nation -> this month's answers
int g_buildLogs = 0;              // log lines of answers that differ from the host's (capped)
int g_delSaved = 0;               // the host's War while CheckForConstructionDelays runs for a joined nation
std::wstring g_txtBudget, g_txtEmergency;  // HandleEconomicGrowth's texts (War even / odd)
const char* const kBuildSite[3] = {"submarine construction", "construction delays", "war budget"};

bool InImage(const void* p) {
    uintptr_t a = (uintptr_t)p;
    return a >= dl::g_base && a < dl::g_base + dl::g_size;
}
uint8_t* CallTo(uint8_t* p) { return p + 5 + *(int32_t*)(p + 1); }

void* Rec(int i) { return i >= 0 && i <= 8 && i < game::NationCount() ? game::Nation(i) : nullptr; }
int IndexOf(void* rec) {
    for (int i = 0; i <= 8 && i < game::NationCount(); i++)
        if (game::Nation(i) == rec) return i;
    return -1;
}
uint8_t& FlagOf(void* rec) { return dl::At<uint8_t>(rec, fBlockaded); }
int Rnd(int n) { return g_testRand >= 0 ? std::min(g_testRand, n - 1) : (int)dl::Call(mRandom, (uint32_t)n); }
int FS() { return fsCell ? *fsCell : 0; }
std::wstring Num(int v) { return std::to_wstring(v); }

int CallIsBlockaded(void* rec) {  // 1 / 0, -1 on a fault
    __try {
        return (dl::Call(mIsBlockaded, (uint32_t)(uintptr_t)rec, (uint32_t)(uintptr_t)rec) & 0xFF) != 0 ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool Blockaded(int n) {
    if (n >= 0 && n <= 8 && g_testBlk[n] >= 0) return g_testBlk[n] != 0;
    void* r = Rec(n);
    return r && CallIsBlockaded(r) == 1;
}

// K blockaded by enemy e on its own: the game's test with K's other wars (its relation cells) read as peace.
bool BlockedBy(int K, int e, const std::vector<int>& all) {
    if (g_testBlk[K] >= 0) return g_testBlk[K] != 0;
    void* r = Rec(K);
    if (!r) return false;
    std::vector<std::pair<int, int>> saved;
    for (int w : all)
        if (w != e) {
            int& c = dl::At<int>(r, fTension + 4 * w);
            saved.push_back({w, c});
            c = 40;
        }
    int b = CallIsBlockaded(r);
    for (auto& s : saved) dl::At<int>(r, fTension + 4 * s.first) = s.second;
    return b == 1;
}

// The blockade tests of K's frame in K's view (game::HostViewEnter with no nation masked: K's wars with the people's
// nations count too). The game's GetBlockadeStrength counts the allies from its player's side: against the player the
// coalition of the player's enemies, for the player its own allies at war; on the host's frame those were the host's
// alliances, here they are K's. `rr`: the real records (Nation(i) answers view indices in the view); `all`: K's enemies
// (real indices, 0 = the host's nation). Plain arrays only while the view is in place (the game calls catch their own
// exceptions). False (nothing tested) when the view is not available: the host's frame then.
struct ViewTests {
    int blockaded = 0;  // K blockaded (by any enemy)
    int by[9] = {};     // K blockaded by enemy e on its own (the other enemies' relation with K read as peace)
    int blk[9] = {};    // enemy j blockaded
};
bool TestsInView(int K, int kwar, void* const rr[9], const int* all, int na, ViewTests& t) {
    bool none[9] = {};
    if (!game::HostViewEnter(K, kwar, none)) return false;
    t.blockaded = g_testBlk[K] >= 0 ? g_testBlk[K] != 0 : CallIsBlockaded(rr[K]) == 1;
    for (int i = 0; t.blockaded && i < na; i++) {
        int e = all[i];
        if (g_testBlk[K] >= 0) {
            t.by[e] = g_testBlk[K] != 0;
            continue;
        }
        int saved[9];
        for (int k = 0; k < na; k++) {  // cell 0 of a record in the view: its relation with K
            saved[k] = dl::At<int>(rr[all[k]], fTension);
            if (all[k] != e) dl::At<int>(rr[all[k]], fTension) = 40;
        }
        t.by[e] = CallIsBlockaded(rr[K]) == 1;
        for (int k = 0; k < na; k++) dl::At<int>(rr[all[k]], fTension) = saved[k];
    }
    for (int i = 0; i < na; i++) {
        int j = all[i];
        if (j >= 1) t.blk[j] = g_testBlk[j] >= 0 ? g_testBlk[j] != 0 : CallIsBlockaded(rr[j]) == 1;
    }
    game::HostViewLeave();
    return true;
}

// Bridge `warfx strength <E> <N> [K]` (read-only probe): the game's GetBlockadeStrength of E against N in N's build and
// blockade areas, on the host's frame and in K's view (default: N, else E, under the player's rules), with the game's
// own account of it (its ExplString: the nations it added).
void* mBlkStrength = nullptr;
int fBuildArea = -1, fBlockadeArea = -1;

int CallStrengthRaw(void* e, int idx, void* area, uint32_t* str) {
    uint32_t arg = (uint32_t)(uintptr_t)str;
    __try {
        return (int)dl::Call(mBlkStrength, (uint32_t)(uintptr_t)e, (uint32_t)idx, (uint32_t)(uintptr_t)area, 1, &arg);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

struct StrengthRes {
    int v[2] = {-2, -2};
    uint32_t s[2] = {};
};
void StrengthOf(void* e, int idx, void* n, StrengthRes& r) {
    void* areas[2] = {dl::At<void*>(n, fBuildArea), dl::At<void*>(n, fBlockadeArea)};
    for (int a = 0; a < 2; a++)
        if (areas[a]) r.v[a] = CallStrengthRaw(e, idx, areas[a], &r.s[a]);
}
std::string FormatStrength(const StrengthRes& r) {
    std::string s;
    const char* tag[2] = {"build", "blockade"};
    for (int a = 0; a < 2; a++) {
        std::string x = r.s[a] ? W2U(dl::ReadUStr((const void*)(uintptr_t)r.s[a])) : std::string();
        for (char& c : x)
            if (c == '\r' || c == '\n') c = ' ';
        s += std::string(a ? " " : "") + tag[a] + "=" + std::to_string(r.v[a]) + " [" + x + "]";
    }
    return s;
}

std::string Strength(const std::vector<std::string>& args) {
    if (!mBlkStrength || fBuildArea < 0 || fBlockadeArea < 0) return "error the blockade strength probe is unavailable";
    if (args.size() < 4) return "error usage: warfx strength <E> <N> [K]";
    int E = atoi(args[2].c_str()), N = atoi(args[3].c_str());
    int K = args.size() > 4 ? atoi(args[4].c_str())
                            : hookcb::PlayerRulesNation(N) ? N : hookcb::PlayerRulesNation(E) ? E : -1;
    if (E < 0 || E > 8 || N < 0 || N > 8 || E == N || game::NationCount() < 9) return "error nations";
    void* rr[9];
    for (int n = 0; n < 9; n++) rr[n] = Rec(n);
    StrengthRes host, view;
    StrengthOf(rr[E], N, rr[N], host);
    std::string s = "ok E=" + std::to_string(E) + " N=" + std::to_string(N) + " host: " + FormatStrength(host);
    bool none[9] = {};
    if (K >= 1 && K <= 8 && game::HostViewEnter(K, std::max(1, hookcb::KWarCounter(K)), none)) {
        StrengthOf(rr[E], N == K ? 0 : N == 0 ? K : N, rr[N], view);
        game::HostViewLeave();
        s += " | view " + std::to_string(K) + ": " + FormatStrength(view);
    } else {
        s += " | view -";
    }
    return s;
}

void News(int K, const std::wstring& text) {
    g_news[K].push_back(text);
    Log("warfx: message for nation %d: %s", K, W2U(text).c_str());
}

void Credit(int gainer, int loser, int vp, std::string& rec) {
    if (vp <= 0) return;
    hookcb::CreditWarVP(gainer, loser, vp, "blockade");
    rec += " " + std::to_string(gainer) + "+" + std::to_string(vp) + "/" + std::to_string(loser);
}

std::string Join(const std::vector<int>& v) {
    std::string s;
    for (int x : v) s += (s.empty() ? "" : ",") + std::to_string(x);
    return s.empty() ? "-" : s;
}

// The month's blockade check of every frame (the load's: `load`, the flags only). Runs after the game's own check.
void Check(bool load) {
    std::map<int, Frame> frames;
    for (int K = 1; K <= 8 && K < game::NationCount(); K++) {
        if (!hookcb::PlayerRulesNation(K)) continue;
        int kwar = hookcb::KWarCounter(K);
        if (kwar <= 0) continue;  // as the game's War > 0
        Frame f;
        f.K = K;
        f.kwar = kwar;
        f.hostEnemy = game::AtWar(K, 0);
        for (int j = 1; j <= 8 && j < game::NationCount(); j++)
            if (j != K && game::AtWar(K, j)) f.enemies.push_back(j);
        if (!f.enemies.empty() || f.hostEnemy) frames[K] = f;
    }
    for (int n = 0; n < 9; n++) {
        void* r = Rec(n);
        g_hostFlag[n] = r && FlagOf(r) != 0;
        g_frameFlag[n] = false;
    }
    g_frames = frames;
    g_month = !load && !g_frames.empty();
    g_cleared = -1;
    if (!load) {
        g_rec.clear();
        g_weary.clear();
        g_build.clear();
    }
    if (g_frames.empty()) return;
    std::string log;
    int viewMiss = 0;
    for (auto& kv : g_frames) {
        Frame& f = kv.second;
        int K = f.K;
        std::string& rec = g_rec[K];
        std::vector<int> all = f.enemies;
        if (f.hostEnemy) all.insert(all.begin(), 0);
        void* rr[9];
        for (int n = 0; n < 9; n++) rr[n] = Rec(n);
        ViewTests vt;
        const bool view = TestsInView(K, f.kwar, rr, all.data(), (int)all.size(), vt);
        if (!view) viewMiss++;
        f.blockaded = view ? vt.blockaded != 0 : Blockaded(K);
        if (f.blockaded) {
            for (int e : all)
                if (view ? vt.by[e] != 0 : BlockedBy(K, e, all)) f.blockers.push_back(e);
            g_frameFlag[K] = true;
        }
        std::vector<int> enemyBlk;
        for (int j : f.enemies) {
            bool own = g_frames.count(j) > 0;  // j's own frame sets its flag and credits its own gains
            if (f.blockaded && !own && !load) Credit(j, K, 20 * (Rnd(3) + FS()), rec);
            if (view ? vt.blk[j] != 0 : Blockaded(j)) {
                enemyBlk.push_back(j);
                if (!own) g_frameFlag[j] = true;
                if (!load) Credit(K, j, 20 * (Rnd(3) + FS()), rec);
            }
        }
        log += " " + std::to_string(K) + "(war " + std::to_string(f.kwar) + ", enemies " + Join(f.enemies) +
               (f.hostEnemy ? "+host" : "") + "): " +
               (f.blockaded ? "blockaded by " + Join(f.blockers) : std::string("not blockaded")) +
               ", blockading " + Join(enemyBlk) + (view ? " (its view)" : " (the host's frame)") + ";";
    }
    if (viewMiss && g_viewMissLogged < 5) {
        g_viewMissLogged++;
        Log("warfx: %d blockade test(s) on the host's frame (the nation's view was not available)", viewMiss);
    }
    std::string set;
    for (int n = 0; n < 9; n++)
        if (g_frameFlag[n]) {
            void* r = Rec(n);
            if (r) FlagOf(r) = 1;
            set += (set.empty() ? "" : ",") + std::to_string(n);
        }
    std::string host;
    for (int n = 0; n < 9; n++)
        if (g_hostFlag[n]) host += (host.empty() ? "" : ",") + std::to_string(n);
    g_lastCheck = std::string(load ? "load" : "month") + ":" + log + " flags " + (set.empty() ? "-" : set) +
                  " (the game's " + (host.empty() ? "-" : host) + ")";
    Log("warfx: blockade check (%s):%s flags set %s (the game's own: %s)", load ? "load, flags only" : "month",
        log.c_str(), set.empty() ? "none" : set.c_str(), host.empty() ? "none" : host.c_str());
    for (auto& kv : g_rec)
        if (!kv.second.empty() && !load) Log("warfx: nation %d's blockade points:%s", kv.first, kv.second.c_str());
}

void SafeCheck(bool load) {
    __try {
        Check(load);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("warfx: blockade check failed (exception)");
    }
}

void __stdcall OnChecked(void* /*form*/, bool load) {
    if (!g_on) return;
    SafeCheck(load);
}

// The unrest of a blockaded nation n (DoBlockadeEffects' and the loop's rule) by the war counter `war`; the messages to
// the nations `to` (K's own "our people" line only to K itself).
void Unrest(int n, int war, const std::vector<int>& to, bool own) {
    void* rec = Rec(n);
    int& u = dl::At<int>(rec, fUnrest);
    int before = u;
    std::wstring name = game::NationName(n);
    if (Rnd(100) < war / 2 + 1 && Rnd(40) < dl::At<int>(rec, fBM)) {
        u++;
        if (u > 5 && Rnd(100) < 50)
            for (int K : to) News(K, L"The blockade is causing hardship and food shortages in " + name + L"!");
    }
    if (Rnd(100) < 5 || (Rnd(100) < war / 2 + 12 && u < 7)) {
        u++;
        if (u >= 7) {
            for (int K : to) News(K, L"The blockade is causing food shortages and privations in " + name + L"!");
        } else if (own) {
            News(n, L"Shortages due to the blockade are causing hardship for our people!");
        }
    }
    if (u != before) Log("warfx: nation %d's unrest from the blockade: %d -> %d (war counter %d)", n, before, u, war);
}

void Share(int x, const std::vector<int>& to, int other, bool toAreGainers, std::string& rec) {
    if (to.empty() || x <= 0) return;
    int share = x / (int)to.size(), extra = x % (int)to.size();
    for (int t : to) {
        int v = share + (extra-- > 0 ? 1 : 0);
        if (toAreGainers)
            Credit(t, other, v, rec);
        else
            Credit(other, t, v, rec);
    }
}

// DoBlockadeEffects of nation n (flagged): true = done here (the game's routine is skipped).
bool Effects(int n) {
    void* rec = Rec(n);
    auto fk = g_frames.find(n);
    if (fk != g_frames.end()) {  // K: the player's own
        Frame& f = fk->second;
        std::string& r = g_rec[n];
        int x = 20 * (Rnd(3) + FS());
        x += 10 * (Rnd(2) + FS());
        std::vector<int> to = f.blockers;
        if (to.empty()) {
            if (f.hostEnemy) to.push_back(0);
            else to = f.enemies;
        }
        r += " effects:";
        Share(x, to, n, true, r);
        News(n, L"The enemy gains " + Num(x) + L" VP for blockading us");
        Unrest(n, f.kwar, {n}, true);
        g_cleared = n;
        FlagOf(rec) = 0;
        return true;
    }
    std::vector<int> owners;  // the frames at war with n
    int war = 0;
    for (auto& kv : g_frames)
        if (std::find(kv.second.enemies.begin(), kv.second.enemies.end(), n) != kv.second.enemies.end()) {
            owners.push_back(kv.first);
            war = std::max(war, kv.second.kwar);
        }
    if (owners.empty()) return false;
    if (g_hostFlag[n]) {  // the host's enemy too: the game's own effects follow; each K its own points
        for (int K : owners) {
            int x = 20 * (Rnd(3) + FS());
            x += 10 * (Rnd(2) + FS());
            g_rec[K] += " effects:";
            Credit(K, n, x, g_rec[K]);
            News(K, L"Gain " + Num(x) + L" VP for blockade of enemy");
        }
        return false;
    }
    int x = 20 * (Rnd(3) + FS());
    x += 10 * (Rnd(2) + FS());
    int share = x / (int)owners.size(), extra = x % (int)owners.size();
    for (int K : owners) {
        int v = share + (extra-- > 0 ? 1 : 0);
        g_rec[K] += " effects:";
        Credit(K, n, v, g_rec[K]);
        News(K, L"Gain " + Num(v) + L" VP for blockade of enemy");
    }
    Unrest(n, war, owners, false);
    g_cleared = n;
    FlagOf(rec) = 0;
    return true;
}

int SafeEffects(int n) {
    __try {
        return Effects(n) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("warfx: the blockade's effects on nation %d failed (exception)", n);
        return 0;
    }
}

void RestoreCleared() {
    if (g_cleared < 0) return;
    void* r = Rec(g_cleared);
    if (r) FlagOf(r) = 1;
    g_cleared = -1;
}

// CheckUnrestChanges' war-weariness rule for K under the player's rules, by K's own war counter and war score (all its
// wars: the B4 ledger with the computer's nations, the score of a war between players): at war, government type 2, the
// enemies' points above K's and BudgetModifier above 18 -> Random(100) < 5 (a second chance first when K is blockaded)
// -> UnrestLevel + 1 and the game's text to K's player. The game's own reads the host's War, EnemyVP and VP.
void Weary(int K) {
    void* rec = Rec(K);
    if (!rec) return;
    int kwar = hookcb::KWarCounter(K), vp = 0, evp = 0;
    hookcb::KWarScore(K, vp, evp);
    int gov = dl::At<uint8_t>(rec, fGov), bm = dl::At<int>(rec, fBM);
    bool blk = FlagOf(rec) != 0 || g_cleared == K;  // a frame's flag reads 0 for the rest of K's turn of the loop
    int& u = dl::At<int>(rec, fUnrest);
    int before = u;
    char b[160];
    snprintf(b, sizeof(b), "war counter %d, war score %d:%d, government %d, BM %d, blockaded %d -> ", kwar, vp, evp, gov,
             bm, blk ? 1 : 0);
    std::string r = b;
    if (kwar <= 0)
        r += "no (at peace)";
    else if (gov != 2)
        r += "no (government)";
    else if (evp <= vp)
        r += "no (not losing)";
    else if (bm <= 18)
        r += "no (BudgetModifier)";
    else {
        bool hit = blk && Rnd(100) < 5;
        if (!hit) hit = Rnd(100) < 5;
        if (hit) {
            u++;
            News(K, g_wearyText);
            r += "unrest " + std::to_string(before) + " -> " + std::to_string(u);
        } else {
            r += "no (draw)";
        }
    }
    g_weary[K] = r;
    Log("warfx: war weariness of nation %d (the player's rule): %s", K, r.c_str());
}

void SafeWeary(int K) {
    __try {
        Weary(K);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("warfx: the war weariness of nation %d failed (exception)", K);
    }
}

// A joined nation's answer to a build rule's War > 0 test (its own counter), counted; logged where it differs from the
// host's (capped).
int BuildWar(int site, int n, int host) {
    int kc = hookcb::KWarCounter(n), k = kc > 0 ? 1 : 0;
    BuildRec& r = g_build[n];
    r.tests[site]++;
    r.war[site] += k;
    if (k != host && g_buildLogs < 60) {
        g_buildLogs++;
        Log("warfx: building for war: nation %d's %s test answers %s (its war counter %d; the host's %s)", n,
            kBuildSite[site], k ? "at war" : "at peace", kc, host ? "at war" : "at peace");
    }
    return k;
}

void Grant(int n) {
    void* rec = Rec(n);
    if (!rec) return;
    int kc = hookcb::KWarCounter(n), bm = dl::At<int>(rec, fBM);
    g_build[n].grants++;
    News(n, kc % 2 == 0 ? g_txtBudget : g_txtEmergency);
    Log("warfx: building for war: nation %d's war budget: BudgetModifier %d -> %d (its war counter %d)", n, bm, bm + 1, kc);
}

// StopBlockadedMoves: the ship moves of the frames' flagged nations it stopped (for the record)
struct StopSnap {
    int n;
    std::vector<void*> ships;
};
std::vector<StopSnap> g_stopSnap;

bool AnyFrameFlag() {
    for (int n = 0; n < 9; n++) {
        void* r = Rec(n);
        if (g_frameFlag[n] && r && FlagOf(r)) return true;
    }
    return false;
}

void StopBefore() {
    g_stopSnap.clear();
    for (int n = 0; n < 9; n++) {
        void* r = Rec(n);
        if (!g_frameFlag[n] || !r || !FlagOf(r)) continue;
        StopSnap s;
        s.n = n;
        void* list = dl::At<void*>(r, fShips);
        int c = dl::ListCount(list);
        for (int i = 0; i < c; i++) {
            void* sh = dl::ListItem(list, i);
            if (sh && dl::At<void*>(sh, fDest)) s.ships.push_back(sh);
        }
        g_stopSnap.push_back(s);
    }
}

void StopAfter(bool forced) {
    std::string s;
    for (auto& snap : g_stopSnap) {
        int stopped = 0;
        for (void* sh : snap.ships)
            if (!dl::At<void*>(sh, fDest)) stopped++;
        s += " " + std::to_string(snap.n) + ":" + std::to_string(stopped) + "/" + std::to_string(snap.ships.size());
    }
    g_stopSnap.clear();
    if (s.empty()) return;
    g_lastStop = std::string(forced ? "war 1 (the host at peace)" : "the host at war") + s;
    Log("warfx: StopBlockadedMoves (%s): moves stopped / under way of the frames' blockaded nations:%s",
        forced ? "run with War 1: the host is at peace" : "the host at war", s.c_str());
}

void* g_oUnrest = nullptr;
void* g_oStop = nullptr;

void SafeStopBefore() {
    try {
        StopBefore();
    } catch (...) {
        g_stopSnap.clear();
    }
}
void SafeStopAfter(bool forced) {
    try {
        StopAfter(forced);
    } catch (...) {
    }
}

bool HookAt(void* target, void* detour, void** orig, const char* name) {
    if (!target || !detour) return false;
    MH_STATUS st = MH_CreateHook(target, detour, orig);
    if (st == MH_OK) {
        st = MH_EnableHook(target);
        if (st != MH_OK) MH_RemoveHook(target);
    }
    if (st != MH_OK) {
        Log("warfx: %s: hook failed (%d)", name, (int)st);
        *orig = nullptr;
        return false;
    }
    return true;
}

void Le(int v, std::vector<int>& p) {
    for (int i = 0; i < 4; i++) p.push_back((v >> (8 * i)) & 0xFF);
}
void Add(std::vector<int>& p, std::initializer_list<int> b) { p.insert(p.end(), b.begin(), b.end()); }

void WearyBegin() { g_wearyGame.clear(); }
void WearyEnd() {
    if (!g_wearyOn || g_weary.empty()) return;
    std::string mine;
    for (auto& kv : g_weary) mine += std::to_string(kv.first) + ",";
    Log("warfx: war weariness this month: the player's rule for nations %s the game's own for %s", mine.c_str(),
        g_wearyGame.empty() ? "none" : g_wearyGame.c_str());
}

}  // namespace

extern "C" void warfx_dWeary();
extern "C" void* warfx_oWeary;
extern "C" void* warfx_wearyBack;
extern "C" void warfx_dSubWar();
extern "C" void* warfx_oSubWar;
extern "C" void* warfx_subBack;
extern "C" void warfx_dGrowthWar();
extern "C" void* warfx_oGrowthWar;
extern "C" void* warfx_growthBack;
extern "C" void warfx_dGrant();
extern "C" void* warfx_oGrant;
extern "C" void warfx_dDelays();
extern "C" void* warfx_oDelays;

namespace {

// CheckUnrestChanges' war-weariness rule, as reproduced in Weary (each field and the game's War > 0 test checked).
std::string InstallWeary(uint8_t* unrest) {
    fGov = dl::Field("TBuilderNation", "GovernmentType");
    int fVP = dl::Field("TBuilderNation", "VP"), fPlayer = dl::Field("TfrmBuildCamp", "PlayerNation"),
        fEvp = dl::Field("TfrmBuildCamp", "EnemyVP"), fMsgs = dl::Field("TfrmBuildCamp", "TurnMessages");
    int* war = game::WarCell();
    if (!unrest || fGov < 0 || fVP < 0 || fPlayer < 0 || fEvp < 0 || fMsgs < 0 || fBM < 0 || fUnrest < 0 ||
        fBlockaded < 0 || !war)
        return "fields or routines";
    // call <War > 0>; test al,al; je <end>; mov eax,[ebx]; cmp byte ptr [eax+GovernmentType],2; jne; mov eax,[ebp-4];
    // mov eax,[eax+EnemyVP]; mov edx,[ebp-4]; mov edx,[edx+PlayerNation]; cmp eax,[edx+VP]; jle; mov eax,[ebx];
    // cmp dword ptr [eax+BudgetModifier],18; jle; mov eax,[ebx]; cmp byte ptr [eax+Blockaded],0; je +15;
    // Random(100) < 5 -> jl inc; Random(100) >= 5 -> jge end; inc: inc [eax+UnrestLevel]; TurnMessages.Add(text)
    std::vector<int> p = {0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x74, -1, 0x8B, 0x03, 0x80, 0xB8};
    Le(fGov, p);
    Add(p, {0x02, 0x75, -1, 0x8B, 0x45, 0xFC, 0x8B, 0x80});
    Le(fEvp, p);
    Add(p, {0x8B, 0x55, 0xFC, 0x8B, 0x92});
    Le(fPlayer, p);
    Add(p, {0x3B, 0x82});
    Le(fVP, p);
    Add(p, {0x7E, -1, 0x8B, 0x03, 0x83, 0xB8});
    Le(fBM, p);
    Add(p, {0x12, 0x7E, -1, 0x8B, 0x03, 0x80, 0xB8});
    Le(fBlockaded, p);
    Add(p, {0x00, 0x74, 0x0F, 0xB8, 0x64, 0, 0, 0, 0xE8, -1, -1, -1, -1, 0x83, 0xF8, 0x05, 0x7C, 0x0F, 0xB8, 0x64, 0, 0, 0,
            0xE8, -1, -1, -1, -1, 0x83, 0xF8, 0x05, 0x7D, 0x1B, 0x8B, 0x03, 0xFF, 0x80});
    Le(fUnrest, p);
    Add(p, {0x8B, 0x45, 0xFC, 0x8B, 0x80});
    Le(fMsgs, p);
    Add(p, {0xBA, -1, -1, -1, -1, 0x8B, 0x08, 0xFF, 0x51, 0x3C});
    uint8_t* at = dl::FindPattern(unrest, 0x1400, p);
    if (!at) return "the rule's code";
    int len = (int)p.size();
    uint8_t* test = CallTo(at);
    uint8_t* r1 = nullptr;
    uint8_t* r2 = nullptr;
    for (int i = 9; i + 5 <= len; i++)
        if (at[i] == 0xB8 && at[i + 1] == 0x64 && at[i + 5] == 0xE8) (r1 ? r2 : r1) = CallTo(at + i + 5);
    bool ok = at[8] == len - 9 && InImage(test) && test[0] == 0x83 && test[1] == 0x3D &&
              *(int**)(test + 2) == war && test[6] == 0 && test[7] == 0x0F && test[8] == 0x9F && test[9] == 0xC0 &&
              test[10] == 0xC3 && r1 && r1 == r2 && InImage(r1) && r1[0] == 0x50 && r1[1] == 0xFF && r1[2] == 0x15 &&
              (!mRandom || mRandom == r1);
    if (!ok) return "the rule's tests";
    const void* text = *(const void**)(at + len - 9);
    g_wearyText = InImage(text) ? dl::ReadUStr(text) : std::wstring();
    if (g_wearyText != L"Our people is getting dissatisfied with the bad war situation!") return "the rule's text";
    if (!mRandom) mRandom = r1;
    warfx_wearyBack = at + 5;
    if (!HookAt(at, (void*)&warfx_dWeary, &warfx_oWeary, "war weariness")) return "hook";
    g_wearyOn = true;
    Log("warfx: the player's war effects for joined nations: war weariness ok (the rule at %#x)",
        (unsigned)((uintptr_t)at - dl::g_base));
    return "";
}

// The War > 0 helper's code: `cmp dword ptr [War],0; setg al; ret`.
bool IsWarTest(uint8_t* t) {
    return InImage(t) && t[0] == 0x83 && t[1] == 0x3D && *(int**)(t + 2) == game::WarCell() && t[6] == 0 &&
           t[7] == 0x0F && t[8] == 0x9F && t[9] == 0xC0 && t[10] == 0xC3;
}
bool LiteralIs(const uint8_t* imm, const wchar_t* text) {
    const void* p = *(const void* const*)imm;
    return InImage(p) && dl::ReadUStr(p) == text;
}

// Building for war: the host's War > 0 tests of the build rules every nation shares, answered for each joined nation by
// its own war counter: AdvanceShipConstruction's submarine rule, CheckForConstructionDelays (the game's War reads the
// counter for the call) and HandleEconomicGrowth's war budget (with the nation's text).
std::string InstallBuild() {
    uint8_t* adv = (uint8_t*)dl::Method("TfrmBuildCamp", "AdvanceShipConstruction");
    uint8_t* del = (uint8_t*)dl::Method("TfrmBuildCamp", "CheckForConstructionDelays");
    uint8_t* gro = (uint8_t*)dl::Method("TfrmBuildCamp", "HandleEconomicGrowth");
    uint8_t* getB = (uint8_t*)dl::Method("TfrmBuildCamp", "GetBNation");
    int fUnr = dl::Field("TBuilderNation", "UnrestLevel"), fBud = dl::Field("TBuilderNation", "BudgetModifier");
    if (!adv || !del || !gro || !getB || fUnr < 0 || fBud < 0 || !game::WarCell() || !mRandom) return "routines";
    // AdvanceShipConstruction: call <War>; test al,al; je; cmp [ebx+15h],3; jle; Random(40); dec eax; jne;
    // lea eax,[ebx+15h]; xor ecx,ecx; mov edx,1; call DecMin; cmp [ebp-30h],0; jne; push <text>
    uint8_t* a = dl::FindPattern(adv, 0x2000, {0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x74, -1, 0x83, 0x7B, 0x15, 0x03, 0x7E, -1,
                                               0xB8, 0x28, 0, 0, 0, 0xE8, -1, -1, -1, -1, 0x48, 0x75, -1, 0x8D, 0x43, 0x15,
                                               0x33, 0xC9, 0xBA, 1, 0, 0, 0, 0xE8, -1, -1, -1, -1, 0x83, 0x7D, 0xD0, 0x00,
                                               0x75, -1, 0x68});
    bool loop = dl::FindPattern(adv, 0x2000, {0xFF, 0x45, 0xD0, 0x83, 0x7D, 0xD0, 0x09}) &&
                dl::FindPattern(adv, 0x100, {0x89, 0x45, 0xD0, 0x8B, 0x55, 0xD0});
    if (!a || !loop || !IsWarTest(CallTo(a)) || CallTo(a + 20) != mRandom ||
        !LiteralIs(a + 50, L"War emergency measures has shortened the construction time of "))
        return "AdvanceShipConstruction's code";
    // HandleEconomicGrowth: call <War>; test al,al; je near; mov edx,ebx; mov eax,esi; call GetBNation;
    // cmp [eax+UnrestLevel],8; jge near
    std::vector<int> pg = {0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x0F, 0x84, -1, -1, -1, -1, 0x8B, 0xD3, 0x8B, 0xC6,
                           0xE8, -1, -1, -1, -1, 0x83, 0xB8};
    Le(fUnr, pg);
    Add(pg, {0x08, 0x0F, 0x8D});
    uint8_t* g = dl::FindPattern(gro, 0x600, pg);
    // ... the grant: test ebx,ebx; jne; mov eax,[War]; and eax,80000001h; jns; dec; or eax,-2; inc; test; jne;
    // picture 1 ("War budget ..."); jmp; picture 2 ("War emergency funding ..."); mov edx,ebx; mov eax,esi;
    // call GetBNation; inc [eax+BudgetModifier]
    std::vector<int> pt = {0x85, 0xDB, 0x75, 0x47, 0xA1, -1, -1, -1, -1, 0x25, 0x01, 0, 0, 0x80, 0x79, 0x05, 0x48, 0x83,
                           0xC8, 0xFE, 0x40, 0x85, 0xC0, 0x75, 0x1A, 0x6A, 0x01, 0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0xB9,
                           -1, -1, -1, -1, 0xBA, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1, 0xEB, 0x18, 0x6A, 0x01, 0xA1,
                           -1, -1, -1, -1, 0x8B, 0x00, 0xB9, -1, -1, -1, -1, 0xBA, -1, -1, -1, -1, 0xE8, -1, -1, -1,
                           -1, 0x8B, 0xD3, 0x8B, 0xC6, 0xE8, -1, -1, -1, -1, 0xFF, 0x80};
    Le(fBud, pt);
    uint8_t* t = dl::FindPattern(gro, 0x600, pt);
    bool gloop = dl::FindPattern(gro, 0x200, {0x33, 0xDB, 0x8B, 0xD3, 0x8B, 0xC6, 0xE8}) != nullptr;
    if (!g || !t || t < g || t - g > 0x100 || !gloop || !IsWarTest(CallTo(g)) || CallTo(g + 17) != getB ||
        *(int**)(t + 5) != game::WarCell() || CallTo(t + 0x4F) != getB ||
        !LiteralIs(t + 0x28, L"War budget means increased allocations to the navy!") ||
        !LiteralIs(t + 0x42, L"War emergency funding granted to the navy!"))
        return "HandleEconomicGrowth's code";
    g_txtBudget = dl::ReadUStr(*(const void* const*)(t + 0x28));
    g_txtEmergency = dl::ReadUStr(*(const void* const*)(t + 0x42));
    // CheckForConstructionDelays: its War tests are calls of the helper (three).
    int tests = 0;
    for (auto c : dl::CallTargets(del, 0xd00))
        if (IsWarTest(c)) tests++;
    if (tests != 3) return "CheckForConstructionDelays' code (" + std::to_string(tests) + " War tests)";
    warfx_subBack = a + 5;
    warfx_growthBack = g + 5;
    bool s1 = HookAt(a, (void*)&warfx_dSubWar, &warfx_oSubWar, "AdvanceShipConstruction (submarines)");
    bool s2 = s1 && HookAt(g, (void*)&warfx_dGrowthWar, &warfx_oGrowthWar, "HandleEconomicGrowth (war budget)");
    bool s3 = s2 && HookAt(t + 0x4B, (void*)&warfx_dGrant, &warfx_oGrant, "HandleEconomicGrowth (grant)");
    bool s4 = s3 && HookAt(del, (void*)&warfx_dDelays, &warfx_oDelays, "CheckForConstructionDelays");
    if (!s4) {
        if (s3) MH_RemoveHook(t + 0x4B);
        if (s2) MH_RemoveHook(g);
        if (s1) MH_RemoveHook(a);
        return "hooks";
    }
    g_buildOn = true;
    Log("warfx: the player's war effects for joined nations: building for war ok (submarines %#x, war budget %#x, "
        "grant %#x, CheckForConstructionDelays %#x)",
        (unsigned)((uintptr_t)a - dl::g_base), (unsigned)((uintptr_t)g - dl::g_base),
        (unsigned)((uintptr_t)(t + 0x4B) - dl::g_base), (unsigned)((uintptr_t)del - dl::g_base));
    return "";
}

}  // namespace

extern "C" int __stdcall warfx_EffectsCB(void* form, void* nation, uintptr_t ret) {
    (void)form;
    RestoreCleared();
    if (!g_on || !g_month || ret != retEffects || !nation) return 0;
    int n = IndexOf(nation);
    if (n < 0 || !FlagOf(nation)) return 0;
    return SafeEffects(n);
}

extern "C" int __stdcall warfx_WearyCB(void* nation) {
    if (!g_wearyOn || !nation) return 0;
    int n = IndexOf(nation);
    if (n < 1 || !hookcb::PlayerRulesNation(n)) {
        if (n >= 0) g_wearyGame += std::to_string(n) + ",";
        return 0;
    }
    SafeWeary(n);
    return 1;
}

extern "C" void* warfx_oWeary = nullptr;     // MinHook's trampoline: the game's `call` of the War > 0 test, then back
extern "C" void* warfx_wearyBack = nullptr;  // the instruction after it (`test al,al; je <the rule's end>`)
// At CheckUnrestChanges' war-weariness rule (EBX: the loop's cell of the nation): K -> the player's rule here, then AL = 0
// (the game's rule skipped); any other nation -> the game's test and rule.
extern "C" __declspec(naked) void warfx_dWeary() {
    __asm {
        mov eax, dword ptr [ebx]
        push eax
        call warfx_WearyCB
        test eax, eax
        jnz handled
        jmp dword ptr [warfx_oWeary]
    handled:
        xor eax, eax
        jmp dword ptr [warfx_wearyBack]
    }
}

// Building for war: the War > 0 test of a shared build rule for nation n (`site`, see kBuildSite).
extern "C" int __stdcall warfx_WarOf(int site, int n) {
    int* war = game::WarCell();
    int host = war && *war > 0 ? 1 : 0;
    if (!g_buildOn || n < 1 || n > 8 || !hookcb::IsHumanRemoteNation(n)) return host;
    return BuildWar(site, n, host);
}

extern "C" void* warfx_oSubWar = nullptr;
extern "C" void* warfx_subBack = nullptr;
// AdvanceShipConstruction's submarine rule (`call <War > 0>`; the nation loop's index at [ebp-0x30]).
extern "C" __declspec(naked) void warfx_dSubWar() {
    __asm {
        push dword ptr [ebp - 0x30]
        push 0
        call warfx_WarOf
        jmp dword ptr [warfx_subBack]
    }
}

extern "C" void* warfx_oGrowthWar = nullptr;
extern "C" void* warfx_growthBack = nullptr;
// HandleEconomicGrowth's war budget rule (`call <War > 0>`; the nation loop's index in EBX).
extern "C" __declspec(naked) void warfx_dGrowthWar() {
    __asm {
        push ebx
        push 2
        call warfx_WarOf
        jmp dword ptr [warfx_growthBack]
    }
}

extern "C" void __stdcall warfx_GrantCB(int n) {
    if (!g_buildOn || n < 1 || n > 8 || !hookcb::IsHumanRemoteNation(n)) return;
    Grant(n);
}

extern "C" void* warfx_oGrant = nullptr;
// HandleEconomicGrowth's grant (BudgetModifier + 1 follows; the game's picture is nation 0's only): K's text.
extern "C" __declspec(naked) void warfx_dGrant() {
    __asm {
        pushad
        push ebx
        call warfx_GrantCB
        popad
        jmp dword ptr [warfx_oGrant]
    }
}

extern "C" void* warfx_oDelays = nullptr;
extern "C" int __stdcall warfx_DelaysBefore(int n) {
    int* war = game::WarCell();
    if (!g_buildOn || !war || n < 1 || n > 8 || !hookcb::IsHumanRemoteNation(n)) return 0;
    int host = *war > 0 ? 1 : 0;
    g_delSaved = *war;
    BuildWar(1, n, host);
    *war = hookcb::KWarCounter(n);
    return 1;
}
// CheckForConstructionDelays(Self=EAX, NatIdx=EDX): Boolean. For a joined nation the game's War reads its own counter.
extern "C" uint32_t __stdcall warfx_DelaysWrap(void* form, int n) {
    int swapped = warfx_DelaysBefore(n);
    uint32_t r = 0;
    __try {
        r = dl::Call(warfx_oDelays, (uint32_t)(uintptr_t)form, (uint32_t)n);
    } __finally {
        if (swapped) *game::WarCell() = g_delSaved;
    }
    return r;
}
extern "C" __declspec(naked) void warfx_dDelays() {
    __asm {
        push edx
        push eax
        call warfx_DelaysWrap
        ret
    }
}

extern "C" void* warfx_oEffects = nullptr;
// DoBlockadeEffects(Self=EAX, aNation=EDX): register only.
extern "C" __declspec(naked) void warfx_dEffects() {
    __asm {
        push eax
        push edx
        push dword ptr [esp + 8]
        push edx
        push eax
        call warfx_EffectsCB
        test eax, eax
        pop edx
        pop eax
        jnz skip
        jmp dword ptr [warfx_oEffects]
    skip:
        ret
    }
}

extern "C" void __stdcall warfx_UnrestWrap(void* form) {
    WearyBegin();
    __try {
        dl::Call(g_oUnrest, (uint32_t)(uintptr_t)form);
    } __finally {
        RestoreCleared();
        g_month = false;
    }
    WearyEnd();
}

// CheckUnrestChanges(Self=EAX)
extern "C" __declspec(naked) void warfx_dUnrest() {
    __asm {
        push eax
        call warfx_UnrestWrap
        ret
    }
}

extern "C" void __stdcall warfx_StopWrap(void* form) {
    int* war = game::WarCell();
    bool forced = g_on && war && *war <= 0 && AnyFrameFlag();
    int saved = war ? *war : 0;
    if (g_on) SafeStopBefore();
    if (forced) *war = 1;
    __try {
        dl::Call(g_oStop, (uint32_t)(uintptr_t)form);
    } __finally {
        if (forced) *war = saved;
    }
    if (g_on) SafeStopAfter(forced);
}

// StopBlockadedMoves(Self=EAX)
extern "C" __declspec(naked) void warfx_dStop() {
    __asm {
        push eax
        call warfx_StopWrap
        ret
    }
}

// ---------------------------------------------------------------------------------------------
// (c) Raiders, submarines and the trade war of a joined nation K under the player's rules (0.6.0, host, in the month).
// The game runs them for the host's nation's wars only and only while its War > 1 (DoStrategicTurn: the submarine loop
// before CheckAIvsAIBattles; CheckRaiderActivity, TdlgSubmarineWarfareSummary and CheckResultOfSinkings after it). Hooked
// at DoStrategicTurn's `call <War > 0>` right after that block (reached from both of its branches, before
// CheckBuildingShipsForWar and DoEndOfTurn): each K with its own counter above 1 at war with computer nations gets them in
// its view (game::HostViewEnter; the people's nations read as at peace with K: the host's own run covers the host's wars
// both ways, and a war between two joined nations keeps the computer's rule): CheckSubmarineActivity of K and of each
// enemy, CheckRaiderActivity's two loops (CheckRaiderSinkings per pair with its tests, its map markers reset), then
// CheckResultOfSinkings. The month's loss fields of every nation read 0 for the frame (the frame's own are added to the
// host's afterwards), the form's trade VP fields and EnemyVP are the frame's (EnemyVP and K's VP: K's war score for the
// raiders' "losing badly" test), its TurnMessages and RaiderSummary are fresh lists; the picture messages and the boxes
// (CheckRaiderProblem's ShowMessagePos) are recorded: all of it goes to K's turn report with a summary line, the host
// sees nothing. The trade VP of each call goes by pair (CreditWarVP: the nation's VP and the pair's war score); the
// FuelShortage of the other nations, the map markers and the host's form fields are put back; relation changes made in
// the view (neutral protests) are not kept (the view's cells are restored). The computer's own submarine sinkings
// (CheckAIvsAISubmarineSinking) of such K's computer pairs are dropped (the frame runs their submarine war). Mine warfare
// (CheckOperationalMineWarfare, CheckStrategicMinefields) stays the host's only.
namespace {
bool g_seaOn = false;
void *mSubAct = nullptr, *mRaidSink = nullptr, *mSinkResult = nullptr, *mIsOper = nullptr, *mSLNew = nullptr,
     *mObjFree = nullptr, *slVmt = nullptr;
int fPTW = -1, fETW = -1, fFormEVP = -1, fFormMsgs = -1, fRaidSum = -1, fAreas = -1, fRaidAct = -1, fRaidType = -1,
    fSeaVP = -1, fNatName = -1, fFuel = -1, fShipStatus = -1, fShipLoc = -1, fShipUsed = -1, fSeaBuildArea = -1;
int fLoss[4] = {-1, -1, -1, -1};  // merchants lost to submarines, to raiders, morale sinkings, own submarines lost
std::map<int, std::string> g_sea;  // K's last frame (bridge `warfx K`: sea=)
int g_aiSubDrops = 0;              // the computer's submarine sinkings dropped this month (K's computer pairs)
std::string g_aiSubPairs;
int g_testSeaR = -1, g_testSeaN = 0;  // TEST ONLY (bridge `warfxsea R n`): n of R's ships raid in the next frame
std::vector<std::wstring> g_boxes;
}  // namespace

extern "C" volatile char warfx_boxCap = 0;  // the frame records ShowMessagePos boxes (warfx_dMsgPos)
extern "C" void* warfx_oMsgPos = nullptr;
extern "C" void* warfx_oAISub = nullptr;
extern "C" void* warfx_oSeaWar = nullptr;

namespace {
int G1(void* fn, void* a, void* b = nullptr, void* c = nullptr) {  // a game call: 1, or 0 on an exception
    __try {
        dl::Call(fn, (uint32_t)(uintptr_t)a, (uint32_t)(uintptr_t)b, (uint32_t)(uintptr_t)c);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
int& Loss(void* rec, int k) { return dl::At<int>(rec, fLoss[k]); }
int Rel0(void* rec) { return dl::At<int>(rec, fTension); }  // in the view: the relation with K

struct SeaRes {  // the frame's own numbers (plain data: filled in the view)
    int calls = 0, faults = 0, other = 0;
    int gainK[9] = {}, gainE[9] = {};  // K's trade VP from enemy j; j's from K
    int raidK[9] = {}, subK[9] = {}, raidE[9] = {}, subE[9] = {};  // merchants: j's sunk by K; K's sunk by j
};

// Attributes `vp` to the nations whose merchant losses (field k) grew since `m0` (proportionally).
void Attribute(void* const rr[9], int k, const int m0[9], int K, int vp, int* gain, int* cnt, SeaRes& r) {
    int d[9] = {}, tot = 0;
    for (int j = 0; j < 9; j++)
        if (j != K) tot += d[j] = std::max(0, Loss(rr[j], k) - m0[j]);
    if (tot == 0) {
        r.other += vp;
        return;
    }
    int left = vp, rem = tot;
    for (int j = 0; j < 9; j++) {
        if (!d[j]) continue;
        int share = rem == d[j] ? left : vp * d[j] / tot;
        rem -= d[j];
        cnt[j] += d[j];
        gain[j] += share;
        left -= share;
    }
}

// In K's view (plain data only; each game call guarded): the game's submarine loop, CheckRaiderActivity's loops, then
// CheckResultOfSinkings. `real(v)` of the view: 0 -> K, K -> 0.
void RunSea(void* f, int K, void* const rr[9], SeaRes& r) {
    void* k = rr[K];
    int m0[9];
    for (int v = 0; v < 9; v++) {  // DoStrategicTurn's loop: i == 0 or at war with the player (K here)
        int n = v == 0 ? K : v == K ? 0 : v;
        if (v != 0 && Rel0(rr[n]) <= 40) continue;
        int p0 = dl::At<int>(f, fPTW), e0 = dl::At<int>(f, fETW);
        for (int j = 0; j < 9; j++) m0[j] = Loss(rr[j], 0);
        r.calls++;
        if (!G1(mSubAct, f, rr[n])) r.faults++;
        int dp = dl::At<int>(f, fPTW) - p0, de = dl::At<int>(f, fETW) - e0;
        if (v == 0) {
            Attribute(rr, 0, m0, K, dp, r.gainK, r.subK, r);
            r.other += de;
        } else {
            r.gainE[n] += de;
            r.subE[n] += std::max(0, Loss(k, 0) - m0[K]);
            r.other += dp;
        }
    }
    void* areas = dl::At<void*>(f, fAreas);  // CheckRaiderActivity: every area's markers reset, then its two loops
    int na = areas ? dl::At<int>(areas, 8) : 0;
    void** ai = areas ? dl::At<void**>(areas, 4) : nullptr;
    for (int i = 0; ai && i < na; i++) {
        dl::At<uint8_t>(ai[i], fRaidAct) = 0;
        dl::At<uint8_t>(ai[i], fRaidType) = 0x11;
    }
    for (int v = 1; v < 9; v++) {
        int n = v == K ? 0 : v;
        if (Rel0(rr[n]) <= 40 || FlagOf(rr[n])) continue;
        int p0 = dl::At<int>(f, fPTW), e0 = dl::At<int>(f, fETW), l0 = Loss(rr[n], 1);
        r.calls++;
        if (!G1(mRaidSink, f, k, rr[n])) r.faults++;
        r.gainK[n] += dl::At<int>(f, fPTW) - p0;
        r.raidK[n] += std::max(0, Loss(rr[n], 1) - l0);
        r.other += dl::At<int>(f, fETW) - e0;
    }
    for (int v = 0; v < 9; v++) {
        int n = v == 0 ? K : v == K ? 0 : v;
        if (Rel0(rr[n]) <= 40 || FlagOf(k)) continue;
        int p0 = dl::At<int>(f, fPTW), e0 = dl::At<int>(f, fETW), l0 = Loss(k, 1);
        r.calls++;
        if (!G1(mRaidSink, f, rr[n], k)) r.faults++;
        r.gainE[n] += dl::At<int>(f, fETW) - e0;
        r.raidE[n] += std::max(0, Loss(k, 1) - l0);
        r.other += dl::At<int>(f, fPTW) - p0;
    }
    dl::At<int>(f, fPTW) = 0;  // the trade VP go by pair (credited afterwards), not to the frame's VP/EnemyVP
    dl::At<int>(f, fETW) = 0;
    r.calls++;
    if (!G1(mSinkResult, f)) r.faults++;
}

int RunSeaGuarded(void* f, int K, void* const rr[9], SeaRes& r) {
    __try {
        RunSea(f, K, rr, r);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int fSLItems = -1, fSLCount = -1;  // TStringList's FList (pairs of string, object) and FCount
std::vector<std::wstring> ListLines(void* sl) {
    std::vector<std::wstring> out;
    int n = sl && fSLCount > 0 ? dl::At<int>(sl, fSLCount) : 0;
    void** items = n > 0 && fSLItems > 0 ? dl::At<void**>(sl, fSLItems) : nullptr;
    for (int i = 0; items && i < n; i++) out.push_back(dl::ReadUStr(items[i * 2]));
    return out;
}
void* NewList() { return (void*)(uintptr_t)dl::Call(mSLNew, (uint32_t)(uintptr_t)slVmt, 1); }

struct TestRaider {
    void* ship;
    uint8_t status;
    void* loc;
};

// TEST (`warfxsea R n`): up to n operational ships of R in active service raid in the defender's build area for this
// frame (R = K: against its first enemy; R an enemy: against K); put back afterwards.
std::vector<TestRaider> TestRaidersOn(int K, const std::vector<int>& enemies, std::string& log) {
    std::vector<TestRaider> out;
    int R = g_testSeaR;
    if (R < 0 || g_testSeaN <= 0 || (R != K && std::find(enemies.begin(), enemies.end(), R) == enemies.end()))
        return out;
    void* raider = Rec(R);
    void* def = Rec(R == K ? enemies[0] : K);
    void* area = def ? dl::At<void*>(def, fSeaBuildArea) : nullptr;
    for (int i = 0; raider && area && i < game::ShipCount(raider) && (int)out.size() < g_testSeaN; i++) {
        void* sh = game::Ship(raider, i);
        if (!sh || dl::At<uint8_t>(sh, fShipStatus) != 0 || dl::At<uint8_t>(sh, fShipUsed) ||
            !(dl::Call(mIsOper, (uint32_t)(uintptr_t)sh) & 0xFF))
            continue;
        out.push_back({sh, 0, dl::At<void*>(sh, fShipLoc)});
        dl::At<uint8_t>(sh, fShipStatus) = 4;
        dl::At<void*>(sh, fShipLoc) = area;
    }
    log = " TEST: " + std::to_string(out.size()) + " raider(s) of nation " + std::to_string(R) + " in nation " +
          std::to_string(R == K ? enemies[0] : K) + "'s build area;";
    g_testSeaR = -1;
    return out;
}
void TestRaidersOff(int R, const std::vector<TestRaider>& t) {
    void* rec = Rec(R);
    for (int i = 0; rec && i < game::ShipCount(rec); i++) {
        void* sh = game::Ship(rec, i);
        for (const TestRaider& x : t)
            if (sh == x.ship && dl::At<uint8_t>(sh, fShipStatus) == 4) {  // else as the game left it (home, interned)
                dl::At<uint8_t>(sh, fShipStatus) = x.status;
                dl::At<void*>(sh, fShipLoc) = x.loc;
            }
    }
}

void SeaFrame(void* f, int K, int kwar, const std::vector<int>& enemies) {
    void* rr[9];
    for (int n = 0; n < 9; n++) rr[n] = Rec(n);
    if (!rr[K]) return;
    bool human[9] = {};
    for (int n = 0; n < 9; n++) human[n] = n != K && (n == 0 || hookcb::IsHumanRemoteNation(n));
    int saved[9][4], fuel[9], vp = 0, evp = 0;
    for (int n = 0; n < 9; n++) {
        for (int k = 0; k < 4; k++) saved[n][k] = std::exchange(Loss(rr[n], k), 0);
        fuel[n] = dl::At<uint8_t>(rr[n], fFuel);
    }
    hookcb::KWarScore(K, vp, evp);
    const int realVP = dl::At<int>(rr[K], fSeaVP), hPTW = dl::At<int>(f, fPTW), hETW = dl::At<int>(f, fETW),
              hEVP = dl::At<int>(f, fFormEVP);
    void* areas = dl::At<void*>(f, fAreas);
    std::vector<uint16_t> marks;
    for (int i = 0; areas && i < dl::At<int>(areas, 8); i++) {
        void* a = dl::At<void**>(areas, 4)[i];
        marks.push_back((uint16_t)(dl::At<uint8_t>(a, fRaidAct) | dl::At<uint8_t>(a, fRaidType) << 8));
    }
    std::string test;
    const int testR = g_testSeaR;
    std::vector<TestRaider> tr = TestRaidersOn(K, enemies, test);
    dl::At<int>(f, fPTW) = dl::At<int>(f, fETW) = 0;
    dl::At<int>(f, fFormEVP) = evp;
    dl::At<int>(rr[K], fSeaVP) = vp;
    void* lists[2] = {NewList(), NewList()};
    void* keep[2] = {dl::At<void*>(f, fFormMsgs), dl::At<void*>(f, fRaidSum)};
    dl::At<void*>(f, fFormMsgs) = lists[0];
    dl::At<void*>(f, fRaidSum) = lists[1];
    const bool pics = lockstep::BeginTextCapture();
    g_boxes.clear();
    warfx_boxCap = 1;
    SeaRes r;
    int ran = -1;
    if (game::HostViewEnter(K, kwar, human)) {
        ran = RunSeaGuarded(f, K, rr, r);
        game::HostViewLeave();
    }
    warfx_boxCap = 0;
    std::vector<std::wstring> pl = pics ? lockstep::EndTextCapture() : std::vector<std::wstring>();
    std::vector<std::wstring> tm = ListLines(lists[0]);
    for (const std::wstring& s : ListLines(lists[1]))  // the raiders' lines (K's own go to the summary list only)
        if (std::find(tm.begin(), tm.end(), s) == tm.end()) tm.push_back(s);
    dl::At<void*>(f, fFormMsgs) = keep[0];
    dl::At<void*>(f, fRaidSum) = keep[1];
    for (void* l : lists) dl::Call(mObjFree, (uint32_t)(uintptr_t)l);
    if (!tr.empty()) TestRaidersOff(testR, tr);
    const int kvp = dl::At<int>(rr[K], fSeaVP) - vp;  // anything the routines gave K's VP field (CheckResultOfSinkings: 0)
    dl::At<int>(rr[K], fSeaVP) = realVP + kvp;
    const int devp = dl::At<int>(f, fFormEVP) - evp;
    dl::At<int>(f, fFormEVP) = hEVP;
    dl::At<int>(f, fPTW) = hPTW;
    dl::At<int>(f, fETW) = hETW;
    for (int i = 0; areas && i < (int)marks.size() && i < dl::At<int>(areas, 8); i++) {
        void* a = dl::At<void**>(areas, 4)[i];
        dl::At<uint8_t>(a, fRaidAct) = (uint8_t)(marks[i] & 0xFF);
        dl::At<uint8_t>(a, fRaidType) = (uint8_t)(marks[i] >> 8);
    }
    std::string fuelLog;
    for (int n = 0; n < 9; n++) {
        for (int k = 0; k < 4; k++) Loss(rr[n], k) += saved[n][k];
        if (n == K) continue;
        uint8_t v = (uint8_t)fuel[n];  // CheckFuelShortage of the others ran with K's counter: theirs stays the host's,
        std::wstring nm = dl::ReadUStr(dl::At<void*>(rr[n], fNatName));  // unless the frame's sinkings cut their fuel
        for (const std::wstring& t : pl)
            if (!nm.empty() && t.rfind(L"Sinkings of merchant shipping", 0) == 0 && t.size() > nm.size() &&
                t.compare(t.size() - nm.size() - 1, nm.size(), nm) == 0)
                v = 1;
        if (dl::At<uint8_t>(rr[n], fFuel) != v) fuelLog += " " + std::to_string(n);
        dl::At<uint8_t>(rr[n], fFuel) = v;
    }
    std::string rec, perEnemy;
    int sumK = 0, sumE = 0, mK = 0, sK = 0, mE = 0, sE = 0;
    for (int j = 0; j < 9; j++) {
        if (j == K) continue;
        if (r.gainK[j] > 0) hookcb::CreditWarVP(K, j, r.gainK[j], "trade war");
        if (r.gainE[j] > 0) hookcb::CreditWarVP(j, K, r.gainE[j], "trade war");
        sumK += r.gainK[j];
        sumE += r.gainE[j];
        mK += r.raidK[j];
        sK += r.subK[j];
        mE += r.raidE[j];
        sE += r.subE[j];
        if (std::find(enemies.begin(), enemies.end(), j) == enemies.end() && !(r.raidK[j] | r.subK[j] | r.raidE[j] | r.subE[j]))
            continue;
        perEnemy += " " + std::to_string(j) + ": raiders " + std::to_string(r.raidK[j]) + "/" + std::to_string(r.raidE[j]) +
                    ", subs " + std::to_string(r.subK[j]) + "/" + std::to_string(r.subE[j]) + ", VP +" +
                    std::to_string(r.gainK[j]) + "/+" + std::to_string(r.gainE[j]) + ";";
    }
    std::vector<std::wstring> news = pl;
    news.insert(news.end(), g_boxes.begin(), g_boxes.end());
    news.insert(news.end(), tm.begin(), tm.end());
    if (mK + sK + mE + sE > 0) {
        wchar_t b[400];
        swprintf(b, 400,
                 L"Trade war this month: our raiders sank %d enemy merchant ships and our submarines %d (+%d VP); enemy "
                 L"raiders sank %d of ours and enemy submarines %d (+%d VP to the enemy).",
                 mK, sK, sumK, mE, sE, sumE);
        news.push_back(b);
    }
    for (const std::wstring& t : news) News(K, t);
    char hd[200];
    snprintf(hd, sizeof(hd), "war counter %d, war score %d:%d, %s%s; calls %d, faults %d", kwar, vp, evp,
             ran == 1 ? "its view" : ran == 0 ? "its view (an exception: stopped)" : "NO VIEW (nothing run)",
             FlagOf(rr[K]) ? ", blockaded (no enemy raiders)" : "", r.calls, r.faults);
    rec = std::string(hd) + "; (raiders K/enemy, subs K/enemy, VP K/enemy)" + perEnemy + test +
          " messages " + std::to_string(news.size()) + " (pictures " + std::to_string(pl.size()) + ", boxes " +
          std::to_string(g_boxes.size()) + ", turn messages " + std::to_string(tm.size()) + ")" +
          "; unattributed VP " + std::to_string(r.other) + ", K's VP field " + std::to_string(kvp) + ", EnemyVP " +
          std::to_string(devp) + "; fuel of others changed:" + (fuelLog.empty() ? " none" : fuelLog);
    g_sea[K] = rec;
    Log("warfx: sea war of nation %d (enemies %s): %s", K, Join(enemies).c_str(), rec.c_str());
    g_boxes.clear();
}

void SeaWar(void* f) {
    g_sea.clear();
    if (g_aiSubDrops)
        Log("warfx: sea war: the computer's submarine sinkings dropped this month for joined nations' computer pairs: %d "
            "(%s)", g_aiSubDrops, g_aiSubPairs.c_str());
    g_aiSubDrops = 0;
    g_aiSubPairs.clear();
    for (int K = 1; K <= 8 && K < game::NationCount(); K++) {
        if (!hookcb::PlayerRulesNation(K)) continue;
        int kwar = hookcb::KWarCounter(K);
        if (kwar <= 1) continue;  // the game's War > 1
        std::vector<int> enemies;
        for (int j = 1; j <= 8 && j < game::NationCount(); j++)
            if (j != K && !hookcb::IsHumanRemoteNation(j) && game::AtWar(K, j)) enemies.push_back(j);
        if (!enemies.empty()) SeaFrame(f, K, kwar, enemies);
    }
}

void SafeSeaWar(void* f) {
    __try {
        SeaWar(f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        warfx_boxCap = 0;
        Log("warfx: sea war failed (exception)");
    }
}

// A pair of CheckAIvsAISubmarineSinking whose submarine war a frame runs: a joined nation under the player's rules with
// its counter above 1 and a computer nation at war with it.
bool SeaPair(int a, int b) {
    for (int s = 0; s < 2; s++) {
        int K = s ? b : a, j = s ? a : b;
        if (K >= 1 && j >= 1 && hookcb::PlayerRulesNation(K) && hookcb::KWarCounter(K) > 1 &&
            !hookcb::IsHumanRemoteNation(j) && game::AtWar(K, j))
            return true;
    }
    return false;
}
}  // namespace

extern "C" void __stdcall warfx_SeaWarCB(void* form) {
    if (g_seaOn && form) SafeSeaWar(form);
}
// DoStrategicTurn, after its war block (EBP: its frame, [ebp-4] = Self): the frames, then the game's `call <War > 0>`.
extern "C" __declspec(naked) void warfx_dSeaWar() {
    __asm {
        pushad
        push dword ptr [ebp - 4]
        call warfx_SeaWarCB
        popad
        jmp dword ptr [warfx_oSeaWar]
    }
}

extern "C" void __stdcall warfx_BoxCB(const void* text) {
    g_boxes.push_back(text ? dl::ReadUStr(text) : L"");
    Log("warfx: sea war: box recorded for the nation's report: '%s'", W2U(g_boxes.back()).c_str());
}
// ShowMessagePos(Msg=EAX, X=EDX, Y=ECX) (ShowMessage's, CheckRaiderProblem's): recorded while a frame runs.
extern "C" __declspec(naked) void warfx_dMsgPos() {
    __asm {
        cmp byte ptr [warfx_boxCap], 0
        jne record
        jmp dword ptr [warfx_oMsgPos]
    record:
        push eax
        call warfx_BoxCB
        ret
    }
}

extern "C" int __stdcall warfx_AISubCB(void* a, void* b) {
    if (!g_seaOn) return 0;
    int x = IndexOf(a), y = IndexOf(b);
    if (x < 1 || y < 1 || !SeaPair(x, y)) return 0;
    g_aiSubDrops++;
    g_aiSubPairs += (g_aiSubPairs.empty() ? "" : " ") + std::to_string(x) + "-" + std::to_string(y);
    return 1;
}
// CheckAIvsAISubmarineSinking(Self=EAX, NationA=EDX, NationB=ECX): Boolean. A frame's pair: False (no sinking).
extern "C" __declspec(naked) void warfx_dAISub() {
    __asm {
        push eax
        push ecx
        push edx
        push ecx
        push edx
        call warfx_AISubCB
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz drop
        jmp dword ptr [warfx_oAISub]
    drop:
        xor eax, eax
        ret
    }
}

namespace {
std::string InstallSea() {
    const char* B = "TBuilderNation";
    const char* F = "TfrmBuildCamp";
    const char* lossNames[4] = {"MerchantsLostToSubsThisTurn", "MerchantsLostToRaidersThisTurn", "MoraleSinkingsThisTurn",
                                "FriendlySubsLostThisTurn"};
    for (int k = 0; k < 4; k++) fLoss[k] = dl::Field(B, lossNames[k]);
    fPTW = dl::Field(F, "PlayerTradeWarfareVPThisTurn");
    fETW = dl::Field(F, "EnemyTradeWarfareVPThisTurn");
    fFormEVP = dl::Field(F, "EnemyVP");
    fFormMsgs = dl::Field(F, "TurnMessages");
    fRaidSum = dl::Field(F, "RaiderSummary");
    fAreas = dl::Field(F, "MapAreas");
    fRaidAct = dl::Field("TMapArea", "RaiderActivity");
    fRaidType = dl::Field("TMapArea", "PlayerRaiderType");
    fSeaVP = dl::Field(B, "VP");
    fNatName = dl::Field(B, "NationName");
    fFuel = dl::Field(B, "FuelShortage");
    fSeaBuildArea = dl::Field(B, "BuildArea");
    fShipStatus = dl::Field("TCampaignShip", "Status");
    fShipLoc = dl::Field("TCampaignShip", "LocationArea");
    fShipUsed = dl::Field("TCampaignShip", "Used");
    mIsOper = dl::Method("TCampaignShip", "IsOperational");
    mSubAct = dl::Method(F, "CheckSubmarineActivity");
    mRaidSink = dl::Method(F, "CheckRaiderSinkings");
    mSinkResult = dl::Method(F, "CheckResultOfSinkings");
    mSLNew = dl::MethodFewestParams("TStringList", "Create");
    fSLItems = dl::Field("TStringList", "FList");
    fSLCount = dl::Field("TStringList", "FCount");
    mObjFree = dl::Method("TObject", "Free");
    slVmt = dl::ClassVmt("TStringList");
    uint8_t* raid = (uint8_t*)dl::Method(F, "CheckRaiderActivity");
    uint8_t* turn = (uint8_t*)dl::Method(F, "DoStrategicTurn");
    uint8_t* bwar = (uint8_t*)dl::Method(F, "CheckBuildingShipsForWar");
    uint8_t* aisub = (uint8_t*)dl::Method(F, "CheckAIvsAISubmarineSinking");
    uint8_t* problem = (uint8_t*)dl::Method(F, "CheckRaiderProblem");
    for (int x : fLoss)
        if (x < 0) return "fields";
    if (fPTW < 0 || fETW < 0 || fFormEVP < 0 || fFormMsgs < 0 || fRaidSum < 0 || fAreas < 0 || fRaidAct < 0 ||
        fRaidType < 0 || fSeaVP < 0 || fNatName < 0 || fFuel < 0 || fSeaBuildArea < 0 || fShipStatus < 0 ||
        fShipLoc < 0 || fShipUsed < 0 || fTension < 0 || fBlockaded < 0)
        return "fields";
    if (!mIsOper || !mSubAct || !mRaidSink || !mSinkResult || !mSLNew || !mObjFree || !slVmt || !raid || !turn ||
        !bwar || !aisub || !problem || fSLItems <= 0 || fSLCount <= 0)
        return "routines";
    // CheckRaiderActivity as reproduced in RunSea: the markers' reset, its two loops' tests, two CheckRaiderSinkings.
    std::vector<int> m1 = {0xC6, 0x80}, m2 = {0xC6, 0x80}, t = {0x83, 0xB8}, b = {0x80, 0xB8};
    Le(fRaidAct, m1);
    m1.push_back(0);
    Le(fRaidType, m2);
    m2.push_back(0x11);
    Le(fTension, t);
    t.push_back(0x28);
    Le(fBlockaded, b);
    b.push_back(0);
    int sinks = 0, tests = 0;
    for (int i = 0; i < 0x120; i++) {
        if (raid[i] == 0xE8 && CallTo(raid + i) == (uint8_t*)mRaidSink) sinks++;
        if (dl::FindPattern(raid + i, t.size(), t)) tests++;
    }
    if (!dl::FindPattern(raid, 0x60, m1) || !dl::FindPattern(raid, 0x60, m2) || !dl::FindPattern(raid, 0x120, b) ||
        sinks != 2 || tests != 2)
        return "CheckRaiderActivity's code";
    // DoStrategicTurn: `call <War > 0>; test al,al; je; cmp dword ptr [War],1; sete dl; mov eax,[ebp-4];
    // call CheckBuildingShipsForWar` (the War > 0 test after the war block).
    uint8_t* at = nullptr;
    for (int i = 0; i < 0x1000 && !at; i++) {
        uint8_t* p = turn + i;
        if (p[0] == 0xE8 && p[5] == 0x84 && p[6] == 0xC0 && p[7] == 0x74 && p[9] == 0x83 && p[10] == 0x3D &&
            *(int**)(p + 11) == game::WarCell() && p[15] == 1 && p[16] == 0x0F && p[17] == 0x94 && p[18] == 0xC2 &&
            p[19] == 0x8B && p[20] == 0x45 && p[21] == 0xFC && p[22] == 0xE8 && CallTo(p + 22) == bwar &&
            IsWarTest(CallTo(p)))
            at = p;
    }
    if (!at) return "DoStrategicTurn's code";
    // ShowMessagePos: CheckRaiderProblem's `or ecx,-1; or edx,-1; call` (push 0; push edx; push ecx; push 0; mov dl,4).
    uint8_t* pos = dl::FindPattern(problem, 0x800, {0x83, 0xC9, 0xFF, 0x83, 0xCA, 0xFF, 0xE8});
    uint8_t* smp = pos ? CallTo(pos + 6) : nullptr;
    if (!smp || !InImage(smp) || smp[0] != 0x6A || smp[1] != 0 || smp[2] != 0x52 || smp[3] != 0x51 || smp[4] != 0x6A ||
        smp[5] != 0 || smp[6] != 0xB2 || smp[7] != 4)
        return "ShowMessagePos";
    bool h1 = HookAt(smp, (void*)&warfx_dMsgPos, &warfx_oMsgPos, "ShowMessagePos");
    bool h2 = h1 && HookAt(aisub, (void*)&warfx_dAISub, &warfx_oAISub, "CheckAIvsAISubmarineSinking");
    bool h3 = h2 && HookAt(at, (void*)&warfx_dSeaWar, &warfx_oSeaWar, "DoStrategicTurn (sea war)");
    if (!h3) {
        if (h2) MH_RemoveHook(aisub);
        if (h1) MH_RemoveHook(smp);
        return "hooks";
    }
    g_seaOn = true;
    Log("warfx: the player's war effects for joined nations: raiders, submarines and the trade war ok (DoStrategicTurn "
        "%#x, ShowMessagePos %#x, CheckAIvsAISubmarineSinking %#x)",
        (unsigned)((uintptr_t)at - dl::g_base), (unsigned)((uintptr_t)smp - dl::g_base),
        (unsigned)((uintptr_t)aisub - dl::g_base));
    return "";
}
}  // namespace

bool Install() {
    fBlockaded = dl::Field("TBuilderNation", "Blockaded");
    fBM = dl::Field("TBuilderNation", "BudgetModifier");
    fUnrest = dl::Field("TBuilderNation", "UnrestLevel");
    fTension = dl::Field("TBuilderNation", "Tension");
    fShips = dl::Field("TBuilderNation", "ShipList");
    fDest = dl::Field("TCampaignShip", "DestinationArea");
    mIsBlockaded = dl::Method("TBuilderNation", "IsBlockaded");
    mBlkStrength = dl::Method("TBuilderNation", "GetBlockadeStrength");  // the `warfx strength` probe only
    fBuildArea = dl::Field("TBuilderNation", "BuildArea");
    fBlockadeArea = dl::Field("TBuilderNation", "BlockadeArea");
    uint8_t* check = (uint8_t*)dl::Method("TfrmBuildCamp", "CheckBlockadeStatus");
    uint8_t* effects = (uint8_t*)dl::Method("TfrmBuildCamp", "DoBlockadeEffects");
    uint8_t* unrest = (uint8_t*)dl::Method("TfrmBuildCamp", "CheckUnrestChanges");
    uint8_t* stop = (uint8_t*)dl::Method("TfrmBuildCamp", "StopBlockadedMoves");
    std::string why;
    std::string wy = InstallWeary(unrest);
    if (!wy.empty())
        Log("warfx: the player's war effects for joined nations: war weariness UNAVAILABLE (%s; the game's own rule)",
            wy.c_str());
    std::string bw = InstallBuild();
    if (!bw.empty())
        Log("warfx: the player's war effects for joined nations: building for war UNAVAILABLE (%s; the host's War for "
            "every nation)",
            bw.c_str());
    std::string sw = InstallSea();
    if (!sw.empty())
        Log("warfx: the player's war effects for joined nations: raiders, submarines and the trade war UNAVAILABLE (%s; "
            "the host's wars only)",
            sw.c_str());
    if (fBlockaded < 0 || fBM < 0 || fUnrest < 0 || fTension < 0 || fShips < 0 || fDest < 0 || !mIsBlockaded || !check ||
        !effects || !unrest || !stop || !game::WarCell())
        why = "fields or routines";
    // Random and FleetSize: CheckBlockadeStatus' `mov eax,3; call Random; add eax,[FleetSize]`
    if (why.empty()) {
        uint8_t* p = dl::FindPattern(check, 0x180, {0xB8, 3, 0, 0, 0, 0xE8, -1, -1, -1, -1, 0x03, 0x05, -1, -1, -1, -1});
        uint8_t* r = p ? CallTo(p + 5) : nullptr;
        if (r && InImage(r) && r[0] == 0x50 && r[1] == 0xFF && r[2] == 0x15) {
            mRandom = r;
            fsCell = (int*)(uintptr_t) * (uint32_t*)(p + 12);
        }
        if (!mRandom || !InImage(fsCell)) why = "Random / FleetSize";
    }
    // The routines as reproduced: DoBlockadeEffects' flag test, Random(2) and Random(40); the loop's rule (`cmp byte ptr
    // [eax+Blockaded],0; je; mov eax,100; call Random; cmp eax,5`); its one call of DoBlockadeEffects.
    if (why.empty()) {
        auto d = [](int v, std::vector<int>& p) {
            for (int i = 0; i < 4; i++) p.push_back((v >> (8 * i)) & 0xFF);
        };
        std::vector<int> eflag = {0x80, 0xBB};
        d(fBlockaded, eflag);
        eflag.push_back(0);
        std::vector<int> rule = {0x80, 0xB8};
        d(fBlockaded, rule);
        for (int b : {0x00, 0x0F, 0x84, -1, -1, -1, -1, 0xB8, 0x64, 0, 0, 0, 0xE8, -1, -1, -1, -1, 0x83, 0xF8, 0x05})
            rule.push_back(b);
        bool ok = dl::FindPattern(effects, 0x60, eflag) && dl::FindPattern(effects, 0x80, {0xB8, 2, 0, 0, 0, 0xE8}) &&
                  dl::FindPattern(effects, 0x140, {0xB8, 0x28, 0, 0, 0, 0xE8}) && dl::FindPattern(unrest, 0x1400, rule);
        int calls = 0;
        for (int i = 0; i < 0x400; i++)
            if (unrest[i] == 0xE8 && CallTo(unrest + i) == effects) {
                calls++;
                retEffects = (uintptr_t)(unrest + i + 5);
            }
        if (!ok || calls != 1) why = "the routines' code";
    }
    if (why.empty()) {
        bool e = HookAt(effects, (void*)&warfx_dEffects, &warfx_oEffects, "DoBlockadeEffects");
        bool u = e && HookAt(unrest, (void*)&warfx_dUnrest, &g_oUnrest, "CheckUnrestChanges");
        bool s = u && HookAt(stop, (void*)&warfx_dStop, &g_oStop, "StopBlockadedMoves");
        if (!s) {
            if (u) MH_RemoveHook(unrest);
            if (e) MH_RemoveHook(effects);
            why = "hooks";
        }
    }
    if (!why.empty()) {
        Log("warfx: the player's war effects for joined nations (blockades): UNAVAILABLE (%s)", why.c_str());
        return false;
    }
    game::SetBlockadeCheckedHook(&OnChecked);
    g_on = true;
    Log("warfx: the player's war effects for joined nations: blockades ok (DoBlockadeEffects %#x, CheckUnrestChanges "
        "%#x, StopBlockadedMoves %#x, Random %#x, FleetSize %#x)",
        (unsigned)((uintptr_t)effects - dl::g_base), (unsigned)((uintptr_t)unrest - dl::g_base),
        (unsigned)((uintptr_t)stop - dl::g_base), (unsigned)((uintptr_t)mRandom - dl::g_base),
        (unsigned)((uintptr_t)fsCell - dl::g_base));
    return true;
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

std::string BlockadedList() {
    std::string s;
    if (fBlockaded < 0) return s;
    for (int n = 0; n <= 8 && n < game::NationCount(); n++) {
        void* r = game::Nation(n);
        if (r && FlagOf(r)) s += std::to_string(n) + ",";
    }
    return s;
}

std::string ClientApplyBlockade(const std::string& list) {
    if (fBlockaded < 0) return "unavailable";
    std::set<int> want;
    for (auto& e : Split(list, ','))
        if (!e.empty()) want.insert(atoi(e.c_str()));
    std::string changed;
    for (int n = 0; n <= 8 && n < game::NationCount(); n++) {
        void* r = game::Nation(n);
        if (!r) continue;
        uint8_t v = want.count(n) ? 1 : 0;
        if (FlagOf(r) != v) {
            changed += " " + std::to_string(n) + (v ? "+" : "-");
            FlagOf(r) = v;
        }
    }
    return changed.empty() ? "unchanged" : "changed" + changed;
}

void ClearState() {
    g_news.clear();
    g_rec.clear();
    g_frames.clear();
    g_month = false;
    g_cleared = -1;
    for (int n = 0; n < 9; n++) g_frameFlag[n] = g_hostFlag[n] = false;
    g_lastCheck.clear();
    g_lastStop.clear();
    g_weary.clear();
    g_build.clear();
    g_sea.clear();
}

std::string BridgeCommand(const std::vector<std::string>& args) {
    if (!g_on) return "error warfx unavailable";
    if (args.size() >= 2 && args[1] == "strength") return Strength(args);
    std::string s = "ok on=1 flags=" + BlockadedList() + " check=" + (g_lastCheck.empty() ? "-" : g_lastCheck);
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    s += " | stop=" + (g_lastStop.empty() ? std::string("-") : g_lastStop);
    if (args.size() >= 2) {
        int K = atoi(args[1].c_str());
        auto it = g_rec.find(K);
        auto fr = g_frames.find(K);
        s += " | nation=" + std::to_string(K);
        if (fr != g_frames.end())
            s += " frame=1 kwar=" + std::to_string(fr->second.kwar) + " blockaded=" +
                 (fr->second.blockaded ? "1" : "0") + " blockers=" + Join(fr->second.blockers) +
                 " enemies=" + Join(fr->second.enemies);
        else
            s += " frame=0";
        s += " rec=" + (it == g_rec.end() || it->second.empty() ? std::string("-") : it->second);
        auto nw = g_news.find(K);
        s += " news=" + std::to_string(nw == g_news.end() ? 0 : (int)nw->second.size());
        auto wy = g_weary.find(K);
        std::string w = wy == g_weary.end() ? std::string("-") : wy->second;
        for (char& c : w)
            if (c == ' ') c = '_';
        s += " weary=" + w;
        auto bw = g_build.find(K);
        if (bw != g_build.end()) {
            const BuildRec& b = bw->second;
            s += " build=subs:" + std::to_string(b.war[0]) + "/" + std::to_string(b.tests[0]) +
                 ",delays:" + std::to_string(b.war[1]) + "/" + std::to_string(b.tests[1]) +
                 ",budget:" + std::to_string(b.war[2]) + "/" + std::to_string(b.tests[2]) +
                 ",grants:" + std::to_string(b.grants);
        } else {
            s += " build=-";
        }
        auto sw = g_sea.find(K);
        std::string sea = sw == g_sea.end() ? std::string("-") : sw->second;
        for (char& c : sea)
            if (c == ' ') c = '_';
        s += " sea=" + sea;
    }
    s += " | wearyrule=" + std::string(g_wearyOn ? "1" : "0") + " buildrules=" + (g_buildOn ? "1" : "0") +
         " searule=" + (g_seaOn ? "1" : "0");
    return s;
}

std::string TestSea(const std::vector<std::string>& args) {
    if (args.size() < 3) return "error usage: warfxsea <nation 1-8> <n> (n <= 0: off)";
    int R = atoi(args[1].c_str()), n = atoi(args[2].c_str());
    if (R < 1 || R > 8) return "error usage: warfxsea <nation 1-8> <n> (n <= 0: off)";
    g_testSeaR = n > 0 ? R : -1;
    g_testSeaN = std::max(0, n);
    Log("warfx: TEST: the next sea war frame with nation %d: %d of its ships raid in the defender's build area", R,
        g_testSeaN);
    return "ok warfxsea=" + std::to_string(g_testSeaR) + ":" + std::to_string(g_testSeaN);
}

std::string TestBuilder(const std::vector<std::string>& args) {
    if (args.size() < 2) return "error usage: shipbuilder <nation> [<ship> <builder>]";
    int n = atoi(args[1].c_str());
    void* rec = n >= 0 && n <= 8 && n < game::NationCount() ? game::Nation(n) : nullptr;
    int fB = dl::Field("TCampaignShip", "BuildingNationIdx"), fId = dl::Field("TCampaignShip", "CShipId");
    void* isB = dl::Method("TCampaignShip", "IsBuilding");
    if (!rec || fB < 0 || fId < 0 || !isB) return "error no such nation or fields";
    if (args.size() >= 4) {
        void* sh = game::Ship(rec, atoi(args[2].c_str()));
        if (!sh) return "error no such ship";
        int before = dl::At<int>(sh, fB);
        dl::At<int>(sh, fB) = atoi(args[3].c_str());
        Log("warfx: TEST: nation %d's ship %s (id %d): BuildingNationIdx %d -> %d", n, args[2].c_str(),
            dl::At<int>(sh, fId), before, dl::At<int>(sh, fB));
    }
    std::string s = "ok";
    for (int i = 0; i < game::ShipCount(rec); i++) {
        void* sh = game::Ship(rec, i);
        if (!sh || !(dl::Call(isB, (uint32_t)(uintptr_t)sh) & 0xFF)) continue;
        s += " " + std::to_string(i) + ":" + std::to_string(dl::At<int>(sh, fId)) + ":" + std::to_string(dl::At<int>(sh, fB));
    }
    return s;
}

std::string TestRandom(const std::vector<std::string>& args) {
    if (args.size() < 2) return "error usage: warfxrand <v|-1>";
    g_testRand = std::max(-1, atoi(args[1].c_str()));
    Log("warfx: TEST: the war effects' Random(n) draws %s", g_testRand < 0 ? "the game's" : ("min(" + std::to_string(g_testRand) + ", n - 1)").c_str());
    return "ok warfxrand=" + std::to_string(g_testRand);
}

std::string TestBlockade(const std::vector<std::string>& args) {
    if (args.size() < 3) return "error usage: warfxblk <nation 0-8> <-1|0|1>";
    int n = atoi(args[1].c_str()), v = atoi(args[2].c_str());
    if (n < 0 || n > 8 || v < -1 || v > 1) return "error usage: warfxblk <nation 0-8> <-1|0|1>";
    g_testBlk[n] = v;
    Log("warfx: TEST: the frames' blockade test of nation %d: %s", n, v < 0 ? "the game's" : v ? "blockaded" : "not");
    std::string s = "ok";
    for (int i = 0; i < 9; i++) s += " " + std::to_string(g_testBlk[i]);
    return s;
}

}  // namespace warfx

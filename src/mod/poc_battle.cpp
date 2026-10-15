// Research commands for tactical battles (investigation 2026-10-14, see MODLOG). Reached only through `poc*` bridge
// commands. The battle engine (seeded RNG, contexts, state hash, minute gate) lives in lockstep.cpp; these commands
// drive it outside a live battle, plus a test entry point that starts a battle against a nation's war mission.
#include "poc_battle.h"
#include "delphi.h"
#include "game.h"
#include "lockstep.h"

namespace poc {

static uint32_t U(const void* p) { return (uint32_t)(uintptr_t)p; }

static uint8_t* Find(void* fn, size_t len, const std::vector<int>& pat) {
    return fn ? dl::FindPattern((uint8_t*)fn, len, pat) : nullptr;
}

static struct {
    bool tried = false, ok = false;
    void *mDoBattle = nullptr, *mLoadWarData = nullptr, *mResetUsedShips = nullptr, *mIsHomeArea = nullptr,
         *mGetPoss = nullptr, *mAreaForPoss = nullptr;
    int fWarInfo = -1, fBuildArea = -1, fStartYear = -1, fTmpPenalty = -1, fPause = -1;
    int warInfoSize = 0x1665, missionSize = 0x207;
} R;

static bool Resolve() {
    if (R.tried) return R.ok;
    R.tried = true;
    R.mDoBattle = dl::Method("TfrmBuildCamp", "DoBattle");
    R.mLoadWarData = dl::Method("TBuilderNation", "LoadWarData");
    R.mResetUsedShips = dl::Method("TfrmBuildCamp", "ResetUsedShips");
    R.mIsHomeArea = dl::Method("TMapArea", "IsHomeArea");
    R.mGetPoss = dl::Method("TfrmBuildCamp", "GetPossessionWithName");
    R.mAreaForPoss = dl::Method("TfrmBuildCamp", "GetAreaForPossession");
    R.fWarInfo = dl::Field("TBuilderNation", "WarInfo");
    R.fBuildArea = dl::Field("TBuilderNation", "BuildArea");
    R.fStartYear = dl::Field("TfrmBuildCamp", "CampaignStartYear");
    R.fTmpPenalty = dl::Field("TfrmBuildCamp", "TmpPenaltyPoints");
    R.fPause = dl::Field("TfrmMain", "PauseGame");
    // The record sizes of the war data (per enemy) and of a mission, as used by the debug menu's Test battle.
    void* test = dl::Method("TfrmBuildCamp", "Testbattle1Click");
    uint8_t* a = Find(test, 0x400, {0x69, -1, -1, 0x65, 0x16, 0x00, 0x00});
    uint8_t* b = Find(test, 0x400, {0x69, -1, -1, 0x07, 0x02, 0x00, 0x00});
    if (a && b) {
        R.warInfoSize = *(int*)(a + 3);
        R.missionSize = *(int*)(b + 3);
    } else {
        Log("poc: Testbattle1Click record sizes not found; using 0x1665/0x207");
    }
    R.ok = R.mDoBattle && R.mLoadWarData && R.mResetUsedShips && R.mIsHomeArea && R.mGetPoss && R.mAreaForPoss &&
           R.fWarInfo >= 0 && R.fBuildArea >= 0 && R.fStartYear >= 0;
    Log("poc resolve: %s", R.ok ? "ok" : "MISSING");
    return R.ok;
}

// ---------------------------------------------------------------- test battle
static uint8_t* Mission(int opp, int m) {
    return (uint8_t*)game::PlayerNation() + R.fWarInfo + (opp - 1) * R.warInfoSize + 4 + m * R.missionSize;
}

static bool LoadWar() {
    void* f = game::BuildCamp();
    void* pn = game::PlayerNation();
    if (!f || !pn || !R.mLoadWarData || R.fWarInfo < 0) return false;
    return (dl::Call(R.mLoadWarData, U(pn), dl::At<int>(f, R.fStartYear) == 1920 ? 1 : 0) & 0xFF) != 0;
}

static std::string Missions(int opp) {
    if (opp < 1 || opp > 8 || !LoadWar()) return "error war data";
    std::string out = "ok ";
    for (int m = 0; m < 10; m++) {
        uint8_t* ms = Mission(opp, m);
        bool loc = *(int*)(ms + 0x23) || *(int*)(ms + 0x27);
        out += std::to_string(m) + ":" + W2U(dl::ReadUStr(*(void**)ms)).substr(0, 40) + "~" +
               W2U(dl::ReadUStr(*(void**)(ms + 4))) + "~vp" + std::to_string(*(int*)(ms + 8)) + (loc ? "~loc" : "~noloc") +
               "; ";
    }
    return out;
}

// The debug menu's "Test battle" (numbered path) without its input dialogs: DoBattle against nation `opp` with its
// war mission `m`. The battle preview dialog and the battle screen follow (and a live battle, if `opp` is a joined
// player who fights live battles: the host's DoBattle is the start of host-vs-player battles).
static void StartBattle(int opp, int m) {
    void* f = game::BuildCamp();
    void* pn = game::PlayerNation();
    if (!f || !pn || opp < 1 || opp > 8 || m < 0 || m > 9 || !LoadWar()) {
        Log("pocbattle: not possible (form/war data)");
        return;
    }
    uint8_t* ms = Mission(opp, m);
    if (!*(int*)(ms + 0x23) && !*(int*)(ms + 0x27)) {
        Log("pocbattle: mission %d against %d has no location (Battle data not complete)", m, opp);
        return;
    }
    dl::Call(R.mResetUsedShips, U(f), (uint32_t)opp);
    if (R.fTmpPenalty >= 0) dl::At<int>(f, R.fTmpPenalty) = *(int*)(ms + 8);
    void* oppN = game::Nation(opp);
    void* area = nullptr;
    void* oppArea = dl::At<void*>(oppN, R.fBuildArea);
    void* myArea = dl::At<void*>(pn, R.fBuildArea);
    if (oppArea && (dl::Call(R.mIsHomeArea, U(oppArea), U(pn), 0) & 0xFF))
        area = oppArea;
    else if (myArea && (dl::Call(R.mIsHomeArea, U(myArea), U(oppN), 0) & 0xFF))
        area = myArea;
    if (!area) {
        void* poss = (void*)(uintptr_t)dl::Call(R.mGetPoss, U(f), *(uint32_t*)(ms + 4));
        if (poss) area = (void*)(uintptr_t)dl::Call(R.mAreaForPoss, U(f), U(poss));
    }
    if (!area) {
        Log("pocbattle: no area for mission %d against %d", m, opp);
        return;
    }
    Log("pocbattle: DoBattle(opp=%d, mission %d '%s', area %p)", opp, m, W2U(dl::ReadUStr(*(void**)ms)).c_str(), area);
    uint32_t st[2] = {U(area), 0};
    DWORD t0 = GetTickCount();
    uint32_t r = dl::Call(R.mDoBattle, U(f), (uint32_t)opp, U(ms), 2, st);
    Log("pocbattle: DoBattle returned %d after %lu ms; battle screen %s", (int)(r & 0xFF), GetTickCount() - t0,
        lockstep::BattleVisible() ? "open" : "closed");
}

// ---------------------------------------------------------------- commands
bool IsAsync(const std::string& c) { return c == "pocbattle" || c == "pocrun" || c == "pocsac" || c == "pocsavebtn"; }

std::string Command(const std::vector<std::string>& a) {
    auto arg = [&](size_t i, const std::string& def = "") { return i < a.size() ? a[i] : def; };
    const std::string& c = a[0];
    if (!lockstep::Resolve() && c != "pocresolve") return "error battle engine unavailable: " + lockstep::Missing();
    if (c == "pocresolve") return lockstep::Resolve() && Resolve() ? "ok" : "error " + lockstep::Missing();
    if (c == "pocrng") {  // pocrng <seed> [iso] | off
        if (arg(1) == "off") return lockstep::SetRng(0, 0), "ok off";
        uint32_t seed = (uint32_t)strtoul(arg(1, "1").c_str(), nullptr, 0);
        bool iso = arg(2) == "iso";
        if (!lockstep::EnsureHooks() && iso) return "error context hooks";
        lockstep::SetRng(iso ? 2 : 1, seed);
        Log("poc rng: seed %u mode %s", seed, iso ? "iso" : "single");
        return "ok seed=" + std::to_string(seed) + (iso ? " iso" : " single");
    }
    if (c == "pocrngstat") return lockstep::RngStats();
    if (c == "pochash") {  // pochash on|off|now
        std::string m = arg(1, "now");
        if (m == "on") {
            if (!lockstep::EnsureHooks()) return "error hooks";
            lockstep::PocHashOn(true);
            return "ok on";
        }
        if (m == "off") return lockstep::PocHashOn(false), "ok off";
        if (!lockstep::BattleForm()) return "error no battle";
        std::string line = lockstep::HashLine("poc hash now");
        Log("%s", line.c_str());
        return "ok " + line;
    }
    if (c == "pocgate") {  // pocgate on|off|step [n]
        std::string m = arg(1, "status");
        if (m == "on") {
            if (!lockstep::EnsureHooks()) return "error hooks";
            lockstep::PocGate(1, 0);
        } else if (m == "off") {
            lockstep::PocGate(0, 0);
        } else if (m == "step") {
            lockstep::PocGate(2, atoi(arg(2, "1").c_str()));
        }
        return lockstep::PocGateState();
    }
    if (c == "pocstate") return lockstep::StateText();
    if (c == "pocdump") return lockstep::DumpState(U2W(arg(1))) ? "ok" : "error dump";  // pocdump <file>
    if (c == "pocmissions") return Resolve() ? Missions(atoi(arg(1, "1").c_str())) : "error";
    if (c == "pocbattle") return Resolve() ? (StartBattle(atoi(arg(1, "1").c_str()), atoi(arg(2, "0").c_str())), "ok") : "error";
    void* m = lockstep::BattleForm();
    if (!m) return "error no battle";
    if (c == "pocrun") return lockstep::RunMinutes(atoi(arg(1, "1").c_str())) ? "ok" : "error";  // as the Run buttons
    if (c == "pocdiv") {  // pocdiv <index> <course> <speed> [ai]: what the course/speed boxes write (-1 = keep)
        bool ok = lockstep::SetDivisionOrder(atoi(arg(1, "-1").c_str()), atoi(arg(2, "-1").c_str()),
                                             atoi(arg(3, "-1").c_str()), a.size() > 4 ? atoi(arg(4).c_str()) : -1);
        return ok ? "ok" : "error division";
    }
    if (c == "pocsideai") {  // pocsideai <side> <ai 0|1> <human 0|1>: hand a whole side to the AI or to a human
        int side = atoi(arg(1, "1").c_str()), ai = atoi(arg(2, "0").c_str()), human = atoi(arg(3, "1").c_str());
        return lockstep::SetSideControl(side, ai != 0, human != 0) ? "ok" : "error";
    }
    if (c == "pocplayerside")  // pocplayerside <0|1>: the battle's human side (display test only)
        return lockstep::SetPlayerSide(atoi(arg(1, "0").c_str())) ? "ok side=" + arg(1, "0") : "error";
    if (c == "pocpause") return R.fPause >= 0 || Resolve() ? (dl::At<uint8_t>(m, dl::Field("TfrmMain", "PauseGame")) = 1, "ok") : "error";
    if (c == "pocspeed") {  // pocspeed <-3..5>: the speed box (simulation step size and timer intervals)
        lockstep::SetSpeed(atoi(arg(1, "1").c_str()));
        return lockstep::StateText();
    }
    if (c == "pocsavebtn") {  // the battle screen's Save button (async: "Exit game?" and "Game saved!" follow)
        if (void* fn = dl::Method("TfrmMain", "sbSaveClick")) dl::Call(fn, U(m), U(m));
        return "ok";
    }
    if (c == "pocsac") return lockstep::SaveSac(U2W(arg(1))) ? "ok" : "error";  // pocsac <file>: the battle save routine
    return "error unknown poc command";
}

}  // namespace poc

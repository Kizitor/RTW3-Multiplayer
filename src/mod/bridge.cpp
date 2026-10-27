// Development/test bridge: a line-based command socket on 127.0.0.1, only enabled when the environment
// variable RTW3MP_BRIDGE_PORT is set (and RTW3MP_BRIDGE_TOKEN, if set, must be the first line sent).
// Lets automated tests drive the mod without touching the user's mouse or keyboard.
#include "common.h"
#include "delphi.h"
#include "economy.h"
#include "game.h"
#include "lockstep.h"
#include "nationplay.h"
#include "net.h"
#include "poc_battle.h"
#include "session.h"
#include "ui.h"
#include "warfx.h"
#include <thread>
#include <atomic>
#include <condition_variable>

static std::string ExecuteOnMain(const std::string& line);
// `submit` runs asynchronously (the Turn's questions may show): status reports submitting=1 until it has returned.
static std::atomic<int> g_submitting{0};

// Runs a command on the game's main thread and waits for the reply. The job owns a copy of the line and shares
// its state with the waiter, so a job that only starts after the waiter gave up is skipped safely.
struct SyncJob {
    std::mutex mx;
    std::condition_variable cv;
    bool done = false, cancelled = false;
    std::string result;
};

static std::string RunSync(const std::string& line) {
    auto job = std::make_shared<SyncJob>();
    RunOnMainThread([job, line] {
        {
            std::lock_guard<std::mutex> lk(job->mx);
            if (job->cancelled) return;
        }
        std::string r;
        try {
            r = ExecuteOnMain(line);
        } catch (...) {
            r = "error exception";
        }
        std::lock_guard<std::mutex> lk(job->mx);
        job->result = r;
        job->done = true;
        job->cv.notify_one();
    });
    std::unique_lock<std::mutex> lk(job->mx);
    if (!job->cv.wait_for(lk, std::chrono::seconds(120), [&] { return job->done; })) {
        job->cancelled = true;
        return "error timeout (main thread busy)";
    }
    return job->result;
}

static BOOL CALLBACK ListWin(HWND h, LPARAM l) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h)) return TRUE;
    wchar_t cls[64], title[200];
    GetClassNameW(h, cls, 64);
    GetWindowTextW(h, title, 200);
    auto* out = (std::string*)l;
    char buf[64];
    snprintf(buf, sizeof(buf), "%p ", h);
    *out += buf + W2U(cls) + " \"" + W2U(title) + "\"" + (IsWindowEnabled(h) ? "" : " (disabled)") + "; ";
    return TRUE;
}

static std::string Execute(const std::string& line) {
    auto args = Split(Trim(line), ' ');
    const std::string& cmd = args[0];
    auto arg = [&](size_t i, const std::string& def = "") { return i < args.size() ? args[i] : def; };
    if (cmd == "ping") return "ok pong";
    // Calls that may open the game's modal dialogs run without waiting, so the test can answer them.
    if (cmd == "aipeace") {  // aipeace <nation> [badLoss]: the game's AI peace routine (through the hook)
        int nation = atoi(arg(1).c_str()), badLoss = atoi(arg(2, "0").c_str());
        RunOnMainThread([nation, badLoss] {
            void* fn = dl::Method("TfrmBuildCamp", "HandleAIPeace");
            void* n = game::Nation(nation);
            if (fn && n && game::BuildCamp())
                dl::Call(fn, (uint32_t)(uintptr_t)game::BuildCamp(), (uint32_t)(uintptr_t)n, (uint32_t)badLoss);
        });
        return "ok";
    }
    if (cmd == "applydiplo") return RunOnMainThread([] { mp::DebugApplyDiplomacy(); }), "ok";
    if (cmd == "copygame") {  // copygame <to slot>: the game's "Copy game" of the open slot (shows the game's dialogs)
        int to = atoi(arg(1, "0").c_str());
        if (to <= 0) return "error slot";
        RunOnMainThread([to] { game::CopyGame(to); });
        return "ok";
    }
    if (cmd == "researchopen") return RunOnMainThread([] { game::OpenResearch(); }), "ok";  // the Research button (modal)
    if (cmd == "loadasync") {  // loadasync <slot> [player]: `load` without waiting (a battle in progress may ask first)
        int slot = atoi(arg(1, "0").c_str()), player = atoi(arg(2, "0").c_str());
        if (slot <= 0) return "error slot";
        RunOnMainThread([slot, player] {
            Log("loadasync %d %d -> %s", slot, player, game::LoadCampaign(slot, player, false) ? "ok" : "error load");
        });
        return "ok";
    }
    if (cmd == "researchrun") {  // researchrun <nation> <count>: the game's monthly research, logged (may show messages)
        int n = atoi(arg(1, "0").c_str()), c = atoi(arg(2, "1").c_str());
        RunOnMainThread([n, c] { game::DebugDoResearch(n, c); });
        return "ok";
    }
    if (cmd == "eventnow") {  // eventnow [index]: client rolls this month's event now (shows the game's dialog)
        if (game::EventActive()) return "error an event dialog is open";
        int idx = atoi(arg(1, "-1").c_str());
        RunOnMainThread([idx] { mp::DebugEvent(idx); });
        return "ok";
    }
    if (cmd == "relturn") {  // relturn: client runs the player's relation routines now (their dialogs: async)
        if (game::EventActive()) return "error an event dialog is open";
        RunOnMainThread([] { mp::DebugRelTurn(); });
        return "ok";
    }
    if (cmd == "peacetest") {  // peacetest <chance> <lossGain> <force>: client, peace with the computer's nations now (async)
        if (game::EventActive()) return "error an event dialog is open";
        int chance = atoi(arg(1, "40").c_str()), lossGain = atoi(arg(2, "0").c_str());
        bool force = arg(3, "0") != "0";
        RunOnMainThread([chance, lossGain, force] { mp::DebugPeaceTest(chance, lossGain, force); });
        return "ok";
    }
    // submit: the client's Submit, which may ask the game's Turn questions (trade protection, foreign stations) first.
    if (cmd == "submit") {
        g_submitting++;
        RunOnMainThread([] {
            struct Done {
                ~Done() { g_submitting--; }
            } done;
            mp::SubmitTurn();
        });
        return "ok";
    }
    if (cmd == "intelcheck") {  // client: the monthly intelligence check now (the game's spy dialogs may show)
        if (game::EventActive()) return "error an event dialog is open";
        return RunOnMainThread([] { mp::DebugIntelCheck(); }), "ok";
    }
    if (cmd == "windows") {
        std::string out;
        EnumWindows(ListWin, (LPARAM)&out);
        return "ok " + out;
    }
    if (cmd.compare(0, 3, "poc") == 0 && poc::IsAsync(cmd)) {  // research POC (battles) that may open game dialogs
        RunOnMainThread([args] { Log("%s -> %s", args[0].c_str(), poc::Command(args).c_str()); });
        return "ok";
    }
    if (cmd == "ls" && lockstep::DebugIsAsync(args)) {  // live battles: commands that load or may open dialogs
        RunOnMainThread([args] { Log("ls %s -> %s", args.size() > 1 ? args[1].c_str() : "", mp::DebugLockstep(args).c_str()); });
        return "ok";
    }
    // TEST ONLY (0.6.0): armstreaty conference <chance> (host: the game's CreateArmsControlTreaty, its dialogs) and
    // armstreaty refresh (the screen's refresh: at war the game's CancelTreaty, with its box on the host)
    if (cmd == "armstreaty" && (arg(1) == "conference" || arg(1) == "refresh")) {
        if (game::EventActive()) return "error an event dialog is open";
        if (arg(1) == "conference") {
            int chance = atoi(arg(2, "100").c_str());
            RunOnMainThread([chance] { game::TestArmsConference(chance); });
        } else {
            RunOnMainThread([] { game::RefreshUI(); });
        }
        return "ok";
    }
    return RunSync(line);
}

static std::string ExecuteOnMain(const std::string& line) {
    auto args = Split(Trim(line), ' ');
    const std::string& cmd = args[0];
    auto arg = [&](size_t i, const std::string& def = "") { return i < args.size() ? args[i] : def; };
    // While a joined player's event dialog is open the nation list is reordered: only read-only commands.
    static const std::set<std::string> kReadOnly = {"status", "eventlast", "eventlist", "windows", "diploview", "relview",
                                                    "rellast", "peacelast", "kwarvp", "battleptrs", "pocheapcheck"};
    if (game::EventActive() && !kReadOnly.count(cmd)) return "error an event dialog is open";
    // pocheapcheck (diagnosis, read only): the campaign's objects through RTTI (strings, object references)
    if (cmd == "pocheapcheck") return "ok " + game::PocHeapCheck("bridge");
    if (cmd.compare(0, 3, "poc") == 0) return poc::Command(args);  // research POC (battles), see poc_battle.cpp
    {
        std::string err;
        if (cmd == "eventchance") return mp::DebugEventChance(atoi(arg(1, "33").c_str())), "ok";
        if (cmd == "status") {
            mp::View v = mp::GetView();
            KV kv;
            kv["role"] = v.role == mp::Role::None ? "none" : v.role == mp::Role::Host ? "host" : "client";
            kv["phase"] = v.phase == mp::Phase::Lobby ? "lobby" : v.phase == mp::Phase::Planning ? "planning" : "processing";
            kv["myNation"] = std::to_string(v.myNation);
            kv["date"] = v.date;
            kv["status"] = v.status;
            kv["players"] = std::to_string(v.players.size());
            kv["submitted"] = v.submitted ? "1" : "0";
            kv["submitting"] = g_submitting > 0 ? "1" : "0";
            kv["remaining"] = std::to_string(v.remainingSec);
            kv["campaignReady"] = v.campaignReady ? "1" : "0";
            kv["slot"] = std::to_string(game::CurrentSlot());
            kv["startBuild"] = std::to_string(game::StartBuildPhase());
            kv["playerIdx"] = std::to_string(game::PlayerIndex());
            kv["busy"] = game::IsBusy() ? "1" : "0";
            kv["battle"] = game::BattleActive() ? "1" : "0";
            std::string s;
            for (auto& e : kv) s += e.first + "=" + e.second + "|";
            return "ok " + s;
        }
        if (cmd == "nations") {
            std::string s;
            for (int i = 0; i < game::NationCount(); i++)
                s += std::to_string(i) + ":" + W2U(game::NationName(i)) + ":funds=" + std::to_string(game::NationFunds(i)) +
                     ":ships=" + std::to_string(game::ShipCount(game::Nation(i))) + "; ";
            return "ok " + s;
        }
        if (cmd == "host") {
            bool ok = mp::Host(arg(1, "Host"), atoi(arg(2, "47624").c_str()), "", atoi(arg(3, "0").c_str()), err);
            return ok ? "ok" : "error " + err;
        }
        if (cmd == "join") {
            bool ok = mp::Join(arg(1, "Client"), arg(2, "127.0.0.1"), atoi(arg(3, "47624").c_str()), "", err);
            return ok ? "ok" : "error " + err;
        }
        if (cmd == "leave") return mp::Leave(), "ok";
        if (cmd == "start") return mp::StartSession(), "ok";
        if (cmd == "advance") return mp::AdvanceNow(), "ok";
        if (cmd == "claim") {  // claim <nation name or campaign index>
            std::string a = line.size() > 6 ? Trim(line.substr(6)) : "";
            if (!a.empty() && isdigit((unsigned char)a[0]))
                for (auto& n : mp::GetView().nations)
                    if (n.index == atoi(a.c_str())) a = n.name;
            return mp::ClaimNation(a), "ok";
        }
        if (cmd == "release") return mp::ReleaseNation(atoi(arg(1).c_str())), "ok";
        if (cmd == "limit") return mp::SetTurnLimit(atoi(arg(1).c_str())), "ok";
        if (cmd == "chat") return mp::SendChat(line.size() > 5 ? line.substr(5) : ""), "ok";
        if (cmd == "turn") return game::ClickTurnButton(), "ok";
        if (cmd == "save") return game::SaveGame() ? "ok" : "error save";
        // battleptrs (read only): the last battle's area and possession the game holds (nil after every load, 0.6.0)
        if (cmd == "battleptrs") return game::BattlePointersState();
        if (cmd == "load")  // load <slot> [player] [mod]: as the player's own load; `mod`: as the mod's own reload
            return game::LoadCampaign(atoi(arg(1).c_str()), atoi(arg(2, "0").c_str()), arg(3, "") == "mod") ? "ok"
                                                                                                            : "error load";
        if (cmd == "setplayer") return game::SetPlayerNation(atoi(arg(1).c_str())), "ok";
        if (cmd == "mpwindow") return ui::ShowMpWindow(), "ok";
        if (cmd == "claimui") return ui::DebugClaimWindow(arg(1, "state"), atoi(arg(2, "-1").c_str()));  // state | toggle <row>
        if (cmd == "savedir") return "ok " + W2U(game::SaveDir(atoi(arg(1, "1").c_str())));
        // --- diplomacy ---
        if (cmd == "diplo") {  // diplo <war|ally|peace|leave|cancel> <target nation>
            std::string why = mp::Diplomacy(arg(1), atoi(arg(2, "-1").c_str()));
            return why.empty() ? "ok" : "error " + why;
        }
        if (cmd == "diploas") {  // host: diploas <from nation> <action> <target nation> [extra]
            std::string extra;
            size_t pos = 0;
            for (int i = 0; i < 4 && pos != std::string::npos; i++) pos = line.find(' ', pos + 1);
            if (pos != std::string::npos) extra = Trim(line.substr(pos + 1));
            std::string why = mp::DebugDiplomacyAs(atoi(arg(1).c_str()), arg(2), atoi(arg(3, "-1").c_str()), extra);
            return why.empty() ? "ok" : "error " + why;
        }
        if (cmd == "claimland") {  // claimland <loser nation> <name|name|...>: the winner of an agreed peace claims land
            size_t pos = line.find(' ');
            pos = pos == std::string::npos ? pos : line.find(' ', pos + 1);
            std::string names = pos == std::string::npos ? "" : Trim(line.substr(pos + 1));
            std::string why = mp::Diplomacy("claim", atoi(arg(1, "-1").c_str()), names);
            return why.empty() ? "ok" : "error " + why;
        }
        if (cmd == "warvp") {  // host: warvp <a> <b> [vpA vpB]: read or set a player war's victory points
            bool set = args.size() >= 5;
            return mp::DebugWarVP(atoi(arg(1).c_str()), atoi(arg(2).c_str()), atoi(arg(3, "0").c_str()),
                                  atoi(arg(4, "0").c_str()), set);
        }
        if (cmd == "terms") return mp::DebugTerms(atoi(arg(1).c_str()), atoi(arg(2).c_str()));  // host: terms <a> <b>
        if (cmd == "vpinfo") return mp::DebugVPInfo();
        if (cmd == "kwar") return mp::DebugKWar(args);  // relations v2: the joined nations' war counters
        if (cmd == "relrules") return mp::DebugRelRules(args);  // relations v2: nations under the player's rules
        if (cmd == "warline") return mp::DebugWarLine(args);    // 0.6.1: players' bars and the war line's memory
        if (cmd == "rellast") return mp::DebugRelLast();        // relations v2: the last relation turn
        if (cmd == "peaceevent")  // B4: the next turn's peace event [the chance of its NegotiatePeace]
            return mp::DebugPeaceEvent(atoi(arg(1, "0").c_str()), atoi(arg(2, "0").c_str()));
        if (cmd == "peacelast") return mp::DebugPeaceLast();    // B4: the last peace with the computer's nations
        if (cmd == "kwarvp") return mp::DebugKWarVP(args);      // B4 (host): a joined nation's computer war scores
        if (cmd == "findship") {  // findship <ship id>: the nations whose ship list holds this ship (CShipId)
            int id = atoi(arg(1, "0").c_str()), fid = dl::Field("TCampaignShip", "CShipId"),
                fname = dl::Field("TCampaignShip", "ShipName");
            if (fid < 0 || fname < 0) return "error unavailable";
            std::string s;
            for (int n = 0; n < game::NationCount(); n++)
                for (int i = 0; i < game::ShipCount(game::Nation(n)); i++) {
                    void* ship = game::Ship(game::Nation(n), i);
                    if (ship && dl::At<int>(ship, fid) == id)
                        s += std::to_string(n) + ":" + std::to_string(i) + ":" + W2U(dl::ReadUStr(dl::At<void*>(ship, fname))) + "; ";
                }
            return "ok " + s;
        }
        if (cmd == "research") return game::DebugResearch(atoi(arg(1, "0").c_str()));  // research <nation>: pct and priorities
        if (cmd == "inv") return mp::DebugInvasions(args);  // 0.7.0: joined nations' invasions (session.cpp)
        if (cmd == "battles") return mp::DebugBattles(args);  // joined players' battles (see session.cpp)
        if (cmd == "ls") return mp::DebugLockstep(args);      // live player-vs-player battles (see lockstep.cpp)
        if (cmd == "fightbattles") return mp::SetFightBattles(arg(1, "1") != "0"), "ok";  // before join
    // 0.7.0, the legacy fleet: `legacy [0|1]` the joined player's choice (read when a new campaign's first month
    // arrives); `legacyallow [0|1]` the host's option; `legacystate`: the build state of this PC's game (read only)
    if (cmd == "legacy") {
        if (!arg(1, "").empty()) mp::SetLegacyWanted(arg(1) != "0");
        return std::string("ok wanted=") + (mp::LegacyWanted() ? "1" : "0");
    }
    if (cmd == "legacyallow") return mp::SetLegacyAllowed(arg(1, "1") != "0"), "ok";
    if (cmd == "legacystate")
        return std::string("ok available=") + (game::LegacyAvailable() ? "1" : "0") +
               " active=" + (game::LegacyActive() ? "1" : "0") + " " + game::LegacyFleetInfo();
    // vcl <form class> <control field | ->: where a form's published control is (tests drive the game's own screens
    // with posted messages): its window, or its parent's window and its rectangle there (read only)
    if (cmd == "vcl") return game::DescribeControl(arg(1).c_str(), arg(2, "-").c_str());
        if (cmd == "enemyvpadd")  // host: enemyvpadd <n>: the host's enemies gain n VP outside a battle (as a blockade)
            return game::ShowEnemyVP(game::EnemyVP() + atoi(arg(1, "0").c_str())), mp::DebugVPInfo();
        if (cmd == "battlevp")  // host: battlevp <opponent> <vp host> <vp opponent> [Major|Minor|Marginal]: end-of-battle routine
            return game::DebugBattleAftermath(atoi(arg(1).c_str()), atoi(arg(2, "0").c_str()), atoi(arg(3, "0").c_str()),
                                              arg(4, "Minor"));
        if (cmd == "possessions" || cmd == "claimable") {  // possessions <nation> | claimable <winner> <loser>
            auto list = cmd == "possessions" ? game::Possessions(atoi(arg(1).c_str()))
                                             : game::ClaimablePossessions(atoi(arg(1).c_str()), atoi(arg(2).c_str()));
            std::string s;
            for (auto& p : list) s += W2U(p.name) + "~" + W2U(p.area) + "~" + std::to_string(p.value) + (p.oil ? "~oil" : "") + "; ";
            return "ok " + std::to_string(list.size()) + " " + s;
        }
        if (cmd == "reserve") return mp::DebugReserve(atoi(arg(1).c_str()), arg(2, "Bot")), "ok";
        if (cmd == "designs") {  // designs <nation>: index:class name:ReadyForBuild (months left in the design study)
            int n = atoi(arg(1).c_str());
            std::string s;
            for (int i = 0; i < game::DesignCount(n); i++)
                s += std::to_string(i) + ":" + W2U(game::DesignName(n, i)) + ":" +
                     std::to_string(game::DesignReadyForBuild(n, i)) + "; ";
            return "ok " + s;
        }
        if (cmd == "setready") {  // setready <nation> <design index> <months>
            int n = atoi(arg(1).c_str()), i = atoi(arg(2).c_str());
            if (game::DesignReadyForBuild(n, i) < 0) return "error no such design";
            game::SetDesignReadyForBuild(n, i, atoi(arg(3).c_str()));
            return "ok " + std::to_string(game::DesignReadyForBuild(n, i));
        }
        if (cmd == "eventlist") {  // eventlist: index:condition:H (needs the host) or -:caption start
            std::string s;
            for (int i = 0; i < 100; i++) {
                int cond = 0;
                std::wstring cap;
                bool hostOnly = false;
                if (!game::GetEventInfo(i, cond, cap, hostOnly)) continue;
                std::string c = W2U(cap.substr(0, 40));
                for (char& ch : c)
                    if (ch == ';' || ch == '|') ch = ',';
                s += std::to_string(i) + ":" + std::to_string(cond) + ":" + (hostOnly ? "H" : "-") + ":" + c + "; ";
            }
            return s.empty() ? "error unavailable" : "ok " + s;
        }
        if (cmd == "eventlast") return "ok " + mp::DebugLastEvent();
        if (cmd == "intel") {  // intel: entries in the in-memory intel report list and distinct non-empty ones
            int unique = 0, count = game::IntelReportCount(&unique);
            return count < 0 ? "error unavailable" : "ok count=" + std::to_string(count) + " unique=" + std::to_string(unique);
        }
        // Per-player intelligence and trade protection (nationplay.cpp).
        if (cmd == "intelinfo") return nplay::IntelInfo();  // fields, cost, labels, reports (+ the host's records)
        if (cmd == "tradecheck") return nplay::TradeCheck(atoi(arg(1, "0").c_str()));  // [nation]: patrols, stations
        if (cmd == "shipstatus")  // shipstatus <nation> <ship> [status]: a ship's TShipStatus (6 = foreign stations)
            return nplay::ShipStatus(atoi(arg(1).c_str()), atoi(arg(2).c_str()), atoi(arg(3, "-1").c_str()));
        if (cmd == "intelbtn") return nplay::IntelButton(atoi(arg(1).c_str()));  // <tag>: the panel's +/- button
        if (cmd == "fsmonth")  // fsmonth <nation> [count]: host, the month's foreign stations/patrols check for it now
            return nplay::MonthCheckRun(atoi(arg(1).c_str()), std::max(1, std::min(50, atoi(arg(2, "1").c_str()))));
        if (cmd == "airunits") {  // airunits: the air units in the game's global list (all nations)
            int count = game::AirUnitCount();
            return count < 0 ? "error unavailable" : "ok count=" + std::to_string(count);
        }
        // Campaign divisions (0.6.1, read only): divs: the in-memory list (Fleet organization) with PlayerNation's index;
        // divstore <K> (host): nation K's list the host keeps from its turns
        if (cmd == "divs") return game::CampaignDivisionsInfo();
        if (cmd == "divstore") return nplay::HostDivisionsInfo(atoi(arg(1, "0").c_str()));
        if (cmd == "doctrine") {  // doctrine <nation>: training priorities, pending ones, months left; missile storage
            game::DoctrineState d = game::GetDoctrine(atoi(arg(1).c_str()));
            if (!d.valid) return "error unavailable";
            auto four = [](const uint8_t* b) {
                return std::to_string(b[0]) + "," + std::to_string(b[1]) + "," + std::to_string(b[2]) + "," + std::to_string(b[3]);
            };
            return "ok training=" + four(d.training) + " pending=" + four(d.pending) + " months=" +
                   std::to_string(d.pendingMonths) + " missiles=" + std::to_string(d.missileStorage) + " pendingMissiles=" +
                   std::to_string(d.pendingMissileStorage) + " missileMonths=" + std::to_string(d.missileMonths);
        }
        if (cmd == "settraining") {  // settraining <nation> <p0> <p1> <p2> <p3> <months>: as the Doctrine dialog's Apply
            uint8_t p[4];
            for (int i = 0; i < 4; i++) p[i] = (uint8_t)(atoi(arg(2 + i).c_str()) != 0);
            game::SetPendingTraining(atoi(arg(1).c_str()), p, atoi(arg(6, "12").c_str()));
            return "ok";
        }
        if (cmd == "setmissiles") {  // setmissiles <nation> <policy> <months>: a pending missile storage policy
            game::SetPendingMissileStorage(atoi(arg(1).c_str()), atoi(arg(2).c_str()), atoi(arg(3, "2").c_str()));
            return "ok";
        }
        if (cmd == "writetest") {  // writetest <slot> <file name> <bytes>: WriteFileBytes into a save slot folder
            std::string name = arg(2);
            if (name.empty() || name.find_first_of("\\/:") != std::string::npos || name.find("..") != std::string::npos)
                return "error bad file name";
            std::wstring dir = game::SaveDir(atoi(arg(1).c_str()));
            if (dir.empty()) return "error no save folder";
            DWORD t0 = GetTickCount();
            bool ok = WriteFileBytes(dir + U2W(name), std::string((size_t)atoi(arg(3, "16").c_str()), 'x'));
            DWORD e = ok ? 0 : GetLastError();
            return (ok ? "ok" : "error write failed (" + std::to_string(e) + ")") + std::string(" ms=") +
                   std::to_string(GetTickCount() - t0);
        }
        if (cmd == "settension") {  // settension <a> <b> <value>: raw write, as the game's own routines would
            game::SetTensionRaw(atoi(arg(1).c_str()), atoi(arg(2).c_str()), atoi(arg(3).c_str()));
            return "ok";
        }
        if (cmd == "setally") {  // setally <a> <b> <months>: an alliance (0 ends it), raw as the game's own routines
            game::SetAlliance(atoi(arg(1).c_str()), atoi(arg(2).c_str()), atoi(arg(3).c_str()));
            return "ok ally=" + std::to_string(game::AllianceMonths(atoi(arg(1).c_str()), atoi(arg(2).c_str())));
        }
        // TEST ONLY: allyrand <0|-1>: every Random draw of the allies' war help for this PC's nation is 0 (-1: the game's)
        if (cmd == "allyrand") return game::TestAllyRandom(atoi(arg(1).c_str()));
        // TEST ONLY: revotest <0|-1> [n:unrest ...]: the next enemy-revolution run on this PC (a joined nation's turn):
        // draws 0 (-1 the game's) and these nations' UnrestLevel in this copy first (a people's nation's put back)
        if (cmd == "revotest") return game::TestRevolution(args);
        if (cmd == "rel") {  // rel <a> <b>
            int a = atoi(arg(1).c_str()), b = atoi(arg(2).c_str());
            return "ok tension=" + std::to_string(game::Tension(a, b)) + " war=" + (game::AtWar(a, b) ? "1" : "0") +
                   " ally=" + std::to_string(game::AllianceMonths(a, b)) + " warCounter=" +
                   std::to_string(game::WarCounter()) + " hostWars=" + std::to_string(game::CountAtWarWithHost());
        }
        if (cmd == "relview") return game::DebugRelView();  // what the tension panel and the war state show
        if (cmd == "diploview") {
            mp::View v = mp::GetView();
            std::string s = std::string("open=") + (v.diploOpen ? "1" : "0") + " note=" + v.diploNote + " rows=";
            for (auto& r : v.diplo)
                s += std::to_string(r.nation) + "/" + r.nationName + "/" + r.player + "/" + r.relation + "/" +
                     std::to_string(r.tension) + "/[" + r.pending + "]/" + (r.canWar ? "W" : "-") +
                     (r.canAlly ? "A" : "-") + (r.allyAccept ? "a" : "-") + (r.canPeace ? "P" : "-") +
                     (r.peaceAccept ? "p" : "-") + (r.canLeave ? "L" : "-") + (r.canCancel ? "C" : "-") +
                     (r.cancelIsDecline ? "d" : "-") + "/vp=" + std::to_string(r.vpMine) + ":" +
                     std::to_string(r.vpTheirs) + "/claim=" + std::to_string(r.canClaim ? r.claimBudget : 0) + ":" +
                     std::to_string(r.claimable.size()) + ":" + std::to_string(r.claims.size()) + "/" + r.outlook + "; ";
            return "ok " + s;
        }
        // --- dev helpers for automated tests ---
        if (cmd == "nflag") {  // nflag <nation> <BooleanField> [0|1]: a Boolean nation field (HasLostWar, Blockaded)
            int n = atoi(arg(1).c_str());
            if (args.size() > 3) game::SetNationFlag(n, arg(2), arg(3) != "0");
            int v = game::NationFlag(n, arg(2));
            return v < 0 ? "error no such nation/field" : "ok " + std::to_string(v);
        }
        if (cmd == "nfield") {  // nfield <nation> <IntegerField> [value]
            void* n = game::Nation(atoi(arg(1).c_str()));
            int off = dl::Field("TBuilderNation", arg(2).c_str());
            if (!n || off < 0) return "error no such nation/field";
            if (args.size() > 3) dl::At<int>(n, off) = atoi(arg(3).c_str());
            return "ok " + std::to_string(dl::At<int>(n, off));
        }
        if (cmd == "areas") {
            void* f = game::BuildCamp();
            void* list = f ? dl::At<void*>(f, dl::Field("TfrmBuildCamp", "MapAreas")) : nullptr;
            int nameOff = dl::Field("TMapArea", "Name");
            std::string s;
            for (int i = 0; i < dl::ListCount(list); i++)
                s += std::to_string(i) + ":" + W2U(dl::ReadUStr(dl::At<void*>(dl::ListItem(list, i), nameOff))) + "; ";
            return "ok " + s;
        }
        if (cmd == "shipinfo" || cmd == "shiporder") {  // shipinfo <nation> <ship> | shiporder <nation> <ship> <area>
            void* ship = game::Ship(game::Nation(atoi(arg(1).c_str())), atoi(arg(2).c_str()));
            if (!ship) return "error no such ship";
            int nameOff = dl::Field("TMapArea", "Name");
            int ordOff = dl::Field("TCampaignShip", "OrderedArea"), locOff = dl::Field("TCampaignShip", "LocationArea");
            if (cmd == "shiporder") {
                void* f = game::BuildCamp();
                void* list = dl::At<void*>(f, dl::Field("TfrmBuildCamp", "MapAreas"));
                void* area = dl::ListItem(list, atoi(arg(3).c_str()));
                if (!area) return "error no such area";
                dl::At<void*>(ship, ordOff) = area;
            }
            auto an = [&](void* a) { return a ? W2U(dl::ReadUStr(dl::At<void*>(a, nameOff))) : std::string("-"); };
            return "ok " + W2U(dl::ReadUStr(dl::At<void*>(ship, dl::Field("TCampaignShip", "ShipName")))) +
                   " loc=" + an(dl::At<void*>(ship, locOff)) + " ordered=" + an(dl::At<void*>(ship, ordOff)) +
                   " status=" + std::to_string(dl::At<uint8_t>(ship, dl::Field("TCampaignShip", "Status")));
        }
        if (cmd == "econ") return economy::BridgeCommand(args);  // econ <K>: the player's economy rules (economy.cpp)
        // TEST ONLY: econrand <v>: the player's economy rules for joined nations draw min(v, n - 1) for Random(n); -1 off
        if (cmd == "econrand") return economy::TestRandom(args);
        // TEST ONLY: econtreaty [<chance> [blind]|cancel]: the arms-control conference at the next month's end; cancel
        if (cmd == "econtreaty") return economy::TestTreaty(args);
        if (cmd == "warfx") return warfx::BridgeCommand(args);  // warfx [K]: the player's war effects (warfx.cpp)
        // TEST ONLY: warfxblk <n> <-1|0|1>: the war effects' blockade test of nation n answers 1 / 0; -1 the game's
        if (cmd == "warfxblk") return warfx::TestBlockade(args);
        // TEST ONLY: warfxrand <v>: the war effects' own Random(n) draws min(v, n - 1); -1 the game's
        if (cmd == "warfxrand") return warfx::TestRandom(args);
    // TEST ONLY: warfxsea <R> <n>: n of R's ships raid in the next sea war frame (raiders, submarines, trade war)
    if (cmd == "warfxsea") return warfx::TestSea(args);
        // TEST ONLY: shipbuilder <n> [<ship> <builder>]: nation n's ships under construction / one ship's building nation
        if (cmd == "shipbuilder") return warfx::TestBuilder(args);
        if (cmd == "ffield") {  // ffield <IntegerField>: a campaign field (e.g. ArmyOffensive, ParliamentDelay), read only
            void* f = game::BuildCamp();
            int off = dl::Field("TfrmBuildCamp", arg(1).c_str());
            if (!f || off < 0) return "error no campaign/field";
            return "ok " + std::to_string(dl::At<int>(f, off));
        }
        // armstreaty: the disarmament treaty and the submarine ban on this PC (0.6.0); TEST ONLY: `armstreaty set <months>
        // [<displacement> <calibre>]` puts one in force, `armstreaty war <n>` sets this PC's game war counter, `armstreaty
        // host <0|1>` (a joined PC) what it assumes of the host's rule until the next month's header
        if (cmd == "armstreaty") {
            if (arg(1) == "set")
                game::TestSetArmsTreaty(atoi(arg(2, "0").c_str()), atoi(arg(3, "2147483647").c_str()),
                                        atoi(arg(4, "2147483647").c_str()));
            if (arg(1) == "war") game::TestSetWarCounter(atoi(arg(2, "0").c_str()));
            if (arg(1) == "host") game::SetHostEndsArmsTreaty(arg(2, "1") != "0");
            game::ArmsTreaty t = game::ArmsTreatyNow();
            if (!t.ok) return "error treaty unavailable";
            return "ok months=" + std::to_string(t.months) + " disp=" + std::to_string(t.disp) + " cal=" +
                   std::to_string(t.cal) + " subban=" + std::to_string(t.subBan) + " base=" + std::to_string(t.base) +
                   " war=" + std::to_string(game::WarCounter()) + " host=" + (game::HostEndsArmsTreaty() ? "1" : "0") +
                   " kept=" + std::to_string(game::ArmsTreatyKept()) + " endedhere=" +
                   std::to_string(game::ArmsTreatyEndedHere()) + mp::DebugArmsTreatyEnds();
        }
        return "error unknown command";
    }
}

static void BridgeThread(uint16_t port, std::string token) {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(ls, (sockaddr*)&a, sizeof(a)) != 0 || listen(ls, 2) != 0) {
        Log("bridge: cannot listen on 127.0.0.1:%u", port);
        return;
    }
    Log("bridge: listening on 127.0.0.1:%u", port);
    while (true) {
        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        std::string buf;
        bool authed = token.empty();
        char tmp[4096];
        while (true) {
            int n = recv(c, tmp, sizeof(tmp), 0);
            if (n <= 0) break;
            buf.append(tmp, n);
            size_t e;
            while ((e = buf.find('\n')) != std::string::npos) {
                std::string line = Trim(buf.substr(0, e));
                buf.erase(0, e + 1);
                std::string reply;
                if (!authed) {
                    authed = line == token;
                    reply = authed ? "ok auth" : "error auth";
                } else if (!line.empty()) {
                    reply = Execute(line);
                }
                reply += "\n";
                send(c, reply.data(), (int)reply.size(), 0);
            }
        }
        closesocket(c);
    }
}

void BridgeStart() {
    char buf[32], tok[128] = {};
    if (GetEnvironmentVariableA("RTW3MP_BRIDGE_PORT", buf, sizeof(buf)) == 0) return;
    GetEnvironmentVariableA("RTW3MP_BRIDGE_TOKEN", tok, sizeof(tok));
    uint16_t port = (uint16_t)atoi(buf);
    if (!port) return;
    std::thread(BridgeThread, port, std::string(tok)).detach();
}

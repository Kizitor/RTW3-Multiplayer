// Development/test bridge: a line-based command socket on 127.0.0.1, only enabled when the environment
// variable RTW3MP_BRIDGE_PORT is set (and RTW3MP_BRIDGE_TOKEN, if set, must be the first line sent).
// Lets automated tests drive the mod without touching the user's mouse or keyboard.
#include "common.h"
#include "delphi.h"
#include "game.h"
#include "net.h"
#include "session.h"
#include "ui.h"
#include <thread>
#include <condition_variable>

static std::string ExecuteOnMain(const std::string& line);

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
    if (cmd == "eventnow") {  // eventnow [index]: client rolls this month's event now (shows the game's dialog)
        if (game::EventActive()) return "error an event dialog is open";
        int idx = atoi(arg(1, "-1").c_str());
        RunOnMainThread([idx] { mp::DebugEvent(idx); });
        return "ok";
    }
    if (cmd == "windows") {
        std::string out;
        EnumWindows(ListWin, (LPARAM)&out);
        return "ok " + out;
    }
    return RunSync(line);
}

static std::string ExecuteOnMain(const std::string& line) {
    auto args = Split(Trim(line), ' ');
    const std::string& cmd = args[0];
    auto arg = [&](size_t i, const std::string& def = "") { return i < args.size() ? args[i] : def; };
    // While a joined player's event dialog is open the nation list is reordered: only read-only commands.
    static const std::set<std::string> kReadOnly = {"status", "eventlast", "eventlist", "windows", "diploview"};
    if (game::EventActive() && !kReadOnly.count(cmd)) return "error an event dialog is open";
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
        if (cmd == "submit") return mp::SubmitTurn(), "ok";
        if (cmd == "limit") return mp::SetTurnLimit(atoi(arg(1).c_str())), "ok";
        if (cmd == "chat") return mp::SendChat(line.size() > 5 ? line.substr(5) : ""), "ok";
        if (cmd == "turn") return game::ClickTurnButton(), "ok";
        if (cmd == "save") return game::SaveGame() ? "ok" : "error save";
        if (cmd == "load") return game::LoadCampaign(atoi(arg(1).c_str()), atoi(arg(2, "0").c_str())) ? "ok" : "error load";
        if (cmd == "setplayer") return game::SetPlayerNation(atoi(arg(1).c_str())), "ok";
        if (cmd == "mpwindow") return ui::ShowMpWindow(), "ok";
        if (cmd == "savedir") return "ok " + W2U(game::SaveDir(atoi(arg(1, "1").c_str())));
        // --- diplomacy ---
        if (cmd == "diplo") {  // diplo <war|ally|peace|leave|cancel> <target nation>
            std::string why = mp::Diplomacy(arg(1), atoi(arg(2, "-1").c_str()));
            return why.empty() ? "ok" : "error " + why;
        }
        if (cmd == "diploas") {  // host: diploas <from nation> <action> <target nation>
            std::string why = mp::DebugDiplomacyAs(atoi(arg(1).c_str()), arg(2), atoi(arg(3, "-1").c_str()));
            return why.empty() ? "ok" : "error " + why;
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
        if (cmd == "rel") {  // rel <a> <b>
            int a = atoi(arg(1).c_str()), b = atoi(arg(2).c_str());
            return "ok tension=" + std::to_string(game::Tension(a, b)) + " war=" + (game::AtWar(a, b) ? "1" : "0") +
                   " ally=" + std::to_string(game::AllianceMonths(a, b)) + " warCounter=" +
                   std::to_string(game::WarCounter()) + " hostWars=" + std::to_string(game::CountAtWarWithHost());
        }
        if (cmd == "diploview") {
            mp::View v = mp::GetView();
            std::string s = std::string("open=") + (v.diploOpen ? "1" : "0") + " note=" + v.diploNote + " rows=";
            for (auto& r : v.diplo)
                s += std::to_string(r.nation) + "/" + r.nationName + "/" + r.player + "/" + r.relation + "/" +
                     std::to_string(r.tension) + "/[" + r.pending + "]/" + (r.canWar ? "W" : "-") +
                     (r.canAlly ? "A" : "-") + (r.allyAccept ? "a" : "-") + (r.canPeace ? "P" : "-") +
                     (r.peaceAccept ? "p" : "-") + (r.canLeave ? "L" : "-") + (r.canCancel ? "C" : "-") +
                     (r.cancelIsDecline ? "d" : "-") + "; ";
            return "ok " + s;
        }
        // --- dev helpers for automated tests ---
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

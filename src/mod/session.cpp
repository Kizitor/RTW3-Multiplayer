#include "session.h"
#include "game.h"
#include "net.h"
#include "saveio.h"
#include <iterator>
#include <tuple>

namespace mp {

std::function<void()> g_onChanged;
std::function<void(const std::string&)> g_onNotice;

struct Player {
    int peer = -1;
    std::string name;
    int nation = -1;    // campaign nation index once resolved
    std::string want;   // nation picked in the lobby (by name), also before a campaign exists
    bool connected = false;
    bool welcomed = false;
    bool submitted = false;
    bool hasState = false;
};

// The ten nations of the game, offered for lobby picks before the host has a campaign.
static const char* kStdNations[] = {"Great Britain", "Germany", "France",  "Russia", "USA",
                                    "Japan",         "Italy",   "Austria-Hungary", "Spain", "China"};

static Role g_role = Role::None;
static Phase g_phase = Phase::Lobby;
static std::string g_myName, g_password;
static int g_myNation = -1;
static std::vector<std::string> g_chat;
static std::string g_status;

// host
static std::map<int, Player> g_players;  // peer -> player
static std::map<int, Bundle> g_submissions;
static std::map<int, std::string> g_reserved;  // nation -> player name (persisted per campaign)
static int g_seq = 0;
static int g_turnLimitSec = 0;
static DWORD g_deadline = 0;
static bool g_forced = false;
static DWORD g_forceTick = 0;
static bool g_hostWantsTurn = false;
static bool g_turnRunning = false;
static bool g_afterTurnPending = false;
static bool g_runTurnWhenIdle = false;
static DWORD g_turnStartTick = 0;
static DWORD g_idleSinceTick = 0;
static int g_hostSlot = -1;
static DWORD g_lastStatusTick = 0;
static std::map<int, std::vector<game::ShipSnap>> g_aiMoveSnaps;
static std::string g_hostWant;         // host's planned nation before a campaign is loaded
static std::string g_lastCampaignKey;  // campaign currently loaded on the host ("" = none)
static bool g_autoStart = false;       // host pressed Start session before the campaign was ready

// Diplomacy between human players. Requests are made while planning and take effect when the host processes
// the month. Agreements made this way are kept by the mod: alliances don't run out and the AI's random peace
// never ends a war the players declared. Relations with AI nations are left to the game.
struct DipItem {
    std::string kind;  // offer_ally / offer_peace (waiting for `to`), or war / ally / peace / leave (at month end)
    int from = -1, to = -1;
};
struct DipSnap {
    struct Human {
        int idx;
        std::string nation, player;
    };
    bool on = false;
    std::vector<Human> humans;
    std::map<std::pair<int, int>, std::pair<char, int>> rel;  // (low, high) -> W/A/P, tension
    std::vector<DipItem> items;
};
static std::vector<DipItem> g_dip;                        // host
static std::set<std::pair<int, int>> g_pactAllies, g_pactWars;  // host: agreements made with the buttons
static std::vector<std::tuple<int, int, int>> g_peaceMask;   // host: wars hidden from the AI peace routine
static DipSnap g_cSnap;                                   // client: as broadcast by the host
static const int kPactMonths = 60;

static std::pair<int, int> PairKey(int a, int b) { return {std::min(a, b), std::max(a, b)}; }

// client
static int g_clientSlot = 77;
static int g_clientSeq = -1;
static long long g_clientIdStart = 0;
static bool g_haveState = false;
static bool g_submitted = false;
static bool g_forceSubmitPending = false;
static std::string g_pendingState;
static int g_remoteRemaining = -1;
static Phase g_remotePhase = Phase::Lobby;
static std::string g_hostName, g_hostNationName, g_date;
static std::vector<NationView> g_lobbyNations;
static std::vector<PlayerView> g_lobbyPlayers;
static bool g_remoteCampaignReady = false, g_remoteCampaignLoaded = false;
static std::string g_myWant;

static const int kIdStride = 20000;

static void Changed() {
    if (g_onChanged) g_onChanged();
}

static void Notice(const std::string& s) {
    Log("notice: %s", s.c_str());
    g_status = s;
    if (g_onNotice) g_onNotice(s);
    Changed();
}

static void AddChat(const std::string& line) {
    g_chat.push_back(line);
    if (g_chat.size() > 300) g_chat.erase(g_chat.begin(), g_chat.begin() + 50);
    // The host's system lines ("* ...") go to every player, so e.g. a refused nation pick is visible to them.
    if (g_role == Role::Host && line.rfind("* ", 0) == 0) {
        KV c;
        c["from"] = "*";
        c["text"] = line.substr(2);
        net::SendAll(net::MSG_CHAT, KVEncode(c));
    }
    Changed();
}

static std::string CleanName(const std::string& s) {
    std::string o;
    for (char c : s)
        if (c != ':' && c != ';' && c != '=' && c != '\n' && c != '\r' && c != '|') o += c;
    o = Trim(o);
    if (o.size() > 24) o.resize(24);
    return o.empty() ? "Player" : o;
}

static std::string PackHB(const std::string& header, const std::string& body) {
    std::string s;
    uint32_t n = (uint32_t)header.size();
    s.append((const char*)&n, 4);
    s += header;
    s += body;
    return s;
}

static bool UnpackHB(const std::string& p, KV& header, std::string& body) {
    if (p.size() < 4) return false;
    uint32_t n;
    memcpy(&n, p.data(), 4);
    if (4 + (size_t)n > p.size()) return false;
    header = KVDecode(p.substr(4, n));
    body = p.substr(4 + n);
    return true;
}

static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static std::string DateStr(int y, int m) {
    if (y <= 0 || m < 1 || m > 12) return "";
    return std::string(kMonths[m - 1]) + " " + std::to_string(y);
}

static std::string HostDate() {
    int y = 0, m = 0;
    if (g_hostSlot > 0) saveio::ReadDate(game::SaveDir(g_hostSlot), g_hostSlot, y, m);
    return DateStr(y, m);
}

// A campaign is open on the strategic screen (also during the initial-fleet steps).
static bool CampaignLoaded() {
    void* f = game::BuildCamp();
    if (!f || !game::FormVisible(f)) return false;
    int slot = game::CurrentSlot();
    return slot > 0 && slot < 50 && game::NationCount() >= 9;
}

// ...and past the initial-fleet steps, so monthly turns can start.
bool CampaignReady() { return CampaignLoaded() && game::StartBuildPhase() == 2; }

// ---------------------------------------------------------------------------------------------
// Host helpers

static std::wstring SessionFile() { return game::SaveDir(g_hostSlot) + L"rtw3mp_session.ini"; }

// Identifies the campaign in a slot, so reservations never leak into a new game saved in the same slot.
static std::string CampaignKey() {
    std::string k = std::to_string(game::CampaignStartYear());
    for (int i = 0; i <= 8 && i < game::NationCount(); i++) k += "|" + W2U(game::NationName(i));
    return k;
}

static std::string EncodePairs(const std::set<std::pair<int, int>>& s) {
    std::string o;
    for (auto& p : s) o += std::to_string(p.first) + "-" + std::to_string(p.second) + ",";
    return o;
}

static std::set<std::pair<int, int>> DecodePairs(const std::string& s) {
    std::set<std::pair<int, int>> o;
    for (auto& e : Split(s, ',')) {
        auto f = Split(e, '-');
        if (f.size() != 2) continue;
        int a = atoi(f[0].c_str()), b = atoi(f[1].c_str());
        if (a >= 0 && b > a && b <= 8) o.insert({a, b});
    }
    return o;
}

static std::string EncodeDipItems(const std::vector<DipItem>& v) {
    std::string o;
    for (auto& d : v) o += d.kind + ":" + std::to_string(d.from) + ":" + std::to_string(d.to) + ";";
    return o;
}

static std::vector<DipItem> DecodeDipItems(const std::string& s) {
    std::vector<DipItem> o;
    for (auto& e : Split(s, ';')) {
        auto f = Split(e, ':');
        if (f.size() != 3 || f[0].empty()) continue;
        DipItem d;
        d.kind = f[0];
        d.from = atoi(f[1].c_str());
        d.to = atoi(f[2].c_str());
        if (d.from >= 0 && d.to >= 0 && d.from <= 8 && d.to <= 8 && d.from != d.to) o.push_back(d);
    }
    return o;
}

static void SaveReservations() {
    if (g_hostSlot <= 0) return;
    KV kv;
    for (auto& r : g_reserved) kv["nation" + std::to_string(r.first)] = r.second;
    kv["turnLimitSec"] = std::to_string(g_turnLimitSec);
    kv["campaign"] = CampaignKey();
    kv["pactAllies"] = EncodePairs(g_pactAllies);
    kv["pactWars"] = EncodePairs(g_pactWars);
    kv["diplomacy"] = EncodeDipItems(g_dip);
    kv["diplomacyDate"] = HostDate();  // pending decisions belong to this month only
    WriteFileBytes(SessionFile(), KVEncode(kv));
}

static void LoadReservations() {
    g_reserved.clear();
    g_dip.clear();
    g_pactAllies.clear();
    g_pactWars.clear();
    std::string s;
    if (g_hostSlot <= 0 || !ReadFileBytes(SessionFile(), s)) return;
    KV kv = KVDecode(s);
    if (KVStr(kv, "campaign") != CampaignKey()) {
        Log("session file belongs to another campaign; ignored");
        return;
    }
    for (auto& e : kv) {
        if (e.first.rfind("nation", 0) == 0) {
            int n = atoi(e.first.c_str() + 6);
            if (n >= 1 && n <= 8 && !e.second.empty()) g_reserved[n] = e.second;
        }
    }
    if (kv.count("turnLimitSec")) g_turnLimitSec = KVInt(kv, "turnLimitSec");
    g_pactAllies = DecodePairs(KVStr(kv, "pactAllies"));
    g_pactWars = DecodePairs(KVStr(kv, "pactWars"));
    if (KVStr(kv, "diplomacyDate") == HostDate())
        g_dip = DecodeDipItems(KVStr(kv, "diplomacy"));
    else if (!KVStr(kv, "diplomacy").empty())
        Log("pending diplomacy from %s dropped (campaign is at %s)", KVStr(kv, "diplomacyDate").c_str(), HostDate().c_str());
}

static std::string ClaimedBy(int nation) {
    for (auto& p : g_players)
        if (p.second.welcomed && p.second.nation == nation) return p.second.name;
    auto it = g_reserved.find(nation);
    return it == g_reserved.end() ? "" : it->second;
}

static std::string CampaignNationName(int idx) { return CleanName(W2U(game::NationName(idx))); }

static int CampaignIndexOf(const std::string& name) {
    for (int i = 0; i <= 8 && i < game::NationCount(); i++)
        if (CampaignNationName(i) == name) return i;
    return -1;
}

static bool IsStdNation(const std::string& name) {
    for (const char* n : kStdNations)
        if (name == n) return true;
    return false;
}

// Who picked this nation in the lobby (before a campaign exists).
static std::string WantedBy(const std::string& name) {
    if (!g_hostWant.empty() && g_hostWant == name) return g_myName + " (host)";
    for (auto& p : g_players)
        if (p.second.welcomed && p.second.want == name) return p.second.name;
    return "";
}

static std::vector<NationView> HostNationRows() {
    std::vector<NationView> rows;
    if (CampaignLoaded()) {
        for (int i = 1; i <= 8 && i < game::NationCount(); i++) rows.push_back({i, CampaignNationName(i), ClaimedBy(i)});
    } else {
        for (const char* n : kStdNations) rows.push_back({-1, n, WantedBy(n)});
    }
    return rows;
}

static std::string HostNationLabel() {
    if (CampaignLoaded()) return CampaignNationName(0);
    return g_hostWant.empty() ? "" : g_hostWant + " (planned)";
}

static std::string PlayerNationLabel(const Player& p) {
    if (p.nation > 0) return CampaignNationName(p.nation);
    return p.want.empty() ? "" : p.want + " (planned)";
}

// ---------------------------------------------------------------------------------------------
// Diplomacy view (shared by host and clients)

static bool IsHumanNation(int n) { return n == 0 || hookcb::IsHumanRemoteNation(n); }

static std::string NationPlayerName(int n) {
    if (n == 0) return CleanName(g_myName) + " (host)";
    return ClaimedBy(n);
}

static DipSnap HostDipSnap() {
    static DipSnap cache;  // reused while the month is processed (the game may be reloading its nations)
    if (g_role != Role::Host || g_phase == Phase::Lobby || !game::DiplomacyAvailable()) return DipSnap();
    if (g_phase != Phase::Planning || !CampaignLoaded()) return cache;
    DipSnap s;
    s.on = true;
    for (int i = 0; i <= 8 && i < game::NationCount(); i++)
        if (IsHumanNation(i)) s.humans.push_back({i, CampaignNationName(i), CleanName(NationPlayerName(i))});
    for (size_t x = 0; x < s.humans.size(); x++)
        for (size_t y = x + 1; y < s.humans.size(); y++) {
            int a = s.humans[x].idx, b = s.humans[y].idx;
            char st = game::AtWar(a, b) ? 'W' : game::AllianceMonths(a, b) > 0 ? 'A' : 'P';
            s.rel[PairKey(a, b)] = {st, game::Tension(a, b)};
        }
    s.items = g_dip;
    cache = s;
    return s;
}

static void EncodeDipSnap(const DipSnap& s, KV& kv) {
    kv["dipOn"] = s.on ? "1" : "0";
    std::string hum, rel;
    for (auto& h : s.humans) hum += std::to_string(h.idx) + ":" + h.nation + ":" + h.player + ";";
    for (auto& r : s.rel)
        rel += std::to_string(r.first.first) + ":" + std::to_string(r.first.second) + ":" + r.second.first + ":" +
               std::to_string(r.second.second) + ";";
    kv["dipHumans"] = hum;
    kv["dipRel"] = rel;
    kv["dipItems"] = EncodeDipItems(s.items);
}

static DipSnap DecodeDipSnap(const KV& kv) {
    DipSnap s;
    s.on = KVInt(kv, "dipOn") != 0;
    for (auto& e : Split(KVStr(kv, "dipHumans"), ';')) {
        auto f = Split(e, ':');
        if (f.size() >= 3) s.humans.push_back({atoi(f[0].c_str()), f[1], f[2]});
    }
    for (auto& e : Split(KVStr(kv, "dipRel"), ';')) {
        auto f = Split(e, ':');
        if (f.size() >= 4 && !f[2].empty()) s.rel[PairKey(atoi(f[0].c_str()), atoi(f[1].c_str()))] = {f[2][0], atoi(f[3].c_str())};
    }
    s.items = DecodeDipItems(KVStr(kv, "dipItems"));
    return s;
}

static std::vector<DiploRow> DiploRows(int me, const DipSnap& s) {
    std::vector<DiploRow> rows;
    bool meHuman = false;
    for (auto& h : s.humans) meHuman |= h.idx == me;
    if (!s.on || !meHuman) return rows;
    auto has = [&](const char* k, int f, int t) {
        for (auto& d : s.items)
            if (d.kind == k && d.from == f && d.to == t) return true;
        return false;
    };
    for (auto& h : s.humans) {
        if (h.idx == me) continue;
        int o = h.idx;
        DiploRow r;
        r.nation = o;
        r.nationName = h.nation;
        r.player = h.player;
        auto it = s.rel.find(PairKey(me, o));
        char st = it == s.rel.end() ? 'P' : it->second.first;
        r.tension = it == s.rel.end() ? 0 : it->second.second;
        r.atWar = st == 'W';
        r.allied = st == 'A';
        r.relation = r.atWar ? "At war" : r.allied ? "Allied" : "Peace";
        bool warMine = has("war", me, o), warTheirs = has("war", o, me);
        bool ally = has("ally", me, o) || has("ally", o, me), peace = has("peace", me, o) || has("peace", o, me);
        bool leaveMine = has("leave", me, o), leaveTheirs = has("leave", o, me);
        bool oaMine = has("offer_ally", me, o), oaTheirs = has("offer_ally", o, me);
        bool opMine = has("offer_peace", me, o), opTheirs = has("offer_peace", o, me);
        std::vector<std::string> p;
        if (warMine) p.push_back("you declared war");
        if (warTheirs) p.push_back("declared war on you");
        if (ally) p.push_back("alliance agreed");
        if (peace) p.push_back("peace agreed");
        if (leaveMine) p.push_back("you leave the alliance");
        if (leaveTheirs) p.push_back("leaves the alliance");
        if (oaMine) p.push_back("you offered an alliance");
        if (oaTheirs) p.push_back("offers an alliance");
        if (opMine) p.push_back("you offered peace");
        if (opTheirs) p.push_back("offers peace");
        for (auto& x : p) r.pending += (r.pending.empty() ? "" : ", ") + x;
        r.canWar = !r.atWar && !warMine && !warTheirs;
        r.allyAccept = oaTheirs;
        r.canAlly = !r.atWar && !r.allied && !ally && !warMine && !warTheirs && !oaMine;
        r.peaceAccept = opTheirs;
        r.canPeace = r.atWar && !peace && !opMine;
        r.canLeave = (r.allied && !leaveMine && !leaveTheirs) || ally;
        bool mineToCancel = oaMine || opMine || ally || peace || warMine || leaveMine;
        r.canCancel = mineToCancel || oaTheirs || opTheirs;
        r.cancelIsDecline = !mineToCancel && (oaTheirs || opTheirs);
        rows.push_back(r);
    }
    return rows;
}

static int RemainingSec() {
    if (g_role == Role::Client) return g_remoteRemaining;
    if (g_phase != Phase::Planning || !g_deadline) return -1;
    int r = (int)((int64_t)g_deadline - (int64_t)GetTickCount()) / 1000;
    return r < 0 ? 0 : r;
}

static const char* PhaseName(Phase p) {
    return p == Phase::Lobby ? "lobby" : p == Phase::Planning ? "planning" : "processing";
}

static void BroadcastLobby() {
    if (g_role != Role::Host) return;
    KV kv;
    kv["host"] = g_myName;
    kv["hostNation"] = HostNationLabel();
    kv["campaign"] = CampaignReady() ? "1" : "0";
    kv["loaded"] = CampaignLoaded() ? "1" : "0";
    kv["date"] = HostDate();
    kv["phase"] = PhaseName(g_phase);
    kv["limit"] = std::to_string(g_turnLimitSec);
    kv["remaining"] = std::to_string(RemainingSec());
    std::string nations;
    for (auto& r : HostNationRows())
        nations += std::to_string(r.index) + ":" + r.name + ":" + (r.claimedBy.empty() ? "" : CleanName(r.claimedBy)) + ";";
    kv["nations"] = nations;
    std::string players = "0:" + CleanName(g_myName) + ":0:1:" + (g_hostWantsTurn ? "1" : "0") + ":" + g_hostWant + ";";
    for (auto& p : g_players) {
        if (!p.second.welcomed) continue;
        players += std::to_string(p.first) + ":" + p.second.name + ":" + std::to_string(p.second.nation) + ":" +
                   (p.second.connected ? "1" : "0") + ":" + (p.second.submitted ? "1" : "0") + ":" + p.second.want + ";";
    }
    kv["players"] = players;
    EncodeDipSnap(HostDipSnap(), kv);
    net::SendAll(net::MSG_LOBBY, KVEncode(kv));
    net::SetDiscoveryInfo(g_myName + "|" + kv["hostNation"] + "|" + kv["date"] + "|" +
                          std::to_string(g_players.size() + 1));
    Changed();
}

static void BroadcastStatus(const std::string& text) {
    if (g_role != Role::Host) return;
    KV kv;
    kv["phase"] = PhaseName(g_phase);
    kv["text"] = text;
    kv["remaining"] = std::to_string(RemainingSec());
    net::SendAll(net::MSG_STATUS, KVEncode(kv));
    g_status = text;
    Changed();
}

// ---------------------------------------------------------------------------------------------
// Diplomacy (host)

static int DipFind(const char* kind, int from, int to) {
    for (size_t i = 0; i < g_dip.size(); i++)
        if (g_dip[i].kind == kind && g_dip[i].from == from && g_dip[i].to == to) return (int)i;
    return -1;
}

static int DipFindPair(const char* kind, int a, int b) {
    int i = DipFind(kind, a, b);
    return i >= 0 ? i : DipFind(kind, b, a);
}

static void DipEraseWhere(std::function<bool(const DipItem&)> pred) {
    for (size_t i = 0; i < g_dip.size();)
        if (pred(g_dip[i]))
            g_dip.erase(g_dip.begin() + i);
        else
            i++;
}

static void DropDiplomacyFor(int nation) {
    DipEraseWhere([&](const DipItem& d) { return d.from == nation || d.to == nation; });
    for (auto* s : {&g_pactAllies, &g_pactWars})
        for (auto it = s->begin(); it != s->end();) it = (it->first == nation || it->second == nation) ? s->erase(it) : std::next(it);
}

// A player's diplomatic request; `from` is the requester's nation. Returns "" or the reason it is refused.
static std::string HostDiplo(int from, const std::string& action, int to) {
    if (g_role != Role::Host || g_phase != Phase::Planning || !CampaignReady())
        return "Diplomacy is possible while the players plan a month.";
    if (!game::DiplomacyAvailable()) return "Diplomacy is not available with this game version.";
    if (from == to || to < 0 || to > 8 || !IsHumanNation(from) || !IsHumanNation(to))
        return "You can only negotiate with nations commanded by other players.";
    std::string A = CampaignNationName(from), B = CampaignNationName(to);
    bool war = game::AtWar(from, to);
    bool allied = !war && game::AllianceMonths(from, to) > 0;
    int declared = DipFindPair("war", from, to), agreedAlly = DipFindPair("ally", from, to),
        agreedPeace = DipFindPair("peace", from, to);
    if (action == "war") {
        if (war) return "You are already at war with " + B + ".";
        if (declared >= 0) return "A declaration of war between you and " + B + " is already pending.";
        DipEraseWhere([&](const DipItem& d) {
            return (d.kind == "offer_ally" || d.kind == "ally" || d.kind == "leave" || d.kind == "offer_peace") &&
                   PairKey(d.from, d.to) == PairKey(from, to);
        });
        g_dip.push_back({"war", from, to});
        AddChat("* " + A + " declares war on " + B + ". The war begins when this month is processed.");
    } else if (action == "ally") {
        if (war) return "Make peace with " + B + " first.";
        if (declared >= 0) return "A declaration of war between you and " + B + " is pending.";
        if (allied) return "You are already allied with " + B + ".";
        if (agreedAlly >= 0) return "The alliance is already agreed; it starts when this month is processed.";
        int theirs = DipFind("offer_ally", to, from);
        if (theirs >= 0) {
            g_dip.erase(g_dip.begin() + theirs);
            g_dip.push_back({"ally", to, from});
            AddChat("* " + A + " accepts the alliance offered by " + B + ". It starts when this month is processed.");
        } else {
            if (DipFind("offer_ally", from, to) >= 0) return "You already offered an alliance to " + B + ".";
            g_dip.push_back({"offer_ally", from, to});
            AddChat("* " + A + " offers an alliance to " + B + ".");
        }
    } else if (action == "peace") {
        if (!war)
            return declared >= 0 ? "The war has not started yet: withdraw the declaration instead."
                                 : "You are not at war with " + B + ".";
        if (agreedPeace >= 0) return "Peace is already agreed; it takes effect when this month is processed.";
        int theirs = DipFind("offer_peace", to, from);
        if (theirs >= 0) {
            g_dip.erase(g_dip.begin() + theirs);
            g_dip.push_back({"peace", to, from});
            AddChat("* " + A + " accepts the peace offered by " + B + ". It takes effect when this month is processed.");
        } else {
            if (DipFind("offer_peace", from, to) >= 0) return "You already offered peace to " + B + ".";
            g_dip.push_back({"offer_peace", from, to});
            AddChat("* " + A + " offers peace to " + B + ".");
        }
    } else if (action == "leave") {
        if (agreedAlly >= 0) {
            g_dip.erase(g_dip.begin() + agreedAlly);
            AddChat("* " + A + " calls off the agreed alliance with " + B + ".");
        } else if (!allied) {
            return "You are not allied with " + B + ".";
        } else if (DipFindPair("leave", from, to) >= 0) {
            return "The alliance with " + B + " already ends this month.";
        } else {
            g_dip.push_back({"leave", from, to});
            AddChat("* " + A + " leaves the alliance with " + B + " at the end of this month.");
        }
    } else if (action == "cancel") {
        int removed = 0;
        for (int i = (int)g_dip.size() - 1; i >= 0; i--) {
            const DipItem d = g_dip[i];
            bool mine = d.from == from && d.to == to, theirs = d.from == to && d.to == from;
            if (!mine && !theirs) continue;
            std::string msg;
            if (d.kind == "offer_ally" || d.kind == "offer_peace") {
                std::string what = d.kind == "offer_ally" ? "alliance" : "peace";
                msg = mine ? A + " withdraws the " + what + " offer to " + B : A + " declines the " + what + " offered by " + B;
            } else if (d.kind == "ally" || d.kind == "peace") {
                msg = A + " calls off the agreed " + std::string(d.kind == "ally" ? "alliance" : "peace") + " with " + B;
            } else if (mine && d.kind == "war") {
                msg = A + " withdraws the declaration of war on " + B;
            } else if (mine && d.kind == "leave") {
                msg = A + " stays in the alliance with " + B;
            } else {
                continue;
            }
            g_dip.erase(g_dip.begin() + i);
            AddChat("* " + msg + ".");
            removed++;
        }
        if (!removed) return "Nothing to withdraw or decline with " + B + ".";
    } else {
        return "Unknown diplomatic action.";
    }
    Log("diplomacy request %d %s %d accepted", from, action.c_str(), to);
    SaveReservations();
    BroadcastLobby();
    return "";
}

// Host, after the game finished the month and before the new month is sent: carry out this month's decisions,
// then keep the players' agreements (alliances don't run out; a war the players declared only ends when both
// agree to peace, even if the game's own peace routines ended it during the month).
static void ApplyDiplomacy() {
    if (!game::DiplomacyAvailable() || !CampaignReady()) return;
    auto both = [](int a, int b) { return IsHumanNation(a) && IsHumanNation(b); };
    auto Nm = [](int n) { return CampaignNationName(n); };
    bool changed = false;
    for (const char* kind : {"leave", "peace", "war", "ally"}) {
        for (size_t i = 0; i < g_dip.size();) {
            if (g_role != Role::Host) return;  // the game's dialogs run the message loop
            DipItem d = g_dip[i];
            if (d.kind != kind) {
                i++;
                continue;
            }
            g_dip.erase(g_dip.begin() + i);
            changed = true;
            if (!both(d.from, d.to)) continue;
            auto key = PairKey(d.from, d.to);
            std::string msg;
            if (d.kind == "leave") {
                if (!game::AtWar(d.from, d.to) && game::AllianceMonths(d.from, d.to) > 0) game::SetAlliance(d.from, d.to, 0);
                g_pactAllies.erase(key);
                msg = Nm(d.from) + " has left the alliance with " + Nm(d.to) + ".";
            } else if (d.kind == "peace") {
                g_pactWars.erase(key);
                game::MakePeace(d.from, d.to);
                msg = "Peace: " + Nm(d.from) + " and " + Nm(d.to) + " have ended their war.";
            } else if (d.kind == "war") {
                g_pactWars.insert(key);
                g_pactAllies.erase(key);
                game::StartWar(d.from, d.to);
                msg = "War: " + Nm(d.from) + " has declared war on " + Nm(d.to) + "!";
            } else if (game::AtWar(d.from, d.to)) {
                msg = "The alliance agreed by " + Nm(d.from) + " and " + Nm(d.to) + " did not come about: they are at war.";
            } else {
                game::SetAlliance(d.from, d.to, kPactMonths);
                g_pactAllies.insert(key);
                msg = "Alliance: " + Nm(d.from) + " and " + Nm(d.to) + " are now allies.";
            }
            AddChat("* " + msg);
            if (d.from == 0 || d.to == 0) Notice(msg);
        }
    }
    if (g_role != Role::Host) return;
    for (auto it = g_pactWars.begin(); it != g_pactWars.end();) {
        int a = it->first, b = it->second;
        if (!both(a, b)) {
            it = g_pactWars.erase(it);
            changed = true;
            continue;
        }
        if (!game::AtWar(a, b)) {
            game::StartWar(a, b);
            AddChat("* The war between " + Nm(a) + " and " + Nm(b) + " goes on: only the two players can make peace.");
            changed = true;
        }
        ++it;
    }
    for (auto it = g_pactAllies.begin(); it != g_pactAllies.end();) {
        int a = it->first, b = it->second;
        if (!both(a, b)) {
            it = g_pactAllies.erase(it);
            changed = true;
        } else if (game::AtWar(a, b)) {
            AddChat("* The alliance between " + Nm(a) + " and " + Nm(b) + " ended with the outbreak of war.");
            it = g_pactAllies.erase(it);
            changed = true;
        } else {
            int m = game::AllianceMonths(a, b);
            if (m <= 0) Log("diplomacy: the game ended the players' alliance %d-%d; restored", a, b);
            if (m < kPactMonths) game::SetAlliance(a, b, kPactMonths);
            ++it;
        }
    }
    // Offers that no longer fit the new situation lapse.
    DipEraseWhere([&](const DipItem& d) {
        bool keep = both(d.from, d.to) &&
                    ((d.kind == "offer_ally" && !game::AtWar(d.from, d.to) && game::AllianceMonths(d.from, d.to) <= 0) ||
                     (d.kind == "offer_peace" && game::AtWar(d.from, d.to)));
        if (!keep) changed = true;
        return !keep;
    });
    if (changed) {
        SaveReservations();
        game::RefreshUI();
    }
}

static void SendStateTo(Player& p, const std::wstring& dir, int y, int m) {
    Bundle b;
    std::string err;
    if (!saveio::BuildStateBundle(dir, g_hostSlot, kIdStride * p.nation, b, err)) {
        Notice("Cannot package the game for " + p.name + ": " + err);
        return;
    }
    KV h;
    h["seq"] = std::to_string(g_seq);
    h["nation"] = std::to_string(p.nation);
    h["nationName"] = W2U(game::NationName(p.nation));
    h["year"] = std::to_string(y);
    h["month"] = std::to_string(m);
    h["remaining"] = std::to_string(RemainingSec());
    net::Send(p.peer, net::MSG_STATE, PackHB(KVEncode(h), b.Pack()));
    p.hasState = true;
    Log("state seq %d -> %s (nation %d, %u bytes)", g_seq, p.name.c_str(), p.nation, (unsigned)b.TotalBytes());
}

static void BroadcastState() {
    if (!game::SaveGame()) {
        Notice("Saving the game failed; cannot send the new month.");
        return;
    }
    std::wstring dir = game::SaveDir(g_hostSlot);
    int y = 0, m = 0;
    saveio::ReadDate(dir, g_hostSlot, y, m);
    SaveReservations();  // stamps the open diplomacy items with the new month
    g_seq++;
    g_submissions.clear();
    g_phase = Phase::Planning;
    g_deadline = g_turnLimitSec > 0 ? GetTickCount() + g_turnLimitSec * 1000 : 0;
    g_forced = false;
    g_hostWantsTurn = false;
    for (auto& kv : g_players) {
        Player& p = kv.second;
        p.submitted = false;
        p.hasState = false;
        if (p.connected && p.welcomed && p.nation > 0) SendStateTo(p, dir, y, m);
    }
    BroadcastLobby();
    BroadcastStatus("Planning " + DateStr(y, m) + ": make your decisions, then press Turn / Submit.");
    AddChat("* New month: " + DateStr(y, m));
}

static bool AllSubmitted(std::string* missing) {
    bool all = true;
    for (auto& kv : g_players) {
        const Player& p = kv.second;
        if (!p.connected || p.nation <= 0 || !p.hasState) continue;
        if (!p.submitted) {
            all = false;
            if (missing) *missing += (missing->empty() ? "" : ", ") + p.name;
        }
    }
    return all;
}

static void RunHostTurn();

static void TryStartHostTurn() {
    if (g_role != Role::Host || g_phase != Phase::Planning) return;
    if (game::IsBusy()) {
        g_runTurnWhenIdle = true;
        return;
    }
    RunOnMainThread(RunHostTurn);
}

static void RunHostTurn() {
    if (g_role != Role::Host || g_phase != Phase::Planning) return;
    if (game::IsBusy()) {
        g_runTurnWhenIdle = true;
        return;
    }
    g_runTurnWhenIdle = false;
    g_hostWantsTurn = false;
    g_phase = Phase::Processing;
    g_turnRunning = true;
    g_afterTurnPending = false;
    g_turnStartTick = GetTickCount();
    BroadcastLobby();
    BroadcastStatus("The host is processing the turn...");
    if (!game::SaveGame()) {
        Notice("Saving failed; turn aborted.");
        g_phase = Phase::Planning;
        g_turnRunning = false;
        return;
    }
    std::wstring dir = game::SaveDir(g_hostSlot);
    int merged = 0;
    for (auto& s : g_submissions) {
        auto it = g_players.find(s.first);
        if (it == g_players.end()) continue;
        std::string err;
        if (saveio::MergeSubmission(dir, g_hostSlot, it->second.nation, s.second, err))
            merged++;
        else
            AddChat("* Could not apply " + it->second.name + "'s turn: " + err);
    }
    g_submissions.clear();
    if (merged > 0 && !game::LoadCampaign(g_hostSlot, 0)) {
        Notice("Reloading the merged game failed!");
        g_phase = Phase::Planning;
        g_turnRunning = false;
        return;
    }
    Log("host turn: %d submissions merged, running turn", merged);
    game::RunOriginalTurn();
    g_idleSinceTick = 0;
    Log("host turn: click handler returned (endOfTurn=%d battle=%d)", g_afterTurnPending, game::BattleActive());
}

static Player* PlayerByPeer(int peer) {
    auto it = g_players.find(peer);
    return it == g_players.end() ? nullptr : &it->second;
}

static void HostOnMessage(net::Msg& m) {
    Player* p = PlayerByPeer(m.peer);
    switch (m.type) {
        case net::EV_CONNECTED: {
            Player np;
            np.peer = m.peer;
            np.name = "(joining)";
            g_players[m.peer] = np;
            break;
        }
        case net::EV_DISCONNECTED: {
            if (p) {
                if (p->welcomed) AddChat("* " + p->name + " disconnected (" + m.data + ")");
                g_players.erase(m.peer);
                g_submissions.erase(m.peer);
                BroadcastLobby();
                if (g_hostWantsTurn && AllSubmitted(nullptr)) TryStartHostTurn();
            }
            break;
        }
        case net::MSG_HELLO: {
            if (!p) break;
            KV kv = KVDecode(m.data);
            std::string reason;
            if (KVInt(kv, "proto") != MP_PROTOCOL)
                reason = "protocol mismatch (host mod " MP_MOD_VERSION ")";
            else if (KVStr(kv, "build") != game::BuildFingerprint())
                reason = "different game version than the host";
            else if (!g_password.empty() && KVStr(kv, "pw") != g_password)
                reason = "wrong password";
            std::string base = CleanName(KVStr(kv, "name"));
            auto taken = [&](const std::string& n) {
                if (n == CleanName(g_myName)) return true;
                for (auto& o : g_players)
                    if (o.first != m.peer && o.second.welcomed && o.second.name == n) return true;
                return false;
            };
            std::string name = base;  // duplicate names (e.g. two copies on one PC) get a number instead of a refusal
            for (int k = 2; taken(name) && k < 100; k++) name = base + " " + std::to_string(k);
            if (!reason.empty()) {
                KV r;
                r["reason"] = reason;
                net::Send(m.peer, net::MSG_REJECT, KVEncode(r));
                net::Kick(m.peer);
                Log("rejected %s: %s", name.c_str(), reason.c_str());
                break;
            }
            p->name = name;
            p->connected = true;
            p->welcomed = true;
            if (CampaignLoaded()) {
                for (auto& r : g_reserved) {
                    if (r.second == name) {  // returning player gets their nation back
                        p->nation = r.first;
                        p->want = CampaignNationName(r.first);
                    }
                }
            }
            KV w;
            w["peer"] = std::to_string(m.peer);
            w["host"] = g_myName;
            w["name"] = name;
            net::Send(m.peer, net::MSG_WELCOME, KVEncode(w));
            AddChat("* " + name + " joined" + (p->nation > 0 ? " as " + CampaignNationName(p->nation) : ""));
            BroadcastLobby();
            if (g_phase == Phase::Planning && p->nation > 0) {
                int y = 0, mo = 0;
                std::wstring dir = game::SaveDir(g_hostSlot);
                if (game::SaveGame()) {
                    saveio::ReadDate(dir, g_hostSlot, y, mo);
                    SendStateTo(*p, dir, y, mo);
                }
            }
            break;
        }
        case net::MSG_PICK: {
            if (!p || !p->welcomed) break;
            std::string name = CleanName(KVStr(KVDecode(m.data), "name"));
            auto refuse = [&](const std::string& why) {
                AddChat("* " + p->name + " cannot take " + name + ": " + why);
                BroadcastLobby();
            };
            if (!CampaignLoaded()) {
                // No campaign yet: remember the wish by name; it is matched when the host loads a campaign.
                if (!IsStdNation(name)) break;
                std::string owner = WantedBy(name);
                if (!owner.empty() && owner != p->name) {
                    refuse("already picked by " + owner);
                    break;
                }
                p->want = name;
                p->nation = -1;
                AddChat("* " + p->name + " picked " + name);
                BroadcastLobby();
                break;
            }
            int n = CampaignIndexOf(name);
            if (n == 0) {
                refuse("it is the host's nation in this campaign");
                break;
            }
            if (n < 1 || n > 8) {
                refuse("not part of this campaign");
                break;
            }
            std::string owner = ClaimedBy(n);
            if (!owner.empty() && owner != p->name) {
                refuse("taken by " + owner);
                break;
            }
            if (p->nation > 0 && g_reserved.count(p->nation) && g_reserved[p->nation] == p->name)
                g_reserved.erase(p->nation);
            if (p->nation > 0 && p->nation != n) DropDiplomacyFor(p->nation);  // the old nation goes back to the AI
            p->nation = n;
            p->want = name;
            if (g_phase != Phase::Lobby) {
                g_reserved[n] = p->name;
                SaveReservations();
            }
            AddChat("* " + p->name + " commands " + name);
            BroadcastLobby();
            if (g_phase == Phase::Planning) {
                std::wstring dir = game::SaveDir(g_hostSlot);
                int y = 0, mo = 0;
                if (game::SaveGame()) {
                    saveio::ReadDate(dir, g_hostSlot, y, mo);
                    SendStateTo(*p, dir, y, mo);
                }
            }
            break;
        }
        case net::MSG_CHAT: {
            if (!p || !p->welcomed) break;
            std::string text = KVStr(KVDecode(m.data), "text");
            if (text.empty()) break;
            if (text.size() > 400) text.resize(400);
            KV c;
            c["from"] = p->name;
            c["text"] = text;
            net::SendAll(net::MSG_CHAT, KVEncode(c));
            AddChat(p->name + ": " + text);
            break;
        }
        case net::MSG_SUBMIT: {
            if (!p || !p->welcomed) break;
            KV h;
            std::string body;
            if (!UnpackHB(m.data, h, body)) break;
            if (KVInt(h, "seq") != g_seq || g_phase != Phase::Planning) {
                AddChat("* Late turn from " + p->name + " ignored");
                break;
            }
            if (KVInt(h, "nation") != p->nation) break;
            Bundle b;
            if (!b.Unpack(body)) break;
            g_submissions[m.peer] = b;
            p->submitted = true;
            AddChat("* " + p->name + " submitted the turn");
            BroadcastLobby();
            if ((g_hostWantsTurn || g_forced) && AllSubmitted(nullptr)) TryStartHostTurn();
            break;
        }
        case net::MSG_DIPLO: {
            if (!p || !p->welcomed) break;
            KV kv = KVDecode(m.data);
            std::string why = p->nation > 0 ? HostDiplo(p->nation, KVStr(kv, "action"), KVInt(kv, "target", -1))
                                            : "Claim a nation first.";
            if (!why.empty()) {
                KV c;
                c["from"] = "*";
                c["text"] = why;
                net::Send(m.peer, net::MSG_CHAT, KVEncode(c));
            }
            break;
        }
        case net::MSG_ACK:
            break;
        case net::MSG_BYE:
            if (p) net::Kick(m.peer);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------------------------
// Client helpers

static void ClientApplyPendingState() {
    if (g_pendingState.empty() || game::IsBusy() || game::BattleActive()) return;
    std::string payload;
    payload.swap(g_pendingState);
    KV h;
    std::string body;
    Bundle b;
    if (!UnpackHB(payload, h, body) || !b.Unpack(body)) {
        Notice("Received a damaged game state.");
        return;
    }
    int nation = KVInt(h, "nation", -1);
    int seq = KVInt(h, "seq");
    std::string err;
    std::wstring dir = game::SaveDir(g_clientSlot);
    if (dir.empty() || !saveio::ApplyStateBundle(b, dir, g_clientSlot, err)) {
        Notice("Cannot store the received game: " + err);
        return;
    }
    g_clientIdStart = saveio::ReadIdNo(dir, g_clientSlot);
    if (!game::LoadCampaign(g_clientSlot, nation)) {
        Notice("Loading the received game failed.");
        return;
    }
    g_myNation = nation;
    g_clientSeq = seq;
    g_haveState = true;
    g_submitted = false;
    g_forceSubmitPending = false;
    g_remoteRemaining = KVInt(h, "remaining", -1);
    g_date = DateStr(KVInt(h, "year"), KVInt(h, "month"));
    game::SetTurnButtonCaption(L"Submit");
    KV a;
    a["seq"] = std::to_string(seq);
    net::Send(0, net::MSG_ACK, KVEncode(a));
    Notice("New month " + g_date + ": you command " + KVStr(h, "nationName") +
           ". Press Submit when your orders are ready.");
}

static void ParseLobby(const KV& kv) {
    g_hostName = KVStr(kv, "host");
    g_hostNationName = KVStr(kv, "hostNation");
    g_remoteCampaignReady = KVInt(kv, "campaign") != 0;
    g_remoteCampaignLoaded = KVInt(kv, "loaded") != 0;
    g_date = KVStr(kv, "date", g_date);
    std::string ph = KVStr(kv, "phase");
    g_remotePhase = ph == "planning" ? Phase::Planning : ph == "processing" ? Phase::Processing : Phase::Lobby;
    g_remoteRemaining = KVInt(kv, "remaining", -1);
    g_turnLimitSec = KVInt(kv, "limit");
    g_lobbyNations.clear();
    for (auto& e : Split(KVStr(kv, "nations"), ';')) {
        auto f = Split(e, ':');
        if (f.size() < 2 || f[1].empty()) continue;
        NationView nv;
        nv.index = atoi(f[0].c_str());
        nv.name = f[1];
        nv.claimedBy = f.size() > 2 ? f[2] : "";
        g_lobbyNations.push_back(nv);
    }
    g_lobbyPlayers.clear();
    int myNation = -1;
    std::string myWant;
    for (auto& e : Split(KVStr(kv, "players"), ';')) {
        auto f = Split(e, ':');
        if (f.size() < 5) continue;
        PlayerView pv;
        pv.peer = atoi(f[0].c_str());
        pv.name = f[1];
        pv.nation = atoi(f[2].c_str());
        pv.connected = f[3] == "1";
        pv.submitted = f[4] == "1";
        std::string want = f.size() > 5 ? f[5] : "";
        if (pv.peer == 0)
            pv.nationName = g_hostNationName;
        else if (pv.nation > 0) {
            for (auto& n : g_lobbyNations)
                if (n.index == pv.nation) pv.nationName = n.name;
            if (pv.nationName.empty()) pv.nationName = want;
        } else if (!want.empty())
            pv.nationName = want + " (planned)";
        g_lobbyPlayers.push_back(pv);
        if (pv.peer != 0 && pv.name == g_myName) {
            myNation = pv.nation;
            myWant = want;
        }
    }
    if (g_myNation > 0 && myNation <= 0 && g_haveState) {
        g_haveState = false;
        game::SetTurnButtonCaption(L"Turn");
        Notice("The host gave your nation to the AI. Pick another nation to keep playing.");
    }
    g_myNation = myNation;
    g_myWant = myWant;
    g_phase = g_remotePhase;
    g_cSnap = DecodeDipSnap(kv);
}

static void ClientOnMessage(net::Msg& m) {
    switch (m.type) {
        case net::EV_CONNECTED: {
            KV kv;
            kv["proto"] = std::to_string(MP_PROTOCOL);
            kv["mod"] = MP_MOD_VERSION;
            kv["build"] = game::BuildFingerprint();
            kv["name"] = g_myName;
            kv["pw"] = g_password;
            net::Send(0, net::MSG_HELLO, KVEncode(kv));
            Notice("Connected, waiting for the host...");
            break;
        }
        case net::EV_DISCONNECTED: {
            Notice("Disconnected from host: " + m.data);
            AddChat("* Disconnected: " + m.data);
            g_role = Role::None;
            g_phase = Phase::Lobby;
            g_haveState = false;
            game::SetTurnButtonCaption(L"Turn");
            net::Shutdown();
            break;
        }
        case net::MSG_REJECT:
            Notice("The host refused the connection: " + KVStr(KVDecode(m.data), "reason"));
            break;
        case net::MSG_WELCOME: {
            KV kv = KVDecode(m.data);
            std::string assigned = KVStr(kv, "name", g_myName);
            if (assigned != g_myName) AddChat("* That name was taken; you joined as " + assigned);
            g_myName = assigned;
            AddChat("* Joined " + KVStr(kv, "host") + "'s game");
            Notice("Joined. Pick a nation in the list and press Claim nation.");
            break;
        }
        case net::MSG_LOBBY:
            ParseLobby(KVDecode(m.data));
            Changed();
            break;
        case net::MSG_STATUS: {
            KV kv = KVDecode(m.data);
            std::string ph = KVStr(kv, "phase");
            g_remotePhase = ph == "planning" ? Phase::Planning : ph == "processing" ? Phase::Processing : Phase::Lobby;
            g_phase = g_remotePhase;
            g_remoteRemaining = KVInt(kv, "remaining", -1);
            std::string text = KVStr(kv, "text");
            if (!text.empty() && text != g_status) g_status = text;
            if (g_phase == Phase::Processing && g_haveState) {
                g_haveState = false;
                game::SetTurnButtonCaption(L"Waiting");
            }
            Changed();
            break;
        }
        case net::MSG_CHAT: {
            KV kv = KVDecode(m.data);
            std::string from = KVStr(kv, "from");
            AddChat(from == "*" ? "* " + KVStr(kv, "text") : from + ": " + KVStr(kv, "text"));
            break;
        }
        case net::MSG_STATE:
            g_pendingState = m.data;
            Notice("Receiving the new month from the host...");
            ClientApplyPendingState();
            break;
        case net::MSG_FORCE_SUBMIT:
            if (g_haveState && !g_submitted) {
                g_forceSubmitPending = true;
                Notice("Time is up: submitting your turn now.");
                SubmitTurn();
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------------------------
// Public API

void Init() {
    char buf[32];
    if (GetEnvironmentVariableA("RTW3MP_CLIENT_SLOT", buf, sizeof(buf)) > 0) {
        int s = atoi(buf);
        if (s >= 50 && s < 1000) g_clientSlot = s;
    }
}

bool Host(const std::string& name, int port, const std::string& password, int turnLimitMin, std::string& err) {
    Leave();
    g_myName = CleanName(name);
    g_password = password;
    g_turnLimitSec = std::max(0, turnLimitMin) * 60;
    if (!net::Listen((uint16_t)port, err)) return false;
    g_role = Role::Host;
    g_phase = Phase::Lobby;
    g_myNation = 0;
    g_players.clear();
    g_submissions.clear();
    g_reserved.clear();
    g_dip.clear();
    g_pactAllies.clear();
    g_pactWars.clear();
    g_hostSlot = -1;
    g_lastCampaignKey.clear();  // SyncHostCampaign picks up the open campaign and its saved assignments
    if (turnLimitMin > 0) g_turnLimitSec = turnLimitMin * 60;
    AddChat("* Hosting on port " + std::to_string(port));
    Notice("Hosting. Everyone can pick a nation now.");
    BroadcastLobby();
    return true;
}

bool Join(const std::string& name, const std::string& ip, int port, const std::string& password, std::string& err) {
    Leave();
    g_myName = CleanName(name);
    g_password = password;
    if (!net::Connect(ip, (uint16_t)port, err)) return false;
    g_role = Role::Client;
    g_phase = Phase::Lobby;
    g_myNation = -1;
    g_haveState = g_submitted = false;
    g_pendingState.clear();
    Notice("Connecting to " + ip + ":" + std::to_string(port) + "...");
    return true;
}

void Leave() {
    if (g_role == Role::None) return;
    net::SendAll(net::MSG_BYE, "");
    Sleep(50);
    net::Shutdown();
    if (g_role == Role::Client) game::SetTurnButtonCaption(L"Turn");
    g_role = Role::None;
    g_phase = Phase::Lobby;
    g_players.clear();
    g_dip.clear();
    g_pactAllies.clear();
    g_pactWars.clear();
    g_cSnap = DipSnap();
    g_submissions.clear();
    g_haveState = g_submitted = false;
    g_pendingState.clear();
    g_hostWantsTurn = g_turnRunning = g_afterTurnPending = false;
    g_autoStart = false;
    AddChat("* Left the session");
    Notice("Not connected.");
}

int StartSession() {
    if (g_role != Role::Host || g_phase != Phase::Lobby) return -1;
    if (!CampaignReady()) {
        g_autoStart = true;
        AddChat("* The session will start as soon as the host's campaign is ready");
        BroadcastLobby();
        return CampaignLoaded() ? 2 : 1;
    }
    g_autoStart = false;
    g_hostSlot = game::CurrentSlot();
    for (auto& p : g_players) {
        if (!p.second.welcomed) continue;
        if (p.second.nation > 0)
            g_reserved[p.second.nation] = p.second.name;
        else
            AddChat("* " + p.second.name + " has no nation yet and will only watch until they pick one");
    }
    SaveReservations();
    AddChat("* Session started");
    BroadcastState();
    return 0;
}

void AdvanceNow() {
    if (g_role != Role::Host || g_phase != Phase::Planning) return;
    TryStartHostTurn();
}

void ClaimNation(const std::string& nationName) {
    std::string name = CleanName(nationName);
    if (g_role == Role::Client) {
        KV kv;
        kv["name"] = name;
        net::Send(0, net::MSG_PICK, KVEncode(kv));
        return;
    }
    if (g_role != Role::Host) return;
    if (CampaignLoaded()) {
        Notice("You command " + CampaignNationName(0) +
               ", the nation chosen at New Game. The other players pick from the remaining nations.");
        return;
    }
    std::string owner = WantedBy(name);
    if (!owner.empty() && owner != g_myName + " (host)") {
        Notice(name + " is already picked by " + owner + ".");
        return;
    }
    g_hostWant = name;
    AddChat("* Host plans to play " + name + ". Choose it on the New Game screen.");
    BroadcastLobby();
}

// Host: match lobby picks to the campaign once one is open, and restore saved assignments of that campaign.
static void SyncHostCampaign() {
    if (g_role != Role::Host || g_phase != Phase::Lobby) return;
    std::string key = CampaignLoaded() ? CampaignKey() : "";
    if (key == g_lastCampaignKey) return;
    g_lastCampaignKey = key;
    for (auto& kv : g_players) kv.second.nation = -1;
    g_reserved.clear();
    if (key.empty()) {
        BroadcastLobby();
        return;
    }
    g_hostSlot = game::CurrentSlot();
    LoadReservations();
    for (auto& kv : g_players) {
        Player& p = kv.second;
        if (!p.welcomed) continue;
        for (auto& r : g_reserved)
            if (r.second == p.name) {
                p.nation = r.first;
                p.want = CampaignNationName(r.first);
            }
    }
    for (auto& kv : g_players) {
        Player& p = kv.second;
        if (!p.welcomed || p.nation > 0 || p.want.empty()) continue;
        int n = CampaignIndexOf(p.want);
        if (n == 0)
            AddChat("* " + p.name + ": " + p.want + " is the host's nation in this campaign. Please pick another.");
        else if (n < 1)
            AddChat("* " + p.name + ": " + p.want + " is not in this campaign. Please pick another.");
        else if (!ClaimedBy(n).empty())
            AddChat("* " + p.name + ": " + p.want + " is already taken. Please pick another.");
        else
            p.nation = n;
    }
    std::string mine = CampaignNationName(0);
    AddChat("* Campaign open: host plays " + mine);
    if (!g_hostWant.empty() && g_hostWant != mine) AddChat("* (the host had planned " + g_hostWant + ")");
    g_hostWant.clear();
    BroadcastLobby();
}

void ReleaseNation(int idx) {
    if (g_role != Role::Host) return;
    g_reserved.erase(idx);
    for (auto& p : g_players)
        if (p.second.nation == idx) {
            p.second.nation = -1;
            p.second.want.clear();
        }
    DropDiplomacyFor(idx);
    SaveReservations();
    AddChat("* " + W2U(game::NationName(idx)) + " is now controlled by the AI");
    BroadcastLobby();
}

std::string ReleaseNationByName(const std::string& name) {
    if (g_role != Role::Host) return "Only the host can give nations to the AI.";
    if (CampaignLoaded()) {
        int idx = CampaignIndexOf(name);
        if (idx == 0) return name + " is your own nation.";
        if (idx < 1) return name + " is not part of this campaign.";
        if (ClaimedBy(idx).empty()) return name + " is already controlled by the AI.";
        ReleaseNation(idx);
        return "";
    }
    // No campaign yet: remove the lobby pick.
    if (g_hostWant == name) {
        g_hostWant.clear();
        AddChat("* The host no longer plans to play " + name);
        BroadcastLobby();
        return "";
    }
    for (auto& p : g_players) {
        if (p.second.welcomed && p.second.want == name) {
            p.second.want.clear();
            p.second.nation = -1;
            AddChat("* " + name + " will be controlled by the AI (" + p.second.name + " can pick another nation)");
            BroadcastLobby();
            return "";
        }
    }
    return name + " is already controlled by the AI.";
}

void SetTurnLimit(int minutes) {
    if (g_role != Role::Host) return;
    g_turnLimitSec = std::max(0, minutes) * 60;
    if (g_phase == Phase::Planning) g_deadline = g_turnLimitSec > 0 ? GetTickCount() + g_turnLimitSec * 1000 : 0;
    SaveReservations();
    BroadcastLobby();
}

void SendChat(const std::string& text) {
    std::string t = Trim(text);
    if (t.empty() || g_role == Role::None) return;
    KV kv;
    kv["text"] = t;
    if (g_role == Role::Host) {
        kv["from"] = g_myName;
        net::SendAll(net::MSG_CHAT, KVEncode(kv));
        AddChat(g_myName + ": " + t);
    } else {
        net::Send(0, net::MSG_CHAT, KVEncode(kv));
    }
}

void SubmitTurn() {
    if (g_role != Role::Client) return;
    if (!g_haveState || g_phase != Phase::Planning) {
        Notice("Nothing to submit: wait for the host to send the month.");
        return;
    }
    if (game::IsBusy()) {
        g_forceSubmitPending = true;
        Notice("Close open dialogs first; your turn will be submitted then.");
        return;
    }
    if (game::CurrentSlot() != g_clientSlot) {
        Notice("Another game is loaded; cannot submit.");
        return;
    }
    game::SaveGame();
    Bundle b;
    std::string err;
    if (!saveio::BuildSubmission(game::SaveDir(g_clientSlot), g_clientSlot, g_myNation, g_clientIdStart, b, err)) {
        Notice("Cannot package your turn: " + err);
        return;
    }
    KV h;
    h["seq"] = std::to_string(g_clientSeq);
    h["nation"] = std::to_string(g_myNation);
    net::Send(0, net::MSG_SUBMIT, PackHB(KVEncode(h), b.Pack()));
    g_submitted = true;
    g_forceSubmitPending = false;
    game::SetTurnButtonCaption(L"Submitted");
    Notice("Turn submitted. You can still change orders and press Submit again until the host processes the turn.");
}

std::string Diplomacy(const std::string& action, int target) {
    if (g_role == Role::Host) return HostDiplo(0, action, target);
    if (g_role != Role::Client) return "Not connected.";
    if (g_phase != Phase::Planning) return "Diplomacy is possible while the players plan a month.";
    if (g_myNation <= 0) return "Claim a nation first.";
    // Quick check against the host's last relations snapshot; the host validates again.
    const DiploRow* r = nullptr;
    auto rows = DiploRows(g_myNation, g_cSnap);
    for (auto& x : rows)
        if (x.nation == target) r = &x;
    if (!r) return "You can only negotiate with nations commanded by other players.";
    const std::string& B = r->nationName;
    if (action == "war" && !r->canWar)
        return r->atWar ? "You are already at war with " + B + "." : "A declaration of war between you and " + B + " is already pending.";
    if (action == "ally" && !r->allyAccept && !r->canAlly)
        return r->atWar ? "Make peace with " + B + " first." : r->allied ? "You are already allied with " + B + "."
                                                                           : "An alliance with " + B + " is already offered or agreed.";
    if (action == "peace" && !r->peaceAccept && !r->canPeace)
        return r->atWar ? "Peace with " + B + " is already offered or agreed." : "You are not at war with " + B + ".";
    if (action == "leave" && !r->canLeave) return "You are not allied with " + B + ".";
    if (action == "cancel" && !r->canCancel) return "Nothing to withdraw or decline with " + B + ".";
    KV kv;
    kv["action"] = action;
    kv["target"] = std::to_string(target);
    net::Send(0, net::MSG_DIPLO, KVEncode(kv));
    return "";
}

std::string DebugDiplomacyAs(int from, const std::string& action, int target) {
    if (g_role != Role::Host) return "host only";
    return HostDiplo(from, action, target);
}

void DebugReserve(int nation, const std::string& player) {
    if (g_role != Role::Host || nation < 1 || nation > 8) return;
    g_reserved[nation] = CleanName(player);
    SaveReservations();
    BroadcastLobby();
}

void DebugApplyDiplomacy() {
    if (g_role != Role::Host) return;
    ApplyDiplomacy();
    BroadcastLobby();
}

void OnNet() {
    net::Msg m;
    while (net::Pop(m)) {
        try {
            if (g_role == Role::Host)
                HostOnMessage(m);
            else if (g_role == Role::Client)
                ClientOnMessage(m);
        } catch (...) {
            Log("exception handling message %u", m.type);
        }
    }
}

void Tick() {
    DWORD now = GetTickCount();
    if (g_role == Role::Host) {
        SyncHostCampaign();
        if (g_autoStart && g_phase == Phase::Lobby && CampaignReady() && !game::IsBusy()) StartSession();
        if (g_phase == Phase::Planning && g_deadline && (int)(now - g_deadline) >= 0 && !g_forced) {
            g_forced = true;
            g_forceTick = now;
            KV kv;
            kv["seq"] = std::to_string(g_seq);
            net::SendAll(net::MSG_FORCE_SUBMIT, KVEncode(kv));
            AddChat("* Time is up for this month");
        }
        if (g_phase == Phase::Planning && g_forced &&
            (AllSubmitted(nullptr) || now - g_forceTick > 30000) && !game::IsBusy())
            RunHostTurn();
        if (g_runTurnWhenIdle && g_phase == Phase::Planning && !game::IsBusy()) RunHostTurn();
        if (g_turnRunning) {
            if (g_afterTurnPending) {
                if (!game::IsBusy() && !game::BattleActive()) {
                    g_afterTurnPending = false;
                    g_turnRunning = false;
                    ApplyDiplomacy();
                    BroadcastState();
                }
            } else if (!game::IsBusy() && !game::BattleActive()) {
                // The game finished the click without ending the month (cancelled, or waiting after a battle).
                if (!g_idleSinceTick) g_idleSinceTick = now;
                if (now - g_idleSinceTick > 4000 && now - g_turnStartTick > 4000) {
                    g_turnRunning = false;
                    g_idleSinceTick = 0;
                    AddChat("* The turn did not advance; resending the current month");
                    BroadcastState();
                }
            } else {
                g_idleSinceTick = 0;
                if (game::BattleActive() && now - g_lastStatusTick > 5000) {
                    g_lastStatusTick = now;
                    BroadcastStatus("The host is fighting a battle...");
                }
            }
        }
        if (g_phase == Phase::Planning && now - g_lastStatusTick > 3000) {
            g_lastStatusTick = now;
            std::string missing;
            AllSubmitted(&missing);
            std::string text = missing.empty() ? "All players are ready." : "Waiting for: " + missing;
            if (g_hostWantsTurn) text = "Host is ready. " + text;
            BroadcastStatus(text);
        }
    } else if (g_role == Role::Client) {
        if (!g_pendingState.empty()) ClientApplyPendingState();
        if (g_forceSubmitPending && !g_submitted && g_haveState && !game::IsBusy()) SubmitTurn();
    }
}

View GetView() {
    View v;
    v.role = g_role;
    v.phase = g_phase;
    v.myName = g_myName;
    v.myNation = g_myNation;
    v.status = g_status;
    v.turnLimitMin = g_turnLimitSec / 60;
    v.remainingSec = RemainingSec();
    v.submitted = g_submitted;
    if (g_role == Role::Host) {
        v.hostName = g_myName;
        v.campaignLoaded = CampaignLoaded();
        v.campaignReady = CampaignReady();
        v.date = HostDate();
        v.myNationName = HostNationLabel();
        PlayerView hv;
        hv.peer = 0;
        hv.name = g_myName;
        hv.nation = 0;
        hv.nationName = v.myNationName;
        hv.connected = true;
        hv.submitted = g_hostWantsTurn;
        v.players.push_back(hv);
        int withNation = 0;
        for (auto& p : g_players) {
            if (!p.second.welcomed) continue;
            PlayerView pv;
            pv.peer = p.first;
            pv.name = p.second.name;
            pv.nation = p.second.nation;
            pv.nationName = PlayerNationLabel(p.second);
            pv.connected = p.second.connected;
            pv.submitted = p.second.submitted;
            pv.ping = net::PeerPingMs(p.first);
            v.players.push_back(pv);
            if (p.second.nation > 0) withNation++;
        }
        v.nations = HostNationRows();
        if (g_phase == Phase::Lobby) {
            std::string autoNote = g_autoStart ? " The session starts automatically once it is ready." : "";
            if (!v.campaignLoaded)
                v.hint = g_autoStart ? "Waiting for your campaign: on the title screen choose New Game (pick your "
                                       "nation) or Load Game." + autoNote
                                     : "Next: pick the nation you want to play (optional), then start a New Game with "
                                       "it, or load a saved campaign. Players can pick their nations now.";
            else if (!v.campaignReady)
                v.hint = "Next: on the campaign screen press Start, then Continue, until the button says Turn." +
                         (g_autoStart ? autoNote : std::string(" Then press Start session."));
            else
                v.hint = "Next: press Start session (" + std::to_string(withNation) + " player(s) have a nation).";
        }
    } else if (g_role == Role::Client) {
        v.hostName = g_hostName;
        v.campaignLoaded = g_remoteCampaignLoaded;
        v.campaignReady = g_remoteCampaignReady;
        v.date = g_date;
        v.players = g_lobbyPlayers;
        v.nations = g_lobbyNations;
        for (auto& n : g_lobbyNations)
            if (g_myNation > 0 && n.index == g_myNation) v.myNationName = n.name;
        if (v.myNationName.empty() && !g_myWant.empty()) v.myNationName = g_myWant + " (planned)";
        if (g_phase == Phase::Lobby && !v.nations.empty()) {
            if (g_myNation > 0)
                v.hint = "You will command " + v.myNationName + ". Waiting for the host to start the session.";
            else if (!g_myWant.empty())
                v.hint = "Your pick: " + g_myWant + ". Waiting for the host to open the campaign.";
            else
                v.hint = "Next: choose a nation in the list and press Claim nation.";
        }
    }
    if (g_role != Role::None) {
        if (g_role == Role::Host) {
            DipSnap s = HostDipSnap();
            v.diplo = DiploRows(0, s);
        } else {
            v.diplo = DiploRows(g_myNation, g_cSnap);
        }
        bool available = g_role == Role::Host ? game::DiplomacyAvailable() : g_cSnap.on || g_phase == Phase::Lobby;
        v.diploOpen = g_phase == Phase::Planning && available && (g_role == Role::Host || g_myNation > 0);
        if (g_phase == Phase::Lobby)
            v.diploNote = "opens when the session starts.";
        else if (!available)
            v.diploNote = "not available with this game version.";
        else if (g_phase == Phase::Processing)
            v.diploNote = "the month is being processed.";
        else if (v.diplo.empty())
            v.diploNote = "no other player commands a nation yet.";
        else
            v.diploNote = "decisions take effect at the end of this month.";
    }
    return v;
}

const std::vector<std::string>& Chat() { return g_chat; }

}  // namespace mp

// ---------------------------------------------------------------------------------------------
// Hook callbacks

namespace hookcb {

static void ResetPeaceMask();

bool OnTurnClick(void* self, void* sender) {
    using namespace mp;
    if (g_role == Role::Host && g_phase != Phase::Lobby) {
        if (game::StartBuildPhase() != 2) return false;
        if (game::CurrentSlot() != g_hostSlot) return false;
        if (g_phase == Phase::Processing) return true;
        std::string missing;
        if (AllSubmitted(&missing)) {
            TryStartHostTurn();
            return true;
        }
        g_hostWantsTurn = true;
        BroadcastLobby();
        std::wstring msg = L"Still waiting for: " + U2W(missing) +
                           L"\n\nYes = advance now (their nations keep last month's orders)\nNo = wait; the turn "
                           L"starts automatically when everyone has submitted.";
        int r = MessageBoxW(game::FormHwnd(self), msg.c_str(), L"Multiplayer", MB_YESNO | MB_ICONQUESTION);
        if (r == IDYES) TryStartHostTurn();
        return true;
    }
    if (g_role == Role::Client) {
        SubmitTurn();
        return true;
    }
    return false;
}

bool OnResignClick(void* self) {
    if (mp::g_role == mp::Role::None) return false;
    MessageBoxW(game::FormHwnd(self), L"Resigning is disabled during a multiplayer session. Leave the session from "
                                      L"the Multiplayer window first.",
                L"Multiplayer", MB_OK | MB_ICONINFORMATION);
    return true;
}

void OnAfterEndOfTurn(void* self) {
    ResetPeaceMask();
    if (mp::g_role == mp::Role::Host && mp::g_turnRunning) {
        mp::g_afterTurnPending = true;
        Log("DoEndOfTurn finished");
    }
}

bool IsHumanRemoteNation(int idx) {
    using namespace mp;
    if (g_role != Role::Host || g_phase == Phase::Lobby || idx <= 0) return false;
    if (g_reserved.count(idx)) return true;
    for (auto& p : g_players)
        if (p.second.nation == idx) return true;
    return false;
}

void OnBeforeAIMoves(void* self) {
    mp::g_aiMoveSnaps.clear();
    for (int i = 1; i <= 8 && i < game::NationCount(); i++)
        if (IsHumanRemoteNation(i)) mp::g_aiMoveSnaps[i] = game::SnapshotShips(i);
}

// The game's AI ends wars between non-player nations by itself (war weariness, or forced by unrest). Wars the
// players declared on each other are hidden from it (tension just below the war level) for the duration of the
// call; only the two players can end them.
static int g_peaceDepth = 0;

bool OnBeforeAIPeace(int nationIdx, bool badLoss) {
    using namespace mp;
    if (g_peaceDepth++ > 0) return false;  // nested call: the outer mask is already in place
    if (g_role != Role::Host || g_phase == Phase::Lobby || g_pactWars.empty()) return false;
    for (auto& w : g_pactWars) {
        if (w.first <= 0 || !game::AtWar(w.first, w.second)) continue;
        g_peaceMask.push_back(std::make_tuple(w.first, w.second, game::Tension(w.first, w.second)));
        game::SetTensionRaw(w.first, w.second, 40);
    }
    if (g_peaceMask.empty()) return false;
    if (nationIdx >= 1 && nationIdx <= 8 && !game::AtWarWithAnyAI(nationIdx)) {
        Log("AI peace (badLoss=%d) skipped for nation %d: its wars were declared by the players", badLoss, nationIdx);
        OnAfterAIPeace();
        return true;
    }
    Log("AI peace for nation %d: %d player war(s) kept out of it", nationIdx, (int)g_peaceMask.size());
    return false;
}

void OnAfterAIPeace() {
    if (g_peaceDepth > 0) g_peaceDepth--;
    if (g_peaceDepth > 0) return;
    for (auto& m : mp::g_peaceMask) game::SetTensionRaw(std::get<0>(m), std::get<1>(m), std::get<2>(m));
    mp::g_peaceMask.clear();
}

static void ResetPeaceMask() {  // in case the game's routine was left by an exception
    if (g_peaceDepth || !mp::g_peaceMask.empty()) Log("peace mask reset (depth %d)", g_peaceDepth);
    g_peaceDepth = 1;
    OnAfterAIPeace();
}

void OnAfterAIMoves(void* self) {
    for (auto& s : mp::g_aiMoveSnaps) {
        int n = game::RestoreShips(s.second);
        if (n) Log("AI moves undone for nation %d: %d ships", s.first, n);
    }
    mp::g_aiMoveSnaps.clear();
}

}  // namespace hookcb

#include "session.h"
#include "datafiles.h"
#include "economy.h"
#include "game.h"
#include "lockstep.h"
#include "nationplay.h"
#include "net.h"
#include "saveio.h"
#include "warfx.h"
#include "invasion.h"
#include "x87.h"
#include <iterator>
#include <tuple>
#include <climits>
#include <random>
#include <algorithm>

namespace mp {

std::function<void()> g_onChanged;
std::function<void(const std::string&)> g_onNotice;
std::function<void()> g_onReveal;

struct Player {
    int peer = -1;
    std::string name;
    int nation = -1;    // campaign nation index once resolved
    std::string want;   // nation picked in the lobby (by name), also before a campaign exists
    bool connected = false;
    bool welcomed = false;
    bool submitted = false;
    bool hasState = false;
    int eventMonth = 0;  // Year*12+Month of the last random event applied for this player
    bool battles = false;  // fights its own battles against AI nations (announced in its hello)
    bool lockstep = false;  // can fight live battles against other players (lockstep.cpp; announced in its hello)
    int lsLevel = 0;        // its live-battle engine level (hello `ls`; 0.3.x: 1; 0 = no live battles)
    std::string mod;        // its mod version (hello)
    int offeredSerial = -1;  // the last rejoinable battle offered to this player (lockstep RejoinInfo.serial, phase 5b)
    int relLevel = 0;        // relations v2: the player's PC runs the player's relation routines (hello `rel`; 0 = no)
    int genLevel = 0;        // 0.6.0: its PC replays the player's battle generator's attempts (hello `gen`; 0 = no)
    int divLevel = 0;        // 0.6.1: its PC loads and sends the nation's own campaign divisions (hello `div`; 0 = no)
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
    struct War {  // victory points of a war between two players and what a peace would bring
        int vpLo = 0, vpHi = 0;
        int winner = -1, level = 0;  // level 0 compromise, 1 minor win, 2 major win
    };
    struct Treaty {  // terms frozen when peace was agreed; the winner may claim possessions until the month ends
        int winner = -1, loser = -1, level = 0, budget = 0;
        std::vector<ClaimOption> claimable;
        std::vector<std::string> claims;
    };
    bool on = false;
    std::vector<Human> humans;
    std::map<std::pair<int, int>, std::pair<char, int>> rel;  // (low, high) -> W/A/P, tension
    std::map<std::pair<int, int>, War> wars;
    std::map<std::pair<int, int>, Treaty> treaties;
    std::vector<DipItem> items;
};
static std::vector<DipItem> g_dip;                        // host
static std::set<std::pair<int, int>> g_pactAllies, g_pactWars;  // host: agreements made with the buttons
static std::vector<std::tuple<int, int, int>> g_peaceMask;   // host: wars hidden from the AI peace routine
static DipSnap g_cSnap;                                   // client: as broadcast by the host
static const int kPactMonths = 60;

// Peace treaties between players (host). Victory points of each war between two players, counted per battle
// (low nation, high nation) -> (VP of low, VP of high), and the terms of each agreed peace.
struct TreatyTerms {
    int winner = -1, loser = -1, level = 0;
    std::vector<std::string> claims;
};
static std::map<std::pair<int, int>, std::pair<int, int>> g_warVP;
static std::map<std::pair<int, int>, TreatyTerms> g_treaties;
// The game's EnemyVP (the host's enemies' points) already given to the enemies' nations; -1 = not known yet.
static int g_enemyVPSeen = -1;
static std::vector<std::string> g_pendingTreaty;          // treaty lines to show when the game is idle (the host
                                                          // too: after the new month went out)

// Monthly countdowns the game only runs for the host's nation, done here for the joined players' nations (host).
static int g_designSeq = -1;                              // month (g_seq) whose design studies were advanced
static int g_doctrineSeq = -1;                            // month (g_seq) whose doctrine changes were advanced
static std::map<int, std::vector<std::string>> g_playerNews;  // nation -> lines for its player
static std::map<int, std::vector<std::string>> g_treatyNews;  // nation -> peace treaty for its player's dialog
static std::mt19937 g_rng((unsigned)(GetTickCount() ^ (GetCurrentProcessId() << 16)));
// The war counter of each joined player's nation (relations v2, B1), kept as the game keeps nation 0's ([General] War):
// months at war (> 0) or at peace (<= 0). Stepped once per month by the host; saved with the month; sent with it.
static std::map<int, int> g_kWar;                         // nation -> counter
static std::map<int, std::pair<int, int>> g_kDecl;        // 0.6.0: nation -> its DeclineCount, EnemyDeclineCount
static std::vector<std::wstring> g_invNews[9];            // 0.7.0: a joined nation's invasion texts for its next month
static void ClearQueuedNews(const char* why);
static int g_hostGen = 0;  // client: the host's battle requests (welcome `gen`; 1 = it merges a declined battle; 2 = it
                           // takes surprise attacks (kind 2) and takeovers at a war's start; 3 = raider checks, 0.7.0)
// 0.6.0 (B6 piece 2), client tests: the next relation turn's forced war plan of nation J with a surprise attack
// (`battles testsa J [k] [land]`; 0.7.0 `land`: its surprise landing in this nation's possession, forced), and
// CheckSurpriseAttack's random gates passed by asking again (`battles saretry n`).
static struct {
    int J = -1;
    bool k = false, land = false;
} g_testSa;
static int g_testSaRetry = 0;
static int g_kWarSteppedSeq = -1;                         // the month (g_seq) whose processing last stepped them
// The joined nations whose relations follow the player's rules (relations v2, B2: the last player who held the nation
// runs the player's relation routines; kept per campaign), and the ones a test switched on (bridge `relrules`).
static std::set<int> g_relNations, g_relForced;
// Relations v2, B3 (host): each such nation's own per-relation state, kept for it as the game keeps nation 0's in the
// other nations' records: their war plans against it (WillGoToWar) and its tech-sharing deals (TechSharing), nine values
// each, saved with the month; its past wars (opponent, end as Year*12+Month, winner 1 it / 2 the other / 0 unknown) and
// its enemies of the last month (a war that ended goes into the history).
static std::map<int, std::vector<int>> g_kWill, g_kTs;
// Relations v2, B5 (d): the joined nations whose own LendLeaseDone is set (the game's is the host's; kept per campaign).
static std::set<int> g_kLend;
static std::map<int, int> g_kRevo;  // B5: each joined nation's own RevolutionPending (real index; the game saves it)
struct KPastWar {
    int opp, end, winner;
};
static std::map<int, std::vector<KPastWar>> g_kHist;
static std::map<int, std::set<int>> g_kEnemies;
// nation -> Year*12+Month of its last relation turn applied (saved with the month: a host that hosts again in the same
// month doesn't apply it a second time)
static std::map<int, int> g_kRelMonth;
static std::string g_lastHostRelations;
// Relations v2, B4 (host): each such nation's month whose peace with the computer's nations was settled (a proposal, or a
// peace event of its turn): one a month (saved with the month, as g_kRelMonth). Its war score with each computer nation
// lives in g_warVP (LedgerPair).
static std::map<int, int> g_kPeaceMonth;
static std::string g_lastHostPeace;

static std::pair<int, int> PairKey(int a, int b) { return {std::min(a, b), std::max(a, b)}; }

// client
static int g_clientSlot = 77;
static int g_clientSeq = -1;
static long long g_clientIdStart = 0;
static bool g_haveState = false;
static bool g_submitted = false;
static bool g_forceSubmitPending = false;
static bool g_submitForcedByHost = false;  // the host's time limit ran out: submit without the Turn's questions
static std::string g_pendingState;
static int g_remoteRemaining = -1;
static Phase g_remotePhase = Phase::Lobby;
static std::string g_hostName, g_hostNationName, g_date;
static std::vector<NationView> g_lobbyNations;
static std::vector<PlayerView> g_lobbyPlayers;
static bool g_remoteCampaignReady = false, g_remoteCampaignLoaded = false;
static std::string g_myWant;
static bool g_fightBattles = true;  // fight this nation's battles against AI nations (sent with the hello)
// Relations v2, B3 (client): the month whose player's relation routines are still to run (the host's header `rel`),
// the month whose event dialog was shown (the game gives the player one of the two a month), the setup from the header.
static int g_relTurnSeq = -1;
static int g_evShownSeq = -1;
static game::PlayerTurnSetup g_relSetup;
static std::string g_lastRelations = "none";
// 0.6.1 (client): the host runs the player's rules for this nation (the month's header has `rel` or `kvp`): the game
// declares no war between it and the nations people play (hookcb::WarOnlyByPlayers).
static bool g_relRulesHost = false;
// Relations v2, B4 (client): the host applies this nation's peace with the computer's nations (its header's kvp); the war
// scores of those wars (nation -> this nation's points, theirs); the month (Year*12+Month) in planning and the one whose
// peace was settled here (a proposal or a peace event: one a month); a "Propose peace" waiting for an idle moment; the
// next turn's forced peace event (tests); the last result; the Diplomacy rows of the computer's nations at war with it.
static bool g_peaceHost = false;
static std::map<int, std::pair<int, int>> g_peacePairs;
static int g_peaceStateMonth = 0, g_peaceMonth = 0;
static int g_peaceProposal = -1;
static int g_forcedPeaceCode = 0;
static int g_forcedPeaceChance = 0;  // tests: that event's NegotiatePeace chance (0: its answer's)
static std::string g_lastPeace = "none";
static std::vector<DiploRow> g_peaceRows;

// Tactical battles of joined players (see game.cpp). Host: battles of a joined player's nation against an AI nation are
// recorded during the month instead of being resolved by the game; after the month the host sends each one to its
// player (the month's files), waits for the results and merges them into the save, then sends the new month.
struct BattleReq {
    int id = 0;
    int a = -1, b = -1, countA = 0, countB = 0;  // the game's call: a attacks b in `area`
    std::wstring area;
    int human = -1, opp = -1;  // the joined player's nation and its opponent
    std::string player;
    int peer = -1;
    int state = 0;  // 0 queued, 1 sent, 2 result merged into the save, 3 to be resolved by the game, 4 resolved
    DWORD sentTick = 0;
    std::string why;      // why the game resolves it
    std::string sentBcs;  // the host save as sent with this battle (the merge's reference)
    // A battle between two players' nations fought live (lockstep.cpp): `human` fights it on its PC (A), `opp`'s
    // player commands the other side from its PC (B); the host relays their messages.
    bool pvp = false;
    std::string player2;
    bool avoided = false;  // the enemy declined battle on the player's PC (the player's VP gain was merged, 0.4.1)
    // 0.6.0: who found the battle. 0 the computer's generator (DoBattleBetweenAINations, resolved by the game if not
    // fought), 1 the player's generator for `human` (B6: the attempts K's PC replays; nothing if none gives a battle),
    // 2 a surprise attack at a war's start (B6 piece 2: CheckSurpriseAttack's pairs from K's relation turn, played with
    // the game's CheckSurpriseAttack in K's battle slot; only to players announcing hello `gen` 2). Only kind 0 falls
    // back to the game's resolution.
    int kind = 0;
    std::vector<game::BattleAttempt> attempts;
    bool declined = false;  // the player declined it; the game's consequences on the player's PC were merged (risk 4)
    // 0.7.0: a raider battle came of the request's raider check (an attempt of type 1, hello `gen` 3): 1 fought on the
    // player's PC, 2 auto-resolved there (QuickResolveRaiderBattle); `opp` is then the interception's enemy
    int raider = 0;
    // 0.7.0 (invasions, P3): the battle came of the request's invasion check (an attempt of type 2, hello `gen` 4);
    // `opp` is then the invasion battle's enemy
    bool invasion = false;
};
static std::vector<BattleReq> g_battles;
static int g_battleNextId = 1;
static bool g_battlePhase = false, g_battleFinishing = false, g_battleMerged = false;
static std::string g_battlePhaseBytes;  // the host save when the battle phase began (restored if a reload fails)
static int g_battleTimeoutSec = 30 * 60;
static DWORD g_battleStatusTick = 0;
// 0.7.0 (invasions, P3), host: a joined nation's invasions during the battle phase. Each battle of the nation takes
// them to its PC (its battle slot) and the result brings them back; `base` is what the phase's first battle took,
// `now` what the last result returned (the next battle takes it); applied by name after the phase's reload.
struct InvPhase {
    inv::KState plan0;                     // the plan before the phase (back if the battles' results are not loaded)
    std::vector<inv::PossState> base, now;
    bool changed = false;
};
static std::map<int, InvPhase> g_invPhase;

// Client: a battle of this player's nation, fought in a separate battle slot.
static std::string g_battleIn;  // a received battle waiting for an idle moment
static struct {
    int id = -1, K = -1, opp = -1;
    bool fighting = false, cancelled = false, starting = false;
    std::wstring area;
    std::string before;  // the battle slot's save before the battle
    int kind = 0;        // 0.6.0: the request's kind (BattleReq::kind)
    int cand = -1;       // ... and the attempt being replayed (kind 1)
    bool dc = false;     // ... and the nation's own decline counts are in the battle slot (sent back with the result)
    int raider = 0;      // 0.7.0: the raider check gave a battle: 1 fought, 2 auto-resolved (BattleReq::raider)
    std::vector<std::string> news;  // ... and the turn messages of the raider checks (the "thwarts" lines)
    bool inv = false;    // 0.7.0 (P3): the nation's invasions are in the battle slot (sent back with the result)
    int iopp = -1;       // ... and the invasion check gave a battle (or a decline) against this nation (real index)
    bool iatt = true;    // ... which the player's nation attacked (its own invasion) or defended against
} g_cb;
static struct {  // TEST ONLY (`battles testraid <side> <J>`): the next raider check forces an interception
    int side = -1, J = -1;
} g_testRaid;
static int g_battleSlot = 87;

// Live player-vs-player battles (lockstep.cpp). The host's own battles against a player's nation start from the
// game's DoBattle (host = A); battles between two players' nations from the battle phase (the first player = A).
static bool g_lsEnabled = true;  // offer (host) / accept (client) live battles
static bool g_autoStrike = false;  // host: the computer flies both sides' carrier strikes in live battles (ui option)
// The legacy fleet (0.7.0). Host: the option, and the nations whose hand-built legacy fleet is merged (kept in
// rtw3mp_session.ini: a resent first month must not clear it again). Client: the player's choice, the month (seq) whose
// legacy build ran here (its turn carries MPLegacy), the picture message due, and the build's start/end notices.
static bool g_legacyAllowed = true, g_legacyWanted = false;
static std::set<int> g_legacyDone;
static int g_legacyBuiltSeq = -1;
static bool g_legacyMsgDue = false;
static struct {
    int opp = -1, peer = -1, bid = -1;
    std::string name;
    DWORD since = 0, lastTry = 0;
    bool testSlotMode = false;  // the battle was started outside a month (tests): its close must not run one
} g_lsHost;
static std::map<int, std::pair<int, int>> g_lsRelay;  // host: battle id -> the two players' peers
static int g_lsNextBid = 100000;                     // host-vs-player battle ids (battle requests count from 1)
static char g_lsRole = 0;                            // client: 'A' or 'B' while a live battle is set up here
static bool g_lsLeave = false;                       // client B: leave the battle slot once the battle screen is closed
static struct {
    bool pending = false;
    int bid = -1, opp = -1;
    bool autoStrike = false;  // the host's carrier-strike option for this battle (the battle message's autostrike=)
    std::string p2;
    DWORD since = 0, lastTry = 0;
} g_lsClientA;

// Random events of the joined player's nation (rolled on this PC when a new month arrives).
static int g_eventRollSeq = -1;        // month (seq) whose event roll is still due
static int g_evUsed[100] = {};         // this nation's event cooldowns (months), like the game's own
static std::string g_evUsedKey;        // campaign + nation the cooldowns belong to
static int g_evUsedMonth = 0;          // Year*12+Month the cooldowns were last counted down to
static int g_evRolledMonth = 0;        // Year*12+Month whose event roll was made (one roll per month)
static int g_eventChancePct = 33;      // chance of an event in a month (the game's own roll is about 1 in 3)
static std::string g_lastEvent;        // summary of the last event (tests and log)

// The messages of the player's economy rules the host ran for this PC's nation (the month's `econnews`), shown once in
// the game's turn report before the month's event roll.
static std::vector<std::wstring> g_econNews;
static inv::KState g_invPlan;          // 0.7.0, client: the nation's invasion plan from the host's last state
static bool g_invPlanFromHost = false;  // ... the host keeps one (hostgen 4); else the form holds none

// 0.7.0, client: after every load of the player's slot the form's invasion fields are the nation's (the save holds the
// host player's: its Turn would send them as the nation's target).
static void ClientInvasionForm(int nation, const char* why) {
    if (!inv::Available() || nation < 1 || nation > 8) return;
    if (g_invPlanFromHost)
        Log("invasions: nation %d's plan in the form (%s): %s", nation, why, inv::ClientSetForm(g_invPlan, false).c_str());
    else if (!inv::FormTarget().empty())
        Log("invasions: the host keeps no invasion plan for nation %d (%s): %s", nation, why,
            inv::ClientSetForm(inv::KState(), true).c_str());
    else
        return;
    game::RefreshUI();
}
static int g_econNewsSeq = -1;

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

// A line in this PC's chat only (the host doesn't send it to the players).
static void AddLocalChat(const std::string& line) {
    g_chat.push_back(line);
    if (g_chat.size() > 300) g_chat.erase(g_chat.begin(), g_chat.begin() + 50);
    Changed();
}

// Host: a system line for one player, shown there as a notice (it flashes the window).
static void NoticeTo(const Player& p, const std::string& text) {
    KV c;
    c["from"] = "*";
    c["text"] = text;
    c["notice"] = "1";
    net::Send(p.peer, net::MSG_CHAT, KVEncode(c));
}

// Host: may this player's PC fight a live battle with the host's engine? (Both PCs of a live battle, and the host,
// need the same live-battle engine level, MP_LS_LEVEL.)
static bool SameLiveEngine(const Player& p) { return p.lsLevel == MP_LS_LEVEL; }

// Why a player's PC can't fight live battles with this host's (another live-battle level), in player language, for
// the reader: the player p themselves, the host, or another player. Both levels; the mod versions too when they differ
// (a test or development build can differ in level alone).
enum class LevelReader { Player, Host, Other };
static std::string LevelWhy(const Player& p, LevelReader r) {
    std::string lv = std::to_string(p.lsLevel), host = std::to_string(MP_LS_LEVEL);
    std::string who = r == LevelReader::Player ? "you" : p.name;
    std::string hostIs = r == LevelReader::Host ? "this host" : "the host";
    if (p.mod != MP_MOD_VERSION)  // (different mod versions can share an engine: 0.4.1 and 0.5.0 are both level 17)
        return "live battles need the same live-battle engine on both PCs: " + hostIs + " has " MP_MOD_VERSION
               " (engine " + host + "), " + who + (r == LevelReader::Player ? " have " : " has ") +
               (p.mod.empty() ? std::string("an older one") : p.mod) + " (engine " + lv + ")";
    return (r == LevelReader::Player ? std::string("your") : p.name + "'s") + " mod plays live battles at level " + lv +
           ", " + (r == LevelReader::Host ? std::string("this host's") : std::string("the host's")) + " at level " + host +
           " (both mod " MP_MOD_VERSION ", but different builds): live battles need the same level on both PCs";
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

static int HostMonth() {  // Year*12+Month of the host's save on disk, 0 if unknown
    int y = 0, m = 0;
    if (g_hostSlot > 0) saveio::ReadDate(game::SaveDir(g_hostSlot), g_hostSlot, y, m);
    return y > 0 ? y * 12 + m : 0;
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

// Possession names may contain any printable character except these separators.
static const char kSepRec = '\x1e', kSepField = '\x1f';

static std::string SafeText(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == kSepRec || c == kSepField || c == '|') c = ' ';
    return s;
}

static std::string EncodeWarVP() {
    std::string o;
    for (auto& w : g_warVP)
        o += std::to_string(w.first.first) + "-" + std::to_string(w.first.second) + ":" + std::to_string(w.second.first) +
             ":" + std::to_string(w.second.second) + ",";
    return o;
}

static void DecodeWarVP(const std::string& s) {
    g_warVP.clear();
    for (auto& e : Split(s, ',')) {
        auto f = Split(e, ':');
        if (f.size() != 3) continue;
        auto p = Split(f[0], '-');
        if (p.size() != 2) continue;
        int a = atoi(p[0].c_str()), b = atoi(p[1].c_str());
        if (a >= 0 && b > a && b <= 8) g_warVP[{a, b}] = {atoi(f[1].c_str()), atoi(f[2].c_str())};
    }
}

static std::string EncodeKWar() {
    std::string o;
    for (auto& w : g_kWar) o += std::to_string(w.first) + ":" + std::to_string(w.second) + ",";
    return o;
}

// 0.6.0: each joined nation's own decline counts (the game's DeclineCount and EnemyDeclineCount): "K:own:enemy,".
static std::string EncodeKDecl() {
    std::string o;
    for (auto& d : g_kDecl)
        o += std::to_string(d.first) + ":" + std::to_string(d.second.first) + ":" + std::to_string(d.second.second) + ",";
    return o;
}

static void DecodeKDecl(const std::string& s) {
    g_kDecl.clear();
    for (auto& e : Split(s, ',')) {
        auto f = Split(e, ':');
        int n = f.size() == 3 ? atoi(f[0].c_str()) : 0;
        if (n >= 1 && n <= 8) g_kDecl[n] = {atoi(f[1].c_str()), atoi(f[2].c_str())};
    }
}

static void DecodeKWar(const std::string& s) {
    g_kWar.clear();
    for (auto& e : Split(s, ',')) {
        auto f = Split(e, ':');
        if (f.size() != 2 || f[0].empty() || f[1].empty()) continue;
        int n = atoi(f[0].c_str());
        if (n >= 1 && n <= 8) g_kWar[n] = atoi(f[1].c_str());
    }
}

// A month (Year*12+Month) per nation: "K:month,..." (g_kRelMonth, g_kPeaceMonth).
static std::string EncodeMonthMap(const std::map<int, int>& m) {
    std::string o;
    for (auto& e : m)
        if (e.second > 0) o += std::to_string(e.first) + ":" + std::to_string(e.second) + ",";
    return o;
}

static std::map<int, int> DecodeMonthMap(const std::string& s) {
    std::map<int, int> m;
    for (auto& e : Split(s, ',')) {
        auto f = Split(e, ':');
        if (f.size() != 2 || f[0].empty() || f[1].empty()) continue;
        int n = atoi(f[0].c_str()), v = atoi(f[1].c_str());
        if (n >= 1 && n <= 8 && v > 0) m[n] = v;
    }
    return m;
}

// Nine values per nation: "K:v0.v1...v8,..." (relations v2, the per-relation state of each nation under the player's rules).
static std::string EncodeKArr(const std::map<int, std::vector<int>>& m) {
    std::string o;
    for (auto& e : m) {
        o += std::to_string(e.first) + ":";
        for (size_t i = 0; i < e.second.size(); i++) o += (i ? "." : "") + std::to_string(e.second[i]);
        o += ",";
    }
    return o;
}

static std::vector<int> DecodeArr9(const std::string& s) {
    std::vector<int> v(9, 0);
    auto f = Split(s, '.');
    for (size_t i = 0; i < f.size() && i < 9; i++) v[i] = atoi(f[i].c_str());
    return v;
}

static std::map<int, std::vector<int>> DecodeKArr(const std::string& s) {
    std::map<int, std::vector<int>> m;
    for (auto& e : Split(s, ',')) {
        auto f = Split(e, ':');
        if (f.size() != 2) continue;
        int n = atoi(f[0].c_str());
        if (n >= 1 && n <= 8) m[n] = DecodeArr9(f[1]);
    }
    return m;
}

static std::string EncodeKHist() {  // "K:opp.end.winner|opp.end.winner,..."
    std::string o;
    for (auto& e : g_kHist) {
        o += std::to_string(e.first) + ":";
        for (size_t i = 0; i < e.second.size(); i++)
            o += (i ? "|" : "") + std::to_string(e.second[i].opp) + "." + std::to_string(e.second[i].end) + "." +
                 std::to_string(e.second[i].winner);
        o += ",";
    }
    return o;
}

static void DecodeKHist(const std::string& s, int nowMonth) {
    g_kHist.clear();
    for (auto& e : Split(s, ',')) {
        auto f = Split(e, ':');
        if (f.size() != 2) continue;
        int n = atoi(f[0].c_str());
        if (n < 1 || n > 8) continue;
        for (auto& w : Split(f[1], '|')) {
            auto x = Split(w, '.');
            if (x.size() != 3) continue;
            KPastWar p{atoi(x[0].c_str()), atoi(x[1].c_str()), atoi(x[2].c_str())};
            if (p.opp >= 0 && p.opp <= 8 && p.opp != n && (nowMonth <= 0 || p.end <= nowMonth)) g_kHist[n].push_back(p);
        }
    }
}

static std::string EncodeTreaties() {
    std::string o;
    for (auto& t : g_treaties) {
        o += std::to_string(t.first.first) + std::string(1, kSepField) + std::to_string(t.first.second) + kSepField +
             std::to_string(t.second.winner) + kSepField + std::to_string(t.second.loser) + kSepField +
             std::to_string(t.second.level);
        for (auto& c : t.second.claims) o += kSepField + c;
        o += kSepRec;
    }
    return o;
}

static void DecodeTreaties(const std::string& s) {
    g_treaties.clear();
    for (auto& e : Split(s, kSepRec)) {
        auto f = Split(e, kSepField);
        if (f.size() < 5) continue;
        int a = atoi(f[0].c_str()), b = atoi(f[1].c_str());
        if (a < 0 || b <= a || b > 8) continue;
        TreatyTerms t;
        t.winner = atoi(f[2].c_str());
        t.loser = atoi(f[3].c_str());
        t.level = atoi(f[4].c_str());
        for (size_t i = 5; i < f.size(); i++)
            if (!f[i].empty()) t.claims.push_back(f[i]);
        g_treaties[{a, b}] = t;
    }
}

static void SaveReservations() {
    if (g_hostSlot <= 0) return;
    KV kv;
    for (auto& r : g_reserved) kv["nation" + std::to_string(r.first)] = r.second;
    kv["turnLimitSec"] = std::to_string(g_turnLimitSec);
    kv["campaign"] = CampaignKey();
    kv["pactAllies"] = EncodePairs(g_pactAllies);
    kv["pactWars"] = EncodePairs(g_pactWars);
    kv["warVP"] = EncodeWarVP();
    kv["enemyVPSeen"] = std::to_string(g_enemyVPSeen);
    kv["diplomacy"] = EncodeDipItems(g_dip);
    kv["treaties"] = EncodeTreaties();
    kv["kwar"] = EncodeKWar();
    kv["kdecl"] = EncodeKDecl();
    kv["kinv"] = inv::EncodeStates(&inv::State(0));
    kv["kecon"] = economy::EncodeState();  // the player's economy rules: each joined nation's own state
    kv["kwill"] = EncodeKArr(g_kWill);
    kv["kts"] = EncodeKArr(g_kTs);
    kv["khist"] = EncodeKHist();
    kv["krelm"] = EncodeMonthMap(g_kRelMonth);
    kv["kpeacem"] = EncodeMonthMap(g_kPeaceMonth);
    {
        std::string kl;
        for (int n : g_kLend) kl += std::to_string(n) + ",";
        kv["klend"] = kl;
        std::string kr;
        for (auto& e : g_kRevo) kr += std::to_string(e.first) + ":" + std::to_string(e.second) + ",";
        kv["krevo"] = kr;
        std::string lg;  // 0.7.0: the nations whose hand-built legacy fleet is in the campaign
        for (int n : g_legacyDone) lg += std::to_string(n) + ",";
        kv["klegacy"] = lg;
    }
    {
        std::string rr;
        for (int n : g_relNations) rr += std::to_string(n) + ",";
        kv["relRules"] = rr;
    }
    kv["diplomacyDate"] = HostDate();  // pending decisions belong to this month only
    WriteFileBytes(SessionFile(), KVEncode(kv));
}

static void LoadReservations() {
    ClearQueuedNews("the host took up another campaign");
    g_reserved.clear();
    inv::DecodeStates("", &inv::State(0));  // 0.7.0: dated like kwar (below)
    g_dip.clear();
    g_pactAllies.clear();
    g_pactWars.clear();
    g_warVP.clear();
    g_treaties.clear();
    g_kWar.clear();
    g_kDecl.clear();
    g_kWarSteppedSeq = -1;
    economy::ClearState();
    g_relNations.clear();
    g_kLend.clear();
    g_kRevo.clear();
    g_kWill.clear();
    g_kTs.clear();
    g_kHist.clear();
    g_kEnemies.clear();
    g_kRelMonth.clear();
    g_kPeaceMonth.clear();
    g_enemyVPSeen = -1;
    if (g_hostSlot > 0) nplay::HostAttach(game::SaveDir(g_hostSlot), CampaignKey());  // the joined nations' own records
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
    for (auto& e : Split(KVStr(kv, "relRules"), ',')) {
        int n = atoi(e.c_str());
        if (n >= 1 && n <= 8) g_relNations.insert(n);
    }
    for (auto& e : Split(KVStr(kv, "klend"), ',')) {  // B5 (d): any save of the campaign (the game never resets it)
        int n = atoi(e.c_str());
        if (n >= 1 && n <= 8) g_kLend.insert(n);
    }
    for (auto& e : Split(KVStr(kv, "klegacy"), ',')) {  // 0.7.0: any save of the campaign
        int n = atoi(e.c_str());
        if (n >= 1 && n <= 8) g_legacyDone.insert(n);
    }
    for (auto& e : Split(KVStr(kv, "krevo"), ',')) {  // B5: as the save's RevolutionPending (any save of the campaign)
        auto f = Split(e, ':');
        int n = f.size() == 2 ? atoi(f[0].c_str()) : 0, r = f.size() == 2 ? atoi(f[1].c_str()) : 0;
        if (n >= 1 && n <= 8 && r >= 1 && r <= 8 && r != n) g_kRevo[n] = r;
    }
    DecodeKHist(KVStr(kv, "khist"), HostMonth());  // past wars: any save of the campaign (later ones dropped)
    // War scores, pending decisions, agreed terms, the joined nations' war counters and economy state (ParliamentDelay,
    // army offensive months) and the months whose relation turn and peace were applied belong to the month they were
    // saved with: an older save of the campaign would count its battles and months again (the scores then restart from
    // the nations' points, see WarVP; the counters from the nations' wars, see KWar). A host that hosts again in the same
    // month applies no second relation turn or peace.
    if (KVStr(kv, "diplomacyDate") == HostDate()) {
        DecodeWarVP(KVStr(kv, "warVP"));
        g_enemyVPSeen = KVInt(kv, "enemyVPSeen", -1);
        g_dip = DecodeDipItems(KVStr(kv, "diplomacy"));
        DecodeTreaties(KVStr(kv, "treaties"));
        DecodeKWar(KVStr(kv, "kwar"));
        DecodeKDecl(KVStr(kv, "kdecl"));
        inv::DecodeStates(KVStr(kv, "kinv"), &inv::State(0));  // 0.7.0: the joined nations' invasion plans
        economy::DecodeState(KVStr(kv, "kecon"));
        g_kWill = DecodeKArr(KVStr(kv, "kwill"));
        g_kTs = DecodeKArr(KVStr(kv, "kts"));
        g_kRelMonth = DecodeMonthMap(KVStr(kv, "krelm"));
        g_kPeaceMonth = DecodeMonthMap(KVStr(kv, "kpeacem"));
    } else if (!KVStr(kv, "diplomacy").empty() || !KVStr(kv, "warVP").empty() || !KVStr(kv, "kwar").empty() ||
               !KVStr(kv, "kecon").empty()) {
        Log("pending diplomacy, war scores, war counters and economy state from %s dropped (campaign is at %s)",
            KVStr(kv, "diplomacyDate").c_str(), HostDate().c_str());
    }
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

// ---------------------------------------------------------------------------------------------
// The joined nations' war counters (relations v2, B1). The game counts nation 0's months at war or at peace in
// [General] War: WarStartsWithNation sets 1 when it was <= 0, DoStrategicTurn adds 1 at war and subtracts 1 at peace,
// DoPeace sets 0. The host does the same for each joined nation K: the month's step at the end of the month, then the
// wars the month started or ended. A nation the host has no counter for yet (an existing campaign, a new player)
// starts from its wars, with the host's counter as the best guess of how long (the guess the events always used).
static int& KWar(int K) {
    auto it = g_kWar.find(K);
    if (it == g_kWar.end()) {
        int host = game::WarCounter();
        bool war = game::AtWarWithAnyone(K);
        int start = war ? (host > 0 ? host : 1) : (host > 0 ? -12 : host);
        Log("war counter of nation %d starts at %d (%s; the host's nation's: %d)", K, start, war ? "at war" : "at peace",
            host);
        it = g_kWar.emplace(K, start).first;
    }
    return it->second;
}

static void HostArmsTreatyCheck(const char* why);

// A war of K started (1) or its last war ended (0) since the counter was stepped.
static void KWarFollow(int K, const char* why) {
    int& n = KWar(K);
    bool war = game::AtWarWithAnyone(K);
    int before = n;
    if (war && n <= 0) n = 1;
    if (!war && n > 0) n = 0;
    if (n != before) {
        Log("war counter of nation %d: %d -> %d (%s)", K, before, n, why);
        if (n == 0) economy::OnWarChanged(K);  // its army offensive ends at the peace, as the game's at DoPeace
    }
    HostArmsTreatyCheck(why);
}

// 0.6.0: the disarmament treaty and a joined nation's war. The game ends the campaign-wide treaty and the submarine ban
// when a war breaks out (CancelTreaty): nation 0's at the screen's refresh while its war counter is above 0 (the host's
// own wars stay the game's, its box included) and two computer nations' when it breaks out (CheckAITensionEvents). A
// joined nation's war is such a war: while the host is at peace and a joined nation's own war counter (B1) shows a war,
// the host ends both once, as the game would (its CancelTreaty without the box); the nation's player gets the game's
// line in the turn report (`econnews`) and every player a chat line. The joined player's PC never ends them itself
// (game.cpp: CancelTreaty's hook), so both copies agree.
static std::map<int, std::vector<std::wstring>> g_kTreatyNews;  // the turn report's line for each nation at war
static int g_treatyEnds = 0;
static std::string g_lastTreatyEnd;

// 0.7.0 (fix-afb): the turn messages the host keeps for each joined nation's next month (its invasions, war effects,
// economy, the treaty's end, the notices) belong to the campaign of this session: dropped when the session ends or the
// host takes up another campaign (else they showed in that nation's first month of the next campaign).
static void ClearQueuedNews(const char* why) {
    size_t n = 0;
    for (int K = 0; K < 9; K++) {
        n += g_invNews[K].size() + warfx::TakeNews(K).size() + economy::TakeNews(K).size();
        g_invNews[K].clear();
    }
    for (auto* m : {&g_playerNews, &g_treatyNews})
        for (auto& e : *m) n += e.second.size();
    for (auto& e : g_kTreatyNews) n += e.second.size();
    g_playerNews.clear();
    g_treatyNews.clear();
    g_kTreatyNews.clear();
    if (n) Log("turn messages kept for the joined nations dropped (%s): %u", why, (unsigned)n);
}

static void HostArmsTreatyCheck(const char* why) {
    if (g_role != Role::Host || game::WarCounter() > 0) return;
    game::ArmsTreaty t = game::ArmsTreatyNow();
    if (!t.ok || (t.months <= 0 && t.subBan <= 0)) return;
    std::vector<int> atWar;
    for (auto& kv : g_kWar)
        if (kv.second > 0 && hookcb::IsHumanRemoteNation(kv.first)) atWar.push_back(kv.first);
    if (atWar.empty() || !game::EndArmsTreatyForWar()) return;
    g_treatyEnds++;
    std::string ks, names, list;
    for (int K : atWar) {
        ks += " " + std::to_string(K);
        list += (list.empty() ? "" : ",") + std::to_string(K);
        names += (names.empty() ? "" : ", ") + CampaignNationName(K);
    }
    g_lastTreatyEnd = list + "/" + std::to_string(t.months) + "/" + std::to_string(t.subBan) + "/" + why;
    for (char& c : g_lastTreatyEnd)
        if (c == ' ') c = '_';
    Log("arms treaty: the war of nation(s)%s ends the disarmament treaty (%d month(s) left, %d t, %d in; submarine ban "
        "%d) (%s)", ks.c_str(), t.months, t.disp, t.cal, t.subBan, why);
    if (t.months <= 0) return;  // the submarine ban alone ends without a word, as in the game
    for (int K : atWar) g_kTreatyNews[K].push_back(L"The outbreak of war cancels the disarmament treaty!");
    AddChat("* The outbreak of war cancels the disarmament treaty! (" + names + " at war)");
}

// Relations v2, B3: the wars of each joined nation that ended since the last look go into its history
// (TensionEffectOfPreviousWars), with `nowMonth` (Year*12+Month) as the end.
static void KHistUpdate(int nowMonth) {
    for (int K = 1; K <= 8 && K < game::NationCount(); K++) {
        if (!hookcb::IsHumanRemoteNation(K)) continue;
        std::set<int> enemies;
        for (int j = 0; j <= 8 && j < game::NationCount(); j++)
            if (j != K && game::AtWar(K, j)) enemies.insert(j);
        auto pe = g_kEnemies.find(K);
        if (pe != g_kEnemies.end())
            for (int j : pe->second)
                if (!enemies.count(j)) {
                    g_kHist[K].push_back({j, nowMonth, 0});
                    Log("relations v2: the war of nation %d with nation %d ended; in its history", K, j);
                }
        g_kEnemies[K] = enemies;
    }
}

// Host, once per processed month (g_seq: the month in planning when the Turn started; a resent month doesn't step),
// after the month and the players' diplomacy, before the new month goes out.
static void KWarMonthStep() {
    if (g_kWarSteppedSeq == g_seq) return;
    g_kWarSteppedSeq = g_seq;
    for (int K = 1; K <= 8 && K < game::NationCount(); K++) {
        if (!hookcb::IsHumanRemoteNation(K)) {
            g_kWar.erase(K);  // a nation the computer plays has no counter of its own; a new player starts afresh
            g_kWill.erase(K);
            g_kTs.erase(K);
            g_kEnemies.erase(K);
            continue;
        }
        int& n = KWar(K);
        int before = n;
        n = n > 0 ? n + 1 : n - 1;
        KWarFollow(K, "the month");
        Log("war counter of nation %d: %d -> %d (%s, month %d)", K, before, n,
            game::AtWarWithAnyone(K) ? "at war" : "at peace", g_seq);
    }
}

// Relations v2, B2: a joined nation's relations follow the player's rules when its player's PC runs the player's
// relation routines (hello `rel`). While the player is away the nation keeps what its last player announced (kept per
// campaign); a nation given to the computer goes back to the computer's rules. Tests can switch it on (`relrules`).
static void NoteRelRules() {
    for (auto& kv : g_players) {
        const Player& p = kv.second;
        if (!p.connected || !p.welcomed || p.nation < 1 || p.nation > 8) continue;
        bool on = p.relLevel >= 1, was = g_relNations.count(p.nation) > 0;
        if (on == was) continue;
        if (on)
            g_relNations.insert(p.nation);
        else
            g_relNations.erase(p.nation);
        Log("relations v2: nation %d follows the %s rules (%s)", p.nation, on ? "player's" : "computer's", p.name.c_str());
    }
}

static std::string NationPlayerName(int n) {
    if (n == 0) return CleanName(g_myName) + " (host)";
    return ClaimedBy(n);
}

// Relations v2, B4: the war score of a joined nation under the player's rules (K) with a computer nation (J) is kept as
// a war between two players' (g_warVP): the player's peace decision on K's PC weighs it (K's VP and EnemyVP there). A new
// war starts at 0:0; the pair is erased at peace.
static bool LedgerPair(int a, int b) {
    auto one = [](int k, int j) {
        return k >= 1 && k <= 8 && j >= 1 && j <= 8 && j != k && hookcb::PlayerRulesNation(k) && !IsHumanNation(j);
    };
    return one(a, b) || one(b, a);
}

// Victory points of a war between two players. A war seen for the first time starts from the nations' own points
// when they fight nobody else (the game counts them per nation), otherwise from zero (a war of the B4 ledger: zero).
// `seeded` tells whether the start came from the nations' points.
static std::pair<int, int>& WarVP(int a, int b, bool* seeded = nullptr) {
    auto key = PairKey(a, b);
    if (seeded) *seeded = false;
    auto it = g_warVP.find(key);
    if (it != g_warVP.end()) return it->second;
    auto onlyEnemy = [](int n, int other) {
        for (int j = 0; j <= 8 && j < game::NationCount(); j++)
            if (j != n && j != other && game::AtWar(n, j)) return false;
        return true;
    };
    int lo = key.first, hi = key.second, vLo = 0, vHi = 0;
    if (!LedgerPair(lo, hi) && onlyEnemy(lo, hi) && onlyEnemy(hi, lo)) {
        if (seeded) *seeded = true;
        if (lo == 0) {
            vLo = std::max(0, game::NationVP(0));
            vHi = std::max(0, game::EnemyVP());
        } else {
            vLo = std::max(0, game::NationVP(lo));
            vHi = std::max(0, game::NationVP(hi));
        }
    }
    Log("war score %d-%d starts at %d:%d", lo, hi, vLo, vHi);
    return g_warVP[key] = {vLo, vHi};
}

// Relations v2, B4: nation K's war scores with the computer's nations it is at war with (`pairs`: "J:K's:J's,..."),
// summed: the player's VP and EnemyVP of K's peace decision.
static void KLedger(int K, int& vp, int& evp, std::string* pairs) {
    vp = evp = 0;
    for (int j = 1; j <= 8 && j < game::NationCount(); j++) {
        if (j == K || !LedgerPair(K, j) || !game::AtWar(K, j)) continue;
        auto& w = WarVP(K, j);
        int mine = K < j ? w.first : w.second, theirs = K < j ? w.second : w.first;
        vp += mine;
        evp += theirs;
        if (pairs) *pairs += std::to_string(j) + ":" + std::to_string(mine) + ":" + std::to_string(theirs) + ",";
    }
}

// What a peace between two players would bring now, with the thresholds of the game's own war-progress checks:
// "winning" = at least twice the enemy's points and 100*G + 500; "slightly ahead" = more points and 100*G + 300.
static DipSnap::War WarOutlook(int a, int b) {
    DipSnap::War w;
    auto key = PairKey(a, b);
    auto& vp = WarVP(a, b);
    w.vpLo = vp.first;
    w.vpHi = vp.second;
    int g = std::max(0, game::WarScale());
    int lead = std::max(w.vpLo, w.vpHi), trail = std::min(w.vpLo, w.vpHi);
    if (lead == trail) return w;
    w.winner = w.vpLo > w.vpHi ? key.first : key.second;
    if (lead >= 2 * trail && lead >= 100 * g + 500)
        w.level = 2;
    else if (lead >= 100 * g + 300)
        w.level = 1;
    if (!w.level) w.winner = -1;
    return w;
}

static int TreatyBudget(int level) { return level >= 2 ? 8 : level == 1 ? 4 : 0; }

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
            if (st == 'W' && game::TreatiesAvailable()) s.wars[PairKey(a, b)] = WarOutlook(a, b);
        }
    for (auto& t : g_treaties) {
        DipSnap::Treaty tr;
        tr.winner = t.second.winner;
        tr.loser = t.second.loser;
        tr.level = t.second.level;
        tr.budget = TreatyBudget(t.second.level);
        tr.claims = t.second.claims;
        if (tr.level > 0)
            for (auto& p : game::ClaimablePossessions(tr.winner, tr.loser))
                tr.claimable.push_back({SafeText(W2U(p.name)), SafeText(W2U(p.area)), p.value, p.oil});
        s.treaties[t.first] = tr;
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
    std::string wars;
    for (auto& w : s.wars)
        wars += std::to_string(w.first.first) + ":" + std::to_string(w.first.second) + ":" + std::to_string(w.second.vpLo) +
                ":" + std::to_string(w.second.vpHi) + ":" + std::to_string(w.second.winner) + ":" +
                std::to_string(w.second.level) + ";";
    kv["dipWars"] = wars;
    // Treaties: one record per agreed peace: lo, hi, winner, loser, level, budget, then claimable possessions as
    // name/area/value/oil quadruples, then "#" and the claims.
    std::string tr;
    for (auto& t : s.treaties) {
        auto f = [](const std::string& x) { return x + kSepField; };
        std::string r = f(std::to_string(t.first.first)) + f(std::to_string(t.first.second)) +
                        f(std::to_string(t.second.winner)) + f(std::to_string(t.second.loser)) +
                        f(std::to_string(t.second.level)) + f(std::to_string(t.second.budget));
        for (auto& c : t.second.claimable)
            r += f(c.name) + f(c.area) + f(std::to_string(c.value)) + f(c.oil ? "1" : "0");
        r += f("#");
        for (auto& c : t.second.claims) r += f(c);
        tr += r + kSepRec;
    }
    kv["dipTreaties"] = tr;
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
    for (auto& e : Split(KVStr(kv, "dipWars"), ';')) {
        auto f = Split(e, ':');
        if (f.size() < 6) continue;
        DipSnap::War w;
        w.vpLo = atoi(f[2].c_str());
        w.vpHi = atoi(f[3].c_str());
        w.winner = atoi(f[4].c_str());
        w.level = atoi(f[5].c_str());
        s.wars[PairKey(atoi(f[0].c_str()), atoi(f[1].c_str()))] = w;
    }
    for (auto& e : Split(KVStr(kv, "dipTreaties"), kSepRec)) {
        auto f = Split(e, kSepField);
        if (f.size() < 7) continue;
        DipSnap::Treaty t;
        t.winner = atoi(f[2].c_str());
        t.loser = atoi(f[3].c_str());
        t.level = atoi(f[4].c_str());
        t.budget = atoi(f[5].c_str());
        size_t i = 6;
        for (; i + 3 < f.size() && f[i] != "#"; i += 4)
            t.claimable.push_back({f[i], f[i + 1], atoi(f[i + 2].c_str()), f[i + 3] == "1"});
        while (i < f.size() && f[i] != "#") i++;
        for (i++; i < f.size(); i++)
            if (!f[i].empty()) t.claims.push_back(f[i]);
        s.treaties[PairKey(atoi(f[0].c_str()), atoi(f[1].c_str()))] = t;
    }
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
        auto key = PairKey(me, o);
        auto wi = s.wars.find(key);
        if (r.atWar && wi != s.wars.end()) {
            const auto& w = wi->second;
            r.vpMine = me == key.first ? w.vpLo : w.vpHi;
            r.vpTheirs = me == key.first ? w.vpHi : w.vpLo;
            const char* size = w.level >= 2 ? "major" : "minor";
            r.outlook = w.level <= 0 ? "a peace now would be a compromise"
                        : w.winner == me ? std::string("you are winning: a peace now would be a ") + size + " victory"
                                         : std::string("they are winning: a peace now would be a ") + size + " defeat";
        }
        auto ti = s.treaties.find(key);
        if (peace && ti != s.treaties.end()) {
            const auto& t = ti->second;
            std::string terms = t.level <= 0 ? "compromise, no terms"
                                : t.winner == me ? std::string("you win (") + (t.level >= 2 ? "major" : "minor") + ")"
                                                 : std::string("they win (") + (t.level >= 2 ? "major" : "minor") + ")";
            r.pending = "peace agreed: " + terms;
            if (t.level > 0 && t.winner == me) {
                int used = 0;
                for (auto& c : t.claims)
                    for (auto& o2 : t.claimable)
                        if (o2.name == c) used += o2.value;
                r.canClaim = true;
                r.claimBudget = t.budget;
                r.claimable = t.claimable;
                r.claims = t.claims;
                r.pending += t.claims.empty() ? ", claim land (worth up to " + std::to_string(t.budget) + ")"
                                              : ", land claimed: " + std::to_string(used) + " of " + std::to_string(t.budget);
            } else if (t.level > 0) {
                r.pending += t.claims.empty() ? ", they may claim your land" : ", they claimed " +
                                                                                   std::to_string(t.claims.size()) +
                                                                                   " possession(s)";
            }
            for (auto& x : p)
                if (x != "peace agreed") r.pending += ", " + x;
        }
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
    for (auto it = g_treaties.begin(); it != g_treaties.end();)
        it = (it->first.first == nation || it->first.second == nation) ? g_treaties.erase(it) : std::next(it);
    for (auto it = g_warVP.begin(); it != g_warVP.end();)
        it = (it->first.first == nation || it->first.second == nation) ? g_warVP.erase(it) : std::next(it);
}

// A player's diplomatic request; `from` is the requester's nation. Returns "" or the reason it is refused.
static std::string HostDiplo(int from, const std::string& action, int to, const std::string& extra = "") {
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
    if (action == "claim") {
        auto it = g_treaties.find(PairKey(from, to));
        if (agreedPeace < 0 || it == g_treaties.end()) return "There is no agreed peace with " + B + ".";
        TreatyTerms& t = it->second;
        if (t.level <= 0 || t.winner != from) return "Only the winner of the war can claim possessions.";
        auto claimable = game::ClaimablePossessions(t.winner, t.loser);
        std::vector<std::string> claims;
        int used = 0;
        for (auto& name : Split(extra, '|')) {
            if (name.empty() || std::find(claims.begin(), claims.end(), name) != claims.end()) continue;
            const game::PossessionInfo* p = nullptr;
            for (auto& c : claimable)
                if (SafeText(W2U(c.name)) == name) {
                    p = &c;
                    break;
                }
            if (!p) return name + " cannot be claimed from " + B + ".";
            used += p->value;
            claims.push_back(name);
        }
        if (used > TreatyBudget(t.level))
            return "Those possessions are worth " + std::to_string(used) + "; you may claim up to " +
                   std::to_string(TreatyBudget(t.level)) + ".";
        t.claims = claims;
        std::string list;
        for (auto& c : claims) list += (list.empty() ? "" : ", ") + c;
        AddChat("* " + A + (claims.empty() ? " leaves the choice of land to the peace conference."
                                           : " claims " + list + " from " + B + "."));
    } else if (action == "war") {
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
            // The terms are fixed now, so the winner can pick the possessions before the month ends.
            TreatyTerms t;
            if (game::TreatiesAvailable()) {
                DipSnap::War w = WarOutlook(from, to);
                if (w.level > 0) {
                    t.winner = w.winner;
                    t.loser = w.winner == from ? to : from;
                    t.level = w.level;
                }
            }
            g_treaties[PairKey(from, to)] = t;
            std::string terms = t.level <= 0 ? "a compromise peace without terms"
                                : CampaignNationName(t.winner) + " wins a " + (t.level >= 2 ? "major" : "minor") +
                                      " victory and may claim " + CampaignNationName(t.loser) + "'s land worth up to " +
                                      std::to_string(TreatyBudget(t.level));
            AddChat("* " + A + " accepts the peace offered by " + B + ": " + terms +
                    ". It takes effect when this month is processed.");
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
                if (d.kind == "peace") g_treaties.erase(PairKey(from, to));
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
static void ShareEnemyVP();
static void ResetPeacetimeVP();
static void ApplyDiplomacy() {
    if (!game::DiplomacyAvailable() || !CampaignReady()) return;
    ShareEnemyVP();  // with this month's enemies, before peace is made
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
                TreatyTerms t;
                auto ti = g_treaties.find(key);
                if (ti != g_treaties.end()) t = ti->second;
                bool terms = t.level > 0 && game::TreatiesAvailable() && game::AtWar(d.from, d.to) &&
                             IsHumanNation(t.winner) && IsHumanNation(t.loser);
                if (terms) {
                    std::vector<std::wstring> claims;
                    for (auto& c : t.claims) claims.push_back(U2W(c));
                    game::TreatyResult r = game::ApplyTreaty(t.winner, t.loser, t.level, claims, g_rng());
                    std::wstring head = U2W("Peace treaty between " + Nm(t.winner) + " and " + Nm(t.loser) + ": " +
                                            Nm(t.winner) + (t.level >= 2 ? " wins a major victory." : " wins a minor victory."));
                    r.report.insert(r.report.begin(), head);
                    if (r.ceded.empty())
                        r.report.push_back(U2W(Nm(t.loser) + " cedes no possessions" +
                                               (r.automatic ? " (none next to " + Nm(t.winner) + "'s possessions)." : ".")));
                    if (r.resourcesCut)
                        r.report.push_back(U2W(Nm(t.loser) + " pays reparations (base resources -" +
                                               std::to_string(r.resourcesCut) + ")."));
                    game::MakePeace(d.from, d.to, t.winner == 0 ? 1 : t.loser == 0 ? 2 : 3);
                    g_treaties.erase(key);
                    g_warVP.erase(key);
                    // Everyone sees the treaty in the chat; the two nations' players also get the game's treaty
                    // dialog once the new month is out (the host's own as well, so nobody waits for its OK).
                    std::vector<std::string> lines;
                    for (auto& l : r.report) lines.push_back(SafeText(W2U(l)));
                    for (int n : {t.winner, t.loser}) {
                        auto& news = n == 0 ? g_pendingTreaty : g_treatyNews[n];
                        if (!news.empty()) news.push_back(" ");  // a second treaty this month
                        news.insert(news.end(), lines.begin(), lines.end());
                    }
                    for (auto& l : lines) AddChat("* " + l);
                    msg = "Peace: " + Nm(d.from) + " and " + Nm(d.to) + " have ended their war (" + Nm(t.winner) +
                          " victorious).";
                } else {
                    game::MakePeace(d.from, d.to);
                    g_treaties.erase(key);
                    g_warVP.erase(key);
                    msg = "Peace: " + Nm(d.from) + " and " + Nm(d.to) + " have ended their war (compromise peace).";
                }
            } else if (d.kind == "war") {
                g_pactWars.insert(key);
                g_pactAllies.erase(key);
                g_warVP[key] = {0, 0};  // a new war is scored from its first battle
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
    // Scores and terms of wars that are over (ended by the game, or a nation went back to the AI); the B4 ledger's wars
    // of a nation under the player's rules with a computer nation stay while they last.
    for (auto it = g_warVP.begin(); it != g_warVP.end();)
        if (!(both(it->first.first, it->first.second) || LedgerPair(it->first.first, it->first.second)) ||
            !game::AtWar(it->first.first, it->first.second)) {
            it = g_warVP.erase(it);
            changed = true;
        } else {
            ++it;
        }
    for (auto it = g_treaties.begin(); it != g_treaties.end();)
        if (DipFindPair("peace", it->first.first, it->first.second) < 0) {
            it = g_treaties.erase(it);
            changed = true;
        } else {
            ++it;
        }
    ResetPeacetimeVP();
    g_enemyVPSeen = game::EnemyVP();  // peace and war made above may have reset the score; nothing new was scored
    SaveReservations();  // also keeps the war scores of the month's battles
    if (changed) game::RefreshUI();
}

// Host: victory points of the joined players' nations in the host's wars. The game credits everything the host's
// enemies achieve to EnemyVP (the "Enemy VP" of the host's screen) and nothing to the enemy nation itself, so a
// player fighting the host never saw its own VP rise. Battles are given to the nation that fought them as they
// happen (hookcb::OnBattleVP); the rest of the month's EnemyVP (blockades, sinkings, forces in an area, ...) is
// shared here among the host's enemies. AI nations keep the game's model and get nothing.
static void ShareEnemyVP() {
    int e = game::EnemyVP(), seen = g_enemyVPSeen;
    g_enemyVPSeen = e;
    if (seen < 0 || e <= seen) return;  // unknown start, or the game reset the score (the host's war ended or began)
    std::vector<int> enemies;
    for (int j = 1; j <= 8 && j < game::NationCount(); j++)
        if (game::AtWar(0, j)) enemies.push_back(j);
    if (enemies.empty()) return;
    int rest = e - seen, share = rest / (int)enemies.size(), extra = rest % (int)enemies.size();
    for (int j : enemies) {
        int give = share + (extra-- > 0 ? 1 : 0);
        if (give <= 0 || !hookcb::IsHumanRemoteNation(j)) continue;
        game::SetNationVP(j, game::NationVP(j) + give);
        Log("victory points: nation %d +%d of the host's enemies' %d this month -> %d", j, give, rest, game::NationVP(j));
    }
}

// Host: a joined player's nation at peace starts its next war from 0, like the host's nation (the game resets the
// player's VP when its last war ends, and only its own).
static void ResetPeacetimeVP() {
    for (int n = 1; n <= 8 && n < game::NationCount(); n++)
        if (hookcb::IsHumanRemoteNation(n) && game::NationVP(n) != 0 && !game::AtWarWithAnyone(n)) {
            Log("victory points: nation %d is at peace; %d -> 0", n, game::NationVP(n));
            game::SetNationVP(n, 0);
        }
}

// Host: results of a joined player's random event (answered on the player's PC). Relation changes go to the
// pairs of the player's nation; tension never crosses into war here (the game's own checks decide wars), and
// alliances with other players are left to the Diplomacy buttons. The nation's own changes are applied right
// away so they count even if the player doesn't submit; a submitted turn carries the same values.
static std::string g_lastHostEvent;

static void HostApplyEvent(Player& p, const KV& kv) {
    int K = KVInt(kv, "nation", -1);
    if (K != p.nation || K < 1 || K > 8 || KVInt(kv, "seq", -1) != g_seq || g_phase != Phase::Planning ||
        !game::DiplomacyAvailable()) {
        Log("event from %s ignored (nation %d, seq %d, current seq %d)", p.name.c_str(), K, KVInt(kv, "seq", -1), g_seq);
        return;
    }
    int y = 0, mo = 0;
    int month = saveio::ReadDate(game::SaveDir(g_hostSlot), g_hostSlot, y, mo) ? y * 12 + mo : 0;
    if (month > 0 && p.eventMonth == month) {
        Log("second event of %s in %s ignored (one per month)", p.name.c_str(), DateStr(y, mo).c_str());
        return;
    }
    p.eventMonth = month;
    std::string applied;
    for (auto& e : Split(KVStr(kv, "tension"), ';')) {
        auto f = Split(e, ':');
        if (f.size() != 2) continue;
        int j = atoi(f[0].c_str()), d = atoi(f[1].c_str());
        if (j < 0 || j > 8 || j == K || d == 0) continue;
        int cur = game::Tension(K, j);
        if (cur > 40) continue;  // already at war
        int now = std::max(0, std::min(40, cur + std::max(-20, std::min(20, d))));
        game::SetTensionRaw(K, j, now);
        applied += " tension " + std::to_string(K) + "-" + std::to_string(j) + " " + std::to_string(cur) + "->" +
                   std::to_string(now);
    }
    for (auto& e : Split(KVStr(kv, "alliance"), ';')) {
        auto f = Split(e, ':');
        if (f.size() != 2) continue;
        int j = atoi(f[0].c_str()), months = std::max(0, std::min(120, atoi(f[1].c_str())));
        if (j < 0 || j > 8 || j == K) continue;
        if (IsHumanNation(j)) {
            applied += " alliance with player nation " + std::to_string(j) + " skipped";
            continue;
        }
        if (game::AtWar(K, j)) continue;
        game::SetAlliance(K, j, months);
        applied += " alliance " + std::to_string(K) + "-" + std::to_string(j) + "=" + std::to_string(months);
    }
    static const std::set<std::string> kDelta = {"Prestige", "BudgetModifier", "BaseResources", "Funds",
                                                 "UnrestLevel", "TechLeakRisk", "Corruption"};
    static const std::set<std::string> kSet = {"BuildConstraint", "BuildConstraintTime", "BuildConstraintType"};
    for (auto& e : Split(KVStr(kv, "delta"), ';')) {
        auto f = Split(e, ':');
        if (f.size() == 2 && kDelta.count(f[0]) && game::AdjustNationField(K, f[0], atoi(f[1].c_str()), true))
            applied += " " + f[0] + (atoi(f[1].c_str()) > 0 ? "+" : "") + f[1];
    }
    for (auto& e : Split(KVStr(kv, "set"), ';')) {
        auto f = Split(e, ':');
        if (f.size() == 2 && kSet.count(f[0]) && game::AdjustNationField(K, f[0], atoi(f[1].c_str()), false))
            applied += " " + f[0] + "=" + f[1];
    }
    if (KVInt(kv, "intel")) {  // the player's monthly spying check (the game's other branch of its event slot)
        nplay::HostAttach(game::SaveDir(g_hostSlot), CampaignKey());
        nplay::HostTakeIntelEvent(kv, K, applied);
    }
    g_lastHostEvent = "nation=" + std::to_string(K) + " event=" + KVStr(kv, "event") + applied;
    Log("event of %s (nation %d, #%s '%s'):%s", p.name.c_str(), K, KVStr(kv, "event").c_str(),
        KVStr(kv, "caption").substr(0, 60).c_str(), applied.empty() ? " no effects" : applied.c_str());
    BroadcastLobby();
}

// Relations v2, B4 (host): the rest of a peace the player's PC made with the computer's nations (its relation changes
// are applied by HostApplyRelations, as the routines'): possessions and ships with the game's own routines (a possession
// must still have its owner, a ship must still be in its nation's list: else logged and skipped), the war state of the
// possessions of the war, the other nations' changes (BudgetModifier only of a nation left without a war: the game's
// peace demobilizes), blockades, the nation's VP (0 without a war, as the game's DoPeace does for the player), the
// war scores, chat lines for everyone. No DoPeace on the host: that is the host's own end of war.
static void HostApplyPeace(int K, const KV& kv, std::string& applied, std::vector<std::string>& chat) {
    auto Nm = [](int n) { return n < 0 ? std::string("independence") : CampaignNationName(n); };
    bool accepted = KVInt(kv, "paccepted") == 1;
    int winner = KVInt(kv, "pwinner");
    std::vector<int> enemies;
    for (auto& e : Split(KVStr(kv, "penemies"), ','))
        if (!e.empty() && atoi(e.c_str()) >= 1 && atoi(e.c_str()) <= 8 && atoi(e.c_str()) != K) enemies.push_back(atoi(e.c_str()));
    std::set<int> sep;  // B5: a revolution's separate peace (only these wars of K ended; its other enemies fight on)
    for (auto& e : Split(KVStr(kv, "pseparate"), ','))
        if (!e.empty() && std::find(enemies.begin(), enemies.end(), atoi(e.c_str())) != enemies.end()) sep.insert(atoi(e.c_str()));
    int lands = 0, ships = 0, states = 0, nations = 0, skipped = 0;
    auto skip = [&](const std::string& what, const std::string& why) {
        skipped++;
        Log("relations v2: peace of nation %d: %s skipped: %s", K, what.c_str(), why.c_str());
    };
    for (auto& rec : Split(KVStr(kv, "pland"), kSepRec)) {
        auto f = Split(rec, kSepField);
        if (f.size() < 4) continue;
        int from = atoi(f[1].c_str()), to = atoi(f[2].c_str());
        if (from < -1 || from > 8 || to < -1 || to > 8 || from == to) continue;
        std::string why = game::MovePossession(U2W(f[0]), from, to, f[3] == "1");
        if (!why.empty()) {
            skip("possession " + f[0], why);
            continue;
        }
        lands++;
        applied += " land " + f[0] + " " + std::to_string(from) + "->" + std::to_string(to);
    }
    for (auto& rec : Split(KVStr(kv, "pships"), kSepRec)) {
        auto f = Split(rec, kSepField);
        if (f.size() < 6) continue;
        int id = atoi(f[0].c_str()), from = atoi(f[1].c_str()), to = atoi(f[2].c_str());
        if (from < 0 || from > 8 || to < 0 || to > 8 || from == to) continue;
        std::string why = game::HandOverShip(id, from, to, U2W(f[3]), U2W(f[4]), U2W(f[5]));
        if (!why.empty()) {
            skip("ship " + f[0] + " (" + f[3] + ")", why);
            continue;
        }
        ships++;
        applied += " ship " + f[0] + " " + std::to_string(from) + "->" + std::to_string(to);
    }
    for (auto& rec : Split(KVStr(kv, "ptaken"), kSepRec)) {
        auto f = Split(rec, kSepField);
        if (f.size() < 6) continue;
        game::PeaceTaken t;
        t.name = U2W(f[0]);
        t.owner = atoi(f[1].c_str());
        t.takenFrom = atoi(f[2].c_str());
        t.invaded = atoi(f[3].c_str());
        t.support = atoi(f[4].c_str());
        t.rebellion = atoi(f[5].c_str());
        if (t.owner < -1 || t.owner > 8) continue;
        std::string why = game::SetPossessionWarState(t);
        if (!why.empty()) {
            skip("war state of " + f[0], why);
            continue;
        }
        states++;
    }
    if (accepted) {  // 0.7.0 (P5): DoPeace's resets of the nation's invasion plan (its possessions' came with `ptaken`)
        inv::KState& st = inv::State(K);
        if (!st.Empty()) {
            Log("invasions: nation %d's peace ends its invasion plan (target %s/%d, enemy target %s/%d, %d mark(s))", K,
                st.target.empty() ? "-" : W2U(st.target).c_str(), st.count,
                st.enemyTarget.empty() ? "-" : W2U(st.enemyTarget).c_str(), st.pending, (int)st.itt.size());
            st = inv::KState();
            SaveReservations();
            applied += " invasion-plan-reset";
        }
    }
    std::set<int> changed;
    for (auto& e : Split(KVStr(kv, "pdelta"), ';')) {
        auto f = Split(e, ':');
        if (f.size() != 3) continue;
        int n = atoi(f[0].c_str()), d = atoi(f[2].c_str());
        const std::string& field = f[1];
        if (n < 1 || n > 8 || n == K || IsHumanNation(n) || !d) continue;
        if (field == "BaseResources")
            d = std::max(-5000, std::min(5000, d));
        else if (field == "Prestige")
            d = std::max(sep.count(n) ? -1000 : -20, std::min(20, d));  // B5: a revolution's separate peace: Prestige 0
        else if (field == "BudgetModifier" && !game::AtWarWithAnyone(n))
            d = std::max(-10, std::min(10, d));
        else if (field == "UnrestLevel" &&  // B5: the enemy-revolution rule's and DoPeace's; not a third nation's still
                 (std::find(enemies.begin(), enemies.end(), n) != enemies.end() || !game::AtWarWithAnyone(n)))  // at war
            d = std::max(-20, std::min(10, d));
        else
            continue;
        if (!game::AdjustNationField(n, field, d, true)) continue;
        if ((field == "Prestige" || field == "UnrestLevel") && game::NationIntField(n, field) < 0)
            game::AdjustNationField(n, field, 0, false);
        changed.insert(n);
        applied += " " + std::to_string(n) + ":" + field + (d > 0 ? "+" : "") + std::to_string(d);
    }
    for (auto& e : Split(KVStr(kv, "plost"), ';')) {
        auto f = Split(e, ':');
        if (f.size() != 2) continue;
        int n = atoi(f[0].c_str());
        if (n < 1 || n > 8 || n == K || IsHumanNation(n)) continue;
        if (game::SetNationFlag(n, "HasLostWar", atoi(f[1].c_str()) != 0)) {
            changed.insert(n);
            applied += " " + std::to_string(n) + ":HasLostWar=" + f[1];
        }
    }
    nations = (int)changed.size();
    std::vector<int> ended;  // the enemies K is at peace with now
    for (int j : enemies)
        if (!game::AtWar(K, j)) ended.push_back(j);
    if (accepted && !ended.empty()) {
        bool kWar = game::AtWarWithAnyone(K);
        if (!kWar) {
            game::SetNationFlag(K, "Blockaded", false);
            if (game::NationVP(K) != 0) applied += " VP " + std::to_string(game::NationVP(K)) + "->0";
            game::SetNationVP(K, 0);
        }
        for (int j : ended) {
            if (!game::AtWarWithAnyone(j)) game::SetNationFlag(j, "Blockaded", false);
            g_warVP.erase(PairKey(K, j));
        }
    }
    std::string sepEnded;  // B5: a revolution's separate peace: the war score of that war ends, its blockade flag
    if (!accepted)
        for (int j : sep) {
            if (game::AtWar(K, j)) {
                skip("separate peace with " + std::to_string(j), "still at war on the host");
                continue;
            }
            if (!game::AtWarWithAnyone(j)) game::SetNationFlag(j, "Blockaded", false);
            g_warVP.erase(PairKey(K, j));
            sepEnded += (sepEnded.empty() ? "" : ", ") + Nm(j);
            applied += " separate " + std::to_string(K) + "-" + std::to_string(j);
        }
    if (!sepEnded.empty())
        chat.push_back("Revolution: " + sepEnded + " made a separate peace with " + Nm(K) + "; " + Nm(K) +
                       "'s other enemies fight on.");
    if (kv.count("prevo")) {  // B5: K's own RevolutionPending (the game's "revolutionary ideas" from that nation)
        int r = KVInt(kv, "prevo");
        if (r >= 1 && r <= 8 && r != K)
            g_kRevo[K] = r;
        else
            g_kRevo.erase(K);
    }
    std::string ens;
    for (int j : enemies) ens += (ens.empty() ? "" : ", ") + Nm(j);
    if (accepted && !ended.empty()) {
        std::string outcome = winner == 1   ? Nm(K) + " wins"
                              : winner == 2 ? ens + (enemies.size() > 1 ? " win" : " wins")
                                            : "a compromise peace";
        chat.push_back("Peace treaty between " + Nm(K) + " and " + ens + ": " + outcome + ".");
        for (auto& l : Split(KVStr(kv, "ptreaty"), kSepRec))
            if (!l.empty() && l != " ") chat.push_back(l);
    } else if (KVStr(kv, "pargs").size() > 0 && !accepted) {
        chat.push_back(Nm(K) + "'s peace negotiations with " + ens + " came to nothing.");
    }
    g_lastHostPeace = "nation=" + std::to_string(K) + " kind=" + KVStr(kv, "kind", "turn") + " code=" + KVStr(kv, "pcode") +
                      " event=" + KVStr(kv, "pevent") + " args=" + KVStr(kv, "pargs") + " accepted=" + (accepted ? "1" : "0") +
                      " winner=" + std::to_string(winner) + " enemies=" + KVStr(kv, "penemies") + " lands=" +
                      std::to_string(lands) + " ships=" + std::to_string(ships) + " states=" + std::to_string(states) +
                      " nations=" + std::to_string(nations) + " skipped=" + std::to_string(skipped) + " separate=" +
                      KVStr(kv, "pseparate") + " revo=" + KVStr(kv, "prevo", "-") + " blockade=" +
                      std::to_string(game::NationFlag(K, "Blockaded")) + " vp=" + std::to_string(game::NationVP(K));
    for (int j : enemies)
        g_lastHostPeace += " " + std::to_string(j) + ":" + (game::AtWar(K, j) ? "war" : "peace") + ",blockade=" +
                           std::to_string(game::NationFlag(j, "Blockaded"));
    Log("relations v2: peace of nation %d with %s (%s, code %s, NegotiatePeace(%s)): %s, winner %d; %d possession(s), %d "
        "ship(s), %d war state(s), %d nation(s) changed, %d skipped, %d treaty line(s)",
        K, ens.c_str(), KVStr(kv, "kind", "turn").c_str(), KVStr(kv, "pcode").c_str(), KVStr(kv, "pargs").c_str(),
        accepted ? "peace" : "no peace", winner, lands, ships, states, nations, skipped,
        (int)Split(KVStr(kv, "ptreaty"), kSepRec).size() - 1);
}

// Relations v2, B4: the end of a peace's wars in nation K's history, with the winner (KHistUpdate has dated them).
static void KHistPeace(int K, const KV& kv, int month) {
    if (KVInt(kv, "peace") != 1 || KVInt(kv, "paccepted") != 1) return;
    int w = KVInt(kv, "pwinner");
    for (auto& e : Split(KVStr(kv, "penemies"), ',')) {
        int j = atoi(e.c_str());
        if (e.empty() || j < 1 || j > 8 || j == K || game::AtWar(K, j)) continue;
        auto& h = g_kHist[K];
        auto it = std::find_if(h.begin(), h.end(), [&](const KPastWar& p) { return p.opp == j && p.end == month; });
        if (it == h.end()) {
            h.push_back({j, month, w});
            g_kEnemies[K].erase(j);
        } else {
            it->winner = w;
        }
    }
}

// Relations v2, B3 (host): what the player's relation routines changed for the player's nation K at the start of its
// month (on its PC, in K's view), applied before the month runs, as events are. K's relations with the computer's
// nations take the routines' changes (a war one of them declared on K starts here); between players only the tension
// moves (wars and alliances between players come from their diplomacy); a war a computer nation joined on K's side
// against the host is the host's war. K's per-relation state is kept for its next month; the other nations' budget
// changes (a war's) are applied; K's own fields come with K's turn, its VP (kept by the host) here. B4: the turn's peace
// with the computer's nations (or a proposal's, `kind=peace`: one a month, a test's any time) comes with it: K's wars
// with them and their wars with the computer's nations end at the tension the game's peace left (HostApplyPeace: the
// rest).
// Relations v2: a joined nation's relation turn and peace change the host's campaign in the middle of the month (its
// planning phase). They are saved at once, as a join saves (the month's own save would come only when the month runs),
// so a host whose game stops in the month and is started again finds them in its campaign; the month's guard (krelm,
// kpeacem) is in the session file before the save. While a dialog of the host's game is open the save waits for it
// (Tick); the month's own save (RunHostTurn) covers a save still waiting.
static bool g_relSavePending = false;
static int g_relSaves = 0;
static void SaveAfterRelations(const char* what, int K) {
    if (g_role != Role::Host) return;
    std::string of = K > 0 ? " of nation " + std::to_string(K) : "";
    if (g_phase != Phase::Planning || g_turnRunning || game::IsBusy() || game::BattleActive() || game::EventActive()) {
        if (!g_relSavePending) {
            Log("relations v2: the campaign is saved after %s%s once the host's game is free", what, of.c_str());
            g_lastHostRelations += " saved=pending";
        }
        g_relSavePending = true;
        return;
    }
    g_relSavePending = false;
    bool ok = game::SaveGame();
    if (ok) g_relSaves++;
    Log("relations v2: campaign saved after %s%s (%s; %d save(s) of this kind)", what, of.c_str(), ok ? "ok" : "FAILED",
        g_relSaves);
    g_lastHostRelations += ok ? " saved=1" : " saved=failed";
}

// Relations v2, B5 (d): the allies' war help of K's turn (its PC ran the game's WarHelpFromAllies in K's view). K's VP
// came with the turn's `vp`; here: the war score of K's wars with the computer's nations (`allyvp`, split evenly: the
// game adds it to the player's VP, not to one war), the other nations' Funds (an ally's 2000 for K: K's own funds come
// with its turn), the lend-lease ships (from the ally to K, by ship id as a peace's) and K's LendLeaseDone.
static void HostApplyAllyHelp(int K, const KV& kv, std::string& applied) {
    int avp = KVInt(kv, "allyvp", 0);
    if (avp > 0) {
        std::vector<int> en;
        for (int j = 1; j <= 8 && j < game::NationCount(); j++)
            if (j != K && LedgerPair(K, j) && game::AtWar(K, j)) en.push_back(j);
        int share = en.empty() ? 0 : avp / (int)en.size(), extra = en.empty() ? 0 : avp % (int)en.size();
        for (int j : en) {
            auto& w = WarVP(K, j);
            int v = share + (extra-- > 0 ? 1 : 0);
            (K < j ? w.first : w.second) += v;
            applied += " allyvp " + std::to_string(K) + "-" + std::to_string(j) + "+" + std::to_string(v);
        }
    }
    for (auto& e : Split(KVStr(kv, "afunds"), ';')) {
        auto f = Split(e, ':');
        if (f.size() != 2) continue;
        int n = atoi(f[0].c_str()), d = std::max(-5000, std::min(5000, atoi(f[1].c_str())));
        if (n < 1 || n > 8 || n == K || IsHumanNation(n) || !d) continue;
        if (game::AdjustNationField(n, "Funds", d, true))
            applied += " funds " + std::to_string(n) + (d > 0 ? "+" : "") + std::to_string(d);
    }
    for (auto& rec : Split(KVStr(kv, "aships"), kSepRec)) {
        auto f = Split(rec, kSepField);
        if (f.size() < 6) continue;
        int id = atoi(f[0].c_str()), from = atoi(f[1].c_str()), to = atoi(f[2].c_str());
        if (from < 1 || from > 8 || to != K || IsHumanNation(from)) continue;
        std::string why = game::HandOverShip(id, from, to, U2W(f[3]), U2W(f[4]), U2W(f[5]));
        if (!why.empty()) {
            Log("relations v2: war help of nation %d: ship %s (%s) skipped: %s", K, f[0].c_str(), f[3].c_str(),
                why.c_str());
            continue;
        }
        applied += " lend-lease ship " + f[0] + " " + std::to_string(from) + "->" + std::to_string(to);
    }
    if (KVInt(kv, "lend") == 1 && !g_kLend.count(K)) {
        g_kLend.insert(K);
        applied += " LendLeaseDone";
    }
}

// B5 (e): the ships K's PC saw seized in its building for war (CheckBuildingShipsForWar in K's view), by ship id: K's ship
// in a computer nation's yard taken by that nation, or a computer nation's ship in K's yard taken by K.
static void HostApplyBuildWar(int K, const KV& kv, std::string& applied) {
    for (auto& rec : Split(KVStr(kv, "bships"), kSepRec)) {
        auto f = Split(rec, kSepField);
        if (f.size() < 6) continue;
        int id = atoi(f[0].c_str()), from = atoi(f[1].c_str()), to = atoi(f[2].c_str());
        int other = from == K ? to : to == K ? from : -1;
        if (other < 1 || other > 8 || other == K || IsHumanNation(other)) continue;
        std::string why = game::HandOverShip(id, from, to, U2W(f[3]), U2W(f[4]), U2W(f[5]));
        if (!why.empty()) {
            Log("relations v2: building for war of nation %d: ship %s (%s) skipped: %s", K, f[0].c_str(), f[3].c_str(),
                why.c_str());
            continue;
        }
        applied += " seized ship " + f[0] + " " + std::to_string(from) + "->" + std::to_string(to);
    }
}

static bool B6On(int K);

// 0.6.0 (B6 piece 2, host): a relation turn's surprise moves at a war's start (keys of a gen 2 client, K's PC in K's
// view): the unowned possessions computer nations took over (`sland`, applied by name with the game's
// ChangePossessionOwner if still unowned) and the surprise attacks (`ksa`: CheckSurpriseAttack's pairs of K with
// computer nations, in order) as one battle request of kind 2 for the battle phase after the month (K's battle slot
// plays them with the game's CheckSurpriseAttack). Not in the years without a tactical surprise battle (before 1896,
// 1925-1934: the game's submarine attack DoSubSurprise has no battle; its effects would stay in the battle slot).
static void HostApplySurprise(Player& p, int K, const KV& kv, std::string& applied, std::vector<std::string>& chat) {
    auto Nm = [](int n) { return CampaignNationName(n); };
    for (auto& rec : Split(KVStr(kv, "sland"), kSepRec)) {
        auto f = Split(rec, kSepField);
        if (f.size() < 4 || f[0].empty()) continue;
        int from = atoi(f[1].c_str()), to = atoi(f[2].c_str());
        std::string why = from != -1                                      ? "not an unowned possession"
                          : to < 1 || to > 8 || to == K || IsHumanNation(to) ? "not by a computer nation"
                                                                           : game::MovePossession(U2W(f[0]), -1, to, false);
        if (!why.empty()) {
            Log("relations v2: surprise move of nation %d's turn: takeover of %s by %d skipped: %s", K, f[0].c_str(), to,
                why.c_str());
            continue;
        }
        applied += " takeover " + f[0] + " -1->" + std::to_string(to);
        chat.push_back("In a surprise move, " + Nm(to) + " has taken control of " + f[0] + "!");
    }
    // 0.7.0 (P4): the surprise landings in the nation's possessions (CheckForWar's, run on its PC): the computer nation
    // invades the possession, marked this month (`kitt`) as the game marks it, if the nation still owns it uninvaded
    for (auto& rec : Split(KVStr(kv, "kland"), kSepRec)) {
        auto f = Split(rec, kSepField);
        if (f.size() < 2 || f[0].empty()) continue;
        std::wstring name = U2W(f[0]);
        int by = atoi(f[1].c_str());
        std::string why = !inv::Available() || p.genLevel < 4         ? "invasions are not on for the nation"
                          : by < 1 || by > 8 || by == K || IsHumanNation(by) ? "not by a computer nation"
                          : inv::PossessionOwner(name) != K           ? "not the nation's possession"
                          : inv::PossessionInvader(name) != -1        ? "invaded already"
                                                                      : inv::SetPossession(name, by, 0, false);
        if (!why.empty()) {
            Log("invasions: surprise landing of nation %d's turn in %s by %d skipped: %s", K, f[0].c_str(), by, why.c_str());
            continue;
        }
        inv::KState& st = inv::State(K);
        if (std::find(st.itt.begin(), st.itt.end(), name) == st.itt.end()) st.itt.push_back(name);
        SaveReservations();
        applied += " landing " + f[0] + " by " + std::to_string(by);
        Log("invasions: surprise landing of nation %d's turn: nation %d invades %s (marked this month)", K, by, f[0].c_str());
        chat.push_back(Nm(by) + " troops have made a surprise landing in " + Nm(K) + "'s possession " + f[0] + "!");
    }
    std::vector<game::BattleAttempt> at;
    for (auto& e : Split(KVStr(kv, "ksa"), ';')) {
        auto f = Split(e, ',');
        if (f.size() != 2 || f[0].empty() || f[1].empty()) continue;
        game::BattleAttempt t;
        t.a = atoi(f[0].c_str());
        t.b = atoi(f[1].c_str());
        int o = t.a == K ? t.b : t.b == K ? t.a : -1;
        if (o < 1 || o > 8 || o >= game::NationCount() || IsHumanNation(o)) {
            Log("relations v2: surprise attack %d > %d of nation %d's turn dropped (not with a computer nation)", t.a, t.b,
                K);
            continue;
        }
        if (at.size() < 16) at.push_back(t);
    }
    if (at.empty()) return;
    std::string pairs;
    for (auto& t : at) pairs += " " + std::to_string(t.a) + ">" + std::to_string(t.b);
    applied += " surprise" + pairs;
    int month = HostMonth(), year = month > 0 ? (month - 1) / 12 : 0;
    if (!B6On(K) || year < 1896 || (year >= 1925 && year <= 1934)) {
        Log("relations v2: surprise attack(s)%s of nation %d's turn not played: %s", pairs.c_str(), K,
            !B6On(K) ? "the player's battles are not on for this nation"
            : year < 1896 ? "no surprise battle before 1896"
                          : "1925-1934: the game's surprise attack is a submarine attack without a battle (DoSubSurprise)");
        return;
    }
    BattleReq r;
    r.id = g_battleNextId++;
    r.kind = 2;
    r.attempts = at;
    r.a = at[0].a;
    r.b = at[0].b;
    r.human = K;
    r.opp = r.a == K ? r.b : r.a;
    r.player = p.name;
    g_battles.push_back(r);
    Log("battle request %d (surprise attack): nation %d vs %d, %d attempt(s):%s, played by %s", r.id, K, r.opp,
        (int)at.size(), pairs.c_str(), p.name.c_str());
    chat.push_back("Surprise attack? " + Nm(r.a) + " may strike " + Nm(r.b) + " at the war's start. " + p.name +
                   " plays it after the month.");
}

static void HostApplyRelations(Player& p, const KV& kv) {
    int K = KVInt(kv, "nation", -1);
    if (K != p.nation || K < 1 || K > 8 || KVInt(kv, "seq", -1) != g_seq || g_phase != Phase::Planning ||
        !game::DiplomacyAvailable() || !hookcb::PlayerRulesNation(K)) {
        Log("relations from %s ignored (nation %d, seq %d, current seq %d)", p.name.c_str(), K, KVInt(kv, "seq", -1),
            g_seq);
        return;
    }
    int month = HostMonth();
    bool proposal = KVStr(kv, "kind") == "peace", test = KVInt(kv, "test") == 1;
    if (proposal) {
        if (month > 0 && !test && g_kPeaceMonth.count(K) && g_kPeaceMonth[K] == month) {
            Log("second peace of %s with the computer's nations this month ignored", p.name.c_str());
            return;
        }
        if (!test) g_kPeaceMonth[K] = month;
    } else {
        if (month > 0 && g_kRelMonth[K] == month) {  // once per nation and month (reconnects, hosting again)
            Log("second relation turn of %s this month ignored", p.name.c_str());
            return;
        }
        g_kRelMonth[K] = month;
        if (KVInt(kv, "pevent") == 1) g_kPeaceMonth[K] = month;  // a peace event was answered: no proposal this month
    }
    bool peaceRan = KVInt(kv, "peace") == 1, accepted = peaceRan && KVInt(kv, "paccepted") == 1;
    std::set<int> exEnemies;
    for (auto& e : Split(KVStr(kv, "penemies"), ','))
        if (!e.empty()) exEnemies.insert(atoi(e.c_str()));
    std::set<int> sepEnemies;  // B5: a revolution's separate peace (the enemy's own wars end too, as in the game)
    for (auto& e : Split(KVStr(kv, "pseparate"), ','))
        if (!e.empty() && peaceRan && exEnemies.count(atoi(e.c_str()))) sepEnemies.insert(atoi(e.c_str()));
    auto Nm = [](int n) { return CampaignNationName(n); };
    std::string applied;
    std::vector<std::string> wars;
    for (auto& e : Split(KVStr(kv, "tension"), ';')) {
        auto f = Split(e, ':');
        if (f.size() != 4) continue;
        int a = atoi(f[0].c_str()), b = atoi(f[1].c_str()), t0 = atoi(f[2].c_str()), t1 = atoi(f[3].c_str());
        if (a < 0 || b <= a || b > 8 || b >= game::NationCount()) continue;
        bool war = t0 <= 40 && t1 > 40, peace = t0 > 40 && t1 <= 40;
        bool players = IsHumanNation(a) && IsHumanNation(b);
        std::string pr = std::to_string(a) + "-" + std::to_string(b);
        if (a != K && b != K) {  // another pair: only a war (a computer nation joining K's war on K's side)
            // B4: a war of one of K's enemies that K's peace ended (the game's DoPeace ends every war of the player's
            // enemies); between the computer's nations only (the people's wars are their own)
            if (peace && !IsHumanNation(a) && !IsHumanNation(b) && game::AtWar(a, b) &&
                ((accepted && (exEnemies.count(a) || exEnemies.count(b))) || sepEnemies.count(a) || sepEnemies.count(b))) {
                game::MakePeace(a, b);
                game::SetTensionRaw(a, b, std::max(0, std::min(40, t1)));
                applied += " peace " + pr;
                wars.push_back("Peace: " + Nm(a) + " and " + Nm(b) + " have made peace (" + Nm(K) + "'s peace).");
                continue;
            }
            if (!war || players || game::AtWar(a, b)) {
                if (t1 != t0) applied += " " + pr + " " + std::to_string(t0) + "->" + std::to_string(t1) + " skipped";
                continue;
            }
            game::StartWar(a, b);
            applied += " war " + pr;
            int other = a == 0 ? b : b == 0 ? a : -1;
            std::string line = "War: " + Nm(a) + " and " + Nm(b) + " are now at war (" + Nm(K) + "'s war).";
            wars.push_back(line);
            if (other >= 0) Notice(Nm(other) + " has entered the war against you on " + Nm(K) + "'s side!");
            continue;
        }
        int o = a == K ? b : a;
        int cur = game::Tension(K, o);
        if (war) {
            if (players || cur > 40) {
                applied += " war " + pr + " skipped (" + (players ? "players" : "already") + ")";
                continue;
            }
            game::StartWar(K, o);
            applied += " war " + pr;
            wars.push_back("War: " + Nm(o) + " and " + Nm(K) + " are now at war.");
        } else if (peace) {
            if (players || cur <= 40) {
                applied += " peace " + pr + " skipped";
                continue;
            }
            game::MakePeace(K, o);
            game::SetTensionRaw(K, o, std::max(0, std::min(40, t1)));  // the game's tension after its peace (DoPeace)
            applied += " peace " + pr;
            if (!accepted) wars.push_back("Peace: " + Nm(o) + " and " + Nm(K) + " have made peace.");
        } else if (proposal && players) {  // a peace leaves the people's nations alone (their cells were put back)
            if (t1 != t0) applied += " " + pr + " " + std::to_string(t0) + "->" + std::to_string(t1) + " skipped";
        } else if (cur <= 40 && t1 <= 40) {
            int now = std::max(0, std::min(40, cur + (t1 - t0)));
            if (now != cur) game::SetTensionRaw(K, o, now);
            applied += " " + pr + " " + std::to_string(cur) + "->" + std::to_string(now);
        }
    }
    for (auto& e : Split(KVStr(kv, "alliance"), ';')) {
        auto f = Split(e, ':');
        if (f.size() != 4) continue;
        int a = atoi(f[0].c_str()), b = atoi(f[1].c_str()), m1 = std::max(0, std::min(120, atoi(f[3].c_str())));
        if (a < 0 || b <= a || b > 8 || b >= game::NationCount()) continue;
        std::string pr = std::to_string(a) + "-" + std::to_string(b);
        if ((IsHumanNation(a) && IsHumanNation(b)) || game::AtWar(a, b)) {
            applied += " alliance " + pr + " skipped";
            continue;
        }
        game::SetAlliance(a, b, m1);
        applied += " alliance " + pr + "=" + std::to_string(m1);
    }
    std::vector<int> will = DecodeArr9(KVStr(kv, "will")), ts = DecodeArr9(KVStr(kv, "ts"));
    will[K] = ts[K] = 0;
    if (!proposal || accepted) g_kWill[K] = will;  // a proposal's view started from this PC's state: only DoPeace's reset
    if (!proposal) g_kTs[K] = ts;
    for (auto& e : Split(KVStr(kv, "budget"), ';')) {
        auto f = Split(e, ':');
        if (f.size() != 2) continue;
        int j = atoi(f[0].c_str()), d = std::max(-5, std::min(5, atoi(f[1].c_str())));
        if (j < 1 || j > 8 || j == K || IsHumanNation(j) || !d) continue;
        if (game::AdjustNationField(j, "BudgetModifier", d, true))
            applied += " budget " + std::to_string(j) + (d > 0 ? "+" : "") + std::to_string(d);
    }
    int dvp = KVInt(kv, "vp", 0);
    if (dvp) {
        game::SetNationVP(K, game::NationVP(K) + dvp);
        applied += " VP " + std::to_string(dvp);
    }
    if (KVInt(kv, "ally") == 1) HostApplyAllyHelp(K, kv, applied);
    if (KVInt(kv, "bwar") == 1) HostApplyBuildWar(K, kv, applied);
    std::vector<std::string> peaceChat;
    if (peaceRan) HostApplyPeace(K, kv, applied, peaceChat);
    std::vector<std::string> surpriseChat;  // 0.6.0 (B6 piece 2): a gen 2 client's surprise moves at a war's start
    if (!proposal && p.genLevel >= 2) HostApplySurprise(p, K, kv, applied, surpriseChat);
    // The nation's own fields the routines changed on its PC (they reach the host with its turn too, which then replaces
    // them with the same values): taken here as well, so the campaign saved below holds them.
    for (auto& e : Split(KVStr(kv, "self"), ';')) {
        auto f = Split(e, ':');
        if (f.size() != 2) continue;
        const std::string& field = f[0];
        int d = atoi(f[1].c_str()), lim = field == "BudgetModifier" || field == "UnrestLevel" ? 10
                                          : field == "Prestige"                              ? 50
                                          : field == "BaseResources"                         ? 20000
                                                                                             : 0;
        if (!lim || !d) continue;
        d = std::max(-lim, std::min(lim, d));
        if (!game::AdjustNationField(K, field, d, true)) continue;
        if (game::NationIntField(K, field) < 0) game::AdjustNationField(K, field, 0, false);
        applied += " own " + field + (d > 0 ? "+" : "") + std::to_string(d);
    }
    KWarFollow(K, peaceRan ? "the player's peace" : "the player's routines");
    KHistUpdate(month);
    KHistPeace(K, kv, month);
    g_lastHostRelations = "nation=" + std::to_string(K) + (proposal ? " kind=peace" : "") + applied;
    Log("relations of %s (nation %d; %s; %s message(s)):%s", p.name.c_str(), K,
        proposal ? "peace proposal" : KVStr(kv, "ran").c_str(), KVStr(kv, "msgs", "0").c_str(),
        applied.empty() ? " no changes" : applied.c_str());
    for (auto& w : peaceChat) AddChat("* " + w);
    for (auto& w : wars) AddChat("* " + w);
    for (auto& w : surpriseChat) AddChat("* " + w);
    if (!applied.empty() && !game::EventActive()) game::RefreshUI();  // the host's screen: its relations, lands, ships
    SaveReservations();  // the month's guard first: a crash before the save below loses the changes, never doubles them
    SaveAfterRelations(proposal ? "a peace proposal" : "a relation turn", K);
    BroadcastLobby();
}

static void SendStateTo(Player& p, const std::wstring& dir, int y, int m) {
    Bundle b;
    std::string err;
    if (!saveio::BuildStateBundle(dir, g_hostSlot, kIdStride * p.nation, b, err)) {
        Notice("Cannot package the game for " + p.name + ": " + err);
        return;
    }
    // The player's own intelligence (spending, effort, reports) instead of the host's (nationplay.cpp).
    nplay::HostAttach(dir, CampaignKey());
    nplay::HostTransformState(b, p.nation);
    // 0.6.1: the nation's own campaign divisions (the Fleet organization tab) instead of the host's: the list its last
    // turn sent, pruned to its ships in this month. A player whose mod doesn't load them (hello `div` 0) gets an empty
    // list: its game would look the nation's ships up among the host's nation (nil ships) and show the host's divisions.
    nplay::HostPutDivisions(b, p.nation, p.divLevel >= 1, "month");
    KV h;
    h["seq"] = std::to_string(g_seq);
    h["nation"] = std::to_string(p.nation);
    h["nationName"] = W2U(game::NationName(p.nation));
    h["year"] = std::to_string(y);
    h["month"] = std::to_string(m);
    h["remaining"] = std::to_string(RemainingSec());
    h["kwar"] = std::to_string(KWar(p.nation));  // the nation's own war counter (0.4.1 clients ignore it)
    // 0.6.0: this host ends the disarmament treaty for a joined nation's war (HostArmsTreatyCheck), so the player's PC
    // may end it at once when its nation's war starts there (older clients ignore the key)
    if (game::ArmsTreatyAvailable()) h["armstreaty"] = "1";
    // 0.7.0: a new campaign's first month (nothing processed yet): the player may build the nation's legacy fleet by
    // hand, once (older clients ignore the key; the player's choice decides, see ClientApplyPendingState)
    if (g_legacyAllowed && p.nation >= 1 && p.nation <= 8 && y == game::CampaignStartYear() && m == 1 &&
        !g_legacyDone.count(p.nation))
        h["legacy"] = "1";
    if (hookcb::PlayerRulesNation(p.nation) && g_kRelMonth[p.nation] != HostMonth()) {
        // Relations v2, B3 (not again in a month whose turn was applied, e.g. after a reconnect): the player's relation routines run on the player's PC this month, with the nation's own
        // per-relation state and past wars and the nations people play (the host's and the joined ones).
        int K = p.nation, now = HostMonth();
        std::vector<int> none(9, 0);
        auto arr = [&](const std::map<int, std::vector<int>>& m) {
            auto it = m.find(K);
            const std::vector<int>& v = it == m.end() ? none : it->second;
            std::string o;
            for (size_t i = 0; i < 9 && i < v.size(); i++) o += (i ? "." : "") + std::to_string(v[i]);
            return o;
        };
        std::string hist, humans;
        auto hi = g_kHist.find(K);
        if (hi != g_kHist.end())
            for (auto& w : hi->second)
                hist += std::to_string(w.opp) + "." + std::to_string(std::max(0, now - w.end)) + "." +
                        std::to_string(w.winner) + "|";
        for (int j = 0; j <= 8 && j < game::NationCount(); j++)
            if (IsHumanNation(j)) humans += std::to_string(j) + ",";
        h["rel"] = "1";
        h["kwill"] = arr(g_kWill);
        h["kts"] = arr(g_kTs);
        h["khist"] = hist;
        h["humans"] = humans;
        // B5 (d): the host applies the allies' war help of the turn (older clients ignore the keys), with K's own
        // LendLeaseDone; B5 (e): and the ship seizures of K's wars.
        h["khelp"] = "1";
        h["kbuild"] = "1";
        h["klend"] = g_kLend.count(p.nation) ? "1" : "0";
    }
    if (hookcb::PlayerRulesNation(p.nation) && game::PeaceAvailable()) {
        // Relations v2, B4 (0.4.x and B3 clients ignore them; also in a month whose turn was applied: proposals): K's war
        // score with its computer enemies (the player's VP and EnemyVP of its peace decision), per enemy, whether its
        // peace was settled this month already, and the nations people play (they stay out of K's peace).
        int K = p.nation, now = HostMonth(), kvp = 0, kevp = 0;
        std::string pairs, humans;
        KLedger(K, kvp, kevp, &pairs);
        for (int j = 0; j <= 8 && j < game::NationCount(); j++)
            if (IsHumanNation(j)) humans += std::to_string(j) + ",";
        h["kvp"] = std::to_string(kvp);
        h["kevp"] = std::to_string(kevp);
        h["kpairs"] = pairs;
        h["kpeace"] = g_kPeaceMonth.count(K) && g_kPeaceMonth[K] == now ? "1" : "0";
        // B5: the enemy-revolution rule runs on K's PC (older clients ignore it); this host applies its separate peace
        // and the enemies' unrest changes, and keeps K's RevolutionPending
        h["krevo"] = std::to_string(g_kRevo.count(K) ? g_kRevo[K] : 0);
        h["humans"] = humans;
    }
    h["intel"] = "1";  // per-player intelligence: the player's turn carries its own (0.4.0 peers ignore the key)
    if (p.divLevel >= 1) h["divs"] = "1";  // 0.6.1: the .off holds the nation's own divisions (see above)
    {
        // The player's economy rules the host ran for this nation (economy.cpp; older clients ignore the keys): their
        // messages since the last state (sent once) and the nation's army offensive months (its budget's x 0.9).
        std::vector<std::wstring> econ = economy::TakeNews(p.nation);
        std::string news;
        for (auto& l : econ) news += SafeText(W2U(l)) + kSepRec;
        // The player's war effects the host ran for this nation (warfx.cpp, B5): their messages go the same way (0.5.0
        // clients show them too).
        std::vector<std::wstring> war = warfx::TakeNews(p.nation);
    for (auto& l : g_invNews[p.nation]) war.push_back(l);  // 0.7.0: the nation's invasions (CheckRebellionAndInvasions)
    g_invNews[p.nation].clear();
        for (auto& l : war) news += SafeText(W2U(l)) + kSepRec;
        // 0.6.0: the disarmament treaty ended by this nation's war (HostArmsTreatyCheck), the game's line, once
        auto tn = g_kTreatyNews.find(p.nation);
        if (tn != g_kTreatyNews.end()) {
            for (auto& l : tn->second) news += SafeText(W2U(l)) + kSepRec;
            Log("arms treaty: the turn report line of nation %d goes with state seq %d", p.nation, g_seq);
            g_kTreatyNews.erase(tn);
        }
        if (!news.empty()) h["econnews"] = news;
        h["kao"] = std::to_string(economy::ArmyOffensiveOf(p.nation));
        if (p.genLevel >= 4 && p.nation >= 1 && p.nation <= 8) {  // 0.7.0: the nation's invasion plan for its form
            inv::KState one[9];
            one[p.nation] = inv::State(p.nation);
            h["kinv"] = "+" + inv::EncodeStates(one);  // "+": present even when empty
        }
        if (!econ.empty())
            Log("economy: %d message(s) of nation %d's rules go with state seq %d", (int)econ.size(), p.nation, g_seq);
        if (!war.empty())
            Log("warfx: %d message(s) of nation %d's war effects go with state seq %d", (int)war.size(), p.nation, g_seq);
        // B5: the nations blockaded now (the host's check and the joined nations' own), for this player's screen (older
        // clients ignore it and keep their own load's flags).
        h["kblk"] = warfx::BlockadedList();
    }
    std::string payload = PackHB(KVEncode(h), b.Pack());
    if (payload.size() > net::MaxMessageBytes()) {
        Notice("The month is too large to send to " + p.name + " (" + std::to_string(payload.size() >> 20) +
               " MB). Please report this with the RTW3MP log.");
        Log("state seq %d for %s NOT sent: %u bytes", g_seq, p.name.c_str(), (unsigned)payload.size());
        return;
    }
    net::Send(p.peer, net::MSG_STATE, payload);
    p.hasState = true;
    Log("state seq %d -> %s (nation %d, %u bytes)", g_seq, p.name.c_str(), p.nation, (unsigned)b.TotalBytes());
}

// 0.6.1 (host): the tension between two nations people play above the game's war line (the strategic screen's tension
// panel: a bar crosses its WarLine at Tension 11; in single player a nation that far above it declares war soon). Wars
// between players are declared only by the players (the game's rules see the nations people play, reserved ones
// included, as players), so both players are told once each time the pair's tension crosses the line: checked as each
// month goes out; the first time after hosting, the pairs above it already.
static const int kWarLineTension = 11;
static std::map<std::pair<int, int>, bool> g_warLineAbove;  // pair (a < b) -> above the line at the last check

static int WarLineNews() {
    std::vector<int> people;
    for (int j = 0; j <= 8 && j < game::NationCount(); j++)
        if (j == 0 || hookcb::WarOnlyByPlayers(j)) people.push_back(j);
    int told = 0;
    for (size_t x = 0; x < people.size(); x++)
        for (size_t y = x + 1; y < people.size(); y++) {
            int a = people[x], b = people[y], t = game::Tension(a, b);
            bool above = t >= kWarLineTension && t <= 40;
            bool& was = g_warLineAbove[{a, b}];
            bool tell = above && !was;
            was = above;
            if (!tell) continue;
            std::string line = "Tension between " + CampaignNationName(a) + " and " + CampaignNationName(b) + " is " +
                               std::to_string(t) + ", above the war line. Wars between players are declared only by "
                               "the players (Multiplayer window, Diplomacy); the game will not declare one.";
            std::string to;
            for (int s : {a, b}) {
                if (s == 0) {
                    AddLocalChat("* " + line);
                    if (g_onNotice) g_onNotice(line);
                    to += " the host";
                    continue;
                }
                for (auto& kv : g_players) {
                    Player& p = kv.second;
                    if (!p.connected || !p.welcomed || p.nation != s) continue;
                    NoticeTo(p, line);
                    to += " nation " + std::to_string(s);
                }
            }
            told++;
            Log("war line: %s (told:%s)", line.c_str(), to.c_str());
        }
    return told;
}

static void BroadcastState() {
    HostArmsTreatyCheck("the month going out");  // also a resent month and a campaign hosted again
    if (!game::SaveGame()) {
        Notice("Saving the game failed; cannot send the new month.");
        return;
    }
    std::wstring dir = game::SaveDir(g_hostSlot);
    int y = 0, m = 0;
    saveio::ReadDate(dir, g_hostSlot, y, m);
    NoteRelRules();
    KHistUpdate(y * 12 + m);  // relations v2: the wars that ended are dated with the new month
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
    WarLineNews();
    // News about each player's design studies goes to that player only (after the month, so it is the last line).
    for (auto& kv : g_players) {
        Player& p = kv.second;
        if (!p.connected || !p.welcomed) continue;
        auto tr = g_treatyNews.find(p.nation);
        if (tr != g_treatyNews.end()) {
            std::string joined;
            for (auto& l : tr->second) joined += l + kSepRec;
            KV c;
            c["treaty"] = joined;
            net::Send(p.peer, net::MSG_CHAT, KVEncode(c));
        }
        auto it = g_playerNews.find(p.nation);
        if (it == g_playerNews.end()) continue;
        for (auto& line : it->second) {
            KV c;
            c["from"] = "*";
            c["text"] = line;
            c["notice"] = "1";
            net::Send(p.peer, net::MSG_CHAT, KVEncode(c));
        }
    }
    g_playerNews.clear();
    g_treatyNews.clear();
}

// Host: the monthly countdown of a joined player's design studies, as the game does it for the host's nation
// (AdvanceDesignStudies): one month less, except for a 1-in-100 technical delay. The game's committee and Air
// Force events about design studies need an answer on the host's screen and are left out for joined players.
static void AdvanceDesignStudiesFor(int n) {
    for (int i = 0; i < game::DesignCount(n); i++) {
        int left = game::DesignReadyForBuild(n, i);
        if (left <= 0 || left >= INT_MAX) continue;
        std::string cls = W2U(game::DesignName(n, i));
        if (std::uniform_int_distribution<int>(0, 99)(g_rng) == 0) {
            Log("design study nation %d '%s': delayed (technical issues)", n, cls.c_str());
            g_playerNews[n].push_back("Design study: technical issues have delayed the " + cls + " class by a month.");
            continue;
        }
        game::SetDesignReadyForBuild(n, i, left - 1);
        Log("design study nation %d '%s': %d -> %d", n, cls.c_str(), left, left - 1);
        if (left == 1) g_playerNews[n].push_back("Design study: the " + cls + " class is ready for construction.");
    }
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

// ---------------------------------------------------------------------------------------------
// Tactical battles of joined players (host side)

static Player* PlayerOfNation(int n) {
    for (auto& kv : g_players)
        if (kv.second.nation == n && kv.second.connected && kv.second.welcomed) return &kv.second;
    return nullptr;
}

static const char* BattleStateName(int s) {
    return s == 0 ? "queued" : s == 1 ? "fighting" : s == 2 ? "fought" : s == 3 ? "to the game" : "resolved by the game";
}

static std::wstring HostBcsPath() { return game::SaveDir(g_hostSlot) + L"RTWGame" + std::to_wstring(g_hostSlot) + L".bcs"; }

// Host: a live battle between two players' nations is over (or abandoned): stop relaying it; the player still in it
// (B, or A if B was the one who left) hears that the other one is gone.
static void LsEndRelay(int id, const std::string& why) {
    auto it = g_lsRelay.find(id);
    if (it == g_lsRelay.end()) return;
    std::string msg = lockstep::PeerLostMessage(id, why);
    net::Send(it->second.first, net::MSG_LOCKSTEP, msg);
    net::Send(it->second.second, net::MSG_LOCKSTEP, msg);
    g_lsRelay.erase(it);
    Log("lockstep relay %d ended: %s", id, why.c_str());
}

static void BattleFallback(BattleReq& r, const std::string& why) {
    if (r.state >= 2) return;
    if (r.pvp) LsEndRelay(r.id, why);
    if (r.state == 1 && PlayerOfNation(r.human)) {
        KV c;
        c["id"] = std::to_string(r.id);
        c["why"] = why;
        net::Send(r.peer, net::MSG_BATTLE_CANCEL, KVEncode(c));
    }
    r.state = 3;
    r.why = why;
    Log("battle %d: resolved by the game (%s)", r.id, why.c_str());
}

static void SendBattle(BattleReq& r) {
    Player* p = PlayerOfNation(r.human);
    if (!p) {
        BattleFallback(r, "the player is not connected");
        return;
    }
    Bundle b;
    std::string err;
    std::wstring dir = game::SaveDir(g_hostSlot);
    // The save as it is now: the results of this phase's earlier battles are merged into it already.
    if (!ReadFileBytes(HostBcsPath(), r.sentBcs) || !saveio::BuildStateBundle(dir, g_hostSlot, kIdStride * r.human, b, err)) {
        BattleFallback(r, "cannot package the game: " + err);
        return;
    }
    // 0.6.1: the battle is generated on the player's PC in a copy where its nation is the player (MakeNationPlayer
    // keeps the divisions whose ships are all the nation's), so its own campaign divisions go in for every mod version.
    // The game uses campaign divisions for its player's side only: the other side of a battle between two joined
    // players is a computer nation in that copy, and a live battle of the host's own nation, generated by the host's
    // game, uses the host's divisions (the joined nation is a computer nation there); both stay as they are.
    nplay::HostAttach(dir, CampaignKey());
    nplay::HostPutDivisions(b, r.human, true, "battle");
    KV h;
    h["id"] = std::to_string(r.id);
    h["divs"] = "1";
    h["nation"] = std::to_string(r.human);
    h["opp"] = std::to_string(r.opp);
    h["a"] = std::to_string(r.a);
    h["b"] = std::to_string(r.b);
    h["countA"] = std::to_string(r.countA);
    h["countB"] = std::to_string(r.countB);
    h["area"] = W2U(r.area);
    h["oppName"] = CampaignNationName(r.opp);
    h["timeout"] = std::to_string(g_battleTimeoutSec);
    if (!r.pvp && p->genLevel >= 1) {  // 0.6.0: the nation's own decline counts (its battle slot holds the host's)
        auto it = g_kDecl.find(r.human);
        h["dc"] = it == g_kDecl.end() ? "0:0" : std::to_string(it->second.first) + ":" + std::to_string(it->second.second);
    }
    if (r.kind != 0) {  // 0.6.0 (only to players announcing hello `gen`): the player's generator's attempts
        h["kind"] = std::to_string(r.kind);
        h["att"] = std::to_string(r.attempts.size());
        bool raids = false;
        for (size_t i = 0; i < r.attempts.size(); i++) {
            const game::BattleAttempt& t = r.attempts[i];
            raids |= t.type == 1 || t.type == 2;  // 0.7.0: a raider check (hello `gen` 3), an invasion check (`gen` 4)
            h["att" + std::to_string(i)] = t.type == 1   ? std::string("R")
                                           : t.type == 2 ? std::string("I")
                                                         : std::to_string(t.a) + "," + std::to_string(t.b) + "," + W2U(t.area);
        }
        if (raids) {  // the nations people play (real indices): no raider or invasion battle with them on the player's PC
            std::string hs;
            for (int n = 0; n < 9 && n < game::NationCount(); n++)
                if (IsHumanNation(n)) hs += (hs.empty() ? "" : ",") + std::to_string(n);
            h["humans"] = hs;
        }
    }
    Player* p2 = r.pvp ? PlayerOfNation(r.opp) : nullptr;
    if (!r.pvp && p->genLevel >= 4 && inv::Available() && hookcb::PlayerRulesNation(r.human)) {
        // 0.7.0 (invasions, P3): the nation's invasions and plan go with every battle of it (the battle's aftermath, the
        // invasion check and the declines act on them in its battle slot) and come back with the result
        auto it = g_invPhase.find(r.human);
        if (it == g_invPhase.end()) {
            InvPhase ph;
            ph.plan0 = inv::State(r.human);
            ph.base = ph.now = inv::KPossessions(r.human);
            it = g_invPhase.emplace(r.human, ph).first;
        }
        inv::KState one[9];
        one[r.human] = inv::State(r.human);
        h["kinv"] = "+" + inv::EncodeStates(one);
        h["kinvp"] = inv::EncodePoss(it->second.now);
    }
    if (p2) {  // the other side is commanded live from p2's PC; the host relays the battle's messages
        h["pvp"] = "1";
        h["p2"] = p2->name;
        h["autostrike"] = g_autoStrike ? "1" : "0";  // the host's carrier-strike option (lockstep StartInfo)
    }
    std::string payload = PackHB(KVEncode(h), b.Pack());
    if (payload.size() > net::MaxMessageBytes()) {
        BattleFallback(r, "the game is too large to send");
        return;
    }
    r.peer = p->peer;
    r.state = 1;
    r.sentTick = GetTickCount();
    if (p2) g_lsRelay[r.id] = {p->peer, p2->peer};
    net::Send(p->peer, net::MSG_BATTLE, payload);
    Log("battle %d sent to %s (%u bytes)%s", r.id, p->name.c_str(), (unsigned)payload.size(),
        p2 ? (", live against " + p2->name).c_str() : "");
}

// A battle between two players' nations: fought live when both players fight their battles and can play live ones
// with the same live-battle engine as the host (else the game resolves it).
static bool HostDeferPvpBattle(int a, int b, int countA, int countB, const std::wstring& area) {
    Player* pa = PlayerOfNation(a);
    Player* pb = PlayerOfNation(b);
    if (!g_lsEnabled || !pa || !pb || !pa->battles || !pb->battles || !pa->lockstep || !pb->lockstep) return false;
    if (!SameLiveEngine(*pa) || !SameLiveEngine(*pb)) {
        std::string what = "The battle of " + CampaignNationName(a) + " against " + CampaignNationName(b) + " in " +
                           W2U(area) + " is resolved by the game: ";
        Player* bad = !SameLiveEngine(*pa) ? pa : pb;
        Log("battle %d vs %d in %s: not fought live: live-battle engine levels %d/%d, host %d", a, b, W2U(area).c_str(),
            pa->lsLevel, pb->lsLevel, MP_LS_LEVEL);
        AddLocalChat("* " + what + LevelWhy(*bad, LevelReader::Host) + ".");
        for (Player* p : {pa, pb}) {  // (each player their own level's reason, else the other's)
            Player* q = SameLiveEngine(*p) ? (p == pa ? pb : pa) : p;
            NoticeTo(*p, what + LevelWhy(*q, q == p ? LevelReader::Player : LevelReader::Other) + ".");
        }
        return false;
    }
    BattleReq r;
    r.id = g_battleNextId++;
    r.a = a;
    r.b = b;
    r.countA = countA;
    r.countB = countB;
    r.area = area;
    r.human = a;
    r.opp = b;
    r.player = pa->name;
    r.player2 = pb->name;
    r.pvp = true;
    g_battles.push_back(r);
    Log("battle request %d: nation %d vs %d in %s (%d:%d ships), fought live by %s and %s", r.id, a, b,
        W2U(area).c_str(), countA, countB, pa->name.c_str(), pb->name.c_str());
    AddChat("* Battle! " + CampaignNationName(a) + " meets " + CampaignNationName(b) + " in " + W2U(area) + ". " +
            pa->name + " and " + pb->name + " fight it live after the month.");
    return true;
}

static std::string InArea(const std::wstring& area) { return area.empty() ? "" : " in " + W2U(area); }

// B6 (0.6.0): nation K's battles against the computer's nations come from the player's battle generator (the host runs
// the game's CheckForBattles2 in K's view, game::RunBattleGenerator; K's PC replays the attempts), not the computer's.
// For a nation under the player's rules whose player's PC replays them (hello `gen`); else the computer's, as before.
static bool B6On(int K) {
    if (g_role != Role::Host || K < 1 || K > 8 || !game::BattleGeneratorAvailable() || !hookcb::PlayerRulesNation(K))
        return false;
    Player* p = PlayerOfNation(K);
    return p && p->battles && p->genLevel >= 1;
}

static std::string AttemptsText(const std::vector<game::BattleAttempt>& at) {
    std::string s;
    for (auto& t : at)
        s += (s.empty() ? "" : "; ") +
             (t.type == 1   ? std::string("raider check")
              : t.type == 2 ? std::string("invasion check")
                            : std::to_string(t.a) + ">" + std::to_string(t.b) +
                                  (t.area.empty() ? std::string(" (area by the game)") : " " + W2U(t.area)));
    return s;
}

// 0.7.0: the player's PC replays raider checks (hello `gen` 3); the first computer nation at war with K names a request
// that has raider checks only (the interception's enemy is known after it).
static bool RaiderChecksOn(int K) {
    Player* p = PlayerOfNation(K);
    return p && p->genLevel >= 3;
}

// 0.7.0 (invasions, P3): the player's PC replays the invasion check (hello `gen` 4) of a nation with invasions or a plan.
static bool InvasionChecksOn(int K) {
    Player* p = PlayerOfNation(K);
    return p && p->genLevel >= 4 && inv::Available() && game::InvasionReplayAvailable() && inv::HasWork(K, inv::State(K));
}

static int FirstComputerEnemy(int K) {
    for (int n = 1; n <= 8 && n < game::NationCount(); n++)
        if (n != K && !IsHumanNation(n) && game::AtWar(K, n)) return n;
    return -1;
}

// The computer nation K's invasion battle would most likely be against (the enemy's pending landing, the owner of K's
// target, the other side of K's first invasion), else -1.
static int InvasionEnemy(int K) {
    const inv::KState& st = inv::State(K);
    auto computer = [K](int n) { return n >= 1 && n <= 8 && n != K && !IsHumanNation(n) && game::AtWar(K, n); };
    if (st.pending >= 0 && st.pending != K && computer(st.pending)) return st.pending;
    int o = st.target.empty() ? -1 : inv::PossessionOwner(st.target);
    if (computer(o)) return o;
    for (auto& p : inv::KPossessions(K)) {
        int n = p.invader == K ? inv::PossessionOwner(p.name) : p.invader;
        if (computer(n)) return n;
    }
    return -1;
}

// Runs nation K's generator; its attempts against computer nations (and, 0.7.0, its raider checks and its invasion
// check) become one request of kind 1 (none if none). `queue` false: a dry run (tests). Returns the request's id, 0 for
// no attempt, -1 if the generator could not run.
static int HostRunGenerator(int K, bool queue, std::string& out) {
    bool human[9];
    for (int n = 0; n < 9; n++) human[n] = IsHumanNation(n);
    std::vector<game::BattleAttempt> at, ok;
    std::string err;
    int war = KWar(K);
    if (!game::RunBattleGenerator(K, war, human, at, err, RaiderChecksOn(K), InvasionChecksOn(K))) {
        out = err;
        Log("the player's battle generator for nation %d: %s", K, err.c_str());
        return -1;
    }
    for (auto& t : at) {
        int opp = t.a == K ? t.b : t.b == K ? t.a : -1;
        if (t.type == 1 || t.type == 2 || (opp >= 1 && opp <= 8 && !IsHumanNation(opp))) ok.push_back(t);
    }
    out = AttemptsText(at);
    Log("the player's battle generator for nation %d (war counter %d)%s: %d attempt(s)%s%s", K, war,
        queue ? "" : " (dry run)", (int)at.size(), at.empty() ? "" : ": ", out.c_str());
    Player* p = PlayerOfNation(K);
    if (ok.empty() || !queue || !p) return 0;
    const game::BattleAttempt* first = nullptr;  // the first attempt with an opponent names the request
    int raids = 0, invs = 0;
    for (auto& t : ok) {
        if (t.type == 1) raids++;
        else if (t.type == 2) invs++;
        else if (!first) first = &t;
    }
    int ie = invs ? InvasionEnemy(K) : -1;
    int opp = first ? (first->a == K ? first->b : first->a) : ie >= 1 ? ie : FirstComputerEnemy(K);
    if (opp < 1) {
        Log("the player's battle generator for nation %d: %d raider check(s), %d invasion check(s) but no computer "
            "nation at war", K, raids, invs);
        return 0;
    }
    BattleReq r;
    r.id = g_battleNextId++;
    r.kind = 1;
    r.attempts = ok;
    r.a = first ? first->a : K;
    r.b = first ? first->b : opp;
    r.area = first ? first->area : std::wstring();
    r.human = K;
    r.opp = opp;
    r.player = p->name;
    g_battles.push_back(r);
    Log("battle request %d (the player's generator): nation %d vs %d%s, %d attempt(s)%s%s, fought by %s", r.id, K, r.opp,
        InArea(r.area).c_str(), (int)ok.size(), raids ? (" (" + std::to_string(raids) + " raider check(s))").c_str() : "",
        invs ? " (an invasion check)" : "", p->name.c_str());
    std::string more = std::string(raids ? ", or its raiders and patrols the enemy's" : "") +
                       (invs ? ", or its invasions bring a battle" : "");
    AddChat("* Battle? " + CampaignNationName(K) + "'s " +
            (first  ? "navy may meet the " + CampaignNationName(r.opp) + " navy" + InArea(r.area) + more
             : invs ? "invasions may bring a battle" + std::string(raids ? ", or its raiders and patrols meet the enemy's" : "")
                    : std::string("raiders and patrols may meet the enemy's")) +
            ". " + p->name + " plays it after the month.");
    return r.id;
}

// Host, DoTurn2: the game is about to look for the host's battle of the month; each joined nation K under the player's
// rules and at war looks for its own first (at most one a month each, as for the player in single player).
// 0.7.0: the nation's invasion target from its turn (`kinvt`, "" none). The game's own checks ran on the player's PC
// (not invaded, value below 10, only home areas while blockaded, the invasion range); the host takes a possession of a
// computer nation, not invaded and below 10, and starts its months of preparation again when the target changes.
static void HostTakeInvasionOrder(const std::string& player, int K, const std::wstring& name) {
    inv::KState& st = inv::State(K);
    if (name == st.target) return;
    std::string why;
    if (!name.empty()) {
        int owner = inv::PossessionOwner(name);
        why = owner == -2   ? "no such possession"
              : owner < 0   ? "it has no owner"
              : owner == K  ? "it is the nation's own"
              : IsHumanNation(owner) ? "a person plays its owner (invasions between players are not supported)"
              : inv::PossessionValue(name) >= 10 ? "its value is 10 or more"
              : inv::PossessionInvader(name) != -1 ? "it is invaded already"
                                                    : "";
    }
    if (!why.empty()) {
        AddChat("* " + player + "'s invasion target " + W2U(name) + " was not taken: " + why + ".");
        Log("invasions: nation %d's target %s refused (%s)", K, W2U(name).c_str(), why.c_str());
        return;
    }
    Log("invasions: nation %d's target %s -> %s", K, st.target.empty() ? "-" : W2U(st.target).c_str(),
        name.empty() ? "-" : W2U(name).c_str());
    st.target = name;
    st.count = 0;
    SaveReservations();
}

// 0.7.0: each joined nation's invasions, the game's CheckRebellionAndInvasions in its view (invasion.cpp), after the
// month's battles (single player's order: the month's battle, then the invasions) and before the war counters' step.
// Nations whose PC announced hello gen 4 (it sends the nation's orders and fights its invasion battles).
static void HostRunInvasions() {
    std::string hid = inv::TakeHideLog();
    if (!hid.empty()) Log("invasions: the host's own routines and the joined nations' invasions this month: %s", hid.c_str());
    if (!inv::Available()) return;
    bool human[9], ran = false;
    for (int i = 0; i < 9; i++) human[i] = IsHumanNation(i);
    for (int K = 1; K <= 8; K++) {
        inv::KState& st = inv::State(K);
        if (st.pending != -1) {  // the month's start (DoStrategicTurn) ends a battle pending since the last month
            Log("invasions of nation %d: the invasion battle pending (nation %d) ends with the month", K, st.pending);
            st.pending = -1;
            ran = true;
        }
        Player* p = PlayerOfNation(K);
        if (!hookcb::PlayerRulesNation(K) || !p || p->genLevel < 4) continue;
        if (!inv::HasWork(K, st)) continue;
        int war = KWar(K);
        auto d = g_kDecl.find(K);
        inv::RunResult r = inv::RunForNation(K, war, human, st, economy::ArmyOffensiveOf(K),
                                             d != g_kDecl.end() ? d->second.second : 0);
        if (!r.ran) {
            Log("invasions of nation %d: not run (%s)", K, r.error.c_str());
            continue;
        }
        Log("invasions of nation %d (war counter %d): %s%s%s", K, war, r.summary.c_str(), r.error.empty() ? "" : "; ERROR ",
            r.error.c_str());
        for (auto& t : r.news) {
            g_invNews[K].push_back(t);
            Log("invasions of nation %d: \"%s\"", K, W2U(t).c_str());
        }
        if (r.enemyVP > 0)  // "Enemy fleets dominate the seas around ...": K's enemies' points (the first computer enemy)
            for (int j = 1; j <= 8; j++)
                if (j != K && !IsHumanNation(j) && game::AtWar(K, j)) {
                    hookcb::CreditWarVP(j, K, r.enemyVP, "invasions");
                    break;
                }
        ran = true;
    }
    if (ran) SaveReservations();  // the plans (`kinv`) of the new month
}

static void HostRunBattleGenerators() {
    if (g_role != Role::Host || !g_turnRunning) return;
    for (int K = 1; K <= 8 && K < game::NationCount(); K++) {
        if (!B6On(K) || KWar(K) <= 0) continue;
        std::string out;
        HostRunGenerator(K, true, out);
    }
}

// The game resolves a battle between two nations it runs. A battle of a joined player's nation against an AI nation is
// recorded instead (the player fights it after the month); everything else stays with the game.
static bool HostDeferBattle(int a, int b, int countA, int countB, const std::wstring& area) {
    if (g_role != Role::Host || !g_turnRunning || area.empty()) return false;
    bool ha = hookcb::IsHumanRemoteNation(a), hb = hookcb::IsHumanRemoteNation(b);
    if (ha && hb) return HostDeferPvpBattle(a, b, countA, countB, area);
    if (ha == hb) return false;  // two AI nations, or two players (the game fights their wars without a battle screen)
    int human = ha ? a : b, opp = ha ? b : a;
    if (B6On(human) && opp != 0) {  // B6: the player's generator finds this nation's battles (no second generator)
        Log("battle of nation %d against %d in %s (the computer's generator): left to the player's generator", human, opp,
            W2U(area).c_str());
        return true;
    }
    Player* p = PlayerOfNation(human);
    if (!p || !p->battles) return false;
    BattleReq r;
    r.id = g_battleNextId++;
    r.a = a;
    r.b = b;
    r.countA = countA;
    r.countB = countB;
    r.area = area;
    r.human = human;
    r.opp = opp;
    r.player = p->name;
    g_battles.push_back(r);
    Log("battle request %d: nation %d vs %d in %s (%d:%d ships), fought by %s", r.id, a, b, W2U(area).c_str(), countA,
        countB, p->name.c_str());
    AddChat("* Battle! " + CampaignNationName(human) + " meets " + CampaignNationName(opp) + " in " + W2U(area) +
            ". " + p->name + " fights it after the month.");
    return true;
}

static bool BattlesDone() {
    for (auto& r : g_battles)
        if (r.state < 2) return false;
    return true;
}

// Host, after the month and before the new month is sent: send each player its battles (one at a time) and wait. The
// strategic screen is locked meanwhile: the save file is the campaign's state until the results are loaded.
static void BattlePhaseTick() {
    if (!g_battlePhase) {
        if (!game::SaveGame() || !ReadFileBytes(HostBcsPath(), g_battlePhaseBytes)) {
            for (auto& r : g_battles) BattleFallback(r, "the host could not save the month");
            return;
        }
        g_battlePhase = true;
        g_battleMerged = false;
        game::SetStrategicEnabled(false);
        Log("battle phase: %d battle(s)", (int)g_battles.size());
    }
    for (auto& r : g_battles) {
        if (r.state == 1) {
            Player* p = PlayerOfNation(r.human);
            if (!p || p->peer != r.peer)
                BattleFallback(r, r.player + " disconnected");
            else if (g_battleTimeoutSec > 0 && GetTickCount() - r.sentTick > (DWORD)g_battleTimeoutSec * 1000)
                BattleFallback(r, "time limit");
        }
    }
    for (auto& r : g_battles) {  // one battle per player at a time (a live battle occupies both players)
        if (r.state != 0) continue;
        bool busy = false;
        for (auto& o : g_battles) {
            if (o.state != 1) continue;
            auto has = [](const BattleReq& x, int n) { return x.human == n || (x.pvp && x.opp == n); };
            busy |= has(o, r.human) || (r.pvp && has(o, r.opp));
        }
        if (!busy) SendBattle(r);
    }
    DWORD now = GetTickCount();
    if (now - g_battleStatusTick > 3000) {
        g_battleStatusTick = now;
        std::string text;
        for (auto& r : g_battles)
            if (r.state == 1)
                text += (text.empty() ? "" : "; ") + r.player + ": " + CampaignNationName(r.human) + " vs " +
                        CampaignNationName(r.opp) + InArea(r.area);
        if (!text.empty()) BroadcastStatus("Battles in progress: " + text);
    }
}

// Host: load the save with the merged results, then let the game resolve the battles that weren't fought. False if
// the campaign could not be loaded (the month is not sent then).
static bool FinishBattlePhase() {
    g_battleFinishing = true;
    bool ok = true;
    std::vector<std::wstring> marks = g_battleMerged ? inv::MarkedThisTurn() : std::vector<std::wstring>();
    if (g_battleMerged && !game::LoadCampaign(g_hostSlot, 0)) {
        Log("battle phase: loading the merged save failed; restoring the month without the battles");
        for (auto& r : g_battles)
            if (r.state == 2) {
                r.state = 3;
                r.why = "the results could not be loaded";
            }
        ok = WriteFileBytes(HostBcsPath(), g_battlePhaseBytes) && game::LoadCampaign(g_hostSlot, 0);
    }
    game::SetStrategicEnabled(true);
    if (ok && !marks.empty())  // 0.7.0: InvadedThisTurn is not saved (the month's new invasions wait a month)
        Log("battle phase: invaded this month kept across the reload: %d of %d", inv::RestoreMarked(marks),
            (int)marks.size());
    // 0.7.0 (P3): the joined nations' invasions as their battles left them (not if the month was restored without the
    // battles' results)
    bool restored = g_battleMerged && std::none_of(g_battles.begin(), g_battles.end(), [](const BattleReq& r) {
                        return r.state == 2;
                    });
    for (auto& e : g_invPhase) {
        if (!e.second.changed) continue;
        if (restored || !ok) {
            inv::State(e.first) = e.second.plan0;
            SaveReservations();
            Log("battle phase: nation %d's invasions as before its battles (the results were not loaded)", e.first);
            continue;
        }
        std::string s = inv::ApplyPossessions(e.first, e.second.base, e.second.now);
        Log("battle phase: nation %d's invasions after its battles: %s", e.first, s.empty() ? "no change" : s.c_str());
    }
    g_invPhase.clear();
    if (!ok) {
        Notice("The month could not be loaded after the battles. Load the save and host again.");
        g_battles.clear();
        g_battlePhase = g_battleFinishing = false;
        return false;
    }
    for (auto& r : g_battles) {
        if (r.state == 2) {
            AddChat("* " + (r.avoided    ? "The " + CampaignNationName(r.opp) + " navy declined battle against " +
                                            CampaignNationName(r.human) + InArea(r.area) + "."
                            : r.declined ? r.player + " declined battle for " + CampaignNationName(r.human) + " against " +
                                               CampaignNationName(r.opp) + InArea(r.area) + "."
                            : r.raider   ? "The raider battle of " + CampaignNationName(r.human) + " against " +
                                             CampaignNationName(r.opp) + InArea(r.area) + " was " +
                                             (r.raider == 2 ? "auto-resolved" : "fought") + " by " + r.player + "."
                                         : std::string(r.invasion ? "The invasion battle of " : "The battle of ") +
                                               CampaignNationName(r.human) + " against " +
                                               CampaignNationName(r.opp) + InArea(r.area) + " was fought by " +
                                               r.player + "."));
            continue;
        }
        if (r.state != 3) continue;
        r.state = 4;  // resolved once, even if the game's routine fails
        if (r.kind != 0) {  // the player's generator: only a battle the player's PC found is a battle
            if (r.kind == 2)  // a surprise attack: the first pair names it (the players see the attacker's side)
                AddChat("* No surprise attack of " + CampaignNationName(r.a) + " on " + CampaignNationName(r.b) + " (" +
                        r.why + ").");
            else
                AddChat("* No battle of " + CampaignNationName(r.human) + " this month (" + r.why + ").");
            Log("battle %d (kind %d): no battle (%s)", r.id, r.kind, r.why.c_str());
            continue;
        }
        std::vector<std::wstring> texts;
        bool fought = game::ResolveAIBattle(r.a, r.b, r.countA, r.countB, r.area, &texts);
        AddChat("* The battle of " + CampaignNationName(r.human) + " against " + CampaignNationName(r.opp) + " in " +
                W2U(r.area) + " was resolved by the game (" + r.why + ").");
        for (auto& t : texts) {  // its result box: the player's news (and the chat), not a dialog on the host
            std::string u = W2U(t);
            for (char& c : u)
                if (c == '\r' || c == '\n') c = ' ';
            AddChat("* " + u);
            g_playerNews[r.human].push_back(u);
        }
        Log("battle %d resolved by the game: %s", r.id, fought ? "battle" : "no battle");
    }
    g_battles.clear();
    g_battlePhase = g_battleFinishing = g_battleMerged = false;
    g_battlePhaseBytes.clear();
    return true;
}

// A fought battle's result is merged into the save file at once, so the player's next battle is built from it.
// A battle fought on a player's PC: its result is in the save file but not yet in the campaign in memory. The host's
// battle hooks never saw it, so its points are counted here for the war score of a war between two players (the
// peace terms), and (relations v2, B4) of a war of a nation under the player's rules with a computer nation.
static void CreditWarScore(int a, int b, int vpA, int vpB) {
    bool players = IsHumanNation(a) && IsHumanNation(b);
    if ((!players && !LedgerPair(a, b)) || !game::AtWar(a, b) || (vpA <= 0 && vpB <= 0)) return;
    auto& vp = WarVP(a, b);  // a new score starts from the nations' points before this battle
    if (a < b) {
        vp.first += vpA;
        vp.second += vpB;
    } else {
        vp.first += vpB;
        vp.second += vpA;
    }
    Log("war score %d-%d: +%d/+%d from a battle fought on a player's PC -> %d:%d", std::min(a, b), std::max(a, b),
        a < b ? vpA : vpB, a < b ? vpB : vpA, vp.first, vp.second);
}

static void HostOnBattleResult(Player& p, const std::string& data) {
    KV h;
    std::string body;
    if (!UnpackHB(data, h, body)) return;
    int id = KVInt(h, "id", -1);
    for (auto& r : g_battles) {
        if (r.id != id || r.state != 1 || r.peer != p.peer) continue;
        std::string outcome = KVStr(h, "outcome");
        // "avoided" (0.4.1): the enemy declined battle on the player's PC (no battle, the player's VP gain is applied).
        bool avoided = outcome == "avoided";
        // "declined" with the slot's result (0.6.0, risk 4): the player declined; the game's consequences are merged.
        bool declined = outcome == "declined" && !body.empty() && !r.pvp;
        {  // 0.6.0: the nation's decline counts after it (its own, kept by the host for its next battle slot)
            auto f = Split(KVStr(h, "dc"), ':');
            if (!r.pvp && f.size() == 2 && !f[0].empty() && !f[1].empty()) {
                std::pair<int, int> dc{atoi(f[0].c_str()), atoi(f[1].c_str())};
                auto it = g_kDecl.find(r.human);
                std::pair<int, int> was = it == g_kDecl.end() ? std::pair<int, int>{0, 0} : it->second;
                Log("battle %d: nation %d's decline counts %d/%d (were %d/%d)", r.id, r.human, dc.first, dc.second,
                    was.first, was.second);
                if (it == g_kDecl.end() || was != dc) {
                    g_kDecl[r.human] = dc;
                    SaveReservations();
                }
            }
        }
        int cand = KVInt(h, "cand", -1);  // kind 1, 2: the attempt that gave it
        auto ip = g_invPhase.find(r.human);
        if (!r.pvp && h.find("kinv") != h.end() && ip != g_invPhase.end() && outcome != "failed") {
            // 0.7.0 (invasions, P3): the nation's invasions and plan as its battle slot left them
            inv::KState one[9];
            inv::DecodeStates(KVStr(h, "kinv").substr(1), one);
            std::vector<inv::PossState> now = inv::DecodePoss(KVStr(h, "kinvp"));
            inv::KState& st = inv::State(r.human);
            std::string was = inv::EncodePoss(ip->second.now);
            bool planChanged = one[r.human].target != st.target || one[r.human].count != st.count ||
                               one[r.human].enemyTarget != st.enemyTarget || one[r.human].pending != st.pending ||
                               one[r.human].itt != st.itt;
            bool changed = planChanged || inv::EncodePoss(now) != was;
            if (changed) {
                st = one[r.human];
                ip->second.now = now;
                ip->second.changed = true;
                SaveReservations();
            }
            std::string ps;
            for (auto& x : now)
                ps += (ps.empty() ? "" : ", ") + W2U(x.name) + " " + std::to_string(x.invader) + "/" +
                      std::to_string(x.support);
            Log("battle %d: nation %d's invasions after it%s: plan %s/%d, enemy plan %s/%d, %d mark(s); %s", r.id,
                r.human, changed ? "" : " (unchanged)", st.target.empty() ? "-" : W2U(st.target).c_str(), st.count,
                st.enemyTarget.empty() ? "-" : W2U(st.enemyTarget).c_str(), st.pending, (int)st.itt.size(),
                ps.empty() ? "no invasion" : ps.c_str());
            // the month's invasion check used the pending battle (single player's DoStrategicTurn ends it before the
            // next check): a second request of the phase does not meet it again ("no invasion target")
            bool checked = std::any_of(r.attempts.begin(), r.attempts.end(),
                                       [](const game::BattleAttempt& t) { return t.type == 2; });
            if (checked && st.pending != -1) {
                Log("battle %d: nation %d's pending invasion battle (nation %d) was this month's", r.id, r.human, st.pending);
                st.pending = -1;
                ip->second.changed = true;
                SaveReservations();
            }
        }
        if (r.kind == 1 && p.genLevel >= 3) {  // 0.7.0: the raider checks' "thwarts" lines: the player's news
            for (auto& l : Split(KVStr(h, "news"), kSepRec)) {
                if (l.empty()) continue;
                g_playerNews[r.human].push_back(l.substr(0, 400));
                Log("battle %d: news for nation %d: %s", r.id, r.human, l.substr(0, 400).c_str());
            }
        }
        if (r.kind >= 1 && cand >= 0 && cand < (int)r.attempts.size() && r.attempts[cand].type == 2) {
            // 0.7.0 (P3): the invasion check gave it: the invasion battle's enemy (a computer nation)
            int iopp = KVInt(h, "iopp", -1);
            bool gave = h.find("iopp") != h.end() || outcome == "fought" || avoided || declined;
            if (gave && (iopp < 1 || iopp > 8 || iopp == r.human || IsHumanNation(iopp))) {
                Log("battle %d: an invasion battle against nation %d is not taken", r.id, iopp);
                BattleFallback(r, "its invasion battle's enemy is not a computer nation");
                return;
            }
            if (gave) {
                r.invasion = true;
                r.a = KVInt(h, "iatt", 1) ? r.human : iopp;
                r.b = r.a == r.human ? iopp : r.human;
                r.opp = iopp;
                r.area = U2W(KVStr(h, "area"));
            }
        } else if (r.kind >= 1 && cand >= 0 && cand < (int)r.attempts.size() && r.attempts[cand].type == 1) {
            // 0.7.0: a raider check gave it: the interception's enemy (a computer nation at war with the player's)
            int ropp = KVInt(h, "ropp", -1);
            r.raider = KVInt(h, "raider", 0);
            if (r.raider != 0 && (ropp < 1 || ropp > 8 || ropp == r.human || IsHumanNation(ropp) || r.raider > 2)) {
                Log("battle %d: a raider battle against nation %d is not taken", r.id, ropp);
                r.raider = 0;
                BattleFallback(r, "its raider battle's enemy is not a computer nation");
                return;
            }
            if (r.raider != 0) {
                r.a = r.human;
                r.b = ropp;
                r.opp = ropp;
                r.area = U2W(KVStr(h, "area"));
            }
        } else if (r.kind >= 1 && cand >= 0 && cand < (int)r.attempts.size()) {
            const game::BattleAttempt& t = r.attempts[cand];
            r.a = t.a;
            r.b = t.b;
            r.opp = t.a == r.human ? t.b : t.a;
            r.area = t.area;
            std::string area = KVStr(h, "area");
            if (!area.empty()) r.area = U2W(area);
        }
        Bundle b;
        if ((outcome != "fought" && !avoided && !declined) || !b.Unpack(body)) {
            std::string why = KVStr(h, "why");
            BattleFallback(r, outcome == "declined"                    ? p.name + " declined it"
                              : outcome == "nobattle" && r.kind == 2 && !why.empty() ? why  // the surprise attack's
                              : outcome == "nobattle"                  ? "no battle came about"
                                                                       : "it was not fought (" + KVStr(h, "why", outcome) + ")");
            return;
        }
        std::vector<int> allowed = {r.human, r.opp};  // and the AI nations allied to either side
        for (int n = 1; n <= 8 && n < game::NationCount(); n++)
            if (!hookcb::IsHumanRemoteNation(n)) allowed.push_back(n);
        std::string bytes, summary, err;
        IniDoc host;
        bool ok = ReadFileBytes(HostBcsPath(), bytes);
        if (ok) {
            host = ParseIni(bytes);
            ok = saveio::ApplyBattleResult(host, r.sentBcs, r.human, r.opp, b, allowed, summary, err, r.raider != 0) &&
                 WriteFileBytes(HostBcsPath(), WriteIni(host));
        }
        if (!ok) {
            Log("battle %d NOT applied: %s", id, err.empty() ? "cannot read or write the save" : err.c_str());
            BattleFallback(r, "its result could not be applied");
            return;
        }
        r.state = 2;
        r.avoided = avoided;
        r.declined = declined;
        g_battleMerged = true;
        if (r.pvp) LsEndRelay(r.id, "the battle's result arrived");
        Log("battle %d applied: %s (player: %s)%s", id, summary.c_str(), KVStr(h, "summary").c_str(),
            avoided    ? "; the enemy declined battle"
            : declined ? "; the player declined battle"
            : r.raider == 2 ? "; a raider battle, auto-resolved"
            : r.raider == 1 ? "; a raider battle"
                            : "");
        const std::string* deltas = b.Get("deltas");
        KV dk = deltas ? KVDecode(*deltas) : KV();
        CreditWarScore(r.human, r.opp, std::max(0, KVInt(dk, "dVP_K")), std::max(0, KVInt(dk, "dVP_J")));
        return;
    }
    Log("battle result %d from %s ignored", id, p.name.c_str());
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
    g_relSavePending = false;  // this save holds the month's relation changes too
    std::wstring dir = game::SaveDir(g_hostSlot);
    nplay::HostAttach(dir, CampaignKey());
    int merged = 0;
    for (auto& s : g_submissions) {
        auto it = g_players.find(s.first);
        if (it == g_players.end()) continue;
        std::string err;
        if (saveio::MergeSubmission(dir, g_hostSlot, it->second.nation, s.second, err)) {
            merged++;
            nplay::HostTakeSubmission(s.second, it->second.nation);  // its own intelligence spending and reports
            bool legacy = saveio::IsLegacySubmission(s.second) && it->second.nation >= 1 && it->second.nation <= 8;
            // 0.6.1: its own campaign divisions; 0.7.0: after a legacy turn only those of the new fleet (or none)
            nplay::HostTakeDivisions(s.second, it->second.nation, legacy);
            if (legacy) {
                g_legacyDone.insert(it->second.nation);  // (a resent first month offers no second build)
                SaveReservations();
                AddChat("* " + it->second.name + "'s hand-built legacy fleet of " +
                        W2U(game::NationName(it->second.nation)) + " is in the campaign.");
            }
        } else {
            Log("host turn: %s's turn NOT applied: %s", it->second.name.c_str(), err.c_str());
            AddChat("* Could not apply " + it->second.name + "'s turn: " + err);
            Notice(it->second.name + "'s turn could not be applied (" + err + "). Their nation keeps last month's "
                   "orders this month.");
        }
    }
    g_submissions.clear();
    std::vector<std::wstring> marks = merged > 0 ? inv::MarkedThisTurn() : std::vector<std::wstring>();
    if (merged > 0 && !game::LoadCampaign(g_hostSlot, 0)) {
        Notice("Reloading the merged game failed!");
        g_phase = Phase::Planning;
        g_turnRunning = false;
        return;
    }
    if (!marks.empty())  // 0.7.0: InvadedThisTurn is not saved: the invasions begun last month keep their month
        Log("host turn: invaded this month kept across the reload: %d of %d", inv::RestoreMarked(marks), (int)marks.size());
    Log("host turn: %d submissions merged, running turn", merged);
    if (g_enemyVPSeen < 0) g_enemyVPSeen = game::EnemyVP();  // the points before this month (see ShareEnemyVP)
    game::RunOriginalTurn();
    g_idleSinceTick = 0;
    Log("host turn: click handler returned (endOfTurn=%d battle=%d)", g_afterTurnPending, game::BattleActive());
}

static Player* PlayerByPeer(int peer) {
    auto it = g_players.find(peer);
    return it == g_players.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------------------------
// Live battles (host side): the host's own battle against a player's nation (host = A), and the relay of battles
// between two players' nations.

static void HostTryStartLockstep() {
    if (g_lsHost.opp < 0 || !lockstep::IsHolding()) return;
    g_lsHost.lastTry = GetTickCount();
    Player* p = PlayerByPeer(g_lsHost.peer);
    if (!p || !p->welcomed) {
        lockstep::ReleaseHold(g_lsHost.name + " is not connected");
        g_lsHost.opp = -1;
        return;
    }
    lockstep::StartInfo si;
    si.bid = g_lsHost.bid;  // allocated once for this battle (a retry or a failed start never reuses it)
    si.slot = game::CurrentSlot();
    si.campaignSlot = true;  // the host's own battle: Save writes a crash checkpoint into the campaign slot
    si.opp = g_lsHost.opp;
    si.autoStrike = g_autoStrike;
    si.myName = g_myName;
    si.peerName = p->name;
    si.sideNames[0] = CampaignNationName(0);
    si.sideNames[1] = CampaignNationName(g_lsHost.opp);
    int peer = p->peer;
    si.send = [peer](const std::string& s) { net::Send(peer, net::MSG_LOCKSTEP, s); };
    std::string err;
    bool retry = false;
    if (lockstep::StartAsA(si, err, retry)) {
        Log("live battle %d started with %s", si.bid, p->name.c_str());
        return;
    }
    if (retry && GetTickCount() - g_lsHost.since < 60000) return;  // Tick tries again
    if (retry) lockstep::ReleaseHold("the game stayed busy");
    Log("live battle with %s not started: %s", p->name.c_str(), err.c_str());
    g_lsHost.opp = -1;
}

// The game opened a battle screen on the host (DoBattle): against a player's nation whose player fights live battles,
// that player commands the other side from its PC.
static void HostOnBattleStarted(int opp) {
    if (g_role != Role::Host || !g_lsEnabled || lockstep::Active() || g_battlePhase) return;
    Player* p = PlayerOfNation(opp);
    if (!p) return;
    if (!p->battles || !p->lockstep) {
        Log("battle against %s: fought against the AI (%s)", p->name.c_str(),
            !p->battles ? "the player doesn't fight battles" : "no live battles on the player's PC");
        return;
    }
    if (!SameLiveEngine(*p)) {
        Log("battle against %s: fought against the AI (live-battle engine level %d, the host's %d)", p->name.c_str(),
            p->lsLevel, MP_LS_LEVEL);
        AddLocalChat("* The battle against " + CampaignNationName(opp) + " is fought against the computer: " +
                     LevelWhy(*p, LevelReader::Host) + ".");
        NoticeTo(*p, "The host fights its battle against " + CampaignNationName(opp) + " against the computer: " +
                         LevelWhy(*p, LevelReader::Player) + ".");
        return;
    }
    g_lsHost = {};
    g_lsHost.opp = opp;
    g_lsHost.peer = p->peer;
    g_lsHost.name = p->name;
    g_lsHost.since = GetTickCount();
    g_lsHost.bid = g_lsNextBid++;
    g_lsHost.testSlotMode = !g_turnRunning;
    if (g_lsHost.testSlotMode) game::SetBattleSlotMode(true);  // outside a month (tests): the close must not run one
    lockstep::Hold("waiting for " + p->name + " (" + CampaignNationName(opp) + ") to join the battle", g_lsHost.bid);
    if (!lockstep::IsHolding()) {
        g_lsHost.opp = -1;
        return;
    }
    RunOnMainThread(HostTryStartLockstep);
}

// A player's PC sent a live battle message: the host's own live battle, or a relayed one.
static void HostOnLockstep(Player& p, const std::string& data) {
    int bid = lockstep::MessageBattleId(data);
    auto it = g_lsRelay.find(bid);
    if (it != g_lsRelay.end()) {
        int to = p.peer == it->second.first ? it->second.second : p.peer == it->second.second ? it->second.first : -1;
        if (lockstep::MessageField(data, "t") == "norejoin") {  // A could not start the battle again for its B
            std::string why = lockstep::MessageField(data, "why"), why2 = lockstep::MessageField(data, "why2");
            Log("lockstep relay %d: %s's PC could not start the battle again: %s", bid, p.name.c_str(), why.c_str());
            if (Player* b = to >= 0 ? PlayerByPeer(to) : nullptr)
                NoticeTo(*b, "Your live battle could not be started again (" + (why2.empty() ? why : why2) +
                                 "): the computer keeps commanding your ships.");
            return;
        }
        if (to >= 0) net::Send(to, net::MSG_LOCKSTEP, data);
        for (auto& r : g_battles)  // a live battle whose players still talk is not timed out by the battle phase
            if (r.id == bid && r.state == 1) r.sentTick = GetTickCount();
        return;
    }
    if (bid >= 0 && bid == lockstep::BattleId() && p.peer == g_lsHost.peer) {
        int peer = p.peer;
        lockstep::OnMessage(data, [peer](const std::string& s) { net::Send(peer, net::MSG_LOCKSTEP, s); });
    }
}

// A player disconnected: its live battles lose that side.
static void HostLsPeerGone(int peer, const std::string& name) {
    if (g_lsHost.opp >= 0 && g_lsHost.peer == peer) {
        if (lockstep::IsHolding())
            lockstep::ReleaseHold(name + " disconnected");
        else
            lockstep::OnPeerLost(name + " disconnected");  // (also a rejoin still waiting for its minute boundary)
        g_lsHost.opp = -1;
    }
    std::vector<int> ids;
    for (auto& r : g_lsRelay)
        if (r.second.first == peer || r.second.second == peer) ids.push_back(r.first);
    for (int id : ids) LsEndRelay(id, name + " disconnected");
}

// Host: a player who was dropped from a live battle is back (joined again and got the nation back by the hello or by
// the reservation, or `ls rejoin`). If that battle still goes on against the computer, the player gets their side back:
// the host's own battle starts again with them at its next minute boundary (lockstep::Rejoin); a relayed battle between
// two players' nations is started again by the first player's PC, which the host asks (`rejoin`; it answers with the
// start or `norejoin`). manual: the test bridge (also for a player who was dropped but never disconnected). False:
// nothing to rejoin, or not possible (`why`; the player is told when it is their setup).
static bool HostOfferRejoin(Player& p, std::string* why = nullptr, bool manual = false) {
    auto no = [&](const std::string& w) {
        if (why) *why = w;
        Log("rejoin: %s (nation %d): %s", p.name.c_str(), p.nation, w.c_str());
        return false;
    };
    if (g_role != Role::Host || !p.welcomed || !p.connected || p.nation <= 0)
        return no("not a connected player with a nation");
    lockstep::RejoinInfo ri;
    bool rejoinable = lockstep::Rejoinable(ri);
    bool own = ri.bid >= 0 && ri.opp == p.nation;
    if (own) p.offeredSerial = ri.serial;  // (a resumed battle is offered to a connected player once: HostResumeTick)
    BattleReq* rel = nullptr;
    if (!own)
        for (auto& r : g_battles)
            if (r.pvp && r.state == 1 && r.opp == p.nation) {
                rel = &r;
                break;
            }
    if (!own && !rel) return no("no live battle of this nation goes on without its player");
    if (own && !rejoinable) return no(ri.why);
    if (own && ri.pending && g_lsHost.opp == p.nation && g_lsHost.peer == p.peer) return true;  // (on its way already)
    if (rel && !manual) {
        auto it = g_lsRelay.find(rel->id);
        if (it != g_lsRelay.end() && it->second.second == p.peer) return no("the player is in that battle");
    }
    std::string nation = CampaignNationName(p.nation);
    std::string against = CampaignNationName(own ? 0 : rel->human);
    // The player's PC must fight live battles with the host's live-battle engine (MP_LS_LEVEL).
    std::string stop, hostWhy;
    if (!g_lsEnabled) {
        stop = hostWhy = "live battles are off on the host";
    } else if (!p.battles) {
        stop = "Fight my battles is off on your PC";
        hostWhy = p.name + " doesn't fight battles";
    } else if (!p.lockstep) {
        stop = "live battles are off on your PC";
        hostWhy = "live battles are off on " + p.name + "'s PC";
    } else if (!SameLiveEngine(p)) {
        stop = LevelWhy(p, LevelReader::Player);
        hostWhy = LevelWhy(p, LevelReader::Host);
    }
    if (!stop.empty()) {
        NoticeTo(p, "Your live battle against " + against + " goes on without you: the computer keeps commanding " +
                        nation + " (" + stop + ").");
        AddLocalChat("* " + p.name + " is back, but the computer keeps commanding " + nation + " in the live battle: " +
                     hostWhy + ".");
        return no(hostWhy);
    }
    if (own) {
        int peer = p.peer;
        std::string err;
        auto send = [peer](const std::string& s) { net::Send(peer, net::MSG_LOCKSTEP, s); };
        if (!lockstep::Rejoin(ri.bid, p.name, send, err)) return no(err);
        g_lsHost.opp = p.nation;
        g_lsHost.peer = p.peer;
        g_lsHost.name = p.name;
        g_lsHost.bid = ri.bid;
        g_lsHost.since = g_lsHost.lastTry = GetTickCount();
        if (ri.held)  // (phase 5b: resumed from the host's save and held since: no minute ran without them)
            NoticeTo(p, "Your live battle against " + against + " waits for you: the host's game was restarted and "
                        "resumed it from its last save (minute " + std::to_string(ri.minute) + "). You get " + nation +
                        " back now; the battle loads in a moment.");
        else
            NoticeTo(p, "Your live battle against " + against + " is still running (minute " +
                            std::to_string(ri.minute) + "; the computer commanded " + nation +
                            " meanwhile): you get your ships back at its next minute. The battle loads in a moment.");
        Log("rejoin: %s is back as nation %d: live battle %d starts again with them at its next minute boundary%s",
            p.name.c_str(), p.nation, ri.bid, ri.resumed ? " (resumed from the host's save)" : "");
        return true;
    }
    Player* a = PlayerOfNation(rel->human);
    if (!a || a->peer != rel->peer || a->peer == p.peer)
        return no("the other player (" + rel->player + ") is not connected");
    g_lsRelay[rel->id] = {rel->peer, p.peer};
    rel->sentTick = GetTickCount();
    rel->player2 = p.name;
    net::Send(rel->peer, net::MSG_LOCKSTEP, lockstep::RejoinRequestMessage(rel->id, p.name));
    NoticeTo(p, "Your live battle against " + against + " (" + a->name + ") is still running: " + a->name +
                    "'s PC starts it again with you at its next minute. The battle loads in a moment.");
    AddChat("* " + p.name + " is back: " + a->name + "'s live battle against " + nation + " starts again with " +
            p.name + ".");
    Log("rejoin: %s is back as nation %d: %s's PC is asked to start relayed battle %d again", p.name.c_str(), p.nation,
        a->name.c_str(), rel->id);
    return true;
}

// ---------------------------------------------------------------------------------------------
// The host's month in progress after a restart (0.6.0 phase 5b). The host's game stopped during its live battle against
// a joined player; loading its campaign slot resumes the battle from its last snapshot (the game's own resume), and
// the lockstep makes it rejoinable and holds it (lockstep::ResumeFromMarker). The month is half done (the game's
// DoTurn2 ran up to the battle; the rest, DoStrategicTurn, runs when the battle screen closes), so the session takes the
// month up where it is instead of starting a new one.

// A battle of the host's campaign slot against a joined player's nation is on screen and can be rejoined: resumed from
// the save, or a live battle whose player was dropped before the host hosted again. Its strategic screen is hidden.
static bool MonthBattleOnScreen(lockstep::RejoinInfo& ri) {
    return lockstep::Rejoinable(ri) && ri.campaignSlot && ri.slot >= 1 && ri.slot <= 49 &&
           ri.slot == game::CurrentSlot() && ri.opp >= 1 && ri.opp <= 8 && game::NationCount() >= 9 &&
           game::BattleActive();
}

// Host, lobby: the campaign is open in the middle of its month (MonthBattleOnScreen; SyncHostCampaign has matched the
// players). The session goes on with that month as RunHostTurn would after the Turn: processing, the AI off for the
// reserved nations, their battles deferred; after DoEndOfTurn the battles, the diplomacy and the new month follow as
// usual (a test battle outside a month runs no month: its close leaves the session to resend the current month).
static void AdoptMonthInProgress(const lockstep::RejoinInfo& ri) {
    g_autoStart = false;
    for (auto& kv : g_players)  // (as StartSession: the players' nations are theirs in this campaign)
        if (kv.second.welcomed && kv.second.nation > 0) g_reserved[kv.second.nation] = kv.second.name;
    if (g_enemyVPSeen < 0) g_enemyVPSeen = game::EnemyVP();  // (as RunHostTurn: the points before this month's rest)
    g_lsNextBid = std::max(g_lsNextBid, ri.bid + 1);        // (the battle keeps its id: no new battle may take it)
    g_submissions.clear();
    for (auto& kv : g_players) kv.second.submitted = false;
    g_hostWantsTurn = false;
    g_forced = false;
    g_deadline = 0;
    g_runTurnWhenIdle = false;
    g_phase = Phase::Processing;
    g_turnRunning = true;
    g_afterTurnPending = false;
    g_turnStartTick = GetTickCount();
    g_idleSinceTick = 0;
    g_lsHost = {};
    g_lsHost.testSlotMode = game::BattleSlotMode();
    SaveReservations();
    std::string side1 = ri.sideNames[1].empty() ? CampaignNationName(ri.opp) : ri.sideNames[1];
    bool testBattle = game::BattleSlotMode();
    Log("resume: the campaign in slot %d is in the middle of its month (battle %d against %s, %s, %s%s): the session "
        "takes the month up (processing)",
        g_hostSlot, ri.bid, ri.peerName.c_str(), side1.c_str(), ri.resumed ? "resumed from the save" : "dropped",
        testBattle ? "; a test battle outside a month" : "");
    AddChat("* The host's month is in progress: its battle against " + side1 + " (" + ri.peerName + ") is on screen. " +
            (testBattle ? std::string("The current month is sent again when the battle is over.")
                        : std::string("The new month follows when the battle and the rest of the month are over.")));
    BroadcastLobby();
    BroadcastStatus("The host is fighting a battle...");
    if (Player* p = PlayerOfNation(ri.opp)) HostOfferRejoin(*p);  // (connected already: joined before the campaign)
}

// Host, during the month: a battle resumed from the save waits for its player (lockstep, phase 5b). A player of that
// nation who is connected already (joined before the slot was loaded, or never left: the host loaded the slot again
// after a failed reload) is offered it once; a player who joins later gets it from the hello, as after a drop. (Outside
// a month, e.g. a test battle in planning, only the hello or `ls rejoin` offer it.)
static void HostResumeTick() {
    if (g_role != Role::Host || g_phase != Phase::Processing || lockstep::Active()) return;
    lockstep::RejoinInfo ri;
    if (!lockstep::Rejoinable(ri) || !ri.resumed || ri.pending) return;
    Player* p = PlayerOfNation(ri.opp);
    if (!p || p->offeredSerial == ri.serial) return;
    Log("resume: %s (nation %d) is connected: the resumed battle %d is offered", p->name.c_str(), p->nation, ri.bid);
    HostOfferRejoin(*p);
    p->offeredSerial = ri.serial;  // (once, also when it was not possible: HostOfferRejoin told them why)
}

// A live battle of the host's was resumed from its save (lockstep, phase 5b): it waits for its player. The host reads
// what to do in the chat and the status line, and the Multiplayer window opens (also without a session).
static void OnLiveBattleResumed(const lockstep::RejoinInfo& ri) {
    std::string side1 = ri.sideNames[1].empty() ? "nation " + std::to_string(ri.opp) : ri.sideNames[1];
    std::string head = "The battle loaded from the save is a live battle against " + ri.peerName + " (" + side1 +
                       ", minute " + std::to_string(ri.dropMinute) + "): it waits for " + ri.peerName + ".";
    std::string how;
    if (g_role == Role::Host) {
        bool connected = false;  // (in the lobby the campaign is matched next, and in a month HostResumeTick offers it)
        for (auto& kv : g_players)
            if (kv.second.welcomed && kv.second.connected && kv.second.name == ri.peerName) connected = true;
        how = connected && g_phase != Phase::Planning
                  ? ri.peerName + " is connected and gets " + side1 + " back in a moment."
                  : "When " + ri.peerName + " joins the session" + (connected ? " again" : "") + ", they get " + side1 +
                        " back.";
    } else if (g_role == Role::None) {
        how = "Host the session (Host game in this window), and " + ri.peerName + " gets " + side1 +
              " back when they join.";
    } else {
        how = "Only a session this PC hosts can bring " + ri.peerName + " back (Leave, then Host game).";
    }
    std::string run = "Or press Run in the battle to go on against the computer (" + ri.peerName +
                      " can still rejoin it later).";
    AddChat("* " + head + " " + how + " " + run);
    if (g_role != Role::Host && !ri.slotMode)
        AddLocalChat("* Host the session before this battle ends: the rest of the month then runs with the other "
                     "players' nations (without a session the computer decides for them until the month is over).");
    Notice("Live battle against " + ri.peerName + " resumed: it waits for them. " + how + " " + run);
    if (g_onReveal) g_onReveal();
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
                HostLsPeerGone(m.peer, p->name);
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
            p->battles = KVInt(kv, "battles") != 0;
            p->lsLevel = std::max(0, KVInt(kv, "ls"));  // 0.3.x: "1"
            p->lockstep = p->lsLevel > 0;
            p->mod = KVStr(kv, "mod").substr(0, 24);
            p->relLevel = std::max(0, KVInt(kv, "rel"));  // relations v2 (older clients: none)
            p->genLevel = std::max(0, KVInt(kv, "gen"));  // 0.6.0: the player's battle generator (older clients: none)
            p->divLevel = std::max(0, KVInt(kv, "div"));  // 0.6.1: the nation's own divisions (older clients: none)
            Log("%s: mod %s, live-battle engine %d (this host: %d), player's relation rules %d, battle generator %d, "
                "campaign divisions %d",
                name.c_str(), p->mod.c_str(), p->lsLevel, MP_LS_LEVEL, p->relLevel, p->genLevel, p->divLevel);
            {
                std::string cpu = KVStr(kv, "cpu").substr(0, 16), dh = KVStr(kv, "dh").substr(0, 16);
                std::string mine = datafiles::Ready() ? datafiles::Combined() : "";
                Log("%s: CPU %s (this host: %s); game data files %s", name.c_str(), cpu.empty() ? "?" : cpu.c_str(),
                    x87::CpuVendor().c_str(),
                    dh.empty() || mine.empty() ? "not compared yet" : dh == mine ? "the same as this host's" : "DIFFERENT");
            }
            // A returning player gets their nation back. Once the session runs, the campaign is the host's even while
            // its strategic screen is hidden (a battle).
            if (CampaignLoaded() || (g_phase != Phase::Lobby && g_hostSlot > 0 && game::NationCount() >= 9)) {
                for (auto& r : g_reserved) {
                    if (r.second == name) {
                        p->nation = r.first;
                        p->want = CampaignNationName(r.first);
                    }
                }
            }
            KV w;
            w["peer"] = std::to_string(m.peer);
            w["host"] = g_myName;
            w["name"] = name;
            w["cpu"] = x87::CpuVendor();  // (the player's log names both)
            if (datafiles::Ready()) w["dh"] = datafiles::Combined();
            // 0.6.0: this host's battle requests (1: a declined battle's result is merged; 2: surprise attacks, kind 2,
            // and takeovers at a war's start from a joined player's relation turn; 3 (0.7.0): raider checks in the
            // player's generator's requests, their battles' results and "thwarts" lines merged)
            // 4 (0.7.0): invasions for joined nations (the nation's plan in its turn `kinvt` and in the state `kinv`)
            w["gen"] = inv::Available() ? "4" : "3";
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
            if (p->nation > 0) HostOfferRejoin(*p);  // a live battle that went on without this player: theirs again
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
            if (p->genLevel >= 4 && h.find("kinvt") != h.end() && p->nation >= 1 && p->nation <= 8)
                HostTakeInvasionOrder(p->name, p->nation, U2W(KVStr(h, "kinvt")));
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
            std::string why = p->nation > 0 ? HostDiplo(p->nation, KVStr(kv, "action"), KVInt(kv, "target", -1),
                                                         KVStr(kv, "extra"))
                                            : "Claim a nation first.";
            if (!why.empty()) {
                KV c;
                c["from"] = "*";
                c["text"] = why;
                net::Send(m.peer, net::MSG_CHAT, KVEncode(c));
            }
            break;
        }
        case net::MSG_EVENT:
            if (p && p->welcomed) HostApplyEvent(*p, KVDecode(m.data));
            break;
        case net::MSG_RELATIONS:
            if (p && p->welcomed) HostApplyRelations(*p, KVDecode(m.data));
            break;
        case net::MSG_LEGACY:  // 0.7.0: the player's legacy build (the fleet itself comes with the turn)
            if (p && p->welcomed && p->nation >= 1 && p->nation <= 8) {
                KV kv = KVDecode(m.data);
                std::string st = KVStr(kv, "state"), info = KVStr(kv, "info");
                Log("legacy fleet: %s (nation %d) %s: %s", p->name.c_str(), p->nation, st.c_str(), info.c_str());
                std::string who = W2U(game::NationName(p->nation));
                if (st == "start")
                    AddChat("* " + p->name + " builds the legacy fleet of " + who + " by hand (the computer's starting "
                            "fleet of " + who + " is replaced when the turn is merged).");
                else if (st == "done")
                    AddChat("* " + p->name + " finished the legacy fleet of " + who + ": " + KVStr(kv, "ships") +
                            " ship(s), funds left " + KVStr(kv, "funds") + ".");
            }
            break;
        case net::MSG_BATTLE_RESULT:
            if (p && p->welcomed) HostOnBattleResult(*p, m.data);
            break;
        case net::MSG_LOCKSTEP:
            if (p && p->welcomed) HostOnLockstep(*p, m.data);
            break;
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

// 0.6.1, a month from an older host (it sends the host's own divisions): this nation's own campaign divisions as this
// PC's game last saved them in the slot, if the slot holds this nation's game of this campaign (the nation stored there
// last and a month at most one before the new one). Returns the divisions of other nations left out (those of a 0.6.0
// month), -1 if the slot holds none of this game.
static int OwnSlotDivisions(const std::wstring& dir, int nation, int year, int month, std::vector<std::string>& lines) {
    std::string pi;
    int y = 0, m = 0, foreign = 0;
    if (!ReadFileBytes(dir + L"rtw3mp_player.ini", pi) || KVInt(KVDecode(pi), "nation", -1) != nation) return -1;
    if (!saveio::ReadDate(dir, g_clientSlot, y, m)) return -1;
    int gap = year * 12 + month - (y * 12 + m);
    if (gap < 0 || gap > 1) return -1;
    if (!saveio::OwnDivisionLines(dir, g_clientSlot, nation, lines, foreign)) return -1;
    return foreign;
}

static void ClientApplyPendingState() {
    if (g_pendingState.empty() || game::IsBusy() || game::BattleActive()) return;
    if (g_cb.fighting || g_cb.starting || !g_battleIn.empty()) return;  // a battle slot is in use (see below)
    if (lockstep::Busy() || g_lsLeave) return;                          // a live battle uses the battle slot
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
    // 0.6.1: the nation's own campaign divisions (Fleet organization). A host that keeps them sends them in the month's
    // .off (`divs`); an older host sends its own (its nation's ships): this PC keeps its own from the slot instead.
    bool hostDivs = KVInt(h, "divs", 0) == 1;
    std::vector<std::string> ownDivs;
    int ownForeign = -1;
    // Without the division hook (DivisionsAvailable) the load would resolve the nation's ship ids against nation 0 (nil
    // ships the Fleet organization dereferences): then an empty list, as a host sends to a client without `div`.
    if (!hostDivs && nation > 0 && !dir.empty() && game::DivisionsAvailable())
        ownForeign = OwnSlotDivisions(dir, nation, KVInt(h, "year"), KVInt(h, "month"), ownDivs);
    if (dir.empty() || !saveio::ApplyStateBundle(b, dir, g_clientSlot, err)) {
        Notice("Cannot store the received game: " + err);
        return;
    }
    if (nation > 0) {
        int n = 0;
        std::string sum, derr;
        std::vector<std::string> got;
        std::vector<saveio::CampDiv> mine;
        if (hostDivs) {
            if (saveio::OwnDivisionLines(dir, g_clientSlot, nation, got, n) && saveio::ParseDivisions(got, mine, false, derr))
                Log("divisions: the month holds nation %d's own %d (kept by the host; %d with other ships)", nation,
                    (int)mine.size(), n);
        } else if (saveio::PutSlotDivisions(dir, g_clientSlot, nation, ownForeign >= 0 ? &ownDivs : nullptr, n, sum, derr)) {
            std::string from = ownForeign < 0 ? std::string("none of this game in the slot")
                                              : std::to_string(ownForeign) + " of other nations left out";
            Log("divisions: an older host sends its own; nation %d's own %d from this slot written instead (%s; %s)",
                nation, n, from.c_str(), sum.c_str());
        } else {
            Log("divisions: an older host sends its own; nation %d's own NOT written: %s", nation, derr.c_str());
        }
    }
    {
        KV mk;  // which nation this slot is played as (for "Copy game", also after leaving the session)
        mk["nation"] = std::to_string(nation);
        WriteFileBytes(dir + L"rtw3mp_player.ini", KVEncode(mk));
    }
    g_clientIdStart = saveio::ReadIdNo(dir, g_clientSlot);
    // 0.7.0: the legacy fleet. In a new campaign's first month the host offers (`legacy`) and the player chose it: the
    // nation's generated fleet and designs go before the load, and the game's manual build starts after it. A month
    // loaded during a build ends that build (its ships were only in this copy).
    game::LegacyAbort();
    g_legacyMsgDue = false;
    g_legacyBuiltSeq = -1;
    bool legacyNow = KVInt(h, "legacy", 0) == 1 && g_legacyWanted && nation >= 1 && game::LegacyAvailable();
    if (KVInt(h, "legacy", 0) == 1 && !legacyNow)
        Log("legacy fleet: offered for nation %d, not used (%s)", nation,
            !g_legacyWanted ? "the player's choice is off" : "unavailable in this copy");
    if (legacyNow) {
        std::string sum, lerr;
        if (saveio::ClearNationFleet(dir, g_clientSlot, nation, sum, lerr)) {
            Log("legacy fleet: nation %d cleared for the manual build: %s", nation, sum.c_str());
        } else {
            Log("legacy fleet: cannot clear nation %d (%s); the generated fleet stays", nation, lerr.c_str());
            legacyNow = false;
        }
    }
    // The nation's own war counter (relations v2); a 0.4.1 host sends none and the host's stays in place.
    game::SetOwnWarCounter(KVInt(h, "kwar", INT_MIN));
    // 0.6.0: the host ends the disarmament treaty for a joined nation's war; then this PC ends it at once too (older
    // hosts don't: their treaty stays here as they have it).
    game::SetHostEndsArmsTreaty(KVInt(h, "armstreaty", 0) == 1);
    // Relations v2, B3: the host runs the player's rules for this nation; its relation routines run here this month.
    g_relRulesHost = h.find("rel") != h.end() || h.find("kvp") != h.end();
    g_relTurnSeq = KVInt(h, "rel", 0) >= 1 && game::PlayerTurnAvailable() ? seq : -1;
    if (g_relTurnSeq >= 0) {
        game::PlayerTurnSetup s;
        std::vector<int> will = DecodeArr9(KVStr(h, "kwill")), ts = DecodeArr9(KVStr(h, "kts"));
        for (int j = 0; j < 9; j++) {
            s.will[j] = will[j];
            s.techSharing[j] = ts[j];
        }
        for (auto& e : Split(KVStr(h, "humans"), ',')) {
            int j = atoi(e.c_str());
            if (!e.empty() && j >= 0 && j <= 8) s.human[j] = true;
        }
        for (auto& w : Split(KVStr(h, "khist"), '|')) {
            auto x = Split(w, '.');
            if (x.size() == 3) s.history.push_back({atoi(x[0].c_str()), atoi(x[1].c_str()), atoi(x[2].c_str())});
        }
        s.allyHelpHost = h.find("khelp") != h.end();  // B5 (d): a 0.6.0 host applies the allies' war help
        s.buildWarHost = h.find("kbuild") != h.end();  // B5 (e): ... and the ship seizures of a war
        s.lendDone = KVInt(h, "klend") == 1;
        g_relSetup = s;
    }
    // Relations v2, B4: a B4 host sends this nation's war score with its computer enemies and applies its peace with
    // them (also in a month whose relation turn was applied: proposals); the nations people play stay out of it.
    g_peaceHost = h.find("kvp") != h.end() && game::PeaceAvailable();
    g_peaceStateMonth = KVInt(h, "year") * 12 + KVInt(h, "month");
    g_peacePairs.clear();
    g_peaceProposal = -1;
    if (g_peaceHost) {
        g_relSetup.peace.vp = KVInt(h, "kvp");
        g_relSetup.peace.enemyVP = KVInt(h, "kevp");
        for (auto& e : Split(KVStr(h, "kpairs"), ',')) {
            auto f = Split(e, ':');
            if (f.size() == 3) g_peacePairs[atoi(f[0].c_str())] = {atoi(f[1].c_str()), atoi(f[2].c_str())};
        }
        for (int j = 0; j < 9; j++) g_relSetup.human[j] = false;
        for (auto& e : Split(KVStr(h, "humans"), ',')) {
            int j = atoi(e.c_str());
            if (!e.empty() && j >= 0 && j <= 8) g_relSetup.human[j] = true;
        }
        if (KVInt(h, "kpeace") == 1) g_peaceMonth = g_peaceStateMonth;
        g_relSetup.peace.revolution = h.find("krevo") != h.end();  // B5: a 0.6.0 host
        g_relSetup.peace.revoPending = KVInt(h, "krevo", 0);
    }
    if (!game::LoadCampaign(g_clientSlot, nation)) {
        Notice("Loading the received game failed.");
        return;
    }
    if (legacyNow) {
        std::string info;
        if (game::EnterLegacyBuild(info)) {
            g_legacyMsgDue = true;
            KV lk;
            lk["state"] = "start";
            lk["info"] = info;
            net::Send(0, net::MSG_LEGACY, KVEncode(lk));
        } else {  // (not expected: LegacyAvailable passed) the month as sent, with the generated fleet
            Log("legacy fleet: the build cannot start (%s); the month is loaded again as sent", info.c_str());
            legacyNow = false;
            if (!saveio::ApplyStateBundle(b, dir, g_clientSlot, err) || !game::LoadCampaign(g_clientSlot, nation)) {
                Notice("Loading the received game failed.");
                return;
            }
        }
    }
    {  // 0.6.0 (B6 round 3): the ship-type flags only a processed month sets (this game processes none)
        std::string st = game::RefreshShipTypeFlags();
        if (!st.empty()) Log("month: ship-type flags %s (the game's CheckShipTypesExist on the received game)", st.c_str());
    }
    // "Enemy VP" of the received game is the host's enemies' score; show the points of this nation's enemies ("VP"
    // is the nation's own, kept by the host).
    int enemyVP = 0;
    for (int j = 0; j <= 8 && j < game::NationCount(); j++)
        if (j != nation && game::AtWar(nation, j)) enemyVP += std::max(0, game::NationVP(j));
    game::ShowEnemyVP(enemyVP);
    Log("victory points shown: own %d, enemy %d", game::NationVP(nation), enemyVP);
    // B5: the host's Blockaded flags (this load's own check sees the host's wars, not this nation's).
    if (h.find("kblk") != h.end()) {
        std::string what = warfx::ClientApplyBlockade(KVStr(h, "kblk"));
        Log("warfx: blockade flags from the host (%s): %s", KVStr(h, "kblk").c_str(), what.c_str());
        if (what.rfind("changed", 0) == 0) game::RefreshUI();
    }
    // The player's economy rules the host ran for this nation (economy.cpp): its army offensive months in this PC's
    // budget figures (the game's ArmyOffensive while the host's is 0; this copy's General section is not merged back), and
    // their messages, shown in the turn report before the month's event roll.
    if (int kao = KVInt(h, "kao")) {
        std::string what = economy::ClientArmyOffensive(kao);
        Log("economy: army offensive of nation %d: %d month(s) (ArmyOffensive %s)", nation, kao, what.c_str());
        if (what.rfind("set", 0) == 0) game::RefreshUI();
    }
    // 0.7.0: the nation's invasion plan in this PC's form (the save holds the host player's); an older host keeps none
    g_invPlanFromHost = h.find("kinv") != h.end() && nation >= 1 && nation <= 8;
    if (g_invPlanFromHost) {
        inv::KState one[9];
        inv::DecodeStates(KVStr(h, "kinv").substr(1), one);
        g_invPlan = one[nation];
    }
    ClientInvasionForm(nation, "the month's load");
    g_econNews.clear();
    for (auto& l : Split(KVStr(h, "econnews"), kSepRec))
        if (!l.empty()) g_econNews.push_back(U2W(l));
    g_econNewsSeq = g_econNews.empty() ? -1 : seq;
    g_myNation = nation;
    g_clientSeq = seq;
    g_haveState = true;
    g_submitted = false;
    g_forceSubmitPending = false;
    g_submitForcedByHost = false;
    // A host with per-player intelligence sent this nation's own spending and reports (the header's `intel`).
    nplay::ClientSetMonth(nation, KVInt(h, "intel") != 0, g_clientSlot);
    g_remoteRemaining = KVInt(h, "remaining", -1);
    g_date = DateStr(KVInt(h, "year"), KVInt(h, "month"));
    game::SetTurnButtonCaption(legacyNow ? L"Continue" : L"Submit");
    KV a;
    a["seq"] = std::to_string(seq);
    net::Send(0, net::MSG_ACK, KVEncode(a));
    if (legacyNow)
        Notice("New month " + g_date + ": you command " + KVStr(h, "nationName") +
               ". Build its legacy fleet by hand: design classes and build ships with the game's Design and Build "
               "buttons, then press Continue; next choose the ships under construction and press Continue again. "
               "Then plan the month and press Submit.");
    else
        Notice("New month " + g_date + ": you command " + KVStr(h, "nationName") +
               ". Press Submit when your orders are ready.");
    g_eventRollSeq = seq;  // this month's event roll happens as soon as the game is idle
}

// ---------------------------------------------------------------------------------------------
// Tactical battles of joined players (player side). The battle is fought in a separate battle slot made from the
// month's files with this player's nation as the player nation (the Copy-game transform); its result goes back to the
// host, then the normal slot is loaded again.

static int g_battleInId = -1;

static void SendBattleResult(int id, const std::string& outcome, const std::string& why, const Bundle* b,
                             const std::string& summary) {
    KV h;
    h["id"] = std::to_string(id);
    h["outcome"] = outcome;
    h["why"] = why;
    h["summary"] = summary;
    if (id == g_cb.id && g_cb.kind != 0 && g_cb.cand >= 0) {  // 0.6.0: the attempt that gave it, and its sea area
        h["cand"] = std::to_string(g_cb.cand);
        h["area"] = W2U(g_cb.area);
        if (g_cb.raider) {  // 0.7.0: a raider battle: how it ended and the interception's enemy (real index)
            h["raider"] = std::to_string(g_cb.raider);
            h["ropp"] = std::to_string(g_cb.opp);
        }
    }
    if (id == g_cb.id && !g_cb.news.empty()) {  // 0.7.0: the raider checks' turn messages, for the player's news
        std::string all;
        for (auto& l : g_cb.news) all += l + kSepRec;
        h["news"] = all;
    }
    int dcOwn = 0, dcEnemy = 0;  // 0.6.0: the nation's decline counts after it (the game's, in the battle slot)
    if (id == g_cb.id && g_cb.dc && game::BattleSlotMode() && game::GetDeclineCounts(dcOwn, dcEnemy))
        h["dc"] = std::to_string(dcOwn) + ":" + std::to_string(dcEnemy);
    if (id == g_cb.id && g_cb.inv && game::BattleSlotMode() && outcome != "failed") {
        // 0.7.0 (invasions, P3): the nation's invasions and plan as the battle slot's game left them
        inv::KState one[9];
        std::vector<inv::PossState> ps;
        inv::SlotRead(g_cb.K, one[g_cb.K], ps);
        h["kinv"] = "+" + inv::EncodeStates(one);
        h["kinvp"] = inv::EncodePoss(ps);
        if (g_cb.iopp >= 0) {
            h["iopp"] = std::to_string(g_cb.iopp);
            h["iatt"] = g_cb.iatt ? "1" : "0";
        }
        Log("battle %d: the nation's invasions after it: plan %s/%d, enemy plan %s/%d, %d invasion(s), %d mark(s)", id,
            one[g_cb.K].target.empty() ? "-" : W2U(one[g_cb.K].target).c_str(), one[g_cb.K].count,
            one[g_cb.K].enemyTarget.empty() ? "-" : W2U(one[g_cb.K].enemyTarget).c_str(), one[g_cb.K].pending,
            (int)ps.size(), (int)one[g_cb.K].itt.size());
    }
    net::Send(0, net::MSG_BATTLE_RESULT, PackHB(KVEncode(h), b ? b->Pack() : std::string()));
    Log("battle %d: %s%s%s", id, outcome.c_str(), why.empty() ? "" : " - ", why.c_str());
}

static void LeaveBattleSlot() {
    game::SetBattleSlotMode(false);
    g_cb.fighting = g_cb.starting = false;
    g_cb.id = -1;
    std::wstring own = game::SaveDir(g_clientSlot) + L"RTWGame" + std::to_wstring(g_clientSlot) + L".bcs";
    if (g_myNation > 0 && FileExists(own) && !game::LoadCampaign(g_clientSlot, g_myNation))
        Notice("Loading your game again failed; it loads with the next month.");
    else if (g_myNation > 0 && FileExists(own)) {
        game::RefreshShipTypeFlags();  // (0.6.0: the month's own flags, as after the month's load)
        ClientInvasionForm(g_myNation, "back from the battle slot");
    }
}

// The session ended (left, disconnected): forget queued battles. A battle being fought goes on as a single-player
// battle in the battle slot; Tick ends the battle slot mode when it is over.
static void ClientDropBattle() {
    if (g_cb.fighting || g_cb.starting) {
        g_cb.cancelled = true;
        Log("battle %d: the session ended during the battle", g_cb.id);
    }
    g_cb.fighting = g_cb.starting = false;
    g_cb.id = -1;
    g_battleIn.clear();
    g_battleInId = -1;
}

// 0.6.1: an older host sends its own divisions with a battle (its nation's ships; MakeNationPlayer drops them): the
// battle slot gets this nation's own instead, as this PC's game saved them in its slot (a 0.6.1 host sends them: `divs`).
// Never fails the battle.
static bool BattleSlotDivisions(const KV& h, const std::wstring& dir, int K) {
    if (h.find("divs") != h.end()) return true;
    std::vector<std::string> own;
    int foreign = 0, n = 0;
    std::string sum, err;
    bool have = saveio::OwnDivisionLines(game::SaveDir(g_clientSlot), g_clientSlot, K, own, foreign);
    if (saveio::PutSlotDivisions(dir, g_battleSlot, K, have ? &own : nullptr, n, sum, err))
        Log("battle: an older host sends its own divisions; nation %d's own %d from its slot instead (%s)", K, n,
            sum.c_str());
    else
        Log("battle: an older host sends its own divisions; nation %d's own NOT written: %s", K, err.c_str());
    return true;
}

static void ClientStartBattle() {
    std::string payload;
    payload.swap(g_battleIn);
    g_battleInId = -1;
    KV h;
    std::string body;
    Bundle b;
    if (!UnpackHB(payload, h, body) || !b.Unpack(body)) {
        Notice("Received a damaged battle from the host.");
        return;
    }
    int id = KVInt(h, "id", -1), K = KVInt(h, "nation", -1), opp = KVInt(h, "opp", -1);
    int a = KVInt(h, "a", -1), bb = KVInt(h, "b", -1);
    std::wstring area = U2W(KVStr(h, "area"));
    if (K != g_myNation || K < 1 || opp < 1 || opp > 8 || opp == K) {
        SendBattleResult(id, "failed", "not this player's nation", nullptr, "");
        return;
    }
    if (!game::BattlesAvailable()) {
        SendBattleResult(id, "failed", "battles are not available with this game version", nullptr, "");
        return;
    }
    std::wstring dir = game::SaveDir(g_battleSlot);
    std::string err, summary;
    if (dir.empty() || !saveio::ApplyStateBundle(b, dir, g_battleSlot, err) || !BattleSlotDivisions(h, dir, K) ||
        !saveio::MakeNationPlayer(dir, g_battleSlot, K, {}, summary, err, game::OwnWarCounter()) ||
        !ReadFileBytes(dir + L"RTWGame" + std::to_wstring(g_battleSlot) + L".bcs", g_cb.before)) {
        SendBattleResult(id, "failed", "cannot prepare the battle slot: " + err, nullptr, "");
        return;
    }
    g_cb.id = id;
    g_cb.K = K;
    g_cb.opp = opp;
    g_cb.area = area;
    g_cb.cancelled = false;
    g_cb.starting = true;
    if (!game::LoadCampaign(g_battleSlot, 0)) {
        SendBattleResult(id, "failed", "cannot load the battle slot", nullptr, "");
        LeaveBattleSlot();
        return;
    }
    game::SetBattleSlotMode(true);
    // 0.6.0 (B6 round 3): the month's state the slot lacks (ship-type flags, the month's resets; game.cpp)
    Log("battle %d: the month's state in the battle slot: %s", id, game::PrepareBattleSlotMonth().c_str());
    auto sw = [K](int v) { return v == 0 ? K : v == K ? 0 : v; };
    // The attempts: the game's call for a battle the computer's generator found (kind 0), or the player's generator's
    // attempts for this nation (kind 1, B6), replayed in order until one gives a battle, as the game's generator goes on
    // after a CheckAttackLocation that found none.
    g_cb.kind = KVInt(h, "kind", 0);
    g_cb.cand = -1;
    g_cb.dc = false;
    {  // 0.6.0: the nation's own decline counts in place of the host player's (the slot is the host's campaign)
        auto f = Split(KVStr(h, "dc"), ':');
        int own0 = 0, enemy0 = 0;
        if (f.size() == 2 && !f[0].empty() && !f[1].empty() && game::GetDeclineCounts(own0, enemy0)) {
            int own = atoi(f[0].c_str()), enemy = atoi(f[1].c_str());
            g_cb.dc = game::SetDeclineCounts(own, enemy);
            if (g_cb.dc)
                Log("battle %d: the nation's decline counts %d/%d in the battle slot (the save's were %d/%d)", id, own,
                    enemy, own0, enemy0);
        }
    }
    std::vector<game::BattleAttempt> atts;
    std::vector<int> humans;  // 0.7.0: the nations people play, in this slot's indices (a raider check's mask)
    for (auto& s : Split(KVStr(h, "humans"), ','))
        if (!s.empty() && atoi(s.c_str()) >= 0 && atoi(s.c_str()) <= 8) humans.push_back(sw(atoi(s.c_str())));
    g_cb.raider = 0;
    g_cb.news.clear();
    g_cb.inv = false;
    g_cb.iopp = -1;
    g_cb.iatt = true;
    if (h.find("kinv") != h.end() && inv::Available()) {
        // 0.7.0 (invasions, P3): the nation's invasions and plan in place of the host's (the slot is the host's campaign)
        inv::KState one[9];
        inv::DecodeStates(KVStr(h, "kinv").substr(1), one);
        Log("battle %d: the nation's invasions in the battle slot: %s", id,
            inv::SlotPut(K, one[K], inv::DecodePoss(KVStr(h, "kinvp"))).c_str());
        g_cb.inv = true;
    }
    if (g_cb.kind == 1 || g_cb.kind == 2) {
        int n = std::min(64, KVInt(h, "att", 0));
        for (int i = 0; i < n; i++) {
            std::string s = KVStr(h, ("att" + std::to_string(i)).c_str());
            size_t c1 = s.find(','), c2 = c1 == std::string::npos ? c1 : s.find(',', c1 + 1);
            game::BattleAttempt t;
            if (s == "R" && g_cb.kind == 1 && game::RaiderReplayAvailable()) {  // 0.7.0: a raider check
                t.type = 1;
                t.a = K;
                atts.push_back(t);
                continue;
            }
            if (s == "I" && g_cb.kind == 1 && g_cb.inv && game::InvasionReplayAvailable()) {  // 0.7.0: the invasion check
                t.type = 2;
                t.a = K;
                atts.push_back(t);
                continue;
            }
            if (c2 != std::string::npos) {
                t.a = atoi(s.c_str());
                t.b = atoi(s.c_str() + c1 + 1);
                t.area = U2W(s.substr(c2 + 1));
            }
            bool sane = t.a >= 0 && t.a <= 8 && t.b >= 0 && t.b <= 8 && (t.a == K) != (t.b == K);
            atts.push_back(sane ? t : game::BattleAttempt());  // (kept so the indices stay the host's)
        }
    } else {
        game::BattleAttempt t;
        t.a = a;
        t.b = bb;
        t.area = area;
        atts.push_back(t);
        AddChat("* Battle! " + W2U(game::NationName(0)) + " meets " + KVStr(h, "oppName") + " in " + W2U(area) + ".");
    }
    int setups = game::DoBattleCalls(), declines = game::PlayerDeclineCalls();
    int vp0 = game::NationVP(0);  // the player's nation is nation 0 in the battle slot
    if (g_cb.kind == 2) {  // 0.6.0 (B6 piece 2): a surprise attack; the battle slot's year decides the game's path
        int y = 0, mo = 0;
        saveio::ReadDate(dir, g_battleSlot, y, mo);
        if (y < 1896 || (y >= 1925 && y <= 1934)) {
            std::string why = y < 1896 ? "no surprise battle before 1896"
                                       : "a surprise attack of 1925-1934 is a submarine attack without a battle";
            Log("battle %d: surprise attack not played (%d: %s)", id, y, why.c_str());
            SendBattleResult(id, "nobattle", why, nullptr, "");
            AddChat("* No surprise attack (" + why + ").");
            LeaveBattleSlot();
            return;
        }
    }
    bool started = false, saTrue = false;
    int played = 0;  // kind 2: the attempts CheckSurpriseAttack was asked for (the others' war was over)
    for (int i = 0; i < (int)atts.size(); i++) {
        const game::BattleAttempt& t = atts[i];
        if (t.a < 0) continue;
        g_cb.cand = i;
        g_cb.opp = t.a == K ? t.b : t.a;
        g_cb.area = t.area;
        if (g_cb.kind == 2) {
            if (!game::AtWar(sw(t.a), sw(t.b))) {  // e.g. a peace came first: no surprise attack between them
                Log("battle %d: attempt %d (%d strikes %d): not at war in the battle slot", id, i, t.a, t.b);
                continue;
            }
            played++;
            // The game's CheckSurpriseAttack: True when it set a battle up or the player let the chance go (its map
            // message), False when it found no opportunity (its random gates, no mission, the attacker declined).
            // Tests (`battles saretry n`): a False with nothing else happening is asked again up to n times.
            for (int tries = 0;; tries++) {
                saTrue = game::StartSurpriseAttack(sw(t.a), sw(t.b));
                if (g_role != Role::Client || g_cb.id != id || saTrue || game::BattleActive() ||
                    game::DoBattleCalls() != setups || game::PlayerDeclineCalls() != declines ||
                    game::NationVP(0) != vp0 || tries >= g_testSaRetry)
                    break;
                Log("battle %d: test: CheckSurpriseAttack asked again (%d of %d)", id, tries + 1, g_testSaRetry);
            }
            started = saTrue;
        } else if (t.type == 2) {  // 0.7.0 (P3): the game's invasion check as this nation, with its invasions
            inv::PossBattle pb;
            long pb0 = inv::PossessionBattles(nullptr);
            int d0 = game::PlayerDeclineCalls(), s0 = game::DoBattleCalls(), v0 = game::NationVP(0);
            game::InvasionCheck ic = game::StartInvasionCheck(humans);
            for (auto& l : ic.news) g_cb.news.push_back(W2U(l));
            bool any = inv::PossessionBattles(&pb) != pb0;
            bool gave = ic.result || game::BattleActive() || game::DoBattleCalls() != s0 ||
                        game::PlayerDeclineCalls() != d0 || game::NationVP(0) != v0;
            if (any && gave) {
                int e = pb.attacker == 0 ? pb.defender : pb.attacker;  // the enemy, this slot's index
                g_cb.iopp = e >= 0 ? sw(e) : -1;
                g_cb.iatt = pb.attacker == 0;
                g_cb.opp = g_cb.iopp;
                g_cb.area = pb.area;
                Log("battle %d: attempt %d (invasion check): %s at %s (%s) against nation %d%s; possession battles: %s%s",
                    id, i,
                    ic.result || game::BattleActive()     ? "a battle"
                    : game::PlayerDeclineCalls() != d0     ? "the player's decline"
                    : game::NationVP(0) != v0              ? "no battle (the points changed)"
                                                           : "a battle set up",
                    W2U(pb.poss).c_str(), W2U(pb.area).c_str(), g_cb.iopp,
                    g_cb.iatt ? " (our invasion)" : " (the enemy's invasion)", inv::PossessionBattlesText(pb0).c_str(),
                    ic.plain >= 0 ? (" (TEST: plain, " + std::to_string(ic.plain) + " answer(s))").c_str() : "");
            } else if (!gave && g_role == Role::Client && g_cb.id == id) {
                Log("battle %d: attempt %d (invasion check) gave no battle (possession battles: %s; %d turn message(s))",
                    id, i, inv::PossessionBattlesText(pb0).c_str(), (int)ic.news.size());
                continue;
            }
            started = ic.result;
        } else if (t.type == 1) {  // 0.7.0: the game's raider check as this nation, the nations people play masked
            int q0 = game::QuickRaiderCalls();
            game::RaiderCheck rc = game::StartRaiderCheck(humans, g_testRaid.side, g_testRaid.J >= 0 ? sw(g_testRaid.J) : -1);
            if (!rc.test.empty()) g_testRaid = {};
            for (auto& l : rc.news) g_cb.news.push_back(W2U(l));
            int ropp = rc.pending ? sw(rc.enemy) : -1;
            bool sane = ropp >= 1 && ropp <= 8 && ropp != K &&
                        std::find(humans.begin(), humans.end(), rc.enemy) == humans.end();
            started = rc.result && sane;
            if (rc.result && !sane) Log("battle %d: attempt %d: a raider battle with nation %d (not a computer enemy)", id, i, ropp);
            if (started) {
                g_cb.opp = ropp;
                g_cb.area = rc.area;
                g_cb.raider = game::QuickRaiderCalls() != q0 ? 2 : 1;
                Log("battle %d: attempt %d (raider check): %s raider battle against nation %d in %s (%s)", id, i,
                    rc.side == 0 ? "our raider intercepted:" : "our patrol intercepts:", ropp, W2U(rc.area).c_str(),
                    g_cb.raider == 2 ? "auto-resolved" : "fought");
            } else if (g_role == Role::Client && g_cb.id == id) {
                Log("battle %d: attempt %d (raider check) found no interception (%d turn message(s))", id, i,
                    (int)rc.news.size());
                continue;
            }
        } else {
            started = game::StartPlayerBattle(sw(t.a), sw(t.b), t.area);
        }
        if (g_role != Role::Client || g_cb.id != id) break;
        if (started || game::BattleActive() || game::DoBattleCalls() != setups ||
            game::PlayerDeclineCalls() != declines || game::NationVP(0) != vp0)
            break;  // a battle, a decline (the player's or the enemy's) or its points: this attempt is the month's
        Log("battle %d: attempt %d (%d attacks %d in %s) found no battle", id, i, t.a, t.b,
            t.area.empty() ? "an area the game picks" : W2U(t.area).c_str());
    }
    bool entered = game::DoBattleCalls() != setups;  // the game set the battle up (its preview was shown)
    bool declinedHere = game::PlayerDeclineCalls() != declines;  // the player declined (the question or the preview)
    if (entered && !game::LastBattleArea().empty()) g_cb.area = game::LastBattleArea();
    opp = g_cb.opp;
    area = g_cb.area;
    if (g_role != Role::Client || g_cb.id != id) {  // the session ended during the battle preview
        Log("battle %d: the session ended while it started (%s)", id, started ? "fought as single player" : "no battle");
        return;
    }
    g_cb.starting = false;
    if (g_cb.kind == 1 && started && game::BattleActive())
        AddChat(std::string(g_cb.raider ? "* Raider battle! " : g_cb.iopp >= 0 ? "* Invasion battle! " : "* Battle! ") +
                W2U(game::NationName(0)) + " meets " +
                W2U(game::NationName(opp)) + (area.empty() ? "" : " in " + W2U(area)) + ".");
    if (g_cb.kind == 2 && started && game::BattleActive())
        AddChat("* Surprise attack! " + W2U(game::NationName(0)) + " meets " + W2U(game::NationName(opp)) +
                (area.empty() ? "" : " in " + W2U(area)) + ".");
    if (started && g_cb.raider == 2) {  // 0.7.0: the player chose "Auto resolve": QuickResolveRaiderBattle ran here
        std::string after, summary, err;
        Bundle res;
        game::SaveGame();
        if (ReadFileBytes(game::SaveDir(g_battleSlot) + L"RTWGame" + std::to_wstring(g_battleSlot) + L".bcs", after) &&
            saveio::ExtractBattleResult(g_cb.before, after, K, opp, res, summary, err)) {
            SendBattleResult(id, "fought", "", &res, summary);
            AddChat("* Raider battle against the " + W2U(game::NationName(opp)) + " navy" +
                    (area.empty() ? "" : " in " + W2U(area)) + " auto-resolved; result sent to the host (" + summary + ").");
        } else {
            SendBattleResult(id, "failed", "cannot read the result: " + err, nullptr, "");
        }
        LeaveBattleSlot();
        return;
    }
    if (!started || !game::BattleActive()) {
        bool declined = declinedHere || (entered && !game::BattleActive());
        int gain = game::NationVP(0) - vp0;
        bool pvp = KVInt(h, "pvp") != 0;
        std::string resolves = g_cb.kind == 0 ? "; the game resolves it." : ".";
        if (!entered && !declinedHere && gain > 0 && !pvp) {
            // The enemy declined battle (the game's AIDeclinesBattle before any preview: "The <nation> navy declines
            // battle! You gain N VP!"): as in single player there is no battle and the player keeps the points. Before
            // 0.4.1 this was "no battle came about" and the host let the game resolve it as a battle between AI nations.
            std::string after, summary, err;
            Bundle res;
            game::SaveGame();
            if (ReadFileBytes(game::SaveDir(g_battleSlot) + L"RTWGame" + std::to_wstring(g_battleSlot) + L".bcs", after) &&
                saveio::ExtractBattleResult(g_cb.before, after, K, opp, res, summary, err)) {
                SendBattleResult(id, "avoided", "", &res, summary);
                AddChat("* The " + W2U(game::NationName(opp)) + " navy declined battle: " + W2U(game::NationName(0)) +
                        " gains " + std::to_string(gain) + " VP.");
            } else {
                SendBattleResult(id, "nobattle", "cannot read the result: " + err, nullptr, "");
                AddChat("* No battle came about" + resolves);
            }
            LeaveBattleSlot();
            return;
        }
        if (declined && !pvp && g_hostGen >= 1) {
            // 0.6.0 (risk 4): the player declined; the game's consequences here (DoPlayerDecline: the enemy's points,
            // prestige, morale, the decline counts) go to the host with the decline, which merges them as for a battle.
            // A host without it (welcome `gen`; 0.5.0) gets the decline alone and lets the game resolve the battle.
            std::string after, summary, err;
            Bundle res;
            game::SaveGame();
            bool ok = ReadFileBytes(game::SaveDir(g_battleSlot) + L"RTWGame" + std::to_wstring(g_battleSlot) + L".bcs",
                                    after) &&
                      saveio::ExtractBattleResult(g_cb.before, after, K, opp, res, summary, err);
            SendBattleResult(id, "declined", ok ? "" : "cannot read the result: " + err, ok ? &res : nullptr, summary);
            AddChat("* You declined the battle" + (ok ? " (" + summary + ")." : resolves));
            LeaveBattleSlot();
            return;
        }
        if (g_cb.kind == 2 && !declined) {  // 0.6.0: a surprise attack that did not come about (the host does nothing)
            const game::BattleAttempt* t = g_cb.cand >= 0 && g_cb.cand < (int)atts.size() ? &atts[g_cb.cand] : nullptr;
            bool mine = t && t->a == K;
            std::string why = !played            ? "the war is over"
                              : saTrue && !entered ? "the player let the chance go"
                              : mine             ? "no opportunity found for the surprise attack"
                                                 : "the surprise attack did not come about";
            SendBattleResult(id, "nobattle", why, nullptr, "");
            AddChat(!played              ? "* No surprise attack: the war is over."
                    : saTrue && !entered ? "* You let the chance of a surprise attack go."
                    : mine               ? "* No opportunity found for a surprise attack."
                                         : "* No surprise attack came about.");
            LeaveBattleSlot();
            return;
        }
        SendBattleResult(id, declined ? "declined" : "nobattle", "", nullptr, "");
        AddChat((declined ? "* You declined the battle" : "* No battle came about") + resolves);
        LeaveBattleSlot();
        return;
    }
    g_cb.fighting = true;
    Log("battle %d: fighting nation %d vs %d in %s (slot %d)", id, K, opp, W2U(area).c_str(), g_battleSlot);
    if (KVInt(h, "pvp") && g_lsEnabled && lockstep::Available()) {  // the other side is a player: a live battle
        g_lsClientA = {};
        g_lsClientA.pending = true;
        g_lsClientA.bid = id;
        g_lsClientA.opp = opp;
        g_lsClientA.p2 = KVStr(h, "p2", "the other player");
        g_lsClientA.autoStrike = KVInt(h, "autostrike") != 0;
        g_lsClientA.since = GetTickCount();
        g_lsRole = 'A';
        lockstep::Hold("waiting for " + g_lsClientA.p2 + " to join the battle", g_lsClientA.bid);
        if (!lockstep::IsHolding()) {
            g_lsClientA.pending = false;
            g_lsRole = 0;
        }
    }
}

// Client A of a live battle between two players' nations: start it once the battle screen is up and idle.
static void ClientTryStartLockstep() {
    if (!g_lsClientA.pending || !lockstep::IsHolding()) {
        g_lsClientA.pending = false;
        return;
    }
    g_lsClientA.lastTry = GetTickCount();
    lockstep::StartInfo si;
    si.bid = g_lsClientA.bid;
    si.slot = g_battleSlot;
    si.opp = g_lsClientA.opp;
    si.autoStrike = g_lsClientA.autoStrike;
    si.myName = g_myName;
    si.peerName = g_lsClientA.p2;
    si.sideNames[0] = W2U(game::NationName(0));  // the battle slot: this player's nation is nation 0
    si.sideNames[1] = W2U(game::NationName(g_lsClientA.opp));
    si.send = [](const std::string& s) { net::Send(0, net::MSG_LOCKSTEP, s); };
    std::string err;
    bool retry = false;
    if (lockstep::StartAsA(si, err, retry)) {
        g_lsClientA.pending = false;
        return;
    }
    if (retry && GetTickCount() - g_lsClientA.since < 60000) return;
    if (retry) lockstep::ReleaseHold("the game stayed busy");
    g_lsClientA.pending = false;
    g_lsRole = 0;
    Log("battle %d: live start failed (%s); fought against the AI", g_lsClientA.bid, err.c_str());
}

static void ClientBattleTick() {
    if (g_cb.fighting) {
        if (game::BattleActive() || game::IsBusy() || lockstep::Busy()) return;
        // The battle screen and the dialogs after it are closed.
        int id = g_cb.id;
        if (g_cb.cancelled) {
            Log("battle %d: finished after the host resolved it; result not sent", id);
        } else if (!game::BattleAftermathSeen()) {
            SendBattleResult(id, "failed", "the battle was left before its end", nullptr, "");
            AddChat(std::string("* The battle was left before its end") + (g_cb.kind == 0 ? "; the game resolves it." : "."));
        } else {
            std::string after, summary, err;
            Bundle res;
            game::SaveGame();
            if (ReadFileBytes(game::SaveDir(g_battleSlot) + L"RTWGame" + std::to_wstring(g_battleSlot) + L".bcs", after) &&
                saveio::ExtractBattleResult(g_cb.before, after, g_cb.K, g_cb.opp, res, summary, err)) {
                SendBattleResult(id, "fought", "", &res, summary);
                AddChat("* Battle result sent to the host (" + summary + ").");
            } else {
                SendBattleResult(id, "failed", "cannot read the result: " + err, nullptr, "");
            }
        }
        LeaveBattleSlot();
        return;
    }
    if (!g_battleIn.empty() && !g_cb.starting && !game::IsBusy() && !game::BattleActive() && !game::EventActive() &&
        !lockstep::Busy() && !g_lsLeave)
        ClientStartBattle();
}

// ---------------------------------------------------------------------------------------------
// Client: random events. The game only rolls them for the host's nation; a joined player's events are rolled and
// answered here, and the host applies the results (game::RunClientEvent explains how).

static std::wstring EventCooldownFile() {
    uint32_t h = 2166136261u;
    for (char c : g_evUsedKey) h = (h ^ (uint8_t)c) * 16777619u;
    char buf[32];
    snprintf(buf, sizeof(buf), "events_%08x.ini", h);
    return ModDataDir() + U2W(buf);
}

static void SaveEventCooldowns() {
    KV kv;
    kv["campaign"] = g_evUsedKey;
    kv["month"] = std::to_string(g_evUsedMonth);
    kv["rolled"] = std::to_string(g_evRolledMonth);
    std::string used;
    for (int u : g_evUsed) used += std::to_string(u) + ",";
    kv["used"] = used;
    WriteFileBytes(EventCooldownFile(), KVEncode(kv));
}

static int ClientMonth() {
    int y = 0, m = 0;
    return saveio::ReadDate(game::SaveDir(g_clientSlot), g_clientSlot, y, m) ? y * 12 + m : 0;
}

// Loads this nation's cooldowns for the open campaign and counts them down to `month`.
static void UpdateEventCooldowns(int month) {
    std::string key = CampaignKey() + "|nation" + std::to_string(g_myNation);
    if (key != g_evUsedKey) {
        g_evUsedKey = key;
        memset(g_evUsed, 0, sizeof(g_evUsed));
        g_evUsedMonth = month;
        g_evRolledMonth = 0;
        std::string s;
        if (ReadFileBytes(EventCooldownFile(), s)) {
            KV kv = KVDecode(s);
            if (KVStr(kv, "campaign") == key) {
                auto f = Split(KVStr(kv, "used"), ',');
                for (size_t i = 0; i < f.size() && i < 100; i++) g_evUsed[i] = atoi(f[i].c_str());
                g_evUsedMonth = KVInt(kv, "month", month);
                g_evRolledMonth = KVInt(kv, "rolled", 0);
            }
        }
    }
    int elapsed = month - g_evUsedMonth;
    if (elapsed > 0 && elapsed < 1200)
        for (int& u : g_evUsed) u = std::max(0, u - elapsed);
    g_evUsedMonth = month;
}

// The other branch of the game's monthly event slot (CheckTurnEvents: about one month in three a random event, else
// the player's intelligence check): the player's spying as the game does it for its player, with this nation seen as
// the player (nationplay.cpp). The results go to the host at once, as an event's do.
static void ClientIntelCheck() {
    if (!nplay::ClientIntelActive()) return;
    KV kv;
    std::vector<std::string> reports;
    std::string summary;
    int seq = g_clientSeq;
    bool send = nplay::ClientRunIntel(g_myNation, kv, reports, summary);
    Log("intel: monthly check for nation %d: %s", g_myNation, summary.c_str());
    for (auto& r : reports) AddLocalChat("* Intelligence report: " + r);
    if (!send) return;
    kv["seq"] = std::to_string(seq);
    kv["nation"] = std::to_string(g_myNation);
    kv["event"] = "-1";
    kv["caption"] = "Intelligence";
    net::Send(0, net::MSG_EVENT, KVEncode(kv));
    game::RefreshUI();
}

// forcedIdx >= 0: that event now (tests). Otherwise this month's roll: once per calendar month, even if the month
// is sent again (rejoin, nation pick, a turn that didn't advance).
static void ClientRunEvent(int forcedIdx) {
    if (g_role != Role::Client || g_myNation <= 0 || !g_haveState || game::EventActive()) return;
    int month = ClientMonth();
    if (month <= 0) {
        Log("event: cannot read the date; no roll this time");
        return;
    }
    UpdateEventCooldowns(month);
    if (forcedIdx < 0) {
        if (g_evRolledMonth == month) {
            Log("event: this month was already rolled");
            return;
        }
        g_evRolledMonth = month;
        SaveEventCooldowns();
        if (std::uniform_int_distribution<int>(0, 99)(g_rng) >= g_eventChancePct) {
            ClientIntelCheck();
            g_lastEvent = "none";
            Log("event: no event this month");
            return;
        }
    } else {
        g_evRolledMonth = month;
    }
    int seq = g_clientSeq;
    game::ClientEventResult r = game::RunClientEvent(g_myNation, forcedIdx, g_evUsed);
    SaveEventCooldowns();
    if (!r.error.empty()) {
        g_lastEvent = "error " + r.error;
        Log("event: %s", r.error.c_str());
        return;
    }
    if (!r.shown) {
        g_lastEvent = "none";
        Log("event: no event this month");
        return;
    }
    g_evShownSeq = seq;  // relations v2: no random tension changes in a month with an event (the game's own chain)
    KV kv;
    kv["seq"] = std::to_string(seq);
    kv["nation"] = std::to_string(g_myNation);
    kv["event"] = std::to_string(r.eventIdx);
    std::string cap = W2U(r.caption);
    if (cap.size() > 160) cap.resize(160);
    for (char& c : cap)
        if (c == '\n' || c == '\r') c = ' ';
    kv["caption"] = cap;
    std::string ten, ally, delta, set;
    for (int i = 0; i < 9; i++) {
        if (r.tension[i]) ten += std::to_string(i) + ":" + std::to_string(r.tension[i]) + ";";
        if (r.alliance[i] >= 0) ally += std::to_string(i) + ":" + std::to_string(r.alliance[i]) + ";";
    }
    for (auto& d : r.fieldDelta) delta += d.first + ":" + std::to_string(d.second) + ";";
    for (auto& s : r.fieldSet) set += s.first + ":" + std::to_string(s.second) + ";";
    kv["tension"] = ten;
    kv["alliance"] = ally;
    kv["delta"] = delta;
    kv["set"] = set;
    net::Send(0, net::MSG_EVENT, KVEncode(kv));
    g_lastEvent = "event=" + std::to_string(r.eventIdx) + " tension=" + ten + " alliance=" + ally + " delta=" + delta +
                  " set=" + set;
    Log("event %d for nation %d: %s | %s", r.eventIdx, g_myNation, cap.substr(0, 60).c_str(), g_lastEvent.c_str());
    game::RefreshUI();
}

// The relation changes of RunPlayerTurn for the host (MSG_RELATIONS); `summary` gets the log's text.
static KV RelationsKV(const game::PlayerTurnResult& r, int seq, std::string& summary) {
    KV kv;
    kv["seq"] = std::to_string(seq);
    kv["nation"] = std::to_string(g_myNation);
    std::string ten, ally, will, ts, budget;
    for (auto& c : r.tension)
        ten += std::to_string(c.a) + ":" + std::to_string(c.b) + ":" + std::to_string(c.before) + ":" +
               std::to_string(c.after) + ";";
    for (auto& c : r.alliance)
        ally += std::to_string(c.a) + ":" + std::to_string(c.b) + ":" + std::to_string(c.before) + ":" +
                std::to_string(c.after) + ";";
    for (int j = 0; j < 9; j++) {
        will += (j ? "." : "") + std::to_string(r.will[j]);
        ts += (j ? "." : "") + std::to_string(r.techSharing[j]);
    }
    for (auto& b : r.budget) budget += std::to_string(b.first) + ":" + std::to_string(b.second) + ";";
    kv["tension"] = ten;
    kv["alliance"] = ally;
    kv["will"] = will;
    kv["ts"] = ts;
    kv["budget"] = budget;
    kv["vp"] = std::to_string(r.vpDelta);
    kv["kwar"] = std::to_string(r.warCounter);
    {
        std::string own;  // the nation's own fields the routines changed (the host's campaign takes them at once)
        for (auto& d : r.self) own += d.first + ":" + std::to_string(d.second) + ";";
        kv["self"] = own;
    }
    kv["ran"] = std::string(r.ranTension ? "tension," : "") + (r.ranPreviousWars ? "previous wars," : "") +
                (r.ranWar ? "war" : "") + (r.ranAllyHelp ? ",ally help" : "") + (r.ranBuildWar ? ",building for war" : "");
    kv["msgs"] = std::to_string(r.messages.size());
    // B5 (d): the allies' war help (0.5.0 hosts ignore these keys; they only get it from a 0.6.0 host's `khelp`)
    if (r.ranAllyHelp) {
        auto T = [](const std::wstring& w) { return SafeText(W2U(w)); };
        auto F = [](const std::string& x) { return x + kSepField; };
        std::string fu, sh;
        for (auto& d : r.allyFunds) fu += std::to_string(d.first) + ":" + std::to_string(d.second) + ";";
        for (auto& s : r.allyShips)
            sh += F(std::to_string(s.id)) + F(std::to_string(s.from)) + F(std::to_string(s.to)) + F(T(s.name)) +
                  F(T(s.reason)) + T(s.renamed) + kSepRec;
        kv["ally"] = "1";
        kv["allyvp"] = std::to_string(r.allyVP);
        kv["afunds"] = fu;
        kv["aships"] = sh;
        kv["lend"] = r.lendDone ? "1" : "0";
    }
    // B5 (e): the ship seizures (0.5.0 hosts ignore the keys; they only get them from a 0.6.0 host's `kbuild`)
    if (r.ranBuildWar) {
        auto T = [](const std::wstring& w) { return SafeText(W2U(w)); };
        auto F = [](const std::string& x) { return x + kSepField; };
        std::string sh;
        for (auto& s : r.buildShips)
            sh += F(std::to_string(s.id)) + F(std::to_string(s.from)) + F(std::to_string(s.to)) + F(T(s.name)) +
                  F(T(s.reason)) + T(s.renamed) + kSepRec;
        kv["bwar"] = "1";
        kv["bships"] = sh;
    }
    summary = "tension=" + ten + " alliance=" + ally + " will=" + will + " budget=" + budget + " vp=" +
              std::to_string(r.vpDelta) + " kwar=" + std::to_string(r.warCounter) + " ran=" + kv["ran"] + " msgs=" +
              kv["msgs"];
    // 0.6.0 (B6 piece 2, a gen 2 host): the turn's surprise attacks ("a,b;" in DoSurpriseAttack's order) and the unowned
    // possessions taken over (pland's layout: name, from -1, to, removeCA 0)
    if (!r.surprise.empty() || !r.takeovers.empty()) {
        std::string sa, tk;
        for (auto& t : r.surprise) sa += std::to_string(t.a) + "," + std::to_string(t.b) + ";";
        for (auto& l : r.takeovers)
            tk += SafeText(W2U(l.name)) + kSepField + std::to_string(l.from) + kSepField + std::to_string(l.to) +
                  kSepField + "0" + kSepRec;
        kv["ksa"] = sa;
        kv["sland"] = tk;
        summary += " ksa=" + sa + " sland=" + std::to_string(r.takeovers.size());
    }
    if (!r.landings.empty()) {  // 0.7.0 (P4, a gen 4 host): the surprise landings in this nation's possessions
        std::string kl;
        for (auto& l : r.landings) kl += SafeText(W2U(l.name)) + kSepField + std::to_string(l.to) + kSepRec;
        kv["kland"] = kl;
        summary += " kland=" + std::to_string(r.landings.size());
    }
    // Relations v2, B4: the peace part (B3 hosts ignore these keys).
    const game::PeaceResult& p = r.peace;
    if (!p.ran) return kv;
    auto T = [](const std::wstring& w) { return SafeText(W2U(w)); };
    auto F = [](const std::string& x) { return x + kSepField; };
    std::string en, lands, ships, taken, delta, lost, treaty;
    for (int j : p.enemies) en += std::to_string(j) + ",";
    for (auto& l : p.lands)
        lands += F(T(l.name)) + F(std::to_string(l.from)) + F(std::to_string(l.to)) + (l.removeCA ? "1" : "0") + kSepRec;
    for (auto& s : p.ships)
        ships += F(std::to_string(s.id)) + F(std::to_string(s.from)) + F(std::to_string(s.to)) + F(T(s.name)) +
                 F(T(s.reason)) + T(s.renamed) + kSepRec;
    for (auto& t : p.taken)
        taken += F(T(t.name)) + F(std::to_string(t.owner)) + F(std::to_string(t.takenFrom)) +
                 F(std::to_string(t.invaded)) + F(std::to_string(t.support)) + std::to_string(t.rebellion) + kSepRec;
    for (auto& n : p.delta)
        for (auto& d : n.second) delta += std::to_string(n.first) + ":" + d.first + ":" + std::to_string(d.second) + ";";
    for (auto& n : p.lostWar) lost += std::to_string(n.first) + ":" + std::to_string(n.second) + ";";
    for (auto& l : p.treaty) treaty += T(l) + kSepRec;
    kv["peace"] = "1";
    kv["pcode"] = std::to_string(p.code);
    kv["pevent"] = p.eventShown ? "1" : "0";
    if (p.negotiated)
        kv["pargs"] = std::to_string(p.chance) + ":" + std::to_string(p.lossGain) + ":" + (p.force ? "1" : "0");
    kv["paccepted"] = p.accepted ? "1" : "0";
    kv["pwinner"] = std::to_string(p.winner);
    kv["penemies"] = en;
    kv["pland"] = lands;
    kv["pships"] = ships;
    kv["ptaken"] = taken;
    kv["pdelta"] = delta;
    kv["plost"] = lost;
    kv["ptreaty"] = treaty;
    std::string sep;  // B5: the enemy-revolution rule (0.5.0 hosts ignore the keys)
    if (p.revolution) {
        for (int j : p.separate) sep += std::to_string(j) + ",";
        kv["prevo"] = std::to_string(p.revoPending);
        kv["pseparate"] = sep;
    }
    std::string shipIds;
    for (auto& s : p.ships) shipIds += std::to_string(s.id) + ":" + std::to_string(s.from) + ">" + std::to_string(s.to) + ",";
    std::string landNames;  // the last key of the summary (names may hold spaces)
    for (auto& l : p.lands)
        landNames += T(l.name) + "~" + std::to_string(l.from) + ">" + std::to_string(l.to) + "|";
    g_lastPeace = "ran=1 code=" + kv["pcode"] + " event=" + kv["pevent"] + " args=" + KVStr(kv, "pargs") + " accepted=" +
                  kv["paccepted"] + " winner=" + kv["pwinner"] + " enemies=" + en + " lands=" +
                  std::to_string(p.lands.size()) + " ships=" + std::to_string(p.ships.size()) + " taken=" +
                  std::to_string(p.taken.size()) + " delta=" + delta + " lost=" + lost + " treaty=" +
                  std::to_string(p.treaty.size()) + " kwar=" + std::to_string(r.warCounter) + " revo=" +
                  (p.revolution ? std::to_string(p.revoPending) : std::string("-")) + " separate=" + sep + " shipids=" + shipIds +
                  " landnames=" + landNames;
    summary += " peace=" + std::string(p.accepted ? "made" : "none") + " code=" + kv["pcode"] + " event=" + kv["pevent"];
    return kv;
}

// After a turn or a proposal: this PC's nation's own state follows (its war counter, its per-relation state for a later
// proposal, its event cooldowns), the screen, the turn messages.
static void ClientAfterRelations(const game::PlayerTurnResult& r) {
    game::SetOwnWarCounter(r.warCounter);
    for (int j = 0; j < 9; j++) {
        g_relSetup.will[j] = r.will[j];
        g_relSetup.techSharing[j] = r.techSharing[j];
    }
    if (r.peace.eventShown) g_peaceMonth = g_peaceStateMonth;
    if (r.peace.ran && !g_evUsedKey.empty()) SaveEventCooldowns();
    game::RefreshUI();
    Changed();
    if (!r.messages.empty()) game::ShowTurnMessagesOf(r.messages);
}

// Relations v2 (client): the nations people play, as the month's state named them and as the host's lobby names them
// now (a nation claimed or reserved after the state was sent is a player's nation too: kept out of the view's routines
// and the peace).
static void MarkLobbyHumans(bool (&human)[9]) {
    for (auto& h : g_cSnap.humans)
        if (h.idx >= 0 && h.idx <= 8) human[h.idx] = true;
}

// Relations v2, B3 (client): the player's relation routines for this PC's nation at the start of its month (after the
// event roll): RandomTensionChanges (in a month without an event), TensionEffectOfPreviousWars, CheckForWar, in the
// nation's own view, with their dialogs here. What they changed goes to the host at once (MSG_RELATIONS, applied before
// the month runs) and into this PC's campaign; the turn messages they wrote follow in the game's turn report. B4: then,
// at war with a computer nation, the player's peace decision and its peace event (not in a month with another event: one
// event a month), from a B4 host only.
static void ClientRunRelations(bool force = false) {
    if (g_role != Role::Client || g_myNation <= 0 || !g_haveState || game::EventActive()) return;
    static int s_relMonth = 0;  // once per calendar month (a resent month has a new seq)
    int month = ClientMonth();
    if (!force && month > 0 && month == s_relMonth) {
        Log("relations: this month's turn was already run");
        return;
    }
    s_relMonth = month;
    game::PlayerTurnSetup s = g_relSetup;
    MarkLobbyHumans(s.human);
    s.K = g_myNation;
    int seq = g_clientSeq;
    s.tensionChanges = g_evShownSeq != seq;
    // B5 (d): the allies' war help as the game's (War > 1 after the month's step: this nation's counter, in place since
    // the load) while the nation fights a computer nation.
    bool computerEnemy = false;
    for (int j = 1; j <= 8 && j < game::NationCount(); j++)
        if (j != s.K && !s.human[j] && game::AtWar(s.K, j)) computerEnemy = true;
    s.allyHelp = s.allyHelpHost && game::AllyHelpAvailable() && game::WarCounter() > 1 && computerEnemy;
    // B5 (e): the ship seizures of a war as the game's (War > 0 after the month's step: this nation's counter).
    s.buildWar = s.buildWarHost && game::BuildWarAvailable() && game::WarCounter() > 0;
    s.peace.run = g_peaceHost && game::PeaceAvailable();
    s.peace.decision = g_evShownSeq != seq && g_peaceMonth != g_peaceStateMonth;
    s.peace.forcedCode = g_forcedPeaceCode;
    s.peace.forcedChance = g_forcedPeaceChance;
    g_forcedPeaceCode = g_forcedPeaceChance = 0;
    // 0.6.0 (B6 piece 2): surprise attacks and takeovers at a war's start, for a host that takes them (welcome `gen` 2)
    s.surprise = g_hostGen >= 2 && game::SurpriseAvailable();
    // 0.7.0 (P4): the surprise landing in this nation's possession too, for a host that keeps its invasions (`gen` 4)
    s.landings = s.surprise && g_hostGen >= 4 && inv::Available();
    if (g_testSa.J > 0) {  // tests (`battles testsa`): nation J's war on this nation starts this turn, with a surprise
        if (s.surprise && g_testSa.J <= 8 && g_testSa.J != g_myNation && !s.human[g_testSa.J]) {
            s.testSaNation = g_testSa.J;
            s.testSaK = g_testSa.k;
            s.testTakeover = !g_testSa.land;
            s.testLanding = g_testSa.land && s.landings;
            s.will[g_testSa.J] = 1;
        } else {
            Log("tests: the forced surprise of nation %d is not possible (host gen %d, here %s)", g_testSa.J, g_hostGen,
                game::SurpriseAvailable() ? "available" : "UNAVAILABLE");
        }
        g_testSa = {};
    }
    if (s.peace.run && month > 0) {
        UpdateEventCooldowns(month);
        s.peace.used = g_evUsed;
    }
    game::PlayerTurnResult r = game::RunPlayerTurn(s);
    if (!r.error.empty()) {
        g_lastRelations = "error " + r.error;
        Log("relations: %s", r.error.c_str());
        return;
    }
    std::string summary;
    KV kv = RelationsKV(r, seq, summary);
    net::Send(0, net::MSG_RELATIONS, KVEncode(kv));
    g_lastRelations = summary;
    Log("relations for nation %d sent: %s", g_myNation, g_lastRelations.c_str());
    if (r.peace.ran) Log("peace for nation %d (its turn): %s", g_myNation, g_lastPeace.c_str());
    ClientAfterRelations(r);
}

// Relations v2, B4 (client): a peace proposal to the computer's nations this PC's nation is at war with (the Diplomacy
// list's "Propose peace", or `peacetest`): the game's NegotiatePeace(chance, lossGain, force) at once in the nation's
// view (the proposal: the game's compromise offer 40, 0), with its dialogs here; the result goes to the host like the
// turn's (MSG_RELATIONS, kind=peace: one a month, a test's any time).
static void ClientRunPeace(int chance, int lossGain, bool force, bool test) {
    if (g_role != Role::Client || g_myNation <= 0 || !g_haveState || game::EventActive()) return;
    if (!g_peaceHost || !game::PeaceAvailable()) {
        g_lastPeace = "error the host does not apply peace with the computer's nations for this nation";
        Log("peace: %s", g_lastPeace.c_str());
        return;
    }
    if (!test && g_peaceMonth == g_peaceStateMonth) {  // e.g. the month's peace event came first (its relation turn)
        g_lastPeace = "none (the proposal was dropped: a peace was negotiated this month)";
        Log("peace: the proposal was dropped (this month's peace was negotiated already)");
        AddLocalChat("* No peace talks: your nation has negotiated with the computer's nations this month already.");
        return;
    }
    int month = ClientMonth();
    if (month > 0) UpdateEventCooldowns(month);
    game::PlayerTurnSetup s = g_relSetup;
    MarkLobbyHumans(s.human);
    s.K = g_myNation;
    s.relations = false;
    s.tensionChanges = false;
    s.peace.run = true;
    s.peace.negotiate = true;
    s.peace.chance = chance;
    s.peace.lossGain = lossGain;
    s.peace.force = force;
    s.peace.forcedCode = 0;
    s.peace.used = month > 0 ? g_evUsed : nullptr;
    int seq = g_clientSeq;
    game::PlayerTurnResult r = game::RunPlayerTurn(s);
    if (!r.error.empty()) {
        g_lastPeace = "error " + r.error;
        Log("peace: %s", r.error.c_str());
        return;
    }
    if (!r.peace.ran) {
        g_lastPeace = "none (not at war with a computer nation)";
        Log("peace: %s", g_lastPeace.c_str());
        return;
    }
    std::string summary;
    KV kv = RelationsKV(r, seq, summary);
    kv["kind"] = "peace";
    if (test) kv["test"] = "1";
    net::Send(0, net::MSG_RELATIONS, KVEncode(kv));
    if (!test) g_peaceMonth = g_peaceStateMonth;
    Log("peace for nation %d sent (%s): %s | %s", g_myNation, test ? "test" : "proposal", g_lastPeace.c_str(),
        summary.c_str());
    ClientAfterRelations(r);
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
            g_hostGen = 0;
            KV kv;
            kv["proto"] = std::to_string(MP_PROTOCOL);
            kv["mod"] = MP_MOD_VERSION;
            kv["build"] = game::BuildFingerprint();
            kv["name"] = g_myName;
            kv["pw"] = g_password;
            kv["battles"] = g_fightBattles ? "1" : "0";
            kv["ls"] = g_lsEnabled && lockstep::Available() ? std::to_string(AnnouncedLsLevel()) : "0";  // engine level
            kv["rel"] = game::PlayerTurnAvailable() ? "1" : "0";  // relations v2: the player's relation routines run here
            kv["gen"] = !game::BattleReplayAvailable() ? "0"
                        : game::SurpriseAvailable() && game::RaiderReplayAvailable()
                            ? inv::Available() ? "4"   // 0.7.0: invasions (the plan in the form, the target sent)
                                               : "3"   // 0.7.0: raiders against patrols too (raider checks replayed)
                        : game::SurpriseAvailable() ? "2"   // 0.6.0 piece 2: surprise attacks and takeovers too
                                                    : "1";  // 0.6.0: replays the player's battle generator
            // 0.6.1: this PC loads its nation's own campaign divisions (game.cpp d_LoadDivisions) and sends them with
            // its turn; the host then sends them back with the month instead of the host's own (else: an empty list)
            kv["div"] = game::DivisionsAvailable() ? "1" : "0";
            kv["cpu"] = x87::CpuVendor();  // (logged by both sides: a live battle's diagnostics name differing vendors)
            if (datafiles::Ready()) kv["dh"] = datafiles::Combined();  // the game data files (logged; checked at a start)
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
            nplay::ClientReset();
            game::SetTurnButtonCaption(L"Turn");
            net::Shutdown();
            lockstep::OnPeerLost("the connection to the host was lost");
            ClientDropBattle();
            break;
        }
        case net::MSG_LOCKSTEP:
            lockstep::OnMessage(m.data, [](const std::string& s) { net::Send(0, net::MSG_LOCKSTEP, s); });
            break;
        case net::MSG_REJECT:
            Notice("The host refused the connection: " + KVStr(KVDecode(m.data), "reason"));
            break;
        case net::MSG_WELCOME: {
            KV kv = KVDecode(m.data);
            std::string assigned = KVStr(kv, "name", g_myName);
            if (assigned != g_myName) AddChat("* That name was taken; you joined as " + assigned);
            g_myName = assigned;
            g_hostGen = std::max(0, KVInt(kv, "gen"));  // 0.6.0 (older hosts: none)
            {
                std::string cpu = KVStr(kv, "cpu").substr(0, 16), dh = KVStr(kv, "dh").substr(0, 16);
                std::string mine = datafiles::Ready() ? datafiles::Combined() : "";
                Log("joined %s: CPU %s there (this PC: %s); game data files %s", KVStr(kv, "host").c_str(),
                    cpu.empty() ? "?" : cpu.c_str(), x87::CpuVendor().c_str(),
                    dh.empty() || mine.empty() ? "not compared yet" : dh == mine ? "the same as the host's" : "DIFFERENT");
            }
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
            if (!KVStr(kv, "treaty").empty()) {  // a peace treaty of my nation: shown in the game's dialog when idle
                if (!g_pendingTreaty.empty()) g_pendingTreaty.push_back(" ");
                for (auto& l : Split(KVStr(kv, "treaty"), kSepRec))
                    if (!l.empty()) g_pendingTreaty.push_back(l);
                break;
            }
            std::string from = KVStr(kv, "from");
            AddChat(from == "*" ? "* " + KVStr(kv, "text") : from + ": " + KVStr(kv, "text"));
            if (KVInt(kv, "notice") && g_onNotice) g_onNotice(KVStr(kv, "text"));  // flash the window
            break;
        }
        case net::MSG_STATE:
            g_pendingState = m.data;
            Notice("Receiving the new month from the host...");
            ClientApplyPendingState();
            break;
        case net::MSG_BATTLE: {
            KV h;
            std::string body;
            if (!UnpackHB(m.data, h, body)) break;
            g_battleIn = m.data;
            g_battleInId = KVInt(h, "id", -1);
            Notice("Battle! " + W2U(game::NationName(g_myNation)) + " meets " + KVStr(h, "oppName") + " in " +
                   KVStr(h, "area") + ". The battle starts as soon as your game is free.");
            break;
        }
        case net::MSG_BATTLE_CANCEL: {
            KV kv = KVDecode(m.data);
            int id = KVInt(kv, "id", -1);
            std::string why = KVStr(kv, "why");
            if (id == g_battleInId) {
                g_battleIn.clear();
                g_battleInId = -1;
            }
            if ((g_cb.fighting || g_cb.starting) && g_cb.id == id) {
                g_cb.cancelled = true;
                Notice("The host resolved this battle without you (" + why +
                       "). Finish or leave the battle; its result is not used.");
            } else {
                AddChat("* The host resolved your battle without you (" + why + ").");
            }
            break;
        }
        case net::MSG_FORCE_SUBMIT:
            if (g_haveState && !g_submitted) {
                g_forceSubmitPending = true;
                g_submitForcedByHost = true;
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
    if (GetEnvironmentVariableA("RTW3MP_EVENT_CHANCE", buf, sizeof(buf)) > 0)
        g_eventChancePct = std::max(0, std::min(100, atoi(buf)));
    g_battleSlot = g_clientSlot + 10;
    if (GetEnvironmentVariableA("RTW3MP_BATTLE_SLOT", buf, sizeof(buf)) > 0) {
        int s = atoi(buf);
        if (s >= 50 && s < 1000 && s != g_clientSlot) g_battleSlot = s;
    }
    if (GetEnvironmentVariableA("RTW3MP_BATTLE_TIMEOUT", buf, sizeof(buf)) > 0) g_battleTimeoutSec = std::max(0, atoi(buf));
    if (GetEnvironmentVariableA("RTW3MP_LIVE_BATTLES", buf, sizeof(buf)) > 0) g_lsEnabled = atoi(buf) != 0;
    if (AnnouncedLsLevel() != MP_LS_LEVEL)
        Log("TEST: RTW3MP_TEST_LS_LEVEL: this copy announces live-battle engine %d (its own is %d)", AnnouncedLsLevel(),
            MP_LS_LEVEL);
    // The month's news about a joined nation's foreign stations and trade protection goes to its player.
    nplay::SetNewsSink([](int nation, const std::string& line) { g_playerNews[nation].push_back(line); });
    lockstep::Hooks lh;
    // A live battle's lines are this PC's own: the host's AddChat would send every "* " line to all players (the other
    // player then read the host's side's game texts and pause reasons, and its own lines twice); the other player gets
    // its lines from the battle itself (ChatBoth and the per-player texts).
    lh.chat = [](const std::string& s) { AddLocalChat(s); };
    lh.notice = [](const std::string& s) { Notice(s); };
    lh.onEnded = [](bool normal, const std::string& why) {
        Log("live battle ended (%s): %s", normal ? "normal" : "abandoned", why.c_str());
        // (a rejoin armed before this drop happened keeps the host's battle player: it starts once the drop is done)
        if ((g_role == Role::Host || g_lsHost.opp >= 0) && !lockstep::RejoinPending()) g_lsHost.opp = -1;
        if (g_lsRole == 'B') g_lsLeave = true;  // back to this player's own game once the battle screen is closed
        g_lsRole = 0;
        g_lsClientA.pending = false;
    };
    lh.canLoad = [](std::string& why) {
        if (g_role != Role::Client || !g_lsEnabled)
            why = "live battles are off on this PC";
        else if (g_cb.fighting || g_cb.starting || (g_lsLeave && game::BattleActive()))
            why = "the player is fighting another battle";
        else if (g_lsLeave) {  // the last live battle's copy is discarded already: this one uses the battle slot again
            g_lsLeave = false;  // (the player's own game loads after this battle)
            Log("live battle: the battle slot is used again (no return to the own game in between)");
        }
        return why.empty();
    };
    lh.loadReady = [] {  // a dialog or event of the player's game: the battle loads once it is closed
        if (game::IsBusy() || game::BattleActive() || game::EventActive()) return false;
        if (g_haveState && !g_submitted && game::CurrentSlot() == g_clientSlot) game::SaveGame();  // keep the plans
        return true;
    };
    lh.battleSlot = [] { return g_battleSlot; };
    lh.slotMode = [](bool on) {
        game::SetBattleSlotMode(on);
        if (on) g_lsRole = 'B';
    };
    lh.resumed = [](const lockstep::RejoinInfo& ri) { OnLiveBattleResumed(ri); };
    lockstep::SetHooks(lh);
}

void SetFightBattles(bool on) { g_fightBattles = on; }
bool FightBattles() { return g_fightBattles; }
void SetAutoStrike(bool on) {
    if (on != g_autoStrike) Log("host option: the computer flies carrier strikes in live battles: %s", on ? "on" : "off");
    g_autoStrike = on;
}
bool AutoStrike() { return g_autoStrike; }
void SetLegacyAllowed(bool on) {
    if (on != g_legacyAllowed) Log("host option: manual legacy fleets of joined players: %s", on ? "on" : "off");
    g_legacyAllowed = on;
}
void SetLegacyWanted(bool on) {
    if (on != g_legacyWanted) Log("legacy fleet: build it by hand when a new campaign's first month comes: %s", on ? "yes" : "no");
    g_legacyWanted = on;
}
bool LegacyWanted() { return g_legacyWanted; }

// Tests and diagnostics: battles of joined players.
//   host:   list | inject <K> <J> <area...> | injectgen <K> <J> [area...] | injectsa <A> <B> | gen <K> [queue|fault] |
//           injectraid <K> (0.7.0) | injectinv <K> (0.7.0) | aitest <K> <J> <area...> | resolve <id|all> | timeout <seconds>
//   client: state | hostgen <n> (as if the host's welcome had `gen` n; 0 = a 0.5.0 host) | saretry <n> |
//           testsa <J> [k] | sadry <J> [k] | testraid <0|1> <J> | testraid off (0.7.0)
// 0.7.0, bridge `inv ...`: joined nations' invasions. Host: `state [K]`, `marks`, `run K` (K's month resolution now),
// TEST ONLY `set <poss> <invader|-1> [support] [itt]`, `target K <poss|->`, `plan K <count> <enemyPoss|-> <pending> [itt]`,
// `rnd <0|1|2>` (the routine's own draws: off, low, high). Client: `form`, TEST ONLY `pick <poss|->` (the form's target
// as the game's Invasion target button sets it). Possession names: '_' for ' '.
std::string DebugInvasions(const std::vector<std::string>& a) {
    std::string cmd = a.size() > 1 ? a[1] : "state";
    auto name = [&](size_t i) {
        std::string s = a.size() > i ? a[i] : "-";
        for (char& ch : s)
            if (ch == '_') ch = ' ';
        return U2W(s == "-" ? "" : s);
    };
    auto stateOf = [](int K) {
        const inv::KState& s = inv::State(K);
        std::string itt;
        for (auto& n : s.itt) itt += W2U(n) + "/";
        return "K=" + std::to_string(K) + " target=" + W2U(s.target) + " count=" + std::to_string(s.count) +
               " enemy=" + W2U(s.enemyTarget) + " pending=" + std::to_string(s.pending) + " itt=" + itt +
               " poss=" + inv::Describe(K) + " cost=" +
               std::to_string(s.target.empty() ? 0 : inv::TargetCost(game::Nation(K)));
    };
    if (!inv::Available()) return "error invasions unavailable";
    if (cmd == "form") return "ok target=" + W2U(inv::FormTarget()) + " hostgen=" + std::to_string(g_hostGen);
    if (cmd == "pick") {
        inv::KState s;
        s.target = name(2);
        return "ok " + inv::ClientSetForm(s, s.target.empty());
    }
    if (cmd == "marks") {
        std::string s;
        for (auto& n : inv::MarkedThisTurn()) s += W2U(n) + "/";
        return "ok marks=" + s;
    }
    if (cmd == "rnd" && a.size() > 2) {
        inv::SetTestRandom(atoi(a[2].c_str()));
        return "ok rnd=" + std::to_string(inv::TestRandom());
    }
    if (cmd == "set" && a.size() > 3) {  // (also on a client: its copy, e.g. before its peace's test)
        std::string e = inv::SetPossession(name(2), atoi(a[3].c_str()), a.size() > 4 ? atoi(a[4].c_str()) : 0,
                                          a.size() > 5 && a[5] == "itt");
        return e.empty() ? "ok " + inv::Describe(-1) : "error " + e;
    }
    if (g_role != Role::Host) return "error host only";
    if (cmd == "hostpeace") {  // TEST ONLY: the game's DoPeace for the host's nation (through the hook; async: its box)
        RunOnMainThread([] {
            if (void* m = dl::Method("TfrmBuildCamp", "DoPeace"))
                if (void* f = game::BuildCamp()) dl::Call(m, (uint32_t)(uintptr_t)f);
            Log("invasions: TEST: the host's DoPeace ran: %s", inv::Describe(-1).c_str());
        });
        return "ok";
    }
    if (cmd == "hostcheck") {  // TEST ONLY: the game's CheckForInvasionBattles for the host's nation (P1's refusals)
        std::string before = inv::TakeHideLog();
        int r = game::RunHostInvasionCheck();
        std::string log = inv::TakeHideLog();
        if (!before.empty())
            Log("invasions: the host's own routines and the joined nations' invasions this month: %s", before.c_str());
        return "ok result=" + std::to_string(r) + " " + log;
    }
    if (cmd == "state") {
        if (a.size() > 2) return "ok " + stateOf(atoi(a[2].c_str()));
        std::string s = "ok all=" + inv::Describe(-1);
        for (int K = 1; K <= 8; K++)
            if (!inv::State(K).Empty()) s += " | " + stateOf(K);
        return s;
    }
    int K = a.size() > 2 ? atoi(a[2].c_str()) : 0;
    if (K < 1 || K > 8) return "error K";
    if (cmd == "target") {
        inv::State(K).target = name(3);
        inv::State(K).count = 0;
        SaveReservations();
        return "ok " + stateOf(K);
    }
    if (cmd == "plan" && a.size() > 5) {
        inv::KState& s = inv::State(K);
        s.count = atoi(a[3].c_str());
        s.enemyTarget = name(4);
        s.pending = atoi(a[5].c_str());
        s.itt.clear();
        if (a.size() > 6) s.itt.push_back(name(6));
        SaveReservations();
        return "ok " + stateOf(K);
    }
    if (cmd == "run") {
        bool human[9];
        for (int i = 0; i < 9; i++) human[i] = IsHumanNation(i);
        auto d = g_kDecl.find(K);
        inv::RunResult r = inv::RunForNation(K, KWar(K), human, inv::State(K), economy::ArmyOffensiveOf(K),
                                             d != g_kDecl.end() ? d->second.second : 0);
        std::string news;
        for (auto& t : r.news) {
            news += W2U(t) + " | ";
            g_invNews[K].push_back(t);
        }
        SaveReservations();
        return (r.ran ? "ok " : "error ") + r.summary + (r.error.empty() ? "" : " ERROR " + r.error) + " news=" + news;
    }
    if (cmd == "escort") return "ok " + inv::EscortTest(K);  // P7: computer nation K's most important area
    return "error unknown";
}

std::string DebugBattles(const std::vector<std::string>& a) {
    std::string cmd = a.size() > 1 ? a[1] : "list";
    if (cmd == "state") {
        std::string s = g_cb.fighting ? "fighting" : g_cb.starting ? "starting" : !g_battleIn.empty() ? "pending" : "none";
        return "ok state=" + s + " id=" + std::to_string(g_cb.fighting ? g_cb.id : g_battleInId) +
               " nation=" + std::to_string(g_cb.K) + " opp=" + std::to_string(g_cb.opp) + " area=" + W2U(g_cb.area) +
               " slot=" + std::to_string(g_battleSlot) + " slotMode=" + (game::BattleSlotMode() ? "1" : "0") +
               " aftermath=" + (game::BattleAftermathSeen() ? "1" : "0") + " fight=" + (g_fightBattles ? "1" : "0") +
               " hostgen=" + std::to_string(g_hostGen) + " raider=" + std::to_string(g_cb.raider) + " testraid=" +
               (g_testRaid.side < 0 ? std::string("off") : std::to_string(g_testRaid.side) + ":" + std::to_string(g_testRaid.J));
    }
    if (cmd == "hostgen" && a.size() > 2) {  // tests (decision 4): the client's decline under an older host
        if (g_role != Role::Client) return "error client only";
        g_hostGen = std::max(0, atoi(a[2].c_str()));
        Log("tests: the host's battle requests taken as gen %d", g_hostGen);
        return "ok hostgen=" + std::to_string(g_hostGen);
    }
    if (cmd == "monthstate") {  // 0.6.0 (B6 round 3): the month's battle state here; `set` (TEST ONLY) sets it to 1
        bool set = a.size() > 2 && a[2] == "set";
        if (set && g_role != Role::Client) return "error client only";
        std::string s = game::MonthStateText(set);
        return s.empty() ? "error unavailable" : "ok " + s;
    }
    // 0.6.0 (B6 piece 2), client tests. saretry: CheckSurpriseAttack's random gates passed by asking again (a False with
    // nothing else happening, up to n times; 0 = once, as single player). testsa: the next relation turn has computer
    // nation J start its war on this nation with a surprise (J's war plan 1 and SurpriseAttack trait; `k`: this nation's
    // trait too, so it attacks; the takeover picks the area's first unowned possession). sadry: DoSurpriseAttack alone
    // in this nation's view now (J the target; `k` as above): its recorded pairs, nothing sent.
    if (cmd == "saretry" && a.size() > 2) {
        if (g_role != Role::Client) return "error client only";
        g_testSaRetry = std::max(0, std::min(50, atoi(a[2].c_str())));
        Log("tests: CheckSurpriseAttack is asked again after a False up to %d time(s)", g_testSaRetry);
        return "ok saretry=" + std::to_string(g_testSaRetry);
    }
    if ((cmd == "testsa" || cmd == "sadry") && a.size() > 2) {
        if (g_role != Role::Client || g_myNation <= 0 || !g_haveState) return "error client with a nation only";
        int J = atoi(a[2].c_str());
        bool k = a.size() > 3 && a[3] == "k";
        bool land = (a.size() > 3 && a[3] == "land") || (a.size() > 4 && a[4] == "land");
        game::PlayerTurnSetup s = g_relSetup;
        MarkLobbyHumans(s.human);
        if (J < 1 || J > 8 || J == g_myNation || s.human[J]) return "error J must be a computer nation";
        if (!game::SurpriseAvailable()) return "error surprise attacks are not available here";
        if (cmd == "testsa") {
            g_testSa.J = J;
            g_testSa.k = k;
            g_testSa.land = land;
            Log("tests: the next relation turn: nation %d starts its war on nation %d with a surprise%s%s", J, g_myNation,
                k ? " (this nation attacks)" : "", land ? " and its landing in this nation's possession" : "");
            return "ok testsa=" + std::to_string(J) + (k ? " k" : "") + (land ? " land" : "") +
                   " hostgen=" + std::to_string(g_hostGen);
        }
        s.K = g_myNation;
        s.relations = s.tensionChanges = s.peace.run = false;
        s.surprise = s.testSaDry = true;
        s.testSaNation = J;
        s.testSaK = k;
        game::PlayerTurnResult r = game::RunPlayerTurn(s);
        if (!r.error.empty()) return "error " + r.error;
        std::string out;
        for (auto& t : r.surprise) out += std::to_string(t.a) + ">" + std::to_string(t.b) + ";";
        return std::string("ok ran=") + (r.ranSurprise ? "1" : "0") + " pairs=" + out;
    }
    if (cmd == "testraid" && a.size() > 2) {  // 0.7.0, client tests: testraid <0|1> <J> | testraid off
        if (a[2] == "off") {
            g_testRaid = {};
            return "ok testraid=off";
        }
        int side = atoi(a[2].c_str()), J = a.size() > 3 ? atoi(a[3].c_str()) : -1;
        if ((side != 0 && side != 1) || J < 1 || J > 8 || J == g_myNation) return "error usage: testraid <0|1> <J> | off";
        g_testRaid.side = side;
        g_testRaid.J = J;
        Log("tests: the next raider check forces an interception of side %d with nation %d", side, J);
        return "ok testraid=" + std::to_string(side) + ":" + std::to_string(J);
    }
    if (cmd == "testinv" && a.size() > 2) {  // 0.7.0 (P3), tests (any PC): testinv <0|1>: the next invasion check plain
        bool on = a[2] == "1";
        if (!game::SetTestInvasionPlain(on)) return "error not available";
        Log("tests: the next invasion check %s", on ? "is plain (no computer decline, not caught out)" : "as the game has it");
        return std::string("ok testinv=") + (on ? "1" : "0");
    }
    if (cmd == "testcoup" && a.size() > 2) {  // 0.7.0 (fix-afb), tests (any PC): the next setup as a coup's ambush
        int mode = atoi(a[2].c_str());
        if (mode < 0 || mode > 2) return "error usage: testcoup <0|1|2>";
        if (!game::SetTestCoup(mode)) return "error not available";
        long runs = game::TestCoupRuns();
        Log("tests: the next battle setup on this PC %s (setups made an ambush so far: %ld)",
            mode == 0   ? "as the game has it"
            : mode == 1 ? "runs as an intelligence coup's ambush"
                        : "runs as an intelligence coup's ambush, the mod's nation for its reinforcements off "
                          "(the game's box)",
            runs);
        return "ok testcoup=" + std::to_string(mode) + " forced=" + std::to_string(runs);
    }
    if (g_role != Role::Host) return "error host only";
    if (cmd == "list") {
        std::string s = "ok phase=" + std::string(g_battlePhase ? "1" : "0") + " timeout=" +
                        std::to_string(g_battleTimeoutSec) + " battles=";
        for (auto& r : g_battles)
            s += std::to_string(r.id) + ":" + std::to_string(r.human) + "-" + std::to_string(r.opp) + ":" + W2U(r.area) +
                 ":" + BattleStateName(r.state) + (r.why.empty() ? "" : ":" + r.why) +
                 (r.kind ? ":kind=" + std::to_string(r.kind) + ":att=" + std::to_string(r.attempts.size()) : "") +
                 (r.declined ? ":declined" : r.avoided ? ":avoided" : "") +
                 (r.raider ? ":raider=" + std::to_string(r.raider) : "") + "; ";
        return s;
    }
    if (cmd == "gen" && a.size() > 2) {  // B6, tests: nation K's generator now; "queue" adds its request (month end)
        int K = atoi(a[2].c_str());
        bool queue = a.size() > 3 && a[3] == "queue";
        bool fault = a.size() > 3 && a[3] == "fault";  // decision 3: a game exception inside the view (no box, no request)
        if (queue && (g_afterTurnPending || g_battlePhase)) return "error not while the month or its battles are processed";
        if (!B6On(K)) return "error the player's battle generator is not on for nation " + std::to_string(K);
        if (fault) game::TestGeneratorFault();
        std::string out;
        int id = HostRunGenerator(K, queue, out);
        if (id < 0) return "error " + out;
        return "ok id=" + std::to_string(id) + " war=" + std::to_string(KWar(K)) + " attempts=" + out;
    }
    if (cmd == "aitest" && a.size() > 4) {  // tests (risk 4): the game's resolution of K vs J now, its box captured
        if (g_afterTurnPending || g_battlePhase) return "error not while the month or its battles are processed";
        int K = atoi(a[2].c_str()), J = atoi(a[3].c_str());
        std::string areaName;
        for (size_t i = 4; i < a.size(); i++) areaName += (i > 4 ? " " : "") + a[i];
        std::vector<std::wstring> texts;
        bool fought = game::ResolveAIBattle(K, J, 3, 3, U2W(areaName), &texts);
        std::string s = "ok fought=" + std::string(fought ? "1" : "0") + " captured=" + std::to_string(texts.size());
        for (auto& t : texts) s += " | " + W2U(t).substr(0, 120);
        return s;
    }
    if (cmd == "injectsa" && a.size() > 3) {  // B6 piece 2, tests: a surprise attack request (kind 2): A strikes B
        if (g_afterTurnPending || g_battlePhase) return "error not while the month or its battles are processed";
        int A = atoi(a[2].c_str()), B = atoi(a[3].c_str());
        int K = hookcb::IsHumanRemoteNation(A) ? A : B, J = K == A ? B : A;
        if (K < 1 || K > 8 || !hookcb::IsHumanRemoteNation(K) || J < 1 || J > 8 || IsHumanNation(J))
            return "error one side must be a joined nation, the other a computer nation";
        if (!B6On(K)) return "error the player's battle generator is not on for nation " + std::to_string(K);
        Player* p = PlayerOfNation(K);
        if (!p || p->genLevel < 2) return "error the player's PC does not play surprise attacks (hello gen < 2)";
        game::BattleAttempt t;
        t.a = A;
        t.b = B;
        BattleReq r;
        r.id = g_battleNextId++;
        r.kind = 2;
        r.attempts = {t};
        r.a = A;
        r.b = B;
        r.human = K;
        r.opp = J;
        r.player = p->name;
        g_battles.push_back(r);
        Log("battle request %d injected (surprise attack): nation %d strikes %d, played by %s", r.id, A, B,
            p->name.c_str());
        return "ok " + std::to_string(r.id);
    }
    if (cmd == "injectgen" && a.size() > 3) {  // B6, tests: a request of the player's generator: K attacks J [in area]
        if (g_afterTurnPending || g_battlePhase) return "error not while the month or its battles are processed";
        int K = atoi(a[2].c_str()), J = atoi(a[3].c_str());
        std::string areaName;
        for (size_t i = 4; i < a.size(); i++) areaName += (i > 4 ? " " : "") + a[i];
        std::wstring area = U2W(areaName);
        auto names = game::AreaNames();
        if (!area.empty() && std::find(names.begin(), names.end(), area) == names.end())
            return "error no sea area '" + areaName + "'";
        if (J < 1 || J > 8 || IsHumanNation(J)) return "error J must be an AI nation";
        if (!B6On(K)) return "error the player's battle generator is not on for nation " + std::to_string(K);
        game::BattleAttempt t;
        t.a = K;
        t.b = J;
        t.area = area;
        BattleReq r;
        r.id = g_battleNextId++;
        r.kind = 1;
        r.attempts = {t};
        r.a = K;
        r.b = J;
        r.area = area;
        r.human = K;
        r.opp = J;
        r.player = PlayerOfNation(K)->name;
        g_battles.push_back(r);
        Log("battle request %d injected (the player's generator): nation %d attacks %d%s", r.id, K, J,
            InArea(area).c_str());
        return "ok " + std::to_string(r.id);
    }
    if (cmd == "injectraid" && a.size() > 2) {  // 0.7.0, tests: a request of the player's generator: one raider check
        if (g_afterTurnPending || g_battlePhase) return "error not while the month or its battles are processed";
        int K = atoi(a[2].c_str());
        if (!B6On(K)) return "error the player's battle generator is not on for nation " + std::to_string(K);
        if (!RaiderChecksOn(K)) return "error the player's PC does not replay raider checks (hello gen < 3)";
        int J = FirstComputerEnemy(K);
        if (J < 1) return "error nation " + std::to_string(K) + " is at war with no computer nation";
        game::BattleAttempt t;
        t.type = 1;
        t.a = K;
        BattleReq r;
        r.id = g_battleNextId++;
        r.kind = 1;
        r.attempts = {t};
        r.a = K;
        r.b = J;
        r.human = K;
        r.opp = J;
        r.player = PlayerOfNation(K)->name;
        g_battles.push_back(r);
        Log("battle request %d injected (the player's generator): a raider check of nation %d (named for %d)", r.id, K, J);
        return "ok " + std::to_string(r.id);
    }
    if (cmd == "injectinv" && a.size() > 2) {  // 0.7.0 (P3), tests: a request of the player's generator: the invasion check
        if (g_afterTurnPending || g_battlePhase) return "error not while the month or its battles are processed";
        int K = atoi(a[2].c_str());
        if (!B6On(K)) return "error the player's battle generator is not on for nation " + std::to_string(K);
        if (!InvasionChecksOn(K))
            return "error the player's PC does not replay invasion checks (hello gen < 4) or the nation has no invasion";
        int J = InvasionEnemy(K);
        if (J < 1) J = FirstComputerEnemy(K);
        if (J < 1) return "error nation " + std::to_string(K) + " is at war with no computer nation";
        game::BattleAttempt t;
        t.type = 2;
        t.a = K;
        BattleReq r;
        r.id = g_battleNextId++;
        r.kind = 1;
        r.attempts = {t};
        r.a = K;
        r.b = J;
        r.human = K;
        r.opp = J;
        r.player = PlayerOfNation(K)->name;
        g_battles.push_back(r);
        Log("battle request %d injected (the player's generator): the invasion check of nation %d (named for %d)", r.id,
            K, J);
        return "ok " + std::to_string(r.id);
    }
    if (cmd == "timeout" && a.size() > 2) {
        g_battleTimeoutSec = std::max(0, atoi(a[2].c_str()));
        return "ok";
    }
    if (cmd == "resolve" && a.size() > 2) {
        if (g_battleFinishing) return "error the battles are being finished";
        int n = 0;
        for (auto& r : g_battles)
            if (r.state < 2 && (a[2] == "all" || atoi(a[2].c_str()) == r.id)) {
                BattleFallback(r, "host decision");
                n++;
            }
        return "ok " + std::to_string(n);
    }
    if (cmd == "inject" && a.size() > 4) {  // a battle as if the game had found it this month (processed at month end)
        if (g_afterTurnPending || g_battlePhase) return "error not while the month or its battles are processed";
        int K = atoi(a[2].c_str()), J = atoi(a[3].c_str());
        std::string areaName;
        for (size_t i = 4; i < a.size(); i++) areaName += (i > 4 ? " " : "") + a[i];
        std::wstring area = U2W(areaName);
        auto names = game::AreaNames();
        if (std::find(names.begin(), names.end(), area) == names.end()) return "error no sea area '" + areaName + "'";
        if (!hookcb::IsHumanRemoteNation(K) || hookcb::IsHumanRemoteNation(J) || J < 1 || J > 8)
            return "error K must be a joined player's nation and J an AI nation";
        Player* p = PlayerOfNation(K);
        if (!p || !p->battles) return "error no connected player who fights battles for nation " + std::to_string(K);
        BattleReq r;
        r.id = g_battleNextId++;
        r.a = K;
        r.b = J;
        r.countA = r.countB = 3;
        r.area = area;
        r.human = K;
        r.opp = J;
        r.player = p->name;
        g_battles.push_back(r);
        Log("battle request %d injected: nation %d vs %d in %s", r.id, K, J, areaName.c_str());
        return "ok " + std::to_string(r.id);
    }
    if (cmd == "injectpvp" && a.size() > 4) {  // a live battle between two players' nations (processed at month end)
        if (g_afterTurnPending || g_battlePhase) return "error not while the month or its battles are processed";
        int K = atoi(a[2].c_str()), J = atoi(a[3].c_str());
        std::string areaName;
        for (size_t i = 4; i < a.size(); i++) areaName += (i > 4 ? " " : "") + a[i];
        std::wstring area = U2W(areaName);
        auto names = game::AreaNames();
        if (std::find(names.begin(), names.end(), area) == names.end()) return "error no sea area '" + areaName + "'";
        if (!hookcb::IsHumanRemoteNation(K) || !hookcb::IsHumanRemoteNation(J) || K == J)
            return "error K and J must be two joined players' nations";
        if (!HostDeferPvpBattle(K, J, 3, 3, area))
            return "error both players must be connected and fight live battles with this host's live-battle engine";
        return "ok " + std::to_string(g_battles.back().id);
    }
    return "error usage: battles list|inject K J area|injectpvp K J area|injectgen K J [area]|gen K [queue|fault]|aitest K J "
           "area|resolve id|all|timeout s|state|hostgen n";
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
    g_lsHost = {};
    g_lsRelay.clear();
    if (g_lsEnabled && !lockstep::Prepare()) Log("live battles unavailable: %s", lockstep::Missing().c_str());
    AddChat("* Hosting on port " + std::to_string(port));
    Notice("Hosting. Everyone can pick a nation now.");
    BroadcastLobby();
    return true;
}

bool Join(const std::string& name, const std::string& ip, int port, const std::string& password, std::string& err) {
    Leave();
    g_myName = CleanName(name);
    g_password = password;
    if (g_lsEnabled) datafiles::StartScan();  // (the game data files' hash: a live battle's start compares it)
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
    if (g_role == Role::None) {
        ClientDropBattle();  // a disconnect already ended the session
        return;
    }
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
    ClearQueuedNews("the session ended");
    g_playerNews.clear();
    g_treatyNews.clear();
    g_pendingTreaty.clear();
    g_warVP.clear();
    g_treaties.clear();
    g_kWar.clear();
    g_kDecl.clear();
    g_kWarSteppedSeq = -1;
    economy::ClearState();
    warfx::ClearState();
    g_kTreatyNews.clear();
    g_treatyEnds = 0;
    g_lastTreatyEnd.clear();
    game::SetHostEndsArmsTreaty(false);
    g_econNews.clear();
    g_econNewsSeq = -1;
    g_invPlanFromHost = false;
    g_relNations.clear();
    g_relForced.clear();
    g_kLend.clear();
    g_kRevo.clear();
    g_kWill.clear();
    g_kTs.clear();
    g_kHist.clear();
    g_kEnemies.clear();
    g_kRelMonth.clear();
    g_relTurnSeq = -1;
    g_relRulesHost = false;
    g_warLineAbove.clear();
    g_kPeaceMonth.clear();  // relations v2, B4
    g_lastHostPeace.clear();
    g_peaceHost = false;
    g_peacePairs.clear();
    g_peaceStateMonth = g_peaceMonth = 0;
    g_peaceProposal = -1;
    g_forcedPeaceCode = g_forcedPeaceChance = 0;
    g_peaceRows.clear();
    g_enemyVPSeen = -1;
    g_eventRollSeq = -1;
    g_submissions.clear();
    g_haveState = g_submitted = false;
    g_submitForcedByHost = false;
    nplay::HostDetach();
    nplay::ClientReset();
    g_pendingState.clear();
    g_hostWantsTurn = g_turnRunning = g_afterTurnPending = false;
    g_autoStart = false;
    if (g_battlePhase) game::SetStrategicEnabled(true);
    g_battles.clear();
    g_invPhase.clear();
    g_battlePhase = g_battleFinishing = g_battleMerged = false;
    g_battlePhaseBytes.clear();
    ClientDropBattle();
    if (lockstep::IsHolding())
        lockstep::ReleaseHold("the session ended");
    else
        lockstep::OnPeerLost("the session ended");
    g_lsHost = {};
    g_lsRelay.clear();
    g_lsClientA.pending = false;
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
// Host, during a session: a connected player without a nation whose name holds a reservation gets it back (e.g. one who
// rejoined while the campaign wasn't on screen). Checked every few seconds; the month goes to them as usual.
static void RematchReservations() {
    static DWORD last = 0;
    DWORD now = GetTickCount();
    if (g_role != Role::Host || g_phase == Phase::Lobby || g_hostSlot <= 0 || now - last < 2000) return;
    last = now;
    if (game::NationCount() < 9) return;
    bool changed = false;
    for (auto& kv : g_players) {
        Player& p = kv.second;
        if (!p.welcomed || !p.connected || p.nation > 0) continue;
        for (auto& r : g_reserved) {
            if (r.second != p.name || PlayerOfNation(r.first)) continue;
            p.nation = r.first;
            p.want = CampaignNationName(r.first);
            changed = true;
            AddChat("* " + p.name + " is back as " + p.want);
            Log("reservation: %s gets nation %d back", p.name.c_str(), r.first);
            if (g_phase == Phase::Planning && !g_turnRunning && !game::BattleActive() && game::SaveGame()) {
                int y = 0, mo = 0;
                std::wstring dir = game::SaveDir(g_hostSlot);
                saveio::ReadDate(dir, g_hostSlot, y, mo);
                SendStateTo(p, dir, y, mo);
            }
            HostOfferRejoin(p);  // a live battle that went on without this player: theirs again
            break;
        }
    }
    if (changed) BroadcastLobby();
}

static void SyncHostCampaign() {
    if (g_role != Role::Host || g_phase != Phase::Lobby) return;
    // A battle of the host's campaign slot against a joined player's nation that can be rejoined is on screen (its
    // strategic screen is hidden): the campaign is open, in the middle of its month (phase 5b, AdoptMonthInProgress).
    lockstep::RejoinInfo ri;
    bool midMonth = !CampaignLoaded() && MonthBattleOnScreen(ri);
    std::string key = CampaignLoaded() || midMonth ? CampaignKey() : "";
    if (key != g_lastCampaignKey || (midMonth && g_hostSlot != ri.slot)) {
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
    if (midMonth && g_hostSlot == ri.slot) AdoptMonthInProgress(ri);
}

void ReleaseNation(int idx) {
    if (g_role != Role::Host) return;
    g_reserved.erase(idx);
    g_relNations.erase(idx);
    g_relForced.erase(idx);
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

// 0.7.0: the player's legacy build ended (the game's own "Continue" steps, or a forced submit): this month's turn
// carries the hand-built fleet, and the host gets a notice.
static void ClientLegacyFinished(int ships, int funds, bool forced) {
    g_legacyBuiltSeq = g_clientSeq;
    game::SetTurnButtonCaption(L"Submit");
    KV k;
    k["state"] = "done";
    k["ships"] = std::to_string(ships);
    k["funds"] = std::to_string(funds);
    if (forced) k["forced"] = "1";
    net::Send(0, net::MSG_LEGACY, KVEncode(k));
    Log("legacy fleet: built%s: %d ship(s), funds %d", forced ? " (ended by the submit)" : "", ships, funds);
    if (!forced)
        Notice("Your legacy fleet is built: " + std::to_string(ships) + " ship(s), funds " + std::to_string(funds) +
               ". Plan the month and press Submit.");
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
    if (game::LegacyActive()) {  // the time limit or the host's Advance end the legacy build as it is
        if (!g_submitForcedByHost) {
            Notice("Finish your legacy fleet first: press Continue on the strategic screen (twice).");
            return;
        }
        int ships = 0, funds = 0;
        game::FinishLegacyBuild(&ships, &funds);
        ClientLegacyFinished(ships, funds, true);
    }
    // The game's Turn asks its player about missing trade protection and foreign stations (PlayerChecksBeforeTurn);
    // the player's Submit asks the same for the player's nation, with the same choice to go back (not when the host's
    // time limit submits).
    if (!g_submitForcedByHost && !nplay::ClientTurnWarnings(g_myNation)) {
        if (!g_submitForcedByHost) g_forceSubmitPending = false;  // (the time limit may have run out meanwhile)
        Notice("Not submitted: you chose to go back at the game's warning. Press Submit when your orders are ready.");
        Log("submit: cancelled at a Turn warning");
        return;
    }
    game::SaveGame();
    Bundle b;
    std::string err;
    if (!saveio::BuildSubmission(game::SaveDir(g_clientSlot), g_clientSlot, g_myNation, g_clientIdStart, b, err,
                                 g_legacyBuiltSeq >= 0 && g_legacyBuiltSeq == g_clientSeq)) {
        Notice("Cannot package your turn: " + err);
        return;
    }
    nplay::ClientAddSubmission(b, game::SaveDir(g_clientSlot), g_clientSlot, g_myNation);  // own intelligence
    KV h;
    h["seq"] = std::to_string(g_clientSeq);
    if (g_hostGen >= 4) h["kinvt"] = W2U(inv::FormTarget());  // 0.7.0: the nation's invasion target ("" none)
    h["nation"] = std::to_string(g_myNation);
    net::Send(0, net::MSG_SUBMIT, PackHB(KVEncode(h), b.Pack()));
    g_submitted = true;
    g_forceSubmitPending = false;
    game::SetTurnButtonCaption(L"Submitted");
    Notice("Turn submitted. You can still change orders and press Submit again until the host processes the turn.");
}

// Relations v2, B4 (client): the Diplomacy list's rows of the computer's nations this PC's nation is at war with, while
// the host applies its peace with them: only "Propose peace" (a peace with all of them at once, the game's negotiation,
// at once; one a month, after the month's relation turn and not in a month whose peace event was answered, before the
// turn is submitted). Kept from the last look while the nation list is in a joined player's view (an event or the
// relation turn runs).
static std::vector<DiploRow> ComputerPeaceRows() {
    if (game::EventActive()) return g_peaceRows;
    std::vector<DiploRow> rows;
    int K = g_myNation;
    if (g_role == Role::Client && g_peaceHost && g_haveState && K >= 1 && K <= 8 && game::DiplomacyAvailable() &&
        game::CurrentSlot() == g_clientSlot && game::PlayerNation() == game::Nation(K)) {
        bool done = g_peaceMonth == g_peaceStateMonth, busy = g_peaceProposal >= 0;
        bool turnFirst = g_relTurnSeq >= 0 && g_relTurnSeq == g_clientSeq;  // the month's relation turn comes first
        bool human[9];
        for (int j = 0; j < 9; j++) human[j] = g_relSetup.human[j];
        MarkLobbyHumans(human);
        for (int j = 1; j <= 8 && j < game::NationCount(); j++) {
            if (j == K || human[j] || !game::AtWar(K, j)) continue;
            DiploRow r;
            r.nation = j;
            r.nationName = CampaignNationName(j);
            r.player = "computer";
            r.relation = "At war";
            r.tension = game::Tension(K, j);
            r.atWar = true;
            r.computer = true;
            auto it = g_peacePairs.find(j);
            if (it != g_peacePairs.end()) {
                r.vpMine = it->second.first;
                r.vpTheirs = it->second.second;
            }
            r.canPeace = !done && !busy && !turnFirst && !g_submitted;
            r.pending = busy ? "peace talks under way" : done ? "peace negotiated this month" : turnFirst ? "the month's relations come first" : g_submitted ? "turn submitted" : "";
            r.outlook = "a peace with the computer's nations is negotiated at once, with all of them";
            rows.push_back(r);
        }
    }
    g_peaceRows = rows;
    return rows;
}

std::string Diplomacy(const std::string& action, int target, const std::string& extra) {
    if (g_role == Role::Host) return HostDiplo(0, action, target, extra);
    if (g_role != Role::Client) return "Not connected.";
    if (g_phase != Phase::Planning) return "Diplomacy is possible while the players plan a month.";
    if (g_myNation <= 0) return "Claim a nation first.";
    for (auto& c : ComputerPeaceRows()) {  // relations v2, B4: a computer nation at war with this nation
        if (c.nation != target) continue;
        if (action != "peace") return "With " + c.nationName + " only peace can be proposed.";
        if (g_submitted) return "Propose peace before you submit your turn.";
        if (g_peaceProposal >= 0) return "Peace talks are under way.";
        if (g_relTurnSeq >= 0 && g_relTurnSeq == g_clientSeq)
            return "Your nation's relations of this month come first; propose peace when they are done.";
        if (!c.canPeace) return "Your nation has negotiated with the computer's nations this month already.";
        g_peaceProposal = target;
        AddLocalChat("* Peace talks with the computer's nations at war with you begin (all of them at once).");
        Changed();
        return "";
    }
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
    if (action == "claim") {
        if (!r->canClaim) return "Only the winner of an agreed peace can claim possessions.";
        int used = 0;
        std::vector<std::string> seen;
        for (auto& name : Split(extra, '|')) {
            if (name.empty() || std::find(seen.begin(), seen.end(), name) != seen.end()) continue;  // as the host
            seen.push_back(name);
            bool found = false;
            for (auto& c : r->claimable)
                if (c.name == name) {
                    used += c.value;
                    found = true;
                    break;
                }
            if (!found) return name + " cannot be claimed from " + B + ".";
        }
        if (used > r->claimBudget)
            return "Those possessions are worth " + std::to_string(used) + "; you may claim up to " +
                   std::to_string(r->claimBudget) + ".";
    }
    KV kv;
    kv["action"] = action;
    kv["target"] = std::to_string(target);
    if (!extra.empty()) kv["extra"] = extra;
    net::Send(0, net::MSG_DIPLO, KVEncode(kv));
    return "";
}

std::string DebugDiplomacyAs(int from, const std::string& action, int target, const std::string& extra) {
    if (g_role != Role::Host) return "host only";
    return HostDiplo(from, action, target, extra);
}

std::string DebugWarVP(int a, int b, int va, int vb, bool set) {
    if (g_role != Role::Host || a < 0 || b < 0 || a > 8 || b > 8 || a == b) return "error host only / bad pair";
    if (!game::TreatiesAvailable()) return "error treaties unavailable";
    auto& vp = WarVP(a, b);
    if (set) {
        bool aLow = a < b;
        vp = aLow ? std::make_pair(va, vb) : std::make_pair(vb, va);
        SaveReservations();
        BroadcastLobby();
    }
    int mineA = a < b ? vp.first : vp.second, mineB = a < b ? vp.second : vp.first;
    return "ok " + std::to_string(a) + ":" + std::to_string(mineA) + " " + std::to_string(b) + ":" + std::to_string(mineB) +
           " scale=" + std::to_string(game::WarScale());
}

std::string DebugVPInfo() {
    std::string s = "ok enemy=" + std::to_string(game::EnemyVP()) + " seen=" + std::to_string(g_enemyVPSeen) + " vp=";
    for (int i = 0; i <= 8 && i < game::NationCount(); i++)
        s += std::to_string(i) + ":" + std::to_string(game::NationVP(i)) + ",";
    return s;
}

std::string DebugKWar(const std::vector<std::string>& args) {
    if (g_role == Role::Client) {
        int own = game::OwnWarCounter();
        return "ok own=" + (own == INT_MIN ? std::string("unknown") : std::to_string(own)) +
               " game=" + std::to_string(game::WarCounter()) + " nation=" + std::to_string(g_myNation);
    }
    if (g_role != Role::Host) return "error not in a session";
    if (args.size() >= 3) {
        int K = atoi(args[1].c_str());
        if (K < 1 || K > 8) return "error nation 1-8";
        KWar(K) = atoi(args[2].c_str());
        SaveReservations();
    }
    std::string s = "ok stepped=" + std::to_string(g_kWarSteppedSeq) + " host=" + std::to_string(game::WarCounter()) +
                    " kwar=";
    for (int K = 1; K <= 8 && K < game::NationCount(); K++)
        if (hookcb::IsHumanRemoteNation(K)) s += std::to_string(K) + ":" + std::to_string(KWar(K)) + ",";
    return s;
}

std::string DebugArmsTreatyEnds() {
    return " ends=" + std::to_string(g_treatyEnds) + " last=" + (g_lastTreatyEnd.empty() ? "-" : g_lastTreatyEnd);
}

std::string DebugRelRules(const std::vector<std::string>& args) {
    if (g_role != Role::Host) return "error host only";
    if (args.size() >= 3) {
        int K = atoi(args[1].c_str());
        if (K < 1 || K > 8) return "error nation 1-8";
        if (atoi(args[2].c_str()) != 0)
            g_relForced.insert(K);
        else
            g_relForced.erase(K);
        Log("relations v2: nation %d forced to the %s rules by a test", K, g_relForced.count(K) ? "player's" : "usual");
    }
    auto list = [](const std::set<int>& s) {
        std::string o;
        for (int n : s) o += std::to_string(n) + ",";
        return o;
    };
    std::string now;
    for (int K = 1; K <= 8 && K < game::NationCount(); K++)
        if (hookcb::PlayerRulesNation(K)) now += std::to_string(K) + ",";
    return "ok rules=" + now + " forced=" + list(g_relForced) + " saved=" + list(g_relNations);
}

std::string DebugWarLine(const std::vector<std::string>& args) {
    if (g_role == Role::None) return "error not in a session";
    std::string sub = args.size() >= 2 ? args[1] : "", out;
    if (sub == "forget" || sub == "check") {
        if (g_role != Role::Host) return "error host only";
        if (sub == "forget") {
            g_warLineAbove.clear();
            Log("war line: memory cleared by a test");
        } else {
            out += " told=" + std::to_string(WarLineNews());
        }
    }
    std::string people, above;
    for (int j = 0; j <= 8 && j < game::NationCount(); j++)
        if (hookcb::WarOnlyByPlayers(j)) people += std::to_string(j) + ",";
    for (auto& kv : g_warLineAbove)
        if (kv.second) above += std::to_string(kv.first.first) + "-" + std::to_string(kv.first.second) + ",";
    return "ok people=" + people + " above=" + above + out;
}

void DebugRelTurn() {  // client: the player's relation routines now (tests; normally once a month after the event roll)
    if (g_role != Role::Client) return;
    g_relTurnSeq = -1;
    ClientRunRelations(true);
}

std::string DebugRelLast() {
    if (g_role == Role::Host) return "ok " + (g_lastHostRelations.empty() ? std::string("none") : g_lastHostRelations);
    if (g_role == Role::Client) {
        std::string state = g_relTurnSeq >= 0 ? " pending" : "";
        return "ok " + g_lastRelations + state;
    }
    return "error not in a session";
}

void DebugPeaceTest(int chance, int lossGain, bool force) {  // client: the peace part now, through the proposal path
    if (g_role != Role::Client) return;
    Log("peacetest %d %d %d", chance, lossGain, force ? 1 : 0);
    ClientRunPeace(chance, lossGain, force, true);
}

std::string DebugPeaceEvent(int code, int chance) {
    if (g_role != Role::Client) return "error client only";
    if (code != 0 && (code < 33 || code > 36)) return "error code 33-36 (0: none)";
    if (chance < 0 || chance > 200) return "error chance 0-200 (0: the answer's)";
    g_forcedPeaceCode = code;
    g_forcedPeaceChance = code ? chance : 0;
    return "ok the next relation turn shows peace event code " + std::to_string(code) +
           (g_forcedPeaceChance ? ", its NegotiatePeace with the chance " + std::to_string(g_forcedPeaceChance) : "");
}

std::string DebugPeaceLast() {
    if (g_role == Role::Host) return "ok " + (g_lastHostPeace.empty() ? std::string("none") : g_lastHostPeace);
    if (g_role == Role::Client)
        return "ok " + g_lastPeace + " host=" + (g_peaceHost ? "1" : "0") +
               " month=" + (g_peaceMonth == g_peaceStateMonth ? "used" : "open") +
               (g_peaceProposal >= 0 ? " proposal=" + std::to_string(g_peaceProposal) : "") +
               " forced=" + std::to_string(g_forcedPeaceCode) +
               (g_forcedPeaceChance ? ":" + std::to_string(g_forcedPeaceChance) : "");
    return "error not in a session";
}

std::string DebugKWarVP(const std::vector<std::string>& args) {
    if (g_role != Role::Host) return "error host only";
    int K = args.size() > 1 ? atoi(args[1].c_str()) : -1;
    if (K < 1 || K > 8) return "error nation 1-8";
    if (args.size() >= 5) {
        int j = atoi(args[2].c_str()), a = atoi(args[3].c_str()), b = atoi(args[4].c_str());
        if (j < 1 || j > 8 || j == K || !LedgerPair(K, j))
            return "error not a war of the ledger (K under the player's rules, J a computer nation)";
        auto& w = WarVP(K, j);
        w = K < j ? std::make_pair(a, b) : std::make_pair(b, a);
        SaveReservations();
    }
    int vp = 0, evp = 0;
    std::string pairs;
    KLedger(K, vp, evp, &pairs);
    std::string kept;  // every war score of K the host keeps (also one whose war already ended)
    for (auto& w : g_warVP)
        if (w.first.first == K || w.first.second == K) {
            int j = w.first.first == K ? w.first.second : w.first.first;
            kept += std::to_string(j) + ":" + std::to_string(w.first.first == K ? w.second.first : w.second.second) +
                    ":" + std::to_string(w.first.first == K ? w.second.second : w.second.first) + ",";
        }
    int now = HostMonth();
    return "ok nation=" + std::to_string(K) + " rules=" + (hookcb::PlayerRulesNation(K) ? "1" : "0") + " vp=" +
           std::to_string(vp) + " evp=" + std::to_string(evp) + " pairs=" + pairs + " kept=" + kept + " peace=" +
           (g_kPeaceMonth.count(K) && g_kPeaceMonth[K] == now ? "used" : "open") + " kwar=" + std::to_string(KWar(K));
}

std::string DebugTerms(int a, int b) {
    if (g_role != Role::Host || !game::TreatiesAvailable()) return "error host only / treaties unavailable";
    std::string s;
    auto ti = g_treaties.find(PairKey(a, b));
    if (ti != g_treaties.end()) {
        const auto& t = ti->second;
        s = "agreed winner=" + std::to_string(t.winner) + " loser=" + std::to_string(t.loser) + " level=" +
            std::to_string(t.level) + " budget=" + std::to_string(TreatyBudget(t.level)) + " claims=";
        for (auto& c : t.claims) s += c + "|";
    } else {
        DipSnap::War w = WarOutlook(a, b);
        s = "outlook winner=" + std::to_string(w.winner) + " level=" + std::to_string(w.level) + " vp=" +
            std::to_string(w.vpLo) + ":" + std::to_string(w.vpHi);
    }
    return "ok " + s;
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

void DebugEvent(int forcedIdx) {
    if (g_role == Role::Client && !game::EventActive()) {
        g_eventRollSeq = -1;  // replaces this month's random roll
        ClientRunEvent(forcedIdx);
    }
}

void DebugEventChance(int pct) { g_eventChancePct = std::max(0, std::min(100, pct)); }

void DebugIntelCheck() {
    if (g_role == Role::Client && g_haveState && !game::EventActive()) ClientIntelCheck();
}

std::string DebugLastEvent() { return g_role == Role::Host ? g_lastHostEvent : g_lastEvent; }

// Tests and diagnostics: live battles (`ls ...`, see lockstep::Debug). Session-level: `ls enable 0|1`, `ls session`,
// `ls autostrike [0|1]` (the host's option "Computer flies carrier strikes in live battles", as its check box sets it),
// `ls rejoin [nation]` (host: a dropped player's live battle is theirs again, see HostOfferRejoin).
std::string DebugLockstep(const std::vector<std::string>& a) {
    std::string c = a.size() > 1 ? a[1] : "status";
    if (c == "enable") {
        g_lsEnabled = a.size() < 3 || a[2] != "0";
        if (g_lsEnabled && g_role == Role::Host) lockstep::Prepare();
        return std::string("ok enabled=") + (g_lsEnabled ? "1" : "0");
    }
    if (c == "autostrike") {
        if (a.size() > 2) SetAutoStrike(a[2] != "0");
        return std::string("ok autostrike=") + (g_autoStrike ? "1" : "0");
    }
    if (c == "rejoin") {  // ls rejoin [nation] (host): a live battle that went on without that nation's player is
        if (g_role != Role::Host) return "error host only";  // theirs again (tests: also without a reconnect)
        int n = a.size() > 2 ? atoi(a[2].c_str()) : -1;
        if (n <= 0) {
            lockstep::RejoinInfo ri;
            lockstep::Rejoinable(ri);
            n = ri.opp;
            for (auto& r : g_battles)
                if (n <= 0 && r.pvp && r.state == 1) n = r.opp;
        }
        Player* p = n > 0 ? PlayerOfNation(n) : nullptr;
        if (!p) return "error no connected player of nation " + std::to_string(n);
        std::string why;
        if (!HostOfferRejoin(*p, &why, true)) return "error " + why;
        return "ok " + p->name + " nation=" + std::to_string(n);
    }
    if (c == "session") {
        std::string out = std::string("ok enabled=") + (g_lsEnabled ? "1" : "0") + " available=" +
                          (lockstep::Available() ? "1" : "0") + " role=" + (g_lsRole ? std::string(1, g_lsRole) : "-") +
                          " hostOpp=" + std::to_string(g_lsHost.opp) + " hostBid=" + std::to_string(g_lsHost.bid) +
                          " testSlotMode=" + (g_lsHost.testSlotMode ? "1" : "0") + " leave=" + (g_lsLeave ? "1" : "0") +
                          " clientA=" + (g_lsClientA.pending ? "1" : "0") + " slotMode=" +
                          (game::BattleSlotMode() ? "1" : "0") + " autostrike=" + (g_autoStrike ? "1" : "0") + " relays=";
        for (auto& r : g_lsRelay)
            out += std::to_string(r.first) + ":" + std::to_string(r.second.first) + "/" + std::to_string(r.second.second) + ",";
        out += " players=";
        for (auto& kv : g_players)
            if (kv.second.welcomed)
                out += kv.second.name + "(" + std::to_string(kv.second.nation) + (kv.second.battles ? ",b" : "") +
                       (kv.second.lockstep ? ",ls" : "") + ");";
        out += " level=" + std::to_string(MP_LS_LEVEL) + " levels=";  // this PC's live-battle engine, the players'
        for (auto& kv : g_players)
            if (kv.second.welcomed) out += kv.second.name + ":" + std::to_string(kv.second.lsLevel) + ";";
        // the host's month runs (a turn, or a month taken up after a restart: phase 5b) and its campaign slot
        out += std::string(" turn=") + (g_turnRunning ? "1" : "0") + " hostSlot=" + std::to_string(g_hostSlot);
        return out;
    }
    return lockstep::Debug(a);
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
    lockstep::Tick();
    // 0.7.0: a legacy build belongs to a joined player's session (its skips must not outlive it)
    if (g_role != Role::Client && game::LegacyActive()) game::LegacyAbort();
    // Client B of a live battle: back to the player's own game once the battle screen is closed.
    if (g_lsLeave && !game::BattleActive() && !game::IsBusy() && !lockstep::Busy()) {
        g_lsLeave = false;
        Log("live battle: leaving the battle slot");
        LeaveBattleSlot();
    }
    // A battle slot's battle that outlived its session (left or disconnected) ends as a single-player battle; on the host,
    // a test battle outside a month (its battle slot mode: its close runs no month) is over.
    if (g_role != Role::Client && game::BattleSlotMode() && !game::BattleActive() && !game::IsBusy() &&
        !lockstep::Busy()) {
        game::SetBattleSlotMode(false);
        Log("battle slot mode ended (%s)", g_role == Role::Host ? "the host's test battle outside a month is over"
                                                                 : "no session: the battle that outlived it is over");
    }
    if (g_role == Role::Host) {
        if (g_lsHost.opp >= 0 && lockstep::IsHolding() && now - g_lsHost.lastTry > 500) HostTryStartLockstep();
        // A rejoin that never got to start (given up at the boundary, cancelled): the host's battle has no player now.
        if (g_lsHost.opp >= 0 && !lockstep::IsHolding() && !lockstep::Active() && !lockstep::RejoinPending())
            g_lsHost.opp = -1;
        SyncHostCampaign();
        RematchReservations();
        HostResumeTick();
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
        if (g_relSavePending && g_phase == Phase::Planning && !g_turnRunning)
            SaveAfterRelations("the relation changes applied while the game was busy", 0);
        if (g_turnRunning) {
            if (g_afterTurnPending) {
                // During the battle phase the strategic screen is locked (IsBusy), so only a modal dialog waits it.
                bool idle = g_battlePhase ? !game::ModalOpen() && !game::BattleActive()
                                          : !game::IsBusy() && !game::BattleActive();
                if (idle && !lockstep::Busy()) {
                    if (!g_battles.empty() && !BattlesDone()) {
                        BattlePhaseTick();  // the players fight their battles; the new month waits for them
                    } else if (g_battles.empty() || FinishBattlePhase()) {
                        g_afterTurnPending = false;
                        g_turnRunning = false;
                        ApplyDiplomacy();
                        HostRunInvasions();  // 0.7.0: after the battles, before the war counters' step
                        KWarMonthStep();
                        BroadcastState();
                    } else {
                        g_afterTurnPending = false;
                        g_turnRunning = false;
                    }
                }
            } else if (lockstep::BattleLost() && !game::BattleActive()) {
                // A live battle's screen could not be restored: the month is half done; it goes on once the host
                // loads the battle save (resending the old month now would undo it).
                g_idleSinceTick = 0;
                if (now - g_lastStatusTick > 5000) {
                    g_lastStatusTick = now;
                    BroadcastStatus("The host must load the battle save to finish the battle...");
                }
            } else if (!game::IsBusy() && !game::BattleActive() && !lockstep::Busy()) {
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
        if (!g_pendingTreaty.empty() && g_phase == Phase::Planning && !g_turnRunning && !game::IsBusy() &&
            !game::BattleActive() && !game::EventActive()) {
            std::vector<std::wstring> lines;
            for (auto& l : g_pendingTreaty) lines.push_back(U2W(l));
            g_pendingTreaty.clear();
            Log("showing a peace treaty (%d lines)", (int)lines.size());
            game::ShowPeaceTreaty(lines);
        }
    } else if (g_role == Role::Client) {
        ClientBattleTick();
        if (g_lsClientA.pending && now - g_lsClientA.lastTry > 500) ClientTryStartLockstep();
        if (!g_pendingState.empty()) ClientApplyPendingState();
        if (g_forceSubmitPending && !g_submitted && g_haveState && !game::IsBusy()) SubmitTurn();
        // The messages of the player's economy rules the host ran for this nation, in the game's turn report, first.
        if (g_econNewsSeq >= 0 && g_econNewsSeq == g_clientSeq && g_haveState && g_pendingState.empty() &&
            !game::IsBusy() && !game::BattleActive() && !game::EventActive()) {
            std::vector<std::wstring> lines;
            lines.swap(g_econNews);
            g_econNewsSeq = -1;
            Log("economy: showing %d message(s) of this nation's money and war rules", (int)lines.size());
            game::ShowTurnMessagesOf(lines);
        }
        // 0.7.0: the legacy build: the game's picture message once the month is shown, then its end (back in normal
        // play after the game's own "Continue" steps). The month's event roll and relation routines wait for it.
        if (g_legacyMsgDue && g_haveState && g_pendingState.empty() && !game::IsBusy() && !game::BattleActive()) {
            g_legacyMsgDue = false;
            game::LegacyShowStartMessage();
        }
        {
            int ships = 0, funds = 0;
            if (game::LegacyActive() && game::LegacyCheckDone(&ships, &funds)) ClientLegacyFinished(ships, funds, false);
        }
        // This month's event roll (once per calendar month, see ClientRunEvent).
        if (g_eventRollSeq >= 0 && g_eventRollSeq == g_clientSeq && g_haveState && g_phase == Phase::Planning &&
            !g_submitted && g_pendingState.empty() && !game::IsBusy() && !game::BattleActive() &&
            !game::LegacyActive() && !g_legacyMsgDue) {
            g_eventRollSeq = -1;
            ClientRunEvent(-1);
        }
        // Then the player's relation routines (relations v2, B3), once the roll and its dialog are done.
        if (g_relTurnSeq >= 0 && g_relTurnSeq == g_clientSeq && g_eventRollSeq < 0 && g_haveState &&
            g_phase == Phase::Planning && !g_submitted && g_pendingState.empty() && !game::IsBusy() &&
            !game::BattleActive() && !game::EventActive()) {
            g_relTurnSeq = -1;
            ClientRunRelations();
        }
        // A "Propose peace" to the computer's nations (relations v2, B4), once the month's turn is done.
        if (g_peaceProposal >= 0 && (g_relTurnSeq < 0 || g_relTurnSeq != g_clientSeq) &&
            (g_eventRollSeq < 0 || g_eventRollSeq != g_clientSeq) && g_haveState &&
            g_phase == Phase::Planning && g_pendingState.empty() && !game::IsBusy() && !game::BattleActive() &&
            !game::EventActive()) {
            g_peaceProposal = -1;
            if (g_submitted)
                Log("peace: the proposal was dropped (the turn is submitted)");
            else
                ClientRunPeace(40, 0, false, false);
        }
        if (!g_pendingTreaty.empty() && g_haveState && g_pendingState.empty() && !game::IsBusy() &&
            !game::BattleActive() && !game::EventActive()) {
            std::vector<std::wstring> lines;
            for (auto& l : g_pendingTreaty) lines.push_back(U2W(l));
            g_pendingTreaty.clear();
            Log("showing a peace treaty (%d lines)", (int)lines.size());
            game::ShowPeaceTreaty(lines);
        }
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
        size_t humanRows = v.diplo.size();
        if (g_role == Role::Client) {  // relations v2, B4: the computer's nations at war with this nation
            auto rows = ComputerPeaceRows();
            v.diplo.insert(v.diplo.end(), rows.begin(), rows.end());
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
        else if (humanRows == 0)
            v.diploNote = "peace with the computer's nations is negotiated at once.";
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
        // 0.7.0: during the legacy build the Turn button is the game's own "Continue" (its start-build steps)
        if (game::LegacyActive() && game::StartBuildPhase() != 2) return false;
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

bool InSession() { return mp::g_role != mp::Role::None; }
bool JoinedPlayerPC() { return mp::g_role == mp::Role::Client; }

void OnAfterEndOfTurn(void* self) {
    ResetPeaceMask();
    game::ResetRelationMasks();
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

bool PlayerRulesNation(int idx) {
    using namespace mp;
    if (g_role != Role::Host || g_phase == Phase::Lobby || idx < 1 || idx > 8 || !IsHumanRemoteNation(idx)) return false;
    if (g_relForced.count(idx)) return true;
    for (auto& p : g_players)
        if (p.second.connected && p.second.welcomed && p.second.nation == idx) return p.second.relLevel >= 1;
    return g_relNations.count(idx) > 0;
}

int KWarCounter(int idx) {
    if (idx < 1 || idx > 8 || !IsHumanRemoteNation(idx)) return game::WarCounter();
    return mp::KWar(idx);
}

bool WarOnlyByPlayers(int idx) {
    using namespace mp;
    if (idx < 0 || idx > 8) return false;
    // The host: a nation reserved or claimed by a player, connected or not (as a joined player's PC sees the header's
    // `humans` and the lobby's); its own CheckForWar sees those nations as players (game.cpp MaskAsPlayer).
    if (g_role == Role::Host) return idx > 0 && IsHumanRemoteNation(idx);
    if (g_role != Role::Client || !g_relRulesHost || g_myNation < 1 || idx == g_myNation) return false;
    if (idx == 0) return true;
    bool human[9];
    for (int j = 0; j < 9; j++) human[j] = g_relSetup.human[j];
    MarkLobbyHumans(human);
    return human[idx];
}

void CreditWarVP(int gainer, int loser, int vp, const char* why) {
    using namespace mp;
    int n = game::NationCount();
    if (vp <= 0 || gainer < 0 || loser < 0 || gainer > 8 || loser > 8 || gainer == loser || gainer >= n || loser >= n)
        return;
    std::string score;
    if (((IsHumanNation(gainer) && IsHumanNation(loser)) || LedgerPair(gainer, loser)) && game::AtWar(gainer, loser)) {
        auto& w = WarVP(gainer, loser);  // a new score starts before these points
        (gainer < loser ? w.first : w.second) += vp;
        score = ", war score " + std::to_string(std::min(gainer, loser)) + "-" + std::to_string(std::max(gainer, loser)) +
                " " + std::to_string(w.first) + ":" + std::to_string(w.second);
    }
    int before = game::NationVP(gainer);
    game::SetNationVP(gainer, before + vp);
    Log("victory points: nation %d +%d (%s, in its war with nation %d) -> %d%s", gainer, vp, why ? why : "", loser,
        before + vp, score.c_str());
}

void KWarScore(int K, int& vp, int& evp) {
    using namespace mp;
    vp = evp = 0;
    if (K < 1 || K > 8 || K >= game::NationCount()) return;
    for (int j = 0; j <= 8 && j < game::NationCount(); j++) {
        if (j == K || !game::AtWar(K, j) || !(LedgerPair(K, j) || (IsHumanNation(K) && IsHumanNation(j)))) continue;
        auto& w = WarVP(K, j);
        vp += K < j ? w.first : w.second;
        evp += K < j ? w.second : w.first;
    }
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

// The game copied the open save to another slot. On a joined player's PC the save is the host's campaign with the
// host's nation as nation 0, which the game always plays: the copy is turned into the player's own campaign.
void OnAfterCopyGame(int from, int to) {
    using namespace mp;
    if (from != g_clientSlot || to == from || to <= 0) return;
    std::wstring src = game::SaveDir(from), dst = game::SaveDir(to);
    int K = g_role == Role::Client && g_myNation > 0 ? g_myNation : -1;
    if (K < 0) {  // not in a session (any more): the nation this slot was last played as
        std::string s;
        if (ReadFileBytes(src + L"rtw3mp_player.ini", s)) K = KVInt(KVDecode(s), "nation", -1);
    }
    if (K < 1 || K > 8) {
        Log("copy of slot %d to %d: no joined nation known; left as it is", from, to);
        return;
    }
    std::string a, b;
    if (!ReadFileBytes(src + L"RTWGame" + std::to_wstring(from) + L".bcs", a) ||
        !ReadFileBytes(dst + L"RTWGame" + std::to_wstring(to) + L".bcs", b) || a != b) {
        Log("copy of slot %d to %d: not copied", from, to);
        return;
    }
    std::vector<int> eut;
    if (g_role == Role::Client && !g_evUsedKey.empty()) eut.assign(g_evUsed, g_evUsed + 100);
    std::wstring mine = game::NationName(K), host = game::NationName(0);
    std::string summary, err;
    HWND owner = GetActiveWindow();
    if (saveio::MakeNationPlayer(dst, to, K, eut, summary, err,
                                 g_role == Role::Client ? game::OwnWarCounter() : INT_MIN)) {
        MessageBoxW(owner,
                    (L"Slot " + std::to_wstring(to) + L" is your own copy of the campaign: you play " + mine + L".\n\n" +
                     host + L", the host's nation, is run by the computer in this copy. The multiplayer session is "
                     L"not affected.")
                        .c_str(),
                    L"Multiplayer", MB_OK | MB_ICONINFORMATION);
    } else {
        Log("copy of slot %d to %d for nation %d failed: %s", from, to, K, err.c_str());
        MessageBoxW(owner,
                    (L"The copy in slot " + std::to_wstring(to) + L" could not be changed to your nation (" + U2W(err) +
                     L"). It plays as " + host + L", the host's nation.")
                        .c_str(),
                    L"Multiplayer", MB_OK | MB_ICONWARNING);
    }
}

// The game ends the host's last war (DoPeace resets the host's VP and EnemyVP to 0): first share what the host's
// enemies scored this month while they are still at war, then start counting from the reset score.
void OnHostPeace(bool after) {
    using namespace mp;
    if (g_role != Role::Host || g_phase == Phase::Lobby) return;
    if (!after)
        ShareEnemyVP();
    else
        g_enemyVPSeen = game::EnemyVP();
}

// A battle gave victory points: give a joined player's nation what it scored against the host, and count the points
// for the war score of a war between two players.
void OnBattleVP(int a, int b, int vpA, int vpB) {
    using namespace mp;
    if (g_role != Role::Host || g_phase == Phase::Lobby) return;
    if (a == 0 || b == 0) {  // the host's battle: the game put the enemy's points into EnemyVP only
        int enemy = a == 0 ? b : a, gain = a == 0 ? vpB : vpA;
        if (gain > 0) {
            if (IsHumanRemoteNation(enemy)) {
                game::SetNationVP(enemy, game::NationVP(enemy) + gain);
                Log("victory points: nation %d +%d in its battle against the host -> %d", enemy, gain,
                    game::NationVP(enemy));
            }
            g_enemyVPSeen = g_enemyVPSeen >= 0 ? g_enemyVPSeen + gain : game::EnemyVP();
        }
    }
    bool players = IsHumanNation(a) && IsHumanNation(b);
    if ((!players && !LedgerPair(a, b)) || !game::AtWar(a, b)) return;  // B4: also a joined nation's computer wars
    bool seeded = false;
    auto& vp = WarVP(a, b, &seeded);
    if (seeded) {  // the nations' points already include this battle
        Log("war score %d-%d: first battle, already counted in the start %d:%d", std::min(a, b), std::max(a, b),
            vp.first, vp.second);
        return;
    }
    if (a < b) {
        vp.first += vpA;
        vp.second += vpB;
    } else {
        vp.first += vpB;
        vp.second += vpA;
    }
    Log("war score %d-%d: +%d/+%d -> %d:%d", std::min(a, b), std::max(a, b), a < b ? vpA : vpB, a < b ? vpB : vpA,
        vp.first, vp.second);
}

// The game just counted down the host nation's design studies (end of month, or after a battle): do the same
// for the joined players' nations, once per month.
void OnAfterDesignStudies(void* self) {
    using namespace mp;
    if (g_role != Role::Host || g_phase == Phase::Lobby) return;
    if (g_designSeq == g_seq) {
        Log("design studies: already advanced this month");
        return;
    }
    g_designSeq = g_seq;
    for (int n = 1; n <= 8 && n < game::NationCount(); n++)
        if (IsHumanRemoteNation(n)) AdvanceDesignStudiesFor(n);
}

// The game just counted down the host nation's pending doctrine changes (training priorities, missile storage
// policy): do the same for the joined players' nations, once per month, and tell them when a change takes effect.
void OnAfterReduceTimeLimits(void* self) {
    using namespace mp;
    if (g_role != Role::Host || g_phase == Phase::Lobby || !game::DoctrineAvailable()) return;
    if (g_doctrineSeq == g_seq) {
        Log("doctrine: already advanced this month");
        return;
    }
    g_doctrineSeq = g_seq;
    for (int n = 1; n <= 8 && n < game::NationCount(); n++) {
        if (!IsHumanRemoteNation(n)) continue;
        game::DoctrineState before = game::GetDoctrine(n);
        game::DoctrineChange c = game::AdvanceDoctrine(n);
        if (before.pendingMonths > 0 || before.missileMonths > 0)
            Log("doctrine nation %d: training %d -> %d months%s, missile storage %d -> %d months%s", n,
                before.pendingMonths, game::GetDoctrine(n).pendingMonths, c.trainingApplied ? " (applied)" : "",
                before.missileMonths, game::GetDoctrine(n).missileMonths, c.missilesApplied ? " (applied)" : "");
        if (c.trainingApplied)
            g_playerNews[n].push_back("Doctrine: new training applied. Crews are now deemed proficient in the new tactics!");
        if (c.missilesApplied)
            g_playerNews[n].push_back("Doctrine: missile stocks are now adapted to the new missile storage policy.");
    }
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

bool OnAIBattle(int a, int b, int countA, int countB, const std::wstring& area) {
    return mp::HostDeferBattle(a, b, countA, countB, area);
}

void OnBattleGenerator() { mp::HostRunBattleGenerators(); }

void OnDoBattleDone(int opp, bool result) {
    Log("DoBattle(opponent %d) -> %d, battle screen %s", opp, result ? 1 : 0, game::BattleActive() ? "open" : "closed");
    if (game::BattleActive()) mp::HostOnBattleStarted(opp);
}

void OnAfterCampaignLoad() {
    lockstep::OnCampaignLoaded();  // a resumed battle saved in a live battle: the computer commands side 1
}
void OnBattleSlotSkippedTurn() { lockstep::OnBattleSlotClosed(); }
void OnStrategicTurnRuns() { lockstep::OnMonthRuns(); }
void OnCampaignLoading() { lockstep::OnCampaignLoading(); }

}  // namespace hookcb

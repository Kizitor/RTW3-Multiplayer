#pragma once
#include "common.h"

// Multiplayer session: lobby, nation claims, per-month state sync, submissions and host turn processing.
// Everything here runs on the game's main thread.
namespace mp {

enum class Role { None, Host, Client };
enum class Phase { Lobby, Planning, Processing };

struct PlayerView {
    int peer = -1;
    std::string name;
    int nation = -1;
    std::string nationName;
    bool connected = false;
    bool submitted = false;
    int ping = -1;
};

// A pickable nation. index = campaign nation index (1..8), or -1 while the host has no campaign yet.
struct NationView {
    int index = -1;
    std::string name;
    std::string claimedBy;
};

// Relation between my nation and another player's nation.
struct DiploRow {
    int nation = -1;
    std::string nationName, player;
    std::string relation;  // "At war", "Allied", "Peace"
    int tension = 0;       // the game's relation-bar value (above 40 = war)
    bool atWar = false, allied = false;
    std::string pending;   // proposals and decisions waiting for the end of the month
    bool canWar = false, canAlly = false, allyAccept = false, canPeace = false, peaceAccept = false;
    bool canLeave = false, canCancel = false, cancelIsDecline = false;
};

struct View {
    Role role = Role::None;
    Phase phase = Phase::Lobby;
    std::string myName;
    int myNation = -1;
    std::string myNationName;
    std::string hostName;
    std::vector<PlayerView> players;
    std::vector<NationView> nations;
    std::string status;
    std::string hint;  // what to do next (lobby)
    std::string date;
    int remainingSec = -1;
    int turnLimitMin = 0;
    bool campaignLoaded = false;
    bool campaignReady = false;
    bool submitted = false;
    std::vector<DiploRow> diplo;
    bool diploOpen = false;  // diplomatic requests are possible now
    std::string diploNote;
};

void Init();
void Tick();
void OnNet();

bool Host(const std::string& name, int port, const std::string& password, int turnLimitMin, std::string& err);
bool Join(const std::string& name, const std::string& ip, int port, const std::string& password, std::string& err);
void Leave();

// Returns 0 = started, 1 = waiting for a campaign, 2 = waiting for campaign setup, -1 = not possible.
// When not ready yet, the session starts automatically once the host's campaign is ready.
int StartSession();
void AdvanceNow();
void ClaimNation(const std::string& nationName);
void ReleaseNation(int idx);
// Gives a nation (by name) back to the AI, also before a campaign exists. Returns "" or a reason.
std::string ReleaseNationByName(const std::string& name);
void SetTurnLimit(int minutes);
void SendChat(const std::string& text);
void SubmitTurn();

// Diplomacy with another player's nation (campaign index): "war", "ally" (offer or accept), "peace" (offer or
// accept), "leave" (the alliance), "cancel" (withdraw my request or decline theirs). Takes effect when the host
// processes the month. Returns "" or why it is not possible right now.
std::string Diplomacy(const std::string& action, int targetNation);
std::string DebugDiplomacyAs(int fromNation, const std::string& action, int targetNation);  // tests (host)
void DebugReserve(int nation, const std::string& player);                                    // tests (host)
void DebugApplyDiplomacy();  // tests (host): run the month-end diplomacy step now
void DebugEvent(int forcedIdx);  // tests (client): roll this month's event now (-1 = random, else that event)
void DebugEventChance(int pct);  // tests (client): chance of an event per month (default 33)
std::string DebugLastEvent();    // tests: last event sent (client) or applied (host)

View GetView();
const std::vector<std::string>& Chat();
bool CampaignReady();

extern std::function<void()> g_onChanged;   // UI refresh
extern std::function<void(const std::string&)> g_onNotice;  // important notices (flash UI)

}  // namespace mp

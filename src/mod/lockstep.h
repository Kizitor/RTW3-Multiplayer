#pragma once
#include "common.h"

// Live player-vs-player tactical battles in lockstep. Both PCs run the same battle from the same snapshot and RNG seed;
// only orders and clock decisions travel, at battle-minute boundaries, with a state hash per minute. The authority
// (A, side 0) owns the clock and the snapshot; the other player (B, side 1) follows. See lockstep.cpp.
namespace lockstep {

// ---- the engine (battle internals; also used by the poc* test commands) ----
bool Resolve();                 // finds the battle routines and variables (by name and code patterns)
std::string Missing();          // what Resolve could not find
bool EnsureHooks();             // installs the battle hooks (once; they do nothing outside a lockstep battle or a POC)
void* BattleForm();             // the battle screen (TfrmMain) or nullptr
bool BattleVisible();
int Minute();                   // battle minutes elapsed (the game's minute counter)
std::string ClockText();        // battle time hh:mm
std::string HashLine(const char* tag);  // the per-minute state hash as a log line (resets the draw counters)
uint64_t StateHashValue();
bool DumpState(const std::wstring& path);
std::string StateText();        // the POC's `pocstate` line
// The RNG: mode 0 = the game's own, 1 = one seeded stream, 2 = seeded + display draws on their own stream.
void SetRng(int mode, uint32_t seed);
std::string RngStats();
void PocHashOn(bool on);        // log a hash at every minute boundary (POC)
void PocGate(int mode, int tokens);  // 0 off, 1 on, 2 add tokens (POC): minute starts need a token
std::string PocGateState();
bool RunMinutes(int n);         // the Run buttons' routine (StartRun), bypassing the lockstep clock
bool SetSpeed(int s);           // the speed box's routine
bool SaveSac(const std::wstring& path);
bool SetSideControl(int side, bool ai, bool human);
bool SetPlayerSide(int side);   // the battle's human side (display test; the simulation depends on it)
bool SetDivisionOrder(int idx, int course, int speed, int ai);  // as the course/speed boxes do (tests)

// ---- live sessions ----
typedef std::function<void(const std::string& payload)> SendFn;  // to the other PC (session routes it)

struct StartInfo {
    int bid = 0;               // battle id (unique per host session)
    int slot = -1;             // the save slot the battle lives in on this PC (-1: the open slot)
    int opp = -1;              // A: BattleOpponentIdx (the save marks the battle in progress with it)
    std::string myName;        // this PC's player name (shown to the other player)
    std::string peerName;      // the other player's name (chat lines)
    std::string sideNames[2];  // nation names of side 0 and side 1
    SendFn send;
};

// Callbacks the session sets once.
struct Hooks {
    std::function<void(const std::string&)> chat;    // a chat line on this PC
    std::function<void(const std::string&)> notice;  // an important notice on this PC
    // The live battle ended on this PC (both roles); `normal` = the game's own end (results shown). B leaves the battle
    // slot once the battle screen is closed; A goes on (as a single-player battle if the other player was dropped).
    std::function<void(bool normal, const std::string& why)> onEnded;
    // B: a start arrived: may this PC take part (live battles on, not in another battle)? A refusal is final.
    std::function<bool(std::string& why)> canLoad;
    // B: may the battle be loaded now (no dialog or event open)? Called until true (then the player's own plans are
    // saved); the start waits meanwhile (A's load timeout applies).
    std::function<bool()> loadReady;
    // B: the battle slot to load into.
    std::function<int()> battleSlot;
    std::function<void(bool on)> slotMode;  // B: battle slot mode on/off (the close must not run a month)
};
void SetHooks(const Hooks& h);

bool Available();       // the engine resolved (live battles possible with this game version)
bool Prepare();         // resolves the engine when a session starts (hooks are installed with the first live battle)
bool Active();          // a lockstep battle runs on this PC (A or B), including its start
bool Busy();            // don't load a month or touch the open save now
bool IsAuthority();     // this PC is A
int BattleId();
bool IsHolding();       // A: held at the start, waiting for StartAsA

// A: hold the battle at its start (no minute may run) until StartAsA or ReleaseHold. bid = the battle id StartAsA will
// use (messages for it, e.g. a relayed `peerlost`, are handled while held).
void Hold(const std::string& why, int bid);
// A battle slot's close skipped the campaign's month (tests, joined players' battle slots): put back the campaign date
// recorded when this PC's live battle opened (the game's resume path writes the battle's day and month into it).
void OnBattleSlotClosed();
// The campaign's month runs (DoStrategicTurn), or a campaign is loaded outside this PC's live battle: the recorded
// campaign date is dropped.
void OnMonthRuns();
void OnCampaignLoading();
void ReleaseHold(const std::string& why);
// A: the battle screen is up (minute 0): snapshot, send it to B, reload it here too and run in lockstep. False with
// `retry` set: not now (a dialog is open), call again later.
bool StartAsA(const StartInfo& info, std::string& err, bool& retry);
// A message for this PC's lockstep (B: also the start). replyTo sends to the other PC.
void OnMessage(const std::string& payload, SendFn replyTo);
// The other PC is gone (disconnected). A: side 1 goes to the AI; B: the battle is abandoned.
void OnPeerLost(const std::string& why);
void Tick();            // timeouts, heartbeats, order detection while paused (every dispatcher tick)
// Forgets the hook frames of battle routines an exception unwound (called on every dispatcher message).
void PruneStale();
// A: a failed (re)load left no battle screen and the battle save could not be reopened: the month must not go on until
// the player loads the slot (the session doesn't resend the month meanwhile).
bool BattleLost();
std::string Status();
std::string Debug(const std::vector<std::string>& args);  // bridge `ls ...`
bool DebugIsAsync(const std::vector<std::string>& args);  // `ls` commands that load or open dialogs
// A lockstep message's battle id (the host relays by it); -1 if unreadable.
int MessageBattleId(const std::string& payload);
std::string PeerLostMessage(int bid, const std::string& why);  // what the host relays when one player disconnects

}  // namespace lockstep

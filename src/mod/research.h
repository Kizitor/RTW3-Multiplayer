#pragma once
#include "common.h"

// 0.8.2 (fix-research): a joined nation's monthly research as the game runs it for its player, and its research messages
// on its player's PC (research.cpp).
//
// The game's TfrmBuildCamp.DoResearch(aNationIdx) shows its research messages (picture messages, "Research breakthrough"
// dialogs, the air/sea rescue question) and runs its player's rules only for nation 0 / PlayerNation; for any other
// nation it runs the computer's research rules and gives the player intelligence reports instead. On the host a joined
// nation K's research therefore ran as the computer's, its player saw none of it. Now the host runs K's month as the
// game runs it for its player: DoResearch(0) in K's view (game::RunPlayerResearch), its dialogs recorded instead of shown
// (pictures, research results, the question), its turn messages and intelligence reports K's; K's player gets them with
// the next month (the state's `kres`: the dialogs, shown with the game's own dialogs on its PC; `econnews`: the turn
// report lines), and answers the question there. The host keeps its own intelligence on K's research.
namespace research {

// After game::InstallHooks (MinHook is set up there): resolves the routines and dialogs and hooks
// TdlgPictureMessage.ShowResearchResult. False: unavailable (the host keeps the computer's research for joined nations,
// a joined player's PC shows no research dialogs).
bool Install();
bool Available();

// Host, from DoResearch's hook: true = joined nation `idx`'s research of the month ran as its player's (the game's call
// is done). False: the game's own routine is to run (not a joined nation, a test's call, unavailable).
bool HostRun(void* form, int idx, bool noNonGunMessages);

// Host: nation K's research messages since the last call (cleared): the dialogs, encoded for the state's `kres` (empty:
// none), and the turn report lines (for `econnews`).
std::string TakeDialogs(int K);
std::vector<std::wstring> TakeLines(int K);
void ClearNews();  // the session ended or the host took up another campaign

// A joined player's PC: the month's dialogs from its state (`kres`, empty: none); ClientDue tells whether they still wait
// for month `seq`, ClientShow shows them (the game's own modal dialogs, one after another) and applies the answers.
void ClientSetMonth(int seq, const std::string& encoded);
bool ClientDue(int seq);
void ClientShow(int seq);
void ClientReset();

// Bridge: `resinfo [K]` (host: the last research month of K as the player's; client: the month's dialogs), TEST ONLY
// `resforce <nation> [area|-1] [as]` (the nation's research in this copy, the host's own nation on the host, a joined
// nation on its player's PC before its turn: its next monthly research completes a tech; `as`: it is also asked the
// air/sea rescue question) and `resplayer <0|1>` (host: 0 = joined nations' research as before 0.8.2).
std::string BridgeCommand(const std::string& cmd, const std::vector<std::string>& args);

}  // namespace research

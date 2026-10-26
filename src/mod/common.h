#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <deque>
#include <functional>

#define MP_MOD_VERSION "0.6.1"
#define MP_PROTOCOL 3
// The live-battle engine level (lockstep.cpp): a live battle is fought only between two PCs with the same level, while
// the rest of the session stays compatible within MP_PROTOCOL. A client announces it in its hello (`ls=<level>`;
// 0.3.x announced `ls=1`: level 1). 2 = 0.4.0 step 2 (simulation PlayerSide per routine, Fastest available as clock
// state); 3 = 0.4.0 phase 1 (deferred actions: change force, change lead, battle turn-away; the order of battle in the
// state hash; Save as the host's crash checkpoint; side 1's computer control kept over a resync); 4 = 0.4.0 phase 1b
// (auto-run per player: a deferred action hands a side to the computer and back; both sides on auto-run run to the end);
// 5 = 0.4.0 phase 2 (manual torpedo salvoes: the torpedo dialog's Fire is a deferred action for that boundary; the
// torpedo mounts and more torpedo fields in the state hash); 6 = 0.5.0 phase 3a (routine air operations: the force air
// settings are orders; land-based CAP: one slot per side, the airfield's side checked; NightCAP kept over a resync);
// 7 = 0.5.0 phase 3b (air strikes: the strike dialog's LaunchSomeAircraft is an immediate action with side-partitioned
// AFIds, the air formations' strike fields are orders, the computer flies only the airfields' strikes of a human side
// unless the host's option says otherwise; air formation fields, the AFID and ScoutCount counters in the state hash);
// 8 = 0.5.0 phase 5a (rejoin: a player dropped from a live battle that goes on against the computer gets side 1 back
// when they return: A starts the battle again mid-way (`rj=` in the start message, the epochs go on), and B takes a new
// start of a battle it still has as a restart); 9 = 0.6.0 phase 4 (missiles: each side's own Fire at will, an order
// honoured through HoldMissileFire inside the automatic fire; the missile dialog's FireMissileMount calls are deferred
// actions; PendingMissileTarget is an order; both kept over a resync (`faw=`, `pmt=`); submarines in the state hash).
// 0.6.0 phase 5b (a live battle resumed from the host's save after its game restarted) keeps 9: the host takes it up
// with level 8's rejoin start; its `hr=1` only changes the returning player's chat line. 10 = 0.6.0 with 0.3.2 merged
// (the campaign date: after every live load both PCs write the campaign's own Year, Month and Day back, decoded from
// the campaign's TDateTime, so the battle runs with the campaign's month and day; the three in the state hash). 11 =
// stage B's fix (after every live load the simulation's ship identification is the battle save's on both PCs, not what
// each PC's display drew during the load: the sighting reports' IdentifyClass/IdentifyType in EOTP draw shared random
// numbers depending on it). 12 = the pre-release fixes (Auto-run off gives a side back as single player does: each
// force's lead division, the forces as they were before auto-run (`pa=` in the start message), the other divisions stay
// with the computer; a manual torpedo salvo of a side on auto-run is refused at the release, as a missile salvo is). 13 =
// side 0's computer control kept over a reload (`ai0=` in the start message: the game's resume gives side 0's divisions
// of computer forces to the computer, also one the host's player had taken back). 14 = TFightingShip.LightState kept
// out of the display's reach and in the state hash (the map's sighting circles wrote it between minutes; the division
// routine of StartMinute reads it: the dawn and dusk DESYNCs). 15 = two-PC conditions (0.4.1): the start message's
// `prefs=` carries only the battle preferences the load and the simulation use (AIControl, FloatWarning, ReadyPopup,
// BreakOnNewSighting, AirApproachMessage, AirSurfaceMessageLvl, LetAIControlForce, LOScalc, SoundOption, Autosave,
// FloatplaneLaunch, RealismSetting, NoFriendlyTorpHits, BSSMHC; the display ones stay each PC's own), B loads with A's
// x87 control word, each PC's map shows its own side (the game decides the map's picture for the first side; B's
// screen stays transparent while it loads, B shows its own ensign), and a READY mismatch writes both PCs' DESYNC
// reports (the `desync` message with `why=ready`). 16 = CPUs and data files (0.4.1): the game's x87 transcendental
// routines (FSIN, FCOS, FPATAN, ...: CPU-dependent rounding) computed in software on both PCs from the load on (`sm=1` in
// the start message; x87.cpp), A's x87 control word checked around every step and hash, the start's `dh=` (the game
// data files' hash: B refuses a battle whose files differ, A names them: `datadiff`), and the READY diagnostics (`g=`,
// `cpu=`, `cw=` in the ready message; `diag=` in the desync message). 17 = the campaign's ship-type flags (0.4.1: a
// battleship, a battlecruiser, a carrier exists; only a processed month sets them, so a joined player's game had 0 and
// identified ships otherwise than the host's): the start's `st=` carries A's, B uses them for the battle, and they are
// in the state hash; the leak check covers the simulation's identification. 18 = the game's temporary files (0.6.1):
// the data files' hash leaves out the names starting with "tmp" (the ship designer rewrites Data\tmpShip.ttp whenever it
// is used: a player who designed a ship lost every live battle to "game data files differ"); the combined hash is not
// level 17's, so a 0.6.0 PC and a 0.6.1 PC are told apart by their levels, not by a data-file difference.
#define MP_LS_LEVEL 18
// The level this copy announces (hello `ls=`, the start's, ready's and rejoin's `lv=`): MP_LS_LEVEL. TEST ONLY: env
// RTW3MP_TEST_LS_LEVEL (read once) announces another level, so one build can play a peer of another mod version; this
// copy's own checks keep MP_LS_LEVEL.
int AnnouncedLsLevel();
#define MP_DEFAULT_PORT 47624

// Window messages used by the main-thread dispatcher.
#define WM_MP_NET (WM_APP + 0x311)
#define WM_MP_CALL (WM_APP + 0x312)

void LogInit(const std::wstring& dir);
void Log(const char* fmt, ...);

std::string W2U(const std::wstring& w);
std::wstring U2W(const std::string& s);
std::string Trim(const std::string& s);
std::vector<std::string> Split(const std::string& s, char sep);

bool ReadFileBytes(const std::wstring& path, std::string& out);
bool WriteFileBytes(const std::wstring& path, const std::string& data);
bool FileExists(const std::wstring& path);
void EnsureDir(const std::wstring& path);

// "<Documents>\My Games\Rule the Waves 3\RTW3MP\" (created on demand).
std::wstring ModDataDir();
std::wstring GameDir();

// Simple key=value text messages (one per line, values may not contain newlines).
typedef std::map<std::string, std::string> KV;
std::string KVEncode(const KV& kv);
KV KVDecode(const std::string& s);
int KVInt(const KV& kv, const char* key, int def = 0);
std::string KVStr(const KV& kv, const char* key, const std::string& def = "");

// Runs a function on the game's main (VCL) thread at the next message-loop iteration.
void RunOnMainThread(std::function<void()> fn);
DWORD MainThreadId();
HWND DispatcherHwnd();
// True inside a dispatcher message that came from the game's outermost message loop (not from a modal dialog, a menu,
// a message box or ProcessMessages nested in some game routine).
bool AtOutermostDispatch();
std::string DispatchDepthText();  // diagnostics: "depth/base"

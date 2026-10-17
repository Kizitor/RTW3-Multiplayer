# Architecture

How RTW3 Multiplayer works in the current version (0.3.2, protocol 3) on Rule the Waves 3 1.01.44. This is the
developer view; players read the [README](../README.md). The history of each part, with the measurements and the
dead ends, is in [MODLOG.md](../MODLOG.md) (linked per section); the reverse-engineered game facts (calling
convention, objects, offsets, battle internals) are in [GAME-INTERNALS.md](GAME-INTERNALS.md); testing is in
[TESTING.md](TESTING.md).

## Contents
- [Overview](#overview)
- [Source files](#source-files)
- [Startup: the proxy loader and the init thread](#startup-the-proxy-loader-and-the-init-thread)
- [Finding the game's code: RTTI resolution](#finding-the-games-code-rtti-resolution)
- [Hooks](#hooks)
- [Threads and the main-thread dispatcher](#threads-and-the-main-thread-dispatcher)
- [The session: roles, phases, players](#the-session-roles-phases-players)
- [The monthly flow](#the-monthly-flow)
- [Save slots and files](#save-slots-and-files)
- [The save merge](#the-save-merge)
- [Network protocol](#network-protocol)
- [User interface](#user-interface)
- [Joined players' battles against AI nations (the battle phase)](#joined-players-battles-against-ai-nations-the-battle-phase)
- [Live battles between players (lockstep)](#live-battles-between-players-lockstep)
- [Test bridge](#test-bridge)
- [Versions and protocol](#versions-and-protocol)

## Overview
- The game is a 32-bit Delphi/VCL program behind a Steam DRM stub. The mod changes no game file: everything happens
  at runtime inside the game's process.
- `version.dll`, a proxy of the Windows DLL placed in the game folder, loads `RTW3MP.dll`. The mod finds the game's
  classes, methods and fields by name through the game's Delphi RTTI and hooks the routines it needs with MinHook.
- The **host** runs the real campaign in its own save slot and is authoritative: it runs all game logic and the turn
  timer. Each **client** (a joined player) plans its own nation on a copy of the month (slot 77) and submits its
  part of the save. The host merges the submissions and runs the month with the AI switched off for human nations.
- Battles: a joined player fights its battles against AI nations after the month on its own PC, in a battle slot
  (87); battles between players are fought live on both PCs in lockstep.

```
RTW3.exe (Delphi/VCL, Steam DRM stub)
  version.dll ............ proxy of the Windows DLL; loads RTW3MP.dll once the game has unpacked itself
  RTW3MP.dll
    delphi ............... RTTI lookups, register-convention calls, Delphi strings
    game ................. bindings and hooks (turn, AI routines, diplomacy, events, battles, research, ...)
    session .............. lobby, months, submissions, merge, diplomacy, VP, treaties, events, battle phase
    saveio ............... save files as INI documents, month packages, merge, Copy-game transform, battle results
    net .................. TCP transport (port 47624) and LAN discovery (UDP 47625)
    lockstep ............. live player-vs-player battles
    ui ................... Multiplayer window, title-screen button, Claim land window
    bridge ............... test commands on 127.0.0.1 (only with RTW3MP_BRIDGE_PORT)
```

## Source files
| File | Role |
|---|---|
| `src/proxy/version_proxy.cpp`, `version.def` | The proxy: forwards the 17 exports of the system `version.dll`; in `RTW3.exe` only, loads `RTW3MP.dll`. |
| `src/mod/main.cpp` | `DllMain`, the init thread, the dispatcher window, `RunOnMainThread`, the `WH_GETMESSAGE` hook (Ctrl+Shift+M). |
| `src/mod/common.h`, `util.cpp` | `MP_MOD_VERSION`, `MP_PROTOCOL`, `MP_DEFAULT_PORT`; the log; file helpers (`WriteFileBytes` with retries); `KV` text messages; paths. |
| `src/mod/delphi.cpp`, `delphi.h` | Delphi runtime interop: VMT scan, method and field lookup by name, `dl::Call`, `ConstUStr`/`ReadUStr`, byte patterns. |
| `src/mod/game.cpp`, `game.h` | Game bindings and all campaign hooks; callbacks into the session (`hookcb::*`). |
| `src/mod/session.cpp`, `session.h` | The multiplayer session (host and client). |
| `src/mod/saveio.cpp`, `saveio.h` | `IniDoc`, `Bundle`, month packages, submissions, the merge, `MakeNationPlayer`, `ExtractBattleResult`/`ApplyBattleResult`. |
| `src/mod/net.cpp`, `net.h` | TCP transport, message types, LAN discovery. |
| `src/mod/ui.cpp`, `ui.h` | The Multiplayer window, the title-screen button, the Claim land window, `config.ini`. |
| `src/mod/lockstep.cpp`, `lockstep.h` | Live battles: the battle engine bindings, the deterministic RNG, the minute protocol. |
| `src/mod/bridge.cpp` | The test bridge. |
| `src/mod/poc_battle.cpp`, `poc_battle.h` | The `poc*` research commands of the battle investigation (inactive unless used). |
| `third_party/minhook/` | MinHook (BSD 2-Clause); `build.bat` compiles its core (`buffer.c`, `hook.c`, `trampoline.c`) with the 32-bit disassembler `hde32.c`. |
| `build.bat` | Builds both DLLs into `dist\` with the Visual Studio C++ x86 tools (`/Zc:threadSafeInit-`, `/PDBALTPATH`). |

## Startup: the proxy loader and the init thread
- **Proxy** (`version_proxy.cpp`): `DllMain` loads the real `version.dll` from the system folder and forwards every
  export to it. Only inside `RTW3.exe` it starts a thread that waits (up to 300 s) for the game's VCL
  `TApplication` window and then loads `RTW3MP.dll` from the game folder. Loading a DLL while the Steam DRM stub is
  still unpacking makes the game quit silently, hence the wait. `RTW3MP_DIAG` writes `rtw3mp_loader.log` into the
  game folder.
- **Init thread** (`main.cpp`): opens the log (`RTW3MP <version> starting (protocol <n>)`), waits again for
  `TApplication` (up to 300 s), scans the game's classes (up to 60 s), resolves the bindings (`game::Resolve`: if
  names are missing, "this game version is not supported" and the mod stays idle), installs the hooks, sets a
  `WH_GETMESSAGE` hook on the game's main thread and starts the bridge. The thread then stays alive: Windows removes
  a message hook when the thread that installed it exits. `RTW3MP_NOINIT` keeps the mod idle; `RTW3MP_DELAY`
  delays the class scan (diagnostics).
- Details: [MODLOG, game facts](../MODLOG.md#investigation-2026-10-04-game-facts-v010),
  [gotchas](../MODLOG.md#investigation-2026-10-04-gotchas-each-cost-a-debugging-round-v010).

## Finding the game's code: RTTI resolution
- `dl::ScanClasses` finds the classes through their VMTs in the unpacked image. Methods and fields are looked up by
  name in the published and extended RTTI (`dl::Method`, `dl::MethodFewestParams` for overloads, `dl::Field`),
  including private fields. No address is hard-coded: offsets in the docs are documentation for 1.01.44 only.
- Routines without a name are found through a call-site byte pattern inside a named method (`dl::FindPattern`,
  `dl::CallTargets`) and checked at runtime; a missing piece turns the dependent feature off with a log line (for
  example `Peace treaties: UNAVAILABLE (...)`, `Joined players' battles: UNAVAILABLE (hooks)`).
- `dl::Call` calls Delphi `register` routines (EAX, EDX, ECX, then the stack); Delphi strings are read with
  `ReadUStr` and passed as constants with `ConstUStr`.
- `game::BuildFingerprint()` derives a fingerprint of the game build from RTTI addresses; the host refuses clients
  with another fingerprint.
- Details: [MODLOG, Delphi RTTI](../MODLOG.md#investigation-2026-10-04-delphi-rtti-a-huge-win-v010) and
  [GAME-INTERNALS.md](GAME-INTERNALS.md).

## Hooks
All hooks are installed once at startup (`game::InstallHooks`); each detour catches exceptions, and most do nothing
outside a session. The callbacks are in `session.cpp` (`hookcb::*`).

| Game routine | Why |
|---|---|
| `TfrmBuildCamp.btbnTurnClick` | Host: start the month or wait for the players ("Still waiting for ..."). Client: the Turn button submits. |
| `btbnResignClick` | Resigning is disabled during a session. |
| `DoEndOfTurn` | Marks the end of the host's month: the battle phase, diplomacy and the new month follow. |
| the AI strategic-move routine (unnamed) | Snapshots the human nations' ship orders before it and restores them after it. |
| `AIMonthlyBuild`, `AINationAdjustBudget`, `PutAIShipsInStatus`, `DetermineAINationResearch`, `CheckAIRebuilds`, `CheckAINationShipsOnTradePatrol`, `AINationAssignRaiders`, `AISendShipsToForeignStations`, `AISendShipToColonies`, `TBuilderNation.SetAITraining` | Generated skip thunks: the call is skipped when its nation is a human player's (connected or reserved). |
| `HandleAIPeace` | Hides the wars the players declared on each other from the AI's peace; keeps the players' VP. |
| `DoPeace` | Keeps the players' prestige (the game sets nations 1-8 to 0); shares the host's enemies' VP before it. |
| `HandleBattleAftermath`, `DoBattleBetweenAINations`, `QuickResolveRaiderBattle` | Victory points per battle (players' VP, war scores). `DoBattleBetweenAINations` also defers battles of joined players (see the battle phase). |
| `DoStrategicTurn` | Skipped in battle-slot mode (a battle screen's close must not run a month). |
| `DoBattle` | Counts battle setups (declined vs no battle) and tells the session that the host's battle screen opened (live battles). |
| `AdvanceDesignStudies` | Counts down the joined players' design studies, once per month. |
| `ReduceTimeLimits` | Counts down the joined players' pending doctrine changes, once per month. |
| `DoResearch` | Puts a joined nation's research priorities back after the game's AI branch. |
| `TfrmResearch.Execute` | On a joined player's PC, opens the Research screen for the player's nation instead of nation 0. |
| `EventLikelihoodCheck` | Vetoes events that need the host while a joined player's event is rolled. |
| `LoadBuildCampaign` | Clears the intel report list before a load and drops empty or repeated entries after it. |
| `CopyGameToOtherSlot` | A joined player's copy becomes the player's own campaign (`MakeNationPlayer`). |

The live battles add their own hooks (`Random32Proc`, the simulation and display routines) with the first live
battle or `poc*` command; see [Live battles](#live-battles-between-players-lockstep).

## Threads and the main-thread dispatcher
- **The game's main (VCL) thread** runs every game call. The `WH_GETMESSAGE` hook creates a message-only window,
  `RTW3MP_Dispatcher`, on that thread at the first message it sees. The dispatcher handles a 250 ms timer
  (`mp::Tick`, `ui::Tick`), `WM_MP_NET` (`mp::OnNet`: network messages) and `WM_MP_CALL` (the
  `RunOnMainThread` queue).
- **The network thread** (`net.cpp`) does all socket I/O. It queues received messages and posts `WM_MP_NET`; it
  never touches the game.
- **The bridge thread** accepts test connections on 127.0.0.1 and runs each command on the main thread through
  `RunOnMainThread`, waiting up to 120 s for the reply.
- **The init thread** stays alive after startup (the message hook's lifetime); the proxy's loader thread ends after
  the load.
- **Nested message loops.** A modal game dialog, a menu or a message box runs its own message loop, so the
  dispatcher keeps running under it: mod code must guard against re-entry. `AtOutermostDispatch()` compares the
  dispatcher's stack depth with the outermost loop's; the live battles free forms only from the outermost loop.
- No implicit thread-local storage in the mod (`/Zc:threadSafeInit-`), see the
  [gotchas](../MODLOG.md#investigation-2026-10-04-gotchas-each-cost-a-debugging-round-v010).

## The session: roles, phases, players
- **Roles:** none, host, client. **Phases:** lobby, planning, processing (`session.h`).
- **Nations:** the host plays nation 0, the save's player nation (chosen at New Game). Joined players claim nations
  1-8 of the campaign; before the host has a campaign they pick by name from the game's 10 nations, and the picks are
  matched when the campaign opens. A player without a nation only watches.
- **Players are identified by name.** At the session start the host reserves each player's nation under the
  player's name; a player who rejoins with the same name gets the nation back (also while the host's battle screen
  is open). Duplicate names get a number ("Friend 2"). **Give to AI** drops a reservation; a reserved nation without
  a connected player stays human (the AI stays off and the nation keeps its orders).
- **Per-campaign state** is kept in `rtw3mp_session.ini` in the host's slot: the reservations, the turn time limit,
  the players' alliances and wars, the war scores, pending diplomacy, agreed peace terms and the VP share marker. A
  campaign key (start year and nation names) keeps it from leaking into another campaign saved in the same slot;
  pending decisions and scores are kept only for the month they were saved in.
- Diplomacy, events, VP and treaties: [MODLOG 0.2.0](../MODLOG.md#feature-2026-10-04-diplomacy-between-players-protocol-2-v020),
  [0.2.4](../MODLOG.md#feature-2026-10-06-random-events-for-joined-players-v024),
  [0.2.5](../MODLOG.md#feature-2026-10-06-peace-terms-for-wars-between-players-v025),
  [0.2.6](../MODLOG.md#fix-2026-10-12-joined-players-victory-points-never-rose-against-the-host-v026).

## The monthly flow
1. **Start.** The host presses **Start session** once a campaign is open past its setup steps (the strategic
   screen, a slot between 1 and 49, the game's start phase 2); pressed earlier, the session starts by itself when the
   campaign is ready. The host saves the reservations and sends the first month.
2. **Send the month** (`BroadcastState`). The host saves the game and sends every player with a nation a package of
   the slot's save files (`MSG_STATE`; file names with the slot number replaced by `{S}`). Each package raises
   `[General] IDNo` by 20,000 x the player's nation, so ids created by different players never collide. A package
   larger than the network limit is not sent (`NOT sent` in the log).
3. **Load it on the client.** The client writes the package into slot 77 (it refuses slots below 50), notes its
   nation in `rtw3mp_player.ini`, loads the campaign with its nation as the player nation and shows its own enemies'
   VP. The Turn button becomes **Submit**. The client acknowledges the month and rolls this month's random event
   (33 % by default, once per calendar month).
4. **Plan.** Everyone plans at the same time. Diplomacy requests (`MSG_DIPLO`) and event results (`MSG_EVENT`) go to
   the host at once. **Submit** saves the client's game and sends its part (`MSG_SUBMIT`, see
   [the save merge](#the-save-merge)); a player can submit again until the month starts.
5. **Start the month.** The host presses **Turn** (a dialog offers to wait for missing players or to go on),
   **Advance turn now**, or the time limit runs out: then every client submits what it has (`MSG_FORCE_SUBMIT`), and
   the month starts when all have submitted or 30 s later. The host saves, merges every submission into its slot,
   reloads the campaign and runs the game's own Turn handler. A nation without a submission keeps last month's
   orders.
6. **The month.** The game runs the month as in single player, with the hooks above: AI routines skipped and AI
   fleet moves undone for human nations, design studies and doctrine counted down for them, VP credited, battles of
   joined players deferred, and the host's own battle against a joined player fought live.
7. **After `DoEndOfTurn`.** If battles were deferred, the battle phase runs
   ([below](#joined-players-battles-against-ai-nations-the-battle-phase)). Then the host applies the players'
   diplomacy (wars, alliances, peace, treaties, the VP share) and sends the new month (step 2).
8. If the Turn click returns without ending the month (cancelled, or waiting after a battle), the host resends the
   current month after 4 s ("The turn did not advance").

## Save slots and files
| Where | Who | What |
|---|---|---|
| `Save\Game<n>`, n = 1-49 | host | The campaign the host plays and hosts: a normal save. Also `rtw3mp_session.ini`. |
| `Save\Game77` (`RTW3MP_CLIENT_SLOT`, 50-999) | joined player | The month the player plans, as sent by the host, plus `rtw3mp_player.ini` (the player's nation, for Copy game after leaving the session). |
| `Save\Game87` (client slot + 10, `RTW3MP_BATTLE_SLOT`) | joined player | The battle slot: the month's files with the player's nation made nation 0 (the Copy-game transform), for the player's battles and live battles. |
| the slot a Copy game goes to | joined player | The game's copy of slot 77, turned into the player's own campaign. |
| `RTW3MP\` next to `Save\` | everyone | `config.ini` (name, IP, port, time limit, Fight my battles), `rtw3mp.log` and `rtw3mp.prev.log` (`rtw3mp_<tag>` with `RTW3MP_LOG_TAG`), `events_<hash>.ini` (a joined player's event cooldowns). |

All paths are under `<Documents>\My Games\Rule the Waves 3\`. The mod treats slots below 50 as the player's own: the
host hosts only from slots 1-49, and a client never writes a month package into a slot below 50 ("refusing to
overwrite a regular save slot").

## The save merge
- **Save files:** the `.bcs` and `MapData<n>.dat` files are UTF-8 INI with CRLF (the `.bcs` with a BOM), and the
  `.off` file is INI as well; `IniDoc` (`saveio.cpp`) parses and writes them line by line (with CRLF), so lines the
  mod doesn't change are written back unchanged. `DesignFiles<i>.des` is positional text and moves as a whole file.
- **A submission** (`BuildSubmission`) holds `[NationK]`, `[NationKShips]`, `[NationKCoastalArtillery]` and
  `[NationKSubmarines]` with the client's `IDNo`, its `DesignFilesK.des` and its map data.
- **The merge** (`MergeSubmission`) checks the nation and the month, replaces those sections in the host's save but
  keeps the host's values of the diplomacy keys (`Tension`, `Allied`, `AITension<j>`, `AIAlliance<j>`) and of `VP`,
  takes the base improvements (`BaseValue`, `BuildingBase`) of the nation's own possessions from the client's map
  data, and moves `IDNo` forward when the client created new ids. It prepares every file first and writes them all
  or none; `WriteFileBytes` retries locked files for about 3 s. A rejected turn is logged (`host turn: <name>'s turn
  NOT applied: <why>`) and shown to the host.
- **Not transferred** from joined players: officers and divisions (`.off`), air units and aircraft types.
- **The Copy-game transform** (`TransformNationPlayer`/`MakeNationPlayer`, 0.2.7) swaps nation 0 and K in every
  section, relation, ship, air unit, mine, possession and design file, so K becomes the save's player nation; it is
  used for Copy game and for the battle slot.
- Details: [design v1](../MODLOG.md#feature-2026-10-04-multiplayer-design-v1-historical-v010),
  [locked files](../MODLOG.md#fix-2026-10-05-a-players-turn-could-be-dropped-when-a-save-file-was-briefly-locked-v023),
  [Copy game](../MODLOG.md#fix-2026-10-13-a-joined-players-copy-game-played-the-hosts-nation-v027).

## Network protocol
- **Transport:** TCP; the host listens on the port (default 47624), clients connect. One background thread does all
  socket I/O with `TCP_NODELAY`. A frame is `[u32 length][u16 type][payload]`; the receiver drops frames above 96 MB
  ("message too large") and peers whose frames are malformed ("protocol error"). Each side pings every 5 s.
- **LAN discovery:** **Find LAN games** broadcasts on UDP port + 1 (47625); the host answers
  `RTW3MP!<port>|<host name>|<host nation>|<date>|<players>`.
- **Payloads:** `KV` text (`key=value` lines), or a header and a body (`PackHB`: a 4-byte header length, a `KV`
  header, then binary data such as a `Bundle` of files).
- **Handshake:** the client's `MSG_HELLO` carries `proto`, `mod`, `build`, `name`, `pw`, `battles` (Fight my battles)
  and `ls` (live battles possible). The host answers `MSG_WELCOME` (the final name) or `MSG_REJECT` with the reason:
  "protocol mismatch (host mod X)", "different game version than the host" or "wrong password".

| Message | Direction | Content |
|---|---|---|
| `MSG_HELLO`, `MSG_WELCOME`, `MSG_REJECT` | client -> host, host -> client | Handshake (above). |
| `MSG_LOBBY` | host -> all | Players, nations, picks, phase, diplomacy snapshot. |
| `MSG_PICK` | client -> host | A nation pick by name. |
| `MSG_STATUS` | host -> all | Phase, remaining time, status text. |
| `MSG_CHAT` | both | Chat lines; `notice=1` flashes the window; `treaty` carries a peace treaty for the player's dialog. |
| `MSG_STATE` | host -> client | The month: header (seq, nation, date, remaining time) and the slot's files. |
| `MSG_ACK` | client -> host | The month was loaded. |
| `MSG_SUBMIT` | client -> host | The player's turn (header and the submission bundle). |
| `MSG_FORCE_SUBMIT` | host -> all | The time limit ran out: submit now. |
| `MSG_DIPLO` | client -> host | A diplomacy request (war, ally, peace, leave, cancel, claim). |
| `MSG_EVENT` | client -> host | The result of the player's random event (relation changes, own fields). |
| `MSG_BATTLE`, `MSG_BATTLE_RESULT`, `MSG_BATTLE_CANCEL` | host -> client, client -> host, host -> client | A battle to fight, its result (or why there was none), a battle the host resolved without the player. |
| `MSG_LOCKSTEP` | both (the host relays between two clients) | Live battle traffic. |
| `MSG_PING`, `MSG_PONG`, `MSG_BYE` | both | Keep-alive and round-trip time; leaving. |

New `KV` keys must be ignorable by older peers of the same protocol; anything else needs a new `MP_PROTOCOL`.

## User interface
- **Title screen:** a child window over the title image's `pnStart` panel, above *Exit*, opens the Multiplayer
  window; the message hook opens and closes it with Ctrl+Shift+M anywhere.
- **The Multiplayer window** is a Win32 tool window owned by the game's visible main screen (the strategic screen or
  the title screen): name, password, host IP, port,
  **Fight my battles**, **Host game**, **Join game**, **Find LAN games**, **Leave**, the player list, nation picks
  (**Claim nation**, **Give to AI**), the turn time limit, **Start session**, **Advance turn now**, the Diplomacy list
  with its buttons, and the chat. Host-only controls are hidden on a joined player's PC. Settings are kept in
  `config.ini`. Notices flash the window.
- **The Claim land window** (`RTW3MP_Claim`) lets the winner of an agreed peace tick the loser's possessions.
- **The Turn button** reads **Submit**, **Submitted** or **Waiting** on a joined player's PC.

## Joined players' battles against AI nations (the battle phase)
- **Deferral (host).** The game resolves battles between nations it runs in `DoBattleBetweenAINations`. During a
  month, the hook records a battle between a joined player's nation and an AI nation instead, when that player is
  connected and fights battles (`battles=1` in its hello). A battle between two joined players' nations is recorded
  for a live battle when both are connected, both fight battles and both can play live battles; otherwise the game
  resolves it as before.
- **The battle phase (host),** after `DoEndOfTurn` and before the new month: the host saves, locks its strategic
  screen ("Battles in progress") and sends each player its battles one at a time (`MSG_BATTLE`: the battle and the
  slot's files). Each result is merged into the save file at once, so the player's next battle starts from it. When
  all are done the host reloads and lets the game resolve the battles that were declined, not fought, timed out
  (30 minutes per battle, `RTW3MP_BATTLE_TIMEOUT`), or whose player left.
- **The player's PC.** The client writes the files into the battle slot, makes its nation nation 0 there
  (`MakeNationPlayer`), loads it and starts the battle through the game's `CheckAttackLocation`: the game's preview,
  then its battle screen. In battle-slot mode the screen's close runs no month. After the results the client saves,
  sends the result (`ExtractBattleResult`: the ship, loss and coastal sections of both sides and their allies, and
  the changes of VP, prestige and fleet morale) and loads slot 77 again.
- **The merge (host):** `ApplyBattleResult` applies only what the battle changed, field by field, for the two
  nations and AI nations; ships missing from the result were sunk; the player's intel fields are never taken;
  nothing changes unless everything applies. A battle between two players' nations also counts for their war score
  (`CreditWarScore`, since 0.3.1): the host's battle hooks never see a battle fought on a player's PC.
- Details: [investigation](../MODLOG.md#investigation-2026-10-14-tactical-battles-joined-players-vs-ai-live-lockstep-pvp-v030),
  [feature](../MODLOG.md#feature-2026-10-15-joined-players-fight-their-battles-against-ai-nations-on-their-own-pc-v030),
  [tests](../MODLOG.md#testing-2026-10-15-joined-players-battles-vs-ai-v030).

## Live battles between players (lockstep)
- **Roles.** A is the authority (side 0, owns the clock and the snapshot): the host for its own battle against a
  joined player's nation, or the first player's PC for a battle between two joined players (the host relays
  `MSG_LOCKSTEP` by battle id). B is side 1. A host battle against a player who doesn't fight battles, or whose PC
  can't play live battles, is fought by the host against the AI as in single player.
- **Start.** A holds the battle at minute 0 and saves it (the game's battle save and the campaign with the battle in
  progress), sends the files, the RNG seed, the game speed, the battle-relevant preferences and its x87 control word.
  Both PCs load the battle through the game's own resume path with the seeded RNG and compare a hash before minute 1.
- **Determinism.** The game's `Random32Proc` is hooked: simulation draws come from the shared seeded stream, display
  draws from a local one. Context hooks mark the simulation routines and the display routines; the display fields
  that drawing code writes (`Hidden`, identification, sighting) are kept as a simulation set and a display set per
  ship. `ls leakcheck` reports any simulation change made between steps. Display routines see this PC's PlayerSide
  even when the simulation calls them. Since 0.3.2 they include the *Show air formations* list
  (`TdlgShowAirFormations.UpdateListSortedByBase`): EOTP refreshes the open window inside the minute, and the list
  shows PlayerSide's formations (before, B's window listed side 0's).
- **Every minute.** A releases each minute; both PCs run it with the game's own minute routine. Orders are field
  values of a side's own divisions and ships: a guard turns local edits into numbered batches that both apply before
  the next minute. After every minute both hash the battle state (including the RNG); on a mismatch A resyncs B from
  a new snapshot. More than 3 resyncs without 10 good minutes in between hand side 1 to the AI.
- **The clock.** A owns it: run, pause, the game speed and the end are requests (`ClockRequest`; B sends them to A),
  and a new speed takes effect with the next release on both PCs. The speed box (`cbGameSpeedChange`) and, since
  0.3.2, its arrows (`UpDown1Click`) only make requests: an arrow asks for one step from the speed in use (on B, the
  speed A announced last), by the game's own rule (at most *Fast* while ships are in contact). *Fastest available*
  is blocked; VCL puts the toggle button down before the click handler runs, and the game's speed governor reads its
  `FDown` at minute boundaries, so since 0.3.2 the blocker puts it up again with the game's own routine (the one the
  down arrow calls). Every other blocked toggle speed button (auto-run, the division report's land-based CAP) is put
  back as it was too (`PutToggleBack`: `TSpeedButton.SetDown(not FDown)` when `FGroupIndex` is not 0).
- **Failures.** B silent for 30 s at a minute boundary (5 minutes while busy), not loaded within 120 s, or
  disconnected: A hands side 1 to the AI and goes on. A lost: B leaves and loads slot 77 again. A watchdog recovers
  a minute that never finishes (10 s). Forms are freed only from the outermost message loop; flags set around game
  calls are restored in `__finally`.
- **Reloads.** `ResetBattleForm` frees the battle form and creates a new one before every load (the start, a
  resync, A's recovery after a failed load) and when B leaves. Since 0.3.2 it first runs the game's own battle
  teardown, as the screen's close (`FormClose`) does: `TfrmMain.ClearData(2, True)` and the air cleanup that frees the
  battle's air formations (before, the global lists outlived the form, and every load appended another copy of the air
  formations, so a battle with aircraft never had equal hashes). It also hides the battle's dialogs that keep pointers
  to ships, divisions and air formations (`TdlgShowAirFormations`, `TdlgDivisionReport`, `TdlgForceOrders`,
  `TdlgSelectLead`, `TdlgTorpTargets`, `TdlgStrike`, `TdlgMissileFire`); their unit variables are found once in the
  image (`FormVarOf`).
- **Display inside a minute.** The game refreshes the ship tree at the end of a minute with `FillShipTree(PlayerSide)`;
  inside the simulation PlayerSide is the canonical 0, so since 0.3.2 the hook replaces that side argument with this
  PC's own side.
- **The campaign date.** The game's resume path writes the battle's own day and month into the campaign's date
  variables, and a battle can be dated in the next month. Since 0.3.2 A records the campaign date when its battle
  opens (`Hold`), every snapshot saves that date in the campaign file (`SaveSnapshot`), and a battle slot's close,
  which runs no month, puts it back; otherwise a resync made the drift permanent and could skip a month.
- **The end.** Both players' *End game* (the game's own rule applies) or the game's own end conditions. When the
  game ends the battle inside a minute, the battle screen's close is deferred to the end of that minute.
- **Not available in live battles yet** (`g_blockers`: the click handler doesn't run, a chat line says why): air
  strikes and routine air operations (CAP, search, land-based CAP); missiles (manual fire; the missile panel's *Fire
  at will* and *Fire mode* aren't click handlers, but the simulation reads them on each PC, so since 0.3.2 they keep a
  new battle screen's defaults and are disabled after every load and at every boundary, see
  [GAME-INTERNALS.md](GAME-INTERNALS.md#battle-commands)); manual torpedo salvoes; change force, change lead and
  battle turn-away; *Fastest available*; auto-run and save. The game's hidden developer items (try for home, enter
  port, sink, teleport, ...) and handlers that no control calls are blocked too. Emergency activation needs no
  blocker: its button is never shown and its click handler is empty.
- Details: [feature](../MODLOG.md#feature-2026-10-15-live-player-vs-player-battles-in-lockstep-v030),
  [stress tests](../MODLOG.md#testing-2026-10-15-live-pvp-battles-lockstep-stress-v030),
  [release candidate](../MODLOG.md#testing-2026-10-15-release-candidate-v030),
  [the limits of live battles](../MODLOG.md#investigation-2026-10-16-the-limits-of-live-battles-static-v031),
  [the 0.3.2 fixes](../MODLOG.md#fix-2026-10-17-speed-controls-air-formations-window-and-battle-dialogs-in-live-battles-v032),
  and the battle facts in [GAME-INTERNALS.md](GAME-INTERNALS.md#tactical-battles).

## Test bridge
- A line-based command socket on 127.0.0.1, only when `RTW3MP_BRIDGE_PORT` is set when the game starts; with
  `RTW3MP_BRIDGE_TOKEN` the first line must be the token.
- Commands run on the main thread and reply `ok ...` or `error ...`. Commands that can open a modal game dialog are
  asynchronous: they reply `ok` at once and log their result.
- The command reference, the runner and the scenario catalog are in [TESTING.md](TESTING.md).

## Versions and protocol
- `MP_MOD_VERSION` and `MP_PROTOCOL` in `src/mod/common.h`; both are in the log's first line (`RTW3MP 0.3.2
  starting (protocol 3)`).
- The host refuses a client with another protocol or another game build. Protocol history: 1 = 0.1.0, 2 =
  0.2.0-0.2.8, 3 = 0.3.0-0.3.2. Within a protocol, versions can play together, but features may need both sides (see
  [CHANGELOG.md](../CHANGELOG.md)).
- Releases are tagged `vX.Y.Z`; the release zip is built by `tools/package_release.ps1` (see the docs agent's
  release checklist in `.github/agents/rtw3-docs.agent.md`).

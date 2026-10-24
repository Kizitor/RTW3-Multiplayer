# Architecture

How RTW3 Multiplayer works in the current version (0.6.0, protocol 3) on Rule the Waves 3 1.01.44. This is the
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
- [Per-player intelligence, trade protection and foreign stations](#per-player-intelligence-trade-protection-and-foreign-stations)
- [Network protocol](#network-protocol)
- [User interface](#user-interface)
- [Joined players' battles against AI nations (the battle phase)](#joined-players-battles-against-ai-nations-the-battle-phase)
- [Each joined nation's relations (relations v2)](#each-joined-nations-relations-relations-v2)
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
    x87, datafiles ....... live battles: the same maths on every CPU, the game data files' hash
    nationplay ........... what the game does for its player only, for joined players (intelligence, trade protection)
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
| `src/mod/x87.cpp`, `x87.h`, `softmath.h` | Live battles (engine level 16): the hooks of the game's 11 x87 transcendental routines and their software versions (fdlibm's algorithms in SSE2 doubles, with the x87's own 66-bit pi reduction, out-of-range and special-value rules and the game's Exp rounding, within 1 ulp of a double of the CPU's results), the same bits on every CPU; the CPU vendor. |
| `src/mod/datafiles.cpp`, `datafiles.h` | Live battles (engine level 16): the hash of the game's `Data` folder (but `Data\Graphics`), computed in a background thread when a session starts, and the list of files a start compares. |
| `src/mod/nationplay.cpp`, `nationplay.h` | What the game does for its player only, for joined players' nations: intelligence, the Turn's trade protection and foreign stations questions, the month's consequences ([below](#per-player-intelligence-trade-protection-and-foreign-stations)). |
| `src/mod/economy.cpp`, `economy.h` | The player's economy rules for joined nations on the host (relations v2, B5 money): the computer's money and budget bonuses left out, MinHook at single instructions found by pattern. |
| `src/mod/warfx.cpp`, `warfx.h` | The player's war effects for joined nations on the host (0.6.0, B5): blockades (their points, flags, effects and messages), war weariness, the build rules' war tests, and raiders, submarines and the trade war (a frame in K's view in DoStrategicTurn) for each joined nation at war. |
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
| `TfrmBuildCamp.btbnTurnClick` | Host: start the month or wait for the players ("Still waiting for ..."). Client: the Turn button submits, after the Turn's trade protection and foreign stations questions for the player's nation (0.4.1, [below](#per-player-intelligence-trade-protection-and-foreign-stations)). |
| `btbnResignClick` | Resigning is disabled during a session. |
| `DoEndOfTurn` | Marks the end of the host's month: the battle phase, diplomacy and the new month follow. |
| the AI strategic-move routine (unnamed) | Snapshots the human nations' ship orders before it and restores them after it. |
| `AIMonthlyBuild`, `AINationAdjustBudget`, `PutAIShipsInStatus`, `DetermineAINationResearch`, `CheckAIRebuilds`, `CheckAINationShipsOnTradePatrol`, `AINationAssignRaiders`, `AISendShipsToForeignStations`, `AISendShipToColonies`, `TBuilderNation.SetAITraining`, `CheckAISpying` (0.4.1) | Generated skip thunks: the call is skipped when its nation is a human player's (connected or reserved). |
| `HandleAIPeace` | Hides the wars the players declared on each other from the AI's peace; keeps the players' VP. Relations v2: runs with the pairs of nations under the player's rules masked (B2). |
| `DoPeace` | Keeps the players' prestige (the game sets nations 1-8 to 0); shares the host's enemies' VP before it. Relations v2 (B4), in a joined nation's peace part on its PC: only every other nation's Prestige is kept (nothing of the host's). |
| `HandleBattleAftermath`, `DoBattleBetweenAINations`, `QuickResolveRaiderBattle` | Victory points per battle (players' VP, war scores). `DoBattleBetweenAINations` also defers battles of joined players (see the battle phase). |
| `DoStrategicTurn` | Skipped in battle-slot mode (a battle screen's close must not run a month). |
| `DoBattle` | Counts battle setups (declined vs no battle) and tells the session that the host's battle screen opened (live battles). |
| `AdvanceDesignStudies` | Counts down the joined players' design studies, once per month. |
| `ReduceTimeLimits` | Counts down the joined players' pending doctrine changes, once per month. Relations v2: runs with the pairs of nations under the player's rules masked (its "separate peace" of a computer nation, B2). |
| `DoResearch` | Puts a joined nation's research priorities back after the game's AI branch. |
| `TfrmResearch.Execute` | On a joined player's PC, opens the Research screen for the player's nation instead of nation 0. |
| `EventLikelihoodCheck` | Vetoes events that need the host while a joined player's event is rolled. |
| `LoadBuildCampaign` | Clears the intel report list before a load and drops empty or repeated entries after it. Drops (and frees) the air units the global list held before the load once the load succeeded, and repeated air units of the save (the same `AirUnitId`): the game's `LoadAircraftData` appends without clearing, and the mod reloads in the same process every month and at every live battle load (0.3.3). Clears the last battle's area and possession (`BattleArea`, `BattlePossession`) before every load: the load frees every map area and possession but sets the two again only for a save with a battle in progress, and the next save wrote their names from freed memory (the joined player's "Error in SaveBuildCampaign procedure! ..." box, 0.6.0; [GAME-INTERNALS](GAME-INTERNALS.md#the-game-and-the-mod-facts-you-can-rely-on)). |
| `CheckBlockadeStatus` | Only the call of `LoadBuildCampaign` (told apart by its return address): the game's load credits every blockade between nation 0 and a nation at war with it (nation 0's VP, or EnemyVP) as the month does; the hook keeps the flags and the random draws and puts the loaded VP and EnemyVP back, so a load gives exactly the saved values (0.4.0). Only in multiplayer: every load while a session is up (hosting or joined) and the mod's own reloads (`game::LoadCampaign`; outside a session that is a joined player's return to its slot after a live battle that outlived the session). A load without a session (the title screen, the host's first load before hosting, the resume after a restart) is the game's own: one credit per start, as in single player, logged as `kept: no multiplayer session`. The month's own calls (`DoTurn`, `DoStrategicTurn`) are untouched. |
| `CopyGameToOtherSlot` | A joined player's copy becomes the player's own campaign (`MakeNationPlayer`). |
| `ShowTensionLevels`, `DrawAreaStrengthBars` | On a PC that plays nation K > 0 (a joined player's), run in the relations view (0.4.1): nation K at index 0, the host's nation at index K, every nation's `Tension`/`Allied` its relation with K; put back afterwards. The tension panel's flags are renamed for the view first. |
| `SetValues`, `UpdateUIValues` | On such a PC, afterwards: slot K's flag shows the host's nation; the War results box, the war line and the player name's hint follow K's own wars (0.4.1). |
| `TdlgNationData.Execute` | On such a PC, a flag's nation index 0 <-> K (the slot K flag shows the host's nation, the player's own flag K) (0.4.1). |
| `CheckAITensionEvents`, `TBuilderNation.DoAITensionChanges`, `CheckStopPlayerAgression`, `CheckAllyTrouble`, `CheckBlitzkrieg`, `CheckChineseConcessions`, `CheckForceOnForeignStations`, `CreateArmsControlTreaty`, `CheckForRevolutionInEnemyNation` | Relations v2 (B2), host: generated thunks (`EmitMaskThunk` -> `MaskedCall`, the call inside `__try`/`__finally`) run the computer's relation rules with every pair of a joined nation under the player's rules looking neutral (tension 5, no alliance); put back afterwards. |
| `TBuilderNation.ChangeAITension` | Relations v2 (B2), host: a pair with a nation under the player's rules is left alone (the change and its turn message). |
| `CheckWarsWithNation`, `GetRandomPossessionInArea`, `GetRandomPossession`, `DoSurpriseAttack`, `CheckForPlayerSurpriseInvasion` | Relations v2 (B3), only while the player's routines run in a joined nation's view on its PC: the nation's own past wars; no surprise landing or attack (generated thunks and detours test the routines' flag; not in B4's peace part). 0.6.0 with a host taking them (welcome `gen=2`): DoSurpriseAttack runs, the takeover branch of CheckForWar runs (its call sites only), the landing in K's own possession stays out. Elsewhere the originals. |
| `CheckSurpriseAttack` | 0.6.0 (B6 piece 2), only in a joined nation's view on its PC: its (attacker, defender) is recorded for a battle request of kind 2 and it does not run (True for K: no "No opportunity found" box; False: the next nation is asked). Elsewhere (the battle slot's replay, the host) the original. |
| `NegotiatePeace`, `ChangePossessionOwner`, `TransferShipAsReparation`, `ShowPeaceTreaty` | Relations v2 (B4), only while a joined nation's peace part runs in its view on its PC: record the arguments, the possession owner changes, the ships handed over (id, new name, log line) and the treaty's lines for the host. `ChangePossessionOwner` also records the takeover at a war's start in the view (0.6.0, CheckForWar's call site). Elsewhere (the host's own peace and the players' treaties included) the originals. |
| `CheckForWar`, `RandomTensionChanges` | Relations v2 (B2), host: the host's player routines see a nation under the player's rules as a player: in CheckForWar its tension with the host reads 5 (at peace), it plans no war and has no alliances of its own; in RandomTensionChanges its tension reads at least 6 (no alliance offer in its name); the routines' own changes are kept. |
| `ComputeAINationIntelCost` | Host: a joined player's nation pays its own intelligence spending instead of getting the AI's intelligence money (0.4.1, `nationplay.cpp`). |
| `GetIntelCostForPlayerNation`, `IntelToString`, `ChangeIntel` | Joined player's PC: the budget's Intelligence line, the labels and the +/- buttons of the politics panel are the player's own; the player's own flag stands for the host's nation (0.4.1). |
| `GetPatrolRequirement` | Joined player's PC: the budget panel's trade protection requirement is the player's nation's, against its own enemies (0.4.1). |
| `CheckShipsOnForeignStations` | Host: after it, the player's part of it and of `CheckPatrols` for each joined player's nation (0.4.1). |

The live battles add their own hooks (`Random32Proc`, the simulation and display routines, and since engine level 16
the game's 11 x87 transcendental routines in `x87.cpp`) with the first live battle or `poc*` command; see
[Live battles](#live-battles-between-players-lockstep).

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
  is open), and a live battle they were dropped from that still goes on gets them back too
  ([rejoin](#live-battles-between-players-lockstep)), also one the host's game resumed from its save after a restart.
  Duplicate names get a number ("Friend 2"). **Give to AI** drops a
  reservation; a reserved nation without a connected player stays human (the AI stays off and the nation keeps its
  orders).
- **Per-campaign state** is kept in `rtw3mp_session.ini` in the host's slot: the reservations, the turn time limit,
  the players' alliances and wars, the war scores, pending diplomacy, agreed peace terms, the VP share marker and
  (relations v2) the joined nations' war counters. A campaign key (start year and nation names) keeps it from leaking
  into another campaign saved in the same slot; pending decisions, scores and war counters are kept only for the month
  they were saved in.
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
   nation in `rtw3mp_player.ini`, loads the campaign with its nation as the player nation (with its nation's own war
   counter in place of the host's, relations v2) and shows its own enemies' VP. The Turn button becomes **Submit**. The
   client acknowledges the month and rolls this month's random event (33 % by default, once per calendar month).
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
   diplomacy (wars, alliances, peace, treaties, the VP share), steps the joined nations' war counters
   ([relations v2](#each-joined-nations-relations-relations-v2)) and sends the new month (step 2).
8. If the Turn click returns without ending the month (cancelled, or waiting after a battle), the host resends the
   current month after 4 s ("The turn did not advance").
9. **The host's game restarted during its battle (engine level 9).** The host's battle is fought in the middle of the
   month (the game's `DoTurn2` runs up to `CheckForBattles2` → `DoBattle`; the rest, `DoStrategicTurn` →
   `DoEndOfTurn`, runs when the battle screen closes), and a restart loses the session. Loading the campaign slot
   resumes the battle; if it is a live battle against a joined player's nation that can be rejoined, a host in the
   lobby takes the month up where it is (processing, as after the Turn; reservations from `rtw3mp_session.ini`), and
   the rest of the month and the new month follow as usual ([resume](#live-battles-between-players-lockstep)).

## Save slots and files
| Where | Who | What |
|---|---|---|
| `Save\Game<n>`, n = 1-49 | host | The campaign the host plays and hosts: a normal save. Also `rtw3mp_session.ini`, `rtw3mp_nations.ini` (the joined nations' own intelligence and foreign-station warning, 0.4.1: [below](#per-player-intelligence-trade-protection-and-foreign-stations)), and during the host's live battle `rtw3mp_livebattle.ini` (the battle save is a live battle's snapshot: [resume](#live-battles-between-players-lockstep)). |
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
  keeps the host's values of the diplomacy keys (`Tension`, `Allied`, `AITension<j>`, `AIAlliance<j>`), of `VP` and
  (0.4.1) of `IntelligenceSpending` and `IntelEffort` (the host's own spending on and effort against nation K: the
  game keeps them on the target nation),
  takes the base improvements (`BaseValue`, `BuildingBase`) of the nation's own possessions from the client's map
  data, and moves `IDNo` forward when the client created new ids. It prepares every file first and writes them all
  or none; `WriteFileBytes` retries locked files for about 3 s. A rejected turn is logged (`host turn: <name>'s turn
  NOT applied: <why>`) and shown to the host. Relations v2 (B4): a ship of the player's section that another nation's
  ships section on the host holds too (the same `Ship<n>Id`: a ship handed over twice) is dropped from the player's
  section and its `ShipCount` follows (`DropForeignShips`, logged).
- **Not transferred** from joined players: officers and divisions (`.off`), air units and aircraft types.
- **The Copy-game transform** (`TransformNationPlayer`/`MakeNationPlayer`, 0.2.7) swaps nation 0 and K in every
  section, relation, ship, air unit, mine, possession and design file, so K becomes the save's player nation; it is
  used for Copy game and for the battle slot.
- Details: [design v1](../MODLOG.md#feature-2026-10-04-multiplayer-design-v1-historical-v010),
  [locked files](../MODLOG.md#fix-2026-10-05-a-players-turn-could-be-dropped-when-a-save-file-was-briefly-locked-v023),
  [Copy game](../MODLOG.md#fix-2026-10-13-a-joined-players-copy-game-played-the-hosts-nation-v027).

## Per-player intelligence, trade protection and foreign stations
Since 0.4.1 (`nationplay.cpp`). The game keeps some things for its player, nation 0, only
([GAME-INTERNALS](GAME-INTERNALS.md#intelligence-trade-protection-and-foreign-stations)): the intelligence spending,
effort and reports, the monthly spying check, the Turn's questions about trade protection and foreign stations, and
the month's consequences of missing ones. Until 0.4.0 a joined player saw (and partly changed) the host's
intelligence, paid nothing for it, and got none of the questions or consequences.
- **The view.** The mod runs the game's own routines with the player's nation K seen as the player, as the joined
  players' events do: the nation list's entries 0 and K swapped, every nation's `Tension`/`Allied` holding its
  relation with K (`TechSharing` 0), the war counter from K's wars, and on the host `PlayerNation` = K. Everything is
  put back after the call, also when the game raises; `game::EventActive()` is true meanwhile. A view is refused while
  another swap is in place (an event, or a display routine's relations view on the player's PC).
- **The host's records:** `rtw3mp_nations.ini` in the host's slot, with the campaign key as `rtw3mp_session.ini`:
  `[Nation<K>]` `IntelSpend` and `IntelEffort` (K's spending on and effort against nations 0-8),
  `ForeignStationsWarned` (the game's `LeaderWarnedAboutForeignStations`, for K), and `[Nation<K>IntelReports]`
  (`ReportNo`, `Intel<i>`). The host's own intelligence stays in the save as in single player.
- **K's month package** (`HostTransformState`): `IntelligenceSpending`/`IntelEffort` of every `[Nation<j>]` hold K's
  values, K's own section 0, `[IntelReports]` K's list; the `MSG_STATE` header has `intel=1`. The Copy-game and
  battle-slot transform (0 <-> K) then gives K's spending on the host's nation to the host's new index, as the game
  keeps it.
- **On the player's PC** (a month with `intel=1`): the politics panel shows the player's own spending and effort; the
  slot of the player's own nation stands for the host's nation (`IntelToString`: its label; `ChangeIntel`: its +/-),
  as the relations view shows it there ([User interface](#user-interface)); inside the view the host's nation is at
  that index already, so both paths read the same record. The budget's Intelligence line counts every nation but the
  player's (`GetIntelCostForPlayerNation`).
- **The monthly spying check** runs on the player's PC in the other branch of the month's event roll: when no random
  event comes (two months in three by default), as the game's `CheckTurnEvents` runs `CheckIntelligence` when it has
  no event. It is the game's `CheckIntelligence` in the view, so the game's dialogs (our spy caught: deny or make him a
  hero; an enemy spy caught; the head of naval intelligence's advice) show on the player's PC. New reports are added
  to the player's list and the chat ("* Intelligence report: ..."). The results go to the host at once as an
  `MSG_EVENT` (`event=-1`, `caption=Intelligence`, `intel=1`, `ireports` (joined by `\x1e`), `ieffort`, and an event's
  `tension` and `delta`: the relations of K and the own-field changes), applied as an event's; the host's
  one-event-per-month rule matches the game's one slot.
- **The player's turn** carries its own intelligence: `[MPIntel] Spend=`, `Effort=` (nine values) and
  `[IntelReports]` in the submission's `nation.bcs`. The host takes them into its records (`HostTakeSubmission`), not
  into the save.
- **The Turn's questions** (`ClientTurnWarnings`, when the player presses **Submit**, before the turn is saved): at
  war "You have N ships on trade protection duties, but M are required. This will give VP to the enemy and might hurt
  your prestige. Continue anyway?", then "There are insufficient ships on foreign stations! Continue anyway? Tip: ...",
  computed for K as `PlayerChecksBeforeTurn` does for its player, in the game's message box. **No** goes back without
  submitting ("Not submitted: you chose to go back at the game's warning."). A submit forced by the host's time limit
  asks nothing. The foreign stations sum is a replica of `CheckForceOnForeignStations(True)`, which is not called: it
  also rolls world events (independence and rebellions of any nation's possessions). The budget panel's trade
  protection line ("N of M") is K's (`GetPatrolRequirement`).
- **The month (host).** `ComputeAINationIntelCost` returns minus K's cost (spending levels x 10 x FleetSize, as the
  player's) where the game adds the AI's intelligence money; `CheckAISpying` is skipped for K. After
  `CheckShipsOnForeignStations` the host runs, in the view, the player's part of it (at peace) and of `CheckPatrols`
  (at war) for each joined nation (`HostMonthCheck`), with the game's chances and texts: the leader's warning, then
  his displeasure (Prestige -1), a rebellion in one of K's possessions that lacks force (`Rebellion` 2-11), the
  complaints of trading companies or the navy league; at war the leader's annoyance about patrols (a message only, as
  in the game). The lines go to the player as chat notices with the new month (`g_playerNews`), and to the log
  (`nation K month: ...`).
- **Not covered:** `MovePlayerShipsOnForeignStations` (the game moves its player's foreign-station ships each Turn) is
  not run for K; the AI routines' passive reports (research, budgets, wars) and the beliefs about enemy ships
  (`EnemyClassName`, `PlayerIntel`, aircraft data) are still the host's; the game's monthly AI money (25 x FleetSize
  and more for poorer nations, in `ComputeExpenses`) still reaches every nation 1-8, joined ones too.
- **Mixed versions** (protocol 3). A 0.4.0 player of a 0.4.1 host gets its own intelligence in its month (empty at
  first) but no spying check, and its spending changes are lost (its turn has no `[MPIntel]`; the merge keeps the
  host's keys), so it pays nothing and gets no AI intelligence money. It gets no Turn questions, but the month's
  consequences apply to its nation and their notices reach it (chat lines). A 0.4.1 player of a 0.4.0 host
  (no `intel=1`) keeps the 0.4.0 intelligence (the host's), and still gets the Turn's questions (they are local).
- Details: [MODLOG](../MODLOG.md#fix-2026-10-20-joined-players-intelligence-mirrored-the-hosts-no-trade-protection-warnings-v041).

## Network protocol
- **Transport:** TCP; the host listens on the port (default 47624), clients connect. One background thread does all
  socket I/O with `TCP_NODELAY`. A frame is `[u32 length][u16 type][payload]`; the receiver drops frames above 96 MB
  ("message too large") and peers whose frames are malformed ("protocol error"). Each side pings every 5 s.
- **LAN discovery:** **Find LAN games** broadcasts on UDP port + 1 (47625); the host answers
  `RTW3MP!<port>|<host name>|<host nation>|<date>|<players>`.
- **Payloads:** `KV` text (`key=value` lines), or a header and a body (`PackHB`: a 4-byte header length, a `KV`
  header, then binary data such as a `Bundle` of files).
- **Handshake:** the client's `MSG_HELLO` carries `proto`, `mod`, `build`, `name`, `pw`, `battles` (Fight my battles),
  `ls` (live battles possible), since relations v2 `rel` (the player's relation routines run on that PC) and since 0.6.0
  `gen` (1: the player's battle generator's requests replayed; 2: also surprise attacks, kind 2, and the view's
  takeovers). The host answers `MSG_WELCOME` (the final name; since 0.6.0 `gen`: the host's battle requests, 1 = it
  merges a declined battle's result, 2 = it also takes surprise attacks and takeovers) or `MSG_REJECT` with the reason:
  "protocol mismatch (host mod X)", "different game version than the host" or "wrong password".

| Message | Direction | Content |
|---|---|---|
| `MSG_HELLO`, `MSG_WELCOME`, `MSG_REJECT` | client -> host, host -> client | Handshake (above). |
| `MSG_LOBBY` | host -> all | Players, nations, picks, phase, diplomacy snapshot. |
| `MSG_PICK` | client -> host | A nation pick by name. |
| `MSG_STATUS` | host -> all | Phase, remaining time, status text. |
| `MSG_CHAT` | both | Chat lines; `notice=1` flashes the window; `treaty` carries a peace treaty for the player's dialog. |
| `MSG_STATE` | host -> client | The month: header (seq, nation, date, remaining time; `intel=1` from 0.4.1: the package holds the player's own intelligence; relations v2: `kwar`, the nation's war counter, and for a nation under the player's rules `rel=1`, `kwill`, `kts`, `khist`, `humans`; B4: `kvp`, `kevp`, `kpairs`, `kpeace`; 0.5.0: `kao`, the nation's army-offensive months, and `econnews`, the texts of the player's money rules for its player; 0.6.0: `econnews` also carries the war effects' texts, `kblk` the nations blockaded, and with `rel=1` `khelp` (the host applies the allies' war help), `kbuild` (... and the ship seizures of a war) and `klend`) and the slot's files. |
| `MSG_ACK` | client -> host | The month was loaded. |
| `MSG_SUBMIT` | client -> host | The player's turn (header and the submission bundle; from 0.4.1 with `[MPIntel]` and `[IntelReports]`). |
| `MSG_FORCE_SUBMIT` | host -> all | The time limit ran out: submit now. |
| `MSG_DIPLO` | client -> host | A diplomacy request (war, ally, peace, leave, cancel, claim). |
| `MSG_EVENT` | client -> host | The result of the player's random event (relation changes, own fields), or (0.4.1, `intel=1`) of its monthly spying check. |
| `MSG_RELATIONS` (40) | client -> host | Relations v2: what the player's relation routines changed for its nation this month (pairs before/after, alliances, per-relation state, other nations' budget changes, VP, war counter); B4: the peace with the computer's nations (`peace`, `pcode`, `pevent`, `pargs`, `paccepted`, `pwinner`, `penemies`, `pland`, `pships`, `ptaken`, `pdelta`, `plost`, `ptreaty`; 0.5.0: `self`, the nation's own BudgetModifier/Prestige/BaseResources/UnrestLevel changes; 0.6.0: `ally`, `allyvp`, `afunds`, `aships`, `lend`, the allies' war help; `bwar`, `bships`, the ship seizures of a war), also alone for a proposal (`kind=peace`; `test=1` from `peacetest`). 0.6.0 (to a `gen=2` host): `ksa` (the surprise attacks "a,b;") and `sland` (the takeovers of unowned possessions, pland's layout). |
| `MSG_BATTLE`, `MSG_BATTLE_RESULT`, `MSG_BATTLE_CANCEL` | host -> client, client -> host, host -> client | A battle to fight, its result (`fought`, since 0.4.1 `avoided` when the enemy declined it, or why there was none), a battle the host resolved without the player. 0.6.0: the battle's `kind` (2: a surprise attack, played with CheckSurpriseAttack), `att`, `att0..` (the player's generator's attempts or the surprise attack's pairs) and `dc` (the nation's decline counts "own:enemy"); the result's `cand`, `area`, `dc` and `declined` with the slot's result; a kind 2 `nobattle` has its reason in `why`. |
| `MSG_LOCKSTEP` | both (the host relays between two clients) | Live battle traffic. |
| `MSG_PING`, `MSG_PONG`, `MSG_BYE` | both | Keep-alive and round-trip time; leaving. |

New `KV` keys must be ignorable by older peers of the same protocol; anything else needs a new `MP_PROTOCOL`.

## User interface
- **Title screen:** a child window over the title image's `pnStart` panel, above *Exit*, opens the Multiplayer
  window; the message hook opens and closes it with Ctrl+Shift+M anywhere.
- **The Multiplayer window** is a Win32 tool window owned by the game's visible main screen (the strategic screen or
  the title screen): name, password, host IP, port,
  **Fight my battles**, **Host game**, **Join game**, **Find LAN games**, **Leave**, the player list, nation picks
  (**Claim nation**, **Give to AI**), the turn time limit, the host's live-battle option **Computer flies carrier
  strikes in live battles** (engine level 7, off by default), **Start session**, **Advance turn now**, the Diplomacy
  list with its buttons, and the chat. Host-only controls are hidden on a joined player's PC. Settings are kept in
  `config.ini`. Notices flash the window.
- **The Claim land window** (`RTW3MP_Claim`) lets the winner of an agreed peace tick the loser's possessions.
- **The Turn button** reads **Submit**, **Submitted** or **Waiting** on a joined player's PC.
- **The relations view (0.4.1).** The game shows relations from nation 0's point of view (every nation's
  `Tension`/`Allied` is its relation with nation 0), so a joined player's strategic screen showed the host's tension
  bars, wars and alliances. `game::ShownNationIndex()` is K when the PC plays nation K > 0 (PlayerNation =
  BuilderNations[K]: a joined player's slot 77, also after leaving the session; not the host, single player or a battle
  slot, where the player's nation is nation 0). While the tension panel (`ShowTensionLevels`) or the map's strength
  bars (`DrawAreaStrengthBars`) are drawn, `KViewBegin`/`KViewEnd` swap nation K into index 0 and the host's nation into
  index K and give every nation K's relation, as `RunClientEvent` does for an event, and put everything back (also on
  an exception; not while an event dialog has its own swap). After `SetValues` slot K's flag shows the host's nation;
  after `UpdateUIValues` the War results box, the war line ("At war with Spain." / "At war with N nations.") and the
  player name's hint ("At peace.") follow K's own wars, whose months the game doesn't keep; a flag's click opens the
  nation of its slot. VCL and RTL routines without RTTI names (TControl.SetVisible and SetText, System._UStrAsg) are
  taken from call sites in ShowTensionLevels and UpdateUIValues and checked at startup (`Relations view: ok`). Only
  display routines see the view; the month, the merge and the player's orders never do. The intelligence labels follow
  the intelligence hooks ([per-player intelligence](#per-player-intelligence-trade-protection-and-foreign-stations));
  the blockade marks and the relations window are left as they are. Bridge `relview`.

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
  the changes of VP, prestige and fleet morale) and loads slot 77 again. When the AI enemy declines the battle before
  any preview (the game's `AIDeclinesBattle`: "The <nation> navy declines battle! You gain N VP!"), there is no battle,
  as in single player: since 0.4.1 the client sends the slot's result with the outcome `avoided` (the player's VP
  gain), the host merges it and resolves nothing (before, it was "nobattle" and the game resolved the battle between AI
  nations; a host on 0.4.0 still does so). Battles against another player's nation (live) are not affected.
- **The player's battle generator for a joined nation (0.6.0, B6).** For a nation K under the player's rules whose
  player's PC replays it (hello `gen=1`) and fights its battles, the host runs the game's `CheckForBattles2` for K once
  a month, right before its own (hook; `DoTurn2`), in K's view (`game::HostViewEnter`, the host-side view that B5's
  war effects share since B5 round 4): the relations view of the player's routines (K at index 0, permuted relation
  cells), `PlayerNation` := K, `War` := K's war counter, the nations people play at Tension <= 40.
  `CheckAttackLocation` is not run there: its calls are recorded as attempts and answer False (all of them, in the
  generator's order until it has none left; 64 only bounds a runaway generator), invasion battles and raiders against
  patrols answer False. The generator's own error box (its except handler's `ShowMessage`, hooked) is recorded in the
  view instead of shown: the run fails, K gets no request that month and the host's log has the box's text. The
  attempts against computer nations make one request of kind 1
  (`BattleReq.kind`: 0 the computer's generator, 1 the player's, 2 a surprise attack); the client replays them
  in order in its battle slot until one gives a battle, a decline or the enemy's points, and returns which (`cand`) and
  the battle's area. Kind 1 never falls back to the game's resolution. The computer's generator's battles of K against
  computer nations are dropped meanwhile (`HostDeferBattle`), so K has one generator. 0.5.0 clients keep kind 0.
- **Surprise attacks and takeovers at a war's start for a joined nation (0.6.0, B6 piece 2).** With a host whose
  welcome has `gen=2` the client lets CheckForWar's surprise moves run in K's view during its relation turn (B3):
  `DoSurpriseAttack` runs where a war started from peace and K or the new enemy has the SurpriseAttack trait;
  `CheckSurpriseAttack` is recorded there (attacker, defender) and not run; the takeover of an unowned possession
  (CheckForWar's fallback after the landing pick: up to 3 `GetRandomPossession`, then `ChangePossessionOwner`) runs and
  is recorded. The landing in K's own possession stays out (`GetRandomPossessionInArea` answers nil there; when it would
  have found a possession worth < 200 the attacker takes nothing over, as in single player where it would have landed:
  the landing's strength test is not evaluated, a small under-count). The relation turn's KV carries `ksa`
  ("a,b;" in DoSurpriseAttack's order, K with computer nations only) and `sland` (pland's layout, from -1). The host
  (players with hello `gen>=2`) moves each possession by name (`MovePossession` from -1: still unowned, taken by a
  computer nation; chat "In a surprise move, X has taken control of P!") and turns `ksa` into one `BattleReq` of kind 2
  for the battle phase after the month (chat "Surprise attack? A may strike B at the war's start. ..."; not before 1896
  nor 1925-1934, where the game's surprise attack is a submarine attack without a battle, `DoSubSurprise`; not without
  the player's battles). K's battle slot plays each pair with the game's `CheckSurpriseAttack(attacker, defender)`
  (`game::StartSurpriseAttack`: its map message, the "Mobilize all ships for the surprise attack?" question, the
  mobilization and the ships it moves, `DoBattle`) until a battle, a decline, the enemy's points or a True without a
  battle (the player let the chance go); the slot's year is checked again. Results merge like kind 1 (`cand`); a
  surprise attack that does not come about is a `nobattle` with its reason, and the host resolves nothing. Test hooks:
  `battles injectsa`, `testsa`, `sadry`, `saretry` (TESTING.md). Mixes: a 0.5.0 or gen=1 host -> B3's skips stay on the
  client (nothing recorded); a 0.5.0 or gen=1 client -> no `ksa`/`sland`, no kind 2 (as before).
- **Declines and the game's resolution (0.6.0, risk 4).** A battle the player declines (the map question or the
  preview; the slot counts `DoPlayerDecline`) comes back as `declined` with the slot's result, merged like a battle
  (the enemy's points of the decline, prestige, morale): the game resolves nothing. The battles the game still resolves
  (kind 0 only: no battle, failed, time limit, the player gone, the host's decision, a 0.5.0 client's decline) have
  their picture messages captured (`lockstep::BeginTextCapture`) and sent to the chat and the player's news instead
  of a box on the host that would hold the new month.
- **The nation's own decline counts (0.6.0).** The game's `DeclineCount` is saved with the campaign (nation 0's) and
  `EnemyDeclineCount` lives in memory (saved, never loaded), so a battle slot held the host player's DeclineCount and
  the client PC's last EnemyDeclineCount. The host keeps
  each joined nation's own (session file key `kdecl` "K:own:enemy,", dated like `kwar`) and sends them with a battle
  (`dc` "own:enemy"; clients with hello `gen>=1`, not live battles); the client puts them in its battle slot
  (`game::SetDeclineCounts`) and sends the slot's counts back with every result (`dc`); the host keeps them. A client
  under a host without welcome `gen` (0.5.0) sends a decline alone ("You declined the battle; the game resolves it.").
  Not done: the enemy's invasion of a possession after a decline. `DoPlayerDecline` invades only in an enemy invasion
  battle, which K does not get, and the host's month resolves invasions as the host player's (GAME-INTERNALS): K's
  invasions need their resolution in K's view (later work).
- **The month's own state in the battle slot (0.6.0, B6 round 3).** The player's game processes no month and the slot
  is the host's month-end save, so the client puts the month's state there itself: the ship-type flags by the game's
  `CheckShipTypesExist` (after the month's load, the battle slot's load and the slot's reload; before, a joined
  player's battles ran with 0 for all three), and the battle slot gets what `DoTurn2`/`DoStrategicTurn` reset for a
  month (the surprise-attack flag the host's save carried, the possession battle's flag, `HeroicVictory`,
  `tmpBattlePrestigeLoss`, `ImportantConvoy`). Computed on the client, so a 0.5.0 host gets it too; no protocol change.
  Not in 0.6.0: a joined nation's invasions (its own resolution, invasion battles, landings in its possessions, the
  enemy's invasion after a decline) and its raider vs patrol battles (assessed: they fit the attempt path as an attempt
  type that K's slot replays with the game's `CheckRaiderVsPatrols`; MODLOG "B6 round 3").
- **The merge (host):** `ApplyBattleResult` applies only what the battle changed, field by field, for the two
  nations and AI nations; ships missing from the result were sunk; the player's intel fields are never taken;
  nothing changes unless everything applies. A battle between two players' nations also counts for their war score
  (`CreditWarScore`, since 0.3.1): the host's battle hooks never see a battle fought on a player's PC.
- Details: [investigation](../MODLOG.md#investigation-2026-10-14-tactical-battles-joined-players-vs-ai-live-lockstep-pvp-v030),
  [feature](../MODLOG.md#feature-2026-10-15-joined-players-fight-their-battles-against-ai-nations-on-their-own-pc-v030),
  [tests](../MODLOG.md#testing-2026-10-15-joined-players-battles-vs-ai-v030).

## Each joined nation's relations (relations v2)
The game runs every nation but nation 0 by its AI rules
([GAME-INTERNALS](GAME-INTERNALS.md#relations-wars-and-battles-nation-0s-and-the-others)). Relations v2 gives each
joined player's nation K the player's own machinery, built in steps (the branch `relations-v2`; the release number is
decided later). Done so far:
- **B1, a war counter per joined nation.** The game counts nation 0's months at war (> 0) or at peace (<= 0) in
  `[General] War`; the host keeps the same counter for every joined nation (`g_kWar`, reserved nations included).
  - *Stepped by the host* once per month, after the month and the players' diplomacy and before the new month goes out
    (`KWarMonthStep`): +1 at war, -1 at peace (DoStrategicTurn), then 1 if a war started and 0 if the last war ended
    (`KWarFollow`, as WarStartsWithNation and DoPeace). A resent month doesn't step again (the step is marked with the
    session's month sequence `g_seq`). A nation the computer plays has none; a new player's nation starts afresh.
  - *An existing campaign* (or a new player) starts from K's wars, with the host's counter as the guess of how long:
    at war the host's counter if the host is at war, else 1; at peace the host's if the host is at peace, else -12
    (the guess the events used before).
  - *Kept* in `rtw3mp_session.ini` (`kwar=K:n,...`), for the month it was saved with, like the war scores.
  - *Sent* with the month (`MSG_STATE` header `kwar`). The client stores it (`game::SetOwnWarCounter`) before loading;
    `LoadCampaign` with a player nation puts it in place of the save's counter (the host's) before the screen is drawn,
    so the game's own war state ("At war for N months.", "At peace for ..."), budgets, events and the player's routines
    read K's. The client never sends `[General]` back. The battle slot and Copy game get K's counter too
    (`MakeNationPlayer`'s `kWar`). From a host without it the client keeps the host's counter and the relations view's
    own war line ("At war with Spain.").
  - Bridge `kwar` (host: the counters, `kwar <K> <n>` sets one; client: its own and the game's). Log: `war counter of
    nation 1: -68 -> 1 (the month)`, `war counter: -68 (the host's nation) -> 1 (nation 1's own)`.
- **B2, the computer's rules leave K's pairs alone.** A joined nation K follows the player's rules when its player's PC
  runs the player's relation routines (B3) and says so in its hello (`rel=1`); while the player is away the nation keeps
  what its last player announced (`relRules` in `rtw3mp_session.ini`), Give to AI ends it, and a host test can switch
  it on (`relrules <K> 1`). `hookcb::PlayerRulesNation(K)`, host only. Then on the host:
  - *The computer's relation routines* (CheckAITensionEvents, DoAITensionChanges, CheckStopPlayerAgression,
    CheckAllyTrouble, CheckBlitzkrieg, CheckChineseConcessions, CheckForceOnForeignStations, CreateArmsControlTreaty,
    CheckForRevolutionInEnemyNation, ReduceTimeLimits, HandleAIPeace) run with every pair (K, j), j in 1..8, looking
    neutral: tension 5 (no war, no outbreak: 10 < t < 40; no common-enemy or most-tense logic: > 7; no alliance attempt:
    0) and no alliance; afterwards the real values are put back (`AIMaskEnter`/`AIMaskLeave`, depth-counted; changes
    the routines made to masked cells are dropped and logged). So no AI war, AI peace, alliance, lapse or "seeking
    improved relations" in K's name, and none of their boxes or turn messages on the host. `ChangeAITension` leaves
    K's pairs alone at any time (`relations v2: N tension change(s) of the computer's rules left out`). Since 0.6.0
    CreateArmsControlTreaty's war test sees K's real wars (B5 (f) below).
  - *The host's player routines* treat K as a player, not as a computer nation (`PlayerMaskEnter`): during CheckForWar
    K's tension with the host reads 5 when they are at peace, its war plan (`WillGoToWar`) 0 and its own alliance cells
    0 (no declaration, surprise landing, volunteering, war-plan warning or alliance quarrel in K's name; afterwards K's
    alliance cells take the other nations' side of each pair, so their decisions about K stay); during
    RandomTensionChanges K's tension with the host reads at least 6 (no alliance offer in K's name). Both keep the
    routine's own changes of the host-K tension.
  - Unchanged: the players' diplomacy (wars, peace, alliances between players), the events, the host's coalition (a
    computer nation joining the host's war, also against K), battles (`CheckAIvsAIBattles` sees K's real wars).
  - A game exception that unwinds through a routine still puts the mask back (`MaskedCall` runs the original inside
    `__try`/`__finally`); `ResetRelationMasks` at the month's end is the last safety net.
  - From B2 on, K's relations with the computer's nations change only by K's player routines (B3), the players'
    diplomacy and the events; a client that doesn't announce `rel` keeps the computer's rules (0.4.x behaviour).
- **B3, the player's relation routines on K's PC.** The host tells a client whose nation follows the player's rules
  in the month's header (`rel=1`, with `kwill`/`kts`: K's per-relation state, `khist`: K's past wars as
  opponent.monthsAgo.winner, `humans`: the nations people play). After the month's event roll the client runs
  (`ClientRunRelations` -> `game::RunPlayerTurn`) the game's `RandomTensionChanges` (only in a month without an event:
  the game gives the player one of these a month), `TensionEffectOfPreviousWars` and `CheckForWar` in K's full view:
  - *The view* (`PvEnter`/`PvLeave`): K at index 0 and the host's nation at index K in the nation list; every record's
    relation cells and alliance cells permuted the same way (its `Tension` = its relation with K, `AITension[w]` = its
    relation with the view's nation w; the own cells of K's and the host's records 0, as nation 0's own `Tension` and
    `Allied`: the game's loops over nations 0..8 read them); the other records' `WillGoToWar` (their war plans against
    K) and `TechSharing`
    (K's deals) from K's state (0 for the nations people play); K's war counter (B1) in place;
    `CheckWarsWithNation` answers from K's past wars (the host records a war of K that ends: `g_kHist`). The other
    nations people play are masked as players (`MaskAsPlayer`: no war plan, declaration or alliance offer between
    players). `GetRandomPossessionInArea`, `GetRandomPossession`, `DoSurpriseAttack` and
    `CheckForPlayerSurpriseInvasion` do nothing in the view (surprise landings and attacks move possessions and ships;
    0.6.0: with a `gen=2` host the surprise attacks and takeovers are recorded, above). `EventActive()` is true
    meanwhile (no load, save, relations view or other swap).
  - *The dialogs* (questions, declarations "Russia has declared war on us!", spy reports) are the player's; the turn
    messages the routines wrote are shown afterwards in the game's turn report (`ShowTurnMessagesOf`, the form's
    `TurnMessages` list swapped for the call).
  - *The results* (`PlayerTurnResult`, real indices): every pair whose tension or alliance changed (before/after), K's
    per-relation state afterwards, the other nations' BudgetModifier changes, K's VP change, K's war counter. They go
    to the host at once (`MSG_RELATIONS` = 40, a number of its own: older hosts ignore it) and into this PC's campaign
    for K's screen; the relation cells and per-relation fields are put back first (other fields keep what the routines
    did: K's own budget, prestige, corruption go to the host with K's turn).
  - *The host* (`HostApplyRelations`, once per nation and month, before the month runs, as events are; the month is kept
    in `rtw3mp_session.ini` too, `krelm`, so a host that hosts again in the same month applies no second turn): K's
    pairs with
    the computer's nations take the routines' changes (a war one declared on K starts with `StartWar`); between
    players only the tension moves (deltas, at most 40); another pair only goes to war (a computer nation that joined
    K's war on K's side; against the host it is the host's war, with a Notice); alliances unless between players or at
    war; K's per-relation state kept (`kwill`/`kts` in `rtw3mp_session.ini`, with the month); the other nations'
    budget changes; K's VP. Chat `* War: Russia and Germany are now at war.`; log `relations of Captain (nation 1;
    tension,previous wars,war; 2 message(s)): war 1-3 1-0 5->6 ...`.
  - Bridge `relturn` (client, async: the routines now, also a second time) and `rellast` (the last turn sent or applied).
- **B4, peace with the computer's nations.** The host's header carries K's war score with its computer enemies (`kvp`,
  `kevp`, `kpairs`: the ledger, `g_warVP` pairs of K and a computer nation counted from 0:0 by `OnBattleVP` and
  `CreditWarScore`), `kpeace` (K's peace this month settled already) and `humans`, also in a month whose turn was
  applied. On K's PC:
  - *The turn's peace part* (`game::RunPlayerTurn`, after B3's routines in the same view; when K is at war with a
    computer nation; not in a month with another event): the player's peace decision of `CheckTurnEvents` mirrored in
    C++ (`PeaceDecision`, the game's `Random`; the war counter as the game has it there, before the month's step: K's
    counter less one while at war), its peace event (`GetPeaceEvent`, the game's `TdlgEventAnswer`, K's
    own event cooldowns), its answer's `NegotiatePeace` -> `DoPeace`, all the game's code with its dialogs. *A proposal*
    (the Diplomacy list's computer row "Propose peace", after the month's relation turn, once a month, before the turn
    is submitted) or a test (`peacetest`): `NegotiatePeace(chance, lossGain, force)` at once (`relations=false`: the
    peace part alone; the war counter as shown).
  - *The masks* (`PeaceMaskEnter`/`Leave`, also on a game exception): the nations people play out of it (tension 40, no
    alliance with K; all their cells put back), K's VP/EnemyVP from the ledger, TotalWar/HasBeenBattle/AutoBuildSubs
    and the month's war record (K's outcome is read from it) set aside, the possessions' nation indices (TakenFrom,
    Invaded) in view indices, K's cooldowns. B3's skip of `GetRandomPossession*` is B3's routines' only.
  - *The records* (hooks, only while the part runs): `ChangePossessionOwner`, `TransferShipAsReparation` (ship id, new
    name, log line), `NegotiatePeace`'s arguments, `ShowPeaceTreaty`'s lines, `DoPeace` (the other nations' Prestige
    kept). The result (`PeaceResult`): code, event, arguments, accepted, winner, enemies, possessions, ships, the war
    state of the possessions of K's war, the other nations' BaseResources/Prestige/BudgetModifier/HasLostWar changes,
    the treaty; the relation changes in B3's diff. K's own effects stay in K's campaign and reach the host with K's
    turn (the merge replaces K's sections: one copy of each ship); while K's war with a people's nation goes on,
    DoPeace's resets of K's war counter, budget, submarine policy, unrest, corruption and blockade are put back. Sent
    with the turn (`MSG_RELATIONS` peace keys) or alone (`kind=peace`, `test=1`).
  - *The host* (`HostApplyRelations` + `HostApplyPeace`, at once): K's wars end at the game's tension (no host
    `DoPeace`), the computer-only wars of K's ex-enemies too, alliances; then the possessions (by name, owner check),
    the ships (by id, the game's hand-over, the player's name), the war state, the other nations' changes
    (BudgetModifier only without a war left), K's history with the winner, blockades and K's VP without a war, the
    ledger pairs erased, the war counter (`KWarFollow`), chat "* Peace treaty between ...". A proposal once a month per
    nation (`g_kPeaceMonth`, kept in `rtw3mp_session.ini` as `kpeacem`; tests exempt). The merge drops a ship of K's
    turn that another nation holds on the host
    (`saveio` `DropForeignShips`). A proposal's changes to K's relations with the people's nations are skipped (the
    client keeps them out: its people's nations are the header's `humans` and the lobby's, a nation claimed or reserved
    after the month's state included). Every apply that changed something redraws the host's screen (`RefreshUI`: its
    own relation with K, lands, ships).
  - *Kept at once (0.5.0):* after an apply the host writes the month's guard (`SaveReservations`) and then saves its
    campaign (`SaveAfterRelations` -> `game::SaveGame`, in the planning phase when the game is free; else pending until
    `Tick` finds it free; the month's own save in `RunHostTurn` covers a pending one). A host whose game stops in the
    month and is started again finds the turn and its peace in its campaign, and the joined players reload that month.
    K's own fields the routines changed travel with the turn (`self`: BudgetModifier, Prestige, BaseResources,
    UnrestLevel; not Funds) and are applied with it, so the saved campaign holds them too (the merge later puts the same
    values in place).
  - Bridge `peacetest`, `peaceevent` (with a chance for its NegotiatePeace), `peacelast` (client/host), `kwarvp`
    (host), `findship`, `nflag`.
- **B5 money, the player's economy rules (`economy.cpp`, host only).** The host runs K's month as one of the computer's
  nations, so the game gave K the computer's money and budget bonuses. `economy::Install` (after `game::InstallHooks`)
  finds each rule by its routine's RTTI name and a byte pattern unique in the routine and hooks the instruction with
  MinHook (generated thunks: leave the instruction out, answer `cmp nation,[form+PlayerNation]` as "the player", or
  return at once), acting only for `hookcb::IsHumanRemoteNation(K)`: `ComputeExpenses` gives K `Funds +=
  GetMonthlyBalance` (no subsidies, no BaseResources floor or x 0.9), `CheckBBNumbers` (+6000 x FleetSize),
  `DeHaltAllShips` and `HaltMostExpensiveShip` return at once, keep-up skipped, preeminence and a war's start by the
  player's rules, the January cuts, a coup's +1, `CheckAITensionEvents`' and `CheckForWar`'s +1s and the spy money left
  out, maintenance at the player's prices. A rule that doesn't match stays the game's. `AdvanceShipConstruction` is
  hooked for the record only (the shared rules' change of K's funds). One log line per joined nation and month after
  `DoEndOfTurn`'s `CheckForWar`; bridge `econ <K>`. Nation 0, the computer's nations and the joined players' PCs are
  unchanged.
- **B5 money, part 2 (0.5.0, `economy.cpp`, host only).** After the game's `CheckPlayerFunds` and `CheckTurnEvents`
  each joined K runs the player's versions: debt penalties, the money of corruption (its question takes the first
  answer), hoarding cuts, the war loan (confirmed at war in 0.6.0: K's counter before the host's step, the text in K's
  turn report) and `SmallFleetCheck`'s complaints, budget rules and notice, with K's own war
  counter (`hookcb::KWarCounter`, session.cpp), K's own ParliamentDelay and army-offensive months (`kecon` in
  `rtw3mp_session.ini`, with the month); K's budget is x 0.9 while its months last (`GetModifiedBudget`'s ArmyOffensive
  test answers for K; the game's global is never written for K). The computer's last decisions for K are kept away
  (5 more `g_skips`, 8 more sites: research doctrine, ammunition, ships sent home, ASW patrols and escorts, air groups,
  a technology partner, a ship purchase, armed merchant cruisers scrapped after a peace, the treaty cut, China's
  concessions). The rules' texts go to K's player with the new month (header `econnews`, shown in the game's turn
  report there), `kao` gives the client's own ArmyOffensive. The month's line and `econ <K>` carry each rule's effect
  and the month's end funds (`fundsEnd == after + ruleFunds + otherFunds`).
- **B5 (a), blockades (0.6.0, `warfx.cpp`, host only).** The game decides blockades for nation 0's wars only. After
  each of the game's `CheckBlockadeStatus` calls (game.cpp's load-guard hook tells warfx; the load's: the flags only)
  every K under the player's rules at war (its own counter > 0) gets the player's check: K blockaded (by any enemy) ->
  its flag and 20 x (Random(3) + FS) for each computer enemy; an enemy other than the host blockaded -> its flag and the
  same for K (`hookcb::CreditWarVP`: the nation's VP and the pair's war score, the B4 ledger; the points of the host's
  wars stay the game's). `DoBlockadeEffects` (hooked) of K is the player's (x to K's blockaders: the enemies that
  blockade it on their own, by the game's test with K's other wars masked; unrest by K's counter; K's messages), of an
  enemy flagged only this way x to the K's at war with it; the loop's own blockade rule in `CheckUnrestChanges` (hooked
  for the end of the loop) runs there with K's counter and the flag reads 0 for the rest of that nation's turn.
  Since round 4 the blockade tests of K's frame (K's, each enemy alone, each enemy's) run in K's view on the host
  (`game::HostViewEnter` with no nation masked, B6's view): the game's `GetBlockadeStrength` counts the allies from K's
  side (against K the coalition of its enemies; for K its own allies at war), not the host's alliances; without the view
  (not in place) the host's frame as before (logged). Bridge `warfx strength E N [K]` shows both.
  `StopBlockadedMoves` (hooked) runs with War 1 while the host is at peace and such a flag is set. The messages go
  with K's `econnews`, the flags to every player (`kblk`, set after the client's load). Bridge `warfx`, TEST `warfxblk`,
  `warfxrand`.
- **B5, war weariness (0.6.0, `warfx.cpp`, host only).** `CheckUnrestChanges`' war-weariness rule (every nation of
  government 2: the host's War and its EnemyVP against its VP) is hooked at its `call` of the War > 0 test: for K under
  the player's rules it runs with K's own counter and war score (`hookcb::KWarScore`: the B4 ledger with the computer's
  nations plus the score of each war between players), the unrest on the host's month, the text with K's `econnews`;
  the host's nation and the computer's nations keep the game's rule.
- **B5 (e), building for war (0.6.0).** Host (`warfx.cpp`): the War > 0 tests of the build rules every nation shares
  (`AdvanceShipConstruction`'s submarine rule and `HandleEconomicGrowth`'s war budget, hooked at their `call`;
  `CheckForConstructionDelays`, hooked at its entry, reads the nation's counter in the game's War for the call) answer
  each joined nation's own counter; the war budget's text goes with K's `econnews`. K's PC (game.cpp `BuildWarPart`,
  right after the allies' war help in the same view, while the host sends `kbuild` and K's counter is above 0): the
  game's `CheckBuildingShipsForWar` with the ships between K and a computer nation in view terms (every other ship reads
  as built at home), its pictures and question for K's player; the ships it hands over are recorded by the peace
  part's hook and go back as `bwar`/`bships` (by ship id), applied by `HostApplyBuildWar` (`HandOverShip`, only between
  K and a computer nation). Ships between two people's nations stay where they are (the host's own pairs keep the
  game's rules on the host). Bridge TEST `shipbuilder`.
- **B5 (c), raiders, submarines and the trade war (0.6.0, round 4, `warfx.cpp`, host only).** The game runs them for the
  host's nation's wars only, and only while its War > 1. A mid-hook at DoStrategicTurn's `call <War > 0>` right after
  that block (reached from both branches, before CheckBuildingShipsForWar and DoEndOfTurn) runs, for each K under the
  player's rules with its own counter above 1 at war with computer nations, a frame in K's view
  (`game::HostViewEnter`, the people's nations masked: the host's own run covers the host's wars, and a war between two
  joined nations keeps the computer's rule): `CheckSubmarineActivity` of K and of each enemy, CheckRaiderActivity's two
  loops (`CheckRaiderSinkings` per pair with its tests, the area markers reset), then `CheckResultOfSinkings`. Every
  nation's monthly loss fields read 0 for the frame and the frame's own are added back; the form's trade VP fields,
  EnemyVP and K's VP field hold K's war score for the frame and are put back; the trade VP of each call go by pair
  (`hookcb::CreditWarVP`, "trade war"); the map markers and the other nations' FuelShortage are put back. The frame's
  picture messages (`lockstep::BeginTextCapture`), its boxes (`ShowMessagePos`, hooked: it records only while a frame
  runs) and its turn messages (fresh TurnMessages/RaiderSummary lists) go to K's report (`econnews`) with a summary
  line "Trade war this month: ..."; the host sees nothing. `CheckAIvsAISubmarineSinking` (hooked) answers False for
  K's computer pairs while their submarine war runs in K's frame. Bridge `warfx K` (`sea=`), TEST `warfxsea`.
- **B5 (f), the arms-control conference and K's money (0.6.0, round 4, game.cpp `MaskedCall`, economy.cpp, host).**
  `CreateArmsControlTreaty` runs under B2's mask, but its first test (`AnyNationIsAtAIWar`: any tension above 40 among
  nations 1..8) sees the real wars of the masked pairs (`TreatyWarsEnter`/`TreatyWarsLeave`, also when it runs inside
  another masked routine such as `CheckAllyTrouble`): a war of a joined nation ends the conference at once, as any war
  does in single play (before, a treaty during K's war scrapped its ships under construction after ComputeExpenses, cut
  its BudgetModifier and made K's PC cancel it with a box). A treaty at peace is a rule every nation shares: K's scrap
  refunds and BudgetModifier -2 go into its month's record (`economy::TreatyBefore`/`TreatyAfter` around the call:
  `econ` treaty=, treatyBM=, conference=), so the 0.5.0 money check covers them. TEST `econtreaty`.
- **B5 (d), the allies' war help (0.6.0, game.cpp `AllyHelpPart`, K's PC).** In the relation turn (B3), before the
  relation routines and in the same view, while the host sends `khelp` and K fights a computer nation with its counter
  above 1: the game's `WarHelpFromAllies` with its dialogs for K's player; the people's nations read as neither at war
  nor allied with anyone (their help and wars are their own), K's own LendLeaseDone (the host keeps it: `klend`, any
  date) and CoBelligerentDelay (this PC's memory: the game never saves it) in place. Relations and alliances it changes
  go with the turn's diff, K's VP with `vp`; `allyvp` (the war score, split over K's computer enemies), `afunds` (an
  ally's 2000), `aships` (lend-lease, by ship id as a peace's) and `lend` are applied by `HostApplyAllyHelp`. Bridge
  `setally`, TEST `allyrand`.
- **B5, the enemy-revolution rule (0.6.0, game.cpp `RevolutionPart`, K's PC).** First in the peace part of the turn
  (B4's masks and records on; the game's `CheckForRevolutionInEnemyNation` without B2's mask, War as the decision's), with
  K's own RevolutionPending (the host keeps it: header `krevo`, result `prevo`, session file). A computer enemy's
  revolution ends its war with K: the general peace (`DoPeace`: B4's accepted peace; the decision is skipped) or a
  separate peace (`pseparate`: `HostApplyPeace` ends only that war's score and blockade flag, `HostApplyRelations` the
  enemy's own wars; K's other wars go on). The enemies' unrest changes travel as `pdelta` UnrestLevel. The people's
  nations read as at peace with K (B4's mask): a war between players never ends by it (logged). TEST `revotest`. On the
  host the game's own rule (B2's thunk, `MaskedCall`) keeps every joined nation at war with the host out of it (its
  unrest reads at most 10 for the call, the rule's own changes kept; logged "revolution (host)").
- **The disarmament treaty and a joined nation's war (0.6.0, session.cpp `HostArmsTreatyCheck`, game.cpp).** The
  treaty and the submarine ban are campaign-wide, and the game ends both when any war breaks out
  ([GAME-INTERNALS](GAME-INTERNALS.md#the-disarmament-treaty-and-the-submarine-ban)). A joined nation's war counts as
  one, like the player's:
  - *The host* ends them (the game's `CancelTreaty` without its box: TreatyTimeRemaining 0, no limits, SubBanTime 0)
    while it is at peace itself and a joined nation's own war counter (B1) is above 0: right after the counter follows
    a war (`KWarFollow`: the relation turn, the month's step) and before any month goes out (`BroadcastState`). Once:
    afterwards nothing is in force. Log `arms treaty: the war of nation(s) 1 ends the disarmament treaty (34 month(s)
    left, ...) (the month)`; while months remained, the game's line "The outbreak of war cancels the disarmament
    treaty!" goes to each such nation's turn report (`econnews`, once) and the chat gets `* The outbreak of war cancels
    the disarmament treaty! (Germany at war)`. The host's own wars stay the game's (its box at the screen's refresh).
  - *A joined player's PC* ends them itself only with such a host (the month's header `armstreaty=1`; a hook on
    `CancelTreaty`): at its first screen refresh at war, without the box (months 0 first), e.g. right after its relation
    turn started a war, so the nation is free of the treaty in that month as the player is in single player (log `arms
    treaty: ended on this PC for its nation's war, as the host ends it (...)`). With an older host the copy keeps the
    host's treaty, without the box (log `arms treaty: CancelTreaty left to the host (a joined player's PC, an older host;
    ...)`), and so does a view on the host. Before 0.6.0 the first screen refresh at war (`SetPlayerNation`) showed the
    box and ended the treaty on that PC alone, every month (the client never sends `[General]` back), also in the
    battle slot.
  - *The conference* (`CreateArmsControlTreaty`) sees the wars of the nations people play, so a joined nation's war
    ends it at once as any war does ("With a war already in progress ..."): one mechanism, B5 (f)'s
    `TreatyWarsEnter`/`TreatyWarsLeave` in `MaskedCall` (above). A treaty it makes at peace goes into each joined
    nation's money record (`econ` treaty=, treatyBM=, conference=).
  - A new header key only, `armstreaty=1` (older clients ignore it; the treaty travels in `[General]`, the line in
    `econnews`): older clients get the host's end with the next month; an older host keeps its treaty and this client
    keeps it too, without a box. Bridge `armstreaty` (TEST `set`, `war`, `host`, `conference`, `refresh`);
    TEST `econtreaty` (the conference at the month's end; `cancel`). Suite scenario `arms_treaty`.
- Details: [MODLOG B1](../MODLOG.md#feature-2026-10-20-relations-v2-b1-a-war-counter-for-each-joined-nation-relations-v2),
  [B2](../MODLOG.md#feature-2026-10-20-relations-v2-b2-the-computers-rules-leave-a-joined-nations-pairs-alone-relations-v2),
  [B3](../MODLOG.md#feature-2026-10-20-relations-v2-b3-the-players-relation-routines-on-the-joined-players-pc-relations-v2),
  [B4](../MODLOG.md#feature-2026-10-21-relations-v2-b4-peace-with-computer-nations-relations-v2),
  [B5 money](../MODLOG.md#feature-2026-10-22-relations-v2-b5-the-players-economy-rules-for-a-joined-nation-relations-v2),
  [B5 money, part 2](../MODLOG.md#feature-2026-10-22-relations-v2-b5-money-part-2-the-players-remaining-economy-rules-and-the-computers-last-decisions-for-a-joined-nation-050-dev),
  [B5 blockades](../MODLOG.md#feature-2026-10-23-b5-blockades-for-a-joined-nation-060-dev),
  [B5 war help from allies](../MODLOG.md#feature-2026-10-23-b5-war-help-from-allies-for-a-joined-nation-060-dev),
  [the disarmament treaty and a joined nation's war](../MODLOG.md#fix-2026-10-23-the-disarmament-treaty-and-a-joined-nations-war-no-box-every-month-the-host-ends-it-once-v060-dev),
  [the save after the apply](../MODLOG.md#fix-2026-10-22-a-host-restart-in-the-month-keeps-the-months-relation-turn-and-peace-050-dev).

## Live battles between players (lockstep)
- **Roles.** A is the authority (side 0, owns the clock and the snapshot): the host for its own battle against a
  joined player's nation, or the first player's PC for a battle between two joined players (the host relays
  `MSG_LOCKSTEP` by battle id). B is side 1. A host battle against a player who doesn't fight battles, or whose PC
  can't play live battles, is fought by the host against the AI as in single player.
- **Start.** A holds the battle at minute 0 and saves it (the game's battle save and the campaign with the battle in
  progress), sends the files, the RNG seed, the game speed, the battle-relevant preferences and its x87 control word.
  Both PCs load the battle through the game's own resume path with the seeded RNG and compare a hash before minute 1.
  Since engine level 15 the preferences sent are only the 14 the load and the simulation use (GAME-INTERNALS, "Two
  PCs"): B swaps A's into its preferences record before every load of the battle (start, resync, rejoin) and gets its
  own back when the battle ends on that PC (the end, a drop, an abort, leaving), in memory only (the game writes the
  Preferences file only from its new-game and nation dialogs); the display preferences stay each PC's own (until
  level 14 B took A's whole ranges, its dark mode, sound volume and range circles too). The battle screen's
  *Preferences* dialog runs as usual, and a change of one of the 14 is put back after it, with a chat line. B also
  sets A's x87 control word before the load (until level 14 only after it), and logs it when its own differs.
- **The map of each PC's side (engine level 15).** The game decides in the simulation and in every load what the map
  shows for its PlayerSide, 0 on both PCs, so B's map showed side 0's picture: B's own ships hidden under their
  flags until side 0 saw them, side 0's ships with their names, and both maps every division's flag. After every
  outermost simulation step that can change it (the minute's start and end, a detach) and after every load each PC
  applies the game's rules for its own side, outside the simulation (`ApplyMapView`: `TFightingShip.SetVisible` for
  the ships, the flags' hidden setter, the game's relabel of every ship; `Hidden` is in the display's dual set, the
  flags, wakes and labels are display objects, so the hash and `ls leakcheck` see nothing). B's battle screen stays
  transparent while it loads (a layered window with alpha 0 from the battle form's reset to the end of the load,
  `CoverBattleForm`), so the first side's picture the load draws is never shown, and B's ensign at the top left is
  side 1's flag (`ApplyOwnFlag`; the resume copies the campaign screen's flag, the first player's in the battle slot).
  `ls mapview` shows the counts per side; `ls mapview off` gives the game's own picture back (tests).
- **CPUs, the x87 control word and the data files (engine level 16).** Intel and AMD can round the x87's
  transcendental instructions (FSIN, FCOS, FPATAN, ...) differently in the last bit, and every one of them in the
  game sits in 11 small RTL routines (GAME-INTERNALS, "Two PCs: CPUs and data files"). `x87.cpp` hooks all 11 by
  their bytes with the first live battle (all found before any is hooked); outside a live battle a hook jumps to the
  game's routine. From the load to the battle's end (A decides: the start's `sm=1`), both PCs compute them in
  software (`softmath.h`: the argument rounded to a double, fdlibm's algorithms in SSE2 doubles, the same bits on
  every CPU); a stub check at install keeps each CPU's own maths if it fails. B loads with A's x87 control word, and
  both PCs check it before and after every simulation step, at every minute's boundary, every release and every
  hash, and put A's back (logged). `datafiles.cpp` hashes the game's `Data` folder but `Data\Graphics` in a
  background thread when a session starts (the host's `Prepare`, the client's `Join`); A's start carries the hash
  (`dh=`), a B whose hash differs refuses before loading and sends its file list in the `abort` (`dm=`), and A names
  the files to both players (`datadiff`) and fights the battle against the computer. A READY mismatch is explained
  in a few words on both PCs: B's `ready` carries the READY hash's parts (`g=`), its CPU vendor (`cpu=`) and control
  word (`cw=`), and A's `desync` carries the words (`diag=`) for the chat line and the notices. Measured cost of the
  software maths: none within the noise
  ([MODLOG](../MODLOG.md#investigation-2026-10-20-live-battles-between-two-pcs-cpus-and-data-files-v041)).
  Engine level 17: the software reproduces the x87's own results to within 1 ulp of a double (its 66-bit pi, its
  out-of-range and special-value rules, the game's Exp under the battle's 53-bit precision), and A's start carries
  the campaign's ship-type flags (`st=`: a battleship, a battlecruiser, a carrier exists; only a processed month sets
  them, and the identification reads them), which B uses for the load and the battle (its own come back at the end)
  and which are in the state hash
  ([MODLOG](../MODLOG.md#fix-2026-10-22-the-strike-battles-desync-the-ship-type-flags-and-the-software-maths-checked-v041)).
- **Determinism.** The game's `Random32Proc` is hooked: simulation draws come from the shared seeded stream, display
  draws from a local one. Context hooks mark the simulation routines and the display routines; the display fields
  that drawing code writes (`Hidden`, identification, sighting, since engine level 14 `LightState`) are kept as a
  simulation set and a display set per
  ship. `ls leakcheck` reports any simulation change made between steps. Display routines see this PC's PlayerSide
  even when the simulation calls them. Since 0.3.2 they include the *Show air formations* list
  (`TdlgShowAirFormations.UpdateListSortedByBase`): EOTP refreshes the open window inside the minute, and the list
  shows PlayerSide's formations (before, B's window listed side 0's). Since 0.3.3 the division report's ship list
  (`TdlgDivisionReport.FillShipList`, also called by the torpedo dialog) is one too: it lowers listed ships'
  `CurrentSpeed`, a simulation field, on the PC where the report opens, so in a live battle every ship's
  `CurrentSpeed` is put back after it (before, opening a report could desync the battle). The battle save keeps no
  ship identification, and the battle screen's first drawing during a load identifies enemy ships with this PC's own
  display draws; since 0.3.3 the first capture after every live load gives the simulation the identification of a
  freshly loaded ship (`TFightingShip.Create`'s type, no class) on both PCs, and the display keeps what it drew
  (before, each PC kept its own as the simulation's, and the sighting reports' shared draws, which depend on it,
  could make the battles drift apart now and then).
- **Every minute.** A releases each minute; both PCs run it with the game's own minute routine. Orders are field
  values of a side's own divisions, ships and forces (since engine level 6 also the forces' routine air ops settings
  and the side's land-CAP request, since level 7 each air formation's strike fields, keyed by its `AFId`, and the
  carriers' aircraft torpedoes and missiles, since level 9 the side's *Fire at will* and the ships' pending
  heavy-missile targets): a guard turns local edits into numbered batches that both apply before the next minute.
  After every minute both hash the battle state (including the RNG; since engine level 3 the order of battle: each
  force's divisions in order, leads, roles, `myForce`, `Admin`; since level 5 every ship's torpedo and missile mounts
  and more of each torpedo's fields; since level 6 the force air settings, the search count-downs, the land-CAP
  requests and each division's `CappedDivision`; since level 7 the air formations' strike fields and links, the
  aircraft weapons aboard, the AFID and ScoutCount counters; since level 9 the pending missile targets, *Fire at will*
  per side and the submarines; since level 10 the campaign's Year, Month and Day); on a mismatch A resyncs B from a
  new snapshot. More than 3 resyncs without 10 good minutes in between hand side 1 to the AI.
- **The clock.** A owns it: run, pause, the game speed and the end are requests (`ClockRequest`; B sends them to A),
  and a new speed takes effect with the next release on both PCs. The speed box (`cbGameSpeedChange`) and, since
  0.3.2, its arrows (`UpDown1Click`) only make requests: an arrow asks for one step from the speed in use (on B, the
  speed A announced last), by the game's own rule (at most *Fast* while ships are in contact). VCL puts a toggle
  speed button down before its click handler runs, and the game's speed governor reads *Fastest available*'s `FDown`
  at minute boundaries: since 0.3.2 its blocker puts the button back (0.3.2: up, with the game's own routine, the one
  the down arrow calls; since engine level 2 as A's clock has it, and the click becomes a clock request, see *Rules
  for human sides*). Every other toggle speed button whose click is blocked is put back as it was too
  (`PutToggleBack`: `TSpeedButton.SetDown(not FDown)` when `FGroupIndex` is not 0).
- **Actions.** *Immediate* actions (detach; since engine level 7 the strike dialog's launches) run at once on the PC
  where they are given, between minutes, and are replayed on the other PC; if both players detach at the same
  boundary, A resyncs (launches don't need it: see *Air strikes*). *Deferred* actions (engine levels 3 to 5: change
  force, change lead, battle turn-away, auto-run, torpedo salvoes; level 9: missile salvoes) are not carried out where
  they are given: the hook
  records them (`D` items: kind, the boundary they were given for, the keys of their objects and the values their
  dialog chose) after the player's earlier order edits (an earlier batch), says "... ordered; it takes effect when the
  next minute starts", and both PCs carry them out at the release that names the batch, inside its shared section
  (simulation context, shared dice). The game's own conditions are checked again there (a skipped order is reported to
  its player only); the game's messages of a replay reach only the player who gave it (`g_dfActor`), and the UI state
  the game routine reads or writes (`SelectedForce`, the selection flags, the Force orders and Select lead dialogs) is
  put back. A resync or the end of the live battle drops pending ones, and their player is told. Kinds can be "this
  boundary only" (torpedo and missile salvoes): released at another boundary, they are dropped on both PCs.
- **The release order** (the same on both PCs): (1) the other PC's immediate actions; (2) side 0's batches in
  sequence order, each item in the order it was given: values and deferred actions; (3) side 1's likewise; (4) the
  formations launched at this boundary into one order (by `AFId`). Only detach counts for the "both players acted"
  resync; every immediate action counts for B's "own action missed the release" resync.
- **Force orders (engine level 3).** *Change force* (ship tree popup): the game's own question, then a deferred
  `chforce`; at the release both PCs move the division as the game's handler does (the game's `TList` methods,
  `myForce`, `Admin` minus 1) and the owner's ship tree is refilled. *Change lead* and *Battle turn away* (Force
  orders): the buttons and the Select lead dialog work as in single player; its OK records a deferred `lead` /
  `turnaway`, and at the release both PCs call the game's `SetForceLeadDiv` / `DoBattleTurnAway` with `SelectedForce`
  set to that force.
- **Save (engine level 3).** *Save* is a clock request. In the host's own live battle A writes a crash checkpoint at
  the next boundary where no minute runs (the snapshot's two save calls into the host's campaign slot; nothing is sent
  or loaded; pending orders stay pending) and both players read "Battle saved (minute N) in the host's campaign slot:
  ...". In a battle between two joined players A answers that the host keeps the battle; nothing is saved. Loading
  the host's slot resumes the battle the game's own way, and the game gives side 1 to the computer itself; a check
  after every campaign load (`lockstep::OnCampaignLoaded`) would do it if side 1's ships were still flagged human.
  Since engine level 9 that battle waits for its player ([resume](#live-battles-between-players-lockstep), below).
  A resync puts side 1's computer-control flags back after its load (the game's resume sets them all; `ai1=` in the
  start message), and since engine level 13 side 0's after every load (`ai0=`: the resume gives each PlayerSide
  division of a computer-controlled force to the computer, also one the host's player had taken back).
- **DESYNC reports (engine level 13, diagnostics only).** Each PC keeps the call sites of its shared random draws of
  its last four minutes and, at every boundary, a copy of the bytes of the battle's objects in the simulation's set
  (the battle screen, forces, divisions, ships, air formations, torpedoes, submarines) for that boundary and the one
  before. A DESYNC makes A write `ls_desync_A_<battle>_<epoch>_<minute>.txt` into the RTW3MP data folder and send B a
  `desync` message ahead of the resync; B writes its own (both stand at that boundary: A releases no minute before
  B's report of the last one is verified). `tools/desync_diff.py` shows where the two lists of call sites part and
  which fields differed at the boundary before. The recorder reads only: no draw, no write. Engine level 14 adds the
  objects' bytes at the start of each minute (StartMinute, after this PC's display ran between the minutes: a field
  the display wrote shows there, not at the boundary) and each field's RTTI type kind, so the comparison leaves
  references and unset record bytes aside. Engine level 15 writes them also when A's READY check finds that the
  loaded battles differ (a start, resync or rejoin; the `desync` message carries `why=ready`): both PCs stand at the
  loaded boundary, and the reports hold the loaded objects and the state, the evidence a two-PC report of "the loaded
  battles differ" lacked until then.
- **Failures.** B silent for 30 s at a minute boundary (5 minutes while busy), not loaded within 120 s, or
  disconnected: A hands side 1 to the AI and goes on (since engine level 8 the battle can be rejoined, see *Rejoin*).
  A lost: B leaves and loads slot 77 again. A watchdog recovers
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
  variables, and a battle can be dated in the next month. Since engine level 10 both PCs write the campaign's
  own Year, Month and Day back after every live load (`LoadCampaignGuarded` → `KeepCampaignDate`: the start, a resync,
  a rejoin, A's recovery) and after the host's resume of a live battle, decoded from the campaign's TDateTime
  (`CampaignDate`: the load builds it from the save's [General] date before the battle save is read, and nothing in a
  battle writes it; never from the battle clock, whose date is the battle's own). The clock stays as loaded. So both PCs
  run the battle with the campaign's month and day (the weather's season, the AI's day parity), as a battle that is
  never reloaded does, and the state hash covers the three. From 0.3.2, still in place: A records the campaign date
  when its battle opens (`Hold`; a rejoin's hold keeps its battle's record, the host's resume records the loaded
  save's date), every snapshot saves that date in the campaign file (`SaveSnapshot`), and the battle's results, the
  month and a battle slot's close, which runs no month, put it back; before 0.3.2 a resync made the drift permanent
  and could skip a month.
- **Rejoin (engine level 8).** A battle whose B was dropped (disconnected, silent, not loaded in time, the
  battles could not be kept in sync, a failed start or reload) goes on against the computer on A, and A remembers it
  (`RememberRejoin`: the battle id, the last epoch, B's name, the side names and the battle screen; forgotten when that
  screen closes, the battle ends, a new DoBattle runs or a new live battle is held). When B's player is back (joins the
  session again, also after restarting the game, and gets the nation back by the hello or `RematchReservations`; or
  the host's `ls rejoin`), the host checks that the player fights battles and plays live battles with the host's
  engine level (otherwise both are told that the computer keeps the side) and calls `lockstep::Rejoin`. From then on
  no new minute starts (the minute gate refuses `StartMinute`; Run is not started: "The battle waits: ... is
  rejoining it"). At the next quiet minute boundary (no minute running, a clean stack; at most 60 s, otherwise the
  computer keeps side 1) A holds the battle and runs the normal start (`StartAsA`): the snapshot into its slot, a new
  seed and the next epoch (the battle id stays), `rj=1` and `min=` in the start message. Both PCs load it, and the first
  load's shared setup gives side 1 back to its player on both (`SetSideControl(1, false, true)`; `ai1=` all zero; side
  1 not on auto-run; the game's own auto-run of side 0 since the drop becomes side 0's per-side auto-run; Fastest
  available as its button is). Then the READY hashes, `go`, and the clock and the orders work as after any start. B
  takes the start like any other, into its battle slot: it needs no month package, and after the battle it loads slot
  77 again and waits for the month as before (a previous live battle's discarded copy no longer makes it refuse). A B
  that still has its copy of the battle (dropped while silent) takes the start as a restart of that battle. A player
  who is back before A's drop has run (it waits for the end of a running minute) is taken back right after it. A
  failed rejoin (B doesn't load within 120 s, refuses, or is gone again) gives side 1 to the computer again, and the
  battle stays rejoinable. A battle between two joined players' nations: the host registers the relay again and asks
  the first player's PC (`rejoin`), which runs the same steps or answers `norejoin` with the reason, also in the
  returning player's words (`why2=`, for the host's notice to them). A's chat at the drop says what will happen: the
  player gets their ships back if they join again, or the computer commands the side to the end (the battle could not
  be remembered, or the player's PC plays live battles at another level). The leading PC's own crash: the host's
  battle is resumed from its save (below); a battle between two joined players is not covered.
- **Resume after the host's game restarted (phase 5b, engine level 9).** The host's game stopped (a crash, or it
  was closed) during its own live battle; B lost A and went back to slot 77. *The marker:* every snapshot A writes into
  the host's campaign slot (the start, a resync, Save, a rejoin; `SaveSnapshot`) also writes `rtw3mp_livebattle.ini`
  there: the battle id and last epoch, the opponent's nation, both player names, the side names, the carrier-strike
  rule, the agreed state the battle save drops (Fire at will per side, night CAP per force, the pending missile targets)
  with its minute, the battle slot mode (a test battle outside a month) and a hash of the battle save (`.sac`). The
  game's own *Save* outside a live battle (a battle that can be rejoined: single player after a drop, or a resumed
  battle after Run) makes the marker follow the new battle save (`RefreshMarkerAfterSave`, posted from the Save
  blocker). It is deleted when the live battle ends normally, when a battle that can be rejoined is over (its end, or
  another DoBattle) and when a new live battle is held; a closed battle screen keeps it (the game's exit closes it too),
  and a marker left behind can't match another battle save. The session's transfers leave `rtw3mp*` files out. *The
  resume:* the host loads the slot (if the game asks "Select autostart point", *Start from latest autosave point*), the
  game resumes the battle from its last save (`LoadScenarioInProgress` → `TfrmMain.Execute`; the strategic screen stays
  hidden) and gives side 1 to the computer. After the load `lockstep::OnCampaignLoaded` → `ResumeFromMarker`: a slot
  1-49 whose marker's hash equals the battle save's (else one log line and nothing else) makes the battle rejoinable as
  after a drop (the rejoin memo, `resumed`; `EnsureHooks`) and **held**: `MinuteGate` refuses `StartMinute` and the Run
  hook (`LS_StartRun`) releases the hold on the host's Run (or auto-run; a key that runs one minute, `FormKeyUp` →
  `StartMinute`, too): the battle goes on against the computer and stays rejoinable ("The battle goes on against the
  computer ..."). Fire at will per side comes from the marker; night CAP and the pending missile targets too when the
  save is from the marker's minute; a test battle gets its battle slot mode back. The host reads why the battle waits in
  the chat and the status line, and the Multiplayer window opens (without the focus, also outside a session). *The
  session:* a host in the lobby whose campaign slot shows such a battle (or a live battle whose player was dropped
  before the host hosted again; `MonthBattleOnScreen`) matches the players by their reservations and takes the month up
  (`AdoptMonthInProgress`: processing and the turn running, as `RunHostTurn` leaves them; `enemyVPSeen` as at the
  month's start; new battle ids above this one). The player of that nation already connected is offered the battle at
  once; one who joins later gets it from the hello, as after a drop (`HostOfferRejoin`; during a month `HostResumeTick`
  also offers a resumed battle once to a player who is connected already). Then the rejoin's own steps: at the next
  quiet boundary (at once while held) `StartAsA` with the memo, `rj=1`, `min=` and `hr=1` (if it was held: B's chat says
  that the host's game was restarted; nothing else reads it), both load, side 1 is the player's, READY, `go`. The
  battle's close runs the rest of the month (`DoStrategicTurn`, under the session's hooks: the AI off for the reserved
  nations, their battles deferred), and after `DoEndOfTurn` the battle phase, the diplomacy and the new month follow; a
  test battle's close runs no month and the session resends the current month ("The turn did not advance"). Without a
  session the battle and the rest of the month are the host's alone, as in 0.3.x. The resumed battle goes on with
  the loaded save's own campaign date, which is also A's record for the rejoin's snapshots (*The campaign date*, above).
- **The end.** Both players' *End game* (the game's own rule applies), or the game's own end of the battle. The
  game's questions inside a minute are answered so that the battle goes on ("... Do you want to end the scenario?":
  No; "... Do you want to continue searching?": Yes) and shown to both players as a proposal ("Press End on both
  sides to end the battle"); with both players' ships on auto-run "end the scenario?" is answered Yes. When the game
  ends the battle inside a minute, the battle screen's close is deferred to the end of that minute. The box that close
  shows (the campaign's `tmpBattleString`, written from side 0's point of view by the battle's
  `HandleBattleAftermath`) is rewritten on B at the end for B's side (its victory or defeat, the points swapped,
  without the host's prestige lines; display text only).
- **The game's texts.** Inside a minute the game's boxes and picture messages get the dialog policy's fixed answers on
  both PCs. Each line of their texts goes only to the player it is about (`GameTextFor`): a sighting box to the side
  of the division that sighted (the box's caller reads that division's side right after it), "Enemy ship ..." lines
  to side 0 (the simulation's point of view), other lines by the units they name (one side's divisions, ships or air
  formations), the battle as a whole to both, the rest to side 0 as before. A's chat shows its lines (`Battle: ...`);
  a break ("The battle paused (...)") names, for each player, the last text of that minute about their side, else "an
  event". The game's own battle log panel still lists every message on both PCs. The live battle's chat lines stay on
  each PC (the session's chat hook is local: on the host it no longer sends them to every player). The game's "Log
  lines out of sync!" box no longer follows a load: the snapshot writes multi-line log entries as one line, and every
  live load aligns the log's two lists.
- **Rules for human sides (engine level 2).** Some game routines treat "the player's side" (`PlayerSide`) differently
  from the computer's. Inside the simulation `PlayerSide` is the canonical `g_simPlayerSide` (0; 2 while side 0's
  ships are on auto-run), and a hook can run one routine with another value (`CallWithSimSide`, restored in
  `__finally`; the display swaps restore `g_simPlayerSide`, so nested hooked frames keep it). In a live battle: the
  computer's air strike routine runs, for each side, with that side when its player commands it (the single-player
  rule: the computer flies only that side's airfield strikes; the players launch their carriers' strikes, see *Air
  strikes*) and with 2 for a side the computer commands (auto-run, a dropped player), or with 2 for both sides when
  the host turned on *Computer flies carrier strikes in live battles* (the rule of engine levels 2 to 6); the
  automatic missile fire runs with 2 (no human side: the computer's rules for both sides; since engine level 9 each
  player's own *Fire at will* holds that side's divisions for the call, see *Missiles*); the 2-hourly
  air search re-plan is skipped while both sides are human (one side human: the game's rule with that side as
  PlayerSide); strikes never ask to confirm their target. *Fastest available* is clock state like the speed (A's
  releases, starts and `info` carry `fastest=`). The missile panel's counts are a display context, not updated on
  side 1's PC (the game counts side 1 as the enemy).
- **Auto-run per player (engine level 4).** The auto-run button asks with the game's own event dialog ("Hand your
  ships to the computer for the rest of the battle? <the other player> keeps commanding theirs.": *Yes* / *Yes, and
  order all forces to move to the closest friendly base* / *No*; while the player's side is on auto-run: "Take back
  command of your ships?") and records a deferred `autorun <side> on|home|off` (`ls autorun` without the question). At
  the release both PCs run the game handler's steps for that side only (`GetForceFlagDivision` of each force and
  `AIControlled` of its flag division; *home* first gives the handler's `ForceWithdraw` 6 and `ForceMission`
  tfmWithdraw to the side's forces that are not computer-controlled and to its 'Support Force's), and then command the
  side like the computer's own side: every force and division `AIControlled`, its ships not human-controlled inside
  the simulation (`SetSideControl`), and while side 0 is on auto-run the simulation's PlayerSide is 2; both PCs first
  record the side's forces' own `AIControlled` (`preAutoF`). *off* (engine level 12) gives the side back as single
  player's Auto-run off does: the forces get the computer control they had before auto-run, the flag division of every
  force that is not computer-controlled is the player's again, and the other divisions stay `AIControlled` (the player
  takes one back with its division report's *AI controlled*; up to level 11 every force and division became the
  player's, also those that started under the computer); its ships are human-controlled again; the force orders stay.
  The game's `AutoRunBattle` flag stays off (the per-side flags cover what it gates in the simulation; with it on, one
  PC's keys would stop the game's run timer). Auto-run per side is session state: the start and resync messages carry
  it (`auto=`, and the forces' control before it as `pa=`), `info` too (`a0`/`a1`), every load applies it again after
  the `ai1=` restore, and a snapshot keeps the sides on auto-run under their players (their forces as before
  auto-run), so the host's crash checkpoint gives the host their ships back. With one side on auto-run the other
  player keeps using the clock as before. With both, A runs the battle continuously at *Fastest available*, breaks
  don't stop it, and the dialog policy answers the game's "Do you want to end the scenario?" with Yes, so the game's
  own end conditions end the battle; a player taking back command pauses it after that minute and puts A's clock
  back. When the live battle ends on A (a drop, an abort), A's ships on auto-run are A's again (as an *off*). Manual
  torpedo and missile salvoes of a side on auto-run are refused, with one notice ("Your ships are on auto-run: the
  computer fires their torpedoes and missiles.").
- **Manual torpedo salvoes (engine level 5).** The division report's *Fire torpedoes* and the torpedo dialog's *Fire*
  work as in single player, between minutes only ("Pause the battle first: torpedoes are fired between minutes."). The
  dialog computes its firing solution on the player's PC (with that PC's own dice) and calls the game's fire routine
  `TfrmMain.FireMount`; its hook doesn't run the routine there but records a deferred `torp` with the ship, the mount's
  index in the ship's mount list, the target, the course (the `Single`'s bits), speed, range and `Manual`, "this
  boundary only", and says "Torpedo salvo ordered; it fires when the next minute starts." A second order for the same
  mount before that release is refused. At the release both PCs check that the ship (afloat, the player's), the mount
  (torpedoes left) and the target (afloat) are still there and call `FireMount` with exactly these arguments inside the
  shared section (its `Random` draws and `SetReloadTime`'s are shared dice), then set the ship's `FiredMountThisTurn`
  as the dialog does; the player who gave it reads "Torpedo salvo fired: <ship> launched N torpedoes." Both players
  firing at the same boundary need no resync. Where the routine returns without firing (no torpedoes left; submerged
  tubes above 25 knots unless the ship is an MTB) the hook lets it run on the player's PC, so the game's own message
  appears; nothing changes there. The AI's own fire (inside the simulation) passes through the hook. When a minute
  starts the torpedo dialog is hidden on both PCs (its solutions are for the paused boundary), as before every reload.
  The *Fire torpedoes* button itself is enabled by the game only with the battle preference *Realism* above its second
  level, which both PCs take from A.
- **Routine air operations and land-based CAP (engine level 6).** *Routine air ops* (`sbRoutineAirOps`, "Manage CAP
  and air recon") and the ship tree's *Search pattern* open the game's modal `TdlgSearch` as in single player, for the
  player's own forces: `TdlgSearch.Execute` is hooked and refuses a force of the other side (the battle screen opens it
  for a selected map flag, which can be the enemy's) with "That force is <player>'s: you can set the routine air
  operations of your own forces only." Its OK (the game's commit) writes the force's nine settings (CAP level, night
  CAP, search left/right limits, distance, interval, two-phase, repeat, night search) and the sectors
  (`SearchPatternOrdered`, 19 bytes by the layout): all ten are force order fields of the guard, so the edit becomes an
  order batch and both PCs apply it at the next release, like a course or speed. `SearchPatternCurrent` (the
  simulation's count-downs per sector) is not an order; it is hashed (group `air`) and in the leak check. Every minute
  the game launches both sides' CAP and scouts from these settings (`CheckSearchAircraft`, the CAP upkeep); the
  2-hourly re-plan stays skipped while both sides are human. `CheckSearchAircraft` runs with the simulation PlayerSide
  2: the game's "Do you want <ship> to launch scout aircraft?" (PlayerSide's carriers that see enemy ships, unless the
  *Floatplane launch* preference is Automatic) would get the fixed answer No for side 0 only; now neither side is asked
  and both launch as their settings say. The floatplane question (`LaunchFloatCheck`, both sides alike) keeps its
  fixed answer, and the in-minute questions of the air search go to the log, not to side 0's chat (they name one
  side's ship). The battle save drops `NightCAP` (a load turns it on for every force after 1930): the start and resync
  messages carry the agreed values (`ncap=`, one digit per force) and both PCs put them back after the load, like
  `ai1=`. `TForce.ForceFlagship` needs nothing: `TdlgSearch.Execute` only repeats `GetForceFlagship`'s own cache write,
  and the simulation's one direct read comes after `CheckObjectives` has rewritten every force's cache in the minute.
  *Land-based CAP:* the division report's land-CAP button works as in single player. The request lives in the global
  `LandCAPDivision[side]`; the game's dialog reads and writes slot 0 (its human side), so on the PC that commands side 1
  the two slots are swapped while that PC's display state is in place (`ApplyViewSide`, the moments PlayerSide is
  swapped); the simulation, the hash, the battle save (`CanonBegin`) and the order guard (two pseudo-objects
  `Llandcap#0`/`#1` with one division-reference field, side = slot, always at the game's layout through `LandCapSlot`)
  see the canonical layout. The game's `CheckLandBasedCAP(side)` tasks the nearest airfield with fighters of either
  side; in a live battle its hook runs the mod's copy of the routine with the airfield's side checked, inside the
  simulation on both PCs (the routine's code is read at startup step by step; anything unexpected keeps the button
  blocked). Outside live battles the game's routines run unchanged.
- **Air strikes (engine level 7).** *Order an airstrike* and the reports list's *Launch strike* open the game's modeless
  strike dialog (`TdlgStrike`) as in single player; it lists the player's own side's squadrons (the display
  PlayerSide). The button's own `PauseGame := 1` (which would stop only this PC's run) becomes the mod's pause request.
  *Launch*, *Ready* and *Stand down* work between minutes only, once the dialog's list was filled after the last minute
  ("Pause the battle first: aircraft are launched, readied and stood down between minutes."). Both launch and ready go
  through the game's `LaunchSomeAircraft` (unnamed), hooked as a simulation context: called from the screen it is an
  *immediate action*, run at once on the player's PC on the agreed state (its frame's entry turns earlier edits into a
  batch and puts the agreed values back) and recorded with its arguments (the source formation by key, number,
  mission, target, the new formation's name, ordered state, loadout, spot flag, its AFId, the ScoutCount it used); the
  other PC replays it first thing in the release's shared section. The new formation gets a **side-partitioned AFId**
  (side 0: 1,000,000 + k, side 1: 1,100,000 + k; the game's counter steps back, so the simulation's own formations
  keep the ids they would have) **before** the frame's exit takes the agreed state, so everything the dialog writes
  afterwards
  (`GroupIndex` from its local `Random`, spotting, `PriorityTarget`, coordination: `LeadAirFormation`, the shared
  `GroupIndex`, `CombinedStrikeSpeed`, aligned `StatusCount`; the source's `Mission`/`OrderedState`/`Loadout`), Stand
  down (`Mission`, `OrderedState`, `Status`, `StatusCount`, and the carrier's `AirTorpedoes`/`AirToSurfaceMissiles` it
  returns), the popups (mission, loadout, number) and the list's own number writes are ordinary orders of the guard
  (object kind `A`, key `A<AFId>#0`, side = the formation's `Side`). The replay copies the recorded name (no
  ScoutCount there; the counter moves by the recorded count), so names and both counters stay equal. Both players
  can launch at the same boundary without a resync: their ids and keys never meet, and at the end of the release both
  PCs sort the formations launched since the minute's end (the tail of the air list) by `AFId`. *Confirm target
  location when spotted* is cleared on every launch from the screen (the check box's value never becomes an order):
  re-targeting during the flight stays unavailable. The dialog holds raw formation pointers: it is refilled by its
  own `FillSquadronList` right after every minute (outside the simulation) and hidden before every reload. The
  computer flies only the airfields' strikes of a human side (single-player rule); the host's option *Computer flies
  carrier strikes in live battles* (off by default; `autostrike=` in the start message, so both PCs use A's value)
  hands both sides' carrier strikes to the computer, as before engine level 7, and the strike dialog still launches
  the players' own strikes besides (the first start's chat and the dialog's first opening say so). Without every piece
  of the routine or the dialog found at startup, the buttons keep their blocker and the computer flies both sides'
  strikes.
- **Missiles (engine level 9).** *Fire at will* on the missile panel is each player's own order for their own side (on
  at the start). The automatic fire (`CheckMissileFiringForAllShips`) still runs with the simulation PlayerSide 2
  (both sides under the computer's rules); while a side's agreed *Fire at will* is off, that side's divisions are held
  for the call: `TDivision.HoldMissileFire` := 1 on those not holding already, put back right after it (`__finally`).
  The routine skips a held division at the same jump where it skips the player's divisions while the box is unchecked,
  and nothing else inside the call reads `HoldMissileFire` (static scan, see
  [GAME-INTERNALS.md](GAME-INTERNALS.md#tactical-battles)); the game's exception, a computer-controlled force of a
  nation below the routine's threshold, fires anyway here too, and a side the computer commands (auto-run, a dropped
  player) is never held. The two settings are synced order state: a pseudo-object per side (kind `M`, keys
  `Mfireatwill#0`/`#1`, side = slot) that only its side's PC edits, through the box's click handler
  (`cbShowBaseFlagsClick`, hooked); the battle save and the form don't keep them, so the start and resync messages carry
  them (`faw=`, two digits; a new live battle starts with both on, a rejoin keeps them). The box shows this PC's side's
  setting; the simulation never reads it (during the routine's call it holds a canonical value, unchecked, on both PCs).
  A click while a minute runs is this player's order for the minute after the next boundary, like a course; a click
  while the side is on auto-run is kept for when the player takes the ships back.
  *Fire mode* stays disabled for both players (showing *Ripple*, a new battle form's default), with a hint: the game
  applies it to PlayerSide's ships only (the salvo routine 0xda7ccc), and the live automatic fire runs with PlayerSide
  2, so the computer sets the salvo size of both sides alike; *Manual fire* chooses one, half or all (honouring one
  player's *Fire mode* would give that player a control the other lacks; a per-side one would be a new simulation
  feature). *Manual fire* (`sbFireSalvoClick`) opens the game's modal missile
  dialog (`TdlgMissileFire`) between minutes only; while the clock runs it makes a pause request instead of the
  handler's `PauseGame := 1` ("Pausing the battle: open Manual fire again when it is paused"). While the dialog is open,
  A releases no minute and B reports busy (the rules for modal dialogs). *Fire one*, *half* and *all* run the dialog's
  own `FireMissiles`; each `TfrmMain.FireMissileMount` call it makes is hooked: on the player's PC the routine doesn't
  run; the hook records a deferred `msl` (the ship, the launcher's index in its mount list, the target ship or `-`, the
  target location, the bearing, the number asked and the predicted count; this boundary only) and returns the count the
  routine would fire (its own preconditions: electrics, the launcher not destroyed, missiles left less this player's
  earlier orders on that launcher, the launcher bears or the ship fires at any bearing, beyond the minimum and within
  the maximum range; then min(launchers, number)), so the dialog's *one* and *half* stop rules and its summary box
  behave as in single player; the chat says "Missile salvo ordered: N missiles from <ships>; they fire when the next
  minute starts." At the release both PCs check the ship, its side, auto-run, the launcher, the target and the routine's
  preconditions again and call `FireMissileMount` with the recorded arguments inside the shared section
  (`SetReloadTime`'s draws are shared dice); the player who gave it reads "Missile salvo fired: <ship> <n>, ... (N
  missiles)." or "Not carried out: ...". A launcher that can't bear gets the dialog's `PendingMissileTarget` (the ship's
  12-byte target location, which the simulation's `FireAllGuns` fires and clears in the ship's next gun step, normally
  within the next minute): an order field of the guard, hashed (its
  first 8 bytes: the game's own clearing leaves the third dword undefined) and carried over a resync (`pmt=`; the battle
  save doesn't keep it). Missile submarines can't fire salvoes in a live battle (the dialog's branch fires them for side
  0 with nation 0's figures and sets the submarine's `Downtime` on one PC): the fire buttons uncheck them first ("they
  were left out"), and the game's `FireSSM` called from the screen is refused with a chat line; the computer's own
  submarine fire runs as before, and the submarines (`Downtime`, `Location`, `TorpsFired`) are hashed (group `sub`).
  Cosmetic: the dialog shows the stocks before the release and plays the launch sound when the order is given. Each part
  degrades on its own: without every piece found at startup (the gate's jump for *Fire at will*; the routines' heads and
  the dialog's code for *Manual fire*) that part stays as before engine level 9 (*Fire at will* disabled, *Manual
  fire* blocked).
- **Not available in live battles:** missile submarines' salvoes from the missile dialog (the computer still fires
  them); *Fire mode*; adjusting a strike's target during its flight.
  The game's hidden developer items (try for home, enter port, sink, teleport, ...) and handlers that no control
  calls are blocked too; they are not player features (the game hides them in every battle). Emergency activation
  needs no blocker: its button is never shown and its click handler is empty.
- **Test commands:** `ls forces [div]`, `ls chforce`, `ls lead`, `ls turnaway`, `ls pending`, `ls save`, `ls autorun`,
  `ls torp`, `ls airops`, `ls landcap`, `ls airlaunch`, `ls fireatwill` and `ls missiles` go through the same code as
  the hooks (`ls torp` and `ls missiles` through the dialogs themselves; `ls airops` and `ls landcap` make the dialogs'
  own writes; `ls airlaunch` calls the launch routine as the strike dialog does; `ls fireatwill` sets the box and calls
  its click handler); `ls mounts` shows a ship's torpedo mounts, `ls airforce` a force's air settings and the land-CAP
  requests, `ls airlist` the air formations, `ls strike` the strike dialog (`ls strikedlg` opens it), `ls missilestat`
  the missile ships, stocks, pending targets, missiles in the air and *Fire at will* per side, `ls rejoinable` the
  dropped battle A remembers for a rejoin; the host's `ls rejoin [nation]` gives it back to that nation's connected
  player at once (also without a reconnect); `ls resumable` a battle resumed from the host's save and the slot's
  marker, `ls resume run` the host's Run on a held resumed battle (without a run), `ls autostart 1|2` the answer to the
  game's next "Select autostart point", and `loadasync <slot>` a `load` that doesn't wait (see
  [TESTING.md](TESTING.md)).
- Details: [feature](../MODLOG.md#feature-2026-10-15-live-player-vs-player-battles-in-lockstep-v030),
  [deferred actions](../MODLOG.md#feature-2026-10-17-deferred-actions-force-orders-and-battle-checkpoints-in-live-battles-v040),
  [auto-run](../MODLOG.md#feature-2026-10-17-auto-run-per-player-in-live-battles-v040),
  [torpedo salvoes](../MODLOG.md#feature-2026-10-17-manual-torpedo-salvoes-in-live-battles-v040),
  [routine air operations and land-based CAP](../MODLOG.md#feature-2026-10-17-routine-air-operations-and-land-based-cap-in-live-battles-v050),
  [air strikes](../MODLOG.md#feature-2026-10-17-air-strikes-in-live-battles-v050),
  [rejoin](../MODLOG.md#feature-2026-10-17-rejoining-a-live-battle-v050),
  [missiles](../MODLOG.md#feature-2026-10-17-missiles-in-live-battles-v060),
  [resume after the host's restart](../MODLOG.md#feature-2026-10-17-resuming-a-live-battle-after-the-hosts-game-restarts-v060),
  [stress tests](../MODLOG.md#testing-2026-10-15-live-pvp-battles-lockstep-stress-v030),
  [release candidate](../MODLOG.md#testing-2026-10-15-release-candidate-v030),
  [the limits of live battles](../MODLOG.md#investigation-2026-10-16-the-limits-of-live-battles-static-v031),
  [the 0.3.2 fixes](../MODLOG.md#fix-2026-10-17-speed-controls-air-formations-window-and-battle-dialogs-in-live-battles-v032),
  [the campaign date](../MODLOG.md#fix-2026-10-17-live-battles-on-the-campaigns-own-date-engine-level-10-v060),
  [the 0.3.3 fixes](../MODLOG.md#fix-2026-10-17-air-units-doubled-with-every-reload-two-live-battle-desyncs-v033),
  [the pre-release fixes](../MODLOG.md#fix-2026-10-17-minor-live-battle-fixes-before-release-v060),
  [side 0's computer control and the DESYNC reports](../MODLOG.md#fix-2026-10-17-the-branch-regressions-three-findings-engine-level-13-v060),
  [dawn and dusk](../MODLOG.md#fix-2026-10-17-dawn-and-dusk-desyncs-lightstate-engine-level-14-v060),
  and the battle facts in [GAME-INTERNALS.md](GAME-INTERNALS.md#tactical-battles).

## Test bridge
- A line-based command socket on 127.0.0.1, only when `RTW3MP_BRIDGE_PORT` is set when the game starts; with
  `RTW3MP_BRIDGE_TOKEN` the first line must be the token.
- Commands run on the main thread and reply `ok ...` or `error ...`. Commands that can open a modal game dialog are
  asynchronous: they reply `ok` at once and log their result.
- The command reference, the runner and the scenario catalog are in [TESTING.md](TESTING.md).

## Versions and protocol
- `MP_MOD_VERSION` and `MP_PROTOCOL` in `src/mod/common.h`; both are in the log's first line (`RTW3MP 0.4.1
  starting (protocol 3)`).
- The host refuses a client with another protocol or another game build. Protocol history: 1 = 0.1.0, 2 =
  0.2.0-0.2.8, 3 = 0.3.0-0.3.3, 0.4.0, 0.4.1, 0.5.0 and 0.6.0 (relations v2 and 0.6.0 keep 3: their new keys are
  ignored by older peers).
  Within a protocol, versions can play together, but features may need both
  sides (see [CHANGELOG.md](../CHANGELOG.md)). The hello's mod version (`mod=`) is not checked; the notices about
  another live-battle level quote it.
- Live battles have their own version, the live-battle engine level `MP_LS_LEVEL` (`src/mod/common.h`): 1 = 0.3.0 to
  0.3.3, 14 = 0.4.0, 17 = 0.4.1, 0.5.0 and 0.6.0. Levels 2 to 13 were development levels of 0.4.0 (the live-battle
  limits branch, whose MODLOG entries say "v0.4.0", "v0.5.0" and "v0.6.0" after the release plan of 2026-10-16, but
  it shipped as one version, 0.4.0), and levels 15 and 16 were rounds of 0.4.1's two-PC fixes; none of them was
  released. A client announces its level in its hello (`ls=<level>`), and the host pairs a live battle
  only when the player's level (both players' levels for a relayed battle) equals its own. Otherwise the battle falls
  back as when a player has live battles off, and the player gets a notice. A's start message carries `lv=` (B
  refuses another level) and B's `ready` carries it too (A drops an older B), for hosts that don't check it: a 0.3.x
  host's own battle against a level-14 player is refused by that player's PC, which never loads it, and the host's
  computer fights the player's side; the host relays a battle between two level-14 players, who fight it live (the
  mixed-version checks of 2026-10-18, cases d and b), but it has no rejoin: A's drop chat then promises a return that
  can't happen. A dropped player who is back with another level doesn't get the battle back (a
  notice). Resuming the host's live battle after its game restarted (phase 5b) kept level 9: it uses level 8's rejoin
  start, and its only new key (`hr=1`) changes the returning player's chat line. Level 10 (after 0.3.2 was merged
  into the branch) keeps the campaign's own date after every live load and hashes it: a level-9 PC would run the
  battle with the battle save's day and month. Level 11 (stage B's fix) gives the simulation the same ship
  identification on both PCs after every live load (the battle save keeps none; before, each PC kept what its own
  display drew during the load, and the sighting reports' shared random draws then diverged now and then). Level 12
  (the pre-release fixes): Auto-run off gives a side back as single player does (the forces' control before auto-run
  travels as `pa=`), and a torpedo salvo of a side on auto-run is refused at the release; a level-11 PC would give
  every division back and fire it. The notices about another level name both levels in player language, and the mod
  versions when they differ. Level 13 carries side 0's computer control over every load (`ai0=`). Level 14 keeps each
  ship's `LightState` in the display's dual set and in the state hash: the map's sighting circles wrote it between
  minutes, StartMinute's division routine reads it, so at dawn and dusk the two PCs could part (the recurring
  `lockstep_ui` e) DESYNCs). Level 15 (0.4.1, from a report of two players on two PCs): the start, resync and rejoin
  carry only the 14 preferences the simulation uses (a level-14 PC would also take A's display ones and miss
  `AIControl` and `BSSMHC`), B loads with A's x87 control word, and each PC's map shows its own side (B's own ships,
  A's only while sighted, no flags of the other side, B's ensign, B's screen transparent while it loads); a READY
  mismatch writes both PCs' DESYNC reports. A 0.4.0 PC (level 14) gets
  no live battle with a 0.4.1 PC: a host of either version fights its battle against a player of the other against the
  computer ("fought against the AI (live-battle engine level 14, the host's 17)", the player's chat names both levels;
  B's own check of the start's `lv=` would refuse it too), and a battle between two joined players of different
  levels is resolved by the game. Level 16 (0.4.1, CPUs and data files): from the load on, both PCs compute the game's
  x87 transcendental routines (Sin, Cos, ArcTan, Exp, Ln, Tan, SinCos, ArcTan2, ArcSin) in software (`x87.cpp`,
  `softmath.h`: the same bits on every CPU; the start's `sm=1`), check A's x87 control word around every step and
  hash, and compare the game data files (`datafiles.cpp`: `Data` but its Graphics, hashed when a session starts; the
  start's `dh=`; B refuses a battle whose files differ and sends its list in the `abort` (`dm=`), A names the files to
  both players (`datadiff`) and fights the battle against the computer). B's `ready` carries the READY hash's parts
  (`g=`), its CPU vendor (`cpu=`) and own control word (`cw=`); on a mismatch A's `desync` carries `diag=`, the few words
  of the chat line and the notices on both PCs ("the loaded battles differ: orders; CPUs AMD (Admiral), Intel
  (Captain)"). The hello and the welcome carry `cpu=` and `dh=` (logged by both sides). Level 17 (0.4.1 as released,
  the strike battles' fix): A's start carries the campaign's ship-type flags (`st=`), which B uses for the battle and
  which are in the state hash, and the software maths reproduces the x87's own results (see *CPUs, the x87 control
  word and the data files* above); a level-16 PC gets no live battle with a level-17 PC.
- Releases are tagged `vX.Y.Z`; the release zip is built by `tools/package_release.ps1` (see the docs agent's
  release checklist in `.github/agents/rtw3-docs.agent.md`).

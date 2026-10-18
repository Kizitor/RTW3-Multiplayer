# MODLOG — Rule the Waves 3 multiplayer mod

The engineering log of the mod, oldest entry first: game facts and reverse engineering (addresses, offsets), design
decisions, fixes, test results and gotchas, as they were found. It is append-only in spirit: entries are not
rewritten later, and a fact that turned out wrong gets a dated correction note next to it.

- Entry headings: `## <Kind> <date>: <title> (vX.Y.Z)`, with Kind = Feature, Fix, Testing, Investigation or Docs. The
  version is the one the work went into (0.1.0 = the development version before the first release).
- The current design is in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), the game facts to rely on in
  [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md), and the user-facing changes in [CHANGELOG.md](CHANGELOG.md).
- Testing: [docs/TESTING.md](docs/TESTING.md). Older entries point to the regression agent file for the scenario
  catalog and the bridge commands; both moved to docs/TESTING.md on 2026-10-16.
- Addresses: RVAs are marked as such; other addresses are in the analysis image with base 0x830000 (see
  [how to read the addresses](docs/GAME-INTERNALS.md#how-to-read-the-addresses)). Short hashes such as e674bf5,
  c06860f, a1a8484 and ebfca72 are development builds of 0.3.0 that were squashed into the release commit f6ee827.

## Index
By version (the version the work went into) and kind, oldest first.

- **0.1.0** (development version, never released)
  - Feature:
    [multiplayer design v1, historical](#feature-2026-10-04-multiplayer-design-v1-historical-v010);
    [implementation status, historical](#feature-2026-10-04-implementation-status-historical-v010)
  - Fix:
    [nation selection in the lobby](#fix-2026-10-04-nation-selection-in-the-lobby-v010);
    [window close, Start session, Give to AI (user report from a second PC)](#fix-2026-10-04-window-close-start-session-give-to-ai-user-report-from-a-second-pc-v010)
  - Investigation:
    [game facts](#investigation-2026-10-04-game-facts-v010);
    [Delphi RTTI, a huge win](#investigation-2026-10-04-delphi-rtti-a-huge-win-v010);
    [route](#investigation-2026-10-04-route-v010);
    [save format of 1.01.44](#investigation-2026-10-04-save-format-of-10144-v010);
    [live experiments](#investigation-2026-10-04-live-experiments-v010);
    [turn pipeline of 1.01.44](#investigation-2026-10-04-turn-pipeline-of-10144-v010);
    [gotchas, each cost a debugging round](#investigation-2026-10-04-gotchas-each-cost-a-debugging-round-v010)
  - Testing:
    [verified in the real game](#testing-2026-10-04-verified-in-the-real-game-v010)
  - Docs:
    [host walkthrough video (docs\hosting.mp4 + 4 stills in the README)](#docs-2026-10-04-host-walkthrough-video-docshostingmp4--4-stills-in-the-readme-v010)
- **0.2.0** (first public release)
  - Feature:
    [diplomacy between players, protocol 2](#feature-2026-10-04-diplomacy-between-players-protocol-2-v020)
- **0.2.1**
  - Fix:
    [joined players' design studies never advanced](#fix-2026-10-05-joined-players-design-studies-never-advanced-v021)
- **0.2.2**
  - Fix:
    [intel reports doubled every month -> "no memory" and "protocol error"](#fix-2026-10-05-intel-reports-doubled-every-month---no-memory-and-protocol-error-v022)
  - Testing:
    [intel-report regression coverage](#testing-2026-10-05-intel-report-regression-coverage-v022)
- **0.2.3**
  - Fix:
    [joined players' doctrine/training never took effect](#fix-2026-10-05-joined-players-doctrinetraining-never-took-effect-v023);
    [a player's turn could be dropped when a save file was briefly locked](#fix-2026-10-05-a-players-turn-could-be-dropped-when-a-save-file-was-briefly-locked-v023)
  - Testing:
    [doctrine regression coverage](#testing-2026-10-05-doctrine-regression-coverage-v023);
    [file-lock regression coverage](#testing-2026-10-05-file-lock-regression-coverage-v023)
- **0.2.4**
  - Feature:
    [random events for joined players](#feature-2026-10-06-random-events-for-joined-players-v024)
  - Testing:
    [joined players' random events](#testing-2026-10-06-joined-players-random-events-v024)
- **0.2.5**
  - Feature:
    [peace terms for wars between players](#feature-2026-10-06-peace-terms-for-wars-between-players-v025)
  - Testing:
    [peace treaties between players](#testing-2026-10-12-peace-treaties-between-players-v025)
- **0.2.6**
  - Fix:
    [joined players' victory points never rose against the host](#fix-2026-10-12-joined-players-victory-points-never-rose-against-the-host-v026)
  - Testing:
    [victory points of joined players](#testing-2026-10-12-victory-points-of-joined-players-v026)
- **0.2.7**
  - Fix:
    [a joined player's "Copy game" played the host's nation](#fix-2026-10-13-a-joined-players-copy-game-played-the-hosts-nation-v027)
  - Testing:
    [joined players' Copy game](#testing-2026-10-14-joined-players-copy-game-v027)
- **0.2.8**
  - Fix:
    [a joined player's Research screen showed and edited the host's research](#fix-2026-10-14-a-joined-players-research-screen-showed-and-edited-the-hosts-research-v028)
  - Testing:
    [joined players' research screen](#testing-2026-10-14-joined-players-research-screen-v028)
- **0.3.0**
  - Feature:
    [joined players fight their battles against AI nations on their own PC](#feature-2026-10-15-joined-players-fight-their-battles-against-ai-nations-on-their-own-pc-v030);
    [live player-vs-player battles in lockstep](#feature-2026-10-15-live-player-vs-player-battles-in-lockstep-v030)
  - Fix:
    [the game's AI research choices reset joined players' research priorities](#fix-2026-10-15-the-games-ai-research-choices-reset-joined-players-research-priorities-v030)
  - Investigation:
    [tactical battles (joined players vs AI, live-lockstep PvP)](#investigation-2026-10-14-tactical-battles-joined-players-vs-ai-live-lockstep-pvp-v030)
  - Testing:
    [joined players' battles vs AI](#testing-2026-10-15-joined-players-battles-vs-ai-v030);
    [live PvP battles (lockstep) stress](#testing-2026-10-15-live-pvp-battles-lockstep-stress-v030);
    [release candidate](#testing-2026-10-15-release-candidate-v030);
    [war score of live battles between joined players](#testing-2026-10-16-war-score-of-live-battles-between-joined-players-v030)
  - Docs:
    [documentation restructured](#docs-2026-10-16-documentation-restructured-v030)
- **0.3.1**
  - Fix:
    [live battles between joined players didn't count in their war score](#fix-2026-10-16-live-battles-between-joined-players-didnt-count-in-their-war-score-v031)
  - Investigation:
    [the limits of live battles (static analysis of 0.3.1)](#investigation-2026-10-16-the-limits-of-live-battles-static-v031)
- **0.3.2**
  - Fix:
    [speed controls, air formations window and battle dialogs in live battles](#fix-2026-10-17-speed-controls-air-formations-window-and-battle-dialogs-in-live-battles-v032)
  - Testing:
    [0.3.2 hotfix (speed controls, air window, dialogs before reloads)](#testing-2026-10-17-032-hotfix-speed-controls-air-window-dialogs-before-reloads-v032)
- **0.3.3**
  - Fix:
    [air units doubled with every reload; two live-battle desyncs](#fix-2026-10-17-air-units-doubled-with-every-reload-two-live-battle-desyncs-v033)

## Investigation 2026-10-04: game facts (v0.1.0)
- Game: Rule the Waves 3, Steam app 2008100, install `F:\SteamLibrary\steamapps\common\Rule the Waves 3`.
- Version: 1.01.44 (Launcher.exe.config `GameVersion`, window caption "Rule the Waves III - Version 1.01.44").
- `RTW3.exe`: native x86 Delphi (Embarcadero, VCL), 11.2 MB. ASLR on (seen at base 0x830000, preferred 0x400000).
- Steam DRM stub (`.bind` section, entry point inside it). `.text` is encrypted on disk (entropy 8.0).
  Rule: never strip/patch the stub. Analysis uses a read-only snapshot of the running process
  (kept outside this repo). The mod hooks at runtime after the stub has run.
- `Launcher.exe`: .NET 4.7.2 Matrix/Slitherine generic launcher; Steam starts it, it starts RTW3.exe.
  Running RTW3.exe directly without Steam env makes the stub relaunch via Steam (=Launcher).
  Direct launch for testing: env `SteamAppId=2008100` + `SteamGameId=2008100`, cwd = install dir.
- No anti-cheat. Single-player only game (no built-in MP).
- Imports from game dir candidates for proxy: winmm.dll, version.dll (both system DLLs imported by RTW3.exe).
- Saves: `<Documents>\My Games\Rule the Waves 3\Save\Game<N>` (slot dirs).
  Back up `<Documents>\My Games\Rule the Waves 3` before testing (the test scripts back up and restore slot 1).

## Investigation 2026-10-04: Delphi RTTI, a huge win (v0.1.0)
- 797 classes recovered from VMTs (vmtSelfPtr -88, name -56, size -52, parent -48, methods -64, fields -68).
- Extended RTTI present: field names/offsets/types for private fields, public method names+addresses+signatures.
  Tools: `rtw3-decomp\delphi_rtti.py`, `rtti_full.py` -> `rtw3-decomp\rtti\<Class>.txt`.
- Forms (DFM in .rsrc, unencrypted): start screen `TfrmSelectNation2` (pnStart: lbTNewGame, lbTLoadGame,
  lbTContinue, lbTAbout, lbTExit labels over a title image); strategic screen `TfrmBuildCamp`
  (btbnTurn "Turn", btbnSave, btbnResign, ...); tactical battle `TfrmMain` (tmrRunMinute, sbPlay/sbRun5/sbRun/sbPause, cbGameSpeed).
- `TfrmBuildCamp` fields: BuilderNations: TList @+0xae4, MapAreas @+0xae8, CampaignDivisions @+0xaf0,
  PlayerNation: TBuilderNation @+0x3204, QuickBattle @+0x4ac8, TurnMessages @+0x3208.
- `TBuilderNation` (48128 bytes): NationName @+4, ShipList @+0x109, SubmarineList @+0x111, OfficerList @+0x115,
  DesignList @+0x119, Funds @+0x1b5, NationNumber @+0x852, Prestige @+0x198 ...; methods SaveMe/LoadMe(TStringList,...).
- Key TfrmBuildCamp methods (RVA in 1.01.44; resolve by name at runtime instead):
  btbnTurnClick 0x527934, DoTurn 0x4b09f4, DoTurn2 0x4b1644, DoStrategicTurn 0x4b1938, DoEndOfTurn 0x4ccd90,
  PlayerChecksBeforeTurn 0x4b28c4, SaveBuildCampaign(ScenarioInProgress, Autosave, BeforeBattle) 0x45e5c4,
  LoadBuildCampaign(aFileName) 0x4698c4, AIMonthlyBuild(aNation) 0x47a34c, DoBattle 0x483b24,
  DoBattleBetweenAINations 0x502104, CheckAIvsAIBattles 0x502a50, GetSaveDirectory(aGameIdx) 0x51c0d0.

## Investigation 2026-10-04: route (v0.1.0)
Native: proxy DLL (system DLL name loaded from the game dir) -> loads `RTW3MP.dll` -> resolves targets by
Delphi RTTI name at runtime (no hard-coded offsets) -> inline hooks. Win32 UI for the MP lobby. TCP over
Hamachi/any LAN VPN. Host authoritative.

## Investigation 2026-10-04: save format of 1.01.44 (v0.1.0)
- Slot dir `Save\Game<slot>\`: `RTWGame<slot>.bcs` (INI, UTF-8 BOM, CRLF, ~770 KB), `RTWGame<slot>.off` (player officers),
  `RTWGame<slot>.sta`, `MapData<slot>.dat` (INI: map areas, possessions with Owner by name, BaseValue, BuildingBase),
  `DesignFiles0..8.des` (per nation designs, positional text).
- .bcs sections: [General] (Year, Month, IDNo = global id counter, War, EnemyVP... player-centric),
  [NationN], [NationNShips], [NationNCoastalArtillery], [NationNSubmarines], [NationNLosses] for N=0..9,
  [IntelReports], [AircraftTypes], [AirUnits], [EUT], [RAD], [VERSION].
- Nation index 0 is always the player; 1..8 AI opponents; 9 = non-participating (China in a GB 1900 game).
  `LoadBuildCampaign` sets `PlayerNation := BuilderNations[0]`. Diplomacy is player-centric (`Tension` with player,
  `AITension1..8`/`AIAlliance1..8` between AIs).
- Ship ids are global (from IDNo); ships reference areas by name (LocationAreaName/OrderedAreaName/DestinationAreaName).

## Investigation 2026-10-04: live experiments (v0.1.0)
- Writing `PlayerNation` (+0x3204) to BuilderNations[1] and refreshing the ship tab shows Germany's fleet:
  the strategic UI follows the pointer. => clients can play their nation on a copy of the host state.
- Posted WM_LBUTTONDOWN/UP to VCL panels and BM_CLICK to buttons drive the UI without touching the user's input.
- frmBuildCamp global var at RVA 0x723000 (1.01.44, dev only; the mod finds the instance by HWND/hook capture).

## Investigation 2026-10-04: turn pipeline of 1.01.44 (v0.1.0)
btbnTurnClick -> (StartBuild 0/1 initial phases with AIBuildInitialFleet/AIMonthlyBuild for 1..8) -> DoTurn2:
PlayerChecksBeforeTurn, SaveBuildCampaign(autosave), officers, AI strategic moves (unnamed, RVA 0x4c024c,
writes ship OrderedArea/LocationArea/Status for nations>0), StopBlockadedMoves, AreaMoveShips (executes orders for all),
CheckForBattles2 (true => battle, strategic turn continues later) -> DoStrategicTurn (... ReduceTimeLimits calls
AIMonthlyBuild ..., ComputeExpenses: every nation Funds += GetMonthlyBalance, CheckTurnEvents, DoResearch per nation,
CheckAIvsAIBattles, raiders) -> DoEndOfTurn (CheckForWar, AdvanceDesignStudies, UI).

## Feature 2026-10-04: multiplayer design v1, historical (v0.1.0)
> Historical: the design as of 0.1.0. The current design is in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
> Since 0.3.0 joined players fight their own battles, so the last line (battles) no longer holds.
Host authoritative, simultaneous planning, per-nation merge:
1. Host plays nation 0 in its normal slot. Clients claim AI nations 1..8.
2. Each month host saves, sends the slot files to every client (IDNo offset per client so new ids never collide).
3. Client loads into its own MP slot (Game77+), sets PlayerNation to its nation, plans, presses Turn => client saves,
   sends [NationK*] sections + DesignFilesK.des + its MapData + IDNo.
4. Host presses Turn (or timer expires) => host saves, merges each submission, reloads, runs the real turn with
   AI decision routines suppressed for human nations (hooks) and AI moves undone for their ships.
5. After DoEndOfTurn the host saves and broadcasts the new month.
Battles: fought on the host (host's nation); client nations' wars resolve via the game's AI-vs-AI logic (v1 limit).

## Feature 2026-10-04: implementation status, historical (v0.1.0)
> Historical: the state of 0.1.0. The current implementation is described in
> [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
- version.dll proxy -> RTW3MP.dll; all targets resolved by RTTI name at runtime (1033 classes found).
- Unnamed AI fleet-move routine found as "the call before StopBlockadedMoves in DoTurn2".
- Save-slot global found from `mov edx,[ptr]; mov edx,[edx]` at the start of SaveBuildCampaign.
- Instances (frmBuildCamp etc.) found by scanning committed heap for the VMT + Owner is TApplication.

## Investigation 2026-10-04: gotchas, each cost a debugging round (v0.1.0)
1. Never read the game image before the VCL `TApplication` window exists (DRM stub still unpacking).
2. Bounds checks on candidate pointers must guard against wraparound: `np=0xFFFFFFFF` passed `np+1<=end`.
3. No implicit TLS in the mod DLL (`thread_local`, MSVC magic statics => build with /Zc:threadSafeInit-).
4. A WH_GETMESSAGE hook dies with the thread that installed it: keep the installer thread alive.
5. Don't subclass the game's VCL-styled panels (the title art stopped painting). Use an own child control.
6. Never call Log from a vectored exception handler that can fire inside Log (OutputDebugString raises 0x40010006).
7. `UpdateUIValues(True,True)` assumes the "ships in service" tab; use (False,False) + `TabControl1Change`.
8. Event dialogs (TdlgEventAnswer) need an answer radio selected before OK closes them (test automation).

## Testing 2026-10-04: verified in the real game (v0.1.0)
*Two local instances, loopback, posted messages only.*
- Title screen "Multiplayer" entry -> MP window; host/join/claim/start over TCP.
- Client (Germany) changed research % and ordered a ship to the Mediterranean -> host merged, ran the month:
  values kept, ship moved, AI routines for Germany suppressed (logged), new month auto-sent (Jan->Feb).
- Client laid down a ship through the real Build ship dialog (built in Germany) -> present on host after
  Feb->Mar processing; AI fleet move on a German ship undone; AIMonthlyBuild suppressed.
- Client on "Ships under construction" tab receives Mar->Apr without UI error; rejoin restores nation.
Test tools: tools\rtw3ctl.py, e2e_test.py, run_turn.py, verify_tabs.py, ui_buildship.py.

## Fix 2026-10-04: nation selection in the lobby (v0.1.0)
*In the evening.*
- Bug (user report, reproduced live with the real window): the nation list was empty until the host had a
  campaign loaded AND past the initial-fleet steps; the host had no pick at all. Joining showed nothing to claim.
- Now: picks by nation name from the 10 game nations before any campaign exists (host can plan one too);
  matched to campaign indices automatically when the host opens a campaign (also during initial-fleet steps);
  conflicts are explained in chat (host system lines are broadcast). Duplicate player names are numbered
  instead of refused (two copies on one PC share config.ini).
- Verified live (tools\live_lobby_test.py): title button -> window -> Host/Join buttons -> picks via the
  dropdown -> host loads Game 1 through the real Load Game screen -> picks resolve -> Start session ->
  client plays Germany in Apr 1900.

## Fix 2026-10-04: window close, Start session, Give to AI (user report from a second PC) (v0.1.0)
*At night.*
- [x] did not close the Multiplayer window: reproduced only with REAL mouse input (posted-message replays
  don't trigger Windows' title-button tracking, not even on a control window). Cause: ui::PreTranslate ran
  IsDialogMessage for every message inside the WH_GETMESSAGE hook and swallowed the mouse-up that the
  system caption-button tracking loop waits for. Fix: only Tab goes through IsDialogMessage.
- Start session / Give to AI looked dead: both were disabled until a campaign was open. Now always clickable
  for the host; Start session explains what is missing and auto-starts when the campaign is ready; Give to AI
  also removes lobby picks before a campaign and explains when nothing is selected/already AI.
  Joined players no longer see host-only buttons; a player whose nation is given to the AI is told.
- Verified with tools\realmouse_test.py (real clicks, ~40 s of game time, 11/11 checks).

## Docs 2026-10-04: host walkthrough video (docs\hosting.mp4 + 4 stills in the README) (v0.1.0)
*Correction 2026-10-16: this entry was dated 2026-10-05, but it was committed in the evening of 2026-10-04 (local
time), before the diplomacy entry below. The video and the stills show the Multiplayer window of 0.1: no Diplomacy
list and no Fight my battles checkbox.*
- tools\record_hosting.py drives two copies (host + "Captain") through the bridge/posted messages only, so the
  user's mouse/keyboard are untouched, and composes frames from PrintWindow captures of the host process's
  windows only (never a screen capture: an earlier screen-capture take leaked another app and was deleted).
- PrintWindow sometimes returns a blank/partial title screen (VCL TImage); the script grabs best-of-8 and
  caches the title frame. The real LAN/VPN address line is overwritten with an example Hamachi IP.
- ctypes: HWND_TOPMOST (-1) must be passed as a pointer-sized value on 64-bit Python.
- ffprobe isn't on PATH, so `um video probe/contact` fail; call the bundled ffmpeg directly.

## Feature 2026-10-04: diplomacy between players, protocol 2 (v0.2.0)
*Late in the day.*
- Game model (1.01.44, all resolved by name; offsets only as documentation):
  - TBuilderNation +0x256 + 4*j = tension towards nation j (j=0 is `Tension`, with the player); >40 = war.
    `SetAITension(Idx,v)` writes both directions (j in 1..8). `IsAtAIWar`, `IsEnemy`, `NationIsAtWar` all test >40.
  - `AIAlliance[1..8]` (+0x27a) and `Allied` (+0x29a, alliance with nation 0) are month countdowns. `Allied` drops
    by 1 per month while the player is at peace (ReduceTimeLimits); at 1 the host gets an "extend?" event.
    AIAlliance drops while nobody is at war (CheckAITensionEvents).
  - `WarStartsWithNation(n)`: n.Tension=50, war counter global (General/War, >0 = months at war) := 1, Allied := 0.
    No dialogs. The debug menu "Start war" does the same by hand.
  - `DoPeace()` ends ALL the player's wars (co-belligerent AI wars too), budget cut etc., war counter := 0. Warns
    "War end not recorded!" (modal) unless the month's history record (form + Year*0x4B0 + Month*100 + disp,
    year/month via pointer cells read from DoPeace's first instructions) is 1 won / 2 lost / 3 compromise.
  - `HandleAIPeace(nation, BadLoss)` ends every AI war of the nation (and of its allies at war) with a modal
    Peace.jpg message; called at random (war weariness, BadLoss=0) or forced by unrest (BadLoss=1).
  - CheckForBattles2 returns at once unless the war counter > 0: a month at war with the host means battles.
- Mod: requests (MSG_DIPLO) only while planning; the host keeps offers/decisions and applies them after
  DoEndOfTurn, before the new month is saved and sent. Player pacts: alliances topped up to 60 months, wars
  restored at month end if the game ended them; HandleAIPeace hook hides player wars (tension 40) during the call.
  Merge keeps the host's Tension/AITension*/AIAlliance*/Allied in [NationK] (host-authoritative diplomacy).
- Gotchas: a test that edits tension during planning gets half-overwritten by the merge (fixed by the key filter);
  allying with both sides of a war makes the game drag the host into it (vanilla coalition logic);
  bridge calls that open modal game dialogs must not wait for the main thread (aipeace/applydiplo are async);
  the game reuses its dialog windows, so the test's per-window click guard must be reset each pass.
- Verified with tools\diplomacy_test.py (two instances, posted messages + bridge, ~2 min of game time): 52/52 checks
  (alliances host<->player and player<->player, war declarations through month end, real button clicks with
  confirmations, withdraw/decline, AI peace kept away from player wars, restore at month end, DoPeace path).

## Fix 2026-10-05: joined players' design studies never advanced (v0.2.1)
- Report (play test on another PC): a joined player could design a ship, but the design study never progressed.
- Cause: `TDesignShip.ReadyForBuild` = months left in the design study (0 = can be built, MAXINT = no study),
  saved in `DesignFiles<K>.des` (TDesignShip.SaveMeToBigFile) and merged to the host each turn. The game's
  `TfrmBuildCamp.AdvanceDesignStudies` (called from DoEndOfTurn and from TfrmMain.FormClose after a battle) walks
  only `PlayerNation.DesignList`, i.e. the host's nation 0. Per design and month: Random(100)==0 -> "technical
  issues" delay; a 2..7 % committee review event (dialog, may delay or cost prestige); CV / BB-BC Air Force events
  in some years; else `dec ReadyForBuild`; at 0 "Our latest design ... is ready for construction" (dialog with
  Go to build screen / Rework / Not now). `TfrmBuildCamp.DesignStudyList` (TDesignStudy) is unused legacy
  (loaded, never saved or advanced).
- Fix: hook AdvanceDesignStudies; after the game's routine, once per session month (`g_seq`), every human nation
  K>0 gets the countdown with the 1 % technical delay (no dialog events). Lines `design study nation K 'class':
  a -> b` in the host log; the player gets private chat lines (`notice=1` flashes the window; older clients ignore
  the key). Host-only change; protocol unchanged (2), so 0.2.0 clients work with a 0.2.1 host.
- Bridge: `designs <nation>`, `setready <nation> <index> <months>`.
- Testing now goes through the regression agent (`.github/agents/rtw3-regression.agent.md`) and
  `tools/regression.py` (one launch, all scenarios, saves restored).
- Test gotchas: the runner never polls the host's bridge during a month (bridge calls wait for the busy main
  thread and give up after 120 s); list replies arrive stripped, so their last entry ends in
  `;`; the host's own study reaching 0 asks (TdlgEventAnswer radios) "Go to the build screen / Not now / Rework the
  design" and the tests select "Not now" before OK.

## Fix 2026-10-05: intel reports doubled every month -> "no memory" and "protocol error" (v0.2.2)
- Report (0.2.0, another PC): a joined player got an "... Error no memory" game error around month 20 of an
  1890 campaign; after rejoining, the end of the next month gave "Disconnected from host: protocol error".
- Cause: `TfrmBuildCamp.LoadBuildCampaign` appends the save's `[IntelReports]` (`Intel0..Intel<ReportNo>`, the
  last one doesn't exist, so one empty entry more) to `TfrmBuildCamp.IntelReports` (TStringList +0x320c) and nothing
  but FormDestroy ever clears it. The game always loads into a fresh strategic screen; the mod reloads into the same
  one every month (client: new month; host: after merging), so the host's list doubled each month (save -> reload
  -> save ...). The save and the month package grew exponentially: out of memory in the 32-bit game (RTW3.exe is
  not large-address-aware: 2 GB) and frames over the 96 MB limit (the receiver's length check -> "protocol error").
  Measured: 6 reloads multiplied the list by 6; the slot-1 test campaign had 3420 entries, 6 distinct.
- Not the cause: per reload the game also leaks ~2.5 MB private memory and ~12 GDI objects (USER objects and
  handles stay flat); documented as a long-session limitation.
- Fix: hook LoadBuildCampaign: clear IntelReports before, drop empty/repeated entries after (repairs bloated
  campaigns in memory; the next save is clean). Uses TStringList FList/FCount (read without refcounting), Clear,
  Add via RTTI. Network: precise drop reasons (`message too large`, `protocol error (bad frame length N)`),
  bad_alloc in the receive path drops the peer instead of crashing, the host refuses to send an oversized month.
- Bridge: `intel` (count / distinct entries in memory).

## Testing 2026-10-05: intel-report regression coverage (v0.2.2)
- `tools/regression.py`: scenario `intel_reports` (repair on load of the bloated slot 1 fixture, two more loads
  before hosting, clean saves at the session start) and 6 checks after every processed month (`intel` count ==
  unique on host and client, <= 30 new reports a month, clean `[IntelReports]` in Game1/Game77, client month
  package <= +25 % per month); client and host private memory is reported per month.
- Gotchas: the slot 1 fixture stays bloated on purpose (ReportNo=3420) so every run exercises the repair; never
  send `save` (or anything that saves) outside a run that backed up the slot: a diagnostic `save` rewrote slot 1
  once. The load checks run before `host`/`start` because the session start saves slot 1.

## Fix 2026-10-05: joined players' doctrine/training never took effect (v0.2.3)
- Report (0.2.0): the joined player "can't set doctrine and/or training".
- Cause: the Doctrine button (`btbnTrainingClick`) opens `TdlgDoctrine` for PlayerNation (the client's own nation,
  fine). Apply stores the new priorities in `TBuilderNation.PendingTrainingPriorities` (4 flags) with
  `PendingTrainingTime = 12`; the budget's "Extra training" is their cost (GetTotalMonthlyTrainingCost). The monthly
  `TfrmBuildCamp.ReduceTimeLimits` counts it down and at 1 copies Pending -> `TrainingPriorities` ("New training
  applied...") for PlayerNation only, i.e. the host's nation 0. Same for `PendingMissileStorage` /
  `MissileStorageTime` -> `MissileStorage`. A scan of ReduceTimeLimits for other PlayerNation-only writes found only
  one-off host events (budget grant, build constraint). Fleet Exercise runs immediately on the client (merged
  with its ships).
- Fix: hook ReduceTimeLimits; after the game's routine, once per session month, every human nation K>0 gets the
  same countdown; host log `doctrine nation K: training a -> b months (applied)...`; the player gets chat lines.
  Bridge: `doctrine`, `settraining`, `setmissiles`.

## Fix 2026-10-05: a player's turn could be dropped when a save file was briefly locked (v0.2.3)
- Found by the regression agent (1 of 2 runs): `* Could not apply Captain's turn: cannot write map data`, only in
  the client's chat; the month ran without that player's orders. WriteFileBytes (temp file + MoveFileEx) had no
  retry; right after the game's own save, antivirus / OneDrive can hold the file for a moment.
- Fix: WriteFileBytes retries create/rename for ~3 s on sharing/lock/access errors, then tries an in-place write,
  and logs the Windows error. MergeSubmission prepares all files first and restores already-written ones if a
  later write fails (all-or-nothing). A rejected turn is now in the host log and a host notice.
  Bridge: `writetest <slot> <file> <bytes>` for the lock test.

## Testing 2026-10-05: doctrine regression coverage (v0.2.3)
- Scenario `doctrine`: the real Doctrine button and TdlgDoctrine (posted messages; layout read from the form
  resource `TDLGDOCTRINE` in RTW3.exe, opened read-only as a data file) make the pending change; nation 1
  training + missile policy (2 months, merged on submit) and the host's own change are checked after
  design_study's two months through a `run_month` hook (no extra months).
- Gotchas: VCL-styled check boxes ignore a posted `BM_CLICK` (the style hook wants the real cursor); post
  `WM_COMMAND`/`BN_CLICKED` with the box's handle to its parent instead. Apply asks "Confirm" (OK).
- Found while testing (intermittent, 1 of 2 runs): the host rejected a player's whole turn with `* Could not
  apply Captain's turn: cannot write map data` (chat only, nothing in the log file). `WriteFileBytes` renames
  its temp file over `MapData1.dat` once, right after the game's own save wrote that file; a short-lived lock by
  another process (antivirus scan; Documents under OneDrive here) makes `MoveFileExW` fail. The bcs and designs
  were already rewritten, but nothing was reloaded, so the month ran without the player's orders.

## Testing 2026-10-05: file-lock regression coverage (v0.2.3)
- Scenario `file_lock` (no months): the client's `writetest 77 rtw3mp_locktest.tmp <bytes>` while Python holds
  that throwaway file with `CreateFileW` share mode 0 (lock released after ~1.2 s -> ok after retries; held for
  the whole call -> `error write failed (5|32)` after ~3 s, file unchanged, no `.mptmp` left; file deleted).
- Every processed month now fails on a `NOT applied` / `rejected` / `write ... FAILED` line in the host log, so a
  dropped turn always shows up as a failure with the log line.
- Gotcha: with a share-mode-0 handle open, `MoveFileExW` onto the file fails with error 5 (access denied), the
  in-place `CreateFileW` with 32 (sharing violation); both are retried by the mod.

## Feature 2026-10-06: random events for joined players (v0.2.4)
- Report (0.2.0): "there are no events for the joined player".
- Game model: `CheckTurnEvents` (in the host's turn) fills one monthly slot (peace event, officer event, buy
  technology, `GetRandomEvent` about 1 in 3, intelligence, random tension changes ...), all for PlayerNation.
  `GetRandomEvent` -> `HandleEvent(idx, Force)` -> `EventLikelihoodCheck` -> `TdlgEventAnswer.Execute(event, nation)`,
  which applies the answer: the player's own Prestige, BudgetModifier, BaseResources, Funds, UnrestLevel,
  TechLeakRisk, Corruption, BuildConstraint*; other nations' `Tension`/`Allied`/`TechSharing` (their relation with
  nation 0); treaties (`CreateArmsControlTreaty`), peace (`NegotiatePeace`), `ArmyOffensive`, `EvilPlan`, possession
  `Rebellion`. Nation pickers (`GetMostTenseNationIndex` etc.) loop indices 1..8 = "not the player".
  `TfrmBuildCamp.Events`: 100 records x 100 bytes from `Data\Events.dat`: +0 caption, +4 affected-nation code,
  answers at +8+24j (caption, budget, prestige, tension, affected, condition at +0x1c+24j), +0x50 condition,
  +0x54 cooldown months (saved as [EUT]), +0x58/+0x5c years, +0x60 government. `EventLikelihoodCheck` allows only
  condition >= 30 events at war and only condition < 30 in peace.
- Fix: the joined player's PC rolls once per calendar month (33 %) after the month loads. `RunClientEvent` makes
  K "the player": swaps TList entries 0 and K, writes K's relations into every nation's Tension/Allied (TechSharing
  0), sets the war counter from K's wars, uses K's own cooldowns (`events_<hash>.ini` in the mod data folder),
  vetoes events that need the host (EventLikelihoodCheck hook; condition 25, 30-40, answers 20, 25-27, 40, 41,
  50-56, 60, 61, 65), calls the game's HandleEvent inside __try/__finally, then restores everything. Relation and
  own-field changes go to the host (MSG_EVENT): tension to the K pairs clamped 0..40, alliances only with AI
  nations, own fields applied in memory at once (the submission carries the same values; no double count because
  the merge replaces [NationK]). One event per player per month on both sides. While an event dialog is open,
  LoadCampaign/SaveGame/SetPlayerNation and state-changing bridge commands refuse.
- Bridge: `eventlist`, `eventnow [idx]`, `eventlast`, `eventchance <pct>`; env `RTW3MP_EVENT_CHANCE`.

## Testing 2026-10-06: joined players' random events (v0.2.4)
- The client copy now runs with `RTW3MP_EVENT_CHANCE=0`; `run_month` waits for the client's monthly roll line
  (answering its event dialog) before any other client command, and every month checks one roll
  (`event: no event this month` at chance 0). The runner deletes `events_*.ini` (client cooldowns, in the RTW3MP
  folder) before and after the run: the fixture always starts at the same month, so a leftover file would mark
  it as already rolled.
- Scenario `events` (planning month before `merge`, no own months): forced "naval rearm" event #5 on the client
  (real `TdlgEventAnswer`, first answer) -> host applied `tension 1-0 5->7 BudgetModifier+2 Prestige+1` to nation 1
  only, client nation order restored; a second forced event (#1) in the same month -> `second event of Captain in
  Aug 1900 ignored (one per month)`; host-only #4 refused (`error event needs the host`); after `merge` host and
  client agree (Prestige 20 -> 21 after the event -> 22 after the month, see below). 28/31 in the first run, 32/32
  (suite 189/189) in the second.
- Found: a chance-100 roll in the month after `diplomacy` B (nation 1 at war with the host and nation 2) found no
  event in its 101 tries (`event: no event this month`). Confirmed by disassembly: `EventLikelihoodCheck` rejects
  events with condition < 30 while the war counter is > 0, so at war only war events qualify, and all of them need
  the host: a joined player at war gets no random events by design. The random path is now tested in the last
  month at peace (diplomacy A: #21 "A new naval secretary believes destroyers...", first answer -> `BudgetModifier-1
  Prestige-2` on the host), and one check documents the war rule with the war month's own roll at chance 100.
- Gotcha: `eventnow` without an index goes through the once-a-month guard and only logs `this month was already
  rolled` in a month that has loaded; set `eventchance 100` before the month instead (pre-month hook).
- Gotcha: a forced second event changes the client's own nation (here Prestige +1, BudgetModifier +1). The host
  ignores its message, but those values still reach the host with the submitted turn (the merge replaces [NationK]).
  Only `eventnow` can do this; the monthly roll is guarded by the cooldown file.
- Gotcha: drive `TdlgEventAnswer` by screen position (RadioButton1..3 top to bottom in Panel1, hidden
  `cbSelectNation`, OK); `Data\Events.dat` (plain text, read-only) gives each answer's budget/prestige/tension
  numbers in `eventlist` order.

## Feature 2026-10-06: peace terms for wars between players (v0.2.5)
- Report (0.2.0): "there is no peace deal for either player in a player-vs-player war". The user chose: the winner
  picks the possessions like in single player; if they don't pick before the month ends, they are picked
  automatically.
- Game model (1.01.44): `NegotiatePeace(aChance=EDX, aLossGain=ECX, ForceBadPeace)` with lossGain -6..+6. Gains:
  1..2 = minor (prestige +1, enemy BaseResources -100), >= 3 = large (prestige +2, -500); losses: <= -5 utterly
  defeated, -4..-3 considerable concessions (ships via `RemoveShipInPeace`, possessions taken). For lossGain > 1
  the player picks possessions in `TdlgPossessions` (unnamed proc at 0xcf4514, budget 2 x lossGain): owned by an
  enemy at war, Value < 11, `AreaIsAdjacentToOwnPossessions(area, player)`; then `ChangePossessionOwner(poss,
  newOwner, RemoveAllCoastalArtillery)`, `ScrapShipsAccordingToTreaty`, `ShowPeaceTreaty(TStringList)`, `DoPeace`.
  War record cell: 1 won, 2 lost, 3 compromise. `AINationSelectPossessionsAfterWar` (0xcf4b64) needs the winner's
  AITension (> 40), which nation 0 doesn't keep, so the mod picks possessions itself.
- `RemoveShipInPeace(form, from, to, var aText)` (unnamed, 0xc5b6b8, found by its call in NegotiatePeace): large,
  recent, in-play ships not under repair, 60 % each, via `TransferShipAsReparation` (renames the ship).
- War progress: `EventLikelihoodCheck` conditions 34-36: winning = VP >= 2 x enemy and >= 100*G + 500, slightly
  ahead = >= 100*G + 300 (G: a global read through `6B 15 [G] 64`, imul with an 8-bit immediate). Nation VP comes from
  `HandleBattleAftermath` (PlayerNation.VP / form.EnemyVP), `DoBattleBetweenAINations` and
  `QuickResolveRaiderBattle`; `HandleAIPeace` zeroes VP of nations 1..8, so the mod keeps its own ledger per pair
  of players (hooks on the three battle functions compare VP before/after; naked thunks copy the stack args).
- Design: terms are fixed when the peace is accepted (level 2 major / 1 minor / 0 compromise), shown in the
  Diplomacy list (war score in the Tension column, outlook in the label) and the chat. The winner may claim the
  loser's possessions worth up to 4/8 (Claim land... window, `claim` diplomacy action validated on the host);
  unclaimed -> automatic pick (most valuable first, within the budget). At month end the host applies:
  possessions (`ChangePossessionOwner`), 1/3 `RemoveShipInPeace` calls, prestige +1/+2 and `PrestigeWarLoss`,
  BaseResources -100/-500 (at most half), then `DoPeace` with record 1/2/3 for the host; `ShowPeaceTreaty` on the
  host, and on the two players' PCs with the new month (MSG_CHAT key `treaty`). Ledger and terms are saved with
  the reservations file. Protocol unchanged (2): new lobby keys `dipWars`/`dipTreaties`.
- Bridge: `claimland`, `warvp`, `terms`, `possessions`, `claimable`; `diploas` takes the claim list; `diploview`
  shows `/vp=/claim=/outlook`.
- Found by the regression run, fixed in the same version:
  - `PrestigeWarLoss(Big)` starts with `ProportionalPrestigeLoss(10)` on PlayerNation (the host), so every treaty
    cost the host prestige. The loser's loss is now computed directly: P div 10 when P >= 20, otherwise 1 (not
    below 0, the game's DecMin helper 0xab7744); major: -1, and -1 again above 20.
  - `DoPeace` (end of the host's last war) sets Prestige of nations 1..8 to 0. AI nations don't use it; the
    joined players' nations did lose theirs at every end of the host's last war, since 0.2.0. A DoPeace hook now
    restores the prestige of human nations (log `end of war: prestige of player nation N kept at P`).

## Testing 2026-10-12: peace treaties between players (v0.2.5)
- Scenario `treaty` (after `diplomacy`, one own month; `events` now finds diplomacy's war month in run order). War
  1-2 by the client + `applydiplo`, `warvp` for a major victory, peace offered/accepted, claim validation on the
  client and the host, the real "Claim land..." window (Diplomacy list row selected with a cross-process
  LVM_SETITEMSTATE, posted clicks, `claimui toggle`), calling the peace off, then the processed month applies the
  treaty (possessions, ships, prestige, reparations, both chats, the client's "Peace treaty" dialog, war score
  lines). After the last month, with `applydiplo`: an automatic minor victory of the stand-in, a host-vs-client
  victory (DoPeace, the host's own treaty dialog) and a compromise. 65 checks (+ 8 for its month) when the
  feature is on; it stops after 3 when the startup line is not `Peace treaties: ok`.
- The first 0.2.5 build had `Peace treaties: UNAVAILABLE (... scale=00000000 ...)`: the G lookup searched the
  imm32 form of `imul edx, [G], 100`, the game uses `6B 15 [G] 64` (imm8). Fixed in the same version (the lookup
  tries the imm8 form first); the client's claim check also skips repeated names now, like the host's.
- The first test runs also found that the loser's `PrestigeWarLoss` (its `ProportionalPrestigeLoss` part acts on
  PlayerNation) cost the host Prestige in every player treaty, and that the game's `DoPeace` sets the Prestige of
  nations 1-8 to 0 when the host's last war ends (older than 0.2.5; also the game's own peaces). Both fixed in the
  same version: the loser's loss is applied to the loser (P div 10 when P >= 20, else 1, not below 0; major -1, -1
  more above 20), a DoPeace hook keeps the players' Prestige (`end of war: prestige of player nation N kept at P (the
  game set X)`); the reparation lines no longer end in "..".
- Final 0.2.5 results: `--only treaty` 80/80 (session 7, treaty 73 = 65 + 8 month checks) in 73 s; full suite
  262/262 (session 7, intel_reports 10, file_lock 7, events 32, merge 17, doctrine 17, design_study 31, diplomacy
  68, treaty 73), 6 processed months (13-17 s each), game open 227 s.
- Observed in the full run (Germany 1 vs France 2, G = 3): one battle in the treaty month (`war score 1-2: +2/+2 ->
  902:102`); claims from the window Senegal 2 + Cochin China 5 + Polynesia 1 = 8 of 8 (Algeria past the budget
  refused) moved to Germany with 3 ships, France BaseResources -500, Prestige Germany 22 -> 24, France 20 -> 17,
  host 19 -> 19; client dialog "Peace treaty" (8 lines). Automatic minor win of France: Tanganyika 3, Marshall
  Islands 1 and two worth 0 (= 4), 1 ship, Germany -100 and Prestige 24 -> 22, France 17 -> 18, host unchanged.
  Britain over Germany: the 3 claims moved, 3 ships, -500, host Prestige 19 -> 20 (+2, DoPeace dent -1), Germany
  22 -> 19, France kept at 18 (`kept at` lines for 1 and 2), the host's own dialog (8 lines). Compromise: nothing
  changed. The game's own AI peace in `diplomacy` (France vs Russia) ceded Corsica and Tunisia to Russia, so the
  claimable lists differ from a `--only treaty` run.
- Follow-up: the reparation lines were to be split at the game's line break into "... as war reparation." and
  "She is renamed ...", but the game's text has none: `RemoveShipInPeace` concatenates `<ship> handed over to
  <winner> as war reparation` and ` She is renamed <new> in <adjective> service.` (the earlier "line break"
  explanation was wrong). With that build `--only treaty` 78/80 (treaty 71/73, 66 s): the client and host dialog
  checks failed on "reparation She". The checks now take the line count from the dialog (`treaty_text`) instead of
  1 + k + s + ... Fixed in the same version by splitting at the game's " She is renamed ": `--only treaty` 80/80
  (treaty 73/73, 69 s), each treaty 11 lines (3 ships x 2), e.g. "B Mecklenburg handed over to Great Britain as war
  reparation." / "She is renamed B Rodney in British service.".
- Gotcha: client `diplo`/`claimland` return when the request is sent; `applydiplo` right after it ran before the war
  declaration reached the host. Wait for `diplomacy request 1 <action> <target> accepted` in the host log.
- Gotcha: `applydiplo` applies a treaty at once, but a processed month afterwards merges the client's planning copy
  of `[Nation1]` (prestige, base resources, ships) over it. Only the real month-end path (after the merge) is
  safe, so the scenario uses `applydiplo` treaties after its last month.
- Gotcha: the game's treaty dialog is `TdlgTotalLog` captioned "Peace treaty" (RichEdit + OK); `DoPeace` takes 1
  host Prestige when a war ends without a major battle ("Navy prestige takes a dent", a TMessageForm).

## Fix 2026-10-12: joined players' victory points never rose against the host (v0.2.6)
- Report (0.2.0): "VP for non host player does not increase when fighting the host".
- Game model (1.01.44): the strategic screen shows `stVP` = PlayerNation.VP (TBuilderNation +0x175, `[NationK] VP=`)
  and `stEVP` = TfrmBuildCamp.EnemyVP (+0x4a63, `[General] EnemyVP=`) (`UpdateVPNumbers`). In the host's wars,
  everything the enemy achieves goes to EnemyVP only: `HandleBattleAftermath` (+GetLogAdjustedVP(VP0) to
  PlayerNation.VP, +GetLogAdjustedVP(VP1) to EnemyVP), QuickResolveRaiderBattle, DoHomeAreaBattle,
  CheckForcesInArea, DoBlockadeEffects, CheckBlockadeStatus, CheckResultOfSinkings, CheckForceOnForeignStations,
  ResolveArmyOffensive, DoPlayerDecline. The enemy nation's own VP is never written. DoPeace (and
  WarStartsWithNation when the war counter 0xf52ff4 is not positive) reset PlayerNation.VP and EnemyVP to 0;
  HandleAIPeace sets VP = 0 for nations
  1..8 after any AI peace. On a joined player's PC PlayerNation is its own nation K, so its "VP" never moved in a
  war with the host, and its "Enemy VP" showed the host's enemies' score (its own points).
- Fix (host): battles are credited as they happen (OnBattleVP, from the existing VP hooks: the enemy side's
  EnemyVP gain of a host battle goes to the enemy nation's VP when it is human); the rest of the month's EnemyVP
  (`EnemyVP - enemyVPSeen`) is shared evenly among the host's enemies at month end, only human ones credited (AI
  nations keep the game's model). A human nation at peace gets VP 0 (as the game does for the player). Human
  nations still at war keep their VP through HandleAIPeace. `[NationK] VP` is host-owned in the merge. The DoPeace
  hook shares the pending points before the game's own end of the host's last war and restarts the count after
  it; the month-end step re-syncs the count after the peace and war it makes (found by the regression run: a
  peace and a new war in the same step left the count at the old score, so the next non-battle points were lost).
- Fix (client): after loading a month, EnemyVP = the VP of the nations at war with the player's nation (the
  host's VP included), then `UpdateVPNumbers`. `[General]` is never merged back, so this only changes the view.
- Bridge: `vpinfo`, `battlevp <opp> <vpHost> <vpOpp> [level]` (the real HandleBattleAftermath through the hook),
  `enemyvpadd <n>`.

## Testing 2026-10-12: victory points of joined players (v0.2.6)
- Validation of the report against 0.2.5 (0.2.5 code + the test-only `vpinfo`/`battlevp`; log `RTW3MP 0.2.5 starting`,
  `Peace treaties: ok (... battleHooks=1/1/1)`): session as in `session`, host `diplo war 1` (`diplomacy request 0
  war 1 accepted`) + `applydiplo` (war=1, hostWars=1), then `battlevp 1 1000 3000 Minor` -> `ok host=0->129
  enemy=0->291 opp=0->0`; host `vpinfo` `enemy=291 seen=-1 vp=0:129,1:0,...`, client `vpinfo` `vp=0:0,1:0,...`.
  Bug confirmed: the host's EnemyVP took the 291 and nation 1's own VP stayed 0. Game open 21 s, slot 1 restored.
- Scenario `victory_points` (after `diplomacy`, before `treaty`, one own month; see the catalog in the regression
  agent): `battlevp` battles of the host against 1 and 2, `enemyvpadd` + `applydiplo` sharing (one enemy, two
  enemies, a drop), peace through the client's "Accept peace" button and `diploas` (separate peace, DoPeace), one
  processed month with only the war 1-2 (merge keeps the host's VP, the client's "Enemy VP" = its enemies' VP), a
  real AI peace (`aipeace 2 0` after `settension 2 3 50`) with both player nations at war. 42 checks + 8 month checks.
- Results, first 0.2.6 build: `--only victory_points,treaty` 127/127 (victory_points 47) in 107 s; full suite 309/309
  in 266 s. Final 0.2.6 build (the `seen` re-sync below): `--only victory_points,treaty` 130/130 (session 7,
  victory_points 50, treaty 73) in 110 s; full suite 312/312 (session 7, intel_reports 10, file_lock 7, events 32,
  merge 17, doctrine 17, design_study 31, diplomacy 68, victory_points 50, treaty 73), 7 processed months (13-17 s
  each), game open 263 s.
- Observed (full run, Germany 1, France 2): battle 1000/3000 -> host VP 0 -> 129, EnemyVP 0 -> 291, Germany 0 -> 291
  (`victory points: nation 1 +291 in its battle against the host -> 291`); `enemyvpadd 101` -> Germany 392; with
  France at war too: +51/+50 -> 443/50; drop 493 -> 0 -> 101: nothing shared; separate peace 0-1: Germany 443 -> 0,
  France keeps 50; peace 0-2 (DoPeace) + a new war 0-1 in one step: host VP 129 -> 0, EnemyVP 101 -> 0, France
  50 -> 0; host at war with both + 1-2 at war: battles 500/2000 and 300/1000 -> Germany 214, France 129 (host
  0 -> 80 -> 138); host peace with both: DoPeace resets the host only, Germany 214 and France 129 kept; the month:
  Germany still 214 after the merge (the client's copy had 0), client `victory points shown: own 214, enemy 129`
  while the host's EnemyVP was 77; `aipeace 2 0`: `AI peace: VP of player nation 1 kept at 214 (the game set 0)`
  and nation 2 at 129; peace 1-2: both -> 0. An `--only` run had a 1-2 battle in the month (`war score 1-2:
  +2/+12`): Germany 214 -> 216, France 129 -> 141, client `own 216, enemy 141` against the host's EnemyVP 343. With
  the final build Germany also gets the re-sync step's 101, so it enters g) with 101: battles -> 315, client `own
  315, enemy 129`, `AI peace: VP of player nation 1 kept at 315 (the game set 0)`.
- Finding, fixed in the same version: `seen` was taken at the start of the month-end step (ShareEnemyVP), but the
  step can reset EnemyVP after that (MakePeace -> DoPeace when the host's last war ends; StartWar). When one step
  ended the host's last war and started a new one, `seen` stayed at the old score (101) while EnemyVP was 0, so the
  new war shared nothing until EnemyVP passed the old `seen`: `enemyvpadd 101` + `applydiplo` with Germany the host's
  only enemy gave Germany +0 of 101 (battles were not affected). Fix: `seen = EnemyVP()` at the end of ApplyDiplomacy,
  and the DoPeace hook (`OnHostPeace`) shares before the game's DoPeace and re-syncs after it. Now asserted: after
  that step seen == enemy == 0 (was 101) with no share line, then `enemyvpadd 101` + `applydiplo` gives Germany
  0 -> 101 (`victory points: nation 1 +101 of the host's enemies' 101 this month -> 101`, seen == enemy == 101); after
  the host's DoPeace in g) seen == enemy == 0 (was 444), no share line. The share before a DoPeace that the game makes
  itself during a month can't be reached with `applydiplo` (ApplyDiplomacy shares first). It stays untested; the
  checks only show that nothing is credited twice.
- The first full run failed `treaty` i) (host Prestige 18 -> 20 instead of +1): the scenario's first version ran
  `battlevp` with the host at peace, leaving host VP 138 / EnemyVP 343. The next host war's start kept the host's VP
  at 138 (WarStartsWithNation resets VP/EnemyVP only while the war counter is not positive, see the fix entry). The
  game's DoPeace then skipped its "without any major battle" dent. The scenario now fights its battles inside a host
  war that DoPeace ends and resets its EnemyVP marker. This was test interference, not a mod bug.
- Gotcha: `battlevp` is synchronous on the host's main thread: run it from a thread and answer dialogs meanwhile.
  GetLogAdjustedVP does not depend on the current score (2000 -> 214 from EnemyVP 0 and from 101).

## Fix 2026-10-13: a joined player's "Copy game" played the host's nation (v0.2.7)
- Report (0.2.0): "non host player can copy save but the save is only for the host player nation".
- Game model (1.01.44): `btbnSaveToOtherSlotClick` -> `TdlgInputNumber` -> `CopyGameToOtherSlot(aFromSlot, aToSlot)`
  (register arguments only) copies RTWGame<n>.bcs/.sta/.off, MapData<n>.dat, DesignFiles0..8.des, Autosave.sac/.off
  and BattleInfo.bbi. The game always plays nation 0 of a save (the player's [Nation0] sections, DesignFiles0,
  [General] War/EnemyVP, the .off officers and divisions, [EUT], the .sta month records). On a joined player's PC the
  open slot (77) is the host's campaign, so the copy played the host's nation.
- Nation numbers in a save (from the save routines' key strings): [Nation<i>*] sections; `Tension`/`Allied`/
  `TechSharing` (relation with nation 0) and `AITension<j>`/`AIAlliance<j>` (j = 1..8, self cell 0, symmetric);
  `Ship<n>BuildingNationIdx`; `AU<n>Nation`, `AT<n>Nation`; [General] BattleAlliedNationIdx/BattleEnemyAlliedNationIdx;
  MapData `MapArea<a>MineLevel<i>` and `MapArea<a>Possession<p>TakenFrom`; DesignFiles<i>.des. Names (possession
  Owner, Enemy1/Enemy2) need nothing. `DoPeace` resets InvasionTarget, EnemyInvasionTarget, ArmyOffensive,
  RevolutionPending, LastMissionIdx (20), EnemyVP, TFW (1), TotalWar, HasBeenBattle, AutoBuildSubs.
- Fix: a CopyGameToOtherSlot hook. When the source is the joined player's slot and the copied .bcs is
  byte-identical to it, `saveio::MakeNationPlayer` swaps nation 0 and K everywhere above, rebuilds the relations
  from K's point of view (the model of the joined player's events), clears the commanders of the host's old ships,
  keeps only divisions of the new player's ships, sets War/EnemyVP for K and the end-of-war values when only the
  host was at war, writes K's own event cooldowns into [EUT], and deletes the host's battle-in-progress files. The
  nation K comes from the session, or from `rtw3mp_player.ini` in slot 77 after leaving it. Kept as they are:
  officers (the player's pool), the .sta month records, intel reports, IDNo.
- Bridge: `copygame <slot>` (the game's routine through the hook). An offline harness (saveio + util built as a
  console program) checked the transform against the 1900 fixture with doctored wars, alliance, VP, mines,
  TakenFrom, commanders and divisions.

## Testing 2026-10-14: joined players' Copy game (v0.2.7)
- Validation of the report against 0.2.6 (0.2.6 code + the test-only `copygame`; log `RTW3MP 0.2.6 starting`):
  session as in `session` (host Great Britain = nation 0, client Germany = nation 1, Aug 1900), client `copygame 78`
  -> the game's `TMessageForm` "Rule the Waves III" [OK]; Game78 `[Nation0] Name=Great Britain`, `[Nation1]
  Name=Germany`, RTWGame78.bcs byte-identical to RTWGame77.bcs, DesignFiles0 the host's, `[General] War=-68`; no log
  line; slot 77 unchanged, the client still in slot 77 as nation 1. Bug confirmed: the copy plays the host's nation.
  A second `copygame 78` showed the overwrite prompt: `TMessageForm` "Confirm" [Cancel, OK]; Cancel copied nothing.
  Game open 24 s, slot 1 restored (ReportNo=3420), Game77/Game78 and the cooldown file deleted.
- Scenario `player_copy` (the last one, one session month + one single-player month; see the catalog in the
  regression agent): a) the client's in-session copy compared key by key with an expectation written from the
  specification (`expected_copy`/`expected_map`/`expected_off`; checked offline first against the mod's own
  transform, harness `MakeNationPlayer`, on three doctored fixtures: identical, incl. 24 cleared commanders, 34 map
  fields, a division kept and one dropped, both war branches), b) slot 77 and slot 1 untouched, c) the host's own
  copy untouched, d) a copy cancelled at "Confirm", g) the real "Copy game" button, a session month, e) leave + `load
  77 0` + `copygame 80` (nation from `rtw3mp_player.ini`), f) the copy loaded and one month played in single player.
  26 checks + 8 month checks.
- Results (0.2.7): `--only player_copy` 41/41 (session 7, player_copy 34) in 66 s; full suite 346/346 (session 7,
  intel_reports 10, file_lock 7, events 32, merge 17, doctrine 17, design_study 31, diplomacy 68, victory_points 50,
  treaty 73, player_copy 34), 8 processed months (13-17 s each) + the copy's month (10 s), game open 311 s.
- Observed in the full run (Mar 1901, after `treaty`: Germany at peace with everyone, the host's War -2): `player
  copy in slot 78: Germany is the player nation, Great Britain is nation 1 (AI); 5 sections, 26 commanders, 32 map
  fields, divisions 0 kept / 0 dropped, at peace`; War -2 kept, EnemyVP 0; [EUT] took the client's cooldowns
  (EUT1=5, EUT5=5, EUT34=9) instead of the host's (EUT32=5); 154 nation numbers swapped. The mod's box: "Slot 78 is
  your own copy of the campaign: you play Germany. Great Britain, the host's nation, is run by the computer in this
  copy. The multiplayer session is not affected." Loaded as nation 0: Germany 38082 funds / 37 ships, Great Britain
  33009 / 51 (the session's values with 0 and 1 swapped), relations in memory = the file. Its month (Mar -> Apr
  1901, 10 s): two "Confirm" boxes, two picture messages, an event for Germany ("Event - April 1901"), the turn report;
  afterwards Germany 44741 funds, Great Britain (AI) 54 ships and its 35 possessions, no exception. The `--only` run
  (Aug 1900) gave the same picture (22 commanders, Germany 3934 -> 4043, Great Britain 51 -> 52 ships, 35 -> 35
  possessions). MapData TakenFrom had no 0/1 values in either run (only the 32 mine lines changed); the offline
  harness covers it.
- No mod bug found. The first `--only` run failed 2 of 34 on test problems: the button test typed 81 into
  `TdlgInputNumber`, which the game runs with 1-9 (the form resource says 4-20) and clamped to 9, so the copy went to
  Game9 (created by the run, deleted by hand); and the game does not write the slot at the end of a single-player
  month, so a file-date wait timed out although the month had been played. Now the button uses the highest free slot
  of 3-9 (chosen before the run, deleted after) and the month ends at the turn report, then `save` + the file date.
- Gotchas: never answer a copy's dialogs with `dismiss_dialogs` ("Confirm" is OK/Cancel, OK overwrites; the
  dialog's default slot is 1, the host's slot on a test PC); the mod's box is a Win32 `#32770` whose text is
  readable through its Static child.

## Fix 2026-10-14: a joined player's Research screen showed and edited the host's research (v0.2.8)
- Report (0.2.0): "Research prio is mirrored from host to non host player but not % of money spent".
- Game model (1.01.44): research priorities and the research budget are per nation (`TBuilderNation.ResearchAreas`
  +0x325: 25 records of 0x35 bytes, Advantage byte +0, Prio integer +1, saved as `Research<i>Prio`; `ResearchPct`
  +0x1b0), so the turn merge already carried them. But `TfrmBuildCamp.btbnResearchClick` opens
  `TfrmResearch.Execute(aNation)` with BuilderNations[0] (`mov eax,[ebx+0xae4]; xor edx,edx; call TList.Get`), not
  PlayerNation. Execute loads `aNation.ResearchPct` into `seResearchBudget`, keeps aNation as
  `CurrentResearchingNation` for the priority buttons, and writes the budget back on OK. On a joined player's PC
  nation 0 is the host's nation: the player saw the host's priorities and budget, and their changes went to the
  host's nation in their own copy (never merged, replaced the next month). A scan of the game's other screens for
  nation-0 lookups (BuilderNations[0] and GetBNation(0) in click/execute handlers) found only the free-design,
  new-game, debug and aircraft-request screens, which a joined player doesn't use in a campaign.
- Fix: a `TfrmResearch.Execute` hook: when the nation passed is nation 0 and PlayerNation is another nation (only on
  a joined player's PC), the screen opens for PlayerNation (log `research screen: opened for the player's nation K
  instead of nation 0`). The host and the joined player's event dialogs (nation list swapped) are unchanged.
- Bridge: `research <nation>` (budget % and the 25 priorities), `researchopen` (the Research button's handler).

## Testing 2026-10-14: joined players' research screen (v0.2.8)
- Validation of the report against 0.2.7 (0.2.7 code + the test-only `research`/`researchopen`; log `RTW3MP 0.2.7
  starting`): session as in `session` (Aug 1900). Host and client: `research 0` -> `ok pct=8 prio=2,2,...,2,0,0` (all
  Medium), `research 1` -> `ok pct=13 prio=1,2,2,2,2,2,2,2,2,2,3,2,3,2,2,3,2,...,0,0`. Client `researchopen` ->
  `TfrmResearch` "Research": `seResearchBudget` "8" and every listed area "Medium" (nation 0's values; nation 1 would
  show 12 and Low/High), no log line. The popup menu's "Low" on row 0 (Machinery development) and budget 7 + OK: client
  `research 0` -> `ok pct=7 prio=1,2,...`, `research 1` unchanged (`pct=13`). After one month: the host's nations 0 and
  1 unchanged (8 / 13 and their priorities), the client's nation 0 back to the host's `pct=8` all Medium, its nation 1
  still 13, and its dialog again "8" / all Medium. Bug confirmed: the screen shows and edits the host's nation, the
  player's own research never changes. The month had `AI suppressed: DetermineAINationResearch for nation 1` (and 2).
  Game open 47 s; slot 1 restored (ReportNo=3420), Game77 and the cooldown file deleted.
- Scenario `research` (after `merge`; shares the next processed month, `design_study 1` in the full run, one own month
  when nothing later processes one): setup `nfield 1 ResearchPct 10` (<= 12, nation 0 has 8). a) client `researchopen`:
  the log line, the dialog shows nation 1's budget and priorities, not nation 0's; b) popup "Low" on row 11 (Explosive
  shells = area 12) and budget 7 + OK: nation 1 only; d) reopened it shows them, 15 % + OK -> the game's message box,
  the dialog stays open, nothing written, WM_CLOSE leaves cleanly; c) host: no log line, nation 0 shown, budget 9 + OK
  changes nation 0 only; after the month the host has 7 % / Low for nation 1 and its own 9 % for nation 0, the client
  keeps its values and gets the host's nation 0, its dialog shows nation 1, the AI skip line for nation 1. 22 checks
  (+ 8 month checks when it processes its own month). The host's nation 0 goes back to its own % afterwards (`nfield`).
- Results (0.2.8): `--only research` 37/37 (session 7, research 30) in 43 s; full suite 368/368 (session 7,
  intel_reports 10, file_lock 7, events 32, merge 17, research 22, doctrine 17, design_study 31, diplomacy 68,
  victory_points 50, treaty 73, player_copy 34), 8 processed months (11-20 s each) + the copy's month (12 s), game open
  322 s.
- Observed: nation 1's research % is 13 in the fixture (set while it was an AI nation) and 14 after `merge`'s `nfield`;
  the dialog's `seResearchBudget` (MaxValue 12) shows "12" for both, WM_CLOSE keeps 13/14, OK would write 12. The
  over-budget box is a `TMessageForm` "Rule the Waves III" [OK] (its text is a TLabel, unreadable).
- No mod bug found. The first `--only research` run failed 3 of 30 checks because of a wrong assumption in the test:
  list row i is not area i. The list shows only the areas the era offers (1900: 0-10, 12, 13; 11 Anti submarine warfare
  is not offered yet) plus a "Naval guns" row (the calibers). The test now maps rows by name through the game's own
  `Data\ResearchAreas3.dat` headers (`[Explosive shells 12]`, "(Adv)" stripped) and doesn't compare "Naval guns".
- Gotchas: `bnMedium`/`bnLow` are "All medium"/"All low" (every area); one area changes only through the list's
  right-click menu: select the row with LVM_SETITEMSTATE, post `WM_CONTEXTMENU` (lParam -1) to the list, then post
  VK_DOWN/VK_RETURN to the `#32768` menu window (the menu loop takes them; MN_GETHMENU + GetMenuState MF_HILITE to see
  the highlight). Sync bridge commands work while the modal dialog is open (the dispatcher drains re-entrantly).

## Investigation 2026-10-14: tactical battles (joined players vs AI, live-lockstep PvP) (v0.3.0)
- Question: can joined players fight their battles on their own PC, and can two players fight one battle in lockstep
  (same start state + seed, only orders travel, shared clock, per-minute checksum, resync)? Research only, no player
  change; POC behind `poc*` bridge commands (`src/mod/poc_battle.cpp`, nothing runs without them). Full report with
  addresses and the design in the caller's session folder (`battle-investigation.md`).
- Campaign side (1.01.44, RVAs):
  - `CheckForBattles2` (0x4bcb10) runs in DoTurn2 after AreaMoveShips, not in DoEndOfTurn: at most one player battle a
    month (invasion, raider, area/enemy rolls → CheckAttackLocation → DoHomeAreaBattle/DoPossessionBattle → DoBattle).
  - `DoBattle` (0x483b24; ECX = a 0x207-byte TWarMission from `PlayerNation.WarInfo + (opp-1)*0x1665 + 4 + m*0x207`):
    DoAutosaveBeforeBattle, CreateForm(TfrmMain) (FormCreate calls Randomize), setup 0x3abccc → side modifiers
    0x3ad8dc (side 0 from **GetBNation(0)**), generation 0x3b2de0 (TdlgBattlePreview, ends with `TfrmMain.Execute('BC')`
    = `Show`, non-modal), `frmBuildCamp.Hide`. The month continues from `TfrmMain.FormClose` → `DoStrategicTurn`.
  - End: EndOfTurnProcedures' end checks → SinkAllSinking 0x586e18 → ShowResultScreen 0x594154 → 0x5bc0e0 calls
    `TransferCampaignShipDamage(ship, LossRisk)` for every ship (FindCShipWithId by CShipRefId; log entries, Fate,
    officers, SinkShip, `SetRepairTime` from damage %) → HandleBattleAftermath (VP, prestige). Damage becomes RepairTime.
  - Setup and aftermath use PlayerNation ~100 times and GetBNation(0) in the side modifiers, ResetUsedShips and the
    result text: a joined player's battle needs K as nation 0 → fight in a battle slot made with the 0.2.7 Copy-game
    transform, not with a long-lived in-memory swap.
  - Joined-vs-AI battles: DoStrategicTurn → CheckAIvsAIBattles → CheckAivsAIBattleInArea → DoBattleBetweenAINations
    (synchronous, modal "Firing.jpg" on the host). Intercept by recording a request and resolving it in a battle phase
    after DoEndOfTurn; blocking inside with a message pump is not recommended.
- Battle engine:
  - Minute = `StartMinute` 0x57b630 (MinutesLeft--, Finished=0) → `tmrMoveIntervalTimer` intervals (MoveTorpedoes
    0x575ee0 only at speed <= 2, MoveAllShips 0x55c2f4) → `EndOfTurnProcedures` 0x57f83c (sighting, gunnery, AI, breaks
    = PauseGame, Finished=1, autosave every prefs+0x46 minutes, end conditions). `tmrRunMinuteTimer` starts a minute
    when MinutesLeft > 0 and Finished; `StartRun(n)` 0x57e9e0 is behind every Run button (Run5 = 5 if Engaged else 60).
  - **Game speed is simulation state**: GameSpeed -3..5 sets the timers and the seconds per interval (1..240 → 240..1
    intervals a minute, global after `GetTravelDistInGU`); travel is rounded per interval, torpedoes move only at
    speed <= 2 (torpedoes in the water cap it). Speed 2 vs 1 diverged at minute 1 in the POC.
  - All randomness: `Random(n)` 0x7834 → `[Random32Proc]` (the only user of the cell; default = Delphi LCG on RandSeed).
  - The human side global (`PlayerSide`, 355 uses) is simulation-relevant (ResolveMissileHit draws Random behind it):
    flipping it on one copy desynced at the next minute. ForceIsHumanControlled/SeenByHumanControlledShips mostly gate
    message text. Both sides human at once works.
  - Display code draws Random: GlobeRender helpers, Globe2Render/tvShipsChange → GetTypeAndNameForEnemy →
    IdentifyType/IdentifyClass (lazy identification), PlaySoundEffect, FillShipTree/FillReportList inside EOTP.
  - `.sac` (SaveScenario 0x5a1010): UTF-8 INI, ~230 KB/49 ships, 0.4 s; positions rounded to 1", no targets or RNG,
    absolute FlagFile paths. Resume = LoadBuildCampaign with ScenarioInProgress → LoadScenarioInProgress →
    TfrmMain.Execute(file), 3.3 s, 6,454 draws.
- POC results (slot 1 fixture, test battle GB vs Germany "Fleet Battle", 49 ships, seed 12345):
  - Seeded RNG with display draws on a separate stream: identical per-minute state hashes over 30 minutes in separate
    launches, with 10- or 1-minute steps and with repaints/resizes; two game copies at once, gated and released minute
    by minute: 25/25 identical. A single shared stream + display activity diverged at minute 28.
  - Orders as state at a boundary (`pocdiv` course/speed, `pocsideai` side control) on both copies: identical; the same
    order on one copy only: caught at the next boundary.
  - `tmrRunMinuteTimer` gate: the minute is held (bridge ping 15 ms), the simulation is unchanged.
  - Two fresh processes resuming the same battle save with the same seed: identical after load and 10 more minutes
    (resync model); the resumed state differs from the saved one (lossy).
  - `pocplayerside 1` shows side 1's view (own ships named) but changes the simulation: swap it only around display code.
- Gotchas:
  - `Finished` is 0 before the first run of a battle; a driver waiting for Finished=1 hung (10 min of game-open time).
  - The third dword of an empty TPointLL (DivisionObjective) is uninitialised: hash lat/long only.
  - The list behind the cell at RVA 0x6f79d8 is the battle's all-ships list, not the air formations (those are the list `mov ebp, var` before
    `TAirFormation.SaveMe`).
  - Never pipe a driver into `Select-Object -First`: PowerShell kills the Python process and skips its restore
    (slot 1 had to be restored from the backup by hand once).
  - Risks not testable on one PC: x87 transcendental results across CPUs (trig is centralised in 7 helpers: SinCos,
    ArcSin/Cos, ArcTan2, angle norm, Sin, Cos, globe distance; inline fsin in GetHitChance/GetBombHitChance) and the
    preferences record (pointer cell at RVA 0x6f78b4; +0x2f, +0x46, +0x56, AI and realism levels gate simulation code), which two
    copies on one PC share through config.ini.
- Validation: build clean; `--only session,merge` 24/24. Game open ~20.6 min in total (including 10 min of the hung
  first run).

## Testing 2026-10-15: joined players' battles vs AI (v0.3.0)
*A development build of 0.3.0, made after 0.2.8; its version string was still 0.2.8.*
- Build under test: a frozen snapshot of the WIP build (outside the repo; another agent kept developing meanwhile),
  installed into the game folder; the runner's new `--dist <folder>` (or env `RTW3_DIST`) compares the installed DLLs
  with it instead of `dist\` (default unchanged; the runner still never installs).
- New scenario `battles` (after `diplomacy`, before `victory_points`): war 1-Spain by `settension 1 7 50` (Spain: the
  AI nation with a possession, Northwestern Spain, in Northern Europe where Germany has 33 ships); injected battles
  (`battles inject 1 7 Northern Europe`) and the battles the game finds by itself are driven on the client with posted
  messages only (`BattleDriver`): map message Accept, preview Accept/Decline/held, the battle screen's auto-run (a
  posted click on the TSpeedButton's panel, its `TdlgEventAnswer` "Yes"), results "Exit battle". Checks: the battle
  phase (status, lock, `battles list`), the battle slot (slot 87, playerIdx 0, nation 0 Germany), the client's and the
  host's log lines, the merge cross-checked field by field against Game87's save (read at `battle N: fought`) in the
  host's merged save (read at `battle N applied`), VP/Prestige = sent + deltas, counts, the new month on both copies;
  a declined battle, a battle held past `battles timeout 20`, the opt-out after a rejoin with `fightbattles 0`. 38 checks
  + 8 per processed month (usually 3 months). Also: Game87 must not exist before a run and is deleted after it.
- Results: `--only battles` 68/69 (3 months, game open 130 s); full run 437/438 (every other scenario passes: 368/368;
  game open 599 s with a random host loop the harness now handles). A fought battle (Battle of Ferrol, 33 s on auto-run):
  `battle 1 applied: 18 ships changed (128 fields), 0 removed, VP 136/106, prestige 0`; e.g. Victoria Louise CrewQuality
  9 -> 14, RepairTime 0 -> 1, BattleStars 0 -> 1, log entries 2 -> 6 in Game87, in the merged save and after the month.
- Mod issue: a battle declined in the game's preview is reported as `nobattle`, not `declined` (client `battle N:
  nobattle`, chat "No battle came about"; host `battle N: resolved by the game (no battle came about)`). The game's
  `CheckAttackLocation` returns DoPossessionBattle's result = DoBattle's, which is False after a declined preview, so
  `StartPlayerBattle` returns false and `ClientStartBattle` takes the `nobattle` branch; its `declined` branch (True
  without a battle screen) does not happen. The outcome (the host lets the game resolve it) is right; the reason, the
  log and both chat lines are wrong. Fix idea: note that DoBattle was entered (it creates TfrmMain before the preview;
  a DoBattle or TdlgBattlePreview.Execute hook that counts calls in battle slot mode) and report `declined` when it was
  entered and no battle screen is up. Related, not verified: the game's own decline consequences (the battle slot's)
  are discarded, so a player loses nothing by declining.
- *Note 2026-10-16: fixed in 0.3.0. The `DoBattle` hook counts the game's battle setups, so a declined preview is
  reported as `declined` (see the [feature entry](#feature-2026-10-15-joined-players-fight-their-battles-against-ai-nations-on-their-own-pc-v030),
  client step 4); the `battles` scenario passed 70/70 in the full default run of ebfca72.*
- Observed: the AI avoids about 2 of 3 injected battles after the map message's Accept (a "Rule the Waves III" box,
  `StartPlayerBattle ... -> none`), so the scenario injects two (fight) or three (decline, hold) candidates a month.
  The game finds Germany-Spain battles by itself in most war months (`battle request N: nation 7 vs 1 in Northern
  Europe (14:29 ships), fought by Captain`, also in the Mediterranean); a preview's Decline is sometimes disabled (also
  for an injected battle), then the battle must be fought. The game's AI peace ended the war once at a month's end
  (deferred battles of that month were still sent and resolved afterwards: they run after DoEndOfTurn). After a time
  limit the host sends the next battle while the client still shows the old one. The "Add to history record" prompt
  never appeared with auto-run. Client private memory +2.5-3 MB per campaign load (8 battles: +43 MB).
- Harness fixes for random host dialogs that never came up before: an event answer that needs a nation
  (`cbSelectNation`, "negotiating an agreement with") and "Select Officer" (`TdlgSelectOfficer`) looped forever on
  OK; `answer_dialogs` now picks the first nation / officer first.

## Feature 2026-10-15: joined players fight their battles against AI nations on their own PC (v0.3.0)
- Request: every player fights tactical battles (after the investigation of 2026-10-14, idea 1).
- Host:
  - The `d_AIBattleDefer` thunk on `DoBattleBetweenAINations(Self, A, B, CountA, CountB, Area)` (ret 12, Boolean),
    chained in front of the VP thunk `d_AIBattle`, asks `hookcb::OnAIBattle`.
  - A battle of a connected joined player who fights battles (hello `battles=1`, the "Fight my battles"
    checkbox) against an AI nation is recorded (`BattleReq`) and returns True. CheckAIvsAIBattles then stops looking
    for another battle of that pair, as after a fought one.
  - After DoEndOfTurn, the battle phase runs before ApplyDiplomacy/BroadcastState:
    1. SaveGame and lock the strategic screen (`SetStrategicEnabled(false)`; the Tick waits only for modal dialogs).
    2. `MSG_BATTLE` (header + the slot bundle), one battle per player at a time.
    3. Each `MSG_BATTLE_RESULT` is merged into the save file at once, so the player's next battle is built from it.
    4. Reload, then the game's own resolution (`ResolveAIBattle`, the original routine through `g_noDefer` in a
       `__try` helper) for battles that were declined, not fought, timed out (30 min) or whose player disconnected or
       reconnected (peer changed).
- Client:
  1. The battle slot (client slot + 10 = 87) gets the month's files with `MakeNationPlayer(K)` (the Copy-game
     transform, so K is nation 0 and every PlayerNation/index-0 use is right) and is loaded.
  2. The battle starts through the game's `CheckAttackLocation(Self, attacker, defender, area)` (ret 4), which shows
     the preview and then the non-modal battle screen.
  3. In battle-slot mode, hooks skip `DoStrategicTurn` and `AdvanceDesignStudies`, which the battle screen's
     FormClose runs.
  4. The `DoBattle` hook counts setups, so a declined preview ("declined") is told from no battle ("nobattle").
  5. After the result screen: SaveGame, `ExtractBattleResult` (the ship/loss/coastal sections of K, J and allied
     nations, with BuildingNationIdx mapped back, plus the deltas of K's VP/Prestige/FleetMorale and of J's VP from
     EnemyVP), the result is sent and slot 77 is reloaded.
- Merge (`ApplyBattleResult`): field by field against the sent snapshot.
  - Ships missing from the result were sunk.
  - Intel fields (PlayerIntel, EnemyClassName/Speed/Belt) are never taken.
  - Only `allowed` nations are merged (K, J and AI nations).
  - Nothing changes unless everything applies.
  - ShipCount and CACount are recounted.
- Robustness (review): battle-slot mode is cleared on disconnect/Leave/Join; a quick reconnect counts as a disconnect;
  the hooks are required for the feature; game calls go through `__try` helpers.
- Bridge: `battles list|inject K J area|resolve id|all|timeout s|state`, `fightbattles 0|1`; env
  `RTW3MP_BATTLE_TIMEOUT`, `RTW3MP_BATTLE_SLOT`.

## Feature 2026-10-15: live player-vs-player battles in lockstep (v0.3.0)
- Request: battles between players fought by both players simultaneously (design 2b of the investigation).
  Implemented by the research agent in `src/mod/lockstep.cpp`; the full design is in its report (outside the repo).
- Roles:
  - A = the authority, side 0: the host for its own battle against a joined player (`DoBattle` from CheckForBattles2;
    the `d_DoBattle` hook calls `hookcb::OnDoBattleDone`), or the first player's PC for two joined players (deferred
    like idea 1; the host relays `MSG_LOCKSTEP` by battle id).
  - B = side 1.
- Start:
  1. A holds the battle at minute 0 and saves it (SaveScenario `.sac` + SaveBuildCampaign with ScenarioInProgress).
  2. It sends the slot's files, the seed, the speed, the battle-relevant preferences offsets and its x87 control word.
  3. Both PCs (A too) recreate the battle form and load through `LoadCampaign` (the game's resume path) with the
     seeded RNG, then compare READY hashes.
- Determinism:
  - `Random32Proc` is hooked: simulation draws use the shared seeded stream, display draws a local one.
  - Context thunks mark the simulation routines (StartMinute, MoveTorpedoes, MoveAllShips, EndOfTurnProcedures,
    DetachShip, ...) and the display routines (GlobeRender, FillShipTree, ...).
  - The display wrote simulation fields: `Hidden`, the identification and the sighting fields (`SightingLevel`,
    `RadarSightingLevel`, `LastSighted`, `SeenByHumanControlledShips`). Each ship keeps a simulation set and a
    display set, swapped with PlayerSide.
  - `ls leakcheck` flags any simulation change made between steps.
- Per minute:
  - A minute-start gate.
  - Orders are state-synced: an order guard snapshots the order fields after each step, sends the local side's
    changes as numbered batches and puts the agreed value back until the release.
  - Detach actions are replayed.
  - A owns the clock (run/pause/speed/end are requests).
  - The hash per boundary includes the RNG state. A mismatch → resync from A's snapshot. More than 3 resyncs without
    10 good minutes → the AI takes side 1.
- Dialogs inside a minute: "keep the battle going" on both PCs. The end needs both players' End (the game's own
  400-minute / enemy-near rule applies).
- Failure handling:
  - B silent ≥ 30 s at a boundary (5 min while busy) or disconnected → A hands side 1 to the AI and continues alone.
  - A lost → B leaves and reloads slot 77.
  - Watchdog for a minute that never finishes.
  - Forms are freed only from a clean stack (outermost message loop, no menu or move loop, no capture).
  - Flags are exception-safe.
- Not available in live battles (chat line): air strikes, routine air ops, missiles, manual torpedoes, emergency
  activation, change force, try for home, auto-run, save.
  - *Correction 2026-10-16: emergency activation is dead code in the game (its button is never shown and its click
    handler is empty; no blocker) and try for home is a developer menu item that the game hides in every battle.
    Change lead, battle turn-away and Fastest available were blocked too; the speed arrows were not blocked. See
    [the limits of live battles](#investigation-2026-10-16-the-limits-of-live-battles-static-v031).*
- Bridge: `ls status|enable|session|timeout|desync|drop|resync|trace|leakcheck|guard|dispwrite|throw|stall|nest|...`,
  `battles injectpvp`; env `RTW3MP_LIVE_BATTLES=0`.

## Testing 2026-10-15: live PvP battles (lockstep) stress (v0.3.0)
- Build under test: c06860f's `dist\` (a frozen copy given to the runner with `--dist`). Two to three game copies on
  one PC, driven by the bridge and posted messages only. Game open in all: ~108 min (5 runs).
- Runner (`tools/regression.py`; the scenarios `lockstep`, `lockstep_pvp`, `lockstep_month`, `lockstep_stress` and
  `--stress` were started by an earlier regression agent and completed here):
  - The leak check (`ls leakcheck 1`) is on in every copy for every live battle of the suite (`ls_leakcheck`: at
    the start of each lockstep scenario, for a copy started again, for the third copy); `ls_log_scan` asserts no
    `lockstep: LEAK` line (only the lines the guard's negative control provokes are excused).
  - `lockstep` (default run, short): + a detach by the client replayed on the host, + `ls dispwrite` on each copy
    kept out of the simulation by the sighting guard.
  - `lockstep_stress` (`--stress`): + desync storms injected on the client and on the host (4 each: one DESYNC per
    injected minute, the minute after each resync equal including the RNG part, the 4th hands side 1 to the AI),
    + a client that never loads (15-s load timeout), + S7: `ls throw`, `ls stall`, `ls nest 8` on each copy and the
    guard's negative control; the long battle f) now has every division on the AI.
  - After-checks: slot 1's and slot 77's saves hold the copies' months. Driver fixes: after a refused agreed end
    30 minutes are run with `ls_run_total` (battle breaks stop single runs after a minute), up to 20 end requests;
    the spam ignores orders to divisions that no longer exist; MSLost (the month's trade war) is not a battle loss;
    an `agree` battle may also be ended by the game itself; a `game` battle that reaches the 900-minute cap ends by
    agreement.
- Results (c06860f):
  - `--only lockstep` 31/31; full default run 522/525 (863 s); full `--stress` run 615/619 (3005 s);
    `--only lockstep_month,lockstep_stress --stress` 136/141 and `--only lockstep_month --stress` 60/61 while fixing.
    Every failure left is a mod issue below, the `battles` c/d timing race (once: a "decline" candidate whose Decline
    was disabled was fought for 71 s under `battles timeout 20`, the queued ones timed out meanwhile), or a test
    expectation fixed since.
  - The host's battle against the joined player inside a real processed month (CheckForBattles2 -> DoBattle ->
    held -> sent -> the client loads it from slot 87 as B -> live -> end -> FormClose -> DoStrategicTurn ->
    DoEndOfTurn -> the idea-1 battle phase -> the new month to the client) worked in 8 months: 424, 451, 424, 904,
    499 and 529 minutes live with every minute's hash equal, plus 12 live minutes then the AI (default), plus the
    client killed in the middle of a minute. Each: one DoEndOfTurn, one month package, no "turn did not advance", the
    client's plans of the month kept, ships/battle fields/losses of both nations equal in the host's save and slot 77,
    VP equal (e.g. "victory points: nation 1 +198 in its battle against the host"), sunk ships in both ("Sunk in
    battle off Northwestern Spain with Spanish ships"); the idea-1 battles of the same month ran after it.
  - No natural DESYNC and no LEAK in any run (thousands of live minutes; the spam: ~100 commands per copy in 40 s,
    three times). Joined vs joined: two live battles in one month, roles both ways, 424 minutes each.
  - Bad states that pass: spam; end requests during a resync, during the load, right after a drop; desync storms;
    silent client/host; a client that never loads; exceptions, stalled minutes, nested message loops; the client
    killed at a boundary and mid-minute (dropped in 0.1-1.1 s, the host's battle and month go on); the host killed
    mid-minute (the client back in slot 77 in 0.8 s, slot mode off, strategic screen and Turn button enabled,
    Leave/Join work, a new session from the restored slot 1); the opt-outs `fightbattles 0` and
    `RTW3MP_LIVE_BATTLES=0` (the host fights its battle against the AI, no lockstep).
  - Memory: every campaign load costs ~3-4 MB private memory (a live battle loads once at its start and once per
    resync on both copies): nine stress battles in a row took the host 91 -> 179 MB and the client 103 -> 203 MB in
    one run, 156 -> 258 / 227 -> 344 MB in the full `--stress` run (S7 with ~7 resyncs: +25-32 MB each). Linear, not
    levelling off: a risk for very long sessions of the 32-bit game (the game's own per-load growth, multiplied by
    battles and resyncs). Reported, not asserted beyond <= 25 MB per battle on average.
- Mod issue 1 (medium): a live battle that the game ends by itself INSIDE a minute in a real month runs the host's
  month inside the battle's simulation step. Seen twice in the `--stress` run (battles 100001 and 100002: "No opposing
  ships are in sight of each other and scenario length is reached!" during a run). The host's results "Exit battle"
  closes its battle screen inside that minute's EndOfTurnProcedures, so FormClose -> DoBattlePrestigeLoss ->
  DoStrategicTurn run nested in the lockstep's K_SIM frame: their boxes go through the minute-box policy ("minute box
  'The Prime Minister is annoyed that you cannot safeguard our coasts with adequate patrols'", "picture message 'War
  budget means increased allocations to the navy!' (Parliament.jpg) skipped") and into the chat as "Battle: ...";
  Yes/No questions of the month would get the battle's fixed answers; the month's random draws come from the battle
  stream; "WARNING a network message handled inside a simulation step (EndOfTurnProcedures, 1 frames)"; the host logs
  "live battle ended (abandoned): the battle screen was closed" while the client logs "The battle is over (the battle
  is over)". The month itself completes correctly. Test battles (battle slot mode: FormClose returns at once) and
  agreed ends at a boundary are not affected. Fix idea: when ShowResultScreen runs with EndGame set inside a step,
  end the live battle normally there (send `done end=1`, leave the K_SIM frames/minute policy and the shared RNG for
  everything nested under the results), or defer the battle form's close (post it) until EndOfTurnProcedures returns,
  so FormClose runs from the outermost message loop.
- Mod issue 2 (medium): a joined player who rejoins while the host's battle screen is open gets no nation, neither
  then nor after the battle/month (`S6`, `P2`, also seen by the earlier agent on e674bf5). The hello handler gives a
  returning player its reserved nation only `if (CampaignLoaded())`, which is false while the battle hides the
  strategic screen; `SyncHostCampaign` re-matches reservations only in the lobby phase; so the player stays at nation
  -1, the new month is not sent to it, and `claim 1` by index does nothing (its lobby list is empty); only `claim
  <name>` after the host's battle works. Fix idea: use a campaign check that does not need the visible strategic
  screen in the hello path (e.g. host slot set and NationCount >= 9), or re-run the reservation match in Tick for
  welcomed players without a nation once `CampaignLoaded()` is true again, then send the month (`SendStateTo`).
- *Note 2026-10-16: both mod issues above were fixed in 0.3.0; see "Confirmed fixed in 0.3.0" in
  [Testing 2026-10-15: release candidate](#testing-2026-10-15-release-candidate-v030).*
- Observations: after a test battle slot 1 holds the live battle's save (`ScenarioInProgress=1`) until the next month
  saves it; a host battle in which nation 1 scores no VP logs no "in its battle against the host" line (the month's
  EnemyVP still reaches it through the share line).

## Testing 2026-10-15: release candidate (v0.3.0)
- Build under test: main a1a8484 (`dist\`, a frozen copy given with `--dist`), installed; both logs show `RTW3MP 0.3.0
  starting (protocol 3)` (new `session` check against `src/mod/common.h`). Game open: ~70 min in five runs.
- Runner changes (`tools/regression.py`):
  - `lockstep_month --stress`: the second real-month battle is `endsoon` (after 6 live minutes A sends the new `ls
    endsoon`; the game's "Scenario absolute time limit is reached!" ends the battle inside the next minute) instead of
    `game`. New check of the deferred close on both PCs: `"Exit battle" in the results: the battle screen closes after
    this minute` -> "The battle is over" -> "live battle ended (normal)" -> `closing the battle screen`; the host's month
    after it with the game's own dialogs and no minute-box/"Battle: ..."/WARNING line before "DoEndOfTurn finished".
  - Rejoin during the host's battle (`lockstep_stress` S6 a, P2 a) now expects the nation back, with the evidence of
    the path (`rejoin_back`: the hello, myNation 1 during the battle and "* Captain joined as Germany", or the host's
    `reservation: Captain gets nation 1 back` and "* Captain is back as Germany").
  - Losses step by step: `StageWatcher` (a 0.1-s thread in `ls_month`) reads the host's save at `battle phase:`, after
    each `battle N applied` and Game87 after each `battle N: fought` (Game87 is rewritten ~0.3 s later). The live
    battle's losses are checked from its start to the battle phase start; a lost ship is one that is gone or whose
    Fate is no longer "XXX" (taking part or not); counters of types without campaign ships (MSLost, AUXLost) are only
    reported; new h2) every idea-1 merge of the month: merged [NationKLosses]/[NationJLosses] = Game87's.
- Investigation of the research run's open failure (`lockstep_month`, "losses come with sunk ships", nation 1 DDLost
  0 -> 1): test attribution, not a mod bug. Reproduced twice with the step-by-step saves: the live battle + DoEndOfTurn
  and every idea-1 merge had no losses (merged Losses equal to the battle slot's), the loss came after the last merge
  from the game's own resolution of an idea-1 battle the player did not fight ("battle 3 resolved by the game:
  battle" after "no battle came about"; in the research run a declined one): DD V2 "Sunk in combat with Spanish
  forces." (in --stress also Lübeck, G8, V3, Preussen: BLost/CLLost/DDLost +4 = 4 ships). The old check compared the
  live battle's save with the save after the whole month, and counted a sunk ship only if it took part (the game's
  AI-vs-AI resolution writes no battle star or "took part" entry). Sunk ships stay in the list with their Fate (idea-1
  merges log `0 removed`).
- Results: `--only lockstep_month --stress` 65/65 (402 s); full default 509/510 (826 s; the fail: the drop12 battle's
  [Nation0Losses] AUXLost 0 -> 5 without a ship, a counter without campaign ships like MSLost: test bug, fixed;
  `--only lockstep_month` 41/41 afterwards); full `--stress` 622/625 (2738 s): every lockstep scenario passes
  (lockstep_month 57/57, lockstep_stress 90/90 with S6 a/P2 a, lockstep_pvp 23/23), the 3 fails are `research` below
  (`--only research,doctrine,design_study` 78/78 on the rerun).
- Mod issue (low-medium): a joined player's research priorities can be re-rolled during the host's month. The game's
  `TfrmBuildCamp.DoResearch(aNationIdx)` (0xc9225c) runs an AI branch for every nation index > 0 (0xc922bb `cmp
  [aNationIdx], 0; jle`): with Random(20) == 1 (5 % a month) it sets every area's Prio (+0x326 in the 0x35-byte
  ResearchAreas records) to Medium (80 %) or Random(3)+1 (0xc92356-0xc923b4). The mod suppresses
  DetermineAINationResearch only. Seen once: after the `design_study 1` month nation 1 kept ResearchPct 7 but every
  Prio was 2 on host and client (wanted 1,..,3,..,1,..,3). Fix idea: hook DoResearch and, for joined/reserved nations,
  restore Prio after the call (or skip the 0xc92356 block), keeping the rest of DoResearch.
- Confirmed fixed in 0.3.0: the rejoin during the host's battle (both kill cases get nation 1 at once by the hello),
  the game's end inside a minute (deferred close, normal end on both, normal host dialogs; also seen for an agreed
  end), seen in 4 real-month battles.
- Follow-up on ebfca72 (the DoResearch hook for the research finding above; `dist\` frozen and installed; game open
  285 + 777 s):
  - `research` e): host `researchrun 1 100` (new async bridge command: the game's DoResearch for a nation `count` times
    through the hook) after the month in which nation 1 got the client's pattern (1,2,..,3,2,1,..,3: areas 0, 10, 12,
    15): `before` == `after` == that pattern, and the hook logged 8 and 6 `research priorities of player nation 1 kept
    (N changed by the game's AI choices)` lines in the two runs (N up to 10 = a full re-roll). Control `researchrun 8
    100` (AI Austria-Hungary): 2 and 1 areas changed, no `kept` line. A real month also logged `research priorities of
    player nation 2 kept (1 changed ...)` for the reserved stand-in nation.
  - `--only research,doctrine,design_study,battles,lockstep`: 172/174. The 2 fails were the `battles` c/d race again,
    a test bug, fixed: the "decline" candidate battle 7 had Decline disabled, was fought for 37 s under `battles timeout
    20` and got the client notice "The host resolved this battle without you (time limit)"; the driver then released
    the held battle 10 at once on that old notice (it was declined, not timed out). The driver now releases a held
    preview only on a notice logged after the held battle became the current one, and d) reads only notices after
    "battle N sent".
  - Full default run: **520/520** (16 months, game open 777 s; research 24/24, battles 70/70).

## Fix 2026-10-15: the game's AI research choices reset joined players' research priorities (v0.3.0)
- Found by the v0.3.0 release-candidate regression run: after a month, nation 1 kept its research budget, but every
  priority was back to Medium (intermittent).
- Game model (1.01.44): `TfrmBuildCamp.DoResearch(Self, aNationIdx, NoNonGunmessages)` (0xc9225c, register arguments,
  plain ret) runs an AI branch for every nation index above 0.
  - It sets some priorities to High by chance (records at +0x3c5, +0x503, +0x538, +0x56d; Japan +0x503).
  - With `Random(20) == 1` (5 % a month) it re-rolls every priority (mostly Medium; 0xc92356-0xc923b4).
  - The mod's AI skip only covered `DetermineAINationResearch`, so this branch still ran for joined players.
- Fix: a `DoResearch` hook. For a joined player's nation it snapshots the 25 Prio values before the game's routine and
  puts them back after; the research progress is untouched (log `research priorities of player nation N kept (C changed
  by the game's AI choices)`).
- Bridge: `researchrun <nation> <count>` (async) runs the game's DoResearch through the hook and logs the priorities
  before and after.

## Docs 2026-10-16: documentation restructured (v0.3.0)
- Request: audit the documentation, restructure it and fix errors; no change to the mod.
- New files: `CHANGELOG.md` (per version, from the release notes), `docs/ARCHITECTURE.md` (the current design),
  `docs/TESTING.md` (how to run the suite, the environment variables, the tools inventory; the bridge command
  reference, the scenario catalog and the known traps moved there from the regression agent file),
  `docs/GAME-INTERNALS.md` (the game facts and the battle knowledge moved there from the research agent file),
  `.github/copilot-instructions.md`, the release texts `extras/RTW3MP_README.txt` (with a `{VERSION}` placeholder) and
  `extras/RTW3MP_LICENSES.txt`, and `tools/package_release.ps1`.
- README reorganized: contents, requirements, a version-agnostic install, uninstall, **Fight my battles** and Copy game
  when joining, the current limitations, troubleshooting with an upgrade section, building and tests.
- This log: a header, an index, entry headings in one format; the 0.1.0 design and status sections marked as
  historical.
- Corrections, each checked in the source:
  - The README and the release README said the mod never modifies "your normal save slots". The host's campaign slot
    is saved and merged into every month (`RunHostTurn` -> `saveio::MergeSubmission`; also `rtw3mp_session.ini`), and
    joined players use slots 77 and 87. Both texts now say so.
  - The README said the mod checks that everyone runs the same game build and mod version. The host compares the
    protocol (`MP_PROTOCOL`) and the game build fingerprint, not the version string (`MSG_HELLO` in `session.cpp`).
  - The README did not say that a live battle needs both players to fight their battles: otherwise the host fights
    its battle against the computer, and a battle between two joined players' nations is resolved by the game
    (`HostOnBattleStarted`, `HostDeferPvpBattle`).
  - The README said the hosting video shows 0.1's window "before the Diplomacy list was added"; it also lacks the
    **Fight my battles** checkbox. It also called the host's nation "nation #1" (it is the save's player nation,
    index 0), and its list of test scenarios was still the one of 0.2.1.
  - The research agent said all save files are UTF-8 INI with CRLF; `DesignFiles<i>.des` is positional text (see the
    save format entry above).
  - The regression agent's trap "a declined preview is reported as `nobattle`" no longer holds since 0.3.0 (the
    `DoBattle` setup count); its bridge reference lacked `release`, `limit`, `chat`, `setplayer`, `savedir` and some
    `status` fields.
  - The README's caption **Claim land…** is **Claim land...** in the mod (`ui.cpp`).
  - The hosting video's entry was dated 2026-10-05; it is from 2026-10-04 (corrected above).
- Packaging check: `tools/package_release.ps1` run against the 0.3.0 `dist\` gives the five entry names of the 0.3.0
  release zip in the same order. The DLLs and `RTW3MP_LICENSES.txt` are byte-identical; `RTW3MP_SecondCopy.bat` differs
  only in its line endings (now CRLF throughout), `RTW3MP_README.txt` only in the intended edits (a CHANGELOG pointer,
  the uninstall note) and its line endings.
- Later the same day, the user's decisions: the mod's code is under the MIT license (`LICENSE`; `RTW3MP_LICENSES.txt`
  now starts with it), and the 0.1 test scripts `live_lobby_test.py`, `repro_issues.py`, `run_turn.py`,
  `verify_tabs.py`, `ui_buildship.py` and the wrapper `diplomacy_test.py` were removed from `tools/` (the entries
  above still mention them).

## Testing 2026-10-16: war score of live battles between joined players (v0.3.0)
- Suspected mod issue, found by reading the code: the war score of a war between two players (the host's per-pair
  ledger behind the peace terms: `warvp`, `terms`, the outlook, the treaty level) is fed only by the host's battle
  hooks (`hookcb::OnBattleVP` from HandleBattleAftermath, DoBattleBetweenAINations, QuickResolveRaiderBattle). Since
  0.3.0 a battle between two joined players who both fight their battles is fought live on their PCs and merged into
  the host's save (`HostOnBattleResult` -> `saveio::ApplyBattleResult`). The merge adds the VP to both nations' `VP`
  fields but credits nothing to the ledger. Before 0.3.0 the host's game resolved those battles
  (DoBattleBetweenAINations) and the hook credited the ledger.
- Build under test: the released v0.3.0 DLLs (a frozen copy given with `--dist`), installed; both logs `RTW3MP 0.3.0
  starting (protocol 3)`.
- Runner change (`tools/regression.py`): new check `lockstep_pvp` f) (`pvp_war_score_check`).
  - Host `warvp 1 2` before and after the month with the live battle(s).
  - Expected rise: the VP of every live battle 1-2 merged that month (`battle N applied: ... VP a/b`; a = the battle's
    own nation K, the first player's, from `battle request N: nation K vs J ..., fought live by ...`; mapped onto 1, 2)
    plus the host's own credits of the month (`war score 1-2: +x/+y -> A:B`).
  - Also: still at war, the score not restarted (`starts at`, `first battle`); a `SKIP-note` when the live battles gave
    0 VP. A fix's own credit line for a merged battle (e.g. `war score 1-2: +x/+y from a battle fought on a player's PC
    -> A:B`) is not counted twice, so the check passes once the ledger is credited.
- Result: `--only lockstep_pvp` (with `session`) **29/31**, game open 105 s, one processed month (Aug -> Sep 1900).
  **Confirmed.**
  - The injected battle came about: `battle request 1: nation 1 vs 2 in Northern Europe (3:3 ships), fought live by
    Captain and Bot`; Captain A, Bot B, 10 live minutes with equal hashes, then A dropped B and finished on auto-run.
  - Host log: `war score 1-2 starts at 0:0` (at the injection, before the month), `battle phase: 1 battle(s)`, `battle 1
    sent to Captain (1769743 bytes), live against Bot`, `lockstep relay 1 ended: the battle's result arrived`, `battle 1
    applied: 12 ships changed (72 fields), 0 removed, VP 180/147, prestige 0 (player: VP 180/147, prestige 0, morale
    0)`. No `war score 1-2:` line in the month.
  - `warvp 1 2`: `1:0 2:0` before and after the month; expected 180:147. f) fails; the other 22 `lockstep_pvp` checks
    pass.
  - The second fail is `session`'s version check: the tree's `src/mod/common.h` already says 0.3.1 (work in progress on
    main), the installed build is 0.3.0. Expected with a frozen older build (new known trap in docs/TESTING.md).
- The same pattern in an earlier 0.3.0 run's host log (2026-10-15): the game-found live battle 15 (23:29 ships) was
  merged with `VP 140/246`, and the month's only credit was the game's resolution of the unfought injected battle 14:
  `war score 1-2: +2/+12 -> 2:12`.
- Impact: in a war between two joined players who both fight their battles, the peace terms ignore every live battle.
  Only battles the game resolves on the host count: unfought requests, raider battles, and the battles of a pair in
  which a player doesn't fight battles. The nations' own `VP` fields do get the battle's points (the merge, by the
  code; not read in this run).
- Fix direction (for the caller): after the merge in `HostOnBattleResult`, credit the ledger with the merged deltas
  (dVP_K, dVP_J) when both nations are players at war. f) then passes.
- Follow-up on 0.3.1, the fix: after a successful merge `HostOnBattleResult` calls the new `CreditWarScore(K, J,
  dVP_K, dVP_J)`; for two human nations at war it adds the points to the pair's score (host log `war score a-b: +x/+y
  from a battle fought on a player's PC -> A:B`); battles against AI nations are ignored; protocol 3 unchanged. DLLs:
  a frozen copy of main's `dist\` (`--dist`), installed; both logs `RTW3MP 0.3.1 starting (protocol 3)`. Game open
  138 + 225 + 763 s in three runs, every check passed.
  - `--only lockstep_pvp`: **40/40**, two months. Aug -> Sep 1900: the injected battle 1 found "no battle came about"
    and the game resolved it (`war score 1-2: +4/+13 -> 4:13`). Sep -> Oct 1900: requests 2 (injected), 3 and 4 (found
    by the game); 2 and 3 found "no battle came about"; battle 4 (`nation 2 vs 1 ... fought live by Bot and Captain`:
    Bot's copy was A, the roles reversed) was merged with `VP 0/230`, followed by `war score 1-2: +230/+0 from a battle
    fought on a player's PC -> 234:13`; the game's resolutions of 2 and 3 added `+0/+1 -> 234:14` and `+5/+12 ->
    239:26`. f): `warvp 1 2` 4:13 before, 239:26 after = 4:13 + 230:0 (battle 4: K = 2 scored 0, nation 1 scored 230)
    + 0:1 + 5:12. Passes.
  - `--only treaty,victory_points,battles`: **193/193** (battles 62/62, victory_points 50/50, treaty 73/73; 5 months).
    `treaty`'s war-score checks hold as before: b) a new war at 0:0, `warvp 1 2 900 100` -> major for 1; c) the session
    file keeps `warVP 1-2:900:100`; k) the treaty month's lines (no 1-2 battle that month); h) 600:700 -> minor for 2;
    i) `warvp 0 1 900 100` -> major for the host; j) 300:300 -> compromise. They set the score with `warvp` and count
    the host's hook lines only. The idea-1 battle of `battles` (nation 1 vs Spain, `battle 2 applied: ... VP 60/246`)
    gave no war-score line: battles against AI nations stay out.
  - Full default run: **512/512** (16 processed months plus the copy's single-player month, game open 763 s).
    `lockstep_pvp` 23/23: battle 9 (Captain A) merged with `VP 518/193`, `war score 1-2: +518/+193 from a battle fought
    on a player's PC -> 518:193`, `warvp 1 2` 0:0 -> 518:193. `victory_points` g) counted a 1-2 battle the host's game
    resolved (`war score 1-2: +0/+22 -> 0:22`, nation 2 VP 129 -> 151); `treaty` 73/73 (no 1-2 battle in its month); the
    idea-1 merge of `battles` (`VP 190/134` vs Spain) and the host's live battle of `lockstep_month` (`war score 0-1:
    +43/+149 -> 43:149`, the hook path) as before.

## Fix 2026-10-16: live battles between joined players didn't count in their war score (v0.3.1)
- Found by the docs agent while auditing the documentation (code reading); confirmed live by the regression agent on
  v0.3.0 (see the Testing entry above): a live battle between nations 1 and 2 was merged with `VP 180/147`, and
  `warvp 1 2` stayed at `0:0`.
- Cause: the war score of a war between two players (`g_warVP`, the peace terms) is fed by `hookcb::OnBattleVP`, which
  the host's battle hooks call (HandleBattleAftermath, DoBattleBetweenAINations, QuickResolveRaiderBattle). Since
  0.3.0 a battle between two joined players who both fight their battles is fought live on their PCs; the host only
  merges its result into the save file (`HostOnBattleResult` -> `saveio::ApplyBattleResult`), so no hook fires.
  Before 0.3.0 the game resolved such battles on the host and the hook counted them.
- Fix: after a successful merge, `HostOnBattleResult` calls `CreditWarScore(K, J, dVP_K, dVP_J)` with the result's
  deltas. For a pair of human nations at war it adds them to the pair's score. A score created at that moment starts
  from the nations' points in memory, which don't hold the merged battle yet (the campaign is reloaded at the end of
  the battle phase), so the battle is added even then; `OnBattleVP` skips that case because the game has already
  added the points to memory there. Battles against AI nations don't count (not a pair of players). Host log `war
  score a-b: +x/+y from a battle fought on a player's PC -> A:B`.
- Only the host's code changed; protocol 3 is unchanged, so 0.3.0 players can join a 0.3.1 host.

## Investigation 2026-10-16: the limits of live battles (static; v0.3.1)
- Question: what does each battle command that live battles block do in the game, and how could it work in a live
  battle? Three research agents read the 1.01.44 image (air operations; missiles and torpedoes; orders): static
  analysis only, no game run, no code changed. Their reports and the plan the user approved stay outside the repo;
  the durable facts are in [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md#battle-commands). Image addresses (base
  0x830000); "guess" as in the reports.
- Not player features, so there is nothing to lift (the docs listed them):
  - Emergency activation: `TfrmMain.sbEmergencyActClick` (0xdeed28) is a bare `ret`. The button is Visible=False and
    Enabled=False in the form; only `CampaignScenario` (+0x7f4, never set to True in the image) or the unreachable
    operational-campaign module `TfrmCampaign` would show it, and `LoadScenarioInProgress` (0xcd02b4, the resume
    path the live battles use) hides it. The Force orders dialog's `sbEmergencyAct` has no OnClick and is visible
    only for `CampaignScenario`; its label is always shown (guess: where the docs' item came from). No blocker exists
    or is needed.
  - Try for home (`TryforHome1Click` 0xe021e8: the AI's head-home routine 0xdc15ec, then the course in a message box)
    and Enter port (`ShowAircraft1Click` 0xdfabe4 → the "division enters port" routine 0xdeccb0, which removes the
    division from the battle; 14 `Random` sites): `TfrmMain.FormCreate` hides both with the other debug items
    (0xd8ab47..0xd8abde), unconditionally, so in every battle, single player included.
  - No control calls `TfrmMain.bnFireClick` (0xdb7200), `Launchstrike2Click` (0xdfb25c), `Bombship1Click` (0xdfebb0)
    or `TdlgDivisionReport.bnFireMissilesClick` (0xae2dc4); only the RTTI method tables refer to them.
- Blocked, but missing from the docs' list: change lead and battle turn-away (Force orders) and *Fastest available*.
  Not blocked: the speed arrows (`UpDown1Click`), which change the speed at once (problem 3 below).
- Where each real command commits (the routine both PCs would have to run):
  - torpedo salvo: `TfrmMain.FireMount` (0xda8cc4, `ret 0x14`, 10 `Random` sites; `SetReloadTime` 0xe5598c, 7),
    called by `TdlgTorpTargets.bnFireClick` (0xaddf75), whose angle jitter (8 sites) and firing-solution list (about
    15) draw outside the simulation;
  - missile salvo: `TfrmMain.FireMissileMount` (0xe04718, `ret 0x10`) → `FireSSM` (0xda6374), called by
    `TdlgMissileFire.FireMissiles` (0xd80c98), which also writes `PendingMissileTarget` (+0xa93, at 0xd80b79) and
    fires missile submarines with side 0 hard-coded (0xd8109b, 0xd810f7);
  - air strike: LaunchSomeAircraft (0xea4a18) from `TdlgStrike.bnLaunchClick` (0xc294a7) and `bnReadyClick`
    (0xc2b843); `bnLaunchClick` then draws `Random(10000)` for the GroupIndex (0xc294b3) and writes more fields
    inline;
  - routine air operations: `TdlgSearch.bnSaveClick` → 0xb4bef4 (9 `TForce` fields, then
    `TForce.ProcessSearchDirectons`); land-based CAP: `TdlgDivisionReport.bnOkClick` writes `LandCAPDivision[0]`
    (0xfb5920);
  - change force: inline in `Changeforce2Click` (0xd8b9b0); change lead: `SetForceLeadDiv` (0xde42fc); turn-away:
    `DoBattleTurnAway` (0xde4550). None of the three draws `Random`.
- Problems found in the 0.3.x live battles (read from the code, to be confirmed live):
  1. Carriers: the squadron picker of `CheckAIAirstrike` (0xdfd50c; the test at 0xdfdb33) skips PlayerSide's carrier
     squadrons, and PlayerSide is 0 in the simulation: the AI flies side 1's carrier strikes and never side 0's.
     `SetAIAirSearchPlan` in EOTP (0xdafa1f, every 120 minutes after minute 600) resets side 1's search sector and
     CapLevel.
  2. Missiles: EOTP reads *Fire at will* (`cbFireAtWill`, 0xda92d1) and *Fire mode* (`rgMissileDoctrine`, 0xda7d20,
     0xda7d56) for side 0 from each PC's own screen: different settings desync the minutes with missile fire, and the
     resync resets both controls (the battle form is created again). Side 1 always fires at will by the AI's rules.
  3. Game speed: VCL puts the blocked *Fastest available* down before the click handler, and the speed governor reads
     `FDown` at minute boundaries (the cap 0xdea00c, the contact update 0xe02d54); `UpDown1Click` (0xdd00d8) changed
     the speed on one PC, also in the middle of a minute.
  4. Fog of war, air: EOTP refreshes *Show air formations* inside the simulation (0xdb03dc, PlayerSide 0), so B's
     window listed side 0's formations; its list keeps `TAirFormation` pointers across a reload.
  5. Fog of war, missiles: `UpdateMissilePanel` (0xe032c8, EOTP) counts side 0's missile ships and targets, on B's
     screen too.
  - 0.3.2 fixes 3 and 4 (the next entry).
- Gaps: the state hash has no torpedo or missile stocks and reload times, no `PendingMissileTarget`, no submarines,
  no force membership or leads and no force air settings; of the air formations it has only the id, the numbers,
  position, course, status, fuel and altitude. The battle save drops NightCAP, the formations' Altitude and CAP
  links, and `PendingMissileTarget`, so a resync loses them.
- Test beds: the test campaign (slot 1, 1900) has no aircraft. Air operations need a 1935 campaign (the game starts
  campaigns in 1890, 1900, 1920 and 1935), missiles a battle after about 1950.
- The plan (approved on 2026-10-16): the hotfix 0.3.2 and corrected docs first. Then commands that both PCs run at
  the next release ("deferred actions": torpedo and missile salvoes, change force, change lead, turn-away), a
  per-side "player side" inside the simulation (the AI air routines, missile fire), routine air operations as
  orders, air strikes, auto-run per player, and *Save* as the host's crash checkpoint.

## Fix 2026-10-17: speed controls, air formations window and battle dialogs in live battles (v0.3.2)
- Problems 3 and 4 of the investigation above; the reload hazard of 4 also applies to the battle's other dialogs.
  Every change acts on the PC that runs it (no new message or key): protocol 3 is unchanged, and 0.3.1 and 0.3.2
  play together.
- The speed arrows: `TfrmMain.UpDown1Click` gets a blocker with the new policy `B_ARROW`. It computes one step from
  the speed in use by the game's rule (up only if `(speed < 5 and not Engaged) or speed < 2`, down to -3) and sends
  it as the speed request the speed box makes (`ClockRequest("speed")`); A applies it with the next release. The
  speed in use is A's pending or current speed; on B, the speed A announced last (`clockSpeed`, from the start,
  release and info messages). The blocker thunk now also passes EDX (Sender) and ECX to `LS_Block`; ECX is the
  arrow's `Button` (btNext 0, btPrev 1).
- *Fastest available*: the new policy `B_FASTEST` puts the button up again with the game's own routine, the one the
  down arrow calls (found in `UpDown1Click`: `cmp byte ptr [eax+FDown], 0; je +7; mov eax, ebx; call <routine>`,
  which also gives the `FDown` offset), and says "Fastest available is not available in a live battle yet; use the
  speed box." `cbFastestAvailableClick` stays blocked.
- Other blocked toggle buttons: VCL flips a toggle speed button's `FDown` before its click handler runs, so a blocked
  click left *Auto-run* and the division report's *land-based CAP* (`sbLandCAP`, GroupIndex 6) down; the report's
  `bnOkClick` then wrote `LandCAPDivision[0]` (not an order field) on that PC only. `LS_Block` now wraps the decision
  (`LS_BlockImpl`) and, for every blocked click except `B_FASTEST`, calls `PutToggleBack(Sender)`:
  `TSpeedButton.SetDown(not FDown)` when the Sender is a `TSpeedButton` with `FGroupIndex` <> 0 (the offsets from
  RTTI: `TCustomSpeedButton.FGroupIndex` +0x1e0, `FDown` +0x1e8, checked against the `UpDown1Click` pattern; `SetDown`
  from the release routine's first call). The blocked handlers are only called by the VCL (no code references), so a
  put-back always undoes a real toggle.
- The missile panel: the simulation reads `cbFireAtWill.Checked` and `rgMissileDoctrine.ItemIndex` on each PC for
  side 0's ships (problem 2 of the investigation). Each live load creates a new battle form (their defaults: unchecked,
  Ripple), and `LockMissilePanel` disables both (`TControl.SetEnabled(False)`, the VMT slot from
  `sbAutoRunBattleClick`) after every load and at every boundary, so neither player can change them. The asymmetry
  stays until both PCs share these settings (0.4.0): side 1's missile ships fire at will under the AI's rules, side 0's
  only when their force is under AI control and a nation value allows it (`[nation+0x149] < 2`; guess: missile tech).
- *Show air formations*: `TdlgShowAirFormations.UpdateListSortedByBase` is a display context (`K_UI`), so the
  refresh inside EOTP runs with this PC's PlayerSide and display set: B's window lists side 1's formations.
- Dialogs before a load: `ResetBattleForm` (the start, every resync, A's recovery, B's leaving) first hides
  (`TCustomForm.Hide`) each visible one of `TdlgShowAirFormations`, `TdlgDivisionReport`, `TdlgForceOrders`,
  `TdlgSelectLead`, `TdlgTorpTargets`, `TdlgStrike` and `TdlgMissileFire`: they keep pointers to the
  battle's ships, divisions and air formations, which the load frees. Their unit variables are found once by scanning
  the image for `mov eax, [cell]; mov eax, [eax]` whose object has exactly that class's VMT (`FormVarOf`). Log:
  `lockstep: battle dialogs resolved` (with `, not found: <classes>` if any is missing) and `lockstep: <class> hidden
  before the battle is loaded again (ok|FAILED)`. (`TfrmTargets`, shown only by the orphan debug handler
  `TfrmMain.bnFireClick`, has no variable the scan finds and was dropped from the list.)
- The battle's data before a load (found by the regression tests: a 1935 battle never started, "the loaded battles
  differ" with af 27x16 vs 27x15): `ResetBattleForm` freed the form, but the battle's global lists outlived it, and
  the resume path (`sub_ea3f88`) appends the saved air formations to the global list (0xfb0108) without clearing it,
  so every load added another copy. The screen's own close (`TfrmMain.FormClose` 0xdcd67c) clears them after
  `DoStrategicTurn`: `mov cl, 1; mov edx, 2; mov eax, [ebp-4]; call ClearData; call 0xea16ac` (at +0x3d2; ClearData
  frees ships, divisions, forces and more for all sides; 0xea16ac deletes and frees every formation whose +0x22 is 0).
  `ResetBattleForm` now makes the same two calls before it frees the form (resolved from that pattern, the first call
  checked against `TfrmMain.ClearData`). Log: `lockstep: battle data cleared (ok), air formations left <n>`.
- The ship tree inside a minute (found by the regression tests: B's tree showed side 0's forces after every minute):
  the end of a minute refills the tree with `FillShipTree(tvShips, PlayerSide, ...)`, and PlayerSide is the canonical
  0 inside the simulation. `LS_CtxEnter` now replaces the side argument (ECX; the thunk's `popad` passes it on) with
  this PC's own side when `FillShipTree` is entered inside the simulation with side 0.
- The campaign date (found by the regression tests: the host's campaign ended up a month ahead of the joined player's,
  "submission is for another month"; it happens on 0.3.1 too). Year, Month and Day are Word unit variables
  (0xf531e0, 0xf531e4, 0xf531dc); the month advance starts from a separate TDateTime (0xf52fe8) that
  `LoadBuildCampaign` sets from the save's [General] date before it calls `LoadScenarioInProgress`, and the `.sac`
  reader (`sub_dd8f78`) then writes the battle's own day and month into the Word variables (never the year). The
  battle's date is random (day `Random(28)+1`, plus hours until the light fits) and can fall into the next month. A
  live battle's first load only moves the day/month (the month advance after the battle still starts from the right
  date), but A's snapshot at a resync saved the moved day/month into [General], the reload took them into 0xf52fe8, and
  a battle dated in the next month then skipped a month (August straight to October). Since 0.3.2, `Hold` records the
  campaign date (the battle has just opened; no load has touched it), `SaveSnapshot` swaps the recorded date into the
  variables around its `SaveBuildCampaign` (the snapshot's [General] keeps the campaign's own date; the battle still
  runs with the `.sac`'s day and month on both PCs, as before), and a battle slot's close, which skips
  `DoStrategicTurn` (tests), puts the recorded date back (`hookcb::OnBattleSlotSkippedTurn` ->
  `lockstep::OnBattleSlotClosed`). Only A changes, so a 0.3.1 player can still be B. The variables are found in
  `LoadBuildCampaign` (`mov ecx, [Day]; movzx ecx, word [ecx]; mov edx, [Month]; ... call EncodeDate; fstp qword ptr
  [campaign date]`, once, at +0xaf8). Log: `lockstep: campaign date recorded: Y-MM-DD` and `campaign date ... put back`.
  A record still set when the next battle opens (tests start a battle over another one without its close) is put back
  into the variables instead of recording the moved date (`campaign date ... kept from the battle before`); a month
  that runs (`DoStrategicTurn` outside a battle slot: `hookcb::OnStrategicTurnRuns`) and a campaign load that isn't
  the live battle's own (`CB_BeforeLoad` -> `hookcb::OnCampaignLoading`, ignored while a live battle is on) drop it.
  Because the battle itself runs with the `.sac`'s day and month, the host's date variables held the battle's month
  until the month advanced, so the battle's aftermath (victory points, prestige, the month's record row) and the
  month's steps before the advance could see the wrong month (a regression run found nation 0's VP 980 in the host's
  memory but 1040 in the client's month package after a real-month battle dated in the next month). A puts the recorded
  date back when the battle's result screen opens (`OnResultsEnter`, the `ShowResultScreen` context) and when the
  month runs (before the record is dropped), as in a battle that was never reloaded. Only A changes.
  (The battle running on the battle's own day and month is the subject of 0.4.0: both PCs will write the campaign's
  date back after each load.)
- Chat lines with a path (found by the regression tests): after the in-minute autosave the pause line went to both
  players as "The battle paused (Game saved! <the host's full save path>)", and a quiet box of the mod's own snapshot
  could be repeated as a stale reason after a resync. Box texts for the chat now go through `ChatSafe` (a drive path is
  cut to its file name), and the mod's own quiet boxes no longer become the reason of a pause.
- Bridge (tests): `ls airwin [show]` reports the window's visibility, its item count, the formations per side and the
  first three columns of each row (`show` opens it as the toolbar button does); `ls ctl <form class> <field>` reports
  a control's class, `enabled`, `visible` and, for speed buttons, `down` and `group` (a dialog's instance is found as
  above); `pocstate` adds `fastest=<0|1>` (the button's `FDown`; -1 when it isn't resolved).
- Docs: the lists of what live battles can't do yet (README, CHANGELOG, ARCHITECTURE) are corrected, and
  GAME-INTERNALS has the battle commands.

## Testing 2026-10-17: 0.3.2 hotfix (speed controls, air window, dialogs before reloads) (v0.3.2)
- Regression agent. Builds (frozen copies, given with `--dist`): the released 0.3.1 for the confirmations and one
  comparison, then the 0.3.2 work in progress: 085a0aa (speed arrows, Fastest available, air window, dialogs before
  reloads), d042d71 (+ blocked toggle buttons put back, missile panel locked, `ls ctl`) and 3cc0628 (+ the battle's
  teardown before every reload, the ship tree inside a minute, `TfrmTargets` dropped), the last one left installed.
  Game open in all: ~127 min (four full or stress runs, the rest short runs and experiments).
- **The 1935 test campaign (new fixture, outside the repo; docs/TESTING.md "The 1935 test campaign").** A USA campaign
  can't host a test battle: `pocbattle` takes the opponent's build area if it is a home area of the host's nation (or
  the reverse), else the possession the mission names, and the USA has neither against any nation (`pocbattle: no area
  for mission 0 against 5`; only Guam against the Soviet Union, where DoBattle found no battle). Made instead: Great
  Britain 1935 (title screen driven with posted messages, slot 9, saved once with `save`; slots 1 and 2 unchanged).
  `pocbattle 2 0` against France (Fleet battle at Plymouth) has 43 air formations, 23 British (naval air stations
  Grimsby and Plymouth, airbase Cork, carriers, floatplanes) and 20 French. The runner backs up and restores slot 9
  like slot 1; `lockstep_air` skips without it.
- **Confirmed on 0.3.1** (the 1900 fixture's test battle, posted clicks):
  - B3a: B's arrow up 0.4 s into a running minute at speed -1 changed B's own speed at once (-1 -> 0, A -1): `DESYNC at
    minute 2: here a3ca603bbad59d99-160d80c2, Captain 55dbea7c854b5021-2de9e8f7`, a resync (epoch 1 -> 2).
  - B3b, only partly: the blocked click left the button down (`sbFastestAvailableClick is not available in a live
    battle (this version).`) and B's own speed jumped to 5 at every boundary (pocstate speed 5, ivsec 240 vs A's -1/8;
    2 vs 1 in contact), but no DESYNC in 52 minutes (7 without contact, 45 engaged): every release applies A's speed
    before the minute.
  - B4: not observable live; no battle with aircraft runs on 0.3.1 (below). Static evidence only. The land-CAP click
    was blocked with its chat line; its stuck Down state isn't readable on 0.3.1 (static evidence only too).
- **Found by the new checks and fixed in 3cc0628:**
  - Live battles with aircraft never started (0.3.1 to d042d71): every load appended the saved air formations to the
    game's global list, and A, which built the battle, loaded it once more: `the loaded battles differ: here
    8e782827a2c4a2e1-5846b9c5, Captain fb8f764afd2dd03a-5846b9c5` at epochs 1-4, then `dropping Captain: the battles
    could not be kept in sync` after 16 s; af host 43 -> 688, client 645 (43 x 16, x 15), and the client kept its 645
    formations after leaving. 3cc0628: `battle data cleared (ok), air formations left 0` before every load, af 43/43.
  - The second player's ship tree ("OB" tab) listed side 0's forces after every minute (`Great Britain Main force`,
    `Great Britain Coastal forces`, with their divisions and ship names; its own right after a load): 0.3.1 and d042d71.
  - `battle dialogs resolved, not found: TfrmTargets` on every copy (d042d71): dropped from the list.
- **Recorded, not checked (0.4.0 material):** a resync hands side 1's divisions to the game's AI. `ls divs` on both
  copies before `ls resync`: side 0 7 of 8 on the AI, side 1 0 of 15; after it on both copies side 1 15 of 15 `:ai`
  (also the batteries), side 0 unchanged, hashes equal (d042d71; the same on 3cc0628: 0/14 -> 14/14, and in every
  `lockstep_ui` run's NOTE). Both copies agree, so nothing desyncs, but the second player commands nothing after the
  first resync.
- **Found, open (0.3.0-0.3.2, not caused by 0.3.2): a live battle's load moves the host's campaign date.**
  - The game's resume path sets the campaign [General] date to the battle's own when A loads the battle (its start and
    every resync; B loads the same save). One `ls resync` right after the start: start save `1900/8/1 sip=1`, the
    resync's save `1900/8/5` (A's in-memory date after its own load). Four test battles in a row: Day 14, 8, 27, 27 on
    3cc0628 and 27, 21, 5, 10 on 0.3.1 (each battle a new random day, carried by the next saves and through months).
  - Now and then the battle's date is in the next month. Full default run on 3cc0628: after `lockstep_ui`'s battle
    slot 1 was saved as Sep 1901 (`ScenarioInProgress 1`) and the auto-run prompt said "Event - September 1901" while
    the client planned Aug 1901; the next month `Captain's turn NOT applied: submission is for another month`
    (`MergeSubmission` compares the submitted save's [General] Year/Month with the host's, saved just before), and as
    the host merged Bot's turn (sent from the drifted save) it reloaded the Sep save: the month after was Oct, the
    client skipped Sep. `--stress` f) (after S6 and a long battle to the game's own end): the same rejection; the host
    then ran Aug -> Sep.
  - Not a race of the tests: battles started from a settled strategic screen (new `host_settled`) do the same, and the
    extra "Rule the Waves III" box at that battle's start was most likely the game's fuel message (the box the new
    `box_text` read in that place since: "N destroyers unable to take part in battle due to lack of fuel, caused by
    distance to closest base (103 nm).").
  - Suggestion: keep the host's campaign date across a live battle's load (restore [General] Year/Month/Day after the
    resume path, or merge by a session month counter instead of the save's date). A check "the host's month after a
    live battle = before" would catch it at once; `ls_after_checks` j) now compares host and client months. (Later the
    same day: fixed in d99e357 for battles that close before the next one; see the final candidate part below.)
- **Minor, open:** "The battle paused (<text>)" names the last box the mod answered, which can be stale: the first
  break after a resync names the resync's own quiet "Game saved! <the host's save path>" box, and that line goes to
  both players' chat (the host's local save path, with its Windows user folder, shown to the other player). (Fixed in
  d99e357, below.)
- **Verified on 3cc0628** (`lockstep_ui` on the 1900 fixture, `lockstep_air` on the 1935 one):
  - Speed arrows: B up and A down between minutes, B down inside a running minute at -1, A up inside one at -2: one
    request each (`Germany set the game speed to -2 (from the next minute).` on both), the minute went on at the old
    speed on both, both at the new speed from the next minute, no DESYNC; two clicks on B 1.5 s apart: requests 0 and
    1; in contact (speed 1, engaged) 4 clicks: one request (2), then none (the game's rule).
  - Fastest available and Auto-run, on A and on B: their chat lines, put up again (`ls ctl ... down=0 group=579/97`,
    `pocstate fastest=0`, autorun 0), the same speed on both; 14 minutes in contact equal.
  - Missile panel: `cbFireAtWill` and `rgMissileDoctrine` enabled=0 (visible=1) on both after the load, at a boundary
    and after a resync.
  - Air window and division report: `ls resync` with both open on both copies (the reports open since the start): each
    copy logs `TdlgShowAirFormations hidden before the battle is loaded again (ok)` and `TdlgDivisionReport hidden ...
    (ok)`, no crash, the window reopens; the land-CAP button put back after a click (`down=0 group=6`, 1900 and 1935),
    the report's Ok, the next minutes equal. `battle dialogs resolved` without `not found` on every copy.
  - Ship tree: B's top nodes list only its own forces after the load, the minutes and the resyncs.
  - 1935 (B4): A's window lists 23 rows (side 0), B's 20 (side 1), no base in both, also after a minute; a resync with
    the windows open keeps af0/af1/afx on both; 30 live minutes (8-37) with every division on the AI, every minute
    equal, the same formations per side on both copies every 5 minutes, each window its own side; in the air: CAP only
    (1 + 2 formations) in one battle, recon searches, CAP and a naval strike readying (af 23/20 -> 49/39, up to 27 + 19
    formations in the air) in the others; a resync at minute 37 with them in the air: the same counts, still in the
    air, 3 minutes equal; no LEAK.
  - Memory: a load costs ~1-6 MB private memory per copy (1900 and 1935); nine battles in a row 81/81 -> 157/169 MB
    (8.7/8.4 MB per battle; 0.3.0: 91/103 -> 179/203).
- **Suite** (`tools/regression.py`): new scenarios `lockstep_ui` (default run, after `lockstep`, no month) and
  `lockstep_air` (default run, last; the session moves to slot 9; SKIP without the fixture). The dialog answerer leaves
  the battle screen's modeless tool windows alone while the screen is enabled (`LS_MODELESS`); new helpers for the
  battle screen (tool bar clicks, the speed arrows, the OB tree, popup menus, the division report, `ls ctl`, `ls
  airwin`), slot 9's backup and restore, `SKIP` in the summary. Diagnosis: every answered game box is logged with its
  text (`box_text`: read-only from the copy's memory, the TLabel has no window), the saves' [General] dates
  (`gen_date`) before every turn and around test battles, `host_settled` before a test battle from the strategic
  screen, and `ls_after_checks` j) requires the host's month = the client's.
- Results:
  - 0.3.2 d042d71: `--only lockstep_ui,lockstep_air` 31/34 (196 s); full default run 537/540 (926 s; the 3 failures were
    the issues fixed in 3cc0628; every older scenario passed); `--stress` stopped after S2 on request.
  - 0.3.2 3cc0628: `--only lockstep_ui,lockstep_air` 46/46 (272 s); full default run 549/550 (1062 s; the fail: the
    month, above) and, with the diagnosis, **558/558** (1052 s); `--only lockstep_ui,lockstep_stress --stress` 121/122
    (1531 s; the fail: the month in f)).
- Testing gotchas (docs/TESTING.md, Known traps): the modeless tool windows and the answerers; a hidden ship tree must
  not be selected ("Cannot focus a disabled or invisible window."); the speed arrows are a TUpDown window; tool bar
  buttons beyond a narrow battle window still take posted clicks; the speed falls back to 1 in contact; a box's text
  needs `box_text`; the host's `status` date is its save file's.
- **0.3.2 final candidate (d99e357: the host's snapshots keep the campaign date, chat lines without paths)**, installed
  and left installed:
  - Date check (a scratch run inside the runner, game open 293 s): 4 test battles in Aug 1900, each with 2 resyncs.
    Battles closed by the host before the next one: every snapshot 1900/8/1, `campaign date recorded: 1900-08-01`, the
    close `campaign date 1900-08-01 put back (was 1900-08-05 after the battle)` (and `-16`), the host's in-memory date
    after the close (`save`) 1900/8/1; the month after merged the client's turn, Aug -> Sep.
  - **Gap (test path):** a `pocbattle` that replaces a battle screen still open on the host (no close between the two
    battles) records the previous battle's day: battle 4, started over battle 3's screen, recorded 1900-08-04 (battle
    3's `.sac` day), and its snapshots and its close kept 1900/8/4. The stress cases chain their battles that way:
    `recorded: 1900-08-01, -08, -31, -15, -13, -20, -21, -09, -13, -21`, slot 1 after S7 1900/8/13, and the campaign
    kept day 21 from then on (Sep 21, Oct 21, ...). A chained battle dated in the next month would still move the
    month. Real play closes every battle before the next one, so as far as known only the tests' chains are affected.
    Suggestion: in `Hold`, keep (or first put back) a recorded date that is still set. (Fixed in 05b81f6, below.)
  - Pause lines: 122 in the two runs' logs (5 of them the first break after a resync), none with a path or "Game
    saved"; the mod's quiet boxes are no pause reason any more. No "Game saved!" box was ever answered inside a minute
    in any run (no in-minute autosave box to test).
  - New suite checks: `lockstep_ui` h) (every snapshot keeps the month's date, the close puts it back), `lockstep_air`
    i) (snapshots keep Jan 1935), `lockstep_stress` g) (the date after the chained battles), and in `ls_log_scan` (every
    lockstep scenario) the battle's chat lines without a file path or "Game saved".
  - Results: full default run **556/558** (1141 s): every lockstep scenario passed (`lockstep` 25/25, `lockstep_ui`
    27/27, `lockstep_pvp` 24/24, `lockstep_month` 34/34, `lockstep_air` 15/15); the 2 fails are `treaty` g): nation 1
    also gained Norway in the treaty month, not by the treaty and not from nation 2 (the month's own processing gave it;
    no log line names it), unrelated to 0.3.2. `--only lockstep_ui,lockstep_stress --stress` **127/128** (1561 s): the
    fail is the stress g) date check (the gap above); f) and every real month merged the client's turn.
  - Game open for this part ~50 min; in all ~177 min.
- **0.3.2 final candidate e (05b81f6: the recorded date kept across chained test battles, dropped when a month runs or
  a campaign is loaded)**, installed and left installed:
  - `--only lockstep,lockstep_ui,lockstep_month,lockstep_stress --stress` **213/213** (1940 s). The stress g) date
    check passes: `campaign date recorded: 1900-12-01` once, then nine `campaign date 1900-12-01 kept from the battle
    before (the variables had 1900-12-26)` (-11, -18, -04, -15, -13, -05, ...), slot 1 after S7 1900/12/1.
    `lockstep_month`'s new per-battle date check passes for both real-month battles (`recorded: 1900-09-01` and
    `1900-10-01`, `the month runs: the recorded campaign date is dropped`, the next month with day 1). Host log:
    recorded 8, kept 9, put back 4, dropped by a month 3, dropped by a campaign load 0.
  - Full default run **574/575** (1034 s). `treaty` 73/73: g) did not recur. The same kind of event showed up in the
    `merge` month: the picture message and the turn report said "Germany has taken over control of Shanghai.", and slot
    1's MapData `Shanghai (MapArea5Possession9): Neutral -> Germany`. So the Norway of the d99e357 run (a neutral
    possession, `MapArea0Possession5`, Owner Neutral in the fixture) was most likely the game's own "taken over control"
    event in the treaty month. The runner now logs every month's possession changes, picture messages and turn reports.
  - The fail (open, first seen): `lockstep_month` battle 1 (drop12, the Nov 1901 turn). VP of nation 0: host 980 (in
    memory, `vpinfo`), client 1040 (its month package); nation 1 112 on both, ship counts equal; before the month 0:700,
    1:2. The battle: "Great Britain has no ships left at sea! The scenario is over!", "The battle was a draw. Own VP: 0
    Enemy VP: 0", then "The Prime Minister is annoyed that you cannot safeguard our coasts with adequate patrols Your
    prestige suffers. (Not enough ships on trade protection)" and the Trade warfare summary. No `victory points:` line
    that month; the campaign date was right (recorded 1901-11-01, dropped, then 1901/12/1). A repeat (`--only
    lockstep_month`, 91 s) passed 43/43 (that war had just started: VP 0).
  - A lead for it: the repeat logged the real-month battle's own date. The Aug 1900 turn's battle had `[Environment]`
    1900/9/10 in its `.sac` while [General] stayed 1900/8/1. A live battle's load (on A too) writes that date into the
    date variables, and in a real month nothing puts them back before `DoStrategicTurn` (05b81f6 only drops the record
    there). So `RecordTurnData`/`MonthRecords` and the rest of the month before `IncMonth` see the battle's month,
    unlike vanilla, whose generated battle never touches them. Suggestion: put the recorded date back when the month
    runs (as at a battle slot's close), then drop it.
  - Game open for this part ~51 min; in all ~228 min.
- **0.3.2 final candidate f (919b40d: on A the battle's results and the month read the campaign's own date)**,
  installed and left installed:
  - `--only lockstep_month --stress` **70/70** (437 s), both real-month host battles in a war with VP already (battle
    2: before 0:747 1:1031). The new aftermath check passed for both: every nation's VP the same in the host's memory,
    its saved slot 1 and the client's month ({0: 993, 1: 1661, 7: 90}); `the battle's results: campaign date 1900-09-01
    put back (the battle's date was 1900-09-13)`; `the month runs: campaign date ... put back, the record is dropped`;
    the next month with day 1.
  - Full default runs **561/567** (959 s), **565/567** (1110 s) and **559/559** (1022 s); `--only lockstep_stress
    --stress` **101/101** (1326 s; g)'s date check passes).
  - The VP mismatch came back in two of the three full runs, so it is not the date (open): `lockstep_month` battle 1
    (`drop12`, the Oct 1901 turn; `.sac` dated 1901/10/29 and 1901/10/23, campaign 1901/10/01, the results line
    present). Second run: nation 0 has 2329 in the host's memory, in its saved slot 1 and in the client's slot 77 file
    as received (written 05:37:14.017); the client's memory has 2429 right after its `LoadCampaign slot 77 player 1`
    (05:37:14.185, "victory points shown: own 979, enemy 2429"). Nation 1 has 979 everywhere. First run: 1308 vs 1388;
    05b81f6's run: 980 vs 1040. So the package is right and the client's own load of it adds 60/80/100 to nation 0.
    In all three, the host's side scored nothing in that battle ("The battle was a draw. Own VP: 0 Enemy VP: 0" twice,
    "Enemy minor victory Own VP: 0 Enemy VP: 246"), and the client had been dropped at minute 12. In the clean run the
    host scored 228. Before 05b81f6 (4 default runs, one with "+0/+90") it never happened. When it happens again, the
    runner probes it (`vp_mismatch_probe`): it looks for repeated VP keys and the keys that differ between the two
    files, then reloads each copy's own slot (`load 77 1` on the client, `load 1 0` on the host) and logs the VP
    before and after. The same extra points on both copies would mean the game's load adds them; extra points only on
    the client would point to its own state.
  - Two other fails, first seen and not repeated: `battles` b) x2 was a test race. The client copies its next battle
    into slot 87 0.34 s after "battle 2: fought". The bundle goes first, in the host's numbering, and the transform
    after it; its write waited for the runner's read ("succeeded after 1 retries"). So the runner read the next battle's
    host-numbered bundle ("34 sunk" = all of Germany; "4 changed" = Spain's ships changed by the merge). Fixed in the
    runner: a slot 87 written after "fought" (before the read) counts as the next battle's copy, and the merge's own
    changes stand in. `lockstep_ui` e): one DESYNC at minute 22 of its 14 AI minutes (pos/ord differ:
    A `pos=cc9d1961 ord=57c99f72`, B `pos=649c68a0 ord=01f7085e`; dmg, ammo, sight, air, torp, ships and the RNG part
    `8732549d` equal, sim draws 5564 on both, no LEAK line). It came right after two "Unknown ship sighted!" pauses,
    in the minute where both copies' identification sets changed (ident A `2ab2952d -> b3cf60b2`, B
    `3469119f -> dc3de5a0`). The resync repaired it; f) failed only because that resync had hidden the reports. It
    didn't recur in the next 2 runs. A lead: an AI decision that reads the display's identification.
  - Game open for this part ~81 min; in all ~309 min.

## Fix 2026-10-17: air units doubled with every reload; two live-battle desyncs (v0.3.3)
- A hotfix of 0.3.2: three bugs that the live proofs of the 0.6.0 development branch found (stages A, B and C) and
  fixed there, backported with their log lines. Protocol 3, no new message or key, the per-minute hash unchanged.
  The branch's other test probes stay there; only `ls testcurspeed` came along (to test the report fix on demand).
  Main has no live-battle engine level, so the branch's level 11 (the identification fix) has no counterpart here.
  The code is the branch's, proved live there; nothing of this entry ran in the game (live tests: open, below).
- **1. Air units doubled with every reload (since 0.1.0).** `TfrmBuildCamp.LoadAircraftData` (0xd1d420, called only
  by `LoadBuildCampaign`) creates a `TAirUnit` per `[AirUnits]` entry and adds it to the global list (cell 0xf27a88,
  the list variable 0xfb0104) without clearing it; the game's own `ClearAirData` (0xd26700) runs only for the quick
  battle, since the game loads a campaign only into a fresh process. The mod reloads in the same process: the host
  after merging the players' turns every month, a joined player for every new month and around its battles (slot
  87, then slot 77 again), both PCs at every live battle start and resync. Each save and reload doubled the list (the
  branch's 1958 test campaign: `AirUnitNo` 595 -> 9520 = x16 after a session, a battle, two resyncs and one month):
  the host's Turn stopped at `PlayerChecksBeforeTurn` -> `CheckAirBaseCapacity` (0xd20900) "CV <name> aircraft
  capacity exceeded!", the air upkeep ran away, the save grew. New (static): the battle setup reads the same list:
  `AddScenarioAirUnits` (0xbf71b8, named by its error text; from `DoBattle` via 0xbdbccc and 0xbe2de0) creates a
  battle air formation for every unit based on the battle ship's campaign ship, so battles set up in such a process
  most likely had extra squadrons (not seen live). Fix (`game.cpp`, the existing `LoadBuildCampaign` hook): the cell
  is read from `LoadAircraftData` (`mov eax, [cell]; mov eax, [eax]; mov edx, ebx; call TList.Add`, at +0x7e) and
  must equal `ClearAirData`'s (`push ebx; push esi; mov esi, [cell]`), else `WARNING: air unit list not found ...`
  and nothing changes. Before a load the hook remembers the list's units; after a successful load (the result in AL,
  now passed to `CB_AfterLoad`) it removes them from the front of the list and frees them (`TObject.Free`, as
  `ClearAirData` does; a failed load keeps them, a list whose front changed is left alone with a WARNING), then drops
  repeated units of the save: the first unit of each `AirUnitId` (+0x04) stays. The id is unique in a campaign the
  game saved (a new unit takes the next value of the campaign's `[General] IDNo` counter: 0xd79de4 `mov eax,
  [0xf5318c]; inc dword ptr [0xf5318c]`), so a campaign saved by an earlier version heals on its first load. New
  (static): the reload never frees the old campaign's nations and ships (`BuilderNations.Clear`, 0xc99a91, then new
  `TBuilderNation` objects), so an old unit's `HomeBase` kept pointing to an intact old ship and the next save wrote
  the right base id for every copy; code that matches a unit by its base object (the capacity check, the air group
  dialog's base views, `RemoveAllAirUnitsFromShip`, the AI's air group growth) reached only the copies of the latest
  load until the next save and reload. Which copy the repair keeps was settled by reading every routine that walks the
  list (GAME-INTERNALS "Duplicated air units: which copy the game changes"): the game's only lookup by `AirUnitId`
  (0xe9f570) returns the first copy, and the end of every battle (0xea7abc) writes the battle's losses, aircrew
  experience, kills and hits to it, or deletes it when its ship was sunk; the later copies differ only by orders made
  through the base after the first reload (desired sizes, transfers, the computer's air group growth), which can be
  given again. So the first copy stays, as on the branch. A unit deleted during a doubled session can come back (in
  the reserve when its ship is gone), whichever copy is kept: the deletions reached only some of its copies. Nothing
  else holds `TAirUnit` pointers (`TdlgCAG.CopyFromAirUnit`, the only RTTI field of that type, is cleared when the
  dialog shows; `TAirFormation` names a `TAircraftType`), and `TAirUnit` has no destructor of its own. Logs: `air
  units: <n> from before the load dropped; the campaign's <m> kept` and `air units: <k> repeated units of the save
  dropped (<a> -> <b>; a save of an earlier version)`; bridge `airunits` -> `ok count=<n>`.
- **2. A division report desynced a live battle (since 0.3.0).** `TdlgDivisionReport.FillShipList` (0xae1874; called
  only by the report's `Execute` and the torpedo dialog's `bnCloseClick` and `bnFireClick`) sets each listed ship's
  `CurrentSpeed` (+0xa48) to `GetMaxWeatherSpeed` when that is lower (0xae1972..0xae1990), on the PC where the report
  opens only (the branch's leak check: `G8.CurrentSpeed 1c000000->1b000000`, then a DESYNC). Fix (`lockstep.cpp`):
  FillShipList is a display context (`K_UI`), and in a live battle (on, not ended, not loading) every ship's
  `CurrentSpeed` is saved at its entry and put back at its exit, outside the simulation (`ReportSpeedsSave` and
  `ReportSpeedsRestore`); the simulation lowers it in the next minute on both PCs, and the report still shows the
  lowered value. FillShipList draws no random number and takes no stack argument, so the new context changes nothing
  else. Log: `lockstep: the division report lowered <ship>'s current speed a -> b on this PC only: put back (the next
  minute lowers it on both PCs)`. Test probe: `ls testcurspeed <ship> <knots>` (docs/TESTING.md).
- **3. The two PCs' simulation started every live load with different ship identification (since 0.3.0).** The
  battle save keeps none (`TFightingShip.Create` writes `IdentifiedAsType` (+0xd70) := 0x16 at 0xe1c6bf and never the
  class +0xd6c; `TFightingShip.LoadMe` reads neither), but the battle screen's first drawing during the load names
  enemy ships through `GetTypeAndNameForEnemy` -> `IdentifyType`/`IdentifyClass` (both draw `Random`) with this PC's
  display draws, and `SetView`'s first swap took those values as the simulation's set. The hash lines' `ident=` (not
  part of `all`) differed in every minute, and the simulation reads the identification: the sighting reports routine
  in EOTP (0xdc960c) calls `IdentifyClass`/`IdentifyType` with shared draws that depend on it (and, a guess, some AI
  decisions may read it). So now and then the battles drifted apart next to "Unknown ship sighted!": 0.3.2's
  `lockstep_ui` e) (the 0.3.2 testing entry above: pos and ord first, the RNG equal, "a lead: an AI decision that
  reads the display's identification"), and on the branch with the RNG first. Fix (`lockstep.cpp`): right after every
  live load (the start and every resync) `SetView` gives the simulation the identification of a freshly loaded ship
  (Create's immediate, read from its `mov byte ptr [ebx+IdentifiedAsType], imm8`, and no class) on both PCs; the
  display keeps what it drew. If the pattern isn't found: a WARNING and the old behaviour. Log: `lockstep: the
  simulation's ship identification as the battle save leaves it (type 22, no class); this PC's display had identified
  <n> ship(s) during the load and keeps that`.
- **Mixed sessions.** The host checks only the protocol and the game build (the hello's `mod=` is not read), so a
  0.3.2 player joins a 0.3.3 host and a 0.3.3 player a 0.3.2 host as before.
  - 1 acts per process. A 0.3.3 host keeps the campaign clean whatever its players run, and heals a doubled one at
    its first load. A 0.3.2 host keeps doubling: its saves, month packages and battle files carry the copies, which a
    0.3.3 player only removes from its own memory at each load. A 0.3.2 player's copies stay on its PC (memory, slots
    77 and 87, and the battles its PC sets up: its battles against AI nations and the live battles it sets up as A of
    two joined players): the submission carries only the nation's sections, the host never merges `[AirUnits]`, and a
    joined player's Turn never runs the game's checks. A live battle doesn't depend on the list on B: B loads A's
    snapshot, whose air formations A's setup made, and the hash covers no air unit. So the host must update; the
    players should, for their own battles and screens.
  - 2 acts per PC: a report opened on a 0.3.3 PC no longer desyncs, one opened on a 0.3.2 PC still does (a resync),
    whichever side each PC plays.
  - 3 needs both PCs: with one 0.3.2 PC the simulation identification still differs (the constructor's on the 0.3.3
    PC, what its display drew on the 0.3.2 PC), so the rare desync stays possible, as between two 0.3.2 PCs. Nothing
    new can go wrong: the hash ignores the identification, and both PCs run the same game code on it.
- **Checks.** `build.bat` without warnings. Every pattern above checked offline against the 1.01.44 image (a script
  kept outside the repository): the list cell from both routines, the only callers of `LoadAircraftData`
  (`LoadBuildCampaign`) and `ClearAirData` (`TfrmQuickBattle.bnGenerateFleetsClick`), `AirUnitId` and the `IDNo`
  counter, `TAirUnit`'s destructor slot (`TObject.Destroy`), `AddScenarioAirUnits` and its callers; FillShipList's
  callers, its single `CurrentSpeed` write, no `Random`, a plain `ret`; Create's single identification write (0x16)
  within the resolver's 0x400 bytes, `LoadMe` without identification, `Random` in `IdentifyType`/`IdentifyClass`.
- **Open:** the live tests of 0.3.3 (regression agent): a joined player's months with the 1935 test campaign (slot 9:
  232 air units; `airunits` on both copies after the session start, every month, a live battle start and `ls
  resync`), a copy of it with every `[AirUnits]` entry doubled (the repair at the first load), `ls testcurspeed` and a
  division report in a live battle, `ident=` equal in every minute, the default suite. Mixed-version pairs need two
  game folders: both copies of one folder load its `RTW3MP.dll`.

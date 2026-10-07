# MODLOG — Rule the Waves 3 multiplayer mod

Journal for the RTW3 multiplayer mod. Paths, versions, names, formats, failures, next steps.

## Game facts (recon 2026-10-04)
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

## Delphi RTTI (huge win)
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

## Route
Native: proxy DLL (system DLL name loaded from the game dir) -> loads `RTW3MP.dll` -> resolves targets by
Delphi RTTI name at runtime (no hard-coded offsets) -> inline hooks. Win32 UI for the MP lobby. TCP over
Hamachi/any LAN VPN. Host authoritative.

## Save format (1.01.44)
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

## Live experiments
- Writing `PlayerNation` (+0x3204) to BuilderNations[1] and refreshing the ship tab shows Germany's fleet:
  the strategic UI follows the pointer. => clients can play their nation on a copy of the host state.
- Posted WM_LBUTTONDOWN/UP to VCL panels and BM_CLICK to buttons drive the UI without touching the user's input.
- frmBuildCamp global var at RVA 0x723000 (1.01.44, dev only; the mod finds the instance by HWND/hook capture).

## Turn pipeline (1.01.44)
btbnTurnClick -> (StartBuild 0/1 initial phases with AIBuildInitialFleet/AIMonthlyBuild for 1..8) -> DoTurn2:
PlayerChecksBeforeTurn, SaveBuildCampaign(autosave), officers, AI strategic moves (unnamed, RVA 0x4c024c,
writes ship OrderedArea/LocationArea/Status for nations>0), StopBlockadedMoves, AreaMoveShips (executes orders for all),
CheckForBattles2 (true => battle, strategic turn continues later) -> DoStrategicTurn (... ReduceTimeLimits calls
AIMonthlyBuild ..., ComputeExpenses: every nation Funds += GetMonthlyBalance, CheckTurnEvents, DoResearch per nation,
CheckAIvsAIBattles, raiders) -> DoEndOfTurn (CheckForWar, AdvanceDesignStudies, UI).

## Multiplayer design (v1)
Host authoritative, simultaneous planning, per-nation merge:
1. Host plays nation 0 in its normal slot. Clients claim AI nations 1..8.
2. Each month host saves, sends the slot files to every client (IDNo offset per client so new ids never collide).
3. Client loads into its own MP slot (Game77+), sets PlayerNation to its nation, plans, presses Turn => client saves,
   sends [NationK*] sections + DesignFilesK.des + its MapData + IDNo.
4. Host presses Turn (or timer expires) => host saves, merges each submission, reloads, runs the real turn with
   AI decision routines suppressed for human nations (hooks) and AI moves undone for their ships.
5. After DoEndOfTurn the host saves and broadcasts the new month.
Battles: fought on the host (host's nation); client nations' wars resolve via the game's AI-vs-AI logic (v1 limit).

## Implementation status (v0.1.0, 2026-10-04)
- version.dll proxy -> RTW3MP.dll; all targets resolved by RTTI name at runtime (1033 classes found).
- Unnamed AI fleet-move routine found as "the call before StopBlockadedMoves in DoTurn2".
- Save-slot global found from `mov edx,[ptr]; mov edx,[edx]` at the start of SaveBuildCampaign.
- Instances (frmBuildCamp etc.) found by scanning committed heap for the VMT + Owner is TApplication.

## Gotchas (each cost a debugging round)
1. Never read the game image before the VCL `TApplication` window exists (DRM stub still unpacking).
2. Bounds checks on candidate pointers must guard against wraparound: `np=0xFFFFFFFF` passed `np+1<=end`.
3. No implicit TLS in the mod DLL (`thread_local`, MSVC magic statics => build with /Zc:threadSafeInit-).
4. A WH_GETMESSAGE hook dies with the thread that installed it: keep the installer thread alive.
5. Don't subclass the game's VCL-styled panels (the title art stopped painting). Use an own child control.
6. Never call Log from a vectored exception handler that can fire inside Log (OutputDebugString raises 0x40010006).
7. `UpdateUIValues(True,True)` assumes the "ships in service" tab; use (False,False) + `TabControl1Change`.
8. Event dialogs (TdlgEventAnswer) need an answer radio selected before OK closes them (test automation).

## Verified in the real game (two local instances, loopback, posted messages only)
- Title screen "Multiplayer" entry -> MP window; host/join/claim/start over TCP.
- Client (Germany) changed research % and ordered a ship to the Mediterranean -> host merged, ran the month:
  values kept, ship moved, AI routines for Germany suppressed (logged), new month auto-sent (Jan->Feb).
- Client laid down a ship through the real Build ship dialog (built in Germany) -> present on host after
  Feb->Mar processing; AI fleet move on a German ship undone; AIMonthlyBuild suppressed.
- Client on "Ships under construction" tab receives Mar->Apr without UI error; rejoin restores nation.
Test tools: tools\rtw3ctl.py, e2e_test.py, run_turn.py, verify_tabs.py, ui_buildship.py.

## Fix 2026-10-04 evening: nation selection in the lobby
- Bug (user report, reproduced live with the real window): the nation list was empty until the host had a
  campaign loaded AND past the initial-fleet steps; the host had no pick at all. Joining showed nothing to claim.
- Now: picks by nation name from the 10 game nations before any campaign exists (host can plan one too);
  matched to campaign indices automatically when the host opens a campaign (also during initial-fleet steps);
  conflicts are explained in chat (host system lines are broadcast). Duplicate player names are numbered
  instead of refused (two copies on one PC share config.ini).
- Verified live (tools\live_lobby_test.py): title button -> window -> Host/Join buttons -> picks via the
  dropdown -> host loads Game 1 through the real Load Game screen -> picks resolve -> Start session ->
  client plays Germany in Apr 1900.

## Fix 2026-10-04 night: window close, Start session, Give to AI (user report from a second PC)
- [x] did not close the Multiplayer window: reproduced only with REAL mouse input (posted-message replays
  don't trigger Windows' title-button tracking, not even on a control window). Cause: ui::PreTranslate ran
  IsDialogMessage for every message inside the WH_GETMESSAGE hook and swallowed the mouse-up that the
  system caption-button tracking loop waits for. Fix: only Tab goes through IsDialogMessage.
- Start session / Give to AI looked dead: both were disabled until a campaign was open. Now always clickable
  for the host; Start session explains what is missing and auto-starts when the campaign is ready; Give to AI
  also removes lobby picks before a campaign and explains when nothing is selected/already AI.
  Joined players no longer see host-only buttons; a player whose nation is given to the AI is told.
- Verified with tools\realmouse_test.py (real clicks, ~40 s of game time, 11/11 checks).

## Docs 2026-10-05: host walkthrough video (docs\hosting.mp4 + 4 stills in the README)
- tools\record_hosting.py drives two copies (host + "Captain") through the bridge/posted messages only, so the
  user's mouse/keyboard are untouched, and composes frames from PrintWindow captures of the host process's
  windows only (never a screen capture: an earlier screen-capture take leaked another app and was deleted).
- PrintWindow sometimes returns a blank/partial title screen (VCL TImage); the script grabs best-of-8 and
  caches the title frame. The real LAN/VPN address line is overwritten with an example Hamachi IP.
- ctypes: HWND_TOPMOST (-1) must be passed as a pointer-sized value on 64-bit Python.
- ffprobe isn't on PATH, so `um video probe/contact` fail; call the bundled ffmpeg directly.

## Feature 2026-10-04 late: diplomacy between players (v0.2.0, protocol 2)
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

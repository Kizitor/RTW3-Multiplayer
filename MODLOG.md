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

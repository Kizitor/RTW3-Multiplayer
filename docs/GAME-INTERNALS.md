# Game internals

Reverse-engineered facts about Rule the Waves 3 1.01.44 and the techniques the mod builds on. Read this before you
work on code that touches the game. The current design of the mod is in [ARCHITECTURE.md](ARCHITECTURE.md); the
evidence for each fact (disassembly excerpts, log lines, measurements) and the full history are in
[MODLOG.md](../MODLOG.md).

## Contents
- [How to read the addresses](#how-to-read-the-addresses)
- [Where the facts come from](#where-the-facts-come-from)
- [The game and the mod: facts you can rely on](#the-game-and-the-mod-facts-you-can-rely-on)
- [Tactical battles](#tactical-battles)
  - [Battle commands](#battle-commands)

## How to read the addresses
- The mod resolves everything by name at runtime. Addresses and offsets here and in MODLOG are documentation for
  1.01.44 only.
- An address is either an RVA (marked "RVA": relative to the image base) or an address in the analysis memory image,
  whose base is 0x830000 (the base seen at runtime; the game has ASLR, its preferred base is 0x400000). Example: the
  global that holds the strategic screen is at RVA 0x723000, which is `[0xf53000]` in the image.
- `+0x...` is a field offset inside an object.

## Where the facts come from
- MODLOG's investigation and fix entries, for example [the game facts](../MODLOG.md#investigation-2026-10-04-game-facts-v010),
  [the save format](../MODLOG.md#investigation-2026-10-04-save-format-of-10144-v010),
  [the turn pipeline](../MODLOG.md#investigation-2026-10-04-turn-pipeline-of-10144-v010),
  [diplomacy](../MODLOG.md#feature-2026-10-04-diplomacy-between-players-protocol-2-v020),
  [random events](../MODLOG.md#feature-2026-10-06-random-events-for-joined-players-v024),
  [peace terms](../MODLOG.md#feature-2026-10-06-peace-terms-for-wars-between-players-v025),
  [victory points](../MODLOG.md#fix-2026-10-12-joined-players-victory-points-never-rose-against-the-host-v026),
  [Copy game](../MODLOG.md#fix-2026-10-13-a-joined-players-copy-game-played-the-hosts-nation-v027),
  [research](../MODLOG.md#fix-2026-10-15-the-games-ai-research-choices-reset-joined-players-research-priorities-v030),
  [the battle investigation](../MODLOG.md#investigation-2026-10-14-tactical-battles-joined-players-vs-ai-live-lockstep-pvp-v030)
  and [the limits of live battles](../MODLOG.md#investigation-2026-10-16-the-limits-of-live-battles-static-v031).
- The offline analysis workspace (outside the repository; described in `.github/agents/rtw3-research.agent.md`):
  the game's memory image, an annotated disassembler with RTTI names, the RTTI dumps and the form layouts.

## The game and the mod: facts you can rely on
Offsets are those of 1.01.44; always resolve by name at runtime.

- **Delphi register calling convention.**
  - The first three arguments go in EAX, EDX, ECX, with Self in EAX. The rest are pushed left to right and the
    callee removes them (`ret N`).
  - A Boolean result is in AL.
  - Constructors take EAX = VMT and DL = 1; `TObject.Free` frees.
  - `var` parameters are passed by pointer.
  - Strings are UnicodeString. Pass constants with `dl::ConstUStr` (refcount -1) and read them with `dl::ReadUStr`.
  - A Delphi string the game returns to you is never released: accept the small leak, as the mod already does.
  - Nested procedures need their parent's frame: don't call them.
- **Finding code.** `dl::Method(class, name)` returns the first method with that name; use `dl::MethodFewestParams`
  for overloads. Unnamed procedures are found through a call-site byte pattern inside a named method
  (`dl::FindPattern`), checked at runtime (see `RemoveShipInPeace`, the G scale value, the war counter).
  Instruction encodings vary (`6B` vs `69` imul): verify every pattern against the image.
- **Hooks** (`game.cpp`, `InstallHooks`; `Hook(target, detour, &orig, name)`).
  - Naked thunks keep EAX/EDX/ECX for the original and copy the caller's stack arguments before calling it.
  - Pair "before" and "after" callbacks with an explicit slot (see `d_BattleAftermath`).
  - Catch exceptions in every callback.
  - MSVC: a function with `__try` can't hold C++ objects with destructors; use plain-data helpers.
- **Game objects.**
  - `frmBuildCamp` (the strategic screen) is the global at `[0xf53000]`; `PlayerNation` is at form +0x3204;
    `BuilderNations` (a TList) at +0xae4.
  - Nation 0 is the save's player. The game mixes "PlayerNation" and "BuilderNations[0]": the Research button used
    index 0 (fixed in 0.2.8 by hooking `TfrmResearch.Execute`).
  - Relations: `Tension`/`Allied`/`TechSharing` = the relation with nation 0, `AITension<j>`/`AIAlliance<j>` =
    between nations 1..8, war = tension > 40.
  - Globals: the war counter (`[General] War`) at 0xf52ff4; `EnemyVP` at form +0x4a63.
- **Randomness.** All of the game's `Random` calls go through one function pointer, `Random32Proc` at `[0xf1c040]`:
  about 6,400 call sites, about 2,500 of them in battle code (`TfrmMain`, `TFightingShip`, `TAirFormation`, ...). Hooking
  that pointer controls every dice roll.
- **Nation swap, the mod's standard trick.** To make the game act for nation K, swap the TList entries 0 and K and
  put K's relations in nation 0's fields; restore everything in `__finally` (see `RunClientEvent`). The save-file
  version of the same swap is `saveio::TransformNationPlayer` (Copy game, 0.2.7): nation sections, relations,
  nation numbers in ships, air units, mines, possessions and design files.
- **Saves.** `.bcs` (campaign), `MapData<n>.dat`, `.off` (officers, divisions), `.sta` (month records),
  `DesignFiles<i>.des`. The `.bcs` and `MapData` files are UTF-8 INI with CRLF (the `.bcs` with a BOM), and the `.off`
  file is INI as well; `IniDoc` in `saveio.cpp` keeps their lines intact. `DesignFiles<i>.des` is positional text
  (the mod moves it as a whole file).
- **Loading a campaign twice in one process.** The game always loads into a fresh process (title screen), and two of
  `LoadBuildCampaign`'s lists are appended to, not replaced: the intel reports (0.2.2) and the **air units**:
  `TfrmBuildCamp.LoadAircraftData` (0xd1d420, called only by `LoadBuildCampaign`) creates a `TAirUnit` per
  `[AirUnits]` entry and `TList.Add`s it to the global list (cell 0xf27a88) without clearing it; the game's own
  `ClearAirData` (0xd26700: frees the units and the aircraft types) is called only by the quick battle; the aircraft
  types' loader (0xe9cc74) clears its list (without freeing). Every in-process reload (the host's month after merging
  the turns, the client's new month, every live battle start and resync) doubled the air units in memory and in the
  next save (seen 2026-10-17 in a 1958 test campaign: `AirUnitNo` 595 -> 9520 = x16 after a session, a live battle,
  two resyncs and one month). Effects: the host's Turn stops at `PlayerChecksBeforeTurn` -> `CheckAirBaseCapacity`
  (0xd20900) "CV <name> aircraft capacity exceeded!" for a carrier with an air group, the air upkeep explodes (funds
  "+ -411,149" in a month). The battle setup reads the same list: `AddScenarioAirUnits` (0xbf71b8, named by its error
  text; from `DoBattle` through 0xbdbccc and 0xbe2de0) creates a battle air formation for every unit whose `HomeBase`
  (+0x22) is the battle ship's campaign ship (`FindCShipWithId`), so a doubled list most likely doubled the squadrons
  of battles set up in that process (static reading, 2026-10-17; not seen live). Since 0.3.3 the mod drops and frees
  the units it held before a load once the load succeeded, and repeated units of a save: `AirUnitId` (+0x04) is
  unique in a campaign the game saved (a new unit takes the next value of the campaign's `[General] IDNo` counter,
  0xd79de4: `mov eax, [0xf5318c]; inc dword ptr [0xf5318c]`). Nothing else keeps `TAirUnit` pointers: `TAirFormation`
  names a `TAircraftType`, and `TdlgCAG.CopyFromAirUnit` (the only field of that type in the RTTI) is cleared when the
  dialog shows (0xb63c08). `TAirUnit` has no destructor of its own (`TObject.Free`, as `ClearAirData` frees them).
  Every other list counter of the save stays. The reload doesn't free the old campaign's nations and ships
  (`LoadBuildCampaign` drops them with `BuilderNations.Clear`, 0xc99a91, then creates new ones: part of the memory
  growth per reload), so an old unit's `HomeBase` still points to the old ship object: base-matched code (the air group
  dialog's base views, `CheckAirGroupNumbers`, the battle setup) sees the old copies only after a save and a reload
  resolve their base ids, which are right.
- **Duplicated air units: which copy the game changes** (static reading, 2026-10-17). A doubled list is [the copies of
  the game's first load] + [the copies of each later load], in that order, in the save too (`SaveAircraftData` writes
  the list in order, `LoadAircraftData` appends in file order).
  - *By `AirUnitId`: the first copy.* The game's only lookup by id (0xe9f570) walks the list from index 0 and returns
    the first match; nothing else that walks the list compares `AirUnitId`. The end of every battle fought on the
    battle screen (0xea7abc, from the scenario's end 0xdec0e0) looks up each battle air formation's unit by the
    formation's `UnitId` (+0x08, the unit's `AirUnitId`, set by the battle setup 0xbf767c) and writes the results to
    that copy: `AircraftNumber` -= the formation's losses, `PilotExperience` +1 to +3, `AirKills`, `BombHits`,
    `TorpedoHits`, `MissileHits` += the formation's (the month's aces, 0xeabc90, are made from these); when the
    formation's ship was sunk it deletes and frees that copy instead (a fleet exercise, flag 0xf273cc, gives +2
    experience only). Fatigue (`TAirFormation.CheckFatigue`) and the pre-battle reliability check (0xea9d30) read it
    too. The AI's area fortification (0xd2db48, from `AIFortifyArea`) moves the first eligible unit of a role and
    stops: usually the first copy as well.
  - *By base: the copies of the latest load.* Code that matches a unit's `HomeBase` with a current ship object, every
    match, reaches only them until a save and a reload: the capacity check, the battle setup (one formation per copy),
    the air group dialog's base views (`TdlgCAG.FillAirUnitList`: a row per copy; its view of all units lists every
    copy), *Move all to reserve*, `RemoveAllAirUnitsFromShip` (scrapping, reparations, a lost possession),
    `ClearAirGroup` and the AI's air group growth and cuts (`AIAddAircraftToAirGroup`,
    `CheckOneShipsOrBaseHasAircraft`).
  - *Every copy, each with its own random draws:* the month's air routine (0xeaa6ec, from `DoTurn` and
    `DoStrategicTurn`: training, replacements from the aircraft type's stock, upgrades, roles, elite loss, the AI's
    disbanding of reserve units), `MoraleLoss`, `DoPurge`, the upkeep (`GetTotalNavalAirCost`) and the counts per
    nation.
  - So the first copy carries a unit's battle record and strength, the later copies the latest orders made through
    its base (desired sizes, transfers, the computer's air group growth), which can be given again. 0.3.3's repair
    keeps the first copy of each `AirUnitId`.
  - Deletions don't hold in a doubled session: a sunk carrier's squadron loses one copy per battle formation, taken
    from the front (copies that weren't in the battle), and scrapping, a deletion in a base view or the AI's cuts
    remove only the copies of the latest load. The other copies come back after a save and a reload, at their base or
    in the reserve when that ship is gone (`TAirUnit.LoadMe` sets `HomeBase` from `FindCShipWithId`: nil when not
    found), and after the repair one of them stays, whichever copy is kept.
- **Carrier capacity** (`TCampaignShip.CheckAirGroupNumbers` 0xd5a9c0, for ships with `InPlay`, `RepairTime` < 100
  and `AircraftCapacity` > 0 that are carriers or seaplane carriers): the sum of the air units' `DesiredAircraftNumber`
  (+0x18) based there, plus half of it for roles 11 and 12 on a carrier below 40000 t and for role 10 (jet fighters) on
  a carrier that is not `JetCapable`; above `AircraftCapacity` -> the Turn stops. The AI never checks it: an AI
  nation's campaign turned into a player's (`MakeNationPlayer`) can carry overfull carriers (seen in the 1958 test
  campaign: Aquila, Nibbio, Altair).
- **Network.** `net.cpp` uses TCP with message types `MSG_*`; payloads are `KV` key=value text or binary bundles
  (`Bundle::Pack`). The host is authoritative, and new fields must be ignorable by older clients or bump `MP_PROTOCOL`.
- **Threading.** All game calls run on the game's main thread. The network thread only posts `WM_MP_NET`; bridge
  calls go through `RunOnMainThread`. A modal game dialog runs its own message loop, so mod code keeps running under
  it: guard against re-entry.

## Tactical battles
What is known as of 2026-10-17 (0.3.3).

- **The battle form.** `TfrmMain` is the battle screen. Its buttons:
  - `sbPlay` "Run one minute", `sbRun5` "Run 5 minutes if engaged, 20 if not", `sbRun` "Run until break";
  - `sbPause`, `cbGameSpeed`, `sbAutoRunBattle` "Auto run battle" (`AutoRunBattle` field), `sbEndGame`,
    `sbSave` (a `.sac` battle save).
  - Timers: `tmrRunMinuteTimer`, `tmrMoveIntervalTimer`.
  - Field `CampaignScenario`. The core simulation procedures are mostly unnamed.
- **Control and visibility.**
  - `TForce.AIControlled`, `TDivision.AIControlled` / `PlayerAIControlled` / `LockedAI` / `Side`.
  - `TFightingShip.Side`, `ForceIsHumanControlled`, `PlayerAIControlled`, `DivisionIsAIControlled`,
    `SeenByHumanControlledShips`, `AskedPlayer`.
- **Campaign side.**
  - `CheckForBattles2` → `DoBattle(anOpponentIndex, aMission, anArea, aPossession)`, the tactical battle for
    PlayerNation → `HandleBattleAftermath(VP0, VP1, aVictoryLevel)`.
  - `DoAutosaveBeforeBattle(EnemyIdx)`; `LoadScenarioInProgress(aFileName, anEnemyIdx)` resumes a saved battle
    (`[General] ScenarioInProgress`).
  - AI wars: `CheckAIvsAIBattles` → `CheckAivsAIBattleInArea` → `DoBattleBetweenAINations` (resolved without a
    battle screen); `QuickResolveRaiderBattle`; `DoPossessionBattle` / `DoHomeAreaBattle` / `FleetBattleCheck`.
  - `[General] BattleAlliedNationIdx` / `BattleEnemyAlliedNationIdx` (allies in battle).
- **Debug menu items** (possible test entry points): `Testbattle1Click`, `Testareabattle1Click`,
  `Generaterandombattle1Click`, `Battle1Click`.
- **Today, in the mod (0.3.3):**
  - Joined players fight their battles against AI nations on their own PC: the host defers `DoBattleBetweenAINations`,
    the player fights in a battle slot (client slot + 10) made with the Copy-game transform, and the host merges the
    result field by field (`saveio::ApplyBattleResult`).
  - Battles between players are fought live in lockstep (`src/mod/lockstep.cpp`): a seeded `Random32Proc`, simulation
    and display contexts, a minute gate, an order guard with state sync, a per-minute hash with resync from a `.sac`
    snapshot, and AI takeover on silence.
- **Lessons from building it** (apply to any future battle work):
  - **Display code writes simulation state.** `Hidden`, the identification and the sighting fields are written by
    GlobeRender and the ship panel. Each ship keeps a simulation and a display copy of these. Run `ls leakcheck 1` in
    every live test: it flags any simulation change made between steps.
  - **The load's own drawing identifies ships** (found live 2026-10-17 on the 0.6.0 branch; 0.3.0-0.3.2 do the same):
    the battle save keeps no identification (`TFightingShip.Create` sets `IdentifiedAsType` (+0xd70) := 0x16 at
    0xe1c6bf, `IdentifiedAsClass` (+0xd6c) stays empty, `LoadMe` reads neither), but the battle screen's first drawing
    during the load names enemy ships through `GetTypeAndNameForEnemy` (0xe343d4) -> `IdentifyType` (0xe40268) and
    `IdentifyClass` (0xe3ffc4), which draw `Random` with this PC's display draws and write both fields (27 of 47 ships
    in the 1900 test battle, 16 in the 1935 one, on each PC). Up to 0.3.2 the first display swap took those values as
    the simulation's set, so both PCs' simulation identification differed from minute 0 (the hash line's `ident=`
    differed in every minute of every battle). The sighting reports routine (unnamed 0xdc960c, called from EOTP at
    0xdaf8dc, inside the simulation) calls `IdentifyClass` (0xdca190) and `IdentifyType` (0xdca56e) with shared draws
    whose number and results depend on the current identification (`IdentifyClass`: `Random(100) < 25`, then a
    comparison with the current class string decides the next branch), and raises "Unknown ship sighted!" (0xdca9cf,
    once per report kind and battle screen, when PlayerSide is not 2): so a ship identified differently on the two PCs
    made the battles drift apart now and then, next to that box (the rare DESYNC: with `rng=` and the sim draw counts
    differing first on the 0.6.0 branch; with pos and ord first and the RNG equal in 0.3.2's `lockstep_ui` e), where an
    AI decision that reads the identification is a guess). Since 0.3.3, right after every live load the simulation's
    set is the constructor's (0x16, no class) on both PCs; the display keeps what it drew. The identification is not
    part of the hash's `all`, so a 0.3.2 PC and a 0.3.3 PC still hash equal states equally, but their simulation
    identification differs as between two 0.3.2 PCs.
  - **The division report writes simulation state too** (found live 2026-10-17 on the 0.6.0 branch, present since
    0.3.0): `TdlgDivisionReport.FillShipList` (0xae1874; called only by the report's `Execute` and the torpedo
    dialog's `bnCloseClick`/`bnFireClick`; no random draws) sets each listed ship's `CurrentSpeed` (+0xa48) to
    `GetMaxWeatherSpeed` when that is lower (0xae1972..0xae1990), on the PC where the report opens only (leak check:
    `<ship>.CurrentSpeed 1c000000->1b000000`, then a DESYNC). Since 0.3.3 the mod hooks it as a display context and
    puts every ship's `CurrentSpeed` back after it (`ReportSpeeds*`); the simulation lowers it in the next minute on
    both PCs. Opening a report also writes the division's `DivisionSpeed` (an order field: the order guard sends it as
    an order).
  - **Game speed is simulation state** (intervals per minute). Change it only at minute boundaries.
  - **Never free a form inside a nested message loop.** Popup menus, move/size loops and native message boxes don't
    raise `ModalLevel`. Require the outermost dispatcher depth, no capture and no menu mode.
  - **Every flag set around a game call must be restored in `__finally`.** A Delphi exception escapes C++ `catch(...)`
    under /EHsc.
  - **The game can end a battle inside a minute** (end conditions in EndOfTurnProcedures). Its close path runs
    FormClose → DoStrategicTurn, so defer the close until the minute is over.
  - **A toggle button is down before its click handler runs.** VCL's `TSpeedButton` (with a `GroupIndex`) toggles
    `FDown` in `MouseUp`, then calls `Click`: blocking the handler leaves the button down. If the game reads the
    button, put it up again (0.3.2, *Fastest available*).
  - **Modeless battle dialogs hold raw pointers** to ships, divisions and air formations. Hide them before the battle
    is loaded again (0.3.2, `ResetBattleForm`).

### Battle commands
From a static reading of 1.01.44 on 2026-10-16 (no game run; summary and evidence in
[MODLOG](../MODLOG.md#investigation-2026-10-16-the-limits-of-live-battles-static-v031)). Image addresses; "guess"
marks what the disassembly doesn't prove.

- **Player commands and the routine each one commits through.** The handlers work inline, often after message boxes.
  - Manual torpedo salvo: the division report's *Fire torpedoes* opens `TdlgTorpTargets` (modeless); its *Fire*
    calls `TfrmMain.FireMount(aMount, aShip, aTarget, FireAngle: Single, TorpSpeed, Range, Manual)` (0xda8cc4,
    `ret 0x14`). The dialog draws `Random` itself (its firing solutions, the angle jitter); `FireMount` (10 sites)
    and `TFightingTorpedoMount.SetReloadTime` (7) draw too. The AI fires through the same routine (`Manual = 0`).
  - Manual missile salvo: the missile panel's *Manual fire* opens `TdlgMissileFire` (modal), which calls
    `TfrmMain.FireMissileMount(aMount, aShip, aTargetShip, aTargetLocation, TargetBearing, NumberToFire): Integer`
    (0xe04718, `ret 0x10`; it calls `FireSSM`, 0xda6374). The dialog also writes `TFightingShip.PendingMissileTarget`
    (+0xa93; fired later by `FireAllGuns`; not in the battle save) and fires missile submarines with side 0
    hard-coded (a game bug for a player on side 1).
  - Air strike: the toolbar's `sbStrike` ("Order an airstrike") or the reports list's *Launch strike* opens
    `TdlgStrike` (modeless). *Launch* and *Ready* call **LaunchSomeAircraft** (0xea4a18, named after its error
    message; EAX = the squadron, EDX = the number, CL = the mission, 5 stack arguments, `ret 0x14`), which creates
    the formation; `bnLaunchClick` then writes more fields inline (`GroupIndex := Random(10000)`, coordination,
    priority target). *Stand down*, mission, loadout and number are plain field writes. The AI launches through the
    same routine.
  - Routine air operations: the toolbar's `sbRoutineAirOps` ("Manage CAP and air recon") or the ship tree's *Search
    pattern* opens `TdlgSearch` (modal). Saving writes 9 `TForce` fields (CapLevel, NightCAP, SearchLimitLeft,
    SearchLimitRight, SearchDistance, SearchInterval, TwoPhaseSearch, RepeatSearch, NightSearch; `sub_b4bef4`) and
    calls `TForce.ProcessSearchDirectons`. These are settings: the simulation launches CAP and search aircraft from
    them every minute, for both sides. NightCAP is not in the battle save.
  - Land-based CAP: the division report's `sbLandCAP` button; when it is down, the report's OK writes the global
    `LandCAPDivision[0]` (0xfb5920, one slot per side). Slot 0 only: the game assumes the human is side 0.
  - Change force: the ship tree's popup *Change force* lists the side's other forces. `Changeforce2Click` (inline,
    after a Yes/No) moves the division between the two forces' `Divisions` lists, sets `myForce` and lowers `Admin`
    by 1. The menu needs a nation battle flag (`tech+0xdc`; guess: command capability).
  - Change lead and battle turn-away: the Force orders dialog, then `TdlgSelectLead`'s OK, call
    `TfrmMain.SetForceLeadDiv(aDivision)` (0xde42fc) and `TfrmMain.DoBattleTurnAway(aNewLeadDivision)` (0xde4550),
    both with the implicit `frmMain.SelectedForce` and without `Random`. Both move the new lead to index 0 of
    `Force.Divisions`; the list order decides the force flagship (the first lead division). The turn-away also
    reverses the ship order of every division in the lead chain and starts a 15-minute cooldown (`TimeToTurnAway`);
    its button needs `tech+0x80` (guess: the doctrine).
- **Dead or developer-only** (no player reaches them, in any battle):
  - Emergency activation: `TfrmMain.sbEmergencyActClick` is a bare `ret`. The button is hidden and disabled in the
    form; only `CampaignScenario` (never set to True) or the unreachable operational-campaign module `TfrmCampaign`
    would show it, and `LoadScenarioInProgress` (the resume path, also the live battles') hides it again. The Force
    orders dialog's button has no handler and is shown only for `CampaignScenario`; its label is always visible.
  - Try for home (`TryforHome1Click`: the AI's head-home course for the selected division, shown in a message box)
    and Enter port (`ShowAircraft1Click`: forces the "division enters port" routine): developer menu items that
    `TfrmMain.FormCreate` hides unconditionally, with Sink, Teleport, Create MTB, Blowup and the other debug items.
  - `TfrmMain.bnFireClick` (a debug list of all divisions), `Launchstrike2Click`, `Bombship1Click` and
    `TdlgDivisionReport.bnFireMissilesClick` (with it, the missile branch of `TdlgTorpTargets.bnFireClick`): only the
    RTTI method tables refer to them. `cbFastestAvailableClick` has no control; only auto-run calls it.
- **UI controls the simulation reads:**
  - *Fire at will* (`cbFireAtWill`, missile panel): while it is unchecked, EOTP's automatic missile fire (0xda91fc,
    years after 1940) skips PlayerSide's divisions (some AI-controlled forces excepted); the other side always fires
    at will.
  - *Fire mode* (`rgMissileDoctrine`: Ripple, Half, All): raises the chance that PlayerSide's ships keep firing from
    40 % to 65 % or 95 % (0xda7ccc).
  - *Fastest available* (`sbFastestAvailable`, a toggle: `AllowAllUp`, `GroupIndex` 579): the speed governor reads
    its `FDown` (`TSpeedButton`+0x1e8) at minute boundaries (StartMinute, StartRun, the contact update in EOTP).
    Down: speed 5 (*Ultra fast*) without contact, at most 2 (*Fast*) in contact; up: in contact a speed above 2 drops
    to 1 (*Normal*).
  - The arrows next to the speed box (`UpDown1Click`, 0xdd00d8) change the speed at once: up while `(GameSpeed < 5
    and not Engaged) or GameSpeed < 2`, down to -3 (and *Fastest available* up).
- **The AI's air operations follow PlayerSide, not the AI flags** (to be confirmed live).
  - `CheckAIAirstrike(side)` (0xdfd50c, StartMinute, both sides) and its squadron picker skip PlayerSide's
    squadrons unless their base is an airfield; nothing checks `AIControlled`. With PlayerSide 0 in the simulation,
    the AI flies side 1's carrier strikes and never side 0's.
  - `TfrmMain.SetAIAirSearchPlan` plans every force at battle generation. In EOTP, every 120 minutes after minute
    600, it re-plans the search sector, distance and CapLevel of the forces again; it skips PlayerSide's, except
    those led by an airfield.
- **Air formations.**
  - One global `TList` (0xfb0108) holds every `TAirFormation` of both sides. The key is `AFId` (+0x04): unique,
    from the counter at 0xfb0120, kept in the battle save; Parent and Lead links are rebuilt from it on load. Names
    are not unique.
  - A squadron is itself a formation (status `asUnready`). A launch appends a child formation (ParentAirFormation =
    the squadron) and takes its aircraft from the squadron; back on deck, `MergeAirFormationWithParent` gives them
    back and the child is deleted and freed.
  - During a battle only LaunchSomeAircraft and the parasite-fighter routine create formations (appended in creation
    order), and only EOTP removes them (`MoveAircraft`, `TAirFormation.CheckStatus2`).
  - *Show air formations* (`TdlgShowAirFormations`, display only) lists PlayerSide's formations; EOTP refreshes it
    inside the minute. Its list and the strike dialog's hold raw `TAirFormation` pointers that EOTP or a load frees.
- **Missiles are torpedo objects:** `TTorpedo` with `Calibre >= 30` (`IsMissile`), in the same global list as the
  torpedoes. Missile launchers are `TFightingTorpedoMount`s with `MountType` 1-6 (heavy, medium and light SAM and
  SSM).
- **Eras** (reference years in the game's `Data\ResearchAreas3.dat`): land-based naval aircraft and seaplane carriers
  1915, flight decks 1918, CVL 1920, CV 1921/1926, ship catapults 1923, night air operations 1930, two-phase search
  1942; air-launched ASM 1943, SSM 1947, SAM 1953. The code runs automatic missile fire in years after 1940, the
  missile panel after 1942 and missile submarines after 1950. Campaigns start in 1890, 1900, 1920 or 1935
  (`TfrmSelectNation2`); the regression suite's test campaign (slot 1, 1900) has no aircraft.

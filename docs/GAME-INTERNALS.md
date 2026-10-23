# Game internals

Reverse-engineered facts about Rule the Waves 3 1.01.44 and the techniques the mod builds on. Read this before you
work on code that touches the game. The current design of the mod is in [ARCHITECTURE.md](ARCHITECTURE.md); the
evidence for each fact (disassembly excerpts, log lines, measurements) and the full history are in
[MODLOG.md](../MODLOG.md).

## Contents
- [How to read the addresses](#how-to-read-the-addresses)
- [Where the facts come from](#where-the-facts-come-from)
- [The game and the mod: facts you can rely on](#the-game-and-the-mod-facts-you-can-rely-on)
- [Relations, wars and battles: nation 0's and the others'](#relations-wars-and-battles-nation-0s-and-the-others)
- [Research speed and the missile era](#research-speed-and-the-missile-era)
- [Intelligence, trade protection and foreign stations](#intelligence-trade-protection-and-foreign-stations)
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
  the turns, the client's new month, every live battle start, resync and rejoin) doubled the air units in memory and
  in the next save (seen 2026-10-17 in a 1958 test campaign: `AirUnitNo` 595 -> 9520 = x16 after a session, a live
  battle, two resyncs and one month). Effects: the host's Turn stops at `PlayerChecksBeforeTurn` ->
  `CheckAirBaseCapacity` (0xd20900) "CV <name> aircraft capacity exceeded!" for a carrier with an air group, the air
  upkeep explodes (funds "+ -411,149" in a month). The battle setup reads the same list: `AddScenarioAirUnits`
  (0xbf71b8, named by its error text; from `DoBattle` through 0xbdbccc and 0xbe2de0) creates a battle air formation
  for every unit whose `HomeBase` (+0x22) is the battle ship's campaign ship (`FindCShipWithId`), so a doubled list
  most likely doubled the squadrons of battles set up in that process (static reading, 2026-10-17; not seen live).
  Since 0.3.3 the mod drops and frees the units it held before a load once the load succeeded, and repeated units of
  a save: `AirUnitId` (+0x04) is unique in a campaign the game saved (a new unit takes the next value of the
  campaign's `[General] IDNo` counter, 0xd79de4: `mov eax, [0xf5318c]; inc dword ptr [0xf5318c]`). Nothing else keeps
  `TAirUnit` pointers: `TAirFormation` names a `TAircraftType`, and `TdlgCAG.CopyFromAirUnit` (the only field of that
  type in the RTTI) is cleared when the dialog shows (0xb63c08). `TAirUnit` has no destructor of its own
  (`TObject.Free`, as `ClearAirData` frees them). Every other list counter of the save stays. The reload doesn't free
  the old campaign's nations and ships (`LoadBuildCampaign` drops them with `BuilderNations.Clear`, 0xc99a91, then
  creates new ones: part of the memory growth per reload), so an old unit's `HomeBase` still points to the old ship
  object: base-matched code (the air group dialog's base views, `CheckAirGroupNumbers`, the battle setup) sees the
  old copies only after a save and a reload resolve their base ids, which are right.
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
- **Every load credits blockades** (found 2026-10-18; [MODLOG](../MODLOG.md#fix-2026-10-18-victory-points-added-on-every-reload-v040)).
  `LoadBuildCampaign` sets `PlayerNation := BuilderNations[0]` (0xc9a69d) and ends its "Setting data" step with
  `CheckBlockadeStatus` (0xccc1c4, called at 0xc9abeb), which sets the nations' `Blockaded` flags (+0x1c5, not saved)
  and also credits blockades: while the war counter (0xf52ff4) is above 0, for each nation 1..8 with `Tension` > 40,
  `PlayerNation.VP` (+0x175) += (Random(3) + FleetSize) * 20 when that nation is blockaded, `EnemyVP` (+0x4a63) += the
  same when PlayerNation is (FleetSize: `[General] FleetSize`, the global 0xf4c914; 3 in the 1900 test campaign, so
  60, 80 or 100 a blockade). Its other callers are the month's `DoTurn` (0xce0f87) and `DoStrategicTurn` (0xce1bb9).
  `TBuilderNation.IsBlockaded` (0xc618e8) ignores its argument: `CheckBlockade` of the nation's `BuildArea` or
  `BlockadeArea`, true when a nation at war with it has a `GetBlockadeStrength` (its active ships in the area times its
  `BlockadeModifier` (+0x206), plus allies at war) above `GetMinimumBlockadeStrength` (FleetSize * 8 + 16) and above the
  nation's own times a factor. So every load of a campaign with a blockade gave nation 0 (or EnemyVP) points the save
  never had, once per load: the game loads once per process, the mod again and again (every month, every live battle
  load). Since 0.4.0 the mod puts back the VP and EnemyVP of the load's own call (told apart by its return address;
  its flags and `Random` draws stay) in multiplayer sessions and for its own reloads; a load without a session keeps
  the game's credit, as in single player. Nothing else on the load path writes VP or EnemyVP (a scan of every
  `[reg+0x175]` and `[reg+0x4a63]` write). A test lever: raising nation 0's `BlockadeModifier` (`nfield 0
  BlockadeModifier 1000`) blockades its enemies from the first check of a war.
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

## Relations, wars and battles: nation 0's and the others'
Static reading of 1.01.44 and live runs of 2026-10-20 ([MODLOG](../MODLOG.md#investigation-2026-10-20-each-players-own-relations-tension-wars-and-battles-of-a-joined-players-nation-v041)).
The game is written for one player, nation 0; it runs every other nation, a joined player's nation K included, as AI.

- **Tension per nation.** Each `TBuilderNation` holds one array: +0x256 `Tension` (the relation with nation 0) and
  +0x25a `AITension[1..8]`; alliances `Allied` +0x29a (with nation 0) and `AIAlliance` (cells +0x276+4j for j 1..8);
  war = above 40. Nation a's relation with b: `b.Tension` when a = 0, else `a.AITension[b]` (`SetAITension` writes both
  directions). `game.cpp`: `TensionCell`, `Tension(a, b)`, `AllianceMonths`. So K's relations with every nation exist
  in the data, apart from the host's.
- **The screens read nation 0's relations** and compare with `PlayerNation` (+0x3204): the strategic screen's tension
  panel (`UpdateUIValues` 0xc8018c names slots 1-8, `ShowTensionLevels` 0xc7f4d8 / `ShowOneTensionLevel` 0xc7f1e4,
  `lbWar<i>`, `imgAlly<i>`, `lbBlock<i>`, `stIntel<i>`), the flags (`SetValues` 0xc7fbcc; a click:
  `TdlgNationData.Execute(slot)`, the player's own flag 0), the War results box `gbWar`, `lbWar` "At war for N months."
  and `lbPlayerNation`'s hint "At peace for ..." (the war counter), the map's strength bars (`DrawAreaStrengthBars`
  0xd10900: nation 0 silver, the others by `GetTensionColor(Tension)`). `TdlgRelations` (the 9x9 matrix) shows every
  pair. Helpers: `IsEnemy` 0xc66410 (the player's coalition only), `NationIsAtWar` 0xc634f8 (PlayerNation: War > 0),
  `IsAtAIWar` 0xc68ecc (false for PlayerNation), `GetRandomEnemy` 0xccd5cc (Tension > 40), `GetRandomEnemyForAI`
  0xc77500 (AITension[1..8]). Side effects: while War > 0 `UpdateUIValues` calls `CancelTreaty` (a box when a
  disarmament treaty ends) and zeroes every nation's DisplacementLimit. Since 0.4.1 a PC whose PlayerNation is nation
  K > 0 shows K's relations (the relations view, ARCHITECTURE).
- **The war counter** `[General] War` (global 0xf52ff4; `0xccd5fc` = War > 0) is nation 0's: months at war (> 0) or
  at peace (<= 0); `WarStartsWithNation` 0xd333bc sets 1 when it was <= 0, DoStrategicTurn (0xce1f69) adds 1 at war and
  subtracts 1 at peace (at -2 a clean-up 0xd26fc8 runs), `DoPeace` ends the war (War := 0, every nation's
  `WillGoToWar` and `Blockaded` cleared). K has none; since relations v2 the host keeps one per joined nation and the
  joined player's PC uses it in place of the host's (ARCHITECTURE, [each joined nation's relations](ARCHITECTURE.md#each-joined-nations-relations-relations-v2)).
  Readers besides the war machinery: `GetModifiedBudget`, `GetTotalMonthlyTrainingCost`, `CheckFuelShortage`,
  `DoResearch`, `ReduceTimeLimits`, `EventLikelihoodCheck`, `CheckUnrestChanges`, `CheckPlayerFunds`, `SmallFleetCheck`,
  `AIMonthlyBuild`, `UpdateUIValues`. The screen (`UpdateUIValues`): at war the War results box `gbWar` and `lbWar`
  "At war for N months." are shown and the player name's hint is emptied; at peace `lbWar`'s text is emptied (or a
  treaty's "Peace treaty displacement limit applies for N months.") but the label is never hidden again, and the hint
  reads "At peace for N months." (under 12 months) or "At peace for Y years[ and M months.]".
- **Nation 0's machinery.** Tension: `RandomTensionChanges` 0xc81100 (from `CheckTurnEvents`; about 50
  `TensionChangeWithMessage` 0xd46a3c calls with explanations, which add "Tension with X increases <why>" to the turn
  messages when a setting allows it, [0xf278b4]+0x361; speeches, questions to the player) and
  `TensionEffectOfPreviousWars`. Wars:
  `CheckForWar` 0xc84f04 (from DoEndOfTurn: common-enemy relief, the `WillGoToWar` countdown +0x1bd with spy warnings
  and rumours, declarations, allies joining on either side, surprise attacks and landings) -> `WarStartsWithNation`
  (Tension 50, War := 1, Allied and TechSharing 0, both nations' BudgetModifier raised). Peace: `GetPeaceEvent`,
  `NegotiatePeace`, `DoPeace`. War effects: `CheckBlockadeStatus`' VP, `CheckRaiderActivity`, `WarHelpFromAllies`,
  `CheckBuildingShipsForWar`, war loans in `CheckTurnEvents`. Battles: `CheckForBattles2` 0xcecb10 (only while War > 0;
  PlayerNation against nations with Tension > 40: invasions, raiders vs patrols, home areas, possessions,
  `GetRandomEnemy`, each through `CheckAttackLocation`).
- **The AI's machinery** (every other nation, K included): `CheckAITensionEvents` 0xd2dc68 and `DoAITensionChanges`
  per nation; `ChangeAITension` 0xc67904 moves a pair by +-1 (its cause goes into the host's turn messages: "Tension
  between USA and France decreases because ..."; questions to the player when a pair involves its ally). An AI war
  breaks out (0xd2e598) for nation i's most tense partner when 10 < t < 40 and Random(100) < t, only while nation 0 is
  NOT at war (0xd2e602; otherwise the tension drops by 1), with "War has broken out between X and Y!" on the host's
  screen, `SetAITension` 50 and the allies (`AllyWithAINationsFightingTheSameEnemy`, `CheckIfAlliedAINationsJoinWar`).
  AI peace: `HandleAIPeace` 0xd32f98 (war weariness, while nation 0 is not at war and the nation not at war with it, or
  forced by unrest; "The war between ... has ended", VP of nations 1-8 zeroed). So a joined nation's wars with AI
  nations follow these rules and freeze while the host is at war; nothing of it reaches K's PC. (Relations v2: not for
  a joined nation under the player's rules, whose pairs these routines don't see, ARCHITECTURE.)
- **Battles between nations the game runs.** `CheckAIvsAIBattles` 0xd32a50: for pairs i, j in 1..8 with AITension > 40
  (nations walked in order, 50 % each) the areas are tried with `CheckAivsAIBattleInArea` 0xd32c2c (50 %, ships of
  both in the area, Random(10) < the counts, then a size-based chance) -> `DoBattleBetweenAINations` (resolved without
  a battle screen; the mod defers a joined nation's battle to its battle phase). A joined nation with a low index
  usually attacks.
- **`AIDeclinesBattle`** 0xcd2188 (from `CheckAttackLocation`): when the AI side declines, "The <nation> navy declines
  battle! You gain N VP!" and PlayerNation's VP += N (`add [eax+0x175], edx`), before any preview (the battle setup
  `DoBattle` is not entered). The player's own decline in the preview gives the enemy VP ("Decline will give the enemy
  ..."; `DoPlayerDecline`).
- **Nation 0's per-relation state lives in the other nations' records** (static, 2026-10-20): `Tension` +0x256 and
  `Allied` +0x29a (the relation), `WillGoToWar` +0x1bd (the nation's war plan against nation 0, counted down by
  CheckForWar; DoPeace clears it), `TechSharing` +0x29e (nation 0's tech-sharing deal), `IntelEffort` +0x1ac (nation 0's
  intelligence on it: the chance of CheckForWar's spy warnings), `IntelligenceSpending` +0x171 (nation 0's spending on
  it: RandomTensionChanges' "annoyed by our extensive intelligence activities" when the nations at peace add up high).
  A joined nation K has its own values of all of them (relations v2 keeps the first four for K, ARCHITECTURE; the
  intelligence ones are the per-player intelligence's).
- **Every routine that changes relations between nations 1..8** (callers of `ChangeAITension` 0xc67904,
  `SetAITension` 0xc6871c, `SetAIAlliance` 0xc686a4, `HandleAIPeace` 0xd32f98; static, 2026-10-20): the AI's own
  (`CheckAITensionEvents`, `DoAITensionChanges`, `CheckIfAlliedAINationsJoinWar`, `AllyWithAINationsFightingTheSameEnemy`,
  `CheckPossibleAIAlliance`, `CheckAivsAIBattleInArea`), reactions to nation 0 (`CheckStopPlayerAgression`,
  `CheckAllyTrouble`, `CheckForceOnForeignStations`, `CheckChineseConcessions`, `CreateArmsControlTreaty`,
  `CheckBlitzkrieg`, `CheckForRevolutionInEnemyNation`, `CheckBuildingShipForFC`), `ReduceTimeLimits` (an ally of nation
  0 at war "has negotiated a separate peace with our enemies!": its tension with everyone set to 2-4, its alliances
  ended), nation 0's own `CheckForWar` (volunteers: "X has entered the war against Y on our side!" = SetAITension 50
  against every enemy of nation 0) and `DoPeace`, `SetInitialAITensions` (campaign start), the relations window's debug
  click. `0xa07124` is `ShowMessagePos(text, -1, -1)`: a modal box ("War has broken out between X and Y!", "X and Y have
  let their treaty lapse due to rising tensions.", "The alliance between X and Y has expired.").
- **`CheckAITensionEvents` in detail** (per nation i 1..8): AI alliances run down while the world is at peace;
  `AIWarLength` (0xf52ff8) counts months with an AI war; a nation at AI war raises its BudgetModifier and makes peace by
  war weariness (`HandleAIPeace(i)`, chance from AIWarLength and unrest; only while nation 0 is at peace and i not at
  war with it) or by unrest ("Racked by unrest X is forced to seek peace with its enemies!", alliances ended); per pair:
  the alliance desync fix, common enemy -> -1; "X is seeking improved relations with us due to ... What do you
  recommend?" (a question to nation 0 for the pair (0, i)); the treaty lapse (alliance > 3, tension > 5), renewal or
  expiry (alliance = 1), an alliance attempt at tension 0 (`CheckPossibleAIAlliance`); a nation tense with nation 0
  (> 7) drifts by government type and nation 0's alliances; the outbreak (above); `CheckIfAlliedAINationsJoinWar`.
- **The player's event chain** (`CheckTurnEvents` 0xce30f0): one of these per month, in order: at war the peace event
  (`GetPeaceEvent` 0xcc2a24: a random event slot whose condition is the code 30-40 the inline decision picks from VP,
  EnemyVP, war months, force ratio and `CheckTotalDefeat`, shown by `TdlgEventAnswer.Execute`); officer stuff,
  `BuyTechnology`, at peace `SmallFleetCheck`, `CheckForPossessionEvent`; a random event (`GetRandomEvent`, 1 in 3);
  `CheckIntelligence`; else `RandomTensionChanges` (also after an intelligence report, 1 in 10 at war, 1 in 8 at peace);
  then at peace `TensionEffectOfPreviousWars` (War < -6: per nation, months since the last war with it from the month
  records, form+0x795f.. per year 0x4B0 / month 100: opponents and outcome, via `CheckWarsWithNation` 0xd4ddc4; -1 with
  "because of previous war." and similar). `CheckForWar` runs from `DoTurn` and from `DoEndOfTurn` (after
  `ShowTurnMessages`). `ShowTurnMessages` 0xc8e3c8 shows the form's `TurnMessages` (+0x3208, a TStringList) in
  `dlgCampTurnRep` ("Turn messages", modal) when it is not empty, and leaves the list as it is.
- **`CheckForWar` in detail** (per nation i with tension < 40): `CheckIfAlliedNationIsAtWar(i)` (i allied with both
  sides of a war of nation 0 picks one: "X cancels their alliance with us ..."), common enemy -1; at peace a surprise
  landing before a war (SurpriseAttack trait or government 4/5; `WillGoToWar` = 1 or Random(300) < t >= 10; tension set
  to 14; "... troops have made a surprise landing in our possession ..." or ChangePossessionOwner "In a surprise move,
  ... have taken control of ...!"); the war plan countdown `WillGoToWar` (+0x1bd) with tension rising and "rumours in X
  about war with us" (chance from the nation's `IntelEffort` +0x1ac); a declaration when tension >= 13 + the nations
  at war with nation 0 (or 12 with chances, or >= 8 at the plan's end): tension 13, "X has declared war on us!" (or
  "has joined the war against", "... are now at a state of war!", "War has broken out between"), `WarStartsWithNation`,
  the alliance checks for every nation, nation 0's BudgetModifier, VP reduced when War > 6, War := 1; else at War <
  -48 a new war plan (3-8 months) at tension >= 11 with the spy reports "Our spy in X reports that they plan to declare
  war on us in N months!"; else the plan dropped ("no longer considering war with us a risk"); then the volunteers
  (tension 0, not allied, nation 0 at war, 10 %); after the loop BudgetModifier +1 for everyone not at war when a war
  started, and `DoSurpriseAttack`.
- **The player's peace** (static, 2026-10-21; relations v2 B4 runs it for a joined nation, [MODLOG](../MODLOG.md#feature-2026-10-21-relations-v2-b4-peace-with-computer-nations-relations-v2)).
  The decision is inline in `CheckTurnEvents` (0xce32c7..0xce37d6): at war, when `Random(4) + 2 < War` (else at war no
  event at all that month): the war loans at `War mod 3 = 1`, then when `War > 4`, VP or EnemyVP above 250 G (G =
  FleetSize [0xf4c914], the `EventLikelihoodCheck` scale) and (`Random(30000) < VP + EnemyVP` or `Random(100) < War`):
  code 36 (EnemyVP > 300 and > 2 VP, 1 in 5), 34 (VP > 300 and > 2 EnemyVP 1 in 5, > 3 EnemyVP 1 in 4, or > 1.5
  EnemyVP with an enemy's unrest > 6 and no weaker economy, 1 in 10), 33 (an enemy without shared areas, or VP near
  parity in a long war); 34 -> 33 at a force ratio (`GetTotalForceRatio`) below 95, 36 -> 35 above 110. With `TotalWar`
  a Fascist/Communist player keeps only 34 (unless `Random(200) <= War`) and `CheckTotalDefeat` (War > 12, VP > 500)
  may call `NegotiatePeace(100, +-5..6)` itself. `GetPeaceEvent(code)` 0xcc2a24 shows an unused event slot whose
  condition is the code (Used +0x54 := 6, `TdlgEventAnswer.Execute(event, 99)`); answers with effect codes 50-56 call
  `NegotiatePeace` (40,0) (25,0) (30,2) (70,-2) (20,-1) (50,1) (10,1). `System.Random` is 0x837834 (`push eax; call
  [RandomProc]; pop edx; mul edx; mov eax, edx`).
- **`NegotiatePeace(aChance, aLossGain, ForceBadPeace)`** 0xcbab88 makes peace with every enemy (Tension > 40) at once:
  hawks (Officers.jpg) and, with TotalWar, the enemy's or the player's refusal speech; modifiers of lossGain and the
  chance from the force ratio, unrest, economies, VP and war months (ForceBadPeace with -5: -2..-5 without them, and
  acceptance forced); acceptance `Random(100) < chance`, else "No common ground ..." (Negotiation.jpg). Outcomes: <= -6
  utterly defeated (BaseResources -10 % or -1000, prestige, HasLostWar, 2-3 ships handed over, a build limit of 5-12
  years, the enemies' possessions choice, alliances may end), -5..-2 considerable, -1 minor loss, 0 compromise, 1 minor
  gain (+200, Prestige +1), 2..10 large gain (+1000, the enemies HasLostWar); the war record cell of the month
  (`MonthRecords[Year, Month]` +0xC: 1 won, 2 lost, 3 compromise; enemy slots +4/+8); for gains
  `SelectPossessionsAfterWar` 0xcf4514 ("Select possessions as part of peace agreement", `TdlgPossessions`: the
  enemies' possessions next to the player's, worth < 11, budget 2 x lossGain, the unused budget as BaseResources; its
  `bnOKClick` 0xc188d8 keeps the dialog open after "Total value of selected possessions is higher than permitted total
  value!" (0xc1863c, a ShowMessage) and asks "You have not selected any possessions. If you leave points unused ..."
  (Yes/No, No keeps it open) when nothing is selected), the
  allies' and enemies' choices (`AINationSelectPossessionsAfterWar` 0xcf4b64: never nation 0's own possessions),
  `ReturnTakenPossessionsAfterWar` 0xd259bc (`ChangePossessionOwner`, `GetBNation(TakenFrom)`: TakenFrom and Invaded
  are nation indices), `ScrapShipsAccordingToTreaty`, the "Peace treaty" (`ShowPeaceTreaty` 0xd3d9a4,
  `TdlgTotalLog`, only with lines), then `DoPeace`.
- **`DoPeace`** 0xcbc6f8: every enemy's tension 1..4 and every war of an enemy (`IsAtWarWithPlayerOrPlayerOpponent`
  0xd4a830 = `BNation(i).Tension > 40`; its loop over the opponents is dead code) at 1..5, the other relations down
  (80 %: 1..3), `WillGoToWar`, `Blockaded` 0 and `SubmarinePolicy` 1 for all, BudgetModifier by the war's length
  and unrest down for all, Prestige of nations 1..8 := 0, VP, EnemyVP, TotalWar, invasions, every possession's
  TakenFrom/Invaded cleared, interned ships back, the AMC and auto-built submarine questions, `War := 0`; it leaves
  DisplacementLimit, HasLostWar, BaseResources and alliances. `TransferShipAsReparation` 0xccdef8 (Self, from, to;
  ship, Rename, reason) moves the ship object (CShipId kept), copies its design for the receiver (a new design id),
  clears its air units, crew -20, puts it in the receiver's build area and logs the hand-over in `myLogEntries`
  ("<date> <reason><nation>", renamed "<date> Renamed <name>" with a name from `GetNameSuggestion`, then always an
  empty line, 0xcce0d3).
- **Loops over nations 0..8 read nation 0's own `Tension`/`Allied`** (0 for the player): `CountNationsAtWarWithPlayer`
  0xcc402c (Tension > 40, used by `CheckForWar`), `RandomTensionChanges`' count of nations at peace 0xc81293 (Tension
  < 40), `NegotiatePeace`'s enemies' gains (0xcbb34e, 0xcbbc79: Tension > 40), allies' gains (0xcbbc3b: Allied > 0) and
  `TerminateAlliances` 0xcbaa84 ("The alliance with <name> is terminated."). `TensionChangeWithMessage` skips the
  PlayerNation; `SetAITension` ignores Idx < 1.
- **The computer's money (relations v2, B5 money).** `ComputeExpenses` 0xca049c gives the player `Funds +=
  GetMonthlyBalance`; each nation 1..8 (loop body 0xca0511..0xca05b6, no test for people's nations) also gets a
  BaseResources floor of Round(1.5 x FleetSize), BaseResources x 0.9 at a BudgetModifier below 5 and subsidies of 25 x
  FleetSize (twice when poorer than the player and tense with it). `CheckBBNumbers` 0xccb6d8 (only from
  `CheckTurnEvents`' loop 1..8) gives +6000 x FleetSize and BudgetModifier +1. `ReduceTimeLimits`' keep-up (+1 toward the
  player's BudgetModifier, 0xc7ba85), its computer preeminence branch (0xc7b9f5) and January cuts (0xc7b303,
  0xc7b33e), a computer coup's +1 (0xd12edc), `CheckAITensionEvents`' four +1s, `CheckForWar`'s +1 for the nations at
  peace when the player went to war (0xc85a47), `WarStartsWithNation`'s +2/+1 (0xd333bc; the player's: +1 below 30, +1
  below 25 with Random(30) > BudgetModifier), `SpySuccess`' "secretly spending an additional 10%" (Funds +=
  ModBudget / 5, 0xc9d5f6), `DeHaltAllShips`/`HaltMostExpensiveShip`, and the maintenance discounts (naval academy 0,
  aircraft x 0.8, light carriers x 0.6) are the computer's alone. Shared by every nation: `AdvanceShipConstruction`
  (from `ReduceTimeLimits`, before `ComputeExpenses`) with `CheckForConstructionDelays` 0xc86314 (called for nations
  0..8: a delayed ship's month is paid back, "Technical problems have delayed the construction of ... by one month.")
  and the hoarding reclaim, and rules such as `HandleEconomicGrowth`. `GetMonthlyBalance` calls no Random and writes
  only idempotent values.
- **The player's own money rules (0.5.0, economy.cpp gives them to a joined nation K on the host).**
  - `CheckPlayerFunds` 0xcb4b44 (after `ComputeExpenses`; reads and writes only the player): debt messages with
    `ProportionalPrestigeLoss` (0xd276f8: Prestige div f when Prestige >= 2f, else 1) and `MoraleLoss` 0xd29040 (any
    nation: FleetMorale -1, each ship's CrewQuality and each air unit's PilotExperience -Random(2)-1); corruption
    (Funds -1000 and a question); the Air Force event (1950-1975, a question); hoarding 1 (Funds > 0.6 x ModBudget +
    20000, calm, 10 %: BM -1, the opera's -2000 x FleetSize for Bombastic or government 2, ArmyOffensive 12; at war only
    ArmyOffensive 4..6) and hoarding 2 (Funds > 40000 x FleetSize, 28 %: a tenth of the funds, BM -1, ArmyOffensive 8).
  - ArmyOffensive (form +0x4a6f) is one value: `GetModifiedBudget` makes every nation's budget x 0.9 while it is above 0
    (test at 0xc4f103); `ResolveArmyOffensive` (in `ReduceTimeLimits`) counts it down and at war resolves an offensive
    with victory points; `DoPeace` ends it (the reset in `WarStartsWithNation` 0xd334c0 is unreachable).
  - `CheckTurnEvents` 0xce30f0: exits in month 2 of 1890/1900/1920 and at War 0/-1; Random(4)+2 < War: the war loan in war
    months 4, 7, 10... (Unrest < 8, BM < 22, Random(16)+10 > BM: BM +1); War > 4: the peace decision; at peace
    `SmallFleetCheck` 0xc87dd4 (only caller): the host player's dialog and early events (A/B), complaints (prestige -1),
    budget rules D1-D4 under the form's ParliamentDelay +0x4a83 (D1: a threatening most tense nation: BM +1 after 36
    months of peace, before that its tension -1), the notice of the smaller share after 1920; then `BuyTechnology`
    (the host player's choice; any computer nation, K too, can be the partner).
  - The computer's remaining decisions in the month (they act on any nation 1..8 unless the mod keeps them away): K's
    doctrine on new research (`CheckNewResearchEffectsForAI`), ammunition each January (`SetAmmoLoadoutAndUsage`), ships
    sent home from colonies, ASW patrols and escorts with their cost in `CheckSubmarineActivity` (only nations at war
    with nation 0), air groups on carriers and bases (`CheckOneShipsOrBaseHasAircraft`), aircraft prototypes and
    licences (`AIDevelopAircraftType`; the player's own development is a request resolved for nation 0 only), the
    automatic scrapping of armed merchant cruisers after a peace (`ScrapShipsAfterPeace`; the player is asked), the
    purchase of another nation's scrapped ship. `AINationAdjustBudget` is the only source of "... is said to have
    increased the naval budget".

## Research speed and the missile era
What is known as of 2026-10-17 (static reading of 1.01.44, and a single-player test campaign played by a driver from
1935 to 1958 with ResearchSpeed 400 from April 1937). The evidence is quoted here; there is no MODLOG entry.

- **`[General] ResearchSpeed`** (the new-game option "Research rate", offered as 10..100 in steps of 10) is the
  global 0xf4c918. `LoadBuildCampaign` reads it without clamping (0xc99b33: `push 0x64`, `mov [0xf4c918],eax`) and
  `SaveBuildCampaign` writes it back, so a value above 100 edited into a save stays.
- **Monthly research** (`TfrmBuildCamp.DoResearch` 0xc9225c, once per nation and month):
  - Points = `GetResearchBudget / 3 / FleetSize + 1` (FleetSize, 0xf4c914, cancels out: the budget scales with it),
    +120 (+100 with poor education, +140 with technical excellence), times war, era and research-percentage
    factors, and last `Round(points * 140 * ResearchSpeed / 100)` (0xc927dc: `imul eax,[ebp-24h],8Ch;
    imul dword [0xf4c918]`): linear in ResearchSpeed. Even a nation at 0% research gets the +120 base.
  - The points are dealt round-robin to the started areas by priority. An area gains at most cost / 5 a month
    (0xc92df1); a tech costs the `Data\ResearchAreas3.dat` cost x 20000 (0xc906f8) and completes when the area's
    progress exceeds it (the progress restarts at 1).
  - **Ahead of a tech's year** (d = tech year - current year) the monthly gain is x1.5 for d < 10, then /16 for
    d >= 5, /4 for d = 4, /3 for d = 3 and x2/3 for d = 1..2 (0xc935f3); while d >= 1 the progress is halved in 8% of
    months (0xc9377c); while d >= 3 a completion is blocked in 70% of months (0xc938f3). **The Missile technology area
    (19) never completes a tech before its year** (0xc93954: progress > cost and the tech's year > the current year
    -> progress = cost - 1): no ResearchSpeed brings "Early surface to surface missiles" (1947) or "Early surface to
    air missiles" (1953) earlier; a high value only makes nations reach those dates on time.
- **ResearchSpeed's other uses** treat every value from 100 up like 100: `AdjustedYear` 0xd7ad98 (10..90 move tech
  years later, any other value leaves them), `AdjustTechs` (campaign creation only), `AIDesignShip` (the design year
  is the current year from 91 up), `AIMonthlyBuild`'s dock target, `CheckBTL`, `HandleEconomicGrowth`, private
  shipbuilding and dock growth (`Random(100) < RS`). `RS == 100 or Random(200) < RS` gates (gun-calibre progress, two
  carrier catch-ups) are weaker from 101 to 199 and certain from 200. A few catch-up gifts of pre-1931 techs (flight
  deck, carrier conversion, dive bombing, power training) fire only at exactly 100. `points * 140 * RS` stays inside
  32 bits up to about RS 1500 for realistic budgets.
- **AI missile ships wait for the calendar.** The AI designs from the `Data\IDes` templates of the design year
  (0xe882e0): SSM mounts appear in CL templates from 1946, CA 1948, DD 1950; medium SAM in BC templates from 1950,
  CL 1954, CV 1955, CA 1956, DD 1958. A design check (0xe84e10, nested 0xe84d64) removes the missile mounts a
  nation lacks the tech for: SSM needs tech 1901, medium / heavy / light SAM need 1905 / 1908 / 1910.
- **Observed** in the test campaign (ResearchSpeed 400, the AI nations left alone):
  - "Early surface to surface missiles": all six AI majors and Spain by 1947/07. "Early surface to air missiles":
    the USSR, USA, Japan and Italy by 1953/10, Germany by 1955/10, France not by 1958/04 (it got Heavy SAM first).
  - First AI ship in service with SSM: 1949/04 (a Japanese CL, 2 HSSM). Three nations with at least 3: 1953/04.
    1958/04: 14 to 37 per major nation, CL, CA and DD with 2-4 HSSM.
  - First AI ship in service with SAM: 1956/07 (a German CVL, 4 MSAM); then a Japanese CL (2 MSAM, 1957) and Italian
    and Soviet CLs with 4 HSSM and 1-2 MSAM (1957-58). In 1958/04 heavy SAM is only on ships under construction.
  - In peacetime the AI keeps most missile ships in reserve or mothballed (1958/04: USA 28 of 37, France 18 of 20,
    Germany 14 of 15).
- **Reading a campaign save for missile ships.** `Ship<i>Description` (written by `GetShipDescription` 0xd04e20)
  lists the mounts, e.g. `Guns: 12 x 6, 8 x 3, 2 MSAM, 4 HSSM, 60 Mines` (kinds `HSAM`, `MSAM`, `LSAM`, `HSSM`,
  `MSSM`, `LSSM`). `Ship<i>InPlay=1` is a ship in service (0: under construction). `Ship<i>Status`: 0 active,
  1 reserve, 2 mothballed, 3 trade protection, 4/5 raider, 6 on foreign station, 7 interned, 9 museum, 10 working up.
- **Single player at the Turn press** (what a test driver meets):
  - "<carrier> has no air units assigned! ... Press ignore to not be reminded again!"
    (`CheckPlayerCarriersHaveAircraft` 0xd45f38): Ignore sets `TCampaignShip.DoNotRemindOfAirGroup` (+0xf7), which
    `TCampaignShip.SaveMe`/`LoadMe` don't write. It is not a preference and lasts until the game closes.
  - "There are insufficient ships on foreign stations! Continue anyway?" (`CheckForceOnForeignStations` 0xcf5f54):
    the shortfalls of all areas are summed and compared with the colonial tonnage (0xd55b58: displacement and type,
    not the place) of all operational ships with Status 6. Status 6 set in the save, as the fleet screen sets it (it
    also clears the destination and ordered areas), ends the prompt and the prestige loss.
  - `TdlgEventAnswer` can still change its selection just after it appears: a radio clicked at once was replaced by
    the dialog's own default. Check the selection again right before OK.
  - Many events are built into the game, not in `Data\Events.dat` (for example "We should issue an ultimatum,
    demanding that they back down!" or "We should increase our cooperation with France."). Answering them with the
    first radio led a passive player's nation into a war.

## Intelligence, trade protection and foreign stations
Static reading of 1.01.44 (2026-10-20), confirmed live on the 0.4.0 release and on 0.4.1
([MODLOG](../MODLOG.md#fix-2026-10-20-joined-players-intelligence-mirrored-the-hosts-no-trade-protection-warnings-v041)).
Which routines act for nation 0 (the save's player) only: `CheckTurnEvents` and its `CheckIntelligence`,
`PlayerChecksBeforeTurn`, the player's part of `CheckShipsOnForeignStations` and `CheckPatrols`,
`MovePlayerShipsOnForeignStations`, `GetIntelCostForPlayerNation`, `ShowTensionLevels`' slots 1-8.
- **Where the game keeps a player's intelligence: on the target nations.** `TBuilderNation.IntelligenceSpending`
  (+0x171, 0 None .. 3 High) and `IntelEffort` (+0x1ac, the effort reached, 0..3) of nation j are nation 0's spending on
  and effort against j (saved per `[Nation<j>]`, like `Tension`). Nation 0's own pair is unused.
  - The politics panel (`pnPolitics`) shows nations 1..8 (`ShowTensionLevels` 0xc7f4d8, indices hard-coded); the label
    under each flag (`stIntel1..8`) is `IntelToString(i)` (0xd3c408: None/Low/Med/High, " *" while IntelEffort <
    IntelligenceSpending); the +/- speed buttons (Tag 1..8 raise, 9..16 lower) call `SpeedButton1Click` ->
    `ChangeIntel` (0xd3c538: nation Tag (or Tag-8) IntelligenceSpending +1 up to 3 / -1 down to 0) ->
    `UpdateBudgetNumbers`, `ShowTensionLevels`.
  - The budget's Intelligence line (`stIntelligence` +0x690, `Label23`) is `GetIntelCostForPlayerNation` (0xc989b8):
    the sum over nations 1..8 of IntelligenceSpending x 10 x FleetSize.
  - `TBuilderNation.GetMonthlyBalance` (0xc5d2ec): for PlayerNation it subtracts GetIntelCostForPlayerNation and
    `GetInvasionTargetCost`; for every other nation it ADDS `ComputeAINationIntelCost(nation)` (0xcae594: FleetSize x
    30 at Tension 0 with nation 0, 40 at 1-4, 50, 60, 70, 80 at 5-8, 90 above). `ComputeExpenses` (0xca049c, in
    `DoStrategicTurn`) adds GetMonthlyBalance to Funds: PlayerNation, then nations 1..8, which also get 25 x FleetSize a
    month (and another 25 x FleetSize while poorer than nation 0 and Tension > 6), and their BaseResources adjusted.
  - The reports: `AddIntelReport(aNation, text)` (0xcb176c) adds "Intelligence report <nation>: text" to
    `TurnMessages` (+0x3208) and "<Mon yyyy> <nation> - text" to `IntelReports` (+0x320c), the one list the Intel
    reports screen shows and `[IntelReports]` saves (`ReportNo`, `Intel0..`). Its callers: `SpySuccess` (the player's
    spying) and passive reports from the AI routines, mostly gated by the target's IntelEffort (DoResearch: "There are
    reports that <X> scientists have invented ..." at Random(100) < 18 x IntelEffort; AINationAdjustBudget,
    HaltMostExpensiveShip, ReduceTimeLimits, CheckForWar, CheckForConstructionDelays, ShowGunImprovementMessage,
    AIFortifyArea, CheckTechSharing) or reactions to nation 0 itself (AIMonthlyBuild: "Alarmed by our dreadnought
    building program ...").
- **The monthly spying check (nation 0 only).** `CheckTurnEvents` (0xce30f0) fills one event slot a month: at war a
  peace event, else officer events, `BuyTechnology`, then with Random(3) = 1 `GetRandomEvent` (only half of those
  while the war counter is -10 or more: at war or within 10 months of a war), else `CheckIntelligence` (0xc86244; if
  it reports nothing, or 1 in 8, `RandomTensionChanges`).
  - `CheckIntelligence`: nations 1..8 in a random direction; `CheckAircraftIntel(i)` when at war with i;
    `CheckIntelligenceForNation(i)` (0xcc1288); after a hit 60 % stop.
  - `CheckIntelligenceForNation(i)`: in peace only when Random(10) <= |war counter|; the head of naval intelligence's
    suggestion when at war with i and spending None (Random(100) < 10, a message box); effort +1 with Random(5) = 1
    while below the spending; not at war: our spy caught when effort > Random(200) and effort x 30 > Random(100)
    (`TdlgEventAnswer.ExecuteOurSpyCaught`: "Deny any involvement" Prestige -, Tension +; "Make the agent a national
    hero!" Prestige +, Tension +; the effort drops); else `SpySuccess(0, i)` when effort > Random(20) or effort + 4 >
    Random(100) (at war also 1 in 50); a communist PlayerNation 1 in 36 more; enemy spy caught at (Tension/3 + 1) >
    Random(100) and 50 % (`ExecuteEnemySpyCaught`: "Give it maximum publicity ..." BudgetModifier +1, Tension +1); the
    effort drops by 1 while above the spending.
  - `SpySuccess(Spying, Target)` (0xc9c744): for Spying = 0 reports on training, docks, ships under construction and
    rebuilds (with a ship data sheet `TdlgShipStatus` or "Our top spy has acquired the plans ..." picture message),
    hidden budgets, research percentage, balance, aircraft data (`ShowEnemyAircraftData`), and gun theft (the
    target's gun quality into GetBNation(0).Guns: "We are now able to produce such guns!"); for an AI spy only the gun
    theft, silently. `CheckAISpying(i)` (0xcba67c, DoStrategicTurn for i = 1..8): the AI's spying on nation 0
    (chance (GetBNation(0).Tension + 5)/170) and on the others (7/150).
  - Ship-level beliefs are the player's too: `TCampaignShip.EnemyClassName` (+0x19), `EnemySpeed` (+0x59),
    `EnemyBelt` (+0x5d), `PlayerIntel` (+0xc3), saved with each nation's ships.
- **Trade protection and foreign stations.**
  - `PlayerChecksBeforeTurn` (0xce28c4, `DoTurn2` before the month; `DoTurn` 0xce09f4 is unused): AMC limits,
    commanders and admirals (auto-assign questions), treaty tonnage, carriers' aircraft, funds into the red, air base
    capacity, mothballed ships, then **trade protection** at war (war counter > 0): `CountShipsOnPatrol(nil)` (Status 3)
    < `GetPatrolRequirement(PlayerNation)` -> "You have N ships on trade protection duties, but M are required. This
    will give VP to the enemy and might hurt your prestige. Continue anyway?" (No = back to the screen), then
    `CheckForceOnForeignStations(True)`, then unrestricted submarine warfare with few submarines.
  - `GetPatrolRequirement(aNation)` (0xcc78a8): for PlayerNation the operational submarines of GetBNation(1..8) at
    Tension >= 40 (a third less for those with none in its BuildArea), scaled by FleetSize, BaseResources,
    GlobalNavalPower and the economic rating; for any other nation it counts the PLAYER's submarines. The budget panel
    shows "N of M" (`stPatrol`).
  - `CheckForceOnForeignStations(Warn)` (0xcf5f54): returns True at war when PlayerNation is blockaded; per map area
    HasRequiredForceInArea(area, False) (`GetTonnageInArea - GetRequiredTonnageInArea`) shortfalls are summed and
    compared with the colonial tonnage of the operational Status 6 ships. Warn: the question "There are insufficient
    ships on foreign stations! Continue anyway?" (and, 6 % each, advice on destroyers and scouting cruisers). Not Warn
    (the month, peace only, never in January, 1 in 5 within four months of a war): Random(2 x required) < shortfall ->
    "The YY has been made aware ..." the first time (`LeaderWarnedAboutForeignStations` +0xae2), "The YY is displeased
    ... Your prestige suffers." (Prestige -1) the next; else the same chance -> a rebellion in a random possession of
    the player whose area lacks force (`TPossession.Rebellion` +0x6d := Random(10) + 2, not where `NaturalOwnerName` is
    the owner); else Random(required) < shortfall and 50 % -> trading companies or the navy league complain. In both
    modes it then rolls world events: independence and rebellions of random possessions of ANY nation (the player's
    own independence question `TdlgEventAnswer.ExecuteGeneral`), Cuba and the Philippines against Spain.
  - `DoStrategicTurn` (0xce1938): `CheckShipsOnForeignStations` (0xcb6718, peace only:
    CheckForceOnForeignStations(False) for nation 0, `AISendShipToColonies` for 1..8), `CheckPatrols` (0xcc7dc4: at
    war, nation 0 short of patrols by more than Random(40) -> "The YY is annoyed that you cannot safeguard our coasts
    with adequate patrols ..." (a message only); the AI patrols and raiders of nation 0's enemies), submarine warfare
    only between nation 0 and its enemies
    (`CheckSubmarineActivity`, the `TdlgSubmarineWarfareSummary`, `CheckResultOfSinkings`), `CheckRebellionAndInvasions`
    (unnamed, 0xcf8824: every possession's rebellion counts down).
  - `MovePlayerShipsOnForeignStations` (0xcfc3b8, DoTurn2) moves PlayerNation's Status 6 ships to the areas that lack
    force; the AI's `AISendShipsToForeignStations`/`AISendShipToColonies` do it for the others.
  - `GetTonnageInArea(area)` (0xc61210) counts the operational ships in the area with Status 0, 3 or 6 (active, trade
    protection, foreign stations); ships under way count where they are, so a shortfall can close by itself when they
    arrive (seen with the USA in the 1900 test campaign).
- **AI routines the mod doesn't skip that still change a joined nation's money** (observed in the 2026-10-20 proofs):
  `ComputeExpenses` (the monthly AI money and the BaseResources floor, above), `CheckBBNumbers` (0xccb6d8, from
  `CheckTurnEvents` for nations 1-8, at peace before 1940, 1 in 10 a month when a rival has two more battleships:
  "<nation> has increased the naval budget to remain a global naval power.", Funds + FleetSize x 6000 and
  BudgetModifier + 1; the player's branch costs prestige and gives FleetSize x 1000), `ReduceTimeLimits`
  (`HaltMostExpensiveShip` while the monthly balance is negative and Funds are below twice the deficit), and the
  PLAYER's spies: `SpySuccess`'s "<nation> is secretly spending an additional 10% on the navy above their official
  figures" adds a fifth of the target's budget to its Funds.

## Tactical battles
The battle facts, gathered since the battle investigation of 2026-10-14; the later findings carry their dates.

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
- **In the mod (since 0.3.0; the current design is in [ARCHITECTURE.md](ARCHITECTURE.md)):**
  - Joined players fight their battles against AI nations on their own PC: the host defers `DoBattleBetweenAINations`,
    the player fights in a battle slot (client slot + 10) made with the Copy-game transform, and the host merges the
    result field by field (`saveio::ApplyBattleResult`). Since 0.4.1 a battle the AI declines (`AIDeclinesBattle`)
    comes back with the outcome `avoided` and is merged with its VP only.
  - Battles between players are fought live in lockstep (`src/mod/lockstep.cpp`): a seeded `Random32Proc`, simulation
    and display contexts, a minute gate, an order guard with state sync, a per-minute hash with resync from a `.sac`
    snapshot, and AI takeover on silence.
- **Lessons from building it** (apply to any future battle work):
  - **Display code writes simulation state.** `Hidden`, the identification and the sighting fields are written by
    GlobeRender and the ship panel. Each ship keeps a simulation and a display copy of these. Run `ls leakcheck 1` in
    every live test: it flags any simulation change made between steps.
  - **The load's own drawing identifies ships** (found live 2026-10-17 in stage B of the live-battle limits branch,
    0.4.0; 0.3.0-0.3.2 do the same): the battle save keeps no identification (`TFightingShip.Create` sets
    `IdentifiedAsType` (+0xd70) := 0x16 at 0xe1c6bf, `IdentifiedAsClass` (+0xd6c) stays empty, `LoadMe` reads
    neither), but the battle screen's first drawing during the load names enemy ships through
    `GetTypeAndNameForEnemy` (0xe343d4) -> `IdentifyType` (0xe40268) and `IdentifyClass` (0xe3ffc4), which draw
    `Random` with this PC's display draws and write both fields (27 of 47 ships in the 1900 test battle, 16 in the
    1935 one, on each PC). Up to 0.3.2 the first display swap took those values as the simulation's set, so both PCs'
    simulation identification differed from minute 0 (the hash line's `ident=` differed in every minute of every
    battle). The sighting reports routine (unnamed 0xdc960c, called from EOTP at 0xdaf8dc, inside the simulation)
    calls `IdentifyClass` (0xdca190) and `IdentifyType` (0xdca56e) with shared draws whose number and results depend
    on the current identification (`IdentifyClass`: `Random(100) < 25`, then a comparison with the current class
    string decides the next branch), and raises "Unknown ship sighted!" (0xdca9cf, once per report kind and battle
    screen, when PlayerSide is not 2): so a ship identified differently on the two PCs made the battles drift apart
    now and then, next to that box (the rare DESYNC: with `rng=` and the sim draw counts differing first on the
    live-battle limits branch; with pos and ord first and the RNG equal in 0.3.2's `lockstep_ui` e), where an AI
    decision that reads the identification is a guess). Since 0.3.3 (engine level 11 on the branch), right after
    every live load the simulation's set is the constructor's (0x16, no class) on both PCs; the display keeps what it
    drew. The identification is not part of the hash's `all`, so a 0.3.2 PC and a 0.3.3 PC still hash equal states
    equally, but their simulation identification differs as between two 0.3.2 PCs.
  - **The division report writes simulation state too** (found live 2026-10-17 in stage A of the live-battle limits
    branch, present since 0.3.0): `TdlgDivisionReport.FillShipList` (0xae1874; called only by the report's `Execute`
    and the torpedo dialog's `bnCloseClick`/`bnFireClick`; no random draws) sets each listed ship's `CurrentSpeed`
    (+0xa48) to `GetMaxWeatherSpeed` when that is lower (0xae1972..0xae1990), on the PC where the report opens only
    (leak check: `<ship>.CurrentSpeed 1c000000->1b000000`, then a DESYNC). Since 0.3.3 the mod hooks it as a display
    context and puts every ship's `CurrentSpeed` back after it (`ReportSpeeds*`); the simulation lowers it in the
    next minute on both PCs. Opening a report also writes the division's `DivisionSpeed` (an order field: the order
    guard sends it as an order).
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
  - **A battle load writes campaign state.** The resume path's `.sac` reader moves the campaign's Day and Month to the
    battle's own date. Put back what the campaign must keep after every load (engine level 10, `KeepCampaignDate`).
- **"The player's side" (2026-10-16, static reading of 1.01.44, for engine level 2).** `PlayerSide` (cell 0xf27008) is
  the battle's human side: 0 or 1, or **2 = no human side**, a value the game itself knows (the battle setup and
  `TdlgSelectScenDLC` set it; `sub_dfd880` tests `PlayerSide <> 2` at 0xdfd936, `FireMissileMount` tests `= 2` at
  0xe047f6). Routines that treat PlayerSide's units differently:
  - Air: `CheckAIAirstrike(Self, Side)` (unnamed, 0xdfd50c, `'Error in CheckAIAirstrike procedure!'`; StartMinute
    calls it for side 0 then side 1 at 0xdab72a/0xdab736) and its squadron picker (0xdfdb33) skip PlayerSide's
    formations unless they belong to an airfield (`ShipType` 12); `sub_dfd880` likewise unless PlayerSide is 2. So
    with PlayerSide 0 the computer never launches side 0's carrier strikes. `SetAIAirSearchPlan(False)` (EOTP every
    120 minutes after minute 600, 0xdafa1f) re-plans every force's search sector and CAP except PlayerSide's
    (0xdfbfe1).
  - Strike targets: `AILaunchStrike` (0xdfdfd4) sets `TAirFormation.ConfirmTargetLoc` (+0xf1) on every strike it
    launches (0xdfe251). At the end of the minute `sub_eab89c` (0xdafb1e) asks PlayerSide's player whether to adjust
    the target of a spotting carrier/AV naval strike, and `TAirFormation.CheckStatus2` (0xe9480c) calls
    `ConfirmTargetLocForAirFormation` (0xe005b0), which retargets side 1's (hard-coded) and land-based strikes with
    the computer's target finder.
  - Missiles: `CheckMissileFiringForAllShips` (unnamed, 0xda91fc, EOTP after 1940 at 0xdaf91f) lets PlayerSide's
    divisions fire only when the missile panel's `cbFireAtWill` is checked (0xda930d); `FireSSMAtTarget` (0xda7ccc)
    applies `rgMissileDoctrine` to PlayerSide's ships and the computer's bonuses to the others (0xda7d13..0xda7daf);
    `FireMissileMount` with PlayerSide 2 logs `<ship> is launching <missile> at enemy ships!` into `MessageLog`
    instead of the once-per-battle `Enemy targeting radar is locking on to <ship>` message, which also sets
    `FirstSighting[side]` (+0x800) and `PauseGame` (+0x7e8).
  - `UpdateMissilePanel` (unnamed, 0xe032c8, EOTP after 1942 at 0xdaf8fa; `Self` only, no stack arguments, no
    `Random`) counts PlayerSide's SSM ships and the sighted **side 1** ships in their range (side 1 hard-coded at
    0xe03376), writes `IsPotentialMissileTarget`/`OwnMissilesCanFire` (read by nothing else; `sub_dc960c` resets
    them), `stShipsBearingMissiles`/`stEnemyMissileTargets`, and shows `pnMissiles` once.
- **Fastest available** (`sbFastestAvailable`, GroupIndex 579, AllowAllUp): `sbFastestAvailableClick` (0xe0561c):
  down → `GameSpeed` 5, `setSpeed`, `ImageIndex` 1; up → `ImageIndex` 0, `GameSpeed` 2, `setSpeed`. The release
  routine 0xde42dc (`UpDown1Click`'s and the speed box's) is `SetDown(False)` (0xabd0f4) + `ImageIndex` 0 (0xabc2b0,
  the published `TCustomSpeedButton.ImageIndex` setter). VCL flips `FDown` before `OnClick`. The speed governor in
  EOTP, StartMinute and StartRun reads `FDown`.
- **Force orders and the battle resume (2026-10-17, static reading of 1.01.44, for engine level 3; `verify_p1.py`
  checks the patterns).**
  - *Change force* `TfrmMain.Changeforce2Click` (0xd8b9b0; Self = frmMain, Sender = a menu item): nothing without
    `SelectedDivision` (+0x7d4); the force is `Forces[Sender.Tag]` (`TComponent.FTag` +0xc; the items are built by
    0xde35a4 for the forces of the division's side other than its own whose `GetForceFlagship` is not a TR/LT); the
    question `MessageDlg('Do you want to reassign this division to  ' + Name + '?', mtConfirmation, [mbYes, mbNo], 0)`
    (MessageDlg 0xa06c50, buttons word 3); on mrYes `old := div.myForce (+0x57); old.Divisions (+0x10).Delete(
    IndexOf(div))` (TList.IndexOf 0x8e6e0c, Delete 0x8e6be4), `force.Divisions.Add(div)` (0x8e6b8c), `div.myForce :=
    force`, `dec div.Admin (+0xe5)`, `FillShipTree(tvShips, PlayerSide, False, nil)`. No `Random` in its call tree.
    `TDivision.Admin` is written in a battle only here and by `TDivision.Create` (0); the `.sac` doesn't keep it.
  - *Change lead / battle turn-away*: `TdlgForceOrders.sbChangeLeadClick` (0xd7e800: its "once during a game"
    question, `SelectingNewLead` +0x7b2 := 1, `FillSelectLead`) and `sbTurnAwayClick` (0xd7e8c8:
    `SelectingLeadForBattleTurnAway` +0x7b3 := 1, `FillSelectLeadForBattleTurnAway`) only set up `dlgSelectLead`.
    `TdlgSelectLead.bnOKClick` (0xd7ed3c) takes `lbLeadShips` (+0x440) `.Items` (`FItems` +0x2d8) `.Objects[ItemIndex]`
    (`GetItemIndex` VMT+0x16c, `GetObject` VMT+0x18) and calls `SetForceLeadDiv(div)` (0xde42fc) or
    `DoBattleTurnAway(div)` (0xde4550), clears the flag and closes itself. Both routines take the force from
    `SelectedForce` (+0x7d8), start from `GetDivisionOfShip(SelectedForce.GetForceFlagship)` (0xd79a48, a scan of the
    divisions list; with no lead division they fault on nil), move the new lead to the front of `Force.Divisions`
    (`TList.Move`: `IndexOf` -1 raises), write guarded fields (LeadDivision, Role, AIControlled, ForceLeadDivision;
    HasChangedLead := 1 / TimeToTurnAway := 15, DivisionSpeed, DivisionCourse and TurnTogether via TurnAround
    0xde44e0), close `dlgForceOrders` and `dlgSelectLead`, and show a message (`ShowMessagePos`). DoBattleTurnAway
    also reverses the ship order (`ReverseShipOrder`) of the new lead and of every division in its `LeadDivision`
    chain (an unbounded walk: a loop would hang) and redraws the globe; TurnAround dereferences each division's
    flagship (a division without ships faults). No `Random` in either call tree (`randtree.py` depth 6). Neither dialog
    has `OnClose`/`OnShow` handlers (Close = Hide).
  - `TForce.GetForceFlagship` (0xe59acc) is a cache writer: `GetForceFlagDivision` (0xe59ddc) clears
    `ForceLeadDivision` of withdrawing divisions (and of AI-controlled ones in side 0's forces), may set it on a
    fallback division, and the result goes into `ForceFlagship` (+0x18). The change-force menu, the Force orders
    dialog and `FillSelectLead` call it from display code.
  - *Seen live (2026-10-17, stage A proof).* `SetForceLeadDiv` sets the old lead's `LeadDivision` (+0x3c) to the new lead
    (0xde4360) and leaves the new lead's pointing at the old one, and both keep `ForceLeadDivision` = 1: a two-division
    cycle. `DoBattleTurnAway` walks `LeadDivision` from the new lead until nil (0xde45fc..0xde460f), so in the game a
    turn-away through that pair after a lead change never ends; the live battles refuse it ("the divisions' lead chain
    loops"). With Realism below Captain's mode (prefs +0x56 < 2) both routines also give the old lead to the computer
    (0xde4345, 0xde45f0). A battle load (a resync) rewrites these links: after it the old lead had no `LeadDivision`,
    and a division moved into a computer-controlled force (the coastal batteries' force) was `AIControlled` with
    `ForceLeadDivision` 0. The game lets *Detach* take every ship of a division; the emptied division leaves the lists at
    the next minute. In the 1900 test campaign the Force orders' *Battle turn away* is disabled for both nations (no
    turn-away tech) and in 1935 enabled; *Change force* was not in any division's popup in either (tech flag +0xdc).
    *Select lead* lists only eligible divisions (a lead battle division plus a destroyer division gives "No eligible
    divisions in force!").
  - *Resuming a battle save*: `LoadBuildCampaign` calls `LoadScenarioInProgress` (0xc9ae81) when the save has a battle
    in progress; that sets `CampaignScenario` := 0, `BCScenario` := 1 and **`PlayerSide` := 0** (0xcd02e7) before
    `TfrmMain.Execute`. `SetupGlobeRTWLayers` loads the `.sac` (0xdde0a0: per force `AIControl`, per ship
    `ForceIsHumanControlled := not force.AIControlled` at 0xddfd70) and then, for a BC scenario with PlayerSide 0, sets
    the side table's AI flags (`[0xf278e4]` records of 0x47 bytes, +8) to side 0 = 0, side 1 = 1 (0xdc3686..0xdc3694).
    `SetDivisionAIControl(Resumed)` (unnamed 0xda9cd8, 'Error in SetDivisionAIControl procedure!'; called at 0xdc3b56
    with DL = 1 for `.sac`/`.sav`) then sets every force of an AI side to `AIControlled` (0xda9d7f), clears
    `ForceIsHumanControlled` of the ships of AI-controlled forces (0xda9e87) and sets every division of an AI side to
    `AIControlled` (0xda9ec8). So any resumed battle save, also one written in a live battle with side 1 human, has
    side 1 under the computer: a live battle's resync must put side 1's flags back after its load. The same routine
    gives every PlayerSide division whose force is `AIControlled` (+0x79) to the computer, resumed or not
    (0xda9f57..0xda9f79: `PlayerAIControlled` +0x31 and `AIControlled` +0x30 := 1), although the `.sac` keeps each
    division's `AIControl=`: a division of side 0 its player took back from a computer force (the division report's
    "AI controlled") is the computer's again after a load (engine level 13 puts side 0's flags back too, `ai0=`).
  - *The minute timer* (`tmrMoveIntervalTimer` 0xdaf614, one tick per interval): `MoveTorpedoes` (only while
    `GameSpeed` +0x6b8 <= 2), `MoveAllShips`, the follow-the-division view (`DivScreenLock` +0x6ee, `SelectedDivision`
    +0x7d4: `LockAngle`/`LockDist`, display only), the ship close-up copy (0xdeb6b4, display only), then on the last
    interval `EndOfTurnProcedures` and 0xde1df0. That one runs outside the simulation's routines (in a live battle
    with this PC's display PlayerSide) and sets `ManeuverOK` (+0x745) := `NoOrdersBeforeSighting` off (the scenario's
    `[Environment] NoOrdersBS=`, 1 in campaign battles; global [0xf27840]) or `FirstSighting[PlayerSide]` or the
    battle's minute ([0xf271b8]) > 30, and enables the order buttons of `SelectedDivision` from it; EOTP sets it every
    10 minutes to (`NoOrdersBeforeSighting` off or `FirstSighting[PlayerSide]`) and minute >= 100 (0xdafb31). Its one
    simulation reader, the division AI routine 0xd968e4, skips a division only when it is not `AIControlled` (and
    `ManeuverOK` is 0, the minute below 60, the role not 2 or 4), and both its callers (the StartMinute loop 0xd955b8
    at 0xd9592d, `DetachShip` at 0xdac0ce) call it for `AIControlled` divisions only: the PC-dependent value changes
    nothing in the simulation.
  - *`TFightingShip.LightState`* (+0xb02, an Integer: 1, 2 or 3 by the light at the ship's place and the battle time,
    `GetLightState` 0xe594e0; it changes at dusk and dawn). The sighting-range routine 0xe596ac(ship, randomize) writes
    it first, then reads it for the range. The simulation calls that routine from the division AI, the fire routines,
    the sighting reports and `DetachShip`; the display calls it too: between minutes (`Intervals` +0x6c8 = 0) the map's
    sighting circles of the PC's own divisions (0xd8d084, under GlobeRender; it skips computer forces unless PlayerSide
    is 2 or `ShowAll`) and the division report (0xae12f7). StartMinute's division routine 0xdab764 reads the flagship's
    `LightState` before the minute's own calls refresh it: with `LightState` 3, `Role` 0 and `TacticalStance` 1 it draws
    Random(100) at 0xdabbbe (return 0xdabbc3). So at a light change the PC whose map had drawn that division took another
    path (the DESYNC report of 2026-10-17: the call sites parted at draw 171 of minute 291, 0xdabbc3 on A only, at 16:48
    of an October battle); the dawn DESYNCs of `lockstep_ui` e) (test battles start at 05:00) fit it. Engine level 14
    keeps the field in the guard's dual set and in the state hash.
  - *The month around the host's battle and a resumed one (2026-10-17, static, for phase 5b, engine level 9).* Before
    the resume `LoadBuildCampaign` asks "Select autostart point." when `BattleIndex` (+0x4a3f) is not -1 (0xc9ae54;
    answer 1 *Regenerate battle from starting parameters* sets `RestartFromBattleGeneration` +0x4a3a and skips the
    resume, 2 *Start from latest autosave point* resumes); `BattleIndex` is written by `DoPossessionBattle` (0xcedf93),
    `DoHomeAreaBattle` (0xceef0b), `CheckSurpriseAttack` (0xcfdb77) and the load (0xc9ad51), not by `DoBattle`.
    `LoadScenarioInProgress` shows the battle screen modelessly (`TfrmMain.Execute` 0xcd0338) and hides `frmBuildCamp`
    (0xcd036f). `DoTurn2` (0xce1644) ends with `CheckForBattles2` (0xce180e): a battle returns with its screen open,
    none runs `DoStrategicTurn` (0xce181a). `TfrmMain.FormClose` (0xdcd67c) of a BC scenario (+0x7f5, the resumed one
    too) shows `frmBuildCamp`, runs the aftermath (`CheckPrisoners`, `DoBattlePrestigeLoss`, `BattleOpponentIdx` := -1)
    and then `DoStrategicTurn` (0xdcda2a); the byte at `[0xf273cc]` (set by a load whose `BattleOpponentIdx` is 0,
    0xc9ae1f) or `QuickBattle` (+0x4ac8) skips all of it. `DoStrategicTurn` (0xce1938) never calls `CheckForBattles2`
    (one host battle per month); it runs `ReduceTimeLimits`, `ComputeExpenses`, `CheckTurnEvents`, `DoResearch`,
    `CheckAIvsAIBattles` (0xce20b1; the only path to `DoBattleBetweenAINations`, via `CheckAivsAIBattleInArea`
    0xd32ce9), the raiders and `DoEndOfTurn` (0xce21f4). `FormCloseQuery` (0xdc6694) refuses to leave an unfinished
    campaign battle; the battle's own Save (`sbSaveClick` 0xde0274) writes `RTWGame<slot>.sac` and the campaign into the
    open slot and asks "Exit game?" (Yes closes `frmBuildCamp`, i.e. the game).
  - *The campaign date and a resumed battle (2026-10-17, static, for 0.3.2 and engine level 10; `verify_p6.py`
    checks the pattern).* Year, Month and Day are Integer unit variables `[0xf531e0]`, `[0xf531e4]`, `[0xf531dc]`
    (cells `[0xf27328]`, `[0xf27b78]`, `[0xf27af4]`): every writer stores a dword; `EncodeDate`'s Word parameters read
    their low words. The month advance runs on a separate TDateTime, *CampaignDate* `[0xf52fe8]`: `DoStrategicTurn`
    does `IncMonth(CampaignDate, 1)` (0xce1c2e; the day kept, clamped) and copies its date into the three
    (0xce1c69..0xce1c81). `LoadBuildCampaign` reads `[General]` Year/Month/Day into the three (0xc99aa1..0xc99af6) and
    builds CampaignDate from them (0xc9a3bc: `mov ecx, [Day]; movzx ecx, word [ecx]; mov edx, [Month]; ...; call
    EncodeDate; fstp qword ptr [CampaignDate]`, +0xaf8, unique in the image) before it resumes a battle (0xc9ae81);
    `SaveBuildCampaign` writes the three, not CampaignDate. The `.sac` reader (`sub_dd8f78`, from `TfrmMain.Execute` →
    0xdde0a0) stores `[Environment] Day` and `Month` into the campaign's Day and Month (0xdd92ca, 0xdd92fa) and its
    Year only into the battle clock `[0xfacab8]`; the battle save (`sub_dd2a10`) writes the clock's date there. So
    every battle load moves the campaign's day and month to the battle's own; nothing in a battle writes CampaignDate.
    The battle's date (`sub_bdf038`): day `Random(28)+1` at 10:30 in the campaign's month, plus 1 to 66 hours, then
    later until the light fits; it can fall into the next month (in December: January of the next year), and before
    1900 the clock is encoded with Year + 100 (0xbdf0a9). The battle reads Month (the weather: EOTP → `sub_dae42c` →
    `sub_dce844`/`sub_dcf0d2`; `TfrmMain.IsWinter` 0xdfd158 → `SetSightMod`; `MonthRecords[Year][Month]` at its end),
    Day (the AI's bearing parity, `sub_d968e4` 0xd97ad6: every minute from StartMinute and once at the end of a
    scenario load, 0xdc3ba7) and Year (era gates, e.g. EOTP's automatic missile fire after 1940, 0xdaf90f).
    *Seen live (2026-10-17, engine level 10):* a December 1900 battle whose snapshot's clock was in January 1901: the
    `.sac` said 1901/1/4 and the load left Year 1900, Month 1, Day 4 in the variables ("the battle save said
    1900-01-04"), which the mod put back to 1900-12-01; the month then ran December -> January on both PCs. The game's
    own minute box "Log lines out of sync! <n>/<m>" (in the EOTP message routine, string 0xd91480) followed a reload
    until engine level 12: the battle save splits a battle log entry with a line break (*The game's texts in a
    battle*, below).
- **Auto-run and the computer's command (2026-10-17, static reading of 1.01.44, for engine level 4; `verify_p1b.py`
  checks the patterns).**
  - *The handler* `TfrmMain.sbAutoRunBattleClick` (0xe02824). With `AutoRunBattle` (+0x7e9) = 0: `dlgEventAnswer`
    (cell 0xf27318) `.ClearHints` (0xc11d64) and `.ExecuteGeneral(prompt, 'Yes', 'Yes, and order all forces to move to
    closest friendly base', 'No', -1)` (0xc11890: three radio choices, an empty answer hides its choice; the result is
    1..3, -1 without a choice; the handler treats only >= 3 as No). Then, for every force of PlayerSide that has a
    `GetForceFlagDivision` (0xe59ddc): with answer 2, `ForceWithdraw` (+0x75) := 6 and `ForceMission` (+0x1c) := 2
    (`tfmWithdraw`) if the force is not `AIControlled` (+0x79), and the same if its name contains 'Support Force'
    (`Pos` 0x83c958 at 0xe028db); then the flag division's `AIControlled` (+0x30) := 1. Then **every division of the
    battle (both sides)** `AIControlled` := 1, `sbFastestAvailable.SetDown(True)` + `cbFastestAvailableClick`,
    `AutoRunBattle` := 1, `sbRun` disabled, the nested 0xe027c0(False) (`Enabled` of `sbPlay` +0x4cc, `sbRun5` +0x4d0,
    `sbRun` +0x4d4, `sbPause` +0x4e4), `sbAutoRunBattle` (+0x610) `.SetDown(True)` (0xabd0f4) and `StartRun(MaxInt)`
    (0xdae9e0). With `AutoRunBattle` = 1: `SetDown(False)`, `AutoRunBattle` := 0, 0xe027c0(True), `PauseGame` (+0x7e8)
    := 1, and the flag division of every force (either side) that is not `AIControlled` gets `AIControlled` := 0; the
    other divisions stay with the computer. PlayerSide and `ForceIsHumanControlled` are not touched.
    `TfrmMain.FormShow` puts `AutoRunBattle` and the button back to off (0xde24dc), so every battle load starts without
    it. `Label5MouseUp` (0xe02610, a Ctrl-click variant without the base option) is bound to no control.
  - `GetForceFlagDivision` writes `ForceLeadDivision` (+0x63): it clears it on withdrawing divisions and, in side 0's
    forces (hard-coded), on AI-controlled ones, then sets it on the lead it picks. No `Random` in it, `ExecuteGeneral`
    or `ClearHints` (`randtree.py` depth 6).
  - `ForceWithdraw` is the force's withdrawal level, shown as "Force fatigue" in the Force orders dialog: its
    "Disengage" (`sbDisengageClick` 0xd7e588) raises it to 6, "Rally" sets 1, and `CheckForceWithdrawal` (0xdaeb08,
    from StartMinute 0xdab706) raises it by chance from the losses of every force. `ForceMission` is `TForceMission`
    (tfmNormal, tfmCautious, tfmWithdraw, tfmAttack, tfmEscort); the tactical AI reads it (`'... selects DISENGAGE due
    to being outnumbered - WITHDRAW'` with tfmWithdraw) and `CheckPortArrival` (0xdec6a0) sets it. No dialog writes
    `ForceMission`. The battle save keeps both and the force's `AIControlled` (0xdd1625, 0xdd1716, 0xdd1699).
  - *What `AutoRunBattle` gates* (every `[reg+0x7e9]` access). Simulation: EOTP's air-attack picture messages draw
    `Random(10)` per pending message only while it is off (0xdaf979); the message processing 0xd8fd5c (EOTP 0xdafb03:
    message boxes, and the break it returns, `PauseGame` := 1) and 0xd91182; `CheckSupportingDDToDetach` (0xdd0783,
    0xdd0b39: the computer's survivor pickup and detach with `Random(100)` when the force is `AIControlled`, the
    battle's `AutoPickUpSurvivors` or auto-run, else the question "Do you want X to pick up survivors from Y ?");
    `CheckPortArrival` (0xdec908: the computer's decision); the sighting messages of human forces (0xdca997, 0xdcabc4);
    `CheckAllForcesForHumanFlagDivs` (0xdf5aca: in a PlayerSide force without a human division afloat, the flag
    division becomes the force's lead, `ForceLeadDivision` := 1); `TFightingShip.ShipLaunchSearchAircraft` (0xe48cb0:
    PlayerSide's ships ask "Do you want X to launch scout aircraft?") and `LaunchFloatCheck` (0xe490d2);
    `DoDamageControl`'s flooding message (0xe34e73). Clock and UI: the end of EOTP (0xdb04bd) and `tmrRunMinuteTimer`
    (0xdb22db: a break doesn't stop the run), the battle autosave's timer (0xdf0223), `FormKeyUp` (0xdae520, 0xdae5b8:
    with it on, Alt and space call `StartRun(MaxInt)` or set `MinutesLeft` := 0 and stop `tmrRunMinute` directly),
    `GlobeMouseMove`. So it must be the same on both PCs, and with it on one PC's keys would change the game's own run
    state.
  - *The computer's command* is read from several flags. `TDivision.AIControlled`: the division AI (0xd955b8 from
    StartMinute 0xdab6f8 → `AIDetermineTacticalStance2` 0xdc108c → one routine per ship type, 0xdb726c / 0xdba690 /
    0xdbd104 / 0xdc0010, then the course routine 0xd968e4; `CheckAllDivAfterMove` 0xdb0bb4 in EOTP;
    `MoveDivisionShips` 0xdb39cc copies it into each ship's `DivisionIsAIControlled` every minute).
    `TForce.AIControlled`: force decisions (`CheckForceWithdrawal`'s flotilla attacks 0xdaf1a1, the survivor, port
    arrival and sighting paths above, `CheckAllForcesForHumanFlagDivs`, `SetAIAirSearchPlan`).
    `TFightingShip.ForceIsHumanControlled`: `TForce.CheckSearchAircraft` (0xe5a334: human ships launch floatplanes only
    through `LaunchFloatCheck`'s question), `DoDamageControl`, `FireGuns`. And `PlayerSide`: three of the four stance
    routines skip their "FORCE WITHDRAW" stances for AI-controlled divisions of PlayerSide (0xdb7a3d, 0xdbaf03,
    0xdc02dd), `CheckAllDivAfterMove` changes the role of carrier divisions not of PlayerSide that follow a non-carrier
    near the enemy (drSupport, before 1943 drCore; 0xdb193a), and the scout question above. In single player the
    game's auto-run sets only the division flags (and `AutoRunBattle`) and keeps PlayerSide, so the player's side
    still meets those PlayerSide tests.
  - `PlayerSide` = 2 is a value the battle setup itself uses: `SetupGlobeRTWLayers` sets it when the side table has not
    exactly one human side (0xdc370c) and when both sides are the computer's (0xdc3a57, also setting `BothComputer`
    +0x7b5). The simulation's PlayerSide-indexed accesses are reads (`FirstSighting[PlayerSide]` at 0xdafb69 and
    0xde1e05 read +0x802 then). The battle save writes `[Sides] PlayerSide=` (0xdd10a4), but no battle load reads it
    (`LoadScenarioInProgress` sets 0).
  - *The end conditions* in EOTP (0xdafd61..0xdb0336; none reads `AutoRunBattle`): the scenario length reached without
    contact; the absolute time limit (length + min(length, 600)); `ObjEndGame`; every 30 minutes (every 120 after a No)
    "Opposing forces are far apart. Do you want to end the scenario?" (0xdaff19); "No contact between fleets!" and
    "... Do you want to continue searching?" (0xdb000f; Yes adds 180 minutes to the search); one side without ships at
    sea; in a campaign battle 200 minutes before its length, "<nation> has no ships left at sea! Do you want to end the
    scenario?" (0xdb025a); all ships of a side back home. A Yes runs `SinkAllSinking` and `ShowResultScreen` and sets
    `EndGame` inside the minute.
- **Manual torpedo salvoes (2026-10-17, static reading of 1.01.44, for engine level 5; `verify_p2.py` checks the
  patterns).**
  - *The UI path.* `TdlgDivisionReport.bnTorps` ("Fire torpedoes") is enabled by the report's `FormShow` (0xae22ca) only
    when the battle preference *Realism* is above its second level (`[0xf278b4]^+0x56` > 1, the record
    `TdlgPrefs.Execute` fills from `rgRealism.ItemIndex` at 0xe09143; one of the battle preferences a live battle's B
    takes from A). `bnTorpsClick` (0xae1594): if the list view's selected ship has `FiredMountThisTurn` (+0xb1a), the
    message "Ship has fired mount this turn! Only one mount can be manually fired each turn." and nothing else; else
    `SelectedShip` (+0x7dc) := that ship or the division's flagship, `SelectingTorpedoTargetFromDivReport` (+0x7b1) :=
    1, `dlgTorpTargets` (`Missiles` +0x468 := 0, caption "Select torpedo target", `FillShipTargetList(ship, lbTargets,
    False, True)`: the ship's `SightedShips` (+0xad9) by range, without the gun check `CanShootAtShip` on this torpedo
    path, texts from `GetTargetText`; the ship box `cbShipsInDivision` with the division's ships), `Show` the dialog and
    `Hide` the report. In the dialog `lbTargetsClick` (0xadd294) lists the firing solutions (0xadd39c: about 15 `Random`
    draws for the bearing and the target's speed; per mount of `TorpedomountList` with `MountType` 0 and tubes:
    "Expended" without torpedoes, else "HIGH <course>" / "LOW <course>" when the solution's run is within the HIGH / LOW
    range (`TorpedoType` +0xda2 / +0xda6) and `MountCanFire`, else "No solution" / "Out of arc"; only HIGH and LOW lines
    carry the mount as their object), `lbMountsClick` enables Fire for a line with a mount. The dialog is modeless; its
    `bnCloseClick` shows the division report again, `Timer1Timer` likewise.
  - *Fire* `TdlgTorpTargets.bnFireClick` (0xadda28), the torpedo branch (`Missiles` = 0; the missile branch fires SSMs
    itself and is reached only through `bnFireMissilesClick`): the mount = `lbMounts.Items.Objects[ItemIndex]`, the
    target from `lbTargets`, the ship = `SelectedShip`; `PlaySoundEffect('TorpedoLaunch')`; speed and range = the HIGH
    (+0xd9a/+0xda2) or LOW (+0xd9e/+0xda6) values by the line's text; the course = `StrToIntDef` of the line's last 3
    characters, then jittered with up to 8 `Random(100)` draws (±1, ±2; night and nation-tech terms); then
    `FireMount(mount, ship, target, Single(course), speed, range, True)` (0xaddf75), `ship.FiredMountThisTurn := 1`
    (0xaddf7d), `dlgDivisionReport.FillShipList` and the missile-mount refresh 0xade448 (it lists only missile mounts,
    so after a salvo the mount list is empty until a target is chosen again).
  - *The fire routine* `TfrmMain.FireMount(aMount, aShip; aTarget, FireAngle: Single, TorpSpeed, Range, Manual)`
    (0xda8cc4; EAX = frmMain, EDX = mount, ECX = ship, the five stack arguments pushed in that order, `ret 0x14`).
    Callers: `bnFireClick` and the AI's fire routine in EOTP (0xda8ac6, 0xda8cb6, `Manual` = 0). Its early returns
    change nothing and draw nothing: `TorpsLeft` = 0 (0xda8cf2); a submerged mount (`TTorpedomount.IsSubmerged`, a pure
    test of `Position` (+8): '0' or '5'..'9') on a ship with `CurrentSpeed` (+0xa48) above 25 that is not an MTB
    (`ShipType` +0x9d3 <> 13): with `Manual` the message "Submerged tubes not useable at speeds above 25 knots!"
    (`ShowMessagePos` 0xa07124 → `MessageDlgPosHelp` 0xa06eb0, mtCustom, [mbOK]), without it silently. Otherwise it
    draws from 10 `Random` sites (with `Manual` the "don't fire" gates of the automatic fire still draw their first
    number for every tube after the first), logs "<ship> is launching torpedoes" into `MessageLog` if `ShouldReport`
    (the ship's `ForceIsHumanControlled` (+0xb1b) or `BothComputer` +0x7b5), and per tube while torpedoes are left:
    `sub_da6488` creates the `TTorpedo` (location, course, `Speed`, names, `Side`, `Calibre` = 18 plus the nation
    record's +0x4a, the ranges, `SettingLow` = the LOW speed; `TList.Add` to the battle's torpedo list), `TorpsLeft` - 1
    (floored at 0, `sub_ab7744`), "Launches torpedo at <GetTypeAndNameForEnemy>" into the ship's `myHits`, and with
    `Reloads` > 0 `TFightingTorpedoMount.SetReloadTime` (0xe5598c, 7 `Random` sites). No UI control, no `PlayerSide`.
  - `FiredMountThisTurn` is a UI gate: written by `bnFireClick` (:= 1) and by `TFightingShip.DoDamageControl` (:= 0 at
    0xe36cf6, after `CheckTorpedoReload`), read only by `bnTorpsClick`; neither the hash nor the battle save has it.
  - *The mounts* are `Tship.TorpedomountList` (+0x9e), torpedo and missile mounts in the design's order
    (`TFightingTorpedoMount`: `Destroyed` +0xf0, `TorpsLeft` +0xf1, `ReloadTime` +0xf5; `TTorpedomount`: `Tubes` +4,
    `Position` +8, `MountType` +0xe, `Reloads` +0xf). In a battle they are written by the simulation (`FireMount`,
    `FireMissileMount`, `ShipFireSAM`, the SSM fire 0xda7ccc, `SetReloadTime`, `CheckReload`, `CheckTorpedoReload`,
    damage 0xe262c4 / 0xe49f58), by the battle load (0xdda2b4) and by two dialogs a live battle blocks (the torpedo
    dialog's missile branch, the developer dialog `TdlgSink`); display code reads them or copies the ship into new
    objects to draw it (`CopyFightingShip` 0xd6c430, from the globe and the ship status dialog).
  - *Torpedoes* (`TTorpedo`, the list `MoveTorpedoes` walks): the battle-save fields `Speed`, `Calibre`, `RunDist`,
    `TorpedoReliability`, `CheckedFail`, `Close`, `SettingLow` are written by `TTorpedo.Create`, the fire routines, the
    simulation (`MoveMe`, the hit check 0xda60e8: `CheckedFail` := 1, `Speed` - 2) and the battle load (0xdf5094). The
    only display routine that walks the torpedo list draws them (0xdcdd74, reads only); the battle-save writer
    (0xdf4aa0) only reads them.
- **Routine air operations and land-based CAP (2026-10-17, static reading of 1.01.44, for engine level 6;
  `verify_p3a.py` checks the patterns).**
  - *The routine air ops dialog.* `TfrmMain.sbRoutineAirOpsClick` (0xdfb5b0, "Manage CAP and air recon") opens it for
    the force of the first division (either side) whose map flag (`TDivision.myFlag` +0x53) is selected, else for the
    ship tree's selected force or the selected division's force (`Searchpattern1Click` 0xdfb358, the tree's popup: the
    tree only), else "Select a force or division!". `TdlgSearch.Execute(aForce, Editor)` (0xb4bfac): `CurrentForce`
    (+0x488) := the force; `ForceFlagship` (+0x18) := `GetForceFlagship` when it is nil; the globe centred on the
    flagship; `cbTwoPhase` enabled only with the nation's two-phase-search tech; the controls filled from the force;
    `ShowModal`. Its OK (`bnSaveClick` 0xb4c1d0: `mov edx, [eax+CurrentForce]; call 0xb4bef4`) runs the unnamed commit
    0xb4bef4, which writes exactly nine fields from the controls: `CapLevel` +0xa8 (`rgCAP.ItemIndex`: 0 Light, 1 Medium,
    2 Heavy, 3 Maximum), `NightCAP` +0xa7, `SearchLimitLeft` +0x10b, `SearchLimitRight` +0x10f, `SearchDistance` +0x113
    (0 = no search), `SearchInterval` +0x117, `RepeatSearch` +0x11c, `NightSearch` +0x11d, `TwoPhaseSearch` +0x11b; then
    `TForce.ProcessSearchDirectons` (0xe5ae44): `SearchPatternOrdered[i]` (+0xac) := the bearing i·20 lies between the
    limits, for i = 0..17. By the layout the array has 19 bytes (`SearchPatternCurrent` follows at +0xbf with 19
    Integers up to +0x10b; the RTTI gives neither a type); index 18 is never used. The spin edits allow left -10..370,
    right -10..360, distance 0..1000, interval 0..350 (interval and *Repeat search* are invisible). The other handlers
    write controls only (`cbNoSearchClick`: distance 0, or 300 again) and the map preview `TfrmMain.TempSearchLeft`..
    `ShowTempSearch` (+0x81c..+0x834; `seDistanceChange`, cleared by `FormClose`). No `Random` in the dialog
    (`randtree.py`).
  - *What the simulation does with them.* `CheckObjectives` (unnamed 0xdab764, StartMinute 0xdab6ff), for every force
    from the last: `TForce.CheckSearchAircraft` (0xe5a1bc; per sector: `SearchPatternCurrent[i]` minus 1 down to 0,
    `Random(3)`, and when the draw is 1 or at minute 0, the sector is ordered and its count-down is 0:
    `LaunchSearchAircraft(i·20)` (nothing with `SearchDistance` 0; `Random(25)`), whose launch sets the count-down to 180
    with `TwoPhaseSearch`, else 300); `GetForceFlagship` (0xdab816: every force's `ForceFlagship` cache is rewritten
    every minute); the CAP upkeep 0xea5048 (when `Random(5)` = 1 and the weather allows: `NightCAP`, `CapLevel`) and the
    emergency CAP 0xea5350 (`NightCAP`, `DoCAP`). Neither CAP routine tests PlayerSide or asks anything. The battle save
    keeps every setting except `NightCAP` (the image has no such key) and sectors 0..17 of both arrays; `TForce.Create`
    sets `CapLevel` 1, `SearchDistance` 300 and, after 1930, `NightCAP` 1, so a battle load turns night CAP on for every
    force. `SetAIAirSearchPlan` writes the same fields except `NightCAP` (see "The player's side").
  - `ForceFlagship` is read directly by the simulation in one place: EOTP's 0xdefcd8 (the computer's forces following
    their main force; `cmp dword ptr [esi+0x18], 0` at 0xdefdbf). It runs after `CheckObjectives` has rewritten every
    force's cache at the start of the minute, so a display write between minutes (`GetForceFlagship` from the dialogs;
    `TdlgSearch.Execute`'s own write when it is nil only repeats it) doesn't reach it.
  - *The scout question.* `TFightingShip.ShipLaunchSearchAircraft` (0xe48bf0; called from `CheckSearchAircraft`'s
    launches) asks "Do you want <ship> to launch scout aircraft?" (MessageDlg; No: no launch) when the ship's `Side`
    (+0xa55) is PlayerSide (`cmp eax, [0xf53630]` at 0xe48c78: the variable behind the cell 0xf27008), it has sighted
    ships, the battle preference *Floatplane launch* (prefs +0x4a, `TdlgPrefs.rgFloatplaneLaunch`: 0 Automatic, 1 Ask
    before launch, 2 Manual only) is not Automatic and `AutoRunBattle` is off. This is the only PlayerSide test in
    `CheckSearchAircraft`'s call tree (static scan, depth 5). The floatplane path (nested 0xe5a244): with *Manual only*
    only ships with `Side` = 1 go on (hard-coded, 0xe5a2aa); ships with `ForceIsHumanControlled` go through
    `TFightingShip.LaunchFloatCheck` (0xe48ff0: Automatic launches; Ask asks "Catapult launch search floatplane(s) from
    <ship>?" (or helicopters, or "... to stop to launch recon floatplane(s)?"); otherwise no launch), the others launch.
  - *Land-based CAP.* `LandCAPDivision[2]` (0xfb5920, cell 0xf27518): one requested `TDivision` per side. The division
    report: `Execute` sets `sbLandCAP.Down := LandCAPDivision[0] = the division` (0xae0acd); `sbLandCAPClick` (0xae261c)
    only asks "Request land CAP for this dvision instead of <slot 0's division>?" when slot 0 holds another division (No:
    the button goes up again); `bnOkClick` writes `LandCAPDivision[0] := frmMain.SelectedDivision` when the button is
    down (0xae0149..0xae0166; it never clears it). Slot 0 only: the game's human side. The battle setups clear both slots
    (0xbdbd23, 0xbdbfbc), the battle save writes `[Side<n>] LandCAPdivName` ('XXX' for none) and a battle load resolves
    both by name and side (0xdded55; nil when not found). `CheckLandBasedCAP(Side)` (unnamed 0xea5630, EAX = the side,
    'Error in CheckLandBasedCAP procedure!'; `CheckObjectives` calls it for side 0 and side 1 at 0xdab79f): if the
    requested division has ships, the airfield division (its flagship `IsAirfield`: `ShipType` 12 with +0x9e0 > 0)
    nearest to it (flagship to flagship, `GetRangeToShip`, strictly nearer in list order) that has a fighter formation
    based there (`TAirFormation.ScenHomeBase` = its flagship and `AircraftType.Role` 0, 10 or 11: the pure test 0xeac8f0)
    gets `CappedDivision` (+0x40) := the request; the CAP upkeep then flies that airfield's CAP over it. **The airfield's
    side is not tested** (no `Side` operand in the routine; run offline from the image with fake divisions, it gave side
    0's request to side 1's nearer airfield): the nearest airfield of either side is tasked. No `Random`; its exception
    handler clears the slot. The division report's OK is the other writer of `CappedDivision`: the report's own division
    gets the `cbCappedDivision` box's choice (0xadffe2).
- **Air strikes (2026-10-17, static reading of 1.01.44, for engine level 7; `verify_p3b.py` checks the patterns).**
  - *The dialog.* `TfrmMain.sbStrikeClick` (0xdfb6d8, "Order an airstrike"): `if tmrRunMinute.Enabled then PauseGame
    := 1`, then on `dlgStrike` (cell 0xf26f74, variable 0xf4c904) the list views' styles (0xe0a2c4) and `TdlgStrike.Execute
    (nil, nil)` (0xc287a8); its bytes are exactly that (`8B 90 <tmrRunMinute> 80 7A 50 00 74 07 C6 80 <PauseGame> 01 A1
    <cell> 8B 00 E8 .. A1 <cell> 8B 00 33 C9 33 D2 E8 <Execute> C3`). `PauseGame` (+0x7e8) is read by
    `tmrRunMinuteTimer` (0xdb22d2: stop the run) and EOTP (0xdafbad: refill the ship tree and the reports) on that PC
    only. `LaunchStrike1Click` (0xdfab7c, the reports list's popup, reports tab): `Execute(report, nil)`. `Execute`:
    `FillDivisionList` (0xc2c4f0: PlayerSide's divisions whose flagship is a carrier with aircraft, `ItemIndex := 0`;
    none: "No airstrike capable units available!"), `TheOriginatingDivision` (+0x4f8) := the combo's division, the
    target, weather, then `FillSquadronList` (0xc28a1c, 'Error in SetupStrike FillSquadronList procedure!') and `Show`
    (modeless). `FillSquadronList` clears `lvAirFormations` and adds, for each ship of `TheOriginatingDivision`, the
    formations with `Side` = PlayerSide, `ScenHomeBase` = that ship, `Status` 0 or 2 and at least `FilterValue` (+0x510)
    undamaged aircraft; for `Status` 0 it writes `LoadedNumber` (+0x1c) := `GetNumberOfAC(asUndamaged)` (a display
    routine writing a formation field). The items' `Data` (`TListItem.FData` +0x28) and `CurrentAirFormation` (+0x50c,
    set by the right-click popup) are raw pointers; the game refills the list only from `Execute`, Launch, Ready,
    `cbDivisionChange` and `seFilterChange` (not after a minute: a vanilla hazard once a formation is merged and freed).
    No `Random` in the dialog except Launch's `Random(10000)` (0xc294b3, outside the simulation).
  - *LaunchSomeAircraft* (unnamed 0xea4a18, 'Error in LaunchSomeAircraft procedure!'; called by `bnLaunchClick` 0xc294a7
    and `bnReadyClick` 0xc2b843 after `movzx ecx, byte ptr [ebx+Mission]; mov eax, ebx`, and by the simulation:
    AILaunchStrike 0xdfdfd4, the escort routines 0xdfe508/0xdfe7c8, `ShipLaunchSearchAircraft`, `DoCAP`). Register
    arguments EAX = the source formation, EDX = the number, CL = the mission; on the stack (pushed in this order)
    `const aTarget: TPointLL` (by reference, copied from `[ebp+18h]`), `aName: string` (`[ebp+14h]`), `aOrderedState`
    (`[ebp+10h]`), `aLoadout` (`[ebp+0Ch]`), `aSpotNow` (`[ebp+8]`); `ret 14h` (0xea4c69); returns the new formation or
    nil. Its work: n := min(n, undamaged aircraft) (0: nil, nothing changed); `TObject.Create`; `TList.Add` to the air
    list; `AircraftNumber`, `Mission`, `CopyBasics` (0xea49bc), `Loadout`, `SetFullFuelLoad`, `Status` := the source's,
    `ParentAirFormation` := the source's parent or the source, `TargetLoc`, `Course` := wind + 180, `Name` := aName,
    `AFId` := the allocator 0xea503c (`inc dword ptr [0xfb0120]; mov eax, [0xfb0120]; ret`), `UnitId`, `LoadedNumber`,
    `OrderedState`, three loss counters 0; asFlying: `StartLaunchProcess` (only the box "Warning! Air formation ... has
    target 0,0!" for a non-CAP mission); aSpotNow: `Status` 3, `StatusCount` 1; an empty name: `<mission name> #<ScoutCount>`
    and `inc [0xfb011c]`; the source's `AircraftNumber` minus n. No `Random` and no PlayerSide read in its call tree
    (54 routines, depth 6). `ConfirmTargetLoc` stays 0 (a new object is zeroed).
  - *Launch* (`bnLaunchClick` 0xc28c1c, 818 instructions): a validation loop with MessageDlg boxes (no selection; no
    target; the special squadron; range; night or dusk landing; the carrier can't fly; torpedoes or ASMs left; target on
    land: change to ground attack?; coordinate the strike?; heavy load?; bombs or torpedoes for this mission?; light load
    only from carriers under 10000 t; no undamaged aircraft; multi-carrier coordination tech), which may write the
    squadron's `Mission` and `Loadout`; then per selected row: `GroupIndex` := 0, `Loadout` 0 -> 1 (escort: 1),
    `LaunchSomeAircraft(sq, StrToInt(SubItems[2]), sq.Mission, @aTargetLocation (+0x514), '', asFlying, sq.Loadout,
    False)`, then on the new formation `GroupIndex` := `Random(10000)`, the source's `Mission` and `OrderedState` := 0,
    at minute 0 with a nation flag spotting (`Status` 3, `StatusCount` 1), `Location` := the carrier's `ShipLocation`,
    for naval/ground attack `PriorityTarget` (+0xae) from `cbPriorityTarget` and `ConfirmTargetLoc` (+0xf1) from
    `cbConfirmTargetLoc` (unless the source was ready), with `cbCoordinated` the first such formation leads
    (`CombinedStrikeSpeed` +0x9e := its cruise speed), and afterwards every launched formation with aircraft (same
    carrier, or the tech) gets `LeadAirFormation` (+0x41) := the lead, the lead's `GroupIndex`, the slowest
    `CombinedStrikeSpeed` and the lead's `StatusCount` when its range is shorter; finally the global 0xfb5918 (cell
    0xf27604) := `cbPriorityTarget.ItemIndex` (a UI default saved with the campaign, read by nothing else) and
    `FillSquadronList`. Every formation write after the call is one of Location, Mission, LeadAirFormation, Status,
    OrderedState, StatusCount, CombinedStrikeSpeed, GroupIndex, PriorityTarget, ConfirmTargetLoc (the verify script lists
    them). *Ready* (`bnReadyClick` 0xc2b52c): the same validation of weapons, then `LaunchSomeAircraft(sq, n, sq.Mission,
    @(0,0), '', asReady, sq.Loadout, False)` and nothing after it; the status machine readies the child. *Stand down*
    (`bnStandDownClick` 0xc2c2b4, "Stand down selected squadron(s)?"): for each selected ready formation `Mission` and
    `OrderedState` := 0, `Status` := 10 (asStrikingBelow), `StatusCount` := `AircraftNumber`, then 0xead168(formation, 0)
    returns its weapons to `ScenHomeBase`: Loadout 4 `AirTorpedoes` (+0xbb3) += LoadedNumber, 5 `AirToSurfaceMissiles`
    (+0xbb7) += n, 7 the missiles 2n, 6 the torpedoes 2n (the simulation's status machine calls the same routine). The
    popups write the right-clicked formation's `Mission`, `Loadout`, `LoadedNumber`; *Auto select strike* also writes the
    ships' `tmpMissilesUsed`/`tmpTorpsUsed` (+0xbdc/+0xbe0), scratch fields read only by `SetBasicMission`, which only
    the dialog calls.
  - *Ids and lists.* The AFID counter 0xfb0120 has four users: the allocator (0xea503e/0xea5043), the battle save
    (0xea3e90) and the battle load (`LoadAirFormations` 0xea3f88: `AFID`, `ScoutCount`, `AFNumber`, then each `LoadMe`,
    then `ParentId`/`LeadId` through `GetAirFormationById` 0xe9f538, a linear search comparing `[item+AFId]`). The other
    `GetAirFormationById` callers (0xdf18bf, 0xdf3224: a hit credited to the formation whose id an object kept at +0x48)
    look up by id too. Nothing compares ids with the counter or indexes an array by id, so ids far above the counter
    (1,000,000+) are safe; they are saved as numbers. `ScoutCount` 0xfb011c (cell 0xf26ed4) is also used by `DoCAP` and
    `ShipLaunchSearchAircraft` for their generated names. `TPointLL` is TGlobe's record `iLongX, iLatY, iHeightZ:
    Integer` (12 bytes; its type info at 0xa8a644); the game prints a coordinate as degrees by dividing by 5,400,000
    (0xa8c3b9). Formations are freed only in EOTP (`MoveAircraft`'s merge and delete, `CheckStatus2`).
- **Air facts confirmed live (stage B proof, 2026-10-17, 1935 fixture).**
  - The game's own run start closes the strike dialog: StartRun (0xdae9e0; `sbPlayClick` calls it with `mov edx, 1`)
    tests `dlgStrike.Visible` (`mov eax, [0xf26f74]; mov eax, [eax]; cmp byte ptr [eax+0x69], 0`, 0xdaea08) and calls
    `TCustomForm.Close` (0xdaea1c). The dialog never stays open into a running minute (vanilla too); only one opened
    while a minute runs is still open at the next boundary.
  - `lvAirFormations` has item check boxes (form resource: `Checkboxes = True`, `MultiSelect`): Launch, Ready and Stand
    down act on the checked rows; a row that is only selected gives "No air formations are selected!". The right-click
    popups depend on the column under the point (`lvAirFormationsContextPopup`; Mission: Naval strike, Ground strike,
    Escort, Recon). A checked row keeps the mission None unless the popup or *Auto select strike* sets it.
  - `TAirFormation.GetNumberOfAC` (0xe966ec): status 11 = the `Damaged` counts of statuses 1..10 (+0x61..) plus
    `DamagedFlying` (+0x89); status 13 for an unready squadron = `AircraftNumber` - GetNumberOfAC(11) -
    `AircraftDestroyed` (+0x8d): the "available" number `FillSquadronList` compares with the filter and writes into
    `LoadedNumber`.
  - `sbRoutineAirOpsClick`'s "selected map flag": `TDivision.myFlag` is a `TDivLocation` (`TGlobeObject`) whose state
    word +0x28 bit 0x40 is the selection (getter 0xab2ff0).
  - The recon launches follow the sectors exactly: after a dialog OK, the next minute's `CheckSearchAircraft` sent one
    scout per ordered sector, bearing carrier -> target = sector x 20 (left 200, right 300: 200, 220, ..., 300), and set
    the sectors' count-downs to 180 (two-phase) or 300.
  - A sunk carrier's squadrons stay in the air list with `ScenHomeBase` = the sunk ship (gone from the all-ships list);
    its side's strike dialog then lists nothing for that division.
  - Stand down after readying with torpedoes: Bearn's `AirTorpedoes` 10 -> 7 (3 aircraft readied with loadout 4), 8
    while readying (the formation's `AircraftNumber` fell to 2, `LoadedNumber` stayed 3), 11 after Stand down
    (+`LoadedNumber`): the stock can end above its start. The same on both PCs (game arithmetic).
  - A battle in battle slot mode (the mod's test battles) applies its results to the host's campaign in memory: the
    next `pocbattle` at the same place found "Enemy unable to challenge our forces in the area!".
  - `TFightingShip.GetTypeAndNameForEnemy` (0xe343d4, the display naming of an enemy ship) calls `IdentifyType`
    (0xe40268) and `IdentifyClass` (0xe3ffc4), which draw `Random` (0x837834) and write `IdentifiedAsType` (+0xd70) and
    `IdentifiedAsClass` (+0xd6c): display code with random draws writing ship fields (the mod's dual identification
    sets keep them out of the simulation, and since engine level 11 also the ones drawn during a load: see "The load's
    own drawing identifies ships" above). The simulation itself reads and writes the identification in the sighting
    reports routine (0xdc960c, EOTP), `TfrmMain.UpdateReport`, and the air attacks (`TAirFormation.AttackShips`,
    `AirBombShip`, `AirdropASM`: `IdentifyType`). The box "Unknown ship sighted!" also comes outside minutes (seen at a
    battle's first display on the host's copy).
  - *The campaign's ship-type flags (0.4.1, engine level 17).* `TFightingShip.SetMistakenType` (0xe4979c, from
    `IdentifyType`, `AttackShips` and `ShowMissileRanges`) and `IdentifyType` read three unit Booleans (0xf54935,
    0xf54936, 0xf54937: a battleship, a battlecruiser, a carrier (CV or CVL) exists; cells 0xf26dc8, 0xf27b34,
    0xf26df0). Only `TfrmBuildCamp.CheckShipTypesExist` (0xd4e314) writes them, from every nation's ships in play
    (`CountShipsOfType(type, InPlay)` for nations 0..8), and only `ReduceTimeLimits` calls it (from `DoTurn` and
    `DoStrategicTurn`: the month's processing). So they hold what this process's last processed month left: 0 in a
    game that never processed a month (a joined player's, or the host's first battle after its game started), the
    campaign's after one. The rules: a battleship whose roll says "mistaken" (Random(100) < 21) becomes a battlecruiser
    if the year is before 1907 or a battlecruiser exists, else a heavy cruiser (no extra draw); a carrier or light
    carrier becomes a heavy cruiser if no carrier exists; a ship identified as a battleship when none exists draws
    another Random(100) (0xe49e36) for a pre-dreadnought or heavy cruiser. The 0.4.1 RC's lockstep_strike DESYNCs
    (minutes 53 and 74) parted exactly there: the host had processed 1935 months (flags 1), the joined player not (0);
    the identification differed silently from minute 47, and the epoch after the first resync parted at the same draw
    with the identification equal at its boundary. No display path wrote the identification (the display's only writer
    is `GetTypeAndNameForEnemy`, a hooked display routine whose writes go to the display set). The mod finds the three
    through `CheckShipTypesExist`'s prologue (`mov esi, eax`, then three `mov eax, [cell]; mov byte ptr [eax], 0`).
- **Missiles (2026-10-17, static reading of 1.01.44, for engine level 9; `verify_p4.py` checks the patterns).** The
  weapons research of 2026-10-16 (`limits-weapons.md` §2, §3) with the details the build needed.
  - *Automatic fire.* `CheckMissileFiringForAllShips` (0xda91fc, EOTP after 1940), per division with ships and `Side` <
    3: no `myForce` (+0x57) → the routine stops (with the box "Warning: Division : ... has no force defined!" when the
    global at cell 0xf271b8 divided by 5 is 0); then the gate (0xda92ce): `cbFireAtWill` (+0x658) `GetChecked` (VMT
    +0x16c) → *fire*; else the force's `AIControlled` (+0x79) and the flagship's (`GetFlagship` = `Ships[0]`) nation
    record (array 0xfacb4e, cell 0xf27a0c, 0x14e bytes per nation) value +0x149 < 2 → *fire*; else `Side` = PlayerSide →
    *skip* (0xda946b); *fire* (0xda931b): `HoldMissileFire` (+0x69) → the same *skip*; then per ship (afloat; sighted
    ships, or the nation's +0xc8 and +0x149 > 0; SSM aboard, or medium SAM with the nation's +0x8c; `Surprised` < 5) a
    `Random(20|22|25, +10 if surprised)` < 12 gate and `AICheckMissileTarget` (0xda7068). Both paths then run the second
    loop: `CheckSAMFireAtSnoopers` (0xda6ccc) for each afloat ship of the division, until the form's `SAMatSnoopers`
    (+0x722) is set. With PlayerSide 2 every path of a side-0/1 division reaches *fire*, so the box's value changes
    nothing. `HoldMissileFire` is read only at 0xda9326 in the routine's call tree (all calls to depth 12; the other
    [reg+0x69] reads there are VCL fields); elsewhere only the division report reads it (`TdlgDivisionReport.Execute`
    0xae0af7, its check box) and writes it (`bnOkClick` 0xae0146, an order), and the targeting step `sub_d92b48` (EOTP
    before the missile fire, 0xd92c01) clears `HoldFire`, `HoldTorpedoFire`, `HoldMissileFire` and `ManualTargeting` of
    every `AIControlled` division each minute. `cbFireAtWill` is read by nothing else (its `OnClick` is
    `cbShowBaseFlagsClick` 0xe008d8: prefs +0x38 := `cbShowBaseFlags.Checked`, repaint), and `rgMissileDoctrine`
    (+0x63c) only by `FireSSMAtTarget` (0xda7d20/0xda7d56, for `Side` = PlayerSide). Neither is written by game code or
    kept by the battle save; `UpdateMissilePanel` shows `pnMissiles` and sets `sbMissilePanel` down but doesn't touch
    them. `TCustomCheckBox.FState` is +0x2d2.
  - *Manual fire.* `sbFireSalvoClick` (0xe04554): `if tmrRunMinute.Enabled then PauseGame := 1`, then on
    `dlgMissileFire` (cell 0xf27aa8, variable 0xf53270) `FillMissileTargetList`, `FillShooterList(EmptyPoint, nil)`,
    `FillTechExplanation`, `ShowModal` (VMT +0x1b4). The dialog (`TdlgMissileFire`, `bsDialog`): `lbMissileTargets`
    (+0x438; sighted enemy ships and `TReport` contacts of PlayerSide), `lvMissileShooters` (+0x440, check boxes;
    PlayerSide's afloat ships with heavy/medium SSM or medium SAM launchers, then the missile submarines of the list
    0xf53188 (cell 0xf2742c) with `Side` (+8) = PlayerSide, `SubType` (+0x4c) = 4, `Downtime` (+0x10) <= 0; the items'
    `Data` is the ship or the submarine), *Fire one/half/all* (`bnFireOneClick` 0xd81494 etc.: `FireMissiles(1|2|4)`),
    *Center map* and *Locate to* (display). `lbMissileTargetsClick` refills the shooters for the target's location.
  - *The commit* `FireMissiles(aNumberToFire)` (0xd80c98): no target → "No target selected!"; the location is the target
    ship's `ShipLocation` or the report's `GetPredictPoint(0)`; for each checked item (`TListItems` count 0x9e6b28,
    `GetItem`, `TListItem` checked 0x9e5ff8): a ship's mounts of type 4, then 5 and 6, then (with the nation's +0x8c,
    within medium SAM range, and the answer to "Fire medium/heavy SAM as well?" kept in `frmMain.FireMSAM` +0x750: 6
    Yes, 14 Yes to all, 13 No to all) types 1 and 2 go through the nested procedure 0xd80aec: below
    `GetMissileMinimumRange(4)` → "too close"; the mount can't bear (`MountCanFire`, not ship type 12) → the ship's
    `PendingMissileTarget` (+0xa93, 12 bytes) := the location (0xd80b79); a friendly ship in the line of fire → "abort";
    else `FireMissileMount(mount, ship, target ship | nil, @location, GetBearingToPoint, N)` (0xd80c0b), and for *one*
    the stop after a launch of 1, for *half* when half of the ship's launchers is not above the missiles fired so far.
    Then `UpdateShooterItem` and the counts. A missile submarine fires `FireSSM` twice with **side 0** (`xor edx, edx`
    at 0xd8109b/0xd810f7) and nation 0's speed, range and calibre, counts 2 and sets its `Downtime` := 5000 (0xd81102).
    Each item is unchecked (0x9e604c). The end: `PlayMissileLaunch` := 0 and the 'Missile Launch' sound when anything
    fired, and the summary box "<n> missiles fired!" with the turn/too-close/abort counts.
  - *The fire routine* `TfrmMain.FireMissileMount(aMount, aShip; aTargetShip, aTargetLocation, TargetBearing,
    NumberToFire): Integer` (0xe04718; EAX = frmMain, EDX = mount, ECX = ship, the four stack arguments pushed in that
    order, `aTargetLocation` by reference, `ret 0x10`; the result is `[ebp-8]`, the missiles fired). Its preconditions,
    all exiting to the end of its try block (0xe04a4a) with nothing written: `ElectricDisabled` (+0xb33), the mount's
    `Destroyed`, `TorpsLeft` > 0, `MountCanFire(TargetBearing)` or `ShipType` 12, the distance (`GetDistanceToPoint`)
    above `GetMissileMinimumRange(MountType)` (0xe034f4: 3000 or 6000 by type) and below the maximum range
    `sub_e4accc(Nation, MountType)` (a nation table at 0xfacb4e). Then `min(Tubes, NumberToFire)` missiles (`TorpsLeft`
    is checked only on entry): `FireSSM(ship.Side, @ShipLocation, name, aTargetShip, GetTrueBearingToPoint, GetSSMSpeed,
    maximum range, GetSSMCalibre, (1 - CrewQuality) * 2)`, `TorpsLeft` - 1 floored at 0, `SetReloadTime` with reloads
    (7 `Random` sites), "Launches <type> at <target | unknown ship!>" into `myHits`. With `ShouldReport` (or PlayerSide
    2) "<ship> is launching <type> at enemy ships!" into `MessageLog`; else, for a target ship that is
    `ForceIsHumanControlled`, once per battle "Enemy targeting radar is locking on to <ship>" with `FirstSighting` and
    `PauseGame`. `MountCanFire`, `GetDistanceToPoint`, `GetBearingToPoint`, `GetTrueBearingToPoint`, the minimum and the
    maximum range draw no `Random` and write nothing. Callers: the dialog, `FireSSMAtTarget` (0xda7ea1, N = 2) and
    `ShipFirePendingHeavySSM` (0xe4b95c: mount types 4 and 5, N = 4, then `PendingMissileTarget` := EmptyPoint, whose
    third dword is the routine's uninitialised stack). `TfrmMain.FireSSM` (0xda6374; EAX, EDX = side, ECX = @location,
    seven stack arguments, `ret 0x1c`) creates the missile (`TTorpedo`, `Calibre` >= 30 = `IsMissile`) and adds it to
    the torpedo list; no `Random`, no UI.
  - *PendingMissileTarget* is written only by the dialog (0xd80b79), `TFightingShip.Create`, the battle setup and load
    (`sub_bdb82c` empties it for every ship of a side, from `LoadScenarioInProgress` 0xcd033f and the battle setups) and
    `ShipFirePendingHeavySSM`; read by `FireAllGuns` (0xe3db26, `IsEmptyPoint` 0xe58a28: the first two dwords zero) and
    the ship steering `sub_db39cc` (from `MoveAllShips`). The battle save doesn't keep it.
  - *Submarines* (`TSubmarine`, the list 0xf53188): during a battle written only by the simulation (EOTP's submarine
    routines 0xdc4154, 0xdcf83c, 0xdec0e0, 0xdee320, `CheckMissileFiringForSubmarines` 0xe0529c (`Downtime` := 20000
    after a salvo, `FireSSM` with the submarine's own side), the air search's `CheckAirSubmarineSighting`), the battle
    load (0xddcec0), the battle setup (0xdc339c: the scenario's 'Submarines' reader 0xd6a9ec, and 0xdee538) and the
    missile dialog's submarine branch; display code (the globe 0xde0998, the missile dialog's lists) only reads them.
- **Missiles confirmed live (stage C proof, 2026-10-17, the 1958 missile fixture: Italy vs the Soviet Union).**
  - *SAM against missiles.* `ResolveMissileHit` (0xdf0a58) tries the target ship's light SAM (`DoesSAMvsMissile` size
    3, 0xdf12a1), its medium SAM (size 2, 0xdf135b), then an adjacent ship's (`sub_e04c3c`: "LSAM/MSAM from adjacent
    ship <name>"), CIWS, radar guided MAA and ECM ("decoyed by countermeasures"). `DoesSAMvsMissile` (0xe4ad8c) for
    size 2 needs the firing ship's nation record flag +0x90 (0xfacbde), set by the research "Improved medium SAM"
    (1970, `Research19Level24`); light SAM mounts come with "Light SAM" (1959); heavy SAM never engages missiles. Then
    `Random(120)` > the superstructure damage %, `ShipFireSAM` (spends the SAMs: the stock drops even when they miss)
    and a `Random(100)` roll per SAM. So in 1958 no SAM stops a missile (the realistic fixture: no SAM fired at a missile
    through a ~49-missile exchange; one Soviet SAM at an aircraft); with the 1970 flag added to the save, both sides' SAMs engaged and the
    attacked side's stock dropped (Italy shot down 2, 42 -> 38; the Soviet SAMs fired 2 and missed, one Italian missile
    was decoyed), equal on both PCs. The lines "<n> Missile shot down by MSAM." etc. go into the target ship's `myHits`
    (+0xd49, both sides) and into the battle's `MessageLog` only for PlayerSide's ships (or PlayerSide 2).
  - *The battle's nation records:* a battle numbers its two nations 0 and 1 (ships' `Nation`, the record array
    0xfacb4e); the fire-at-will gate value +0x149 was 0 for both nations, so the game's exception (a computer force of a
    nation below 2 fires with the box unchecked) applies to every computer-controlled force.
  - *Launchers before 1959 are fixed* ("Improved SSM launchers: Launchers can be trainable", 1959): *Manual fire*
    mostly answers "<k> ships with mounts not bearing will turn to fire SSM" (PendingMissileTarget) besides the launches.
    `FireAllGuns` (0xe3da88, from the minute's gun step for every afloat ship at each interval) calls
    `ShipFirePendingHeavySSM` unless `[ship+0xa72]` > `Random(10)` or `[ship+0xdda]` > 4, which fires up to 4 per heavy
    or medium SSM launcher and clears the target whatever it fired: a pending target normally lasts less than a minute.
  - *UpdateMissilePanel* shows `pnMissiles` once per battle screen (form +0x723) when PlayerSide's missile ships have
    enemy ships in range, and every new battle screen (each live battle load) starts with the panel hidden.
  - The missile dialog lists the targets as the sighting knows them ("Unidentified ship", "CA", "CL") and the own
    missile submarines with Downtime <= 0 as `SSG <name>` rows; its shooter rows show the loaded missiles
    (`TorpsLeft`, "0" while reloading).
- **The game's texts in a battle, and whose they are (2026-10-17, static reading of 1.01.44, for the pre-release fixes
  of 0.4.0, engine level 12).**
  - *The message routine* (0xd8fd5c, called by EOTP at 0xdafb03) moves `TfrmMain.MessageLog` (+0x7f8, one entry per
    message: the first character is the message type '0'..'?', a jump table at 0xd8ff3e; `Objects[i]` the unit it is
    about) into the log panel `richLog` and into `TotalLogLines` (+0x7bc, `AddObject`) and `TotalTimeStamps` (+0x7c0,
    `Add` of "dd hh:nn "); some types set the routine's result (EOTP's break, `PauseGame`) and show a box
    (`ShowMessagePos`, gated by the preference +0x1f and `AutoRunBattle` off). The missile hits of an interval come as
    one box from `MissileHitString` (+0x7c4) at its end (`MessageDlg`, preference +0x1e). After every batch it compares
    the two lists' counts and on a difference shows "Log lines out of sync! <lines>/<stamps>" (0xd90dd5, no break) and
    deletes the longer list's last entries.
  - *The battle log in the battle save:* the save (0xdd1010) writes the two lists with `TStrings.SaveToFile` as
    `TLog.log` and `TTime.log`, the load (0xdde0a0) reads them back with `LoadFromFile` when the files exist, and the
    setup (0xdc339c) clears them. `LoadFromFile` splits an entry with a line break (the batteries' "<division> is out of
    sight of the\nflagship ...") into two, so a save and a load of a log with k such entries gave n+k lines for n time
    stamps: the box at the first minute after every load (seen: 18/15, 29/26 and 89/82). Display data only: nothing in
    the simulation reads the two lists (the total log dialog `TdlgTotalLog` shows them).
  - `TfrmMain.PauseString` (+0x838) collects reasons ("Sighting\r", "Gun hit <ship type>\r") but nothing reads it.
  - *Sighting boxes* (the sighting reports routine 0xdc960c): "Unknown ship sighted!" / "Land installation sighted!"
    (0xdca9d3) for a sighting by a division whose force is not `AIControlled`, while `FirstSighting[PlayerSide]`
    (+0x800) is 0, PlayerSide is not 2 and `AutoRunBattle` is off; then `FirstSighting[the division's Side]` := 1 and
    `DialogUp` (+0x71d) := 1. So with both sides human and PlayerSide 0, the second side's sightings show the box again
    and again until side 0 sights something. "Unknown radar contact!" (0xdcac10) follows similar rules. Right after
    either box the code reads the sighting division's side (`mov eax, [ebp-20h]; mov eax, [eax+23h]`, `TDivision.Side`).
  - *Missile hits* (`ResolveMissileHit` 0xdf0a58): "Our ship <GetTypeAndName> is hit by a missile!" when the ship's
    side is PlayerSide or PlayerSide is 2, else "Enemy ship <GetTypeAndNameForEnemy> is hit by a missile!"; both into
    `MessageLog` and `MissileHitString`. Other texts of the second side's own units appear in a battle only because
    its divisions are human there: "<division> is out of sight of the flagship and switches to AI control!", "<division>
    signals misunderstood!", "<ship> reports a sonar contact, possible submarine!", "<ship> is sinking!", "Enemy
    targeting radar is locking on to <ship>", the enter-port, detach and survivor questions.
  - `ShowMessagePos` (0xa07124) pushes four arguments and calls `MessageDlgPosHelp` (0xa06eb0) with no frame of its own:
    in a hook of `MessageDlgPosHelp` the box's caller is the return address after those four arguments and EBP is the
    caller's frame. `MessageDlg` (0xa06c50) has a frame.
  - *The result box:* the end of a battle (0xdc4154, inside EOTP, after the results dialog) calls
    `TfrmBuildCamp.HandleBattleAftermath(VP0, VP1, aVictoryLevel)` (0xcc0844): it adds the log-adjusted VP to the player
    nation and the enemy, draws `Random` for prestige and morale, and writes `tmpBattleString` (+0x4abe): "<the player
    nation's Name2 (the adjective)> <level lowercase> victory" or "Enemy <level> victory" or "The battle was a draw.",
    then '\r'-separated lines ("Gain one prestige", "Fleet morale improves", "Lose one prestige", ...), then "\r\rOwn VP:
    <VP0>\rEnemy VP: <VP1>". `TfrmMain.FormClose` shows it (`ShowMessagePos` at 0xdcd802); nothing else reads it.
  - *Fire mode* (`rgMissileDoctrine.ItemIndex` +0x2e0) is read only in 0xda7ccc and only for ships whose side is
    PlayerSide: the chance to keep firing starts at 40 %, +25 with *Half* (and more than 2 heavy SSM left), +55 with
    *All*; the other side's ships get +10 against a large target instead.
- **Two PCs: the preferences, the number format and the map of a side (2026-10-19, static reading of 1.01.44 with
  the offline workspace's call graph, and the live runs nb1-nb8 of
  [MODLOG](../MODLOG.md#investigation-2026-10-19-live-battles-between-two-pcs-v041); for engine level 15).**
  - *The Preferences file* (`<Documents>\My Games\Rule the Waves 3\Preferences`, an INI file, section `[Settings]`) is
    read by `TdlgPrefs.LoadPrefs` (0xe0b0b8) into one record at 0xf5327c (cell 0xf278b4), called from
    `TfrmCampaign.FormCreate` (the game's start), `TfrmSelectNation2.FormPaint` and the dialog's reset. It is written
    by `SavePrefs` (0xe0d588) only from `TdlgCampaignStart.FormClose` and `TfrmSelectNation2.FormClose`: never during a
    battle or at the game's exit. The battle screen's *Preferences* (`TfrmMain.sbPrefsClick` 0xdcf764 ->
    `TdlgPrefs.Execute` 0xe08dd0) writes the record directly on Ok (e.g. `RealismSetting := rgRealism.ItemIndex` at
    0xe09143; opened from a battle screen and closed with a plain Ok it wrote `RealismSetting` -1 in a test).
  - *Which preferences a battle uses* (every read of the record classified by whether the battle load,
    `LoadScenarioInProgress` 0xcd01b0, or the simulation roots, StartMinute 0xdab630, EOTP 0xdaf83c, MoveAllShips
    0xd8c2f4, MoveTorpedoes 0xda5ee0, the detach routines, reach it, and what the read decides). **The simulation's**
    (they change the loaded battle, the simulation's state or its shared random numbers; a live battle runs both PCs
    with A's since level 15): `AIControl` +0xc (every load: `SetInitialDivAIControlFromUserPrefs` 0xd765f4, called by
    the scenario loader 0xd64f80 for every division, sets `TDivision.AIControlled`), `FloatWarning` +0x14
    (`DoDamageControl` writes a ship field by it), `ReadyPopup` +0x18 (`TAirFormation.CheckStatus2`),
    `BreakOnNewSighting` +0x1c (the sighting routine 0xdc960c draws a shared `Random(100)` by it),
    `AirApproachMessage` +0x25 (EOTP draws `Random(10)` by it), `AirSurfaceMessageLvl` +0x2a and `SoundOption` +0x42
    (`TAirFormation.AttackShips` and the message routine 0xd8fd5c draw sound variants by them), `LetAIControlForce`
    +0x2f (EOTP gives forces to the computer), `LOScalc` +0x3f (line of sight in the sighting routine), `Autosave`
    +0x46 (EOTP saves every N minutes), `FloatplaneLaunch` +0x4a, `RealismSetting` +0x56 (18 routines: the detach, the
    sighting routine, the objectives, torpedo fire, ...), `NoFriendlyTorpHits` +0x357, `BSSMHC` +0x37b (a value of the
    simulation routine 0xdf0d5d; no dialog sets it). **Display, sound or the clock** (each PC's own): the breaks
    (`BreakOnSighting` +0x0, `BreakOnId` +0x4, `BreakOnHit` +0x10, `BreakOnTorpedoHit`, `BreakOnMissileHit`,
    `BreakOnSinking`, `BreakOnSunk`, `BreakOnSubsighting`, `BreakOnAirAttackOnShips/OnLand`, `BreakOnAirCombat`
    +0x1d..+0x24: a box and the routine's break result; in a live battle A's break stops the clock for both, B's is
    ignored, and boxes inside a minute get the mod's fixed answer), `OpenFireMsg` +0x8, `LoseContactPopup` +0x19 (only
    the box "... switches to AI control!", 0xde3aad), `AirCombatMessageLvl` +0x26, `DamageMeter`, `CourseIndicator`,
    `ShipFlags`, `Compass`, `BaseFlags` (+0x34..+0x38), `DarkMode` +0x39, `GridLines`, `CourseClick`,
    `SpaceBarRestarts`, `BackgroundSound` +0x4e, `SoundVolume` +0x52, the window sizes and states, the colours,
    `NoBorders`, `Scale` +0x34e, the deck colours, the range circles, `InsetView`, `FireLines`, `EnglishRanks` +0x358
    (officer rank texts, 0xd39184/0xd39344) and `ConvertCharacters` +0x40 (`ConvertGermanCharactersInName` 0xd4ce8c:
    ä->ae, ö->oe, ü->ue, å->aa and the capitals, in the campaign loaders `TBuilderNation.LoadMe`, `TPossession.LoadMe`,
    `TCampaignShip.LoadMe` and the naming routines; a battle's ships keep the battle file's `Ship<n>Name`). **Not read
    in a battle:** `FOW` +0x30 (one reader, outside the battle code), `AutoSubMoves`, `AutoASWMoves`; `LargeBattles`,
    `SupportForce` and `HistoricalForces` shape only the battle A sets up before it saves it; `AutoAssignCO` +0x364
    only the aftermath (the result screen 0xdc4154 -> `HandleBattleAftermath`, each player's own).
  - *Live checks of the classification:* B with `ConvertCharacters=1` on 0.4.0 (A 0): READY and 10 minutes equal; B
    with 34 preferences different from A's on level 15 (both lists): every hash equal over 48 minutes of two battles.
    `AIControl` differing on 0.3.2 and 0.4.0: READY equal (0.4.0's `ai0`/`ai1` put the computer control back).
  - *Number format.* The game takes the user's locale (`FormatSettings`, variable 0xf2b6c8, cell 0xf27848:
    `ThousandSeparator` +0xC0, `DecimalSeparator` +0xC2, `DateSeparator` +0x6, `ListSeparator` +0xA) but only for
    display ("Day sighting range: 24.000 yds" on a comma locale). Its files hold no decimal numbers and no
    locale-formatted dates: every save file (`.bcs`, `.off`, `.sta`, `.des`, `MapData<n>.dat`, the battle's `.sac`) is
    UTF-8 with a BOM and integer-only (the battle clock is `Day`, `Month`, `Hour`, `Minute`, `TimeElapsed`), and no
    game code parses a float from a file (the float conversions are list sorts and `TCustomIniFile.ReadFloat`, which
    `LoadPrefs` doesn't use). So neither the separators nor the ANSI code page change what a PC reads from another PC's
    files. The battle file's only absolute path is `FlagFile=` (the side flags; the mod rewrites it to the reading PC's
    game folder, `FixFlagFiles`).
  - *The map of a side.* The game decides what the map shows inside the simulation and in every load, for its
    PlayerSide (0 on both PCs of a live battle) and the computer side of the side table 0xf278e4 (0x47-byte records
    per side; the resume makes side 1 the computer's): the sighting routine 0xdc960c calls
    `TfrmMain.SetHiddenDiv` (0xdb2424) for every division (its flag hidden for side 3 or a computer force
    (`TForce.AIControlled`), else shown; a role-0 division of a computer force keeps its flag), sets each ship of a
    human side visible unless its division's flag is hidden, hides each ship of the computer side unless
    `GetBestSightingLevel() > 0` and `SeenByHumanControlledShips` (skipped when `TfrmMain.BothComputer` +0x7b5),
    shows a human side's ships that the enemy spotted (`OwnShipSpotted` +0xb21, `OwnShipRadarSpotted` +0xb25,
    `ShipType` 12) and labels every ship (0xde4ce4: names for the human side, the identification for the other; all
    ships through 0xde3f88, the routine `CbShipNameDisplayChange` calls). `TFightingShip.SetVisible` (0xe3f498) sets
    `Hidden` (+0xaf5) and its wake objects (`Wake` +0xaec, `SmokeLine` +0xae1, `Wake1` +0xaf0); the hidden setter of a
    `TGlobeObject` is 0xab2ff8 (bit 4 of the state word +0x28). Division flags are `TDivision.myFlag` (+0x53,
    `TDivLocation`) and move with their divisions. At far zoom the drawing (0xd8d570) shows newly sighted enemy ships
    too (the game's own rule). With both sides human the second side's PC got side 0's picture: its own ships hidden
    under their flags until side 0 saw them, side 0's ships and names shown, every division's flag on both maps.
    Right after a load `SeenByHumanControlledShips` holds the first side's sightings only (the load's sighting routine
    ran while side 1 was still the computer's).
  - *The ensign and the form during a load.* The resume (`LoadScenarioInProgress`, 0xcd02c9..0xcd02e2) copies the
    campaign screen's flag image (`TfrmBuildCamp` +0x554) into `TfrmMain.ImgFlag` (+0x530) with `TImage.SetPicture`
    (0xa0e5dc: `FPicture.Assign`); the battle slot's campaign is the first player's, so the second PC showed A's
    ensign. `TfrmMain.LoadSideFlags` (0xdf6b60) loads `SideFlags[0]` (+0x754) and `SideFlags[1]` (+0x758) (TGraphic,
    `LoadFromFile` at VMT +0x58) from the side records' flag file (side table +4 and +0x4b), falling back to
    `<game>\Flags\<its file name>`. The load shows the battle screen and draws the first side's picture (the map on
    its fleet, its flags) before anything else can run, and gives the form its saved size again (1000x719 on the
    suite's PC, the values of its Preferences' `MainFormWidth` +0x5d and `MainFormHeight` +0x61), so every load draws
    at that size whatever the window was before.
  - *The x87 control word.* A process starts with 0x37F; the game sets 0x27F later (both copies at battle time on one
    PC). A DLL loaded into one PC's game (drivers, overlays, hooks) could change it there; the loaded battle and every
    minute compute with it.
- **Two PCs: CPUs and data files (2026-10-20, 1.01.44, engine level 16).**
  - *x87 transcendentals.* Intel and AMD don't round FSIN, FCOS, FSINCOS, FPTAN, FPATAN, FYL2X, FYL2XP1 and F2XM1
    identically (+ - * / and FSQRT are exact). In this build every such instruction sits in one of 11 tiny RTL leaf
    routines (standard frame, Extended arguments on the stack, the result in ST0); a byte scan of the code (with a
    boundary vote and a look at the neighbours: the other 11 hits are float constants after a `ret` or bytes inside a
    call's displacement) finds no other, and no FYL2XP1 at all:
    | Routine | Address | Instruction | Arguments |
    |---|---|---|---|
    | System Exp | 0x837d84 | F2XM1 (and FSCALE) | Extended, `ret 0xc` |
    | System Cos / Sin | 0x837da4 / 0x837db4 | FCOS / FSIN | Extended, `ret 0xc` |
    | System SinCos | 0x837dc4 | FSINCOS | Extended; EAX = @Sin, EDX = @Cos |
    | System Ln | 0x837dd8 | FYL2X | Extended, `ret 0xc` |
    | System ArcTan | 0x837dec | FPATAN | Extended, `ret 0xc` |
    | ArcSin(Single) | 0x87c0a8 | FPATAN(x, FSQRT(1-x*x)) | Single, `ret 4` |
    | ArcTan2(Y, X) | 0x87c0c0 | FPATAN | X at +8, Y at +0x14, `ret 0x18` |
    | Tan (a second math unit) | 0xa8cb68 | FPTAN | Extended, `ret 0xc` |
    | SinCos, ArcTan2 (second copies, identical bytes) | 0xa8cd0c, 0xa8cdd4 | FSINCOS, FPATAN | as above |
    Game code reaches the System routines through four helpers: sub_e594e0 (Cos and ArcTan of the day of the year: the
    sun, 76 calls: dawn and dusk, `TFightingShip.IsTimeOKToLaunchAC`, the air formations' launch and darkness checks,
    the battle timer's region, the campaign), sub_adc840 (Sin and ArcSin: `FireMount`'s region,
    `IsFriendlyShipInLineOfFire`, the torpedo targets), sub_db63fc (Cos and ArcTan: `CheckSupportingDDToDetach`,
    `GetClosestEnemyShipFromAll`) and sub_eb206c (Sin and Cos, the air unit). The second unit's SinCos and ArcTan2 are
    the globe library's geodesy (distances and bearings between latitude/longitude points), and the simulation calls
    it two levels down: `TFightingShip.GetBearingToShip`, `GetTargetBearing`, `GetHitChance`, `AngleDistToPoint`,
    `TDivision.FormLineAhead`, `UpdateLineAbreastPoints`, `TAirFormation.MoveMe`, `AdjustCourseToTarget`, ... (and the
    map's projections and drawing call it millions of times a battle minute). Counted live (test battle, 48 ships): a
    battle load calls Exp 31-33, Cos 10-14k, Sin 10-13k, Ln 284-286, ArcTan 0.8-1.8k, Tan 17, SinCos(2) 4.8-8.8k,
    ArcTan2(2) 4.6-6.3k times, the same count on both PCs; 40 minutes with gunfire about SinCos(2) 73M, ArcTan2(2) 15M,
    Sin 2.1M, Cos 0.9M, ArcTan 49k (most of them the map's drawing). Nudging every result of one copy by one unit in
    the last place of a double (`RTW3MP_TEST_X87=dbl`, 2048 times a real CPU difference) changed neither the READY hash
    nor 40 minutes of hashes; by about 1e-6 (`ppm`) every load differed in the computer-controlled divisions' orders
    (`DivisionCourse`, `DivisionSpeed`): the results reach the state through roundings.
  - *What the x87 routines compute (0.4.1, measured on an AMD CPU against mpmath).* The battle's control word is 0x27F
    (53-bit precision, round to nearest). Precision control rounds FMUL/FADD/FSUB/FSQRT but not the transcendental
    instructions (64-bit significands). So the game's Exp rounds `x*log2(e)` (FLDL2E's 64-bit constant) and
    `1 + F2XM1(f)` to 53 bits: it is up to 6 ulps of a double off the true value for |x| < 10, 44 below 50, 352 below
    700 (exact with 0x37F). FSIN, FCOS, FSINCOS and FPTAN reduce the argument with a 66-bit pi (0x3243F6A8885A308D3 *
    2^-64, 4.04e-21 below pi; no other precision from 62 to 80 bits fits): cos of pi/2 as a double gives
    6.1230317691118863e-17 (true: 6.123233995736766e-17), sin(334416.25...) is 15884 ulps off, and near multiples of
    pi/2 the relative error reaches 4e13 ulps of the tiny result. From |x| >= 2^63 (and for infinities) they leave the
    argument in ST0 and set C2: Sin and Cos return the argument, Tan returns the indefinite NaN (FPTAN pushed nothing,
    the routine's `fstp st(0)` pops the argument), SinCos stores the argument as the cosine and the indefinite NaN as the
    sine. FPATAN with two NaNs returns the one with the larger significand. softmath.h reproduces all of it (to within
    1 ulp of a double on 20 million arguments per range; 388 edge cases bit-identical): MODLOG "Fix 2026-10-22".
  - *The data files a battle reads* (`RTW3MP_TEST_FILELOG`, the test battle of 1900): the load opens `Data\Gundata.dat`,
    `HorPen.dat`, `VerPen.dat`, `Torpedodata.dat`, `BasicData.dat`, `MapData.dat`, every nation's `<Nation>WarInfo.dat`,
    the map layers `WorldRTW.LYR`, `RTWShallow.LYR` and `NoGo.LYR`, the coastal batteries' designs
    `Data\IDes\<n> in Coastal Battery.sdf`, the ensigns in `Flags\*.bmp`, and the battle's save files (slot 87 on B:
    `RTWGame<n>.sac/.bcs/.off/.sta`, `MapData<n>.dat`, `DesignFiles0-8.des`). The minutes open no game file (only the
    slot's files). At its start the game reads the other `Data` tables and 384 of `Data\Graphics`' drawings.

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
- **The AI's air operations follow PlayerSide, not the AI flags** (confirmed live in
  [stage A](../MODLOG.md#testing-2026-10-17-stage-a-live-proof-v060-branch) and
  [stage B](../MODLOG.md#testing-2026-10-17-stage-b-live-proof-v060-branch): with each human side as the PlayerSide of
  its own call the computer flew only the airfields' strikes, with PlayerSide 2 both sides' carrier strikes as well).
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

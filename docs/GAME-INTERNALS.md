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
  [research](../MODLOG.md#fix-2026-10-15-the-games-ai-research-choices-reset-joined-players-research-priorities-v030)
  and [the battle investigation](../MODLOG.md#investigation-2026-10-14-tactical-battles-joined-players-vs-ai-live-lockstep-pvp-v030).
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
- **Network.** `net.cpp` uses TCP with message types `MSG_*`; payloads are `KV` key=value text or binary bundles
  (`Bundle::Pack`). The host is authoritative, and new fields must be ignorable by older clients or bump `MP_PROTOCOL`.
- **Threading.** All game calls run on the game's main thread. The network thread only posts `WM_MP_NET`; bridge
  calls go through `RunOnMainThread`. A modal game dialog runs its own message loop, so mod code keeps running under
  it: guard against re-entry.

## Tactical battles
What is known as of 2026-10-15 (0.3.0).

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
- **Today, in the mod (0.3.0):**
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
  - **Game speed is simulation state** (intervals per minute). Change it only at minute boundaries.
  - **Never free a form inside a nested message loop.** Popup menus, move/size loops and native message boxes don't
    raise `ModalLevel`. Require the outermost dispatcher depth, no capture and no menu mode.
  - **Every flag set around a game call must be restored in `__finally`.** A Delphi exception escapes C++ `catch(...)`
    under /EHsc.
  - **The game can end a battle inside a minute** (end conditions in EndOfTurnProcedures). Its close path runs
    FormClose → DoStrategicTurn, so defer the close until the minute is over.

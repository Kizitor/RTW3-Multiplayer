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

*Note 2026-10-17:* the entries of the live-battle limits branch carry v0.4.0, v0.5.0 and v0.6.0, the releases the plan
of 2026-10-16 proposed. The branch ships as one release, 0.4.0 (after the 0.3.3 hotfix), so there is no 0.5.0 or 0.6.0,
and the entries tagged v0.4.0 are only part of it. The live-battle engine levels 2 to 13 (`MP_LS_LEVEL`) are the
branch's development levels (0.4.0 plays at level 14), and docs/ARCHITECTURE.md names its features by them; its
commits titled "WIP 0.4.0", "WIP 0.5.0" and "WIP 0.6.0" are development commits of 0.4.0.

*Note 2026-10-23:* 0.5.0 and 0.6.0 later became versions of their own (each joined nation under the player's rules,
then its war effects and battles; their groups below). The entries dated 2026-10-17 that carry v0.5.0 or v0.6.0 still
belong to 0.4.0.

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
- **0.4.0** (the live-battle limits; its entries carry v0.4.0, v0.5.0 or v0.6.0, see the note above)
  - Feature:
    [live battles, phase 0 step 2: engine level, carriers, missiles, Fastest available](#feature-2026-10-16-live-battles-phase-0-step-2-engine-level-carriers-missiles-fastest-available-v040);
    [deferred actions, force orders and battle checkpoints in live battles](#feature-2026-10-17-deferred-actions-force-orders-and-battle-checkpoints-in-live-battles-v040);
    [auto-run per player in live battles](#feature-2026-10-17-auto-run-per-player-in-live-battles-v040);
    [manual torpedo salvoes in live battles](#feature-2026-10-17-manual-torpedo-salvoes-in-live-battles-v040);
    [routine air operations and land-based CAP in live battles](#feature-2026-10-17-routine-air-operations-and-land-based-cap-in-live-battles-v050);
    [air strikes in live battles](#feature-2026-10-17-air-strikes-in-live-battles-v050);
    [rejoining a live battle](#feature-2026-10-17-rejoining-a-live-battle-v050);
    [missiles in live battles](#feature-2026-10-17-missiles-in-live-battles-v060);
    [resuming a live battle after the host's game restarts](#feature-2026-10-17-resuming-a-live-battle-after-the-hosts-game-restarts-v060)
  - Fix:
    [live battles on the campaign's own date, engine level 10](#fix-2026-10-17-live-battles-on-the-campaigns-own-date-engine-level-10-v060);
    [minor live-battle fixes before release](#fix-2026-10-17-minor-live-battle-fixes-before-release-v060);
    [the branch regression's three findings, engine level 13](#fix-2026-10-17-the-branch-regressions-three-findings-engine-level-13-v060);
    [dawn and dusk DESYNCs (LightState), engine level 14](#fix-2026-10-17-dawn-and-dusk-desyncs-lightstate-engine-level-14-v060);
    [victory points added on every reload](#fix-2026-10-18-victory-points-added-on-every-reload-v040)
  - Testing:
    [stage A live proof (v0.6.0 branch)](#testing-2026-10-17-stage-a-live-proof-v060-branch);
    [stage B live proof (v0.6.0 branch)](#testing-2026-10-17-stage-b-live-proof-v060-branch);
    [regression suite for the live-battle limits, written offline (v0.6.0 branch)](#testing-2026-10-17-regression-suite-for-the-live-battle-limits-written-offline-v060-branch);
    [stage C live proof (missiles, v0.6.0 branch)](#testing-2026-10-17-stage-c-live-proof-missiles-v060-branch);
    [the live-battle limits suite, first live runs and engine level 12 (v0.6.0 branch)](#testing-2026-10-17-the-live-battle-limits-suite-first-live-runs-and-engine-level-12-v060-branch);
    [live check of the polish round and the branch regression's findings](#testing-2026-10-17-live-check-of-the-polish-round-and-the-branch-regressions-findings-dist_ll_14-dist_ll_15);
    [the 0.4.0 release candidate on dist_ll_16, and mixed versions with 0.3.3](#testing-2026-10-18-the-040-release-candidate-on-dist_ll_16-and-mixed-versions-with-033-v040);
    [the second 0.4.0 release candidate on dist_vp_2](#testing-2026-10-18-the-second-040-release-candidate-on-dist_vp_2-v040)
  - Docs:
    [0.4.0 release candidate](#docs-2026-10-18-040-release-candidate-v040);
    [final pre-release pass](#docs-2026-10-18-final-pre-release-pass-v040);
    [0.4.0 release docs](#docs-2026-10-18-040-release-docs-v040)
- **0.4.1**
  - Fix:
    [a joined player's screen showed the host's relations; an enemy's decline became a battle](#fix-2026-10-20-a-joined-players-screen-showed-the-hosts-relations-an-enemys-decline-became-a-battle-v041);
    [joined players' intelligence mirrored the host's; no trade protection warnings](#fix-2026-10-20-joined-players-intelligence-mirrored-the-hosts-no-trade-protection-warnings-v041);
    [the strike battles' DESYNC (the ship-type flags) and the software maths checked](#fix-2026-10-22-the-strike-battles-desync-the-ship-type-flags-and-the-software-maths-checked-v041)
  - Investigation:
    [live battles between two PCs](#investigation-2026-10-19-live-battles-between-two-pcs-v041);
    [live battles between two PCs, CPUs and data files](#investigation-2026-10-20-live-battles-between-two-pcs-cpus-and-data-files-v041);
    [each player's own relations: tension, wars and battles of a joined player's nation](#investigation-2026-10-20-each-players-own-relations-tension-wars-and-battles-of-a-joined-players-nation-v041)
  - Testing:
    [twopc, live battles under two-PC conditions](#testing-2026-10-20-twopc-live-battles-under-two-pc-conditions-v041);
    [the 0.4.1 release candidate dist_rc041_1](#testing-2026-10-21-the-041-release-candidate-dist_rc041_1-v041);
    [the 0.4.1 release candidate 2 dist_rc041_2](#testing-2026-10-22-the-041-release-candidate-2-dist_rc041_2-v041)
  - Docs:
    [0.4.1 release candidate](#docs-2026-10-21-041-release-candidate-v041)
- **0.5.0** (built on the branch `relations-v2` after 0.4.1: each joined nation under the player's own rules; its
  entries carry "(relations v2)" or "(0.5.0-dev)" from before the release number was decided)
  - Feature:
    [B1: a war counter for each joined nation](#feature-2026-10-20-relations-v2-b1-a-war-counter-for-each-joined-nation-relations-v2);
    [B2: the computer's rules leave a joined nation's pairs alone](#feature-2026-10-20-relations-v2-b2-the-computers-rules-leave-a-joined-nations-pairs-alone-relations-v2);
    [B3: the player's relation routines on the joined player's PC](#feature-2026-10-20-relations-v2-b3-the-players-relation-routines-on-the-joined-players-pc-relations-v2);
    [B4: peace with computer nations](#feature-2026-10-21-relations-v2-b4-peace-with-computer-nations-relations-v2);
    [B5 money: the player's economy rules for a joined nation](#feature-2026-10-22-relations-v2-b5-the-players-economy-rules-for-a-joined-nation-relations-v2);
    [B5 money, part 2: the player's remaining economy rules, and the computer's last decisions for a joined nation](#feature-2026-10-22-relations-v2-b5-money-part-2-the-players-remaining-economy-rules-and-the-computers-last-decisions-for-a-joined-nation-050-dev)
  - Merge:
    [0.4.1 into relations v2](#merge-2026-10-22-041-into-relations-v2-050-dev)
  - Fix:
    [a host restart in the month keeps the month's relation turn and peace](#fix-2026-10-22-a-host-restart-in-the-month-keeps-the-months-relation-turn-and-peace-050-dev);
    [the game's AI peace and the relations mask: VP kept, no access violation](#fix-2026-10-22-the-games-ai-peace-and-the-relations-mask-vp-kept-no-access-violation-v050)
  - Testing:
    [the 0.5.0 release candidate dist_rc050_1](#testing-2026-10-22-the-050-release-candidate-dist_rc050_1-v050);
    [the 0.5.0 release candidate 2 dist_rc050_2](#testing-2026-10-23-the-050-release-candidate-2-dist_rc050_2-v050)
- **0.6.0** (built after 0.5.0: the player's war effects and battles for joined nations; its entries carry
  "0.6.0-dev" or "0.6.0")
  - Feature:
    [B5 blockades for a joined nation](#feature-2026-10-23-b5-blockades-for-a-joined-nation-060-dev);
    [B5 war help from allies for a joined nation](#feature-2026-10-23-b5-war-help-from-allies-for-a-joined-nation-060-dev);
    [B5 war weariness for a joined nation](#feature-2026-10-23-b5-war-weariness-for-a-joined-nation-060-dev);
    [B5 building for war for a joined nation](#feature-2026-10-23-b5-building-for-war-for-a-joined-nation-060-dev);
    [B5 round 4, joined nations' war effects after the merge](#feature-2026-10-23-b5-round-4-joined-nations-war-effects-after-the-merge-060-dev);
    [B6, the player's battle generator for a joined nation](#feature-2026-10-23-b6-the-players-battle-generator-for-a-joined-nation-060)
  - Fix:
    [risk 4, a joined player's declined battle is merged and the game's result box stays off the host](#fix-2026-10-23-risk-4-a-joined-players-declined-battle-is-merged-and-the-games-result-box-stays-off-the-host-060);
    [B6 round 3: the month's own state in a joined player's battle slot, raider battles assessed](#fix-2026-10-23-b6-round-3-the-months-own-state-in-a-joined-players-battle-slot-raider-battles-assessed-v060-dev);
    [the disarmament treaty and a joined nation's war: no box every month, the host ends it once](#fix-2026-10-23-the-disarmament-treaty-and-a-joined-nations-war-no-box-every-month-the-host-ends-it-once-v060-dev);
    [the joined player's SaveBuildCampaign access violation](#fix-2026-10-23-the-joined-players-savebuildcampaign-access-violation-060)
  - Testing:
    [B5 war loans of a joined nation at war, confirmed live](#testing-2026-10-23-b5-war-loans-of-a-joined-nation-at-war-confirmed-live-060-dev);
    [the 0.6.0 release candidate dist_rc060_1](#testing-2026-10-23-the-060-release-candidate-dist_rc060_1-v060);
    [the 0.6.0 release candidate 2 dist_rc060_2](#testing-2026-10-23-the-060-release-candidate-2-dist_rc060_2-v060)
  - Investigation:
    [B5 round 2: the allies' war help outcomes (static) and the enemy-revolution rule (design)](#investigation-2026-10-23-b5-round-2-the-allies-war-help-outcomes-static-and-the-enemy-revolution-rule-design-060-dev);
    [B5 round 3: the enemy-revolution rule for joined nations](#investigation-2026-10-23-b5-round-3-the-enemy-revolution-rule-for-joined-nations-060-dev);
    [B6 decisions (attempts, decline counts, generator errors, wording)](#investigation-2026-10-23-b6-decisions-attempts-decline-counts-generator-errors-wording-v060-dev);
    [surprise attacks and takeovers at war start for joined nations](#investigation-2026-10-23-surprise-attacks-and-takeovers-at-war-start-for-joined-nations-v060-dev)
- **0.6.1** (fixes of bugs reported on 0.6.0; its entries carry "0.6.1-dev" or "0.6.1")
  - Fix:
    [the tension bar above the war line between two players: its hint and a chat line say why no war comes](#fix-2026-10-25-the-tension-bar-above-the-war-line-between-two-players-its-hint-and-a-chat-line-say-why-no-war-comes-v061);
    [Fire torpedoes greyed out in live battles: the battle's Realism in both players' chat](#fix-2026-10-25-fire-torpedoes-greyed-out-in-live-battles-the-battles-realism-in-both-players-chat-v061-dev);
    [the ship designer's temporary file no longer stops live battles](#fix-2026-10-25-the-ship-designers-temporary-file-no-longer-stops-live-battles-v061);
    [a reserved nation is played by a person on the host too](#fix-2026-10-25-a-reserved-nation-is-played-by-a-person-on-the-host-too-v061);
    [campaign divisions of joined players and duplicated divisions](#fix-2026-10-25-campaign-divisions-of-joined-players-and-duplicated-divisions-v061);
    [battles the game resolves for joined players: no battle when a side has no ship there that can fight](#fix-2026-10-26-battles-the-game-resolves-for-joined-players-no-battle-when-a-side-has-no-ship-there-that-can-fight-v061)
  - Testing:
    [campaign divisions, the live proof and the scenario divisions](#testing-2026-10-25-campaign-divisions-the-live-proof-and-the-scenario-divisions-v061);
    [the 0.6.1 release candidate dist_rc061_1](#testing-2026-10-26-the-061-release-candidate-dist_rc061_1-v061);
    [the 0.6.1 release candidate 2 dist_rc061_2](#testing-2026-10-26-the-061-release-candidate-2-dist_rc061_2-v061)
- **0.7.0** (the legacy fleet, raider battles and invasions for joined players; its entries carry "0.7.0-dev" or
  "0.7.0")
  - Feature:
    [the legacy fleet for joined players](#feature-2026-10-25-the-legacy-fleet-for-joined-players-v070-dev);
    [raider battles for joined nations](#feature-2026-10-26-raider-battles-for-joined-nations-v070-dev);
    [invasions for joined nations](#feature-2026-10-26-invasions-for-joined-nations-v070-dev)
  - Fix:
    [the legacy fleet with 0.6.1's campaign divisions: a hand-built fleet starts without divisions](#fix-2026-10-25-the-legacy-fleet-with-061s-campaign-divisions-a-hand-built-fleet-starts-without-divisions-v070-dev);
    [the game's box before the preview of a battle after an intelligence coup; old turn messages in a new campaign](#fix-2026-10-26-the-games-box-before-the-preview-of-a-battle-after-an-intelligence-coup-old-turn-messages-in-a-new-campaign-v070);
    [the division stance routine's box after a live battle](#fix-2026-10-26-the-division-stance-routines-box-after-a-live-battle-v070)
  - Testing:
    [the 0.7.0 release candidate dist_rc070_1](#testing-2026-10-26-the-070-release-candidate-dist_rc070_1-v070);
    [the 0.7.0 release candidate 2 dist_rc070_2](#testing-2026-10-26-the-070-release-candidate-2-dist_rc070_2-v070)
- **0.7.1** (fixes of bugs reported on 0.7.0; its entries carry "0.7.1-dev" or "0.7.1")
  - Fix:
    [the bare "Access violation ... Read of address 00000109" box: a campaign save cut short](#fix-2026-10-28-the-bare-access-violation--read-of-address-00000109-box-a-campaign-save-cut-short-v071-dev);
    [a joined player's trade protection and raiders, and View data of the player's own ships](#fix-2026-10-28-a-joined-players-trade-protection-and-raiders-and-view-data-of-the-players-own-ships-v071-dev);
    [a full disk: a failed save stops the month, the low-disk notice, and what grows over a long session](#fix-2026-10-28-a-full-disk-a-failed-save-stops-the-month-the-low-disk-notice-and-what-grows-over-a-long-session-v071-dev);
    [a failed save is not always a full disk: a file in use is saved again by itself](#fix-2026-10-28-a-failed-save-is-not-always-a-full-disk-a-file-in-use-is-saved-again-by-itself-v071-dev);
    [the battle setup's "Error in SelectForce procedure!" box: a side without ships that can fight and its ally's division](#fix-2026-10-28-the-battle-setups-error-in-selectforce-procedure-box-a-side-without-ships-that-can-fight-and-its-allys-division-v071)
  - Testing:
    [the 0.7.1 release candidate dist_rc071_1](#testing-2026-10-28-the-071-release-candidate-dist_rc071_1-v071);
    [the 0.7.1 release candidate 2 dist_rc071_2](#testing-2026-10-29-the-071-release-candidate-2-dist_rc071_2-v071)

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

## Feature 2026-10-16: live battles phase 0, step 2: engine level, carriers, missiles, Fastest available (v0.4.0)
The approved plan's phase 0 step 2 (session folder: `live-battle-limits-plan.md`, research `limits-air.md` §A/D/E,
`limits-weapons.md` §3/6/8, `limits-orders.md` §7). Built on 0.3.2 (main d042d71: blocked toggles put back,
`LockMissilePanel`). Static work: every address below was read with `rdis.py` and the runtime patterns were checked
against the memory image (`verify_p0b.py` in the offline workspace); nothing here has run in the game yet.
- **Engine level.** `MP_LS_LEVEL 2` next to `MP_PROTOCOL` (still 3); `MP_MOD_VERSION` 0.4.0.
  - Hello `ls=<level>` (0.3.x sends `ls=1`). The host keeps each player's `lsLevel` and `mod` (log `<name>: mod
    <ver>, live-battle engine <n> (this host: 2)`).
  - Pairing: the host's own battle only if the player's level is the host's (`HostOnBattleStarted`); a relayed battle
    only if both players' levels are the host's (`HostDeferPvpBattle`). Otherwise the old fallback (the host fights the
    AI / the game resolves the battle), a host log and chat line, and a notice (`MSG_CHAT notice=1`) for the player(s):
    "live battles need the same mod version".
  - Defence in depth for hosts that don't check (a 0.3.x host pairs any `ls != 0`): A's `start` carries `lv=`; B
    refuses another level through the existing refusal (`abort`) with "the live battle engines differ (level 1 there,
    2 here): live battles need the same mod version", plus a chat line and a notice. B's `ready` carries `lv=` too; A
    drops a B without it (0.3.x) or with another level (`DropPeer` with the same reason: side 1 to the AI).
- **Simulation PlayerSide.** `g_simPlayerSide` (0) replaces the literal 0 in `ApplyViewSide`, so `CanonBegin/End` and
  every display swap back into the simulation restore it. `CallWithSimSide(side, fn, ...)` sets it (and PlayerSide
  itself unless this PC's display set is in place) for one register-convention game call and restores both in
  `__finally`. Before, any nested hooked frame reset PlayerSide to 0 at its exit.
- **Carriers (bug B1).** `CheckAIAirstrike(Self, Side)` (0xdfd50c, RVA 0x5cd50c) is resolved from StartMinute's
  `33 D2 8B C3 E8 <rel> BA 01 00 00 00 8B C3 E8 <rel>` (0xdab726; both targets equal) and checked against its error
  message literal (0xdfd834). Hooked with a register detour (`LS_dAirStrike`): in a live battle's minute it runs with
  simulation PlayerSide `AirstrikeSimSide(side)` = 2 for both calls, so the computer flies both sides' carrier
  strikes (a policy function: phase 3b returns `side` for a side with manual strikes). PlayerSide reads under it:
  0xdfd65c (a Random(100)<10 target search for non-player sides), the picker 0xdfdb36, `sub_dfd880` 0xdfd92c/0xdfd936
  (which itself treats 2 as "no human side").
  - After each call every formation of sides 0 and 1 gets `ConfirmTargetLoc` (+0xf1) cleared: `AILaunchStrike` sets
    it on every strike (0xdfe251), and it would make EOTP's `sub_eab89c` ask PlayerSide's player mid-minute and
    `ConfirmTargetLocForAirFormation` (0xe005b9: `Side = 1` hard-coded) retarget side 1's and land-based strikes.
    Strikes now fly to their first target point on both sides.
  - The formations made in the call (AFId above the highest before it) are counted per side for `ls airstat`, with a
    log line per call that made any. `CheckAISurpriseAirstrike` (0xdff61c, StartMinute at minutes 0 and 11; side 1
    hard-coded) is unchanged; its strikes lose the confirmation too.
  - `SetAIAirSearchPlan(Include)` (named) is hooked: in a live battle's minute with Include = False it is skipped while
    both sides are human (log `... re-plan skipped`), runs with the human side as PlayerSide when exactly one is, and
    passes through otherwise. Include = True (battle generation) always passes through.
- **Missiles (bug B2).** `CheckMissileFiringForAllShips(Self)` (0xda91fc) from EOTP's `A1 <year cell> 81 38 94 07 00 00
  7E 08 8B 45 FC E8` (0xdaf90f; checked against its error message literal 0xda95d0) runs with simulation PlayerSide
  `MissileSimSide()` = 2 in a live battle: the Fire-at-will gate (0xda930d) passes every division and
  `FireSSMAtTarget`'s doctrine (0xda7d13/0xda7d49) is the computer's for both sides, so the panel no longer matters.
  `LockMissilePanel` (0.3.2) keeps disabling the two controls after every load and at every boundary.
  - Side effect found while checking: `FireMissileMount` (0xe047f1) with PlayerSide 2 logs `<ship> is launching
    <missile> at enemy ships!` into `MessageLog` (real names, both sides) instead of the once-per-battle `Enemy
    targeting radar is locking on to <ship>` message that also set `FirstSighting[side]` (+0x800, read by
    `GetBasicNightSightingRange` and EOTP) and `PauseGame` (+0x7e8). Deterministic on both PCs; to be judged in the
    live proof (a narrower scope, PlayerSide 0 inside `FireMissileMount` only, would keep the old message).
- **Missile panel (bug B5).** `UpdateMissilePanel(Self)` (0xe032c8; EOTP's 0x796 year call at 0xdaf8ea; checked by its
  `stEnemyMissileTargets`/`pnMissiles` loads) is a `K_UI` context (no stack arguments: pop 0; no `Random` in it or its
  callees). It counts PlayerSide's ships but takes **side 1** as the enemy (0xe03376), so on the PC that shows side 1
  it would count that side's own ships as targets: a gate (`LS_dMissilePanel`) skips it there; on side 0's PC it runs
  with that PC's display set.
- **Fastest available (bug B3, clock state).** `B_FASTEST` (in main's `LS_BlockImpl`) puts the button back as both
  PCs have it (`S.fastest`: up unless the clock turned it on), then requests `fastest <the clicked state>`. A records
  it (`<who> turned Fastest available on/off (from the next minute).`, with speed 5/2 as the button's click sets);
  releases, starts and resyncs carry `fastest=0|1` (the `.sac` doesn't keep it), and `info` too. Both PCs apply it
  inside the shared section before the minute: `SetDown` (main's `E.setDown`) + the `ImageIndex` setter (0xabc2b0,
  resolved as the release routine's second call and checked against `sbFastestAvailableClick`'s `mov edx, 1; call`),
  then the speed. A speed request (box or arrows) turns it off on both, as the game's speed box does.
  `cbFastestAvailableClick` stays blocked. Without the two routines the button falls back to 0.3.2 (kept up).
- **Tests:** `ls level`, `ls simside`, `ls airstat`, `ls fastest [0|1]` (docs/TESTING.md); `ls status` has `fastest=`,
  `ls session` `level=` and `levels=`.

## Feature 2026-10-17: deferred actions, force orders and battle checkpoints in live battles (v0.4.0)
The approved plan's phase 1 (session folder: `live-battle-limits-plan.md` building block 1 and rows 6 and 9a, research
`limits-orders.md` §2/5/6). Static work on top of phase 0 step 2: every address was read with `rdis.py`, the runtime
patterns were checked against the memory image (`verify_p1.py` in the offline workspace, all OK) and `randtree.py`
found no `Random` in any of the replayed routines; nothing here has run in the game yet. Engine level `MP_LS_LEVEL 3`.
- **Deferred actions** (`lockstep.cpp`, new section before `ApplyBatches`). A batch item `D<kind>\x1f<minute>\x1f<key>
  ...` (kinds `chforce <division> <force>`, `lead <force> <division>`, `turnaway <force> <division>`; keys are the order
  guard's `kind+name#ordinal`). `QueueDeferred` runs only from the screen of a running live battle (`CanOrderNow`:
  no simulation frame, no shared section, not inside a release), calls `DetectEdits` first (the player's earlier
  edits get the earlier batch number), stamps the boundary the item is meant for (the minute counter between minutes,
  the last boundary + 1 while a minute runs), sends it as its own batch and says `<what> ordered; it takes effect when
  the next minute starts.` The game routine doesn't run on the PC where the order is given.
- **The release order** (`ApplyBatches(seq0, seq1, relN)`, inside the release's shared section, the same on both PCs):
  (1) the other PC's immediate actions (`A` items, replayed as before); (2) side 0's batches in sequence order, each
  item in the order it was given: `V` values and `D` deferred actions (`RunDeferred`, both PCs, the actor's too); (3)
  side 1's likewise. For `V`/`A` items alone this is the 0.3.x order. Only `A` items count for "both players acted"
  (resync) and for B's "own action missed the release" (resync); a `D` item that misses a release simply rides the next
  one. `DfKind.thisBoundaryOnly` (none yet; torpedo salvoes later) drops an item released at another boundary than its
  stamp, on both PCs, with a chat line for its player only.
- **Replays** (`RunDeferred`). The game's preconditions are checked again first, deterministically, from the agreed
  state: the keys resolve; chforce: the division is in its force's list, the target is another force of its side with
  ships; lead/turnaway: the division is in the force, the force is not computer-controlled, `HasChangedLead` = 0
  (lead) / `TimeToTurnAway` <= 0 (turnaway), the division has ships, the force has a lead division
  (`GetDivisionOfShip(GetForceFlagship)`, which the routines would dereference) other than it, and for a turn-away
  the `LeadDivision` chain ahead of it ends and every division in it has ships. A failed check skips the action on
  both PCs: a log line on both, `Not carried out: <what> (<reason>).` in the chat of its player only.
  - chforce is applied mod-side exactly as `Changeforce2Click`: `old.Divisions.Delete(old.Divisions.IndexOf(div))`,
    `new.Divisions.Add(div)` (the game's `TList` methods), `myForce := new`, `Admin -= 1`; then the ship tree of the
    PC whose side it is is refilled as the handler does (`FillShipTree(tvShips, PlayerSide, False, nil)`, after the
    shared section).
  - lead/turnaway call `SetForceLeadDiv` / `DoBattleTurnAway` (`CallForceRoutine`) with `SelectedForce` set to the
    recorded force; `SelectedForce`, `SelectedDivision`, `SelectingNewLead`, `SelectingLeadForBattleTurnAway` are put
    back in `__finally`, and a Force orders / Select lead dialog the routine closed is shown again after the shared
    section (`RunAfterRelease`).
  - Quiet replays: `g_dfActor` (the giving side) is set around the routine; `LS_MsgDlg` / `LS_ShowPic` answer the
    game's boxes as in the simulation, and only the giving player's PC puts the text in its chat (`Battle: Force lead
    division changed! ...`); the other PC logs `deferred action's box '...' answered 1 (quiet)` and doesn't touch
    `g_lastBox` (which feeds A's "The battle paused (...)" line for both players).
- **Hooks** (`LS_BlockImpl`): `Changeforce2Click` (`B_CHFORCE`: the game's own question with the handler's literal,
  dialog type and buttons read from its code, keys taken before the modal question and resolved again after it, then
  `QueueChangeForce`); `TdlgForceOrders.sbChangeLeadClick` / `sbTurnAwayClick` (`B_LEADBTN`: the game's handlers run;
  they only set a selection flag and fill and show `dlgSelectLead`); `TdlgSelectLead.bnOKClick` (`B_SELECTLEAD`: the
  chosen division as the handler reads it, the force from `SelectedForce`, `QueueForceLead` with the buttons'
  conditions plus "not ordered already for this force"; on success the flag is cleared and both dialogs close as after
  the game's routine; a refusal goes to the chat and leaves the dialog open); `sbSaveClick` (`B_SAVE`: a clock request
  `save`). Resolution (`ResolveForceOrders`): `TList.IndexOf/Delete/Add`, `SetForceLeadDiv`, `DoBattleTurnAway`,
  `TForce.GetForceFlagship`, `TCustomForm.Show` and the fields by name; `MessageDlg` = the start of the
  MessageDlgPosHelp-caller pattern; the call targets of `Changeforce2Click`, `bnOKClick` and both routines and the
  `SelectedForce` reads are checked. A missing piece turns that feature off (its click handlers keep the "not
  available" chat line); a failed hook on `Changeforce2Click` / `bnOKClick` does the same instead of failing the engine.
- **Dropped orders.** `ClearOrders(dropWhy)`: a resync (A's `StartTransfer`, B's `BOnStart`) and the end of the live
  battle (`Cleanup`: end, drop, leave) tell the player which deferred actions were not carried out (`Not carried out
  (the battle was synchronised again): <what>. Give the order again.`).
- **State hash and leak check.** A ninth group `force`: per force (forces list order) its name, side, its divisions'
  names in list order, HasChangedLead, TimeToTurnAway, ForceWithdraw, AIControlled, Rallied, FlotillaAttack; per
  division myForce, LeadDivision, Role, ForceLeadDivision, Admin. Not `ForceFlagship` (display code rewrites the
  cache). `OnBoundary` now always puts the agreed values back before hashing (before: only with pending own orders), so
  a display-side `GetForceFlagship` that rewrites `ForceLeadDivision` of the other side's divisions between the minute's
  end and the hash can't raise a false DESYNC (the guard already restores it before every step). The leak check adds
  the forces' division lists (membership and order) and each division's myForce and Admin (none of them is written by
  display code: `Admin`'s only battle writers are `Changeforce2Click` and `TDivision.Create`). Trace dumps list forces.
- **Save** (plan row 9a). `AHandleRequest("save")`: in the host's own battle (`StartInfo.campaignSlot`, set by
  `HostTryStartLockstep`) A remembers it and `ATryCheckpoint` (first thing in `ATryRelease`) saves at the next boundary
  without a running minute, modal dialog or hooked frame: `DetectEdits`, `RestoreAgreed`, `SaveSnapshot` (the start's
  two save calls into the host's campaign slot, simulation set), `ReapplyOverlay`; nothing is sent or loaded and the
  pending orders stay pending. Chat for both: "Battle saved (minute N) in the host's campaign slot: if the host's game
  stops, loading that slot finishes the battle against the computer." In a battle between two joined players (A's
  temporary battle slot): "<who> pressed Save: the host keeps battles between two players, so nothing is saved."
- **Reload safety.** Verified statically that the game's own resume hands side 1 to the computer: `LoadScenarioInProgress`
  sets PlayerSide 0 (0xcd02e7), `SetupGlobeRTWLayers` sets the side table's side-1 AI flag for a BC scenario
  (0xdc3694), and `SetDivisionAIControl(Resumed)` (0xda9cd8, called at 0xdc3b56) makes every side-1 force and division
  AIControlled and clears its ships' `ForceIsHumanControlled` (0xda9d7f, 0xda9e87, 0xda9ec8). A vanilla save has side 1
  under the computer anyway (the same path, plus the battle setup's 0xdc3606). `lockstep::OnCampaignLoaded` (from
  game.cpp's `CB_AfterLoad` through `hookcb::OnAfterCampaignLoad`) logs the resumed battle and calls
  `SetSideControl(1, true, false)` only if side-1 ships are still flagged human afterwards, so it is a no-op for every
  1.01.44 save; it skips the lockstep's own loads and a running live battle.
- **Found while checking that path: a resync gave side 1 to the computer.** The same resume runs in every lockstep
  load. The first load fixes it (`SetSideControl(1, false, true)`), but the resync branch only rewrote the ships' human
  flags, so after any resync all of side 1's forces and divisions were AIControlled (both PCs alike: no desync, but the
  computer commanded the second player's ships). Now A's start message carries `ai1=<forces>/<divisions>` (side 1's
  AIControlled flags in list order, from the agreed state before the snapshot), and both PCs put them back in the
  setup's shared section after a resync load (`ApplySideAiFlags`, log `side 1's computer control as before the load:
  ...`; if the lists differ, side 1 becomes human as on the first load).
- **Tests:** `ls forces [div]`, `ls chforce`, `ls lead`, `ls turnaway`, `ls pending`, `ls save` (docs/TESTING.md).
- **Open for the live proof:** the menu `Changeforce1` is only visible with the nation's battle tech flag `+0xdc` and
  the turn-away button with `+0x80` (guesses about what they mean; the bridge commands don't need them); whether the
  1900 fixture has two forces per side; that the replays' globe redraw and dialog `Close`/`Show` inside or after the
  shared section draw nothing from the shared stream; the hash's new group over long stress runs.

## Feature 2026-10-17: auto-run per player in live battles (v0.4.0)
The approved plan's phase 1b (session folder: `live-battle-limits-plan.md` row 8). Built on phase 1 and on main's 0.3.2
hotfix 3cc0628 (merged in 5c4d734: the game's battle teardown before every live reload; the ship-tree side rewrite now
compares the side with 0 or `g_simPlayerSide`). Static work: every address below was read with `rdis.py`, the runtime
patterns were checked against the memory image (`verify_p1b.py` in the offline workspace, all OK) and `randtree.py`
found no `Random` in `GetForceFlagDivision`, `ExecuteGeneral` or `ClearHints`; nothing here has run in the game yet.
Engine level `MP_LS_LEVEL 4`. The facts are in GAME-INTERNALS ("Auto-run and the computer's command").
- **What `AutoRunBattle` (TfrmMain +0x7e9) gates.** Every `[reg+0x7e9]` access of 1.01.44 (`fieldref.py 7e9`; the
  RTTI names of the hits are misleading: 0xd90432.. are in the EOTP message routine 0xd8fd5c, not `GlobeMouseMove`).
  - Simulation: EOTP's air-attack picture messages draw `Random(10)` per pending message only while it is off
    (0xdaf979), so it must be the same on both PCs; the message routine 0xd8fd5c/0xd91182 (EOTP 0xdafb03) shows
    message boxes and returns the break that sets `PauseGame`; the computer's paths of `CheckSupportingDDToDetach`
    (0xdd0783, 0xdd0b39, `Random(100)`), `CheckPortArrival` (0xdec908), the human forces' sighting messages (0xdca997,
    0xdcabc4), `CheckAllForcesForHumanFlagDivs` (0xdf5aca), PlayerSide's scout and floatplane questions
    (`ShipLaunchSearchAircraft` 0xe48cb0, `LaunchFloatCheck` 0xe490d2), `DoDamageControl`'s flooding message
    (0xe34e73). The first four also take the computer's path for a force with `TForce.AIControlled`.
  - Clock and UI: EOTP's end (0xdb04bd), `tmrRunMinuteTimer` (0xdb22db) and the autosave (0xdf0223) keep the run going
    over a break; `FormKeyUp` (0xdae520, 0xdae5b8) lets Alt and space call `StartRun(MaxInt)` or set `MinutesLeft` := 0
    and stop `tmrRunMinute` directly, i.e. one PC's keys would change the game's run state in the middle of a minute.
  - The end conditions (EOTP 0xdafd61..0xdb0336) don't read it: in single player the end questions appear anyway.
- **The handler's force orders.** "...move to closest friendly base" writes `ForceWithdraw` (+0x75, the force's
  withdrawal level, "Force fatigue"; the Force orders' Disengage sets the same 6) and `ForceMission` (+0x1c,
  `TForceMission`: 2 = tfmWithdraw) for PlayerSide's forces with `TForce.AIControlled` (+0x79) = 0 and for those whose
  name contains 'Support Force' (0xe028b4..0xe028eb). `ForceWithdraw` and `AIControlled` are in the order guard
  (`g_oForce`) and the hash; `ForceMission` is in neither (no dialog writes it; `CheckPortArrival` 0xdecab8 sets it in
  the simulation) and now joins the hash's `force=` group. The battle save keeps all three (0xdd1625..0xdd1716).
- **The UI pieces.** 0xe027c0 is a nested procedure of the handler that sets `Enabled` of `sbPlay` (+0x4cc), `sbRun5`
  (+0x4d0), `sbRun` (+0x4d4) and `sbPause` (+0x4e4); +0x610 is `sbAutoRunBattle` itself (GroupIndex 97, AllowAllUp: its
  Down state shows auto-run), +0x650 `sbFastestAvailable`. `FormShow` (0xde24dc) resets the flag and the button.
- **The AI gates.** The division AI runs for `TDivision.AIControlled` (0xd955b8 → `AIDetermineTacticalStance2`
  0xdc108c → 0xdb726c / 0xdba690 / 0xdbd104 / 0xdc0010 → 0xd968e4; `CheckAllDivAfterMove` 0xdb0bb4); force decisions
  read `TForce.AIControlled` (flotilla attacks in `CheckForceWithdrawal` 0xdaf1a1 and the paths above); ships read
  `ForceIsHumanControlled` (`TForce.CheckSearchAircraft` 0xe5a334, `DoDamageControl`, `FireGuns`); and PlayerSide
  tests treat the player's side differently: three stance routines skip "FORCE WITHDRAW" for AI-controlled PlayerSide
  divisions (0xdb7a3d, 0xdbaf03, 0xdc02dd), `CheckAllDivAfterMove` re-roles carrier divisions only off PlayerSide
  (0xdb193a), PlayerSide's ships ask before launching scouts (0xe48c78). The game's auto-run sets only the division
  flags and `AutoRunBattle`; inside a live battle's simulation PlayerSide is 0, so **side 0 on the game's auto-run
  would keep the player's rules there (the review's finding), side 1 never does** (it is never the simulation's
  PlayerSide). PlayerSide 2 is the game's own value for "no human side" (`SetupGlobeRTWLayers` 0xdc370c/0xdc3a57); the
  simulation's PlayerSide-indexed accesses are reads (0xdafb69, 0xde1e05).
- **Design (decisions from the findings).**
  - A side on auto-run is commanded like the computer's own side: the handler's steps for that side only
    (`GetForceFlagDivision` of each force with one and its flag division's `AIControlled`; "home" first gives the
    handler's orders, with its conditions read before anything is changed), then `SetSideControl(side, ai, !ai)` as for
    the AI takeover: every force and division `AIControlled`, its ships `ForceIsHumanControlled` 0 inside the
    simulation and `DivisionIsAIControlled` 1, `g_human[side]` false (so the 2-hourly air search re-plan treats it as
    the computer's). While side 0 is on auto-run the simulation's PlayerSide (`g_simPlayerSide`) is 2, for every
    simulation routine; side 1 needs nothing. "off": the handler's own off for that side (flag divisions), then
    `SetSideControl(side, false, true)`: every force and division back (`AIControlled` 0), the orders stay.
  - `AutoRunBattle` stays off on both PCs: the per-side flags cover its simulation effects, it isn't needed for the
    end (the dialog policy answers the end question), and leaving it off keeps the `FormKeyUp` keys away from the run
    timer. Its remaining effects (fewer message boxes and breaks, the air-attack messages' draw) stay as in 0.3.x.
  - Both sides on auto-run: A's clock runs continuously (`runLeft` INT_MAX/2) with Fastest available (speed 5; without
    the button's routines speed 5 alone), OnBoundary doesn't stop it for a break, and `LS_MsgDlg` answers "... Do you
    want to end the scenario?" with Yes (both PCs: the auto-run state changes only at releases), so the game's own end
    conditions end the battle. "Do you want to continue searching?" keeps today's Yes (the game's absolute time limit
    ends such a battle; the players can still agree to end). An "off" of either player stops the run after that minute
    and puts A's clock (Fastest available, speed) back.
  - The snapshot is also the host's crash checkpoint: `SaveSnapshot` writes the sides on auto-run under their players
    (forces' and divisions' `AIControlled` 0, PlayerSide 0, restored right after the save), so loading the host's slot
    outside a live battle gives the host their ships back; every lockstep load applies the auto-run state again.
- **Implementation** (`lockstep.cpp`).
  - `ResolveAutoRun` (after `ResolveForceOrders`): from `sbAutoRunBattleClick` the dialog cell (`A1 <cell> 8B 00 E8
    <ClearHints>`), the home orders (`80 7F <TForce.AIControlled> 00 75 0B C7 47 <ForceWithdraw> <6> C6 47
    <ForceMission> <2>`, offsets checked against RTTI, values read), the 'Support Force' literal (`B9 01 00 00 00 8B D0
    B8 <literal> E8`), `C6 40 <TDivision.AIControlled> 01`, and its calls of `GetForceFlagDivision` and
    `ExecuteGeneral`; the button field by name. Without all of them `g_featAutoRun` is off and the button keeps the
    0.3.x chat line.
  - `B_AUTORUN` (`LS_BlockImpl`): `CanOrderNow`, then `AutoRunClick`: the game's `ClearHints` and `ExecuteGeneral`
    (the hooked entry: `LS_ExecGeneral` passes it through between minutes) with "Hand your ships to the computer for the
    rest of the battle? <the other player> keeps commanding theirs." (or "...'s ships are on auto-run too: the battle
    then runs to its end at the fastest speed.") and Yes / Yes, and order all forces to move to the closest friendly
    base / No, or "Take back command of your ships?" Yes / No while this side is on auto-run (`EffectiveAuto` counts
    this PC's pending autorun orders); then `QueueAutoRun`. `LS_Block` puts the button back as before; the release sets
    it (`SetAutoButton`).
  - Deferred kind `autorun <side> on|home|off` (`kDfKinds`, any release): `RunAutoRun` (from `RunDeferred`, both PCs,
    in the release's shared section) checks that the side is the giver's, then `AutoOn`/`AutoOff`, `S.autoRun[side]`,
    `SetSimSideFromAuto` (PlayerSide written unless this PC's display set is in place), log, the giver's chat and button
    and the other player's chat after the shared section. `ApplyRelease` compares `BothAuto()` before and after:
    `AutoClockChanged` (A) starts or ends the continuous run.
  - Session state `S.autoRun[2]`: the start message carries `auto=<a0><a1>` (B: `S.startAuto`), `LoadSnapshot` sets it
    first in the setup's shared section, derives `g_human` from it (and `peerAI`), restores side 1's flags from `ai1=`
    as before, then hands every side on auto-run to the computer again and sets the simulation's PlayerSide; after the
    load the button shows this PC's side. `info` carries `a0`/`a1` (B: `S.clockAuto`, display only). `ResetSimSides`
    (PlayerSide 0, both human) at `Hold`, at B's new session and in `Cleanup`; `Cleanup` on A also gives side 0 back to
    its player if it was on auto-run (a drop, an abort, a failed load) and says so unless the battle is over.
  - Bridge: `ls autorun [on|home|off]`, `ls status` `auto0=`/`auto1=`, `ls forces` `:withdraw=`/`:mission=`
    (docs/TESTING.md).
- **Open for the live proof:** that the AI commands an auto-run side 0 as strongly as side 1 (stances, scouts, flotilla
  attacks, survivors) and that PlayerSide 2 for the whole simulation changes nothing else for the human side 1 (the
  shared message log then names both sides' ships: a fog-of-war effect while side 0 watches); "home" moving the forces
  to their base; both sides on auto-run reaching the game's end (and how long a no-contact battle runs to its absolute
  limit); a resync and a drop while on auto-run; the hash's `ForceMission` over long runs; that the event dialog's
  caption ("Event - <month>") reads well.

## Feature 2026-10-17: manual torpedo salvoes in live battles (v0.4.0)
The approved plan's phase 2 (session folder: `live-battle-limits-plan.md` row 4; research `limits-weapons.md` §1, §7).
Built on phase 1's deferred actions and phase 1b. Static work: every address below was read with `rdis.py`, the runtime
patterns were checked against the memory image (`verify_p2.py` in the offline workspace, all OK) and `randtree.py` found
no `Random` outside `FireMount` itself, `SetReloadTime` and the display routine `GetTypeAndNameForEnemy`; nothing here
has run in the game yet. Engine level `MP_LS_LEVEL 5`. The facts are in GAME-INTERNALS ("Manual torpedo salvoes").
- **What the game does.** Division report → *Fire torpedoes* (`TdlgDivisionReport.bnTorpsClick` 0xae1594: the game's
  "Ship has fired mount this turn!" check, `SelectedShip`, `FillShipTargetList(ship, lbTargets, False, True)`, shows the
  modeless `TdlgTorpTargets`) → a target (`lbTargetsClick`: the firing solutions 0xadd39c, about 15 local draws) → a
  mount line ("HIGH/LOW <course>" lines carry the mount object; `lbMountsClick` enables Fire) → *Fire*
  (`TdlgTorpTargets.bnFireClick` 0xadda28: speed/range from the ship's HIGH or LOW torpedo settings, the course from the
  line's last 3 characters jittered by up to 8 local `Random(100)` draws, `FireMount(mount, ship, target,
  Single(course), speed, range, True)` at 0xaddf75, then `FiredMountThisTurn := 1` at 0xaddf7d). `TfrmMain.FireMount`
  (0xda8cc4, `ret 0x14`) reads no UI control and no PlayerSide; its only other callers are the AI's fire routine in EOTP
  (0xda8ac6, 0xda8cb6, `Manual` = 0). The *Fire torpedoes* button is enabled by `TdlgDivisionReport.FormShow` only with
  the battle preference Realism above its second level (prefs +0x56 = `TdlgPrefs.rgRealism`; in kPrefs, so B uses A's
  value).
- **The early returns are state-free.** `TorpsLeft` = 0 → return (0xda8cf2); `IsSubmerged` (a pure `Position` test) and
  `CurrentSpeed` > 25 and `ShipType` <> 13 (MTB) → with `Manual` `ShowMessagePos('Submerged tubes not useable at speeds
  above 25 knots!')` (0xda8d36, through `MessageDlgPosHelp`: the dialog policy shows it outside the simulation), then
  return. No `Random`, no write before them; the exit only frees its local strings.
- **The hook** (`LS_dFireMount`, a naked detour: EAX/EDX/ECX and the five stack arguments to `LS_FireMount`; 1 = `ret
  0x14` without the routine, 0 = `jmp` to the trampoline). Inside the simulation (the AI's fire, the release), during a
  load or outside a live battle it passes through. Otherwise (`TorpOrder`): the routine's own early returns, checked
  with its own fields and constants (read from its code: speed limit 25, ship type 13), let the routine run on this PC
  (the game's message, nothing else); else feature, ship/mount/target classes, `CanOrderNow`, the ship on this PC's
  side, keys (`S<name>#n` for ship and target, the mount's index in `Tship.TorpedomountList`), "not ordered already for
  this ship and mount" (`PendingDeferred`), then `QueueDeferred("torp", {ship, mount index, target, angle bits as 8 hex
  digits, speed, range, manual})` with the chat "Torpedo salvo ordered; it fires when the next minute starts."
  (`QueueDeferred` got an optional chat text). A refusal goes to the chat; an exception refuses (logged). The dialog's
  own `FiredMountThisTurn := 1` still runs on the giving PC: a UI gate only (`bnTorpsClick` reads it, `DoDamageControl`
  clears it), in neither the hash nor the battle save.
- **The release** (`RunDeferred`, kind `torp`, `DfKind.thisBoundaryOnly` = true: released at another boundary than its
  stamp it is dropped on both PCs, "it was meant for minute N and arrived too late"). Checks: the feature, the keys
  resolve to fighting ships, the ship is the giver's side, neither ship is sunk, the mount index gives a
  `TFightingTorpedoMount` with torpedoes left. Then `CallFireMount` (the trampoline, five stack arguments, `g_dfActor` =
  the giver in `__finally`, so a message box of the routine reaches only the giver's chat) inside the release's shared
  section: its 10 `Random` sites and `SetReloadTime`'s 7 draw the shared stream on both PCs in the same order (side 0's
  items, then side 1's); then `FiredMountThisTurn := 1` as the dialog does. Logs on both PCs (torpedoes added to the
  list, TorpsLeft before/after, ReloadTime); the giver's chat after the shared section: "Torpedo salvo fired: <ship>
  launched N torpedoes." `DfText` names only the firing ship (the target's true name is in the logs only: fog of war).
- **The gates** (`B_TORPS`, the two click handlers): between minutes only, the same gate as Detach (`BetweenMinutes`:
  running, no minute, not in a release, A's clock not running on), else "Pause the battle first: torpedoes are fired
  between minutes."; the torpedo dialog in its missile mode (`Missiles` <> 0, only reachable through the still blocked
  *Fire missiles*) stays unavailable. Without every piece (`ResolveTorpedoes`: FireMount's head pattern with the
  `IsSubmerged` call, the `CurrentSpeed`/`ShipType`/`TorpsLeft` offsets and the message call reaching
  `MessageDlgPosHelp`, `ret 0x14`, `bnFireClick`'s call followed by the `FiredMountThisTurn` write, its `Missiles` test,
  the fields by name, the hooks of both handlers and of FireMount, the torpedo dialog's form variable) the feature is
  off and both buttons say "not available"; with FireMount's pattern found the hook is installed anyway, so no call
  outside the simulation ever runs on one PC alone.
- **The torpedo dialog at the minute start.** `ApplyRelease` hides `TdlgTorpTargets` (if visible) right before the
  minute starts, on both PCs ("The torpedo dialog closed as the minute started: its firing solutions hold only between
  minutes."): a solution is only valid for the boundary it was computed at. It was already hidden before every reload
  (`kBattleDialogs`).
- **State hash and leak check** (limits-weapons §7). Group `ammo`: per ship (in the hash's division/ship order) the
  number of mounts in `TorpedomountList` and each mount's `TorpsLeft`, `ReloadTime`, `Reloads`, `Destroyed` (torpedo and
  missile mounts). Group `torp`: each torpedo's `Speed`, `Calibre`, `RunDist`, `TorpedoReliability`, `CheckedFail`,
  `Close`, `SettingLow` besides location, course, remaining range and side. Writers checked statically: the mounts only
  by the simulation, the battle load, `TFightingTorpedoMount.Create` and two dialogs a live battle blocks (the torpedo
  dialog's missile branch 0xaddba6, the developer dialog `TdlgSink` 0xb50a20); the only display code that touches them
  reads them (the dialogs' lists) or copies the ship into fresh objects to draw it (`CopyFightingShip` 0xd6c430 from the
  globe's 0xdeb1e4 and the ship status dialog). The torpedo fields only by `TTorpedo.Create`, the fire routines, the
  simulation (`MoveMe`; the hit check 0xda60e8 sets `CheckedFail` and lowers `Speed`) and the battle load; the only
  display routine walking the torpedo list (the drawing 0xdcdd74) and the battle-save writer (0xdf4aa0) only read. The
  leak check (`LocalSimHash`) covers both (the torpedo fields through `g_torpFields`, the mounts through the same
  `MountBytes` walk); its LEAK line names `<ship>.mounts <old>-><new>`. The trace dumps list the mounts (`M<i>`) and the
  torpedoes (`T<i>`).
- **Bridge:** `ls mounts <division>.<ship> | <ship index>` (any battle) and `ls torp <division> [ship|-] [target|first]
  [mount|first]` (async; the division report through `ShowDivisionDialog`, the hooked *Fire torpedoes*, the dialog's
  ship box, target and mount lists through their own handlers with `SetItemIndex` (VMT +0x170, read from `bnTorpsClick`)
  and `Items.Objects` (+0x2d8, slot 0x18, read from `bnFireClick`), the hooked *Fire*), reporting the solutions and what
  was queued or why not (docs/TESTING.md).
- **Not done (deviations).** The giving PC's `FiredMountThisTurn` is not put back when the hook refuses an order (a UI
  gate the next minute clears; behind the between-minutes gate a refusal is in practice a duplicate, whose first order
  set it already). The dialog is hidden, not refreshed, at the minute start (a refresh would recompute the solutions on
  one PC right before the minute; the player reopens it from the division report at the next pause).
- **Open for the live proof:** salvoes from A, B and both at one boundary in the 1900 fixture (ships in torpedo range:
  run to close contact; *Fire torpedoes* needs A's Realism preference above its second level, `ls torp` reports
  `button=`); equal hashes for 10+ minutes while the torpedoes run and hit; `ls trace 1` showing the FireMount (RVA
  0x578xxx) and SetReloadTime (0x625xxx) draws only inside the release's shared section; `ls leakcheck 1` clean while
  the dialogs are used; a resync with torpedoes in the water (the `.sac` keeps them, both PCs load the same); an order
  that misses its release (dropped on both PCs, the giver's chat); the submerged-tube refusal on a fast ship; a
  duplicate order; whether the battle screen's report list shows the other side's "<ship> is launching torpedoes" lines
  (both sides are human inside the simulation, so `ShouldReport` logs both sides' launches on both PCs, as for the AI's
  automatic fire since 0.3.0).

## Feature 2026-10-17: routine air operations and land-based CAP in live battles (v0.5.0)
The approved plan's phase 3a (session folder: `live-battle-limits-plan.md` row 2; research `limits-air.md` §1, A, B).
Static work: every address below was read with `rdis.py`, the runtime patterns were checked against the memory image
(`verify_p3a.py` in the offline workspace, all OK) and `randtree.py` found no `Random` in the dialog's commit,
`ProcessSearchDirectons`, `TdlgSearch.Execute` or `CheckLandBasedCAP` (depth 6). The mod's own resolver ran offline too:
a 32-bit harness (session folder `p3a_harness`: the mod's sources linked into an exe that maps the memory image at
0x830000 in a suspended child, scans the RTTI and calls `lockstep::Resolve()` and `EnsureHooks()`) logged `air ops on
(commit 00B4BEF4: 9 of 9 stores, ProcessSearchDirectons 1, 18 sectors 1; SearchPatternOrdered +0xac 19 bytes,
SearchPatternCurrent +0xbf 76 bytes; scout question's PlayerSide test found), land CAP on (slots 00FB5920,
CheckLandBasedCAP 00EA5630 read ok, fighter test 00EAC8F0)` and `41 of 41 click handlers`, `land CAP airfield side on,
scout question on`, `routine air ops on, land-based CAP on`. A logic test in the same harness (`harness3.cpp` includes
lockstep.cpp; fake divisions and forces with the image's VMTs in the battle's lists) passed all its checks: on side 1's
PC the division report's write into slot 0 becomes the order `Llandcap#1`, a write into the other slot is no order and
is put back, `ls airops` gives the expected sectors (left 40, right 120: `ordered=0011111000000000000`) and items
(`g_oForce` 10..19), the release applies them in the game's layout and the display layout returns, the hash is the same
in both layouts and equal to the other PC's after the same release, and `FreezeView` restores the layout; the mod's
land-CAP copy picks the side's own airfield, and the game's own `CheckLandBasedCAP` run from the image copy (its two
`GetRangeToShip` calls redirected to a stand-in) gave side 0's request to side 1's nearer airfield: the quirk, on the
game's code. Nothing here has run in the game yet. Engine level `MP_LS_LEVEL 6`, `MP_MOD_VERSION 0.5.0`. The facts are
in GAME-INTERNALS ("Routine air operations and land-based CAP").
- **The force air settings are orders.** `TdlgSearch.bnSaveClick` (0xb4c1d0) calls the unnamed commit 0xb4bef4, which
  writes exactly nine `TForce` fields from the dialog's controls (`CapLevel` +0xa8, `NightCAP` +0xa7, `SearchLimitLeft`
  +0x10b, `SearchLimitRight` +0x10f, `SearchDistance` +0x113, `SearchInterval` +0x117, `RepeatSearch` +0x11c,
  `NightSearch` +0x11d, `TwoPhaseSearch` +0x11b: the verify script lists every `[esi+...]` store) and then
  `ProcessSearchDirectons` (0xe5ae44: `SearchPatternOrdered[0..17]`). The ten fields join `g_oForce` (indexes 10..19;
  `SearchPatternOrdered` as 19 raw bytes: the RTTI has no type for it, `ForceArraySize` takes the size from the next
  field's offset, 0xbf - 0xac). `SearchPatternCurrent` (+0xbf, 19 Integers: the simulation's count-downs per sector,
  written only by `CheckSearchAircraft`, the battle load and read by the save) is not an order. `ResolveAirOps` checks
  the commit's call in `bnSaveClick`, its nine stores (`89/88 86 <offset>`), its `mov eax, esi; call
  ProcessSearchDirectons`, the 18-sector loop (`cmp ebx, 12h`) and the array store; with all of them
  `sbRoutineAirOpsClick` and `Searchpattern1Click` (policy `B_AIROPS`) let the game's handler run, else they keep the
  "not available" line.
- **Only the player's own forces.** `sbRoutineAirOpsClick` (0xdfb5b0) opens the dialog for the first division of
  either side whose map flag is selected (`myFlag` +0x53), so a player could open the other side's force (and read its
  CAP level and search sector). `TdlgSearch.Execute` is hooked through the blockers' thunk (`B_SEARCHDLG`; EDX = the
  force, CL = Editor): in a live battle a force of the other side doesn't open, chat "That force is <player>'s: you can
  set the routine air operations of your own forces only."; the scenario editor's call (Editor) passes. Behind it the
  guard holds anyway: `DetectEdits` turns only objects with `side == S.mySide` into orders, `RestoreAgreed` puts every
  object's agreed values back at the next step and boundary, so an edit of the other side's force (`ls airops ... raw=1`)
  stays on that PC until then and is never sent.
- **ForceFlagship needs nothing.** `TdlgSearch.Execute` writes `ForceFlagship` (+0x18) := `GetForceFlagship` only when
  it is nil, and `GetForceFlagship` (0xe59acc) writes the cache itself whenever the force has a flag division, so the
  dialog does what every display caller of it does (the division report, the Force orders dialog, the change-force
  menu). The simulation reads the cache directly in one place found (`ForceFlagship` readers scanned through
  `myForce`/`SelectedForce`/the forces list): EOTP's 0xdefcd8 (called at 0xdafa7b; the computer's forces following
  their main force, `cmp dword ptr [esi+0x18], 0` at 0xdefdbf), and StartMinute's `CheckObjectives` calls
  `GetForceFlagship` for every force first (0xdab816). So no order and no hash entry. (A pre-existing corner, not
  changed: a force whose flag division exists only with a PC's pending orders in place.)
- **Night CAP over a resync.** The image has no 'NightCAP' key: the battle save drops it, and `TForce.Create` (0xe584a4)
  sets it for every force after 1930. `StartTransfer` sends `ncap=<one digit per force>` (the agreed values, after the
  `RestoreAgreed` of a running battle; on the first start the generated battle's), B keeps it (`S.startNcap`), and
  `LoadSnapshot` applies it on both PCs inside the setup's shared section after `ai1=` and auto-run (`night CAP as before
  the load: on for N of M forces (K put back)`), before `TakeAgreed`.
- **The scout question (found here, not in limits-air A).** `TFightingShip.ShipLaunchSearchAircraft` (0xe48bf0) asks
  "Do you want <ship> to launch scout aircraft?" when `Side` = PlayerSide (0xe48c72: `mov eax, [esi+0xa55]; cmp eax,
  [0xf53630]`, the variable behind the PlayerSide cell 0xf27008), the carrier sees ships and the battle preference
  *Floatplane launch* (prefs +0x4a, `rgFloatplaneLaunch`, in kPrefs) is not Automatic. With the canonical PlayerSide 0
  the live battle's dialog policy answered No for side 0's carriers only (side 1's are never asked), and the text (a
  ship's name) went to A's chat. `TForce.CheckSearchAircraft` (named; called only by `CheckObjectives` 0xdab80f) is
  hooked and runs with `CallWithSimSide(2)` in a live battle: neither side is asked, both launch as their own settings
  say (as the computer's forces do). That test is the only PlayerSide read in its call tree (static scan, depth 5, 113
  routines). The floatplane path stays as it is for both sides alike (`LaunchFloatCheck` for ships with
  `ForceIsHumanControlled`, both sides inside the simulation: Automatic launches, Ask gets the fixed answer No, Manual
  only launches nothing; the vanilla hard-coded `Side = 1` test at 0xe5a2aa only lets side 1 reach that check under
  Manual only); while `CheckSearchAircraft` runs, the policy keeps its boxes in the log (`air search box '...' answered
  N (not shown)`): they name one side's ship and would reach A's chat. Without the pattern the hook isn't installed.
- **Land-based CAP: one slot per side.** `LandCAPDivision[2]` (0xfb5920, cell 0xf27518) is resolved from
  `TdlgDivisionReport.bnOkClick`: `mov eax, [ebx+sbLandCAP]; cmp byte ptr [eax+FDown], 0; je; mov eax, [esi]; mov eax,
  [eax+SelectedDivision]; mov edx, [<cell>]; mov [edx], eax` (0xae0149, the offsets from the RTTI). The guard gets two
  pseudo-objects `Llandcap#0`/`#1` (kind `L`, side = slot, one division-reference field at offset 0 of the slot's
  address). Vanilla writes and reads slot 0 only (`Execute` sets the button's Down from it, `sbLandCAPClick` names its
  division in "Request land CAP for this dvision instead of ...?", `bnOkClick` writes it), so on the PC that commands
  side 1 `ApplyViewSide` swaps the two slots whenever the display state is in place and back when the simulation's is
  (`SwapLandCap`, `g_lcSwapped`): B's dialog shows and changes B's own request; `CanonBegin` (the hash, the battle save)
  sees the game's layout; `SetView(-1)` (before every load) and `FreezeView` (the live battle's end) put the game's
  layout back. The guard never depends on the swap: `Walk` gives each pseudo-object the address of its side's slot in
  the current layout (`LandCapSlot`), so `DetectEdits` at a step's entry (still in the display layout) and `TakeAgreed`
  at its exit (in the game's) read the same canonical values. `sbLandCAPClick` (policy `B_LANDCAP`) runs the game's
  handler when the feature is on, so `PutToggleBack` no longer touches it.
- **Land-based CAP: the airfield's side (the vanilla quirk, confirmed).** `CheckLandBasedCAP(Side)` (0xea5630, EAX =
  the side; `CheckObjectives` 0xdab79f/0xdab7a6) gives `CappedDivision` (+0x40) := `LandCAPDivision[Side]` to the
  nearest airfield division (`GetFlagship.IsAirfield`, `GetRangeToShip` flagship to flagship, strictly nearer) that has a
  fighter formation based there (`ScenHomeBase`, `AircraftType.Role` 0/10/11 through 0xeac8f0); the routine has no
  `Side` operand at all, so a request can task the other side's airfield. In a live battle one player's request could
  then send the other player's fighters. The routine is resolved through StartMinute (`33 D2 8B C3 E8 .. 8B C3 E8
  <CheckObjectives>`) and CheckObjectives (`33 C0 E8 X B8 01 00 00 00 E8 X`), checked against its error message, and its
  code is read step by step (the target test, the divisions list cell, the candidate test, the range comparison, the
  air formations list, the home-base and fighter tests, the write; the fighter test's bytes): only then the hook is
  installed (`LS_dLandCap`). Inside a live battle's simulation (`LiveSimNow`) the mod runs its copy, `LandCapForSide`:
  the same game routines (`GetFlagship`, `IsAirfield`, `GetRangeToShip`, the fighter test) on the same lists in the
  same order, plus `TDivision.Side = side`; an exception clears the
  request as the game's handler does. Both PCs run it inside the minute alike. It also computes the game's own choice
  for the log (`land CAP of side S over <division>: <airfield> covers it; the game alone would have tasked <division>
  of side T`, logged when the assignment changes; `ls landcap` `other=` counts them). Outside live battles (single
  player, battles against the computer, a live battle after a drop) the game's own routine runs: nothing changes there.
  Without every piece the land-CAP button keeps its blocker and the slots are not swapped (they are still guarded and
  hashed).
- **State hash and leak check.** Group `ord`: each force's ten air settings (after its existing fields), both land-CAP
  requests in the game's layout (the division's name and side) and each division's `CappedDivision` (the land-CAP
  routine's result, or the division report's CAP choice: its OK writes it from `cbCappedDivision`, 0xadffe2; an order
  field since 0.3.0). Group `air`: each force's `SearchPatternCurrent`, which the leak check (`LocalSimHash`) covers too.
  The `ls trace` dumps list the force fields and an `L` line with the requests.
- **Bridge:** `ls airforce <force>` (any battle: the settings, the sectors, the count-downs, the flagship cache, the
  requests), `ls landcap` (any battle: the requests, the swap, the counts, every airfield's `CappedDivision`), `ls airops
  <force> [cap=] [nightcap=] [left=] [right=] [dist=] [interval=] [twophase=] [repeat=] [nightsearch=] [raw=1]` and `ls
  landcap <division>|-` (live battle: the dialogs' own writes on this PC's side, then `DetectEdits`; `batch=#n`); `ls
  airstat` adds `searchCalls=`, `landcapCalls=` and two `rules=` digits (docs/TESTING.md).
- **Deviations.** `MP_MOD_VERSION` is 0.5.0 (as the first 0.4.0 commit set 0.4.0; put it back if 0.4.0 ships from
  this branch). The scout-question rule and the quiet air-search boxes are additions (above). `ls airops` writes the
  given fields and leaves the rest as they are (the dialog writes every field from controls that show them, the same
  result) and doesn't check the nation's two-phase-search tech (the dialog's checkbox is disabled without it); `ls
  landcap -` clears a request, which the game's dialog can't. README and CHANGELOG are left for the release.
- **Open for the live proof (1935 campaign):** the settings from both PCs (`ls airops`, then the real dialog through
  posted messages: rgCAP, the spin edits, OK) arriving on the other PC at the next release (`ls airforce` equal on both);
  recon formations (`ls airstat` `m<side>=` recon count, `ls airwin`) leaving in the ordered sectors on both PCs; CAP
  levels changing the CAP formations; night CAP off, then `ls resync` (`ncap=` in the start, `night CAP as before the
  load`, `ls airforce nightcap=0` on both); land CAP from each side with an airfield near its division (`ls landcap`:
  the requests, `swapped=1` on B between minutes, the airfield's `CappedDivision` on both PCs a minute later, and a
  case where the game alone would have picked the other side's airfield); the refusal for the other side's force; equal
  hashes for 30+ minutes with `ls leakcheck 1` clean; the floatplane preference on Automatic and on Ask.

## Feature 2026-10-17: air strikes in live battles (v0.5.0)
The approved plan's phase 3b (session folder: `live-battle-limits-plan.md` row 1, building block 2; research
`limits-air.md` §1, C, D, E; the review's finding on the action exit). Static work: every address below was read with
`rdis.py`, the runtime patterns were checked against the memory image (`verify_p3b.py` in the offline workspace, all OK:
the two dialog calls, the routine's error message, its five stack arguments and `ret 14h`, the allocator and its
counter, ScoutCount's increment, `sbStrikeClick`'s bytes, `FillSquadronList` from `Execute`, Stand down's weapons
return, every formation write of Launch after the call; no `Random` and no PlayerSide read in `LaunchSomeAircraft`'s
call tree, 54 routines, depth 6) and a scan of the display contexts' call trees (237 routines) found no write to the
formation fields the hash now covers. The mod ran offline twice: a 32-bit harness (session folder `p3b_harness`, the
technique of `p3a_harness`: the image mapped at 0x830000 in a suspended child, `lockstep.cpp` included) logged `air
strikes: on (LaunchSomeAircraft 00EA4A18: both dialog calls 1, error message 1, stack arguments 1; AFID counter 00FB0120,
ScoutCount 00FB011C; dlgStrike 00F4C904, styles 00E0A2C4, Execute 00C287A8, FillSquadronList 00C28A1C; ...)` and
`lockstep hooks: ok (21 of 21 context hooks, 44 of 44 click handlers) ... air strikes from the strike dialog on`, and
its logic test passed 33 checks with two PCs simulated in one process (their lockstep state, battle lists and the image's
counters swapped; fake divisions, carriers and formations with the image's VMTs; the mod's real hook on the routine with
its trampoline pointed at a stand-in that does the routine's work as the image shows it, so a launch takes the real
thunk, frame, guard, record and replay): the stack arguments arrive, A's launch gets 1000000 and the counter steps back,
the agreed state holds the new formation under its final key, the dialog's writes become orders under it (the
confirm-target flag cleared, no order for it), B's launch at the same boundary gets 1100000, after the release both PCs
have the same list order (5, 6, 1000000, 1100000; B's PC sorted), counters (AFID as before, ScoutCount +2), names,
numbers, targets, the dialog's values and equal state hashes with no resync; Ready, then Launch from the readied
formation (1100001, 1100002, parent the squadron) on one side only; `ls airlaunch` (degrees to TGlobe units, refused for
the other side's formation and while a minute runs), `ls airlist`, `MaxAFId`, `NextUiAfId` above loaded launches, and a
dropped launch reported on B. The phase-3a harness still passes on this code. Nothing has run in the game yet. Engine
level `MP_LS_LEVEL 7`. The facts are in GAME-INTERNALS ("Air strikes").
- **The launch is an immediate action.** `LaunchSomeAircraft` (unnamed 0xea4a18) is resolved from the call both
  `TdlgStrike.bnLaunchClick` (0xc294a1) and `bnReadyClick` (0xc2b83d) make (`0F B6 4B <Mission> 8B C3 E8`, the same
  target) and checked: its error message, `mov esi, [ebp+18h]; lea edi, [ebp-14h]; movsd x3` (aTarget), `mov edx,
  [ebp+14h]` (aName), `cmp byte ptr [ebp+10h], 6` (aOrderedState), `mov eax, [ebp+0Ch]; mov [ebx+Loadout], eax`,
  `cmp byte ptr [ebp+8], 0` (aSpotNow), `ret 14h`. It becomes the context hook `H_LAUNCH` (K_SIM, pop 0x14: the thunk
  copies the five stack arguments). Called with no simulation frame open (the dialog, `ls airlaunch`) in a running
  live battle, `LS_CtxEnter` records the arguments (`OnLaunchEnter`: EAX the source, EDX the number, CL the mission,
  the stack above the return address); the frame's entry is the guard's step (earlier edits become an order batch,
  the agreed values are put back), so the routine runs on the agreed state, as the replay will. `LS_CtxExit` gives the
  new formation its final AFId **before** the frame's exit takes the agreed state (`OnLaunchPreExit`; the review's
  finding: `TakeAgreed` runs before the old action exit), then queues `Alaunch <source key> <number> <mission> <target,
  12 bytes hex> <name, UTF-8 hex> <ordered state> <loadout> <spot> <new AFId> <ScoutCount used> <side>`. The other PC
  replays it first in the release's shared section (`ReplayLaunch`, through the same hook, nested: no record) with the
  recorded name, sets the same AFId, steps its counter back the same way, adds the recorded ScoutCount use and clears
  `ConfirmTargetLoc`; the routine's only box ("... has target 0,0!") goes to its player only (`g_dfActor`). A replay
  that can't run (the source gone, another side) is logged and the hash catches the difference.
- **Both players at one boundary: no resync.** New formations get side-partitioned AFIds, 1,000,000 + k for side 0 and
  1,100,000 + k for side 1 (`NextUiAfId`: above the side's count and every such id in the battle, so a resync's load or
  a checkpoint can't lead to a reuse; the launching PC assigns its side's ids, the record carries them), and the AFID
  counter steps back when the routine took exactly the next id, so the simulation's own formations keep the ids they
  would have had. Keys (`A<AFId>#0`) of the two players' new formations never meet. The formations launched since the
  end of the minute (`g_airMark`, the air list's count after EOTP and after a load) are the list's tail on both PCs in
  different orders; `SortLaunched` at the end of `ApplyBatches` (inside the shared section) orders that tail by AFId on
  both. Launch actions no longer count for the "both players acted" resync (detach still does); B's "own action missed
  the release" check still covers them. Large ids are safe: the counter's only users are the allocator, the battle save
  and the battle load; `GetAirFormationById` (0xe9f538) is a linear search; the save writes ids as numbers.
  `MaxAFId`/`AirStrikeAfter` (the computer's strike counts) ignore that range.
- **Air formations are order objects.** `Walk` adds kind `A`: every formation, key `A<AFId>#0`, side =
  `TAirFormation.Side`; fields (`g_oAir`): Mission, Loadout, LoadedNumber, OrderedState, Status, StatusCount,
  GroupIndex, LeadAirFormation (a reference by key), CombinedStrikeSpeed, PriorityTarget, ConfirmTargetLoc, TargetLoc
  and Location (12 bytes, only where the layout says 12), AircraftNumber. Everything the dialog writes after the
  routine (GroupIndex from its local `Random(10000)`, spotting, Location, PriorityTarget, the coordination: lead, shared
  GroupIndex, CombinedStrikeSpeed, StatusCount; the source's Mission and OrderedState), Ready, Stand down, the popups
  (mission, loadout, number) and `FillSquadronList`'s `LoadedNumber` writes are orders of the player's own side.
  **Stand down returns weapons (found here):** `bnStandDownClick` calls 0xead168(formation, 0), which adds the
  formation's torpedoes or missiles back to its carrier's `AirTorpedoes` (+0xbb3) / `AirToSurfaceMissiles` (+0xbb7), on
  one PC only; both fields join `g_oShip` (indexes 7, 8) and the hash (`ammo`). **Confirm target:** Launch sets
  `ConfirmTargetLoc` from its check box after the routine returns, so the replayed action alone can't keep it off:
  `DetectEdits` clears it on a formation of this side whose agreed value is 0 (set outside the simulation) before the
  edits are read (`"confirm target location" cleared on A<id>#0`); with the computer's launches cleared after
  `CheckAIAirstrike` as before, no formation asks in a live battle, and re-targeting during the flight stays unavailable.
- **The dialog's gates and safety.** Launch, Ready and Stand down (`B_STRIKEBTN`) and `ls airlaunch` pass `StrikeReady`:
  a running live battle, no simulation frame, the clock paused (`BetweenMinutes`), the boundary processed
  (`S.atBoundary`, `Minute() == S.boundary`) and, while the dialog is open, its list filled since the last minute.
  `FillSquadronList` (0xc28a1c, from `Execute`: `mov eax, ebx; call <fill>; mov eax, ebx; call TCustomForm.Show`,
  checked against its error message, its `TheOriginatingDivision` test and the Launch/Ready calls) is a display context
  (K_UI) whose exit marks the list fresh; `AfterMinute` marks it stale; `OnBoundary` fills the open dialog again right
  after each minute, outside the simulation (`RefreshStrikeList`; a dialog without a listed division is hidden). A
  stale click refills it and says so. The dialog stays hidden before every load (`kBattleDialogs`; its variable now comes
  from `sbStrikeClick`'s cell, so `HideBattleDialogs` finds it even without a scan). `sbStrikeClick` (`B_STRIKE`) is
  replaced in a live battle by its own two calls (the styles 0xe0a2c4, `Execute(nil, nil)`) and a pause request while
  the clock runs: its `PauseGame := 1` would stop only this PC's run (`tmrRunMinuteTimer` 0xdb22d2) and refill its
  tree in EOTP (0xdafbad). `LaunchStrike1Click` (`B_STRIKEDLG`) runs the game's handler. Without every piece (the
  routine and its counters, the dialog's variable and calls, Stand down's return, the fields, the hooks including
  `CheckAIAirstrike`'s) the buttons keep the blocker ("The strike dialog is not available in a live battle (this
  version).") and A starts the battle with the computer's strikes for both sides.
- **The computer's strikes for human sides.** `AirstrikeSimSide(side)` returns that side while its player commands it
  (`g_human`): `CheckAIAirstrike` then skips that side's carrier formations, the single-player rule (only airfields'
  strikes are the computer's); 2 for a side the computer commands (auto-run, a dropped player). The host's new option
  *Computer flies carrier strikes in live battles* (`ui.cpp`, a check box under the turn time limit, host-only, kept in
  `config.ini` as `autostrike`; `mp::SetAutoStrike`; default off) gives 2 for both sides, 0.4.0's rule. It reaches A as
  `StartInfo.autoStrike` (the host's own battle; a relayed battle's message to the first player carries
  `autostrike=`), A's start and resync messages carry `autostrike=` (on when A itself lacks the strike feature), and
  B uses A's value, so both PCs choose alike. The first start's chat says which rule applies when the battle has
  aircraft.
- **State hash and leak check.** `air=`: each formation's StatusCount and parent (by AFId: `ObjName` names a formation
  `AF<id>`), the AFID counter and ScoutCount; `ord=`: Mission, Loadout, LoadedNumber, OrderedState, GroupIndex, lead,
  CombinedStrikeSpeed, PriorityTarget, ConfirmTargetLoc, TargetLoc (8 bytes); `ammo=`: each ship's AirTorpedoes and
  AirToSurfaceMissiles. The leak check skips the formations' order fields (as the ships') and covers the counters. The
  `ls trace` dumps get the new fields and a `C afid=.. scout=..` line.
- **A resync** drops pending orders as before. A's launches are in A's snapshot (immediate actions), its unreleased
  settings for them are not; B's unreleased launches are lost with B's state, and B's chat says `Not carried out (the
  battle was synchronised again): Launch <name>. Give the order again.` (`ReportDroppedDeferred`).
- **Bridge:** `ls airlaunch <AFId> <number> <mission> <lat> <lon>|@<ship> [loadout] [spot] [state]`, `ls airlist`, `ls
  strike`, `ls strikedlg [report]` (async), session level `ls autostrike [0|1]`; the recipe for the real dialog without
  the mouse is in docs/TESTING.md.
- **Deviations.** (1) `ConfirmTargetLoc` is cleared in `DetectEdits` and in the record's replay, not only in the action:
  the dialog sets it after the routine returns. (2) Stand down's weapons return made the carriers' `AirTorpedoes` /
  `AirToSurfaceMissiles` order fields (not in the plan; without it Stand down would desync the carrier's stock). (3)
  `FillSquadronList` is hooked as a display context: its exit is how the gate knows the list is fresh, whoever filled
  it. (4) Names: the replay copies the launching PC's generated name, so when both players launch at one boundary their
  two new formations can carry the same number ("Naval strike #12" on each side); the counters stay equal. (5) `ls
  airlaunch` also takes a ship as the target and an optional state (2 = Ready); `ls strike`, `ls strikedlg` and `ls
  autostrike` are additions. (6) The host option lives in `config.ini` like the window's other settings, not in the
  campaign's session file. (7) README and CHANGELOG are left for the release.
- **Open for the live proof (1935 campaign):** see the report's checklist: a strike by A, one by B, both at one pause,
  each through the real dialog (`ls strikedlg <report>`, LVM_SETITEMSTATE or *Auto select strike*, Launch, the boxes);
  `ls airlist` and the hashes equal on both copies through spotting, take-off, flight, attack, return, landing, the
  merge into the squadron and the formation's deletion (60-90 minutes); Ready and Stand down (with torpedoes: the
  carrier's `AirTorpedoes` back on both); `ls resync` with strikes airborne; the dialog open across minutes and across a
  resync (no crash, `TdlgStrike hidden ...`); the host option on (`aistrike=` for carriers of both sides) and off (only
  airfields); `ls leakcheck 1` clean.

## Feature 2026-10-17: rejoining a live battle (v0.5.0)
The approved plan's phase 5a (session folder: `live-battle-limits-plan.md` row 9(b): a player who crashes or
disconnects gets their ships back when they reconnect during the same battle, host-vs-player battles first). Code only:
the rejoin reuses the start path, the minute gate and the start's side-control setup, so no new game facts were needed;
nothing has run in the game yet. Engine level `MP_LS_LEVEL 8`.
- **Before.** A dropped B (disconnected or silent, `DoDrop`): side 1 went to the computer, the live battle ended on A
  (`Cleanup`, `S.on = false`) and the battle went on as a single-player battle at A's pace; `HostLsPeerGone` cleared
  `g_lsHost.opp`. B left the battle and loaded slot 77 again; a client that came back during the host's month got its
  nation back (0.3.0), but not the battle.
- **A remembers the dropped battle** (`RememberRejoin`, called by `DoDrop`, by `RecoverAlone` after a reload alone and
  by `StartAsA`'s failure path): the battle id, the last epoch, `StartInfo.opp` (in the host's own battle the player's
  nation), both names, the side names, the carrier-strike rule, the campaign-slot flag, the battle screen (the
  `TfrmMain` of this PC's last load) and `game::DoBattleCalls()`. `RejoinInvalid` (every Tick while no live battle
  runs) forgets it when that form is gone or hidden, another DoBattle ran (a freed form's address can be reused),
  `EndGame` is set or the results are up; the public `Hold` (a new live battle) forgets it too. The computer holds
  side 1 meanwhile, as before; A's drop chat now says that the player gets the ships back by joining again.
- **The player is back** (session). `HostOfferRejoin(p)` runs from the hello (after the reservation gave the nation
  back) and from `RematchReservations`; the host's `ls rejoin [nation]` runs it for tests (also for a B dropped while
  still connected). For the host's own battle (`lockstep::Rejoinable`, `opp` = the player's nation) it checks
  `g_lsEnabled` and the player's `battles`, `lockstep` and `SameLiveEngine` (otherwise the player's notice `Your live
  battle against <nation> goes on without you: the computer keeps commanding <nation> (...)` and a host chat line: a
  returning player with another engine level doesn't get the battle back), points `g_lsHost` at the new peer and calls
  `lockstep::Rejoin(bid, name, send to the new peer)`; the player's notice says the battle is still running and loads
  in a moment.
- **The hold at the next minute boundary** (engine). `Rejoin` arms `g_rjTry`. While it waits, `MinuteGate` (before
  its live-battle branch) refuses every `StartMinute` (`MinutesLeft` := 0; logged once, `rejoin: the next minute is
  held`) and `LS_StartRun` skips the game's `StartRun` (with a chat line at most every 3 s): a minute that runs (single
  player since the drop) finishes, no new one starts. `RejoinTick` (from `Tick`, also without a live battle) waits for a
  quiet boundary: no simulation frame, the move-interval timer off, not in turn processing, `CleanStack`. (`Finished`
  is not used: it is 0 before a battle form's first run, which may hold for a reloaded battle too; a guess, not
  checked.) It waits at most `g_tRejoin` (60 s, `ls timeout`'s fifth value), then `RejoinGiveUp`: the gate opens, the
  computer keeps side 1, both players are told. At the boundary it reads the game's own auto-run (`AutoRunBattle`) and
  the Fastest available button, then calls `HoldImpl` (the old `Hold` without its chat and the memo reset) and
  `StartAsA` with the memo's `StartInfo` and the new peer: `S.rejoin`, `S.epoch` = the memo's (the battle id stays, the
  epochs go on), side 0's auto-run = the game's auto-run (`SaveSnapshot` writes the side under its player, the loads
  hand it to the computer again), `S.fastest` = the button. `S.on` is set only for that call; from then on the live
  battle's own gate holds the clock.
- **The start** is the normal one. `StartTransfer` saves the snapshot into the slot and sends the files with `rj=1` and
  `min=<minute>`, `ai1=` with every flag 0 (side 1 is its player's again), `auto=` (side 1 off) and `fastest=`. Both PCs
  load it with the new seed, and `LoadSnapshot`'s first load (`!resync && !everRunning`, true after the hold; B:
  `firstLoad`) runs `SetSideControl(1, false, true)` in its shared section on both PCs; then the READY hashes and `go`.
  `ACheckReady` says `<name> is back in the live battle at minute <m> and commands <side 1> again. ...` and drops the
  memo (a new drop remembers the battle again). Pending orders, deferred actions and side 1's auto-run from before the
  drop went with the drop (`Cleanup`), as at any start. A resync before `go` (the loaded battles differ) takes the
  resync path with the current flags (side 1 human). If the start fails, `StartAsA`'s failure path or `RecoverAlone`
  keeps the computer on side 1 (the failure path now also hands side 1 to the computer when this PC's load opened the
  battle but its setup failed, `S.st == Loading`); if B doesn't load within 120 s, refuses or disconnects, `DoDrop` does
  (`<name> could not rejoin the battle (...)`). Each of them remembers the battle again.
- **The returning player's PC (B)** takes the start like any start (`BOnStart`; with `rj=1` the chat `Back in the live
  battle: ... <host> gives you <side 1> back (the computer commanded it meanwhile). Loading...`): `canLoad` (a client,
  live battles on, not in another battle), the queue (`TryLoadQueued`: a clean stack; `loadReady`: no dialog, event or
  battle), the battle slot (87), its own preferences and x87 control word kept for the end. It needs no month package:
  a restarted copy loads the battle slot from the title screen (`LoadCampaign` hides the start form, as for a month);
  after the battle `g_lsLeave` → `LeaveBattleSlot` loads slot 77 again and the player waits for the month as before (in
  a test battle, phase planning, the host's hello sends the month first). `canLoad` no longer refuses while `g_lsLeave`
  is set without a battle screen (the previous live battle's copy was discarded): the battle takes the slot again and
  the return to slot 77 in between is skipped. A B that still has the battle (dropped while silent with `ls mute`, or
  its leave hasn't run yet) gets a non-resync start for its own battle id: the new restart branch of `BOnStart` treats
  it as a new start (`firstLoad`, the leave cancelled, its pending deferred actions reported as `the battle was started
  again`; the session's `loadReady`, which waits until no battle screen is up, is skipped while its copy is on screen)
  and keeps the preferences and the control word saved at the first start. `AfterMinute` now ends a running
  minute in every live state (a restart queued while B's old minute still ran would otherwise wait for the watchdog).
- **Races.** A player who is back before A's drop has run (`DropPeer` defers `DoDrop` to the end of a running minute):
  `Rejoinable` also says yes for a pending drop of the battle (`S.dropPending`), `Rejoin` arms, `DoDrop` says `<name>
  left the live battle (...) and is back: ...`, and `RejoinTick` starts right after it (the session's `onEnded` keeps
  `g_lsHost` while a rejoin is armed; a host Tick line clears `g_lsHost.opp` once nothing holds, runs or waits). The
  player leaving again while the rejoin waits: `OnPeerLost` (the host's `HostLsPeerGone`, a relayed `peerlost`, a
  client's lost connection, `Leave`) cancels it first.
- **Battles between two joined players' nations** (relayed; a small step, so included). `HostOfferRejoin` finds the
  battle request (`pvp`, being fought, `opp` = B's nation; the battle phase keeps both nations busy while A finishes it
  alone), registers the relay again with B's new peer, refreshes its time limit and sends A `t=rejoin bid p2 lv`
  (`lockstep::RejoinRequestMessage`). A (a client) handles it in `HandleMessage` before the battle-id filter:
  `Rejoin(bid, name, replyTo)` and the same steps through the relay, or `t=norejoin why` back, which the host doesn't
  relay but turns into a notice to B. The leading PC's own crash (A lost) stays out of scope (phase 5b).
- **Bridge:** `ls rejoinable` (any copy, also without a live battle), the host's `ls rejoin [nation]`, `ls status`
  `rejoin=0|1|2 rj=0|1`, `ls timeout ... [rejoin s]`; every step logs `lockstep: rejoin: ...` (the lines are in
  docs/TESTING.md).
- **Deviations.** (1) The hold has two steps: until the boundary the rejoin's own gate (`S.on` stays false, so the
  minute that still runs keeps the single-player dialogs and dice), then `S.on` only from the start call on. (2) The
  trigger is the player's return as the nation (hello, reservation); a player dropped while still connected must join
  again (or the host's `ls rejoin`, tests). (3) The memo also covers a failed start and a failed reload that went on
  alone, not only `DoDrop`. (4) The battle id stays; the epochs go on. (5) The game's own auto-run of the host's ships
  since the drop becomes side 0's auto-run instead of blocking the rejoin. (6) Battles between two joined players are
  included. (7) README and CHANGELOG are left for the release.
- **Open for the live proof (1900 fixture):** see the report's checklist (B killed and started again, B reconnected
  without a restart, the return during a minute, during a resync and after the battle ended, another engine level, the
  month after the battle, equal hashes after the rejoin, side 1 human again on both copies).

## Feature 2026-10-17: missiles in live battles (v0.6.0)
The approved plan's phase 4 (session folder: `live-battle-limits-plan.md` row 3; research `limits-weapons.md` §2, §3,
§6-§8). Built on phase 0 step 2 (the automatic fire with the simulation PlayerSide 2, the locked missile panel), phase
1's deferred actions and phase 2's torpedo salvoes. Static work: every address below was read with `rdis.py` and the
weapons scripts, the runtime patterns were checked against the memory image (`verify_p4.py` in the offline workspace,
all OK; the scans are in its `p4\` folder) and `randtree.py` found no `Random` in the routines the salvo prediction
calls; nothing here has run in the game yet. Engine level `MP_LS_LEVEL 9`, `MP_MOD_VERSION` 0.6.0. The facts are in
GAME-INTERNALS ("Missiles").
- **What the game does.** The automatic fire `CheckMissileFiringForAllShips` (0xda91fc, EOTP after 1940) gates each
  division with `Side` < 3 at 0xda92ce: *Fire at will* (`cbFireAtWill.GetChecked`) → fire; else a computer-controlled
  force (`AIControlled` +0x79) whose flagship's nation record (array 0xfacb4e, 334 bytes each) has +0x149 < 2 → fire;
  else `Side` = PlayerSide → skip (0xda946b); fire → `TDivision.HoldMissileFire` (+0x69) → the same skip. Since phase 0
  the routine runs with PlayerSide 2 in a live battle, so every side-0/1 division reaches the hold test and the box
  changes nothing. *Manual fire* (`sbFireSalvoClick` 0xe04554) opens the modal `TdlgMissileFire`; its *Fire one / half
  / all* run `FireMissiles(1|2|4)` (0xd80c98), which per checked ship and launcher calls the nested procedure 0xd80aec:
  "too close", the launcher can't bear → the ship's `PendingMissileTarget` := the location (0xd80b79, fired later by
  the simulation's `FireAllGuns` → `ShipFirePendingHeavySSM`), a friendly ship in the line → "abort", else
  `TfrmMain.FireMissileMount(mount, ship, target | nil, @location, bearing, N)` (0xe04718), whose result drives the
  dialog's stop rules (*one*: stop after a launch of 1; *half*: when half of the ship's launchers is not above the
  missiles fired so far) and its summary box. A missile submarine fires `FireSSM` twice with side 0 and nation 0's
  figures and gets `Downtime` := 5000 (0xd81102).
- **HoldMissileFire is a safe switch** (the task's precondition). `p4\hold_scan.py`: in the call tree of 0xda91fc (all
  calls, depth 12) the only `[reg+0x69]` read of game code is 0xda9326 (the gate); the other hits are VCL `FVisible`
  reads. `p4\hold_global.py` (the whole image): `HoldMissileFire` is read there and by the division report
  (`TdlgDivisionReport.Execute`, its check box) and written by the report's `bnOkClick` (0xae0146, an order of the
  guard) and by the targeting step `sub_d92b48` (0xd92c01: EOTP, before the missile fire, clears the hold flags of
  `AIControlled` divisions). The routine's second loop (`CheckSAMFireAtSnoopers` 0xda6ccc, per ship) doesn't read it.
- **Each player's own Fire at will** (the main session's symmetric design). `LS_MissileFire` keeps PlayerSide 2 and, for
  a side whose agreed setting is off and whose player commands it (`g_human`; auto-run and a dropped player are the
  computer's), sets `HoldMissileFire` := 1 on that side's divisions with ships and a force that are not holding already
  (`MissileHoldBegin`, a static array, an exception holds nothing and is logged), runs the routine and clears exactly
  those again in `__finally` (`MissileFireLive`). The game's exception is kept: a division of a computer-controlled
  force whose nation's +0x149 is below 2 fires with the box unchecked in single player, so it is not held. The order
  guard takes its snapshots outside the call, so the held values never become orders. During the call the box's
  `FState` (+0x2d2) is the canonical 0 on both PCs (with PlayerSide 2 the routine's path doesn't depend on it, and
  nothing else in the simulation reads the box). Log `automatic missile fire of side <s>: held (its Fire at will is off)
  (<n> division(s) held)` on every change.
- **Synced order state.** The two settings live in `g_faw[2]` (default 1 = on, as in 0.4.0/0.5.0) and are a
  pseudo-object per side (kind `M`, field `FireAtWill`, keys `Mfireatwill#0`/`#1`, side = slot), so the guard makes
  batches of a side's edits and both PCs apply them at the release like a course; each PC edits only its own slot. The
  box's `OnClick` is `cbShowBaseFlagsClick` (0xe008d8, shared with *Base flags*: prefs +0x38 :=
  `cbShowBaseFlags.Checked`, repaint); its new blocker (`B_FIREATWILL`) sees the click after VCL toggled the box
  (`FireAtWillClicked`: this player's order, refused with the box put back while the battle can't take orders) and lets
  the handler run. The battle save and the form keep neither value: the start and resync messages carry them (`faw=`),
  `LoadSnapshot` applies them in its shared setup (`ApplyFireAtWill`) and shows this PC's side's value
  (`ShowFireAtWill`: `FState` and `BM_SETCHECK`, no `OnClick`); a new live battle starts with both on (`Hold`), a rejoin
  keeps the battle's. Hashed (`ord=`, 2 bytes) and dumped (`M faw=`).
- **The panel.** `LockMissilePanel` (every boundary and load) now disables only *Fire mode* (`rgMissileDoctrine`: the
  computer picks both sides' salvo size; read only by `FireSSMAtTarget` for PlayerSide's ships) with a hint on the group
  and its buttons, and enables *Fire at will* with a hint (hints set once per battle screen: `FHint` +0x90 through
  `_UStrAsg`, which copies the mod's literal, `FShowHint`, `FParentShowHint`). Without the feature the box stays
  disabled (0.4.0). `UnlockMissilePanel` (`Cleanup`, not during a load) gives the panel back when the live battle ends
  on a PC that goes on alone (0.4.0 left it locked after a drop).
- **Manual salvoes.** `ResolveMissiles` reads `FireMissileMount`'s head (the preconditions `ElectricDisabled`, the
  mount's `Destroyed`, `TorpsLeft` > 0, `MountCanFire(bearing)` or ship type 12, `GetDistanceToPoint` above
  `GetMissileMinimumRange(MountType)` and below `sub_e4accc(Nation, MountType)`, all exiting to 0xe04a4a, the end of its
  try block, with nothing written), its count `min(Tubes, NumberToFire)` (0xe048d3), result `[ebp-8]` and `ret 10h`.
  `LS_dFireMissileMount` (naked: EAX/EDX/ECX and the four stack arguments to `LS_FireMissileMount`; 1 = `mov eax,
  [g_lsMslCount]; ret 10h`) passes inside the simulation (the computer's `FireSSMAtTarget`, the pending heavy SSM, the
  release), during a load and outside a live battle. From the screen (`MslOrder`): the feature, the classes,
  `MissileOrdersOpen` (orders open, no minute running, at the processed boundary, no release waiting, B's clock not
  running on, the own side not on auto-run), the ship on this PC's side, the keys, then `MslPredict` with the stock less
  this player's pending orders on that launcher (`MslLeftAfterPending`): 0 → no order and 0 to the dialog (the routine
  would fire nothing and change nothing); else a deferred `msl` (ship, mount index, target key or `-`, the location as
  24 hex digits, bearing, N, the predicted count; this boundary only; queued without the generic chat) and the predicted
  count to the dialog. One chat line per dialog action (`MslNote`, posted): "Missile salvo ordered: N missiles from
  <ships>; they fire when the next minute starts." Several orders on one launcher at a boundary are allowed (the
  dialog's *one* spreads over ships); the torpedoes' one-per-mount rule doesn't apply.
- **The release** (`RunDeferred`, kind `msl`): the feature, the keys, the ship the actor's and afloat, the target (if
  any) afloat, the side not on auto-run, the launcher by index, then `MslPredict` again on the agreed state, and
  `CallFireMissileMount` (the trampoline, `g_dfActor` in `__finally`) inside the shared section: `FireSSM` passes
  (simulation context) and `SetReloadTime`'s draws are shared dice; side 0's items first. Logs on both PCs (fired, the
  missiles added to the torpedo list, the stock before/after, ReloadTime); the giver's chat after the release, one line
  for all salvoes ("Missile salvo fired: <ship> <n>, ... (N missiles).") and each skip reason once (`MslReleasedNote`).
  `DfText`: "Missile salvo of <ship>".
- **Gates.** *Manual fire* (`B_SALVO`, new): the feature, orders open; between minutes with the timer off at the
  processed boundary the game's handler runs (its `PauseGame := 1` needs the timer on, so it never fires here) and opens
  the modal dialog; otherwise a pause request ("Pausing the battle: open Manual fire again when it is paused ...").
  While it is open the existing modal rules apply (checked): A's `ATryRelease`/`ATryCheckpoint` skip while
  `game::ModalOpen()`, B reports `busy=1` (A's wait limit 300 s), a release reaching B during the dialog waits
  (`hasPendingRel`) and is applied after it, loads wait for a clean stack. *Fire one / half / all* (`B_SALVOFIRE`, new
  click handlers of `TdlgMissileFire`): `MissileOrdersOpen`, then the checked missile submarines are unchecked
  (`UncheckMissileSubs`: the items whose `Data` is a `TSubmarine`, `TListItem.SetChecked` 0x9e604c) with a chat line,
  and `FireMissiles` runs. `bnFireMissilesClick` and `TfrmMain.bnFireClick` (dead code) keep their blockers.
- **Missile submarines.** The dialog's branch is the only one-PC writer of submarine state (`Downtime`), and it fires as
  side 0: besides the unchecking, `FireSSM` (0xda6374; EAX, EDX = side, ECX = @location, seven stack arguments, `ret
  1Ch`) is hooked (`LS_dFireSSM`) and refused from the screen with a chat line; the simulation's calls pass.
- **Hash and leak check.** `PendingMissileTarget` (+0xa93, 12 bytes, checked against `Sunk` = +0xa9f) is an order field
  of the ship (`g_oShip` index 9) and hashed in `ord=` with its first 8 bytes (`ShipFirePendingHeavySSM` clears it with
  `EmptyPoint` from an uninitialised local, so the third dword can differ; `IsEmptyPoint` reads only the first two).
  The battle save doesn't keep it and the setups clear it (`sub_bdb82c`), so the start and resync messages carry the
  set ones (`pmt=<all-ships index>:<24 hex>;...`). The missile mounts were already in the `ammo=` mount walk (phase 2).
  New group `sub=`: the submarine count and each one's `Downtime`, `Location` (8 bytes), `TorpsFired`; writers checked
  (`p4\sub_writers.py`, `p4\sub_torpsfired.py`): during a battle only the simulation (0xdc4154, 0xdcf83c, 0xdec0e0,
  0xdee320, `CheckMissileFiringForSubmarines`, `CheckAirSubmarineSighting`, 0xda65e4), the battle load (0xddcec0), the
  setup (0xdc339c: 0xd6a9ec, 0xdee538) and the dialog's blocked branch; the globe (0xde0998) and the dialog's lists only
  read them. The leak
  check (`LocalSimHash`) covers them; the trace dumps list them (`U<i>`). `SSMHitsScored` and
  `MissilesIncomingThisTurn` are not hashed yet.
- **Bridge:** `ls fireatwill [0|1]` (the box and its handler, then the guard), `ls missiles <division> [target|first]
  [1|2|4] [all]` (async: the dialog's fills, its target click, the shooters' check boxes, the hooked fire handler; its
  boxes answered quietly), `ls missilestat` (any battle: per side the missile ships, stocks by launcher type, pending
  targets, missiles in the air, missile submarines; Fire at will mine/agreed/box; the hold and salvo counters). The
  first start's chat explains the three controls when the battle has missile ships. Formats in docs/TESTING.md.
- **Deviations.** (1) *Fire at will* starts on for both sides (the 0.4.0/0.5.0 behaviour). (2) A side the computer
  commands is never held. (3) The game's computer-force exception is mirrored, not overridden. (4) The box is canonical
  only around the routine's call (its only simulation reader), not a general display swap. (5) Missile submarines can't
  fire salvoes in a live battle (the game's branch is side 0 only). (6) Manual salvoes are refused while the own side is
  on auto-run. (7) Cosmetic: the dialog shows the stocks before the release and plays the launch sound at the order
  (the release's `PlayMissileLaunch` plays it again in the next minute on both PCs, through the display context
  `PlaySoundEffect`). (8) The panel's reset at the end of a live battle is new. (9) A B order that reaches A after A's
  release (A released while B's dialog was open) is dropped on both PCs ("arrived too late"); `MissileOrdersOpen`
  refuses orders on B once a release waits. (10) README and CHANGELOG are left for the release.
- **Open for the live proof:** no fixture has missile ships yet (a 1950s+ campaign, see the report's checklist).

## Feature 2026-10-17: resuming a live battle after the host's game restarts (v0.6.0)
The approved plan's phase 5b (session folder: `live-battle-limits-plan.md` row 9(c): resume a live battle after the
leading PC restarts). The host's game stops (a crash, or it is closed) during its own live battle against a joined
player; until now its player went back to waiting, and loading the host's slot finished the battle against the computer
(0.4.0's Save checkpoint). Now the host can take the battle up live again with that player. Static work: every game
address below was read with `rdis.py`; nothing has run in the game yet. The engine level stays `MP_LS_LEVEL 9` (below).
- **Investigation: the host's session and the load order.** `mp::Host` works from the title screen or a campaign. In the
  lobby `SyncHostCampaign` takes up the open campaign (the slot's `rtw3mp_session.ini` reservations, the players' picks)
  only while its strategic screen is visible (`CampaignLoaded`: `frmBuildCamp` visible, slot 1-49); *Start session*
  needs `CampaignReady` and otherwise arms the automatic start; the month starts with `RunHostTurn` (save, merge,
  reload, the game's Turn handler with `g_turnRunning`). A returning player gets the nation from the hello only with
  `CampaignLoaded` or outside the lobby; `RematchReservations` and the rejoin offer run outside the lobby only.
- **Investigation: a campaign load that resumes a battle.** `LoadBuildCampaign`: if `BattleIndex` (+0x4a3f) is not -1
  the game asks "Select autostart point." (`TdlgEventAnswer.ExecuteGeneral` at 0xc9ae54: 1 "Regenerate battle from
  starting parameters." sets `RestartFromBattleGeneration` +0x4a3a and skips the resume; 2 "Start from latest autosave
  point." resumes); then `LoadScenarioInProgress` (0xc9ae81 → 0xcd01b0): `CampaignScenario` 0, `BCScenario` 1,
  `PlayerSide` 0, `TfrmMain.Execute(<slot>.sac)` (0xcd0338, a modeless show), and `frmBuildCamp.Hide` (0xcd036f): when
  `LoadBuildCampaign` returns the battle screen is open and the strategic screen hidden (the mod's `game::LoadCampaign`
  shows it only when `BattleOpponentIdx` is -1). `BattleIndex` is written by `DoPossessionBattle` (0xcedf93),
  `DoHomeAreaBattle` (0xceef0b), `CheckSurpriseAttack` (0xcfdb77) and the load (0xc9ad51), so a real month's battle asks
  and `pocbattle` (`DoBattle` directly) doesn't; the lockstep's own loads answer 2 (`LS_ExecGeneral`, quiet).
- **Investigation: the month around the host's battle.** `DoTurn2` (0xce1644): the autosave, officers, the AI's
  strategic moves (0xcf024c), `StopBlockadedMoves`, `AreaMoveShips`, `MovePlayerShipsOnForeignStations`, then
  `CheckForBattles2` (0xce180e): a battle → `DoTurn2` returns with the battle screen open; none → `DoStrategicTurn`
  (0xce181a). The battle's close, `TfrmMain.FormClose` (0xdcd67c) with `BCScenario` (+0x7f5): `Hide`,
  `frmBuildCamp.Show`, the aftermath (invasion result, history record, `CheckPrisoners`, `DoBattlePrestigeLoss`,
  `BattlePossession` nil, `BattleOpponentIdx` -1), then `DoStrategicTurn` (0xdcda2a; `AdvanceDesignStudies` instead only
  for the quick-battle flag `[0xf275c4]`). A resumed battle is a BC scenario as well, so its close runs the rest of the
  month too. `DoStrategicTurn` (0xce1938) doesn't call `CheckForBattles2` again (one host battle per month) and runs
  `ReduceTimeLimits` (0xce1dcf), `ComputeExpenses`, `CheckTurnEvents`, `DoResearch`, `CheckAIvsAIBattles` (0xce20b1, the
  only path to `DoBattleBetweenAINations`: `CheckAivsAIBattleInArea` 0xd32ce9), `CheckRaiderActivity`, ... and
  `DoEndOfTurn` (0xce21f4). So while the host's battle is on screen no battle of a joined player has been deferred yet
  (`g_battles` is empty), the AI moves of human nations are undone already and the merged submissions are in the save: a
  restart during the host's battle loses no session state the rest of the month needs (`g_enemyVPSeen` and the per-month
  guards are rebuilt, the war scores and pending diplomacy come back from `rtw3mp_session.ini`).
  `TfrmMain.FormCloseQuery` (0xdc6694) refuses to leave an unfinished campaign battle ("You cannot leave a RTW campaign
  scenario before it is finished! Use save and exit..."); the game's own Save of a campaign battle (`sbSaveClick`
  0xde0274) writes `SaveDir(slot)\RTWGame<slot>.sac` (0xde0380) and the campaign (0xde0398) and asks "Exit game?"
  (0xde03a8; Yes closes `frmBuildCamp`, the game): the vanilla save-and-quit.
- **What a mid-month restart did before.** The client lost A and went back to slot 77. The restarted host either played
  alone (no session) or hosted a new session that stayed in the lobby: with the battle screen up `CampaignLoaded` is
  false, so no reservation was matched and a returning player got no nation; *Start session* waited for the strategic
  screen, i.e. for the month's end. The rest of the month ran without the session's hooks (`IsHumanRemoteNation` is
  false in the lobby): the AI built, moved, budgeted and set research for the joined players' nations, their battles
  against AI nations were resolved by the game (`HostDeferBattle` needs `g_turnRunning`), the host's battle didn't count
  for a war score (`OnBattleVP` needs a running session) and their design studies and doctrine were not counted down
  that month. *Start session* then sent the next month.
- **The marker** (`lockstep.cpp`, `WriteMarker`). Every `SaveSnapshot` of the host's own battle (`S.campaignSlot`: the
  start, a resync, Save, a rejoin's start) also writes `rtw3mp_livebattle.ini` into the slot (KV): `bid`, `epoch`,
  `opp`, `host`, `peer`, `s0`/`s1`, `autostrike`, `faw`, `ncap`, `pmt`, `minute`, `slotmode` (`game::BattleSlotMode()`:
  the host's test battle outside a month), `sac` (FNV-1a 64 and the size of `RTWGame<slot>.sac`) and `saved`. Only a
  load that resumes exactly that battle save uses it, so a marker left behind is inert. Deleted by `Ended(true)` on A,
  by `ForgetRejoin(..., over)` when a battle that can be rejoined is over (`RejoinInvalid`: its end, or another
  DoBattle) and by `Hold` (a new live battle); a closed battle screen alone keeps it (the game's exit closes it too).
  The game's own Save outside a live battle of a battle that can be rejoined (single player after a drop, or a resumed
  battle after Run): the Save blocker posts `RefreshMarkerAfterSave`, which follows the new battle save (hash, minute,
  night CAP, pending targets) after the handler has saved (inside its "Exit game?" box). The session's file transfers
  leave the marker out (`BuildBundle` skips `rtw3mp*`, `BuildStateBundle` takes only the save files). Save's chat line
  says so now ("... loading that slot resumes the battle, and it waits for <player> to join the host's session again
  (or goes on against the computer).").
- **The resume** (`ResumeFromMarker`, from `OnCampaignLoaded` after its side-1 check; not in the lockstep's own loads or
  while a live battle runs). A slot 1-49 with a marker whose `sac` equals the battle save's hash, a complete marker and
  the engine (`EnsureHooks`): the rejoin memo of phase 5a is filled from the marker (`resumed`, a new `serial`, the
  battle screen of this load, `DoBattleCalls`, `slot`) and `held`; Fire at will per side comes from the marker
  (`ApplyFireAtWill`), night CAP and the pending missile targets too when `Minute()` equals the marker's minute (else a
  log line), a test battle gets its battle slot mode back; side 1 stays the computer's (the game's resume). Otherwise
  one log line (`... is not taken up as a live battle (<why>)`) and the battle is the game's own as before. **The
  hold:** `MinuteGate` refuses `StartMinute` (`ResumeHeld`: no live battle, the memo valid and held) and `LS_StartRun`
  treats the host's Run (the Run buttons, auto-run, `pocrun`) as "go on against the computer": `ReleaseResumeHold` logs,
  says so in the chat and lets the game's run start; a key that runs one minute (`FormKeyUp` → `StartMinute` 0xdab630
  without a run, `MinutesLeft` 0) counts as the host's Run too. The battle stays rejoinable. The rejoin's start clears
  the hold (`RejoinTick`), as does the end of the memo. No modal box: the session's `resumed` hook (posted after the
  load) puts the explanation into the chat and the status line and opens the Multiplayer window without the focus
  (`ui::RevealMpWindow`, also outside a session; never owned by the battle screen, which the live battles free).
- **The session** (`session.cpp`). `SyncHostCampaign` in the lobby also takes up a campaign whose battle screen shows a
  battle of the host's campaign slot that can be rejoined (`MonthBattleOnScreen`: `lockstep::Rejoinable` with
  `campaignSlot` and the open slot; a resumed one, or a 5a battle whose player was dropped before the host hosted again)
  and then `AdoptMonthInProgress`: the players' nations reserved as at *Start session*, `g_enemyVPSeen` as at the
  month's start if unknown, `g_lsNextBid` above the battle's id, processing with `g_turnRunning` as `RunHostTurn` leaves
  them, `SaveReservations`, chat and status, and the rejoin offered to the player of that nation if connected. A player
  who joins later gets the nation from the hello (outside the lobby now) and the battle through `HostOfferRejoin`;
  during a month `HostResumeTick` offers a resumed battle once (`Player.offeredSerial`) to a connected player of that
  nation (the host loaded the slot again after a failed reload). Then the rejoin of phase 5a unchanged: at the next
  quiet boundary (at once while held) `StartAsA` from the memo, the snapshot (and a new marker) with the next epoch,
  `rj=1`, `min=` and `hr=1`, both PCs load, side 1 human, READY, `go`. The battle's close runs `DoStrategicTurn` under
  the session's hooks (the AI off for the reserved nations, their battles deferred, VP and war scores credited), and
  after `DoEndOfTurn` the battle phase, the diplomacy and the new month follow; a resumed test battle's close runs no
  month and the session resends the current month after 4 s ("The turn did not advance"). The player's notice says the
  battle waited for them (held) or that the computer commanded their side meanwhile (released).
- **B** takes the start as any rejoin's; with `hr=1` its chat says that the host's game was restarted and the battle
  goes on from its last save. **Why the level stays 9:** the PCs exchange exactly the rejoin start of level 8; `hr=1` is
  an ignorable key that only picks B's chat line, and the marker never leaves the host's PC.
- **Bridge:** `ls resumable` (any copy: `on held rejoinable bid epoch opp slot slotMode serial loadedAt minute age
  pending marker=match|stale|none|-... peer why`), `ls resume run` (the host's Run on a held resumed battle, without a
  run), `ls autostart 0|1|2` (the answer to the next "Select autostart point.", one-shot, installs the battle hooks),
  `loadasync <slot> [player]` (a `load` that doesn't wait); `ls session` ends with `turn=` and `hostSlot=`. Every step
  logs `lockstep: resume: ...` or `resume: ...` (the lines are in docs/TESTING.md).
- **Deviations.** (1) The marker is a side file, tied to the battle save by its hash, not a key in the campaign file.
  (2) Run releases the hold without a question (the explanation is in the chat and the status line; the window opens
  without a modal box). (3) The month is taken up automatically when the host hosts with such a battle on screen; this
  also covers a host that left the session and hosted again while a dropped player's battle went on (phase 5a). (4) A
  rejoin that fails after it was started leaves the battle unheld (the computer keeps side 1; the player can join
  again). (5) During a month a connected player is offered a resumed battle once; in planning (test battles) only a new
  hello or `ls rejoin` offers it. (6) README and CHANGELOG are left for the release.
- **Not covered.** Battles between two joined players (A on a player's PC). If the host doesn't host before the resumed
  battle ends, the rest of that month runs without the session as before (the chat says so). The game's "Select
  autostart point." stays the host's question (*Regenerate* skips the resume). **For the merge of main's 0.3.2 date
  fix:** the resume's load writes the battle's day and month into the campaign date variables like every battle load;
  `ResumeFromMarker` must then record the campaign date from the loaded save (its `[General]` date, i.e.
  `CampaignDate`), not from the variables, before the rejoin's snapshot saves it. Until then (as in all of live-limits)
  a resync's or rejoin's snapshot can carry the battle's date, and `rtw3mp_session.ini`'s war scores and pending
  diplomacy are dropped at the restart when its `diplomacyDate` no longer matches the save's month.
- **Open for the live proof:** see the report's checklist (the host killed mid-battle in a test battle and in a real
  month's battle, the load before and after hosting, held and released, the rejoin, the month and the new month after
  it, a second crash of the resumed battle, the game's own Save after Run, stale markers).

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
  - *Correction 2026-10-17: Year, Month and Day are Integer (32-bit) variables: every writer stores a dword and
    `SaveBuildCampaign` reads dwords; only `EncodeDate`'s Word parameters read the low words. And since engine
    level 10 both PCs keep the campaign's own date after every live load. See
    [live battles on the campaign's own date](#fix-2026-10-17-live-battles-on-the-campaigns-own-date-engine-level-10-v060).*
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

## Fix 2026-10-17: live battles on the campaign's own date, engine level 10 (v0.6.0)
- Done right after merging 0.3.2 into the live-battle branch. 0.3.2 keeps the host's campaign date in its snapshots
  and puts it back when the results open and when the month runs, but every live load still left the battle's own day
  and month in the date variables: both PCs ran the battle with them (the weather by month, the AI's day parity), and
  a resumed battle's rejoin recorded them (phase 5b's open point). Static work: the research of 2026-10-17 (session
  folder `limits-date.md`; `verify_p6.py` in the offline workspace checks the pattern and the operand sizes). Nothing
  here has run in the game yet.
- **Mechanism (1.01.44).** `LoadBuildCampaign` reads `[General]` Year/Month/Day into the three variables (Month
  0xc99ada, Day 0xc99af6) and builds the TDateTime CampaignDate `[0xf52fe8]` from them (0xc9a3bc..0xc9a3db, +0xaf8, the
  resolver's pattern, unique in the image) before it resumes the battle (`LoadScenarioInProgress`, 0xc9ae81). The
  `.sac` reader (`sub_dd8f78`, from `TfrmMain.Execute` -> 0xdde0a0) then stores `[Environment] Day` and `Month`, the
  battle clock's date when the battle was saved, into the campaign's Day and Month (0xdd92ca, 0xdd92fa); its Year only
  goes into the clock. Nothing in a battle or on the `.sac` path writes CampaignDate.
- **Fix.** `LoadCampaignGuarded`, the only path of every live load (`LoadSnapshot` on both PCs for the start, a resync
  and a rejoin; A's `RecoverAlone`), calls `KeepCampaignDate` after a successful load, before the READY hash: Year,
  Month and Day := DecodeDate(CampaignDate), computed as `SysUtils.DecodeDate` does (milliseconds rounded, whole days
  since 1899-12-30, the proleptic Gregorian calendar; checked offline against a day-by-day walk of 1800-2099, times of
  day included). CampaignDate and the battle clock stay as loaded. The date never comes from the clock: its date is the
  battle's own, and before 1900 the generator encodes it as Year + 100 (0xbdf0a9). Both PCs load the same files, so
  both keep the same date. Log: `lockstep: the load: campaign date 1900-08-01 kept (the battle save said 1900-08-14)`,
  or `(as the battle save: ...)`; a `WARNING` line when the date can't be decoded, when the year differed (never
  expected: the `.sac` reader doesn't write it) or when A's record differs from the loaded date.
- **The host's resume** (phase 5b): `ResumeFromMarker` calls `RecordLoadedCampaignDate`: the same restore (`resume:
  campaign date ... kept`) and A's record taken from it (`resume: campaign date recorded from the loaded save: ...`), so
  the rejoin's snapshots swap the loaded save's own date in. Before, the rejoin's hold recorded the `.sac`'s day and
  month, and its snapshots made the drift permanent.
- **The state hash** covers Year, Month and Day (12 bytes into `all`, after the clock); the hash line ends with
  ` date=Y-MM-DD`. Month and Day are simulation inputs: the weather (EOTP -> `sub_dae42c` -> `sub_dce844`/`sub_dcf0d2`),
  `TfrmMain.IsWinter` (sight), the AI's bearing parity (`sub_d968e4`, 0xd97ad6) and the battle's `MonthRecords` row.
- **Engine level 10** (`MP_LS_LEVEL`): a level-9 PC would run the battle with the `.sac`'s day and month, so both PCs
  need 10. `MP_MOD_VERSION` stays 0.6.0; `MP_PROTOCOL` stays 3.
- **0.3.2's host-side pieces stay as belts and braces:** `Hold`'s record, `SwapCampaignDate` around the snapshot's
  campaign save, `OnResultsEnter`, `OnMonthRuns`, `OnBattleSlotClosed`, `OnCampaignLoading`. With the restore, the
  results and the month find the date already right (no `the battle's results: ... put back` line any more).
  `RecordCampaignDate` runs in `HoldImpl`, the branch's half of `Hold`, so a rejoin's hold puts its battle's record
  back too.
- **The variables are Integers** (0.3.2 called them Word variables): every writer stores a dword (`LoadBuildCampaign`
  0xc99ada/0xc99af6, the `.sac` reader 0xdd92ca/0xdd92fa, `DoStrategicTurn` 0xce1c6f/0xce1c75/0xce1c81) and
  `SaveBuildCampaign` reads dwords; only `EncodeDate`'s Word parameters read the low words. The mod now reads and
  writes them as `int32_t` (0.3.2's `uint16_t` wrote only the low words: the same values, since the high words are 0).
- **The merge itself:** the box texts the branch's own code sends to the chat (a deferred action's box and picture
  message, the auto-run end line) go through `ChatSafe` like 0.3.2's. Quiet boxes no longer set `g_lastBox`, so
  `ls missiles` takes its summary box from `g_lastQuietBox`.
- **Tests.** `lockstep_month`'s aftermath check now expects every live load on the host to keep the month's own date
  (`the load: campaign date <date> kept (the battle save said ...)` when the battle's own date differs) and no
  `the battle's results: ... put back` line. `lockstep_ui` h), `lockstep_air` i) and `lockstep_stress` g) (the
  snapshots' [General] date, the record) are unchanged. **Open for the live proof:** both copies' hash lines carry the
  campaign's date from the first READY on; a battle dated in the next month (and a December battle) through the
  start, `ls resync`, a rejoin and the host's resume; the host's and the client's month after the battle.

## Testing 2026-10-17: stage A live proof (v0.6.0 branch)
- Research agent, the branch `live-limits` (db97846 + the changes below), engine level 10. The features planned for
  0.4.0 (phase 0 step 2, phases 1, 1b and 2) and the campaign date (engine level 10), proved live with two game copies
  (three for the battles between joined players) on one PC, driven only through the test bridge and posted messages:
  an interactive driver around `tools/regression.py` (its preflight, backups, launch, close and restore; slot 1 and,
  for the 1935 campaign, slot 9 backed up and put back by hash after every run; `Game2` never touched). The drivers and
  logs stay in the session folder. Builds: frozen copies `dist_ll_1`..`dist_ll_4`. Game open: ~60 min in five runs plus
  the full default run.
- **Test switches added** (test only, normal play unchanged): env `RTW3MP_TEST_LS_LEVEL=<n>` (read once: the level this
  copy announces in its hello and in the start/ready/rejoin messages; its own checks keep `MP_LS_LEVEL`; `ls level`
  shows `announced=`), `ls testclock <hours>` (A's next start or resync snapshot dates the battle later: a battle in the
  next month or a December battle in January on demand) and `ls testcurspeed <ship> <knots>` (a ship's CurrentSpeed on
  both copies, for the division report check below). docs/TESTING.md lists them.
- **Bug found and fixed: opening a division report moved a ship's speed on one PC only.** The leak check on the second
  player's copy: `LEAK the simulation state changed outside the simulation (pos) between EndOfTurnProcedures and a shared
  section at t=06:32; display routines in between: GlobeRender,GetTypeAndNameForEnemy x64; G8.CurrentSpeed
  1c000000->1b000000` (1900 battle, minute 77, while the torpedo dialog was opened for each division). Cause:
  `TdlgDivisionReport.FillShipList` (0xae1874, called by the report's `Execute` and the torpedo dialog's `bnCloseClick`
  and `bnFireClick`) sets each listed ship's `CurrentSpeed` (+0xa48) to `GetMaxWeatherSpeed` when that is lower
  (0xae1972..0xae1990): a display routine writing a simulation field, so the next minute differs (a DESYNC and a
  resync). It is reachable since 0.3.0 (the division report was never blocked); the torpedo dialog (phase 2) calls it
  twice more per salvo. Fix (`lockstep.cpp`): FillShipList is a display context (`g_ctx`, K_UI) and, in a live battle,
  every ship's CurrentSpeed is saved at its entry and put back at its exit (`ReportSpeedsSave`/`ReportSpeedsRestore`;
  log `the division report lowered <ship>'s current speed a -> b on this PC only: put back (the next minute lowers it on
  both PCs)`). The report still shows the lowered value. Verified in a relayed battle: `ls testcurspeed 18 45` on both
  copies (Patrie 12 -> 45), the second player opened its report: `lowered Patrie's current speed 45 -> 16 on this PC
  only: put back`, 45 on both afterwards, 6 minutes equal, no LEAK; 224 minutes of the 1935 battle with every report
  opened every few minutes: no LEAK, no DESYNC.
- **Phase 0 step 2 (1935 battle `pocbattle 2 0`, `ls autostrike 1`).**
  - Carriers: 130 minutes with every division on the AI and Fastest available on: every minute equal (9..138), no
    DESYNC; `ls airstat` identical on both copies at every 10-minute sample; `aistrike` 0/2 (minute 38) -> 2/2 -> 3/2
    -> 3/5 (128), `ainaval` 3/5, `confirm=0` throughout, `cleared` 5; no "Do you want to adjust final strike target
    location" box; the floatplane questions `answered 7 (not shown)` on both. A second 1935 run: 224 minutes equal,
    `aistrike` 4/6.
  - Fastest available: the second player's posted click put its own button back up (`down=0`), A `want=1`, both chats
    `France turned Fastest available on (from the next minute).`; after the release both `fastest=1 down=1`, speed 5;
    A's click the same; `ls speed 3` (A) and `ls speed 2` (B) turned it off on both (`... set the game speed to 3
    (Fastest available off)`); `ls fastest 1/0/1` from both; `ls resync` with it on: `epoch 2 sent (... fastest 1 ...)`,
    on and down on both afterwards. Speed 5 without contact (minutes 18-48), 2 in contact (from 58) on both.
  - Missile panel (static only, no missile era): `UpdateMissilePanel=012232C8` resolved, `rules=111111`, no call in 1935
    (`missileCalls=0`; the game calls it from 1942).
  - Engine levels: a host announcing 9 (`RTW3MP_TEST_LS_LEVEL=9`): B `battle 100000 refused: the live battle engines
    differ (level 9 there, 10 here): live battles need the same mod version`, its chat and notice; A drops it and the
    battle goes on against the computer. A player announcing 9: host `Captain: mod 0.6.0, live-battle engine 9 (this
    host: 10)`, `battle against Captain: fought against the AI (live-battle engine level 9, the host's 10)`, the player's
    chat `The host fights its battle against Germany against the computer: live battles need the same mod version ...`.
    Two joined players, one announcing 9: `battle 2 vs 1 in Northern Europe: not fought live: live-battle engine levels
    9/10, host 10`, both chats `The battle of Germany against France ... is resolved by the game: live battles need the
    same mod version (...)`.
- **Phase 1, deferred actions (1900 battle, RealismSetting 2 for the run).** `ls lead` from A and B at one boundary:
  both carried out at the release, each giver's chat `Battle: Force lead division changed!`, the other copy's box
  `answered 1 (quiet)`; a second lead change refused at once on both (`The lead of this force was changed once
  already.`); the turn-away cooldown refused at once (`A battle turn-away is possible again in 14 minutes.`); turn-aways
  from both at one boundary carried out; `ls chforce` from both (one rode the next release: its batch arrived after A's
  run); a release-time failure (B's chforce then its lead order for the moved division: `not carried out ...: the
  division is not in that force any more`); a division emptied by three detaches (1935): `ls lead` and `ls turnaway` to
  it `not carried out ...: the division has no ships left`. A turn-away after a lead change is refused (`the divisions'
  lead chain loops`): the game's own SetForceLeadDiv leaves a two-division LeadDivision cycle and DoBattleTurnAway would
  walk it forever (GAME-INTERNALS). Real dialogs through posted messages: Force orders (the ship tree's "Status..." on a
  force) -> Change lead -> the game's question -> Select lead -> OK (second player, 1900) and -> Battle turn away -> Select
  lead -> OK (first player, 1935): ordered, carried out at the next release, quiet on the other copy. *Change force* is
  in no division's popup in either campaign (the nations' tech flag), so only `ls chforce` reached it. `ls resync` with
  orders pending on both: `Not carried out (the battle was synchronised again): ... Give the order again.` on each
  giver, `side 1's computer control as before the load: 0 of 2 forces, 0 of 18 divisions`, side 1 0/18 on the computer
  on both. A drop with the second player's order pending: `Not carried out (the live battle ended): ...`. *Save* by A
  and by B in the host's battle: `battle saved into slot 1 at t=08:37 (ok)`, both chats `Battle saved (minute 7) in the
  host's campaign slot: ...`; in a battle between two joined players `Germany pressed Save: the host keeps battles
  between two players, so nothing is saved.` (and France's). The `force=` group never differed (no false alarm).
- **Phase 1b, auto-run.** Each side on auto-run while the other commands (side 0: `the simulation's PlayerSide is 2
  (auto-run 10)`), each giver's and the other player's chat lines, the giver's button down; back with `off`; a resync
  while side 0 is on auto-run (`(the sides on auto-run under their players)`, `auto-run 10` sent, `side 0 is on
  auto-run: the computer commands it again after the load`); home (`2 forces ordered to the closest friendly base`, the
  forces `withdraw=6 mission=2`, courses changed); the real button (posted click, the game's question answered Yes:
  `auto-run question (hand over) answered 1`); both on auto-run to the game's own end at minute 800 (`No opposing ships
  are in sight of each other and scenario length is reached!`), results "Exit battle" on both, 764 minutes equal; taking
  back command from both-on-auto (`A player took back command: the battle pauses after this minute.`, Fastest and speed
  back at the next release); A on auto-run and `ls drop`: `You command your ships again (auto-run off).`. In a real
  month (war 0-1): the host's battle against the joined player, both on auto-run from minute 3, the game's end at
  minute 800 in 101 s, 800 minutes equal, `British marginal victory Own VP: 72 Enemy VP: 22`; `lockstep_month`'s host
  battle checks all passed (in the campaign, ships and VP equal on host and client, the client's plans kept). Note:
  "off" gives every division and force of that side to its player, also the ones the battle's setup gave the computer
  (destroyer division, coastal batteries), as the first load does for side 1.
- **Phase 2, torpedo salvoes** (Realism 2: *Fire torpedoes* enabled, `button=1`). B's salvo (1900, minute 64):
  `deferred torpedo salvo of side 1 carried out at minute 64 ...: 1 torpedoes in the water (1 -> 0 left, reload 24)`
  on both, B's chat `Torpedo salvo fired: Mecklenburg launched 1 torpedo.`; a duplicate refused (`A torpedo salvo from
  that mount is ordered already; ...`); A's and B's at one boundary (minute 79): side 0's then side 1's; `ls trace 1`
  of that minute, identical on both copies: ENTER shared, `578d6c 578d94 625a46` (A's FireMount and SetReloadTime),
  the same for B's, EXIT shared, then StartMinute; SetReloadTime otherwise only inside MoveAllShips/EOTP/StartMinute.
  `ls mounts` equal on both after each salvo. A resync with a torpedo in the water: `torps=1` before and after on both,
  11 minutes equal. A late order (1935): B's salvo raced A's run: `... (given for minute 180) not carried out at minute
  181: it was meant for minute 180 and arrived too late` on both, B's chat `Not carried out: Torpedo salvo of Patrie
  (...)`. Hits (1935): `minute box 'Enemy ship BB Saint Louis-class is hit by a torpedo!' answered 1` on both copies,
  193 minutes equal through them. The submerged-tube refusal was not reached: no ship in either campaign sails above 25
  knots with loaded submerged tubes (the battlecruisers stay at 24 even alone).
- **The campaign date (engine level 10).** `ls hash` at READY and every hash line carry `date=` equal on both copies
  (1935-01-01, 1900-08-01, ...); `the load: campaign date <the campaign's> kept (the battle save said ...)` on both
  copies after every load (starts, resyncs). Dated in a later month: at the start (1935: `.sac` 1935/3/1 -> kept
  1935-01-01) and through a resync (1900: 1900/9/22 -> kept 1900-08-01). December: a real December 1900 month's live
  battle, resync dated 1901/1/4: `campaign date 1900-12-01 kept (the battle save said 1900-01-04)` on both, the month
  then ran Dec 1900 -> Jan 1901 on host and client. Real months Aug -> Sep -> Oct -> Nov -> Dec -> Jan with a live
  battle each: `the month runs: campaign date ... put back, the record is dropped`, host and client in the same month
  after each. The 0.3.2 VP display issue (the client's enemy VP after a dropped battle) did not happen in one dropped
  real-month battle (`drop12`: VP {0: 512, 1: 1006} in the host's memory, slot 1, the client's memory and file; the
  client showed `own 1006, enemy 512`) nor in the auto-run one.
- **Open:**
  - One unexplained DESYNC (1900, minute 33, while the torpedo dialog of every division of both sides was opened every
    two minutes): `here 856a0541224ad150-27cba77f, Captain 23fa935aa64803da-ca300f77`; pos, sight and the RNG differ
    (sim draws 3554 / 3594), ord equal; only the second player's copy answered `minute box 'Unknown ship sighted!'`;
    no LEAK line. Repaired by the automatic resync; not reproduced in 14 minutes with `ls trace 1` and the same probing,
    nor in 224 minutes of the 1935 battle. Like the 0.3.2 `lockstep_ui` e) DESYNC (also next to "Unknown ship
    sighted!"); a lead: a simulation read of identification that display code wrote (identification is in neither the
    hash's `all` nor the leak check).
  - Minor: a break names the last box answered, also from an earlier minute (`The battle paused (Log lines out of sync!
    29/23).` for every later break after a resync; the game's own box); after a level refusal A's chat offers a rejoin
    that can't succeed.
- **Suite** (`tools/regression.py` on dist_ll_4): **548/558** (1159 s game open; every scenario up to `lockstep` passed, `lockstep_pvp` 24/24, `lockstep_month`
  36/36 with its dropped real-month battle's VP check passing). The 10 fails are 0.3.2-era checks of features this
  branch enabled, for the regression agent to update: `lockstep_ui` a) and e) (and the second f) check) expect the
  missile panel disabled, but *Fire at will* is each side's own since engine level 9 (`cbFireAtWill 1/1`,
  `rgMissileDoctrine 0/1`); c) x2 expect Auto-run blocked with its old chat line, but it asks the game's question now
  (phase 1b); d) x2 expect *Fastest available* blocked, but it is shared (phase 0 step 2: `fastest after the minute
  ('1', '1')`); the first f) found the division reports opened at the start closed: the auto-run question of c) disabled
  the battle screen and the dialog answerer then pressed the reports' Ok (`LS_MODELESS` is skipped only while the screen
  is enabled); `lockstep_air` e) x2 expect the land-CAP button blocked, but it is an order since engine level 6 (the
  button stays down, the next 3 minutes equal).

## Testing 2026-10-17: stage B live proof (v0.6.0 branch)
- Research agent, the branch `live-limits` (a5861e7 + the changes below), engine level 10, then 11. The features of
  phases 3a, 3b, 5a and 5b, proved live with two game copies (three for the battles between joined players) on one PC,
  driven only through the test bridge and posted messages: stage A's interactive driver around `tools/regression.py`
  (its preflight, backups, launch, close and restore; the slot backups kept in the session folder; slot 1 and, for the
  1935 campaign, slot 9 put back by hash after every run; `Game2` never touched; Preferences put back by hash after the
  Realism-2 runs), and kills and restarts of the run's own copies by PID (`ls_kill`, `ls_relaunch`). Builds: frozen
  copies `dist_ll_5`, `dist_ll_6` and `dist_ll_7`.   Game open: ~81 min in four runs plus two full default runs (~36
  min). The drivers, jobs and logs stay in the session folder.
- **Bug found and fixed: the two PCs' simulation started every live battle with different ship identification (engine
  level 11).** The rare DESYNC next to "Unknown ship sighted!" came back in the full default run on dist_ll_6:
  `lockstep_ui` e) `DESYNC at minute 15: here 91ab2f8c68c6405c-5835eb6b, Captain cc474cc979828074-856113e2` (minutes 11
  to 14 equal; at 15 `pos`, `ord` and `rng` differ, sim draws 2450 / 2449). The hash lines' `ident=` group (the
  simulation's identification: `CanonBegin` swaps the simulation set in) differed between the copies in every minute of
  every battle (0 of 90 minutes equal in a 1900 battle, 0 of 26 in that suite battle), and the per-minute dumps (`ls
  trace 1`) showed why: at minute 1, 8 German ships' `IdentifiedAsClass` differed (A 'Victoria Louise', B 'CA Victoria
  Louise'; A 'CL Stettin', B 'Stettin'; ...). Cause: the battle save keeps no identification (`TFightingShip.Create`
  sets `IdentifiedAsType` 0x16, no class; `LoadMe` reads neither), but the battle screen's first drawing during the load
  names enemy ships through `GetTypeAndNameForEnemy` -> `IdentifyType`/`IdentifyClass` with this PC's display draws
  (27 ships on each copy in the 1900 battle, 16 in the 1935 one), and `SetView`'s first swap after the load took those
  values as the simulation's set. The simulation itself identifies ships in the sighting reports routine (unnamed
  0xdc960c, EOTP 0xdaf8dc: `IdentifyClass` 0xdca190, `IdentifyType` 0xdca56e, "Unknown ship sighted!" 0xdca9cf) with
  shared draws whose number depends on the current identification, so a ship identified differently on the two copies
  sooner or later made the shared RNG diverge. Stage A's minute-33 DESYNC (rng first, sim draws 3554 / 3594, the box on
  one copy only) and 0.3.2's `lockstep_ui` e) DESYNC fit it (main has the same capture). **Fix** (`lockstep.cpp`):
  right after every live load (`SetView`) the first capture gives the simulation the identification the battle save
  leaves (the constructor's type, read from `TFightingShip.Create`'s `mov byte ptr [ebx+IdentifiedAsType], imm8`, and
  no class) on both PCs; the display keeps what it drew. Log after every load: `the simulation's ship identification as
  the battle save leaves it (type 22, no class); this PC's display had identified 27 ship(s) during the load and keeps
  that`. `MP_LS_LEVEL` 11 (a level-10 PC keeps its own identification). **Proved on dist_ll_7:** `ls hash` at READY
  `ident=7841827c` on both copies; 100 minutes of the 1900 battle with the torpedo dialogs of both sides opened every 3
  minutes: `all` and `ident` equal in all 100 minutes (45 different identification states: the simulation identified
  ships, alike); after `ls resync` and after a rejoin's load (B killed and back) `ident` equal again and 10 + 10 minutes
  equal; the 1935 battle with the host option on (computer strikes of both sides, air attacks with `IdentifyType`,
  torpedo and bomb hits): 200 minutes, `all` and `ident` equal in every one (52 states), no DESYNC, no LEAK.
- **Test probes added** (read-only or test only, normal play unchanged): `ls airbase` (each formation's home base, field
  or ship, and the bases' AirTorpedoes / AirToSurfaceMissiles), `ls flagsel <division> 0|1` (a division's map flag
  selected as a click on the map does). **Probe fix:** `ls strike` read the rows of the strike dialog hidden before a
  reload, whose items still pointed to the freed formations (garbage AFIds after a resync); it now checks each item
  against the air list. The game never touches those items while the dialog is hidden (a reopening refills it).
  docs/TESTING.md lists the probes, the corrected strike-dialog recipe and new traps.
- **Phase 3a, routine air ops and land CAP (1935 `pocbattle 2 0`, Great Britain vs France).**
  - The real dialog (the ship tree's popup *Search pattern* on a force node, `TdlgSearch` driven by posted messages,
    OK): A on its carrier force `orders #2 (paused, 4 items)` (left 200, right 300, distance 250 and the pattern; its
    first OK had set CAP Maximum, night CAP off, night search on), B on its carrier force `orders #1 (paused, 6 items)`
    (Heavy, 20..100, 350, night search off); both at one boundary: `released orders side0<=#2 side1<=#1: 14 fields`,
    `ls airforce` equal on both. Then `ls airops` from both at one boundary (A: CAP Light, night CAP off, 150..250, 200;
    B: Maximum, night CAP off, 300..40, 400, two-phase on, night search on): `released ... 13 fields`, equal.
  - Recon in the ordered sectors on both copies: the next minute's scouts flew bearing carrier -> target 200, 220, ...,
    300 (Argus) and 20, ..., 100 (Painleve), the sectors' count-downs 180 (two-phase) and 300; identical air lists.
  - Night CAP over `ls resync`: `night CAP as before the load: on for 8 of 11 forces (3 put back)` on both copies,
    night CAP off for the three forces on both afterwards, the other settings and the count-downs kept.
  - Land CAP: B through its division report (the land-CAP button by a posted click, down, OK: `VLlandcap#1 ... France
    Battle Division 8`) and A by `ls landcap 0` at one boundary; before the release `swapped=1` on B only, each copy
    showing its own request; after it both `landcap0=0:GB Battle Division 11 landcap1=4:France Battle Division 8`, the
    airfields' CappedDivision equal. The own-airfield rule where the enemy airfield is nearer, on both copies: `land CAP
    of side 1 over France Battle Division 8: Naval air station Brest covers it; the game alone would have tasked Naval
    air station Plymouth of side 0` (Plymouth 52 nm, Brest 74 nm) and for side 0 `GB Battery 5: Naval air station Grimsby
    covers it; the game alone would have tasked Naval air station Dunkerque of side 1`. A through its report (the game's
    "Request land CAP for this dvision instead of GB Battle Division 11?" Yes), B `ls landcap 25` and `ls landcap -`.
  - The other side's force refused: `ls airops` (`error That force is France's: you can set the routine air operations of
    your own forces only.`, and Great Britain's on B) and the real tool bar button with the other side's map flag
    selected (`ls flagsel`): no dialog, the chat line on each copy. `raw=1`: the edit stayed on that copy until the next
    step and nothing desynced.
  - 140 minutes with every setting equal (1..140), `ls leakcheck 1` clean, no DESYNC.
- **Phase 3b, air strikes (1935).**
  - A's strike through the real dialog (`ls strikedlg <report>`, the rows checked, Launch, "will land at dusk" Yes,
    "coordinate the strike?" No): three formations from the screen (AF1000000..1000002); B's through *Auto select
    strike* (N strike #45..#47, mission 3, one torpedo loadout: Bearn's AirTorpedoes 10 -> 5 on both, "confirm target
    location" cleared); both at one pause with the mission set through the list's own popup (A 'N strike #48'
    AF1000003, B AF1100003/1100004): `3 formations launched at this boundary put into one order (AF1000003 first)`, no
    resync. Each release: the other copy `replayed launch from A<squadron>#0 -> AF<id>`, `ls airlist` identical.
  - The whole cycle on both copies, `ls airlist` identical at every 3-minute sample: readying -> ready -> spotting ->
    spotted -> taking off -> flying -> attacking -> returning -> landing -> striking below -> merged and gone, 8 of 9 by
    minute 366; 330 minutes equal (82..411), then 489 more to the game's end.
  - Ready (B, real dialog): `launch from the screen: A41#0 -> AF1100000 ... ordered state 2`, replayed on A. A
    torpedo-loaded ready formation (`ls airlaunch ... 4 0 2`): Bearn 10 -> 7 on both. Stand down (B, real dialog,
    "Stand down selected squadron(s)?" Yes): `orders #9 (paused, 7 items)` with `VSBearn#0 7 = 11`; after the release
    11 on both (the game's arithmetic: 8 while readying, + LoadedNumber 3).
  - Launch, Ready and Stand down while a minute ran (speed -3, the dialog opened mid-minute): `Pause the battle first:
    aircraft are launched, readied and stood down between minutes.` each; the dialog still open at the boundary,
    `fresh=1`. Ready twice right after the pause, then `ls resync` before A's release: `Not carried out (the battle was
    synchronised again): Ready None #59; Ready None #60. Give the order again.`, `TdlgStrike hidden before the battle is
    loaded again (ok)`, no crash. The game's own StartRun closes the strike dialog (GAME-INTERNALS), so "open across
    minutes" means opened while a minute runs, open at its boundary.
  - The host option: off, every computer strike while both players commanded came from airfields (`ls airbase`:
    Plymouth, Grimsby, Brest; the carriers flew only CAP); on (`ls autostrike 1`, a new session), both chats `Air
    strikes: the computer flies both sides' carrier and airfield strikes (the host's option).`, `auto=1` on both, carrier
    strikes for side 1 (Bearn) and side 0 (Argus) while both players commanded, 81 minutes equal.
- **Phase 5a, rejoin (1900).**
  - B killed in a minute: A `Captain left the live battle (Captain disconnected). The AI commands Germany now; ... If
    Captain joins the session again during this battle, Captain gets their ships back.`, `can be rejoined`; A ran 3
    minutes alone; B started again and joined: the month first, then `rejoin: battle 100000 starts again with Captain
    at minute 7`, epoch 2 (rejoin), B `side 1 ai=0 human=1`; on both: `rj=1 peerAI=0`, no side-1 division or force on
    the computer, `auto1=0`, 5 minutes equal.
  - `leave` and `join` without a restart, the join while A ran alone at speed -2: `rejoin: waiting for the minute
    boundary (a minute runs ...)` x5, `the next minute is held (minute 13)`, live again after 24 s, 5 minutes equal.
  - Another engine level (`RTW3MP_TEST_LS_LEVEL=9`): host `rejoin: Captain (nation 1): live battles need the same mod
    version (...)` and chat `* Captain is back, but the computer keeps commanding Germany in the live battle: ...`, the
    player's chat `* Your live battle against Great Britain goes on without you: the computer keeps commanding Germany
    (...)`; back at level 10 the same battle was rejoined. After the battle's end: `rejoin: Captain (nation 1): no live
    battle of this nation goes on without its player` and nothing else.
  - A real month (Aug 1900, war 0-1): B killed at minute 4 of the host's battle, started again, joined (it loaded the
    battle slot straight from the title screen), live again at minute 4, both on auto-run to minute 420 (416 minutes
    equal after the rejoin); then `DoEndOfTurn finished`, `state seq 2 -> Captain`, the client in Sep 1900; the month
    checks passed.
  - Relayed (three copies, war 1-2, `battles injectpvp`): Bot's PC as A, Captain killed and back: `rejoin: Captain is
    back as nation 1: Bot's PC is asked to start relayed battle 6 again`, live, 5 minutes equal; then Captain's PC as A,
    Bot killed and back (battle 7), the same. `norejoin`: Bot killed again, A alone to the end with its results kept up,
    Bot back: A `rejoin of battle 7 by Bot refused: no live battle was dropped on this PC`, host `lockstep relay 7:
    Captain's PC could not start the battle again: ...`, Bot's chat `* Your live battle could not be started again (no
    live battle was dropped on this PC): the computer keeps commanding your ships.`; the result then merged.
- **Phase 5b, resume (1900).**
  - Test battle: the host killed at minute 3; started again, `load 1 0`: `a battle in progress was resumed from a save`,
    `resume: a test battle outside a month: battle slot mode on`, `resume: campaign date 1901-02-01 kept (the battle save
    said 1901-02-26)`, `resume: battle 100003 (epoch 1) ... resumed at minute 0 ...; held until Captain rejoins or Run is
    pressed`, the chat's and the status line's explanation, the Multiplayer window open; held (the clock stood);
    `host`: `the session takes the month up (processing)`; Captain joined: `rejoin ... (resumed from the host's save,
    held until now)`, a marker with epoch 2, B `the host's game was restarted`; live, 5 minutes equal. A second crash
    after that rejoin resumed again (epoch 2); Run (`pocrun 3`): `resume: Run pressed: battle 100003 goes on against the
    computer`, 3 minutes alone; host and join: live again from minute 3 (released: `the computer commanded it
    meanwhile`); both on auto-run to the end: marker deleted, `battle slot: DoStrategicTurn skipped`, the month sent
    again (the client in Feb 1901).
  - A real month (Feb 1901): the host killed at minute 3 of its battle, started again, `ls autostart 2` + `loadasync 1
    0`: resumed and held (epoch 1, `slotMode=0`), `* Host the session before this battle ends: ...`, the session took the
    month up, Captain joined: live (epoch 2), 5 minutes equal; a second crash: resumed (epoch 2), live again (epoch 3);
    both on auto-run to minute 800, marker deleted, `DoEndOfTurn finished`, the client in Mar 1901, month checks passed.
  - A resume restarts from the last snapshot (the start, a resync, Save or a rejoin's start), not from the minute of
    the crash (the marker's minute 0 after a crash at minute 3).
  - Vanilla saves: the campaign without a battle loads with no resume line (`ls resumable` `marker=none`); a battle saved
    by the game's own Save outside any live battle (no marker), after a restart: only the game's resume (`a battle in
    progress was resumed from a save ...`), not held, Run ran at once.
- **The rare DESYNC next to "Unknown ship sighted!"**: none in the stage B runs on dist_ll_5/6 (about 2,500 live
  minutes of the 1935 battles, the 1900 battles and months, and a 90-minute attempt with stage A's conditions: Realism
  2, the torpedo dialog of every division of both sides opened every 2 minutes, `ls trace 1`), then one in the full
  default run on dist_ll_6 (`lockstep_ui` e): its cause and the fix are the first bullet above. (Before finding it the
  lead was FillShipList's speed write, fixed in stage A; it doesn't explain the identification difference, which the
  trace dumps show in every battle.)
- **Seen (minor):** the norejoin notice gives A's reason in A's words ("no live battle was dropped on this PC"); the
  level notice reads "the same mod version as the host (0.6.0; you have 0.6.0)" when only the test switch differs; the
  end of a resumed test battle logs `battle slot mode ended (no session)` with a session running (the month was sent
  again all the same); the game's own `Log lines out of sync! n/m` box after loads (as in stage A).
- **Suite** (`tools/regression.py`, the full default run). On dist_ll_6 545/558: the `lockstep_ui` e) DESYNC above, and
  `lockstep_air` f), g) (no formation took off in the 57 minutes of that battle: 07:00, every formation unready; the
  battle's conditions, they passed on dist_ll_4 and dist_ll_7). **On dist_ll_7 549/559** (game open 1015 s): the only
  DESYNC is the scenario's own injected one (`lockstep` e)); `lockstep_ui` e)'s 14 AI minutes with the fleets engaged
  equal; in the run's logs `ident` equal in all 94 minutes host/client compared (`all` in 93: the injected one) and 10
  of 10 between the two players' copies in `lockstep_pvp`; the leak checks clean. The 10 fails are the 0.3.2-era checks
  stage A listed, for the regression agent: `lockstep_ui` a), e), f)#2 (missile panel: *Fire at will* is per side since
  level 9), c) x2 (auto-run asks the game's question since 1b), d) x2 (Fastest available shared since 0 step 2), f)#1
  (the reports opened at the start were closed by the dialog answerer while c)'s auto-run question disabled the
  screen); `lockstep_air` e) x2 (land CAP is an order since level 6: the button stays down; 3 minutes equal). No real
  issue.
- **Open.** The identification fix belongs in main as well (0.3.2 takes the same capture: its rare `lockstep_ui` e)
  DESYNC). The default suite has no rejoin or resume case (its `killB` mode starts the killed client again with
  `RTW3MP_LIVE_BATTLES=0`); 5a and 5b are proved by this entry's runs only. The minor points under *Seen*.

## Testing 2026-10-17: regression suite for the live-battle limits, written offline (v0.6.0 branch)
- Regression agent, branch `ll-regress` from `live-limits` c9b74b9 (0.6.0, engine level 11). The game was in use by
  another agent (the missile-era campaign, then stage C), so everything here was written and validated **offline**:
  no copy started, no DLL installed, the game folder, the saves and the Preferences untouched. Formats come from
  `src/mod` and the stage A/B logs; none of the new checks has run live yet.
- **The 10 outdated checks** of the dist_ll_7 run (549/559) now check the branch's behaviour, as precisely as before:
  `lockstep_ui` a)/e)/f) the missile panel (Fire mode disabled, Fire at will enabled and showing each copy's own side,
  `ls missilestat` box = faw[side], agreed = faw); c) Auto-run per player through the game's own question (No: answered
  3, nothing ordered; Yes: one order, at the release `auto-run on for side s ... auto-run 10|01` on both, the side's
  divisions on the AI, the buttons, the chats, `ls simside`; take back: `auto-run 00`); d) Fastest available shared (B
  on, A off: one request on both, the button put back until the release, then `ls fastest`/`pocstate` on both; a speed
  request releases it); f) the land-CAP button is an order (down=1, `VLlandcap#1`, `ls landcap` equal and naming the
  division), Fastest kept over the resync; g) new: the identification line of every live load and `ident=`/`date=`
  equal every minute. `lockstep_air` e) became f) land CAP as an order with the own-airfield rule, plus d) routine air
  ops, e) night CAP over a resync and g) the short strike variant.
- **New scenarios** (`tools/regression.py`; TESTING.md has the full catalog rows): default `lockstep_limits` (engine
  level, deferred orders from A/B/both with the real Force orders and Select lead dialogs, a resync with orders pending
  dated a month later, turn-aways, Save, the realism requirement, the division report's speed (a5861e7), auto-run per
  player, a killed client restarted, both on auto-run to the end, `ident=`/`date=` every minute (c9b74b9)); group
  `rejoin` and `--stress`: `lockstep_rejoin` (leave and join, a return during a minute A runs alone, another engine
  level refused then back, a return after the end, a relayed battle rejoined and `norejoin`), `lockstep_resume` (the
  host killed and resumed, held, hosting again, the rejoin; a second crash, Run releases it; both on auto-run to the
  end; the host at another level refused; the game's own battle save); group `torpedo` and `--stress`:
  `lockstep_torpedo` (B, A, both with `ls trace 1`, the duplicate refused, a resync with torpedoes in the water, a late
  order; the runner sets RealismSetting=2 for the run and puts the Preferences back); group `air` and `--stress`:
  `lockstep_strike` (the real dialog with the mission picked in the list's popup from A, Auto select from B, both in
  one pause, the whole strike cycle, the host option off and on); group `missiles` and `--stress`: `lockstep_missiles`
  (automatic fire, each player's Fire at will, manual salvoes from A/B/both, the panel's own side, a resync with a
  pending heavy-missile target), a SKIP with its reason until a fixture is given (`--missile-fixture <GameN>` or
  `--missile-slot <n>`, `--missile-nations <h>,<k>`). `lockstep_stress` S6 a) expects the rejoin now (the killed
  client gets the live battle back; then a drop and the host's finish).
- **Runner:** `--group`, `--dry-run` (the plan, game time, fixtures, slots, Preferences and preflight; starts, writes
  and deletes nothing), the `--missile-*` options, `EST` per scenario in `--list`; preflight for the Preferences backup,
  the missile slot and the kept-log tags of the new restarts; cleanup of `lockstep_resume`'s copy of slot 1 and the
  trace files. New `tools/regression_selftest.py`: the parsers and log patterns against the bridge's reply formats and
  the proofs' log lines, `--list`/`--dry-run`/the fixture and Preferences helpers in a sandbox, and with `--logs` a
  report over recorded runs.
- **Validation (offline):** `py_compile` and pyflakes clean on both files; a static check of every call of the
  runner's functions against their signatures; `--list`; the self-test 68/68, and 146/146 with `--logs` over the stage
  A/B mod logs (suite7, b2, b3, b4, stage A): every deferred and load-date line parsed, host/client `ident=`/`date=`
  equal in the dist_ll_7 and b4 logs (b3 on dist_ll_6: `ident` differs from minute 1, the bug c9b74b9 fixed), and 106
  of 106 recorded trace minutes with the same draws at the same call sites. The trace check first compared the frame
  markers too: their 24 state-hash bits differed on every marker of every recorded minute (46 per minute in b3, 30 in
  stage A), so `trace_compare` now counts them apart (`hash_diff`); whether they agree since c9b74b9 is a live check.
- **Game time:** the dist_ll_7 default run was 1015 s. Estimated: default about 27 min (`lockstep_limits` ~6,
  `lockstep_ui` ~5, `lockstep_air` ~4), `--stress` about 100 min; groups `rejoin` ~21 min, `air` ~14, `torpedo` ~5,
  `missiles` ~6 (`EST` in the runner; measure and update after the first live runs).
- **Live-only** (to debug when the game is free): every new check's timing and dialogs (the real Force orders/Select
  lead dialogs, the strike list's popup, the auto-run question's radios, the land-CAP button's state), the trace
  markers' hash bits, `lockstep_stress` P2 (its restart with `RTW3MP_LIVE_BATTLES=0` meets the rejoin's refusal now),
  the missile panel's counts on side 1's PC (not updated there by design: visual only), and the missile scenario on a
  real fixture. Plan: `--group limits` first (the default run's new part, ~15 min), then `--only lockstep_rejoin`,
  `--only lockstep_resume`, `--group torpedo`, `--group air`, `--group missiles` with the fixture, each fixed before
  the next; then the full default run and `--stress`.
- **Seen while reading (not fixed, product code):** the level-refusal notice still says "the same mod version as the
  host (0.6.0; you have 0.6.0)" when only the engine level differs; A's drop chat promises "If Captain joins the
  session again during this battle, Captain gets their ships back" also when the player's engine level makes that
  impossible; `ls drop` (a deliberate drop) makes the battle rejoinable like a disconnect; every live test battle
  leaves slot 1 saved with the battle in progress until the next month's save (battle slot mode skips the month), so a
  `load 1 0` before that month resumes the finished test battle as the game's own; `ls torp` reaches the torpedo
  dialog even with *Fire torpedoes* disabled (the bridge only: the realism gate is the button).

## Testing 2026-10-17: stage C live proof (missiles, v0.6.0 branch)
- Research agent, the branch `live-limits` (19b80e0 + the changes below), engine level 11 (unchanged). The missiles of
  phase 0 step 2 (the automatic fire) and phase 4 (each player's *Fire at will*, manual salvoes), proved live with two
  game copies on one PC, driven only through the test bridge and posted messages: stage B's driver (the runner's
  preflight, backups, launch, close and restore; slots 1 and 9 put back by hash after every run; the missile fixture
  installed as slot 48 for a run and deleted after it; `Game2` never touched; Preferences verified unchanged after every
  run). Builds: frozen copies `dist_ll_8` to `dist_ll_11`. Game open: ~65 min in nine runs (c1 to c8) plus the suite's
  `lockstep*` scenarios (741 s). The drivers, jobs and logs stay in the session folder.
- **The missile fixture** (new; kept outside the repository like the 1935 one; docs/TESTING.md "The 1958 missile test
  campaign"): the 1935 test campaign played forward to April 1958 (ResearchSpeed 400), made a host campaign of Italy
  with the mod's own `saveio::MakeNationPlayer` (K = 6: Great Britain becomes AI nation 6) and edited by a script:
  Italy's 14 Mediterranean missile ships active, the Soviet SAM cruiser Chkalov (4 heavy SSM + 2 medium SAM) and 7 more
  Soviet missile ships moved to the Mediterranean, two missile submarines per side, Italy's carriers' air units cut to
  the game's capacity rule. A variant adds "Improved medium SAM" (1970) to both nations, the only way a medium SAM can
  engage a missile (in 1958 no SAM stops one: GAME-INTERNALS "Missiles confirmed live"). The battle: host `load 48 0`,
  the client claims the Soviet Union, `settension 0 3 50`, `pocbattle 3 1 1` (the Cruiser battle in The Mediterranean).
- **Bug found and fixed: the campaign's air units doubled with every load in the same process** (every version since
  0.1.0). The host's month after a live missile battle was refused three times (c3, c4, c6) by the game's Turn check
  "CV Aquila aircraft capacity exceeded!", although the fixture's Aquila fits; the host's campaign saved after c6 held
  `AirUnitNo=9520` = 595 x 16 (every unit 16 times; every other list of the save unchanged). Cause:
  `TfrmBuildCamp.LoadAircraftData` (0xd1d420, called by `LoadBuildCampaign`) adds each `[AirUnits]` entry as a new
  `TAirUnit` to the global list (cell 0xf27a88) without clearing it; the game's own `ClearAirData` (0xd26700) is called
  only by the quick battle, and the game itself loads a campaign only into a fresh process. The mod reloads in the same
  process: the host after merging the players' turns every month, the client every new month, both copies at every
  live battle start, resync and rejoin (c6: the session, the battle, two resyncs and one month = 2^4). Effects: the
  host's Turn stops (`PlayerChecksBeforeTurn` -> `CheckAirBaseCapacity` 0xd20900) as soon as a carrier's air group
  counts twice, the air upkeep runs away (funds "+ -411,149" in c4's month), the save grows. **Fix** (`game.cpp`, the
  existing `LoadBuildCampaign` hook): the list's cell is read from `LoadAircraftData` (`mov eax, [cell]; mov eax, [eax];
  mov edx, ebx; call TList.Add`) and cross-checked against `ClearAirData`'s `mov esi, [cell]` (else a WARNING and
  nothing changes); before a load the mod remembers the units the list holds, after a successful load it removes them
  from the front of the list and frees them (`TObject.Free`; a failed load or a changed front keeps them), then drops
  repeated units of the save (the first of each `AirUnitId` +0x04 stays: it is unique in a campaign the game saved), so
  a campaign saved by an earlier version heals on its first load. Nothing else keeps `TAirUnit` pointers
  (`TAirFormation` names an aircraft type; `TdlgCAG.CopyFromAirUnit` is cleared when the dialog shows, 0xb63c08). Logs:
  `air units: <n> from before the load dropped; the campaign's <m> kept` and `air units: <k> repeated units of the save
  dropped (<a> -> <b>; a save of an earlier version)`; bridge `airunits` -> `ok count=<n>`. **Proved on dist_ll_11:**
  c6's save loaded -> `8925 repeated units of the save dropped (9520 -> 595 ...)`, loaded again: 595; c7: 595 on host
  and client at the session, after the battle load, after a resync with 9 missiles in the air and after the end; the
  month ran (April -> May 1958 in 20 s, no capacity box) and six more to November 1958 (634, later 619 units: the game's
  own changes, the same on both copies, never doubled); c8: two battles in one session, 595 throughout. The fix doesn't
  touch the battle simulation (a live battle's air groups come from A's battle save; every minute's hashes stayed
  equal), so `MP_LS_LEVEL` stays 11. Main (0.3.2) has the same load path: a candidate for a 0.3.x hotfix.
- **Test probes added** (test only or read-only, normal play unchanged): `pocbattle <opp> <mission> [map area index]`
  (for nations that share no home area: Italy and the Soviet Union), `ls testpmt <all-ships index>` (A's next start or
  resync message carries a pending missile target for that ship: a real one is consumed within a minute, see 3.), `ls
  missilestat` gains `intercepted shotDown= bySAM= decoyed=` per defending side (the target ships' `myHits` lines) and
  `nations` (the battle's nation records: medium SAM, the anti-missile flag +0x90, the fire-at-will gate value), `ls
  ctl` shows the control's hint, `airunits`. docs/TESTING.md lists them.
- **1. Automatic fire (phase 0 step 2): PASS.** c3 (realistic fixture, a night battle from 03:04): the missile
  submarines fired first (Italy's at minute ~15, the Soviet one at ~170), every division to the AI at minute 170, the
  ships' exchange from minute 372: Italy 44 -> 6 heavy SSM, the Soviet Union 14 -> 3, up to 18 in the air, hits on both
  sides (Fiume hit; Razyaryonnyy, Obraztsovyy and a merchant sunk); `ls missilestat` equal at each of 40 sampled
  minutes, minutes 1 to 400 equal, no LEAK, no DESYNC; no SAM fired at a missile (1958). c6 (SAM variant, by day): 99
  minutes, 75 with missiles flying, Italy 40 -> 0, the Soviet Union 24 -> 0, up to 25 in the air, `intercepted
  shotDown=2/1 bySAM=2/0` (Italy's SAMs shot down 2 Soviet missiles, its medium SAM stock 42 -> 38; a Soviet ship's own
  defences 1), equal on both at every minute. c8's second battle (Chkalov drawn): 140 minutes, the Soviet SAMs fired 2
  at the Italian salvo and missed (42 -> 40), one Italian missile decoyed (`decoyed=0/1`), equal. The panel shows the
  own side only: hidden on every new battle screen; A's comes back by itself when its ships have targets and its counts
  update; B's counts are not updated (`the missile panel's counts are not updated on this PC (side 1)`), as designed;
  *Fire mode* disabled (showing Ripple) with its hint on both copies (`Fire mode: in a live battle the computer picks
  the salvo size of both sides' automatic fire. Manual fire chooses one, half or all.`): the second player's salvo size
  stays the computer's; *Fire at will* enabled with its hint. The dialog lists the own ships as shooters and the targets
  as the sighting knows them ("Unidentified ship", "CA", "CL").
- **2. Each player's Fire at will (phase 4): PASS.** The real check box (BN_CLICKED to the panel), c3: A off -> only
  A's box unchecked, B's still checked; the order released at the next boundary, both copies `automatic missile fire of
  side 0: held (its Fire at will is off) (4 division(s) held)`, `faw=01 agreed=01` on both; B off -> `faw=00` on both (5
  held); both back on in one pause -> one release with 2 fields, `at will (0 division(s) held)` on both; no automatic
  SSM fire of a held side (c4, 87 minutes); every minute equal. A click while a minute runs is this player's order for
  the minute after the next boundary, like a course (heldCalls show it; docs/TESTING.md said "refused": corrected); it
  is refused only while the battle can't take orders. A side on auto-run is never held (`at will (0 division(s) held)`
  with the box off; c8: one minute on auto-run, Italy fired 12 heavy SSM, equal); a click during auto-run is kept for
  later. The battle's two nation records have the gate value 0, so the game's exception (a computer force fires whatever
  the box says) applies to every computer-controlled force, as in single player.
- **3. Manual salvoes (phase 4): PASS.** Through the real dialog (`TdlgMissileFire`, opened by a posted click on
  *Manual fire*), c4: A *Fire one* on Fiume -> `missile salvo ordered (#4): Fiume mount 2 (type 4) at Chkalov, bearing
  46, 1 of 1 to fire`, A's chat only; at the next minute on both `deferred missile salvo of side 0 carried out ...: 1
  fired, 1 missiles in the air`, and the dialog's pending target fired 3 more (heavy SSM 40 -> 36, air 4 on both); B
  *Fire half* on Chkalov ("4 missiles fired!"): two orders (mounts 0 and 1), carried out at minute 90 on both, B's chats
  only; both players in one pause, twice (pending targets of both; a pending target and a deferred salvo): one release
  each, equal. The pending target of a launcher that can't bear (the launchers are fixed before 1959): c3, "0 missiles
  fired! 1 ships with mounts not bearing will turn to fire SSM." -> `pend=1` on A, released at the next boundary, fired
  and cleared by the simulation on both. Over a resync: the simulation consumes a pending target in the ship's next gun
  step (it fires up to 4 per launcher and clears it), so a real one never lasted to a boundary; with `ls testpmt` (c6):
  `1 pending missile target(s) as before the load` on both, pending 1 on both, consumed in the next minute on both,
  equal. Refusals: *Manual fire* while a minute runs (B, c8): no dialog, `Pausing the battle: open Manual fire again
  when it is paused (missiles are fired between minutes).`, both copies stopped at the next boundary; on auto-run (A,
  c8): `Your ships are on auto-run: the computer fires their missiles.`; *Fire* in a dialog left open while A ran a
  minute (B, c4): `busy=1`, A waited, "Pause the battle first: missiles are fired between minutes.", no crash, B caught
  up after *Close*. Across resyncs (c4): B's dialog open with an order -> B loaded after *Close*, its chat "Not carried
  out (the battle was synchronised again): Missile salvo of Obraztsovyy. Give the order again."; A's own dialog open ->
  A's resync waited for *Close*; Fire at will kept. Missile submarines (B's `SSG Shch26` checked with a cruiser):
  unchecked before the fire, the chat line, the cruiser's salvo carried out. The bridge path (c8, B): `ls missiles 5 3
  1` -> `queued #4 (1)`, carried out at minute 80 on both.
- **4. Robustness: PASS.** Missile exchanges of 99 minutes (c6) and 140 minutes (c8, 70 with combat) with SAM
  interceptions (side 0 shot down 2 by SAM; side 1's SAMs fired and missed, one missile decoyed: the shots are random,
  no side-1 SAM kill was seen); a resync with missiles in the air (c4: 2 at minute 108, the same before and after on
  both, 5 minutes equal; c7: 9); a rejoin with a missile in the air (c4, minute 117: B killed, `can be rejoined`, B
  started again and rejoined, `Fire at will per side 11`, air 0/1 on both, 37 minutes equal); auto-run of a missile side
  (2. above; every battle ended on both sides' auto-run: c3 at minute 436, c4 429, c6 and c8 600, 28 to 519 minutes
  equal to the end); the result in the campaign and the month (c7, after the air-unit fix): the month ran, the VP (Italy
  2071, the Soviet Union 827: `victory points: nation 3 +827 in its battle against the host`) and Italy's damaged ships'
  RepairTime equal in the host's slot 48 and the client's slot 77, then six more months. All runs: every compared
  minute equal, `ls leakcheck 1` clean on every copy, no DESYNC.
- **5. Suite** (`tools/regression.py --only lockstep,lockstep_ui,lockstep_pvp,lockstep_month,lockstep_air`, run once on
  dist_ll_11; game open 741 s): **133/143**. `session`, `lockstep`, `lockstep_pvp` and `lockstep_month` all PASS (the
  only DESYNC is `lockstep` e)'s injected one; the leak checks clean). The 10 fails are exactly the 0.3.2-era checks
  stages A and B listed, for the regression agent: `lockstep_ui` a), e), f)#2 (the missile panel: *Fire at will* is per
  side since level 9), c) x2 (auto-run asks the game's question since 1b), d) x2 (Fastest available shared since 0 step
  2), f)#1 (the reports opened at the start were closed by the dialog answerer while c)'s auto-run question disabled the
  screen); `lockstep_air` e) x2 (land CAP is an order since level 6: the button stays down; 3 minutes equal). No real
  issue. The 1935 campaign's battles (`lockstep_air`, carriers and strikes) logged `air units: 232 from before the load
  dropped; the campaign's 232 kept` at every load on both copies.
- **Seen (minor, not missiles):** B's results box shows side 0's view ("Italian major victory ... Gain one prestige
  ... Own VP: 2,722" on the Soviet copy), and some of B's pause reasons are side 0's texts; A's chat shows the game's
  questions about B's divisions ("Do you want SU ... to enter port"); `battle slot mode ended (no session)` at a test
  battle's end with a session running (stage B). A test battle in the planning phase changes the joined nation's ships
  on the host only: the player's turn (merged after it) puts back `[Nation3Ships]` (real battles run after the merge).
  Joined players' air units are never merged (the global `[AirUnits]`).
- **Open.** No real month generated a live battle in seven months of war (c7): every battle here was `pocbattle` (the
  debug Test battle = `DoBattle`, the start of the host's real battles against a joined player). A side-1 SAM kill was
  not seen (attempts only). The air-unit fix belongs in main too. Missiles can ship enabled in 0.6.0: no missile bug was
  found, and every hash, leak check and missile count agreed.

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

## Fix 2026-10-17: minor live-battle fixes before release (v0.6.0)
Research agent, branch `ll-polish` (from live-limits 72a3ef7). Twelve small points from the stage A-C proofs, each fixed
or explained. Static work and clean builds (`dist_ll_12`; `dist_ll_13` adds the chat hook of item 11); the live checks
wait for the game (listed at the end). Engine level `MP_LS_LEVEL` 12: items 6 and 7 change what the simulation does.
- **1. "Log lines out of sync! n/m" after every live load.** The EOTP message routine (0xd8fd5c) adds each message to
  `TfrmMain.TotalLogLines` (+0x7bc) and its "dd hh:nn " to `TotalTimeStamps` (+0x7c0); after every batch it compares the
  two counts and on a difference shows the box (0xd90dd5, `ShowMessagePos`; it sets no break) and deletes the longer
  list's last entries. The battle save (0xdd1010) writes the lists as `TLog.log`/`TTime.log` with `TStrings.SaveToFile`,
  the load (0xdde0a0) reads them with `LoadFromFile`, which splits an entry with a line break: the batteries'
  "<division> is out of sight of the\nflagship ..." came back as two lines (stage B: 3 battery boxes, then `Log lines
  out of sync! 18/15`). **Fix:** A's snapshot writes such entries as one line (`JoinLogLineBreaks` in `SaveSnapshot`,
  log `battle saved into slot <n> ... (<k> battle log entries with line breaks written as one line)`), every live load
  and the host's resume align the two lists for an older save (`AlignLogLists`: the time stamps get the last one again
  or lose their extra ones; `the battle log after the load had <n> lines and <m> time stamps: aligned`), and a box that
  still comes is answered without a chat line. Display data only (nothing in the simulation reads them). **The stale
  reason:** the box never paused anything; later breaks named it because a break named `g_lastBox`, the last box of any
  minute (stage A: every break after a resync read `The battle paused (Log lines out of sync! 29/23).`). Now a break
  names only a text of its own minute (`g_minuteText`, cleared when a minute starts; picture messages count too), else
  "an event".
- **2. The level-refusal notices** said "the same mod version as the host (0.6.0; you have 0.6.0)" when only the level
  differed (a test switch or another build). Now (`LevelWhy` in session.cpp, for the player, the host or a third
  player): both mod versions and both levels when the versions differ, else "your mod plays live battles at level 10,
  the host's at level 12 (both mod 0.6.0, but different builds): live battles need the same level on both PCs". The PC
  that refuses a start (B) and A at the ready check say "<name>'s mod plays live battles at level <n>, this PC's at
  level <m> (...)"; B's refusal carries its level (`lv=`) back to A.
- **3. A's drop chat** promised "If <name> joins the session again during this battle, <name> gets their ships back."
  also when no rejoin could work. `DoDrop` now remembers the battle first (`RememberRejoin`) and says what will happen:
  the promise only when the battle is remembered and the player's level is A's; "The AI commands <side> to the end of
  this battle; it goes on here." when it could not be remembered; "<name> can take their ships back only with a mod that
  plays live battles at level <A's> (theirs: <n>)." after a drop for another level.
- **4. The `norejoin` notice** gave A's reason in A's words ("no live battle was dropped on this PC"). A's PC now sends
  the reason in the returning player's words as well (`why2=`: "<first player>'s game has no battle of yours going on
  without you", "... closed that battle's screen", "... is in another battle now", ...); the host's notice uses it.
- **5. `battle slot mode ended (no session)`** came with a session running: on the host the line ends the slot mode of
  its test battle outside a month, which is right; the wording now says so (`... (the host's test battle outside a month
  is over)`; without a session `... (no session: the battle that outlived it is over)`).
- **6. Auto-run off freed the divisions that started under the computer.** Single player's off (`sbAutoRunBattleClick`
  0xe02824, read again): `AutoRunBattle` := 0, `PauseGame` := 1, and for every force that is not `AIControlled` (+0x79)
  its flag division gets `AIControlled` := 0; every other division stays with the computer (its on had set every
  division of both sides to 1; force flags untouched). The live auto-run also sets the side's forces to the computer
  (the simulation's computer paths of one side), and its off gave every force and division back. **Fix** (level 12):
  both PCs record the side's forces' own flags at the on (`preAutoF`), and the off gives the forces those back, the flag
  division of each force that is not computer-controlled to the player, and leaves the other divisions with the
  computer, as single player (`side <s> back to its player as in single player: the lead divisions of <n> forces; <k>
  forces stay with the computer as before auto-run; <d> divisions stay with the computer`). The chat says it: "... you
  get each force's lead division back (<n>); the computer keeps its other divisions until you clear "AI controlled" in
  their division reports." (an order of the guard, as in any battle). The record is session state (`pa=<side 0>/<side
  1>` in the start message; a side whose record is unknown, the game's own auto-run of side 0 at a rejoin, takes its
  loaded flags); the snapshot writes an auto-run side's forces as they were before it (the host's crash checkpoint). A's
  hand back when the live battle ends on auto-run does the same.
- **7. Torpedo salvoes on auto-run** were accepted, missile salvoes refused. Both are refused now with one notice, "Your
  ships are on auto-run: the computer fires their torpedoes and missiles.": the division report's *Fire torpedoes* and
  the torpedo dialog's *Fire* (`B_TORPS`), the fire routine's hook (`TorpOrder`), *Manual fire* and its buttons, from
  the player's auto-run order on (`EffectiveAuto` counts the pending order). The release carries each player's orders
  out in the order given: a salvo ordered before the auto-run order (in the same pause too) fires, as single player
  would have fired it at once; a `torp` (level 12) or `msl` of a side already on auto-run there is skipped ("the side is
  on auto-run"). (Corrected after the branch regression: this said such a salvo was dropped.)
- **8. *Fire mode* stays locked for both players, by design.** The salvo routine (0xda7ccc) reads
  `rgMissileDoctrine.ItemIndex` (+0x2e0) only for ships whose side is PlayerSide (40 % chance to keep firing, +25 with
  *Half* and more than 2 heavy SSM left, +55 with *All*; the other side gets +10 against a large target). The live
  automatic fire runs with PlayerSide 2, so both sides get the computer's rule; A's choice alone would give the host a
  control the second player lacks, and a fair per-side *Fire mode* (a hook on 0xda7ccc with synced per-side state) is a
  new simulation feature, not a fix. The hint and the start chat now say why: "Fire mode: the game applies it to one
  side only, so in a live battle the computer sets the salvo size of both sides' automatic fire alike. Manual fire
  chooses one, half or all."
- **9. "Computer flies carrier strikes" on:** the strike dialog still launches, which causes no problem (the computer's
  `CheckAIAirstrike` makes its own formations from ready squadrons; the players' launches are side-partitioned immediate
  actions). Kept; the start chat says it ("... (the host's option); "Order an airstrike" still launches your own strikes
  while the battle is paused, besides the computer's.") and the first opening of the dialog in a battle too.
- **10. B's result box** showed the host's side. The box is the campaign's `tmpBattleString` (+0x4abe), written by the
  end of the battle's `HandleBattleAftermath` (0xcc0844, called at 0xdc6017 inside the end routine, also on B) and shown
  by `TfrmMain.FormClose` (0xdcd802); nothing else reads it. On B at the normal end the mod rewrites it for B's side:
  the victory line from B's side (the host's adjective -> "Enemy", "Enemy" -> B's nation's `Name2`), the points swapped,
  the host's prestige and morale lines left out (`the result box shows this player's side: '...' (the game's: '...')`).
  The simulation and the host's campaign are untouched.
- **11. B's pause reasons (fog of war).** Leaks found: B read side 0's texts as pause reasons, e.g. "B Collingwood has
  more than 50% flotation damage!", "BB Saint Louis has less than 50% of main ammo remaining!", "Our ship CA Fiume is
  hit by a missile!", side 0's sightings ("Unknown ship sighted!") and questions naming side 0's ships. The reverse leak
  was in A's `Battle: ...` chat: the second side's own messages ("<division> is out of sight of the flagship ...",
  "<ship> reports a sonar contact", "Enemy targeting radar is locking on to <its ship>", its enter-port questions) and
  its sightings: with both sides human the sighting box repeats for side 1 until side 0 sights (`FirstSighting` +0x800
  per side, the check uses PlayerSide 0). **Fix:** each line of a game text goes only to the player it is about
  (`GameTextFor`): a sighting box to the side of the division that sighted (the hook now gets the stack and EBP:
  `ShowMessagePos` has no frame, and its caller reads `[ebp-20h].Side` right after the box; log `minute box '...'
  answered 1 (side <s>'s sighting)`), "Enemy ship ..." lines to side 0, other lines by the real names they contain (one
  side's divisions, else ships or air formations; "<name>-class" is an identification, not a name), the battle as a
  whole to both, the rest to A as before. A's chat shows only A's lines (else `the game's text is the other player's
  (not shown here)` in the log); a break names, for each player, the last text of that minute about their side, else
  "The battle paused (an event)." The game's own battle log panel still lists every message on both PCs (known limit,
  README). **A third leak path (found in review):** on the host every live-battle chat line went to all players
  (lockstep's chat hook was the session's `AddChat`, which on the host sends each "* " line to everyone): in the host's
  own battle B read A's `Battle: ...` lines and pause reasons, and its own `ChatBoth` lines twice, and third players
  read the host's battle. The hook is now `AddLocalChat`: each PC's live-battle lines stay on it, and B gets its own
  from the battle (`ChatBoth`, its pause line), as it always did in a battle between two joined players.
- **12. `ls torp`** called the division report's *Fire torpedoes* handler even when the button was disabled (Realism
  below 2). It now refuses like the button: `error the division report's Fire torpedoes is disabled (button=0: ...)`.
- **Docs:** ARCHITECTURE (auto-run off, the game's texts, the result box, rejoin and level notices, *Fire mode*,
  autostrike, level 12), GAME-INTERNALS (the message routine and the log lists, the sighting and missile-hit texts,
  `ShowMessagePos`, the result box, *Fire mode*), TESTING (log lines, chat texts, `ls torp`, the `pa=` key, traps).
- **To verify live** (once the game is free; 1900, 1935 and 1958 fixtures): the box gone after a start, a resync and a
  rejoin with battery messages in the log (TLog.log one line per entry; no `Log lines out of sync` line); the pause
  lines per player in a minute with a side-1 event and a side-0 sighting; Auto-run off on A and on B (`ls forces div`
  flags as single player, forces as before, equal hashes, `pa=` over a resync while on auto-run); a torpedo dialog and a
  torpedo salvo refused on auto-run and a salvo given before an auto-run order in the same pause fired at the release
  ahead of it; the level notices with
  `RTW3MP_TEST_LS_LEVEL`; the drop chat for a level drop; B's result box; the strike dialog's note with autostrike on;
  `ls torp` with Realism 1; the battle slot wording at a test battle's close; the client's session chat (the
  Multiplayer window) without the host's live-battle lines; the suite's `lockstep*` scenarios.

## Testing 2026-10-17: the live-battle limits suite, first live runs and engine level 12 (v0.6.0 branch)
- Regression agent, branch `ll-regress`: `live-limits` 72a3ef7 merged (8f1892f, stage C and the air-units fix), then
  `ll-polish` 6347cc0/20b212a (a320fcf, engine level 12). Live runs on the snapshot dist_ll_11 (72a3ef7, level 11)
  until the caller's pause; the level 12 changes were adapted as code only (no game).
- **New and wired** (1270cbf): `lockstep_missiles` on the 1958 missile test campaign (`--missile-fixture
  <folder>\Game48`: slot 48 for the run, deleted after; defaults nations 0,3, mission 1, `--missile-area 1`): the
  start chat, the panel's hints through `ls ctl`, automatic fire with the interceptions of `ls missilestat`, each
  player's Fire at will with the divisions handed back first (a computer force fires whatever the box says), a Fire at
  will click in a running minute taking effect at the minute after, the refusal on auto-run, and the resync with a
  pending heavy-missile target through `ls testpmt`. New `air_units` (default run; groups `airunits` and `air`): on the
  1935 campaign `airunits` equals the saves' [AirUnits] count on both copies after the session start, two months, a
  live battle's start and a resync, every load logging `air units: <n> from before the load dropped; the campaign's <m>
  kept`; a bloated copy (every unit twice, slot 47) heals on its first load (`... repeated units of the save dropped
  (2k -> k; ...)`) and when saved.
- **Live runs (dist_ll_11)**, game open 600 + 659 + 845 + 476 + ~780 s (about 56 min): `--group limits` 104/114 and
  118/128 (`lockstep_air` 34/34 both times, 239-245 s; `lockstep_ui` 233-255 s; `lockstep_limits` 166 s);
  `--only lockstep_rejoin` 42/44 (831 s); `--only lockstep_resume` 23/25 to its part e) (460 s), then a run through f)
  stopped in g) for the pause. Every fail but two was the suite's: fixed in c6cef99 (auto-run off compares the AI
  counts of the copies; no second Fastest release check, the contact rule caps the speed; limits b) moves a division in
  when no force has room, "No eligible divisions in force!" a NOTE; the host's results closed by `ls_finish_host`;
  Stand down waits up to 90 minutes; rejoin a) waits for the leave lines; rejoin e) keeps A's results up during B's
  restart), 9d8eaf5 (`run_alone_until` presses `pocrun` again after the game's boxes; B's accept says "(the host's game
  was restarted)" only when the battle was held) and ea9baa8 (`ls_relaunch` gives the host its extra environment:
  resume g) restarted the host without `RTW3MP_TEST_LS_LEVEL`, the battle started normally and the part waited for a
  refusal that never came). Not yet run live: `lockstep_torpedo`, `air_units`, `lockstep_strike`, `lockstep_missiles`,
  resume h)-i).
- **Engine level 12 in the checks** (ea9baa8 and this entry's commit): Auto-run off as single player (`side s back to
  its player as in single player: the lead divisions of <n> forces; ...` alike on both copies and the player's chat);
  `AUTO_FIRE` for torpedoes and missiles: `lockstep_torpedo` g) refuses B's salvo with auto-run carried out and while it
  is only ordered, and checks that a salvo ordered before the auto-run order in the same pause fires first, alike on
  both; Fire mode's hint and the missile start chat in full; the strike chats and the strike dialog's note (once a
  battle) with "Computer flies carrier strikes" on; level notices with both levels (rejoin c), resume g)); `ls torp`
  refused at Realism 1; B's result box for its own side (`result_box_side`: the victory from B's side, the points
  swapped); no "Log lines out of sync!" box named after the live loads (limits k)); the drop chat promising the ships
  back exactly when `ls rejoinable` says a rejoin can work (every `ls_drop_checks`); `norejoin`'s reason in B's words.
  Self-test 75/75.
- **Product issues seen** (reported, not fixed): (1) `lockstep_ui` e) on dist_ll_11: a DESYNC at minute 32 with every
  division on the AI and the fleets apart; the `pos`/`ord` hashes and the draw counts differ (2562 vs 2524) with nothing
  logged between; the resync recovered. (2) `lockstep_limits` c): across a resync with orders pending side 0's AI flags
  changed alike on both copies (6 -> 7 of 7 divisions on the AI): the snapshot carries side 1's flags only (`ai1`), so a
  resync can hand the host player's own divisions to the computer; to re-check on level 12. (3) The release's skip "the
  side is on auto-run" for `torp`/`msl` is only a safety net: a salvo ordered before the auto-run order in the same pause
  fires first (the release keeps the order given), and from the auto-run order on the button refuses
  (`EffectiveAuto`); ll-polish's "to verify live" list expects that salvo skipped. `lockstep_torpedo` g) checks that it
  fires (as in single player, where it would have fired at once): say if it should be skipped.
- **Gotchas:** to pause a run, stop the runner's `python` before its game copies (later parts start new ones), then
  restore with the runner's helpers (TESTING.md, Known traps). A Python `open(p, 'w', newline=<invalid>)` truncates the
  file before it fails: the suite was rebuilt from git and a saved diff; write `<file>.new` and `os.replace`.
- **Plan for the live runs on the level 12 build** (dist_ll_13): `--group limits`, `--only lockstep_rejoin`, `--only
  lockstep_resume`, `--group torpedo`, `--group air` (`air_units`, `lockstep_strike`), `--group missiles
  --missile-fixture <folder>\Game48`, each fixed before the next; then the full default run and `--stress` on the
  final build.

## Fix 2026-10-17: the branch regression's three findings, engine level 13 (v0.6.0)
Research agent, branch `ll-polish` (after 20b212a). Three points the branch regression found live on dist_ll_11
(level 11). Static work and a clean build (`dist_ll_14`); the live checks wait for the game (listed at the end). Engine
level 13: item 2 changes the state after a load.
- **1. The `lockstep_ui` e) DESYNC at minute 32** (every division on the AI by orders, the fleets apart, speed 3; the
  `pos` and `ord` hashes and the shared draw counts differ, A 2562, B 2524; nothing logged in between; the resync
  repaired it). **The cause is not in the logs:** the run had no `ls trace`, and the client's log of that part is gone
  (the runner restarted the client in `lockstep_limits`, and a later run replaced its `.prev.log`). A's log has minutes
  19..31 equal on both copies, `rng` included, so the copies parted inside minute 32: one routine took another path on
  one copy (38 shared draws more on A) from an input the state hash doesn't cover. Ruled out by reading the code: the
  polish round's code (not in dist_ll_11; it reads lists and names and draws nothing); a clock (no simulation routine
  reaches GetTickCount, QueryPerformanceCounter or GetLocalTime); messages handled inside the simulation (no
  ProcessMessages under it); `ManeuverOK` (+0x745), which the minute timer's 0xde1df0 recomputes outside the
  simulation from `FirstSighting[PlayerSide]` with each copy's display side, but whose only simulation reader skips
  only divisions that are not on the AI and is called for AI divisions only (GAME-INTERNALS). The same family as the
  0.3.2-era e) DESYNCs (minutes 22 and 33). **Now every DESYNC documents itself:** each copy keeps the call sites of
  its shared draws of its last 4 minutes and the bytes of the battle's objects (the battle screen, forces, divisions,
  ships, air formations, torpedoes, submarines; the simulation's set) of its last two boundaries. On a DESYNC A writes
  `ls_desync_A_<battle>_<epoch>_<minute>.txt` into the RTW3MP data folder and sends B a `desync` message ahead of the
  resync; B writes its own (`lockstep: DESYNC report <file> written (<n> kB; ...)`; the newest 12 files stay). Both
  stand at that boundary: A releases no minute before B's report of the last one is verified. `tools/desync_diff.py
  <A> <B>` prints where the two call-site lists part (the routine that drew on one copy only), the fields that differ
  in the bytes of the boundary before (RTTI names, heap pointers counted apart) and the hashed state lines that differ.
  It reads only (no draw, no write); `ls desyncreport` writes one on demand. The targeted repro is in the live list.
  (Found live afterwards with these reports: `TFightingShip.LightState`, the next entry.)
- **2. Side 0's computer control over a reload.** `lockstep_limits` c): after a resync with orders pending, the host's
  divisions on the AI went from 6 to 7 of 7, alike on both copies. The game's `SetDivisionAIControl` (0xda9f75) gives
  every PlayerSide division of a computer-controlled force to the computer on every load, also one the host's player
  had taken back, although the `.sac` keeps each division's flag; the start message carried side 1's flags only
  (`ai1=`). **Fix:** it also carries `ai0=<forces>/<divisions>` (A's agreed flags: pending orders taken out first), and
  both copies put them back after every load (the first and a rejoin's too, where they equal the battle save's), before
  a side on auto-run goes to the computer again: `side 0's computer control as before the load: <f> of <F> forces,
  <d> of <D> divisions`. With level 12's Auto-run off nothing else changes: a side on auto-run is all the computer's in
  `ai0=`/`ai1=` and again after the load, and `pa=` keeps its forces' control from before auto-run. Protocol 3.
- **3. A salvo ordered before auto-run in the same pause fires** (decision: the order given is the rule; single player
  would have fired it at once). The code did so already: the release runs each side's batches in sequence and each
  batch's items in the order given, and the `torp`/`msl` skip reads `S.autoRun` as the release has it at that item; a
  salvo after the player's auto-run order is refused at the buttons (`EffectiveAuto`), and one that still arrives is
  skipped ("the side is on auto-run"). The polish entry (item 7, its live list) and TESTING said such a salvo was
  dropped: corrected; a comment at the skip.
- **Docs:** ARCHITECTURE (`ai0=`, the DESYNC reports), GAME-INTERNALS (the load's PlayerSide rule, the minute timer,
  `ManeuverOK` and `NoOrdersBS`), TESTING (`ls desyncreport`, `desync_diff.py`, `ai0=`, the salvo rule).
- **To verify live** (dist_ll_14, after the polish list above): `ls desync` on B: both reports written, `desync_diff.py`
  names the corrupted division's `DivisionCourse` in the boundary's bytes and in the state lines; a resync and a rejoin
  with a side-0 division of a computer force taken back (`ls order <i> -1 -1 0`): `ls forces div` flags equal before and
  after on both copies and the `side 0's computer control` line, also with side 0 on auto-run and after Auto-run off;
  a torpedo salvo then Auto-run in one pause on B: the salvo fires at the release, then auto-run, alike on both; the
  e) repro: the 1900 test battle with the division reports open on both, both sides' Auto-run on and off, Fastest on
  and off, every division on the AI, speed 3, 14-minute runs (with `ls leakcheck 1`) until a DESYNC or 150 minutes,
  then the two reports through `desync_diff.py`; the suite's `lockstep*` scenarios once.

## Fix 2026-10-17: dawn and dusk DESYNCs (LightState), engine level 14 (v0.6.0)
Research agent, branch `ll-polish`. Found live on dist_ll_14 with the DESYNC reports of the entry before; fixed in
dist_ll_15 and checked live (the next entry).
- **The DESYNC.** The targeted `lockstep_ui` e) repro (division reports open on both copies, both sides' Auto-run on and
  off, Fastest on and off, every division on the AI, speed 3, 14-minute runs) DESYNCed at minute 291 of an October
  battle (16:48; the fleets engaged). Both copies wrote their reports; `tools/desync_diff.py`: minutes 288-290 the same
  call sites, minute 291 A 4036 shared draws, B 3990, parting at draw 171: A drew at 0xdabbc3 (and later 0xdaf083), B
  went on with the next routine; the raw boundary before showed no field difference.
- **The cause.** 0xdabbc3 is the return of a Random(100) in StartMinute's division routine 0xdab764: for a division
  with `Role` 0 and `TacticalStance` 1 whose flagship's `TFightingShip.LightState` (+0xb02, not hashed) is 3. The
  sighting-range routine 0xe596ac writes `LightState` (from the place and the battle clock) before it reads it; the
  simulation calls it inside the minute, and the display calls it between minutes: the map's sighting circles of the
  PC's own divisions (0xd8d084 under GlobeRender, only while `Intervals` is 0) and the division report. So after a
  boundary where the light changed, the PC whose map had drawn that division started the minute with the new value,
  the other with the simulation's older one. The boundary's bytes can't show it (the display wrote after them). Test
  battles start at 05:00 on many runs: the `lockstep_ui` e) DESYNCs (minute 32 at 05:41 on dist_ll_11, minutes 22 and
  33 in 0.3.2-era runs) sit at dawn.
- **Fix (engine level 14):** `LightState` joins the guard's dual fields (`g_guard`: the simulation keeps its value, the
  display its own, the display adopts the simulation's changes) and the state hash (`sight=`). At a light change a copy
  now logs `the display wrote 1 sighting fields since the last boundary (kept out of the simulation)`.
- **The reports, sharper:** they also keep the objects' bytes at the start of each minute (StartMinute, after the
  display ran: `raw start <m>`, the one that would have shown this field), and each field's RTTI type kind
  (`fields <class>: <offset>=<name>:<kind>`, `dl::FieldList`), so `desync_diff.py` sets references (class, string,
  method, pointer kinds) and the unset third dword of 12-byte records apart. Seen: `TForce.AlternatePoint`'s longitude
  differs by 2^24 (one turn) between the copies after a load; the battle save writes it, nothing in the simulation
  reads it.
- **Docs:** GAME-INTERNALS (`LightState`, 0xe596ac, 0xd8d084, 0xdab764), ARCHITECTURE (the dual fields, the reports,
  level 14), TESTING (the report's sections, dawn and dusk, the guard lines).

## Testing 2026-10-17: live check of the polish round and the branch regression's findings (dist_ll_14, dist_ll_15)
Research agent. Two driver runs (`files\proofP`, session folder) and the branch regression's suite (a copy of
`ll-regress/tools` with this tree's `common.h`); game open 1560 + 413 + 813 s (46 min); slots 1 and 9 restored and
verified after each run, slot 2 untouched, Preferences unchanged, v0.3.2 installed again at the end.
- **dist_ll_14 (level 13), run p1.** The start: `ls level` 13 on both, `side 0's computer control as before the load:
  1 of 2 forces, 7 of 8 divisions` on both. **ai0:** GB Battery 0 (a division of the computer-controlled "Great
  Britain Coastal forces") given back (`ls order 15 -1 -1 0`), then `ls resync`: every side-0 flag the same before and
  after on both copies (6 of 8), the line on both, 3 minutes equal (level 12 would have handed it back to the
  computer). **The report:** `ls desync` on B: `DESYNC report ls_desync_A_100000_2_4.txt written (1051 kB ...)` and B's;
  `desync_diff.py`: the same call sites, `D0 GB Battle Division 2 DivisionCourse` 25 vs 62 (the test's +37) and its
  ships' courses. **e) repro:** 448 minutes (7..460) equal, no LEAK; a second battle DESYNCed at minute 291 (the entry
  before). **Item 8 (a rejoin while the host's battle, dated in the next month, is open):** `ls testclock 744` and a
  resync dated the battle save 1900-09-25; both loads kept 1900-08-01; `ls drop`, the client left and joined again (the
  hello path: `state seq 1 -> Captain (nation 1, ...)`) and planned Aug 1900 (slot 77 [General] 1900/8/1); after the
  host's battle the month merged its turn (`merge nation 1: 4 sections, 22 possession fields`, no rejected turn), Aug
  -> Sep on both. Not present on the branch: engine level 10 writes the campaign's own date back after every live load,
  so the hello path's save has it. **The polish list:** no `Log lines out of sync` line after the start, three resyncs
  and a live rejoin (leave and join: `rejoin: Captain commands side 1 again`, hashes equal), A's snapshot `9 battle log
  entries with line breaks written as one line`; Auto-run off of both sides logged alike on both copies (`side 1 back to
  its player as in single player: the lead divisions of 2 forces; ...`); the pause lines per player (A `The battle paused
  (an event).`, B `(Unknown ship sighted!)`, A's `the game's text is the other player's (not shown here)`), the client's
  Multiplayer window without the host's lines; `ls torp` at Realism 1 refused (`... Fire torpedoes is disabled
  (button=0 ...)`); the drop chat with a rejoin possible; `battle slot mode ended (the host's test battle outside a month
  is over)`.
- **dist_ll_15 (level 14), run p2.** A battle from 14:17 to 22:05 (462 minutes) with the e) recipe: no DESYNC, every
  hash equal (`LightState` in it), the guard's line at the light changes (A 19:12, 19:57, 20:01; B 19:12, 19:51, 19:54,
  20:01); `ls desync`: `raw start 469` names the test's `DivisionCourse` change, the noise down to four battle-screen
  fields and `AlternatePoint`.
- **The suite on dist_ll_15** (`--only lockstep,lockstep_ui,lockstep_limits,lockstep_torpedo`, RealismSetting 2 for the
  run): **138/140**. lockstep 26/26 (the injected DESYNC, `ls dispwrite` kept out with 188 fields), lockstep_ui 37/37
  (e) minutes 19..32 equal, the minutes of dist_ll_11's DESYNC), lockstep_limits 50/51 (c) the side-0 flags as before
  the resync: PASS; B's result box for its side: ok), lockstep_torpedo 17/18 (g) the salvo ordered before auto-run in
  the same pause fires first, alike on both; refused on auto-run, carried out or ordered: PASS). The two FAILs are the
  suite's: limits j) waits for the end question's `answered 6 (both sides on auto-run)` line, but this battle ended at
  its length with a message box (742 minutes equal, every other condition met); torpedo b)'s second order for the same
  mount got the dialog's "no firing solution" (both of the ship's mounts listed "No solution" after the first order;
  one salvo released) instead of the mod's duplicate refusal.
- **Snapshot that passed:** `dist_ll_15` (RTW3MP A36C3836A4274F72, version A46644BE1041AB45).

## Docs 2026-10-18: 0.4.0 release candidate (v0.4.0)
Docs agent, live-limits. The branch becomes 0.4.0 (decisions: version 0.4.0, the host option "Computer flies carrier
strikes in live battles" off by default, missiles enabled).
- **Merges:** `ll-polish` (042dd62, verified live as dist_ll_15) and `main` (09fbd55, v0.3.3). `src/` is 042dd62's
  with `MP_MOD_VERSION` 0.4.0 (`MP_PROTOCOL` 3, `MP_LS_LEVEL` 14) and 412228c's comment above `DedupAirUnits`: main's
  three 0.3.3 fixes were already on the branch (compared line by line; the branch's `ls testcurspeed` lacks main's
  `off <= 0` guard, a test command). `tools/regression.py` and the regression agent file stay the branch's; main's
  0.3.3 checks are to be ported (TESTING keeps their results and traps and lists them in the Scenario catalog).
- **Build:** `build.bat` without warnings; snapshot `dist_ll_16` (RTW3MP BC8F25EB48DDA769, version 75C9A65F92FAF988).
  Its RTW3MP.dll differs from dist_ll_15's only in the six version strings, the time stamps and the PDB id.
- **Docs:** CHANGELOG's 0.4.0 entry above 0.3.3 (without the three fixes 0.3.3 shipped, with the polish round's
  fixes); README (0.4.0, the option's default, auto-run and the game's texts as the polish round left them, upgrade
  notes with 0.3.3 as the previous release); the MODLOG index (a 0.3.3 group and one 0.4.0 group for the entries
  tagged v0.4.0 to v0.6.0); ARCHITECTURE, GAME-INTERNALS and TESTING name 0.4.0 instead of 0.6.0.
- **Open:** the release date in CHANGELOG's heading; the suite on dist_ll_16.

## Testing 2026-10-18: the 0.4.0 release candidate on dist_ll_16, and mixed versions with 0.3.3 (v0.4.0)
- Regression agent, branch `ll-regress`: `ll-polish` 042dd62 merged (7c716c7); main's 0.3.3 checks ported (3f109d5:
  `air_units` on slot 49 with main's bloated copy, `airunits` after every load, the game's own error boxes failing a
  run, `lockstep_ui` k) through `ls testcurspeed`, S8, the identification checks, the hooks line; reconciled with the
  branch's own checks, no duplicates), engine levels 13/14 and the build's own version (`build_version`: 0.6.0 on
  dist_ll_15, 0.4.0 on dist_ll_16); then `live-limits` e78de4f merged (2d5667e, the release candidate) and the 0.6.0
  texts renamed (d7ddd32). Live debugging on dist_ll_15 (run 1: 169/172), then on dist_ll_16 (`MP_MOD_VERSION` 0.4.0,
  engine level 14, protocol 3).
- **Final runs on dist_ll_16** (`--dist <snapshot>`; the stress-only scenarios once each after their fixes):
  - Full default run: **670/673** in 1528 s with the game open (`lockstep_ui` 41/41 213 s, `lockstep_limits` 52/52
    175 s, `lockstep_pvp` 26/26, `air_units` 10/10 61 s, `lockstep_air` 40/41 264 s, `lockstep_month` 36/38). Fails:
    `lockstep_month` battle 1 (`drop12`) x2, the VP difference open since 0.3.2 (product, below); `lockstep_air` g)
    "from both" picked B's squadron with no undamaged aircraft left (suite: a031e8a tries the next squadron; 41/41 in
    the next run).
  - `--stress --only lockstep_stress,lockstep_pvp,lockstep_month,air_units,lockstep_air`: **264/264** in 2733 s
    (`lockstep_stress` 101/101 in 24 min, `lockstep_pvp` long 36/36, `lockstep_month` long 64/64 in 706 s: the agreed
    end was refused 19 times, the game ended the battle at minute 980; `air_units` three months 12/12).
  - The other `--stress` scenarios: `lockstep_torpedo` **19/19** (the re-run after the stray-dialog fix, 609 s;
    the first run needed a division report closed by hand), `lockstep_missiles` **23/23**
    (`--missile-fixture <folder>\Game48`, ~330 s), `lockstep_strike` **19/19** (407 s with the game open),
    `lockstep_rejoin` **38/38** (563 s) and `lockstep_resume` **37/37** (378 s).
  - So every scenario of `--stress` passed on dist_ll_16 except the known VP difference in the default
    `lockstep_month`.
- **Suite fixes on dist_ll_16** (95a46a1, 8b01ddd, e77af8b, eab55c0, a031e8a, 3a33cf8; no product code changed):
  - `lockstep_strike`: `ls strikedlg` is asynchronous and the probe took its `ok` for a report (c)-e) opened empty
    dialogs); the reports list is owner-drawn and its item 0 is not always a report (`report_count`, the first item
    that opens the dialog); a January battle that starts at 14:00 or later gets no report before night (replaced at
    once with `ls endsoon`); with *Floatplane launch* on "Ask before launch" no search floatplane flies in a live battle
    (README), so the runner sets FloatplaneLaunch=0 for runs with `lockstep_strike` (backed up and put back like
    RealismSetting); the search sectors follow the bearing to the enemy (`pocdump` positions); the game's "CV <name>
    cannot perform flight operations!" is retried; the option's note comes only from the real "Order an airstrike"
    button; squadrons whose mission the dialog set are not the computer's strikes (launched formations have a parent);
    g) with every division on the AI.
  - `lockstep_missiles`: the automatic fire emptied Italy's 40 heavy SSM before the manual salvoes and the
    pending-target resync, which now run first, with both sides held through the approach under the computer; a
    computer-controlled force of a gate-0 nation fires whatever Fire at will says (as in single player), so the hold
    checks count the held ships only (`msl_held_ssm`); division indexes are read anew at every try; g) at speed -1.
  - `lockstep_rejoin` e): A finishing the relayed battle alone was stopped by a game box at 17:47 and waited without
    `pocrun`; B then rejoined the unfinished battle and the month drove a second relayed battle (the run was aborted
    and restored with the runner's helpers).
  - `lockstep_torpedo` b)-e) take the boundaries as they come; a division report left under the torpedo dialog blocked
    the host's tool bar (`close_modeless` loops, `ls_finish_host` closes leftovers first).
  - `lockstep_stress` S7's guard negative control provokes a DESYNC on purpose: its battle is registered so that the
    runner deletes its reports.
- **Mixed versions** (a game-folder copy outside the Steam library with dist_033, the Steam folder with dist_ll_16;
  TESTING.md, "Mixed versions"):
  - a) A 0.4.0 host with a 0.3.3 player: **20/20**. The join, a month, war 0-1; the game's own battle of the host
    against the player's nation is fought against the computer with the host's chat and the player's notice naming both
    versions and levels; that month goes on, then peace and a normal month. A first try with the debug `pocbattle`
    failed its month check: a non-live test battle closes with the rest of a turn, so the host's campaign moved a month
    on its own (test-only path).
  - b) A 0.3.3 host with two 0.4.0 players at war: **19/19**. Their battle is **fought live**, relayed by the 0.3.3
    host (10 minutes equal on both players' PCs, no DESYNC, the result merged). The README says that live battles need
    0.4.0 on the host's PC too; for a battle between two joined players that is not so.
  - c) A 0.4.0 host with two 0.3.3 players at war: **19/19**. Their battle is resolved by the game (`not fought live:
    live-battle engine levels 1/1, host 14`) with the host's chat and each player's notice.
- **Product issues** (reported, not fixed):
  - The VP difference after `lockstep_month`'s `drop12` battle (open since 0.3.2): the files agree (nation 0: 1402),
    the client's memory has 1462, and `vp_mismatch_probe` found that each copy's own reload adds points to nation 0
    (the host's `load 1 0` 1402 -> 1502, the client's second `load 77 1` 1462 -> 1482): the game's load path, not the
    package. Not new in 0.4.0.
  - Under a 0.3.3 host (case b) A's drop chat promises B its ships back if B joins again; only a 0.4.0 host can start
    a relayed battle again.
  - README, "Upgrading" and "Same mod version": two joined players on 0.4.0 fight their battle live under a 0.3.x host
    (it relays the battle); only the host's own battles against them are fought against the computer.
- Game open: 1020 s on dist_ll_15, about 10,850 s on dist_ll_16 (the debugging runs, the aborted run, the final
  runs, the mixed cases and the torpedo re-run).

## Docs 2026-10-18: final pre-release pass (v0.4.0)
Docs agent, live-limits, after the regression's GO on dist_ll_16 (the entry before). Docs, tools and tests only:
`src/` is unchanged since e78de4f, so dist_ll_16 stays the release build.
- **`ll-regress` merged** (da44631, fc33e85): the release candidate's results, the mixed-version checks and main's
  0.3.3 checks in the suite; no conflicts (it had merged e78de4f).
- **README and CHANGELOG corrected with the mixed-version results:** a live battle needs 0.4.0 on both players' PCs. A
  0.4.0 host fights its battle against a 0.3.3 player against the computer (the chat and the notice name both
  versions, the month goes on) and has the game resolve a battle between two 0.3.3 players. A 0.3.3 host relays a
  battle between two 0.4.0 players, who fight it live, but can't start it again for a player who dropped out although
  A's drop chat promises it: a known issue in CHANGELOG 0.4.0 and in README's Known limitations ("update the host").
  That a 0.3.x host's own battles against a 0.4.0 player are fought against the computer comes from the code (B
  refuses another level, and 0.3.3's A drops a B that refuses), not from a test.
- **GAME-INTERNALS:** "The AI's air operations follow PlayerSide" cites stages A and B instead of "to be confirmed".
- **Package text:** `extras\RTW3MP_README.txt` names the files the mod keeps in the host's slot
  (`rtw3mp_session.ini`, `rtw3mp_livebattle.ini`), as README's Uninstall does; README's example zip is 0.4.0's.
- **TESTING:** the Contents link to the missile-era fixture section and a second "How the runner uses it" removed
  (the suite's docs had folded that section into the 1958 campaign's); 0.3.3's traps name the suite's own checks.
- **Left for the release:** the date in CHANGELOG's heading, and the VP fix (branch `vp-reload`) under 0.4.0's Fixed.

## Fix 2026-10-18: victory points added on every reload (v0.4.0)
- Research agent, branch `vp-reload` from live-limits e78de4f (the 0.4.0 release candidate). The VP difference open
  since 0.3.2 (`lockstep_month` battle 1, `drop12`; latest in the regression agent's run 16 on dist_ll_16: nation 0
  has 1402 in both files, 1462 in the client's memory; `vp_mismatch_probe`: the host's `load 1 0` 1402 -> 1502, the
  client's second `load 77 1` -> 1482). Static work on 1.01.44, then a negative control on 0.3.3 and the proof of the
  fix, both live. Protocol 3 and engine level 14 unchanged: a live battle's simulation and dice don't change.
  Commits: 55a1fc5 (the fix, `game.cpp` only), 30c3048 (the suite), 0aedcf0 (the docs), 882fa4c (live-limits merged
  in), then this entry's last results.
- **Mechanism (1.01.44).** `LoadBuildCampaign` (0xc998c4) sets `PlayerNation := BuilderNations[0]` (0xc9a69d: `call
  TList.Get(0); mov [edx+0x3204], eax`) and ends its "Setting data" step with `CheckBlockadeStatus` (0xccc1c4, called at
  0xc9abeb right after `SetValues`). The routine clears and sets the nations' `Blockaded` flags (+0x1c5; the save has
  no such key), and it credits blockades: while the war counter is above 0 (0xccd5fc: `cmp dword ptr [0xf52ff4], 0;
  setg al`), for each nation i = 1..8 with `Tension` (+0x256) > 40, when PlayerNation `IsBlockaded`, `EnemyVP`
  (+0x4a63) += (Random(3) + [0xf4c914]) * 20 (0xccc256..0xccc26c), and when nation i is, PlayerNation's `VP` (+0x175)
  += the same (0xccc29a: `mov eax, 3; call Random; add eax, [0xf4c914]; shl eax, 2; lea eax, [eax+eax*4]; mov edx,
  [esi+0x3204]; add [edx+0x175], eax`). [0xf4c914] is `[General] FleetSize` (stored by the load at 0xc99b0c; 3 in the
  1900 fixture), so a blockade gives 60, 80 or 100: every difference seen since 0.3.2 (60, 80, 100; run 16: +60 and
  +80 on the client, +100 on the host). `TBuilderNation.IsBlockaded` (0xc618e8) ignores its argument and returns
  `CheckBlockade(BuildArea +0x70)` or `CheckBlockade(BlockadeArea +0x78)` (0xc61920): true when a nation at war with it
  has a `GetBlockadeStrength` (0xc76b74: its strength in the area, active ships for blockade, times its
  `BlockadeModifier` +0x206 / k, plus allies at war) above `GetMinimumBlockadeStrength` (0xc7864c: FleetSize * 8 + 16)
  and above the nation's own times a factor. The routine's other callers are `DoTurn` (0xce0f87, after
  `AreaMoveShips`) and `DoStrategicTurn` (0xce1bb9): the month's own credits, as in single player. A scan of every
  write to `[reg+0x175]` and `[reg+0x4a63]` in the image found no other writer on the load path (`TBuilderNation.LoadMe`
  0xc500ab and the load's `[General] EnemyVP` 0xc99dce read the file).
- **So every load of a campaign with a blockade credited it once more.** The game loads a campaign once per process;
  the mod loads it again in the same process: the host after merging the players' turns (every month), a joined player
  for every new month and around its battles (slot 87, then slot 77), and both PCs at every live battle start, resync
  and rejoin. Where the points went:
  - The host's reloads went into its saves: nation 0's VP, or EnemyVP when nation 0 was blockaded, and through EnemyVP
    the shares `ShareEnemyVP` gives the host's human enemies ("victory points: nation 1 +X of the host's enemies' ...").
  - A joined player's battle slot is a Copy-game transform with the player's nation K as nation 0: its load credited
    K (or EnemyVP, J's side), and `ApplyBattleResult`'s deltas (`dVP_K` = [Nation0] VP after the battle - as sent,
    `dVP_J` from [General] EnemyVP) carried that credit into the host's save with the battle: a joined player's VP from
    its battles against AI nations, and the war score of a war between players (a battle fought on A's PC).
  - A joined player's month load credited nation 0 in its memory only: shown above the host's value until the next
    month replaced it (the host keeps its own `VP` keys when it merges a turn). The only variant earlier tests saw.
  - Not the mod: its battle VP (`OnBattleVP`) come only from `HandleBattleAftermath` and the AI/raider battle hooks,
    never from a load, and the game's load re-applies no pending battle result (no VP writer on the load path but this
    one).
- **Why the difference showed only after `lockstep_month`'s `drop12` battle, and only in full runs:** a client's own
  load shows extra points for nation 0 only when nation 0 blockades an enemy (an EnemyVP credit is replaced by the
  client's own view, `ShowEnemyVP`, and the host's own reloads agree with its files). GB and Germany both build in
  Northern Europe, and GB blockades Germany only once Germany's active strength there is low, after the battles of a
  full run (the failing months were Sep-Nov 1901); isolated `lockstep_month` runs fight right after the war starts,
  when Germany blockades GB instead (seen in this entry's suite runs). The host's 0 VP in those battles was a
  coincidence: run 16's failing battle ended "Enemy marginal victory Own VP: 246 Enemy VP: 342".
- **Fix (`game.cpp`).** A hook on `CheckBlockadeStatus` (a register procedure, Self in EAX, no stack arguments): the
  thunk passes the caller's return address to `CB_BeforeBlockade`. Only LoadBuildCampaign's own call counts:
  `ResolveLoadBlockade` scans LoadBuildCampaign's first 0x2000 bytes for `E8` calls to the routine and keeps the
  return address of the only one (+0x132c in 1.01.44); none or several -> `Load VP guard: UNAVAILABLE` and no hook.
  For that call the hook keeps every nation's VP and EnemyVP, runs the game's routine (its flags and its `Random(3)`
  draws as before, so the dice of a live battle's load stay the same) and puts the values back. Logs: at startup
  `Load VP guard: ok (CheckBlockadeStatus=..., 1 call(s) in LoadBuildCampaign)` and `hook CheckBlockadeStatus at rva
  0x49c1c4`; for each load whose check changed something `load: the game's blockade check gave victory points the save
  doesn't have; put back: nation 0 160 -> 60 (blockaded: 1)` (`EnemyVP a -> b` when nation 0 is blockaded; the
  blockaded nations after the check). The month's two calls are untouched. Every load counts, the title screen's too
  and without a session: a load gives exactly the saved values (in single player with the mod installed as well).
  *Changed the same day: only in multiplayer sessions and for the mod's own reloads (the last points of this entry).*
- **Not repaired:** points a reload already added to a save can't be told from earned ones (the month's credits are
  the same amounts and leave no report line), so they stay.
- **Main (0.3.3)** has the same bug (the reloads are as old as the mod; live battle loads since 0.3.0) and the fix
  applies unchanged: it touches only `game.cpp`, independent of the live battles. Backport: cherry-pick the code
  commit onto main (or copy the `Api` fields, `ResolveLoadBlockade`, the "Victory points on every load" section and the
  `InstallHooks` line), then `vp_reload_check` with its two calls in `scenario_lockstep_month` and the after-month
  check (main's runner has `vp_mismatch_probe` too), a CHANGELOG line for 0.3.4. Checked offline: the code commit
  applies to main's tree (`git apply --check`, one hunk 5 lines off) and main 0.3.3 with it builds without warnings;
  not run in the game. *Decided the same day: no 0.3.4; 0.4.0 carries the fix, main stays as it is.*
- **Tests (`tools/regression.py`).** `lockstep_month`: `vp_reload_check` after every host battle: a blockade made on
  purpose (nation 0's `BlockadeModifier` 1000 with `nfield` and saved on both copies: GB blockades Germany at every
  check while at war), then each copy loads its own slot twice; after each load every nation's VP and EnemyVP equal
  the file's and the load logged one `put back` line naming nation 1; the modifier is put back and saved. After the
  peace month: every nation's VP the same in the host's memory, its slot 1 and the client's memory.
  `load_answering` (a bridge `load` while the copy's dialogs are answered) is shared with `vp_mismatch_probe`.
- **Live, negative control** on the installed 0.3.3 (dist_033; a driver in the session folder on top of the runner's
  `main`: its preflight, backups of slots 1 and 9, launch, close, restore): session, war 0-1, the lever on both copies,
  each copy loads its own slot twice. The host's slot 1 has VP 0 for every nation: `load 1 0` -> nation 0 60, then
  100 (a second run: 100, then 80). The client's slot 77 had no war yet in that month: no credit. Game open 20 s and
  21 s.
- **Live, the fix** (dist_vp_1, the lever from the war's start; 48/48, game open 137 s): `Load VP guard: ok` and the
  hook on both copies. Aug 1900: the host's battle 100000 (`drop12`, "The battle was a draw. Own VP: 0 Enemy VP: 0");
  the month's loads put back on the host "nation 0 100 -> 0" (the merge reload) and "60 -> 0" (the battle's start),
  on the client "60 -> 0" (B's battle load) and "140 -> 60" (its new month); after the month every nation's VP the same
  in the host's memory, slot 1, the client's memory and slot 77 (0:60, 1:130); `vp_reload_check`: the host 160 -> 60
  twice, the client 140 -> 60 and 120 -> 60, VP and EnemyVP equal to the files after every load. Sep 1900 at war
  with the blockade: battle 100001 (`drop12`, "Enemy marginal victory Own VP: 0 Enemy VP: 90"), one "victory points:
  nation 1 +90 in its battle against the host -> 220" line; after the month 0:160, 1:330 everywhere (the month's own
  credit stays: 60 -> 160); the month's loads put back 2 credits on the host, 3 on the client; one more load each:
  260 -> 160, VP equal to the files.
- **Suite on dist_vp_1:** `--only lockstep,lockstep_pvp,lockstep_month` **113/113** (game open 355 s): session 8/8,
  lockstep 25/25, lockstep_pvp 42/42 (f): "war score 1-2: +70/+786 from a battle fought on a player's PC ->
  74:824"), lockstep_month 38/38 (Dec 1900, battle 100001 `drop12`: "victory points: nation 1 +83 in its battle
  against the host -> 159" once;
  `vp_reload_check`: the host 554 -> 474 and 534 -> 474 put back, the client 554 -> 474 twice, VP and EnemyVP equal to
  the files after every load; after the peace month every nation's VP the same in the host's memory, slot 1 and the
  client's memory). `--only lockstep_month --stress` **73/73** (game open 675 s): Aug 1900 battle 100000 `agree` (its
  reload check: the host 80 -> 0 and 100 -> 0, the client 100 -> 0 twice), Sep 1900 battle 100001 `endsoon` after
  those reloads (its aftermath: nation 0 198, nation 1 492, nation 7 498 in the host's memory, slot 1, the client's
  memory and its file; its reload check: the host 278 -> 198 twice, the client 258 -> 198 twice), the peace month.
  Natural blockades in both runs, all put back: Germany blockaded GB (the host's merge reload and live battle start
  `EnemyVP 287 -> 187 (blockaded: 0)`, the client's month load `EnemyVP 530 -> 430`; --stress `EnemyVP 80 -> 0`, `100
  -> 0`); the battle slot loads of `lockstep_pvp` (`nation 0 100 -> 0 (blockaded: 2)`, 84 -> 4, 134 -> 74, also on
  the third copy) and of the idea-1 battles of `lockstep_month --stress` (`nation 0 380 -> 240 (blockaded: 1 7)`,
  400 -> 240 twice), whose merges then carried only the battles' own VP ("battle 1 applied: ... VP 0/246").
- **After merging live-limits** (ca0122f: ll-regress's final runner and the docs' pre-release pass; no `src/` change
  there) into this branch (882fa4c): `--only lockstep_month` with the merged runner on dist_vp_1 **50/50** (session
  10/10, lockstep_month 40/40; game open 116 s): Aug 1900 battle 100000 `drop12`, the reload check put back 202 -> 142
  twice on the host and 242 -> 142, 222 -> 142 on the client.
- Game open: negative control 41 s, proof 137 s, suites 355 s + 675 s + 116 s: about 22 min in all. Snapshot
  `dist_vp_1` (RTW3MP 4467E3DB52DEBC80, version F8BD671F03270F7B); the game folder's 0.3.3 DLLs put back afterwards
  (hashes checked), slots 1 and 9 as before.
- **Changed the same day: the guard only in multiplayer sessions** (decision: outside a session the game behaves as
  in single player, and a single-player game loads a campaign once per process anyway). The load's credit is put back
  when a session is up on this PC (`hookcb::InSession`: hosting or joined, the lobby included) or when the load is one
  of the mod's own reloads (`game::LoadCampaign`'s new `modReload`, on by default). The mod's reloads run in a session
  (the host's merge reloads, a joined player's months and battle slots, every live battle load) except one: a joined
  player going back to its own slot after a live battle that outlived the session (`LeaveBattleSlot` from `Tick` when
  `g_lsLeave`, set by the live battle's end, e.g. "the connection to the host was lost"); it stays guarded. Not
  guarded: the game's own loads without a session (the title screen, the host's first load before hosting, the resume
  after a restart, which runs before hosting again): one credit per start, as in single player. A first load after
  *Host game* was pressed (in the lobby) counts as in a session. The bridge's `load <slot> <player>` stands for the
  player's own load (`modReload` off); `load <slot> <player> mod` for the mod's reload. Logs: at startup `Load VP guard:
  ok (...; loads in a multiplayer session and the mod's own reloads keep the saved victory points, a load without a
  session is the game's own)`; a load without a session that got a credit: `load: the game's blockade check gave
  victory points: nation 0 0 -> 100 (blockaded: 1); kept: no multiplayer session (a load outside a session is the
  game's own)` (saved value -> after the check). Snapshot `dist_vp_2` (RTW3MP D7A6A56AD5E2860A, version
  489F3389F6C1EF05).
  - Live, the session rule (a driver on the runner's `main`; 17/17, game open 23 s): war 0-1 and the lever saved, the
    host's `load 1 0` in the session put back `nation 0 60 -> 0`; after `leave` on both copies (role `none`) two
    `load 1 0` kept the game's credit (`nation 0 0 -> 100` twice, VP 100 against the file's 0, the `kept` line), and
    `load 1 0 mod` put it back (`100 -> 0`, VP equal to the file).
  - In the session: `--only lockstep_month` **50/50** twice (session 10/10, lockstep_month 40/40). The reload checks
    put back on the host 80 -> 0 twice, on the client 100 -> 0 and 60 -> 0; in the rerun 190 -> 90 twice and 150 ->
    90, 170 -> 90, VP and EnemyVP equal to the files after every load; the month after equal everywhere. The first
    run stalled 11 minutes after the drop: the host's `TdlgLoading` (the game's progress box with a progress bar) was
    still visible after the live battle's start, and the runner's `autorun_click` waits while any `Tdlg*` window is
    visible. Hidden with `ShowWindow` (a posted `WM_CLOSE` didn't close it), the run went on and passed (779 s). A
    clean rerun passed in 103 s; a watcher saw the box shown and hidden within a second at each battle load there.
    First seen; not the guard (it touches no window). Suggestion for the runner: ignore a `TdlgLoading` in
    `autorun_click`, or report it.
  - Game open for this change: 23 s + 779 s + 103 s, about 15 min.

## Testing 2026-10-18: the second 0.4.0 release candidate on dist_vp_2 (v0.4.0)
- Regression agent, branch `ll-regress`: `live-limits` c9055dd (the VP fix, the guard only in sessions, the bridge's
  `load ... mod`) merged by fast-forward. The suite's loads fit the bridge's new meaning: the plain `load <slot>
  <player>` is the player's own load everywhere (session start, `move_session`, `air_units`, the resume restarts:
  outside a session the game keeps its credit, as in single player; `vp_reload_check` and `vp_mismatch_probe` load in
  the session: guarded). Snapshot `dist_vp_2` (RTW3MP D7A6A56AD5E2860A) installed for the runs, `dist_033` put back.
- **Runs on dist_vp_2** (`--dist <snapshot>`):
  - Full default run: **674/684** in 1464 s. `lockstep_month` 40/40 (no VP difference after `drop12` any more).
    Fails: `lockstep_ui` k) host and j): a sighting-only LEAK right after `ls testcurspeed` at dawn (t=05:12), the
    test command's own (suite: excused, 2b44526); `lockstep_air` g) x6: the Fleet battle at Plymouth had no carrier
    division on either side ("No airstrike capable units available!" on both copies; suite: such a battle is replaced,
    2b44526); the client's game error box at `lockstep_air`'s start (twice: the scenario and the run), below.
  - Whole `--stress` run (all 26 scenarios, the long variants, `--missile-fixture <folder>\Game48`): **920/923** in
    5385 s. Every scenario passed except: `lockstep_limits` f) (the host's division report did not open: its popup
    menu overshot), `lockstep_pvp` b) long (the game refused the agreed end 3 times, then ended the battle at its
    length, minute 793; the check now accepts that, as `lockstep_month`'s does), `lockstep_stress` S8 (1 and 3 of 15
    division reports opened: popup menus and battery nodes); `lockstep_strike` was skipped (`lockstep_air`'s battle
    left slot 9 with a battle in progress; the run's backup is accepted since 6bfca49). No game error box, no
    `TdlgLoading` left visible. `lockstep_month` long 67/67, `lockstep_rejoin` 38/38, `lockstep_resume` 37/37,
    `lockstep_torpedo` 19/19, `air_units` 12/12, `lockstep_air` 41/41, `lockstep_missiles` 24/24.
  - A follow-up `--stress` run of the scenarios with fixes, `--only lockstep_ui,lockstep_limits,lockstep_month,
    lockstep_stress,lockstep_air,lockstep_strike`: a first try aborted in `lockstep_limits` (a popup menu that took no
    keys stayed open on the client and the next resync waited for it 4 minutes; 24/32, 437 s), then **333/333** in
    3351 s after 7175467: `lockstep_ui` 41/41, `lockstep_limits` 53/53, `lockstep_month` long 67/67 (the separate
    `lockstep_month --stress`), `lockstep_stress` 101/101 (S8 15 of 15 reports on both copies), `lockstep_air` 41/41,
    `lockstep_strike` 20/20 (after `lockstep_air` in one run).
  - So every scenario passed on dist_vp_2; the failures were the suite's, except the error box.
- **Suite fixes** (2b44526, b0e6a23, 6bfca49, 7175467, f893051; no product code changed):
  - `testcur_leak`: the first LEAK of a copy right after `ls testcurspeed` that names only `SightingLevel`/
    `RadarSightingLevel` is the command's own baseline (it re-baselines the leak check while the display's sighting
    values are in place); any other LEAK still fails.
  - `carrier_divs`: `lockstep_air` b) and `strike_battle` replace a battle without a carrier division on a side (the
    strike dialog lists only those), in a fresh session, up to 3 battles, a NOTE each.
  - Popup menus (`popup_pick`, `lv_popup_pick`): keys at a 0.25 s pace with the highlight read before each key (an
    overshoot is corrected); a menu that can't be used is closed (`close_menus`: VK_ESCAPE, then MN_CANCELMENUS sent
    to the menu window) and the copy's GUI thread state reported; `close_modeless` and each `popup_pick` close a menu
    left open first. A live probe (`popup_probe.py`, kept with the session files: both copies, force and division
    nodes) found every menu taking a posted key in 0.05 s; the two slow cases were in long runs. `open_divreport`
    skips coastal batteries (their "Status..." opens no division report).
  - `lockstep_pvp` b) long: the game's own end at the battle's length after a refused agreed end is accepted.
  - `lockstep_strike` accepts the run's backup of slot 9 when an earlier scenario fought in the slot.
  - `ls_drive_endsoon` runs to minute 2 first: `ls endsoon` sets the length to max(1, m/2 - 1), so given at minute 0
    or 1 the game's limit (length + min(length, 600)) came a minute after the TEST line's "end of minute m+1".
  - The session checks `Load VP guard: ok` on both copies; `autorun_click` reports a `TdlgLoading` still visible
    after 15 s with its evidence (`LOADING_STUCK`, in the summary) and no longer waits for it.
- **The loading box stall** the VP agent saw once (the host's `TdlgLoading` visible after a live battle's start):
  not seen in these runs (no `LOADING_STUCK` evidence in the default, `--stress` or follow-up runs).
- **Mixed versions** (the game-folder copy with dist_033, the Steam folder with dist_vp_2; TESTING.md):
  - d) A 0.3.3 host's own battle against a 0.4.0 player: **20/20** (the first run 19/20: its check expected no hold
    and no send). Fought against the computer, as the README says, by this path: 0.3.3 knows no engine levels, holds
    the battle and sends it; the 0.4.0 PC refuses it at once and never loads it (`battle 100000 refused: Admiral's mod
    plays live battles at level 1, this PC's at level 14 (a live battle needs the same level on both PCs: the same mod
    version)`, the chat "The live battle with Admiral could not start: ..." and the notice "A live battle could not
    start: Admiral's mod plays live battles at another level"); the host's AI then commands the player's nation
    ("Captain left the live battle (...). The AI commands Germany now; the battle goes on here."). One month, the
    player's turn merged, then peace and a normal month; no error box.
  - b) again: **19/19**, relayed live by the 0.3.3 host as on dist_ll_16 (10 minutes equal, the result merged).
- **Product issues** (reported, not fixed):
  - The client's "Error in SaveBuildCampaign procedure! Access violation at address 00C5B9D4 in module 'RTW3.exe'.
    Read of address 00300028" at `lockstep_air`'s start in the default run (its save that keeps the plans before the
    live battle loads; client log 14:36:24). The same code address as the three boxes of a 0.3.3 run (`@UStrAddRef`
    with a bad string pointer, then -1), both times on the client at its first `SaveBuildCampaign` after it joined a
    session on another campaign in the same process. The battle and the rest of the scenario ran normally (its return
    to slot 77 loaded the 232 air units); not seen in the `--stress` or follow-up runs, which make the same switches.
    Predates 0.4.0 (0.3.3 is released with it). A stack trace (a debugger, or the `RTW3MP_DIAG` handler extended with
    one) would find the stale string.
  - Test commands only: `ls testcurspeed` re-baselines the leak check with the display's sighting values in place
    (it could do it with the simulation's set in place); `ls endsoon` given at minute 0 or 1 ends the battle a minute
    later than its TEST line says.
  - Wording under a 0.3.x host (d): the 0.4.0 PC's refusal reason says "this PC's at level 14", and the 0.3.3 host
    shows it verbatim ("Captain left the live battle (Captain: ... this PC's at level 14 ...)"), so on the host's
    screen "this PC" is the player's. 0.3.3 also sends its battle chat lines to every player, so the 0.4.0 player
    reads the host's "waiting for Captain (Germany) to join the battle". Cosmetic; only with a 0.3.x host.
- Game open: default 1464 s, `--stress` 5385 s, the aborted follow-up 437 s, the popup probes 177 s, the follow-up
  3351 s, mixed d) 116 s + 178 s, mixed b) 86 s: 11,194 s, about 3 h 7 min. Afterwards: `dist_033` back in the game
  folder (hashes checked), the folder copy deleted, slots 1, 2 and 9 as before (slot 9 equal to the pristine copy),
  Preferences RealismSetting=1 and FloatplaneLaunch=1, no `RTW3.exe`.

## Docs 2026-10-18: 0.4.0 release docs (v0.4.0)
Docs agent, live-limits, after the regression's GO on dist_vp_2 (the entry before). Docs only: `src/` equals c9055dd
(the VP guard only in multiplayer sessions), so dist_vp_2 is the release build.
- **`ll-regress` merged** by fast-forward (b791475: the second release candidate's runs, mixed case d), the suite's
  fixes).
- **CHANGELOG:** the heading `## 0.4.0 (2026-10-18)`; the summary names the VP fix. Compatibility describes case d) as
  the regression saw it: a 0.3.x host's own battle against a 0.4.0 player is refused by the player's game with a
  notice, its chat naming both live-battle levels, and the host's computer fights the player's side (the first pass
  had it from the code only). A second known issue: "Error in SaveBuildCampaign procedure! Access violation ..." at
  the first save after joining a session on a different campaign without restarting the game, seen in the
  regression's runs on dist_vp_2 and on 0.3.3; the advice is to restart the game first.
- **README:** the same in *Same mod version*, the upgrade notes and Known limitations (a new bullet, with a
  Troubleshooting pointer to it). ARCHITECTURE: the 0.3.x host's path of case d).
- **Release files:** `dist\` holds the dist_vp_2 DLLs (RTW3MP D7A6A56AD5E2860A, version 489F3389F6C1EF05), which
  `tools/package_release.ps1` packages; a rebuild of this tree differs from them only in the time stamps and the PDB
  id.
- **Checks:** `build.bat` without warnings, `git diff c9055dd HEAD -- src` empty, the offline self-test, the links and
  anchors, the MODLOG index, the personal-data scan.

## Investigation 2026-10-19: live battles between two PCs (v0.4.1)
Research agent, branch `fix-net` (from main d539e4d = v0.4.0). A report from two players on 0.3.2, on two PCs over a
network, verbatim: "when trying to load player vs player battels the non host player is loading in as the host
nations side the battle closes and send the error message Synchronising the battle again (the loaded battles
differ)... this is also over a network connection, one battle did work but the non player hosts ships were not
visible on the map." Every earlier test ran both PCs on one PC: one Preferences file, one locale, one screen, one
game folder, localhost; and no test looked at B's battle map. Static work in the offline workspace (`rdis.py`, its call
graph, `rtti\`), live runs nb1-nb9 with the session folder's driver (`files\nb\nb_repl.py`, jobs `j_nb*.py`,
PrintWindow evidence in `files\nb\shots\`). Game open 2087 s in the runs nb1-nb9 and 377 s + 390 s in the two
regression runs (2854 s, 48 min); slot 1 restored after every run, slot 2 untouched, the Preferences file's hash
unchanged after every run, 0.4.0 (`dist_vp_2`) back in the game folder after every run.
- **The reading, in 0.3.2's code (and 0.4.0's).** A loads and takes its READY hash (`StartTransfer`: `StateHash().all`
  with the clock, plus the RNG state); B loads and sends `ready` with its own; `ACheckReady` compares them and on a
  difference logs `the loaded battles differ` and requests a resync: both chats say "Synchronising the battle again
  (the loaded battles differ)...", B's battle screen closes and loads again; after three resyncs A drops B ("the
  battles could not be kept in sync") and the computer fights B's side. The hash covers positions, damage, ammunition,
  sighting, orders (with the divisions' and ships' names), air formations and torpedoes, not the identification.
- **Reproduced on 0.4.0 (nb1; B with other Preferences than A):** PrintWindow captures of B's battle screen during its
  load and its resync load show side 0's picture: the map on the British fleet and coast, the British division flags,
  the British ensign (`nb1_Bload_*`, `nb1_Bresync_*`). After the load (minutes 0 and 5) B's map is side 0's: the
  British ships with names and flags shown, B's own German ships hidden under their division flags until side 0's
  ships see them (`nb1_B_m5_out3.png`), and A's map shows B's division flags. READY and the minutes stay equal: the
  hashes don't cover what the map shows.
- **Causes of the display symptoms (static reading, GAME-INTERNALS "Two PCs").**
  - *The map.* The game decides in the simulation and in every load what the map shows, for its PlayerSide, which is
    0 on both PCs of a live battle, and the side table's computer side, which the resume sets to side 1: the sighting
    routine 0xdc960c calls `SetHiddenDiv` 0xdb2424 for every division (flags hidden only for a computer force), shows
    the ships of a human side, hides the computer side's unless sighted by human-controlled ships, and labels every
    ship (names for the human side). The 0.3.0 display swap gives B its PlayerSide and its own copy of `Hidden`, but
    nothing ever applied the rules for B's side, so B's copy kept side 0's picture: B's ships as side 0 sees enemies,
    side 0's as own ships. Both sides' forces are human in a live battle, so both maps showed every flag.
  - *The load's picture.* The resume shows the battle screen and draws the first side's picture before anything else
    runs (the map centred on side 0's fleet).
  - *The ensign.* The resume copies the campaign screen's flag into `TfrmMain.ImgFlag` (`LoadScenarioInProgress`
    0xcd02c9..0xcd02e2, `TImage.SetPicture` 0xa0e5dc); the battle slot's campaign is the first player's.
- **The READY mismatch on every attempt: not reproduced.** No condition we can set on one PC made the loaded battles
  differ, on 0.3.2, 0.4.0 or 0.4.1:
  - *Preferences* (the main suspect: the Preferences reader and every read of the record classified by whether the
    battle load or the simulation reaches it, GAME-INTERNALS): `AIControl` differing on 0.3.2 (nb2: B 0, nb3: B 1, A 2)
    and on 0.4.0: READY `all` equal (the battle file's per-division `AIControl` wins over the preference on a resume;
    0.3.2 differed only in `ident=`, the known 0.3.2 identification, which `all` leaves out). `ConvertCharacters` (the
    only preference that changes names, `ConvertGermanCharactersInName` 0xd4ce8c: ö->oe in the campaign loaders) on B
    on 0.4.0 (nb5, verified in B's memory): READY and 10 minutes equal; the battle's ships keep the battle file's
    `Ship<n>Name`. 34 preferences different on 0.4.1 (nb6): equal.
  - *Number format and code page:* every save file and the battle file is UTF-8 with a BOM and holds integers only (no
    decimal number, no locale-formatted date); nothing parses a float from a file (`FormatSettings` 0xf2b6c8 is used
    for display only); B with a comma locale (nb6, `RTW3MP_TEST_LOCALE=comma`): equal, its screen shows "24.000 yds".
  - *Window size and screen:* the load gives the battle form its saved size (the Preferences' `MainFormWidth`/`Height`,
    1000x719 here) whatever the window was; with the copies resized to 1600x1000/820x600 (nb4) and 1560x980/900x650
    (nb6, then a resync) every minute stayed equal.
  - *Latency, jitter, bandwidth:* the client through `tools/net_delay_proxy.py` (nb6: 120 ms +-60 ms each way, 4000
    kbit/s): a test battle and a month battle equal.
  - *Paths:* the battle file's only absolute path is `FlagFile=` (0.3.0's `FixFlagFiles` with A's `gd=`). *The game
    version:* the hello's build fingerprint refuses another `RTW3.exe`.
  - What one PC can't test: **different CPUs** (the x87 transcendental instructions can differ in the last bit between
    vendors; a comparison that flips at a threshold during the load would flip again at every reload of the same
    state, which fits "every attempt", but such a flip is rare) and **different game data files** (nothing checks
    `Data\`; a modded file the battle reads would make the battles differ). And the report is from 0.3.2, which still
    had the air units doubled with every reload (0.3.3, campaigns with aircraft); its effect on a live load is not
    known (the resume takes the battle's air formations from the battle file).
  - So since engine level 15 a READY mismatch writes both PCs' DESYNC reports (below): the next report from two PCs
    shows which fields their loads made differently.
- **Fixes (engine level 15, `MP_MOD_VERSION` 0.4.1, protocol 3; `src/mod/lockstep.cpp` unless noted):**
  - *Each PC's map shows its own side* (`ApplyMapView`, after the outermost StartMinute, EOTP, detach and CSDD steps,
    from `FrameEnterCount`, and after every load): the game's rules for this PC's view side, outside the simulation
    and with the display's random draws (`g_ui`): own ships visible unless their division's flag is hidden, or when the
    enemy spotted them (`OwnShipSpotted`, `OwnShipRadarSpotted`, `ShipType` 12); the other side's only while
    `max(SightingLevel, RadarSightingLevel) > 0` and `SeenByHumanControlledShips` (right after a load the sighting
    levels alone: the load's sighting routine ran while side 1 was the computer's); flags only for own divisions not of
    a computer force (and a role-0 division of one); the game's relabel of every ship; nothing when `BothComputer`.
    `TFightingShip.SetVisible` is found by name, the flags' hidden setter in `SetHiddenDiv` (`8B 46 ?? B2 01 E8`), the
    relabel in `CbShipNameDisplayChange` (`89 02 8B C3 E8`). `Hidden` is in the display's dual set and the flags,
    wakes and labels are display objects: the hashes and `ls leakcheck` see nothing (checked in every run).
  - *B's battle screen is transparent while it loads* (`CoverBattleForm`: `TWinControl.HandleNeeded`, then a layered
    window with alpha 0 from the battle form's reset to the end of the load and the map pass; removed on every failure
    path too).
  - *B shows its own ensign* (`ApplyOwnFlag`: `TPicture.Assign(ImgFlag.Picture, SideFlags[1])`, the side flag
    `LoadSideFlags` 0xdf6b60 read from the battle file).
  - *Battle preferences:* the start, resync and rejoin carry only the 14 the simulation uses (`kPrefs`: `AIControl`,
    `FloatWarning`, `ReadyPopup`, `BreakOnNewSighting`, `AirApproachMessage`, `AirSurfaceMessageLvl`,
    `LetAIControlForce`, `LOScalc`, `SoundOption`, `Autosave`, `FloatplaneLaunch`, `RealismSetting`,
    `NoFriendlyTorpHits`, `BSSMHC`; until level 14 21 byte ranges from +0x10 to +0x37a, 0.3.0's, which gave B A's
    display options too (sound volume, dark mode, window state, scale, deck colours, range circles) and missed
    `AIControl` +0xc and `BSSMHC` +0x37b, which the simulation routine 0xdf0d5d reads). B logs the differences at the
    first load (`battle preferences: <A>'s for the load and the simulation (...)`) and gets its own back at the end
    (`this PC's own battle preferences again (...)`, also after a drop or an abort), in memory only. The battle
    screen's Preferences button (`B_PREFS`, `TfrmMain.sbPrefsClick`) runs the dialog, then puts the 14 back with a log
    line and a chat line (the dialog opened from the battle screen wrote `RealismSetting` -1 after a plain Ok).
  - *A's x87 control word for B's load too* (until level 14 B set it only after the load), logged when B's own
    differs.
  - *A READY mismatch writes both PCs' DESYNC reports* (`ACheckReady` -> `DesyncReport(..., "the loaded battles
    differ")`, `desync` message with `why=ready` to B, which writes its own at `WaitReady`; the report's line `why the
    loaded battles differ`). nb9 (`ls testreadydiff` on B, then `ls resync`): both reports written, `desync_diff.py`
    found the one field (`D0 GB Battle Division 2 DivisionCourse` 0x58/0x7d), the resync loaded equal battles, 4
    minutes equal.
  - Test switches and probes: `RTW3MP_TEST_PREFS`, `RTW3MP_TEST_FPUCW`, `RTW3MP_TEST_LOCALE` (`ApplyTestSwitches`;
    `game.cpp` hooks the Preferences reader and disables the writer only with `RTW3MP_TEST_PREFS`), `ls mapview`, `ls
    prefs`, `ls testcwload`, `ls testreadydiff`; `tools/net_delay_proxy.py` (docs/TESTING.md, "Two-PC conditions").
- **Live proofs:**
  - nb4 (dist_nb_1, B with `RTW3MP_TEST_PREFS` and `RTW3MP_TEST_FPUCW`): READY equal; 5, 20, 15 and 3 minutes and a
    resync equal, no LEAK; B's map: own 31/31 ships, A's 0 -> 4 -> 10 of 16 as sighted, no A flags
    (`nb4_B_m25_out4.png`, the mirror `nb4_A_m25_out4.png`); windows 1600x1000 and 820x600: 15 minutes equal. 508 s.
  - nb6 (dist_nb_2; combined): B with a comma locale and 34 other Preferences (file juggling: the game's own reader),
    the windows 900x650/1560x980, the client through the delay proxy. Test battle: READY equal, 12 minutes, a resync
    after the resize, 6 minutes: equal, no LEAK, no DESYNC (`nb6_B_m0.png`, `nb6_B_m12_out3.png`, `nb6_B_m18.png`).
    The host's battle against the client in a real month (CheckForBattles2 -> DoBattle, `ls_month` drop12): READY
    equal, 12 minutes equal, no LEAK, no DESYNC, 9/9 month checks, the month on to September (`nb6_Bmonth_000.png`:
    B's fleet at night, no British ship). The Preferences file unchanged. 386 s.
  - nb7: the client announcing level 14 (a 0.4.0 player): `battle against Captain: fought against the AI (live-battle
    engine level 14, the host's 15)`, the client's chat names both levels. 104 s.
  - nb8 (dist_nb_3): B's ensign is the German one (`nb8_B_m0.png`, `the battle screen shows side 1's ensign`); the
    Preferences dialog on B: `RealismSetting 1 -> -1` put back, chat line, 5 minutes equal, B's own (Realism 2,
    floatplanes off) back at the end. 165 s.
  - nb9 (dist_nb_4): the READY-mismatch reports (above). 124 s.
- **Where battles between players come from, and who is A:** the host's month (`CheckForBattles2` -> `DoBattle`, the
  host's nation side 0: A = the host), test battles (the same `DoBattle`), and battles between two joined players
  (`HostDeferPvpBattle`: the battle is sent to the first nation's player, who sets it up with its nation as side 0 =
  A; the other is B). A joined player's battle phase never makes a battle against the host: `HostDeferBattle` defers
  only battles of a joined player's nation against an AI nation and between two joined players' nations; the host's
  own battles come from its `CheckForBattles2`. In every case B is side 1, and the map, cover and ensign fixes key on
  the PC's view side.
- **Regression:** `--only lockstep,lockstep_ui,lockstep_month` on dist_nb_3: 121/121 (session 11, lockstep 30,
  lockstep_ui 40, lockstep_month 40), 377 s; on the final build dist_nb_4 (the committed source but for comments):
  122/122 (`lockstep_ui` 41: its fleets engaged this time, so the arrows' cap in contact was checked too), 390 s. The
  offline self-test: 89/89.
- **Open:** the battle log tab lists both sides' messages on both PCs (it was so before; "CL Hela opens fire ..." on
  A); `SoundOption`, `AirApproachMessage`, `BreakOnNewSighting` and `AirSurfaceMessageLvl` stay A's because the game
  draws shared random numbers under them for message and sound variety: those draws could become display draws, and
  the options each PC's own; a check of the game data files (a fingerprint of `Data\` in the hello) and the CPU
  question remain; the suite has no two-PC scenario yet (TESTING proposes one).

## Investigation 2026-10-20: live battles between two PCs, CPUs and data files (v0.4.1)
Research agent, branch `fix-net` (round 2 of the report above: A's start check failed on every attempt, "the loaded
battles differ"; the players can't send logs). Engine level 16. Static work in the offline workspace (a byte scan for
the x87 transcendental opcodes, `rdis.py`'s call graph); live runs nb10-nb13 with the session folder's driver (jobs
`j10_*.py` to `j13_*.py`). The test PC has an AMD CPU (Zen 5).
- **Where the CPU could matter.** Intel and AMD don't round FSIN, FCOS, FSINCOS, FPTAN, FPATAN, FYL2X, FYL2XP1 and
  F2XM1 identically (+ - * / and FSQRT are exact). In 1.01.44 every such instruction sits in one of 11 tiny RTL leaf
  routines (GAME-INTERNALS, "Two PCs: CPUs and data files"): System's Exp, Cos, Sin, SinCos, Ln, ArcTan, an
  ArcSin(Single), an ArcTan2, and the globe unit's Tan, SinCos and ArcTan2, which the simulation calls for bearings,
  hit chances, line-abreast and line-ahead points and air formation moves. A battle load calls them ~25-40k times
  (the same count on both PCs), a battle minute ~2M times (mostly the map's projections).
- **What a CPU difference does (perturbation: `RTW3MP_TEST_X87` on B, both PCs on their own x87).** Every result of
  the 11 routines on B nudged by one unit in the last place of the 64-bit mantissa (the size of a real Intel/AMD
  difference): READY equal, 5 minutes equal. By one unit of a double (2048x more): READY equal over a resync, 40
  minutes equal with gunfire (3.4M results nudged). By about 1e-6 of the value (`ppm`): **every load differed**
  ("the loaded battles differ: orders", four resyncs, then B dropped): the computer-controlled divisions' orders
  (`DivisionCourse`, `DivisionSpeed`, desync_diff) go through these routines and a rounding. So the dependency exists,
  but a real CPU difference is 2^-64 relative, about 13 orders of magnitude below what changed a load here: not the
  players' cause on its own.
- **The same maths on every CPU anyway (`x87.cpp`, `softmath.h`).** The 11 routines are hooked by their exact bytes
  (all of them are found before any is hooked: the two copies of SinCos and ArcTan2 have identical bytes). Outside a
  live battle a hook jumps to the game's own routine. In a live battle (from the load to its end, both PCs: the
  start's `sm=1`) the argument is rounded to a double by the x87 (the battle's control word) and computed by fdlibm's
  algorithms in SSE2 doubles (only + - * / and sqrt, round to nearest, every SSE exception masked by a guard): the
  same bits on every x86 CPU. Accuracy against this CPU's x87 (offline, 300k arguments each): sin, cos, tan, atan,
  atan2, exp, log within 1-2 ulps for the game's arguments (for |x| ~ 1e5 the x87's own 66-bit pi is the error: this
  AMD returns the classic Intel sin(pi) = 1.2246063538223773e-16). A stub check at install calls each hook as the
  game's Delphi code does (registers, stack, results) and the battle keeps each CPU's own maths if it fails. **Proof:**
  B nudging by `ppm` with the software maths on: READY equal, 40 minutes equal (gunfire from minute 31), B's
  `nudged` count unchanged from the load to minute 40 (the hardware path unused).
- **Speed.** Per call (in-game `ls x87 bench`, ns, this CPU/software): sin 17.5/41.4, cos 20.3/42.7, atan 36.6/32.2,
  exp 9.6/18.0, ln 16.3/30.9; a battle minute calls them ~3-4M times (the globe's SinCos and ArcTan2 for the map), about 70-90 ms of CPU per 10 battle minutes. Wall time of 10 battle minutes in one battle, alternating with a resync between (nb13): software 6.99, 7.95, 6.90 s; each CPU's own 8.98, 9.00, 6.82 s: no measurable cost.
- **The x87 control word.** Since level 15 B loads with A's word; level 16 checks it on both PCs before and after every
  simulation step, at every minute's boundary, every release and every hash, puts A's back and logs it (`lockstep: FPU
  control word <w> at <where>, the battle's (A's) is <a>: put back (<n> times in this battle)`, the first 20, then every
  100th). B's own word goes into its `ready` and into the diagnostics when it differs from A's.
- **The game data files (`datafiles.cpp`).** A file-open probe (`RTW3MP_TEST_FILELOG`) of the 1900 test battle: the load
  opens Gundata, HorPen, VerPen, Torpedodata, BasicData, MapData and every nation's WarInfo `.dat`, the map layers
  WorldRTW, RTWShallow and NoGo, two coastal battery designs in `Data\IDes`, the ensigns in `Flags`, and the slot's
  save files; the minutes open no game file. `Data` but its `Graphics` (drawings only) is hashed when a session
  starts (1417 files; 11 s on a cold disk while two copies start, a background thread). A's start carries the
  combined hash (`dh=`); a B whose hash differs refuses before loading with its list (`dm=`, `name:hash|`), A names the
  files to both (`datadiff`) and drops B, who never loaded: no resync. nb12 (`ls testdatafile
  Data\Gundata.dat;Data\Extra.dat` on B): A `Captain's game data files differ from this PC's: 2 files:
  Data\Gundata.dat, Data\Extra.dat (only on one PC)`, `Captain left the live battle (...). The AI commands Germany now`;
  B `The live battle could not start: your game data files differ from Admiral's: Data\Gundata.dat, Data\Extra.dat
  (only on one PC). The battle is fought against the computer.` and the notice `No live battle: ...`.
- **Diagnostics a player can relay.** B's `ready` carries the READY hash's parts (`g=`: the ten groups, the clock with
  the date, the random-number state), its CPU vendor and own control word; A's `desync` carries `diag=`. The chat line
  on both PCs: `Synchronising the battle again (the loaded battles differ: orders; CPUs AMD (Admiral), Intel
  (Captain))...`, the notice on both: `Live battle: the loaded battles differ: orders; CPUs AMD (Admiral), Intel
  (Captain) (loading again)`, the final drop: `the battles could not be kept in sync (orders; CPUs ...)` (nb10,
  `ls testreadydiff`; nb11's `ppm` run). Parts in plain words: positions, damage, ammunition, sightings,
  identification, orders, aircraft, torpedoes, forces, submarines, clock, random numbers. The hello and the welcome
  carry `cpu=` and `dh=`: `Captain: CPU Intel (this host: AMD); game data files the same as this host's|DIFFERENT|not
  compared yet` and `joined Admiral: CPU AMD there (this PC: Intel); ...`.
- **Gotchas.** Hooking the first copy of SinCos/ArcTan2 changed its bytes, so a search after it found only the second
  (nb10: 9 of 11 hooked): collect every target first. `KVEncode` turns newlines into spaces: a list in one KV value
  needs another separator (nb11 named all 1418 files "only on one PC"). The client never ran `lockstep::Prepare` (host
  only): its scan starts in `Join`. MSVC's inline asm can't put a label inside a macro: each stub's software and test
  paths are their own naked functions (`jne NAME_soft`, C4414 silenced for them). A Python `open(p, 'w',
  newline='\\n')` raised after truncating the file.
- **Open:** the players' failure stays unexplained by the CPU (13 orders of magnitude) and by every two-PC condition
  tried; with level 16 a data-file difference no longer reaches the start check, and the next report will name what
  differed. Regression on the final build (`files\dist_nb_9`): `--only lockstep,lockstep_ui,lockstep_month` 122/122, game open 424 s; offline self-test 89/89. Game open in nb10-nb13: 174 + 585 + 571 + 150 s; slot 1 restored and the Preferences file unchanged after every run, 0.4.0 (`dist_vp_2`) back in the game folder after every run.

## Investigation 2026-10-20: each player's own relations: tension, wars and battles of a joined player's nation (v0.4.1)
- Research agent, branch `fix-strat` from main d539e4d (0.4.0). A report from a match of two players on 0.3.2 (the host
  plays nation 0, the joined player nation K): "Tension is the same as the host and non host player they should be
  separate", "The wars were shared amongst players since the tension bars are not independent", and no battles for the
  joined player, neither against computer nations nor against the host. Static reading of 1.01.44, then a facts run on
  the released 0.4.0 (dist_vp_2, game open 155 s) with the 1900 test campaign (host Great Britain = 0, client Germany =
  1, Spain = 7, Russia = 3); the client's screen was read from its memory (the strategic screen at RVA 0x723000 and its
  controls; no screen capture).
- **The data.** Every nation holds one tension array (TBuilderNation +0x256 `Tension` = its relation with nation 0,
  +0x25a.. `AITension[1..8]`; `Allied` +0x29a with nation 0, `AIAlliance[1..8]` cells +0x276+4j; war = above 40), so
  nation K's relations exist on their own: K-host in K's `Tension`, K-j in K's `AITension[j]` (and j's, `SetAITension`
  writes both). In the facts run K's data was 0:5 2:7 3:3 4:3 5:3 6:3 7:4 8:3, the host's 1:5 2:8 3:4 4:4 5:1 6:2 7:3 8:3.
- **Everything that shows relations reads nation 0's.** `TfrmBuildCamp.UpdateUIValues` (0xc8018c) names slots 1-8 of
  the tension panel (`imgFlag<i>.Hint` = GetBNation(i).Name, " (Treaty)" while `Allied` > 0) and calls
  `ShowTensionLevels` (0xc7f4d8): per slot `ShowOneTensionLevel(i, Tension<i>)` (0xc7f1e4: size, colour, "Tension with X
  is N" or "You are at war with X" from GetBNation(i).Tension), `lbWar<i>` (Tension > 40), `lbBlock<i>` (Blockaded),
  `imgAlly<i>` (Allied > 0), `stIntel<i>` (IntelToString: GetBNation(i).IntelligenceSpending). The war state comes from
  the war counter ([0xf52ff4], `0xccd5fc` = War > 0): the "War results" box `gbWar`, `lbWar` "At war for N months.",
  `lbPlayerNation`'s hint "At peace for ..."; while War > 0 UpdateUIValues also calls `CancelTreaty` and zeroes every
  nation's DisplacementLimit (+0x17d). `SetValues` (0xc7fbcc) loads the flags (imgPlayerNation from PlayerNation,
  imgFlag<i> from GetBNation(i)) and ends with UpdateUIValues. A flag's click opens `TdlgNationData.Execute(i)` (the
  player's own flag: 0). The map's strength bars (`DrawAreaStrengthBars` 0xd10900, from DrawMapDetails) colour nations
  1-8 by GetTensionColor(Tension). The relations window (`TdlgRelations.Execute`, the 9x9 matrix) reads Tension for
  pairs with 0 and AITension otherwise: complete. The game's own helpers compare with PlayerNation but read `Tension`:
  `TBuilderNation.IsEnemy` 0xc66410 (only through the player's coalition), `NationIsAtWar` 0xc634f8 (PlayerNation: War
  > 0), `IsAtAIWar` 0xc68ecc (false for PlayerNation).
- **So the joined player's screen showed the host's relations** (PlayerNation = K, the fields relative to nation 0): in
  Aug 1900 the client's panel equalled the host's ("1:Germany is 5 ... 8:Austria-Hungary is 3", slot 1 being Germany
  itself with its tension with the host), after `settension 1 7 50` (K at war with Spain in the data, as `battles`
  does) still "Spain is 3", and after a war of the host with Russia (reserve 3, `diploas 0 war 3`, applydiplo, release
  3: WarStartsWithNation) "3: You are at war with Russia [WAR]", the War results box and "At war for 2 months." on the
  client, while K's relation with Russia stayed 3 in the data. The host's war never touched K's data; on the screen it
  looked like the client's war, and the client's real war (Spain) was invisible.
- **K's relations follow the game's AI rules** (the game runs every nation but nation 0 as AI): `CheckTurnEvents` runs
  `CheckAITensionEvents` (0xd2dc68) and `DoAITensionChanges` per nation; `ChangeAITension` (0xc67904) moves a pair by
  +-1 with a cause that goes into the host's turn messages ("Tension between USA and France decreases because they share
  common values."). An AI war breaks out (0xd2e598) for nation i's most tense AI partner m when 10 < t < 40 and
  Random(100) < t, and only while the player is NOT at war (0xd2e602 `call 0xccd5fc`; otherwise the tension drops by 1);
  "War has broken out between X and Y!" appears on the host. AI peace (`HandleAIPeace` 0xd32f98: "The war between ...
  has ended", VP of nations 1-8 zeroed) comes at random, only while the player is NOT at war and the nation is not at
  war with the player (0xd2ddd9..0xd2dde9), or forced by unrest. So K can go to war on its own, but only by the AI's
  rule, frozen while the host is at war, and without any message on K's PC. Over the months K's tensions drifted by
  +-1..2 (`lockstep_month`: France 7 -> 5 -> 3). K's relation with the host follows the host's player rules (below).
- **Nation 0 only (the player's machinery):** `RandomTensionChanges` (0xc81100: 50 `TensionChangeWithMessage` calls with
  explanations, the leader's speeches, "X asks your opinion" questions), `TensionChangeWithMessage` (0xd46a3c: the
  turn message "Tension with X increases ..."), `CheckForWar` (0xc84f04, from DoEndOfTurn: the `WillGoToWar` countdown
  +0x1bd with spy warnings "they plan to declare war on us in N months", rumours, "X has declared war on us!", allies
  joining, surprise attacks, invasions), `WarStartsWithNation` (0xd333bc: Tension 50, War := 1, Allied 0, TechSharing
  0, BudgetModifier +1), `TensionEffectOfPreviousWars`, the peace events (`GetPeaceEvent`, `NegotiatePeace`, `DoPeace`),
  the war counter step (DoStrategicTurn 0xce1f69: ++ at war, -- at peace), `CheckBlockadeStatus`' VP,
  `CheckRaiderActivity`, `WarHelpFromAllies`, `CheckBuildingShipsForWar`, and the battle generator `CheckForBattles2`
  (0xcecb10: PlayerNation against nations with Tension > 40: invasions, raider vs patrols, home areas, possessions,
  `GetRandomEnemy` -> `CheckAttackLocation`). Events for K are rolled on K's PC with K's relations swapped in (0.2.4).
- **Battles of K.** Against AI nations: `CheckAIvsAIBattles` (0xd32a50) for pairs with AITension > 40 (50 % per
  nation, areas walked) -> `CheckAivsAIBattleInArea` (0xd32c2c: 50 %, ships of both in the area, Random(10) < the ship
  counts, a size-based chance) -> `DoBattleBetweenAINations`, which the mod defers to K's battle phase. In the facts run
  the first month gave "battle request 1: nation 1 vs 7 in Northern Europe (29:16 ships)", sent to the client, where
  the game's `CheckAttackLocation` said "The Spanish navy declines battle! You gain 300 VP!" (`AIDeclinesBattle`
  0xcd2188, before any preview: `add [PlayerNation+0x175], N`); the client reported "nobattle" and the host let the
  game resolve it as a battle between AI nations: "In a battle in Northern Europe ... the German ship DD S22 has been
  sunk". Since CheckAIvsAIBattles walks nations 1..8 in order, a joined nation with a low index usually attacks, and a
  weaker AI enemy usually declines: such a player saw battles announced and never fought them. Against the host: the
  host's `CheckForBattles2` -> DoBattle -> a live battle (`lockstep_month` gets one in the first war month in this
  branch's runs). The host's own war with Russia brought the host a battle in its first month in the facts run.
- **Design** for the per-player war machinery, awaiting a decision: (1) keep the game's AI rules for K's pairs and make
  them fair and visible (a war counter per player sent with the month; K's AI wars and AI peace no longer frozen while
  the host is at war; the host forwards the month's messages about K to K's player; "Propose peace" to an AI enemy);
  (2) the player's relation routines (RandomTensionChanges, CheckForWar, peace events) for K on K's own PC at the start
  of its month under the K swap, the AI rules switched off for K's pairs on the host, the results applied by the host
  like events, and K's per-relation state (WillGoToWar, ...) kept by the mod; (3) the same in the host's month (its
  dialogs would open on the host; not recommended). The player's battle generator for K (CheckForBattles2 in K's view,
  its CheckAttackLocation turned into K's battle-phase requests) is a separate step for either.

## Fix 2026-10-20: a joined player's screen showed the host's relations; an enemy's decline became a battle (v0.4.1)
- **The relations view (`game.cpp`).** `ShownNationIndex()` is K when PlayerNation is BuilderNations[K] with K in 1..8
  (a joined player's PC, also after leaving the session), else 0 (the host, single player, a battle slot where K is
  nation 0). While a display routine runs, `KViewBegin` puts nation K at index 0 and the host's nation at index K and
  sets every nation's `Tension`/`Allied` to its relation with K (computed from the cells first); `KViewEnd` puts
  everything back, also when a game exception unwinds (`__finally`); depth-counted, and not while an event dialog has
  its own swap. Hooks (all on the main thread, no-ops when ShownNationIndex() is 0): `ShowTensionLevels` (in the view;
  first the flags' hints, as UpdateUIValues sets them, from the view's nations), `DrawAreaStrengthBars` (in the view),
  `SetValues` (afterwards slot K's flag picture = the host's `GetFlagFileName`), `TdlgNationData.Execute` (a flag's
  nation index 0 <-> K), `UpdateUIValues` (afterwards the War results box, `lbWar` and the player name's hint follow K's
  own wars: "At war with Spain." / "At war with N nations." / "At peace."; K's war months are unknown, the war counter
  is the host's). VCL and RTL routines without RTTI names, from call sites checked at startup: TControl.SetVisible
  (0x981b54, `setg dl; mov eax,[ebx+lbWar1]; call` in ShowTensionLevels), TControl.SetText (0x981ca0, `mov edx,[ebp-4];
  mov eax,[ebx+stIntel1]; call`), System._UStrAsg (0x83bccc, `mov eax,[ebx+imgFlag1]; add eax,FHint; call` in
  UpdateUIValues; it copies a literal string, refcount < 0). Log: `Relations view: ok (...)`, and on every change
  `relations view: nation 1 sees its own relations: Great Britain 5 France 7 ...; at peace`.
- What it shows on K's PC: slot K = the host's nation (flag, name, K's tension with it, war and alliance marks), the
  other slots K's relations, the War results box and the war line from K's wars, a flag's click the nation of its
  slot. Left as they were: the intelligence labels (slot K shows the host's own record; per-player intelligence is
  another branch's work), the blockade marks (a nation's own Blockaded flag, set by the host's blockade check), the
  relations window (it shows every pair already), the turn messages and warnings (the host's month: see the
  investigation). The game's war-counter side effects of UpdateUIValues (CancelTreaty, DisplacementLimit) still follow
  the host's counter.
- **An enemy's decline is no battle (`session.cpp`).** The client compares nation 0's VP (K in the battle slot) around
  `StartPlayerBattle`; no preview and points gained, in a battle against an AI nation, means the game's
  `AIDeclinesBattle`: the client saves the battle slot and sends the result (`ExtractBattleResult`) with the outcome
  `avoided`; the host merges it like a fought battle (the VP only) and resolves nothing: `battle N applied: 0 ships
  changed (0 fields), 0 removed, VP 300/0, prestige 0 (...); the enemy declined battle`, chat "The Spanish navy declined
  battle against Germany in Northern Europe." Before, the client's "nobattle" made the host run
  `DoBattleBetweenAINations`. A battle the player declines in the preview, and a live battle, are unchanged.
- **Bridge:** `relview` (read-only, also while an event dialog is open). **Runner:** `relview`/`relview_check`;
  `session` checks the client's and the host's screens, `lockstep_month` both screens at war and the client's after the
  peace; BattleDriver knows the outcome `avoided`. MP_MOD_VERSION 0.4.1; protocol 3 and engine level 14 unchanged.
- **Older peers.** A 0.4.0 client (or host) shows the host's relations as before; the view needs no message, so a
  0.4.1 client shows its own relations under any host. A 0.4.0 host takes `avoided` for a battle not fought and has the
  game resolve it (the 0.4.0 behaviour); a 0.4.0 client still reports "nobattle" to a 0.4.1 host, which resolves it.
- **Tests** (frozen builds dist_rf_1, dist_rf_2; the research driver `rf_live.py` in the session folder, regression.py's
  helpers): dist_rf_1, 16/16 (game open 61 s): the client's panel "1:Great Britain is 5 | 2:France is 7 | 3:Russia is
  3 | ... | 7:Spain is 4", "At peace."; a month later "7:You are at war with Spain [WAR]", War results box, "At war with
  Spain."; with the host at war with Russia the client's Russia slot "Tension with Russia is 3" and K's data at peace
  with Russia; the flags' clicks open nation 0 (slot 1) and nation 2 (slot 2); the host's screen unchanged throughout.
  dist_rf_2: the same display checks, a battle Spain started fought on the client and merged ("battle 1 applied: 18 ships
  changed", 93 s); with two injected battles Germany started, battle 1 `avoided` and merged with VP 300/0, battle 2
  fought (444 s: the driver then left a third battle without auto-run, a driver bug). Suite `--only
  session,lockstep_month,lockstep_pvp` on dist_rf_2: 82/82 (session 13/13, lockstep_pvp 26/26, lockstep_month 43/43, a
  live battle in the first war month; game open 260 s).

## Fix 2026-10-20: joined players' intelligence mirrored the host's; no trade protection warnings (v0.4.1)
Research agent, branch `fix-intel` from main d539e4d (the 0.4.0 release). Two reports from a match between two PCs on
0.3.2: "Non host player spying mirrors host." and "non host player does not get warnings for not having ships on
Trade protection." Static work on 1.01.44 (the game facts are in
[GAME-INTERNALS](docs/GAME-INTERNALS.md#intelligence-trade-protection-and-foreign-stations)), validation on the
installed 0.4.0, the fix, live proofs with host and client on one PC. Commits 256d1f6 (the mod), b1e2848 (the
runner), 385517b (a bridge read), then the docs. Protocol 3 and live-battle engine level 14 unchanged.
- **0.4.0, as validated** (the release DLLs, slot 1 at Aug 1900, host Great Britain = nation 0, client Germany =
  nation 1, two months):
  - The client's politics panel and budget showed the host's intelligence (flag labels `Med *|None|Med *|None|Low|...`,
    the budget's Intelligence line 150 on both PCs). Its + on France was lost with its turn; its + under its own flag
    (Germany) raised the host's spending on Germany (`[Nation1] IntelligenceSpending` 2 in the host's save after the
    merge). Its report list was the host's (6 -> 7 -> 8 entries, with the host's reports on Germany itself: "Apr 1900
    Germany - New ship under construction of class B Brandenburg is rumoured to carry 9 in main guns.").
  - Germany paid nothing for intelligence and got the AI's intelligence money: funds 3934 -> 5999 -> 8064 (+2065 a
    month) while the client's screen showed a monthly balance of 123, then -27.
  - The host's Turn asked "There are insufficient ships on foreign stations! Continue anyway? ..." both months; the
    client's Submit asked nothing (Germany has no shortfall in 1900; proof 3 uses a nation with one).
- **Cause.**
  - The game keeps its player's intelligence on the target nations: `IntelligenceSpending` (+0x171) and `IntelEffort`
    (+0x1ac) of nation j are nation 0's spending on and effort against j; the reports are one list
    (`TfrmBuildCamp.IntelReports` +0x320c, saved as `[IntelReports]`). The month package is the host's save, so the
    client loaded the host's values and reports. Its +/- (`SpeedButton1Click` -> `ChangeIntel` 0xd3c538) changed the
    `[Nation<j>]` of other nations, which its turn doesn't carry, and under its own flag its own `[NationK]`, which the
    merge took. `GetIntelCostForPlayerNation` (0xc989b8) sums nations 1-8; `GetMonthlyBalance` (0xc5d2ec) subtracts it
    for PlayerNation only and adds `ComputeAINationIntelCost` (0xcae594) for every other nation: in the host's month K
    got the AI's intelligence money. The monthly spying (`CheckTurnEvents` -> `CheckIntelligence` 0xc86244) is the
    player's only; `CheckAISpying` (0xcba67c) ran for K as for an AI.
  - The Turn's questions are in `PlayerChecksBeforeTurn` (0xce28c4, from `DoTurn2`), which a joined player's Turn never
    runs: it submits. In the month the player's parts of `CheckShipsOnForeignStations` (0xcb6718 ->
    `CheckForceOnForeignStations(False)` 0xcf5f54) and `CheckPatrols` (0xcc7dc4) act for PlayerNation only, and the
    mod skips the AI's `AISendShipsToForeignStations`/`AISendShipToColonies` for human nations (since 0.1.0): K got
    neither the player's warnings and consequences nor the AI's ship moves.
- **Fix** (`src/mod/nationplay.cpp`, design in
  [ARCHITECTURE](docs/ARCHITECTURE.md#per-player-intelligence-trade-protection-and-foreign-stations)):
  - The host keeps each joined nation's spending, effort, reports and foreign-station warning in `rtw3mp_nations.ini`
    (its slot, with the campaign key), puts them into the player's month package (`MSG_STATE` header `intel=1`) and
    takes them back from its turn (`[MPIntel] Spend=/Effort=` and `[IntelReports]` in `nation.bcs`). The merge keeps
    the host's `IntelligenceSpending`/`IntelEffort` in `[NationK]`.
  - Player's PC: `GetIntelCostForPlayerNation`, `IntelToString` and `ChangeIntel` hooked (every nation but its own
    counts; its own flag stands for the host's nation). The monthly spying check is the game's `CheckIntelligence` with
    K seen as the player (the "view": nation list 0 <-> K, every nation's relation with K, the war counter from K's
    wars), run when the month's event roll finds no event; the game's dialogs show on the player's PC, the results go
    to the host at once as an `MSG_EVENT` (`event=-1`, `intel=1`, `ireports`, `ieffort`, `tension`, `delta`).
  - Host month: `ComputeAINationIntelCost` returns minus K's cost (levels x 10 x FleetSize); `CheckAISpying` is skipped
    for K; after `CheckShipsOnForeignStations` the player's parts of it and of `CheckPatrols` run for each joined
    nation in the view, with the game's chances and texts, as a replica (`CheckForceOnForeignStations` also rolls world
    events); the texts reach the player as notices with the new month.
  - Submit asks `PlayerChecksBeforeTurn`'s trade protection (at war) and foreign stations questions for K in the game's
    message box (`MessageDlgPosHelp` 0xa06eb0, found by its call there); No goes back ("Not submitted: you chose to go
    back at the game's warning."); a submit the time limit forces asks nothing. `GetPatrolRequirement` hooked: the
    budget panel's "N of M" is K's.
- **What an old peer sees** (both directions keep protocol 3): a 0.4.0 player of a 0.4.1 host gets its own
  intelligence in its month (empty at first) but no spying check and no questions; its spending changes are lost
  (no `[MPIntel]`; the merge keeps the host's keys), it pays nothing and gets no AI intelligence money; the month's
  foreign-station and patrol consequences apply to its nation and their notices reach it as chat lines. A 0.4.1 player
  of a 0.4.0 host (no `intel=1`) keeps the 0.4.0 intelligence (the host's) and gets the Turn's questions (local).
- **Live proofs** (the drivers are kept outside the repo; host and client on one PC):
  - Proof 1 (dist_it_4, slot 1, Germany, 3 months): the client's own panel after `intelbtn` 2,2,2,1,1,3 (France High,
    the host's nation Med under its own flag, Russia Low; Intelligence 180, balance -57), the host's unchanged (its own
    Russia Med and Japan Low; `[Nation1] IntelligenceSpending` 0). Host: `intel: nation 1's turn: spending
    2,0,3,1,0,0,0,0,0 (cost 180 a month)`, `AI suppressed: CheckAISpying for nation 1`, `intelinfo` `month1=intel:-180`.
    The client's list was its own: 0, then 7 after its checks ("Sep 1900 Russia - New ship under construction of class
    DD Verni is rumoured to carry 2 in main guns.", "Sep 1900 Great Britain - The ship CA Europa will reportedly be
    commissioned in 0 months.", "Sep 1900 USA - USA has a budget balance of 489"), also as chat lines; the host's its
    own (6 -> 10); the saves separate (slot 1: 8 entries, slot 77: 7). An enemy spy caught on the client in the third
    month: host `event of Captain (nation 1, #-1 'Intelligence'): tension 1-4 3->4 BudgetModifier+1`. Germany's funds
    +1343 and +1784 in months 2 and 3 (0.4.0: +2065); month 1 gave +18,460 (see the observations).
  - Proof 3 (dist_it_6, slot 1, the USA = nation 4, its 45 ships put in reserve on the client so that its shortfall
    lasts; 4 months): Submit -> the foreign stations question -> No: not submitted, `submit: cancelled at a Turn
    warning`; Submit -> Yes: submitted; the question again each month. Host: `nation 4 month: at peace, foreign
    stations short of 10000 (required 10000) in 2 areas, ships on foreign stations 0: insufficient` every month; Oct
    1900 "The President has been made aware of a lack of ships on foreign stations. He strongly urges you to remedy the
    situation.", Dec 1900 "The President is displeased with the lack of ships on foreign stations! Your prestige
    suffers. (Prestige 20 -> 19)", both as chat notices on the client; prestige 19 on both PCs; the quiet months
    logged (`no consequence this month (rebellion roll: the possession's area has its force; war counter -68,
    1900/09)`). `fsmonth 4 12`: 10 notices, prestige 19 -> 16. Without the reserve (dist_it_5) the USA's shortfall
    (6000 of 6000 tons in one area) closed during the first month, when ships under way arrived: the question at the
    first Submit, nothing afterwards (correct: the game counts ships where they are).
  - Proof 2 (dist_it_8, slot 9 at Jan 1935, Germany at war with France by `settension 1 2 50`): Submit -> "You have 0
    ships on trade protection duties, but 16 are required. This will give VP to the enemy and might hurt your
    prestige. Continue anyway?" (No, then Yes); the month: `nation 1 month: at war, 0 ships on trade protection of 16
    required` and "The Führer is annoyed that you cannot safeguard our coasts with adequate patrols. ..." on the
    client; the client's budget panel "0 of 8" at peace and "0 of 16" at war (the host's own "0 of 11"); the client's
    spying check at war showed the game's "The head of naval intelligence suggests that as we are war with France,
    ..." and aircraft data on the client.
  - Suite on dist_it_8 (= 385517b): `session` 11/11, `lockstep_month` 40/40 (dist_it_7: the same). Game open in all:
    about 12 minutes (validation 49 s, proofs 106 + 77 + 74 + 92 + 55 + 58 s, suites 99 + 109 s).
- **Test tooling.** Bridge: `intelinfo`, `tradecheck`, `shipstatus`, `intelbtn`, `intelcheck`, `fsmonth`; `submit` is
  asynchronous (`status` `submitting=1` until done). Runner: the client's intel list may be empty; run_month and
  ls_month wait for `submitting=0` (proof 2 on dist_it_6 met "Error in SaveBuildCampaign procedure! Cannot create
  file" when the runner read slot 77 while the resubmit saved); events i) ignores the spy-caught questions.
- **Observations, not changed:**
  - The AI's money still reaches joined nations: `ComputeExpenses` (25 x FleetSize a month and more for poorer
    nations, BaseResources floor) and `CheckBBNumbers` ("<nation> has increased the naval budget to remain a global
    naval power.", Funds + FleetSize x 6000 and BudgetModifier + 1, 1 in 10 a month when a rival has two more
    battleships): very likely Germany's +18,460 in proof 1's first month (18,000 at FleetSize 3). `ReduceTimeLimits`
    can halt a joined nation's most expensive ship (`HaltMostExpensiveShip`) when its balance is negative and its funds
    low; paying for its own intelligence makes that a little likelier.
  - `MovePlayerShipsOnForeignStations` (the game's moves of its player's foreign-station ships) is not run for joined
    nations; the AI routines' passive reports and the beliefs about enemy ships (`EnemyClassName`, `PlayerIntel`,
    aircraft data) stay the host's.
  - The spy events change K's relations through the event path (`tension` key, clamped as events'): fix-strat's
    tension work sees them as event changes of the K-j pair.

## Testing 2026-10-20: twopc, live battles under two-PC conditions (v0.4.1)
Regression agent, branch `reg-041` (from `fix-net` 2931c17: engine level 15, 0.4.1, protocol 3). The suite gets the
two-PC scenario the investigation of 2026-10-19 proposed, built from its harness pieces; no product code changed.
Snapshot `dist_tp_1` (built in `reg-041` from 2931c17 without warnings; RTW3MP D95BE2AB2010788A) installed for each
run, `dist_vp_2` (0.4.0) back after each run, slot 1 and the Preferences restored (verified by hash), slot 2 untouched.
- **The scenario `twopc`** (group `twopc`; the default run: short; `--stress`: full; docs/TESTING.md, the catalog and
  "Two-PC conditions"):
  - a) B (the client) leaves, is closed and started again with `RTW3MP_TEST_LOCALE=comma`; the shared Preferences file
    holds B's variant from B's start to the scenario's end (the runner backs the file up for a run with `twopc`): the 14
    battle preferences a live battle takes from A (`kPrefs`, read from the source) each differ from A's `ls prefs`, and
    22 display keys (dark mode, scale, sound volume, fog of war, English ranks, German characters, the break and message
    options, ship flags, damage meter, ..., the battle window's 1400x880). B joins through `tools/net_delay_proxy.py` on
    47634 (default 120 ms +-60 ms each way, 4000 kbit/s; `--stress` 200 +-100 ms, 2000 kbit/s; `NET_PROXY` in the
    runner).
  - b) a test battle: the start check (READY `all` and `ident`), B's battle preferences (the log line with all 14 B's ->
    A's, `ls prefs`: A's 14, B's display ones, B's own kept for after), B's ensign, both maps at the load, the window
    sizes, every division on the AI, minutes from both sides, the maps between minutes, a resync (the maps again), the
    minutes' hashes, the end (default: `ls endsoon`, the game ends it inside a minute; `--stress`: A drops B) and B's
    own preferences back; `--stress` adds the map check's negative control (`ls mapview off` on B) and the battle
    screen's Preferences dialog on B.
  - c) war 0-1 and real months until CheckForBattles2 starts the host's battle against nation 1 (`ls_month` with a new
    `live_hook`): the same start, preference, ensign, map and size checks, lockstep_month's `host_battle_checks` (the
    month goes on), B's own preferences back; `--stress`: a resync in that battle, the game ends it inside a minute,
    and a normal month after the peace.
  - d) B's own Preferences file unchanged (the shared file still B's variant byte for byte: nothing wrote it, neither B
    with A's battle preferences in memory nor A), both copies' `ls prefs` their own, `ls_after_checks`, `ls_log_scan` (no
    LEAK with `ls leakcheck 1` on both copies, no lockstep error, the identification), the proxy's statistics (bytes
    towards B, the bandwidth cap's waits). e) the run's conditions again: B leaves, the proxy stops, the run's
    Preferences back (hash), B started again normally and in the session directly, the host's battle window its own size.
- **B's map check** compares `ls mapview` with the simulation's sighting state at the same boundary: B's own DESYNC
  report (`ls desyncreport`, the state section: each ship's `SightingLevel`, `RadarSightingLevel`,
  `SeenByHumanControlledShips`, `Sunk` by its division's `Side`; read and deleted at once; both copies' states are
  equal, so it serves A's mirror too). B's map must show every own ship of a human force (the game hides a computer
  force's own ships until they are spotted: A had 9 of its 11 ships in computer forces in the month battles), no
  division flag of A, no more A wakes than A ships shown, and of A's ships exactly those B's side sights
  (`max(SightingLevel, RadarSightingLevel) > 0` and `SeenByHumanControlledShips`; right after a load the levels alone,
  as the map pass does). So "B's own ships visible, A's unspotted ships absent" is checked exactly at every load and
  between minutes. The battle file is no reference: the load's own sighting routine sets the levels again (the
  resync's file said 3 of A's 16 ships sighted, the loaded state 4, B's map 4).
- **The probe the product could offer:** `ls mapview` counts what a map shows but not what the side sights; the
  report file works, but `ls mapview ships` (per ship `side:index:shown:sight:radar:seen:sunk`) or `sighted0=`/
  `sighted1=` counts (each side's ships the other side sights by the game's rule) would make the check one bridge call
  and name the ship that differs.
- **Runs** (`--dist` dist_tp_1, the game lock held for each run; game open 150 s + 192 s + 219 s = 561 s):
  - Run 1, `--group twopc`: 65/68. Every check of the product passed (the start checks, the minutes, the maps with A's
    ships partly sighted 4 times, B's preferences and ensign, the month, the Preferences file, the proxy: 9.4 MB towards
    B, no LEAK); three failures were the suite's own assumptions: a battle window keeps its size into the next battle of
    the same game (the month battle started at the test battle's 900x650 and 1560x980; only a new window and a resync
    load take the Preferences' size), and B's battle window is hidden for most of a load and shown only at its end, so a
    30 ms watch with a 0.4 s margin saw nothing of two loads and took the old battle's picture before a resync for the
    load's. Fixed in the suite (ae51d08): the size check asserts that the windows differ; `CoverWatch` polls the form's
    handle every ~3 ms and keeps the state changes.
  - Run 2, `--group twopc`: **68/68** (twopc 57/57 in 175 s). B's 3 loads: hidden, then clear 42-161 ms before "battle
    loaded from slot", shown 2-3 ms after it: the cover works, so the check now requires the clear state on every load
    (an uncovered load would show the window opaque for that last ~0.1 s) with a 50 ms margin. B's map: 0, 3, 2, 2 of
    A's 16 ships in the test battle, 0 and 3 of 12 in the month battle, each exactly the sighted ones; the resync's file
    said 3 sighted, the loaded state 2. The month: Aug -> Sep 1900 with the host's battle in its first month (`drop12`,
    12 minutes equal), lockstep_month's checks, peace.
  - Run 3, `--group twopc --stress`: **88/88** (twopc 77/77 in 201 s). The negative control: with B's map pass off, a
    minute later B's map showed 4 of A's ships with all 16 of their wakes and all 8 of A's flags (0.4.0's picture; the
    capture shows the British ships named and flagged on B's map), the check named both; `ls mapview on` gave B's
    picture back at once. The battle screen's Preferences on B with OK: "RealismSetting 1 -> -1 ... put back" and the
    chat line, B's 14 still A's, B's file unchanged. 17 minutes and a resync in the test battle, the month battle with a
    resync and the game's own end inside a minute (`endsoon`), a normal month after the peace (Aug -> Oct 1900). The
    proxy at 200 +-100 ms and 2000 kbit/s: 13.3 MB towards B, the battle files waiting up to 7.7 s. 4 of B's loads,
    each clear 43-173 ms before its end.
  - After every run: slot 1 restored, the Preferences back (8208607E..., verified), slots 77/87 gone, slot 2 untouched,
    no RTW3.exe or proxy left, no DESYNC report left, `dist_vp_2` back in the game folder (hashes compared), the lock
    released. The offline self-test: 115/115.
- **The three reported symptoms under two-PC conditions** (on one PC: Preferences, locale, window sizes, network): B
  never showed the host's side while loading (the watch) and showed its own ensign; the start check was equal on every
  load (2 test battle loads and 1-2 month battle loads per run, with all 14 battle preferences and 22 display ones
  different, `AIControl` among them); B's own ships were all shown at every check. What one PC can't give (another CPU,
  other data files) stays with fix-net's second round.
- **Product findings:** none failing. Observations: a sighted ship of the other side shows without its wake on both
  maps (B: 0 wakes for 4 of A's ships shown; nb6: 1 of 7), a display detail with no fog-of-war effect; the battle
  screen's Preferences dialog still writes `RealismSetting` -1 on a plain OK (the game's), which the mod puts back;
  B's `ConvertCharacters=1` (umlauts converted in its own campaign) changed nothing the month checks see.
- **Testing gotchas** (docs/TESTING.md, Known traps): `ls desyncreport` logs "DESYNC report ... written" (match
  `DESYNC at minute`; never on A in a month: `ls_drive_live` counts every DESYNC line of A); compare maps with the
  simulation's state, not the battle file; a computer force's own ships are hidden until spotted; B's battle window is
  shown only at a load's end (watch at ~3 ms); a battle window keeps its size into the next battle; the client is
  closed to change its environment, so its unsubmitted plans are lost (the scenario plans after the restart).
- Commits (branch `reg-041`, not pushed): 15cdb11 (the scenario, offline), ae51d08 (after the first live run), and the
  docs with the final suite changes.

## Docs 2026-10-21: 0.4.1 release candidate (v0.4.1)
Docs agent, branch `docs-041` from `rel-041` ffe1677 (`MP_MOD_VERSION` 0.4.1, `MP_LS_LEVEL` 16, protocol 3; the merges
of fix-strat, fix-intel, fix-net and reg-041). Docs only: `git diff ffe1677 HEAD -- src tools` is empty.
- **CHANGELOG:** the drafts of the merged branches made one entry: a summary of who must update (both players of a
  live battle; the host and the joined players for the campaign fixes), Added (the same maths on every CPU, the data
  files compared, the start check's diagnostics and reports), Fixed (each side's map, the load's cover and ensign, the
  battle preferences, the battle screen's Preferences, the x87 control word, the relations view, the AI's decline, the
  joined players' intelligence, the trade protection and foreign station warnings), Compatibility (protocol 3, each
  mix of 0.4.0 and 0.4.1) and Known issues. The heading keeps `<!-- TODO release: date -->`. Corrected from the
  drafts: the declined battle needs 0.4.1 on the host and on the player's PC (the client reports `avoided`, a 0.4.0
  client still "nobattle"); *AI control* was missing before level 15 (`AIControl` +0xc, and `BSSMHC` +0x37b, which no
  dialog sets), so it doesn't count "as before".
- **README:** Joining a session: the Submit questions, what a joined player's screen shows (relations, intelligence,
  trade protection and foreign stations) and the versions each needs. Live battles: the battle preferences listed,
  the second player's own back at the end, the data file check, the map and the load per side, "0.4.0 or newer" for
  the version check. Known limitations: intelligence (the game's passive reports and the beliefs about other navies'
  ships stay the host's), the monthly messages, the money of joined nations (`ComputeExpenses`, `CheckBBNumbers`,
  `HaltMostExpensiveShip`; `MovePlayerShipsOnForeignStations` for the host's nation only), the rework for a later
  release, the unexplained start-check failure. Troubleshooting: "the loaded battles differ" as its own item (what to
  check, what to send or read out, the join log lines with the CPUs and the data files). Upgrading: 0.4.1. The
  install example names the 0.4.1 zip.
- **ARCHITECTURE:** `x87.cpp`, `x87.h`, `softmath.h`, `datafiles.cpp` and `datafiles.h` in the source table and the
  module map; the x87 hooks next to the live battles' other hooks; the client's Turn hook asks the questions; a
  level-16 bullet in the live-battle design (it was only in the version history); the version history: 16 = 0.4.1,
  15 a development level, the log's first line of 0.4.1, level 14 also missed `AIControl`. After the merge of the
  relations view: the intelligence label and +/- of the player's own nation's slot are the host's nation's inside the
  view and outside it (`NP_IntelIdx` maps only the PlayerNation object, which the view moves to index 0).
- **GAME-INTERNALS:** the tactical battles section no longer says "as of 2026-10-17 (0.3.3)"; "In the mod" names the
  `avoided` outcome.
- **MODLOG index:** 0.4.1's kinds in the index's order (Fix, Investigation, Testing, Docs), without "(not released
  yet)"; this entry.
- **Checks:** the links and anchors of every tracked Markdown file, the MODLOG index (every entry linked once, the
  file in date order), the personal-data scan of the changed files, `git diff ffe1677 HEAD -- src tools` empty.

## Testing 2026-10-21: the 0.4.1 release candidate dist_rc041_1 (v0.4.1)
Regression agent, branch `reg-041b` from rel-041 ffe1677. The frozen build `dist_rc041_1` (rel-041 ffe1677: 0.4.1,
live-battle engine 16, protocol 3) installed with `--dist`, the released 0.4.0 (`dist_vp_2`) put back after every run;
slots 1 and 9 and the Preferences identical to the pre-run copies after every run (MD5). Game open 11,226 s in all.
- **Runs** (checks passed; game open):
  - The default run (twopc short): 737/738 (1629 s). The fail: `lockstep_air` c) "after a minute": the 1935 battle's
    "You now have reconnaisance aircraft at your disposal!" box disabled the battle screen and the answerer closed A's
    Air formations window behind it (suite; fixed: a disabled `LS_MODELESS` window is never a question). Again with the
    fix: `lockstep_air` 41/41.
  - `--stress` (missile fixture): 979/989 (6575 s): `twopc` full 77/77, `lockstep_stress` 101/101 (S8 15 of 15),
    `lockstep_rejoin` 47/47, `lockstep_resume` 37/37, `lockstep_torpedo` 20/20, `lockstep_missiles` 23/23,
    `lockstep_month` 70/70, `lockstep_pvp` 27/27. The fails, all suite: `events` 27/32 (below), `air_units` M1 stalled
    240 s on the aircraft types question of a 1935 month (`TfrmAircraftTypes` "Do you want to continue development of
    the aircraft type at the top of the list for 640?" after a bought private venture), and behind it `lockstep_air` b)
    and `lockstep_strike` b), g) could not start their battles. The month's answerer now presses No; the form was
    closed by a posted click so `lockstep_missiles` ran valid. Again (`--only events --group air`): `events` 47/47,
    `air_units` 10/10, `lockstep_air` 41/41, `lockstep_strike` 11/14 (below).
  - `--only lockstep_month --stress`: 77/86 first: the client's spy check (0.4.1) showed the game's spy dialogs at the
    session start ("Our top spy has managed to get hold of the blueprints for ...", then the ship status window) and
    the client's bridge refused the session's checks behind them; `session` now waits for the roll
    (`wait_event_roll`): 86/86 (186 s; battles agree 514 minutes and endsoon).
- **events** (suite): a spy check that found something goes to the host as the player's event of the month (event=-1,
  "event of Captain (nation 1, #-1 'Intelligence')"), and the host ignores any other event of that month, so the
  scenario's forced `eventnow 5` was "second event of Captain in Aug 1900 ignored (one per month)". Consistent with the
  game (its monthly event slot is an event or `CheckIntelligence`); the scenario expects it when it happens and checks
  the event's application only in a month whose slot is free.
- **New checks** (the caller's request): the relations view and the player's own intelligence together on the joined
  player's PC (`session`): `relview` + `intelinfo` name the same nation in every slot (slot 1 = the host's flag with the
  player's own spending on the host's nation, the player's own nation in no slot, client=1); the +/- buttons
  (`intelbtn 1`/`9`, `3`/`11`) change the player's own spending on nation 0 and nation 3, the label, the cost (0 -> 30)
  and the log follow, the whole panel is right while raised and the screen's `stIntel1..8` texts (TStaticText, read
  cross-process) show 'Low *' under the raised flag only; the host's panel as before. Pass on every run since.
- **lockstep_strike, a product issue** (the second strike battle of `--only events --group air`, 07:00 start, the host
  option on): two DESYNCs between the two copies on one PC, at minutes 53 and 74 (resynced, the battle went on). The
  `ident=` part of the hash differs from minute 48 while `sight=` and `all` agree; `desync_diff.py`: at boundary 52 the
  simulation's `TFightingShip.IdentifiedAsType` of S1.0 London and S1.2 Anson is 00 on A and 03 on B; the draws part at
  minute 53, draw 2635 (B draws at rva 0x619e3b first). B's map shows its own side since 0.4.1, and B's copy identified
  ships of the other side that A's did not: a display write into the simulation that the identification's two sets
  (`SwapShip`) and the leak check did not catch. The reports and both logs are kept with the developers' session files
  (`r41\desync_strike`). Not seen in 0.4.0's strike runs, nor in the other five strike battles of this candidate.
  The same run's first battle (13:03) had no search report within 150 minutes; a later one B's four strike formations
  never took off (status 4 "spotted", then gone). With the software maths off (`RTW3_LS_X87SOFT=0`, new, diagnosis
  only) 19/19 once, with them 18/19 and 19/19: these two are not tied to the maths by this sample. `ls x87 selftest`:
  the software routines within 1 ulp of this CPU (tan 2); it does not cover SinCos and ArcTan2, the most called ones.
- **Mixed versions** (TESTING.md, Mixed versions): a) a 0.4.1 host with a 0.4.0 player 22/22, r) a 0.4.0 host with a
  0.4.1 player 25/25; the host's battle against the player is fought against the computer with both levels named, the
  0.4.1 features degrade as documented.
- **Verdict:** NO-GO for publishing 0.4.1 until the identification DESYNC above is understood: everything else passes
  after the suite fixes.
- Commits (branch `reg-041b`, not pushed): 3435b4a, 3b7ce30, 6430e6a and the docs.

## Fix 2026-10-22: the strike battles' DESYNC (the ship-type flags) and the software maths checked (v0.4.1)
The 0.4.1 RC regression's NO-GO: a DESYNC in `lockstep_strike`'s second battle (the host option "Computer flies
carrier strikes"), the identification hash apart from minute 48, two of the host's ships identified only on B's copy;
and doubts about the software maths (one strike battle without a search report in 150 minutes, one where B's strikes
never took off; the self-test didn't cover SinCos and ArcTan2). Engine level 17, protocol 3.
- **Cause: the campaign's ship-type flags, not a display path.** Both DESYNCs (minutes 53 and 74) part at the same
  draw: B makes an extra Random(100) in `TFightingShip.SetMistakenType` (0xe49e36, rva 0x619e3b). That branch reads a
  unit Boolean, "a battleship exists" (0xf54935); its neighbours say a battlecruiser (0xf54936) and a carrier
  (0xf54937) exists. Only `TfrmBuildCamp.CheckShipTypesExist` (0xd4e314) writes them, called only by
  `ReduceTimeLimits` in the month's processing: the host had processed 1935 months earlier in that run (1), the
  joined player's game never does (0). `SetMistakenType`'s battleship case then mistakes a battleship for a
  battlecruiser on A and a heavy cruiser on B without a draw (the silent identification difference of minutes 48 to
  52: London and Anson A 00, B 03); its carrier case makes a carrier a heavy cruiser on B. Proof that it is no display
  write: after the first resync (epoch 2) the two PCs' states at boundary 73 were identical, identification included,
  and minute 74 still parted at the same draw. The display's only identification writer is `GetTypeAndNameForEnemy`, a
  hooked display routine (its writes go to the display set); the other writers (`IdentifyType`, `IdentifyClass`,
  `SetMistakenType`, `UpdateReport`, the sighting routine's 0x16 reset at 0xdcb223, `AttackShips`) run in the
  simulation (static scan of every write to +0xd70/+0xd6c). A read-only memory diff of the two copies' globals (the
  443 that the simulation's code references) during a strike battle found the flags equal when the host had processed
  no month yet (both 0): why most strike battles passed.
- **Reproduced:** `RTW3_LS_SHIPTYPES=010101,000000,nosync,nohash` (A's flags as after a 1935 month, B's 0, the RC's
  rules): DESYNC at minute 113, the call sites part inside `SetMistakenType` (B's extra draws at rva 0x619d9c,
  0x619dd8, 0x619e3b), Painlevé (a French carrier) identified as a light carrier on A and a heavy cruiser on B.
- **Fix:** A's start carries the three flags (`st=`, every start, resync and rejoin); B sets them before the load and
  gets its own back when the battle ends ("lockstep: ship-type flags 000000 here, Admiral's 010101: the load and the
  battle use Admiral's"); the flags are in the state hash (a difference is a READY mismatch at the load: with
  `nosync` alone both strike battles were given up after "the loaded battles differ"). With `010101,000000` (the fix)
  `lockstep_strike` and `lockstep_air` ran without a DESYNC.
- **Guard:** the leak check (`ls leakcheck 1`, on in every live battle of the suite) now hashes the simulation's set of
  `IdentifiedAsType` between the steps: a display write of the identification into the simulation's set is a LEAK
  `(ident)` (`ls testident <ship> <type>` provokes one; `ls leakcheck ident 0` turns it off). First catch: the suite's
  own `ls testcurspeed` took the leak check's new baseline with the display set in place (a false LEAK of three
  enemies' identification in `lockstep_ui` j/k); the baseline is now taken with the simulation's set in place.
- **The software maths, offline** (a 32-bit test program with build.bat's compiler running the 11 routines' exact x87
  instruction sequences under the battle's control word 0x27F against softmath.h; truth from mpmath; 20 million
  arguments per range: small, large, tiny, denormal, near multiples of pi/2, every ArcTan2 quadrant, logarithms near 1
  and at or below 0, F2XM1's range). Before the fix (max error against the CPU's result, in ulps of a double): Exp 6
  (|x| < 10) to 352 (|x| < 700), Sin/Cos up to 15884 for |x| in 10..1e6 and 4e13 relative near multiples of pi/2,
  Tan 1.84, ArcTan2 1.27 (1.40 at 20M), ArcSin(Single) 1.74, Ln 0.81, ArcTan 0.76; garbage for huge arguments
  (sin(1e19) = 1.6e237, sin(1e300) NaN, cos(1e300) -inf); 45 edge cases differed (cos(pi/2), sin(pi), tan(1),
  tan(pi/2), the out-of-range rules, NaN pairs). The large differences are the CPU's: FSIN/FCOS/FSINCOS/FPTAN reduce
  with a 66-bit pi (0x3243F6A8885A308D3 * 2^-64, confirmed: 0.43-0.53 ulp of the 64-bit result on 48 arguments up to
  k = 10^12; no other precision fits) and the game's Exp rounds `x*log2(e)` and `1 + F2XM1` to 53 bits. softmath.h now
  reproduces both, the out-of-range rules (|x| >= 2^63: Sin and Cos return the argument, Tan the indefinite NaN,
  SinCos the argument as the cosine and NaN as the sine), fdlibm's tangent kernel, an ArcTan2 with the division's
  remainder, FPATAN's NaN-pair rule, Ln of a negative NaN and ArcTan2(+-1, -denormal). After (20M per range, 0x27F):
  | Routine | max ulp (64-bit) | max ulp (double) | double differs | class/sign |
  |---|---|---|---|---|
  | Exp | 2048 | 1.000 | 2.7e-6 | 0 |
  | Sin, Cos, SinCos | 1740 | 0.850 | 3.3% (0.02% near k*pi/2) | 0 |
  | Tan | 1843 | 0.900 | 3.6% | 0 |
  | ArcTan2 | 1737 | 0.848 | | 0 |
  | ArcSin(Single) | 1742 | 0.851 | | 0 |
  | Ln, ArcTan | 1861, 1732 | 0.909, 0.846 | | 0 |
  388 edge cases bit-identical (also under 0x37F). Exp's rare 1-ulp differences: 1 + F2XM1 within 1e-4 ulp of a
  rounding midpoint where this CPU's F2XM1 is half an ulp off (no software can predict it). 32-bit MSVC had computed a
  few final operations on the x87 (results depending on the precision control): forced to SSE2. Cost per call (ns,
  before/after): sincos 34/33, sin 33/30, atan2 31/35, tan 35/40, exp 8/52 (about 40 Exp calls per battle).
- **In-game self-test** (`ls x87 selftest`, logged at install): under 0x27F, now with sincos and atan2 and arguments
  at multiples of pi/2 and 45 degrees: `exp 0 cos 1 sin 1 ln 1 atan 1 tan 1 sincos 1 atan2 1` (AMD).
- **A/B, the battles' oddities:** with the CPU's own maths (`RTW3_LS_X87SOFT=0`, the final engine) one strike battle
  had no search report in 150 minutes (07:00 start) and in another a formation of B launched from the strike dialog
  went from readying straight to gone (never flew): both oddities happen with the hardware maths too, so they are the
  battles' own conditions (the distance and the hour against 150 minutes of 150 nm search, the game's handling of a
  readying strike), not the maths; the suite's checks b) and f) are too strict for them. Software battles: reports
  within 50-155 minutes when found; the same "no report" case at 06:49, 06:53 and 13:32 starts.
- **Policy:** the software maths stays on in every live battle. It now matches the CPU's own results to within one
  ulp of a double everywhere and every special case exactly, costs nothing measurable, and the CPU vendor alone
  doesn't promise equal x87 results (microcode and generations differ; the vendor stays in the hello and the READY
  diagnostics). `RTW3MP_TEST_X87SOFT=0` / `ls x87 soft 0` keep the CPU's maths for diagnosis.
- **Runs** (final build dist_f41_5, every copy with `ls leakcheck 1`): `lockstep_ui` 41/41, `twopc` 57/57 (G1, 383 s); `lockstep_strike` 19/19 (battle 1 06:47, reports in 60 minutes; battle 2 with the host option, the computer's carrier strike within 45 minutes) and `lockstep_air` 41/41 (G2, 644 s); `lockstep_strike` 17/19 and `lockstep_air` 41/41 (G3, 765 s: f) one formation of B launched from the dialog went from readying to gone, as with the hardware maths in H1; g) no computer carrier strike within 240 minutes of a 07:00 battle, as in the RC's air4 run: the battle's conditions); `lockstep_missiles` 23/23 (G4, the missile-era fixture, 137 s). No DESYNC, no LEAK, every minute equal and the identification equal in every strike battle (`ident_differ` empty). Four strike battles, two with the host option.
- Diagnosis runs on the way (dist_rc041_1, dist_f41_1..4): D1, R1/R1b (the `ls shiptypes` gate, then the READY
  mismatch), R1c (the reproduction), F1/F2 (build 4: twopc 57/57, lockstep_air 41/41, the false LEAK above), H1/H2
  (hardware maths). Game open: 4628 s in all. Not pushed.

## Testing 2026-10-22: the 0.4.1 release candidate 2 dist_rc041_2 (v0.4.1)
Regression agent, branch `reg-041c` from rel-041 f1fad94. The frozen build `dist_rc041_2` (f1fad94: 0.4.1, live-battle
engine 17, protocol 3) installed with `--dist`; the released 0.4.0 (`dist_vp_2`) put back after every run; slots 1 and 9
and the Preferences identical to the pre-run copies after every run (MD5); no copy, proxy or folder copy left. Game open
11,492 s in all.
- **Runs** (checks passed; game open):
  - The default run: 736/736 (1846 s).
  - `--stress` with the missile fixture: 1021/1025 (6209 s). PASS: `twopc` full 77/77, `lockstep_stress` 101/101 (S8),
    `lockstep_resume` 37/37, `lockstep_torpedo` 19/19, `air_units` 12/12, `lockstep_air` 41/41, `lockstep_strike` 21/21,
    `lockstep_month` long 70/70, `lockstep_pvp` 27/27, events 27/27 and every default scenario but `battles`. The four
    fails were the suite's (below).
  - Re-runs of the affected scenarios: `battles`, `lockstep_rejoin`, `lockstep_missiles`, `lockstep_strike` 161/163 (2000
    s; two more suite issues, fixed); `lockstep_strike`, `lockstep_rejoin` 74/76 (1050 s; strike 21/21 with a battle
    replaced; the fails: the product issue below, counted by `lockstep_rejoin` and `session`).
  - `lockstep_month --stress`: 86/86 (144 s).
  - Mixed versions at level 17 (`mixed_041.py`, the 0.4.0 game in a folder copy, deleted): a) 0.4.1 host, 0.4.0 player
    22/22 (145 s: "Captain: mod 0.4.0, live-battle engine 14 (this host: 17)", the month, the host's battle against the
    player fought against the computer naming levels 14 and 17); r) 0.4.0 host, 0.4.1 player 25/25 (98 s: "mod 0.4.1,
    live-battle engine 17 (this host: 14)", the player's intelligence kept as 0.4.0's, the battle to the computer with
    both levels). Nothing stuck, no game box.
- **The fix of f1fad94:** no DESYNC and no LEAK in any live battle, six strike battles among them (and three replaced).
- **Suite issues** (fixed on `reg-041c`, the affected scenarios re-run):
  - `lockstep_strike` b), f), g) and the battles' own conditions (RC 1 and the fix's runs): a battle without a report
    on a screen within 150 minutes is replaced once (two 07:0x battles found nothing: no search aircraft aloft); f)
    is now two checks, the lockstep part strict, and a formation the game takes away before take-off on both copies
    alike is a NOTE (its base, states, aircraft; seen for one formation in two battles), the dark excuses one on deck at
    the end, a side none of whose formations flew is a SKIP-note; the cycle runs a minute at a time while a formation
    is in the air, and with no attack seen a strike that reached its target point (10 nm) is a SKIP-note (re-run 1:
    four flew 6 -> 8, none seen at 7 in 3-minute samples); g) checks the carrier strike when the battle can show one (a
    report by day with an hour left), else another battle once (re-run 1: 07:00..11:01 without contact, replaced: a
    strike in 50 minutes), then a SKIP-note; every minute equal is its own check.
  - `battles` b): since 0.4.1 an avoided battle's VP gain is merged; the stress run's month merged battle 1 (avoided,
    VP 360/0) before battle 2, and the check added battle 2's deltas only (374 = 360 + 14).
  - `lockstep_rejoin` b): RejoinTick can start the battle again right at the boundary of the minute A waited for,
    before the minute gate logs "the next minute is held"; a start at the next minute counts.
  - `lockstep_missiles` f) and d): the manual salvos (the both-step's lone ones released on a retry too) can empty a
    side's launchers ("nothing to fire (no missiles are left in that launcher)", "the launcher is destroyed", every
    shooter at 0): a SKIP-note; d) tries a silent side once by hand at the end (a salvo there is still a FAIL).
  - Month checks: the joined player's own monthly check (the game's CheckIntelligence on its PC, since 0.4.1) can add a
    report it already has (`lockstep_rejoin` M1: "spy event, 4 new reports", 11 entries, 10 distinct; the mod adds none
    itself, the next load drops repeats): a repeat up to that check's new reports is a NOTE.
- **Product issue to follow up** (once, re-run 2): the third copy (k2, a fresh process, nation 2) showed the game box
  "Error in AdjustForcesForBattle procedure! Access violation at address 0096C748 in module 'RTW3.exe'. Read of address
  02EBC02F" in `DoBattle` of the relayed battle of `lockstep_rejoin` e) (`battles injectpvp`, its player copy in slot
  86); the game caught it, the battle ran, and every check of it passed. 0096C748 lies among the System string routines
  (`@UStrAsg` 0096BCCC): a bad string pointer, like the older SaveBuildCampaign box. Not seen in any earlier run of the
  session; whether it predates 0.4.1 is open.
- **Verdict: GO** for 0.4.1 with dist_rc041_2: the RC 1 blocker is gone, every other fail was the suite's and passed on
  re-run, mixed versions work; the AdjustForcesForBattle box is rare, caught by the game and harmless in the run, to be
  investigated after the release. Not pushed.

## Feature 2026-10-20: relations v2, B1: a war counter for each joined nation (relations v2)
The user's decision on the design of the [investigation](#investigation-2026-10-20-each-players-own-relations-tension-wars-and-battles-of-a-joined-players-nation-v041)
was option 2 plus B6: each joined nation's relations under the player's own rules, built in steps on the branch
`relations-v2` (from 0.4.1's fix-strat 2e1a042), each proven live and committed on its own. B1 is the first.
- **Why.** The game counts nation 0's months at war or at peace in `[General] War` (0xf52ff4) and many routines read it
  (the war machinery, budgets, training costs, fuel, research, events, the screen's war state; GAME-INTERNALS). A joined
  nation K had none: its PC showed and used the host's counter, the events guessed one, the battle slot too.
- **Host (`session.cpp`).** `g_kWar` holds a counter per joined nation (reserved nations included). `KWarMonthStep`
  runs once per month after `ApplyDiplomacy` and before `BroadcastState`: +1 at war, -1 at peace, then `KWarFollow`
  (1 when a war started, 0 when the last war ended), the game's rule for nation 0 (DoStrategicTurn 0xce1f69, then
  WarStartsWithNation and DoPeace). The step is marked with the processed month (`kwarStepped`; the save on disk still
  holds that month at this point), so a resent month doesn't step again. A nation the computer plays drops its counter;
  a nation without one (an existing campaign, a new player) starts from its wars with the host's counter as the guess
  (at war: the host's if the host is at war, else 1; at peace: the host's if at peace, else -12). Saved in
  `rtw3mp_session.ini` (`kwar=1:1,2:-69,`, `kwarStepped=Aug 1900`) with the month's other date-bound state; sent in the
  `MSG_STATE` header (`kwar`). Log `war counter of nation 1: -68 -> 1 (the month)`.
- **Client (`game.cpp`, `session.cpp`).** `ClientApplyPendingState` stores the header's counter
  (`game::SetOwnWarCounter`, INT_MIN when absent); `game::LoadCampaign` with a player nation writes it over the save's
  counter before `SetPlayerNation` draws the screen (log `war counter: -68 (the host's nation) -> 1 (nation 1's own)`),
  also after a battle slot. The game's own lines then show K's war: "At war for N months." with the War results box,
  at peace the hint "At peace for N months."; the relations view's `KViewWarState` keeps its "At war with Spain." line
  only for a host without counters. `RunClientEvent` uses the counter when it agrees with K's wars;
  `MakeNationPlayer`/`TransformNationPlayer` take it (`kWar`) for the battle slot and Copy game. The client never
  sends `[General]` (its turn holds its own nation's sections), so the host's counter is untouched.
- **UpdateUIValues at peace** empties `lbWar` but never hides it once a war was shown (single player too); the runner's
  war-state check accepts that.
- **Bridge** `kwar` (host: the counters, `kwar <K> <n>` sets one; client: own and game). **Runner:** `own_war_counter`,
  `kwar_counters`, `kwar_step`; `month_checks` asserts the step after every processed month (9 checks a month now);
  `relview_check` expects the game's own war lines when the client knows its counter.
- **Older peers.** A 0.4.1 client ignores `kwar` (its screen keeps "At war with Spain."); a 0.4.1 host sends none and a
  relations-v2 client keeps the host's counter and the 0.4.1 lines. Protocol 3.
- **Live proof** (dist_rv_1, RTW3MP 0E80BFE4C9B64DEF: this commit's source but two comments; driver
  `rv_live.py b1` in the session folder, 1900 fixture, host and client on one PC, Germany = nation 1, France reserved
  for "Bot"): 32/32 in 55 s of game
  time. Session start Aug 1900: host counter -68, `kwar=1:-68,2:-68` (both at peace: the host's), the client's game -68,
  "At peace for 5 years and 8 months.". Germany at war with Spain (`settension 1 7 50`), Aug -> Sep: Germany 1, France
  -69, the host -69 (its own step), the client "At war for 1 months." with the War results box, the session file
  `kwar=1:1,2:-69,`, `kwarStepped=Aug 1900`, `diplomacyDate=Sep 1900`; Sep -> Oct: 2, "At war for 2 months."; peace
  (`settension 1 7 5`), Oct -> Nov: 0, "At peace for 0 months.", France -71. The host's screen unchanged throughout.
  (A first run, 55 s, had 4 failing checks of the driver: it expected `kwarStepped` = the new month and a hidden war
  line at peace.)

## Feature 2026-10-20: relations v2, B2: the computer's rules leave a joined nation's pairs alone (relations v2)
- **Why.** The game runs every nation but nation 0 by its AI rules, so a joined nation K's relations with the
  computer's nations (and K's decisions about the host) came from the computer: AI wars and AI peace (frozen while the
  host is at war), alliances, the drift, reactions to the host, and in the host's own CheckForWar K could plan and
  declare war on the host. Under the player's rules (option 2) those relations are K's player's: they follow the
  player's routines on K's PC (B3), the players' diplomacy and the events.
- **Which routines** (static, the callers of `ChangeAITension`, `SetAITension`, `SetAIAlliance`, `HandleAIPeace`; the
  dumps of `CheckAITensionEvents`, `CheckForWar`, `RandomTensionChanges`, `ReduceTimeLimits`; GAME-INTERNALS):
  the computer's relation rules are `CheckAITensionEvents`, `DoAITensionChanges`, `CheckStopPlayerAgression`,
  `CheckAllyTrouble`, `CheckBlitzkrieg`, `CheckChineseConcessions`, `CheckForceOnForeignStations`,
  `CreateArmsControlTreaty`, `CheckForRevolutionInEnemyNation`, `ReduceTimeLimits` (an ally's "separate peace") and
  `HandleAIPeace`, plus `ChangeAITension` from anywhere. In the host's own routines K appears as a computer nation in
  `CheckForWar` (war plans, declarations, surprise landings, volunteering, alliance quarrels) and `RandomTensionChanges`
  (alliance offers "X suggests an axis of steel ...").
- **Who.** `hookcb::PlayerRulesNation(K)` (host): K is a joined nation and its connected player announced `rel` >= 1
  in the hello; while the player is away, what its last player announced (`relRules` in `rtw3mp_session.ini`,
  updated with every month); Give to AI ends it; bridge `relrules <K> 1` forces it for tests. Clients announce `rel`
  from B3 on, so this commit changes nothing in a real session; the proof forces it.
- **How (`game.cpp`).** The routines run through generated thunks (`EmitMaskThunk`: push edx, eax, idx; call
  `MaskedCall`, which calls the original with `dl::Call` inside `__try`/`__finally`); `ReduceTimeLimits` and
  `HandleAIPeace` in their existing detours. Kind 0 (`AIMaskEnter`/`AIMaskLeave`, depth-counted): every cell of a pair
  (K, j), j in 1..8, both directions, reads tension 5 and alliance 0 during the call (5: no war, no outbreak needing
  10 < t < 40, no common-enemy or most-tense logic needing > 7, no alliance attempt needing 0) and gets its value back
  afterwards; changes the routine made to a masked cell are dropped and logged. Kind 1, the host's `CheckForWar`: K's
  tension with the host reads 5 while they are at peace, its `WillGoToWar` 0 and its own alliance cells 0; afterwards
  the tension gets the routine's change on top of its real value, the war plan back, and K's alliance cells the other
  nations' side of each pair (their decisions about K stay). Kind 2, the host's `RandomTensionChanges`: K's tension
  with the host reads at least 6 (no alliance offer in K's name), the routine's change kept. `ChangeAITension` (naked
  detour, `ret 4`) skips a pair with K. `ResetRelationMasks` (month end) undoes a mask an exception left and logs the
  month's count of skipped ChangeAITension calls. All hooks or none (`Player's rules for joined nations: ok`).
- **Kept:** the players' diplomacy (the mod's `StartWar`/`MakePeace`/`SetAlliance` run outside these routines), the
  events, the host's coalition (a computer nation that joins the host's war, also against K), the battles
  (`CheckAIvsAIBattles` sees K's real wars), the host's drift of its tension with K (`RandomTensionChanges`,
  `TensionEffectOfPreviousWars`).
- **Facts found:** `RandomTensionChanges` doesn't run every month: `CheckTurnEvents` gives the player one thing a month
  (the peace event at war, officer stuff, a technology offer, a random event 1 in 3, an intelligence report), and the
  random tension changes only when none of these came (or 1 in 8/10 after an intelligence report). `CheckForWar` runs
  from `DoTurn` and from `DoEndOfTurn`. `0xa07124` is `ShowMessagePos` (modal).
- **Live proof** (dist_rv_2 = this commit's code, RTW3MP AF2FF5059D2E8462; `rv_live.py b2`; `relrules 1 1`; baits:
  Germany-Russia 35 (an AI outbreak), Germany-Japan 0 (an AI alliance attempt), Germany's tension with the host 15
  (a declaration of war on the host: certain under the computer's rules, 13 + 0 nations at war <= 15), France-Russia 35
  (France = the stand-in, computer's rules) and USA-Italy 35): 26/26 in 46 s (a first run, 24/26 in 50 s, required the
  RandomTensionChanges log line, but the routine didn't run in those months). Both months: Germany's pairs with the
  nations 1-8 unchanged (tension and alliances), other pairs changed ("War has broken out between USA and Italy!"; in
  the first run also "between Russia and France!"), no box about Germany on the host, Germany not at war with the host
  at tension 15; the host log `relations v2: CheckAITensionEvents runs without the pairs of nation(s) 1 (player's
  rules)`, the same for DoAITensionChanges, CheckForceOnForeignStations, ReduceTimeLimits, CheckChineseConcessions,
  `CheckForWar sees nation(s) 1 as players`, `2 tension change(s) of the computer's rules left out (ChangeAITension)`.
  The client's `diplo war 2` (against the stand-in) applied at the month's end: Germany at war with France, Germany's
  war counter 1, the client's screen "At war for 1 months.", the host's screen unchanged.
- **Runner:** `relrules`, `computer_pairs`; `month_checks` + `relrules_month_check` (while nation 1 follows the player's
  rules: the host's month left nation 1's relations with the computer's nations alone; allowed: a computer nation that
  joined the host's war against it); `session`: nation 1 follows the player's rules exactly when its client announced
  them, the stand-in never (12 checks).
- **Older peers.** A 0.4.x client announces no `rel`: its nation keeps the computer's rules (the 0.4.x behaviour). A
  0.4.x host runs the computer's rules for every joined nation.

## Feature 2026-10-20: relations v2, B3: the player's relation routines on the joined player's PC (relations v2)
- **What.** A joined nation K under the player's rules gets the player's own relation routines, as nation 0 does in
  single player: `RandomTensionChanges` (with its explanations, speeches and questions), `TensionEffectOfPreviousWars`
  and `CheckForWar` (war plans, spy warnings and rumours, declarations, allies and volunteers), at the start of K's
  month on K's PC, with the dialogs answered by K's player; the host applies what they changed before the month runs.
  Clients announce it (`rel=1` in the hello, `game::PlayerTurnAvailable`), so from this commit B2 is on for them.
- **The view (`game.cpp`, `RunPlayerTurn`).** The routines read relations from nation 0's point of view; in K's full
  view K is at index 0 and the host's nation at index K in the nation list, and every record's 9 relation cells and 9
  alliance cells are rewritten with the permuted relations (record of view index v, cell w = the real relation of
  P(v) and P(w), P swapping 0 and K), K's per-relation state goes into the other records (`WillGoToWar`: their war
  plans against K, `TechSharing`: K's deals; 0 for the nations people play), and `CheckWarsWithNation` answers from
  K's past wars (hook). The other nations people play are masked as players in CheckForWar and RandomTensionChanges
  (the B2 masks, by view index: no war plan, declaration or alliance offer between players). Surprise landings and
  attacks return nothing in the view (`GetRandomPossessionInArea`, `GetRandomPossession`, `DoSurpriseAttack`,
  `CheckForPlayerSurpriseInvasion`; they move possessions and ships the host would have to replay: B5). Afterwards
  the cells and per-relation fields are put back (also on a game exception, `__finally`), the changes come back in
  real indices (every pair whose tension or alliance changed, K's per-relation state, the other nations' budget
  changes, K's VP change, K's war counter) and are written into K's campaign for its screen. The turn messages the
  routines wrote are shown in the game's own turn report (`ShowTurnMessages` with the form's `TurnMessages` swapped).
  The game gives the player one of its events or the random tension changes a month (`CheckTurnEvents`' chain): in a
  month with an event, `RandomTensionChanges` is left out.
- **Client (`session.cpp`).** The month's header `rel=1` (with `kwill`, `kts`, `khist`, `humans`) arms the turn; it
  runs after the event roll when the game is idle (`ClientRunRelations`, once per calendar month), sends
  `MSG_RELATIONS` (40, a number of its own away from the sequence other branches extend) and shows the turn report.
  Log `relations for nation 1 sent: tension=1:3:14:50;1:4:3:2; alliance= will=0.0.0.0.0.0.0.0.0 budget=0:1;2:1;3:3;...
  vp=0 kwar=1 ran=tension,previous wars,war msgs=3`.
- **Host (`HostApplyRelations`).** Once per nation and month (also across reconnects; the header leaves `rel` out
  for a month already done), in planning, for a nation under the player's rules: K's pairs with the computer's
  nations take the changes (a war: `StartWar`; tension deltas on the current values, at most 40); between players only
  the tension (a delta, at most 40); another pair only a war (a computer nation that joined K's war; against the host
  it is the host's war and a Notice); alliances unless between players or at war; K's `kwill`/`kts` kept with the
  month; the computer nations' BudgetModifier changes; K's VP change; `KWarFollow`; chat "* War: Russia and Germany are
  now at war.". Log `relations of Captain (nation 1; tension,previous wars,war; 3 message(s)): war 1-3 1-4 3->2 ...
  budget 3+3 budget 4+1 ...`. K's wars that end go into its history (`khist`, dated with the new month), read by
  `TensionEffectOfPreviousWars`.
- **Fix of B1's step marker.** B1 marked the month's step with the save's date (`kwarStepped`), but after a live
  battle the host's save already holds the new month at that point: the next month's step was skipped (the suite's
  `lockstep_month` after `lockstep_pvp`: counters 1 -> 1). The step is marked with the session's month sequence now
  (`g_seq`, in memory; the session file keeps no marker); the bridge's `stepped=` shows it.
- **Peace events** are left for B4: the game's peace offers are events (`GetPeaceEvent`: an event slot whose
  condition is the code 30-40 the inline decision in `CheckTurnEvents` picks) whose answers run `NegotiatePeace` and
  `DoPeace`, the terms. Until B4 a war between K and a computer nation does not end (no computer peace under B2, no
  player's peace yet).
- **Live proofs** (driver `rv_live.py b3`, 1900 fixture, host and client on one PC; bait: Russia's tension with Germany
  14): dist_rv_3 (BE8F532126D33C03) 23/23 in 54 s: the first month's turn on the client ("Tension with Spain is reduced
  as they do not want to raise tensions with a strong nation." in its turn report; host `1-7 4->3`); after Aug 1900
  the client showed "War has broken out between Germany and Russia" (the game's dialog, on the client only), the host
  logged `war 1-3`, Germany's counter 1 then 2, the client's screen "At war for 1 months.", the host's screen unchanged;
  the war generated a battle request for the client ("battle request 1: nation 1 vs 3 in Northeast Asia, fought by
  Captain"). dist_rv_4 (FB6A16910D20A22F = this commit's code): run 1 failed 5 of 23 in 54 s (the driver expected the
  war in the first month; the player's rule skips a declaration 8 % of the months: Random(180) < 15 and at peace for
  4 years; it came a month later: "Germany and Russia are now at a state of war!"; and the host's own event "Talks
  with Germany fails due to the high tension between the nations!" counted as Germany's dialog), run 2 with up to 3
  months 23/23 in 47 s.
- **Suite** `--only session,lockstep_month,lockstep_pvp`: dist_rv_3 16/18 (lockstep_pvp's second client, the stand-in
  "Bot" on v2, was held by its relation turn's turn report: the runner now waits for it, `wait_relations_turn` with
  port/pid/nation), then 87/89 (the per-month check counted the new month's relation turn as the month's change; and
  B1's step marker), dist_rv_4 100/100 (session 14, lockstep_pvp 39, lockstep_month 47) in 211 s.
- **Runner:** `wait_relations_turn` (after the event roll in `run_month` and `ls_month`, for the client and for K2, and
  at the session start), `relturn_lines`; `relrules_month_check` allows the new month's relation turn's own changes
  when the turn started from the value before the month. **Bridge:** `relturn`, `rellast`.
- **Windows Defender** flagged one development build of this source as `Trojan:Win32/Phonzy.A!ml` (machine-learning
  heuristic, 23:25:09) and removed it; the next build of the same source (FB6A16910D20A22F) was not flagged. A false
  positive, but released builds may meet it too.
- **Older peers.** A 0.4.x host ignores `rel` and `MSG_RELATIONS` (the computer's rules for K, as before); a 0.4.x
  client announces no `rel` (the computer's rules for its nation). Protocol 3.

## Feature 2026-10-21: relations v2, B4: peace with computer nations (relations v2)
- **What.** A joined nation K under the player's rules ends its wars with the computer's nations by the player's own
  rules, as nation 0 does in single player: at the start of K's month, after B3's routines, the player's peace decision
  picks a peace event whose answers negotiate (the game's `TdlgEventAnswer`, answered by K's player), and the
  Multiplayer window's Diplomacy list has a row for each computer nation at war with K with "Propose peace" (the
  negotiation at once). The game's own `NegotiatePeace` and `DoPeace` run in K's view on K's PC with their dialogs
  (the hawks, the enemy's refusal, "Select possessions as part of peace agreement", the "Peace treaty", the AMC and
  submarine questions); the host applies the result right away, as B3's wars.
- **Facts (static, 1.01.44).** The decision is inline in `CheckTurnEvents` (0xce32c7..0xce37d6): at war, if
  `Random(4) + 2 < War`: the war loans at `War mod 3 = 1` (the player's budget: B5), then if `War > 4` and the player's
  VP or `EnemyVP` exceed 250 G (G = FleetSize, the scale of `EventLikelihoodCheck`) and (`Random(30000) < VP + EnemyVP`
  or `Random(100) < War`): code 36 (enemy far ahead), 34 (we are far ahead, or ahead by 1.5 with an enemy in unrest),
  33 (no shared areas, or VP near parity in a long war), 34 -> 33 below a force ratio of 95, 36 -> 35 above 110; with
  `TotalWar` a Fascist/Communist player drops all but 34 (`Random(200) > War`) and `CheckTotalDefeat` may negotiate
  itself; then `GetPeaceEvent(code)` (an unused event slot whose condition is the code, +0x54 := 6,
  `TdlgEventAnswer.Execute(event, 99)`), whose answers call `NegotiatePeace(chance, lossGain, ForceBadPeace)`. Peace is
  always with every enemy (`Tension > 40`) at once; `DoPeace` ends every war of the enemies
  (`IsAtWarWithPlayerOrPlayerOpponent` 0xd4a830 is only `BNation(i).Tension > 40`: its loop over the opponents is dead
  code), sets each enemy's tension 1..4, relaxes every other relation, resets `WillGoToWar`, `Blockaded` and
  `SubmarinePolicy` of all, budgets and unrest by the war's length, Prestige of nations 1..8 := 0, `War := 0`.
  `TransferShipAsReparation` moves the ship object (its `CShipId` stays), copies its design into the receiver's list
  (new design id), clears its air units, crew -20, puts it in the receiver's build area and logs "<date>
  <reason><nation>" and, renamed, "<date> Renamed <name>" in `myLogEntries`, then always an empty line (0xcce0d3).
  `ReturnTakenPossessionsAfterWar` uses `ChangePossessionOwner` and `GetBNation(TakenFrom)`: `TakenFrom` (and
  `Invaded`) hold nation indices. The game's loops over nations 0..8 read nation 0's own `Tension`/`Allied` (0 for the
  player): `CountNationsAtWarWithPlayer` (in `CheckForWar`), `RandomTensionChanges`' count of nations at peace,
  `NegotiatePeace`'s enemies' and allies' gains and `TerminateAlliances`.
- **The peace part (`game.cpp`, `PeacePart` in `RunPlayerTurn`, the same view as B3).** Runs when K is at war with a
  computer nation: the decision mirrored in C++ (`PeaceDecision`, the game's own `Random` 0x837834 found from the
  decision's first instructions, the same order of tests and Random calls; skipped in a month with another event: one
  event a month) -> `GetPeaceEvent`; or a proposal / test: `NegotiatePeace(chance, lossGain, force)` at once (the
  proposal is the game's compromise offer 40, 0). The view (`PvEnter`, B3's too) has the own cells of K's and the
  host's records at 0 (K's record's cell 0 held its real relation with the host: K at war with or allied to the host
  was its own enemy or ally there). Masks (`PeaceMaskEnter`/`Leave`, also on a game exception): the
  nations people play read tension 40 and no alliance with K, and all their cells are put back afterwards (their wars
  are their own; `DoPeace` would end them); K's VP and `EnemyVP` = its war score with its computer enemies (the host's
  ledger); `TotalWar` 0 (the host's), `HasBeenBattle` from the war score, `AutoBuildSubs` 0 (the host's setting), the
  save's war record of the month cleared (NegotiatePeace writes K's outcome there: the winner) and put back; the
  possessions' `TakenFrom`/`Invaded` in view indices; K's own event cooldowns. Hooks record only while it runs:
  `ChangePossessionOwner` (name, real owners, RemoveAllCoastalArtillery), `TransferShipAsReparation` (id, nations, the
  new name, the reason, the log's "Renamed" line), `NegotiatePeace` (the arguments), `ShowPeaceTreaty` (the lines);
  `DoPeace` in the view keeps every other nation's Prestige. B3's view-skip of `GetRandomPossession*` is limited to
  B3's routines (the possession choice after a war uses it). Afterwards: the other nations' BaseResources, Prestige,
  BudgetModifier and HasLostWar changes (the people's nations put back), the war state of the possessions of K's war
  (K, its enemies, the computer nations whose war with them ended; DoPeace's clearing of the others is put back), K's
  war counter (DoPeace's 0; while a war with a people's nation goes on, the old one, and K's BudgetModifier,
  SubmarinePolicy, UnrestLevel, Corruption and Blockaded from before DoPeace: no demobilization), the relation changes in B3's
  diff (K's pairs, its enemies' wars, alliances ended, `WillGoToWar`), the winner (the war record: 1 K, 2 enemy, 3
  compromise). Log `relations v2: nation 1's peace proposal: 1-7 50->3 ...; peace part: code 0, event 0, NegotiatePeace
  (100, 2, 1), peace, winner 1, 1 possession(s), 0 ship(s), ...`.
- **Client (`session.cpp`).** The turn's `MSG_RELATIONS` carries the peace part (`peace`, `pcode`, `pevent`, `pargs`,
  `paccepted`, `pwinner`, `penemies`, `pland`, `pships`, `ptaken`, `pdelta`, `plost`, `ptreaty`); a proposal or test
  sends its own (`kind=peace`, `test=1`). "Propose peace" (`ComputerPeaceRows`: player "computer", At war, only Propose
  peace, a confirmation) is offered after the month's relation turn (its peace event comes first) and waits for an
  idle moment; once a month, not in a month whose peace event was answered (a queued proposal that meets one is
  dropped: log `peace: the proposal was dropped ...`, a chat line), before the turn is submitted. Log `peace for
  nation 1 sent (proposal): ran=1 code=0 ...`.
- **Host (`HostApplyRelations`, `HostApplyPeace`).** K's ended wars at the game's new tension (`MakePeace`, then the
  reported tension; no host `DoPeace`), the computer-only wars of K's ex-enemies that its peace ended (the people's
  are skipped), alliances, then the possessions by name (`ChangePossessionOwner` if the owner is still the reported
  one), the ships by `CShipId` in the giver's list (`TransferShipAsReparation` without the game's second rename, then
  the player's name and log line, before the entry's closing empty line), the possessions' war state, the other
  nations' changes (BudgetModifier only of a
  nation left without a war: the game's peace demobilizes), K's history with the winner (`KHistPeace`), `Blockaded`
  := 0 and VP := 0 for K without a war, `Blockaded` := 0 for an ex-enemy without a war, the war scores erased,
  `KWarFollow`. Chat "* Peace treaty between Germany and Spain: Germany wins." and the treaty's lines (or "... peace
  negotiations with ... came to nothing."). Log `relations v2: peace of nation 1 with Spain (peace, code 0,
  NegotiatePeace(100:2:1)): peace, winner 1; 1 possession(s), 0 ship(s), ... 0 skipped, 5 treaty line(s)`. A
  proposal is once a month per nation (`g_kPeaceMonth`, also after a peace event of its turn; tests exempt).
- **War score ledger.** A war of K (player's rules) with a computer nation is scored in `g_warVP` like a war between
  players (`LedgerPair`, from 0:0): `OnBattleVP` (battles the host resolves) and `CreditWarScore` (battles fought on
  K's PC) count it, the month-end cleanup keeps it while the war lasts, a peace erases it. The header sends K's sums
  `kvp`/`kevp` (the decision's VP and EnemyVP), the pairs `kpairs`, `kpeace` and `humans` (also in a month whose turn
  was applied: proposals).
- **Merge safety (`saveio::MergeSubmission`, `DropForeignShips`).** A ship of the player's submitted `[Nation<K>Ships]`
  that another nation's ships section on the host holds too (the same id: two players received the same ship, or the
  host handed it over after the player's turn was built) is dropped from K's section, `ShipCount` follows, logged
  ("merge nation 1: ship 101 (New B) dropped: nation 7 holds it on the host"). Offline harness (saveio + util): 14/14.
- **Bridge:** `peacetest <chance> <lossGain> <force>` (client, async), `peaceevent <code> [chance]` (client: the next
  turn's peace event past the decision; with a chance its answer's `NegotiatePeace` runs with it: 100 takes the
  computer's offer), `peacelast` (client/host), `kwarvp <K> [J vpK vpJ]` (host), `findship <id>`,
  `nflag <n> <field> [0|1]`. **Suite:** `relations_peace` (group `relations`). **Live driver:** `b4_live.py` (outside
  the repo).
- **Not the game's:** the war loans before the decision are left to B5; `TotalWar` is the host's state, so K's
  decision never takes the total-war branch (K has no total-war state yet); DoPeace's relaxation of the computer's
  other relations (`ChangeAITension -1`, at most 7) and its unrest and submarine resets of other nations stay on K's
  PC (the host keeps its computer rules for them).
- **The war counter of the decision.** The game decides in `CheckTurnEvents`, before `DoStrategicTurn` steps `War`
  (0xce1f69), so the decision, `CheckTotalDefeat`, the event's `NegotiatePeace` and `DoPeace` read the value from before
  the step; K's month starts with its counter already stepped by the host (B1, `KWarMonthStep`). `PeaceRunGuarded`
  puts K's counter less one in place while at war (> 1) for the turn's decision and its peace event (also a forced
  one, `peaceevent`), the counter as shown for a proposal or `peacetest` (while planning; single player has none), and
  the shown value back afterwards on every path, a game exception included (DoPeace's 0 stays; the shown value again
  while a war with a people's nation goes on). Log `relations v2: peace: the turn's decision sees the war counter 4
  (the game's, before the month's step; shown 5)`.
- **Once a month also across a host restart (risk 6).** The months whose relation turn and peace were applied
  (`g_kRelMonth`, `g_kPeaceMonth`, memory only before) are saved in `rtw3mp_session.ini` (`krelm`, `kpeacem`:
  `K:Year*12+Month,`; `HostApplyRelations` saves the file after every apply) and read back only for the month they
  were saved with (`diplomacyDate`, like `kwar`); a host that stops and hosts again in the same month sends no second
  `rel=1` and takes no second peace. Files without the keys load as before. A restart from a save older than the
  apply skips that month's turn instead of applying it twice. Suite: `relations_peace` c) `kpeacem`, d) `krelm`.
- **Status: proven live** (2026-10-22, B4 without the B5 money rules; host and client on one PC, the 1900 fixture,
  Germany = nation 1 against Russia; 50/50 checks, the game open 91 s): a gain (`peacetest 100 2 1`: Sakhalin ceded
  to Germany on both copies, Russia's BaseResources -500 as reported, the ledger pair erased, war counter and
  blockades 0, the chat's treaty), a loss (`-6`: two ships handed over, held by Russia only on both copies, also after
  the month's merge), "Propose peace" (the game's compromise offer refused, applied as `kind=peace`, a second one
  refused), the computer's offer taken through the game's own peace event in K's turn (`peaceevent 34 100`: applied
  with the turn, `kind=turn accepted=1`, no proposal afterwards) and a host killed and started again in the same
  month (`krelm`/`kpeacem` read back: no second relation turn sent, a forced one ignored, the proposal still
  refused). Suite `relations_peace` 45/45 (with `session`).
- **Fixes from the live proof.** (1) The host's screen showed its old tension with K after K's turn changed it (B3's
  apply never redrew it; the suite's `relview` check, one run in three): `HostApplyRelations` ends with `RefreshUI`
  when it changed something. (2) A nation reserved after the month's state (the suite's "Bot") was not in the
  client's `humans`: K's DoPeace relaxed K's tension with it (7 -> 1) and the host applied it as a players' pair. The
  client adds the lobby's people's nations (`MarkLobbyHumans`, the lobby's `dipHumans`) to the turn's, the proposal's
  and the Diplomacy rows' list; the host skips a proposal's changes of K's relations with people's nations
  (`... skipped`). (3) The runner's possessions answer: the lowest value, and the game's own messages over the dialog
  first (see GAME-INTERNALS, `bnOKClick`).
- **Restart trade-off (seen live).** The apply lives in the host's memory until the host saves its campaign (the
  month's processing): a host that is killed and started again from the month's save has neither the turn's changes
  nor the peace, and `krelm`/`kpeacem` keep it from taking them again this month (in the proof Russia's war with
  Germany was back for the rest of the month; the next month decides again). **Older peers.** A 0.4.x or
  B3 host sends no `kvp`: no peace part, no computer rows (the computer's rules, as before); a 0.4.x or B3 client
  ignores the new keys.

## Feature 2026-10-22: relations v2, B5: the player's economy rules for a joined nation (relations v2)
- **What.** The host runs the month of each joined nation K and the game treats K as one of its computer nations, so K got
  the money and budget bonuses the game gives the nations it plays (e.g. Germany +18,460 in one month: CheckBBNumbers'
  6000 x FleetSize). The new `src/mod/economy.cpp` takes them away on the host and gives K the player's rule where the
  player has one. Nation 0 and the computer's nations are unchanged; a joined player's PC is unchanged; existing
  campaigns keep the money they have (nothing retroactive).
- **Facts (static, 1.01.44; addresses in the 0x830000 image).** `ComputeExpenses` 0xca049c: the player gets
  `Funds += GetMonthlyBalance`; each nation 1..8 (loop body 0xca0511..0xca05b6, nation in ebx) gets a BaseResources floor
  of Round(1.5 x FleetSize), BaseResources x 0.9 every month at a BudgetModifier below 5, the balance, and two subsidies of
  25 x FleetSize (one only when its modified budget is below the player's and its tension with the player is above 6).
  `CheckBBNumbers` 0xccb6d8 (only caller: CheckTurnEvents' loop 1..8, so its player branch is dead code): a capital-ship
  race gives the computer +6000 x FleetSize and BudgetModifier +1. `ReduceTimeLimits`' funding loop (0..8): keep-up
  (`cmp edi,[ebx+PlayerNation]; je` 0xc7ba85, +1 toward the player's BudgetModifier, 30 %), preeminence (0xc7b9f5; the
  player's branch needs unrest below 3, Random(100) < 10 - unrest and two below the highest BudgetModifier, the
  computer's unrest below 6 and 20 - unrest), January cuts 1..8 (0xc7b303, 0xc7b33e). `CheckForCoups`: a computer coup
  always succeeds with +1 (0xd12edc); the player answers a dialog. `CheckAITensionEvents`: its only BudgetModifier writes
  are +1 at war with a computer nation (0xd2ddb3), arms race (0xd2e592) and both sides of a war declared between computer
  nations (0xd2e662, 0xd2e689). `CheckForWar`: when the player's nation went to war, every nation 1..8 with tension below
  40 gets +1 (0xc85a47). `WarStartsWithNation` 0xd333bc: the enemy +2 up to 30 (IncMax) and +1 below 22; the player +1
  below 30 and +1 below 25 when Random(30) > BudgetModifier. `SpySuccess` (spying nation 0 only): a target with tension
  above 7 gets the report "secretly spending an additional 10%" and Funds += ModBudget / 5 (0xc9d5f6). The computer's
  loop runs `DeHaltAllShips` (every ship's Halted := 0) and up to four `HaltMostExpensiveShip` while the balance is
  negative. Maintenance: `GetNavalAcademyCost` (the computer pays 0, 0xc77b09), `ApplyAirMaintenanceModifiers` (x 0.8,
  0xc658d1), `GetShipMaintenanceCost` (a light carrier with aircraft x 0.6, 0xc59eda). `GetMonthlyBalance`'s tree calls no
  Random and writes only idempotent values (BudgetModifier >= 3, raider status at peace). The date advances in
  `DoStrategicTurn` after `CheckUnrestChanges` and before `ReduceTimeLimits`.
- **Now for K (B2's masks in mind).** Under the player's rules (B2) K's pairs read 5 during `CheckAITensionEvents`, so
  none of its +1s reach K; a joined nation of an older client (no `rel`) gets them. During `CheckForWar` K at peace reads
  5 (< 40): K gets the +1. Everything else above reaches K (no test for people's nations).
- **How (`economy.cpp`, installed from `main.cpp` after `game::InstallHooks`).** Each rule is found by its routine's RTTI
  name and a byte pattern unique in the routine (field offsets from RTTI; call targets checked) and hooked with MinHook
  at the instruction (generated thunks: leave the instruction out, answer `cmp nation,[form+PlayerNation]` as "the
  player", or return at once), only when `hookcb::IsHumanRemoteNation`: `ComputeExpenses` skips the computer's part for K
  and adds `GetMonthlyBalance(K)` (at the player's prices); `CheckBBNumbers`, `DeHaltAllShips`, `HaltMostExpensiveShip`
  return at once; keep-up skipped, preeminence by the player's branch; the January cuts, the coup's +1,
  `CheckAITensionEvents`' four +1s and `CheckForWar`'s +1 left out; `WarStartsWithNation` restores K's BudgetModifier and
  applies the player's increments with the game's Random; `SpySuccess`'s tension test reads "not tense" for K (no report,
  no money); the three maintenance tests answer "the player". A rule that doesn't match stays the game's; the start
  logs `economy: the player's economy rules for joined nations: ok (20 of 20; ...)`.
- **Left as they are.** The computer's unrest cuts in `CheckUnrestChanges` (BudgetModifier -1, twice with unrest -1: the
  player's unrest rules show the host's dialogs and follow the host's war counter; dropping them would leave K's unrest
  without relief); the treaty cut R1 (the computer -1, the player -2) and the isolationism cut R4 (the player's needs the
  host's ParliamentDelay); the shared rules (HandleEconomicGrowth's +1 at the host's war, AdvanceShipConstruction's
  hoarding reclaim, DoPeace's caps, the nationalist and fascist +1s); the global-economy event's BaseResources changes of
  nations 1..8; a coup itself (the government changes); `LayDownShip`'s 5 % first-of-class fee. The intel part of the
  balance (`ComputeAINationIntelCost`) is the player's own spending since 0.4.1 (`nationplay.cpp`, merged with 0.4.1).
- **Record and log.** Each joined nation's month is logged after the month's `CheckForWar` (MinHook on DoEndOfTurn's
  call): `economy: nation K (<name>) YYYY-MM: funds a -> b (balance +c at the player's prices, maintenance +m); withheld:
  subsidy s, BB check n, spy n (funds f), BM +n (AI war, arms race, AI war start, host's war, coup), January -n, keep-up n,
  BR floor, BR x0.9, auto-halt n, resume n; the player's rules: preeminence n (+n), war start +p (computer's +g)`.
- **Bridge:** `econ <K>` (host): Funds, BudgetModifier, BaseResources, the balance at the player's and the computer's
  prices, fs, and the last month's record (`rec`, `before`, `after`, `applied`, `subsidy`, the counts; see TESTING).
- **Not yet (B5 follow-ups; done in part 2, below):** the player's own rules for K: war loans every third month at war (needs K's war counter,
  B1's ledger in session.cpp), `CheckPlayerFunds`' debt penalties and corruption, the hoarding cuts (K-local, never the
  global ArmyOffensive), `SmallFleetCheck`'s +1/-1. Without them K's BudgetModifier only moves by the shared rules, the
  player's routines on K's PC (B3's RandomTensionChanges) and the player's preeminence and war-start rules.
- **The record of the rules every nation shares.** `AdvanceShipConstruction` (in `ReduceTimeLimits`, after the date's
  step, before `ComputeExpenses`) is hooked at its entry for the record only: how much it changed each joined nation's
  Funds (`CheckForConstructionDelays`, called for every nation 0..8 in its loop, pays a delayed ship's month back, e.g.
  "Technical problems have delayed the construction of ... by one month."; the hoarding reclaim takes 5 or 10 %). The
  month's line ends with `shared construction rules +n`, `econ` has `shared=n`.
- **Status: proven live** (2026-10-22, B4 + money, host and client on one PC, the 1900 fixture, Germany = nation 1,
  three processed months with its BudgetModifier 4 below the host's, then the host's war with Germany; 76/76 checks, the
  game open 79 s): every month one line per joined nation; `after - before == applied` (the balance only); nothing
  after ComputeExpenses (no +6000 x FleetSize, no spy money); 75 = 25 x FleetSize withheld each month; the funds before
  ComputeExpenses == the client's submitted funds + the shared construction rules (the first run found +282 in one
  month: a construction delay's refund, the record's reason); the client shows the host's funds; the stand-in nation
  2 (reserved, no client) has its own record (+2033 and +1733 of shared construction refunds); the computer nation 3
  and the host's nation 0 have no record (the game's economy); the computer's four automatic halts of Germany's ships
  in deficit left out (`auto-halt 4`); the host's war: BudgetModifier 15 -> 16 by the player's rule (the computer's
  would have given +2 and +1). Existing campaigns keep their money: nothing is taken back, the first month starts
  from the submitted funds.

## Merge 2026-10-22: 0.4.1 into relations v2 (0.5.0-dev)
- **What.** `main` (21c69c6, the 0.4.1 release: live battles between two PCs with the software maths and the game data
  files, engine level 17; each player's own intelligence and trade protection warnings, `nationplay.cpp`; the twopc
  suite group) merged into `relations-v2` (B1-B5 on fix-strat 2e1a042). `MP_MOD_VERSION` "0.5.0-dev", protocol 3.
- **How.** 0.4.1 is one commit on 0.4.0 that also holds fix-strat, which `relations-v2` has as its own commits: a
  plain merge (base 0.4.0) saw fix-strat added twice and gave 49 conflicts. Resolved against fix-strat instead (a
  temporary commit with main's tree on 2e1a042, merged without commit; the merge commit's parents are relations-v2 and
  main): 22 conflicts in 8 files, every one both sides': `build.bat` (both sides' sources), the bridge (`relturn`,
  `peacetest` and `submit`, `intelcheck`), the month's header (`rel`, `kwill`, `kts`, `khist`, `humans`, `kvp`,
  `kevp`, `kpairs`, `kpeace` and `intel=1`), the hello (`rel` and `cpu`, `dh`; one log line with both), `game.cpp`'s
  resolve and install (B2-B4 and `nplay::Resolve`/`InstallHooks`), `EventActive()` = the event view, the player's view
  (B3/B4) or nationplay's; the runner's session checks, groups and estimates; MODLOG, ARCHITECTURE and TESTING. No game
  routine is hooked twice: nationplay hooks the intelligence cost and labels, `ChangeIntel`, the patrol requirement and
  `CheckShipsOnForeignStations`; economy.cpp's sites are elsewhere (installed after `game::InstallHooks`, MinHook
  already initialized).
- **Runner.** `build_version` reads a version with a suffix ("mod 0.5.0-dev"); `events`' random path waits for a month
  in which nation 1 is at war with nobody (a run whose `battles` needed retry months put its planned month on
  `lockstep_pvp`'s war month: no event qualifies at war); `relations_peace` a)'s chat check names nation 1's other
  computer enemies too ("Germany and Russia, Spain": `battles` leaves a war with Spain).
- **Tests.** Build without warnings (dist_rv_10). Suite `--only session,lockstep_month,lockstep_pvp,events,battles
  --group relations`: run 1 233/237 (the two runner issues above), run 2 217/217 in 370 s of game time.

## Fix 2026-10-22: a host restart in the month keeps the month's relation turn and peace (0.5.0-dev)
- **Problem.** A joined player's relation turn (B3) and peace (B4) are applied on the host in the month's planning phase
  (`HostApplyRelations`) and lived only in the host's memory until the month's own save (`RunHostTurn`). The risk-6 guard
  (`krelm`/`kpeacem` in `rtw3mp_session.ini`) prevents a second turn after a restart, so a host whose game stopped in the
  month lost them: in the B4 live run the war 1-3 the joined nation had just ended was back after the restart.
- **Options.** (a) Save the host's campaign right after the apply; (b) record the identity of the save with the guard and
  apply the turn again from a stored copy when an older save is loaded. (b) has to replay `HostApplyPeace` (possessions,
  ships, other nations' fields) on a reloaded campaign while keeping the session-side effects (ledger, war counter,
  history, chat) single, and K's own fields still live only on K's PC; (a) uses the game's own save at a moment the mod
  already saves at (a join, a claim and a rematch save in the planning phase to send the month), so the newest save
  always holds what was applied. Chosen: (a).
- **How.** `HostApplyRelations` writes the guard first (`SaveReservations`) and then saves (`SaveAfterRelations`:
  `game::SaveGame` in the planning phase when no turn runs, no battle, no event view and the host's game is free
  (`IsBusy`: no modal dialog); otherwise pending, retried from `Tick`; `RunHostTurn`'s own save clears it). A crash
  between the guard and the save loses the changes and never doubles them. Log `relations v2: campaign saved after a
  relation turn|a peace proposal of nation K (ok; n save(s) of this kind)`; the host's `rellast` reply ends in
  `saved=1` (`saved=pending` while it waits). The saved month is the month the clients get when they join again
  (`SendStateTo` packs the save), so a client that reloads the month after the restart sees the same.
- **K's own fields.** The routines on K's PC also change K's own fields, which reached the host only with K's turn
  (MergeSubmission replaces K's sections): a restart lost them with K's plans. `RunPlayerTurn` now sends their changes
  (`self` = BudgetModifier, Prestige, BaseResources, UnrestLevel; clamped on the host) and the host applies them with the
  rest, so the saved campaign holds them; the month's merge then puts the same values in place. Funds are left out: a
  peace's `ScrapShipsAfterPeace`/`ScrapShipsAccordingToTreaty` on K's PC pays refunds for ships the host never scraps.
- **Wording.** Different mod versions can share a live-battle engine (0.4.1 and 0.5.0 are both level 17): `LevelWhy`
  says "live battles need the same live-battle engine on both PCs: the host has 0.5.0 (engine 17), you have 0.4.0
  (engine 14)"; `LevelMismatchText` drops "the same mod version" (the `lockstep_resume` check follows).
- **Status: proven live** (B4 driver `--event --restart`, dist_rv_12, 102 s open): five saves after applies; the host
  killed after the turn's accepted peace event and started again: the peace 1-3 still in the host's campaign and in the
  client's reloaded month (war 0, tension 2), nation 1's own fields equal on both, ledger pair erased, `peace=used`, no
  second turn (`second relation turn of Captain this month ignored`), the next month processed (Sep -> Oct 1900), ships
  handed over held once. (The run's two failures were the driver's: it compared with its own in-memory re-declaration
  of the war; fixed.) Limit: a crash in the second between the guard and the save loses that apply.

## Feature 2026-10-22: relations v2, B5 money, part 2: the player's remaining economy rules, and the computer's last decisions for a joined nation (0.5.0-dev)
- **What.** B5 money left K without the player's own rules and still let the computer decide some things for K. Now, on
  the host, each joined nation K also gets the player's war loans, debt penalties (and the money of corruption),
  hoarding cuts and SmallFleetCheck's budget rules, and the computer no longer spends K's money or decides for K in the
  month. Nation 0, the computer's nations and a joined player's PC run the game's rules as before.
- **Facts (static, 1.01.44; RE note re_econ.md in the developers' files).** `CheckPlayerFunds` 0xcb4b44 (after
  `ComputeExpenses`, player only): (a) debt: Funds < 0 gives a message (OK box, not logged) and by the escalation
  (Funds > -50 or Random(100) < 10: "in the red"; > -1000: finance ministry 40 %, then "losing confidence" 40 % with
  War < -2 and ProportionalPrestigeLoss(10); else unless Balance >= 100 and Random(100) >= 20: "outraged" 50 % with
  War < -4 and PPL(10), else MoraleLoss (FleetMorale -1, every ship's crew and every air unit's pilots -1/-2) and
  Prestige -1); (b) corruption (Random(300 - C) < 10 and (War <= 0 or Random(100) < War + 70)): Funds > 4000 and
  Random(300) < C: Funds -1000 and a question (answer 2: Corruption -10, PPL(10)); else a question only; (c) the Air Force
  event (1950-1975, 1/1000, a question; BM -1 on answer 2); (d) hoarding 1 (Funds > 0.6 x ModBudget + 20000, the most
  tense nation below 6, Balance > -100, ArmyOffensive 0, 10 %): at war ArmyOffensive := 4..6 only; Bombastic or
  government 2: Funds -2000 x FS, BM -1, ArmyOffensive := 12; else BM -1, ArmyOffensive := 12; (e) hoarding 2 (Funds >
  40000 x FS, ArmyOffensive 0, 28 %): Funds -= Funds div 10, BM -1, ArmyOffensive := 8. ArmyOffensive (form +0x4a6f) is
  one value: `GetModifiedBudget` 0xc4f103 makes every nation's budget x 0.9 while it is above 0 (`ResolveArmyOffensive`
  counts it down monthly; its reset in `WarStartsWithNation` 0xd334c0 is unreachable, only `DoPeace` ends it).
  `CheckTurnEvents` 0xce30f0 (exits in month 2 of the start years and at War 0/-1): Random(4)+2 < War: the war loan
  (War mod 3 == 1, Unrest < 8, BM < 22, Random(16)+10 > BM: BM +1 with the leader's/parliament's message), then at War > 4
  the peace decision (B4, K's PC); at peace `SmallFleetCheck` 0xc87dd4: A/B (the host player's dialog and early events),
  C complaints (prestige -1), D budget rules (D1 the most tense nation's threat: War > -36 lowers that tension, else BM +1
  and ParliamentDelay 10..13; D2 government 0; D3 a disarmament treaty BM -1; D4 after 1950 BM -1), E the notice of the
  smaller share after 1920. `BuyTechnology` (the host player's choice) can take K as the partner.
- **How (economy.cpp; host only, `hookcb::IsHumanRemoteNation(K)`).** After the game's `CheckPlayerFunds` and
  `CheckTurnEvents` each joined K runs the player's version with K's fields, K's own war counter (`hookcb::KWarCounter`,
  the pre-step value the game's War has there) and K's own ParliamentDelay and army-offensive months (the game's global
  ArmyOffensive is never written for K); K's budget is x 0.9 while K's months last (`GetModifiedBudget`'s ArmyOffensive
  test answers for K); the months count down where `ReduceTimeLimits` counts the game's and end at K's peace. The
  corruption question takes its first answer (the money goes, no further effect); the Air Force event and
  SmallFleetCheck's A/B parts are not run for K. The texts (the game's, with K's parliament and leader) go to K's player
  with the new month (header `econnews`) and are shown in the game's turn report there; header `kao` sets the client's
  own ArmyOffensive (its budget figures show the x 0.9; that copy's General section is never merged back). Per-K state:
  `rtw3mp_session.ini` key `kecon=K:pd:ao:notice,...` (with the month, as `kwar`).
- **The computer's last decisions for K (audit, static).** Kept away from K now: `CheckNewResearchEffectsForAI` (K's
  doctrine set by the computer), `SetAmmoLoadoutAndUsage` (January), `MaybeSendHomeShipInColonies`, `PutShipOnASWPatrol`,
  `CheckOneShipsOrBaseHasAircraft` (g_skips); `CheckSubmarineActivity`'s escorts built and paid for K and its ASW research
  priority (K answers as the player), `BuyTechnology` with K as the partner, the scrap sell-on with K as the buyer (twice
  the scrap value), `ScrapShipsAfterPeace`'s automatic scrapping of K's armed merchant cruisers (the player's "No" path),
  the treaty cut R1 (K gets the player's second -1, no tension message), `CheckChineseConcessions` for K = China. Already
  away: `AINationAdjustBudget` (the only source of "... is said to have increased the naval budget"), `AIMonthlyBuild`,
  `CheckAIRebuilds` (refits), `PutAIShipsInStatus` (reserve, scrapping), `DetermineAINationResearch`, `SetAITraining`,
  `CheckAISpying` and the B5 money sites. Repairs cost nothing for anyone; `ScrapShipsAccordingToTreaty` and
  `CreateArmsControlTreaty` scrap for the player too. Left: aircraft development (the computer develops prototypes and
  licences for K; K's requests live on its PC), `CheckForRevolutionInEnemyNation` and `WarHelpFromAllies` (war effects,
  B5 later), the global-economy event, `ReduceTimeLimits` R2/R4, GetModifiedBudget's x 0.97 (the host's War).
- **Record.** The month's line gains `; the player's money rules (war counter W): debt ..., corruption ..., hoarding ...,
  war loan n, complaint ..., budget rule ..., notice n; funds +f, BM +g, prestige +h, N message(s); treaty cut t; army
  offensive A month(s) (budget -x), ParliamentDelay P; month's end funds E (other changes +o); left out for the player:
  ...`; `econ K` adds `ran kwar debt ... ruleFunds ruleBM rulePrestige news kao kpd aoCut ended fundsEnd otherFunds asw
  techPartner shipBuyer amcKept china aoNow pdNow ...` (old keys unchanged). TEST knob `econrand <v>` (K's rules draw
  min(v, n - 1)); bridge `ffield <Field>` reads a campaign field.
- **Status: proven live** (econ driver, dist_rv_13, the 1900 fixture, Germany = nation 1, five processed months, 121/121,
  116 s open): M1 debt (-50000, war counter -10): "outraged", prestige -2, ComputeExpenses' balance only (+123); M2 at war
  with nation 4 (counter 4): the war loan, BM 10 -> 11; M3 SmallFleetCheck D1 (counter -40, tension 25 with Britain): the
  hawk party's bill, BM +1, K's ParliamentDelay 13 (the host's untouched), the client's turn report showing the text; M4
  hoarding rule 2: 176630 -> 158967 (a tenth), BM -1, K's army offensive 8 (client ArmyOffensive 8); M5: 7 months, K's
  budget x 0.9 (aoCut 5540). Every month: the month's end funds == ComputeExpenses' + the rules' funds (other 0), the
  funds before == the client's submitted funds, the stand-in its own record, nation 3 and nation 0 no record and no
  suppression, the global ArmyOffensive 0; `AI suppressed` for nation 1: AIMonthlyBuild, AINationAdjustBudget,
  CheckAIRebuilds, CheckAISpying, CheckNewResearchEffectsForAI, DetermineAINationResearch, MaybeSendHomeShipInColonies,
  PutAIShipsInStatus, SetAITraining. Review fixes before the run: K's army offensive no longer ends at a war's start.
## Testing 2026-10-22: the 0.5.0 release candidate dist_rc050_1 (v0.5.0)
The release candidate dist_rc050_1 (rel-050 dc2cbfc: 0.5.0, live-battle engine 17, protocol 3) on the reg-050 runner,
installed from its frozen snapshot (`--dist`), 0.4.1 (dist_rc041_2) put back after every run; slots 1 and 9 and the
Preferences identical to their backup after every run (hash), no copy left running, the game folder copies deleted.
- **Runs.** The default run 814/822 (1723 s with the game open); `--stress` (28 scenarios with the missile fixture)
  1158/1163 (5287 s); `--stress --only diplomacy,battles,victory_points,lockstep_month` (the re-run and `lockstep_month
  --stress`) 291/295 (489 s). `relations_peace` 33/33 in both full runs (B4: offers, "Propose peace", terms, the save
  after the apply) and the money checks of every month (`econ_month_check`, B5) ran in all three. Every long live-battle
  case passed: `lockstep_month` 79/79 twice (an agreed end at 514 minutes, the game's end inside a minute), `twopc` full
  83/83, `lockstep_rejoin` 41/41, `lockstep_resume` 43/43, `lockstep_torpedo` 20/20, `lockstep_stress` 119/119,
  `lockstep_strike` 21/21, `lockstep_missiles` 23/23, `lockstep_air` 41/41.
- **Product: the AI peace under the relations v2 mask (regressions against 0.4.1).** `CB_BeforeAIPeace` (game.cpp) enters
  the B2 mask (`relations v2: HandleAIPeace runs without the pairs of nation(s) 1`: every pair of nation 1 with 1..8
  reads tension 5) before the mod's own AI-peace guards run, and `CB_AfterAIPeace` leaves it only after them:
  - `victory_points` h) failed in all three runs: `aipeace 2 0` (the AI war 2-3 ends, the players' war 1-2 goes on) left
    no `AI peace: VP of player nation K kept at P (the game set 0)` line and the VP of nations 1 and 2 at 0 (319/135,
    327/153, 315/129 -> 0). `AIPeaceRestoreVP` asks `AtWarWithAnyone(i)` while nation 1's pairs read peace, so neither 1
    (at war with 2 only) nor 2 (its war with 1 masked) gets its VP back. dist_rc041_2 passed h) in both its runs.
  - In two of the three runs the host showed the game's box "Error in HandleAIPeace procedure! Access violation at
    address 01058F0B in module 'RTW3.exe'. Read of address 000001A5" at `diplomacy`'s forced AI peace (`aipeace 2 1`:
    nation 2's only war is the players' war with nation 1). `OnBeforeAIPeace` (session.cpp) skips the game's routine for
    a nation whose wars the players declared, but under the mask `AtWar(1, 2)` reads peace: no `AI peace (badLoss=1)
    skipped for nation 2: its wars were declared by the players` line, the game's routine runs for a nation without a
    visible war. In a campaign the same can come from any HandleAIPeace call for a computer nation whose only war is
    with a joined nation under the player's rules.
  - The fix to try: run `AIPeaceKeepVP` and `OnBeforeAIPeace` before `MaskEnterSafe(0, "HandleAIPeace")` (and enter the
    mask only when the routine runs), and leave the mask before `OnAfterAIPeace` and `AIPeaceRestoreVP`; then run
    `diplomacy` and `victory_points` again (and the default run).
- **Product note (no effect on the money).** A war of the host's nation with nation 1 declared at the month's end
  (`diplomacy` B) opens an economy record of its own with the month's date after the month's record was logged: `econ 1`
  then shows `pending=1 expenses=0 warStarts=1`, and the next month logs a second line for that date ("1901-01: no
  ComputeExpenses; ... war start +1 (computer's +2)").
- **Suite fixes (reg-050).** bb91257 `relrules_month_check`: the joined player's own event of the new month (its
  intelligence result `#-1 'Intelligence'`: 1-4 3->4; a random event #34: 1-3 3->4, 1-4 4->5) changes nation 1's pairs
  after the host's month like its relation turn; the first change by the player's side must start from the value before
  the month (`event_tension_changes`). f64beb4 `econ_month_check`: with a record opened after the month's end it reads
  the month's own line (`ECON_LINE_RE`, "month's end funds F (other changes +0)"). 4893861 `player_copy` a) and g): the
  copy's [General] War is the player's own war counter since 0.5.0 (saveio `MakeNationPlayer`, `own_kwar`: 0 after the
  last war ended that month; the host's guess -1 before). 01565b0 `lockstep_air` g) says why its wait for a ready
  formation ended. Self-test 121/121.
- **Flaky (passed when run again).** `battles` b) in the `--stress` run: seven attempts avoided or declined by the
  enemy (the game's choice; 71/71 in the re-run). `lockstep_air` g) in the default run: "a formation B readied became
  ready (status 2) within 27 minutes (for Stand down)" (41/41 in the `--stress` run).
- **Mixed versions** (TESTING.md, Mixed versions; driver `mixed_050.py`): a) a 0.5.0 host with a 0.4.1 player: the join,
  two months, the computer's relation rules for its nation and the player's money rules without their messages (4
  message lines sent with `--debt`, none shown), the host's battle fought live (415 and 424 minutes equal, no DESYNC):
  66/66, 67/67. b) a 0.4.1 host with a 0.5.0 player: the computer's rules for its nation (no relation turn, the host's
  war counter, no money messages), the host's battle fought live (416 minutes equal, no DESYNC): 39/39. c) a 0.5.0 host
  with a 0.4.0 player: the battle refused live with the new notice ("live battles need the same live-battle engine on
  both PCs: this host has 0.5.0 (engine 17), Captain has 0.4.0 (engine 14)."; the player's: "the host has 0.5.0
  (engine 17), you have 0.4.0 (engine 14)."), nothing stuck: 52/52 (50/51 before a driver fix: the debt's messages come
  in the months after it).
- **Verdict: NO-GO** for publishing this build: the AI-peace regressions above (a game error box on the host, the
  players' VP lost) sit in the path the 0.5.0 relations rules add. Everything else, the mixed versions included, passed.
  Game open: 8562 s in all (the three runs 7499 s, the mixed runs 1063 s).

## Fix 2026-10-22: the game's AI peace and the relations mask: VP kept, no access violation (v0.5.0)
Main session, on `rel-050` after the NO-GO of the 0.5.0 release candidate dist_rc050_1 (the testing entry above).
- **Seen:** `victory_points` h) failed in 3 of 3 runs: a game AI peace set the VP of the players at war to 0 (319/135 ->
  0/0; 0.4.1 passed it). The `diplomacy` scenario's forced AI peace (`aipeace 2 1`) gave the host the game's box "Error
  in HandleAIPeace procedure! Access violation at address 01058F0B ... Read of address 000001A5" in 2 of 3 runs.
- **Cause:** `CB_BeforeAIPeace` (`game.cpp`) entered the relations-v2 mask (`AIMaskEnter`: every pair of a nation
  under the player's rules at tension 5, no alliance) before the older protections ran, and `CB_AfterAIPeace` left it
  only after them. `AIPeaceRestoreVP` checks `AtWarWithAnyone` before it puts a player nation's VP back, and saw a
  nation whose wars were hidden as at peace, so the game's VP reset stood. A computer nation whose only war was with a
  nation under the player's rules entered the game's `HandleAIPeace` with no visible enemy: the skip in
  `OnBeforeAIPeace` covers only wars the players declared on each other (`g_pactWars`), and the game's routine read
  through the enemy it couldn't find.
- **Fix:** the nation's war state and the players' VP are recorded before the mask. After the masks, a nation that was
  at war and has no visible war left is skipped: the game's routine doesn't run, since those wars end only by the
  players' own peace (B4). Log `AI peace (badLoss=N) skipped for nation N: its wars are with players' nations`. The
  masks are left before `AIPeaceRestoreVP`, which now sees every war. The strategic layer only: `MP_LS_LEVEL` stays 17,
  protocol 3. Verified by the regression of the next release candidate (`diplomacy`, `victory_points`, the default
  suite).

## Testing 2026-10-23: the 0.5.0 release candidate 2 dist_rc050_2 (v0.5.0)
The release candidate dist_rc050_2 (rel-050 b5b0da7: 0.5.0, live-battle engine 17, protocol 3; the AI peace fix of the
entry above) on the reg-050b runner, installed from its frozen snapshot (`--dist`; RTW3MP.dll SHA-256 55806E24...8702,
version.dll A5098282...F670), 0.4.1 (dist_rc041_2) put back after every run; slots 1 and 9 and the Preferences identical
to their backup after every run (hash; the backup itself equal to the one taken before the first candidate's runs), no
copy or delay proxy left running, the game folder copy deleted.
- **Runs.** `--stress --only diplomacy,victory_points` three times: 144/144 each (161, 163 and 160 s with the game open;
  `session` 17/17, `diplomacy` 74/74, `victory_points` 53/53). The default run: 835/836 (1779 s): `diplomacy` 75/75 with
  the new check, `victory_points` 53/53, `relations_peace` 33/33, every live-battle scenario passed (`lockstep` 33/33,
  `lockstep_ui` 41/41, `lockstep_limits` 52/52, `lockstep_pvp` 29/29, `lockstep_month` 49/49, `twopc` 60/60, `air_units`
  10/10, `lockstep_air` 41/41); its one fail is `battles` b) (flaky, below). `--stress --only session,events,battles,
  lockstep_month,lockstep_pvp,relations_peace`: 261/261 (898 s): `battles` 71/71 (b) fought at its first attempt),
  `lockstep_pvp` long 30/30 (the players' live battle ended by agreement at 424 minutes), `lockstep_month` long 79/79 (an
  agreed end at 457 minutes, the game's end inside a minute), `relations_peace` 33/33, `events` 31/31.
- **The AI peace fix works** (b5b0da7), in all four runs with `diplomacy` and `victory_points`:
  - `diplomacy`'s forced AI peace (`aipeace 2 1`, nation 2's only war the players' war with nation 1): the host logs `AI
    peace (badLoss=1) skipped for nation 2: its wars are with players' nations` (01:15:06, 01:18:16, 01:21:04, 01:26:40),
    the players' war goes on, no game box.
  - `victory_points` h) (`aipeace 2 0` ends the AI war 2-3 while the players' war 1-2 goes on): `AI peace: VP of player
    nation 1 kept at 315 (the game set 0)` and `AI peace: VP of player nation 2 kept at 129 (the game set 0)` (327 and 129
    in the default run); the AI nations get the game's 0.
  - "No game error box on either copy in the whole run" passed in every run; no log of any copy has an access violation.
- **Suite (reg-050b).** 8896b72 `diplomacy`: the host log's skip line after the forced AI peace is a check (the older
  guard's `... its wars were declared by the players` counts too; a SKIP-note when nation 2 has another war), so the
  path no longer depends on the box, which came in 2 of 3 runs: dist_rc050_1's runs had no skip line at all. Self-test
  121/121.
- **Flaky (passed when run again).** `battles` b) in the default run: six attempts in three months, five "The Spanish
  navy declines battle! You gain 360 VP!" (`StartPlayerBattle 0 attacks 7 in Northern Europe -> none`, outcome
  `avoided`, its VP merged) and one declined by the client's side (`battle 5: resolved by the game (Captain declined
  it)`); the next run fought at its first attempt (`-> battle`, `battle 1: fought`). Spain was blockaded by nation 1 in
  both runs (`blockaded: 7` at every battle load), so the blockade doesn't decide it: the game's choice, as in the first
  candidate's `--stress` run.
- **Mixed versions** (`mixed_050.py` and its helper with the reg-050b runner): a) a 0.5.0 host with a 0.4.1 player
  (`--debt -30000`), run again since the host's AI-peace path changed: the join (`Captain: mod 0.4.1, live-battle engine
  17 (this host: 17), player's relation rules 0`), two months, the host's battle fought live (424 minutes equal, no
  DESYNC, ended by agreement), the money rules' 4 message lines not shown on the 0.4.1 PC, peace and a normal month, no
  game error box: 67/67 (148 s).
- **Verdict: GO** for publishing 0.5.0 with exactly this build (dist_rc050_2). Game open: 3309 s in all (the five suite
  runs 3161 s, the mixed run 148 s).

## Feature 2026-10-23: B5 blockades for a joined nation (0.6.0-dev)
- **What.** The game decides blockades for its player's wars only (nation 0, the host's): the points, the Blockaded
  flags, their effects and messages. Now each joined nation K under the player's rules gets them in its own wars too:
  K's blockades of its enemies and its enemies' blockades of K, with the points in K's war score (and the enemy's), the
  effects on K's ship moves, trade and unrest, and the messages in K's turn report. The host's own nation and the
  computer's wars among themselves are unchanged.
- **Facts (static, 1.01.44; files\rv\b4\re_b5b6.md and files\b5\dis_*.txt in the developers' files; GAME-INTERNALS
  "Blockades").** `IsBlockaded` 0xc618e8 ignores its argument; `CheckBlockade` 0xc61920 is True at the first single enemy
  (by the nation's own relation cells) above the minimum and 1.1 x the nation's strength in its build or blockade area.
  `CheckBlockadeStatus` 0xccc1c4 (the load, DoTurn, DoStrategicTurn 0xce1bb9) only flags nation 0 and its enemies and
  only while War > 0: 20 x (Random(3) + FleetSize) to the other side per enemy. `DoBlockadeEffects` 0xd007d4 (first call of
  each nation's turn of `CheckUnrestChanges`' loop, 0xcbdbeb): x = 20 x (R3 + FS) + 10 x (R2 + FS) (nation 0 blockaded:
  EnemyVP; an enemy: the game adds x to that enemy's own VP, its text "Gain x VP for blockade of enemy"), unrest by War
  and a hardship picture. The loop's own rule 0xcbe821 for any flagged nation: unrest by War, a picture at 7, else the
  turn message "Shortages due to the blockade are causing hardship for our people!" (into the host's report).
  `StopBlockadedMoves` 0xd054d8 (DoTurn2, before the month's check) stops moves out of a flagged nation's build area only
  while War > 0 (its own message is never shown). The flag is not saved; ProductivityGain, CheckFuelShortage,
  GetMaximumShipyardCapacity, AdvanceShipConstruction, the raiders and EventLikelihoodCheck read it for any nation.
- **Before.** K's flag was set only in a war with the host (and cleared at every check); K's wars with computer nations
  had no blockade at all; in a war with the host DoBlockadeEffects gave the blockaded K +x VP (the game's quirk) with the
  host's War in its unrest roll and the texts in the host's report.
- **How (`src/mod/warfx.cpp`, host; `hookcb::PlayerRulesNation(K)`, K's own war counter > 0).** game.cpp's
  CheckBlockadeStatus hook (the 0.4.0 load VP guard) now tells warfx after every call (`SetBlockadeCheckedHook`; the
  load's after its VP were put back). Each K's frame: K blockaded (the game's `IsBlockaded`) -> K's flag and 20 x (R3 +
  FS) for each computer enemy at war with it; each enemy j (not the host) blockaded -> j's flag, 20 x (R3 + FS) for K.
  An enemy under the player's rules credits its own gains (its own frame), so nothing counts twice; the host's wars keep
  the game's check and points. The load's call: the flags only (no draw, no points). Points: `hookcb::CreditWarVP` (the
  nation's VP field, as battles do, and the pair's war score: the B4 ledger or a war between players). Hooks:
  `DoBlockadeEffects` (K: the player's version: x split over K's blockaders, each found by the game's test with K's
  other wars' cells read as peace, the host's share to its VP; unrest by K's counter; K's texts. A computer enemy flagged
  by the frames only: x to the K's at war with it, unrest by their counter, the hardship texts to them. An enemy the
  host's check flagged too: the game's own, and each K its own x and text), with the loop's blockade rule for these
  nations run there by the frame's counter (their flag reads 0 for the rest of that nation's turn of the loop; put back
  at the next nation's call or when `CheckUnrestChanges`, hooked, returns); `StopBlockadedMoves` (War 1 for the call
  while the host is at peace and a frame's flag is set). The texts go with K's `econnews` (0.5.0 clients show them too);
  header `kblk` (the nations flagged now) sets the client's flags after its load (its own load's check sees the host's
  wars). The rules reproduced in C++ are checked by pattern at startup (else UNAVAILABLE and the game's own).
- **Compatibility.** Protocol 3; new header key `kblk` (older clients keep their own load's flags), war texts inside
  `econnews`. Engine 17 unchanged. A 0.5.0 host gives nothing new (no frames, no `kblk`); 0.5.0 clients of a 0.6.0 host
  get the texts in their turn report and the points in their VP and Enemy VP, without the flags on their screen.
  Existing campaigns: the frames start from the campaign's wars and counters, nothing is saved.
- **Status: proven live** (files\b5\b5_live.py --part a; the 1900 fixture: K = Germany, J = Russia, the host at peace).
  dist_b5_3 (RTW3MP 02B7586DF013EACB), 100 s open: M1 the game's own test: Germany blockades Russia from the war's first
  month (`1(war 1, enemies 3): not blockaded, blockading 3`): Germany +100 +100, VP and the ledger by the same, Russia's
  flag on both copies, "Gain 100 VP for blockade of enemy" in Germany's turn report; M2 (TEST `warfxblk 3 1`, `warfxblk
  1 0`, `warfxrand 0`): Germany +60 (20 x FS 3) +90 (30 x FS), Russia's unrest 3 -> 5; M3 (Germany forced blockaded):
  Russia +60 +90 (its VP and its side of the ledger), Germany's unrest 1 -> 3 by its counter 3, its texts "The enemy
  gains 90 VP for blockading us" and "Shortages due to the blockade ...", Germany's flag on both copies; M4
  `StopBlockadedMoves` with War 1 (`1:0/0`), the host's War back to -72. The host's VP and EnemyVP 0 throughout, the other
  nations' VP unchanged; a declined battle's war score (+1/+9) is the battle's. Suite `--group relations` (session,
  relations_peace, war_effects) 94/94, 138 s open (dist_b5_4).
- **Notes.** The war-weariness rule (0xcbe8fb) still weighs the host's war score and War for every nation; allied ships
  in a blockade count by the host's alliances (GetBlockadeStrength's own rule); the score of a war between the host and K
  gets no blockade points beyond the game's. A war between two joined nations gets them (each side's frame credits its
  own gains, into the war score of the peace terms): the suite's `victory_points` g) counts them (Germany blockading
  France there: +60 +90; a first 0.6.0 run failed on it).

## Feature 2026-10-23: B5 war help from allies for a joined nation (0.6.0-dev)
- **What.** The game runs `WarHelpFromAllies` for its player only: allies' operations add victory points, allies join
  the war, quarrel, hesitate (with the player's answer: lend-lease ships, 2000 of the ally's money, a revoked alliance,
  prestige), and nations at war with the player's enemy offer an alliance. Now each joined nation K under the player's
  rules gets the same in its wars with computer nations, with its own dialogs on its PC, and the host applies the
  results.
- **Facts (static, 1.01.44; GAME-INTERNALS "War help from allies").** `WarHelpFromAllies` 0xcd4454 (DoStrategicTurn
  0xce218f after the war step, War > 1); `GetRandomEnemy` 0xccd5cc (201 tries of `Random(8) + 1`); the joining helper
  0xcd42e4 gives nation 0 20 x (Random(22) + FS) VP and `SetAITension(ally, enemy, 50)`; lend-lease `DoLendLease`
  0xc77628 (TransferShipAsReparation, once per campaign: form `LendLeaseDone` +0x4a8b, saved); `CoBelligerentDelay`
  +0x2e9 counts down per nation at war with the player's enemy and is not saved (0 at every load).
- **How (game.cpp `AllyHelpPart`, K's PC; session.cpp `HostApplyAllyHelp`).** In K's relation turn (B3), right after
  `PvEnter` and before the relation routines: when the host sends `khelp` (0.6.0, with `rel=1`), K's counter (the War
  on K's PC) is above 1 and K fights a computer nation, the game's routine runs in K's view: the people's nations read
  as neither at war nor allied with anyone (their help and their wars are their own; cells put back afterwards), K's own
  LendLeaseDone (`klend`, kept by the host in `rtw3mp_session.ini` for any date: the game never resets it) and K's
  CoBelligerentDelay values (this PC's memory, as the game keeps them) in place, the lend-lease ships recorded by the
  peace part's `TransferShipAsReparation` hook. Its dialogs are K's player's; its relation and alliance changes go with
  the turn's diff (another pair: a computer nation joining K's war), K's VP with `vp`; MSG_RELATIONS gains `ally=1`,
  `allyvp` (the host adds it to K's war scores with its computer enemies, split evenly: the game adds it to the player's
  VP), `afunds` (other nations' Funds: an ally's -2000, computer nations, +-5000), `aships` (from a computer ally to K by
  ship id, as a peace's) and `lend`. TEST `allyrand 0` (client): the routine's draws 0, GetRandomEnemy's Random(8)
  going round 0..7 (all 0 would never find an enemy).
- **Compatibility.** Protocol 3; a 0.5.0 host sends no `khelp` (no ally help); a 0.5.0 client ignores it (no ally help,
  as before). Engine 17 unchanged.
- **Status: proven live** (files\b5\b5_live.py --part d, dist_b5_4 RTW3MP BDD131F4CD9E0661; Germany at war with
  Russia, allied with the USA, the host at peace), 64 s open, 44/44: M1 (the USA at war with Russia, `allyrand 0`): "The
  operations of our ally USA adds 13VP" (the picture on the client), the host's turn line `VP 13 allyvp 1-3+13`; M2 (the
  USA at peace with Russia at tension 10): "Our ally USA joins the war against Russia!", the host `war 3-4 VP 60
  allyvp 1-3+60` (the helper's 20 x FS), the war on both copies. The host's VP and EnemyVP unchanged. Natural months
  (no allies) log `ally help` in the turn with nothing to do. Suite `war_effects` M1 checks it (94/94 with the group).
- **Notes.** Not proven live: the hesitation question's answers (lend-lease, the ally's 2000, a revoked alliance) and the
  co-belligerent alliance offer (their records and the host's apply are the peace part's proven paths). K's wars with
  the host or another player get no allied help (the people's nations stay out).
## Feature 2026-10-23: B5 war weariness for a joined nation (0.6.0-dev)
- **What.** `CheckUnrestChanges`' war-weariness rule gives every nation of government type 2 unrest from the host's
  war: the host's War, its EnemyVP against its VP. A joined nation K under the player's rules now gets it from its own
  wars, by its own war counter and war score; the host's nation and the computer's nations keep the game's rule.
- **Facts (static, 1.01.44; GAME-INTERNALS "Blockades").** The rule 0xcbe8c0..0xcbe93d of the loop i = 0..8 (EBX = the
  loop's cell of the nation N): `call 0xccd5fc` (War > 0: `cmp dword ptr [War],0; setg al; ret`), N's GovernmentType
  +0x1cc = 2, the form's EnemyVP +0x4a63 > nation 0's VP +0x175, N's BudgetModifier > 18, then (N blockaded:
  Random(100) < 5 first) Random(100) < 5 -> N's UnrestLevel + 1 and TurnMessages.Add("Our people is getting
  dissatisfied with the bad war situation!") (the host's report). Before: K's unrest followed the host's wars (5 % a
  month while the host lost one, whatever K's own), the text went to the host, K's own wars gave none.
- **How (`warfx.cpp`; session.cpp `hookcb::KWarScore`).** MinHook at the rule's `call` (found by a pattern of the whole
  rule with the RTTI offsets; the War > 0 helper's code, the two Random calls and the text checked, else `war weariness
  UNAVAILABLE` and the game's rule). The detour reads N from [EBX]: K under the player's rules -> the rule in C++ with
  K's counter (`KWarCounter` > 0), its GovernmentType and BudgetModifier, its war score (`KWarScore`: K's points and its
  enemies' over all its wars, the B4 ledger with the computer's nations and the score of each war with the host or
  another joined nation), K's flag as the month's blockade check left it (the frames clear it for the rest of K's
  turn), the game's Random (TEST `warfxrand`); the unrest on the host's month, the text with K's `econnews`; then AL = 0,
  so the game's rule is skipped for K. Any other nation: the game's test and rule. A line per K and one per month (`the
  player's rule for nations ..., the game's own for ...`). Independent of the blockade hooks.
- **Compatibility.** Protocol 3, no new key (the text goes with `econnews`, which 0.5.0 clients show). A 0.5.0 host
  keeps the game's rule for K (the host's war). Engine 17 unchanged.
- **Status: proven live** (files\b5\b5_live.py --part w, dist_b5_7 RTW3MP 6F80FB741E8C5396; the 1900 fixture, K = Germany,
  J = Russia, the host at peace; 57/57, 76 s open). Germany's GovernmentType 2 (its own is 1) and BudgetModifier 20
  (`nfield` on both copies), `warfxrand 0`. W1 `kwarvp 1 3 100 0`: "war counter 1, war score 160:0 (its blockade of
  Russia added 60), government 2, BM 20, blockaded 0 -> no (not losing)" and "the player's rule for nations 1, the game's
  own for 0,2,3,4,5,6,7,8"; W2 `kwarvp 1 3 0 500`: "war counter 2, war score 60:500 ... -> unrest 2 -> 3", the message
  "Our people is getting dissatisfied with the bad war situation!" for nation 1, shown in the client's turn report;
  the host's VP and EnemyVP 0 throughout, its counter stepping at peace. Suite `war_effects` M1/M2 check both.
- **Notes.** The same loop's rules for the nations at war with the host (intel reports, the computer's BudgetModifier
  cuts above unrest 7/8) stay the game's, as 0.5.0 left the computer's unrest cuts.

## Testing 2026-10-23: B5 war loans of a joined nation at war, confirmed live (0.6.0-dev)
- **What.** 0.5.0's money part 2 gives each joined nation K the player's war loan (`economy.cpp` `TurnEventsK`, on the
  host after the game's `CheckTurnEvents`) by K's own war counter. Confirmed now in a month at war, with 0.6.0's war
  effects around it.
- **The rule (static, 1.01.44, `CheckTurnEvents` 0xce32c7..0xce3409).** Random(4) + 2 < War, War mod 3 = 1, UnrestLevel
  < 8, BudgetModifier < 22 and Random(16) + 10 > BudgetModifier -> the text (governments 4 and 5: 80 % "The <leader
  title> pledges additional funding for the navy! ...", else "PX rubber stamps additional war loan! ..."; the others
  "PX authorizes additional war loan! ...") and BudgetModifier + 1. For K: the same tests in the same Random order with
  K's counter before the host steps it (as the game's War at that point), K's fields, leader and parliament (PX), the
  text with K's `econnews` (the client's turn report), the BudgetModifier on the host's month (the client gets it with
  the month).
- **Evidence.** files\b5\b5_live.py --part w, month W3 (dist_b5_7): Germany's counter 4 (`kwar 1 4`), `econrand 1`
  (Random(4) + 2 = 3 < 4, Random(16) + 10 = 11 > 10), BudgetModifier 10 on both copies: `econ 1` loan=1, BudgetModifier
  10 -> 11 on the host and the client, "The Reichstag authorizes additional war loan! The naval budget increases." in
  the client's "Turn messages"; the host's VP and EnemyVP unchanged. Suite `war_effects` M3 (new check) the same: group
  `relations` 97/97, 119 s open (with the war weariness checks).
- **Gaps: none in the rule.** Kept as they are: the game draws one Random(4) for the loan and the month's peace decision,
  K has two (the host's for the loan, its own PC's for B4's peace decision); the text is a line of K's turn report,
  not a picture (0.5.0's choice for all of K's money texts). Compatibility unchanged since 0.5.0.

## Feature 2026-10-23: B5 building for war for a joined nation (0.6.0-dev)
- **What.** The game runs `CheckBuildingShipsForWar` for its player only: at war, the player's ships building in a
  foreign yard are seized by the yard's nation, and foreign ships in the player's yards are seized by the player (at
  war at once, otherwise at the player's answer). The War > 0 tests of the build rules every nation shares read the
  host's War. Now each joined nation K under the player's rules gets the seizures on its own PC (its pictures and
  question), and the shared build rules answer each joined nation's own war counter on the host.
- **Facts (static, 1.01.44; GAME-INTERNALS "Building for war").** `CheckBuildingShipsForWar(NewWar)` 0xcd5ad8
  (`DoStrategicTurn` 0xce21c5, after the war step while War > 0): the ships `IsBuilding` whose `BuildingNationIdx` +0x61
  (an index) is not their nation's and not `Rebuild` +0x58; nation 0's ship in yard b: b's Tension > 6 / 7 / 8 (4 %,
  10 %, always) -> b takes it; a nation's ship in yard 0: Tension > 40 -> taken, else (2 months left or NewWar)
  `ExecuteGeneral` (yes: Tension + Random(5) + 1, taken); `TransferShipAsReparation` and `UpdateUIValues`. Computer
  nations order ships in other nations' yards (`CheckAIBuildingShipAbroad`). The shared rules' tests (`ReduceTimeLimits`,
  before the war step): `AdvanceShipConstruction` 0xcd921e (submarines, nation at [ebp-0x30]), the three of
  `CheckForConstructionDelays(NatIdx)` 0xc86314, `HandleEconomicGrowth` 0xcdc41c (the war budget's BudgetModifier + 1,
  nation in EBX; nation 0's picture by War's parity at 0xcdc4c0).
- **How.** Host (`warfx.cpp`): MinHook at the two `call`s of the War > 0 helper (the loop's nation from [ebp-0x30] / EBX:
  a joined nation answers its own `KWarCounter` > 0), at the grant (0xcdc50b: the joined nation's text, "War budget
  means increased allocations to the navy!" at an even counter, "War emergency funding granted to the navy!" at an odd
  one, with `econnews`) and at `CheckForConstructionDelays`' entry (the game's War holds the nation's counter for the
  call, put back in `__finally`); each checked by pattern (the helper's code, Random, GetBNation, the texts, the loop's
  index; else `building for war UNAVAILABLE` and the game's). K's PC (game.cpp `BuildWarPart`, after the allies' war
  help in the relation turn's view, while the host sends `kbuild` and K's counter is above 0): the game's routine with
  NewWar = counter 1; the ships between K and a computer nation get their `BuildingNationIdx` in view terms, every other
  ship reads as built at home (put back afterwards); the hand-overs are recorded by the peace part's
  `TransferShipAsReparation` hook and go to the host as `bwar`/`bships` (by ship id, name, reason, the log's renamed
  line); `HostApplyBuildWar` hands them over with `HandOverShip` (only between K and a computer nation); the tension of a
  seizure from a nation at peace goes with the turn's diff. TEST bridge `shipbuilder`.
- **Compatibility.** Protocol 3, engine 17. New keys: `kbuild` (header, with `rel=1`), `bwar`/`bships` (the relation
  turn). A 0.5.0 client ignores `kbuild` (no seizures for its nation, as before); a 0.6.0 client of a 0.5.0 host gets no
  `kbuild` and runs none. The shared build rules follow every joined nation's counter on a 0.6.0 host, whatever the
  client.
- **Status: proven live** (files\b5\b5_live.py --part e, dist_b5_8 RTW3MP 65DCAFF8677CA262; the 1900 fixture, K =
  Germany, J = Russia, the host at peace; 34/34, 44 s open). `shipbuilder` on both copies: Germany's B Wettin (487) in
  Russia's yard, Russia's B Retvizan (496) in Germany's; Germany's BudgetModifier 5. Host month: "nation 1's war budget
  test answers at war (its war counter 1; the host's at peace)", "war budget: BudgetModifier 5 -> 6", "construction
  delays test answers at war"; the client's turn report "War emergency funding granted to the navy!". Germany's relation
  turn (counter 2): the pictures "Due to the current war situation, the authorities in Russia have decided to seize our
  ship B Wettin, currently building there." and "We have seized the Russian ship B Retvizan, ...", the client's line
  "handed over 487:1>3 496:3>1", the host's "seized ship 487 1->3 seized ship 496 3->1", `findship` on both copies; the
  host's VP and EnemyVP unchanged. Suite `war_effects` (M1 the seizures, M3 the war budget 3 -> 4 before the war loan
  -> 5): group `relations` 98/98, 123 s open.
- **Notes.** Ships between two people's nations are not seized (the host's own pairs keep the game's rules on the host:
  its ships in K's yards by K's tension, K's ships in its yards at the host's answer). The other texts of these rules
  (a month more or less for a ship) stay nation 0's, as before.

## Investigation 2026-10-23: B5 round 2: the allies' war help outcomes (static) and the enemy-revolution rule (design) (0.6.0-dev)
- **The allies' war help: the outcomes no live run has shown yet, verified statically** (game.cpp `AllyHelpPart`,
  session.cpp `HostApplyAllyHelp`; GAME-INTERNALS "War help from allies"). Each travels by a path a live run did prove:
  - Lend-lease ships (`DoLendLease` 0xc77628, once a campaign): `TransferShipAsReparation` from the computer ally to K,
    recorded by the peace part's hook (`CB_TransferShip` into `g_allyRec`), sent as `aships` and handed over by
    `HandOverShip` (computer nation -> K only). The same hook, record and `HandOverShip` carried the seizures of
    "building for war" live (Russia's B Retvizan to Germany, files\b5\run_e1.txt) and B4's peace ships; `lend` sets K's
    LendLeaseDone on the host (`klend` back to K, any date).
  - The 2000 of the ally's money: `AllyMaskLeave` takes every other nation's Funds change (`allyFunds`), sent as
    `afunds`, applied by `AdjustNationField(n, "Funds", d)` (computer nations only, capped at +-5000).
  - A revoked alliance (the answer's `MessageDlg`) and the co-belligerent offer (Allied := 20, or the nation's Tension + 1):
    alliance and tension cells of K's pairs, read by the relation turn's diff after the part (the B3 path every turn
    uses); CoBelligerentDelay stays in K's PC's memory (`g_kCob`, as the game keeps it unsaved).
  - Cheap test switches would need a forced answer in `ExecuteGeneral` and a forced branch past `GetRandomEnemy`;
    `allyrand 0` reaches the operations and the joining helper only (both live in round 1). Left for a later suite step.
- **The enemy-revolution rule (design, not built).** `CheckForRevolutionInEnemyNation` 0xcc5c90 (`ReduceTimeLimits`,
  before `CheckTurnEvents`): for each nation i at war with nation 0 (Tension > 40): UnrestLevel - 1 above 8 at War 1
  and at War 2, governments 4/5 above 6: 10 % - 1; `CheckTotalWar(i)` -> stop; UnrestLevel > 10 and Random(10) <
  UnrestLevel - 10 -> a revolution: VP > EnemyVP and Random(100) < 60: "The government of X has collapsed in revolution!
  ... harsh peace conditions." (else "racked by unrest ... disputed territories" / "serious internal disturbances ...
  compromise peace"), Revolution.jpg, `ReturnTakenPossessionsAfterWar`, `PickShipAsReparation`, `SetAITension` /
  `SetAIAlliance`; with other enemies left (`HasOtherEnemy`) "The other members of the enemy coalition resolve to fight
  on." (a separate peace with i), else "The enemy coalition collapses. A general peace is concluded." (`ShowPeaceTreaty`,
  `DoPeace`). Today it runs for the host's enemies only (B2's mask keeps K's pairs out).
  - Proposal: on K's PC inside `PeaceRunGuarded`'s turn path (the counter before the step, as the game's
    `ReduceTimeLimits`), before the decision and with the peace records on: `CheckForRevolutionInEnemyNation` in K's
    view; a general peace comes back as an accepted peace (B4's `paccepted`, code -2: the host's existing apply path:
    lands, ships, treaty, the wars' end); the decision is skipped when it ended the war.
  - Two host-side additions it needs: (1) a separate peace (`pseparate=J`: only the war with J ends; its lands, ship and
    treaty applied, K's other computer wars go on: today `paccepted` ends all of them); (2) the computer enemies'
    UnrestLevel changes (the relief at War 1/2) as `pdelta` UnrestLevel (today BaseResources, Prestige and BudgetModifier
    only). The host's computer enemies keep the game's rule. A war between K and the host (or two joined nations)
    stays out of it today on both sides (B2's mask hides K's pairs from the host's rule): a revolution there would end a
    war between players, so it should go through the players' own peace (the 0.2.5 treaties), not `DoPeace`: to decide.
  - Live proof plan: K at war with J, J's UnrestLevel 20 on both copies (Random(10) < 10: a revolution every month),
    K's war score from `kwarvp` for each branch; a second enemy for the separate peace. Effort: M.

## Investigation 2026-10-23: B5 round 3: the enemy-revolution rule for joined nations (0.6.0-dev)
- **Facts** (`CheckForRevolutionInEnemyNation` 0xcc5c90, constants read from the image): the branch is harsh (VP >
  EnemyVP and Random(100) < 60: K's BaseResources +1000, Prestige +1, the enemy's UnrestLevel := 6, HasLostWar),
  territories (EnemyVP < VP * 1.3: +500, Prestige +1, HasLostWar) or compromise; `HasOtherEnemy` 0xd0b4b0 returns 0, 1
  (another enemy) or 2 (another enemy with more tonnage than the revolting one, `GetShipTonnage`). Then 2 and VP <
  EnemyVP * 1.5, or EnemyVP > VP * 1.1 and Random(100) < 50 - War / 2: "The other members of the enemy coalition resolve
  to fight on." (VCL `ShowMessagePos` 0xa07124: a box, not the turn report), the enemy's Tension with the player :=
  Random(3) + 2 and its Prestige := 0, no `DoPeace` (a separate peace); otherwise the general peace (the war record cell
  := 1 or 3, `ShowPeaceTreaty`, `DoPeace`). The revolting nation's wars with everyone end (`SetAITension` Random(5)) and
  its alliances too. `RevolutionPending` (+0x4a73, saved, reset by `DoPeace`) is an index in `BuilderNations` (the view's
  when K's view is in place); its post-loop rule raises the player's unrest ("Revolutionary ideas from X ..."). The first
  enemy for which `CheckTotalWar` is true ends the whole routine.
- **Built** (game.cpp `RevolutionPart`/`RevolutionGuarded`, called first in `PeacePart`, turn path only): the game's
  routine (B2's mask thunk bypassed: `MaskOrig`) on K's PC in K's view with B4's masks and records, War = the decision's
  (the counter before the month's step), K's own RevolutionPending in the form's field (the host keeps it: header
  `krevo`, result `prevo`, session file `krevo`); the people's nations read as at peace with K (B4's mask): their wars
  with K never end by it (logged, with "it would be a candidate" above unrest 10). The decision is skipped after a
  general peace or when no computer enemy is left. Result keys `pseparate` (the enemies whose war ended without
  `DoPeace`) and `pdelta` UnrestLevel (also `DoPeace`'s). Host (`HostApplyPeace`/`HostApplyRelations`): a separate peace
  erases only that war's score and blockade flag (K's VP and other wars stay; no war record, as the game keeps none for
  it), applies the revolting nation's own wars' end, its Prestige to 0 and a chat line; UnrestLevel deltas (-20..+10)
  for K's (ex-)enemies and nations left without a war. TEST `revotest <0|-1> [n:unrest ...]` (client: the next run's
  draws 0 and unrest values in K's copy; a people's nation's put back).
- **Live** (files\b5\run_r3.txt, dist_b5_11, 48/48, 70 s): K = 1 at war with Russia (3), USA (4) and the stand-in
  nation 2; R1 `revotest 0 3:25 4:3 2:25`, ledger 0:2000: "Russia is suffering serious internal disturbances ...
  compromise peace", the "fight on" box, "Revolutionary ideas from Russia ..." on K's PC; war 1-3 over on both copies,
  1-4 and 1-2 go on; host "peace 1-3 ... 3:Prestige-20 3:UnrestLevel-1 separate 1-3 own UnrestLevel+2", ledger 1-3
  erased, 1-4 kept, RevolutionPending 0 -> 3 kept by the host; nation 2 logged "stays out (unrest 25: it would be a
  candidate)", its and the host's unrest untouched by the rule, the host's VP/EnemyVP unchanged. R2 USA forced: the
  general peace (DoPeace, accepted=1), war 1-2 goes on, RevolutionPending 3 -> 0.
- **Compatibility**: a 0.5.0 host sends no `krevo`: a 0.6.0 client runs no revolution (as 0.5.0). A 0.5.0 client
  ignores `krevo` and sends no `prevo`/`pseparate`; the host keeps K's RevolutionPending. A 0.5.0 host ignores
  `pseparate`/`prevo` and the UnrestLevel entries of `pdelta`. Live battles unchanged (engine 17, protocol 3).
- **Gotchas**: two people at war with the same computer nation each give it the war-start relief (per player-war, as
  the blockade effects); DoPeace in K's view lowers every nation's unrest: a third nation still at war on the host keeps
  its own. Open: a suite check in `war_effects` (not added this round).

## Feature 2026-10-23: B5 round 4, joined nations' war effects after the merge (0.6.0-dev)
- **Merge smoke** (worktree from `int-060` 7b182df, B5 + B6): `war_effects` + `relations_peace` 98/98, 134 s open: no
  merge regression in B5's area.
- **The enemy-revolution rule and the host's own war.** On K's PC the host's nation already reads as at peace with K
  (B4's mask, round 3). On the host, the game's own rule (`CheckForRevolutionInEnemyNation` 0xcc5c90 from
  `ReduceTimeLimits`, P = the host's nation, through B2's thunk) loops over nations 1..8 whose Tension cell with nation 0
  (+0x256) is above 40 (0xcc5ceb) and makes a revolution of one above unrest 10 (0xcc5d6d: `UnrestLevel > 10` and
  `Random(10) < UnrestLevel - 10`). B2's AI mask hides only the cells between nations 1..8, so a joined nation at war
  with the host was a candidate: its revolution would have forced a peace between players by the computer's rule
  (harsh / territories / compromise, `DoPeace`). Static (the code read), not provoked live without the fix.
  - Fix (game.cpp `RevoCapEnter`/`RevoCapLeave` around the call in `MaskedCall`): a nation people play
    (`IsHumanRemoteNation`) at war with the host reads at most unrest 10 for the call; the rule's own changes (its
    relief) are kept on top of the real level. Log "relations v2: revolution (host): the nations people play at war with
    the host stay out of its enemy-revolution rule (no forced peace between players): 1 (unrest 25: it would be a
    candidate)".
  - Suite `war_effects` M4 (new): the host's nation at war with nation 1 too (`ls_war01`), client `revotest 0 J:25 0:25`,
    nation 1's unrest 25 on both copies, the month through `ls_month` (drop12). Live (dist_b54_2, 81/82, 217 s): a live
    battle 0-1 in the month (100000, drop12) driven by the live months' tools; nation 1's PC: "Russia is suffering
    serious internal disturbances. They have been forced to accept a compromise peace.", "wars ended 3 (the general
    peace: DoPeace)", "... stay out ...: 0 (unrest 25: it would be a candidate)"; host "peace 1-3", its own rule's line
    above; the war 0-1 went on on both copies (nation 1's unrest on the host 25 -> 23, its counter 5 -> 6), then the
    players' peace (`ls_peace01`). The one FAIL was the setup check's own expectation (the client sees the host's war
    only with the month; fixed).
- **Blockade tests count the allies by K's alliances.** `GetBlockadeStrength` 0xc76b74 (E = the blockader) adds, when E
  is at war with nation 0, every other nation at war with nation 0 (the coalition), else every nation allied with
  nation 0 (`Allied` +0x29a) and at an AI war (`IsAtAIWar` 0xc68ecc: a cell 1..8 above 40, never the PlayerNation), and
  nation 0 itself when E is its ally at an AI war. On the host's frame K's tests therefore counted the host's allies.
  - Built: game.cpp `HostViewEnter(K, war, human)` / `HostViewLeave()` (game.h), B6's inline view of
    `RunBattleGenerator` made a function (the generator now calls it; one view in the code). warfx.cpp `TestsInView`:
    the frame's three tests (K blockaded, each enemy alone with the other enemies' relation with K read as peace, each
    enemy blockaded) in K's view with no nation masked (K's wars with people's nations count too; the host's nation at
    index K); the credits, flags and effects afterwards on the real indices as before; the host's frame when the view
    is not in place (logged). Check line suffix "(its view)" / "(the host's frame)". Bridge `warfx strength E N [K]`
    (read-only probe: the strength and the game's own account on both frames).
  - Live (dist_b54_3, group `relations` 216/216, 367 s: session 17, relations_peace 33, war_effects 67, battle_generator
    62, battle_surprise 37): `warfx strength 1 3` -> host "blockade=94 [Germany: 94]", view 1 "blockade=104 [Germany:
    94, USA: 10]" (USA, nation 1's ally at war with Russia, counts in its view only); the month's line "1(war 1, enemies
    3): not blockaded, blockading 3 (its view)"; the generator through the shared view unchanged (62/62, 37/37).
  - Gotcha: in a view `Nation(i)` answers view indices: warfx takes the real records before entering.
- **(c) Raiders, submarines and the trade war of a joined nation** (`warfx.cpp`, host only; 5cededf).
  - Static: the game runs them for the host's wars only and only under War > 1: `DoStrategicTurn`'s submarine loop
    (`CheckSubmarineActivity(N)`, N = 0 and each nation at war with it) before `CheckAIvsAIBattles`, then (War > 1 at
    0xce20b6) `CheckRaiderActivity` 0xcce50c, the `TdlgSubmarineWarfareSummary`, `CheckResultOfSinkings` 0xd00ac4;
    both branches then reach `call <War > 0>` at 0xce21af. Raiders: Status 4, `IsOperational`, Used 0; chance 12/15/20
    by type (x1.5 in the defender's home area, else 2 / half by the defender's possessions there), 5 x Random(100), VP
    per merchant 0xd46bb0 into the form's `PlayerTradeWarfareVPThisTurn` / `EnemyTradeWarfareVPThisTurn`;
    `CheckRaiderProblem` 0xcd06ac interns (Status 7), scuttles (8, `SinkShip`) or sends home (0, RepairTime 2) and shows
    its boxes with `ShowMessagePos` 0xa07124 directly. `CheckSubmarineActivity(N)`: target nation 0 unless N is, else
    `GetRandomEnemy`; War tests 0xcc9fdf/0xcca022; neutral protests. `CheckAIvsAISubmarineSinking(A, B)` 0xd31e68 (from
    `CheckAIvsAIBattles` 0xd32aa1): one of A's submarines sunk by B (15 %).
  - Built: a mid-hook at 0xce21af runs, for each K under the player's rules with its own counter above 1 at war with
    computer nations, a frame in K's view (`game::HostViewEnter`, the people's nations masked): `CheckSubmarineActivity`
    of K and of each enemy, CheckRaiderActivity's two loops (markers reset; `CheckRaiderSinkings` per pair with the
    game's tests: Tension > 40, the defender not `Blockaded`), `CheckResultOfSinkings`. The four monthly loss fields of
    every nation read 0 for the frame and are added back; the trade VP of each call go by pair
    (`hookcb::CreditWarVP`, "trade war"; the frame's VP/EnemyVP fields zeroed); EnemyVP and K's VP field hold K's war
    score during the frame (the raiders' "losing badly" test), then the host's again; the area markers and the other
    nations' FuelShortage are put back (unless the frame's own sinkings cut a nation's fuel). TurnMessages and
    RaiderSummary are fresh TStringLists for the frame; its lines, the picture messages (`BeginTextCapture`) and the
    boxes (`ShowMessagePos` hooked, recording only while a frame runs) go to K's report (`econnews`) with a summary
    "Trade war this month: our raiders sank m ... (+v VP); enemy raiders sank ...". `CheckAIvsAISubmarineSinking`
    (hooked) answers False for K's computer pairs (their submarine war is the frame's; the month's count is logged).
    Bridge `warfx K` `sea=` and `searule=`, TEST `warfxsea R n`. Mine warfare stays the host's.
  - Gotchas: a blockaded nation's merchants stay at home (the game's raider rule): with K blockaded in M2 the enemy's
    raiders do nothing and J must not be blockaded for K's raids (`warfxblk J 0`). TStringList's items and count are
    `FList`/`FCount` by RTTI, not TList's +4/+8 (the first build read no turn message). The TEST raiders are put back
    only while still on raider status: dist_b54_5 put an interned one (B Brandenburg, "interned in a neutral port due
    to lack of fuel") back to Active, and the client's next two battles in The Baltic ("3 attacks 0", M2 and M3) failed
    in the game's `SelectForce` (box "Error in SelectForce procedure! Access violation at address 00FD7A62 ... Read of
    address 0000000D": 0xbe7a62 with that run's image at 0xc20000; a side with no force, the fallback reads a nil main
    division). Only that run had an internment; dist_b54_5 restored unconditionally (no `cmp byte ptr [ecx+esi],4` in
    its SeaFrame, 5cededf has it); not seen since. TEST only (the frame itself writes no Status or LocationArea); the
    mechanism inside SelectForce is not proven.
  - Live: dist_b54_5 run 2: "3: raiders 11/0, subs 0/0, VP +55/+0", credit (1, +55, 3). dist_b54_6 (5cededf)
    `--only war_effects` 86/86, 183 s: M2 "sea war of nation 1 (enemies 3): war counter 2, ... its view, blockaded (no
    enemy raiders); calls 4, faults 0; 3: raiders 7/0, subs 0/0, VP +35/+0; TEST: 6 raider(s) ...; messages 7";
    nation 1's report "German raider KE Kohlbrand sinks 1 Russian merchant ships in The Baltic", "German raider B
    Wittelsbach sinks 2 ...", "Trade war this month: ..."; client "economy: showing 10 message(s)"; no box on either
    copy; the last month at war "the computer's submarine sinkings dropped ...: 1 (3-1)". Group `relations` on
    dist_b54_6: 196/196, 373 s (session 17, relations_peace 33, war_effects 69, battle_generator 40, battle_surprise
    37): M2 "3: raiders 5/0, VP +25/+0", credit (1, +25, 3), client "showing 10 message(s)"; no SelectForce box in
    either run (no internment either).
- **(f) Money check in a war's first month** (B6's smoke of the merged build, dist_b63_2, `battles` B1: "nation 1's
  funds at the month's end 3728 == ComputeExpenses' 2346 + the player's money rules +0 (other +1382)", BM 18 -> 16, the
  balance -794 -> +3714). Not a B5 money effect: the game's arms-control conference during nation 1's war.
  - Cause. The host's conference event (DoStrategicTurn: ComputeExpenses 0xce1e0b, then CheckTurnEvents 0xce1e42 ->
    GetRandomEvent -> HandleEvent -> `TdlgEventAnswer.Execute` 0xc0ed1e/0xc0ed34 -> `CreateArmsControlTreaty`
    0xcb6bf0) runs under B2's mask (host log 10:05:15 "CreateArmsControlTreaty runs without the pairs of nation(s) 1"),
    so its first test `AnyNationIsAtAIWar` (0xcb6c26; `IsAtAIWar` 0xc68ecc: a tension above 40 between two of nations
    1..8) missed the war 1-7 and the treaty was made. Per nation (0xcb7962..): `ScrapShip(n, ship, False)` 0xca0690 for
    each ship under construction over `GetTreatyLimit` or the calibre limit (not Rebuild): Funds += the scrap value
    (0xca09c4; StartBuild 2: `GetScrapValue` 0xca0d8c = Round(Displacement / 25) x BuildProgress +0x9a / Cost +0x96),
    then BudgetModifier -2 (0xcb7aa2, helper 0xab7744 = dec with floor 0). Host 10:05:19 "Germany has scrapped the
    following ships under construction to comply with the treaty". The same treaty made nation 1's PC show "The outbreak
    of war cancels the disarmament treaty!" (`CancelTreaty` 0xd30b88 from `UpdateUIValues` 0xc80828 while
    TreatyTimeRemaining > 0 and its own War > 0): B6's stall at `battles` D1.
  - Fix (`game.cpp` MaskedCall): for `CreateArmsControlTreaty` the masked pairs at war (above 40 before the mask, its own
    or a caller's such as `CheckAllyTrouble`'s) show their own tension for the call (TreatyWarsEnter), put back to 5
    afterwards: a war in progress ends the conference with the game's message before it changes anything, as in single
    play. The conference's money for K is now in K's record (`economy.cpp` TreatyBefore/TreatyAfter around the call,
    after ComputeExpenses: `econ` treaty= funds, treatyBM=, conference=; otherFunds and the log line's "other changes"
    net of it), and the 0.5.0 money check's sum includes it (a treaty at peace is a rule every nation shares, the player
    included). TEST `econtreaty <chance> [blind]|cancel` (host: the conference at the month's end; blind = without the
    fix, a treaty cancelled at once). Suite `arms_treaty` (group relations; one scenario with the treaty fix's since the
  merge: [the disarmament treaty and a joined nation's war](#fix-2026-10-23-the-disarmament-treaty-and-a-joined-nations-war-no-box-every-month-the-host-ends-it-once-v060-dev)).
  - Live: dist_b54_7 `--only arms_treaty` 43/43, 91 s: M1 (war 1-3) "the arms-control conference
    (CreateArmsControlTreaty) sees the war(s) of the nations people play: 1-3 3-1", the host's "With a war already in
    progress, the conference ends in solemn declarations and no action.", treaty months 0 -> 0, `econ 1` conference=1
    treaty=0 treatyBM=0, money check PASS; M2 blind (B1 reproduced): "Germany has scrapped ... B Preussen, B Hessen, B
    Wettin", "nation 1: the arms-control conference changed its funds by +1363 and its BudgetModifier by -2", bm 18 ->
    16, balance -794 -> +3522; money check PASS "5426 == 4063 + 0 + the arms-control treaty's scraps +1363 (other +0)"
    (the check before had other +1363).
  - Group `relations` on dist_b54_8: run 1 53/57 (relations_peace M1 had the game's own conference at peace, "A naval
    arms limitation treaty will be in effect for 14 years!", money check PASS with treaty +0 BM -2; then war_effects
    M1's Turn was refused on the host: "We have more capital ship tonnage in service than permitted by treaty! You must
    scrap 16,800 tons ...", `host turn: click handler returned (endOfTurn=0 ...)` -> suite `treaty_guard`); run 2
    233/234 (arms_treaty M1 no treaty, M2 blind +546 BM -2 recorded; one false positive: war_effects M2 (c)'s box
    filter matched the host's research messages "Our scientists ... Early coastal submarine" -> skipped); run 3 119/123:
    war_effects M3 BM 6 instead of 5 (an extra +1, not investigated), arms_treaty M1 timed out in an undeclinable
    battle of nation 1 (Russia attacks, auto-run still running at 240 s). Open: a battle driver for arms_treaty's war
    months, the M3 BM.
- **(g) Group `relations` reliability** (suite only; dist_b54_8 = HEAD's source):
  - war_effects M3's BudgetModifier 6 (expected 5) is the game's, not B5's: the new month's intelligence result on nation
    1's PC (client "intel: monthly check for nation 1: ... tension 2:1;, own BudgetModifier:1;", its dialog "Event - Dec
    1900" answered with the first choice "Give it maximum publicity and denounce the aggressive policies of France"),
    applied by the host before the check (host "event of Captain (nation 1, #-1 'Intelligence'): tension 1-2 8->9
    BudgetModifier+1"): both copies 3 -> 4 (war budget) -> 5 (loan) -> 6. The check now wants 5 + the new month's own
    changes of nation 1 from the host log (`own_field_changes`: its events' `BudgetModifier+n` and its relation turns'
    `own BudgetModifier+n`, after the relation turn's host line).
  - arms_treaty M1's timeout: run_month's unplanned path forced the undeclinable battle onto auto-run, answered "No
    contact between fleets. Do you want to continue searching?" Yes (+180 minutes) and has no restart for a run stopped
    at a minute boundary; the client logged nothing for 216 s. Its war months and war_effects M1-M3 now run through
    `war_month` (a `BattleDriver` without a plan: declines, a forced fight on auto-run with its `pocstate` polling and
    `pocrun` restart, `FIGHT_TIMEOUT` more).
  - Run g1 (dist_b54_8, 447 s): 241/247; war_effects 69/69 (M3 BM (6, 6), own [event +1]), arms_treaty 26/27 (M1 battle
    declined at the preview, no treaty; M2 blind treaty recorded). New, not from these changes: the client's "Error in
    SaveBuildCampaign procedure! Access violation at address 00C25D4B ... Read of address 00000000" at arms_treaty M2's
    submit and "... 00C2751B ... Read of address 18740127" at battle_generator's start (client image at 0xC20000: static
    0x835d4b = the memory manager's FreeMem reading a block header, 0x83751b = System.Move with a bad source: a corrupted
    heap; first after war_effects M4's live battle and a declined battle preview; the known SaveBuildCampaign issue
    of 0.3.3/0.4.0 had another address), and battle_surprise b) "takeovers: none" (Japan had taken Southern Korea by the
    game's own event in war_effects M3; Northern Korea was taken in every earlier run).
  - Run g2 (dist_b54_8, 363 s): **222/222** (session 17, relations_peace 33, war_effects 69: M3 BM (5, 5), no own
    change; arms_treaty 26: M1 and M2 battles declined at the preview, after M4's live battle as in g1, no error box;
    battle_generator 40, battle_surprise 37: Northern Korea taken over). The g1 AV did not come back.

## Feature 2026-10-23: B6, the player's battle generator for a joined nation (0.6.0)
- **What.** A joined nation K under the player's rules gets the player's battles against computer nations, as nation 0
  in single player: the game's own generator `CheckForBattles2` (0xcecb10) runs for K once a month on the host, in K's
  view, and its battle goes to K's PC (the battle phase). Before, K's battles came from the computer's generator
  (`CheckAIvsAIBattles` -> `DoBattleBetweenAINations`), which (the user's report) gave the non-host no battles in some wars.
- **The game's generator (static, 1.01.44).** `DoTurn2` calls `CheckForBattles2` at 0xce180e (also legacy `DoTurn` and
  two test menus); `DoStrategicTurn` runs after it unless a battle came. It reads `War` (0xf52ff4 > 0: 0xccd5fc),
  `PlayerNation` (form +0x3204) and `GetBNation(i).Tension` (+0x256 > 40); CheckForInvasionBattles (0xd10228), 30 %
  CheckRaiderVsPatrols (0xcce6bc), then 50 % passes over the map areas and enemies (home area, GetStrengthInArea > 7),
  GetRandomEnemy (0xccd5cc: Random(8)+1 with Tension > 40, 201 tries, nil-safe) passes, the strongest-area helper
  0xceca88, raiders again, GetRandomHomeArea passes, each calling `CheckAttackLocation(attacker, defender, area)`
  (0xced17c, ret 4; area nil at 0xcecddf: it picks one itself); the first True ends the search (at most one battle a
  month), a False goes on. CheckAttackLocation -> 0xced0e8 / DoHomeAreaBattle 0xcee638 / DoPossessionBattle 0xced48c
  (AIDeclinesBattle 0xcd2188, the map question, DoPlayerDecline 0xd4927c) -> DoBattle 0xcb3b24.
- **Host (`game::RunBattleGenerator`, hook on CheckForBattles2).** Before the host's own call, for each K with the
  player's rules, a client announcing `gen=1` that fights its battles and K's war counter > 0: the B3 view on the host
  (nation list slots 0<->K, permuted relation and alliance cells), `PlayerNation := K's record`, `War := K's counter`,
  and the nations people play read Tension <= 40 (their battles stay the host's generator's and, between joined
  players, the computer's live battles). CheckAttackLocation does not run in the view: each call is recorded as an
  attempt (attacker, defender in real indices, area or "" = the game picks) and answers False like a call without a
  battle, so the attempts are the ones single player would make until one gives a battle (up to 4, then True).
  CheckForInvasionBattles and CheckRaiderVsPatrols answer False in the view (generated skip thunks on a view flag).
  Everything is put back right after. The attempts against computer nations become one request of kind 1.
- **Request kinds (`BattleReq.kind`):** 0 the computer's generator (as before), 1 the player's generator, 2 a surprise
  attack (reserved, later). Kind 1 goes only to clients with hello `gen>=1` (MSG_BATTLE `kind`, `att`, `att0..attN`
  = "a,b,area"); its fallbacks (no battle, failed, time limit, disconnect, the host's decision) never call
  `ResolveAIBattle`: no battle that month ("* No battle of Germany this month (...)"). With B6 on for K, the computer's
  generator's battles of K against computer nations are dropped in `HostDeferBattle` (log "left to the player's
  generator"; returns True to the game = no battle), so K has one generator.
- **K's PC.** The battle slot replays the attempts in order through the game's CheckAttackLocation (mission,
  possession and its questions re-rolled there as before; area "" -> nil); an attempt that gives a battle, a preview,
  a decline (the player's: a new `DoPlayerDecline` counter hook, or the enemy's points) is the month's, else the next
  ("battle N: attempt i (...) found no battle"). The result carries `cand` and the battle's area (`DoBattle`'s area,
  captured in its hook), so the host merges it for the right opponent.
- **Not through this path (later work):** invasion battles need K's own invasion state (form +0x4a8c..+0x4a98 are the
  host player's; K's invasion orders are not sent); raiders against patrols read the form's pending raider battle
  (+0x4a1b) and resolve through QuickResolveRaiderBattle (which runs DoStrategicTurn): a later attempt type replayed
  on K's PC. Battles against the host stay the host's generator's live battles (suite `lockstep_month` 49/49).
- **Live proof** (`files\b6\b6_live.py`, 1900 fixture, host and client on one PC, dist_b6_1, game open 179 s, 72/75):
  K=1 Germany at war with Spain (7). `battles gen 1` x40 (dry runs): 40/40 with 4 attempts, opponents only Spain, areas
  Northern Europe, The Baltic, The Mediterranean and "the game picks", attacks and defences; the host's relations and
  nation unchanged. Month 1: "the player's battle generator for nation 1 (war counter 1): 4 attempt(s): 7>1 Northern
  Europe; 7>1 (area by the game); ...", "battle request 1 (the player's generator)", the computer's battle of the pair
  "left to the player's generator"; on the client Spain declined battle -> "battle 1 applied: ... VP 160/0 ...; the
  enemy declined battle". Month 2: request 2 fought on the client, "battle 2 applied: 17 ships changed (100 fields),
  0 removed, VP 0/75". The 3 failures: an injected kind-0 battle whose preview allowed no Decline (fought), a time-limit
  case the AI avoided, and the client's saved [IntelReports] repeating one entry of its own monthly check (the game's
  CheckIntelligence; TESTING.md's per-month checks note it in memory), not B6. The proof build also held the next
  commit's risk-4 changes (declines merged, the result box captured); the generator's path is the same.
- **Suite** (dist_b6_2): `--only battle_generator,battles,lockstep_month,relations_peace` 218/226 in 368 s with
  `lockstep_month` 49/49 (battles against the host: the host's generator's live battles, unchanged); the failures were
  the runner's (the scenario ran after `battles`, which leaves the client with `fightbattles 0`: it now joins again with
  1) and `battles` c), which risk 4 changes (next commit); then `--only battles,battle_generator` 155/155 in 342 s
  (battle_generator a)-c): one request of kind 1 a month for three months, war counter 4..6, one computer battle of
  the pair suppressed each month).
- **Compatibility.** Protocol 3. A 0.5.0 client announces no `gen`: its nation keeps the computer's generator (kind 0,
  as before). A 0.5.0 host ignores `gen` and sends kind 0 only. Existing campaigns continue (no new save state).
- **Bridge:** `battles gen K [queue]` (a dry run of K's generator, or its request), `battles injectgen K J [area]`;
  `battles list` shows `kind=1:att=N`. **Suite:** scenario `battle_generator` (group `battlegen`, also in `relations`).
## Fix 2026-10-23: risk 4, a joined player's declined battle is merged and the game's result box stays off the host (0.6.0)
- **Before.** A battle a joined player declined on its PC came back as `declined` without the slot's result: the host
  had the game resolve it as a battle between computer nations (`ResolveAIBattle` -> `DoBattleBetweenAINations`
  0xd32104), whose result box ("In a battle in <area> between the A and B navies ..." / "An indecisive engagement ...",
  `ShowPicMessage(text, 'Firing.jpg', True)` 0xd326eb) opened on the host and held the new month until the host's player
  answered it; and the game's consequences of the decline on the player's PC (`DoPlayerDecline` 0xd4927c: the enemy's
  points EnemyVP +0x4a63, prestige and fleet morale -1 after repeated declines, the decline count +0x32ec, an invasion
  of the possession) were lost. The same box came for every battle the game resolved (no battle, failed, time limit,
  the player gone).
- **Now.** The battle slot counts the player's declines (`DoPlayerDecline` hook: the map question's No, and the
  preview's Decline, which returns from DoBattle without it) and sends `declined` WITH the slot's result. (Corrected
  2026-10-28, 0.7.1 docs: the preview's Decline runs `DoPlayerDecline` too; later live records show it raising
  `DeclineCount` and setting `InvadedThisTurn`, which only `DoPlayerDecline` does; GAME-INTERNALS is corrected.)
  (`ExtractBattleResult`: ships, losses, VP of both sides, prestige and morale): the host merges it like a battle
  (`battle N applied: ...; the player declined battle`, chat "* Captain declined battle for Germany against Spain in
  Northern Europe.", the war score credited), for both kinds of request; the game resolves nothing. A battle the game
  still resolves (kind 0: no battle, failed, time limit, disconnect, the host's decision, a 0.5.0 client's decline)
  has its picture messages captured instead of shown (`lockstep::BeginTextCapture/EndTextCapture`: the ShowPicMessage
  hook, installed alone if no live battle installed it yet) and they go to the chat and to the joined player's news of
  the new month.
- **Gaps:** the decline count (+0x32ec, a form field) is the battle slot's only (K has no persistent copy of it on the
  host); a possession the enemy invades after the decline (+0x75/+0x7d) is not in `ApplyBattleResult` (as for battles).
- **Live** (dist_b6_1, `files\b6\b6_live.py`): a request of the player's generator declined in the preview -> "battle 3
  applied: 0 ships changed (0 fields), 0 removed, VP 0/63, prestige 0 ...; the player declined battle" (Spain's points
  of the decline, no resolution by the game). **Suite** (dist_b6_2): `battles` c) (now: merged, both chat lines, never
  resolved by the game) and `battle_generator` d)-e) in 155/155; e) `battles aitest 1 7 Northern Europe` -> "ok
  fought=1 captured=1 | An indecisive engagement has taken place between the German and Spanish navies in Northern
  Europe.", host log "picture message captured: ...", no TdlgPictureMessage on the host.
- **Mixed versions** (protocol 3): a 0.5.0 host ignores the bundle of `declined` and lets the game resolve it, with its
  box, as before; a 0.5.0 client sends `declined` without it: a 0.6.0 host lets the game resolve it (kind 0, box
  captured). **Bridge:** `battles aitest K J area`; `battles list` shows `:declined`/`:avoided`.
## Investigation 2026-10-23: B6 decisions (attempts, decline counts, generator errors, wording) (v0.6.0-dev)
- **The decisions** (the caller's, after B6 and risk 4): (1) all of the generator's attempts, (2) the joined nation's
  own decline counts and the enemy's invasion after a decline, (3) no "Error in CheckForBattles" box on the host and no
  request that month, (4) the client's wording under an older host. (2)'s invasion is a NO-GO (below).
- **(1) Attempts.** `kMaxAttempts` 4 -> 64 (game.cpp): the CheckAttackLocation calls in K's view are recorded in the
  generator's order until it has none left (each answers False, so CheckForBattles2 walks its whole search); 64 only
  bounds a runaway. The client reads up to 64 (`att`) and replays them in order until a battle, a decline or the
  enemy's points. Measured (1900 fixture, Germany at war with Spain, war counter 3-4): 20 dry runs had 4-7 attempts
  ({4: 3, 5: 10, 6: 6, 7: 1}); the month requests 4-6 (5, 6, 4, 4, 4, 5). The first attempt usually decides: request
  13 "attempt 0 (7 attacks 1 in an area the game picks) found no battle", then the enemy declined at attempt 1.
- **(2) Decline counts.** `DeclineCount` (form +0x32ec) is saved and loaded ([General]; SaveBuildCampaign 0xc8eacb,
  LoadBuildCampaign 0xc99f35..0xc99f4a); `EnemyDeclineCount` (+0x32f0) is saved (0xc8eae4) but never loaded (its key
  has no other user): it lives in memory for the session. So a battle slot held the host player's DeclineCount and the
  client PC's last EnemyDeclineCount (live: "the save's were 0/1, 0/2, 0/3" = the previous slot's). Writers:
  DoPlayerDecline (DeclineCount +1 when the war's points are close or behind; -1/-10 after its warnings, with prestige
  or fleet morale -1), CheckSurpriseAttack := 0 (0xcfdb6a, 0xcfe020), AIDeclinesBattle 0xcd2eaa and 0xbe7111
  EnemyDeclineCount += 1, 0xbe9f3d := 0. Readers: DoPossessionBattle, DoHomeAreaBattle, ArrangePossessionBattleDetails,
  AdjustPlayerPenaltyPointsForArea, CheckSurpriseAttack, 0xcf8824 (EnemyDeclineCount in the invasion modifier). Mod:
  the host keeps each joined nation's (`g_kDecl`, session file `kdecl` "K:own:enemy,", dated like `kwar`) and sends
  `dc` "own:enemy" with MSG_BATTLE to clients announcing hello `gen>=1` (not live battles); the client sets them after
  loading the slot ("battle N: the nation's decline counts a/b in the battle slot (the save's were c/d)") and returns
  the slot's counts with every result (`dc`); the host logs "battle N: nation K's decline counts x/y (were u/v)" and
  stores them when changed or new. Live: three battles the AI declined 0/1 -> 0/2 -> 0/3; the preview's Decline reset
  EnemyDeclineCount to 0 and once raised DeclineCount ("battle 12: nation 1's decline counts 1/0 (were 0/3)").
- **(2) NO-GO: the enemy's invasion after a decline, applied on the host.** DoPlayerDecline 0xd4927c invades
  (Invaded := InvasionBattlePending, InvasionSupport := -1, InvadedThisTurn := 1, "<X> forces invade <p> without
  opposition ...") only when the possession is EnemyInvasionTarget (+0x4a94) and InvasionBattlePending (+0x4a98) > 0,
  i.e. in an enemy invasion battle (CheckForInvasionBattles), which K's view skips; else the player's own invasion
  (Invaded = 0) loses 2 InvasionSupport. Neither happens in K's battle slot today. The month's invasion resolution
  (unnamed 0xcf8824..0xcfa1d1, from DoStrategicTurn 0xce1f54) is the player's: all possessions, no owner test; at
  peace every Invaded := -1 (0xcf9900); at war Invaded > 0 = an enemy invades nation 0's possession (nation 0's
  strength against a random enemy, its prestige, TakenFrom := 0), Invaded = 0 = nation 0's invasion; AI invasions only
  start against nation 0's possessions (0xcf9226). K's Invaded/InvadedThisTurn set by name on the host would be
  cancelled at peace or resolved as the host's: K's invasions need their resolution in K's view (with the landings).
- **(3) The generator's error box.** CheckForBattles2's own except handler (0xced047) calls
  `ShowMessage('Error in CheckForBattles procedure!'#13 + E.Message)` (0xced05c -> 0xab7970) and returns, so the mod's
  `__try` never saw a game exception and the box would open on the host. game.cpp finds ShowMessage from that call
  site (`FindGeneratorErrorBox`: `mov edx, <literal>; call; mov eax, [ebp-0x14]; call`) and hooks it: in K's view the
  text is recorded, elsewhere it passes through. RunBattleGenerator then fails ("exception in the game's battle
  generator: <box text>"), the view is left as usual, no request that month. Startup: "Player's battle generator: its
  error box in a joined nation's view goes to the log". Test hook `battles gen K fault` (an access violation in the
  view's first CheckAttackLocation, inside the game's try block). Live: "error exception in the game's battle
  generator: Error in CheckForBattles procedure! Access violation at address ... in module 'RTW3MP.dll'. Write of
  address 00000000", the host log line, no request, no window or error box on the host; the next `battles gen 1` ok.
- **(4) Wording.** The host's welcome carries `gen=1` (it merges a declined battle's result); the client keeps it (0
  until a welcome has it). With it a decline goes with the slot's result ("* You declined the battle (VP ...)."),
  without it alone as with 0.5.0, and the chat says "* You declined the battle; the game resolves it.". Test: client
  `battles hostgen n`, `battles state` shows `hostgen=`. Live (hostgen 0): battle 12 declined in the preview ->
  client "battle 12: declined", host "battle 12: resolved by the game (Captain declined it)", after the battles
  "battle 12 resolved by the game: battle" with "picture message captured: '... the Spanish ships DD Furor have been
  sunk..'", no box on the host, both chat lines.
- **Mixed versions** (protocol 3, engine 17): 0.5.0 host + 0.6.0 client: no `gen` in the welcome -> the decline goes
  alone, "the game resolves it" (kind 0 only; the host shows its box as 0.5.0 does); no `dc` -> the slot keeps the
  host player's DeclineCount, as before. 0.6.0 host + 0.5.0 client: no hello `gen` -> no `dc`, kind 0 only, a decline
  without its result resolved by the game (box captured), as before. 0.6.0 + 0.6.0: all four.
- **Suite** (`--only battles,battle_generator`): run 1 on dist_b6_5's first build 165/166 in 312 s (game open 310 s):
  `battles` 86/86 (c) decision 2, new c2) decision 4, one more month), `battle_generator` 62/63 (a) the hook line, b)
  attempts and the fault, c) the replay order); the failure was the client's [IntelReports] repeating one entry in
  M3 (the game's CheckIntelligence, known, not B6). Run 2 (after a rebuild) 154/154 in 356 s; the
  committed build (dist_b6_5: RTW3MP.dll 5484D8B1, version.dll 72C7FFC4; after a fix: RunBattleGenerator's early
  return also clears the test fault flag) 178/178 in 361 s (game open 358 s): `battles` 97/97, `battle_generator`
  64/64; dry runs {4: 5, 5: 10, 6: 3, 7: 2}, the month requests 4-6 attempts, requests 9 and 19 "attempt 0 ... found
  no battle" then the battle (in order); c2) battle 15 under hostgen 0: "battle 15: resolved by the game (Captain
  declined it)" -> "battle 15 resolved by the game: battle", captured, none shown. Host and client on one PC; the
  released 0.5.0 DLLs reinstalled after each run (hashes checked), slots and Preferences restored.

## Investigation 2026-10-23: surprise attacks and takeovers at war start for joined nations (v0.6.0-dev)
- **The question** (B6 piece 2): a joined nation K's surprise moves at a war's start, which the player's routines make
  in single player and B3 left out in K's view (`GetRandomPossessionInArea`, `GetRandomPossession`, `DoSurpriseAttack`,
  `CheckForPlayerSurpriseInvasion` answered nil/False there). Answer: the surprise attacks (both directions) and the
  takeover of an unowned possession: GO (live, both fought); the surprise landing in K's own possession: NO-GO for now
  (it needs K's invasions resolved in K's view, below).
- **CheckForWar's landing block** (1.01.44, static, then live): for B_i with Tension < 40 while War <= 0, with the
  SurpriseAttack trait (+0x1f3) or GovernmentType 4 (5: Random(100) < 30) and (`WillGoToWar` = 1 or Tension >= 10 and
  Random(300) < Tension): Tension := 14 (0xc85014); Random(100) < 50, or with the trait Random(100) < 90, else nothing
  more; `P.GetRandomPossessionInArea(B_i.BuildArea)` (call 0xc85056); worth < 200 (+0x58) and B_i stronger there than
  half of nation 0 -> the landing (Invaded := i, InvadedThisTurn := 1); otherwise up to three `GetRandomPossession(nil,
  @B_i.BuildArea)` (0xc850f7/0xc85115/0xc85133) until Owner (+0x61) nil -> "In a surprise move, <adjective> troops have
  taken control of <p>!" and `ChangePossessionOwner(p, B_i, False)` (0xc85196). 0xab7744 is a floored Dec (WillGoToWar
  1 -> 0); Tension 14 then declares the war (CountNationsAtWarWithPlayer + 13 <= 14) unless War < -48 and Random(180) <
  14 (rumours). The war-start block (only while War <= 0) keeps [ebp-0x24] := IndexOf(B_i) with B_i's trait, else 0 with
  nation 0's; `DoSurpriseAttack` at the end (0xc85a64) when > -1. `CheckForPlayerSurpriseInvasion` (only call 0xc85529,
  after WarStartsWithNation set War := 1) is unreachable.
- **DoSurpriseAttack / CheckSurpriseAttack** (static, then live): DoSurpriseAttack 0xd29e84 (EDX never read): nation 0's
  trait -> CheckSurpriseAttack(nation 0, ECX or GetRandomEnemy), False -> "No opportunity found for surprise attack!";
  else every nation at war with nation 0 with the trait -> CheckSurpriseAttack(it, nation 0) until True.
  CheckSurpriseAttack 0xcfd82c (EAX form, EDX attacker, ECX defender; Boolean; own except box "Error in
  CheckSurpriseAttack procedure!"): `LoadSurpriseWarInfo` reads Data\SurpriseWarInfo.dat keys `Nation1Mission0..10`
  only (the Far East: Kiautschou Bay, Shanghai, Weihaiwei, Liaotung Peninsula, Southern Korea, The Philippines, Formosa,
  Cam Ranh Bay, Kwang-Chou-Wan, Hong Kong, Northern Korea; the file's Nation2-8 sections are empty); year < 1896 False;
  Random(100) < 20 False; war history 0xd0decc(idx, 1901|1935) and Random(100) < 60 False; 1896-1924 the first mission
  the defender owns (none: False), an AI attacker's `AIDeclinesBattle(..., Implement True, ...)` (declines: False and
  the player's points); 1925-1934 `DoSubSurprise` (a submarine attack, no battle) and False; from 1935 the carriers'
  pick 0xbf5798. Then `MapMessageDialog(text, point, NoChoice = an AI attack)` (Decline -> True without a battle), the
  flag [0xf52ffc] := 1 ([General] BattleIsSurpriseAttack), DeclineCount := 0, "Mobilize all ships for the surprise
  attack?" (nation 0 attacking), the mobilizations, nation 0's destroyers (cruisers) moved to the area, `DoBattle`
  called directly. It reads no War, Tension or trait: it runs standalone for any pair with nation 0 on one side (the
  game's debug menu `Testsurpriseattack1Click` 0xd5cdf4 calls it so) but re-rolls its gates. Fixture 1900: Japan alone
  has the trait; Germany owns Kiautschou Bay, Japan Formosa; Northern/Southern Korea and Shanghai are unowned.
- **The mod, K's PC** (game.cpp "Surprise attacks and takeovers"; on when the host's welcome has `gen=2` and the hooks
  resolved, `SurpriseAvailable`; else B3's skips stay). At start-up the call sites inside CheckForWar are found (E8 to
  the target in its first 0x1000 bytes: landing pick 1, takeover picks 3, owner change 1; else unavailable: "Surprise
  attacks and takeovers at a war's start for a joined nation: ok (CheckForWar's call sites 1/3/1)"). While the
  relation routines run in K's view (`PvRunGuarded(..., surprise)`): `DoSurpriseAttack` runs (its thunk's flag
  `g_pvSkipSA`); `CheckSurpriseAttack` is hooked: in the view it records (real attacker, real defender) and answers
  True for K (no "No opportunity" box) or False (the next nation is asked), elsewhere it is the game's;
  `GetRandomPossessionInArea` at CheckForWar's call site runs and answers nil (no landing), a possession worth < 200
  blocks this nation's takeover picks (single player would have landed; the strength test is not evaluated: a guess
  that slightly under-counts takeovers); `GetRandomPossession` at the takeover sites runs; `ChangePossessionOwner` from
  the takeover site is recorded (the game moves it in K's campaign). Other call sites in the view keep B3's nil. Pairs
  with a people's nation are dropped. `PlayerTurnResult.surprise/.takeovers` -> relation-turn keys `ksa` ("a,b;") and
  `sland` (pland's layout, from -1).
- **The host**: `HostApplySurprise` (relation turns of clients with hello `gen>=2`): `sland` -> `MovePossession(name, -1,
  to)` (still unowned, a computer nation taking it; else logged and skipped), chat "In a surprise move, X has taken
  control of P!"; `ksa` -> one `BattleReq` of kind 2 (pairs of K with computer nations, <= 16) for the battle phase after
  the month, chat "Surprise attack? A may strike B at the war's start. <player> plays it after the month."; not without
  the player's battles (B6On), before 1896 or in 1925-1934 (logged). Results merge like kind 1 (`cand`); a kind-2
  `nobattle` carries its reason, chat "No surprise attack of A on B (<why>)."; nothing is resolved by the game.
- **K's battle slot**: kind 2 checks the slot's year again, skips pairs no longer at war ("the war is over"), then
  `game::StartSurpriseAttack(a, b)` = the game's CheckSurpriseAttack with its dialogs, mobilization and battle, per pair
  until a battle, a decline, the enemy's points or True without a battle ("You let the chance of a surprise attack
  go."); none: "No opportunity found for a surprise attack." (K attacking) / "No surprise attack came about.".
- **Compatibility** (protocol 3): host welcome `gen=2`, client hello `gen=2`. 0.6.0 host + 0.6.0 client: everything.
  0.6.0 host + 0.5.0 or f3547a9 client (no hello gen / gen 1): no `ksa`/`sland` (the client's view skips), no kind 2,
  kind 1 as before. 0.5.0 or f3547a9 host (no welcome gen / gen 1) + 0.6.0 client: the client keeps B3's skips (nothing
  recorded or sent), declines as before. Live-battle engine unchanged.
- **Tests**: host `battles injectsa A B`; client `battles saretry n` (TEST: a False of CheckSurpriseAttack with nothing
  else happening is asked again, passing its 20 %/60 % gates), `battles testsa J [k]` (TEST: the next relation turn has J
  plan its war on K with the trait; `k` K's trait too; the takeover picks the area's first unowned possession),
  `battles sadry J [k]` (TEST: DoSurpriseAttack alone in K's view, pairs returned, nothing sent). Scenario
  `battle_surprise` (groups `battlegen`, `relations`).
- **Live** (1900 fixture, nation 1 Germany, J Japan): dist_b6_6 run 4 (`--only battle_surprise`, 54/54, 59 s): the
  relation turn "test: the takeover picks Northern Korea", "nation 5 takes over Northern Korea (owner -1; recorded for
  the host)", "surprise attack: nation 5 may strike nation 1 at the war's start", K's boxes "In a surprise move,
  Japanese troops have taken control of Northern Korea!" and "Germany and Japan are now at a state of war!"; host
  "relations of Captain (...): ... war 1-5 ... takeover Northern Korea -1->5 surprise 5>1", "battle request 1 (surprise
  attack): nation 1 vs 5, 1 attempt(s): 5>1"; Japan owns it on both copies; `sadry 5 k` -> pairs 1>5 (no box), `sadry
  5` -> 5>1. Battle 1 (5>1): "StartSurpriseAttack 5 attacks 0 -> False", asked again, "The Japanese navy declines battle!
  You gain 200 VP!" -> avoided, merged (VP 200/0); battle 2 (injected 1>5): map message, "Mobilize all ships for the
  surprise attack?" Yes, preview Decline -> declined, merged (17 ships changed: the mobilization; VP 0/176). dist_b6_6
  run 5 (with battles and battle_generator): both FOUGHT - battle 20 (Japan strikes Germany from the relation turn):
  "German marginal victory Own VP: 175 Enemy VP: 33"; battle 21 (Germany strikes Japan): mobilized, "6 destroyers
  unable to take part in battle due to lack of fuel ...", the preview's Decline disabled, "German major victory Own VP:
  1,072 Enemy VP: 160"; both merged. No dialog of them on the host. dist_b6_7 run 6 (`--only
  battles,battle_generator,battle_surprise`, 179/179 in 423 s): battle 18 (5>1) avoided ("The Japanese navy declines
  battle! You gain 200 VP!", merged VP 200/0, EnemyDeclineCount 1 -> 2); battle 19 (1>5) "StartSurpriseAttack 0
  attacks 5 -> True", "battle 19: fighting nation 1 vs 5 in Southeast Asia (slot 87)", "Enemy marginal victory Own VP:
  110 Enemy VP: 147", host "battle 19 applied: 21 ships changed (81 fields), 0 removed, VP 110/147"; the decline
  counts 0/2 -> 0/0.
- **Approximations and risks**: the landing's strength test (above); the surprise attack is played in the battle phase
  after the month (single player: at once, before the month's battles), so a war ended meanwhile drops it; the slot
  re-rolls CheckSurpriseAttack's gates (one roll, as single player; `saretry` is a test); the pre-battle effects
  (mobilization, ships moved, DeclineCount := 0) reach the host only with a fought, declined or avoided result (the
  ships' changed fields; not the mobilization's cost), not with a no-battle; [General] BattleIsSurpriseAttack stays in
  the slot; the request is in memory (a host restart in the month loses it; the war and the takeover are saved); the
  1925-1934 submarine attack is skipped; the 1935+ carrier path is not tested live (the fixture is 1900); a takeover the
  host refuses stays on K's PC until the next month's state.
- **NO-GO for now: the landing in K's own possession** and the rest of K's invasions. The month's invasion resolution
  (0xcf8824..0xcfa1d1) is nation 0's (GAME-INTERNALS); K's needs: (1) that routine run for K in K's view (on K's PC at
  its month start, or a host-side view with text capture), every other possession masked, the host's own run kept
  off K's possessions, the results (owner, TakenFrom, InvasionSupport, Invaded, prestige, VP) recorded and applied by
  name; (2) K's invasion battles (CheckForInvasionBattles' form fields +0x4a8c..+0x4a98 per K, a request kind for
  DoPossessionBattle, HandleInvasionResults in the slot, possession fields in the battle result); (3) then the landing
  (CheckForWar's Invaded := i on K's possession) and (4) DoPlayerDecline's invasion branch. Estimate ~4 working days
  (1.5 for (1), 2 for (2), 0.5 for (3)+(4) with tests); the main risks are 0xcf8824's size (unnamed, ~6.6 KB) and
  masking K's possessions out of the host's month.

## Fix 2026-10-23: B6 round 3: the month's own state in a joined player's battle slot, raider battles assessed (v0.6.0-dev)
Third B6 round on the merged 0.6.0 development line (`int-060`: B5 war effects + B6 generator, surprise attacks).
Engine level 17, protocol 3 (no protocol change).
- **The question.** A joined player's game never processes a month (the host's does), and its battle slot is the
  host's month-end save. What does the battle path (CheckAttackLocation, CheckSurpriseAttack, DoBattle, the battle
  screen, its close and aftermath) read that only the month writes, or that the save carries as the host player's?
  0.4.1 fixed the ship-type flags for live battles only (B takes A's).
- **Static audit** (offline scripts, not in the repo): every function reachable by direct calls from `DoTurn`,
  `DoTurn2` and `DoStrategicTurn` (stopping at the battle entries) against every function reachable from the battle
  path (the five entries above plus all methods of `TfrmMain`, `TFightingShip`, `TAirFormation`, `TDivision` and the
  battle dialogs; stopping at the month), minus what `LoadBuildCampaign` writes; unit variables (direct and through
  import cells, read/write from the operand access) and `TfrmBuildCamp` fields (approximated as `[reg+disp]`, disp >=
  0x9dc). Limits: virtual calls and event handlers are not followed (hence all methods of the battle classes as roots);
  the field pass has false positives from other large classes (`TBuilderNation` is 48 KB), triaged by hand.
  - *Real, now set in the battle slot:* the ship-type flags `[0xf54935..37]` (BB, BC, carrier exist; only
    `ReduceTimeLimits` -> `CheckShipTypesExist` 0xd4e314 writes them; `TFightingShip.SetMistakenType` and
    `IdentifyType` read them: a joined player's game had 0 for all three, so its battles mistook ships and drew Random
    differently from single player). The month's resets: `[0xf52ffc]` = `[General] BattleIsSurpriseAttack` (DoTurn2
    clears it, `CheckSurpriseAttack` sets it at 0xcfdb61/0xcfe017, saved and loaded: the host's own surprise attack of
    the month came into the player's battle slot, and the setup's objectives 0xbe2313, the minute-10 step
    `tmrMoveIntervalTimer` 0xdafcd8 and `FormClose`'s `AdvanceDesignStudies` 0xdcda03 treated the player's battles as
    surprise attacks); `[0xf4c5e9]` (the possession battle's flag: DoTurn2, `DoPossessionBattle` and
    `DoHomeAreaBattle` clear it, `ArrangePossessionBattleDetails` sets it, `CheckSurpriseAttack`'s `DoBattle` reads it
    as left); `HeroicVictory` (+0x3301), `tmpBattlePrestigeLoss` (+0x4ac6) and `ImportantConvoy` (+0x4ad2)
    (`DoStrategicTurn` resets them, the battle's end sets them at 0xdc520c/0xdc5bca, `DoBattlePrestigeLoss` reads them:
    the convoy's unrest message, the glorious-victory and failure prestige; only `HeroicVictory` resets itself there).
  - *Harmless:* `[0xfb591c]` (`TAirUnit.CheckAircraftType`'s once-a-month "We have no aircraft with ... capability!"
    flag: once per game session on a joined PC), the form pointers `[0xf53000]`/`[0xf53274]`, per-battle setup
    variables (the objective text `[0xf4c5f0]`, `[0xfb5920]`), saved and loaded options and research records, and
    `SurpriseAttackPossession` (`CheckSurpriseAttack` clears it first).
  - *Not changed:* the host player's invasion state in the slot (`InvasionTarget` +0x4a8c, `EnemyInvasionTarget`
    +0x4a94, `InvasionBattlePending` +0x4a98, loaded from the save; `InvasionCount` +0x4a90 is not loaded): it matters
    only for a battle at the host's invasion target, and clearing it while this PC's own `InvasionCount` is set could
    meet a nil target. Part of the joined nations' invasions, which stay for a release after 0.6.0 (below).
- **Fix** (`game::RefreshShipTypeFlags`, `game::PrepareBattleSlotMonth`): the client runs the game's
  `CheckShipTypesExist` (it only counts every nation's ships in play, order-independent, so the nation swap of the slot
  does not matter) after the month's load (log `month: ship-type flags A -> B (the game's CheckShipTypesExist on the
  received game)`), after the battle slot's load and when it loads its slot again; the battle slot also gets the
  month's resets (log `battle N: the month's state in the battle slot: ship-type flags A -> B, surprise attack s -> 0,
  possession battle p -> 0, HeroicVictory h tmpBattlePrestigeLoss l ImportantConvoy c -> 0`). The two unit variables
  come from `DoTurn2`'s own code (`mov byte ptr [var], 0` before `SurpriseAttackPossession := nil`; `PendingRaiderBattle
  := 0; mov eax, [cell]; mov byte ptr [eax], 0`), the fields by name. Computed rather than sent: it works under a 0.5.0
  host too; the host's flags come from its month's `ReduceTimeLimits`, the client's from the month-end fleet (they differ
  only if the world's first or last ship of a type is completed or sunk later in that `DoStrategicTurn`). Live battles
  keep A's flags (0.4.1); a joined player as A now sends the month's flags instead of 0.
- **Test hooks:** client `battles monthstate [set]` (read; `set`, TEST ONLY: the five month fields to 1, as an earlier
  battle leaves them); `ls shiptypes <hex>` (0.4.1) sets wrong flags first. Suite: `battle_generator` d)
  (`month_state_checks`).
- **Live (dist_b63_2, 1900 fixture, 2026-10-23).** `--only battle_generator,battle_surprise,lockstep_month` 147/148 in
  307 s (session 17/17, battle_surprise 37/37, lockstep_month 49/49, battle_generator 44/45: the one FAIL was the
  check's own order, fixed after it: the battle phase comes before the new month reaches the client). After `ls
  shiptypes 010101` and `battles monthstate set` on the client: `battle 2: the month's state in the battle slot:
  ship-type flags 010101 -> 000000, surprise attack 0 -> 0, possession battle 1 -> 0, HeroicVictory 1
  tmpBattlePrestigeLoss 1 ImportantConvoy 1 -> 0`, battle 3 `000000 -> 000000`, the host's `ls shiptypes` 000000 (in
  1900 no dreadnought, battlecruiser or carrier is in play; a 1935 campaign has 1s), then `month: ship-type flags 000000
  -> 000000`. The merge's smoke run (`--only battles,battle_generator,battle_surprise`, same build): `battles` 56/58
  before it stopped, every battle slot with its month-state line; its two FAILs are outside B6: the 0.5.0 money check
  of B1 (nation 1's funds +1382 "other" in its war-start month: B5's war loan, which the check does not count yet),
  and D1's month load held by the game's box "The outbreak of war cancels the disarmament treaty!"
  (`TfrmBuildCamp.CancelTreaty` 0xd30b88 from `UpdateUIValues` 0xc80828 in the client's load, with the nation's own war
  counter 2 while the host's game had made an arms control treaty, "CreateArmsControlTreaty runs without the pairs of
  nation(s) 1"): a box on the joined player's screen each such month, and the runner's month timed out.
- **Raider vs patrol battles for a joined nation: NO-GO for 0.6.0 (assessed, not built).** `CheckRaiderVsPatrols`
  0xcce6bc (CheckForBattles2, Random(100) < 30): when at war, `CheckRaiderInterceptions(P, B_i)` for each enemy and
  `(B_i, P)` for each enemy raiding P (B_i.Tension > 40, not blockaded), then `CheckPendingRaiderBattle` 0xcd1910 (its
  error text). `CheckRaiderInterceptions` 0xccee94 rolls per operational raider and patrolling ship in the same area,
  records one interception in `PendingRaiderBattle` (+0x4a1b flag, +0x4a1c/+0x4a20 the two `TCampaignShip`s, +0x4a24
  the enemy nation, +0x4a28 the area, +0x4a2d the caption "X ship intercepts Y raider in A.", +0x4a31 the side) and
  writes "Patrolling ship/aircraft X thwarts raider Y attack on merchant shipping in A!" lines. `CheckPendingRaiderBattle`
  asks the player (`TdlgMapMessage.RaiderBattleDialog`): `QuickResolveRaiderBattle` 0xd0b534 (it calls
  `DoStrategicTurn`, which the battle slot mode skips) or a tactical battle that does not pass `DoBattle` (autosave,
  `BattleArea`, `CreateForm(TfrmMain)`, the setup 0xbdbf1c: "Sink or damage the enemy raider." / "Escape with minimum
  damage to be able to continue raiding."). It fits the attempt path as an attempt type: the host's view records
  "raider check" where the generator called it (and answers False, as now); K's battle slot runs the game's
  `CheckRaiderVsPatrols` there (K is nation 0 = P, with K's war counter), and a True ends the replay like a battle. Still
  needed: hello/welcome `gen=3` (older clients must not get the entry), the client's detection of the raider battle
  screen and of a quick resolution (neither passes `DoBattle`, which `ClientStartBattle` counts), the outcome and merge
  of a quick resolution, the thwart lines into the player's news (the ship changes come back with the result's ship
  sections), the order (single player rolls them before the month's raider sinkings, the slot after the host's), and a
  suite scenario that forces an interception (raider and patrol orders in one area, the random gates). Estimate 1.5-2
  working days with the live proof; later.
- **Not in 0.6.0: a joined nation's invasions** (decision 2026-10-23): its own invasion resolution and invasion
  battles, the landings in its own possessions and the enemy's invasion after a decline. The month's invasion
  resolution (0xcf8824) is the host player's; K's needs it run in K's view, with invasion battles as a request kind and
  `HandleInvasionResults` in the slot (estimate ~4 working days; see the surprise attacks entry above).

## Fix 2026-10-23: the disarmament treaty and a joined nation's war: no box every month, the host ends it once (v0.6.0-dev)
Research agent, branch `fix-treaty` from `b6-r3` c46a9b7 (the merged 0.6.0 development line plus B6 round 3). Engine
level 17 and protocol 3 unchanged: one new state header key (`armstreaty`, ignored by older clients), live battles
untouched.
- **The report.** B6's smoke test of the merged build (suite b63-1, `battles` D1, 2026-10-23 10:05-10:22): the joined
  player's month load stopped on the game's box "The outbreak of war cancels the disarmament treaty!" (a
  `TMessageForm`): `TfrmBuildCamp.CancelTreaty` 0xd30b88 called by `UpdateUIValues` 0xc80828 (`SetPlayerNation`'s
  refresh) with nation 1's own war counter 2, after the host's game had made a treaty in that month (host log
  "relations v2: CreateArmsControlTreaty runs without the pairs of nation(s) 1"; the host's own counter -6..-70).
- **The game (static, 1.01.44; the facts in [GAME-INTERNALS](docs/GAME-INTERNALS.md#the-disarmament-treaty-and-the-submarine-ban)).**
  - The treaty is campaign-wide: `TfrmBuildCamp` fields in `[General]` (TreatyTimeRemaining +0x4a5b, the displacement
    and calibre limits +0x4a53/+0x4a57, TreatyTonnageBasePercentage +0x4a5f), each nation's TreatyTonnageLimit +0x20e,
    and the submarine ban SubBanTime +0xa63. `GetTreatyLimit` 0xd023fc, `GetCalibreLimit` 0xd254a0,
    `CheckAllowableTreatyBuild` 0xc76e58 and `CheckTreatyTonnageLimit` 0xc76800 apply them to every nation, with no
    exemption for a nation at war; `ReduceTimeLimits` counts down and cuts the player's funding under a treaty.
  - `CancelTreaty`: `cmp [ebx+0x4a5b],0; jle` -> the box only while months remain (`ShowMessagePos` 0xa07124), then
    months 0, both limits 0x7fffffff, the treaty label "-", SubBanTime 0. Callers: `UpdateUIValues` (0xc807ba: `call
    0xccd5fc` = War > 0 -> "At war for N months.", 0xc80828 `call CancelTreaty`, then DisplacementLimit +0x17d and
    BuildConstraint +0x2ae of nations 0..8 zeroed) and `CheckAITensionEvents` (0xd2e691, right after "War has broken out
    between X and Y!"). So in single player any war's outbreak ends the treaty for everyone: the player's at the next
    screen refresh, with the box once (later refreshes find no months left), two computer nations' at once.
  - `CreateArmsControlTreaty(aChance)` 0xcb6bf0 starts with `AnyNationIsAtAIWar` 0xd30b58 (`IsAtAIWar` 0xc68ecc:
    AITension[1..8] > 40, false for PlayerNation): a war between two of nations 1..8 ends the conference with "With a war
    already in progress, the conference ends in solemn declarations and no action."
- **The cause** (two 0.5.0 pieces, no 0.6.0 change in the path):
  - *B1:* `LoadCampaign` puts the joined nation's own war counter in `War` and then calls `SetPlayerNation` ->
    `RefreshUI` -> `UpdateUIValues`; at war that is `CancelTreaty` on the host's copy: the box while the host's treaty
    has months, and the treaty ended on that PC only (a client never sends `[General]` back). The next month brings the
    host's treaty again: the box at every month load while the war lasts (and in the battle slot, which gets the
    nation's counter too), and the joined player's copy without the treaty's limits the host still applies.
  - *B2:* the relations mask makes the joined nation's pairs neutral (tension 5) while `CreateArmsControlTreaty` runs,
    so its first test missed the nation's war with a computer nation and the conference went ahead during the war
    (b63-1).
- **0.5.0 has it** (static; not run live on 0.5.0): `git show v0.5.0:src/mod/game.cpp` has the same counter swap before
  `SetPlayerNation` (lines 3101-3103, "war counter: %d (the host's nation) -> %d (nation %d's own)") and the mask entry
  for `CreateArmsControlTreaty` (line 4023); the client takes `kwar` (v0.5.0 session.cpp 3010). Any 0.5.0 session in
  which a treaty is in force while a joined nation under the player's rules is at war shows the box every month. Before
  0.5.0 the joined PC used the host's counter: the box came at most once, with the host's own war, which the host's
  game ended too.
- **Design: the joined nation's war counts as the player's** (variant A). In single player no nation at war is bound by
  the treaty: every war's outbreak ends it for everyone, once. So the host ends the treaty and the submarine ban once
  (the game's `CancelTreaty`, months set to 0 first so that its box stays off the host's screen) when it is at peace
  itself and a joined nation's own war counter is above 0; the nation's player gets the game's sentence in the turn
  report (`econnews`, as the box's place), everyone a chat line. The joined PC ends its copy the same way (without the
  box) at its first screen refresh at war, but only when the month's header says the host applies the rule
  (`armstreaty=1`): a war that its relation turn starts at the beginning of a month frees the nation at once, as the
  player's war does at the refresh after the end of turn in single player, and the host ends it right after
  (`KWarFollow` of the relation turn), so both copies agree. With an older host the joined PC keeps the host's treaty
  quietly. Rejected: variant B (the treaty stays and the joined PC keeps it quietly), which would bind a nation at war,
  and the host's player and the computer's nations with it, under a treaty, which the game never does. The host's own
  wars stay the game's (its box at the screen's refresh). The conference sees the joined nations' wars (variant A's
  other half: a war in progress, no treaty).
- **Changes.**
  - `session.cpp`: `HostArmsTreatyCheck(why)`, called at the end of `KWarFollow` (the relation turn's, the peace's and
    the month's step) and at the start of `BroadcastState` (also a resent month, a campaign hosted again): host at peace
    (`game::WarCounter() <= 0`), a joined nation with `g_kWar` > 0, the treaty or the ban in force ->
    `game::EndArmsTreatyForWar()`, log `arms treaty: the war of nation(s) 1 ends the disarmament treaty (34 month(s)
    left, 15000 t, 12 in; submarine ban 0) (the month)`; while months remained, `g_kTreatyNews[K]` -> `econnews` (log
    `arms treaty: the turn report line of nation 1 goes with state seq N`) and chat `* The outbreak of war cancels the
    disarmament treaty! (Germany at war)`. `hookcb::JoinedPlayerPC()`. `SendStateTo`: header `armstreaty=1`; the
    client: `game::SetHostEndsArmsTreaty` (cleared by `Leave`).
  - `game.cpp`: a MinHook hook on `CancelTreaty` (`d_CancelTreaty`/`CB_KeepArmsTreaty`): on a joined player's PC with
    the host's rule the game's end without the box (log `arms treaty: ended on this PC for its nation's war, as the host
    ends it (35 month(s) left, submarine ban 0; war counter 1)`), without it skipped (log `arms treaty: CancelTreaty left
    to the host (a joined player's PC, an older host; ...)` when the state changes), and skipped on the host while a
    view puts another war counter in place (`EventActive`). `MaskedCall`: for `CreateArmsControlTreaty`,
    `AIMaskShowWars` (replaced at the merge with B5 round 4 by its `TreatyWarsEnter`, below) puts the real tension
    back into the masked pairs at war while it runs and `AIMaskHideWars` masks
    them again (log `relations v2: CreateArmsControlTreaty sees the war(s) 1-3 of the nations under the player's rules
    (a war in progress)`). Install log `Disarmament treaty with joined nations' wars: ok (cancel=... conference=...
    masked=1)`.
  - Bridge `armstreaty` (read: months, limits, ban, war counter, host, kept, endedhere, ends, last) and TEST `armstreaty
    set <months> [<disp> <cal>]` (every nation's TreatyTonnageLimit MaxInt: a first try with the fixture's 0 made the
    host's pre-turn check refuse the Turn, "Due to treaty build restrictions, we can have a maximum of 21,000 tons of
    warships building"), `armstreaty war <n>`, `armstreaty host <0|1>`, async `armstreaty refresh` and `armstreaty
    conference <chance>`.
  - Suite: scenario `arms_treaty` (groups default and `relations`, after `relations_peace`; docs/TESTING.md).
- **Live proof** (1900 fixture, nation 1 Germany vs Russia; dist_ft_2: RTW3MP 60CA9521CEB1DED1 before the joined PC's own
  end, dist_ft_3: 78674E49E28D922D, the committed code):
  - dist_ft_3 `--only relations_peace,arms_treaty` (11:33-11:36): 110/110 (session 17, relations_peace 33, arms_treaty
    with its month checks). The client's lines: `CancelTreaty left to the host (a joined player's PC, an older host;
    treaty 35 month(s) ...; war counter 1)` (TEST host 0), `ended on this PC for its nation's war, as the host ends it
    (35 month(s) left ...)` (host 1), and in M2's battle slot (B6's request, loaded before the host's month-end step)
    `ended on this PC ... (34 month(s) left ...; war counter 1)` without a box; the host's `the war of nation(s) 1 ends
    the disarmament treaty (34 month(s) left ...) (the month)` and `CreateArmsControlTreaty sees the war(s) 1-3`.
  - dist_ft_2 `--only arms_treaty` (11:09-11:11): every behaviour as designed; 2 FAILs of a too wide box filter (it
    counted the turn report's line as a box), fixed. `--only relations_peace,arms_treaty` (11:21-11:24): session 17/17,
    relations_peace 33/33, arms_treaty 58/59 (the one FAIL is a month check, below).
  - The checks (dist_ft_2 runs; dist_ft_3 the same with c) split as above): a) the host's own war under a treaty
    (`armstreaty set 24`, `war 1`, `refresh`): the game's box on the host, the treaty ended there, nothing left to
    anyone (unchanged). b) a 36-month treaty reaches the client at peace (both 35, 15000 t, 12 in). c) war 1-3: on
    dist_ft_2 the client's refresh at war kept it (35 months, no box); M2: one host end (34 months left), both copies 0
    months and no limits, no `TMessageForm` box on either copy, chat line, the client's turn report "Gain 130 VP for
    blockade of enemy | The outbreak of war cancels the disarmament treaty!". d) the conference at war: "sees the
    war(s) 1-3", the picture message "With a war already in progress, the conference ends in solemn declarations and no
    action.", no treaty. e) M3, M4: the client's loads at war counter 3 and 4, no box, no second end. Peace at the end.
  - dist_ft_2 `--only battles,relations_peace,arms_treaty` (11:12-11:20): battles 86/86 (D1 included; the game made no
    treaty this time); relations_peace aborted at its month by a tactical battle of the host, at war with France (the
    stand-in nation 2, computer's rules) since battles' month C ("War has broken out between Great Britain and France",
    the game's own): unrelated, the next runs passed. Game open 271 + 132 + 471 + 167 + 175 = 1216 s in five runs.
- **Notes.** The month check "client save [IntelReports] has no empty or repeated entries" failed in arms_treaty M3: the
  client's own intelligence check of M2 ("spy event, 2 new reports") made the same report twice; the list check of M2
  already allows that ("a repeat among its own monthly check's new reports"), the save check doesn't. Unrelated to the
  treaty (0.4.1's per-player intelligence); left for its owner. `UpdateUIValues` at war also zeroes every nation's
  DisplacementLimit and BuildConstraint on the joined PC (B1's behaviour, unchanged: the nation's own go back with its
  turn, as the player's own would in single player).
- **Merge with B5 round 4 (int-060, 2026-10-23).** B5 round 4 (f) fixed the same cause on its branch (B2's mask hid a
  joined nation's war from `CreateArmsControlTreaty`; battles B1: a treaty during nation 1's war scrapped its ships under
  construction, +1,382, and cut its BudgetModifier by 2). One implementation of each part after the merge:
  - The conference sees the joined nations' wars through B5's `TreatyWarsEnter`/`TreatyWarsLeave` in `MaskedCall` (only
    cells the mask holds at 5 are shown, also nested in `CheckAllyTrouble`'s mask; TEST `econtreaty <chance> blind`);
    `AIMaskShowWars`/`AIMaskHideWars` are gone. Log `relations v2: the arms-control conference (CreateArmsControlTreaty)
    sees the war(s) of the nations people play: 1-3 3-1 (...)`.
  - The end for a war stays this fix's: the host ends a treaty in force once (`EndArmsTreatyForWar`), no box on any month
    load, `armstreaty=1` to the clients. B5's TEST cancels (`econtreaty cancel`, `blind`'s cancel at once) now call
    `EndArmsTreatyForWar` too (the game's original `CancelTreaty`, so also on a joined PC), and B5's month-end conference
    (`econtreaty <chance>`) runs `TestArmsConference`, the call `armstreaty conference` makes.
  - B5's money record of a treaty (`econ` treaty=, treatyBM=, conference=; `economy::TreatyBefore`/`TreatyAfter`) is
    unchanged; a treaty is signed only at peace now.
  - One suite scenario `arms_treaty` (4 months, group `relations` after `war_effects`, EST 150 s): a) the host's own war
    as before; b) M1 at peace the game's conference at the month's end (`econtreaty 100`) makes a treaty, its money for
    nation 1 in the record and the money check, the same treaty on both copies (its tonnage limits then lifted with
    `armstreaty set`); c) war 1-J, the client's refresh with an older host's rule and with this host's, M2 the host's
    end once; d) M3 the conference at the month's end during the war: "With a war already in progress", conference=1
    treaty=0 treatyBM=0; e) M4 no box, no second end. Both war months of B5's version (M1 war, M2 `blind`) are folded
    in (B5's M2 reproduced the failure, the record is now checked with a treaty signed at peace). The suite's
    `treaty_guard` is off while the scenario keeps its treaty (`ctx.keep_treaty`).
  - Live (dist_int060_2, RTW3MP EB12731EA74602CD; 13:05-13:19, game open 823 s): `--only session,relations_peace,
    war_effects,arms_treaty,battle_generator,battle_surprise,battles,lockstep_month` 417/420: session, battles (D1
    included), relations_peace, arms_treaty (b: 0 -> 132 months, 15000 t, 10 in; nation 1 `econ` conference=1 treaty=+0
    treatyBM=-2: none of its ships under construction was over the limits in Jul 1901, B5's runs measured +1,363 and
    +546 with the same record; M2 `the war of nation(s) 1 ends the disarmament treaty (131 month(s) left ...)`; d) `sees
    the war(s) ... 1-3 3-1` and the game's message), battle_generator, battle_surprise and lockstep_month all PASS; no
    game error box on either copy. war_effects 66/69: M2 "host intel list/save [IntelReports] has no repeated
    entries" (the host's own monthly intelligence gave "Intelligence report France: Our 7 in guns are superior ..."
    twice in its turn report; the mod's clean-up removed it at the next turn, `intel reports: 11 entries -> 9`; the
    known repeat of the game's intelligence, not the merge) and M2 (c) "the host shows no dialog for it" (a false
    positive: the host's turn report line "Research breakthrough: Submarines, Early coastal submarine, ..." matched the
    box filter; the frame's lines, "Trade war this month: ..." and the client's 10 messages were there).
  - Suite fix after the merge: war_effects M2 (c) reads a host box line by line (`sea_war_box_lines`: a turn report
    joins its lines with " | ") and skips the host's research lines ("Our scientists ...", "Research breakthrough:
    ..."); regression_selftest checks it with that run's turn report (122/122).

## Testing 2026-10-23: the 0.6.0 release candidate dist_rc060_1 (v0.6.0)
The release candidate dist_rc060_1 (int-060 20eb23a: 0.6.0, live-battle engine 17, protocol 3; RTW3MP.dll SHA-256
C36D487D01CD10AC..., version.dll 157EE071C3133102...) on the reg-060 runner, installed from its frozen snapshot (`--dist`),
the released 0.5.0 (dist_rc050_2) put back after every run; slots 1 and 9 and the Preferences identical to their backup
after every run (hash; the backup equal to the one taken before the 0.5.0 candidates' runs), no copy or delay proxy left.
- **Runs.** The default run 1061/1063 (2140 s with the game open): every 0.6.0 scenario passed (`war_effects` 69/69,
  `arms_treaty` 62/62, `battle_generator` 45/45, `battle_surprise` 37/37) and every live-battle one; its two fails are
  `relations_peace` b)/d) (suite, below). `--stress` 1383/1395 (5943 s): `battles` 119/119, `battle_generator` 68/68,
  `lockstep_month` long 79/79, `twopc` full 83/83, `lockstep_rejoin` 53/53, `lockstep_torpedo` 19/19, `lockstep_strike`
  21/21; its fails are suite issues and battle conditions (below). `--group relations` twice: 287/287 (478 s), then
  261/264 (477 s) with the client's SaveBuildCampaign box (below). No other game error box in any run.
- **NO-GO: the client's "Error in SaveBuildCampaign procedure! Access violation at address 00C2B9D4 in module 'RTW3.exe'.
  Read of address 00000029"** in the second `--group relations` run (15:57:38), at `arms_treaty`'s M2 submit: the
  client had just run c) (TEST `armstreaty` host 0 and 1 with a refresh at war: "arms treaty: CancelTreaty left to the
  host ...", then "arms treaty: ended on this PC for its nation's war, as the host ends it (276 month(s) left ...)", its
  war counter 1 -> 0 by the test), the turn checks at war, then the submit's save. RVA 0xB9D0 is `@UStrAddRef` (`mov edx,
  [eax-8]` with eax = 0x31): the save copied a string whose pointer was 0x31. B5 round 4's run_g1 (dist_b54_8) had the
  same box at the same submit (00C25D4B, read 00000000) and one at `battle_generator`'s start (00C2751B, read 18740127);
  no 0.4.x or 0.5.0 run had one, and the client never joined another campaign (the 0.3.3 trigger). Heap or string
  corruption on the joined player's PC in a 0.6.0 path before that save (`war_effects`' and `arms_treaty`'s client parts
  are the suspects); the box stops nothing (the turn went in), but per the release rule it is a NO-GO until
  investigated. Both logs of the run are kept with the developers' session files (`r60\rel2_*`).
- **The repeated intelligence report is the game's** (open since 0.4.1 RC2). From the live image (RVAs): the host's
  monthly check `CheckIntelligence` (0x456244) calls `CheckIntelligenceForNation` (0x491288) once per nation (1..8 or
  8..1, after a hit 60 % stop), which calls `SpySuccess(0, i)` (0x46c744) once (a second time only for a communist
  player, 1 in 36). `SpySuccess`'s gun branch (0x46d704: the player's spying, Random(100) < 50) loops up to 11 times:
  a calibre `Random(16) + 2` drawn with replacement, then "Our N in guns are superior to X guns of the same calibre."
  (0x46d872) or "X N in guns have better performance than ours." (0x46d7d9) and a 40 % chance to go on. A calibre
  drawn twice gives the same report twice in one call: the host's "Our 7 in guns are superior to French guns of the
  same calibre." twice in the merge run, and a joined player's "spy event, 2 new reports" (one call of the game's
  `CheckIntelligence` in its view) with both the same. No routine of the mod runs twice. Suite: the month checks allow
  such a repeat on the host and in both saves (`GUN_REPORT_RE`, a NOTE).
- **Suite issues, fixed** (re-run in the relations runs where they apply): `relations_peace` b)/d) after `treaty` h) in
  the same month (the players' peace had handed ship 495 to France on the host; the client's copy, a month behind,
  handed it to Russia; the host logged "skipped: not among nation 1's ships"): such a ship stays where the host has it
  (`PEACE_SHIP_SKIPPED`); `victory_points` a) after the player's own peace in the month's relation turn zeroed nation
  1's VP (`vp_shown_ok`, `--stress` only); `war_effects` without a ship under construction (a `SKIP-note`) and with an
  old ship of J commissioned in M1 (the newest ship now); `lockstep_resume` i) counting the re-sent month's "already
  rolled" (`session_again` waits for that roll). `ls_start_test` ends at once with the game's box when its `pocbattle`
  gives no battle.
- **Battle conditions (`--stress`, not re-run: the candidate was stopped):** "Enemy unable to challenge our forces in the
  area! Gain 102405 VP!" at `lockstep_limits`' and `lockstep_stress` f)'s test battle after a chain of test battles
  (the message's VP = the form's +0x32fd + 5, RVA 0x3b70bc; seen on 2026-10-17 too); `lockstep_missiles` in a battle whose
  sides never sighted each other (the game's end at minute 500), 11/18.
- **Not run on this candidate:** `--stress --only lockstep_month` (its long variant passed inside the `--stress` run,
  79/79), the re-runs of the battle conditions and the mixed versions (the driver `mixed_060.py`, cases a and b, is in
  the developers' session files: TESTING.md, Mixed versions).
- Game open 2140 + 5943 + 478 + 477 = 9038 s. Suite commits on reg-060: 447cb09, b137fbb, 0392dc5, f6bf03c, 0e51b7a.

## Fix 2026-10-23: the joined player's SaveBuildCampaign access violation (0.6.0)
The release candidate dist_rc060_1 stopped at the joined player's "Error in SaveBuildCampaign procedure! Access
violation at address 00C2B9D4 ... Read of address 00000029" ([the 0.6.0 release candidate
dist_rc060_1](#testing-2026-10-23-the-060-release-candidate-dist_rc060_1-v060)). Branch fix-av from int-060 13b33de.
- **Where it came from.** `SaveBuildCampaign` writes `BattleAreaName` and `BattlePossessionName` (0xc8ee5d-0xc8eecf):
  `mov eax, [esi+0x4a3b]` (`BattleArea`, a `TMapArea`) or `[esi+0x4a36]` (`BattlePossession`, a `TPossession`), then
  `mov eax, [eax+0x54]` (`Name`), `push eax` and its "Ident=Value" writer 0xab6bc8, whose prologue `_UStrAddRef`s the
  value (0x83b9d0: `mov edx, [eax-8]`, the faulting 0x83b9d4 = RVA 0xB9D4; `'XXX'` when the field is nil). The two
  fields are set by `DoBattle` (0xcb3b9a/0xcb3bb9). The battle screen's close (`TfrmMain.FormClose` 0xdcd9e4) clears
  the possession, nothing clears the area (`DoStrategicTurn` 0xce19fb reads it for "You may retire the CO <area>, ..."
  while `RetireAdmiral` +0x9ed is set, and on Yes writes `BattleArea.AreaCommander` +0x178). `LoadBuildCampaign`
  frees every map area with its possessions (`LoadAndDisplayMap` -> `LoadMapData` 0xce7248 -> `ClearMapData`, then a
  new `TMapArea` per area; possessions live in `TMapArea.Possessions` +0x84) but sets the two fields from `[BATTLE
  INFO]` only for a save with a battle in progress (0xc9ad91: ScenarioInProgress or `BattleIndex` > -1, else `jle
  0xc9aea3` past them; `InvasionTarget`/`EnemyInvasionTarget` are set on every load, 0xc9ab49/0xc9ab5d). So after a
  battle and any load both point at freed objects, and the next save reads `Name` from whatever the memory manager
  put there. A `TPossession` is 3420 bytes, a medium block that is coalesced and reused for anything; a `TMapArea` (392
  bytes) is mostly reused by the next load's areas in the same order. The forms of the box: a `Name` slot holding the
  characters of a string "1" (0x00000031: the RC's read of 0x29) or "00" (0x00300030: the 0.3.3 read of 0x00300028),
  "Integer overflow" (the line's concatenation `_UStrCatN` 0x83c71c met a garbage length: the game's own code has no
  overflow checks, no `jno`/`into` in the image, so the RTL's length check raised it), and FastMM's `FreeMem` (0x835d4b,
  header 0) or `System.Move` (0x83751b) in B5's run_g1.
- **The experiment** (the same build, RTW3MP_TEST_KEEPBATTLEPTRS=1 = the game's behaviour, with RTW3MP_DIAG: the
  pointers and their `Name` before every save and after every load), `--only war_effects,arms_treaty`: the client kept
  them across 23 loads, the host across 9. One `BattlePossession` pointer 0F43EF00 read "East Prussia" (the battle's)
  before a save, then "Liaotung Peninsula", "Sakhalin", "Annam" after three loads (each load built another possession
  in the freed block); another, 10320330: "East Prussia" -> "Northern Korea" -> "Weihaiwei", the name its next save
  wrote; 11CCFA70: "Finland" -> "British Columbia" -> "Maine" -> "Sakhalin". `BattleArea` 07E71920 kept "The Baltic"
  (its block reused by the same area); the host's "Northern Europe". No box in that run: it needs a block reused by
  something other than a possession. With the fix (RTW3MP_DIAG only): "load: the last battle's area The Baltic and
  possession Eastern Germany cleared (...)" 7 times on the client and 2 on the host; only a live battle's load
  (ScenarioInProgress) set them again, from its save ("Northern Europe", "United Kingdom"); 155/156 (the one FAIL is the
  client's own intelligence repeat in its save, open since 0.4.1), no box.
- **Why 0.6.0** (an inference from the code and the experiment). The stale pointers are as old as the mod's reloads; 0.6.0's battles for joined nations (home waters,
  possessions, missions, B6) fight and decline battles at possessions on the joined player's PC, and a declined or
  avoided battle never opens the battle screen whose close clears the possession: `BattlePossession` stayed set
  through the slot 87/77 reloads. In earlier versions the client's battles came from the computer's generator, mostly
  without a possession, and a stale `BattleArea` was mostly reused by the same area.
- **Not the treaty.** `CancelTreaty` (0xd30b88) writes integers, the label's caption ('-', `TControl.SetText`
  0x981ca0) and `FHint` (+0x90, `_UStrClr`); the treaty's `[General]` keys are integers (0xc8e781-0xc8e820). Its timing
  in `arms_treaty` c) was the timing of the battles and reloads before M2; the client-side end stays as designed. The B5
  path (`econtreaty cancel`, `blind`) calls the same routine: the same. The B6 occurrence at `battle_generator`'s start
  in B5's run_g1 and dist_av_1's "Integer overflow" at `battle_generator`'s M1 submit (16:24:30, run2) are the same
  stale `Name` reads. The 0.4.1 k2 "Error in AdjustForcesForBattle" box (`@UStrAsg`) may be the same pointers too:
  the battle setup reads `BattleArea` (`IsHomeArea` 0xbe3f0b) and `BattlePossession` (`DoPlayerDecline` 0xbe4066) (a
  guess; not reproduced).
- **The fix** (`game.cpp`, the `LoadBuildCampaign` hook): every load starts with both fields nil, the state
  `FormCreate` leaves; the load sets them again for a battle in progress; a failed load gets back those that still
  belong to the campaign in memory. The joined player's PC, the host and the 0.3.3 case (another campaign loaded in
  the same process) take the same path. Log: "The last battle's area across a load: ok" at the start, "load: the last
  battle's area X and possession Y cleared (the load sets them again only for a battle in progress)". TEST ONLY:
  RTW3MP_TEST_KEEPBATTLEPTRS=1.
- **Diagnosis tools kept** (no effect without the variable): RTW3MP_DIAG's exception handler logs an access violation,
  integer overflow or division error in the game's code with the registers, the objects they point to (class, 16
  dwords) and the game's return addresses on the stack (RVAs), Delphi exceptions with their class, and no longer counts
  the mod's own guarded reads; `pocHeapCheck` (`dl::HeapCheck`) walks frmBuildCamp and the air unit list through RTTI
  (fields, records, static and dynamic arrays, `TList` and `TStringList` entries; components not entered): every
  UnicodeString valid, every reference a live object (a freed block starts with the memory manager's link), about 4700
  objects and 14 500 strings in 16 ms, before every save and after every load. Bridge: `pocheapcheck`, `battleptrs`
  (read only). Suite: `battle_area_month_check` in `month_checks` (the client holds neither after the month).
- **Proof** (dist_av_4 = 6bbca05, RTW3MP 69410DC1EA40B9F8, no test variables; game open 1908 s): `--only arms_treaty`
  five times 83/83, 83/83, 83/83, 82/83, 83/83 (the FAIL: the client's own intelligence repeat in its M2 save, open
  since 0.4.1); `--group relations` twice 302/302 (497 s) and 276/276 (410 s); `--only
  war_effects,arms_treaty,battle_generator` 229/229 (343 s). No game error box on either copy, the treaty the same on
  both copies at every step of `arms_treaty` (its checks), the client's loads cleared the last battle's area and
  possession 1-11 times a run, and `battle_area_month_check` passed after every month. Before the fix, with the
  diagnosis on, the box itself came once in three runs: "... Integer overflow" at `battle_generator`'s M1 submit
  (dist_av_1, `--group relations`), as in the release candidate's second `--group relations` run and B5's run_g1.

## Testing 2026-10-23: the 0.6.0 release candidate 2 dist_rc060_2 (v0.6.0)
The release candidate dist_rc060_2 (int-060 d0964a3: 0.6.0, live-battle engine 17, protocol 3; RTW3MP.dll SHA-256
616282C8173AD39A..., version.dll 04CABEAB6A3BB44E...) on the reg-060b runner, installed from its frozen snapshot (`--dist`),
the released 0.5.0 (dist_rc050_2) put back after every run; slots 1 and 9 and the Preferences identical to their backup
after every run (hash), no game folder copy, delay proxy or `RTW3.exe` left.
- **Runs.** The default run 1122/1126 (2083 s with the game open): its fails are `battle_surprise` b) (three lines: the
  game's takeover step, below) and `lockstep_pvp` f) (suite, below). `--stress`: a first run stopped at `lockstep` after
  1010 s (suite: `player_copy`, below); the second whole run 1427/1431 (6778 s); its four fails (`lockstep_stress` f),
  `air_units` M2 and e), `lockstep_missiles` e)) are battle and game conditions: `--stress --only
  lockstep_stress,air_units,lockstep_missiles` gave 33/37 (868 s: `lockstep_stress` S8, below) and then 176/176 (1727 s).
  `--group relations` twice: 301/301 (497 s) and 302/302 (663 s). `--stress --only lockstep_month`: 99/99 (398 s).
- **No SaveBuildCampaign box, and no other game error box, in any run** (the runner's `game_error_boxes`, both logs):
  the per-month check that the client holds no last battle's area or possession passed every month, and the client's
  log has "load: the last battle's area ... cleared" at the loads after its battles (10 and 11 in the relations runs).
- **The old trigger, run** (the joined player's first save after a join on another campaign in the same process, 0.3.3's
  `air_units` and dist_vp_2's `lockstep_air` start; the fix entry above marked it fixed without running it): the driver
  `other_campaign.py` (developers' session files `r62`, the reg-060b runner's helpers). The client's game loads slot 9
  (the 1935 campaign) on its own, joins a session on it (France), fights a live battle there (`pocbattle 2 0`: both
  games hold its area, "Northern Europe", and no possession), goes back to slot 77 (a load: area 0), then the host moves
  to slot 1 (1900) and the same client process joins it as nation 1: `battleptrs` area=0 poss=0, `pocheapcheck` bad=0
  (4714 objects), two months submitted (the client's SaveBuildCampaign) and merged, no game error box, no
  SaveBuildCampaign or access violation line in either log: 35/35 (91 s; the first run's 34/35 expected a possession
  for a battle that has none: driver). The control with `RTW3MP_TEST_KEEPBATTLEPTRS=1` (the game's own load): the area
  pointer 07C90260 survived the return load and the move to the 1900 campaign, its freed block re-allocated for the 1900
  campaign's own "Northern Europe" (so `pocheapcheck` called it "of this campaign"); no save was made there (9/10, 54 s:
  the control's STALE expectation was too narrow). The flow is practical: fixed, as the fix entry said.
- **Mixed versions** (`mixed_060.py`, a folder copy with 0.5.0 outside the Steam library, deleted after each case):
  a) a 0.6.0 host with a 0.5.0 player: 66/66 (261 s): the join ("battle generator 0"), the player's relation rules, B5's
  blockade and its message on the 0.5.0 PC, no B6 for its nation, a battle it declined resolved by the game on the host,
  the host's battle against it **fought live** (514 minutes equal, no DESYNC, agreed end), peace and a normal month.
  b) a 0.5.0 host with a 0.6.0 player: 54/55 (421 s), then 55/55 (354 s) with the driver's pattern fixed (its bare
  `surprise` matched B6 round 3's battle-slot state lines, which every 0.6.0 PC logs): hostgen=0, no allies' war help,
  building for war or surprise moves, a declined battle going alone, the host's battle **fought live** (575 and 436
  minutes equal, no DESYNC, agreed ends). TESTING.md, Mixed versions.
- **Suite issues, fixed and run again** (TESTING.md, Known traps): the client save's repeat of what its own monthly
  check repeated the month before (the host sends the player's reports back: dist_av_4's `arms_treaty` M2, and live in
  this candidate's first relations run: "Sep 1901  Spain - The Spanish navy is reportedly doing a lot of night
  training." three times, so not only the gun comparison repeats; `saved_own_repeat`, a NOTE with the texts); B5's
  blockade points in a war between two players' nations (`victory points: nation G +v (blockade, in its war with nation
  L) -> t, war score 1-2 x:y`) in `lockstep_pvp` f) and `treaty` k) (`WAR_VP_CREDIT_RE`, `war_score_steps`); `battles` b)
  when the fought battle is the player's generator's request ("..., 4 attempt(s), fought by Captain": `battle_area`);
  `player_copy` f) with the war `battle_surprise` leaves in the copy (its month's battle preview stopped the first
  `--stress` run: peace in the copy first); `battle_surprise` b) without the game's takeover step (a `SKIP-note`).
- **Battle and game conditions** (each passed when run again): `battle_surprise` b) (CheckForWar's landing/takeover step
  runs in 50 %, with the trait 95 %); `air_units` M2 (the 1935 campaign's arms-control conference bound the host to
  scrap 141,500 tons: the game refuses its Turn); `lockstep_stress` f) (the last hash line after the game's own end, the
  state equal but rng 3 draws apart); `lockstep_stress` S8 (the drop met a `#32770` box on the client: it never came
  back); `lockstep_missiles` e) (held ships of side 0 lost SSM in the approach, as on dist_rc060_1; the missile code is
  unchanged since 0.5.0: watch it).
- **Verdict: GO** for publishing 0.6.0 with exactly dist_rc060_2: no product issue; the SaveBuildCampaign box of
  dist_rc060_1 did not recur in any run (default, `--stress`, both relations runs, the old trigger), the mixed versions
  fight live in both directions, every fail was the suite's or a battle or game condition that passed when run again.
- Game open 2083 + 1006 + 6778 + 868 + 1727 + 497 + 663 + 398 + 261 + 421 + 354 + 91 + 91 + 54 = 15292 s. Suite commits
  on reg-060b: 8ca6425, a268b03 and this entry's.

## Fix 2026-10-25: the tension bar above the war line between two players: its hint and a chat line say why no war comes (v0.6.1)
- **The report** (0.6.0, two friends on two PCs; the host plays nation 0, the joined player nation K): "The tension bar
  when exceeds the critical threshold and no war is declared for non host".
- **The "critical threshold"** is the war line of the strategic screen's tension panel (static, 1.01.44): a TShape
  `WarLine` in `pnTension` (Top 43 of the panel's 251), no code touches it. `ShowOneTensionLevel` 0xc7f1e4 sets each
  bar's Height to Tension x 20 and its Top to 251 minus that: a bar crosses the line at Tension 11 and fills the panel
  at about 12.6; `GetTensionColor` 0xd10c80 colours it (< 4 green, 4-6 olive, 7-9 yellow, 10-20 orange, 21 and more
  red); its hint is "Tension with X is t", above 40 "You are at war with X" (GAME-INTERNALS, the tension bars).
- **The single-player rule** (`CheckForWar` 0xc84f04, static; GAME-INTERNALS has it in full): per nation at tension
  T < 40 (0xc84f9c skips T >= 40), the war plan `WillGoToWar` counts down; with a plan only rumours; else at a long
  peace (War < -48) Random(180) < T turns the month into rumours; else a declaration when T >= 13 + the nations at war
  with the player (or 12, or 10-11 with War > 2, with chances); else at War < -48 and T >= 11 a war plan of 3-8 months
  (5 %) with the spy report. So in single player a nation whose bar is that far above the line declares war within the
  month, or soon.
- **Two readings, both reproduced live on the released 0.6.0** (dist_rc060_2 installed, the 1900 campaign: host Great
  Britain = 0, client "Captain" Germany = 1, "Bot" France = 2 reserved; a driver in the session folder, 268 + 88 s open):
  - (a) K and a computer nation: `settension 1 3 15` and `1 4 25` on both copies -> in K's first relation turn the
    client's dialogs "War has broken out between Germany and Russia" and "USA has joined the war against Germany!",
    host `relations of Captain (nation 1; ...): ... war 1-3 war 1-4`. **Works**, as the host's own (`settension 0 5 15`,
    `0 6 25`: "Great Britain and Japan are now at a state of war!", "Italy has declared war on us!" in its month).
    K and Japan at 40: never a war in 3 months, as in single player (T >= 40 is skipped; the mod stops tension
    changes at 40: `HostApplyRelations`, the events, `PlayerMaskLeave`).
  - (b) the two players: host-K 25 -> 26 -> 28 -> 28 over 3 months, **no war**: pushed by both players' own
    `RandomTensionChanges` ("Tension with Great Britain increases as the war is increasing tensions!", "... due to
    historical enmity!") and never relieved; both screens "Tension with Germany is 28" / "Tension with Great Britain is
    28", the bars far above the war line, and nothing said why.
  - The report is (b).
- **Cause: by design, but silent.** Since relations v2 wars between players are declared only by the players
  (Diplomacy in the Multiplayer window): the host's `CheckForWar` sees K as a player (`PlayerMaskEnter` kind 1: K's
  tension with the host reads 5, no plan), K's own (B3, in K's view) sees the host's and the other people's nations so
  (`MaskAsPlayer`), and the computer's rules leave K's pairs alone (B2). A computer nation that far above the line would
  have declared; the players got no word that the game never will.
- **A hypothesis dropped: CheckForWar twice a month.** Its call sites are `DoTurn` 0xce100c and `DoEndOfTurn` 0xcfcdb3,
  but `DoTurn` 0xce09f4 has no code callers (only the RTTI method table points at it; GAME-INTERNALS already called it
  unused): the month is `btbnTurnClick` 0xd57c7b -> `DoTurn2` 0xce1644 -> `CheckForBattles2` 0xce180e ->
  `DoStrategicTurn` 0xce181a (`CheckTurnEvents` 0xce1e42 with `RandomTensionChanges` and
  `TensionEffectOfPreviousWars`, the war step, `WarHelpFromAllies` 0xce218f, `CheckBuildingShipsForWar` 0xce21c5) ->
  `DoEndOfTurn` 0xce21f4 -> `ShowTurnMessages` -> `CheckForWar` (unless `GameOver` +0xa05). Single player runs it once
  a month, after the tension routines: B3's order already. Running it twice for K would have doubled K's war chances
  against single player, so B3 is unchanged; GAME-INTERNALS' "runs from DoTurn and from DoEndOfTurn" is corrected.
- **What changed** (the rule stays: the game declares no war between players; whether it should is left to a later
  decision):
  - *The bar's hint, on every PC in a session* (game.cpp `PeoplesBarHints` after `ShowTensionLevels`, outside the
    relations view): at peace the bar of a nation people play gets " - played by a person: wars between players are
    declared only in the Multiplayer window (Diplomacy), never by the game" after the game's "Tension with X is t".
    Which nations (`hookcb::WarOnlyByPlayers`, session.cpp): on the host a joined nation under the player's rules (the
    one its CheckForWar sees as a player); on a joined player's PC, while the month's header has `rel` or `kvp` (the
    host runs the player's rules for its nation), the host's nation (slot K in the view) and the nations of the
    header's `humans` and of the lobby (a reserved nation too: K's CheckForWar sees it as a player and B2 leaves the
    pair alone).
  - *A chat line, from the host* (session.cpp `WarLineNews`, as each month goes out in `BroadcastState`, after "New
    month"): for every pair of the host's nation and the joined nations under the player's rules at peace whose
    tension is 11 or more and was below at the last check (a per-pair memory; the first check after hosting tells the
    pairs above it already), both players get `* Tension between Great Britain and Germany is 25, above the war line.
    Wars between players are declared only by the players (Multiplayer window, Diplomacy); the game will not declare
    one.` (a joined player as a notice, the host in its own chat with a flash); log `war line: ... (told: the host
    nation 1)`.
  - Bridge `warline` (read-only: the nations with the hint on this PC, the host's pairs above the line) and, TEST
    ONLY on the host, `warline forget` / `warline check`. No protocol change (3), live-battle engine 17.
- **Version mixes** (static: both parts are local to the PC that has them): a 0.6.1 host with a 0.6.1 player: the
  hints on both screens, the line on both. A 0.6.1 host with a 0.6.0 player: the host's hint and line; the player
  gets the line as a chat notice ("* ..." from the host, as any host line), no hint on its bars. A 0.6.0 host with a
  0.6.1 player: no line (the host sends none); the player's bars have the hint (a 0.6.0 host sends `rel`, `kvp` and
  `humans`), the host's not. 0.6.0 with 0.6.0: as before.
- **Live proof** (dist_fw_1 installed with the driver, `fw_live.py --proof --months 2 --ka 15 --kh 0 --kb 25`: nation
  1's war counter -10 for the month, so the rumour branch at War < -48 cannot take the declaration's place; 73 s
  open, 48/48 checks; slot 1 and the Preferences restored, 0.6.0 reinstalled):
  - (a) Sep 1900: the client's box "Germany and Russia are now at a state of war!", its log `relations for nation 1
    sent: tension=0:1:25:24;1:2:7:6;1:3:15:50;1:7:4:3; ... kwar=1 ran=tension,previous wars,war msgs=6`, the host's
    `relations of Captain (nation 1; tension,previous wars,war; 6 message(s)): 0-1 25->24 1-2 7->6 war 1-3 ...`.
  - (b) the host's `war line: Tension between Great Britain and Germany is 25, above the war line. ... (told: the
    host nation 1)`; the line once in each PC's chat in M1 and not again in M2 (tension 24, then 23); `relview`: host
    slot 1 "Tension with Germany is 24 - played by a person: ...", client slot 1 "Tension with Great Britain is 24 -
    played by a person: ...", no computer nation's bar with it (the client's France bar has it: reserved for Bot; the
    host's not: France does not follow the player's rules there, `warline` people=1,); no war 0-1.
- **Suite**: a new scenario `relations_war` (group `relations`; TESTING.md): (a) and (b) as above, with `warline
  forget` before the month and `warline check` after it (told=0), ending at peace (`peacetest 100 2 1`) with the
  players' tension and nation 1's war counter as they were. On dist_fw_1, `--only session,relations_peace,war_effects,lockstep_pvp,relations_war`: **172/172**
  in 323 s (session 17/17, relations_peace 34/34, war_effects 73/73, relations_war 18/18 in 37 s with Russia: "War has
  broken out between Germany and Russia", lockstep_pvp 30/30: the live battle between two joined players fought, no
  DESYNC); `regression_selftest.py` 129/129.
- Game open: 268 + 88 (the 0.6.0 repro runs) + 73 (proof) + 323 (suite) = 752 s.

## Fix 2026-10-25: Fire torpedoes greyed out in live battles: the battle's Realism in both players' chat (v0.6.1-dev)
Report (0.6.0, two friends on two PCs): "torpedoes can't be manually fired player vs player combat". Static reading of
1.01.44 with `rdis.py`; live runs on the released 0.6.0 (dist_rc060_2) and on this fix (dist_ftp_1): the 1900 fixture
in slot 1, host-vs-player test battles, each copy with its own battle preference Realism in memory
(`RTW3MP_TEST_PREFS=RealismSetting=<n>`, as two PCs' Preferences), the division report and the torpedo dialog driven
the player's way with posted messages (the ship tree's "Status...", BM_CLICK on *Fire torpedoes*, posted clicks on the
target and mount lists, BM_CLICK on *Fire*; the session folder's `ftp_live.py`). Engine level stays 17.
- **The game's rule.** `TdlgDivisionReport.FormShow` (0xae2298) enables *Fire torpedoes* only above Realism's second
  level: `mov eax, [0xf278b4]; cmp dword ptr [eax+0x56], 1; setg dl; mov eax, [ebx+0x4bc]` (`bnTorps`) `...; call
  [ecx+0x9c]` (SetEnabled), 0xae22ca..0xae22de, the class's only access of +0x4bc; the DFM has the button
  `Enabled = False` and no hint. The levels are `TdlgPrefs.rgRealism`'s items: 0 "Admiral's mode", 1 "Rear admiral's
  mode", 2 "Captain's mode" (its hint: "This will govern which divisions you can control manually during battle.").
  **The game's default is 1:** `TdlgPrefs.LoadPrefs` pushes 1, the default, before `ReadInteger('Settings',
  'RealismSetting')` (0xe0b688..0xe0b69b). The automatic torpedo fire (`CheckTorpedoTargetsForAllShips` 0xda9648 and
  the routines it calls, 0xda7688..0xda9c00, `FireMount` with `Manual` = 0) reads no preference (no reference to the
  cell 0xf278b4 there): ships fire their torpedoes on their own at every level, unless their division holds torpedo
  fire.
- **Cause: the gate is A's Realism, the game's default closes it, and nothing told the players.** Since 0.4.1 a live
  battle runs both PCs with A's 14 battle preferences (`kPrefs`, RealismSetting among them; A is the host in its own
  battles, the player who starts it in a battle between two joined players). With A at the default, Rear admiral's
  mode, *Fire torpedoes* is greyed out on both PCs, whatever B chose. The only trace was B's log line; the README said
  it in its torpedo and troubleshooting entries. Not a regression and no refusal on B: the button follows A's level on
  both PCs, and at Captain's mode both players fire.
- **Live on 0.6.0** (dist_rc060_2; game open about 8.5 minutes in two runs):
  - (a) A at Rear admiral's mode, B at Captain's mode: *Fire torpedoes* disabled on both copies (`ls ctl` enabled=0);
    B's record RealismSetting=1 during the battle (`ls prefs` "after the battle: RealismSetting 1 -> 2"); no chat line
    about the realism or torpedoes on either PC; B's log only: `battle preferences: Admiral's for the load and the
    simulation (RealismSetting 2 -> 1); this PC's own come back after the battle`; `ls torp` refused on both ("the
    division report's Fire torpedoes is disabled (button=0: ...)").
  - (b) A at Captain's mode, B at Admiral's mode: enabled on both copies; B's record 2, its own 0 kept for after the
    battle: B's button follows A's level, not its own Preferences. (c), both at Captain's mode, is the suite's
    `lockstep_torpedo` (B, A and both at one boundary, through `ls torp`: 19/19 on dist_rc060_1, no fail in
    dist_rc060_2's whole `--stress` run); B's own level never reaches the battle (its record holds A's), so (b) and
    (c) fire alike; the salvoes through the real dialog are the live proof below, on the fix, whose change touches no
    part of the fire path.
- **The fix** (`lockstep.cpp`: `BattleRealism`, `RealismName`, `RealismLine`, `TorpOffNotice`; each PC's own chat, no
  change to the battle):
  - *At the start* both players read the battle's Realism and what it means for torpedoes. A: `Realism: <mode> (your
    battle preference, for both players). ...`; B: `Realism: <mode> (<A>'s battle preference, for both players; yours
    isn't used). ...`; at Captain's mode `... Fire torpedoes in the division report works while the battle is
    paused.`, below it `... Fire torpedoes in the division report needs Captain's mode, which <you|A> can choose in
    the Preferences before the next battle. Your ships still fire torpedoes on their own.` (`ACheckReady`, the first
    running: A's own line, and B's text through A's `info` message; at a rejoin only B's). A's start message carries
    `rz=1` ("A says it"): a B whose A sends none (A on 0.6.0) says it itself at its first `go`, from its record (A's
    level since the load).
  - *Once per battle*, when a division report is on screen with *Fire torpedoes* off for the Realism (`TorpOffNotice`
    from `lockstep::Tick`, every 250 ms: the report visible, `bnTorps.FEnabled` 0, the record's level below 2), each
    PC logs `a division report shows Fire torpedoes off (the battle's RealismSetting <n>)`, says `Fire torpedoes is
    off: it needs the battle preference Realism at Captain's mode, and this battle uses yours, <mode>. Your ships still
    fire torpedoes on their own; choose Captain's mode in the Preferences before the next battle to fire them
    yourself.` (B: `... uses <A>'s, <mode> (yours isn't used). Your ships still fire torpedoes on their own; <A> can
    choose Captain's mode in the Preferences before the next battle.`) and sets the notice `Live battle: Fire torpedoes
    needs Realism at Captain's mode (see the chat)` (the Multiplayer window flashes). A disabled button gets no click
    (no `OnClick`), so the notice comes with the report; a hint on the button would need a Delphi string written into
    the game's control for the same words.
  - Bridge: `ls realism` -> `ok realism=<n> name=<mode> torps=<0|1> said= a_says= noted=`.
- **Decision: the game's rule stays.** Realism is a package of the simulation's rules (which divisions the player
  commands, detach, lead changes and turn-aways, the sighting routine, the objectives: 18 routines read it), and the
  game offers manual torpedo fire only at Captain's mode in single player too. Both PCs use one level, A's, so the
  battle is the same for both players and stays deterministic. Enabling *Fire torpedoes* below Captain's mode in live
  battles only would hand players a control their chosen level withholds, while the automatic fire launches torpedoes
  for both sides at every level. What was broken is that the rule was invisible; the gating itself works.
- **Versions** (engine level 17 kept: `rz` is a new key that 0.6.0 ignores; the lines are chat, outside the battle's
  state): 0.6.1 with 0.6.1: both start lines and both notices. A on 0.6.1, B on 0.6.0: both start lines (B shows A's
  `info` line like any other), the notice on A only. A on 0.6.0, B on 0.6.1: B's own start line and B's notice, nothing
  new on A. 0.6.0 with 0.6.0: as before. Live (two game folders, the second with dist_rc060_2's DLLs; 113 s): a 0.6.1
  host at Admiral's mode with a 0.6.0 player: A's line and notice, on B `Realism: Admiral's mode (Admiral's battle
  preference, for both players; yours isn't used). ...` from A, no notice on B; a 0.6.0 host at Rear admiral's mode
  with a 0.6.1 player: no line on A, B's own line (`ls realism` a_says=0) and B's notice. Each battle 7 minutes equal
  on both copies, no LEAK, no DESYNC.
- **Live proof on dist_ftp_1** (game open about 26 minutes in five runs, most of it finding torpedo range):
  - A at Rear admiral's mode, B at Captain's mode: *Fire torpedoes* disabled on both; A's `Realism: Rear admiral's mode
    (your battle preference, for both players). Fire torpedoes in the division report needs Captain's mode, which you
    can choose in the Preferences before the next battle. Your ships still fire torpedoes on their own.`, B's `Realism:
    Rear admiral's mode (Admiral's battle preference, for both players; yours isn't used). ... which Admiral can choose
    ...`; the report opened on each copy: the notice once on each, `ls realism` realism=1 torps=0 noted=1 on both
    (a_says=1 on B); 7 minutes equal.
  - Both at Captain's mode: enabled on both, `Realism: Captain's mode (...). Fire torpedoes in the division report
    works while the battle is paused.` on both, no notice, `ls realism` torps=1 noted=0. Through the real dialog
    (both sides' divisions steered at each other with `ls order`, positions from `pocdump`): B alone, `S22 mount 0 at
    Mallard, course 244.0, speed 24, range 700, manual 1: 1 torpedoes in the water (1 -> 0 left, reload 0)` carried out
    at minute 20 on both copies, B's chat `Torpedo salvo fired: S22 launched 1 torpedo.`; A alone, `Revenge mount 0 at
    G10, course 120.0, ...` at minute 25 (reload 22); 44 minutes equal, no LEAK, no DESYNC. Both at one boundary
    (another battle): `deferred torpedo salvo of side 0 carried out at minute 31 (given for minute 31): Benbow mount 1
    at Gefion, course 300.0, speed 15, range 1800, manual 1: ...` then `... of side 1 ...: G10 mount 0 at Welland,
    course 155.0, ...` on both copies in that order, each giver's chat, 33 minutes equal, no LEAK, no DESYNC.
  - The game's own lines in the torpedo dialog seen on the way: "Centreline swivel mount HIGH 244", "Forward (sub)
    HIGH 120", "Out of arc", "No solution", "Expended", "Friendly ship in line of fire!".
- **Suite:** `limits_realism` (`lockstep_limits` f), `lockstep_torpedo` a)) takes the battle's start marks and checks
  the start lines on both copies and, below Captain's mode, the notice on both (`ls realism` noted=1), at Captain's
  mode none. On dist_ftp_1 with the run's RealismSetting 2 (`--only lockstep_torpedo,lockstep_ui,lockstep_pvp`):
  109/109 (session 16/16, `lockstep_ui` 41/41, `lockstep_pvp` 30/30, `lockstep_torpedo` 22/22: a) with the new
  checks, B, A and both at one boundary with the random-draw trace, the resync with torpedoes in the water, auto-run),
  630 s with the game open. (d) A battle between two joined players (`lockstep_pvp`, relayed by the host): Captain
  was A (`Realism: Captain's mode (your battle preference, for both players). ...`), Bot was B (`Realism: Captain's
  mode (Captain's battle preference, for both players; yours isn't used). ...`), through the host's relay.
- **Found on the way (not fixed here):** the live battles' data-file check compares `Data\tmpShip.ttp`. Two copies
  started from two game folders (the mixed-version runs' first try) had their live battle refused: "Captain's game
  data files differ from this PC's: 1 files: Data\tmpShip.ttp", and the battle went on against the computer. The game
  folder's copy had been rewritten during another run that day (19076 bytes, 21:40; the folder copy's 14400 bytes of
  10/04). The path is used by a `TfrmDesignMain2` routine (0xb1765c: `'\Data\tmpShip.ttp'` at 0xb17bb8, then
  `LoadShipGraphic`); which actions write it was not traced. If normal play writes it, two real PCs' files differ and
  their live battles fall back to the computer; `datafiles.cpp` skips only `Data\Graphics`.
- Docs: README (torpedo salvoes, troubleshooting), ARCHITECTURE (manual torpedo salvoes: the lines, `rz`, the
  decision), GAME-INTERNALS (the levels, the default, the automatic fire), TESTING (`ls realism`, the lines, the
  catalog rows of `lockstep_limits` and `lockstep_torpedo`).

## Fix 2026-10-25: the ship designer's temporary file no longer stops live battles (v0.6.1)
Found by the torpedo fix's mixed-version proof (a host from the game folder, a player from a copy of it): the host's
`Captain's game data files differ from this PC's: 1 files: Data\tmpShip.ttp`, and the live battle went to the
computer. The game folder's `Data\tmpShip.ttp` had been rewritten that day by another test run (19076 bytes, the
install's had 14400). Static reading of 1.01.44 with `rdis.py`; live runs on dist_i61_1.
- **The file.** The only `tmp` file name among the game's strings is `'\Data\tmpShip.ttp'` (0xb17bb8). Its one user is
  a `TfrmDesignMain2` routine (0xb1765c): when the designer has no ship object yet (`[form+0xc6b] = 0`) it builds the
  game folder + that path, writes the file (0xb02fa8) and reads it back (`LoadShipGraphic` 0xb0021c). The routine has
  25 callers, all the designer's own handlers (`FormShow`, `bnNewShipClick`, `bnAddTurretClick`, `bnDeleteTurretClick`,
  `bnAddTorpMountClick`, `seDisplacementChange`, `sbAutoBuildClick`, ...); 0xb02fa8 has 4, all designer handlers
  (`bnSaveClick`, `bnLoadTemplateClick`, the routine, `bnExportDesignClick`). So every use of the ship designer
  rewrites it, and no battle reads it. `Data\tmpMapData.dat` (2.3 MB, dated the install) is named nowhere: not in the
  game's image (ASCII or UTF-16), not in `Launcher.exe`. Write dates: of the 1417 files of `Data` (Graphics aside) on
  this PC, after three weeks of tests only `tmpShip.ttp` is newer than the install (rewritten again at 22:16 by
  another agent's run). `Data\IDes` is written only by a developer's Alt+click on the designer's `Label37`
  (`Label37MouseDown`: `test [ebp+0x10],2`, then a nested routine with `'Data\IDes\'`), not by play; it stays in the
  hash (its designs are real data: the battle load reads coastal battery designs there).
- **The impact (0.6.0, engine 16 and 17).** `datafiles.cpp` hashed every file of `Data` but `Data\Graphics`: on two
  real PCs a player who had used the ship designer (or whose designer was used last with another design) had another
  `tmpShip.ttp`, so every live battle of the session was refused by B ("your game data files differ from ...'s:
  Data\tmpShip.ttp") and fought against the computer.
- **The fix (engine level 18).** The scan leaves out every file whose name starts with `tmp` (case-insensitive) at any
  level of `Data` (`Temporary`), and logs them: `data files: 1415 files of Data (but Data\Graphics and the game's
  temporary files tmp*: Data\tmpMapData.dat, Data\tmpShip.ttp) hashed in <ms> ms: <hash>`. The test switch
  (`ls testdatafile`, `RTW3MP_TEST_DATAFILE`) leaves such names out as well and says so ("(not hashed, the game's
  temporary files: ...)"). The combined hash is not 0.6.0's, so `MP_LS_LEVEL` 18: a 0.6.0 PC and a 0.6.1 PC are told
  apart by the level check before any battle loads ("... the host has 0.6.1-dev (engine 18), you have 0.6.0 (engine
  17)") instead of a data-file notice naming files that are the same.
- **Proof (dist_i61_1, the fix with the merged 0.6.1 work).** The suite, `--only
  session,relations_war,lockstep_torpedo,lockstep_ui,lockstep_pvp,twopc,lockstep_data`: **200/200** in 828 s of game
  (session 17, relations_war 19, lockstep_ui 41, lockstep_data 11, lockstep_pvp 30, twopc 61, lockstep_torpedo 21).
  The new `lockstep_data` (13 s): both scans `1415 files of Data (but Data\Graphics and the game's temporary files
  tmp*: Data\tmpMapData.dat, Data\tmpShip.ttp)`, hash `aebbfdfd9daeb498` on both; B's `ls testdatafile
  Data\tmpShip.ttp;Data\IDes\tmpProbe.sdf` left the hash as it was, and the live test battle started live (B:
  `the game data files are the same on both PCs (1415 files, aebbfdfd9daeb498)`), 4 minutes equal, ended by the game
  inside minute 5; the control `Data\Gundata.dat` changed it (`395928512d7dce60`), `off` put it back.
- **Proof with two game folders (dist_i61_1; the session folder's `i61_tmp.py`, 161 s of game).** The host from the
  game folder, the client from a copy outside the Steam library (deleted afterwards) whose `Data\tmpShip.ttp` differs
  (SHA-256 `C4D2B0A6...` against `DF05EBE3...`) and which has `Data\IDes\tmpProbe.txt` too. Both 0.6.1: the client's
  scan left out its three `tmp*` files, the host's its two, both 1415 files and `aebbfdfd9daeb498`; the live test
  battle started live (B: `the game data files are the same on both PCs (1415 files, aebbfdfd9daeb498)`), 4 minutes
  equal, ended inside minute 5. With `RTW3MP_TEST_FILELOG=1` on both, the battle's load, minutes and close opened 61
  (host) and 94 (client) files, `Data\Gundata.dat` among them, no `tmp*` file. The released 0.6.0 in the copy: the
  host logged `battle against Captain: fought against the AI (live-battle engine level 17, the host's 18)`, its chat
  `* The battle against Germany is fought against the computer: live battles need the same live-battle engine on both
  PCs: this host has 0.6.1-dev (engine 18), Captain has 0.6.0 (engine 17).`, the player's `* The host fights its
  battle against Germany against the computer: ... the host has 0.6.1-dev (engine 18), you have 0.6.0 (engine 17).`,
  no data-file line on either PC; the host finished alone. (Two checks of the suite's session scenario failed there as
  expected of a mixed pair: "both copies run the build under test", and the host's slot 1 bar still read the tension
  5 after the 0.6.0 player's intelligence event had made it 6: the host doesn't redraw its bars after an event of a
  player, as before.)
- Docs: README (same game data files, the engine, troubleshooting, Upgrading 0.6.1), ARCHITECTURE (level 18, the
  data files), GAME-INTERNALS (the game's temporary files), TESTING (the scan line, `ls testdatafile`, the two-folder
  check, the `lockstep_data` row). Game open: 828 s (the suite, with the next fix) + 161 s (two folders) = 989 s.

## Fix 2026-10-25: a reserved nation is played by a person on the host too (v0.6.1)
The war line's follow-up: on the host, the tension bar of a nation reserved for a player who wasn't connected had no
"played by a person" hint, while the joined player's PC showed it (fix-war's proof: client slot 2, France, reserved for
Bot: the hint; host slot 2: none, `warline` people=1,).
- **Why.** The host asked `PlayerRulesNation`: a joined nation under the player's rules (its player's PC announced
  `rel`, or did when it was last connected: `relRules`). A nation reserved without such a player (a reservation without
  a join, a player away since before 0.5.0, an older version) followed the computer's rules, and the host's own
  `CheckForWar` could declare a war between the host and it. A joined player's PC sees every reserved or claimed nation
  as a person's (the header's `humans` and the lobby's).
- **The fix.** On the host a nation people play is one reserved or claimed by a player, connected or not
  (`hookcb::IsHumanRemoteNation`), for the bar's hint (`WarOnlyByPlayers`), the war-line chat line (`WarLineNews`) and
  the host's own player routines (`MaskAsPlayer`: CheckForWar sees its tension with the host as 5, no war plan, no
  alliance of its own; RandomTensionChanges at least 6), so the hint is true: the game declares no war between the
  host and a nation a person plays. The computer's routines (`AIMaskEnter`) still leave only the nations under the
  player's rules alone: a reserved nation without a player who runs its relations keeps its relations with the
  computer's nations as before. Log: `relations v2: CheckForWar sees nation(s) 1 2 as players (the nations people
  play)`.
- **Proof (dist_i61_1, the same suite run, 200/200).** `relations_war` 19/19 with a new check of the stand-in's
  nation 2 (France, reserved for Bot, not connected): the host's bar "Tension with France is 8 - played by a person:
  ...", the client's "Tension with France is 6 - played by a person: ..." (both at peace with it), `warline` people
  `1,2,` on the host (before: `1,`) and `0,2,` on the client; the host's log `relations v2: CheckForWar sees
  nation(s) 1 2 as players (the nations people play)` in each month, and no war, chat line or box of the month
  changed in the other scenarios (session, lockstep_pvp, twopc: all passed).
- Docs: ARCHITECTURE (B2's player routines, the war line, the hooks table), README (wars between players), TESTING
  (the `relations_war` row).

## Fix 2026-10-25: campaign divisions of joined players and duplicated divisions (v0.6.1)
Two players of 0.6.0 on two PCs reported: "Non host player sees hosts player ships on vessel overview and not their
own. Divisions seems to not exists for a non host player but seem to work and save for a host player." and "Divisions
seem to duplicate for the host on no host trial." Branch fix-div from dev-061 9a163f1 (0.6.1-dev): the fix 30b2e7e, the
review's fix edaeb16, the scenario 6bcdc08
([the proof and the scenario](#testing-2026-10-25-campaign-divisions-the-live-proof-and-the-scenario-divisions-v061)).
- **The screen.** The "vessel overview" is the strategic screen's *Fleet organization* tab, index 9 of `TabControl1`
  (Map, Ships in service, Ships under construction, Submarines, Coastal fortifications, Ships sunk or scrapped, Area
  overview, Base overview, Officers, Fleet organization): the panel `pnDivisionEditor` with the list view
  `lvDivisions`, which `FillDivisionList2` (0xd39c14) fills with every campaign division and its ships
  (`DoCampDivShipLine`), and *New division* (`bnNewDiv` -> `TdlgCreateDivision`). `TfrmCampOverview` ("Campaign force
  listing") belongs to the Steam and Iron campaign (`TfrmCampaign.bnOverviewClick`), not to the strategic game.
- **The game's model** (static reading of 1.01.44;
  [GAME-INTERNALS](docs/GAME-INTERNALS.md#campaign-divisions-the-fleet-organization-tab)). The game keeps one division
  list, its player's: `TfrmBuildCamp.CampaignDivisions` (+0xaf0). The save keeps it in the slot's `.off`,
  `[CampaignDivisions]`: `CampDivNo`, then `CampDiv<i>CDivId`, `Name`, `Abbreviation`, `DivisionType` (CA 3, DD 9,
  ...), `DivisionExperience`, `Training`, `Commander`, `LeadDivision`, `Role`, `ShipCount` and `ShipId<j>` (`Commander`
  and `LeadDivision` -1 = none, booleans 0/1), next to `[Officers]` (`PlayerNation.OfficerList`, written by
  `SaveBuildCampaign` at 0xc8f20d). `LoadBuildCampaign` sets `PlayerNation := BuilderNations[0]` (0xc9a69d), loads
  `[Officers]` into `PlayerNation.OfficerList`, then calls `LoadCampaignDivisions(CampaignDivisions, file,
  'CampaignDivisions')` (0xd37054), which appends without clearing. `TCampaignDivision.LoadMe` (0xd607ac) takes the
  commander from `PlayerNation.GetOfficerWithId` and adds `FindCShipWithId(PlayerNation, id)` to the division's ships
  without a nil check (0xd60a96). So on a joined player's PC the month's `.off`, the host's, resolved against nation
  0: the host's ships. *Add ships to division* (`AddShipToDivision`) offers PlayerNation's ships (seen live: Germany's
  cruisers); *Ships in service* lists PlayerNation's ships with their division
  (`GetCampaignDivisionForShip`). `TCampaignDivision.Create` takes `CDivId` from the campaign's `IDNo` counter
  (0xd79de4): unique in a campaign the game saved. The battle generator (the quick-battle unit, the callers of
  `GetRelatedDivision`) builds the player's side from the player's campaign divisions (seen live, below).
- **The mod up to 0.6.0.** The month package carried the host's whole slot, its `.off` included; a turn carried only
  nation K's `.bcs` sections, its designs and its map data, no `.off`. The host merged the turns and reloaded its slot
  in place every month with a submission; the client reloaded slot 77 in place every month.
- **Reproduced on the released 0.6.0** (dist_rc060_2; three runs, game open 353 s): the host's two divisions ("Host Div
  A" and "Host Div B", two British ships each) written into slot 1's `.off`, the client's made with the game's own
  dialogs (posted messages). The client's *Fleet organization* showed the host's two divisions with British ships while
  its *Ships in service* showed 30 German ships. The client's divisions ("K Div 1" and "K Div 2"; ships added from an
  *Add ships to division* dialog that offered Germany's cruisers, one renamed, one dissolved) never reached the host and
  were gone from slot 77 after a month; they stayed on the client's screen only because its list was never cleared.
  The host's divisions doubled with every merged month, 2 -> 4 -> 8, in its `.off`, its `Autosave.off` and on its
  screen (the month's "Some ships or divisions have no commander assigned ..." question gave the copies admirals); the
  client's screen list grew 4 -> 8 -> 16. No exception in either log.
- **Causes.**
  - *The host's divisions on the joined player's screen:* the month's `.off` was the host's, and every load resolves
    the divisions against nation 0, the host's nation in slot 77.
  - *The joined player's own divisions lost:* nothing carried them to the host, and the next month's `.off` replaced the
    slot's.
  - *Duplicates:* `LoadCampaignDivisions` appends and the game loads a campaign once per process, but the mod reloads
    in the same process (the host after every merge, the client every month, the battle slots, the live battles), so
    each reload added the save's divisions to those in memory and the next save wrote them all: a third appended list
    after the intel reports (0.2.2) and the air units (0.3.3).
- **The fix** (`game.cpp`, `saveio.cpp`, `nationplay.cpp`, `session.cpp`, `bridge.cpp`; the design in
  [ARCHITECTURE](docs/ARCHITECTURE.md#each-joined-players-campaign-divisions-061)):
  - *Every load* (single player too): the `LoadBuildCampaign` hook takes the divisions out of the list before the load
    and frees them after a successful one (the *Fleet organization* list view, whose rows point at them, is filled
    again first with the game's `FillDivisionList2`); a failed load that left the list empty gets them back. A hook of
    `LoadCampaignDivisions` drops repeated `CDivId`s after the call (the first kept, `LeadDivision` links pointed at it:
    the repair of saves an earlier version wrote) and packs nil ships out of every division.
  - *A joined player's own slot:* while the mod loads slot 77 as nation K, the hook makes `PlayerNation` nation K's for
    the `LoadCampaignDivisions` call and puts it back (also after an exception), so the divisions find K's ships and
    officers.
  - *The turn* carries `divisions.ini`, the slot `.off`'s `[CampaignDivisions]`. The host validates it (only the game's
    fields, integers but `Name` and `Abbreviation`, at most 200 divisions of 40 ships; a rejected list keeps the stored
    one), drops repeats and keeps it in `rtw3mp_nations.ini` `[Nation<K>Divisions]`. The host's save and month never
    see it.
  - *K's month package:* its `.off` gets K's stored divisions pruned to K's ships of the month (header `divs=1`) for a
    client whose hello has `div` >= 1; an older client gets an empty list. Every other byte of the `.off` stays.
  - *K's battle package* (a battle against the computer) gets K's divisions for every client version (header `divs=1`;
    the battle slot's `MakeNationPlayer` keeps the divisions whose ships are all K's).
  - *A client of an older host* (no `divs`) writes its own divisions from slot 77 into the month's `.off` (when the slot
    holds this nation's game of the month before; divisions with other nations' ships left out) and into its battle
    slots. A client whose division hook is unavailable (no hello `div`) writes an empty list instead (edaeb16, from the
    review): its load would resolve the nation's ship ids against nation 0 and leave nil ships that the *Fleet
    organization* dereferences.
  - Protocol 3 and live-battle engine 17 unchanged (new keys and a new file that older peers ignore). Bridge: `divs`,
    `divstore <K>` (read-only, [TESTING.md](docs/TESTING.md#bridge-command-reference)).
- **Version mixes** (from the code; not run with mixed versions).
  - 0.6.1 host, 0.6.1 player: the whole round trip.
  - 0.6.1 host, 0.6.0 player: the player gets an empty division list, no longer the host's divisions and ships. Its own
    divisions are not sent and are gone from its slot every month (its 0.6.0 game still appends on its own reloads).
    The host's divisions never double. The player's battles against the computer get what the host stored for it:
    nothing, from a 0.6.0 player.
  - 0.6.0 host, 0.6.1 player: the host still doubles its own divisions (0.6.0's code). The player drops the host's
    divisions and keeps its own from its slot, pruned to its ships, in its months and its battle slots; they never
    reach the host.
  - 0.6.0 host, 0.6.0 player: as reported.
  - A save that 0.6.0 doubled is repaired when 0.6.1 loads it (the first division of each `CDivId` stays). A joined
    player's divisions that 0.6.0 lost can't be recovered: the player makes them again.
- **Not covered.** A joined nation's live battles: the host's game generates them, the nation is a computer nation
  there, and the game uses campaign divisions for its player's side only. What the host's month and battles would
  change in a joined nation's divisions (experience from battles, the monthly division training and experience) is not
  applied to them: they stay as the player's last turn sent them. A player who reconnects before the merge gets the
  stored list again (division changes not yet merged are lost, like other orders).
  *Note 2026-10-25 (docs, from the code, not run):* only the live battles with the host's nation are generated by the
  host's game. A live battle between two joined players is generated in its first player's battle slot (`SendBattle`
  with `pvp=1`, whose package carries that player's divisions like any battle package, then `MakeNationPlayer`), so
  there only the second player's divisions go unused.

## Testing 2026-10-25: campaign divisions, the live proof and the scenario divisions (v0.6.1)
dist_fd_2 (edaeb16: 0.6.1-dev, protocol 3, live-battle engine 17; RTW3MP.dll 6E601883E5F01B08..., version.dll
F153C56827DEA9BB...), installed from its frozen snapshot, the released 0.6.0 (dist_rc060_2) put back after every run;
host and client on one PC with the 1900 test campaign (the host Great Britain in slot 1, the client Germany as nation
1). Slot 1 and the Preferences identical to their backup after every run (hash), Game77 and Game87 removed.
- **The live proof** (one run, game open 212 s; slot 1's `.off` given the host's two divisions "Host Div A" and "Host
  Div B" twice, as a 0.6.0 month leaves them):
  - a) *Session start:* the client's `divs` n=0 nation=1, slot 77's `.off` `CampDivNo=0` and its *Fleet organization*
    empty, while the host's showed its two ("divisions of nation 1: 0 in its month (none stored yet)").
  - b) *The joined player's own divisions:* the client made "K Div 1" (CA Hertha and Victoria Louise) and "K Div 2" (B
    Brandenburg and Wittelsbach), renamed "K Div 2R", with the game's own dialogs (the ship picker offered Germany's
    ships). After each of three months (Sep, Oct and Nov 1900): the client's `divs` n=2 nation=1 with the same ship ids
    (CDivId 60561 and 60562: nation 1's id range of the month package), slot 77's `.off` `CampDivNo=2`; the host's
    `divstore 1` the same two; the host's `divs` exactly its own two, its `.off` and `Autosave.off` `CampDivNo=2`; each
    copy's *Fleet organization* its own two. Logs: "divisions of nation 1: 2 stored", "divisions of nation 1: 2 in its
    month (34 ships of nation 1; 0 ship id(s) removed ...)", "divisions: the month holds nation 1's own 2", "campaign
    divisions: 2 of nation 1 loaded", "campaign divisions: 2 from before the load dropped; the campaign's 2 kept",
    "campaign divisions: the Fleet organization list filled again". No exception, access violation, game error box or
    "Missing ship".
  - c) *The repair:* the host's load of the doubled `.off` logged "campaign divisions: 2 repeated divisions of the save
    dropped (4 -> 2; a save of an earlier version)", and its next save wrote 2.
  - d) *A battle against the computer:* Germany against Spain in Northern Europe in the third month, fought (a second
    one the enemy declined): the host's "divisions of nation 1: 2 in its battle (...)", the client's "player copy in
    slot 87: ... divisions 2 kept / 0 dropped", Game87's `.off` `CampDivNo=2`, and in the battle `ls forces div` listed
    "K Div 1" as side 0's division 0: the game's generator built the player's side from it.
- **The scenario `divisions`** (6bcdc08; [TESTING.md](docs/TESTING.md#scenario-catalog)): in the default run after
  `battles`, two processed months, about 90 s. The host and the client make two divisions each with the game's dialogs
  (New division, Add ships to division, one Rename division on the client; posted messages only). Checks: the client's
  hello "campaign divisions 1"; the host's divisions never on the client (`divs`, its list); the client's two with
  nation 1's ships only; after each month (the first with the *Fleet organization* shown, so the list is filled again
  inside the load) the client keeps exactly its own two (`divs`, its list, slot 77's `.off`), the host stored them
  (`divstore 1`, the log lines) and keeps exactly its own two (`divs`, its list, slot 1's `.off` and `Autosave.off`
  `CampDivNo`: no duplicates after its reload); no exception, access violation, "Error in FillDivisionList2" or
  "Missing ship" in either log or box. 18 checks + 2 x the month checks = 42. New parsers `parse_divs` and
  `off_divisions` with their self-tests: `regression_selftest.py` 133/133.
- **The suite** on dist_fd_2, `--only session,battles,lockstep_month,divisions`: session 17/17, battles 90/90,
  divisions 42/42, lockstep_month 51/51 = **200/200** (game open 486 s).
- Game open 353 (the 0.6.0 reproduction) + 212 + 486 = 1051 s.

## Testing 2026-10-26: the 0.6.1 release candidate dist_rc061_1 (v0.6.1)
The release candidate dist_rc061_1 (a2d918d: 0.6.1, live-battle engine 18, protocol 3; RTW3MP.dll SHA-256
08E1AE99C7DC2B35..., version.dll 650AD2502DB71783...) on the reg-061 runner (a2d918d plus the suite changes below),
installed from its frozen snapshot (`--dist`), the released 0.6.0 (dist_rc060_2) put back after every run (hash
checked); slots 1 and 9 and the Preferences identical to their backup after every run (hash), the save folders as before,
no game folder copy, delay proxy, `python` or `RTW3.exe` left. Both logs of every run after the first are kept with the
developers' session files (`r61\logs_run_*`).
- **Runs** (checks, seconds with the game open):
  - The default run (29 scenarios): 1183/1189 (2456 s); its five failing scenarios are below.
  - `--stress` (35 scenarios, with the engine-18 live scenarios `lockstep_limits`, `lockstep_rejoin`, `lockstep_resume`
    and `relations_war`, `battle_generator`, `player_copy`, `lockstep_pvp`, `lockstep_air`): 1514/1515 (6055 s); the one
    fail is `player_copy` f) (suite, fixed); `--only relations_war,player_copy` with the fix: 74/74 (106 s).
  - `--group relations` (the host now counts the reserved nation 2 as a player in its CheckForWar): 295/295 (473 s), then
    318/321 (646 s): the game's DoBattleBetweenAINations box, below.
  - `--stress --only lockstep_month`: 99/99 (294 s).
  - With the game open: 2456 + 6055 + 106 + 473 + 646 + 294 = 10030 s, and 767 s for the mixed cases.
- **A game error box in the second relations run: "Error in DoBattleBetweenAINations procedure! Access violation at
  address 00603D64 in module 'RTW3.exe'. Read of address 000000AC"**, on the host, during `battle_generator` e)'s TEST
  command `battles aitest 1 7 Northern Europe` (`ResolveAIBattle`: the game's `DoBattleBetweenAINations` of nation 1
  against Spain now, with the counts 3/3). The game's own except handler showed it (a TMessageForm, which the routine's
  text capture does not take), so the command timed out (`error timeout (main thread busy)`; nothing in the host log from
  03:27:33 to the next month) and the next scenario's answerer answered it (`battle_surprise`'s box check and `session`'s
  whole-run check fail with it). In that run the scenario's three months had no fought battle (every request avoided or
  declined: `c) fought: None`); in the three runs where e) passed (`ok fought=1 captured=1`: default, `--stress`, the first
  relations run) a battle had been fought before. Not reproduced, no `RTW3MP_DIAG` in the run. Per the release rule a
  NO-GO until investigated: the TEST command's fixed counts may exceed the forces in the area (a test artifact), but the
  same routine resolves a declined kind-0 battle in the battle phase with the counts of the game's request
  (`ResolveAIBattle`'s caller in session.cpp). Not new in 0.6.1 (the path and the check date from 0.6.0). Both logs:
  `r61\logs_run_20261026_033126`.
- **The default run's other fails**, by class:
  - `battle_generator` (and `session`'s whole-run check): the host's "Error in SaveBuildCampaign procedure! Cannot create
    file "...\Save\Game1\RTWGame1.bcs". The process cannot access the file because it is being used by another process"
    at its month-end save, one second after `battle 24 applied` (M2). Environmental: a file-sharing collision (the game
    creates its save without sharing; the runner reads slot 1 right after each `battle N applied`; the save folder is in
    OneDrive); the run's logs were not kept. Not seen again: the scenario passed in `--stress` and in both relations
    runs (TESTING.md, Known traps).
  - `relations_war` M1 (two checks): the host's intel list and save repeated "Intelligence report Italy: The Italian navy
    is reportedly doing a lot of night training." (two adjacent copies in one month; not the gun comparison the checks
    allow). No routine of the mod makes intelligence reports on the host (the joined player's monthly check runs on its
    PC), so the game's own spying; flaky: 19/19 in `--stress`, in the re-run and in both relations runs.
  - `player_copy` f): `nations` had Russia's funds 9646 in the copy and 9694 in the client's memory. Suite:
    `relations_war` (new in 0.6.1's suite, right before) ends with `peacetest` on the client after slot 77's save, and the
    game's peace changes the funds of the nations it touches (`--stress`: Russia 13480/13420 and Japan 3324/3315, the
    nations of the peace's delta; the re-run: France, Japan and Italy, whose scrapped ships its turn report lists). The
    copy is made from slot 77, so f) now compares the other nations' funds with slot 77's `[NationJ] Funds` (a NOTE lists
    the nations whose memory differs): 74/74.
  - `lockstep_air` g): a formation B readied stayed at status 1 and left B's list at minute 37 before the Stand down
    could run: the battle's own course (flaky); `--stress` 41/41 (the Stand down through the real dialog passed).
- **Campaign divisions in a live battle between two joined players** (a docs question, `lockstep_pvp`'s new NOTE):
  CONFIRMED in the `--stress` run. Battle 28 is set up on the attacker's PC (A, the client, nation 1 Germany, side 0)
  and relayed to the defender (B, the third copy "Bot", nation 2 France, side 1): A's campaign divisions "RT K Div 2R" (2
  ships) and "RT K Div 1" (1 ship) are battle divisions of side 0 on both copies; B's "RT Bot Div" is nowhere, its side has
  the game's own divisions ("France Battle Division 2", "France Cruiser Division 4", ...). The battle's 26 divisions
  (index, side, name) are the same on both copies (the new check).
- **Suite changes** (reg-061, selftest 134/134): ac4b2db `battle_dialogs` records the game's error boxes answered in a
  joined player's battle (the mixed run's 0.6.0 player boxes had passed unnoticed); 9c85a02 `lockstep_pvp`'s campaign
  divisions (above); 9eb0b23 `player_copy` f) against slot 77 (above).
- **Mixed versions** (TESTING.md, "Mixed versions"): a) a 0.6.1 host with a 0.6.0 player 91/91 (190 s): the battle against
  the player fought against the computer with both notices naming the engines (18 and 17), the old player's divisions
  stay on its PC (`divstore 1` empty, no duplicate on the host); the 0.6.0 player's own boxes in its battles against the
  computer after it made a division (CreateDivisionForBCBattle and SelectForce access violations, "Missing ship in RT K
  Div 1") are 0.6.0's: the same with both PCs on 0.6.0 (control z: 46/48, 199 s), fixed by 0.6.1 (0.6.0 players:
  upgrade). b) a 0.6.0 host with a 0.6.1 player 89/89 (183 s) and b2 57/57 (151 s): the player keeps its own two
  divisions in `divs`, slot 77 and its battle slot. c) two 0.6.1 folders with different `Data\tmpShip.ttp`: 24/24 (44 s),
  the data files the same, 4/4 minutes equal.
- **Verdict**: every scenario passed in at least one run; the suite fails were fixed and re-run, the default run's
  other fails were flaky or environmental. The DoBattleBetweenAINations box (a game error box) blocks the automatic
  release until the developers have looked at it. (Cause and fix: [Fix 2026-10-26, below](#fix-2026-10-26-battles-the-game-resolves-for-joined-players-no-battle-when-a-side-has-no-ship-there-that-can-fight-v061).)

## Fix 2026-10-26: battles the game resolves for joined players: no battle when a side has no ship there that can fight (v0.6.1)
The 0.6.1 release candidate's second `--group relations` run (MODLOG "Testing 2026-10-26: the 0.6.1 release candidate
dist_rc061_1") showed the host's "Error in DoBattleBetweenAINations procedure! Access violation at address 00603D64 in
module 'RTW3.exe'. Read of address 000000AC" during `battle_generator` e)'s `battles aitest 1 7 Northern Europe`.
Branch fix-aib from int-061 cc955eb.
- **The address.** The box's address is absolute, and RTW3.exe has ASLR: that run's start lines say `random=000E7834`
  (RVA 0x7834), so the base was 0xE0000 (not 0x400000) and the fault RVA 0x523D64: in the analysis image (0x830000)
  0xd53d64, the first instruction of `TCampaignShip.IsOperational`, `movzx edx, byte ptr [eax+0xac]` (`Status`), with
  Self (eax) nil.
- **The routine.** `DoBattleBetweenAINations(NationA, NationB, ShipCountA, ShipCountB, anArea)` 0xd32104 plays about
  min(ShipCountA, ShipCountB) rounds (3 above 6; the exact rule in GAME-INTERNALS); each round picks one side's ship
  (50 %) with its nested picker 0xd31f78: `GetRandomShipInArea(anArea, nation, [ssActive, ssTradeProtection, ssRaider,
  ssColonies])` (set word 0x0059), then `IsOperational` on the pick without a nil check (0xd31fa4).
  `GetRandomShipInArea` 0xd10d60 returns nil when the nation has no ship in the area with that status that is
  operational (in play, `RepairTime` 0, not interned or a museum): the AV. The routine's except handler (0xd3270a)
  shows the box, a modal TMessageForm, so the host's main thread waits for an answer (the run's `battles aitest` timed
  out after 120 s). When the picks happen to take only the side that has ships, the routine "fights" the battle: the
  other side loses ships to a fleet that isn't there.
- **The game's own call never meets it.** Its only caller, `CheckAivsAIBattleInArea` 0xd32c2c (from
  `CheckAIvsAIBattles`), counts both nations with `CountShipsInArea(stAll, anArea, [the same set])` (0xc5d958) and calls
  the routine only when both counts are above 0, with those counts. Neither sets any other field: the routine and its
  nested procedures never read `BattleArea` (+0x4a3b) or `BattlePossession` (+0x4a36), so 0.6.0's clearing of both
  before every load (6bbca05) has nothing to do with it.
- **The mod's calls.** `game::ResolveAIBattle` called the routine with counts it was given: the battle phase's fallback
  (session.cpp `FinishBattlePhase`) for a kind-0 battle that wasn't fought (`BattleFallback`: the time limit, the player
  disconnected or not connected, a decline from an older version, "no battle came about" on the player's PC, a battle
  that could not be sent, built or applied, `battles resolve`), with the counts of when the game found it, and `battles
  aitest` (0.6.0, risk 4) with 3/3. Neither looked at the ships in the area at that time.
- **Normal play reaches it (since 0.3.0, f6ee827, the fallback).** The game finds a battle with ships of both sides in
  the area, but the fallback runs after the month (`CheckRaiderActivity`, the rest of `DoStrategicTurn`) and after the
  battle phase, whose fought battles are merged into the save before it. A side can be without a ship that can fight
  there by then: sunk or damaged (`RepairTime`) in another battle of that month in the same area (a second battle of
  the player's nation there, the game's own battle of the computer nation against a third nation, another joined
  player's battle against it, an earlier fallback of the same battle phase), or by raiders later in the month. And "no
  battle came about" on the player's PC, the commonest fallback, is just what a player's nation without a ship that can
  fight there gets. The test path since 0.6.0: `battle_generator` e) takes an area where nation J has a possession and
  nation 1 at least 4 ships; J's ships are there only in some months (in the failed run every battle there was avoided
  or declined; in the three passing runs a battle had been fought there).
- **Reproduced live** (the RC dist_rc061_1, the probe `aib_probe.py` with the suite's helpers in the developers'
  session files: the session scenario, then one month; 57 s with the game open): `battles aitest 1 5 Northern Europe`
  (Japan has no ship there, Germany 29) showed the box 3 times out of 3 and `battles aitest 1 5 Southeast Asia` (Germany
  none there, Japan 4) 2 of 2, each command held until the probe answered the box; after the month 2 of 3 (the first
  "fought": the picks took only Germany's side). The real fallback: two kind-0 battles injected in Southeast Asia
  (`battles inject 1 5 Southeast Asia`; nation 1 has no ship there) came back from the player's PC as "no battle came
  about" (the game there found no battle), and the host's fallback had the routine resolve them after the battle
  phase: one showed the box on the host ("battle 2 resolved by the game: no battle"), the other was "fought" ("battle 1
  resolved by the game: battle": Japan's ships against a German fleet that isn't there). `battles aitest 1 7 Northern
  Europe` (both nations have ships there) fought, its box captured, no error box.
- **The fix** (e80e507): `ResolveAIBattle` counts both nations the game's way (`TBuilderNation.CountShipsInArea(stAll,
  area, [ssActive, ssTradeProtection, ssRaider, ssColonies])`, new `FightingShipsInArea`). Either 0 (or the count not
  available): no battle, the routine not called, host log `ResolveAIBattle K-J in <area>: no battle (ships that can
  fight there: a/b; when found c/d)`; else the routine gets the counts of now, as from the game's own caller (log `...
  ships that can fight there a/b (when found c/d)`). The fallback's chat line stays ("* The battle of ... was resolved
  by the game (...)"), with no result text when there was no battle. 0.6.0's clearing before every load stays.
- **Suite** (0d17a7d): `battle_generator` e2) `battles aitest 1 J <area>` where J has no ship that can fight (the
  host's save; new helper `fighting_ships_by_area`): `ok fought=0 captured=0`, the log line, no game error box. Check e)
  depended on where J's ships happened to be. Selftest 134/134.
- **Proof** (dist_ab_1 = 0d17a7d, SHA-256 RTW3MP.dll 352905F2..., version.dll B9DBE5E3...): the probe 37/37 (163 s
  with the game open): `battles aitest 1 5 Northern Europe` 3 of 3 and `... Southeast Asia` 2 of 2 `ok fought=0
  captured=0` with "no battle (ships that can fight there: 29/0; when found 3/3)" and "0/4", after the month
  3 of 3 (28/0); the two Southeast Asia fallbacks (no battle came about) "no battle (ships that can fight there: 0/4;
  when found 3/3)"; a battle against Spain in Northern Europe held past the time limit (the time-limit fallback, ships
  on both sides) resolved with "ships that can fight there 29/16 (when found 3/3)", its result captured; `battles aitest
  1 7 Northern Europe` fought (29/15, 28/15). No error box. The suite: `--group relations` 321/321 (526 s) and 309/309
  (575 s), `--only battles,battle_generator` 168/168 (440 s). e) fought in all three (Spain had ships in Northern
  Europe); e2) `battles aitest 1 7 Northeast Asia` "no battle (ships that can fight there: 1/0; ...)" in all three.
  `battles`: five battles held past the time limit in Northern Europe resolved with 28/17, 27/16, 26/15, 26/15, 26/14
  (each resolution sinks ships, so the counts fall: how a later fallback in the same area can find a side with none),
  and c2)'s three declines under an emulated older host with 26/13, 24/13, 23/12; no box. After every run v0.6.0 was put
  back, slots 1 and 9 and the Preferences were identical to their backup, no `RTW3.exe` left. With the game open: 57 +
  163 + 526 + 575 + 440 = 1761 s.
- **Compatibility**: only the host resolves these battles, so a host on 0.6.1 is enough; protocol, engine and
  saves unchanged.
- Docs: CHANGELOG (0.6.1 Fixed, Compatibility), README (battles against computer nations), ARCHITECTURE (the game's
  resolution in the battle phase), GAME-INTERNALS (the routine's precondition, absolute addresses in error boxes),
  TESTING (`battles aitest`, e2), the trap).

## Testing 2026-10-26: the 0.6.1 release candidate 2 dist_rc061_2 (v0.6.1)
The second release candidate dist_rc061_2 (int-061 c7955f9: 0.6.1, live-battle engine 18, protocol 3; RTW3MP.dll
SHA-256 8D70CE59B0199F80..., version.dll 38556CF04D05A6E2...): RC1 with the fix above (e80e507) and `battle_generator`
e2) (0d17a7d). Run on the reg-061b runner (c7955f9, no suite change), installed from its frozen snapshot (`--dist`),
the released 0.6.0 (dist_rc060_2) put back after every run (hash checked); slots 1 and 9 and the Preferences identical
to their backup after every run (hash; the backup equal to RC1's), the save folders as before, no game folder copy,
delay proxy, `python` or `RTW3.exe` left. Both logs of every run are kept with the developers' session files
(`r63\logs_*`).
- **Runs** (checks, seconds with the game open):
  - The default run (29 scenarios): 1133/1135 (2112 s); its two fails are below.
  - `--stress --only lockstep_month`: 99/99 (464 s): the host's battle against the player in Aug 1900 agreed at minute
    417 (14/14), the one in Sep 1900 ended soon at minute 7 (16/16).
  - `--group relations`: 321/321 (520 s).
  - `--only lockstep_limits,lockstep_data` (the re-run): 83/83 (206 s).
  - Mixed versions a): 78/78 (180 s), below.
  - With the game open: 2112 + 464 + 520 + 206 + 180 = 3482 s.
- **No game error box** on either PC in any run: the runner's box checks passed, and the run outputs and every kept log
  have no "Error in", "Access violation", "Integer overflow" or "Missing ship" but `battle_generator` b)'s own `battles
  gen 1 fault` (decision 3: the box recorded, none shown).
- **The fix at work**: `battle_generator` e) `battles aitest 1 7 Northern Europe` fought (`ResolveAIBattle 1-7 in
  Northern Europe: ships that can fight there 25/3 (when found 3/3)`; the relations run 32/4), e2) `battles aitest 1 7
  Northeast Asia` gave no battle and no box (`ResolveAIBattle 1-7 in Northeast Asia: no battle (ships that can fight
  there: 1/0; when found 3/3)`) in both runs; the default run's `battles` had two more resolutions by the game (a
  battle left at the time limit among them), both fought (27/6, 25/2).
- **The default run's fails**, flaky (a battle condition of the game): `lockstep_limits` (8/9: its live case did not
  start) and `lockstep_data` b) (8/9): the host's `pocbattle 1 0` got "Enemy unable to challenge our forces in the area!
  Gain 102405 VP!" (DoBattle returned 0: no ship of nation 1 for the test battle) right after `lockstep`'s and
  `lockstep_ui`'s test battles (Great Britain's 30 and 26 ships against Germany's 11, none sunk in the live minutes;
  `lockstep_ui`'s host ends its battle alone on auto-run). The test battles' results stay in the host's campaign in
  memory (TESTING.md, Known traps: seen before in `--stress` only); the campaign was at March 1903 (RC1's default run, at
  April 1903, started both). The re-run passed: `lockstep_limits` 55/55 (its live case 47/47), `lockstep_data` 11/11.
- **Mixed versions** (TESTING.md, "Mixed versions"; `mixed_061.py --case a --no-battle87` in the developers' session
  files `r63`): a 0.6.1 host with a 0.6.0 player in a folder copy outside the Steam library (deleted afterwards): 78/78
  (180 s). The join line `Captain: mod 0.6.0, live-battle engine 17 (this host: 18), ..., campaign divisions 0`; the
  host's battle against the player in Aug 1900 fought against the computer (`battle against Captain: fought against the
  AI (live-battle engine level 17, the host's 18)`), the host's chat "* The battle against Germany is fought against the
  computer: live battles need the same live-battle engine on both PCs: this host has 0.6.1 (engine 18), Captain has
  0.6.0 (engine 17)." and the player's notice "... the host has 0.6.1 (engine 18), you have 0.6.0 (engine 17)."; nothing
  held or sent; nothing stuck (the month completed Aug -> Sep 1900 with the player's turn merged, the player plans again
  in slot 77, no battle window, the strategic screen enabled); the peace month and two division months as in RC1; no
  game error box, exception or data-file "differ" line. The battles on the 0.6.0 player's PC (`battle87`) were left out:
  after it made a division they show 0.6.0's own boxes (RC1's control z).
- **Verdict: GO** for publishing 0.6.1 with exactly this build: every scenario passed (the two flaky fails when run
  again), no game error box anywhere, the mixed versions as designed.

## Feature 2026-10-25: the legacy fleet for joined players (v0.7.0-dev)
Request: "Manual build for legacy fleet should be available to the non host player." In the game, the title screen's
**Manual build of legacy fleet** lets the player build nation 0's starting ("legacy") fleet by hand when a campaign is
created; a joined player claims a nation whose fleet the game generated. Branch feat-legacy (from dev-061 9a163f1).

- **The game's manual build (static, 1.01.44; GAME-INTERNALS
  [the legacy fleet](docs/GAME-INTERNALS.md#the-legacy-fleet-the-manual-build-of-the-starting-fleet)).**
  - `lbLegacyFleetClick` (0xbcc090) toggles `cbManualFleetBuild` (+0x4ac).
  - The unnamed new-game routine (0xbc8f68, from `bnSlotOKClick`) gives every nation `Funds = 4 x GetModifiedBudget()`
    (`shl esi,2`, 0xbc963c) and `StartBuild` 0, and branches on the box at 0xbc994f.
    - Automatic: `AIBuildInitialFleet` for nations 0-8, then `AIMonthlyBuild` for the ships under construction.
    - Manual (0xbc9de7), for `PlayerNation` only: prestige - 1; `Funds = trunc(round(Funds x 0.9) x IFM / 10)` (the
      constant at 0xbca180 is 0.9; IFM is `InitialFleetModifier`, from the nation's data, not the save); captions; the
      build-year global 1899 (cell 0xf27238 -> 0xf52ff0); the picture message 'Build your existing fleet'.
  - The player then designs and builds with the normal screens. `LayDownShip` in sbBuild makes complete ships and
    takes their cost. Its funds check holds only for PlayerNation while the title box is ticked (0xd10e20).
  - `btbnTurnClick`'s two start-build steps (0xd57994):
    - "finished?": `cap = (FleetSize+1) x 3000`, AssignOfficers, then `AIBuildInitialFleet` **for list indices 1-8**.
    - "ships under construction" (cost / 20): `AIMonthlyBuild` 1-8, then SetButtonsToNormal.
  - In single player the AI reacts to the manual fleet (0xca37ea: extra CL/CA when the player has many).
  - StartBuild is not saved; `LoadBuildCampaign` -> `SetButtonsToNormal` ends a start build.
- **Design (agreed 2026-10-25).**
  - The host's option **Manual legacy fleets** (default on); the player's choice **Build my legacy fleet by hand**
    (default off, read when the first month arrives: no dialog, so the 1935 fixture is never offered by surprise).
  - Only in the campaign's first month (Year = CampaignStartYear, Month 1), once per nation (`klegacy`).
  - On the player's PC, in the player's own view: the game's builder with its own dialogs.
  - The fleet reaches the host as an ordinary turn, marked `MPLegacy`. New header key `legacy`, new `MSG_LEGACY` (41)
    for notices; protocol 3.
- **Built** (ARCHITECTURE [the legacy fleet](docs/ARCHITECTURE.md#the-legacy-fleet-of-joined-players-070)):
  - saveio: `ClearNationFleet` (K's ships, ShipCount 0, an empty `DesignFilesK.des` "<version>\r\n0\r\n" with
    DesignIDCount kept, K's air units on those ships), `DropAirUnitsOn`, the legacy merge's air-unit cleanup.
  - game: `EnterLegacyBuild` repeats the manual branch.
    - By name: `lbWar`, `InitialFleetModifier`, `cbManualFleetBuild`, `TCustomCheckBox.FState`, `GetModifiedBudget`,
      `TControl.SetTextBuf`/`Show`, `ShowPicMessage`, `SetButtonsToNormal`.
    - By pattern: the build year from LayDownShip (`A1 var 89 86 E4 00 00 00`), FleetSize from btbnTurnClick
      (`A1 var 40 69 F0 B8 0B 00 00`), the picture dialog's cell before the call to ShowPicMessage
      (`A1 cell 8B 00 B9 .. BA ..`).
    - While StartBuild < 2, `AIBuildInitialFleet`, `AIMonthlyBuild` and `AssignOfficersToUncommandedShips` are skipped
      (g_skips). The two new entries never skip otherwise.
  - session: the offer, the clear before the load, the start after it, the Turn button passed to the game while
    building, the event roll held back, forced submits, notices, `klegacy`.
  - ui: the two checkboxes. bridge: `legacy`, `legacyallow`, `legacystate` and the read-only `vcl` probe (a form's
    published control: its window or its parent's and its rectangle), for driving the game's own screens in tests.
- **Gotchas.**
  - The steps' loops go by list index, and K is not swapped to 0, so the skips are required: without them,
    "Continue" would give K a second, generated fleet.
  - The client's copy's officers are the host's: `AssignOfficersToUncommandedShips` would have put host officers'
    ids into K's ships.
  - `ShipKey` gives the field as "Id=value": a first `ShipIdsOf` compared `== "Id"` and found nothing (merge line
    "0 ship(s) -> 0", live run 1); fixed and covered offline (1935: France's 10 of 232 air units dropped on both sides).
  - The designer's Auto design asks "Auto design ship of type DD - Destroyer?" (OK). Its battleship ("B") design fails
    its report ("Too many and/or too heavy secondary guns for displacement!"); DD, CL and CA pass.
  - "Save and finish" -> the design report (TdlgTotalLog) -> "go to the build dialog?". An answerer that presses any
    OK also presses the build dialog's OK (one more ship): answer by class.
  - The suite's `wait(..., pids=...)` answers the picture message. Its `u.SendMessageW` has no argtypes (a 64-bit
    buffer address overflows): use `_u32p`.
  - A host hosts only slots 1-49 (so slot 46, not a hidden slot over 49); 48 is the missile fixture's, 49 air_units'.
  - The first month's event roll waits for the build (its effects would otherwise be on the generated fleet).
  - The player's Submit then asks the game's "There are insufficient ships on foreign stations! Continue anyway?".
- **Differences from single player (accepted).**
  - The AI's fleets were built with the campaign and don't answer the hand-built one.
  - K's ships have no commanders (as every joined nation's).
  - 1920/1935: finished carriers start without air groups. The reminder "... has no air units assigned! Do you want to
    go to the air groups screen?" is the host's own `PlayerChecksBeforeTurn`. A joined player's Submit asks only trade
    protection and foreign stations (`nplay::ClientTurnWarnings`), and air units are not part of a turn. So no: the
    reminder doesn't let the player fix it (static). Carriers laid down get their first air groups at launch from the
    game (README).
- **The legacy test campaign** (TESTING [the legacy test campaign](docs/TESTING.md#the-legacy-test-campaign)): a new
  1900 campaign of Great Britain made with the title screen through posted messages (`vcl`) in the free slot 3, saved,
  copied to the developers' session folder and removed (the save folder compared before and after: unchanged).
- **Live.**
  - Run 1, two copies, slot 48:
    - The client cleared "43 ship(s), 19 design(s), 0 air unit(s)". Budget 132840 x4 x0.9 x IFM 10/10 = 478224
      (was 9928), prestige 20 -> 19.
    - It built 5 DD, 2 CL and 4 CA (B failed, above). Continue: funds 15000 = (4+1) x 3000, "AIBuildInitialFleet
      skipped" for nations 1-8. Then 1 CA under construction (-1601), Continue: "built: 12 ship(s), funds 13399".
    - Host merge: "4 sections, IDNo 601/20625". The merged save's Nation1Ships, CoastalArtillery, Submarines and
      Nation1 (without the host's keys) and DesignFiles1 equal the client's. Every other section is as sent, except
      [General] IDNo.
    - February: 12 ships on both, no second offer, `klegacy=1,`. Game open about 9 min.
  - Suite `--only legacy_fleet`:
    - Runs 1 and 2 failed on the suite's own driving (above).
    - Run 3: **38/38** (legacy_fleet 21/21). Germany 8 ships (ids 20601-20607, 20620), funds 12381. The merge "43
      ship(s) -> 8". A battle against France in Northern Europe fought on the client with the hand-built ships, none
      of the 43 generated ids back. Game open 154 s.
  - Broader runs:
    - `--only session,merge,events,design_study,battles,player_copy,air_units,legacy_fleet`: 259/263.
      - `battles` b) x2: the known slot-87 race (TESTING Known traps, "34 sunk").
      - `events`: its h) hook needs diplomacy's months (an artifact of the subset).
      - legacy f): no battle, because `battles` ends with the client joined with `fightbattles 0`. The scenario now
        sets `fightbattles 1` before its join.
    - `--only battles,legacy_fleet`: 139/140. `battles` 102/102, and f) fought after `battles`. The one fail was a
      check reading `legacystate` while the month's event dialog (which follows the build at once) refused it; the
      check now answers it first.
    - `--only legacy_fleet` again: **38/38**. Game open 642 + 457 + 155 s.
- **Versions.** Both PCs need 0.7.0. A 0.6.x host never sends `legacy`; a 0.6.x player ignores it (its nation keeps
  the generated fleet); `MSG_LEGACY` to an old host is never sent (no offer) and would be ignored. Protocol 3.
- **Commits:** 0b84c15 (saveio), 87b27c7 (game), c5cc565 (session, UI, bridge), 245f0a2 and 149e3c4 (suite), and the
  docs commit.

## Fix 2026-10-25: the legacy fleet with 0.6.1's campaign divisions: a hand-built fleet starts without divisions (v0.7.0-dev)
Branch dev-070: feat-legacy (the legacy fleet, built on 0.6.0) merged into the 0.6.1 release candidate (int-061
a2d918d), whose joined players keep their own campaign divisions
([0.6.1](#fix-2026-10-25-campaign-divisions-of-joined-players-and-duplicated-divisions-v061)).
- **The overlap.** A 0.7.0 host's month puts nation K's stored divisions into the package's `.off` (pruned to K's ships
  of the month), and the player's PC loads them against K. The legacy build clears K's ships after that, so the list
  named ships the slot no longer held: the load's hook (`PackDivisionShips`) would drop the nil ships but keep the
  divisions, empty, in the player's Fleet organization, and the legacy turn would send them back to the host. On the
  host, a legacy turn without a valid `divisions.ini` would keep the stored list, whose ships the merge had just removed.
- **The fix.**
  - `saveio::ClearNationFleet` also empties the slot's `[CampaignDivisions]` (CampDivNo=0; every other byte of the
    `.off` as it was; an `.off` without the section, or no `.off`, stays as it is). The player's log: "legacy fleet:
    nation K cleared for the manual build: <n> ship(s), <n> design(s), <n> air unit(s), <n> campaign division(s)
    removed".
  - `nplay::HostTakeDivisions(sub, K, fresh)`: the host's merge passes `fresh` for a legacy turn (`MPLegacy`). Its list
    (the divisions the player made of the hand-built ships, or none) replaces the stored one as every month ("divisions
    of nation K: <n> stored for its legacy fleet"); a legacy turn without a valid list leaves an empty one ("... the
    legacy turn's list unusable (<why>); none stored"), never the generated fleet's.
- **Offline** (a harness linking `saveio.cpp` and `util.cpp`; a fresh January 1900 save, Germany's 33 ships): two
  divisions of its generated ships in slot 77's `.off` -> "33 ship(s), 14 design(s), 0 air unit(s), 2 campaign
  division(s) removed", the `.off` byte for byte the fresh game's (CampDivNo=0), the legacy submission's
  `divisions.ini` an empty list; a fresh `.off`, an `.off` without the section and a slot without one unchanged: 15/15.
  The legacy harness of feat-legacy on the merged `saveio.cpp`: all its merge and air-unit checks pass (only its two
  summary texts changed).
- **The suite:** `legacy_fleet` checks it: a) "0 campaign division(s) removed", `divs` n=0 and slot 77's CampDivNo=0
  when the build starts; c) none after the build, then one division of the hand-built cruisers made in the Fleet
  organization ("RT Legacy Div"); d) the host's `divstore 1` = that division, its ships among the merged ones, "1
  stored for its legacy fleet"; e) February: the division back on the player's PC.
- **The merge's conflicts** (both sides kept everywhere): MODLOG (the index: the 0.6.1 group, then 0.7.0; the entries),
  README (the upgrade notes: 0.7.0 above 0.6.1, with 0.6.1's live-battle engine 18), ARCHITECTURE (contents, "Not
  transferred", both sections, the `MSG_STATE` and `MSG_SUBMIT` rows), GAME-INTERNALS (contents, both sections),
  `saveio.h` (`BuildSubmission`'s comment), `session.cpp` (the host's merge loop: the divisions, then `klegacy`),
  `regression.py` (`EST`: `divisions` and `legacy_fleet`). Version 0.7.0-dev; `build.bat` without warnings;
  `regression_selftest.py` 138/138.
- **Live** (the snapshot dist_m70_1, 2026-10-26):
  - `--only session,legacy_fleet,divisions,battles`: session 17/17, battles 102/102, divisions 42/42, legacy_fleet
    22/25 = 183/186 (game open 547 s). The player's log: "legacy fleet: nation 1 cleared for the manual build: 43
    ship(s), 19 design(s), 0 air unit(s), 0 campaign division(s) removed"; `divs` n=0 and CampDivNo=0 at the build's
    start. The three fails were the new division steps: the month's turn report (`TdlgCampTurnRep`, after the event
    roll that follows the build) was still open, so the Fleet organization's New division never opened; the host
    stored the empty list ("divisions of nation 1: 0 stored for its legacy fleet", "0 in its month"). The scenario
    now closes the month's dialogs first.
  - `--only legacy_fleet` again: **42/42** (legacy_fleet 25/25): "RT Legacy Div" of the hand-built cruisers 20606 and
    20607 (CDivId 20621, from the player's id range), "divisions of nation 1: 1 stored for its legacy fleet",
    `divstore 1` the same, back on the player's PC in February (game open 186 s).
  - Game open 547 + 186 = 733 s; slots, Preferences and `config.ini` as before, 0.6.0 reinstalled after each run.

## Feature 2026-10-26: raider battles for joined nations (v0.7.0-dev)
Branch raid-070 from int-061 a2d918d (the 0.6.1 release candidate); the code and the scenario 4d84e61. Until now a
joined nation K's battle generator (B6, 0.6.0: `CheckForBattles2` in K's view on the host, the attempts replayed on
K's PC) answered False for `CheckRaiderVsPatrols`: K never got the battles between raiders and patrols of single
player (MODLOG "B6 round 3": NO-GO for 0.6.0, the plan below).
- **The game (static, 1.01.44;
  [GAME-INTERNALS](docs/GAME-INTERNALS.md#tactical-battles), *Raider vs patrol battles*).** `CheckForBattles2` checks
  the raiders at two places, each after `Random(100) < 30` (0xcecb7e, 0xcecfbc); a True ends the month's search.
  `CheckRaiderVsPatrols` 0xcce6bc: War > 0; loop 1 the player's raiders against each enemy (Tension > 40, not
  `Blockaded`), loop 2 each enemy's raiders against the player (the player not blockaded); `CheckRaiderInterceptions`
  0xccee94 per raider (Status 4, operational) and defender ship in the same area (trade protection / foreign station
  ships of type 1, 3, 4, 6 over 1000 t; other defenders only with their own Tension >= 40, i.e. the enemy's cruisers)
  rolls `Random(150) < c` for the one interception (`PendingRaiderBattle` +0x4a1b: flag, ships, enemy, area, caption,
  side) and `Random(100) < 2c` for the "Patrolling ship ... thwarts ... raider ... attack on merchant shipping in
  ...!" line (`TurnMessages` and `RaiderSummary`; both ships `Used`). `CheckPendingRaiderBattle` 0xcd1910:
  `RaiderBattleDialog` ("Fight Battle" -> the tactical battle set up by 0xbdbf1c, no `DoBattle`, ending in
  `HandleBattleAftermath`; "Auto resolve" -> `QuickResolveRaiderBattle` 0xd0b534, its boxes, VP and sinkings, then
  `DoStrategicTurn`), True either way; the flag is not cleared there.
- **The design (as planned in B6 round 3).**
  - *Host, K's view:* the view's `CheckRaiderVsPatrols` detour (`d_RaiderVsPatrols`, replacing 0.6.0's skip thunk)
    records each raider check as an attempt of type 1 (`BattleAttempt.type`) at its place among the attempts and
    answers False (the search goes on, as after a check that found nothing), only when K's PC announced hello `gen` 3
    (`RaiderChecksOn`); else False unrecorded (0.6.x). The host's own raider checks run untouched. A request whose
    attempts are raider checks only is named for K's first computer enemy; it goes as `att<i>=R` with `humans`.
  - *K's PC:* `game::StartRaiderCheck` runs the game's `CheckRaiderVsPatrols` in the battle slot (K = PlayerNation)
    with the nations people play at Tension <= 40 meanwhile (as the host's view), `PendingRaiderBattle` cleared first
    (`PrepareBattleSlotMonth` clears it too: `, pending raider battle n -> 0` at the end of its log line), the new
    turn messages kept. False: the next attempt (`attempt i (raider check) found no interception`). True with the
    battle screen up: fought like any battle (the aftermath -> `fought`); True after `QuickResolveRaiderBattle` ran
    (its hook counts): the result at once. The opponent is the interception's enemy (a computer nation, else no
    battle). Results carry `raider` (1 fought, 2 auto-resolved), `ropp`, `cand`/`area`, and every result of the
    request the `news` lines.
  - *Host, the merge:* the news into K's player news (`g_playerNews`, with the new month), `ropp` as the opponent
    (refused unless a computer nation), `ApplyBattleResult(..., keepUsed)`: the thwarts' `Used` marks stay the host's
    (the month's raider sinkings, B5's, ran before the battle phase; they are not changed), the war score credited,
    chat "* The raider battle of K against J in <area> was fought / auto-resolved by <player>.", `battles list`
    `:raider=n`.
- **Gotchas.**
  - A blockaded enemy is never raided (loop 1): with Spain blockaded by Germany (B5's flags, "blockading 7"), the first
    forced side-0 test found nothing at all (0 thwarts, no interception): the game's rule, not a bug. The test switch
    lifts the defender's flag during its check.
  - The player's own ships only patrol on trade protection or a foreign station (the Tension >= 40 test reads the
    defender's own field; nation 0's is not its war state).
  - `Random` 0x837834 draws through the procedure in `[0xf1c040]` (`push eax; call [cell]; pop edx; mul edx`): the
    test swaps that cell for calls whose caller lies in `CheckRaiderInterceptions` (the return address at
    `[esp + 8]`), so the dialog, the quick resolution and the battle keep the game's draws.
  - `battles testraid` must come before `DebugBattles`' host-only guard (first run: "error host only").
- **Tests.** Host `battles injectraid <K>` (a request with one raider check), client `battles testraid <0|1> <J>`
  (the next raider check: the raiding nation's ship on raiding duty in the area of an eligible defender ship, the
  player's patrol on trade protection, the draws of `CheckRaiderInterceptions` 0; status, area and the blockade flag
  put back after the check; log `StartRaiderCheck: TEST: forced interception ...`). Scenario `battle_raider`
  (TESTING.md): a) the logs "Raiders against patrols for joined nations: ok", hello "battle generator 3",
  `hostgen=3`; b) 20 dry runs record raider checks (13 in 11 runs on dist_rd_3), relations unchanged; c) four forced
  months, each direction fought and auto-resolved, checked on both PCs and in the saves. `battle_generator` and
  `battle_surprise` take hello `gen` 3 and welcome `gen` 3 now; `cstate` reads `raider=` and `testraid=`;
  `battle_dialogs` answers the raider dialog (`quick`/`decline`: Auto resolve; else Fight Battle).
- **Live (dist_rd_3, run 3, `--only battle_raider`: 96/96, game open 193 s).** Germany (1) at war with Spain (7):
  - side 0, fought: Blücher (our raider) intercepted by Infanta Maria Teresa in the Mediterranean, "Patrolling ship CA
    Infanta Maria Teresa thwarts German raider CA Blücher attack on merchant shipping in The Mediterranean!" in
    Germany's news; the battle screen, auto-run, results, "CA Blücher is forced to scuttle at a neutral shore due to
    damage incurred in battle.", "German marginal victory Own VP: 168 Enemy VP: 141"; host `battle 1 applied: 2 ships
    changed (22 fields), 0 removed, VP 168/141 ...; a raider battle`; every changed field cross-checked, the scuttled
    ship sunk on the host;
  - side 0, auto-resolved: "Enemy ship CL Castilla sunk by our raider CA Hertha.", `battle 3 applied: 3 ships changed
    (6 fields), 0 removed, VP 547/0 ...; a raider battle, auto-resolved`;
  - side 1, fought: "German ship CA Blücher intercepts Spanish raider in Northeast Asia." (run 2), `battle 5 applied:
    2 ships changed (12 fields) ...; a raider battle`;
  - side 1, auto-resolved: `battle 7 applied: 1 ships changed (5 fields), 0 removed, VP 954/0 ...; a raider battle,
    auto-resolved`;
  - each month: the thwart lines in the client's chat with the new month and not in the host's; nation 0's ships in
    the merged save as at the phase start; no raider dialog or box on the host; the test's own status and area
    changes not in the host's save.
- **The suite** on dist_rd_3, `--only battles,battle_generator,battle_surprise,battle_raider`: session 17/17, battles 90/90, battle_generator 73/73, battle_surprise 39/39, battle_raider 96/96 = **315/315** (game open 641 s).
- **Version mixes** (protocol 3; live-battle engine 18 unchanged: no live-battle path changed).
  - 0.7.0 host, 0.7.0 player: raider battles as above.
  - 0.7.0 host, 0.6.x player (hello `gen` 2): no raider check is recorded for its nation (0.6.x behaviour: the view
    answers False); the welcome's `gen` 3 is read as ">= 2" there.
  - 0.6.x host, 0.7.0 player: the host never records raider checks (its view's skip thunk); nothing changes for the
    player (no `news` without raider checks).
  - 0.6.x with 0.6.x: unchanged.
- **Differences from single player and risks.**
  - The interception chances are rolled on the month-end state on K's PC (as the kind-1 attempts are), not at the
    month's start; the 30 % gates are rolled in the host's view at the month's start.
  - A thwarted raider still sinks merchant ships that month on the host (B5's sinkings come first) and its `Used`
    mark is not merged.
  - Thwart lines of a request the time limit dropped (the host resolved it without the player) are lost with its late
    result.
  - The quick resolution's own boxes are shown on K's PC only (as in single player), not in K's news.
- Game open 24 + 127 + 193 + 641 = 985 s (runs 1-4).

## Feature 2026-10-26: invasions for joined nations (v0.7.0-dev)
Branch inv-070 from dev-070 5bcee82: 19473ca (P1, the month's resolution), 318f6ca (the host's marks), 2c057e2 (P2,
the orders), e37c854 (P3, invasion battles), 63443ac (the pending battle used up), e5393bb (P4, landings at a war's
start), 95b1d6e (P5, peace), 44bdf0c (P7, the computer's escorts), 47db5bd (the host check of the cases), then P6 (the
scenario's parts f-j, the docs). Until now a joined nation K had no invasions: RTW3 1.01.44
keeps them for nation 0 only (the possession fields, the form's plan, a month routine that treats every invasion as
nation 0's), and a K invasion set on the host would have been resolved as the host player's or cancelled at peace.
- **The game (static and live, 1.01.44; [GAME-INTERNALS](docs/GAME-INTERNALS.md#relations-wars-and-battles-nation-0s-and-the-others),
  *Invasions in detail*).** The month routine names itself "CheckRebellionAndInvasions" (its except text); it also
  counts the base expansions down and runs the rebellions of the possessions nation 0 does not own. The plan lives
  in the form (`InvasionTarget`, `InvasionCount`, `EnemyInvasionTarget`, `InvasionBattlePending`); `InvadedThisTurn`
  is never saved. `CheckForInvasionBattles` picks invaded possessions without asking whose invasion it is.
  `DoStrategicTurn` ends a pending battle at the month's start. 1.01.44's map question has no Decline: the player
  declines in the battle preview (disabled when defending); without own ships in the area the enemy lands without a
  battle. `HandleInvasionResults` lands by the battle's result level. `DoPeace` clears every possession's invasion and
  the plan. `GetMostImportantArea` scores the computer's fleet areas by the player's invasions and the computer's own
  planned landing.
- **The design.**
  - *State:* K's invasions in the game's possession fields on the host, in the real numbering (Invaded = the
    invader; a K invasion has K on exactly one side, the other a computer nation), K's plan in the session file
    (`kinv`, dated like `kwar`), K's possessions invaded this month in the plan (`itt`). Saved with the campaign:
    every PC's map shows them.
  - *The host's own routines* see only the invasions involving nation 0: its `CheckRebellionAndInvasions` and
    `DoPeace` hide K's while they run (at peace the war state is put back), its `DoPossessionBattle` from the host's
    invasion check answers False at K's invasion; once a month the log counts them ("invasions: the host's own
    routines and the joined nations' invasions this month: ...").
  - *P1, K's month:* after the battle phase, the game's routine in K's view (`inv::RunForNation`: K as PlayerNation,
    K's war counter, K's invasions only, K's plan in the form, the base countdown and the rebellion tail left out, a
    fresh TurnMessages list, the boxes captured): its texts to K's turn report, its "Enemy fleets dominate" points to
    K's first computer enemy, owner changes as the game does them, the target's monthly cost in K's money. A battle
    pending since the last month ends first (`DoStrategicTurn`'s reset).
  - *The host's marks:* `InvadedThisTurn` is put back after the turn's merge reload and the battle phase's reload.
  - *P2, orders:* K's PC shows K's plan (`kinv` with the month's state) and sends the target set with the game's
    button (`kinvt`); the host refuses a person's possession, a value of 10 or more, an invaded one.
  - *P3, invasion battles:* the generator's view records the invasion check (type 2, `att<i>=I`); every battle
    request carries `kinv`/`kinvp`; K's PC puts them in the battle slot (the save's invasions cleared) and replays the
    check with the game's routine; every result returns the slot's state (and `iopp`/`iatt`); the host applies the
    changes by name after the phase's reload.
  - *P4, landings at a war's start:* with a gen 4 host K's relation turn lets CheckForWar's surprise landing run in
    K's view (the game's pick and strength test; else its takeovers) and records it (`kland`); the host sets the
    invasion if K still owns the possession uninvaded and marks it this month.
  - *P5, peace:* K's accepted peace resets K's plan on the host (DoPeace's resets); the possessions' war state comes
    with B4's `ptaken`. The host's peace leaves K's invasions alone (the hiding above).
  - *P7, the computer's escorts:* `GetMostImportantArea` of a computer nation J at war with K sees K's invasions of
    J's possessions as the player's (+80 "is invaded by player") and K's possession in a plan of J's side as the
    form's `EnemyInvasionTarget` (" If at War. Enemy invasion area"; its Tension test passed for that branch only).
  - *Decisions (accepted 2026-10-26):* the host's battle aftermath may change K's invasion support, like an ally's
    battle; "Enemy fleets dominate ... Lose N VP!" goes to K's first computer enemy; K's invasions pause in months
    when K's player isn't connected with gen 4; one invasion per possession, a newer one replacing the other (logged
    "... started nation n's invasion of p while nation m's was hidden: nation m's ends").
- **Gotchas.**
  - The routine looks at a target only when `Random(20) > Value` and its area has no invasion: low forced draws never
    pass it.
  - The client's turn sends its form's target every month: a test sets the target on both PCs (`inv pick`).
  - MinHook hooks created after start-up need `MH_EnableHook` (game.cpp's `Hook` only creates them).
  - Without the player's ships in the area there is no battle: "Friendly ships not available to oppose the enemy in
    the area!"; tests pick areas with ships (`battle_pairs`).
  - `GetMostImportantArea` draws random numbers: `inv escort` restores RandSeed between its two calls.
- **Tests.** Bridge `inv` (state, marks, run, set, target, plan, rnd, escort, hostpeace, hostcheck; client form,
  pick), `battles injectinv K`, `battles testinv 1`, `battles testsa J land` ([TESTING](docs/TESTING.md#bridge-command-reference)).
  Scenario `battle_invasion` (group `battlegen`): a)-d) P1/P2, e) P3, f) P5, g) P4, h) P7, i) the cases, j) the host's
  peace.
- **Live** (the game's texts from the logs).
  - P1/P2 (dist_iv_1/2): nation 1 invading Russia's Liaotung Peninsula, draws low: "Our forces in Liaotung Peninsula
    have been defeated!"; Russia invading Cameroon: "Cameroon passes from nation 1 to nation 3" and "The Russian army has
    defeated our beleaguered forces in Cameroon! Remnants of our troops have taken to the forest..." (nation 1's
    guerrilla afterwards, as in single player); the host's routine hid them ("CheckRebellionAndInvasions 1x (1
    hidden)"); the host's possession refused as a target; Kamchatka taken; "invasion target -N" in nation 1's month;
    `battle_invasion` 43/43.
  - P3 (dist_iv_3, the P3 code alone): `battle_invasion` 77/77, session 17/17. e1: the invasion battle at Northern
    Russia (Northern Europe) fought, VP 484/90, the result level 0 ("does not warrant a continuation"): the plan
    cleared, the slot's state back = the host's; e2: Russia's pending landing at Kiautschou Bay declined in the preview
    (DoPlayerDecline): "Kiautschou Bay 3/-1, 1 mark" back and applied ("new: invader 3 support -1"). Earlier runs met
    the computer's decline ("The Russian navy declines battle! You gain 176 VP!": nation 1's troops land at Kamchatka)
    and the landing without a battle ("Friendly ships not available to oppose the enemy in the area!": "Tanganyika new:
    invader 3 support -1").
  - P4-P7 and the cases (dist_iv_5, all of it; runs 2 and 3): f) peacetest 100 2 1: "nation 1's peace ends its
    invasion plan (target Liaotung Peninsula/1, enemy target Tanganyika/-1 ...)", both invasions over, the client's form
    empty; g) Japan's surprise landing in Kiautschou Bay recorded ("kland=1") and applied (invaded by 5, marked); h)
    Formosa (Southeast Asia) 5 -> 85 points with nation 1's invasion, West Africa 0 -> 2 with Cameroon in Japan's plan,
    the month's computer decisions "GetMostImportantArea: joined nations' invasions shown 4x"; i2) "Our forces have
    taken control of the Russian possession Northern Russia" (owner 3 -> 1); i3) "the host's invasion battles: none at
    Liaotung Peninsula (nation 1's invasion of nation 3's possession)"; j) the host's DoPeace cleared its own invasion
    of Kamchatka and left Liaotung Peninsula (invaded by 1, support 1) and Northern Russia (by 3). Not seen: i1, a
    computer start in nation 1's month (12 runs against Japan and Russia, draws high): its gates are the fleets'
    strengths in the area (the random enemy's over 2 G + 1 and over 1.1 times the nation's, then the blockade, the
    weather, the enemy's own strength and range); the scenario tries 16 runs and notes it if none.
  - Fixed on the way: a second request of the phase met the nation's leftover pending battle and the game's "Debug:
    Invasion error, no invasion target!" box (the host ends the pending battle once the month's check ran); the
    scenario's `inv_of` found only the list's first entry.
- **The suite** (dist_iv_6, the final code): `--only battles,battle_generator,battle_surprise,battle_raider,battle_invasion,relations_peace,war_effects,legacy_fleet`: session 17/17, battles 90/90, relations_peace 34/34, war_effects 73/73, battle_generator 73/73, battle_surprise 39/39, battle_raider 95/96, battle_invasion 115/115 (i1 noted: no computer start in 16 runs), legacy_fleet skipped there (no fixture given) = 536/537, game open 910 s. battle_raider's one failure is the runner's known race: its merge check read slot 87 after the client had copied the next battle into it ("slot 87 was rewritten for the next battle before it was read", also six times in this branch's first suite on dist_iv_2); `--only battle_raider` again: 93/94 (the same race, the slot unreadable), then 96/96; `--only legacy_fleet` with the legacy test campaign (`RTW3_LEGACY_FIXTURE`): 25/25.
- **Version mixes** (protocol 3; live-battle engine 18 unchanged: no live-battle path changed).
  - 0.7.0 host, 0.7.0 player (hello and welcome `gen` 4): invasions as above.
  - 0.7.0 host, a player below hello `gen` 4 (0.6.x, an older 0.7.0-dev): no resolution in its view, no invasion
    check, no `kinv`/`kinvp`, no landing recorded: its nation has no invasions, as in 0.6.x.
  - 0.6.x host, 0.7.0 player (welcome `gen` 3 or less): no `kinvt`; the player's form loses the host player's target
    (display fix); its relation turn keeps the landing out; its battle slots keep the host's save as before.
  - The host's marks across its reloads work with any player.
- **Differences from single player and risks.**
  - The decisions above (the host's aftermath and K's support, the "Enemy fleets dominate" points, the pauses, one
    invasion per possession).
  - The invasion check is replayed on the month-end state on K's PC, like the other attempts; a decline there ends
    the request (single player's search may go on to a second encounter that month).
  - K's base expansions complete without their message, and the base expansion's tension rule does not apply to K.
  - Invasions between people's nations are not supported: K cannot target a person's possession; the host's own
    routine may let a joined nation at war with the host invade the host's possession as a computer nation would.
- Game open (this round): 123 + 115 + 113 + 273 + 237 + 182 + 210 + 268 + 269 + 916 (the suite) + 206 + 264 + 265 s.

## Testing 2026-10-26: the 0.7.0 release candidate dist_rc070_1 (v0.7.0)
The release candidate dist_rc070_1 (dev-070 14c03c6: 0.7.0, live-battle engine 18, protocol 3; RTW3MP.dll SHA-256
B62359099638269E..., version.dll C7A8F7E9C2508C9C...): the legacy fleet, raider battles and invasions for joined
players, and the released 0.6.1 merged in. Run on the reg-070 runner, installed from its frozen snapshot (`--dist`), the
released 0.6.1 (dist_rc061_2) put back after every run (hash checked); slots 1 and 9 and the Preferences identical to
their backup after every run, the save folders as before (no slot 46/48/49 left), no game folder copy, delay proxy,
`python` or `RTW3.exe` left. The logs of every run are kept with the developers' session files (`r70\logs_*`).
- **Runs** (checks, seconds with the game open):
  - The default run (32 scenarios, `--legacy-fixture`): 1396/1397 (2920 s); `battle_raider` 96/96, `battle_invasion`
    115/115; the fail is `legacy_fleet` (suite, below). `--only lockstep_air,legacy_fleet` with the fix: 83/83 (510 s).
  - `--stress` (38 scenarios, both fixtures): 1770/1774 (6752 s): `battle_invasion` e2) (2; the game's choice, below)
    and the host's game error box (2 checks), below; `legacy_fleet` itself 25/26.
  - `--group relations`: 390/391 (874 s; `relations_war` a): the month's tension routines took 1-3 from 15 to 11,
    under CheckForWar's line: flaky) and 417/417 (717 s).
  - `--stress --only lockstep_month`: 99/99 (332 s).
  - Re-runs `--stress --only lockstep_missiles,legacy_fleet`: 28/36 (515 s) and `--stress --only battle_invasion,
    lockstep_missiles,legacy_fleet`: 161/172 (878 s) and 144/150 (593 s), below.
  - Mixed versions (TESTING.md, "Mixed versions"; `mixed_070.py`, `r70`): a) 63/64 (390 s; a driver fault) and 64/64
    (181 s); b) 65/65 (179 s) and 66/66 (163 s): both builds fight the host's battle live (514/424 minutes equal, no
    DESYNC); the 0.6.1 side gets no raider battle, invasion or legacy build; no box.
  - With the game open: 2920 + 510 + 6752 + 874 + 717 + 332 + 515 + 878 + 593 + 913 (mixed) = 15004 s.
- **Game error boxes (product):**
  - The host's "Error in AIDetermineTacticalStance2 procedure! Division: SU Destroyer Division 10 Access violation at
    address 006EB5C0 in module 'RTW3.exe'. Read of address 000009D3" (`--stress`, 10:25:34): the 1958 missile battle
    going on against the computer after `lockstep_missiles` dropped B (B had been on auto-run and back: one division
    stayed with the computer), run to its end by `legacy_fleet`'s new prologue; the battle ended normally. Not seen in
    the two later runs of that sequence (one stopped before it).
  - The client's "Error in AdjustForcesForBattle procedure! Access violation at address 000EC748 in module 'RTW3.exe'.
    Read of address 02EBC02F" (the last re-run, `battle_invasion`'s month "invasion 1"): the host's battle "Germany vs
    Russia in Northern Europe" became, in the battle slot, the game's invasion battle at the nation's invaded
    Liaotung Peninsula (`the nation's invasions in the battle slot: ... 1 of the nation's set (Liaotung Peninsula 1/2)`,
    `attempt 0 (invasion check): the player's decline at Liaotung Peninsula ... (our invasion)`); the box came between
    the map message and the preview, the battle was declined and the month went on. The read address is the one of
    0.4.1's box at a relayed battle's start, but the path is 0.7.0's invasion battle in the battle slot.
- **Suite fixes** (reg-070): `legacy_fleet` ends the host's battle that the scenario before left open (ad9adec;
  `lockstep_air`'s goes on against the computer: the load of slot 46 kept the battle screen up and the month never
  ran, `host turn: click handler returned (endOfTurn=0 battle=1)`); `battle_invasion` e2) tries up to three times when
  the game drops the enemy's pending battle (`possession battles: Annam (-1 attacks 0)`, 19 ships of nation 1 there:
  the player much stronger, `Random(100) < 40`), then a SKIP-note; `legacy_fleet` a) answers a turn report that comes
  before the build's picture message (NOTE): the host had kept four picture messages `captured` by `battle_invasion`'s
  last tests after its last month and sent them with the legacy campaign's first month (a minor product note: the
  host's per-nation messages outlive a session). The last two are not exercised yet (the runs that met their cases
  came before them). `lockstep_missiles` met battles that ended early twice (no contact; the launchers spent; then
  "timeout: B in a minute"): flaky, 23/23 in the other two runs.
- **Verdict: NO-GO** for publishing 0.7.0 with exactly this build: two game error boxes, one in 0.7.0's own invasion
  battle path in the battle slot. Everything else passed or was shown flaky; the mixed versions as designed.

## Fix 2026-10-26: the game's box before the preview of a battle after an intelligence coup; old turn messages in a new campaign (v0.7.0)
The 0.7.0 release candidate's last `battle_invasion` re-run (MODLOG "Testing 2026-10-26: the 0.7.0 release candidate
dist_rc070_1") showed on the joined player's PC "Error in AdjustForcesForBattle procedure! Access violation at address
000EC748 in module 'RTW3.exe'. Read of address 02EBC02F": in month "invasion 1" the host's battle "Germany vs Russia in
Northern Europe" became, in the player's battle slot, the game's invasion battle at Liaotung Peninsula (nation 1's
invasion of Russia's possession: `attempt 0 (invasion check)`), and the box came between its map message and its
preview. 0.4.1's k2 box (todo afb-av; TESTING "Known traps") read the same address. Branch fix-afb from dev-070
b809447.
- **The address.** That run's `random=000E7834`: base 0xE0000, RVA 0xC748, in the image 0x83c748: `System._UStrCatN`,
  `add eax, [ecx-4]` (the length of one of the strings it joins) with ecx = 0x02EBC033. That value is no heap address
  but the code bytes `33 C0 EB 02` (`xor eax, eax; jmp +2`): a string read from code. 0.4.1's box, 0096C748, is base
  0x960000 + 0xC748 with the same read.
- **The routine.** `AdjustForcesForBattle` 0xbec828 (unnamed; its except handler shows the box, `mov edx, <text>` at
  +0x3fc) is called once, by the battle setup (0xbe3abc) after its two force builds (0xbe7c18), before the preview.
  With the coup flag (0xf4c5ea, cell 0xf26ee4) it removes side 1's extra divisions while side 0 has fewer ships, and
  while side 0 is still the weaker it calls its nested reinforcement routine 0xbec424 (AL ship type, EDX side 0, its
  frame pushed) at 0xbecab8 and 0xbecade. That routine takes the side's nation from the parent's local [ebp-4] for the
  division builders (0xbedb94, 0xbecfd0: the new ships' names). Only the sibling routine of the random reinforcements
  (0xbec62c, the path without the flag) sets that local (side 0 `PlayerNation` +0x3204, side 1
  `GetBNation(BattleOpponentIdx)` +0x4a7f); `AdjustForcesForBattle` itself never does. On the coup's path it holds what
  the setup's second force build left there: its return address 0xbe3aa6 (two stack arguments put it just where the
  next call's [ebp-4] is). `NationName` (+4) of that "nation" is the code at 0xbe3aaa, 33 C0 EB 02: the same read on
  every PC and in every run, whatever the base. A game bug; single player has it too.
- **The coup.** `DoPossessionBattle` 0xced48c (from `CheckForInvasionBattles`, `CheckAttackLocation` 0xced32a and the
  test menus) clears the coup flag and the caught-out flag (0xf4c5eb) at its start and sets the coup flag when the
  mission's type is 2 or 3 and `Random(15)` < the enemy record's `IntelEffort` (+0x1ac) + `GetCLScoutPenalty` (before
  1935) +-1 (aircraft in the area): "Due to an intelligence coup, we can ambush an enemy force!" (else `CheckCaughtOut`
  sets the other: "Unexpected battle!"). `AdjustForcesForBattle` clears it only once side 0 is not the weaker. In the
  RC run nation 1 invaded Liaotung Peninsula with one ship against Russia's: the coup, the reinforcements, the box; in
  the other runs the coup's roll failed. 0.4.1's k2 box (a fresh process; France against Germany in Northern Europe
  from `CheckAttackLocation`) had the same cause: only `DoPossessionBattle` sets the flag.
- **Not the mod's invasion state.** `inv::SlotPut` sets `InvasionTarget`, `EnemyInvasionTarget` and the possessions'
  fields by name after the slot's load (`LoadCampaign(87)` comes first), 0.6.0's clearing of `BattleArea` and
  `BattlePossession` holds, and the routine reads none of them. The mod's state only decides which mission
  `DoPossessionBattle` takes (`InvasionBattlePending` > -1: `GetClosestXMission`). Noted, left as it is: the battle
  slot is the host's save, so the coup's and `CheckCaughtOut`'s `IntelEffort` there is the host's intelligence against
  the enemy, not the joined nation's own (its month package gets its own, `nplay::HostTransformState`; the battle
  package does not).
- **The fix** (4cd5192): game.cpp `InstallAfbFix` finds the routine from its error text (the literal, its `mov edx`,
  the two calls `push ebp; movzx eax, byte ptr [ebp-0x19]; xor edx, edx; call; pop ecx` before it, the routine's head
  and its read of the parent's local checked) and hooks the nested routine. At those two calls (their return
  addresses) `CB_AfbReinforce` gives the parent's local the side's nation when it holds none (side 0 `PlayerNation`,
  else the battle opponent's), as the sibling routine would. Log `battle setup: the ambush's reinforcements of side 0
  had no nation (AdjustForcesForBattle's unset local held 00493AA6: the game's code, rva 0x3b3aa6); the mod gave them
  nation 0 (Germany) ...`; start line `The ambush's reinforcements in the battle setup (AdjustForcesForBattle): ok
  (calls at rva 0x3bcab8/0x3bcade, flag found)`. On every PC, for every battle the game sets up (the host's own too).
- **The test switch** (TEST ONLY, the same commit): `battles testcoup <0|1|2>` -> `ok testcoup=<n> forced=<n>`: the
  next `AdjustForcesForBattle` on the PC runs as an ambush that finds the player's side the weaker: a hook at its start
  sets the flag without touching the stack, and its own calls of the side count (0xbf7d20, found at its `push 1; xor
  ecx, ecx; mov dl, 0x16; xor eax, eax; call`) see side 1 + 1000; 2: without the mod's nation (the game's own path); 0
  clears the flag. Gotcha: a first version called a logging routine from the start hook; that call's frame wrote the
  stack below the routine's, so the unset local held another value and the game's box said "Integer overflow".
- **The host's turn messages** (the same commit; the RC's minor item: `battle_invasion`'s last invasion texts showed in
  nation 1's first month of `legacy_fleet`'s campaign): `ClearQueuedNews` drops what the host keeps for each joined
  nation's next month (its invasions, war effects, economy, the treaty's end, the notices) when the session ends
  (`Leave`) and when the host takes up another campaign (`LoadReservations`); log `turn messages kept for the joined
  nations dropped (<why>): N`.
- **The suite** (8daca28): `battle_invasion` e3): nation 1's own invasion battle at J's possession in the sea area where
  J is the strongest against its ships (`inv plan 1 0 - 1`, client `battles testinv 1` and `battles testcoup 1`):
  `forced=` +1, the reinforcements' line ("gave them nation 0"), no AdjustForcesForBattle box, fought or declined (the
  driver declines; in one run the preview's Decline was greyed out and the battle was fought). Env `RTW3_INV_COUP=2`
  (diagnosis) sends 2 and checks that the game's box comes back.
- **Proof** (dist_fa_2; the committed build differs from it only in the PE timestamps):
  - Reproduced on purpose: `--only battle_invasion` with `RTW3_INV_COUP=2` (13:14, 210 s): e3)'s battle 7 at Sakhalin
    (Northeast Asia: 3 Russian ships, 1 German) showed the RC's box word for word, "Error in AdjustForcesForBattle
    procedure! Access violation at address 000EC748 in module 'RTW3.exe'. Read of address 02EBC02F" (base 0xE0000
    again), after `battle setup: TEST: the ambush's reinforcements of side 0 keep the game's local (00493AA6: the
    game's code, rva 0x3b3aa6, no nation)`, the value the static analysis predicted; 130/131 (the run's "no game error
    box" checks fail on it, as they should).
  - `--only battle_invasion` with the fix, three times, 130/130 each, no box: 13:17 (433 s; e3: "had no nation
    (AdjustForcesForBattle's unset local held 00493AA6: the game's code, rva 0x3b3aa6); the mod gave them nation 0
    (Germany)", the preview's Decline greyed out, the battle fought: "German major victory", "After the victorious
    battle, our troops land in Sakhalin!"), 13:25 (235 s; e3 declined) and 13:48 with `legacy_fleet` (577 s; e3
    declined). In that run the host logged `turn messages kept for the joined nations dropped (the session ended): 8`
    before `legacy_fleet`'s session, and the client's first month showed the game's "Build your existing fleet" with no
    old turn report before it (the RC's minor item). Its `legacy_fleet` 8/16: the game's auto design answered "Auto
    design ship of type CA" with "Design report for CL Prinz" and no "go to the build dialog?" (b) and c) timed out);
    `--only legacy_fleet` at 13:59: 25/25 (flaky: the game's auto design).
  - `--group relations` (13:29, 656 s): 390/391, no box; the one FAIL is `relations_war` a)'s known flake (the month's
    tension routines took 1-3 from 15 to 12 before CheckForWar); `--only relations_war` at 13:58: 19/19.
  - `--only battles,battle_generator,battle_raider` (13:40, 497 s): 288/288 (battles 114, battle_generator 61,
    battle_raider 96), no box.
  - The first test switch (dist_fa_1, 12:16-13:13, 5 runs): its start hook called a logging routine, whose frame wrote
    the stack below the routine's: the box came as "Error in AdjustForcesForBattle procedure! Integer overflow" (the
    local held 00DDE804); and where nation 1 was not the weaker after the cut (Northern Europe in b), 1 v 1 or 2 v 1 in
    Northeast Asia) no reinforcements came: hence the stack-free start hook and the count hook.
  - About 69 minutes with the game open in all (dist_fa_1 1237 s, dist_fa_2 2888 s). After every run v0.6.1 was put
    back, slots 1 and 9 and the Preferences were identical to the backup, and the game lock was released.
- **0.4.1's box** (todo afb-av): the same cause, see above. **Snapshot:** dist_fa_2 (RTW3MP.dll 10346436...,
  version.dll 89987677...).

## Fix 2026-10-26: the division stance routine's box after a live battle (v0.7.0)
The 0.7.0 RC1 stress run's host box at 10:25:34: "Error in AIDetermineTacticalStance2 procedure! Division: SU
Destroyer Division 10 Access violation at address 006EB5C0 in module 'RTW3.exe'. Read of address 000009D3". The 1958
missile battle of `lockstep_missiles` had gone on against the computer on the host: side 1's player turned auto-run on
(minute 99) and off (minute 100: one division stayed with the computer), the host dropped the player (minute 101:
`SetSideControl` 1 to the computer), then `legacy_fleet`'s prologue pressed the host's own auto-run (every division
`AIControlled`, Fastest available); the box came about two seconds later, Italy had no ships left at sea nine seconds
later. It did not come again: not in the regression agent's three re-runs of that order (one reached the host's
auto-run), not in two runs here.
- **The fault.** That run's image base was 0xe0000 (`random=000E7834`, rva 0x7834; `frmMain=00803274` is the known
  frmMain variable 0xf53274): 0x6eb5c0 is rva 0x60b5c0, analysis 0xe3b5c0 = `TFightingShip.IsBigShip`, whose first
  instruction reads `ShipType` +0x9d3 of Self: Self was nil. The box is `AIDetermineTacticalStance2`'s own handler
  (0xdc108c, a `TfrmMain` method without an RTTI name; Self EAX, the division EDX; it shows its box once per battle
  screen, +0x73d), so the nil came from inside its call tree.
- **Where the nil comes from.** `AIDetermineTacticalStance2` checks `Ships.Count > 0` before its own
  `GetFlagship.IsBigShip`, and each caller has read the division's own flagship before (the StartMinute loop at
  0xd9593e, `CheckAllDivAfterMove` at 0xdb0c72; `DetachShip`'s new division is named "<ship> *"), so the division's own
  flagship was not nil. The stance routine for destroyers (0xdbd104), for `Role` drSupport (2) or drScreen (4) with a
  target in 12000 yards and `Random(20) < 10`, calls `LeadDivision.GetFlagship.IsBigShip` without checking that the
  lead division has ships (0xdbddc4..0xdbddd6); `GetFlagship` is nil for a division without ships. Its other
  `IsBigShip` calls test their ship first (or call a deterministic closest-enemy routine twice). The other stance
  routines read `LeadDivision.GetFlagship` unguarded as well (0xdb778d, 0xdb8381, 0xdb86f0). The game keeps these
  references valid only at the end of each minute: `AllShipsCheckSinkings` takes sunk ships out of their divisions,
  then `CheckAllDivAfterMove` walks the divisions backwards, clears a `LeadDivision` without ships (0xdb0cb5: nil,
  `Role` drIndependent) and removes a division without ships (`RemoveAsDivTarget`: every `TargetDiv` and
  `LeadDivision` pointing to it := nil). In between a reference can point to a division without ships: inside that
  walk (its rescuer call of the stance routine at 0xdb115d, while divisions further down the list are still empty) and
  after a division empties outside it (a detached division's reattach to its `InitialDivision`, 0xdc7020). Survivor
  pickups that start these rescues are automatic when the force is the computer's, with the game's own auto-run or
  the preference: after the drop side 1's forces were the computer's and the host's auto-run made every division the
  computer's, so the path ran where the live battle had asked the player (minute 36: "Do you want to detach DD ... to
  pick up survivors", answered No). A game bug the drop and the auto-run reach, not a pointer the mod left: the mod
  writes only flags in those paths (`SetSideControl`, `AutoOff`), no division list or reference. Ruled out: a freed
  division (the box names it, its name string intact), a nil in a `Ships` list (no game code puts one there: no
  `Pack`, `SetCount` or direct `FList` write on `Ships`, the battle load tests each ship), the resync (minute 28, no
  box until minute 101+), the missiles (no torpedo object reaches this routine).
- **Not reproduced on purpose.** Two runs here (`--stress --only lockstep_missiles,legacy_fleet` with the fixtures, the second
  with `RTW3MP_DIAG=1` through a launcher wrapper: the runner drops `RTW3MP_*` from the game's environment) met other
  battles (one ended by the scenario length, one with Italy's fleet gone as in the RC1 run); no box, the condition not
  met. A probe at the minute's start and end could not see it: the empty reference lasts only inside the end-of-minute
  walk or until it. The rejoin proof run below met the condition by itself.
- **The fix** (0.7.0, live-battle engine 18): the lockstep hooks put a guard before `AIDetermineTacticalStance2`
  (`LS_dStance` → `LS_StanceGuard`; the routine is found through `DetachShip`'s call, `cmp byte [ebx+AIControlled],0;
  je; cmp dword [edi+Sinking],0; jne; mov edx,ebx; mov eax,[ebp-8]; call`, and checked by its first test `mov eax,ebx;
  call GetFlagship; call IsBigShip`). For the division it is about to judge, a `LeadDivision` without ships becomes nil
  with `Role` drIndependent and a `TargetDiv` without ships nil: what the end-of-minute walk does a moment later. Any
  other state passes unchanged; it draws no random number and reads only shared state, so both PCs of a live battle do
  the same. Log lines `lockstep: the division stance routine's guard: on (<address>)` at the hooks' install and
  `lockstep: stance guard: division '<name>' (side <s>) at t=<time>, from rva <caller>: its lead division '<name>'
  (LeadDivision := none, role independent) has no ships left` (at most 20 a game). The hooks are installed with the
  first live battle of a game and stay: live battles and the host's battle after a drop are covered; a single-player
  battle without a live battle before it runs the game's routine unchanged. Only a battle that would have shown the
  box plays differently from 0.6.1 (there the routine stops at the fault): in a live battle with a 0.6.1 player that
  minute can differ (a DESYNC, then the resync), so the engine stays 18.
- **Proof** (dist_fs_2 = the fix commit's build): the guard found its routine on every copy (`the division stance
  routine's guard: on (0067108C)`, rva 0x59108c). `--stress --only lockstep_missiles,legacy_fleet` with both fixtures
  and `RTW3MP_DIAG=1`: 65/65, no box (Italy's fleet gone after the host's auto-run, as in the RC1 run; nothing for the
  guard to do). `--only lockstep_missiles` with the missile fixture three times: 40/40 each, no box.
  `--only lockstep_rejoin,lockstep_limits`: 117/117, no box, and the condition came by itself: the relayed battle's
  host copy went on against the computer after "Captain disconnected" (minute 11), and the guard cleared the lead of
  'G9 *' (t=17:17) and 'G8 *' (t=17:43), detached torpedo-boat escorts whose leads 'Gefion *' and 'Stettin *' had no
  ships left, both in the StartMinute loop (return rva 0x565c6d = 0xd95c6d). The game was open 42 minutes in all.

## Testing 2026-10-26: the 0.7.0 release candidate 2 dist_rc070_2 (v0.7.0)
The release candidate dist_rc070_2 (dev-070 1e4c1a6: 0.7.0, live-battle engine 18, protocol 3; RTW3MP.dll SHA-256
EE78FEFC29E8982F..., version.dll 534F2031AF9F120B...): RC1 (dist_rc070_1) with the fixes for its two game error boxes,
the ambush's reinforcements in `AdjustForcesForBattle` with the old turn messages dropped (4cd5192, suite 8daca28) and
the guard before the division stance routine (d748834). Run on the reg-070b runner, installed from its frozen snapshot
(`--dist`), the released 0.6.1 (dist_rc061_2) put back after every run (hash checked); slots 1 and 9 and the
Preferences identical to their backup after every run, the save folders as before (no slot 46/48/49 left), no game
folder copy, delay proxy, `python` or `RTW3.exe` left, the game lock released. The logs of every run are kept with the
developers' session files (`r72\logs_*`).
- **Runs** (checks, seconds with the game open):
  - `--only battle_invasion` twice: 148/148 (296 s) and 147/147 (218 s). e3)'s coup ambush at Sakhalin (2 and 4 ships
    of J against 1 of nation 1 in Northeast Asia): "had no nation (...); the mod gave them nation 0 (Germany)", no box,
    declined; i1) a computer start in the first run, a SKIP-note in the second.
  - Mixed versions a), a 0.7.0 host with a 0.6.1 player (`mixed_070.py`, copied to `r72` with its import pointed at the
    reg-070b runner): 64/64 (178 s). The host's battle against the player in Oct 1900 fought live (424 minutes equal,
    no DESYNC, ended by agreement), the month going on (Oct -> Nov 1900, the player planning): nothing stuck; no raider,
    invasion or legacy work for the 0.6.1 side; no box.
  - `--group relations`: 417/417 (644 s; `relations_war` 19/19).
  - The default run (32 scenarios, `--legacy-fixture`): 1454/1456 (2845 s): `battle_invasion` e3) (suite) and
    `lockstep_limits` j) (a battle condition), below; `legacy_fleet` 25/25 after `battle_invasion`.
  - `--only battle_invasion` with the e3) fix and `RTW3_INV_E3_TARGET=Northern Russia`: 147/147 (307 s).
    `--only lockstep_limits`: 72/72 (166 s).
  - `--stress` (38 scenarios, both fixtures): 1797/1800 (7228 s): `battles` c/d) and c2) (the game's choice) and
    `air_units` d) (suite), below; `battle_invasion` 130/130 (e3) the new SKIP-note), `lockstep_missiles` 23/23,
    `lockstep_stress` 138/138, `lockstep_pvp` 45/45, `twopc` 85/85, `legacy_fleet` 25/25.
  - `--stress --only battles,air_units` with the `air_units` fix: 119/119 (330 s).
  - With the game open: 296 + 218 + 178 + 644 + 2845 + 307 + 166 + 7228 + 330 = 12212 s.
- **No game error box** in any run, on any PC: neither of RC1's (AdjustForcesForBattle, AIDetermineTacticalStance2)
  nor another. The exceptions in the logs are the tests' own: `battle_generator` b)'s `battles gen 1 fault` and
  `lockstep_stress` S7's `ls throw` (the minute box "Error in MoveInterval procedure! External exception E0545354").
- **The stance guard by itself** (`lockstep: stance guard: division ... has no ships left`): in the default run twice on
  the host, in the battles `lockstep` and `lockstep_ui` left going on against the computer ("Captain left the live
  battle (test). The AI commands Germany now"), RC1's box situation: an empty lead division cleared, no box; in the
  `--stress` run in `lockstep_strike`'s live battle on both copies alike (the same divisions at t=11:58 and 12:30, every
  minute equal, 21/21), once more on the host and 20 times on a client copy that `lockstep_rejoin` later killed.
- **Failures and their classes:**
  - `battle_invasion` e3) (default run; suite): nation 1 had no ships left in Northeast Asia by then, so the only
    candidate was Northern Russia (9 ships of J, 16 of nation 1 in Northern Europe): `forced=` 0 -> 1, no "battle setup:
    the ambush's reinforcements" line, no box, declined. The mod logs that line only when the routine's local holds no
    nation; the same at Northern Russia in an `--only battle_invasion` run (14 v 33, `RTW3_INV_E3_TARGET`) and in the
    `--stress` run (6 v 13): where nation 1 is not outnumbered the reinforcement step meets no unset nation. The check now
    requires the line only where J outnumbers nation 1, else it checks the box and the outcome and logs `SKIP-note e3)`
    (dff556c).
  - `lockstep_limits` j) (default run; a battle condition): both on auto-run to the game's own end at the scenario's
    length (minute 800, "No opposing ships are in sight of each other and scenario length is reached!"): minute 800's
    hash line had the state equal on both (`all=17e91617d5d81e2a`) and the game's draws one apart (`sim=63577` and
    `63576`, `rng=` differs), as `lockstep_stress` f) under dist_rc060_2; both result boxes the same battle (434 to 174
    VP). Passed when run again (ended by the end question at minute 480, 425 minutes equal).
  - `battles` c/d) and c2) (`--stress`; the game's choice): all attempts but one ended before the preview: "The Spanish
    navy declines battle! You gain N VP!" (13) and "Enemy unable to challenge our forces in the area!" (12), as
    `battles` b) under dist_rc050_2. Passed when run again (90/90).
  - `air_units` d) (`--stress`; suite): the client's `airunits` met "error an event dialog is open": the game's news
    picture message of the new month ("The Italian Duce holds a belligerent speech! Tensions rise!") was still up after
    the event roll (the saved counts were right: 232 on both). `airunits(port, pid)` now answers the copy's dialogs and
    reads again (0dd4d8b). Passed when run again (12/12).
- **Suite fixes** (reg-070b): dff556c (`battle_invasion` e3), env `RTW3_INV_E3_TARGET`), 0dd4d8b (`air_units` d)); TESTING.md
  (the catalog, the environment variables, the game times, Mixed versions, Known traps).
- **Verdict: GO** for publishing 0.7.0 with exactly dist_rc070_2: no product issue. Neither of RC1's game error boxes came
  back (the fixed ambush path shown twice by e3)) and no other box came; every failure was a suite issue (fixed and run
  again) or a battle condition of the game (passed when run again); the mixed versions as designed.

## Fix 2026-10-28: the bare "Access violation ... Read of address 00000109" box: a campaign save cut short (v0.7.1-dev)
A 0.7.0 playthrough of two players on two PCs (no logs): a bare box titled "Rule the Waves III", "Access violation at
address 0113D52A in module 'RTW3.exe'. Read of address 00000109." (no PC or moment known). The same playthrough had a
full disk on the host ("Error in SaveDesignFiles procedure! Stream write error ...").
- **The routine (static).** RTW3.exe loads at a random 64 KB-aligned base, so only the address's low 16 bits are the
  rva's: 0xD52A. Of the 110 addresses 0x83d52a + k x 0x10000 in the code section (analysis base 0x830000), exactly
  one is an instruction that reads [reg+0x109] (checked from its routine's start): 0xc5d52a (rva 0x42d52a) `mov eax,
  [edi+0x109]` in `TBuilderNation.GetNationAreaShipString(anArea, ActiveOnly, ExcludeKE): string` (0xc5d4e0), its first
  use of Self (EDI = EAX at entry): `ShipList` +0x109. Self was nil; the report's base was 0xd10000. Its callers: the
  strategic map's hint (`WorldGlobeMouseMove` -> 0xce8d10 -> 0xcec8e8: "Map area: ..." with each nation's ships for
  `GetBNation(i)`, i = 0..8, unchecked; `GetBNation` (0xc80040) is nil for i >= Count), the map's area details
  (`WorldGlobeMouseUp` and `Details1Click` -> 0xcea020, the same loop), and the battle prompts of `DoPossessionBattle`,
  `DoHomeAreaBattle` and `CheckSurpriseAttack` for `PlayerNation` (inside the game's error handlers or the mod's
  `GuardedCall`: never a bare box). Only the map's handlers have no handler of the game, so their fault reaches
  `TApplication.Run`'s: the bare box. The other 109 addresses read no +0x109 (or are no instruction start).
- **How a nation goes missing.** `LoadBuildCampaign` (0xc998c4) empties `BuilderNations` (0xc99a89) and adds a nation
  per `[Nation<i>]` section until the first missing one (`IndexOf = -1`, 0xc9a41d), without an error; its "Loading ship
  designs" loop then calls `LoadDesignsFromLargeFile` (0xd49c5c) for the nil `GetBNation(i)`: "Error loading game!
  Access violation at address <base+0x519cb9> ... Read of address 00000004" (its own box), and it returns False with
  the nations before the cut. 0.7.0's `game::LoadCampaign` showed the strategic screen after a failed load too. A
  `.bcs` written to a full disk is cut short (the game writes `[VERSION]` last): the mod's reloads of the host's slot
  after a merge or the client's month (the host sends its slot's files) load it the same way.
- **Reproduced (0.7.0, dist_rc070_2):** slot 1 copied to a throwaway slot 61, its `.bcs` cut before `[Nation5]`; the
  bridge's `loadasync 1`, then `loadasync 61`: the game's box above ("... 008D9CB9 ... Read of address 00000004", base
  0x3c0000), `LoadCampaign slot 61 player 0 -> FAILED`, `nations` lists 0..4; the first posted WM_MOUSEMOVE over a sea
  area of `WorldGlobe` (373,59) shows the bare #32770 box "Access violation at address 007ED52A in module 'RTW3.exe'.
  Read of address 00000109." The RTW3MP_DIAG trace: rva 42d52a <- 4bc938 (0xcec8e8) <- 4b8e0d (0xce8d10) <- 52801e
  (`WorldGlobeMouseMove`) <- the VCL's mouse dispatch. Without a campaign loaded before, the map has no areas and no
  hint shows (the first two runs).
- **Fix 1, the load.** `game::LoadCampaign` refuses a `.bcs` without the nine `[Nation<i>]` sections or `[VERSION]`
  before the game's load, so the campaign in memory stays whole (`LoadCampaign refused: slot <n>'s campaign file is
  incomplete (<bytes> bytes, <k> of 9 nations, no [VERSION] section: a save cut short (a full disk?))`); after the
  game's load it checks `game::CampaignComplete` (nine nations, none nil, `PlayerNation` one of them) and reports a
  failure (`LoadCampaign: the loaded campaign is incomplete (...)`). Every caller already handles a failed load.
- **Fix 2, the map.** `WorldGlobeMouseMove`, `WorldGlobeMouseUp` and `Details1Click` (hooked) do nothing while the
  campaign in memory is incomplete, also after the player's own load of such a file (`the map's hint and details are
  off: the campaign in memory is incomplete (a failed load)`, at most once a minute).
- **Fix 3, the mod's own calls.** The mod's main-thread work (its timer, network messages and `RunOnMainThread` calls,
  so every bridge command) runs under an SEH guard: a game exception no game routine handled (a Delphi exception or a
  processor fault) is logged with the mod's call site and the 0.6.0 RTW3MP_DIAG trace (registers, objects, the game's
  return rvas; now also without RTW3MP_DIAG) and that piece of work is dropped, instead of the game's bare box with no
  trace: `ERROR: game exception <code> at rva <x> [<class>: <message>] in the mod's main-thread work (<function>, line
  <n>): caught, that work dropped`. `RunOnMainThread` records its caller (`__builtin_FUNCTION`, `__builtin_LINE`). The
  mod's C++ exceptions stay with its own catch blocks (/EHsc).
- **Test switches:** `RTW3MP_TEST_NOLOADCHECK=1` (the mod's loads take such a file: the game's own load) and the
  bridge's `pocguard` (`GetNationAreaShipString` with Self nil from the mod's main-thread work).
- **Proof (dist_av7_1, dist_av7_2):** the reproduction on the fix: `LoadCampaign refused: slot 61's campaign file is
  incomplete (495473 bytes, 5 of 9 nations, no [VERSION] section ...)`, `nations` 0..8, 77 posted mouse moves, no box.
  With `RTW3MP_TEST_NOLOADCHECK=1`: the game's own "Error loading game!" box, then the map guard's line and no bare box;
  `pocguard`: `ERROR: game exception c0000005 at rva 42d52a in the mod's main-thread work (Execute, line 100): caught,
  that work dropped` with the trace, no box (without RTW3MP_DIAG). `regression.py --only
  session,divisions,legacy_fleet,battle_invasion,relations_war --legacy-fixture ...` on dist_av7_2: 234/234 checks
  passed (game open 510 s); the host's and client's logs hold no refused load, no map guard line and no game exception.
  Game open in all: 699 s.

## Fix 2026-10-28: a joined player's trade protection and raiders, and View data of the player's own ships (v0.7.1-dev)
Two reports from a two-PC game on 0.7.0 (host = nation 0, the joined player = nation K): "Non host players' trade
protection ships don't stay" (the next month they are back in active service) and "Non host also can't view the data of
their own ships via the 'View Data' item" of the ship list's popup menu.

**Reproduced on the released 0.7.0** (the installed dist_rc070_2, the 1900 fixture, host and client on one PC; the new
scenario `ship_status`, `--only session,ship_status`, two runs, 199 + 191 s with the game open): 73/78, the five
failures exactly the two bugs.
- On the client (nation 1 = Germany at war with Spain, its war counter 2: the popup enables *Trade protection* and
  *Raider* only at war) Hela and Gazelle were set to trade protection (3), Hertha to raider (the game sets 5), three
  more to reserve (1), mothballed (2) and foreign station (6), all through the game's popup. A watcher of the saves
  showed where the war statuses go: the client's slot 77 at its Submit 3/3/5, the host's merged save 3/3/5, the host's
  month at `nation 1 month: at war, 2 ships on trade protection of 4 required` (the mod's check at
  CheckShipsOnForeignStations) still 3/3, then the save of the battle phase 0/0/0 with `OldStatus` 0 (so already 0 at
  `DoEndOfTurn`'s first call, `RecordOldStatusForShips` 0xd2a5e0), the host's memory and slot 1 0/0/0, the client's next
  month "AF". Reserve, mothballed and foreign station stayed. Not the merge, not the battle (the battle-phase save was
  already 0) and not the AI-moves restore (no `AI moves undone for nation 1` in M1).
- View data on the client for the German light cruiser Hela showed "HELA / COLLINGWOOD CLASS (1899) / BRITISH
  BATTLESHIP / 14000 tons, 18 knots" with the British flag (one PrintWindow of the game's dialog, kept in the session
  folder); on the host its own Collingwood showed correctly.

**Cause 1, trade protection and raiders: a side effect of the maintenance cost, under the relations mask.**
- A probe (a diagnostic build, removed afterwards): from DoStrategicTurn's start a thread polled the Status byte of every
  ship of the joined nations; on a change it suspended the main thread, read EIP and the stack's return addresses,
  resumed it and logged them. The month's log:
  `relations v2: ReduceTimeLimits runs without the pairs of nation(s) 1 (player's rules)`,
  `AI suppressed: AINationAdjustBudget / AIMonthlyBuild / CheckAIRebuilds for nation 1`, then
  `nation 1 ship 5 status 4 -> 0 at eip 0xc4f392; returns 0xc5d33e 0xc7c083 0xce1dd4 ...` (EIP in
  `TBuilderNation.GetMonthlyExpenses` right after its `GetTotalMonthlyMaintenanceCost` call, 0xc5d33e in
  `GetMonthlyBalance`, 0xc7c083 ReduceTimeLimits' `mov eax, edi; call GetMonthlyBalance` of its loop of the computer's
  nations, the one that calls HaltMostExpensiveShip while the balance is negative, 0xce1dd4 DoStrategicTurn's call of
  ReduceTimeLimits), and ships 6 and 10 3 -> 0 in the same pass.
- `TBuilderNation.GetShipMaintenanceCost(aShip, AllActive)` (0xc59b60), at its end:
  `mov eax, edi; call NationIsAtWar; test al, al; jne done; movzx eax, [ebx+0xac]; add al, 0xfd; sub al, 2; jb reset;
  sub al, 2; jne done; reset: mov byte [ebx+0xac], 0` (0xc59f1d-0xc59f39): a ship on trade protection (3) or raiding
  (4, 5) returns to active service whenever its nation's maintenance is computed while the nation is not at war.
  `NationIsAtWar` (0xc634f8): PlayerNation while `[0xf52ff4]` (the war counter) > 0, or `Tension` (+0x256) > 40, or
  `IsAtAIWar` (0xc68ecc: not PlayerNation; any of the cells +0x25a.. (AI tension with 1..8) > 40).
- Relations v2 B2 (0.5.0) runs ReduceTimeLimits (and the other computer relation routines) with every pair (K, j) of
  a nation under the player's rules at tension 5 (`AIMaskEnter`), so inside it K is at peace with the computer's
  nations, and its halt loop computed K's balance: every month K's trade protection and raiders went back to active
  service. In single player the player's nation uses the war counter (NationIsAtWar's first test), and on the player's
  PC the client's own war counter is in place (also in the battle slot, `MakeNationPlayer`), so only the host's month
  had it. The host's own war counter (-68 in the test) played no part.

**Cause 2, View data: nation 0 hard-coded.** `TfrmBuildCamp.Data1Click` (0xca2f5c, the item of both popup menus of the
ship lists): `push 1; push 0; mov edx, [eax+0x594] (lvShips); xor ecx, ecx; call ShowShipDataFromLV`; the same nation 0
in `lvShipsDblClick`, `TdlgMoveShips.Viewdata1Click`, `TdlgMapDetails.D1Click` and `Shipdata1Click`.
`ShowShipData(aShip, aNationIdx, Correct, ShowPicture)` (0xcb3528) looks the ship's `DesignRefId` (+0x08) up with
`GetBNation(idx).GetShipDesignFromId` (nil: it returns silently), sets the design's Nation (+0x14) := idx and shows
nation idx's flag and name. Design ids are per nation (2..N for every nation of the fixture), so on a joined player's
PC (PlayerNation = nation K, nation 0 the host's) a ship showed the host's design with the same id. Another screen that
reads nation 0 for the player, like the research screen (0.2.8) and the relations view (0.4.1).

**Fixes** (game.cpp; protocol 3 and the live-battle engine 18 unchanged):
- `AIMaskEnter` notes the ships with Status 3-5 of every masked nation; `AIMaskLeave`, after the real relations are
  back, gives each of them that the masked routine set to 0 its status again when the nation is at war by the game's
  own test (`NationAtWarNow`: the player's war counter, else the tension with nation 0 or any AI tension above 40):
  `relations v2: nation 1 at war: 3 ship(s) on trade protection or raiding kept (ReduceTimeLimits saw it at peace)`.
  At peace the game's rule applies, as for its own player; ships sunk, interned or changed otherwise are left alone.
- A hook of `ShowShipData` (naked thunk: EAX/EDX kept, ECX replaced): a ship of the player's nation K asked for as
  nation 0's on a PC where PlayerNation is BuilderNations[K] is shown as nation K's (`ship data: a ship of the player's
  nation 1 shown with its own designs (the game asked for nation 0's)`). Elsewhere (the host, single player, the battle
  slot, the views that put K at index 0) nothing changes. `ShowShipDataFromLV` keeps index 0, so the player's own ships
  under construction still show. Read-only bridge command `shipdata` (the last call: ship, owner, asked, used, the
  design's class).

**Version mixes** (each fix lives on one PC, no message changes):
- 0.7.1 host + 0.7.0 client: the statuses are kept (the host's month); the 0.7.0 client's View data still shows the
  host's designs.
- 0.7.0 host + 0.7.1 client: View data shows the player's own ships; the 0.7.0 host still returns the trade
  protection and raiders to active service every month.
- Live battles between 0.7.0 and 0.7.1 PCs as before (engine 18).

**Proof** (dist_tp7_2: both fixes plus the probe, `--only session,ship_status`, 197 s): 78/78. Over M1 (a war month
with the player's battle fought on the client) and M2 (war) the host keeps Hela 3, Gazelle 3, Hertha (set 5: a raider
that must break out; 4 after M1, the game's own breakout), Brandenburg 1, Wittelsbach 2, Victoria Louise 6 in memory
and in slot 1, and the client's next months and slot 77 show them (the list: TP, TP, R, RF, MB, FS); the probe shows the
reset and the restore of the same ships around the same routine (`status 4 -> 0` in ReduceTimeLimits' halt loop, then
`0 -> 4` at the mask's end); the host's 48 ships unchanged. View data on the client for Hela: "HELA CLASS (1899) /
GERMAN LIGHT CRUISER / 3200 tons, 22 knots", `shipdata` `ship=Hela owner=1 asked=0 used=1 design=Hela dnation=1`; on
the host its Collingwood as before (`owner=0 asked=0 used=0`). Back to active fleet and at peace (M3) every ship is 0.

**Suite on the final build** dist_tp7_3 (the two fixes, the probe removed), `--only session,battles,divisions,ship_status`:
222/224 with the game open 722 s; divisions 42/42, ship_status 62/62 (its months M1 with battle 5 of the month fought on
the client, M2). The two failures are one box, counted by `battles` and by `session`'s whole-run check: the known save
collision `Error in SaveBuildCampaign procedure! Cannot create file "...\Game1\RTWGame1.bcs". The process cannot access
the file because it is being used by another process` on the host one second after battle 5 was applied in `battles`
A2 (the runner reads the host's slot right after a merge; [Known traps](docs/TESTING.md#known-traps)). After that run the
scenario's save watcher (each version of slot 77 and slot 1 during a month, used for the diagnosis above) became
opt-in (env `RTW3_SS_WATCH=1`): it reads the slots right after the game's saves, the same collision's risk. The same four scenarios again on dist_tp7_3 with the committed suite (the watcher off): 235/235 (session 17/17, battles 114/114, divisions 42/42, ship_status 62/62), 697 s with the game open; the collision did not recur.

## Fix 2026-10-28: a full disk: a failed save stops the month, the low-disk notice, and what grows over a long session (v0.7.1-dev)
Two players of 0.7.0 on two PCs reported that the host's game showed "Error in SaveDesignFiles procedure! Stream write
error: There is not enough space on the disk"; the host's disk really was nearly full, and the same playthrough showed
a bare "Access violation at address 0113D52A in module 'RTW3.exe'. Read of address 00000109." box (investigated on its
own branch: [the bare box](#fix-2026-10-28-the-bare-access-violation--read-of-address-00000109-box-a-campaign-save-cut-short-v071-dev)).
Branch fix-disk from dev-071 a3b17e5 (0.7.1-dev).
- **The game's failed save** (static reading of 1.01.44; [GAME-INTERNALS](docs/GAME-INTERNALS.md#the-game-and-the-mod-facts-you-can-rely-on)).
  `SaveBuildCampaign` (0xc8e5c4) writes, when no battle is in progress, `ClearUnusedDesignFiles` and
  `SaveDesignFilesInOneFile` (0xd481ac) for nations 0-8 (`DesignFiles<n>.des`: "v10139", the count, the designs,
  `TStringList.SaveToFile`), then `SaveAircraftData`, `SaveMonthRecords` (the `.sta`), the `.bcs`, the `.off`
  (`Autosave.*` for an autosave) and with BeforeBattle `BattleInfo.bbi`. Every part has an except handler that shows the
  error with `ShowMessage` (0xab7970) and goes on: "Error in SaveDesignFiles procedure!" + the message (0xd4836c), then
  the routine returns its `[ebp-0xd]`, which is still True; "Error in SaveBuildCampaign procedure!"#13 + the message
  (0xc8f450) ends the save early. `SaveToFile` opens with `fmCreate` (the file emptied first). So a full disk gives up to
  ten boxes (nine design files, then the `.bcs`), the save returns as usual, and the files it could not write are empty
  or old; the caller can't tell.
- **What the mod did with it (0.7.0).** `game::SaveGame` always returned true. After a failed save the host's month
  (`RunHostTurn`) merged the turns into the slot and loaded it again, `BroadcastState` packed the slot's files for the
  players, a joined player's Submit packed its slot, and a battle's result was read from the battle slot's save: a
  campaign without designs (empty `DesignFiles<n>.des`) loaded on the host and sent to everyone, or a turn taken from
  empty files. A guess, not shown: the reported access violation may come from such a load (the other branch's
  question). Also `WriteFileBytes` (every write of the mod: the merge, the month package, the battle slot) fell back to
  writing a file in place after any error; on a full disk that empties the old file (CREATE_ALWAYS) and then fails.
- **The fix.**
  - `game.cpp`: a hook of `SaveBuildCampaign` (`d_SaveCampaign` -> `CB_SaveCampaign`, the call inside
    `__try`/`__finally`, nested saves counted) records every save, the game's own and the mod's: a box of the game during
    it (the `ShowMessage` hook, now always installed, found from `SaveBuildCampaign`'s handler, else from
    `CheckForBattles2`'s; the B6 uses unchanged) or a file it had to write (`.bcs`/`.off` or `Autosave.*`, `.sta`, the nine
    design files when ScenarioInProgress = -1) missing, older than the save (2 s slack) or cut short makes it a failed
    save: `save: slot <n> FAILED (save; <MB> MB free on its drive): <the box's text / the file>`. Only the save's first
    box is shown; its repeats go to the log (`save: the game's box <k> of this save not shown: ...`). `game::SaveGame`
    returns false after a failed save; `SaveOkCount`, `SaveFailCount`, `LastSaveFailed`, `LastSaveError`; the bridge's
    `savestate`.
  - `session.cpp`: a failed save stops the step that saved (`SaveStopped`): the host's month (nothing merged, no reload,
    the turns kept, back to planning), sending the new month, the battle phase (the battles wait instead of being given
    to the computer), a player's turn, a player's save before a live battle. The notice and chat line: "The host's disk is
    full (<n> MB free on drive C:): the month stopped; the last good state is kept. Free space and save again (the game's
    Save button): it goes on then." (the host's to everyone; a player's own one locally and "[disk] My game could not save
    (the disk is full): my turn waits until it can." to the host; "... game could not save the campaign" when the disk
    isn't low). Once any save works (`SaveOkCount` moved, e.g. the player's own Save), `Tick` runs the step again; a
    merge whose write failed on a full disk stops the month the same way (`WriteFailedDiskFull`). A joined player's
    battle result is never read from a failed save (`SaveBattleSlot`: the result goes as failed, the host lets the game
    resolve the battle). (Corrected 2026-10-28, 0.7.1 docs: only a battle of the computer's generator is resolved by
    the game; one of the player's own generator is no battle that month, as ARCHITECTURE says.) A received month that a full disk could not store stays pending and is stored again every 15 s
    ("Your disk is full ...: the new month could not be stored. Free some space: it is stored and loaded then by
    itself.").
  - Low disk space: before the month's saves (host: the Turn, the new month, the battles; client: its turn, storing the
    month) `LowDiskCheck` reads the save folder's drive (`FreeSpaceMB`, GetDiskFreeSpaceExW); below 500 MB
    (`kLowDiskMB`) a notice and a chat line, once a month: "Low disk space on drive C: <n> MB free; the game may fail to
    save. Free some space." The value: the largest test campaign's slot is 11.6 MB (the 1958 missile campaign; 1900:
    2.6 MB); a month writes the slot, the autosave and, on a joined player's PC, the battle slot, each file briefly twice
    while it is replaced: about 50 MB at once at most, so 500 MB warns ten such months ahead.
  - `util.cpp`: `WriteFileBytes` doesn't write in place after ERROR_DISK_FULL or ERROR_HANDLE_DISK_FULL (the old file
    stays; `write <path> FAILED: the disk is full ...; the file is left as it was`); the log rotates at 32 MB within a
    session (the earlier lines in the `.prev.log`: at most two files per log tag).
  - Test hooks: the bridge's `diskfree <MB>|off` (what the free-space check reports), `diskfull on|off` (a `WriteFile`
    hook installed on the first `on`: writes into files under the Save folder fail with ERROR_DISK_FULL after the file
    was created or emptied, as on a full disk; the game's saves and the mod's writes alike), `diskinfo`.
- **The growth audit** (the question: does anything the mod causes grow without bound?).
  - Static: every list add reachable from `LoadBuildCampaign` four calls deep (117 routines, `addscan.py` in the
    offline workspace): besides the intel reports (0.2.2), the air units (0.3.3) and the campaign divisions (0.6.1) only
    `DesignStudyList` (+0xaec) is appended to without clearing, from `[DesignStudies]`, which nothing writes (the
    string's only user is the load; `TDesignStudy.SaveMe` has no caller): it stays empty, not fixed. `MonthRecords` is a
    fixed array (the `.sta` keeps its size); nations, designs, officers, map areas and ships are new objects at every
    load.
  - The mod's files: its slots 77 and 87 are written over (`ApplyStateBundle` deletes the old save files first; a live
    battle's B deletes the old `*.skv`); `ls_desync_*.txt` keeps the 12 newest (`PruneDesyncReports`, since 0.4.0);
    `rtw3mp_*.ini` are one per slot; the loader log (`RTW3MP_DIAG` only) is written anew at each start; the logs rotated
    only at a start (fixed: 32 MB). Nothing in the game's folder or the temp folder.
  - Live, the released 0.7.0 (dist_rc070_2), `disk_growth` (game open 175 s): six save and reload cycles on each copy
    changed nothing (host slot 1: 2,556,688 B, IDNo 40553, 6 reports, 0 air units, 419 officers, 171 designs, 38079 .bcs
    lines; client slot 77: 1,748,992 B), private memory +2.5 to 10 MB per reload (the known leak, Known limitations).
    Eight months (Aug 1900 -> Apr 1901): host 2,556,688 -> 2,655,147 B (+12 KB a month: .bcs lines 38079 -> 39594,
    designs 171 -> 178, reports 6 -> 9), client 1,748,992 -> 1,833,725 B, the logs +6.6 and +2.3 kB a month, no DESYNC
    report, no new file in the game's folder. So 0.7.0's sessions grow only as the campaign does; the reported full disk
    was not the mod's doing (as far as these runs show: no live battles in them).
  - Live after the fix (dist_dk7_3, `disk_growth`, game open 171 s): the same: the reload cycles change nothing (host
    2,556,688 B, 314 ships counted now; client 1,748,992 B), eight months host 2,556,688 -> 2,679,810 B, client 1,748,992
    -> 1,854,282 B, logs +6.8 and +2.3 kB a month, the temp folder one new file of 53 B, nothing in the game's folder.
- **The live proof of the full disk** (`disk_full`, the bridge's TEST `diskfree` and `diskfull`; dist_dk7_1: 54/54, game
  open 89 s). The simulated full disk gives the report's box word for word: on the host's Turn the game's save showed
  nine "Error in SaveDesignFiles procedure!Stream write error: There is not enough space on the disk" and then "Error in
  SaveBuildCampaign procedure!" (box 10), left all nine `DesignFiles<n>.des` of slot 1 at 0 bytes, and returned as
  usual. With the fix the first box showed, the nine others went to the log (`save: the game's box <k> of this save
  not shown`), `save: slot 1 FAILED (save; 30 MB free on its drive): Error in SaveDesignFiles procedure!Stream write
  error: There is not enough space on the disk ...`, "disk: a save failed; the month stopped": no merge, no reload,
  the host's date unchanged, the notice and the chat line on both copies. With the simulation off the host's `save`
  worked and the month went on by itself right after it ("the save works again; the month goes on", "1 submissions
  merged"); the client's next month had nine complete design files. The client's Submit on a full disk: `save: slot 77
  FAILED`, "your turn stopped", not submitted, "[disk] My game could not save (the disk is full): my turn waits until it
  can." on the host; after its `save` the turn went in by itself. A write of the mod on a full disk (`writetest`):
  error 112, the old file unchanged, no `*.mptmp`. The low-disk check at 120 MB: one line on each copy in the month.
- **Review** (a code-review agent on the diff): a time-limited month (`g_forced`) called `RunHostTurn` again every tick
  after a failed save (a box every 250 ms), now not until a save works; a stopped Submit could submit the next month's
  turn by itself, now dropped when a new month is loaded (the host's stops when a new month goes out); the game's box
  of a failed save runs a message loop in which the session's timer and network messages ran (a player joining would
  save again inside the save, and that inner save looked successful): now `Tick` and `OnNet` wait while
  `game::SaveInProgress()` and `SaveGame` refuses a nested save.
- **The check is fix-av109's rule.** The save's check of the `.bcs` uses `CampaignFileProblem` (the nine `[Nation<i>]`
  sections and `[VERSION]`, which the game writes last), copied word for word from fix-av109 (5fb5687), whose
  `LoadCampaign` refuses such a file: one rule for a save cut short. The merge of both branches (int-071) keeps one
  copy, used by the load's refusal and the save's check.
- **Suite** (`tools/regression.py`): `disk_full` (default run, group `disk`), `disk_growth` (`--stress`, group `disk`),
  `run_month(started=True)` and `month_prepare` (a month the caller set going), `BOX_EXPECTED` (the error boxes a
  scenario provokes are not the run's failure).
- **Commits and runs.** The fix eea420c, the suite ae73201. The final build dist_dk7_4 (`RTW3MP.dll` SHA-256
  B7875DCD220E...): `--only battles,lockstep_month,disk_full,disk_growth` 331/331 (session 17, battles 102,
  lockstep_month 51, disk_full 54, disk_growth 107), game open 523 s. Before it: dist_dk7_3 `--only
  battles,lockstep_month,disk_full` 212/212 (403 s) and `--only disk_growth` 107/107 (171 s), dist_dk7_1 `--only
  disk_full` 54/54 (89 s); the released 0.7.0 `--only disk_growth` 106/107 (175 s; the failure the check's own, fixed).
  After the battles and the live battle of the final run the host's slot held 25 files (16 before: the battle's save,
  its logs and the like, under fixed names), and the next eight months grew it as before (3,299,695 -> 3,433,269 B, the
  client's 2,054,116 -> 2,145,932 B); private memory still grows by about 4 MB a reload (the known leak, Known
  limitations), not on disk.
- **Merge smoke (int-071: fix-av109, fix-disk and fix-tp merged on dev-071, 2026-10-28).** dist_i71_1 (`RTW3MP.dll`
  SHA-256 BF6B5271A544...), `--only session,disk_full,ship_status,divisions,battles`: 277/277 (session 17, battles 102,
  divisions 42, ship_status 62, disk_full 54), game open 575 s, no game error box. Both logs: "Failed saves: ok"; the
  host's: "relations v2: nation 1 at war: 3 ship(s) on trade protection or raiding kept (ReduceTimeLimits saw it at
  peace)" in ship_status's war months; the only failed saves are disk_full's own (slot 1 at the host's Turn, slot 77 at
  the client's Submit); no refused load, map guard line or game exception on either copy.

## Fix 2026-10-28: a failed save is not always a full disk: a file in use is saved again by itself (v0.7.1-dev)
On int-071 (the three 0.7.1 fixes merged, 35eb80f). [The full-disk fix](#fix-2026-10-28-a-full-disk-a-failed-save-stops-the-month-the-low-disk-notice-and-what-grows-over-a-long-session-v071-dev)
stopped the step at every failed save and called it a full disk ("The host's disk is full (...)" when the drive had
less than 500 MB free, else "The host's game could not save the campaign"). A save also fails when another program
holds one of its files for a moment: "Error in SaveBuildCampaign procedure! Cannot create file ... The process cannot
access the file because it is being used by another process". The README's troubleshooting names antivirus and
OneDrive for it; the game's save folder is in OneDrive on many PCs (this one too), and the suite's runner caused it
once by reading slot 1 right after a save. Such a moment stopped the month until the host saved by hand.
- **The game's side** (live, the TEST `savelock` below; [GAME-INTERNALS](docs/GAME-INTERNALS.md#the-game-and-the-mod-facts-you-can-rely-on)).
  The game creates each file with `fmCreate` (CreateFile without sharing), so any other open handle fails it with
  error 32: "Error in SaveBuildCampaign procedure!"#13"Cannot create file "...\Game1\RTWGame1.bcs". The process cannot
  access the file because it is being used by another process", about 20 ms into the save (the design files and the
  `.sta` already new, the `.bcs` and `.off` old). The system's part comes from `FormatMessage` in the user's language
  (Delphi's `SysErrorMessage`, the trailing dot cut); "Cannot create file", "Stream write error" and the "Error in ..."
  prefixes are the game's own English. The same save a moment later works.
- **The cause** (`game.cpp`). `SaveEnd` names it (`game::LastSaveCause`; `savestate` `cause=none|full|inuse|other`;
  the log `save: slot <n> FAILED (save; <MB> MB free on its drive, the slot's files <KB> KB; the cause: <cause>)`):
  a full disk when a box says "not enough space on the disk", "disk is full" or the system's words for errors 112
  and 39; else a file in use for "being used by another process", "sharing violation", "Cannot create file" or the
  system's words for errors 32, 33 and 1224; else, with no such text, a full disk when the drive has less free space
  than the slot's files took when the save began (plus 1 MB); else another error. A save that works sets the cause
  back to none. The players' text (`LastSaveText`, `LastSaveFile`) leaves the folders out (only the double quotes
  Delphi puts round a file name end a path: an apostrophe may be part of a folder's name).
- **A file in use is saved again** (`session.cpp`). The five steps that stop on a failed save (the host's month,
  sending the new month, the battles; a player's turn, a player's save before a live battle) save with `StepSave`
  (`game::SaveGame(true)`: the game's box about a file in use stays off the screen, logged `save: the game's box <k>
  of this save not shown (a file in use: the mod saves again)`). `SaveStopped` with that cause holds the step and
  `SaveRetryTick` (in `Tick`, when the game is idle; for the save before a live battle also while the battle waits to
  load) saves again into the same slot 2 s later, up to five times (`disk: the month: saving again (<k> of 5)`); the
  last try shows the game's box, so a failure that lasts shows one box and a passing one none. Once a try works the
  step goes on with that save (`disk: the step goes on with the save that just worked`: a second save at once would
  meet the other program again, which reads the file just written). After five failed tries the step stops as for a
  full disk and waits for a save that works (the player's Save). A full disk and another error stop at once, as
  before, each with its own words. A stop that was no save of the game's names its cause itself (the merge's own write
  on a full disk: `SaveStopped(Turn, false, DiskFull)`); a `SaveGame` that refused to save (an event dialog) falls back
  to the free-space rule (`FailedSaveCause`). A joined player's battle slot is saved again at once, five times 1 s
  apart (`SaveBattleSlot`; the game is busy for those seconds), before the battle's result is given up.
- **The words** (notice and chat line; the host's to everyone, a player's own locally and its line to the host):
  - a file in use, saving again: "A file of the host's campaign save was in use by another program (for example
    OneDrive or antivirus): the month waits; it saves again by itself." (a player: "A file of your campaign save ...:
    your turn waits; ..."; to the host: "[disk] My game could not save (a file of the save was in use by another
    program): my turn waits; it saves again by itself.");
  - still in use after five tries: "A file of the host's campaign save is still in use by another program
    (RTWGame1.bcs): the month stopped; the last good state is kept. Close that program or pause OneDrive's sync, then
    save (the game's Save button): it goes on then." (to the host: "[disk] My game could not save (a file of the save
    is still in use by another program): my turn waits until it can.");
  - a full disk, unchanged: "The host's disk is full (<n> MB free on drive C:): the month stopped; the last good state
    is kept. Free space and save again (the game's Save button): it goes on then.";
  - another error: "The host's save failed (<the game's text, without folders>): the month stopped; the last good
    state is kept. Save again (the game's Save button): it goes on then." (to the host: "[disk] My game could not save
    (the save failed: <text>): my turn waits until it can.").
- **Test hook.** The bridge's `savelock <seconds>|off [file]`: the next save finds a file of its slot (default
  `RTWGame<slot>.bcs`) opened without sharing from its start; a thread of its own closes it <seconds> s later (the main
  thread may sit in the game's box). When another program holds the file itself it waits for it up to 3 s.
- **Review** (a code-review agent on the diff): the merge's own disk-full stop read the cause of an older save (a stale
  "in use" would have saved again every 2 s without end): the cause is now passed, reset after a save that works, and
  falls back to the free space; the players' text kept folders after an apostrophe in a folder's name.
- **Live proof** (`disk_full` f and g, [TESTING](docs/TESTING.md#scenario-catalog)). dist_sr7_1, `--only
  session,disk_full`: 104/106, game open 169 s (the two failures the checks' own: the host's date is read from its
  `.bcs`, empty while the file is held; the host's status line is soon replaced by "All players are ready"). f) the
  client's Submit with `RTWGame77.bcs` held 4 s: failed at 19:38:45.16 (cause inuse, no box), try 1 failed, the file
  free at 49.15, try 2 worked at 49.50 and the turn went in on that save; the host's Turn with `RTWGame1.bcs` held 5 s:
  tries 1 and 2 failed, the next worked, the month went on and was merged; no box on either copy, the lines on both.
  g) held 25 s: tries 2.25 s apart (2 s plus the save), "saving again (5 of 5)", the month stopped (cause inuse), the
  game's box once (the fifth try's), "... is still in use by another program (RTWGame1.bcs) ..." on both copies;
  nothing merged and the date unchanged; the file free, the host's `save`: the month went on by itself.
  dist_sr7_2 (the review's fixes), `--only session,disk_full,lockstep_month,battles`: session 17/17, lockstep_month
  51/51, battles 111/112 (c2: every battle of both tries avoided by the computer, no preview to decline: the game's
  roll, seen before), disk_full 84/89: g) passed, but f)'s host lock was not taken: `TEST: savelock: RTWGame1.bcs
  cannot be opened (error 32)` at 20:07:22.495, 14 s after a save, i.e. another program really held the host's file
  then (the folder is OneDrive's); the game's save a few milliseconds later worked. `savelock` now waits for such a
  holder (up to 3 s).
  The final build dist_sr7_3 (`RTW3MP.dll` SHA-256 1558CC15D5E8...), `--only session,disk_full,lockstep_month,battles`:
  session 17/17, disk_full 89/89, lockstep_month 51/51, battles 99/102, game open 461 s. The three battles failures
  are the scenario's, not saves (no failed save, no save again, no hidden box and no exception in either log during
  it): b) the month's second battle for the player loaded the battle slot (20:11:12) before the runner read the first
  one's result from it (20:11:20), so the cross-check compared another battle's slot; c2) the declined battle 17 got no
  "resolved by the game: battle|no battle" line (the game's resolution). `--only session,battles` again on the same
  build: 107/107 (battles 90/90), game open 250 s. Game open in all: about 25 min (169 + 591 + 461 + 250 s).
- **Commits.** The fix afc3834 (with ARCHITECTURE and GAME-INTERNALS), the suite 463a357 (with TESTING).

## Testing 2026-10-28: the 0.7.1 release candidate dist_rc071_1 (v0.7.1)
The release candidate dist_rc071_1 (int-071 30e1ed1: 0.7.1, live-battle engine 18, protocol 3; RTW3MP.dll SHA-256
814AB3014D30E8F3..., version.dll 45E7D1DD8865EC69...): the released 0.7.0 with the three 0.7.1 fixes (the cut-short
save's bare box, a joined player's trade protection and raiders and View data, a full disk and a file of the save in
use). Run on the reg-071 runner, installed from its frozen snapshot (`--dist`), the released 0.7.0 (dist_rc070_2) put
back after every run (hash checked); slots 1 and 9 and the Preferences identical to their backup after every run, the
save folders as before (Game1, Game2, Game9), no game folder copy, delay proxy, `python` or `RTW3.exe` left, the game
lock released. The logs of every run are kept with the developers' session files (`r711\logs_*`).
- **Runs** (checks, seconds with the game open):
  - `--group disk`: 213/213 (310 s): `disk_full` 89/89 (with f) and g): a file of the save in use, saved again by
    itself or the month stopped after five tries), `disk_growth` 107/107.
  - The default run (34 scenarios, `--legacy-fixture`): 1456/1465 (3163 s): `ship_status` 62/62, `lockstep_month`
    51/51, `twopc` 61/61, `lockstep_air` 41/41; the failures below (`battles` b) and c2), `battle_raider` c),
    `battle_invasion` e2) and e3), `disk_full` stopped at its first month, `legacy_fleet` not run). No game error box.
  - Mixed versions (`mixed_071.py`, [TESTING](docs/TESTING.md#mixed-versions)): a) a 0.7.1 host with a 0.7.0 player
    87/87 (224 s), b) a 0.7.0 host with a 0.7.1 player 87/87 (205 s), below.
  - `--only battles,battle_invasion` with the suite's fixes: 250/250 (520 s): `battles` b) fought, c2) an injected
    battle declined, `battle_invasion` e1) to e3) (e3) at Sakhalin, 3 ships of J against 1: "had no nation ... gave them
    nation 0").
  - `--group relations`: 414/416 (785 s): the client's SelectForce box (below), counted by `war_effects` and by
    `session`'s whole-run check; `battle_raider` 94/94 with its fix, `relations_war` 19/19.
  - Not run after the box (NO-GO): the `--stress` run (the long `lockstep_month`, `lockstep_stress`, the missile and
    legacy campaigns, `disk_growth` after the other campaigns) and `--only lockstep_air,disk_full,legacy_fleet` for the
    `disk_full` fix.
  - With the game open: 310 + 3163 + 224 + 205 + 520 + 785 = 5207 s.
- **The game error box (NO-GO).** In the `--group relations` run, `war_effects` M1 (Sep -> Oct 1900, nation 1 at war
  with Russia, its ally the USA), the client's battle 1 at 21:48:35-37: the host's battle of the player's generator for
  nation 1 (`the player's battle generator for nation 1 (war counter 1): 6 attempt(s): 3>1 (area by the game); 1>3 The
  Baltic; ...`, `battle request 1 ... fought by Captain`). On the client: `notice: Battle! Germany meets Russia in .`,
  the battle slot loaded, the map message accepted, then the game's box "Error in SelectForce procedure! Access
  violation at address 00777A62 in module 'RTW3.exe'. Read of address 0000000D" (RVA 0x3b7a62: `SelectForce`'s
  fallback reads the nil main division of a side that has no force after its tries,
  [GAME-INTERNALS](docs/GAME-INTERNALS.md#the-other-relation-and-war-routines)), `DoBattle(opponent 3) -> 0`,
  `StartPlayerBattle 3 attacks 0 in  -> none`, `battle 1: declined`, and at the next load `the last battle's area
  Indian Ocean and possession Tanganyika cleared`: the game's `CheckAttackLocation` (the attempt's area left to the
  game) chose Russia's attack on Tanganyika. The host: `battle 1 applied: 2 ships changed (2 fields), 0 removed, VP
  0/0 ...; the player declined battle`, the month went on, no other box. Not the scenario's TEST draws (`allyrand`
  patches `Random` only inside the allies' war help). Not new in 0.7.1: the same box (the same RVA, 0x3b7a62) came on
  0.6.0 (2026-10-23, a `--group relations` run of the SaveBuildCampaign fix, `war_effects` M2's battle of the player's
  generator "Germany vs Russia in Northern Europe"), before the runner recorded the boxes answered in a client's
  battle; the default run's `war_effects` of this candidate had none. A joined player sees the game's error box and
  the battle is lost as a decline: a product issue (the battle of the player's generator on the player's PC, where the
  0.6.1 fix guards only the battles the host's game resolves), so by this regression's rule no release with it.
- **0.7.1's fixes as tested.** `ship_status` 62/62 (the statuses kept over the war months on both PCs, View data of
  the player's own ships); `disk_full` 89/89 with the file in use; no "save: slot N FAILED", "saving again" or "the
  save worked again" outside `disk_full`'s own TEST windows in any log of these runs (the saves are in a OneDrive folder
  on this PC: no real file in use met). The mixed versions as the fix entry says: a) the 0.7.1 host keeps the 0.7.0
  player's trade protection and raider (Hela 3, Gazelle 3, Hertha 4; `relations v2: nation 1 at war: 3 ship(s) on trade
  protection or raiding kept (ReduceTimeLimits saw it at peace)`), the 0.7.0 player's View data shows the host's design
  with the same id (Hela as "COLLINGWOOD CLASS (1899)"); b) the 0.7.1 player's View data shows its own design
  (`shipdata` `ship=Hela owner=1 asked=0 used=1 design=Hela`), the 0.7.0 host returns the trade protection and the
  raider to active service (Hela, Gazelle, Hertha 0: the 0.7.0 bug); both: the join line (`Captain: mod <the other>,
  live-battle engine 18 (this host: 18), player's relation rules 1, battle generator 4, campaign divisions 1`), two
  months merged, the host's battle against the player fought live (514 and 424 minutes equal, no DESYNC, ended by
  agreement), no box.
- **Failures and their classes** (besides the box):
  - `battles` b) (default run; the game's choice): nine attempts, all "avoided" ("The Spanish navy declines battle!"),
    as under dist_rc050_2. Passed when run again.
  - `battles` c2) (default run; suite): its three injected battles were avoided by the enemy and a battle of the
    player's generator (kind 1) was declined in its preview in the same month; c2) took it for its own and checked the
    injected battles' way, while the host resolves a generator battle as designed (`battle 20 (kind 1): no battle
    (Captain declined it)`). Only an injected battle counts now (b753578). Passed when run again.
  - `battle_raider` c) side 0 and side 1 fought (default run; suite): the battle slot read while the client copied its
    next battle into it, 0.47 and 0.41 s after "fought" (once no file, once the next copy: "33 sunk in the battle slot,
    afloat in the merged save"), the race `battles` b) already leaves out of its cross-check. Such a read is a
    `SKIP-note` now (`battle_slot_race`, d0e490d; self-test 139/139). Passed when run again (`--group relations`).
  - `battle_invasion` e2) (default run; suite): the landing at Kiautschou Bay the client reported was applied by the
    host (`Kiautschou Bay new: invader 3`) and ended by the game's own CheckRebellionAndInvasions in the same month
    ("The Russian troops in Kiautschou Bay have been defeated!"); the check read the invasions after the month. e1) and
    e2) accept that now. e3) (default run; suite): its candidate Finland (42 ships of J, 1 of nation 1 in The Baltic)
    gave "Friendly ships not available to oppose the enemy in the area!": no battle, no setup. e3) picks by the ships
    that can fight and gives a battle without a preview the SKIP-note (47ffadb). Passed when run again.
  - `disk_full` (default run; suite): it came after `lockstep_air`, whose session was on the 1935 campaign with its test
    battle going on against the computer; its first month pressed Turn under that battle (`battle slot: DoStrategicTurn
    skipped`, `host turn: click handler returned (endOfTurn=0 battle=1)`, the battle in progress offered to the player
    again and `load of slot 9 FAILED (the battle did not open)`), and the month never ran (240 s): the 0.7.0 RC2 trap of
    `legacy_fleet` after `lockstep_air`, met by the scenario 0.7.1 put in between; `legacy_fleet` was not run (only the
    runner's `turn` reaches Turn under a battle screen). `disk_full` and `disk_growth` now end the battle and host a
    session on slot 1 first (`disk_session`, 3d448ac); not yet run live (the NO-GO came first).
- **Suite fixes** (reg-071): b753578 (`battles` c2)), d0e490d (`battle_raider` c), self-test 139/139), 47ffadb
  (`battle_invasion` e1) to e3)), 3d448ac (`disk_full`, `disk_growth`); TESTING.md (the catalog, the game times, Mixed
  versions, Known traps).
- **Verdict: NO-GO** for publishing 0.7.1 with dist_rc071_1: a game error box on a joined player's PC in a normal
  scenario (the client's "Error in SelectForce procedure! ... Read of address 0000000D" in a battle of the player's
  generator). It is not a regression of 0.7.1 (0.6.0 showed it, 0.7.0 has the same path) and every other failure was the
  suite's or the game's choice; 0.7.1's own fixes and the mixed versions passed. Logs: `r711\logs_run_20261028_220026`
  (`rtw3mp_47701.log` the host, PID 16860; `rtw3mp_47702.log` the client, PID 26056, lines 212-225).

## Fix 2026-10-28: the battle setup's "Error in SelectForce procedure!" box: a side without ships that can fight and its ally's division (v0.7.1)
Branch fix-selforce from int-071 6db6828: the NO-GO of [the 0.7.1 release
candidate](#testing-2026-10-28-the-071-release-candidate-dist_rc071_1-v071), the joined player's box "Error in
SelectForce procedure! Access violation at address 00777A62 in module 'RTW3.exe'. Read of address 0000000D" in
`war_effects` M1 (base 0x3C0000: rva 0x3b7a62, 0xbe7a62 in the analysis image).
- **The mechanism** (static; [GAME-INTERNALS](docs/GAME-INTERNALS.md#the-other-relation-and-war-routines), proven
  live below). `SelectForce` (0xbe7c18) builds a side's force; the setup calls it for side 0 and, if that side got one,
  for side 1, and a False ends the setup cleanly (DoBattle 0, no preview). When the side's nation has no ship for a
  division (8 tries, then ship types 7/4/3/9/10 and nine random ships of any status in the area) and the side has no
  division, the nested **ally fallback** 0xbe776c builds the division of the side's ally (the setup's pick 0xbdbe14:
  for side 0 an ally of the player at war with the opponent, for side 1 another enemy of the player) and looks in the
  battle's division list for a division of the same side to join. There is none; unless `Random(100) < 10` (the ally's
  division leads, a battle with the ally's ships alone) it reads that nil division's ships at 0xbe7a62: the AV, the
  except box, False, DoBattle 0, no consequences. Without such an ally, or without its ships there, the game's own
  **no-force rule** 0xbe6f84 runs instead: "Friendly ships not available to oppose the enemy in the area! Enemy gains N
  VP!" (side 0: EnemyVP, `DeclineCount` + 1, an enemy's landing goes ahead) or "Enemy unable to challenge our forces
  in the area! Gain N VP!" (side 1). Single player then goes on with the generator's next candidate (DoBattle False ->
  `DoPossessionBattle` False -> `CheckAttackLocation` False). This corrects the 0.6.0 note ("when a side has none
  after 8 tries, its fallback reads the main division"): the fault needs the empty side's ally with ships there.
- **The RC's battle** (the r711 logs). `war_effects` allies nation 1 (Germany) with X = the USA, at war with J =
  Russia (`setally`, `settension`: the situation the game makes when an ally joins a war). The player's generator's
  first attempt `3>1 (area by the game)` ran on the player's PC as the game's `CheckAttackLocation(Russia, Germany,
  nil)`: the area by Germany's possessions, the Indian Ocean with Tanganyika (the client's next load cleared "the last
  battle's area Indian Ocean and possession Tanganyika"), Russia's active strength there above 3. Germany had no ship
  there that could fight, the USA had: the box, DoBattle 0, and the replay took the entered setup for the player's
  decline (`battle 1: declined`); the host merged "2 ships changed (2 fields), VP 0/0" as "the player declined battle"
  (most likely the USA's ships the fallback marked `Used`, which `ResetUsedShips` clears only for nation 0 and the
  opponent: the TEST run below, with the ally's two ships in the area, merged "2 ships changed (3 fields)"). Side 1
  had no ally (no other enemy of Germany), so the empty side was Germany's.
- **The area and the host's attempts.** The host's view records every `CheckAttackLocation` call of the player's
  generator without running it, an explicit area or not (by design: B6); the player's PC runs the game's own call with
  area nil exactly as single player's `CheckForBattles2` does (0xcecddf, 0xcecf72: `push 0`): a random area where the
  defender has a possession, the possession, and the presence test of the **attacker** only (`GetStrengthInArea(area,
  False, ActiveOnly True, False)` > 3); the defender's ships are never looked at. So the empty side is the game's, single
  player meets it the same way, and the replay needs no change. 0.7.1's trade protection (Status 3) does not count in
  that test (active ships only) and is not needed by the force build (its last resort takes a ship of any status there);
  `war_effects`' TEST raiders and `BuildingNationIdx` swaps (ships under construction are not in play) play no part.
- **Who meets it.** Any nation at war with J that has an ally (`Allied` > 0) at war with J too (the game's "Our ally X
  joins the war against Y!"), when J attacks one of its possessions in an area where it has no ship that can fight and
  the ally has (or it attacks J's possession where J has none and another enemy of it has ships there): 90 % of such
  setups show the box, in single player too (1.01.44). Before this fix a joined player saw it in its battle slot and
  lost the attempt as a decline; the host could see it in its own battles. Without the ally the same situation is the
  game's no-force rule, which the suite meets every few runs (below).
- **The fix: the guard** (`game.cpp` `InstallSelectForceHooks`, every PC; found by SelectForce's prologue, checked
  against its error text, the setup's two calls and the bytes of the no-force step). MinHook on SelectForce (a wrapper:
  the TEST switch and the log of a force build without a force), on the ally fallback's start (from the no-force step:
  a snapshot of the ally's ships' `Used`), on the no-force rule's start (counted) and on the fallback's read of the
  division (0xbe7a59, `d_SfRead`): with no division to join, at the no-force step, it puts back the ally's ships the
  fallback marked `Used`, unwinds the fallback's frame and jumps to SelectForce's no-force exit (0xbe9ee8: result
  False, its try frame's end), as after the game's handled exception: no box, no battle, no rule's points (log `battle
  setup: side 0 (nation 0 Germany) has no ship that can fight in <area> and its ally 3 Russia's division (n ship(s)) has
  no division of the side to join: the game's SelectForce would read that missing division (...); the mod ends the
  force build there: no battle, as after the game's box (n of the ally's ships unused again)`). Elsewhere (a nil
  division off the no-force step, never seen) the game's own branch where the ally's division leads; its 10 % branch
  stays the game's. Start line `The battle setup's force builder (SelectForce): ok, the ally fallback guarded (rva
  0x3b7c18, ...)`.
- **The replay** (`session.cpp` `ClientStartBattle`). The setups without a force are counted apart (`game::NoForce`:
  the guard's ends and the game's rule; before, every setup without a preview read as the player's decline). The
  guard's: no battle for that attempt, the next attempt is played, as single player (`attempt i (...) found no battle
  (its setup found no force: the next attempt, as single player)`); a request that ends without a battle is `nobattle`
  with the reason in `why` (client chat "* No battle came about (Germany had no ship that could fight in the Indian
  Ocean).", host "* No battle of Germany this month (...)"): nothing merged, no decline counted (the map message's
  Accept before the setup resets the player's own decline count, as in single player). The game's no-force rule ends
  the search with its points, as 0.7.0 did, but as what it is: the enemy's side without ships is `avoided` with the
  result (chat "* The <X> navy had no ships to challenge our forces in <area>: Germany gains N VP."; before 0.7.1 it was
  sent as the player's decline), our side without ships `declined` with the result (chat "* Friendly ships not available
  to oppose the <X> navy in <area>: <X> gains N VP."), both with the result key `noforce` (0 or 1): a 0.7.1 host logs
  "the enemy had no ships there that could fight (the game's no-force rule)" or "the player's nation had no ships there
  ... (the game's no-force rule, not a decline)" and chats "* Germany had no ships to oppose Russia in The Baltic:
  Russia gains N VP." instead of "... declined battle ..."; a `nobattle`'s `why` names any kind's reason.
- **Mixed versions.** Protocol 3 and live-battle engine 18 unchanged; the guard is each PC's own. A 0.7.1 player under a
  0.7.0 host: no box, the next attempt; `nobattle` (no battle), `avoided`/`declined` merged as before (the key and
  the reason ignored: a 0.7.0 host's chat says "declined battle" for the rule of our side). A 0.7.0 player under a
  0.7.1 host: the box on that PC and a `declined` as before (no `noforce`: the host merges it as before).
- **Test hooks** (TESTING.md). `battles testnoforce <0|1|2> [ally] [n]` (any PC): the next n force builds of side 0
  without the side's ships in the battle's area (they read `Used`), the setup's ally of side 0 (or the given nation) in
  its cell with one active ship of each type it lacks there moved in, the fallback's own draws high (Random's procedure
  for calls from its code), StartPlayerBattle's attempts plain meanwhile (`testinv`'s answers: no computer decline,
  not caught out); 2: the guard off (the game's box). `battles injectgen K J area xN`: N identical attempts.
- **Suite** (`tools/regression.py`; self-test 139/139). `battle_generator` f) `battle_noforce` (one more month; EST
  170 s): the K-X alliance and the X-J war, `battles testnoforce 1 X 2`, `injectgen` of one and of two attempts: f1)
  every TEST build ended by the guard (n >= 1 ally ships unused again), no game error box; f2) the one-attempt request
  `nobattle` with the reason on both sides, not merged, decline counts not raised; f3) the two-attempt request's
  attempt 1 played. `war_month` notes every setup without a force; `battle_generator` d) accepts the rule's wording;
  `peace_war` puts a war counter left above 0 at peace to 0 (`battles` ends its war with a bare `settension` and
  `war_effects` started the next one with no month between: its M1 "war counter 2" read 6). A code-review agent found
  `battle_noforce`'s early return leaving its alliance and requests (now `try`/`finally`, `battles resolve`).
- **Before the fix** (dist_sf7_1 = this branch's TEST switch and logging without the guard; the probe `sf_probe.py` in
  the developers' session files: the session scenario, war 1-7 (Spain), alliance 1-3 (Russia), war 3-7, the client's
  `battles testnoforce 2 3`, three requests `injectgen 1 7 Northern Europe`; 48 s with the game open): battle 1 avoided
  by Spain (no setup), battle 2 `battle setup: TEST: side 0 (nation 0 Germany) without its ships in Northern Europe (33
  held back), its ally 3 Russia (the setup's) ... (1 ship(s) moved in), the fallback's draws high`, `the game's fallback
  gives it its ally's division (nation 3 Russia)`, the client's box "Error in SelectForce procedure! Access violation at
  address 00777A62 in module 'RTW3.exe'. Read of address 0000000D" (word for word the RC's, the same base), `SelectForce
  side 0 -> no force`, `DoBattle(opponent 7) -> 0`, `battle 2: declined`, the host's `battle 2 applied: 2 ships changed
  (3 fields), ... VP 0/0 ...; the player declined battle`: the RC's symptoms.
- **After the fix** (dist_sf7_2, RTW3MP.dll SHA-256 F8C4B2559A1B8C5A..., version.dll 68D64AC20BEB68C8...). The probe with
  `testnoforce 1 3 2` and requests of one, two and one attempt(s): 35/35, 49 s: battle 1 guarded (`... (1 of the ally's
  ships unused again)`), `battle 1: nobattle - Germany had no ship that could fight in Northern Europe`, the host's
  decline counts 0/0 (were 0/0), no `battle 1 applied`, `battle 1 (kind 1): no battle (...)`; battle 2's attempt 0
  guarded, attempt 1 played (Spain declined: avoided); no box. `--only session,war_effects,battle_generator,battles`:
  267/269 (512 s; the two suite failures above, fixed) and 256/256 (472 s). `--group relations`: 406/406 (750 s;
    `battle_generator` 65/65 with f)). Natural cases on
  the way: `battles` (run 1) "Enemy unable to challenge our forces in the area! Gain 215 VP!" -> `battle 7: avoided`, the
  host's "... the enemy had no ships there that could fight (the game's no-force rule): the player's points"
  (before 0.7.1: "the player declined battle"); `war_effects` M1 and M2 (relations run) the player's generator's
  attempt `3>1 The Baltic` with no German ship there: the fallback entered with the USA as the ally, no USA ship there
  either, so the rule of our side ("battle 1 applied: ... VP 0/305 ...; the player's nation had no ships there that
  could fight (the game's no-force rule, not a decline)"); in that run the fallback was entered 7 times: twice by the
  TEST (guarded) and 5 times naturally, each followed by the rule (`war_effects` M1 and M2 with the USA as the ally,
  without ships there; `arms_treaty` M2 and M3 and `battle_surprise` M2 without an ally). `war_effects` ran three
  times: 72/73 (run 1: the counter, a suite failure), 73/73, 73/73. After every run v0.7.0 (dist_rc070_2) was put back
  (hash checked), slots 1 and 9 and the Preferences were identical to their backup, no test slot and no `RTW3.exe`
  left, the game lock released. With the game open: 48 + 49 + 512 + 472 + 750 = 1831 s.
- Docs: GAME-INTERNALS (the force builder, its fallback and rule; `CheckAttackLocation`'s area pick and presence test;
  the corrected 0.6.0 note), ARCHITECTURE (hooks, the battle phase's setups without a force, the result's `noforce`,
  versions), TESTING (`battles testnoforce`, `injectgen xN`, `battle_generator` f)).

## Testing 2026-10-29: the 0.7.1 release candidate 2 dist_rc071_2 (v0.7.1)
The release candidate dist_rc071_2 (int-071 e760781: 0.7.1, live-battle engine 18, protocol 3; RTW3MP.dll SHA-256
7BFF4A5C25AB9692..., version.dll 8CAB68DB221A2B2B...): the first candidate with [the battle setup's SelectForce
guard](#fix-2026-10-28-the-battle-setups-error-in-selectforce-procedure-box-a-side-without-ships-that-can-fight-and-its-allys-division-v071),
the NO-GO of [the first candidate](#testing-2026-10-28-the-071-release-candidate-dist_rc071_1-v071) fixed. Run on the
reg-071b runner (int-071 e760781 and the suite fixes below), installed from its frozen snapshot (`--dist`), the released
0.7.0 (dist_rc070_2) put back after every run (hash checked); slots 1 and 9 and the Preferences identical to their
backup after every run, the save folders as before (Game1, Game2, Game9), no game folder copy, delay proxy, `python` or
`RTW3.exe` left, the game lock released. The logs of every run are kept with the developers' session files
(`r712\logs_*`).
- **Runs** (checks, seconds with the game open):
  - `--group relations` (the first candidate's NO-GO group): 431/431 (706 s): `war_effects` 73/73, `arms_treaty`
    66/66, `battle_generator` 90/90 with f), `battle_surprise` 39/39, `battle_raider` 93/93, `relations_war` 19/19.
  - `--only war_effects` twice more (its area is the game's pick): 90/90 (160 s) and 90/90 (162 s).
  - The default run (34 scenarios, `--legacy-fixture`): 1590/1598 (3312 s): `ship_status` 62/62, `lockstep_month`
    51/51, `twopc` 61/61, `lockstep_air` 41/41, `disk_full` 90/90, `legacy_fleet` 25/25; the failures below
    (`battle_raider` c), `battle_invasion` i2)).
  - The whole `--stress` run (41 scenarios, `--missile-fixture`, `--legacy-fixture`; the first `--stress` run of 0.7.1):
    2008/2015 (8522 s): `lockstep_month` (long) 95/95, `twopc` (full) 85/85, `lockstep_rejoin` 42/42, `lockstep_resume`
    45/45, `lockstep_stress` 125/125, `lockstep_strike` 21/21, `lockstep_missiles` 23/23, `disk_full` 90/90,
    `disk_growth` 107/107, `legacy_fleet` 25/25, `battle_raider` 93/93; the failures below (`battle_invasion` e3),
    `lockstep_limits`, `lockstep_data` b), `lockstep_torpedo` b) to e)).
  - Run again: `--only battle_invasion,lockstep_limits,lockstep_data` 213/213 (429 s), `--group torpedo` 38/38 (273 s).
  - Mixed versions (`mixed_071.py`, [TESTING](docs/TESTING.md#mixed-versions)): a) a 0.7.1 RC2 host with a 0.7.0
    player 87/87 (466 s), b) a 0.7.0 host with a 0.7.1 RC2 player 87/87 (472 s).
  - Not needed: `--group disk` (`disk_full` passed in the default and the `--stress` run, `disk_growth` in the
    `--stress` run) and `--stress --only lockstep_month` (its long variant was part of the `--stress` run).
  - With the game open: 706 + 160 + 162 + 3312 + 8522 + 429 + 273 + 466 + 472 = 14502 s.
- **No game error box** in any run but `disk_full`'s own (its "Error in SaveDesignFiles procedure! ... There is not
  enough space on the disk" on both copies and its file in use of f) and g)); `session`'s whole-run check passed in
  every run.
- **The SelectForce fix as tested.** `battle_generator` f) (`battle_noforce`) passed in all three runs that have it
  (relations, default, `--stress`): both TEST force builds of the client's `battles testnoforce 1 X 2` ended by the
  guard (`... the mod ends the force build there: no battle, as after the game's box (1 of the ally's ships unused
  again)`, 1 or 2 ships), no box; the request of one attempt `nobattle - Germany had no ship that could fight in Northern
  Europe` with the host's `battle N (kind 1): no battle (...)`, not applied, no decline counted; the request of two
  attempts played its attempt 1. On their own, in every log of these runs (host and client, the copies of killed and
  restarted copies included, each line counted once): the guard 0 times (its line came only after the TEST's), the
  game's ally fallback entered 23 times (4 times with the USA as Germany's ally in `war_effects`, Germany without a ship
  that can fight in The Baltic and the USA without ships there, 19 times without an ally), each followed by the game's
  no-force rule, which came 38 times in all: 32 on the joined player's PC (17 for its own side, "Friendly ships not
  available to oppose the enemy in the area!", merged by the host as `the player's nation had no ships there that could
  fight (the game's no-force rule, not a decline): the enemy's points`; 15 for the enemy's side, "Enemy unable to
  challenge our forces in the area!", `the enemy had no ships there that could fight (the game's no-force rule): the
  player's points`), in `war_effects` (four months of one run, the attempts `3>1 The Baltic` and `3>1 (area by the
  game)` with the game's pick The Baltic), `arms_treaty`, `battle_generator`, `battle_surprise`, `battle_raider` and
  `battle_invasion`; 6 on the host (its own battles against nation 1, two of them the test battles of `lockstep_limits`
  and `lockstep_data` below). The first candidate's case (an area where the player's ally has ships and the player
  none) did not come up on its own; the attempts left to the game (`(area by the game)`) went to Northeast Asia and The
  Baltic.
- **0.7.1's other fixes.** `ship_status` 62/62 twice; `disk_full` 90/90 twice, both after `lockstep_air` and from a
  session on slot 1 (the first candidate's suite fix `disk_session`, live for the first time); `disk_growth` 107/107. A
  real file in use (outside every TEST window; the session files' `inuse_scan.py`): in the `--stress` run's
  `battle_generator` c), right after `battle 29 applied` (01:35:20, the runner reading slot 1 for its cross-check, or
  OneDrive), the host's month save found `RTWGame1.bcs` in use: `save: the game's box 1 of this save not shown (a file
  in use: the mod saves again)`, `disk: a save failed: a file of it is in use (RTWGame1.bcs); sending the new month
  waits: it saves again in 2 s (1 of 5)`, the host's notice, and 2.3 s later `the save worked again (1 of 5); sending
  the new month goes on`: no box, the month went on. No other failed save outside `disk_full`'s TEST windows.
- **Mixed versions** as on the first candidate: the join line `Captain: mod <the other>, live-battle engine 18 (this
  host: 18), player's relation rules 1, battle generator 4, campaign divisions 1` both ways, two months merged once
  each; a) the 0.7.1 host keeps the 0.7.0 player's trade protection and raider (`relations v2: nation 1 at war: 3
  ship(s) on trade protection or raiding kept (ReduceTimeLimits saw it at peace)`), the 0.7.0 player's View data shows
  the host's design with the same id (Hela as "COLLINGWOOD CLASS (1899)", expected); b) the 0.7.1 player's View data
  shows its own design ("HELA CLASS (1899)"), the 0.7.0 host returns Hela, Gazelle and Hertha to active service (the
  0.7.0 bug, expected); the host's battle against the player in Dec 1900 fought live (412 and 463 minutes equal, no
  DESYNC, ended by agreement); no box, exception, data-file "differ" line or failed save.
- **Failures and their classes:**
  - `battle_raider` c) side 1 auto-resolved (default run; suite, seven FAIL lines): side 1 fought just before sank
    Spain's raider ("Spain has no ships left at sea! The scenario is over!"), and the next forced interception found no
    ships for its pair (client `StartRaiderCheck: TEST: forced interception (side 1, nation 7): no ships for it`): no
    interception, `battle 38: nobattle`, the host's `battle 38 (kind 1): no battle (no battle came about)`, nothing
    merged. Such a case checks its clean no-battle and is a `SKIP-note` now (44bd725). Passed in the `--stress` run.
  - `battle_invasion` i2) (default run; suite): J was the USA (Russia had fewer than three possessions worth 1..9 by
    then), and i2)'s target The Philippines (Southeast Asia) gave "Our forces in The Philippines have been defeated!"
    in all four runs, the first with the draws low: the game's month resolution defeats the player's invasion whenever
    the enemy's strength in the area is above the invader's (with support 50 its draw always passes), so no capture can
    come there. i2) takes up to three candidates by the ships in the area and logs a `SKIP-note` when every run was
    defeated (a531fbc). Passed in the `--stress` run and when run again (Northern Russia taken).
  - `battle_invasion` e3) (`--stress` run; suite): its only candidate Northern Russia (14 ships of J, 6 of nation 1 in
    Northern Europe), the setup forced into the coup's ambush (forced +1), the preview reached and declined, but no
    reinforcement line and no box. e3) wanted the line wherever J outnumbers nation 1 by ships; Northern Russia gave no
    line in any run, outnumbered or not, while The Philippines (2 v 3, the default run: `held 00773AA6: the game's
    code, rva 0x3b3aa6`, the value of the first candidate and of 0.7.0: the SelectForce wrapper keeps that stack slot)
    and Sakhalin gave it. No line without a box is a `SKIP-note` now (f4aad3a); an unset local the game reads shows its
    box. Passed when run again (Sakhalin, 3 v 1: "gave them nation 0").
  - `lockstep_limits` and `lockstep_data` b) (`--stress` run; the game's): their test battle gave no battle, "Enemy
    unable to challenge our forces in the area! Gain 102405 VP!" (the same VP as under dist_rc061_2: the earlier test
    battles' results stay in the host's campaign in memory). Passed when run again.
  - `lockstep_torpedo` b) to e) (`--stress` run; the game's): no division had a firing solution in 60 boundaries, the
    fleets never met ("No contact between fleets! The scenario is over!"). Passed when run again (`--group torpedo`
    38/38).
- **Suite fixes** (reg-071b): 44bd725 (`battle_raider` c)), a531fbc (`battle_invasion` i2)), f4aad3a (e3)); self-test
  139/139; TESTING.md (the catalog, the game times, Mixed versions, Known traps).
- **Verdict: GO** for publishing 0.7.1 with dist_rc071_2: no game error box in any run (the first candidate's
  SelectForce box is guarded: `battle_generator` f) three times, three `war_effects` runs without it), 0.7.1's fixes
  passed, the mixed versions passed, and every failure was the suite's (fixed, passed again) or the game's choice
  (passed when run again).

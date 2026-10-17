# Testing

How RTW3 Multiplayer is tested. The regression suite (`tools/regression.py`) starts two or three real copies of the
game on one PC and drives them only through the mod's test bridge and posted window messages; it backs up and
restores the save slots it uses. The regression agent (`.github/agents/rtw3-regression.agent.md`) runs it after every
change. This file is the suite's reference: how to run it, the environment, the tools, the runner's helpers, the
scenario catalog, the bridge commands and the traps found so far. How the mod works: [ARCHITECTURE.md](ARCHITECTURE.md);
game facts: [GAME-INTERNALS.md](GAME-INTERNALS.md); test results over time: the "Testing" entries in
[MODLOG.md](../MODLOG.md).

Short hashes in this file (e674bf5, c06860f, a1a8484, ebfca72) name development builds of 0.3.0. They were squashed
into the release commit f6ee827 and are not in the published history.

## Contents
- [Before you start](#before-you-start)
- [Running the regression suite](#running-the-regression-suite)
- [Environment variables](#environment-variables)
- [Tools inventory](#tools-inventory)
- [The runner in detail](#the-runner-in-detail)
- [Scenario catalog](#scenario-catalog)
- [Bridge command reference](#bridge-command-reference)
- [Known traps](#known-traps)

## Before you start
- **Safety first.** The safety rules in `.github/agents/rtw3-regression.agent.md` apply to everyone who starts the
  game for a test, person or agent: never the real mouse or keyboard, never a screen capture, close only the
  processes you started, never touch a game the user is playing, never save outside a backed-up run, and keep the
  save slot 1 fixture exactly as it is. Read them before the first run.
- **What you need:**
  - Windows, Python 3 (the suite needs only the standard library; `tools/record_hosting.py` also needs Pillow and
    ffmpeg), Steam running, and Rule the Waves 3 1.01.44.
  - The game folder in env `RTW3_DIR` (the default is in `tools/rtw3ctl.py`).
  - The build under test installed: run `build.bat`, then copy `dist\version.dll` and `dist\RTW3MP.dll` into the
    game folder while no `RTW3.exe` runs. The runner never installs; it reports whether the installed DLLs match
    `dist\` (or the folder given with `--dist`).
  - Save slot 1 holds the test campaign: past its setup steps, with the host playing nation 0. Its intel report list
    is bloated on purpose (`ReportNo=3420`), so every run exercises the 0.2.2 repair on load. Don't replace or clean
    it; the runner restores it after every run.

## Running the regression suite
From the repository root:

```
python tools\regression.py                           the default run: every scenario but lockstep_stress
python tools\regression.py --list                    the scenarios, the months each processes, what each guards
python tools\regression.py --only research,doctrine  some scenarios (session is always added)
python tools\regression.py --stress                  the long live-battle variants and lockstep_stress
python tools\regression.py --dist <folder>           compare the installed DLLs with that build
```

| Option | Effect |
|---|---|
| `--list` | Prints the scenarios and exits. |
| `--only a,b,...` | Runs only those scenarios, in catalog order; `session` is always added because the others build on it. |
| `--stress` | Adds `lockstep_stress` and switches `lockstep_pvp` and `lockstep_month` to their long variants (about 40 minutes more). |
| `--dist <folder>` | The build the installed DLLs are compared with (default: env `RTW3_DIST`, else the repository's `dist\`). Use a frozen copy of the build under test when another agent may rebuild `dist\` during the run. |

- The default run takes about 13-15 minutes of game time (16 processed months and one single-player month of a
  copy); with `--stress` about 46-50 minutes.
- Exit code: 0 = every check passed and the saves were restored; 1 = a check failed, or the saves were not restored,
  or a started copy, an event cooldown file, a copy slot or a battle slot was left behind; 2 = not started (a
  precondition failed).
- While a run lasts, slot 1's backup is in the Windows temp folder (`rtw3mp_regression_backup_Game1`). The details of
  what the runner checks before, during and after a run are in [The runner in detail](#the-runner-in-detail).

## Environment variables
Set by the runner when it launches a copy (see "Environment" in the regression agent file): `SteamAppId` and
`SteamGameId` (2008100: a direct start without the relaunch through Steam), `RTW3MP_BRIDGE_PORT` (47701 host, 47702
client, 47703 the third copy), `RTW3MP_LOG_TAG`, and for clients `RTW3MP_CLIENT_SLOT` and `RTW3MP_EVENT_CHANCE=0`.

| Variable | Read by | Effect |
|---|---|---|
| `RTW3_DIR` | the tools (`rtw3ctl.py`) | The game folder (default in `tools/rtw3ctl.py`). |
| `RTW3_DIST` | `regression.py` | Default of `--dist`. |
| `PYTHONIOENCODING=utf-8` | Python | Set it before reading logs through PowerShell pipes (see [Known traps](#known-traps)). |
| `RTW3MP_BRIDGE_PORT` | the mod | Enables the test bridge on 127.0.0.1 at that port. |
| `RTW3MP_BRIDGE_TOKEN` | the mod | If set, the first line sent to the bridge must be this token. |
| `RTW3MP_LOG_TAG` | the mod | Log file `rtw3mp_<tag>.log` instead of `rtw3mp.log`. |
| `RTW3MP_CLIENT_SLOT` | the mod | A joined player's save slot (default 77; 50-999). |
| `RTW3MP_BATTLE_SLOT` | the mod | A joined player's battle slot (default: client slot + 10; 50-999, not the client slot). |
| `RTW3MP_EVENT_CHANCE` | the mod | Chance in % of a joined player's monthly random event (default 33; 0-100). |
| `RTW3MP_BATTLE_TIMEOUT` | the mod | Seconds a player has for a battle of the battle phase (default 1800; 0 = no limit). |
| `RTW3MP_LIVE_BATTLES` | the mod | `0` turns live battles off on that PC. |
| `RTW3MP_DIAG` | the mod and the proxy | Logs exceptions; the proxy also writes `rtw3mp_loader.log` into the game folder. |
| `RTW3MP_NOINIT` | the mod | The mod stays idle (diagnostics). |
| `RTW3MP_DELAY` | the mod | Waits that many milliseconds before reading the game's classes (diagnostics). |

## Tools inventory
What `tools/` holds. "Current" tools are used by the suite, the agents or the release; the two standalone scripts are
run by hand. **A script that starts a session or processes a month saves slot 1 without a backup unless it runs inside
`tools/regression.py`**: run the standalone ones (and `e2e_test.py` on its own) only after backing up slot 1 yourself
(safety rule 6).

| File | Status | Used by | What it does |
|---|---|---|---|
| `regression.py` | current | the regression agent | The regression suite. |
| `rtw3ctl.py` | current | the suite, every other script, the agents | Starts a copy with the bridge (`launch`), sends one bridge command (`cmd`), waits for a `status` value (`wait`); `SAVE_DIR`, the game folder. |
| `e2e_test.py` | current (library) | the suite imports it (`dismiss_dialogs`, window helpers) | The 0.1 end-to-end test (two copies, a month); the suite's `merge` scenario came from it. Saves without a backup. |
| `ui_mp.py` | current (library) | the suite (`IDS`, `mp_window`, `text`, `windows_of`) and the standalone scripts | Drives the real Multiplayer window with posted and sent messages. |
| `package_release.ps1` | current | the docs agent's release checklist | Builds `RTW3MP-<version>-win32.zip` from `dist\` and `extras\`. |
| `realmouse_test.py` | standalone, needs the user's consent | regression agent, safety rule 1 | Moves the real mouse (~30 s): the window's close button, Start session and Give to AI with real clicks. Saves without a backup. |
| `record_hosting.py` | standalone | the hosting video in `docs/` | Records `docs/hosting.mp4` and the stills from `PrintWindow` captures of the host's windows; needs Pillow and ffmpeg. The addresses it shows (25.17.204.86, 192.168.1.20) are fictitious examples. Saves without a backup. |

Removed on 2026-10-16: the 0.1 scripts `live_lobby_test.py`, `repro_issues.py`, `run_turn.py`, `verify_tabs.py` and
`ui_buildship.py`, and `diplomacy_test.py` (a wrapper for `regression.py --only session,diplomacy`); older MODLOG
entries still mention them.

The live-battle drivers of the 0.3.0 work (`ls_live.py`, `ls_pvp.py`, `ls_drive.py`, `ls_tracediff.py`) are in the
developers' session folder, not in the repository; the suite's `lockstep*` scenarios are the maintained tests of
live battles.

## The runner in detail
Runner: `python tools/regression.py [--only <name,...>] [--list] [--dist <folder>] [--stress]`. It refuses to start while
`RTW3.exe` runs, without Steam, when a backup from an interrupted run differs from slot 1, when `Game87` (the
client's battle slot) exists, (with `player_copy`) when Game78-80 exist, or (with `lockstep_pvp`) when Game76/Game86
exist. It backs up slot 1, launches host
and client once (scenarios may start a third copy or restart a killed one), runs the selected scenarios in order (`--only` always adds `session`; a scenario that throws
stops the rest), closes the PIDs it started, restores slot 1 (verified by hash), deletes `Game77`, `Game87`, Game76/86 and
`player_copy`'s slots, and prints
per-scenario results, the processed months with their duration, the live battle cases (checks, seconds), the logs it
kept of restarted copies, the game-open time and `N/M checks passed`. The default run is every scenario but
`lockstep_stress`; `--stress` adds it and switches `lockstep_pvp`/`lockstep_month` to their long variants.
Exit code 0 = all passed and saves restored, 1 = a check failed (or saves not restored), 2 = not started.
Keep total game-open time low (each processed month costs ~15-25 s). Shared helpers in the runner:
`run_month` (pre-month hooks -> submit -> host "All players are ready" -> `turn` -> answer dialogs on both PIDs
until the client plans the new month, 240 s timeout -> `wait_event_roll` -> `month_checks` -> month hooks),
`answer_dialogs`, `chat`, `designs`, `rel`/`row`, `intel`/`saved_intel`/`private_mb`/`intel_snapshot`,
`log_mark`/`log_since` (per-month slices of the host and client logs), events: `force_event`,
`select_first_answer`, `answer_events`, `roll_lines`, `host_event_view`/`client_event_view`,
`remove_event_files`, treaties: `client_request`, `apply_diplo`, `terms`, `warvp`, `poss_list`, `nation_view`,
`greedy` (the mod's automatic pick), `lv_rows`/`lv_select` (`RemoteBuf`), `claim_state`/`open_claim_window`/
`claim_button`/`claim_closed`, `capture_treaty`/`TREATY_SEEN`, `wait_until`, `wait_chat`, `new_chat_missing`,
victory points: `vpinfo`, `battlevp` (answers the host's dialogs while the synchronous call runs), `wars_of`,
`vp_shares` (ShareEnemyVP's split), `vp_diff`, `apply_vp`, `client_accept_peace` (the client's "Accept peace" button),
Copy game: `read_save` (a save file as sections), `slot_snapshot`, `expected_copy`/`expected_map`/`expected_off` (the
0.2.7 specification, written from the spec, not from `saveio.cpp`), `verify_copy` (diffs by kind: sections, relations,
numbers, general, eut, other, map, off, plus file checks), `client_cooldowns`, `copy_dialogs`, `copy_with_button`,
`single_player_month`, `nation_table(port)`, research screen: `research` (the bridge reply as pct/prio), `research_areas`
(the game's area table), `research_view`/`shown_prio`/`shows` (what the dialog shows, rows mapped to areas),
`open_research`/`close_research` (OK or WM_CLOSE), `popup_priority` (the list's right-click menu), `message_forms`,
battles: `BattleDriver` (run_month's client side in a battle month: plan {id: 'fight'|'decline'|'hold'}, `fight_any`,
records per battle, the host's save at the battle phase start `s0`, the merged save `s1`, the battle slot's save
`g87`), `battle_dialogs` (one pass of the battle dialog policy; `UNPLANNED` declines battles in months without a
driver), `click_autorun`/`battle_panel`, `battles_list`, `cstate`, `inject`, `battle_pairs` (an AI nation with a
possession where nation 1 has >= 4 ships), `ships_by_id`/`ship_changes`/`merged_mismatches` (the cross-check),
`rejoin`, `select_event_nation`, live battles (lockstep): `LsAnswerer`/`ls_ans`/`ls_quiet` (the dialog thread),
`lss`/`lsn`/`pst` (`ls status`, `ls session`, `pocstate` as dicts), `ls_run`/`ls_run_total`/`ls_idle_equal`,
`ls_compare` (per-minute hashes of both copies), `ls_divs`/`ls_all_ai`, `ls_end_both`/`ls_end_reset`, `ls_drive_end`
('agree' / 'game'), `ls_start_test` (`pocbattle 1 0` to running on both), `ls_finish_host` (the host's battle on
auto-run), `autorun_click`, `ls_month` (a processed month with live battles: `ls_detect`, `ls_drive_live` modes
`drop<N>`/`agree`/`game`/`killB`, auto-run of battles that are not live, the returning client's claim), `host_live`,
`ls_host_battle`, `battle_effects`/`same_ships` (a battle in the campaign: took part, lost ships by Fate, losses of ship-backed types; host vs client), `lost_ship`/`losses_explained`/`effects_txt`, `StageWatcher` (a thread in `ls_month`: the host's save at `battle phase:`, after each `battle N applied`, Game87 after each `battle N: fought`) and `month_stages` (the month's losses step by step), `idea1_merge_checks` (h2), `ls_drive_endsoon`, `deferred_close_checks`, `rejoin_back` (how a returning player got its nation back), `expected_version` (`src/mod/common.h`), `parse_save`,
`host_battle_checks`, `ls_war01`/`ls_peace01`, `ls_rejoin`/`ls_ensure_session`, `ls_kill`/`ls_relaunch`/`ls_keep_log`,
`restore_slot1_copy`, `k2_start`/`k2_stop`, `pvp_war_score_check` (`lockstep_pvp` f): the war score 1-2 against the
month's merged live battles), `ls_spam`, `ls_resync_once`, `ls_after_checks` (the j) checks),
`ls_log_scan` (no lockstep error, hooks once, no LEAK), `ls_leakcheck` (`ls leakcheck 1` per copy/PID), `ls_detach_check`,
`ls_guard_check`, `ls_case` (a stress case that fails without stopping the scenario),
`turn_button`, `month_after`.

**Per-month checks (`month_checks`, 8 after every processed month, counted in the scenario that processed
it):** host and client `intel` count == unique; intel reports grow by at most 30 a month (host and client);
the saved `[IntelReports]` of `Game1\RTWGame1.bcs` and `Game77\RTWGame77.bcs` have no empty or repeated entries
(ReportNo == IntelN keys == distinct non-empty entries); the client's month package (`state seq N -> <name>
(nation 1, B bytes)` in the host log) grows by at most 25 % over the previous month (baseline: the session
start) and no `NOT sent`; no rejected turn or failed write in the host log for that month; the client rolled the
new month's event exactly once (at chance 0 exactly one `event: no event this month`, never `already rolled`).
Reported, not asserted: private memory (GetProcessMemoryInfo PrivateUsage) of client and host after the session
start and after each month (summary table), and handled transient file locks.

## Scenario catalog
Keep this table current when you add or change scenarios.

| Scenario | What it guards | Checks | Months | Notes |
|---|---|---|---|---|
| `session` | both logs show the version of this source tree (`RTW3MP <MP_MOD_VERSION> starting (protocol <MP_PROTOCOL>)` from `src/mod/common.h`, read only: 0.3.0 / 3), both bridges up, host loads slot 1 as nation 0, host/join/claim nation 1/start, client plans nation 1 in slot 77, nation 2 reserved as stand-in player "Bot", client Multiplayer window open; records the intel baseline at the start | 8 | 0 | always first |
| `intel_reports` | 0.2.2 intel-report fix. Part 1 runs inside `session`, after the first load and before hosting (nothing saved yet): the bloated fixture is repaired on load (host `intel` count == unique > 0, `intel reports: N entries -> M` in the host log), and two more `load 1 0` keep the count. Part 2: at the session start the client's list and both saves (host saved slot 1 at the start) are clean, the month package was sent, and the client's load of that clean save logs no repair line (the game's own trailing empty entry is dropped quietly) | 10 | 0 | fails without the fix; needs the bloated fixture (safety rule 7) |
| `file_lock` | 0.2.3 write-retry fix: the client's `writetest` into slot 77 with a throwaway file (`rtw3mp_locktest.tmp`) locked from Python. Unlocked: ok at once, no retry line. Lock released after ~1.2 s: ok after ~1-3 s, file written, `write ...: succeeded after N retries` in the client log. Lock held for the whole call: `error write failed (32 or 5)` after >= 3 s, `write ... FAILED: error E (in place: E2)`, file unchanged, no `.mptmp` left; test file deleted | 7 | 0 | never lock a real save file |
| `events` | 0.2.4 joined players' random events. Session start: one `event: no event this month` (chance 0), `events_<hash>.ini` with nation 1 and the month marked rolled. a) `eventlist` >= 50 events, some H, "XX appears to be considering a naval rearm" (#5) listed and not H. b) host nation 1 Prestige/BudgetModifier, `rel 1 j` (j=0,2..8), `rel 0 i` (i=2..8); client `nations` order, playerIdx. c) `eventnow 5` -> client `TdlgEventAnswer` "Event - <month>"; while open `status` answers, `nations` refused; first answer + OK; `eventlast` `event=5`. d) exactly one tension entry j != 1, delta Prestige:1; host `eventlast` `nation=1 event=5 tension 1-j a->b` with b = clamp(a+d, 0, 40) (no entry if a > 40); host `rel 1 j` == b; every other recorded pair unchanged; host Prestige +1 and BudgetModifier + reported; client Prestige == host; client nation order and playerIdx restored; client and host log lines. e) a second forced non-H event (non-ship captions with cond 0 first; up to 4 tries if one is not shown): shown and sent, host logs `second event of Captain in <date> ignored (one per month)`, host values and `eventlast` unchanged. f) `eventnow <first H>` -> `error event needs the host`, no dialog, nothing sent. g) hook after the next month: host == client Prestige and BudgetModifier; values before/after the event, at submit and after the month reported. h) pre-month hook: `eventchance 100` before the last month with nation 1 at peace (`diplomacy A` in the full run; the last month without `diplomacy`); after it the client rolled `event N for nation 1` by itself (dialog answered within 15 s, no `no event this month`), host `event of Captain (nation 1, #N` + `eventlast`; then `eventchance 0`. i) war rule: `eventchance 100` before `diplomacy`'s war month (month B); after it nation 1 is at war (`rel 0 1` or `rel 1 2` war=1), that month's roll logged exactly `event: no event this month` with no dialog, `eventlast` is `none`, and a random `eventnow` adds no dialog (`this month was already rolled`); then `eventchance 0` (skipped with a `SKIP` log line if `diplomacy` is not selected) | 32 (31 without `diplomacy`) | 0 (g after the next month, h in the last month at peace, i in the war month; processes 2 itself if no later scenario processes any) | client launched with `RTW3MP_EVENT_CHANCE=0`; `events_*.ini` deleted before/after the run |
| `merge` | client research % and a ship order survive the host's month (host and client); `merge nation 1` and `AI suppressed ... for nation 1` in the host log; no AI suppression for AI nation 3 | 9 + 8 | 1 | from `tools/e2e_test.py` |
| `research` | 0.2.8 joined players' Research screen (the game opened it for nation 0 = the host's nation). Setup: `research 0/1` on host and client (25 values each); if nation 1's % is > 12 (fixture 13, after `merge` 14) the dialog's "12" is logged and it is closed (WM_CLOSE); `nfield 1 ResearchPct 10` (<= 12, != nation 0's). a) client `researchopen`: `TfrmResearch` opens, client log `research screen: opened for the player's nation 1 instead of nation 0` once, the dialog's budget = nation 1's (not nation 0's), its priorities = nation 1's in every listed area (rows mapped by name through `Data\ResearchAreas3.dat`) and != nation 0's in the areas where they differ. b) the popup menu sets one area (preferably one whose row != its index: "Low" on row 11 = Explosive shells, area 12) -> dialog and `research 1` changed, `research 0` not (dialog open); budget 7 typed + OK closes without a message; client nation 1 = pct 7 + that priority, nation 0 unchanged. d) reopened: shows them (log line again); 15 % + OK -> `TMessageForm` "Rule the Waves III" [OK], dialog stays open, nothing written; answered, the dialog still open, WM_CLOSE leaves it (nation 1 unchanged, no window, bridge answers). c) host `researchopen`: no `research screen:` line, the dialog shows nation 0; budget +1 + OK changes host nation 0 only. After the next processed month (hook): host nation 1 = the client's values, host nation 0 = the host's own, client nation 1 kept, client nation 0 = the host's, the client's dialog shows nation 1 (log line, WM_CLOSE), host log `AI suppressed: DetermineAINationResearch for nation 1` (if the game called it; never for AI nation 3); then the host's nation 0 % goes back (`nfield`). e) (ebfca72) host `researchrun 1 100` (`research_run`: the game's DoResearch 100 times for joined nation 1 through the mod's hook, ~99 % to hit its 5 % full re-roll): the `before` and `after` priorities of the test line both equal the client's pattern (and `research 1` afterwards), at least one `research priorities of player nation 1 kept (N changed ...)` line (8 in the first run, one with 10 changed: a full re-roll); control `researchrun 8 100` on AI nation 8 (Austria-Hungary): its changes recorded (2 areas in the first run), no `kept` line for it | 24 (+ 8 month checks = 32 when it processes its own month) | 0 (shares the next month: `design_study 1` in the full run; 1 own month if no later scenario processes any) | after `merge` (whose `nfield` 14 it overrides); fails on 0.2.7 (a, b, month checks). 0.2.8: `--only research` 37/37 in 43 s, full run 368/368. a1a8484: the month checks failed once at random (DoResearch's re-roll, MODLOG); ebfca72: 24/24 |
| `doctrine` | 0.2.3 doctrine fix. Player path on the client: real "Doctrine" button -> `TdlgDoctrine` -> toggle a training check box -> Apply -> Close gives a pending change with 12 months (falls back to `settraining` if the dialog can't be driven). Then nation 1 training shortened to 2 months + missile policy pending 2 months (client, merged on submit) and the host's own training change (2 months). After month 1: host and client at 1 month, not applied, one `doctrine nation 1: training 2 -> 1 months, missile storage 2 -> 1 months` line. After month 2: training and missiles applied on host and client, both Doctrine chat lines, `(applied)` line, host's own change applied by the game, no countdown lines for AI nations | 17 | 0 (shares `design_study`'s 2; processes 2 itself if `design_study` is not selected) | setup in the planning month before `design_study`; month checks run as a `run_month` hook |
| `design_study` | a joined player's study (set to 2 on the client, submitted) counts down 2 -> 1 -> 0 on the host, host and client agree, one `design study nation 1` log line per month, client chat "ready for construction" (or the 1 % "technical issues" line with the value kept); the host's own study (2) still advances in the game's routine (0 or 1 after two months) with no `design study nation 0` lines; AI nation 3's designs unchanged and no countdown lines for AI nations | 15 + 16 | 2 | added for the design-study fix (0.2.1); fails without the fix |
| `diplomacy` | offers/accept/decline/withdraw, war/peace/alliance at month end, AI peace kept away from player wars, Diplomacy buttons via posted clicks | 52 + 16 (51 + 16 if the host is at war in month B) | 2 | moved from the former `tools/diplomacy_test.py`; its month A is the `events` random-path month, month B the `events` war-rule month; its peaces are compromises (wars started at month end score 0:0) |
| `battles` | joined players fight their battles against AI nations on their own PC (after 0.2.8). a) both logs `Joined players' battles: ok (frmMain=.. checkAttack=..)` (else one more failing check and stop); defaults: client `battles state` none, slot 87, slotMode 0, fight 1; host `battles list` phase 0, timeout 1800. Setup: J = the AI nation with a possession in a sea area where nation 1 has >= 4 ships (`battle_pairs`; the fixture: Spain, Northern Europe), war 1-J by `settension 1 J 50` (set again before every month: the game's AI peace may end it); `battles inject` refused for an unknown area, an AI nation as K, the stand-in as J; `battles resolve 99` -> `ok 0`. b) two candidates injected a month (up to 3 months; a battle the game found itself is fought if none comes about; the AI avoids about 2 of 3): `battle request N injected`, `battle phase: N battle(s)`, `battle N sent to Captain`; during the phase host busy=1, TfrmBuildCamp disabled, status "Battles in progress: Captain: Germany vs Spain in Northern Europe", `battles list` phase=1 fighting; client in slot 87 (playerIdx 0, `nations` 0 = Germany, `battles state` starting nation 1 opp J, slotMode 1); dialogs: map message Accept, preview Accept, battle screen, auto-run prompt "Yes", results "Exit battle", `battles state` aftermath=1 at the next box; `battle slot: DoStrategicTurn skipped`; `StartPlayerBattle 0 attacks J in <area> -> battle`, `battle N: fighting nation 1 vs J in <area> (slot 87)`, `battle N: fought`; back in slot 77 as nation 1 in the new month; `battle N applied: X ships changed (F fields), R removed, VP a/b, prestige p` with X+R > 0; cross-check: every ship field the battle changed (Game87 after the battle vs the host save at the phase start; intel fields and BuildingNationIdx excluded) has Game87's value in the merged save (read at the applied line), sunk ships gone; after the month too (reported when another battle of the month changed them again); merged VP of 1 and J and Prestige of 1 = sent + the line's deltas; after the month host memory = its save (= merged unless another battle followed); `nations` ship counts minus the sunk; the client's Prestige/VP/ship count = the host's; chat lines; no exception; `battles list` empty. c/d) `battles timeout 20`; three candidates each a month (up to 3 months): c) Decline pressed in the preview; **reported as declined** (client `battle N: declined`, host `(Captain declined it)`, both chat lines: FAILS on this build, reported as `nobattle`); the host had the game resolve it (`resolved by the game (<why>)`, then `resolved by the game: battle|no battle`, chat), the month went on. d) a held preview: `resolved by the game (time limit)` 20-30 s after the send, client notice "... without you (time limit) ...", the late answer `battle result N from Captain ignored` (or not sent), the game resolved it, slot mode off; `battles timeout 1800` restores the default. Battles the game found itself (usually some): request line, sent and settled, chat "* Battle! Germany meets Spain in <area>. Captain fights it after the month." (SKIP line without any). e) rejoin with `fightbattles 0` (host gives nation 1 back and sends the month, the client loads it, myNation 1, fight=0); inject refused `no connected player who fights battles for nation 1`; a processed month at war: no request, phase or sent line. End: peace 1-J (the original tension), `battles state` none, `battles list` empty | 38 (37 without battles the game found itself) + 8 month checks per processed month (3-7 months; 61-94, usually 61-70) | 3 (+ up to 4 when the game finds no battle) | after `diplomacy` (peace on both fronts), before `victory_points` (the war 1-J is over and the client stays joined with `fightbattles 0` for the rest of the run); select `events` with it only when `diplomacy` is selected; the runner deletes Game87. After 0.2.8: `--only battles` 68/69 (130 s), full run 437/438 (only "reported as declined", fixed in 0.3.0); ebfca72 (0.3.0): 70/70 in the full default run (520/520) |
| `victory_points` | 0.2.6 joined players' victory points. a) both logs `Peace treaties: ok (... battleHooks=1/1/1)` (else one more failing check and stop); the client's latest `victory points shown: own V, enemy E` = its `vpinfo`; start: the host at war with nobody, 1-2 at peace. b) host `diplo war 1` (+ `diplomacy request 0 war 1 accepted`) + `applydiplo`; `battlevp 1 1000 3000 Minor`: the host's VP and EnemyVP rise as before, nation 1 + the EnemyVP gain (0.2.5: unchanged), `victory points: nation 1 +X in its battle against the host -> Y`, seen == enemy, nation 2 and AI 3-8 unchanged. c) `enemyvpadd 101` + `applydiplo`: nation 1 +101 (the only enemy), its `... +101 of the host's enemies' 101 this month -> V` line, seen == enemy, the host's VP/EnemyVP and 2-8 unchanged, `enemyVPSeen` in `rtw3mp_session.ini`. d) host `diplo war 2` + `applydiplo` (a second war resets nothing); `enemyvpadd 101` + `applydiplo`: 1 +51, 2 +50 (index order), two share lines, seen == enemy. e) a drop: `enemyvpadd -E` (the game's reset) + `enemyvpadd 101`: nothing shared, seen = enemy = 101. f) host offers peace to 1, the client accepts with its "Accept peace" button (row via LVM_SETITEMSTATE, BM_CLICK), equal `warvp` (compromise): separate peace, the host's VP/EnemyVP unchanged, `nation 1 is at peace; V -> 0`, nation 2 keeps its VP; peace 0-2 (`diploas 2 peace 0`) + host `diplo war 1` in one step: DoPeace resets the host's VP and EnemyVP, `nation 2 is at peace; V -> 0`, the new war started; re-sync: after that step seen == enemy == 0 and no share line in it, then `enemyvpadd 101` + `applydiplo` gives nation 1 +101 with its share line, seen == enemy == 101. g) client and host `diplo war 2` + `applydiplo` (the host at war with 1 and 2, 1-2 at war); `battlevp 1 500 2000` / `battlevp 2 300 1000` credit 1 / 2 (lines, the other unchanged); host peace with both (button, `diploas`) + `applydiplo`: DoPeace resets the host only, 1 and 2 keep their VP (no `is at peace` line), seen == enemy == 0 and no share line (the DoPeace hook credits nothing twice); EnemyVP marker `enemyvpadd 77`; one processed month: host VP of 1 and 2 = credited + their `war score 1-2` points, `merge nation 1`; client `vpinfo` vp == the host's, `enemy` = the VP of 1's enemies (> 0 and != the host's EnemyVP), client `victory points shown: own V, enemy E` once; i) the host's EnemyVP unchanged by the month and the merge. h) `settension 2 3 50` + `aipeace 2 0`: the AI war 2-3 ends, 1-2 goes on, `AI peace: VP of player nation K kept at P (the game set 0)` for 1 and 2, their VP kept, AI nations 0 (SKIP line without an AI peace). End: peace 1-2 (compromise) + `applydiplo`: 1 and 2 VP 0 with their `is at peace` lines, the host at war with nobody; marker removed, the host's VP and EnemyVP 0 | 42 + 8 month checks = 50 (48 if h is skipped); 3 when the VP hooks are unavailable | 1 (g) | after `diplomacy`, before `treaty` (treaty's `applydiplo` treaties must stay after the last processed month); the host's wars only via `applydiplo`, its battles only inside a host war that DoPeace ends (Known traps). 0.2.6: 50/50 (`--only victory_points,treaty` 130/130, full run 312/312) |
| `treaty` | 0.2.5 peace terms of wars between players. a) both logs `Peace treaties: ok (... battleHooks=1/1/1)` (else one more failing check and stop). Start: 1-2 at peace, host at war with nobody; `claimable 1 2/2 1/0 1` only the loser's possessions, Value < 11. b) client `diplo war 2` + `applydiplo`: war, chat, score 0:0; `warvp 1 2 A 100` (A = max(900, 100G+500)) -> `terms` outlook winner 1 level 2; client row vp/outlook "you are winning: ... major victory". c) client offers, nation 2 accepts (`diploas 2 peace 1`): terms agreed level 2 budget 8, chat on both sides, client row `claim=8:N:0`, a second offer refused, `rtw3mp_session.ini` has `warVP` and `treaties`. d) unknown name, the winner's own possession, a loser's possession worth >= 11, land worth > 8 refused on client (`claimland`) and host (`diploas 1 claim 2`); the loser can't claim; a valid claim accepted (terms, chat, `land claimed: v of 8`). f) client `diplo cancel 2` erases the terms (outlook again, `treaties=` empty, claim 0:0:0); agreed again the other way round, the old claim is gone. e) client window: row of nation 2 selected (LVM_SETITEMSTATE), button 1084 "Claim land...", Tension column `VP A:100`, Pending text, label = outlook; click opens `RTW3MP_Claim` (owner = MP window, MP window disabled, `claimui` target/budget/rows = `claimable 1 2`, claim pre-checked, sum label); untick + Cancel (1095) sends nothing; WM_CLOSE likewise; 1094 withdraws the claims (chat); ticks within 8, a tick past it is undone ("(over the limit)"), 1093 sends the claims (terms, chat, `claim=8:N:k`). g) processed month: one `treaty 1 over 2 (level 2): k possession(s) (claimed), s ship(s), prestige +2, resources -500` line, war over, the claimed possessions moved (host and client), treaty + "Peace: ... (<nation 1> victorious)." in both chats, client `showing a peace treaty (n lines)` + the "Peace treaty" dialog (`treaty_text`: heading, ceded, every hand-over as `... as war reparation.` plus its own sentences, reparations; n = the dialog's lines, one period per line; the same lines in both chats; answered), no host dialog; prestige exact: winner +2, loser by the formula (P div 10 when P >= 20 else 1, not below 0; major -1, -1 more above 20), the host (not a party) unchanged unless it answered an event that month; funds/ships reported. k) that month's `war score 1-2: +x/+y -> A:B` lines add up from A:100, no `first battle` line. h) `applydiplo` (after the last month): new war 0:0, nation 2 ahead (minor), 2 offers / client accepts (budget 4), the loser can't claim, > 4 refused, nobody claims -> `treaty 2 over 1 (level 1): k (automatic)` = most valuable first within 4, exact: Prestige 2 +1, loser 1 by the formula (minor), BaseResources 1 -r, ships -s/+s; the host (not a party) keeps its Prestige. i) host `diplo war 1` + `applydiplo`, `warvp 0 1`, client offers / host accepts, host `claimland 1 ...`, `applydiplo`: war over (warCounter 0, hostWars 0, `diplomacy: peace 1-0`), `treaty 0 over 1 (level 2) ... (claimed)`, claims moved, exact deltas (host Prestige +1 = treaty +2 and the DoPeace dent -1; loser 1 by the formula, major), the players keep their Prestige through DoPeace (nation 2 unchanged, nation 1 at its treaty value, host log `end of war: prestige of player nation N kept at P (the game set 0)` for 1 and 2), the host's own treaty dialog (same `treaty_text` checks). j) equal score 300:300: compromise terms, claims refused, `applydiplo`: "(compromise peace)", no treaty line, possessions/Prestige/BaseResources of the host, 1 and 2 unchanged | 65 + 8 month checks = 73 with every optional case (SKIP lines otherwise); 3 when the feature is unavailable | 1 (g) | needs `Peace treaties: ok`; h-j use `applydiplo` after the last month (see Known traps). 0.2.5: 73/73 (full run 262/262; with the reparation lines split at " She is renamed ", `--only treaty` 73/73 in 69 s, 11 lines per treaty) |
| `player_copy` | 0.2.7 joined players' "Copy game". Start: the client's date and `nations`, a snapshot of slot 77, the client's cooldowns (`events_<hash>.ini`). a) client `copygame 78`: the game's "Rule the Waves III" box [OK] and the mod's "Multiplayer" box naming both nations, no overwrite prompt; client log `player copy in slot 78: Germany is the player nation, Great Britain is nation 1 (AI); ...` with the counts of the specification; Game78 compared key by key with `expected_copy`/`expected_map`/`expected_off` of slot 77: files (.sta copied, no Autosave.sac/.off, BattleInfo.bbi or DesignFiles_swap.tmp), names and the 5 traded [Nation0*]/[Nation1*] sections (ship ids), every relation key of Nation0-8, nation numbers 0 <-> 1 and the host's ships' CommanderId -1, [General] (War/EnemyVP rule, ScenarioInProgress -1, ...), [EUT] = the client's cooldowns, MapData mines/TakenFrom, .off divisions, design files 0 <-> 1, nothing else changed (12 checks). b) slot 77 and slot 1 byte-identical; the client still plans nation 1 in slot 77, same date and nations. c) host `copygame 79`: the game's box only, Game79 = slot 1 file by file ([Nation0] Great Britain), no copy log line. d) `copygame 78` again, "Confirm" answered Cancel: `copy of slot 77 to 78: not copied`, Game78 and slot 77 unchanged. g) the real "Copy game" button -> `TdlgInputNumber` (the highest free slot of 3-9) -> boxes, log line, the copy as specified and byte-identical to Game78 (SKIP line without a free slot). One session month: `merge nation 1`, the client plans the next month as nation 1. e) client `leave`, slot 77's `rtw3mp_player.ini` `nation=1`, `load 77 0` plays the host's nation (what 0.2.6 copied), `copygame 80`: boxes and log line, Game80 as specified from slot 77's new month ([EUT] kept outside the session). f) `load 78 0`: slot 78, playerIdx 0, no dialog; `nations` = the session's values with 0 and 1 swapped; `rel 0 j`, `rel 1 j`, warCounter and hostWars match the copy's file; one single-player month (`turn`, dialogs answered up to the turn report, no battle), then `save` writes the next month into slot 78; responsive, Great Britain (AI) keeps ships and possessions, no exception or session line in the client log | 26 + 8 month checks = 34 (33 if g) is skipped) | 1 (+1 single-player month of the copy) | the last of the non-lockstep scenarios (it leaves the session; the lockstep scenarios join again); Game78-80 must not exist before the run, the button's slot (3-9) is chosen free; all deleted afterwards. 0.2.7: 34/34 (`--only player_copy` 41/41 in 66 s; full run 346/346) |
| `lockstep` | live player-vs-player battles, the host against the joined player, short (from the research driver ls_live.py). a) `ls session` enabled=1 available=1 on both, the host lists `Captain(1,b,ls)` (else one failing check and stop); the client's plans changed (nation 1 ResearchPct, not submitted). b) `pocbattle 1 0` (the debug Test battle = DoBattle outside a month: battle slot mode): host preview accepted, "battle held at its start", "battle <bid> epoch 1 sent (n files, b bytes ...)", the client's "battle loaded from slot 87", both running within 150 s; roles A side 0 / B side 1, same bid/epoch, pocstate playerSide 0/1, host `ls session` hostOpp=1 testSlotMode=1 slotMode=1, client role=B slotMode=1. c) runs asked by A (3) and B (2) at speed 3: >= 5 minutes, every per-minute hash equal, no DESYNC. d) orders of both sides at one paused boundary: the same course on both copies = the ordered one, each copy received the other's batch; d detach) the client detaches a ship (`ls detach`) between minutes: one division more on both copies (22 -> 23), the next 2 minutes equal, no resync; d guard) `ls dispwrite` on the client, then on the host: "the display wrote N sighting fields ... (kept out of the simulation)" on that copy, no LEAK, no DESYNC, the next 2 minutes equal. e) `ls desync` on B: "DESYNC at minute m", "Synchronising the battle again", a new epoch, the next 2 minutes equal. f) both `ls end` before minute 400: "The battle can't end yet" on both, both running, end=0/0. g) `ls drop`: "dropping Captain: test", "The AI commands Germany now", the client back in slot 77 as nation 1 (its three log lines), the host runs 2 minutes alone; the client's plans kept (canLoad saved slot 77). h) the host's battle on auto-run to its end and closed: "battle slot: DoStrategicTurn skipped", date unchanged, slot mode off. i) after-checks (`ls_after_checks`: also slot 1's and slot 77's saves of the copies' months). k) one normal month: the plans reached the host. l) no lockstep error in the logs, `lockstep hooks: ok` at most once per copy; the leak check (on in every copy) logged no LEAK | 16 + 8 month checks = 24 | 1 | after `player_copy`; joins the session again if needed (and turns the leak check on). c06860f: `--only lockstep` 31/31 in 108 s; full run 24/24 |
| `lockstep_pvp` | a live battle between two joined players, three copies (from ls_pvp.py). a) a third copy (bridge 47703, slot 76, battle slot 86) joins as "Bot": the host gives it the reserved nation 2 and its month (`state seq N -> Bot (nation 2, ...`), it plans nation 2 in slot 76, the host lists `Captain(1,b,ls)` and `Bot(2,b,ls)`. Setup: war 1-2 (`settension 1 2 50`). b) `battles injectpvp 1 2 Northern Europe` before each month (up to 3; the attacker's CheckAttackLocation often finds "no battle came about", the game also finds 1-2 battles itself) until a live battle runs (`ls_month` with K2: both clients submit): the request "fought live by <p1> and <p2>", "battle N sent to <p> (B bytes), live against <q>", the relay ended with the result, "battle N applied: ..."; the attacker's copy as A in its battle slot (StartPlayerBattle, epoch 1 sent), the defender's as B (loaded from slot 86/87, left the battle slot afterwards); every per-minute hash equal; an order of B the same on both. Default: 10 live minutes, A drops B, A fights on alone on auto-run and sends "battle N: fought". --stress: to the agreed end (>= 400 minutes) on both, plus h) an idea-1 battle of nation 1 against an AI nation injected into the same month: run before or after the live one, never at the same time, and settled. c) the new month on all three copies, ship counts of nations 1 and 2 equal on the three. f) the war score of the war 1-2 (host `warvp 1 2`, read before and after the month with the live battle(s)) rose by the VP of every live battle 1-2 merged that month (`battle N applied: ... VP a/b`, a = the battle's own nation K, the first player's, as in `battle request N: nation K vs J ..., fought live by ...`; mapped onto 1, 2) plus the host's own credits of that month (`war score 1-2: +x/+y -> A:B` of its battle hooks, e.g. the game's resolution of an unfought request; a fix's credit line for a merged battle, such as `... from a battle fought on a player's PC -> A:B`, is not added twice); still at war, the score not restarted (`starts at`/`first battle`); a `SKIP-note` when the live battles gave 0 VP (then it can't tell). Fails on 0.3.0, passes on 0.3.1 (MODLOG 2026-10-16). d) peace 1-2, the third copy leaves and is closed, nation 2 stays reserved for Bot; after-checks; e) no lockstep error in the three logs, no LEAK (the leak check is on in all three copies) | 14 + 9 per processed month (8 month checks + "advanced by exactly one month"): usually 23-41 (+1 with --stress) | 1-3 | needs `session`'s Bot reservation; Game76/86 must not exist before the run and are deleted after it. c06860f: full run 22/22 (1 month, battle 17: Captain A, Bot B, 10 minutes equal, A finished alone, merged); --stress 23/23 (1 month, two live battles 9 and 11 with the roles both ways, each 424 minutes equal, ended by the game's proposal + both End, plus the idea-1 battle). Released 0.3.0, `--only lockstep_pvp` (2026-10-16): 22/23, f) fails: battle 1 merged with VP 180/147, `warvp 1 2` 0:0 before and after. 0.3.1: `--only lockstep_pvp` 40/40 (2 months; battle 4 with Bot as A, VP 0/230: `warvp 1 2` 4:13 -> 239:26 = +230:0 plus the game's +0:1 and +5:12); full run 23/23 (battle 9, VP 518/193: 0:0 -> 518:193) |
| `lockstep_month` | **the host's battle against the joined player inside a REAL processed month** (CheckForBattles2 -> DoBattle during the host's turn -> lockstep -> end -> FormClose -> DoStrategicTurn -> DoEndOfTurn -> idea-1 battle phase -> the new month to the client). Setup: the host at war with nation 1 (`diplo war 1` + applydiplo). Months (`ls_month`, up to 8; the client sets nation 1 ResearchPct before each submit) until a host battle against nation 1 runs live; per battle: the host's preview, held, sent, started, running at minute 0 in this order after "host turn: 1 submissions merged", `ls session` hostOpp=1 testSlotMode=0 slotMode=0; one DoEndOfTurn, the date one month on; the client loaded it from slot 87 as B; every per-minute hash equal, the client's order the same on both; the end (default `drop12`: 12 live minutes, the host drops the client and fights on against the AI on auto-run; --stress battle 1 `agree`: the end at >= 400 minutes, by agreement or (after a refused agreed end) by the game itself, battle 2 `endsoon` (0.3.0): after 6 live minutes A sends `ls endsoon` and the GAME ends the battle inside the next minute ("TEST scenario length a -> b: the game ends the battle at the end of minute M" and "Scenario absolute time limit is reached!" on both, over at minute M); for both: both copies report it as the normal end ("The battle is over (...)"), not "the battle screen was closed", and the host logs no "WARNING ... handled inside a simulation step"; the deferred close (battle 2, and any battle whose results' "Exit battle" came inside a minute): on both copies `"Exit battle" in the results: the battle screen closes after this minute` -> "The battle is over" -> "live battle ended (normal)" -> `closing the battle screen ("Exit battle" in the results)` in this order, the host's month after it with the game's own dialogs answered by the test ("Turn messages") and no minute-box/"Battle: ..."/WARNING line between the close and "DoEndOfTurn finished" (on c06860f the month ran inside the battle's simulation step)); the month completed and reached the client once (one `state seq N -> Captain (nation 1,` line, no "turn did not advance"), host and client in the new month, live battle and slot mode off; the campaign (`battle_effects`: the live battle's save in slot 1 at its start vs the host's save at the battle phase start (after DoEndOfTurn, read by `StageWatcher`; the host's save after the month when no battle phase ran): ships of both nations took part (battle star / "Took part" log entry), every warship loss counted in [NationNLosses] has a lost ship (gone, or Fate no longer "XXX", whether it took part or not; MSLost, the month's trade war, is only reported), the step-by-step attribution of the month's losses logged, the client's slot 77 has the same ships, battle fields (RepairTime, BattleStars, log entries, Status, Fate, CrewQuality) and Losses for nations 0 and 1; ship counts and VP of 0 and 1 equal on host and client (the "victory points: nation 1 +X in its battle against the host" line if any); the client's ResearchPct of that month on host and client. --stress: h) the second battle's months also carry idea-1 battles of nation 1 against an AI nation (war 1-J, two injected a month): the battle phase ran after the live battle and its battles were sent and settled; h2) each idea-1 merge of that month (saves read at once: as sent -> Game87 -> merged): the merged [Nation1Losses]/[NationJLosses] = the battle slot's and every loss the merge added has a lost ship (the game's resolution of unfought battles after the last merge is reported). Then peace (compromise, the client's "Accept peace" button) and one normal month (no battle); after-checks, error/leak scan | 1 + 9 per `ls_month` + 9 per battle (`drop12`; 10 for `agree`, +1 when its close was deferred; 11 for `endsoon`; +2 interplay h/h2) + 14: 33 with one battle month (default), 57 with --stress and two battle months | 2-9 | `drop12` in the default run (the agreed end (5-8 min) and `endsoon` (~1.5 min) only with --stress). a1a8484 (0.3.0): `--only lockstep_month --stress` 65/65 (Aug 1900 battle 100000 agree 417 minutes, its close deferred; Sep 1900 battle 100001 endsoon at minute 6 -> over at 7; idea-1 battles 1 and 2 merged with no losses; the game's resolution of battle 3 sank DD V2: DDLost 0 -> 1 after the last merge). c06860f: full run 32/33 (Nov 1901: battle 100001, 12 minutes equal, "victory points: nation 1 +198 in its battle against the host"; the failure was MSLost, fixed); `--only lockstep_month,lockstep_stress --stress` 53/53 (Aug 1900 battle 100000: 424 minutes, agreed after the game's "far apart" proposal; Sep 1900 battle 100001: 451 minutes, the game's proposal agreed, Nymphe sunk, +15 VP, idea-1 battle phase after it); full --stress run 51/53 (499 and 529 minutes equal, both ended by the game inside a minute: the host's month nested in the simulation step) |
| `lockstep_stress` | `--stress` only. Every case is an `ls_case` (a failure is one check and `ls_recover` cleans up; the scenario goes on) and ends with the client back in slot 77. Test battles (`pocbattle 1 0`): S1 d) spam 40 s each of run/pause/speed, of orders (the host runs a minute every 1.2 s) and of both, from both copies (~2.5 commands a second each): no DESYNC, no clock difference ("release of minute N at minute N+1"), no resync, no drop, no error reply (an order to a division that is gone is not one), both idle at one boundary afterwards and 3 more minutes equal; e) both `ls end` during a resync (live, in sync afterwards) and right after a drop (no hang; the client back). S2 e) both `ls end` while the client loads the battle (starts and runs in sync); the client leaves (`ls abort`). S3 c) counter reset: 2 desyncs, 12 good minutes reset `resyncs` 2 -> 0, 3 more stay live, the 4th: "dropping Captain: the battles could not be kept in sync"; storms on B and on A (a new battle each): 4 injected desyncs, each caught in its own minute (4 DESYNC lines, the RNG part of the desynced minute reported), the minute after each resync equal on both including the RNG part (3/3), the 4th -> the AI takes side 1, the host goes on alone, the client back, no hang. S4 a silent client (`ls mute 1`, timeouts 12 s): dropped after 12 s, the client leaves; loadto: a client muted from the start never loads: the host gives up after its 15-s load timeout ("the battle was not loaded within 15 s") and goes on alone, the client never left slot 77. S5 a silent host: the client leaves after 12 s. S7 robust: `ls throw` and `ls stall` on the client and on the host each end in a resync (the minute marked failed), `ls nest 8` + `ls resync` waits for the outermost message loop on the client (load) and on the host (send), live afterwards, 3 minutes equal; the guard's negative control (`ls guard 0` + `ls dispwrite` on the client): DESYNC + resync, the leak check logs LEAK (excused), the guard back on at the DESYNC: 2 minutes equal, no later LEAK; S7 drops the client at its end. g) private memory of both copies after each of these 9 battles: average growth <= 25 MB a battle (reported per battle); no lockstep error, no LEAK. S6 a) the client killed (taskkill /F) at a boundary: the host drops it at once ("Captain disconnected"), its battle goes on 2 minutes; the client started again joins while the host's battle is open and must get nation 1 and its month without claiming again (0.3.0: by the hello, `myNation` 1 while the battle is open and the client's chat "* Captain joined as Germany", or by the host's `reservation: Captain gets nation 1 back` and "* Captain is back as Germany" (`rejoin_back`); failed on c06860f). f) a test battle with every division on the AI at speed 5 to its end (>= 400 minutes: the game's end, its proposal agreed, or the agreed end after minute 1200; after a refused end 30 minutes with `ls_run_total`, up to 20 end requests), every minute equal, both screens closed, a normal month. Real months (host at war with 1): P1 i) the client with `fightbattles 0`: the host's battle against it "fought against the AI (the player doesn't fight battles)", no lockstep, month completes; P2 a) the client killed in the middle of a minute of the host's live battle: dropped within 60 s, the host's battle (auto-run) and month go on; the client started again (RTW3MP_LIVE_BATTLES=0) joins at once and must get its nation (the same evidence) and the new month without claiming (failed on c06860f); its month loads; P3 i) that client (`ls session` enabled=0, host `Captain(1,b)`): "fought against the AI (no live battles on the player's PC)"; P4 live again, peace, a normal month. b) the host killed during a minute of a live battle: the client leaves the battle within 60 s, is back in slot 77, not stuck (slot mode off, strategic screen enabled, Turn button visible and enabled), `leave`; slot 1 put back from the backup, the host started again, a new session (join, claim, start), one month | ~90 + 9 per `ls_month` + 8 per `run_month` (~150 in all) | 6-15 | last. c06860f: S1-S5, S7, f), P1, P3, P4, b) pass; S6 a) and P2 a) fail (the rejoin issue). a1a8484 (0.3.0): 90/90; S6 a) and P2 a) pass, both by the hello (myNation 1 during the battle, phase planning / processing) |

Full default run of 0.3.1 (2026-10-16, `--dist` = a frozen copy of its `dist\`): **512/512** (16 processed months plus
the copy's single-player month, game open 763 s; `lockstep_pvp` 23/23 with its new war-score check f)).
Full run of the v0.3.0 release candidate (a1a8484, `--dist` = a frozen copy of its `dist\`): **510 checks** in the
default run (16 processed months plus the copy's single-player month, game open 826 s: 509/510, the fail a test bug
fixed since: AUXLost counted as a warship loss) and **625 checks** with `--stress` (23 months, game open 2738 s = ~46
min: 622/625, the three fails the random `research` mod issue below: the game's AI branch of DoResearch re-rolled
nation 1's priorities; the scenario passed on its rerun). With ebfca72 (that issue fixed, `research` e) added): **520/520** in the default run (16 months, game open 777 s). Earlier, c06860f: **525 checks** in the default run
(19 processed months plus the copy's single-player month, game open 863 s = ~14.5 min: 522/525, the fails being the
`battles` c/d timing race and a test bug fixed since) and **619 checks** with `--stress` (+`lockstep_stress`, the long
`lockstep_pvp`/`lockstep_month` variants; 23 months, game open 3005 s = ~50 min: 615/619, the four fails are the two
mod issues of 2026-10-15 in MODLOG, fixed in 0.3.0). The counts move with the months the random parts need (`battles` 61-78,
`lockstep_pvp` 23-41, `lockstep_month` 33-60, `lockstep_stress` 90 + 9 per extra real month). Before the lockstep
scenarios: 368 checks with peace treaties available plus `battles` (37-38 + 8 per battle month: 429-438 with its usual
3-4 months; 367 + ... if the host is at war in month B; fewer if a treaty SKIP, the `victory_points` h) SKIP or the
`player_copy` g) SKIP applies), 11-12 processed months plus one single-player month of the copy (~7-10 min of game
time; 424 and 599 s in the two runs after 0.2.8, which met random host loops once each). Without `battles`: 368
checks, 8 months (322 s in the 0.2.8 run). With `Peace treaties: UNAVAILABLE` the `victory_points` and
`treaty` scenarios stop after 3 checks each: 251 checks, 6 months (~3.5 min) without `battles`.
Scenarios may share months: `ctx.month_hooks` callables run after every `run_month` (the `doctrine` scenario
sets up, then checks after the next two months; `research` checks after the next month; `events` checks after the
next month, after its random-path month and after the war month), `ctx.pre_month_hooks` (ctx, label, n) run before month n of the run is submitted
(`events` sets `eventchance 100` there); a hook that never got its months fails at the end.

Standalone (not in the default run): `tools/realmouse_test.py` (real mouse, needs consent) and
`tools/record_hosting.py` (records the docs video).

## Bridge command reference
The bridge exists only when `RTW3MP_BRIDGE_PORT` is set at launch, and it listens on 127.0.0.1 only. Send one
command per line, for example `python tools\rtw3ctl.py cmd 47701 status`. A reply is one line, `ok ...` or
`error ...`. Commands run on the game's main thread; the bridge waits at most 120 s for them. Commands that can
open a modal game dialog are asynchronous: they reply `ok` at once and log their result, and the test answers the
dialogs. Sources: `src/mod/bridge.cpp`, `src/mod/lockstep.cpp` (`ls ...`), `src/mod/poc_battle.cpp` (`poc*`).

`ping`, `status` (role, phase, myNation, date, status, players, submitted, remaining, campaignReady, slot,
startBuild, playerIdx, busy, battle),
`nations`, `host <name> <port> <limitMin>`, `join <name> <ip> <port>`, `claim <name|index>`, `start`,
`submit`, `turn`, `advance`, `leave`, `load <slot> <player>`, `save` (writes the slot: safety rule 6),
`mpwindow`, `windows`,
`nfield <nation> <IntegerField> [value]`, `areas`, `shipinfo/shiporder`, diplomacy: `diplo <action> <n>`,
`diploas <from> <action> <n> [extra]` (host; extra = the claim list for `claim`), `reserve <n> <player>` (host:
stand-in human nation), `rel <a> <b>`, `diploview` (rows `nation/name/player/relation/tension/[pending]/flags`, since
0.2.5 followed by `/vp=mine:theirs/claim=budget:nclaimable:nclaims/outlook`; parse with `regression.row`),
`settension`, `aipeace` (async), `applydiplo` (async), peace treaties (0.2.5): `claimland <loser> <name|name>` (the
winner of an agreed peace, client or host; empty list = leave it to the peace conference), `warvp <a> <b> [vpA vpB]`
(host: read or set a player war's score -> `ok a:vpA b:vpB scale=G`), `terms <a> <b>` (host: `ok agreed winner=W
loser=L level=N budget=B claims=x|y|` or `ok outlook winner=W level=N vp=a:b`), `possessions <n>`, `claimable
<winner> <loser>` (`ok N name~area~value[~oil]; ...`), `claimui state` / `claimui toggle <row>` (client claim window:
`ok open=0` or `ok open=1 target=.. budget=.. mpEnabled=0|1 checked=i,j sum=<label text> rows=i:name:value; ...`;
toggle ticks a row through the list view's own LVN_ITEMCHANGED path), design studies:
`designs <nation>` (index, class name, ReadyForBuild for each design), `setready <nation> <index> <months>`,
intel reports: `intel` -> `ok count=<entries> unique=<distinct non-empty>` (host or client, the campaign in
memory; a clean list has count == unique), doctrine: `doctrine <nation>` -> `ok training=a,b,c,d pending=a,b,c,d
months=N missiles=M pendingMissiles=P missileMonths=T`, `settraining <nation> <p0> <p1> <p2> <p3> <months>` (as the
dialog's Apply, with a chosen month count), `setmissiles <nation> <policy> <months>` (0 Minimum, 1 Adequate,
2 Plentiful), file writes: `writetest <slot> <file name> <bytes>` -> `ok ms=N` or `error write failed (E) ms=N`
(writes <bytes> 'x' through the mod's `WriteFileBytes` into that slot's save folder, on the main thread; use a
throwaway name, never a real save file), random events (0.2.4): `eventlist` -> `ok <idx>:<cond>:<H|->:<caption, 40
chars>; ...` (H = needs the host, never rolled for joined players), `eventnow [idx]` (client, async: shows the
game's event dialog now; refuses host-only events, then `eventlast` is `error event needs the host`; a forced event
ignores the client's once-a-month roll guard, `eventnow` without an index does not), `eventlast` (client: the last
event sent, `event=N tension=j:d;..
alliance=j:m;.. delta=Field:d;.. set=Field:v;..`, or `none` / `error ...`; host: the last one applied, `nation=K
event=N tension K-j a->b ... Prestige+1 ...`), `eventchance <pct>` (client: chance of the monthly roll).
While a client's event dialog is open its nation list is reordered, and its bridge answers only `status`,
`eventlast`, `eventlist`, `windows`, `diploview` (everything else: `error an event dialog is open`).
Victory points (0.2.6): `vpinfo` -> `ok enemy=<EnemyVP> seen=<S> vp=0:v0,1:v1,...,8:v8,` (host: the game's EnemyVP = the
host's enemies' score and S = the part of it the host already credited to its human enemies, -1 = not known yet;
client: `enemy` = the "Enemy VP" it shows, `seen` always -1), `battlevp <opp> <vpHost> <vpOpp> [Major|Minor|Marginal]`
(host, synchronous: sets BattleOpponentIdx and calls the game's own `HandleBattleAftermath` through the mod's hook, as
after a tactical battle; raw VP, the game log-adjusts them -> `ok host=a->b enemy=c->d opp=e->f`), `enemyvpadd <n>`
(host: EnemyVP += n outside a battle, like a blockade or a sinking; a negative n imitates the game's reset; replies
with the `vpinfo` text).
Copy game (0.2.7): `copygame <toSlot>` (async, `ok` at once) runs the game's `CopyGameToOtherSlot(open slot, toSlot)`
on the main thread through the mod's hook, as the "Copy game" button does after its slot dialog. The game's dialogs
follow: the overwrite prompt only if the target exists, then "Saved game copied to slot N"; from a joined player's
slot (77) the mod then rewrites the copy and shows its "Multiplayer" box (see Known traps). Host copies of its own slot
are left alone. Log lines (client): `player copy in slot N: <nation> is the player nation, <host> is nation K (AI); S
sections, C commanders, M map fields, divisions a kept / b dropped, at war|at peace`, `copy of slot 77 to N: not
copied` (cancelled or not copied), `copy of slot 77 to N: no joined nation known; left as it is`.
Research screen (0.2.8): `research <nation>` -> `ok pct=<ResearchPct> prio=<25 comma-separated Prio values>` (1 Low,
2 Medium, 3 High; the save holds areas 0-22, records 23-24 are 0; area indexes as in the game's
`Data\ResearchAreas3.dat` headers), `researchopen` (async, `ok` at once) runs the strategic screen's Research button
handler (`btbnResearchClick`) on the main thread: the modal `TfrmResearch` opens (not while an event dialog is open).
Since 0.2.8 a joined player's screen opens its own nation; client log `research screen: opened for the player's nation
K instead of nation 0` (none on the host). Since ebfca72 the mod hooks `TfrmBuildCamp.DoResearch(aNationIdx)`, whose AI
branch (every nation > 0) sets areas to High by chance and with a 5 % chance a month re-rolls every priority: a joined
player's 25 Prio values are put back after it, host log `research priorities of player nation N kept (C changed by the
game's AI choices)`. Host `researchrun <nation> <count>` (async, `ok` at once; tests) runs the game's DoResearch for that
nation `count` times through the hook on the main thread (each run is a month of research progress for it; it may show
the game's research messages: answer them, don't poll the host's bridge meanwhile), then logs `research test nation N x
count: before ok pct=.. prio=.. after ok pct=.. prio=..` (`research test: not possible` without a campaign, for nation
0 or while an event dialog is open). A joined nation's research progress gained that way is replaced by the next merge
of its [NationK]; an AI nation keeps it (the control uses Austria-Hungary, nation 8, which no other scenario checks).
Joined players' battles against AI nations (0.3.0, developed after 0.2.8; `session.cpp`/`game.cpp`):
host `battles` / `battles list` -> `ok phase=0|1 timeout=S battles=id:K-J:area:state[:why]; ...` (state `queued`,
`fighting` = sent, `fought` = merged, `to the game`, `resolved by the game`; the area may contain spaces: parse with
`battles_list`), `battles inject <K> <J> <area name with spaces>` -> `ok <id>` (planning only, else `error not while
the month or its battles are processed`; a request as if the game had found it this month, processed at the end of
the next processed month; refused: `error no sea area '<name>'`, `error K must be a joined player's nation and J an AI
nation` (also for a reserved stand-in nation as J), `error no connected player who fights battles for nation K`),
`battles resolve <id|all>` -> `ok <n>` (the game resolves it, a host decision), `battles timeout <s>` (default 1800,
env `RTW3MP_BATTLE_TIMEOUT`); client `battles state` -> `ok state=none|pending|starting|fighting id= nation= opp= area=
slot=87 slotMode=0|1 aftermath=0|1 fight=0|1` (`id=-1` while `starting`: the preview is up; `aftermath=1` once
HandleBattleAftermath ran in battle slot mode, until the slot mode ends), `fightbattles 0|1` (before `join`: sent with
the hello; a rejoin with another value changes it). Logs: both copies `Joined players' battles: ok (frmMain=..
checkAttack=..)`; host `battle request N: nation a vs b in <area> (x:y ships), fought by <player>` (+ chat `* Battle!
<K> meets <J> in <area>. <player> fights it after the month.`), `battle request N injected: nation K vs J in <area>`,
`battle phase: N battle(s)`, `battle N sent to <player> (B bytes)`, `battle N applied: X ships changed (F fields), R
removed, VP a/b, prestige p (player: VP a/b, prestige p, morale m)`, `battle N: resolved by the game (<why>)` and after
the merge `battle N resolved by the game: battle|no battle`, `battle result N from <player> ignored`; client `player
copy in slot 87: ...`, `LoadCampaign slot 87 player 0`, `StartPlayerBattle <a> attacks <b> in <area> -> battle|none`,
`battle N: fighting nation K vs J in <area> (slot 87)`, `battle slot: DoStrategicTurn skipped`, `battle N:
fought|declined|nobattle|failed - why`, `notice: The host resolved this battle without you (<why>). ...`.
The POC commands below work on whichever copy shows a battle (the suite uses `pocstate`, `pocrun`, `pocsideai`,
`pocspeed` on the client's battle).
Battle research POC (2026-10-14, `src/mod/poc_battle.cpp`; nothing is hooked or changed until one of these is used;
not part of the suite; drivers in the research report): `pocresolve` (the POC's runtime lookups: `ok` or `error
<missing>`), `pocmissions <opp>` (`ok m:title~possession~vp<n>~loc|noloc; ...`, the war missions against nation opp),
`pocbattle <opp> <mission>` (async: the debug "Test battle" without its input dialogs = `DoBattle`; the
`TdlgBattlePreview` "Battle preview" follows, answer **Accept**; it autosaves into the open slot: backed-up runs only),
`pocstate` (`ok running=<tmrRunMinute on> moving=<tmrMoveInterval on> frm=1 visible= t=<battle hh:mm> min=<battle
minute> left=<MinutesLeft> finished=<0 before the first run and while a minute runs, 1 at a boundary> pause= autorun=
endgame= engaged= speed=<GameSpeed -3..5> intervals= ivsec=<seconds per interval> playerSide= forces= divs= allships=
af= torps= draws=<sim>/<ui>/<other> gate=<on>/<tokens>`; idle at a boundary = running=0 moving=0 finished=1),
`pocrun <minutes>` (async: the Run buttons' StartRun; a battle break (PauseGame) stops it early), `pocpause`,
`pocspeed <-3..5>` (the speed box; it changes the simulation, not only the display), `pocrng <seed> [iso]` / `pocrng
off` (seeded battle RNG behind Random32Proc; `iso` puts display-only draws on a separate, not reproducible stream),
`pocrngstat` (top callers of display/other draws as RVA*count), `pochash on|off|now` (per-minute state hash in the log:
`poc hash #n t=.. min=.. all=<16 hex> pos= dmg= ammo= sight= ident= ord= air= torp= ships=n(side0/side1) ... draws sim=
ui= other= rng=<state> speed=`; `ident` is display state and not part of `all`), `pocdump <file>` (the hashed fields per
division/ship as text), `pocgate on|off|step [n]|status` (holds each minute at its boundary until a token is given;
`holding=1` while a minute waits), `pocdiv <division index> <course> <speed> [ai]` (what the course/speed boxes write;
-1 keeps), `pocsideai <side> <ai 0|1> <human 0|1>` (hand a side to the AI or to a human), `pocplayerside <0|1>` (the
battle's human side, display test only: it changes the simulation), `pocsac <file>` (async: the battle save routine
into any file, then a "Game saved!" box), `pocsavebtn` (async: the battle screen's Save button: `RTWGame<slot>.sac` +
the campaign with ScenarioInProgress, then "Exit game?" (answer No) and "Game saved!"). A campaign `load` of that slot
resumes the battle (possibly after "Select autostart point"). Since the live battles the `poc*` commands run on the
battle engine of `src/mod/lockstep.cpp` (its hooks are installed by the first `pocrng iso`/`pochash on`/`pocgate on` or
the first live battle; `pocstate` adds `ls=<off|holding|loading|waitready|running|ended>`).
Live player-vs-player battles (lockstep, 0.3.0; `src/mod/lockstep.cpp`, integration in `session.cpp`):
a host battle against a joined player's nation (the game's DoBattle, e.g. `pocbattle 1 0`) whose player fights battles
(`fightbattles 1`) and announced live battles (hello `ls=1`; env `RTW3MP_LIVE_BATTLES=0` turns them off) is fought live:
the host holds the battle at minute 0, saves it (the Save button's files, into the host slot: backed-up runs only),
sends the slot's files, both copies load them (the client into its battle slot 87) and run in lockstep. Outside a
month (tests) the host's battle runs in battle slot mode (its close runs no month). Bridge `ls ...` on either copy:
`ls status` -> `ok on=0|1 role=A|B st=idle|holding|loading|waitready|running|ended bid= epoch= side= minute= t=
boundary=<minute of the last boundary> running=<a minute runs> left=<minutes A still releases> speed= peer= peerAI=
muted= seq=<own>/<other's batches> pending=<unreleased own>/<other's> overlay= verified= peerDone= resyncs= ok= end=0|1/0|1
proposal= busy= leave= atb= broken= failed= peerEnded= lost= frames= disp=<dispatcher depth>/<base> hash=<state>-<rng>`
(idle between minutes: both `st=running running=0`, A `left=0`, A `boundary`
= B `boundary` = A `peerDone`); `ls run 1|5|until|<n>`, `ls pause`, `ls speed <-3..5>`, `ls end` (toggles this
player's end request; both requested = the agreed end at the next boundary: the game's own End routine, which refuses
before minute 400 / with enemies near: `The battle can't end yet: <reason>`; after the end the battle screens stay open:
press their "View results" button, then "Exit battle"), `ls order <division index> <course> <speed> [ai]` (as the
course/speed boxes write; becomes an order batch), `ls detach <division index> <ship index>` (async; between minutes
only), `ls divs` (`ok i:side:name:course/speed:ships[:ai]; ...`), `ls hash`, `ls desync` (this copy's next minute
differs: expect `DESYNC` and a resync), `ls drop` (A: side 1 to the AI now), `ls abort` (B: leave), `ls resync` (A,
async), `ls mute 0|1` (this copy drops all lockstep traffic: a silent player), `ls timeout <boundary s> [lost s]
[load s] [heartbeat s]` (defaults 30 30 120 5; kept across battles), `ls trace 0|1` (files `ls_trace_<A|B>_<bid>_<epoch>_<minute>.txt`
(the call sites of the shared random draws, plus state-hash markers at the entry/exit of every outer simulation step and
shared section; decode with the session folder's `ls_tracediff.py <bid> <epoch> <minute>`) and `ls_dump_...` (pocdump)
in the RTW3MP data folder per minute; slows the frames), `ls hash` (hashed in the simulation's set of the dual fields),
`ls view 0|1` (the display swap to the own side from the next load; 0 = diagnosis only).
Test commands of the review/round-3 fixes (work on either copy; `leakcheck` also outside a battle, the others only in
a live battle): `ls leakcheck 0|1` (a per-process switch, kept across battles: the simulation state (hashed ship,
torpedo and air fields except orders and identification, plus the division/ship list order) at the end of every
simulation step must be the same at the start of the next one, else `lockstep: LEAK the simulation state changed
outside the simulation (<group>) between <X> and <Y> at t=..; display routines in between: <names>; <ship.field
old->new>` (up to 50 per switch-on); the suite turns it on in every copy and asserts no LEAK line), `ls guard 0|1`
(the sighting fields' display/simulation dual set; 0 = the old leak, tests only: restore 1), `ls dispwrite` (between
minutes: +1 to every ship's four sighting fields outside the simulation, as the battle map's GlobeRender does -> `ok
<n>`, log `TEST <n> sighting fields changed ...`; with the guard on the next boundary logs `the display wrote <n>
sighting fields since the last boundary (kept out of the simulation)` and nothing desyncs; with `ls guard 0` it desyncs
and the leak check logs LEAK), `ls throw` (an exception in this copy's next MoveAllShips, code e0545354: `TEST
exception in MoveAllShips`, `context frame MoveAllShips dropped`, `the minute from N failed: ...`, a resync), `ls stall`
(this copy's next released minute does not start: `TEST minute start refused`, the 10-s watchdog marks the minute failed,
a resync), `ls nest <s>` (async: a nested message loop on the main thread for s seconds, `TEST nested message loop for s
s (dispatcher d/base)` ... `done`; a load or resync meanwhile waits: `waiting for the outermost message loop: inside a
nested message loop`), `ls endsoon` (0.3.0, A only, between minutes: the next released minute carries `endsoon=1` and
both copies shorten the scenario (the game's ScenarioLength) so that the game's own "Scenario absolute time limit is
reached!" ends the battle INSIDE that minute -> `ok length=<old length> minute=<boundary>`, B: `error A only`; both log
`lockstep: TEST scenario length a -> b: the game ends the battle at the end of minute M`; the results' "Exit battle"
then gives `"Exit battle" in the results: the battle screen closes after this minute (t=..)`, the normal end at the
boundary and `closing the battle screen ("Exit battle" in the results)`: the deferred close). More than 3 resyncs without 10 good minutes in between (or more than 3 failed minutes without a
good one) drop the client (`dropping Captain: the battles could not be kept in sync`). Session level `ls enable
0|1`, `ls session` (`ok enabled= available= role= hostOpp= hostBid= testSlotMode= leave= clientA= slotMode= relays=
players=<name>(<nation>[,b][,ls]);`), host `battles injectpvp <K1> <K2> <area>` (a live battle between two joined
players' nations for the end of the next month; K1's copy is A, K2's is B, the host relays). Logs (`lockstep: ...`):
`battle <bid> epoch <e> sent (<n> files, <bytes> bytes, seed <s>, speed <v>)`, `battle loaded from slot <s> (seed ..,
minute .., t=..) in <ms> ms`, `battle <bid> epoch <e> running at minute <m>`, per minute `lockstep hash A|B t=..
min=<m> all=.. ...` (same format as `poc hash`), `orders #<seq> (<why>, <n> items) ...`, `orders #<seq> of <peer>
received`, `released orders side0<=#a side1<=#b: ...`, `DESYNC at minute <m>: here <h>, <peer> <h>`, `Synchronising
the battle again (<why>)...`, `dropping <peer>: <why>`, `<peer> left the live battle (<why>). The AI commands <nation>
now; ...`, `The live battle ended on this PC: <why>`, `The battle is over (<why>).`, `minute box '<text>' answered
<n>` (the game's questions inside a minute get a fixed answer on both copies: keep the battle going), `quiet box ...`
(the mod's own save/load), `the display wrote <n> sighting fields since the last boundary (kept out of the simulation)`
(the guard caught a display write: normal), `WARNING <what> handled inside a simulation step` (a mod message dispatched
by a message loop inside a step), `waiting for the outermost message loop: ...` (a load/resync deferred: normal), `the
minute from <m> failed: <why>`, `resync requested: <why>`. Live test drivers in the session folder: `ls_live.py` (two
copies; 18 phases: basic, orders, races, detach, desync, robust, guard, storm, dstorm, long, drop, muteb, mutea, loadto,
disc, finish, rejoin, month, plus diag), `ls_pvp.py` (three copies:
host + K1 slot 77 + K2 slot 76 (battle slot 86); war 1-2, `battles injectpvp`, months until a live battle is fought and
merged) and `ls_drive.py <A port> <B port>` (a running live battle to its agreed end). After an ended live battle both
battle screens stay open: "View results" (a TButton on TfrmMain), then the results' "Exit battle".
Commands that may open modal game dialogs must be async on the bridge side; answer the dialogs from the test.

### Other commands
Also in `bridge.cpp`: `release <nation index>` (host: gives that nation to the AI, like **Give to AI**), `limit
<minutes>` (host: the turn time limit; restarts the countdown of a planning month and writes
`rtw3mp_session.ini`), `chat <text>` (a chat line), `setplayer <nation index>` (switches the nation the game
plays and refreshes the screen; tests only) and `savedir [slot]` (the slot's save folder, default 1).

## Known traps
- The host's month processing opens many dialogs (events, picture messages, turn report). Answer them
  with `dismiss_dialogs(pid)` in a loop and **clear `e2e_test._clicks` each pass**: the game reuses its dialog
  windows, so the per-window click guard otherwise aborts.
- Event dialogs (`TdlgEventAnswer`) need an answer selected before OK closes them (`dismiss_dialogs` picks
  the first answer).
- The host's own "Still waiting for ..." box (title "Multiplayer") appears if the host presses Turn before
  everyone submitted: wait for host status "All players are ready" first, answer it as a fallback.
- `campaignReady` in `status` is only reported while hosting; before `host`, use `playerIdx == 0` and
  `startBuild == 2` to know the campaign is loaded.
- Don't process a month while the host's nation is at war with a player's nation: the month would start a
  tactical battle the test can't play. Apply such cases with `applydiplo` instead. (Since 0.3.0 the `lockstep_month`
  scenario processes such months on purpose with `ls_month`, which plays the host's battle live; `run_month` still
  can't, see the live battles below.)
- Values a client edits during planning are merged into the host's save at Turn; diplomacy keys
  (`Tension`, `AITension*`, `AIAlliance*`, `Allied`) keep the host's values by design, and so does `VP` since 0.2.6.
- Random game events can delay things by a month (e.g. a 1 % "technical issues" design delay). Write checks
  that accept the documented random outcome and assert the evidence (log or chat line) instead of flaking.
- Never poll the host's bridge while it processes a month: bridge calls run on the game's main thread and the
  bridge gives up waiting after 120 s (`RunSync`). `run_month` polls only the client.
- List replies (`designs`, `areas`, `nations`, `diploview` rows) end in `; ` and `rtw3ctl.cmd` strips the reply,
  so the last entry ends in `;`: parse them with `regression.items`.
- When the host's own design study reaches 0 the game asks in a `TdlgEventAnswer` with radio answers "Go to the
  build screen / Not now / Rework the design" + OK: `dismiss_dialogs` selects the "Not now" radio before OK (the
  others open the build tab or the modal ship designer). The committee-review event uses the same class; any
  answer is fine there.
- The client's Multiplayer window only exists after `mpwindow` (the `session` scenario opens it); its chat log
  (control 1070) is where a player's private lines (design studies, doctrine, diplomacy) show up.
- Game dialogs can be driven with posted messages once you know their layout. Read it from the game's own form
  resource instead of guessing: load `RTW3.exe` as a data file (`LoadLibraryExW(..., LOAD_LIBRARY_AS_DATAFILE)`,
  read-only), `FindResourceW(<FORM CLASS IN CAPS>, RT_RCDATA)` and parse the binary DFM (`TPF0`): class, name,
  caption and parent of every control. Never run `dismiss_dialogs` on a copy while you drive one of its
  `Tdlg*` dialogs yourself (it would answer that dialog too).
- `TdlgDoctrine` (strategic screen button `btbnTraining`, a `TBitBtn` "Doctrine"): group box "Training
  priorities" with 4 `TCheckBox` (Gunnery, Night fighting, Torpedo warfare, Damage control), its own "Apply"
  (`bnApply`) and "Stop training"; a second "Apply" outside it belongs to the missile storage `TRadioGroup`
  (Minimum/Adequate/Plentiful), so pick the Apply inside the training group. "Close" (`bnClose`, mrOK) closes
  it. Toggle a check box by posting the parent (the group box) `WM_COMMAND` with `BN_CLICKED` and the box's
  handle as lParam (what a real click produces; VCL turns it into `CN_COMMAND` -> Toggle -> OnClick). A posted
  `BM_CLICK` does NOT toggle it: the game's VCL style hook wants the real cursor over the box (push buttons such
  as Apply/Close do work with `BM_CLICK`). `BM_SETCHECK` only changes the picture. Read state with `BM_GETCHECK`.
  Apply opens a "Confirm" `TMessageForm` (OK) and makes the pending change with 12 months; "Damage control" is
  training flag index 3. Tests then shorten it with `settraining`.
- A rejected submission: since the 0.2.3 fix the host logs `host turn: <name>'s turn NOT applied: <why>` and
  `merge nation K rejected: cannot write <what> (Windows error E)`, keeps the chat line `* Could not apply
  <player>'s turn: <why>` and shows a Notice; the merge is all-or-nothing. Every processed month asserts there
  is no `NOT applied` / `rejected` / `write ... FAILED` line (`month_checks`), and logs `write <path>: succeeded
  after N retries` / `replaced in place` lines (transient locks that were handled). Background: a 0.2.3 run lost
  a player's whole month to `cannot write map data`, a short lock on `MapData1.dat` right after the game's own
  save (antivirus; Documents under OneDrive).
- File locks for tests: open the file from Python with `CreateFileW(..., share mode 0, OPEN_EXISTING)`. A rename
  over it (`MoveFileExW`) then fails with error 5, an in-place write with 32.
- Joined players' random events (0.2.4): the client rolls once per calendar month as soon as the new month is
  idle (also at the session start). With an event the game's `TdlgEventAnswer` ("Event - <Month Year>") opens on
  the client and, while it is open, most bridge commands are refused. So `run_month` waits after every month
  until the client's roll line is in its log (`wait_event_roll`, 15 s) and answers the client's event dialogs
  meanwhile; only then do `month_checks` and the month hooks run. Layout (form resource `TDLGEVENTANSWER`):
  `lbEventCaption` (a TLabel, no window text), `Panel1` with `RadioButton1..3` (the answers, top to bottom) and a
  hidden `cbSelectNation` combo, `Button1` "OK", "View almanac", "View all relations". `select_first_answer`
  picks the top-most radio (screen position, not enumeration order) with `BM_CLICK` (falls back to `WM_COMMAND
  BN_CLICKED` to the panel) and the first nation if the combo is shown and empty; `dismiss_dialogs` then presses OK
  and keeps the selected answer.
- `Data\Events.dat` (game folder, plain text, read-only) is the event table: a header line
  `caption;N;;;;cond;fromYear;toYear;x` followed by its answers `text;budget;prestige;tension;c;x`. Event indexes
  in `eventlist` follow the file order (#5 = "XX appears to be considering a naval rearmament programme", first
  answer 2;1;2 = BudgetModifier +2, Prestige +1, tension +2). Handy to predict a forced event's deltas.
- A forced second event in the same month is shown and answered on the client and changes the client's own nation
  (e.g. Prestige); the host ignores its message (`second event ... ignored`), but the player's own fields still
  reach the host with the submitted turn ([NationK] is replaced by the client's values). Only `eventnow` can do
  this (the monthly roll is guarded by `events_<hash>.ini`); tests that compare nation fields after such a month
  must use the client's values at submit as the baseline.
- At war (confirmed by disassembly): `TfrmBuildCamp.EventLikelihoodCheck` rejects events with condition < 30 while
  the war counter is > 0 (and war events while at peace), so at war only war events qualify, and all of those need
  the host. A joined player at war therefore gets no random events by design (a chance-100 roll logs `event: no
  event this month`). The `events` scenario rolls its random event in the last month in which nation 1 is at
  peace, and checks the war rule with the war month's own roll at chance 100.
- `eventnow` without an index is this month's random roll and goes through the once-a-month guard: once the month
  was rolled (every month is rolled when it loads) it only logs `event: this month was already rolled`. To roll
  the random path in a given month, set `eventchance 100` before that month is processed (a pre-month hook).
- Client diplomacy requests are asynchronous: the client's bridge answers `ok` once `diplo`/`claimland` is *sent*.
  Before anything that depends on it (`applydiplo`, host `terms`, `diploas` accepting it) wait for the host log line
  `diplomacy request 1 <action> <target> accepted` (`client_request` in the runner). A race here made `applydiplo`
  run before the war declaration arrived. Host-side `diplo`/`diploas`/`warvp` are synchronous, but the client sees
  the result only with the next lobby broadcast: poll `diploview` (`wait_until`).
- Peace treaties (0.2.5): the scenario fails fast (3 checks) unless both logs say `Peace treaties: ok (... 
  battleHooks=1/1/1)`; without it every treaty call is refused or a compromise. The game's treaty dialog is
  `TdlgTotalLog` captioned "Peace treaty" (RichEdit `mmRichTotal` + OK/Cancel, the same form as the design report);
  `answer_dialogs` records its text in `TREATY_SEEN` before pressing OK.
- `applydiplo` applies a treaty at once (exact deltas), but the joined player gets the treaty dialog and the new
  values only with the next processed month, and that month's merge replaces `[Nation1]` (prestige, base resources,
  ship list) with the client's planning copy. So treaties that change nation 1 are applied with `applydiplo` only
  after the scenario's last processed month; the treaty that must reach the client goes through a real month (the
  host applies it after the merge, as for players).
- The game's `DoPeace` (the host's last war ends) shows "With the war ending without any major battle, Navy prestige
  takes a dent!" and takes 1 Prestige from the host: a host treaty without battles is +2 - 1. It also sets the
  Prestige (+0x198) of nations 1-8 to 0 (its nation loop: `if idx > 0: GetBNation(idx).Prestige = 0`); since 0.2.5
  the mod's DoPeace hook puts the players' (and reserved nations') values back and logs `end of war: prestige of
  player nation N kept at P (the game set X)` (also seen in `diplomacy`'s host peace). `TMessageForm` texts are a
  TLabel (no window text): count such boxes, don't read them.
- `TBuilderNation.PrestigeWarLoss(Big)` is not a per-nation routine: it first runs
  `form.ProportionalPrestigeLoss(10)`, which acts on `[form+0x3204]` = PlayerNation (the host's nation: -1, or
  -Prestige/10 from 20 up), then `Self.Prestige -= 1` (Big only; once more above 20). The mod applies that formula
  to the loser itself (`treaty_loss` in the runner); compare the host's Prestige around every treaty (the first
  0.2.5 build cost the host 20 -> 18 -> 17 in treaties it was not a party to).
- The game's own AI peace (`aipeace`, HandleAIPeace) for a joined nation's war with an AI nation can cede that
  nation's possessions and shows the host a "Peace treaty" dialog (in `diplomacy`: Corsica and Tunisia from
  France to Russia). So the stand-in's possessions at the start of `treaty` differ between runs with and without
  `diplomacy`: read `claimable`/`possessions` at run time, never hard-code them.
- The game's reparation text has no line break: `RemoveShipInPeace` concatenates `<ship> handed over to <winner>
  as war reparation` and ` She is renamed <new> in <adjective> service.` (literals at 0xc5b5d4/0xc5b65c), so it
  reads "... as war reparation She is renamed ...". Count a treaty's lines from its dialog, not as 1 per ship.
- Python output piped through PowerShell (`| Tee-Object`) garbles non-ASCII text such as ship names (`M?nchen`): set
  `PYTHONIOENCODING=utf-8` before reading such logs as evidence.
- `terms`/`warvp` on a pair that is not at war create a ledger entry (`war score a-b starts at ...`); a war started by
  the month-end step always restarts at 0:0, so read the score only after the war began.
- The war score of a war between players (`warvp`, `terms`) is the mod's own ledger (kept in `rtw3mp_session.ini`), not
  the nations' `VP` fields. A battle merged from a player's PC (`battle N applied: ... VP a/b`) adds to the `VP` fields;
  0.3.0 credited nothing to the ledger for it, since 0.3.1 the merge does (`war score a-b: +x/+y from a battle fought
  on a player's PC -> A:B`; `lockstep_pvp` f), MODLOG 2026-10-16). Compare `warvp` deltas
  with the month's merged VP and `war score a-b:` lines, never with the nations' VP. The ledger of a pair at war is
  created as soon as the host's diplomacy snapshot (`HostDipSnap`: the lobby broadcast, the Diplomacy list) sees the
  war, e.g. right after `battles injectpvp` (`war score 1-2 starts at 0:0`), so a month normally finds it there.
  `treaty` k) and `victory_points` g) count only lines in the hook format (`war score a-b: +x/+y -> A:B`): there the
  stand-in Bot is not connected, so a 1-2 battle is resolved by the host's game, never merged from a player's PC.
- `session`'s version check reads `src/mod/common.h`: a frozen older build tested with `--dist` (e.g. the released
  0.3.0 while the tree already says 0.3.1) fails that one check. Expected; report it as such.
- The Diplomacy list (1081) and other list views of the game copy are read and selected with cross-process
  LVM_GETITEM/LVM_GETITEMTEXT/LVM_SETITEMSTATE: the LVITEM goes into a scratch buffer allocated in the game copy
  (`RemoteBuf`, 32-bit LVITEM layout, 60 bytes, lParam at 32). LVM_SETITEMSTATE notifies the window like a click
  (the button captions follow the selection). The claim window `RTW3MP_Claim` is top-level, owned by the
  Multiplayer window; its buttons (1093 Claim selected, 1094 Let the conference choose, 1095 Cancel) take a posted
  `WM_COMMAND` with BN_CLICKED and the button handle.
- Chat checks compare counts (`new_chat_missing`), not offsets: the chat log drops its oldest lines past 300.
- `events` finds diplomacy's war month in run order (`months_through(names, 'diplomacy')`), so scenarios after
  `diplomacy` may process months too. Without `diplomacy` its random path takes the last processed month, and
  `battles` (nation 1 at war with an AI nation), `victory_points` and `treaty` process theirs with nation 1 at war:
  select `events` together with them only when `diplomacy` is selected too.
- Victory points (0.2.6): `battlevp` runs synchronously on the host's main thread (the reply comes after the game's
  HandleBattleAftermath), so call it from a thread and answer the host's dialogs meanwhile (`battlevp` helper; none
  seen so far). The game log-adjusts the raw values, independently of the current score: 1000 -> 129, 3000 -> 291,
  2000 -> 214, 500 -> 80, 300 -> 58.
- Don't leave a `battlevp` at peace behind: it gives the host VP and EnemyVP outside a war, the next host war's start
  did not reset them (host VP 138; WarStartsWithNation resets only while the war counter is not positive), and the
  game's DoPeace at its end then skipped the "war ending without any major battle" dent, so `treaty` i) saw host
  Prestige +2 instead of +1. `victory_points` fights its battles inside a host war that DoPeace ends and removes its
  EnemyVP marker (`enemyvpadd -E`) at the end.
- The host's `seen` is set first thing in the month-end step (ShareEnemyVP), so `vpinfo` seen reaching the expected
  EnemyVP marks a finished sharing `applydiplo` (`apply_vp`). Since the final 0.2.6 build `seen` == EnemyVP after
  every month-end step and after every DoPeace (the hook shares before the game's DoPeace and re-syncs after it); the
  first build kept a stale `seen` when one step ended the host's last war and started a new one. DoPeace resets only
  the host's VP and EnemyVP; the VP of nations 1-8 stay (unlike their Prestige). HandleAIPeace does zero all of 1-8
  (`the game set 0` in the mod's `AI peace: VP of player nation K kept at P` line).
- `ShareEnemyVP` splits the month's rest among the host's enemies found by `rel 0 j` (AI enemies take a share that
  nobody is credited with): compute the expectation from `wars_of(0)` and `vp_shares`, not from a fixed 101 = 51 + 50.
- Copy game dialogs (0.2.7): the game's overwrite prompt is a `TMessageForm` "Confirm" with **OK / Cancel** (OK =
  overwrite), "Saved game copied to slot N" a `TMessageForm` "Rule the Waves III" [OK]; their texts are TLabels
  (unreadable). The mod's box is a Win32 `#32770` "Multiplayer" [OK]; its text is readable from its `Static` child
  with WM_GETTEXT. Never let `dismiss_dialogs` near a copy: it would press OK on the overwrite prompt. Use
  `copy_dialogs(pid, confirm, done)`, which answers only those boxes (Cancel when `confirm` is False) and records them.
- The "Copy game" button (`btbnSaveToOtherSlot`, a TBitBtn, BM_CLICK works) opens `TdlgInputNumber`: `seNumber`
  (TSpinEdit, set with WM_SETTEXT and read back), OK, Cancel. At run time the game sets it to 1 with the range 1-9 (the
  form resource says 4-20): `81` was clamped to 9 and created Game9, and OK pressed blindly copies onto slot 1, behind
  the "Confirm" prompt (on the test PC that is the host's slot). The runner picks the highest slot of 3-9 that is free
  before the run for the button test and deletes it afterwards; slots 78-80 (bridge copies) must not exist at start.
- In single player the game does not write the slot when a month ends (the host's session saves it itself): a
  single-player month is over when the game's turn report (`TdlgCampTurnRep` "Turn messages") was answered and the
  strategic screen is idle; then `save` the throwaway slot and read its `[General]` Year/Month
  (`single_player_month`). Its first dialogs are one or two `TMessageForm` "Confirm" boxes (Yes). Don't poll the
  bridge while the month runs. Germany's copy got its own `TdlgEventAnswer` events (as nation 0).
- Research screen (0.2.8, form resource `TFRMRESEARCH`): `lvResearch` (report view: Research area, Last research,
  Priority, Levels; PopupMenu High/Medium/Low/-/Details), `seResearchBudget` (TSpinEdit, MaxValue 12), `tcResearch`
  (tabs General / Naval guns), `Button1` "OK" (mrOK), `bnMedium`/`bnLow` = "All medium"/"All low" (they set EVERY
  area, not the selected one). One area changes only through the right-click menu: select the row (LVM_SETITEMSTATE),
  post `WM_CONTEXTMENU` (wParam = the list, lParam -1, the keyboard form) to the list, wait for the `#32768` window,
  then post WM_KEYDOWN VK_DOWN/VK_UP to it until `GetMenuState(MN_GETHMENU, i, MF_BYPOSITION) & MF_HILITE` marks the
  item, and VK_RETURN (`popup_priority`; the menu strings read "&High", "&Medium", "&Low"). The item's OnClick changes
  `CurrentResearchingNation` at once (bridge `research` shows it while the dialog is still open: sync commands work
  during the modal dialog). The budget: WM_SETTEXT on the TSpinEdit, read back with WM_GETTEXT; OK (BM_CLICK) writes
  it, WM_CLOSE does not. More than 12 + OK: a `TMessageForm` "Rule the Waves III" [OK] (the text "Research budget
  cannot be more than 12% of total budget!" is a TLabel, unreadable), the dialog stays open and nothing is written.
  Never `dismiss_dialogs` a copy while the dialog is open (it would answer that box). The TSpinEdit clamps a value
  above 12 when the dialog loads it: nation 1's fixture value 13 (set while it was AI) and `merge`'s `nfield` 14 show
  as "12" (OK would write 12), so `research` sets 10 with `nfield` before comparing.
- The Priority column's row i is NOT area i: the list shows only the areas the era offers (1900: 0-10, 12, 13; 11 "Anti
  submarine warfare" not yet) plus a "Naval guns" row (the calibers, not in the area table). Map rows by name through
  the game's `Data\ResearchAreas3.dat` headers (`[Explosive shells 12]`; strip " (Adv)", the nation's advantage mark),
  as `research_view`/`shown_prio` do; Low/Medium/High = 1/2/3.
- `research`'s after-month checks can fail at random (seen once, in the v0.3.0 --stress run; the default run and a
  rerun passed: nation 1 ResearchPct 7 kept, every Prio back to 2). The game's `TfrmBuildCamp.DoResearch(aNationIdx)` (0xc9225c) has an AI branch for every
  nation index > 0 (`cmp [aNationIdx], 0; jle`), so also for joined nations: with Random(20) == 1 (5 % a month) it
  re-rolls every area's Prio (+0x326 of the 0x35-byte records: 80 % Medium, else Random(3)+1, code 0xc92356-0xc923b4).
  Up to a1a8484 the mod hooked only DetermineAINationResearch (a mod issue, MODLOG); since ebfca72 its DoResearch hook
  puts a joined nation's priorities back (`kept` line), and `research` e) forces the case with `researchrun 1 100`. A
  failure of these month checks is a mod regression again.
- Joined players' battles (driving them: `BattleDriver`/`battle_dialogs` in the runner; never `dismiss_dialogs` while a
  battle dialog is up: it presses the first button, which is "Decline" on the preview):
  - The client's dialogs, in order: `TdlgMapMessage` "Battle" (Accept/Decline; Accept, then the game may still let the
    AI avoid the battle: a `TMessageForm` "Rule the Waves III" [OK] and `StartPlayerBattle ... -> none`, outcome
    `nobattle`, about 2 of 3 injected battles against Spain in the fixture), `TdlgBattlePreview` "Battle preview"
    (bnAccept/bnDecline TButtons, BM_CLICK works; **Decline is sometimes disabled** (the game's DeclineAllowed: seen for
    battles the game found with the AI nation attacking and for an injected one): then it must be accepted and fought;
    the driver switches such a battle to 'fight'), a `TMessageForm` [OK],
    the battle screen `TfrmMain` (non-modal), the results `TdlgResults` "Scenario results - ..." (buttons "View map"
    mrOK, "Ship details", "Air details", **"Exit battle"** mrYes: press Exit battle), one more `TMessageForm` [OK]
    (HandleBattleAftermath has run by then: `battles state` aftermath=1), then the client sends the result and loads
    slot 77. The "Add to history record" InputQuery (a form with a TEdit and OK/Cancel) did not appear in the runs
    with auto-run; answer it Cancel if it does.
  - Auto-run: `sbAutoRunBattle` is a TSpeedButton (no window) at Left 1218, Top 5 (28x28) on `TfrmMain.Panel1` (the
    top TPanel, 39 px high): post WM_MOUSEMOVE/WM_LBUTTONDOWN/WM_LBUTTONUP at (1232, 19) to Panel1 (`click_autorun`).
    Its prompt is a `TdlgEventAnswer` (radios "Yes" / "Yes, and order all forces to move to closest friendly base" /
    "No" + OK): select "Yes" by caption. Both sides then run on the AI at the fastest speed; a few Yes/No
    `TMessageForm`s still come ("end the scenario?": Yes) and a run can stop at a minute boundary (`pocrun 100000`
    restarts it). A Spain-Germany battle in Northern Europe took 35-55 s of real time. Fallback without the prompt:
    `pocsideai 0 1 0`, `pocsideai 1 1 0`, `pocspeed 5`, `pocrun 100000`.
  - The host's bridge may be polled once the host log says `battle phase:` (the month is over; the strategic screen is
    locked and the host idles), not before. The merged save (`battle N applied`) is on disk only until the host loads
    it and saves the new month: read it at once (`BattleDriver.s1`); the battle slot's save right after the client's
    `battle N: fought` (the next battle overwrites Game87).
  - With nation 1 at war with an AI nation that has ships in the same area, the game finds battles by itself almost
    every month (`battle request N: nation 7 vs 1 ... fought by Captain`); they come after the injected ones in the
    battle phase and are declined (or fought when Decline is disabled). The game's resolution of those after the merge
    changes VP and ships again: check the merge on the merged save, not only after the month.
  - Up to the build tested on 2026-10-15 (developed after 0.2.8), a preview Decline was reported as `nobattle` ("No
    battle came about"), not `declined` (the game's CheckAttackLocation returns False after a declined preview),
    and the `battles` check "reported as declined" failed. Correction 2026-10-16: since 0.3.0 the client counts the
    game's battle setups (the `DoBattle` hook), so a declined preview is reported as `declined` and the check
    passes (`battles` 70/70 in the full default run of ebfca72).
  - The game's AI peace can end the injected war at a month's end (no log line; the host's `victory points: nation 1
    is at peace; V -> 0` follows): `battles` sets the war again before every month (`settension 1 J 50`).
  - Battles deferred to the battle phase run after DoEndOfTurn: a battle of a war the month-end ended is still sent
    and resolved (vanilla would have fought it before the peace).
  - Rejoin (`leave`, `fightbattles 0`, `join Captain ...`): the host gives a returning player its reserved nation back
    and sends the month again (host `state seq N -> Captain (nation 1, ...`, client `LoadCampaign slot 77 player 1`,
    `event: this month was already rolled`); wait for the host's `players=1` first, and for those lines and
    `myNation=1` after the join (the old slot 77 still shows playerIdx 1 before: a `status` check alone races).
  - After a time limit the host sends the next battle while the client is still busy with the old one (a held
    preview whose Decline is disabled becomes a fight; its result is not sent: `battle N: finished after the host
    resolved it; result not sent`), and battles sent meanwhile time out while pending. The driver attributes the
    client's dialogs to the battle the host had sent last when the client logged its latest `player copy in slot 87`
    (`BattleDriver.current`), not simply to the last one sent.
  - A "decline" candidate whose Decline is disabled is fought, and under `battles timeout 20` it outlives the time limit
    (the client logs "The host resolved this battle without you (time limit)" for it). The held battle that follows must
    not take that notice as its own: the driver releases a held preview only on a notice that came after the held
    battle became the current one (`notices0`), and d) reads only notices logged after "battle N sent" (fixed after
    ebfca72's targeted run, where the held battle 10 was released at once and declined: 2 failing d) checks).
  - Client private memory grows by ~2.5-3 MB per campaign load (two per battle: slot 87, then slot 77 again); a month
    with 8 battles added ~43 MB (full run: 83 -> 160 MB over the four battle months). Reported, not asserted.
- A host event whose answer names a nation (`TdlgEventAnswer` with `cbSelectNation` shown, "... negotiating an
  agreement with") loops forever ("Rule the Waves III" box, event again) if OK is pressed without a nation:
  `answer_dialogs` now picks the first nation first (`select_event_nation`), on both copies. Likewise the game's
  "Select Officer" (`TdlgSelectOfficer`: `lvOfficers` list view + OK/Cancel; OK without a selection = the same loop):
  `select_officer` selects the first officer (LVM_SETITEMSTATE through `lv_select`) before OK. Both are random host
  events (seen in Jan 1901 and in the `merge` month) that earlier runs never met.
- `player_copy` leaves the session (`leave`) for its last part; the lockstep scenarios after it join again
  (`ls_ensure_session`: `ls enable 1`, `fightbattles 1`, `join Captain`; the host gives nation 1 back).
- Live battles (lockstep) in the suite (`lockstep*` scenarios; helpers `LsAnswerer`, `ls_month`, `ls_drive_live`):
  - Dialogs are answered by a background thread (`LsAnswerer`, `ls_ans(ctx)`), paused (`ls_quiet`) whenever `run_month`,
    `apply_diplo` or `wait` answer with `answer_dialogs`; two answerers at once double-click. It accepts previews and map
    messages, says "Yes" to the auto-run prompt only for copies in `autorun`, presses "Exit battle" on results and the
    ended battle screen's "View results" (a TButton on TfrmMain) only for copies in `view_results`.
  - **Never press "View results" in the same pass as "Exit battle"** (or while any dialog is up): the posted click waits
    in the queue of the disabled battle screen and runs after its close, the results come again, "Exit battle" closes
    it a second time and FormClose runs DoStrategicTurn twice: the host's month advanced Aug -> Oct (two DoEndOfTurn,
    "doctrine/design studies: already advanced this month"). A player can't do this (the screen is disabled behind the
    modal results); the answerer now waits 10 s after "Exit battle", needs the screen enabled and no dialog, and stops
    once the copy's log shows the close (`LS_CLOSE_RE`). After the game's own end (or auto-run) one "Exit battle" closes
    the battle; after an agreed end the screens stay open with "View results".
  - Never poll the host's bridge while its month runs, also not "just" `ls status`: `ls_month` polls the clients
    (`ls_detect`: B = a client running a battle as B) and only then A; the host's own battle screen is classified from
    its log (`host_live`: "battle held at its start" vs "live battle ended"/"fought against the AI"), not its bridge.
  - Test battles: `pocbattle 1 0` puts the host's battle into battle slot mode only when it starts live (testSlotMode). A
    host battle that is NOT live outside a month (the opt-out) would run DoStrategicTurn at its close (a month outside
    the session): test the opt-outs (`fightbattles 0`, `RTW3MP_LIVE_BATTLES=0`) in real months only. A new `pocbattle`
    frees the previous battle screen without FormClose (that is how several test battles follow each other).
  - The host's battle against nation 1 in a real month is random (CheckForBattles2): 1-2 months per battle in the runs
    so far; the scenarios allow 6-8 months. Each such month is a `ls_month` (never `run_month`: its `answer_dialogs`
    would press the preview's first button, Decline).
  - Every division goes to the AI (`ls order i -1 -1 1`, from each side's own copy) for contact; an order of one side's
    division from the other copy would be a local edit, not an order. The game sets the speed back after a break: a long
    run drifts to speed 1 (~1.3 battle minutes per second); 400-800 minutes take 5-9 min.
  - "No contact between fleets. Do you want to continue searching?" is a game proposal (`ls status` proposal=1): mode
    'game' accepts it (both `ls end`), mode 'agree' plays on to minute 410. The agreed end before 400 minutes / half the
    battle is refused by the game on both copies ("The battle can't end yet").
  - A reconnecting player (after a kill) while the host's battle screen is open: on c06860f it got no nation (the
    host's `CampaignLoaded()` is false while the strategic screen is hidden) and `claim 1` by index did nothing (its lobby
    list is empty). Since 0.3.0 the hello matches the reservation during a session (`myNation` 1 at once, chat "* Captain
    joined as Germany"; in a test battle (phase planning) the host also saves and sends the month right away: `state seq
    N -> Captain (nation 1, ...` while its battle screen is still open), and the host's Tick (`RematchReservations`) gives
    it back to a connected player without a nation (`reservation: Captain gets nation 1 back`, "* Captain is back as
    Germany"). The runner still claims by name (`nation1()`) as a fallback and fails the check if it had to.
  - When the game ends a live battle by itself inside a minute (e.g. "No opposing ships are in sight ... scenario length
    is reached!", or `ls endsoon`), the results' "Exit battle" comes inside that minute's simulation step. Since 0.3.0
    (a1a8484) the close is deferred: both copies log `"Exit battle" in the results: the battle screen closes after this
    minute (t=..)`, then the normal end at the boundary ("The battle is over (...)", "live battle ended (normal)"), then
    `closing the battle screen ("Exit battle" in the results)`; FormClose -> DoStrategicTurn -> DoEndOfTurn run from the
    outermost message loop with the game's own dialogs (answered by the answerer: picture messages, "Trade warfare
    summary", "Turn messages"). An agreed end can take the same path (seen in an `agree` battle). On c06860f the host's
    month ran nested in the simulation step (minute-box answers, "Battle: ..." chat lines, "WARNING a network message
    handled inside a simulation step", host "live battle ended (abandoned): the battle screen was closed"); the
    deferred-close check fails on any such line between the close and "DoEndOfTurn finished". Battle slot mode (test
    battles) is not affected (FormClose returns at once).
  - The game's own resolution of an idea-1 battle the player did not fight (declined, "no battle came about", timed out:
    `battle N resolved by the game: battle`, after the last merge) can sink a ship of the joined nation: Fate "Sunk in
    combat with Spanish forces.", [Nation1Losses] DDLost +1, but no battle star and no "took part" log entry (seen twice:
    DD V2). Sunk ships stay in the ship list with a Fate other than "XXX" (idea-1 merges always log `0 removed`). Check
    losses per step (`StageWatcher` reads the host's save at `battle phase:`, after each `battle N applied` and Game87
    after each `battle N: fought` within 0.1 s: the next battle rewrites Game87 ~0.3 s after "fought"), never across the
    whole month, and count a ship as lost by its Fate, not by "took part".
  - `ls abort` (B leaves silently) does not tell A: A drops B after its 30-s lost timeout.
  - The client killed during a battle: its slot 77 is the one canLoad saved (or the submitted month); `Game87` keeps the
    battle files (deleted after the run).
  - The leak check (`ls leakcheck 1`) is a per-process switch: the runner turns it on in `ls_ensure_session` (host and
    client, every lockstep scenario starts with it), for a copy started again (`ls_relaunch`) and for the third copy
    (`k2_start`); `ls_log_scan` asserts no `lockstep: LEAK` line (two checks per scan: errors/hooks, leak). Lines a test
    provokes on purpose go into `LS_EXCUSED` (only S7's guard-off control); `ls throw`'s exception code `e0545354` is
    ignored by the error scan. The guard's own `the display wrote N sighting fields ... (kept out of the simulation)`
    lines are normal (reported).
  - The guard's negative control (`ls guard 0` + `ls dispwrite`) must turn the guard back on as soon as the host logs the
    DESYNC: with the guard off the battle map's own GlobeRender writes the sighting fields every minute, so a guard
    restored only after the resync gave a second DESYNC (minute 31) and an unexcused LEAK in the first S7 run.
  - Every stress case must end with the client back in slot 77 (`ls_start_test` waits for it): a case that leaves its
    battle live makes the next one time out (S7 now drops the client at its end).
  - `ls order` to a division index that no longer exists (sunk, merged, the list renumbered) answers `error division`:
    the spam re-reads the list and does not count it as an error.
  - The agreed end is refused with "Enemy ships are near or airstrikes are in the air" as long as the fleets are in
    contact, and the game's own end ("No opposing ships are in sight ... scenario length") does not come either: a test
    battle without AI orders ran to minute 1200 that way. Battle breaks ("... loses contact with formation!") stop each
    run after one minute, so after a refused end the driver runs 30 minutes with `ls_run_total` (not one `ls run 30`),
    up to 20 end requests; f) puts every division on the AI (`ls_all_ai`), as the real-month battles do.
  - After a test battle (battle slot mode outside a month) slot 1 holds the live battle's save (`ScenarioInProgress=1`,
    reported by the after-checks) until the next processed month saves it; loading slot 1 in between would resume that
    battle. The host-kill case restores slot 1 from the run's backup before starting the host again.
  - A host battle in which nation 1 scores no VP logs no `victory points: nation 1 +X in its battle against the host`
    line; EnemyVP of the month (trade war) still reaches nation 1 through the month-end share line. Not a failure.
  - Memory: every campaign load costs a copy ~3-4 MB private memory (game level); a live battle loads once at its start
    and once per resync on both copies, so S7 (about 7 resyncs) adds ~25-32 MB and nine battles in a row 91/103 -> 179/203
    MB (host/client; 156/227 -> 258/344 MB later in the full `--stress` run). g) asserts the average growth per battle
    (<= 25 MB), it does not level off.

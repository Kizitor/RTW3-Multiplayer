# Testing

How RTW3 Multiplayer is tested. The regression suite (`tools/regression.py`) starts two or three real copies of the
game on one PC and drives them only through the mod's test bridge and posted window messages; it backs up and
restores the save slots it uses. The regression agent (`.github/agents/rtw3-regression.agent.md`) runs it after every
change. This file is the suite's reference: how to run it, the environment, the tools, the runner's helpers, the
scenario catalog, the bridge commands and the traps found so far. How the mod works: [ARCHITECTURE.md](ARCHITECTURE.md);
game facts: [GAME-INTERNALS.md](GAME-INTERNALS.md); test results over time: the "Testing" entries in
[MODLOG.md](../MODLOG.md).

Short hashes in this file (e674bf5, c06860f, a1a8484, ebfca72) name development builds of 0.3.0. They were squashed
into the release commit f6ee827 and are not in the published history. The other short hashes (d99e357, 919b40d,
6da8bfe, a5861e7, c9b74b9, ...) are development commits of 0.3.2, 0.3.3 and 0.4.0 and may be missing from the
published history too. The live-battle limits branch was released as 0.4.0: its engine levels 2 to 13 were
development levels, and its MODLOG entries and commits carry v0.4.0, v0.5.0 or v0.6.0 from the plan of 2026-10-16
(there is no 0.5.0 or 0.6.0).

## Contents
- [Before you start](#before-you-start)
- [Running the regression suite](#running-the-regression-suite)
- [Environment variables](#environment-variables)
- [Tools inventory](#tools-inventory)
- [The runner in detail](#the-runner-in-detail)
- [Scenario catalog](#scenario-catalog)
- [The 1935 test campaign](#the-1935-test-campaign)
- [The 1958 missile test campaign](#the-1958-missile-test-campaign)
- [Mixed versions](#mixed-versions)
- [Two-PC conditions](#two-pc-conditions)
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
  - Optional: save slot 9 holds the 1935 test campaign for live battles with aircraft (`lockstep_air`, `air_units`
    and `lockstep_strike`, which are skipped without it). See [The 1935 test campaign](#the-1935-test-campaign).
  - Optional: the 1958 missile test campaign for `lockstep_missiles` (skipped without it), given to the run with
    `--missile-fixture <folder>\Game48` (or `--missile-slot`). See
    [The 1958 missile test campaign](#the-1958-missile-test-campaign).
  - `lockstep_torpedo` needs the battle preference Realism above its second level: the runner sets
    `RealismSetting=2` in the game's Preferences for a run that includes it, and puts the file back afterwards
    (verified by hash). `twopc` writes the second player's own Preferences into the file from that player's restart to
    the scenario's end (the per-copy Preferences of [Two-PC conditions](#two-pc-conditions)); the runner backs the file
    up for a run that includes it and puts it back (verified by hash). Other runs leave the Preferences alone.

## Running the regression suite
From the repository root:

```
python tools\regression.py                           the default run: every scenario but the --stress ones
python tools\regression.py --list                    the scenarios, their months, game time, groups and what each guards
python tools\regression.py --dry-run [options]       what a run with these options would do; starts and writes nothing
python tools\regression.py --only research,doctrine  some scenarios (session is always added)
python tools\regression.py --group rejoin,air        the scenarios of some groups (session is always added)
python tools\regression.py --stress                  the long live-battle variants and the --stress scenarios
python tools\regression.py --dist <folder>           compare the installed DLLs with that build
python tools\regression.py --group missiles --missile-fixture <folder>\Game48   the 1958 missile test campaign
python tools\regression_selftest.py [--logs <folder>] the offline self-test of the suite's parsers and runner (no game)
```

| Option | Effect |
|---|---|
| `--list` | Prints the scenarios (months, estimated game time in seconds, the default run or `--stress`, the groups) and the groups, and exits. |
| `--dry-run` | Prints what the run would do and exits: the scenarios in order with their game time and the total, the processed months, slot 1, the 1935 campaign, the missile-era fixture, the Preferences' RealismSetting and what the run would do with it, the slots it writes and deletes, and every preflight problem (exit 2 when the run could not start now). Starts, writes and deletes nothing (a leftover backup equal to its slot is only reported). |
| `--only a,b,...` | Runs only those scenarios, in catalog order; `session` is always added because the others build on it. |
| `--group g,...` | Runs the scenarios of these groups (with `--only`: both), in catalog order, plus `session`: `limits` (`lockstep_ui`, `lockstep_limits`, `lockstep_air`), `rejoin` (`lockstep_rejoin`, `lockstep_resume`), `air` (`air_units`, `lockstep_air`, `lockstep_strike`), `airunits` (`air_units`), `torpedo` (`lockstep_torpedo`), `missiles` (`lockstep_missiles`), `live-limits` (every live-battle-limits scenario: `lockstep_ui`, `lockstep_limits`, `lockstep_rejoin`, `lockstep_resume`, `lockstep_torpedo`, `lockstep_air`, `lockstep_strike`, `lockstep_missiles`), `twopc` (`twopc`: live battles under two-PC conditions). |
| `--stress` | Adds the `--stress` scenarios (`lockstep_stress`, `lockstep_rejoin`, `lockstep_resume`, `lockstep_torpedo`, `lockstep_strike`, `lockstep_missiles`) and switches `lockstep_pvp`, `lockstep_month` and `twopc` to their long variants. |
| `--missile-fixture <GameN>` | `lockstep_missiles`: a missile-era campaign folder (the 1958 missile test campaign: `fixture_missile\Game48`), copied into the free save slot N for the run and deleted afterwards (refused when slot N holds other files). Env `RTW3_MISSILE_FIXTURE`. |
| `--missile-slot <n>` | `lockstep_missiles`: a save slot that holds the missile-era campaign, used as it is (backed up and put back). Env `RTW3_MISSILE_SLOT`. |
| `--missile-nations <h>,<k>` | The host's nation and the opponent the client claims (the save's nation indices; default `0,3`: Italy hosts, the client claims the Soviet Union). Env `RTW3_MISSILE_NATIONS`. |
| `--missile-mission <m>` | The mission of `pocbattle <k> <m> [area]` (default 1: the Cruiser battle). Env `RTW3_MISSILE_MISSION`. |
| `--missile-area <i>` | The map area index of `pocbattle` (default 1 with the default nations: The Mediterranean, which the two nations need since they share no home area; `-` for none). Env `RTW3_MISSILE_AREA`. |
| `--dist <folder>` | The build the installed DLLs are compared with (default: env `RTW3_DIST`, else the repository's `dist\`). Use a frozen copy of the build under test when another agent may rebuild `dist\` during the run. |

- Game time: the default run took 1528 s with the game open on the 0.4.0 release candidate dist_ll_16 (2026-10-18;
  `lockstep_ui` 213 s, `lockstep_limits` 175 s, `lockstep_pvp` 114 s, `lockstep_month` 107 s, `air_units` 61 s,
  `lockstep_air` 264 s; the 0.3.2-era default run took 1015 s on dist_ll_7). The `--stress` scenarios on dist_ll_16:
  `lockstep_torpedo` 465 s, `lockstep_strike` ~400 s (plus ~60 s for each night battle it replaces), `lockstep_missiles`
  ~330 s, `lockstep_rejoin` 563 s, `lockstep_resume` 378 s. On the second release candidate dist_vp_2 (2026-10-18):
  the default run 1464 s, a whole `--stress` run (all 26 scenarios, the long variants) 5385 s, about 90 minutes.
  `twopc` (0.4.1, dist_tp_1, 2026-10-20): 175 s short (the default run), 201 s full (`--stress`); `--group twopc` 192 s
  and 219 s with the game open. The 0.5.0 release candidate dist_rc050_1 (2026-10-22): the default run 1723 s, the
  whole `--stress` run 5287 s; its second candidate dist_rc050_2 (2026-10-23): the default run 1779 s, `--stress --only
  diplomacy,victory_points` 160-163 s, `--stress --only session,events,battles,lockstep_month,lockstep_pvp,
  relations_peace` 898 s. The 0.6.0 release candidate dist_rc060_1 (2026-10-23): the default run 2140 s, the whole
  `--stress` run 5943 s, `--group relations` 478 and 477 s; its second candidate dist_rc060_2: the default run 2083 s,
  the whole `--stress` run 6778 s, `--group relations` 497 and 663 s, `--stress --only lockstep_month` 398 s; 0.6.1's
  `relations_war` 37 s (dist_fw_1); `lockstep_data` 13 s and `--only session,relations_war,lockstep_torpedo,
  lockstep_ui,lockstep_pvp,twopc,lockstep_data` 828 s (dist_i61_1); `divisions` (dist_fd_2, 2026-10-25): 90 s, and
  `--only session,battles,lockstep_month,divisions` 486 s. The 0.6.1 release candidate dist_rc061_1 (2026-10-26): the
  default run 2456 s, the whole `--stress` run 6055 s, `--only relations_war,player_copy` 106 s, `--group relations`
  473 and 646 s, `--stress --only lockstep_month` 294 s; its second candidate dist_rc061_2: the default run 2112 s,
  `--stress --only lockstep_month` 464 s, `--group relations` 520 s, `--only lockstep_limits,lockstep_data` 206 s.
  `--list` estimates the default run at about 41 minutes
  (31 processed months) and `--stress` at about 112 minutes; the groups:
  `rejoin` ~16 min, `air` ~13 min, `torpedo` ~8 min, `missiles` ~6 min,
  `twopc` ~3 min (plus `session` each). `--list` and `--dry-run` add up the estimates per scenario (`EST`, `EST_STRESS`
  in the runner: measured).
- Exit code: 0 = every check passed and the saves were restored; 1 = a check failed, or the saves were not restored,
  or a started copy, an event cooldown file, a copy slot or a battle slot was left behind; 2 = not started (a
  precondition failed).
- While a run lasts, slot 1's backup is in the Windows temp folder (`rtw3mp_regression_backup_Game1`), with
  `lockstep_air`, `air_units` or `lockstep_strike` slot 9's (`rtw3mp_regression_backup_Game9`), with `--missile-slot
  <n>` slot n's (`rtw3mp_regression_backup_Game<n>`), with `lockstep_torpedo` (RealismSetting=2) or `lockstep_strike`
  (FloatplaneLaunch=0, Automatic: with "Ask before launch" no search floatplane flies in a live battle) the Preferences
  (`rtw3mp_regression_backup_Preferences`; with `twopc` too: it writes the second player's variant into the file during
  its scenario) and with `lockstep_resume` its copy of slot 1
  (`rtw3mp_regression_snapshot_Game1`). `--missile-fixture <folder>\Game48` puts the campaign into slot 48 and
  `air_units` its bloated copy of the 1935 campaign into slot 49; both are deleted after the run (the preflight refuses
  to start when such a slot already holds other files). The details of
  what the runner checks before, during and after a run are in [The runner in detail](#the-runner-in-detail).
- `tools/regression_selftest.py` checks the suite offline: its parsers and log patterns against the bridge's reply
  formats and log lines of the live proofs, `--list`, `--dry-run` and the fixture and Preferences helpers in a sandbox
  folder it makes in the current folder and deletes; with `--logs <folder>` also a report over a recorded run's mod logs
  and trace files. It never starts the game or touches the saves, logs or Preferences. Run it after every change of the
  runner or a parser.

## Environment variables
Set by the runner when it launches a copy (see "Environment" in the regression agent file): `SteamAppId` and
`SteamGameId` (2008100: a direct start without the relaunch through Steam), `RTW3MP_BRIDGE_PORT` (47701 host, 47702
client, 47703 the third copy), `RTW3MP_LOG_TAG`, and for clients `RTW3MP_CLIENT_SLOT` and `RTW3MP_EVENT_CHANCE=0`.

| Variable | Read by | Effect |
|---|---|---|
| `RTW3_DIR` | the tools (`rtw3ctl.py`) | The game folder (default in `tools/rtw3ctl.py`). |
| `RTW3_DIST` | `regression.py` | Default of `--dist`. |
| `RTW3_LS_X87SOFT` | `regression.py` (`lockstep_strike`) | Diagnosis only: `0` or `1` is sent to A as `ls x87 soft <n>` before each strike battle (the live battle's maths: `0` this CPU's x87, `1` the software routines of engine level 16), and `ls x87 selftest` is logged. Used on dist_rc041_1 to compare the strike battles with and without the software maths (MODLOG, Testing 2026-10-21). |
| `RTW3_LS_SHIPTYPES` | `regression.py` (`lockstep_strike`) | Diagnosis only: `<A's flags>,<B's flags>[,nosync][,nohash]` sets both copies' ship-type flags before each strike battle (`ls shiptypes`, engine level 17; the options go to both copies). `010101,000000,nosync,nohash` reproduces the 0.4.1 RC's identification DESYNC; `010101,000000` shows the fix (B takes A's); with `nosync` alone the loads differ (READY). MODLOG, Fix 2026-10-22. |
| `RTW3_MISSILE_FIXTURE`, `RTW3_MISSILE_SLOT`, `RTW3_MISSILE_NATIONS`, `RTW3_MISSILE_MISSION`, `RTW3_MISSILE_AREA` | `regression.py` | Defaults of the `--missile-*` options. |
| `RTW3_SHOTS` | `regression.py` (`twopc`) | A folder for PrintWindow captures (PNG) of both copies' battle windows at each of `twopc`'s map checks: evidence only, the game's own windows (safety rule 2). Not set: no captures. |
| `PYTHONIOENCODING=utf-8` | Python | Set it before reading logs through PowerShell pipes (see [Known traps](#known-traps)). |
| `RTW3MP_BRIDGE_PORT` | the mod | Enables the test bridge on 127.0.0.1 at that port. |
| `RTW3MP_BRIDGE_TOKEN` | the mod | If set, the first line sent to the bridge must be this token. |
| `RTW3MP_LOG_TAG` | the mod | Log file `rtw3mp_<tag>.log` instead of `rtw3mp.log`. |
| `RTW3MP_CLIENT_SLOT` | the mod | A joined player's save slot (default 77; 50-999). |
| `RTW3MP_BATTLE_SLOT` | the mod | A joined player's battle slot (default: client slot + 10; 50-999, not the client slot). |
| `RTW3MP_EVENT_CHANCE` | the mod | Chance in % of a joined player's monthly random event (default 33; 0-100). |
| `RTW3MP_BATTLE_TIMEOUT` | the mod | Seconds a player has for a battle of the battle phase (default 1800; 0 = no limit). |
| `RTW3MP_LIVE_BATTLES` | the mod | `0` turns live battles off on that PC. |
| `RTW3MP_TEST_LS_LEVEL` | the mod (`lockstep_rejoin` c), `lockstep_resume` g)) | Test only: this copy announces that live-battle engine level (hello `ls=`, the start's, ready's and rejoin's `lv=`) while its own checks keep `MP_LS_LEVEL`, so one build plays a peer of another mod version: a client with it gets no live battles from a host of another level (`fought against the AI (live-battle engine level 9, the host's 10)`, relayed: `resolved by the game`), a host with it is refused by B (`<host>'s mod plays live battles at level 9, this PC's at level 10 (a live battle needs the same level on both PCs: the same mod version)`; the session's notices name both levels and, when they differ, both mod versions). Logged at startup (`TEST: RTW3MP_TEST_LS_LEVEL: ...`); `ls level` shows `announced=`. |
| `RTW3MP_DIAG` | the mod and the proxy | Logs exceptions; the proxy also writes `rtw3mp_loader.log` into the game folder. Since 0.6.0 the mod logs an access violation, integer overflow or division error in the game's code with its registers, the objects they point to and the game's return addresses on the stack (`diag: exception ... at rva ...`, `diag:   stack (return rvas ...)`), a Delphi exception with its class, and runs `pocheapcheck` before every save and after every load (`pocheapcheck before SaveBuildCampaign: objects=... strings=... bad=0`, with the last battle's area and possession when the game holds them). |
| `RTW3MP_TEST_KEEPBATTLEPTRS` | the mod | Test only: `1` lets every load keep the last battle's area and possession as the game does (`load: TEST: the last battle's area ... kept across the load`): the experiment of the 0.6.0 fix of the joined player's SaveBuildCampaign box; the per-month `battleptrs` check then fails after a battle. |
| `RTW3MP_TEST_PREFS` | the mod ([Two-PC conditions](#two-pc-conditions)) | Test only: `Key=Value;Key=Value` with the Preferences file's keys (e.g. `RealismSetting=2;AIControl=0;FOW=0;DarkMode=1;Scale=1`): this copy's own preferences, written into the game's preferences record after every run of its Preferences reader (`TdlgPrefs.LoadPrefs`) and at the mod's start; the copy then never writes the shared file (its writer `SavePrefs` is skipped: `TEST: the game's Preferences writer skipped ...`). Logged `TEST: RTW3MP_TEST_PREFS (<when>): this copy's preferences in memory: ...`. To prove that nothing writes the file, give the copy its own file instead (per-copy Preferences in [Two-PC conditions](#two-pc-conditions)). |
| `RTW3MP_TEST_FPUCW` | the mod | Test only: this copy's x87 control word (hex, e.g. `007F`) at the mod's start (`TEST: RTW3MP_TEST_FPUCW: ...`). The game sets its own (0x27F) later, so on 1.01.44 it doesn't last until a battle; `ls testcwload` shows the word now. |
| `RTW3MP_TEST_LOCALE` | the mod | Test only: `comma` gives this copy a comma locale's separators in the game's `FormatSettings` at the mod's start: decimal `,`, thousands `.`, date `.`, list `;` (`TEST: RTW3MP_TEST_LOCALE=comma: ...`). The battle screen then shows "24.000 yds"; nothing the PCs exchange changes ([GAME-INTERNALS](GAME-INTERNALS.md), "Two PCs"). |
| `RTW3MP_TEST_X87` | the mod ([Two-PC conditions](#two-pc-conditions)) | Test only (engine level 16): `ext[:N]`, `dbl[:N]` or `ppm[:N]` makes this copy's own x87 transcendental results (FSIN, FCOS, FPATAN, ...) those of "another CPU": every N-th result (default 1) of the game's 11 routines is nudged by one unit in the last place of the 64-bit mantissa (`ext`, the size of a real Intel/AMD difference), of a double (`dbl`) or by about 1e-6 of its value (`ppm`); exact results (0, +-1) stay. Only the hardware path is nudged: a live battle with the software maths (the default) doesn't use it. Logged `TEST: RTW3MP_TEST_X87=...`; `ls x87 pert` sets it at run time. |
| `RTW3MP_TEST_X87SOFT` | the mod (A) | Test only: `0` makes A start live battles with each PC's own x87 routines (`sm=0`, as level 15) instead of the software maths; `ls x87 soft 0|1` sets it at run time. With `RTW3MP_TEST_X87` on B this shows what a CPU difference would do. |
| `RTW3MP_TEST_CPU` | the mod | Test only: the CPU vendor this copy announces and logs (`Intel`, `AMD`, ...) instead of CPUID's: the hello, the live battle's start and ready messages and the READY diagnostics (`CPUs AMD (Admiral), Intel (Captain)`). The maths doesn't change. |
| `RTW3MP_TEST_DATAFILE` | the mod | Test only: `<name>[;<name>...]` (e.g. `Data\Gundata.dat;Data\Extra.dat`): this copy hashes those game data files as if their content differed (a name that isn't there: as an extra file; since engine level 18 a name starting with `tmp`, the game's temporary files, stays out as in the scan). The files themselves are never touched. `ls testdatafile` does the same at run time. |
| `RTW3MP_TEST_FILELOG` | the mod | Test only: `1` logs every file the game opens for reading, once per phase (`pocfilelog: [startup|battle load|battle|after the battle] <path>`), from the mod's start (a CreateFileW hook). |
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
| `regression_selftest.py` | current | the regression agent | The suite's offline self-test (no game): parsers, log patterns, `--list`, `--dry-run`, fixtures and Preferences in a sandbox; `--logs <folder>` reports over recorded logs. |
| `rtw3ctl.py` | current | the suite, every other script, the agents | Starts a copy with the bridge (`launch`), sends one bridge command (`cmd`), waits for a `status` value (`wait`); `SAVE_DIR`, the game folder. |
| `e2e_test.py` | current (library) | the suite imports it (`dismiss_dialogs`, window helpers) | The 0.1 end-to-end test (two copies, a month); the suite's `merge` scenario came from it. Saves without a backup. |
| `ui_mp.py` | current (library) | the suite (`IDS`, `mp_window`, `text`, `windows_of`) and the standalone scripts | Drives the real Multiplayer window with posted and sent messages. |
| `package_release.ps1` | current | the docs agent's release checklist | Builds `RTW3MP-<version>-win32.zip` from `dist\` and `extras\`. |
| `realmouse_test.py` | standalone, needs the user's consent | regression agent, safety rule 1 | Moves the real mouse (~30 s): the window's close button, Start session and Give to AI with real clicks. Saves without a backup. |
| `record_hosting.py` | standalone | the hosting video in `docs/` | Records `docs/hosting.mp4` and the stills from `PrintWindow` captures of the host's windows; needs Pillow and ffmpeg. The addresses it shows (25.17.204.86, 192.168.1.20) are fictitious examples. Saves without a backup. |
| `desync_diff.py` | standalone, reads files only | anyone after a live-battle DESYNC | Compares the two PCs' DESYNC reports (`ls_desync_<A|B>_<battle>_<epoch>_<minute>.txt`, engine level 13): where the call sites of the shared draws part, the fields that differ in the objects' bytes of the boundary before, the hashed state lines that differ. `python tools/desync_diff.py <A report> <B report> [--words N]`. |
| `net_delay_proxy.py` | current (test harness) | `regression.py`'s `twopc` (started and stopped by the runner), two-PC conditions ([Two-PC conditions](#two-pc-conditions)) | A localhost TCP proxy between a joining client and the host's session port: a one-way delay, jitter and a bandwidth cap per direction, statistics lines. `python tools/net_delay_proxy.py --listen 47634 --target 127.0.0.1:47624 --delay 120 --jitter 60 --kbps 4000 [--seed N] [--log <file>] [--stats 30]`. Standard library only. |

Removed on 2026-10-16: the 0.1 scripts `live_lobby_test.py`, `repro_issues.py`, `run_turn.py`, `verify_tabs.py` and
`ui_buildship.py`, and `diplomacy_test.py` (a wrapper for `regression.py --only session,diplomacy`); older MODLOG
entries still mention them.

The live-battle drivers of the 0.3.0 work (`ls_live.py`, `ls_pvp.py`, `ls_drive.py`, `ls_tracediff.py`) are in the
developers' session folder, not in the repository; the suite's `lockstep*` scenarios are the maintained tests of
live battles.

## The runner in detail
Runner: `python tools/regression.py [--only <name,...>] [--group <group,...>] [--list] [--dry-run] [--dist <folder>]
[--stress] [--missile-fixture <GameN> | --missile-slot <n>] [--missile-nations <h>,<k>] [--missile-mission <m>]`. It
refuses to start while
`RTW3.exe` runs, without Steam, when a backup from an interrupted run differs from slot 1 (or from slot 9, the missile
slot, or the Preferences), when `Game87` (the
client's battle slot) exists, (with `player_copy`) when Game78-80 exist, (with `lockstep_pvp` or `lockstep_rejoin`) when
Game76/Game86 exist, (with `lockstep_torpedo`) when the Preferences have no `RealismSetting`, or when the missile-era
fixture's slot is taken by other files or is one the suite writes itself. It backs up slot 1 (and slot 9 when
`lockstep_air` or `lockstep_strike` runs and the 1935 fixture is in place; the missile slot given with `--missile-slot`;
the Preferences when `lockstep_torpedo` sets `RealismSetting=2`; a `--missile-fixture` folder is copied into its free
slot), launches host
and client once (scenarios may start a third copy or restart a killed one), runs the selected scenarios in order (`--only` always adds `session`; a scenario that throws
stops the rest), closes the PIDs it started, restores slot 1, slot 9, the missile slot and the Preferences (verified by
hash), deletes `Game77`, `Game87`, Game76/86, the missile fixture's copy, `lockstep_resume`'s copy of slot 1, the trace
files of its traced battles and
`player_copy`'s slots, and prints
per-scenario results (`SKIP` with the reason for a scenario whose fixture is missing), the processed months with their duration, the live battle cases (checks, seconds), the logs it
kept of restarted copies, the game-open time and `N/M checks passed`. The default run is every scenario but
`--stress` scenarios (`STRESS_ONLY`); `--stress` adds them and switches `lockstep_pvp`/`lockstep_month` to their long
variants; `--group` picks the scenarios of `GROUPS`.
Exit code 0 = all passed and saves restored, 1 = a check failed (or saves not restored), 2 = not started.
Keep total game-open time low (each processed month costs ~15-25 s). Shared helpers in the runner:
`run_month` (pre-month hooks -> submit -> host "All players are ready" -> `turn` -> answer dialogs on both PIDs
until the client plans the new month, 240 s timeout -> `wait_event_roll` -> `wait_relations_turn` (relations v2) ->
`month_checks` -> month hooks),
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
`g87`), `war_month` (run_month of a war month with a `BattleDriver` without a plan: a battle declined, or fought on
auto-run when the game allows no decline), `battle_dialogs` (one pass of the battle dialog policy; `UNPLANNED` declines battles in months without a
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
month's merged live battles), `ls_spam`, `ls_resync_once`, `ls_after_checks` (the j) checks; also the host's month =
the client's month),
`ls_log_scan` (no lockstep error, hooks once, chat lines without a file path (0.3.2), no LEAK), `ls_leakcheck` (`ls leakcheck 1` per copy/PID), `ls_detach_check`,
`ls_guard_check`, `ls_case` (a stress case that fails without stopping the scenario),
`turn_button`, `month_after`, the battle screen (0.3.2): `battle_frm`, `post_click`, `click_tool` (`TOOL_AT`: fastest
(926, 19), aircraft (1074, 19), autorun (1232, 19) on Panel1), `click_arrow` (the TUpDown's upper/lower half),
`ob_tab`, `battle_tree`/`tv_items`/`tree_tops` (a tree view read cross-process), `popup_pick` (any control's popup
menu; `popup_priority` uses it), `divreport`/`open_divreport`, `lsctl`, `airwin`/`air_rows`, `close_modeless`,
`panel_locked`, `speeds`, `arrow_check`, `ai_count`, `teardown_lines`; the 1935 fixture: `fixture1935`,
`air_bases`/`air_own_side`, `airborne`/`air_sample`; `LS_MODELESS` (the answerer leaves those windows alone while the
battle screen is enabled), `SKIPPED` (the summary's SKIP); diagnosis: `box_text`/`box_texts` (the message of a game box
or picture message, read from the copy's memory; every answered `TMessageForm`/`TdlgPictureMessage` is logged with
`(text: '...')`), `turn_report`/`dialog_text` (the turn report's lines, logged with its Close), `gen_date` (a slot's
[General] `Year/Month/Day sip=<ScenarioInProgress>`, logged before every turn, at each test battle's start and in the
after-checks), `ymd`/`ymd_log`/`next_month_ymd`, `poss_fields`/`poss_changes` (the possessions in a slot's MapData and
the [General] invasion keys: every month that moves one logs `possessions that changed hands this month`),
`host_settled` (before a test battle from the strategic screen: no box up, the screen enabled, `status` not busy),
`saved_vp`/`file_time` (a slot's [NationN] VP; a file's write time), `vp_mismatch_probe` (`lockstep_month`, only
when the VP differ: repeated VP keys or sections (`save_repeats`), the keys that differ between the host's slot 1 and
the client's slot 77 (`save_diff`), then `load 77 1` on the client and `load 1 0` on the host with the VP before and
after each load), `vp_reload_check` (`lockstep_month`, after every host battle, 0.4.0; it runs in the session, where
every load keeps the saved VP: a blockade made on purpose,
nation 0's `BlockadeModifier` set to `BLOCKADE_LEVER` (1000) with `nfield` and saved on both copies, then each copy
loads its own slot twice; after each load every nation's VP and EnemyVP equal the file's and the mod logged one
`LOAD_VP_RE` line ("load: the game's blockade check gave victory points the save doesn't have; put back: ...",
nation 1 blockaded); the modifier is put back and saved), `load_answering` (a bridge `load` while the copy's dialogs
are answered).

The 0.4.0 live-battle limits (engine level 11) add these helpers: parsers `parse_forces`/`parse_forces_div`
(`ls forces [div]`), `parse_pending`, `parse_landcap`/`landcap_shared`, `parse_airforce`, `parse_airlist`/`airlist_same`,
`parse_strike`, `parse_tail_why` (`ls rejoinable`, `ls resumable`), `parse_missilestat`/`missile_stocks`,
`parse_mounts`, `parse_marker`/`read_marker` (the host's `rtw3mp_livebattle.ini`), `parse_torp`, `parse_missiles`,
`deferred_lines` (the releases' `deferred <what> of side <s> carried out | not carried out` lines), `quiet_boxes`,
`hash_groups`/`groups_compare`/`ls_groups` (the per-minute hash line's groups, e.g. `ident=` and `date=`), `load_dates`,
`trace_entries`/`trace_compare`/`trace_cleanup` (`ls trace 1` files: call sites equal, the frame markers' state-hash
bits counted apart), `expected_ls_level` (`MP_LS_LEVEL` of `src/mod/common.h`), `realism_pref`; live helpers
`ll_noans` (a copy out of the answerer while a test drives its dialogs), `ll_boxes`/`ll_answer_boxes` (the game's boxes
by rules), `ll_question`/`ll_answer_question` (the game's `TdlgEventAnswer` question: radios by caption), `ll_synced`
(B's batches on A before Run), `ll_release`, `ll_part` (one failing part does not end the battle), `ll_desyncs`,
`ll_leaks`, `ll_minutes_check`, `ll_epoch`, `ll_resync`, `ll_forces`, `ll_force_pick`, `tree_select`, `open_report_of`,
`ll_kill`/`ll_relaunch`/`ll_kill_k2`/`ll_relaunch_k2` (with `LOG_SEGS`/`ll_scan_start`/`ll_scan_texts`: the log scan
covers every run of a copy), `ll_join`, `ll_live_both`, `ll_side_human`, `ll_rejoined_checks`; the scenarios' parts
`limits_*` (`lockstep_limits`), `deferred_step`, `force_lead_dialog`/`lb_click` (the real Force orders and Select lead
dialogs), `rejoin_return`/`rejoin_dropped`/`alone_minutes`/`client_planning`, `relay_rejoin_drive` (`ls_drive_live`
mode `relayrejoin`), `slot1_snapshot`/`slot1_rollback`, `kill_host_mid_minute`, `resume_host`, `host_session_join`,
`rejoin_resumed`, `session_again`, `airops_part`, `nightcap_snapshot`, `landcap_order_check`, `strike_open`/`lv_check`/
`strike_press`/`strike_short_part` (the strike dialog's rows checked through the list view, its buttons by BM_CLICK),
`lv_popup_pick` (a list view sub-item's popup menu), `strike_manual`, `strike_session`, `computer_strikes`/`bases_of`,
`torp_try`/`torp_search`/`torp_release_check`, `missile_fixture`/`missile_try`/`missile_release_check`,
`fire_panel`/`panel_check`, `autorun_button_check`, `fastest_click_check`; the runner: `missile_plan`,
`prefs_realism_for_run`/`prefs_restore`, `leftover_backup`, `dry_run`.

`twopc` (0.4.1, engine level 15; [Two-PC conditions](#two-pc-conditions)) adds: `battle_prefs_keys` (`kPrefs` of
`src/mod/lockstep.cpp`: the battle preferences a live battle takes from A), `parse_prefs`/`parse_pref_changes` (`ls
prefs`, the mod's "Key a -> b" lists), `prefs_values`/`prefs_set`/`twopc_variant` (the second player's Preferences:
each key of `TWOPC_ALT` set to a value other than A's, a missing key into its section), `write_atomic`,
`parse_mapview`/`map_problems`/`map_ref` (`ls mapview` against the game's rule), `state_ships`/`parse_state_ships`/
`hexint` (`ls desyncreport` on B: the simulation's sighting fields of every ship, the report read and deleted at once),
`force_ai_of_divs` (`ls forces` and `ls forces div`), `sac_sighting`/`battle_file` (a battle file's sighting counts,
logged at loads), `port_free`/`proxy_start`/`proxy_stop`/`parse_proxy_log` (the delay proxy, `PROXY`; `NET_PROXY`
makes `ls_rejoin`, `ll_join` and `ls_drive_live`'s killB join through it), `CoverWatch`/`battle_form_any`/
`load_windows`/`cover_report` (B's battle window every ~3 ms during its loads), `twopc_win_size`/`twopc_win_place`
(SetWindowPos), `shot_png`/`write_png`/`twopc_shots` (PrintWindow captures with `RTW3_SHOTS`), `live_hook` (`ls_month(...,
live_hook=fn)`: `fn(what, rec, A, B, r)` from `ls_drive_live` with 'load' after the start check and 'minutes' before
the battle's end), and the scenario's parts `twopc_setup`, `twopc_load_checks`, `twopc_map_check`, `twopc_sizes`,
`twopc_resync`, `twopc_negative_control`, `twopc_prefs_dialog`, `twopc_end_test`, `twopc_prefs_back`,
`twopc_cover_check`, `twopc_test_battle`, `twopc_live_hook`, `twopc_month`, `twopc_final`, `twopc_restore`. The
runner backs the Preferences up for a run with `twopc` (`prefs_for_run(keep=True)`), refuses to start when the proxy's
port 47634 is taken, and stops a proxy the scenario left running.

`divisions` (0.6.1) adds: `parse_divs` (`divs`, `divstore <K>`), `off_divisions`/`slot_divisions` (a `.off`'s
`[CampaignDivisions]`, also `Autosave.off`'s: `CampDivNo` and each division's id, name and ship ids), `nation_ships`
(the ship ids of a nation's ships section), `div_summary`, `division_lines` (the mod log's division lines since a
mark), and the *Fleet organization* driver: `fo_set_tab` (a tab of the strategic screen's `TabControl1` picked with a
posted `TCM_SETCURFOCUS`, as a click), `fo_controls`/`fo_list` (the division editor's buttons and `lvDivisions`, read
cross-process after *Collapse all* and *Expand all*), `fo_new_division`/`fo_division_dialog` (*New division* and the
game's `TdlgCreateDivision`: type, name, abbreviation, OK), `fo_popup` (a row's popup menu, only that row selected),
`fo_add_ships` (*Add ships to division*: the first ships the game's `TdlgSelectShip` offers), `fo_rename` and `fo_make`
(a copy's divisions made through these dialogs; posted messages only).

**Per-month checks (`month_checks`, 10 (11 while nation 1 follows the player's rules) after every processed month, counted in the scenario that processed
it):** host and client `intel` count == unique (the client's count may be 0 since 0.4.1: its list holds only the
player's own reports; its own monthly check runs the game's CheckIntelligence, which can add a report it already has:
up to that check's new reports, `intel: monthly check for nation K: ... N new reports`, a repeat is a NOTE; on the host
a repeat of the game's own gun-comparison report is a NOTE too, below); intel reports grow by at most 30 a month (host and client);
the saved `[IntelReports]` of `Game1\RTWGame1.bcs` and `Game77\RTWGame77.bcs` have no empty or repeated entries
(ReportNo == IntelN keys == distinct non-empty entries; a repeat of the game's gun-comparison report, `GUN_REPORT_RE`
"<Mon yyyy>  X - Our N in guns are superior to Y guns of the same calibre." or "... N in guns have better performance
than ours.", is a NOTE: the game's `SpySuccess` compares gun qualities in a loop of up to 11 calibre draws with
replacement, so one spy success can report a calibre twice, in the turn report and the list; the next load drops the
repeat, 0.2.2; Known traps, the 0.6.0 release candidate; and in the client's save a repeat its own monthly check made
the month before, excused then in its list: the player's reports go to the host with its turn and come back in the next
month's package, so `Game77` holds that repeat once more, a NOTE with the repeated text (`saved_own_repeat`: ReportNo ==
IntelN keys, no empty entry, 0 < the other repeats <= the list's repeats of the month before; 0.6.0 release candidate
2)); the client's month package (`state seq N -> <name>
(nation 1, B bytes)` in the host log) grows by at most 25 % over the previous month (baseline: the session
start) and no `NOT sent`; no rejected turn or failed write in the host log for that month; the client rolled the
new month's event exactly once (at chance 0 exactly one `event: no event this month`, never `already rolled`);
relations v2 (`kwar_month_check`): the host stepped every joined nation's war counter as the game steps nation 0's
(`kwar` before the Turn and after the month: +1 at war, -1 at peace, then 1 when a war started, 0 when the last ended,
`kwar_step`) and the client's game uses nation 1's own counter (`kwar` on the client: own = game = the host's); and while nation 1 follows the player's rules (`relrules`, B2), one more: the host's month left nation 1's relations with the computer's nations alone (`relrules_month_check`; allowed: a computer nation that joined the host's war against nation 1; the new month's changes by the player's side, already applied when the check runs, are excused when the first of them starts from the value before the month: the player's own event of the new month, `event of Captain (nation 1, #idx ...): tension 1-j a->b` in the host log, its random event or since 0.4.1 its intelligence result `#-1 'Intelligence'` (`event_tension_changes`), then its relation turn).
0.5.0 (`econ_month_check`): the joined nation 1's month under the player's money rules: `econ 1` on the host has the
month's record (`ended=1`) and its funds at the month's end are ComputeExpenses' result plus the player's money rules
(`fundsEnd == after + ruleFunds + treaty + otherFunds`, `otherFunds` 0, the funds now the same; `treaty` since 0.6.0:
the arms-control conference after ComputeExpenses, the scrap value of nation 1's ships under construction over a new
treaty's limits, 0 from a host without it); skipped for a host without the
record or a month without a joined nation 1. Since 0.6.0 (`treaty_guard`, after every month): an arms-control treaty in
force (the game's own conference event, e.g. Sep/Oct 1900 in the fixture) is cancelled on both copies (`econtreaty
cancel`) and noted, because its tonnage limit stops the host's next Turn ("We have more capital ship tonnage in service
than permitted by treaty! You must scrap ..."); not while `arms_treaty` runs (`ctx.keep_treaty`). Since 0.6.0
(`battle_area_month_check`, one more check after every month): the client's game holds no last battle's area or
possession (`battleptrs` on the client: `area=0 poss=0`): every load clears them, the game's load kept them pointing
into freed memory and the next save showed "Error in SaveBuildCampaign procedure! ..." (a client without the command,
before 0.6.0, is noted once and not checked). A war of the host's nation with nation 1 that starts after the month's end
(`applydiplo` at the month's end, `diplomacy` B) opens a record of its own with the same date (`pending=1 expenses=0
warStarts=1`; logged with the next month as `<date>: no ComputeExpenses`): then the check reads the month's own host
log line instead (`ECON_LINE_RE`: `month's end funds F (other changes +0)`, F = the funds now).
Reported, not asserted: private memory (GetProcessMemoryInfo PrivateUsage) of client and host after the session
start and after each month (summary table), and handled transient file locks.

## Scenario catalog
Keep this table current when you add or change scenarios.

0.3.3's checks (the runner of v0.3.3) are in this suite since the 0.4.0 release candidate (3f109d5), reconciled with
the branch's own: the `air_units` scenario (main's design on slot 49; the branch's slot-47 variant and its battle part
replaced, `lockstep_air` checks every live battle load), the air-unit counts after each load in `session` and
`lockstep_air`, the division report test `lockstep_ui` k) (the branch's `lockstep_limits` g) dropped as its duplicate),
`lockstep_stress` S8, the identification checks (`ls_ready` after every live load, `ident=` in `ls_compare`, the scans'
`ident_scan_checks`; the branch's whole-battle checks feed them) and the game's error boxes (`game_error_boxes`). Their
0.3.3 results are below the table.

| Scenario | What it guards | Checks | Months | Notes |
|---|---|---|---|---|
| `session` | both logs show the version of the build under test (`RTW3MP <version> starting (protocol <MP_PROTOCOL>)`: the one x.y.z (with a suffix such as -dev) after "mod " in the installed RTW3MP.dll, `build_version`; the protocol from `src/mod/common.h`: 0.4.0 / 3), both bridges up, host loads slot 1 as nation 0, host/join/claim nation 1/start, client plans nation 1 in slot 77, nation 2 reserved as stand-in player "Bot", client Multiplayer window open; records the intel baseline at the start; 0.3.3: `airunits` on both copies = their saves' [AirUnits] count (0 for the 1900 fixture, then no `air units:` line), no air-unit WARNING; 0.4.1: `relview` (`relview_check`) on the client shows nation 1's relations (slot 1 = the host's nation Great Britain, every slot named and its tension = `rel 1 j` on the client, the war and alliance marks, the war state: "At peace." from a 0.4.1 host, since relations v2 the game's own "At peace for N months." from the client's own war counter) and on the host its own relations as before; relations v2: nation 1 follows the player's rules exactly when the client announced them (the host log's "player's relation rules N", `relrules`), the stand-in nation 2 never; 0.4.1 (the relations view and the player's own intelligence together, 2026-10-21): on the client `relview` + `intelinfo` (`intel_panel_check`: every slot's flag and intelligence label name the same nation, slot 1 = the host's flag with the player's own spending on the host's nation, the player's own nation in no slot, client=1), its +/- buttons (`intel_buttons_check`: `intelbtn 1`/`9` under the host's flag change the spending on nation 0, `intelbtn 3`/`11` on nation 3; the label, the Intelligence cost, the log's `intel: spending on <nation> a -> b`, the whole panel while raised and the screen's `stIntel1..8` texts read cross-process (`panel_intel_texts`, left to right = slots 1..8); each put back at once), and the host's panel as before (slot i = nation i, client=0) | 17 | 0 | always first |
| `intel_reports` | 0.2.2 intel-report fix. Part 1 runs inside `session`, after the first load and before hosting (nothing saved yet): the bloated fixture is repaired on load (host `intel` count == unique > 0, `intel reports: N entries -> M` in the host log), and two more `load 1 0` keep the count. Part 2: at the session start the client's list and both saves (host saved slot 1 at the start) are clean, the month package was sent, and the client's load of that clean save logs no repair line (the game's own trailing empty entry is dropped quietly) | 10 | 0 | fails without the fix; needs the bloated fixture (safety rule 7) |
| `file_lock` | 0.2.3 write-retry fix: the client's `writetest` into slot 77 with a throwaway file (`rtw3mp_locktest.tmp`) locked from Python. Unlocked: ok at once, no retry line. Lock released after ~1.2 s: ok after ~1-3 s, file written, `write ...: succeeded after N retries` in the client log. Lock held for the whole call: `error write failed (32 or 5)` after >= 3 s, `write ... FAILED: error E (in place: E2)`, file unchanged, no `.mptmp` left; test file deleted | 7 | 0 | never lock a real save file |
| `events` | 0.2.4 joined players' random events. Session start: one `event: no event this month` (chance 0), `events_<hash>.ini` with nation 1 and the month marked rolled. a) `eventlist` >= 50 events, some H, "XX appears to be considering a naval rearm" (#5) listed and not H. b) host nation 1 Prestige/BudgetModifier, `rel 1 j` (j=0,2..8), `rel 0 i` (i=2..8); client `nations` order, playerIdx. c) `eventnow 5` -> client `TdlgEventAnswer` "Event - <month>"; while open `status` answers, `nations` refused; first answer + OK; `eventlast` `event=5`. d) exactly one tension entry j != 1, delta Prestige:1; host `eventlast` `nation=1 event=5 tension 1-j a->b` with b = clamp(a+d, 0, 40) (no entry if a > 40); host `rel 1 j` == b; every other recorded pair unchanged; host Prestige +1 and BudgetModifier + reported; client Prestige == host; client nation order and playerIdx restored; client and host log lines. e) a second forced non-H event (non-ship captions with cond 0 first; up to 4 tries if one is not shown): shown and sent, host logs `second event of Captain in <date> ignored (one per month)`, host values and `eventlast` unchanged. f) `eventnow <first H>` -> `error event needs the host`, no dialog, nothing sent. g) hook after the next month: host == client Prestige and BudgetModifier; values before/after the event, at submit and after the month reported. h) pre-month hook: `eventchance 100` before the last month with nation 1 at peace (`diplomacy A` in the full run; the last month without `diplomacy`); after it the client rolled `event N for nation 1` by itself (dialog answered within 15 s, no `no event this month`), host `event of Captain (nation 1, #N` + `eventlast`; then `eventchance 0`. i) war rule: `eventchance 100` before `diplomacy`'s war month (month B); after it nation 1 is at war (`rel 0 1` or `rel 1 2` war=1), that month's roll logged exactly `event: no event this month` with no event dialog (since 0.4.1 the month's intelligence check may show its spy-caught questions, `TdlgEventAnswer` too: not counted), `eventlast` is `none`, and a random `eventnow` adds no dialog (`this month was already rolled`); then `eventchance 0` (skipped with a `SKIP` log line if `diplomacy` is not selected) | 32 (31 without `diplomacy`; 27 when the session start's spy check took the month's event slot: Known traps) | 0 (g after the next month, h in the last month at peace, i in the war month; processes 2 itself if no later scenario processes any) | client launched with `RTW3MP_EVENT_CHANCE=0`; `events_*.ini` deleted before/after the run |
| `merge` | client research % and a ship order survive the host's month (host and client); `merge nation 1` and `AI suppressed ... for nation 1` in the host log; no AI suppression for AI nation 3 | 9 + 8 | 1 | from `tools/e2e_test.py` |
| `research` | 0.2.8 joined players' Research screen (the game opened it for nation 0 = the host's nation). Setup: `research 0/1` on host and client (25 values each); if nation 1's % is > 12 (fixture 13, after `merge` 14) the dialog's "12" is logged and it is closed (WM_CLOSE); `nfield 1 ResearchPct 10` (<= 12, != nation 0's). a) client `researchopen`: `TfrmResearch` opens, client log `research screen: opened for the player's nation 1 instead of nation 0` once, the dialog's budget = nation 1's (not nation 0's), its priorities = nation 1's in every listed area (rows mapped by name through `Data\ResearchAreas3.dat`) and != nation 0's in the areas where they differ. b) the popup menu sets one area (preferably one whose row != its index: "Low" on row 11 = Explosive shells, area 12) -> dialog and `research 1` changed, `research 0` not (dialog open); budget 7 typed + OK closes without a message; client nation 1 = pct 7 + that priority, nation 0 unchanged. d) reopened: shows them (log line again); 15 % + OK -> `TMessageForm` "Rule the Waves III" [OK], dialog stays open, nothing written; answered, the dialog still open, WM_CLOSE leaves it (nation 1 unchanged, no window, bridge answers). c) host `researchopen`: no `research screen:` line, the dialog shows nation 0; budget +1 + OK changes host nation 0 only. After the next processed month (hook): host nation 1 = the client's values, host nation 0 = the host's own, client nation 1 kept, client nation 0 = the host's, the client's dialog shows nation 1 (log line, WM_CLOSE), host log `AI suppressed: DetermineAINationResearch for nation 1` (if the game called it; never for AI nation 3); then the host's nation 0 % goes back (`nfield`). e) (ebfca72) host `researchrun 1 100` (`research_run`: the game's DoResearch 100 times for joined nation 1 through the mod's hook, ~99 % to hit its 5 % full re-roll): the `before` and `after` priorities of the test line both equal the client's pattern (and `research 1` afterwards), at least one `research priorities of player nation 1 kept (N changed ...)` line (8 in the first run, one with 10 changed: a full re-roll); control `researchrun 8 100` on AI nation 8 (Austria-Hungary): its changes recorded (2 areas in the first run), no `kept` line for it | 24 (+ 8 month checks = 32 when it processes its own month) | 0 (shares the next month: `design_study 1` in the full run; 1 own month if no later scenario processes any) | after `merge` (whose `nfield` 14 it overrides); fails on 0.2.7 (a, b, month checks). 0.2.8: `--only research` 37/37 in 43 s, full run 368/368. a1a8484: the month checks failed once at random (DoResearch's re-roll, MODLOG); ebfca72: 24/24 |
| `doctrine` | 0.2.3 doctrine fix. Player path on the client: real "Doctrine" button -> `TdlgDoctrine` -> toggle a training check box -> Apply -> Close gives a pending change with 12 months (falls back to `settraining` if the dialog can't be driven). Then nation 1 training shortened to 2 months + missile policy pending 2 months (client, merged on submit) and the host's own training change (2 months). After month 1: host and client at 1 month, not applied, one `doctrine nation 1: training 2 -> 1 months, missile storage 2 -> 1 months` line. After month 2: training and missiles applied on host and client, both Doctrine chat lines, `(applied)` line, host's own change applied by the game, no countdown lines for AI nations | 17 | 0 (shares `design_study`'s 2; processes 2 itself if `design_study` is not selected) | setup in the planning month before `design_study`; month checks run as a `run_month` hook |
| `design_study` | a joined player's study (set to 2 on the client, submitted) counts down 2 -> 1 -> 0 on the host, host and client agree, one `design study nation 1` log line per month, client chat "ready for construction" (or the 1 % "technical issues" line with the value kept); the host's own study (2) still advances in the game's routine (0 or 1 after two months) with no `design study nation 0` lines; AI nation 3's designs unchanged and no countdown lines for AI nations | 15 + 16 | 2 | added for the design-study fix (0.2.1); fails without the fix |
| `diplomacy` | offers/accept/decline/withdraw, war/peace/alliance at month end, AI peace kept away from player wars (0.5.0 b5b0da7: the forced AI peace of nation 2, whose only war is the players' war with nation 1, is skipped: `AI peace (badLoss=1) skipped for nation 2: its wars are with players' nations`), Diplomacy buttons via posted clicks | 52 + 16 (51 + 16 if the host is at war in month B) | 2 | moved from the former `tools/diplomacy_test.py`; its month A is the `events` random-path month, month B the `events` war-rule month; its peaces are compromises (wars started at month end score 0:0) |
| `battles` | joined players fight their battles against AI nations on their own PC (after 0.2.8). a) both logs `Joined players' battles: ok (frmMain=.. checkAttack=..)` (else one more failing check and stop); defaults: client `battles state` none, slot 87, slotMode 0, fight 1; host `battles list` phase 0, timeout 1800. Setup: J = the AI nation with a possession in a sea area where nation 1 has >= 4 ships (`battle_pairs`; the fixture: Spain, Northern Europe), war 1-J by `settension 1 J 50` (set again before every month: the game's AI peace may end it); `battles inject` refused for an unknown area, an AI nation as K, the stand-in as J; `battles resolve 99` -> `ok 0`. b) two candidates injected a month (up to 3 months; a battle the game found itself is fought if none comes about; the AI avoids about 2 of 3: since 0.4.1 such a battle is `battle N: avoided` on the client and merged with the player's VP gain, `battle N applied: ...; the enemy declined battle`, instead of being resolved by the game): `battle request N injected`, `battle phase: N battle(s)`, `battle N sent to Captain`; during the phase host busy=1, TfrmBuildCamp disabled, status "Battles in progress: Captain: Germany vs Spain in Northern Europe", `battles list` phase=1 fighting; client in slot 87 (playerIdx 0, `nations` 0 = Germany, `battles state` starting nation 1 opp J, slotMode 1); dialogs: map message Accept, preview Accept, battle screen, auto-run prompt "Yes", results "Exit battle", `battles state` aftermath=1 at the next box; `battle slot: DoStrategicTurn skipped`; `StartPlayerBattle 0 attacks J in <area> -> battle`, `battle N: fighting nation 1 vs J in <area> (slot 87)`, `battle N: fought` (<area> from the fought battle's request line, `battle_area`; since 0.6.0 that can be the player's generator's request `... in <area>, N attempt(s), fought by Captain` when the injected ones were declined: dist_rc060_2 `--stress`); back in slot 77 as nation 1 in the new month; `battle N applied: X ships changed (F fields), R removed, VP a/b, prestige p` with X+R > 0; cross-check: every ship field the battle changed (Game87 after the battle vs the host save at the phase start; intel fields and BuildingNationIdx excluded) has Game87's value in the merged save (read at the applied line), sunk ships gone; after the month too (reported when another battle of the month changed them again); merged VP of 1 and J and Prestige of 1 = sent + the line's deltas, plus those of the month's battles of the same nations merged before it (an avoided battle's VP gain, since 0.4.1: dist_rc041_2's battle 1 avoided with VP 360/0, then battle 2; a battle of other nations merged before: a `SKIP-note`); after the month host memory = its save (= merged unless another battle followed); `nations` ship counts minus the sunk; the client's Prestige/VP/ship count = the host's; chat lines; no exception; `battles list` empty. c/d) `battles timeout 20`; three candidates each a month (up to 3 months): c) Decline pressed in the preview; **reported as declined** (client `battle N: declined`); since 0.6.0 (risk 4) with the decline's consequences on the client, merged by the host (`battle N applied: ...; the player declined battle`, chat `* Captain declined battle for <K> against <J> in <area>.` and `* You declined the battle (VP ...`), never resolved by the game (before 0.6.0: `(Captain declined it)` and the game's resolution), the month went on; decision 2 (0.6.0): client `battle N: the nation's decline counts a/b in the battle slot (the save's were c/d)`, host `battle N: nation 1's decline counts x/y (were a/b)`, the session file's `kdecl` has nation 1's latest counts. d) a held preview: `resolved by the game (time limit)` 20-30 s after the send, client notice "... without you (time limit) ...", the late answer `battle result N from Captain ignored` (or not sent), the game resolved it, slot mode off; `battles timeout 1800` restores the default. Battles the game found itself (usually some): request line, sent and settled, chat "* Battle! Germany meets Spain in <area>. Captain fights it after the month." (SKIP line without any). c2) decision 4 (0.6.0): client `battles hostgen 0` (`battles state` hostgen=0: as under a 0.5.0 host), three injected battles declined a month (months `battles D1`, `D2`, up to 2): one declined in its preview -> client `battle N: declined` sent alone (no `battle N applied`), host `battle N: resolved by the game (Captain declined it)`, chat `* The battle of <K> against <J> in <area> was resolved by the game (Captain declined it).` and `* You declined the battle; the game resolves it.`, after the battles `battle N resolved by the game: battle|no battle` with `picture message captured` when it fought and no TdlgPictureMessage answered on the host meanwhile; `battles hostgen` back to the welcome's value after (1; 2 since B6 piece 2). e) rejoin with `fightbattles 0` (host gives nation 1 back and sends the month, the client loads it, myNation 1, fight=0); inject refused `no connected player who fights battles for nation 1`; a processed month at war: no request, phase or sent line. End: peace 1-J (the original tension), `battles state` none, `battles list` empty | 43 (42 without battles the game found itself) + the month checks per processed month (4-9 months; dist_b6_5: 86-97) | 4 (+ up to 5 when the game finds no battle) | after `diplomacy` (peace on both fronts), before `victory_points` (the war 1-J is over and the client stays joined with `fightbattles 0` for the rest of the run); select `events` with it only when `diplomacy` is selected; the runner deletes Game87. After 0.2.8: `--only battles` 68/69 (130 s), full run 437/438 (only "reported as declined", fixed in 0.3.0); ebfca72 (0.3.0): 70/70 in the full default run (520/520) |
| `divisions` | 0.6.1: each joined player's own campaign divisions (the *Fleet organization* tab), made with the game's own dialogs on both copies (New division, Add ships to division, one Rename division on the client; posted messages only). a) the host log's hello "campaign divisions 1"; `divs` answers on both copies (host nation 0, client nation 1); the client's divisions at the start hold nation 1's ships only. b) the host makes two divisions with its own ships; none of them and none of its ships reach the client (`divs`, its Fleet organization list). c) the client makes two with nation 1's ships (one renamed). d) after each of two processed months (the first with the Fleet organization shown on both copies: the list is filled again inside the load; the second on the map): the client's `divs` = its own two with the same ships, nation 1, none twice; the host's `divstore 1` = the client's two; the host's `divs` = its own two, none twice; saved: slot 1's `.off` (and `Autosave.off` when written that month) `CampDivNo` = the host's two, slot 77's = the client's two (no duplicates after the reloads); the logs (host "divisions of nation 1: 2 stored (repeats dropped: 0 ..." and "... 2 in its month", its reload's "2 loaded" and "2 from before the load dropped"; client "the month holds nation 1's own 2 (kept by the host; 0 with other ships)", "2 of nation 1 loaded", "... from before the load dropped", in the first month "the Fleet organization list filled again"); both Fleet organization lists the copy's own only. e) no exception, access violation, "Error in FillDivisionList2" or "Missing ship" in either log, no such box | 18 + 2 x month checks = 42 | 2 | after `battles`, before `victory_points`; needs 0.6.1 on both copies; the divisions stay for the later scenarios (slot 1 is restored after the run). dist_fd_2 (edaeb16, 2026-10-25): 42/42 in `--only session,battles,lockstep_month,divisions` (200/200, 486 s), this scenario 90 s |
| `victory_points` | 0.2.6 joined players' victory points. a) both logs `Peace treaties: ok (... battleHooks=1/1/1)` (else one more failing check and stop); the client's latest `victory points shown: own V, enemy E` = its `vpinfo` (0 and 0 after nation 1's own peace with the computer's nations in the month's relation turn, `relations v2: peace: DoPeace`, after the load: a NOTE, `vp_shown_ok`); start: the host at war with nobody, 1-2 at peace. b) host `diplo war 1` (+ `diplomacy request 0 war 1 accepted`) + `applydiplo`; `battlevp 1 1000 3000 Minor`: the host's VP and EnemyVP rise as before, nation 1 + the EnemyVP gain (0.2.5: unchanged), `victory points: nation 1 +X in its battle against the host -> Y`, seen == enemy, nation 2 and AI 3-8 unchanged. c) `enemyvpadd 101` + `applydiplo`: nation 1 +101 (the only enemy), its `... +101 of the host's enemies' 101 this month -> V` line, seen == enemy, the host's VP/EnemyVP and 2-8 unchanged, `enemyVPSeen` in `rtw3mp_session.ini`. d) host `diplo war 2` + `applydiplo` (a second war resets nothing); `enemyvpadd 101` + `applydiplo`: 1 +51, 2 +50 (index order), two share lines, seen == enemy. e) a drop: `enemyvpadd -E` (the game's reset) + `enemyvpadd 101`: nothing shared, seen = enemy = 101. f) host offers peace to 1, the client accepts with its "Accept peace" button (row via LVM_SETITEMSTATE, BM_CLICK), equal `warvp` (compromise): separate peace, the host's VP/EnemyVP unchanged, `nation 1 is at peace; V -> 0`, nation 2 keeps its VP; peace 0-2 (`diploas 2 peace 0`) + host `diplo war 1` in one step: DoPeace resets the host's VP and EnemyVP, `nation 2 is at peace; V -> 0`, the new war started; re-sync: after that step seen == enemy == 0 and no share line in it, then `enemyvpadd 101` + `applydiplo` gives nation 1 +101 with its share line, seen == enemy == 101. g) client and host `diplo war 2` + `applydiplo` (the host at war with 1 and 2, 1-2 at war); `battlevp 1 500 2000` / `battlevp 2 300 1000` credit 1 / 2 (lines, the other unchanged); host peace with both (button, `diploas`) + `applydiplo`: DoPeace resets the host only, 1 and 2 keep their VP (no `is at peace` line), seen == enemy == 0 and no share line (the DoPeace hook credits nothing twice); EnemyVP marker `enemyvpadd 77`; one processed month: host VP of 1 and 2 = credited + their `war score 1-2` points (0.6.0: + their blockade points, `victory points: nation G +v (blockade, in its war with nation L)`), `merge nation 1`; client `vpinfo` vp == the host's, `enemy` = the VP of 1's enemies (> 0 and != the host's EnemyVP), client `victory points shown: own V, enemy E` once; i) the host's EnemyVP unchanged by the month and the merge. h) `settension 2 3 50` + `aipeace 2 0`: the AI war 2-3 ends, 1-2 goes on, `AI peace: VP of player nation K kept at P (the game set 0)` for 1 and 2, their VP kept, AI nations 0 (SKIP line without an AI peace). End: peace 1-2 (compromise) + `applydiplo`: 1 and 2 VP 0 with their `is at peace` lines, the host at war with nobody; marker removed, the host's VP and EnemyVP 0 | 42 + 8 month checks = 50 (48 if h is skipped); 3 when the VP hooks are unavailable | 1 (g) | after `diplomacy`, before `treaty` (treaty's `applydiplo` treaties must stay after the last processed month); the host's wars only via `applydiplo`, its battles only inside a host war that DoPeace ends (Known traps). 0.2.6: 50/50 (`--only victory_points,treaty` 130/130, full run 312/312) |
| `treaty` | 0.2.5 peace terms of wars between players. a) both logs `Peace treaties: ok (... battleHooks=1/1/1)` (else one more failing check and stop). Start: 1-2 at peace, host at war with nobody; `claimable 1 2/2 1/0 1` only the loser's possessions, Value < 11. b) client `diplo war 2` + `applydiplo`: war, chat, score 0:0; `warvp 1 2 A 100` (A = max(900, 100G+500)) -> `terms` outlook winner 1 level 2; client row vp/outlook "you are winning: ... major victory". c) client offers, nation 2 accepts (`diploas 2 peace 1`): terms agreed level 2 budget 8, chat on both sides, client row `claim=8:N:0`, a second offer refused, `rtw3mp_session.ini` has `warVP` and `treaties`. d) unknown name, the winner's own possession, a loser's possession worth >= 11, land worth > 8 refused on client (`claimland`) and host (`diploas 1 claim 2`); the loser can't claim; a valid claim accepted (terms, chat, `land claimed: v of 8`). f) client `diplo cancel 2` erases the terms (outlook again, `treaties=` empty, claim 0:0:0); agreed again the other way round, the old claim is gone. e) client window: row of nation 2 selected (LVM_SETITEMSTATE), button 1084 "Claim land...", Tension column `VP A:100`, Pending text, label = outlook; click opens `RTW3MP_Claim` (owner = MP window, MP window disabled, `claimui` target/budget/rows = `claimable 1 2`, claim pre-checked, sum label); untick + Cancel (1095) sends nothing; WM_CLOSE likewise; 1094 withdraws the claims (chat); ticks within 8, a tick past it is undone ("(over the limit)"), 1093 sends the claims (terms, chat, `claim=8:N:k`). g) processed month: one `treaty 1 over 2 (level 2): k possession(s) (claimed), s ship(s), prestige +2, resources -500` line, war over, the claimed possessions moved (host and client), treaty + "Peace: ... (<nation 1> victorious)." in both chats, client `showing a peace treaty (n lines)` + the "Peace treaty" dialog (`treaty_text`: heading, ceded, every hand-over as `... as war reparation.` plus its own sentences, reparations; n = the dialog's lines, one period per line; the same lines in both chats; answered), no host dialog; prestige exact: winner +2, loser by the formula (P div 10 when P >= 20 else 1, not below 0; major -1, -1 more above 20), the host (not a party) unchanged unless it answered an event that month; funds/ships reported. k) that month's `war score 1-2: +x/+y -> A:B` lines add up from A:100 (since 0.6.0 with the host's other credits of the war in log order, `victory points: nation G +v (<why>, in its war with nation L) -> t, war score 1-2 x:y`, e.g. B5's blockade points: `war_score_steps`, dist_rc060_2), no `first battle` line. h) `applydiplo` (after the last month): new war 0:0, nation 2 ahead (minor), 2 offers / client accepts (budget 4), the loser can't claim, > 4 refused, nobody claims -> `treaty 2 over 1 (level 1): k (automatic)` = most valuable first within 4, exact: Prestige 2 +1, loser 1 by the formula (minor), BaseResources 1 -r, ships -s/+s; the host (not a party) keeps its Prestige. i) host `diplo war 1` + `applydiplo`, `warvp 0 1`, client offers / host accepts, host `claimland 1 ...`, `applydiplo`: war over (warCounter 0, hostWars 0, `diplomacy: peace 1-0`), `treaty 0 over 1 (level 2) ... (claimed)`, claims moved, exact deltas (host Prestige +1 = treaty +2 and the DoPeace dent -1; loser 1 by the formula, major), the players keep their Prestige through DoPeace (nation 2 unchanged, nation 1 at its treaty value, host log `end of war: prestige of player nation N kept at P (the game set 0)` for 1 and 2), the host's own treaty dialog (same `treaty_text` checks). j) equal score 300:300: compromise terms, claims refused, `applydiplo`: "(compromise peace)", no treaty line, possessions/Prestige/BaseResources of the host, 1 and 2 unchanged | 65 + 8 month checks = 73 with every optional case (SKIP lines otherwise); 3 when the feature is unavailable | 1 (g) | needs `Peace treaties: ok`; h-j use `applydiplo` after the last month (see Known traps). 0.2.5: 73/73 (full run 262/262; with the reparation lines split at " She is renamed ", `--only treaty` 73/73 in 69 s, 11 lines per treaty) |
| `relations_peace` | relations v2 B4: peace with the computer's nations for nation 1 under the player's rules. Start: both logs `Peace with the computer's nations for a joined nation: ok`, `relrules` has 1 and the client's `peacelast` says host=1 (else stop). Setup: J = a computer nation at peace with everyone, not allied with 1; `settension 1 J 50` on both copies (`kwar 1 1` if it was at peace), `kwarvp 1 J 400 100`. a) `peacetest 100 2 1` (`wait_peace`: the game's dialogs answered, `answer_possessions` takes the first possession): applied once (one `relations v2: peace of nation 1 with` line), the war over on both copies (tension 0..5), the possessions ceded to 1 held by 1 on the host, J's BaseResources/Prestige as reported, the score erased (`kwarvp` kept), war counter 0 and Blockaded 0 (no other war), host chat `* Peace treaty between ...`, the client's "Peace treaty" dialog when the treaty has lines (`TREATY_SEEN`). b) war again, `peacetest 100 -3 1`: applied once, war over, ships handed over held by J only on both copies (`findship`) and the host's ship counts (a ship the host logs as `ship N (...) skipped: not among nation 1's ships`, `PEACE_SHIP_SKIPPED`, stays where the host has it: a peace between players earlier in the same month, `treaty` h), handed it over on the host, and the client's copy gets that with the next month; a NOTE), J's BaseResources/HasLostWar as reported, nation 1's HasLostWar on the client after a hand-over. c) war again, `diplo peace J` (the Diplomacy row, player `computer`, flags P only): `NegotiatePeace(40, 0, 0)` applied as kind=peace; a second proposal refused (`this month`), row without P, `kwarvp` peace=used; `rtw3mp_session.ini` keeps the month (`kpeacem` 1:Year*12+Month of `diplomacyDate`). d) `settension 1 J 5`, one month: each ship handed over held once on both copies (the merge; a ship the host skipped: by the host's holder on both), the war over; after nation 1's relation turn `rtw3mp_session.ini` keeps its month (`krelm`; no turn: a NOTE). | 22 + month checks | 1 (d) | needs B4 (`Peace with the computer's nations ... ok`); the winner of a) and b) follows the game's modifiers (`peacelast` winner): the checks follow what the client reported. 0.5.0: a) also checks the host log `relations v2: campaign saved after a peace proposal of nation 1 (ok`: the host saved its campaign right after the apply. 45/45 with `session` (dist_rv_7, 2026-10-22, about 40 s). |
| `war_effects` | 0.6.0 (B5): the player's war effects for nation 1 under the player's rules (group `relations`, after `relations_peace`). Start: host log `warfx: the player's war effects for joined nations: blockades ok`, client log `War help from allies for a joined nation: ok`, `relrules` has 1 and the host answers `warfx` (else stop). Setup: J and X = two computer nations at peace with everyone, the host at peace; on both copies `settension 1 J 50` (`peace_war`), `setally 1 X 24`, `settension 1 X 2`, `settension X J 50`. M1: host `warfxblk J 1` and `warfxblk 1 0`, client `allyrand 0`: two host lines `victory points: nation 1 +v (blockade, in its war with nation J)` and no other blockade credit; the client's `relations v2: war help from allies for nation 1 (war counter 2): VP n` with n > 0 and the host's `relations of` line with ` VP n` and `allyvp 1-J+n`; nation 1's war score with J (`kwarvp`) up by the credits + n + a battle's of the month (the host's `war score 1-J: +a/+b` lines), its VP at least by the credits + n; J's flag on the host (`warfx`) and the client (`nflag`, from `kblk`), not 1's; the host's `warfx: message for nation 1: Gain x VP for blockade of enemy` and the client's `economy: showing`. War weariness: before M1 nation 1's GovernmentType 2 and BudgetModifier 20 (`nfield` on both copies, the old values put back at the end), `kwarvp 1 J 100 0`: one host line `warfx: war weariness of nation 1 (the player's rule): ... government 2, BM 20 ... no (not losing)` and the month's line listing 0, 2..8 under the game's own; before M2 `kwarvp 1 J 0 5000`, `warfxrand 0`: `blockaded 1 -> unrest u -> u+1` and `warfx: message for nation 1: Our people is getting dissatisfied with the bad war situation!`. M2: `warfxblk J -1`, `warfxblk 1 1`, `allyrand -1`, `settension X J 30`: two credits to J for 1's blockade, J's side of the score up by them (+ a battle's), its VP at least by them; 1's flag on both copies, `The enemy gains x VP for blockading us`. M2 (c) raiders, submarines and the trade war (0.6.0, round 4; when `warfx 1` has `searule=1`): `warfxsea 1 6` and `warfxblk J 0` (J not blockaded: a blockaded nation's merchants stay at home, the game's raider rule): the host's `warfx: sea war of nation 1 (enemies J): war counter 2, ... its view, blockaded (no enemy raiders); ... J: raiders n/0, subs 0/0, VP +x/+0; TEST: 6 raider(s) ...` with n > 0, the trade credits `(1, +x, J)` = the frame's VP, nation 1's side of the war score up by at least x; nation 1's report gets the frame's lines (`warfx: message for nation 1: German raider ... sinks n Russian merchant ships in The Baltic`, `Trade war this month: ...`; client `economy: showing N message(s)`) and the host shows no box for it (no SubmarineWarfareSummary, no raider/submarine/merchant text). Building for war: before M1 the newest ship under construction of nation 1 and of J (`shipbuilder`, the highest CShipId) set to be built in the other's yard on both copies; after M1 the client's `relations v2: building for war for nation 1 ...` hands over both (`id:1>J`, `id:J>1`), the host's `relations of` line has `seized ship id 1->J` and `seized ship id J->1`, `findship` on both copies (without a ship under construction of nation 1 or J, e.g. after the months of a `--stress` run, a `SKIP-note M1 building for war`). M3 (`warfxrand -1`; the war budget and the war loan: BudgetModifier 3 and UnrestLevel 2 on both copies, `kwar 1 4`, `econrand 1`): `warfx: StopBlockadedMoves (run with War 1` with ` 1:`; `warfx: building for war: nation 1's war budget: BudgetModifier 3 -> 4` and `War budget means increased allocations to the navy!` (counter 4), `econ 1` with `loan=1` and `economy: nation 1 news: ... war loan! ...`, BudgetModifier 5 on both copies plus the new month's own changes of nation 1 by its PC before the check, from the host log (`own_field_changes`: `event of ... (nation 1, #i '...'): ... BudgetModifier+n`, `relations of ... (nation 1; ...): ... own BudgetModifier+n`; a run of 2026-10-23 had 6: the intelligence result's "Give it maximum publicity ..." answer, +1); the host's VP and EnemyVP unchanged throughout. M1-M3 run through `war_month` (a `BattleDriver` without a plan: a battle of the month is declined, or fought on auto-run when its preview allows no decline; the month gets `FIGHT_TIMEOUT` more). Ends at peace (`warfxblk 1 -1`, alliance 0, X-J 30, the fields put back, `peace_war(1, J, 5)`). Setup (0.6.0, round 4): `warfx strength 1 J` lists the ally X in nation 1's view and not on the host's frame; M1 the month's check line ends "(its view)". M4 (0.6.0, the enemy-revolution rule): the host's nation at war with nation 1 too (`ls_war01`: `diplo war 1` + applydiplo), client `revotest 0 J:25 0:25`, nation 1's unrest 25 on both copies; the month through `ls_month` (drop12: a live battle 0-1 of the month is driven like lockstep_month's): nation 1's PC logs J's revolution and "wars ended J (the general peace: DoPeace)", the host applies "peace 1-J"; the war 0-1 stays out on both sides (client "... stay out ...: 0 (unrest 25: it would be a candidate)", host "revolution (host): ... 1 (unrest 25: it would be a candidate)") and goes on (host and client); then `ls_peace01` (the players' compromise). | 21 + month checks | 4 | needs 0.6.0 on both copies. A natural blockade may add credits (Germany vs Russia in the 1900 fixture: Germany blockades Russia from the war's first month): the checks count only the forced side's. 94/94 with `session` and `relations_peace` (dist_b5_4, 2026-10-23, 138 s); with M4 and the view: group `relations` 216/216 (dist_b54_3, 2026-10-23, 367 s). |
| `arms_treaty` | 0.6.0: the disarmament treaty, the arms-control conference and a joined nation's war (group `relations`, after `war_effects`; fix-treaty and B5 round 4 (f), one scenario since the merge). Start: both logs `Disarmament treaty with joined nations' wars: ok`, `relrules` has 1, the host at peace, `armstreaty` answers on both copies and `econtreaty` on the host (else stop); nation 1's wars at the start ended by `peacetest 100 2 1` (else stop); J = a computer nation at peace with everyone, not allied with 1; no war among nations 1-8 (a FAIL otherwise, the scenario goes on). The suite's treaty guard is off meanwhile (`ctx.keep_treaty`). a) the host's own war under a treaty (TEST `armstreaty set 24 15000 12`, `armstreaty war 1`, `armstreaty refresh` on the host): one box "The outbreak of war cancels the disarmament treaty!" on the host, the treaty ended there (0 months, no limits), `kept` unchanged, the client unchanged; the host's war counter and every nation's DisplacementLimit/BuildConstraint put back. b) M1 at peace, TEST `econtreaty 100` (the game's CreateArmsControlTreaty(100) at the month's end, after ComputeExpenses): the host log `TEST: the arms-control conference ran: treaty months 0 -> n` (n > 0), `econ 1` conference=1, treatyBM < 0 (the signing's -2), treaty= the scrap value of nation 1's ships under construction over the limits (>= 0) as the line `economy: nation 1: the arms-control conference changed its funds by +x and its BudgetModifier by -2` has it, = fundsEnd - after - ruleFunds (the 0.5.0 money check, `econ_month_check`, sums treaty=); the same treaty on both copies, the client's `armstreaty` host=1, no box. Its tonnage limits are lifted on both copies (`armstreaty set` with its own months and limits: no Turn stops at "We have more capital ship tonnage ..."); without a treaty from the conference the TEST treaty `armstreaty set 36 15000 12` on both copies stands in. c) war 1-J (`settension` on both copies, nation 1's counter above 0 on the host); the client's `armstreaty host 0`, `war 1`, `refresh`: treaty kept, `kept` up, log `CancelTreaty left to the host (a joined player's PC, an older host`, no box; `armstreaty host 1`, `refresh`: 0 months, no limits, `endedhere` +1, log `arms treaty: ended on this PC for its nation's war, as the host ends it`, no box (war counter put back). M2 (`war_month`): the host log `arms treaty: the war of nation(s) 1 ends the disarmament treaty (n month(s) left` once (n > 0), `ends` +1; no treaty and no box on either copy; the host's chat `* The outbreak of war cancels the disarmament treaty! (`, the log `arms treaty: the turn report line of nation 1 goes with state seq` once and the client's `economy: showing n message(s)`. d) M3 (`war_month`) with TEST `econtreaty 100`: the host log `the arms-control conference (CreateArmsControlTreaty) sees the war(s) of the nations people play: 1-J J-1`, `TEST: the arms-control conference ran: treaty months 0 -> 0`, the game's "With a war already in progress" message answered on the host, `econ 1` conference=1 treaty=0 treatyBM=0, no treaty on either copy (other wars of nations 1-8 listed). e) M4 (`war_month`): nation 1's counter >= 3, no box, no treaty on either copy, `ends` unchanged. End: `peacetest 100 2 1` -> peace 1-J; every nation's TreatyTonnageLimit put back on both copies. TEST `econtreaty 100 blind` (not in the scenario) reproduces the conference before 0.6.0 (battles B1: a treaty during nation 1's war, ended at once).
| `battle_generator` | 0.6.0 (B6): the player's battle generator for nation 1 under the player's rules (`scenario_battle_generator`; group `battlegen`, also in `relations`). Start: both logs `Player's battle generator for joined nations: ok`, the host's `Player's battle generator: its error box in a joined nation's view goes to the log` (decision 3), the host's hello line `battle generator 1` (or 2, B6 piece 2); `relrules` has 1 (else forced); a client left with `fightbattles 0` (scenario `battles`) joins again with 1. Setup: J = a computer nation with a possession where nation 1 has >= 4 ships (`battle_pairs`), `settension 1 J 50` on both copies. b) `battles gen 1` x20 (dry runs): all ok, every attempt has nation 1 on one side and a computer nation on the other; the host's relations unchanged; decision 1: the attempt counts logged (`attempts per dry run`), all < 64 and one > 4 (else a SKIP line; dist_b6_5: 4-7); decision 3: `battles gen 1 fault` -> `error exception in the game's battle generator: Error in CheckForBattles procedure! Access violation ...`, the host log line, no new request, no TMessageForm/Tdlg window and no game error box on the host, then a normal `battles gen 1` ok. c) up to 3 months (`BattleDriver`, fight_any): `the player's battle generator for nation 1 ` once a month, at most one `battle request N (the player's generator)`, sent with an outcome; no request of the computer's generator for nation 1 against a computer nation (`left to the player's generator` lines counted); a fought one merged (`battle N applied:`); decision 1: every request of the player's generator in the run replayed in order on the client (`battle N: attempt i (...) found no battle`, i = 0, 1, ...; a SKIP line if none had one). d) risk 4: `battles injectgen 1 J area` and `battles inject 1 J area`, both declined: merged (`the player declined battle`) or as the game had it (enemy declined, no battle, a preview without Decline fought), never `resolved by the game`. e) `battles aitest 1 J area`: `fought=0` or a captured box, no TdlgPictureMessage on the host. e2) (0.6.1) `battles aitest 1 J area` where J has no ship that can fight (the host's save): `ok fought=0 captured=0`, the host log `ResolveAIBattle 1-J in <area>: no battle (ships that can fight there: n/0; ...)`, no game error box. The war ends at the end. | ~17 + month checks | up to 4 | the AI often declines battle (avoided): c) passes without a fought battle. |
| `battle_surprise` | 0.6.0 (B6 piece 2): surprise attacks and takeovers at a war's start for nation 1 under the player's rules (`scenario_battle_surprise`; groups `battlegen` and `relations`). a) both logs `Surprise attacks and takeovers at a war's start for a joined nation: ok (CheckForWar's call sites 1/3/1)`, the host's hello line `battle generator 2`, the client's `battles state` `hostgen=2` (`relrules` 1 and `fightbattles 1` ensured as in `battle_generator`). Setup: J = Japan (5) if a computer nation, else the first of 3-8; nation 1 at peace with everyone (`settension ... 5`). b) client `battles testsa J` and one month: the client's relation turn logs the test pick, `surprise move: nation J takes over <p>`, `surprise attack: nation J may strike nation 1` and sends `ksa=J,1;` and `sland=1`; the host: the war 1-J, `takeover <p> -1->J surprise J>1` in the `relations of` line, J owns <p> on both copies (`possessions J`), one `battle request N (surprise attack)` (kind 2, `battles list` `kind=2:att=1`), the chat lines `* In a surprise move, <J> has taken control of <p>!` and `* Surprise attack? <J> may strike <nation 1> at the war's start.` (the game's CheckForWar runs its landing/takeover step only in 50 %, with the trait 95 %: a relation turn with the test's war plan line but no `test: the takeover picks` line is a `SKIP-note b) the takeover`, and the war, `sland=0`, the request and the surprise attack's chat line are checked without it; dist_rc060_2's default run). c) client `battles sadry J k` -> `ok ran=1 pairs=1>J;` (no "No opportunity found" box, no TMessageForm), `battles sadry J` -> `pairs=J>1;`, nothing sent. d) host `battles injectsa 1 J`, client `battles saretry 10`, one month with a `BattleDriver` (both requests planned `fight`: the first that comes to a battle is fought, the other declined): each request sent, played with `StartSurpriseAttack` (slot indices), its outcome `fought`/`declined`/`avoided` merged (`battle N applied:`) or `nobattle` with `battle N (kind 2): no battle (<why>)`, never `resolved by the game`; no box of the surprise attacks on the host (BOX_SEEN). Ends at peace 1-J. | 13 + 2 x month checks = 37 | 2 | the AI attacker often declines its own attack (avoided: `The Japanese navy declines battle! You gain 200 VP!`). dist_b6_6 run 4 (`--only battle_surprise`, 54/54 in 59 s): avoided, and nation 1's attack on Formosa declined in the preview (17 ships changed: the mobilization); run 5: both fought; dist_b6_7 run 6 (`--only battles,battle_generator,battle_surprise`, 179/179 in 423 s, this scenario ~60 s): avoided, and nation 1's attack fought in Southeast Asia, merged (21 ships changed). |
| `relations_war` | 0.6.1: the tension bar above the war line (the 0.6.0 report "The tension bar when exceeds the critical threshold and no war is declared for non host"; `scenario_relations_war`; group `relations`). a) nation 1 under the player's rules and a computer nation J at peace with everyone, not allied with it and without a war plan against it (the client's `rellast` will=), their tension 15 (+ nation 1's wars) on both copies and nation 1's war counter -10 (`kwar 1 -10`: at War < -48 CheckForWar's rumours, Random(180) < tension, may take the declaration's place): after the month nation 1's relation turn (RandomTensionChanges, TensionEffectOfPreviousWars, then CheckForWar once, as the game's month) declared the war: war 1-J on both copies, the client's `relations for nation 1 sent: ... ran=tension,previous wars,war`, the host's `relations of ... (nation 1; ...): ... war 1-J`, the game's war message among the client's boxes ("has declared war on us", "state of war", "War has broken out between", "joined the war against" with J's name). b) the players' tension 25 on both copies, the host's war-line memory cleared first (`warline forget`): the chat line `* Tension between <host's nation> and <nation 1> is N, above the war line. Wars between players are declared only by the players (Multiplayer window, Diplomacy); the game will not declare one.` once on each PC and not again (`warline check`: `told=0`); `relview`: host slot 1 `Tension with <nation 1> is N - played by a person: wars between players are declared only in the Multiplayer window (Diplomacy), never by the game`, client slot 1 (the host's nation) the same with the host's nation, no computer nation's bar (slots 3-8) with it; `warline` people: host `1,`, client `0,`; no war 0-1 (tension >= 11). Ends at peace (`peacetest 100 2 1`), the players' tension and nation 1's war counter as they were. | 6 + month checks = 18 | 1 | needs 0.6.1 (an older build: skipped, no checks); the reserved stand-in nation 2 (its player not connected) is left out of b)'s check of the computer nations' bars and has its own: a person's nation on both PCs (its bar's hint while at peace with it, `warline` people lists it on both; before the fix the host showed it only while it followed the player's rules, the client always). dist_fw_1 (2026-10-25, `--only session,relations_peace,war_effects,lockstep_pvp,relations_war` 172/172 in 323 s): 18/18 in 37 s (J = Russia, "War has broken out between Germany and Russia"; the line "... Great Britain and Germany is 25 ..."); dist_i61_1 (2026-10-25, with the stand-in's check): 19/19 (France: the hint on both PCs, `warline` people `1,2,` on the host, `0,2,` on the client). |
| `player_copy` | 0.2.7 joined players' "Copy game". Start: the client's date and `nations`, a snapshot of slot 77, the client's cooldowns (`events_<hash>.ini`). a) client `copygame 78`: the game's "Rule the Waves III" box [OK] and the mod's "Multiplayer" box naming both nations, no overwrite prompt; client log `player copy in slot 78: Germany is the player nation, Great Britain is nation 1 (AI); ...` with the counts of the specification; Game78 compared key by key with `expected_copy`/`expected_map`/`expected_off` of slot 77: files (.sta copied, no Autosave.sac/.off, BattleInfo.bbi or DesignFiles_swap.tmp), names and the 5 traded [Nation0*]/[Nation1*] sections (ship ids), every relation key of Nation0-8, nation numbers 0 <-> 1 and the host's ships' CommanderId -1, [General] (War/EnemyVP rule, ScenarioInProgress -1, ...; 0.5.0: War = the player's own war counter, `kwar` on the client, when it agrees with nation 1's wars, else the host's guess as before), [EUT] = the client's cooldowns, MapData mines/TakenFrom, .off divisions, design files 0 <-> 1, nothing else changed (12 checks). b) slot 77 and slot 1 byte-identical; the client still plans nation 1 in slot 77, same date and nations. c) host `copygame 79`: the game's box only, Game79 = slot 1 file by file ([Nation0] Great Britain), no copy log line. d) `copygame 78` again, "Confirm" answered Cancel: `copy of slot 77 to 78: not copied`, Game78 and slot 77 unchanged. g) the real "Copy game" button -> `TdlgInputNumber` (the highest free slot of 3-9) -> boxes, log line, the copy as specified and byte-identical to Game78 (SKIP line without a free slot). One session month: `merge nation 1`, the client plans the next month as nation 1. e) client `leave`, slot 77's `rtw3mp_player.ini` `nation=1`, `load 77 0` plays the host's nation (what 0.2.6 copied), `copygame 80`: boxes and log line, Game80 as specified from slot 77's new month ([EUT] kept outside the session). f) `load 78 0`: slot 78, playerIdx 0, no dialog; `nations` = the session's values with 0 and 1 swapped, the other nations' funds as slot 77 has them (the client's memory can differ: `relations_war` ends with `peacetest` on the client after slot 77's save, and the game's peace changes the loser's and its ally's funds; a NOTE lists them); `rel 0 j`, `rel 1 j`, warCounter and hostWars match the copy's file; one single-player month (`turn`, dialogs answered up to the turn report, no battle; a war the session left in the copy, e.g. `battle_surprise`'s 1-J that the next month's relation turn ends, is ended there first with `settension 0 j 5`, a NOTE: its battle preview once stopped the month and the run, dist_rc060_2 `--stress`), then `save` writes the next month into slot 78; responsive, Great Britain (AI) keeps ships and possessions, no exception or session line in the client log | 26 + 8 month checks = 34 (33 if g) is skipped) | 1 (+1 single-player month of the copy) | the last of the non-lockstep scenarios (it leaves the session; the lockstep scenarios join again); Game78-80 must not exist before the run, the button's slot (3-9) is chosen free; all deleted afterwards. 0.2.7: 34/34 (`--only player_copy` 41/41 in 66 s; full run 346/346) |
| `lockstep` | live player-vs-player battles, the host against the joined player, short (from the research driver ls_live.py). a) `ls session` enabled=1 available=1 on both, the host lists `Captain(1,b,ls)` (else one failing check and stop); the client's plans changed (nation 1 ResearchPct, not submitted). b) `pocbattle 1 0` (the debug Test battle = DoBattle outside a month: battle slot mode): host preview accepted, "battle held at its start", "battle <bid> epoch 1 sent (n files, b bytes ...)", the client's "battle loaded from slot 87", both running within 150 s; roles A side 0 / B side 1, same bid/epoch, pocstate playerSide 0/1, host `ls session` hostOpp=1 testSlotMode=1 slotMode=1, client role=B slotMode=1. c) runs asked by A (3) and B (2) at speed 3: >= 5 minutes, every per-minute hash equal, no DESYNC. d) orders of both sides at one paused boundary: the same course on both copies = the ordered one, each copy received the other's batch; d detach) the client detaches a ship (`ls detach`) between minutes: one division more on both copies (22 -> 23), the next 2 minutes equal, no resync; d guard) `ls dispwrite` on the client, then on the host: "the display wrote N sighting fields ... (kept out of the simulation)" on that copy, no LEAK, no DESYNC, the next 2 minutes equal. e) `ls desync` on B: "DESYNC at minute m", "Synchronising the battle again", a new epoch (its READY hash compared), the next 2 minutes equal; both copies wrote their DESYNC report of that minute (engine level 13: `DESYNC report ls_desync_<A|B>_<battle>_<epoch>_<minute>.txt written`) and `tools/desync_diff.py` over the two names `DivisionCourse` (the field `ls desync` changes); the reports of injected desyncs are deleted after the run, others kept as evidence. f) both `ls end` before minute 400: "The battle can't end yet" on both, both running, end=0/0. g) `ls drop`: "dropping Captain: test", "The AI commands Germany now", the client back in slot 77 as nation 1 (its three log lines), the host runs 2 minutes alone; the client's plans kept (canLoad saved slot 77). h) the host's battle on auto-run to its end and closed: "battle slot: DoStrategicTurn skipped", date unchanged, slot mode off. i) after-checks (`ls_after_checks`: also slot 1's and slot 77's saves of the copies' months). k) one normal month: the plans reached the host. l) no lockstep error in the logs, `lockstep hooks: ok (<n> of <n> context hooks, ...)` at most once per copy with every context hook of this source tree (`expected_ctx_hooks`: 22); the identification (0.3.3, every lockstep scenario's scan: `ident_scan_checks`): every live load logged its identification line, `ident=` equal at READY and in every minute compared; the leak check (on in every copy) logged no LEAK | 16 + 8 month checks = 24 (25 since 0.3.2: the chat-line check of `ls_log_scan`) | 1 | after `player_copy`; joins the session again if needed (and turns the leak check on). c06860f: `--only lockstep` 31/31 in 108 s; full run 24/24 |
| `lockstep_ui` | the battle screen's own controls in a live test battle (`pocbattle 1 0`: host A side 0, client B side 1; 0.3.2, updated for 0.4.0). a) both running, A verified the start; each copy logs `battle dialogs resolved` once and finds every modeless battle dialog; the missile panel (engine level 9, `panel_check`): "Fire mode" (`rgMissileDoctrine`) disabled on both, "Fire at will" (`cbFireAtWill`) enabled and showing each copy's own side (`ls missilestat` box = faw[side], agreed = faw = 11); reported: `ls divs` per side around an `ls resync`, the private memory a load costs; a division report opened on each copy stays open. b) the speed arrows (posted clicks on `UpDown1`): B up and A down between minutes, B down inside a running minute at -1, A up inside a running minute at -2: one request `<nation> set the game speed to N (from the next minute).` on both, the minute in progress unchanged, both at N next minute, no DESYNC; two clicks on B 1.5 s apart: N+1, N+2. c) Auto-run per player (engine level 4, `autorun_button_check`, B then A, each out of the answerer): the click shows the game's question (`TdlgEventAnswer`, 3 radios: Yes / Yes, and order all forces to the closest friendly base / No): "No" logs `auto-run question (hand over) answered 3`, nothing pending, `ls autorun` mine=0, the button up; "Yes": `... answered 1`, the chat `Auto-run on ordered; ...`, one pending order; at the release both log `auto-run on for side s ... auto-run 10\|01` and `side s ai=1 human=0`, every division of that side on the AI on both, the clicked button down (the other up), the chats ("The computer commands your ships now (auto-run). ..." / "<name> handed their ships to the computer (auto-run)."), `ls simside` sim 2 (side 0) or 0 (side 1) on both, `pocstate autorun=0`; 2 minutes equal; the button again, "Take back command of your ships?" Yes (2 radios): `(take back) answered 1`, at the release `auto-run off ... auto-run 00` and, as in single player (engine level 12), `side s back to its player as in single player: the lead divisions of <n> forces; <k> forces stay with the computer as before auto-run; <d> divisions stay with the computer (...)` alike on both copies, the divisions on the AI equal on both, the button up, the chats (the player's "... As in single player, you get each force's lead division back (<n>) ..."), sim 0. d) "Fastest available" as a shared clock control (engine level 2, `fastest_click_check`): B on, then A off: one request `<nation> turned Fastest available on\|off (from the next minute).` on both, the clicked button put back to the state in effect until the release, A's `ls fastest` want; after the next minute `ls fastest` fastest and down, `pocstate fastest` on both, `Fastest available on\|off` logged on both, the same speed, no DESYNC; `ls fastest 1`, then a speed request (`ls speed 3`) releases it ("... (Fastest available off)"). e) every division on the AI, 14 minutes: equal, no DESYNC, fastest 0/0, the same speed; while engaged 4 clicks on B's arrow up stop at 2 (contact rule; a NOTE when the fleets never engage); the panel's Fire at will still each copy's own (faw 11). f) `ls airwin show` on both; B's report's land-CAP button (engine level 6) is an order: the button stays down (down=1 group=6), no "not available" line, `ls landcap` unchanged until the report's OK; `ls fastest 1` before an `ls resync` with both windows open: each copy logs the windows hidden before the load (ok), both running, `epoch sent (... fastest 1 ...)`, fastest 1 and down 1 after the load on both, the air window opens again; `ls fastest 0`; B's report again, land CAP clicked, OK: a `VLlandcap#1` batch, at the release `ls landcap` landcap1 equal on both and naming that division; 2 minutes equal; the panel after the resync. g) B's ship tree lists only nation 1's forces after the load, the resyncs and the minutes; every load logged `battle data cleared (ok), air formations left 0` (>= 3 per copy); every minute's `date=` equal on both and the campaign's own (the identification: every live load's line and `ident=` of every minute go to the scan's `ident_scan_checks`). k) 0.3.3, the division report's speed write (`divreport_speed_check`, the client's report, then the host's): a ship of the report's division (`pocdump`, `ship_index`) set to 45 knots on both copies (`ls testcurspeed`); the copy opens the report: it logs `the division report lowered <ship>'s current speed 45 -> n on this PC only: put back ...`, the other copy no such line; with the report open 45 on both and `ls hash` all/ident equal (only `ord` may differ while an order the report gave is pending); 3 minutes equal, no DESYNC, no LEAK, the ship below 45 on both afterwards. h) A drops B, the host finishes on auto-run, slot mode off; the snapshots saved the campaign's own date in slot 1, the host's close logged `campaign date <date> put back`; i) after-checks; j) no lockstep error, chat lines without a file path or "Game saved", no LEAK | ~45 | 0 | after `lockstep`. The 0.3.2 expectations (the panel disabled, Auto-run and Fastest blocked, the land-CAP button put up) failed 10 checks on dist_ll_7 (549/559): replaced by the 0.4.0 behaviour above. Live on dist_ll_11 (`--group limits` twice, 233-255 s): a)-d) and g)-j) pass after the suite's fixes; e) one DESYNC at minute 32 at dawn (05:41: the display's `LightState` write, fixed in engine level 14), f) after it. dist_ll_15 (2026-10-18): 41/41 in 199 s (e) minutes 19..32 equal; k) client and host); dist_ll_16 (2026-10-18, the full default run): 41/41 in 213 s; dist_vp_2 (2026-10-18): the default run 39/41 (k) host and j): a sighting-only LEAK right after `ls testcurspeed` at dawn, the test command's own baseline, excused since 2b44526), `--stress` 41/41 twice |
| `lockstep_limits` | the 0.4.0 live-battle limits (engine level 11) in one 1900 test battle, the default run's quick checks (from the stage A/B live proofs). a) the start (as `lockstep` b)); `ls level` level = peer = announced = `MP_LS_LEVEL` of the source tree on both, the host's `ls session` levels=Captain:<level>; the start's load keeps the campaign's own date (`the load: campaign date <camp> kept (the battle save said ...)`) and logs the identification line on both. b) deferred orders (engine level 3): change of force (`ls chforce`) from A alone, from B alone and from both at one boundary with B's lead order failing at the release; change of lead from both at one boundary, B through the real Force orders and Select lead dialogs (`force_lead_dialog`), the game's box in the giver's chat only (`deferred action's box '...' answered 1 (chat)` / `(quiet)`); a second lead change refused at once ("The lead of this force was changed once already."), B's order on A's force refused ("That force is not yours."); every release: both copies log the same `deferred ... of side s carried out at minute m (given for minute m)` (or `not carried out: <why>`) lines, side 0's first, the pending lists empty, `ls forces` (and `ls forces div`) identical, the minute equal, no resync. c) a resync with orders pending on both copies, its snapshot dated a month later (`ls testclock 744`): the orders dropped with the chat line on both, both copies log the same `side 0's computer control as before the load: <f> of <F> forces, <d> of <D> divisions` (engine level 13: `ai0=`) and side 1's, the divisions on the AI per side as before, both copies keep the campaign's own date. d) battle turn-aways (`ls turnaway`) from A alone, from B alone, the cooldown refusal, both at one boundary 15 minutes later (`the divisions' lead chain loops` after a lead change is the game's own refusal). e) Save as the host's checkpoint (`ls save` from A, then from B): `battle saved into slot 1 at t=... (ok)`, both chats "Battle saved (minute m) in the host's campaign slot: ...", the marker of slot 1 written for the battle, its epoch and minute (the file and `ls resumable` marker=match), slot 1's [General] the campaign's date with a battle in progress, the `.sac` rewritten. f) the realism requirement: the division report's *Fire torpedoes* (`ls ctl TdlgDivisionReport bnTorps`) enabled on both copies when the run's RealismSetting is >= 2, disabled at 1 (B takes A's preferences), where `ls torp` is refused as well ("the division report's Fire torpedoes is disabled (button=0 ...)", engine level 12); 0.6.1: both copies' chat named the battle's Realism once at the start (A "Realism: <mode> (your battle preference, for both players). ...", B "... (<A>'s battle preference, for both players; yours isn't used). ..."), and at Realism 1 the first division report on screen said once on each copy why *Fire torpedoes* is off ("Fire torpedoes is off: it needs the battle preference Realism at Captain's mode, ...", `ls realism` noted=1). (g) the division report's speed write: `lockstep_ui` k), on both copies.) h) auto-run per player through the bridge (`ls autorun`): B on and off, A on (sim 2), a resync with A on auto-run (kept), A off, B "home" (forces ordered to the closest friendly base: `auto-run home ... (<n> forces ordered ...)`), equal minutes; every off as in single player (engine level 12: `side s back to its player as in single player: ...` alike on both, the divisions on the AI equal on both). i) a killed client restarted (engine level 8): killed at a boundary, A drops it ("Captain disconnected"), remembers the battle (`ls rejoinable` on=1), runs 3 minutes alone; the client started again joins: the host gives nation 1, its month and the battle back (`rejoin: Captain is back as nation 1: live battle <bid> starts again ...`), A starts it again at the next boundary (a new epoch, `sent (..., rejoin)`), B accepts it, both chats, `ls rejoinable` on=0; then `ll_rejoined_checks` (same epoch, side 1 its player's on both, 3 minutes equal). j) both players on auto-run: the battle runs on its own at the fastest speed (fastest 1 on both), a take-back pauses it, then to the game's own end (its end question answered `6 (both sides on auto-run)` on both copies, or its length reached: "... scenario length is reached! The scenario is over!"): both screens closed, no month, the marker deleted; B's result box shows its own side (engine level 12, `result_box_side`: "the result box shows this player's side: ..." with the victory from B's side and the points swapped). k) every minute of the battle (the client's log before its kill and after its restart): `date=` equal on both and the campaign's own (`ident=` and every live load's identification line: the scan's `ident_scan_checks`, also over the client's two runs); no "Log lines out of sync!" box named in a pause or a chat line (engine level 12: the battle log's two lists aligned at every live load; quietly answered ones counted in a NOTE); after-checks; no lockstep error, no LEAK in any log of the copies | ~75 | 0 | after `lockstep_ui`; one battle, 166 s on dist_ll_11 before its fixes (~4 min estimated). Group `limits`. Live on dist_ll_11: most parts pass; b), h) and j) fixed in the suite; c) side 0's AI flags changed across the resync on both copies (6 -> 7 of 7 divisions; fixed in engine level 13: `ai0=`). dist_ll_15 (2026-10-18): all pass in 135 s; dist_ll_16 (the full default run): 52/52 in 175 s (j) ended at the battle's length or by the answered end, either is accepted); dist_vp_2 (2026-10-18): the default run 52/52, `--stress` 50/51 (f): the host's division report did not open, its popup menu overshot), after the popup fixes (7175467) 53/53 |
| `lockstep_data` | 0.6.1, engine level 18: the game's temporary files stay out of the live battles' data-file check (`scenario_lockstep_data`; groups `limits` and `live-limits`; the 0.6.0 bug: the ship designer rewrites `Data\tmpShip.ttp` whenever it is used, so after one player designed a ship every live battle of the session was fought against the computer, "game data files differ ...: Data\tmpShip.ttp"). a) both copies' scan line `data files: <n> files of Data (but Data\Graphics and the game's temporary files tmp*: <names>) hashed ...`: the names are the game folder's `tmp*` files (any level of `Data`, Graphics aside), n the other files, the same hash on both and in `ls x87` (`data=`). b) B `ls testdatafile Data\tmpShip.ttp;Data\IDes\tmpProbe.sdf`: B's hash unchanged, its log "(not hashed, the game's temporary files: ...)"; a live test battle starts live (B "the game data files are the same on both PCs", no "differ" or "not compared" line), 4 minutes equal, the game ends it inside a minute (`ls endsoon`), no month. c) the control: `ls testdatafile Data\Gundata.dat` on B changes its hash, `ls testdatafile off` puts it back. | 6 + the log scan | 0 | the real two-folder difference is the developers' driver (Two-PC conditions, "The game's temporary files"). Skipped on builds older than 0.6.1. dist_i61_1 (2026-10-25, `--only session,relations_war,lockstep_torpedo,lockstep_ui,lockstep_pvp,twopc,lockstep_data` 200/200 in 828 s): 11/11 in 13 s (1415 files, `Data\tmpMapData.dat` and `Data\tmpShip.ttp` left out). |
| `lockstep_pvp` | a live battle between two joined players, three copies (from ls_pvp.py). a) a third copy (bridge 47703, slot 76, battle slot 86) joins as "Bot": the host gives it the reserved nation 2 and its month (`state seq N -> Bot (nation 2, ...`), it plans nation 2 in slot 76, the host lists `Captain(1,b,ls)` and `Bot(2,b,ls)`. Setup: war 1-2 (`settension 1 2 50`). b) `battles injectpvp 1 2 Northern Europe` before each month (up to 3; the attacker's CheckAttackLocation often finds "no battle came about", the game also finds 1-2 battles itself) until a live battle runs (`ls_month` with K2: both clients submit): the request "fought live by <p1> and <p2>", "battle N sent to <p> (B bytes), live against <q>", the relay ended with the result, "battle N applied: ..."; the attacker's copy as A in its battle slot (StartPlayerBattle, epoch 1 sent), the defender's as B (loaded from slot 86/87, left the battle slot afterwards); every per-minute hash equal; an order of B the same on both. Default: 10 live minutes, A drops B, A fights on alone on auto-run and sends "battle N: fought". --stress: to the agreed end (>= 400 minutes) on both, or, after the game refused the agreed end ("Forced battle end not allowed!" while enemies are near), the game's own end at the scenario length (dist_vp_2: minute 793, as `lockstep_month`'s `agree` accepts it), plus h) an idea-1 battle of nation 1 against an AI nation injected into the same month: run before or after the live one, never at the same time, and settled. c) the new month on all three copies, ship counts of nations 1 and 2 equal on the three. f) the war score of the war 1-2 (host `warvp 1 2`, read before and after the month with the live battle(s)) rose by the VP of every live battle 1-2 merged that month (`battle N applied: ... VP a/b`, a = the battle's own nation K, the first player's, as in `battle request N: nation K vs J ..., fought live by ...`; mapped onto 1, 2) plus the host's own credits of that month (`war score 1-2: +x/+y -> A:B` of its battle hooks, e.g. the game's resolution of an unfought request; since 0.6.0 also its `victory points: nation G +v (<why>, in its war with nation L) -> t, war score 1-2 x:y` lines, e.g. B5's blockade VP: `WAR_VP_CREDIT_RE`, dist_rc060_2; a fix's credit line for a merged battle, such as `... from a battle fought on a player's PC -> A:B`, is not added twice); still at war, the score not restarted (`starts at`/`first battle`); a `SKIP-note` when the live battles gave 0 VP (then it can't tell). Fails on 0.3.0, passes on 0.3.1 (MODLOG 2026-10-16). d) peace 1-2, the third copy leaves and is closed, nation 2 stays reserved for Bot; after-checks; e) no lockstep error in the three logs, no LEAK (the leak check is on in all three copies). Campaign divisions (0.6.1, a docs question): before the war month the client keeps its two from `divisions` (else it makes "RT K Pvp") and the third copy makes "RT Bot Div" (`pvp_divs_setup`, the game's own dialogs); at the first live battle 1-2's load the battle's division list (`ls divs`: index, side, name) is the same on both players' copies (a check), and `NOTE lockstep_pvp divisions: CONFIRMED|REFUTED|INCONCLUSIVE: ...` says whose campaign divisions are in the battle and on which side (not a failing check) | 15 + 9 per processed month (8 month checks + "advanced by exactly one month"): usually 24-42 (+1 with --stress) | 1-3 | needs `session`'s Bot reservation; Game76/86 must not exist before the run and are deleted after it. c06860f: full run 22/22 (1 month, battle 17: Captain A, Bot B, 10 minutes equal, A finished alone, merged); --stress 23/23 (1 month, two live battles 9 and 11 with the roles both ways, each 424 minutes equal, ended by the game's proposal + both End, plus the idea-1 battle). Released 0.3.0, `--only lockstep_pvp` (2026-10-16): 22/23, f) fails: battle 1 merged with VP 180/147, `warvp 1 2` 0:0 before and after. 0.3.1: `--only lockstep_pvp` 40/40 (2 months; battle 4 with Bot as A, VP 0/230: `warvp 1 2` 4:13 -> 239:26 = +230:0 plus the game's +0:1 and +5:12); full run 23/23 (battle 9, VP 518/193: 0:0 -> 518:193); dist_ll_16 (2026-10-18): the default variant 26/26 in 114 s, the long one 36/36 in 235 s; dist_vp_2 (2026-10-18): the default variant 26/26, the long one 26/27 (b): the game refused the agreed end 3 times and ended the battle at its length, minute 793; accepted since b0e6a23); dist_rc061_1 (2026-10-26): the default variant 30/30 (before the divisions check), the long one 32/32 with `NOTE lockstep_pvp divisions: CONFIRMED` (battle 28, A = the client on side 0: its "RT K Div 2R" and "RT K Div 1" are battle divisions of side 0 on both copies; B's "RT Bot Div" is nowhere, B's side has the game's own divisions; 26 divisions equal on both copies) |
| `lockstep_month` | **the host's battle against the joined player inside a REAL processed month** (CheckForBattles2 -> DoBattle during the host's turn -> lockstep -> end -> FormClose -> DoStrategicTurn -> DoEndOfTurn -> idea-1 battle phase -> the new month to the client). Setup: the host at war with nation 1 (`diplo war 1` + applydiplo). Months (`ls_month`, up to 8; the client sets nation 1 ResearchPct before each submit) until a host battle against nation 1 runs live; per battle: the host's preview, held, sent, started, running at minute 0 in this order after "host turn: 1 submissions merged", `ls session` hostOpp=1 testSlotMode=0 slotMode=0; one DoEndOfTurn, the date one month on; the client loaded it from slot 87 as B; every per-minute hash equal, the client's order the same on both; the end (default `drop12`: 12 live minutes, the host drops the client and fights on against the AI on auto-run; --stress battle 1 `agree`: the end at >= 400 minutes, by agreement or (after a refused agreed end) by the game itself, battle 2 `endsoon` (0.3.0): after 6 live minutes A sends `ls endsoon` and the GAME ends the battle inside the next minute ("TEST scenario length a -> b: the game ends the battle at the end of minute M" and "Scenario absolute time limit is reached!" on both, over at minute M); for both: both copies report it as the normal end ("The battle is over (...)"), not "the battle screen was closed", and the host logs no "WARNING ... handled inside a simulation step"; the deferred close (battle 2, and any battle whose results' "Exit battle" came inside a minute): on both copies `"Exit battle" in the results: the battle screen closes after this minute` -> "The battle is over" -> "live battle ended (normal)" -> `closing the battle screen ("Exit battle" in the results)` in this order, the host's month after it with the game's own dialogs answered by the test ("Turn messages") and no minute-box/"Battle: ..."/WARNING line between the close and "DoEndOfTurn finished" (on c06860f the month ran inside the battle's simulation step)); the month completed and reached the client once (one `state seq N -> Captain (nation 1,` line, no "turn did not advance"), host and client in the new month, live battle and slot mode off; the campaign (`battle_effects`: the live battle's save in slot 1 at its start vs the host's save at the battle phase start (after DoEndOfTurn, read by `StageWatcher`; the host's save after the month when no battle phase ran): ships of both nations took part (battle star / "Took part" log entry), every warship loss counted in [NationNLosses] has a lost ship (gone, or Fate no longer "XXX", whether it took part or not; MSLost, the month's trade war, is only reported), the step-by-step attribution of the month's losses logged, the client's slot 77 has the same ships, battle fields (RepairTime, BattleStars, log entries, Status, Fate, CrewQuality) and Losses for nations 0 and 1; ship counts and VP of 0 and 1 equal on host and client (the "victory points: nation 1 +X in its battle against the host" line if any); the client's ResearchPct of that month on host and client. --stress: h) the second battle's months also carry idea-1 battles of nation 1 against an AI nation (war 1-J, two injected a month): the battle phase ran after the live battle and its battles were sent and settled; h2) each idea-1 merge of that month (saves read at once: as sent -> Game87 -> merged): the merged [Nation1Losses]/[NationJLosses] = the battle slot's and every loss the merge added has a lost ship (the game's resolution of unfought battles after the last merge is reported). Then peace (compromise, the client's "Accept peace" button) and one normal month (no battle); after-checks, error/leak scan | 1 + 9 per `ls_month` + 9 per battle (`drop12`; 10 for `agree`, +1 when its close was deferred; 11 for `endsoon`; +2 interplay h/h2) + 14: 33 with one battle month (default), 57 with --stress and two battle months; since 0.3.2 +1 per battle (the campaign date: recorded when the battle opened, `the month runs: ... the record is dropped`, the next month with the same day), +1 per battle (919b40d: the aftermath: every nation's VP the same in the host's memory, its saved slot 1 and the client's month package; since engine level 10 every live load on the host keeps the month's own date (`the load: campaign date ... kept (the battle save said ...)` when the battle's own `.sac` date differs) and no `the battle's results: campaign date ... put back` line; on a VP difference `vp_mismatch_probe` logs its findings) and +1 (`ls_log_scan`'s chat lines): 36 with one battle month; 70 in a `--stress` run with two; since 0.4.0 +1 per battle (`vp_reload_check`: a blockade of nation 1 made on purpose, nation 0's `BlockadeModifier` 1000 saved on both copies; each copy loads its own slot twice, and after every load each nation's VP and EnemyVP equal the file's and the mod put back the load's blockade credit) and +1 after the peace month (every nation's VP the same in the host's memory, its slot 1 and the client's memory): 40 with one battle month on 882fa4c's runner, +3 with --stress | 2-9 | `drop12` in the default run (the agreed end (5-8 min) and `endsoon` (~1.5 min) only with --stress). a1a8484 (0.3.0): `--only lockstep_month --stress` 65/65 (Aug 1900 battle 100000 agree 417 minutes, its close deferred; Sep 1900 battle 100001 endsoon at minute 6 -> over at 7; idea-1 battles 1 and 2 merged with no losses; the game's resolution of battle 3 sank DD V2: DDLost 0 -> 1 after the last merge). c06860f: full run 32/33 (Nov 1901: battle 100001, 12 minutes equal, "victory points: nation 1 +198 in its battle against the host"; the failure was MSLost, fixed); `--only lockstep_month,lockstep_stress --stress` 53/53 (Aug 1900 battle 100000: 424 minutes, agreed after the game's "far apart" proposal; Sep 1900 battle 100001: 451 minutes, the game's proposal agreed, Nymphe sunk, +15 VP, idea-1 battle phase after it); full --stress run 51/53 (499 and 529 minutes equal, both ended by the game inside a minute: the host's month nested in the simulation step); dist_ll_16 (the full default run, 2026-10-18): 36/38, the open VP difference after the `drop12` battle again (the host's side scored 0): the files agree (1402 for nation 0), the client's memory has 1462, and `vp_mismatch_probe` found that each copy's own reload adds points to nation 0 (the host's `load 1 0` 1402 -> 1502, the client's `load 77 1` 1462 -> 1482): the game's load path, a product issue open since 0.3.2; the long variant (`--stress`) 64/64 in 706 s (the agreed end refused 19 times, the game ended the battle at minute 980); dist_vp_1 (0.4.0, the VP fix): `--only lockstep,lockstep_pvp,lockstep_month` with lockstep_month 38/38 (Dec 1900 `drop12`; the reloads put back 554/534 -> 474 on the host and 554 -> 474 twice on the client) and `--only lockstep_month --stress` 73/73 (`agree` and `endsoon`, every reload equal to the files); after the merge of live-limits (882fa4c's runner) `--only lockstep_month` 40/40; dist_vp_2 (2026-10-18, the guard only in sessions): the default run 40/40 (no VP difference after `drop12` any more), `--stress` 67/67 twice (battle 1 `agree` ended by agreement at minute 514 after 3 refusals; battle 2 `endsoon`; `vp_reload_check` after both). 0.4.1: +3 `relview_check`s, the client's screen with the war 0-1 (slot 1 "You are at war with Great Britain" and the war mark, "At war with Great Britain."), the host's (slot 1 Germany at war) and the client's after the peace month: 43 with one battle month (dist_rf_2, 2026-10-20: 43/43) |
| `twopc` | live battles under two-PC conditions (0.4.1, engine level 15; group `twopc`; the default run: short, `--stress`: full; [Two-PC conditions](#two-pc-conditions)). a) B (the client) leaves, is closed (its PID) and started again with `RTW3MP_TEST_LOCALE=comma`; the shared Preferences file holds B's variant from then on to the scenario's end (the runner backed it up): every battle preference a live battle takes from A (the 14 of `kPrefs`) differs from A's `ls prefs`, and 22 display keys do (DarkMode, Scale, SoundVolume, FOW, EnglishRanks, ConvertCharacters, the break and message options, ShipFlags, ..., MainFormWidth x MainFormHeight 1400x880); B joins through `tools/net_delay_proxy.py` on 47634 (default 120 ms +-60 ms, 4000 kbit/s; `--stress` 200 +-100 ms, 2000 kbit/s): `TEST: RTW3MP_TEST_LOCALE=comma` in B's log, B's `ls prefs` its variant, A's unchanged. b) a test battle (`pocbattle 1 0`): READY (`all` and `ident`) equal, no "the loaded battles differ"; B's "battle preferences: Admiral's for the load and the simulation (<the 14: B's -> A's>)", B's `ls prefs` A's 14 with its own display ones and its own 14 kept for after the battle; "the battle screen shows side 1's ensign"; the maps (`twopc_map_check`): B's `ls mapview` view 1 = PlayerSide 1, every B ship afloat shown (of human forces), no A division flag, no more A wakes than A ships shown, and of A's ships exactly those B's side sights in the simulation's state (B's DESYNC report by `ls desyncreport`, read and deleted: SightingLevel or RadarSightingLevel > 0 and SeenByHumanControlledShips; right after a load the levels alone); A's map the mirror (A's computer forces' own ships shown once spotted, the game's rule); the windows differ in size at the load and are resized with SetWindowPos (A 900x650, B 1560x980); every division on the AI, 6 minutes (`--stress` 10, then the negative control: `ls mapview off` on B, a minute, the check finds A's side on B's map, `ls mapview on` gives B's own back; the battle screen's Preferences on B with OK: the 14 stay A's, B's file unchanged; 5 more minutes), a resync (the maps again), 2 minutes; every minute's hash equal; the end (default `ls endsoon`: the game ends it inside a minute on both; `--stress`: A drops B, the host finishes on auto-run); B's "this PC's own battle preferences again (<the 14: A's -> B's>)" and `ls prefs` its own; B's battle window during each load (`CoverWatch`, every ~3 ms: hidden, clear = layered alpha 0, shown) never shown opaque while the load's picture could be in it. c) war 0-1, real months (up to 4, `--stress` 6) until CheckForBattles2 starts the host's battle against nation 1 (`ls_month` with `live_hook`): the same start, preference, ensign, map and size checks at its start; `drop12` (`--stress`: 10 minutes, a resync, 2 minutes, then `ls endsoon`) with the maps before its end; `host_battle_checks` (lockstep_month's: the order of the start, one DoEndOfTurn, the minutes equal, the drop or the game's end, the month to the client, ships, losses, VP, the client's plans); B's own preferences back; B's load never shown; peace (`--stress`: and a normal month). d) the shared file still B's variant byte for byte (nothing wrote B's own Preferences file: neither B with A's battle preferences in memory nor A), B's and A's `ls prefs` their own; `ls_after_checks`; `ls_log_scan` (no LEAK with `ls leakcheck 1` on both, no lockstep error, the identification at every load); the proxy carried B's session (towards B >= 1.5 MB per live start plus the join's package; the bandwidth cap's largest wait > 1 s). e) the run's own conditions again: B leaves, the proxy stops, the run's Preferences back (hash), B started again without the switch and in the session directly, the host's battle window its own size | 57 (short) / 77 (`--stress`), with one month battle | 1 (`--stress` 2; one more per month without the host's battle) | dist_tp_1 (2026-10-20): `--group twopc` 68/68 (57/57, 175 s) and `--group twopc --stress` 88/88 (77/77, 201 s); the first run 65/68 on two assumptions of the suite (MODLOG). The client's logs of its two restarts kept (`rtw3mp_47702.twopc<N>.log`: the second is B's log under two-PC conditions); `RTW3_SHOTS=<folder>`: PrintWindow captures of both battle windows at each map check |
| `lockstep_rejoin` | returns to a live battle (engine level 8; group `rejoin`, `--stress`; from the stage B proof). One 1900 test battle: a) the client leaves (`leave`): A drops it and remembers the battle (`rejoin_dropped`), runs 2 minutes alone, the client joins again: the battle back (`rejoin_return`: host line, A's restart at the boundary, B's accept, both chats, the identification kept, 3 minutes equal). b) it leaves again and joins while A runs a slow minute alone: A waits for that minute's boundary (`rejoin: waiting for the minute boundary (a minute runs ...)`, `the next minute is held`; or, when RejoinTick starts the battle right at that boundary before the minute gate's line, the start at the minute after the waited one), then the return. c) another engine level: the client killed and started with `RTW3MP_TEST_LS_LEVEL=<level-1>` (`ls level` announced): it joins and plans nation 1, the host logs its level (`Captain: mod ..., live-battle engine <l> (this host: <L>)`, `ls session` levels) and refuses the return with both levels (engine level 12: `rejoin: Captain (nation 1): Captain's mod plays live battles at level <l>, this host's at level <L> (both mod <v>, but different builds): live battles need the same level on both PCs`), the host's chat "* Captain is back, but the computer keeps commanding <nation> in the live battle: <the same>." and the player's notice "* Your live battle against <nation> goes on without you: the computer keeps commanding <nation> (your mod plays live battles at level <l>, the host's at level <L> (...): live battles need the same level on both PCs).", A keeps the battle for the player; killed and started again at this level: the same battle rejoined. d) a return after the battle's end: the client killed, A finishes alone ("battle <bid> can no longer be rejoined (...)", the marker deleted, `ls rejoinable` on=0); the client started again joins: `no live battle of this nation goes on without its player`, it plans its month. e) a relayed battle between two joined players (a third copy "Bot", war 1-2, `battles injectpvp 2 1`, up to 3 real months, `ls_drive_live` mode `relayrejoin`): B (Captain's PC) killed in a minute, started again and joined: the host asks A's PC to start the relayed battle again (`rejoin: Captain is back as nation 1: Bot's PC is asked to start relayed battle <id> again`), rejoined, 3 minutes equal; B killed again, A finishes alone and keeps its results up: B's return refused by A (`norejoin`: the host's `lockstep relay <id>: Bot's PC could not start the battle again: ...`, B's notice "Your live battle could not be started again (<why>): the computer keeps commanding your ships." with the reason in B's words, never "this PC" (engine level 12: `why2`)), the result merged by the host, peace 1-2, the third copy closed. f) after-checks; no lockstep error, no LEAK in any run of the copies | ~40 | 1 (up to 3) | 831 s on dist_ll_11 (42/44: a) a race with the client's leave lines and e) A's results closed during B's restart, both fixed in the suite, not yet re-run); dist_ll_16 (2026-10-18): 38/38 in 563 s, after one more suite fix (e): A finishing alone was stopped by a game box at 17:47 and waited without `pocrun`, so B rejoined the unfinished battle); dist_vp_2 (2026-10-18, `--stress`): 38/38 |
| `lockstep_resume` | the host's game killed in its live battle and started again (phase 5b; group `rejoin`, `--stress`). a) setup: a processed month (slot 1 without a battle in progress), a copy of slot 1 kept outside the save folder (`slot1_snapshot`). b) a test battle; the host killed in a minute (taskkill /F): the client leaves the battle and is back in slot 77; the marker of slot 1 (`rtw3mp_livebattle.ini`) on disk for that battle and epoch, slotmode 1. c) the host started again, `load 1 0`: "a battle in progress was resumed from a save", "resume: a test battle outside a month: battle slot mode on", the campaign date kept, `resume: battle <bid> (epoch <e>) of slot 1 resumed at minute <m> ...: held until Captain rejoins or Run is pressed`; `ls resumable` on=1 held=1 slotMode=1 marker=match; no minute runs while held. d) `host`: "the session takes the month up" (`status` phase processing, `ls session` turn=1); the client joins and gets the resumed battle back (`rejoin: Captain is back as nation 1: live battle <bid> starts again ... (resumed from the host's save)`, A's start "(resumed from the host's save, held until now)", B's accept "(the host's game was restarted)", `ls resumable` on=0, the marker follows the new epoch, 3 minutes equal). e) a second crash: resumed again (epoch of the marker), Run (`pocrun 3`) releases it against the computer (`resume: Run pressed: battle <bid> goes on against the computer`, held=0 rejoinable=1), hosting again, the rejoin. f) both on auto-run to the end of the resumed battle: `resume: marker of slot 1 deleted (the live battle is over)`, `battle slot: DoStrategicTurn skipped`, the session resends the month, the client plans nation 1. g) the host started with `RTW3MP_TEST_LS_LEVEL=<level-1>` (slot 1 put back from the copy), a new session, `pocbattle 1 0`: the client refuses the start (`battle <bid> refused: Admiral's mod plays live battles at level <l>, this PC's at level <L> (a live battle needs the same level on both PCs: the same mod version)`, its chat "The live battle with Admiral could not start: ..."), the host drops it and finishes alone in battle slot mode. h) the game's own battle save: without a session `pocbattle 1 0` (not live), 3 minutes, the battle screen's Save: no marker; started again, `load 1 0`: only the game's own resume ("... side 1 is the computer's"), nothing held, `ls resumable` on=0 marker=none, Run runs at once. i) slot 1 put back from the copy, the host at this level, a new session, a normal month; after-checks; no lockstep error, no LEAK in any run | ~25 | 2 | ~15 min (estimate); the runner deletes its copy of slot 1. Live on dist_ll_11: a)-f) pass (460 s to e)); g) hung there: the host's restart lost its `RTW3MP_TEST_LS_LEVEL` (`ls_relaunch` passed the environment to the client only; fixed), so the battle started normally; h), i) not yet run live; dist_ll_16 (2026-10-18): 37/37, a)-i) in 378 s; dist_vp_2 (2026-10-18, `--stress`): 37/37 |
| `lockstep_torpedo` | manual torpedo salvoes (engine level 5; group `torpedo`, `--stress`; the runner sets RealismSetting=2 for the run, SKIP when the run's setting is below 2). One 1900 test battle, every division on the AI until the fleets are in torpedo range: a) the realism requirement met: *Fire torpedoes* enabled on both copies (0.6.1: both chats named the battle's Realism, Captain's mode, at the start; no "Fire torpedoes is off" notice). b)-e) one boundary after the other (2 minutes apart, up to 60), as the firing solutions come (`torp_here`: every division of a side tries at this boundary, the battle does not run): the first boundary with a solution on both sides gives d), one of B alone b), one of A alone c); e) follows the first release. b) a salvo of B through the torpedo dialog (`ls torp <division> - first`: the division report, Fire torpedoes, the target and mount lists, Fire): "Torpedo salvo ordered; it fires when the next minute starts.", `ls pending` lists it; a second order for that mount not queued (the mod's "A torpedo salvo from that mount is ordered already; ...", or the dialog's "No solution" for every mount after the first order); the release (`torp_release_check`): both copies log the same `deferred torpedo salvo of side s carried out at minute m (given for minute m): <ship> mount <i> ...: <n> torpedoes in the water (<a> -> <b> left, reload <r>)` lines, the giver's chat "Torpedo salvo fired: ...", `ls mounts` of the ship identical on both, the minute equal, no resync. c) a salvo of A alone. d) salvos of A and B at one boundary with `ls trace 1` on both: side 0's first; the trace of the release's minute: the same draws at the same call sites on both (`trace_compare`; the frame markers' state-hash bits counted apart), the draws inside the release's shared sections equal. e) right after the first release, a resync with its torpedoes in the water (`ls hash` torps= on both, the same after the load), 5 minutes equal. f) a salvo order that arrives after A's Run (B's `ls torp` and A's `ls run 1` at once, 3 tries): dropped on both copies (`... not carried out at minute <m+1>: it was meant for minute <m> and arrived too late`), B's chat; a NOTE when the race was not provoked. g) auto-run (engine level 12): a salvo of B ordered before its own auto-run order in the same pause fires first (the release keeps the order given: `deferred torpedo salvo of side 1 carried out ...` before `auto-run on for side 1 ...`, alike on both copies); with B on auto-run, carried out or only ordered (pending), its salvo is refused at *Fire torpedoes* ("Your ships are on auto-run: the computer fires their torpedoes and missiles."), nothing queued. h) A drops B; the trace files deleted; no lockstep error, no LEAK | ~16 | 0 | ~6 min (the 1900 fleets meet late). dist_ll_15 (2026-10-18): 16/18 in 358 s: d) and e) not provoked in their old order (no boundary with a solution on both sides after b) and c), no torpedo in the water at minute 151): b)-e) take the boundaries as they come since 70ce2ea; g) all pass; dist_ll_16 (2026-10-18): 19/19 in 465 s, b)-e) all provoked; the re-run after the stray-dialog fix: 19/19, 609 s, no help needed (f) a NOTE: the late order not provoked); dist_vp_2 (2026-10-18, `--stress`): 19/19 in 346 s |
| `lockstep_stress` | `--stress` only. Every case is an `ls_case` (a failure is one check and `ls_recover` cleans up; the scenario goes on) and ends with the client back in slot 77. Test battles (`pocbattle 1 0`): S1 d) spam 40 s each of run/pause/speed, of orders (the host runs a minute every 1.2 s) and of both, from both copies (~2.5 commands a second each): no DESYNC, no clock difference ("release of minute N at minute N+1"), no resync, no drop, no error reply (an order to a division that is gone is not one), both idle at one boundary afterwards and 3 more minutes equal; e) both `ls end` during a resync (live, in sync afterwards) and right after a drop (no hang; the client back). S2 e) both `ls end` while the client loads the battle (starts and runs in sync); the client leaves (`ls abort`). S3 c) counter reset: 2 desyncs, 12 good minutes reset `resyncs` 2 -> 0, 3 more stay live, the 4th: "dropping Captain: the battles could not be kept in sync"; storms on B and on A (a new battle each): 4 injected desyncs, each caught in its own minute (4 DESYNC lines, the RNG part of the desynced minute reported), the minute after each resync equal on both including the RNG part (3/3), the 4th -> the AI takes side 1, the host goes on alone, the client back, no hang. S4 a silent client (`ls mute 1`, timeouts 12 s): dropped after 12 s, the client leaves; loadto: a client muted from the start never loads: the host gives up after its 15-s load timeout ("the battle was not loaded within 15 s") and goes on alone, the client never left slot 77. S5 a silent host: the client leaves after 12 s. S7 robust: `ls throw` and `ls stall` on the client and on the host each end in a resync (the minute marked failed), `ls nest 8` + `ls resync` waits for the outermost message loop on the client (load) and on the host (send), live afterwards, 3 minutes equal; the guard's negative control (`ls guard 0` + `ls dispwrite` on the client): DESYNC + resync, the leak check logs LEAK (excused), the guard back on at the DESYNC: 2 minutes equal, no later LEAK; S7 drops the client at its end. S8 (0.3.3) the division report's natural case: every division on the AI, 45 live minutes in runs of 3, at each boundary each copy opens the report of its next division (its ship tree's divisions in turn, `open_divreport(pid, k)`) and closes it: every minute equal, no DESYNC, no LEAK; the clamps put back (`the division report lowered ...`) reported; the game refuses the report of a division under the AI on the host's side ("Division is under AI control!", counted). g) private memory of both copies after each of these 10 battles: average growth <= 25 MB a battle (reported per battle); the campaign date (0.3.2 d99e357): A's last snapshot in slot 1 still has the month's own [General] date, every battle logged `campaign date recorded: <that date>`, host and client in the same month (each case's `pocbattle` replaces the host's open battle screen without its close); no lockstep error, chat lines without a file path, no LEAK. S6 a) the client killed (taskkill /F) at a boundary: the host drops it at once ("Captain disconnected"), its battle goes on 2 minutes; the client started again joins while the host's battle is open: since engine level 8 it gets the live battle back (`rejoin_return`: the host's `rejoin: ... starts again`, A's new epoch, B's accept, 3 minutes equal; before 0.6.0 it got nation 1 and its month while the host finished alone, `rejoin_back`, failed on c06860f), then the host drops it again (`ls drop`): the client back in slot 77 planning nation 1 with its month, the host finishes alone (`can no longer be rejoined` at the end). f) a test battle with every division on the AI at speed 5 to its end (>= 400 minutes: the game's end, its proposal agreed, or the agreed end after minute 1200; after a refused end 30 minutes with `ls_run_total`, up to 20 end requests), every minute equal, both screens closed, a normal month. Real months (host at war with 1): P1 i) the client with `fightbattles 0`: the host's battle against it "fought against the AI (the player doesn't fight battles)", no lockstep, month completes; P2 a) the client killed in the middle of a minute of the host's live battle: dropped within 60 s, the host's battle (auto-run) and month go on; the client started again (RTW3MP_LIVE_BATTLES=0) joins at once and must get its nation (the same evidence) and the new month without claiming (failed on c06860f); its month loads; P3 i) that client (`ls session` enabled=0, host `Captain(1,b)`): "fought against the AI (no live battles on the player's PC)"; P4 live again, peace, a normal month. b) the host killed during a minute of a live battle: the client leaves the battle within 60 s, is back in slot 77, not stuck (slot mode off, strategic screen enabled, Turn button visible and enabled), `leave`; slot 1 put back from the backup, the host started again, a new session (join, claim, start), one month | ~90 + 9 per `ls_month` + 8 per `run_month` (~150 in all) | 6-15 | last. c06860f: S1-S5, S7, f), P1, P3, P4, b) pass; S6 a) and P2 a) fail (the rejoin issue). a1a8484 (0.3.0): 90/90; S6 a) and P2 a) pass, both by the hello (myNation 1 during the battle, phase planning / processing); 0.3.2 d99e357: 92/93, the fail the new g) date check (a `pocbattle` that replaces an open battle screen records the previous battle's day: MODLOG, Testing 2026-10-17); 05b81f6: 93/93 (the chained battles keep the recorded date); dist_ll_16 (2026-10-18, `--stress`): 101/101 in 24 min (S7's negative-control DESYNC reports deleted since 3a33cf8); dist_vp_2 (2026-10-18): 100/101 (S8 opened 1 and 3 of 15 reports: popup menus and battery nodes), after 6bfca49/7175467 101/101 (S8 15 of 15 on both copies) |
| `lockstep_air` | live battles with aircraft, on the 1935 test campaign (save slot 9, [below](#the-1935-test-campaign)); without it `SKIP lockstep_air: <why>`. Runs after the slot-1 scenarios: the session moves to that campaign. a) client and host leave, the host loads slot 9 as nation 0 and hosts, the client claims France (nation 2) and plans it in slot 77. b) `settension 0 2 50`, `ls autostrike 0` (the host option "Computer flies carrier strikes" off; the original put back at j)), `pocbattle 2 0` (Fleet battle at Plymouth): both running, A verified the start, the same air formations per side on both, both chats "Air strikes: launch your carriers' strikes with "Order an airstrike" while the battle is paused; ...", `ls airlist` auto=0. c) `ls airwin show` on each copy: each window its own side (0 < rows <= af0 / af1, no base in both lists), again after a minute. d) routine air ops (engine level 6, `airops_part`): search sectors and CAP through `ls airops` from A alone, from B alone and from both at one boundary: the order batch, at the release `ls airforce` of each force identical on both copies (the settings; the count-downs run on); the other side's force refused (`That force is ...'s: you can set the routine air operations of your own forces only.`; through the bridge and through the tree's toolbar click). e) `ls resync` with the windows open and night CAP on: the windows hidden before the load (ok), the same formations per side after it, night CAP as before on both (`night CAP as before the load: on for <n> of <m> forces`, `nightcap_snapshot` equal). f) land CAP as an order (`landcap_order_check`, B then A): a division report's land-CAP button down + OK: a `VLlandcap#<side>` batch, at the release `ls landcap` landcap0/1 equal on both and `land CAP of side s over <division>: <airfield> covers it[; the game alone would have tasked ...]` on both; the own-airfield rule (only the requesting side's airfields cover it); 3 minutes equal. g) strikes through the real dialog, the short variant (`strike_short_part`): Ready (the strike dialog's rows checked through the list view, Ready by BM_CLICK) from A alone, B alone and both at one boundary: `launch from the screen: ... ordered state 2` with the side's own AFId range, replayed on the other copy, `ls airlist` identical, no resync (both: one order of the launches); Stand down of a formation B readied (the game's "Stand down selected squadron(s)?" Yes): it stands down on both, `ls airbase` identical; the dialog's buttons while a minute runs (B): each says "Pause the battle first: ..." once, nothing launched, at the boundary the dialog still open and refilled (fresh=1); the dialog open across a resync (hidden before the load, no crash, open again); `ls airlaunch` from A, B and both. h) every division on the AI, 30 live minutes with both windows open: aircraft in the air on both sides, `ls airlist` identical at every sample, every minute equal. i) `ls resync` with aircraft in the air: the same formations per side afterwards, still in the air, 3 minutes equal. j) A drops B, B back in slot 77 as France; the option put back. k) every load logged `battle data cleared (ok)` and the identification line; the snapshots saved the campaign's own date (Jan 1935) in slot 9's [General], every minute's `date=` that date (`ident=`: the scan's check); no lockstep error, chat lines without a file path, no LEAK. 0.3.3: after every load (a) the host's of slot 9 and the client's month, b) the start, e) and i) the resyncs, j) the client's return to slot 77) each copy holds the [AirUnits] count of the save it loaded and logged that it dropped the units from before the load (`air_after_load`); the READY hash of b), e) and i) (`ls_ready`); b) reports the battle's weather ([Environment] of its .sac) | ~50 | 0 | after the slot-1 scenarios and `air_units`; the runner backs up and restores slot 9. The 0.3.2 expectation of e) (the land-CAP button put up again) failed twice on dist_ll_7: land CAP is an order since engine level 6. 0.3.1 and 0.3.2 up to d042d71: b) failed (the air formations piled up at every load); d99e357: 15/15; 3cc0628: 13/13 in three runs. Live on dist_ll_11: 34/34 (239-245 s, `--group limits` twice); dist_ll_15 (2026-10-18): 40/41: g)'s strike dialog in a running minute refused 2 of its 3 buttons (a disabled button takes no click: only the enabled ones are pressed since 70ce2ea); dist_ll_16 (the full default run): 40/41: g)'s bridge launch "from both" picked B's squadron with no undamaged aircraft left ("ok none ..."): the suite now tries the next squadron; dist_vp_2 (2026-10-18): the default run 35/43 (g) x6: the battle had no carrier division on either side, "No airstrike capable units available!", replaced since 2b44526; the client's game error box "Error in SaveBuildCampaign procedure! Access violation ..." at the start, a product issue that predates 0.4.0, Known traps), `--stress` 41/41 twice (carrier divisions on both sides in the first battle) |
| `air_units` | the campaign's air units no longer double with every load in the same process (0.3.3 8f81c07, live-limits 72a3ef7; default run, groups `airunits` and `air`, before `lockstep_air`; the 1935 test campaign, SKIP without it; main's design). Up to 0.3.2 the game's `LoadAircraftData` appended a save's [AirUnits] to its global list at every load in the same process (the host's reload after every merged month, the client's every month, both at every live battle load): the host's Turn then stopped at "CV <name> aircraft capacity exceeded!". The run's backup of slot 9 copied to the throwaway slot 49 with every [AirUnits] entry doubled (`air_copy`: as 0.3.2 saved it after one reload): a) the host's first load repairs it (`air units: N repeated units of the save dropped (2N -> N; a save of an earlier version)`, `airunits` N); b) `save` writes the repaired list (AirUnitNo N, every AirUnitId once); c) the next load keeps it (`air units: N from before the load dropped; the campaign's N kept`, no repair line); d) a session on the copy (the client claims France, nation 2): `airunits` on both = the saved counts of slots 49 and 77 = N; e) real months with a merged turn (2, `--stress` 3, `run_month(slot=49, checks=False)`: another campaign's months, `OWN_CAMPAIGN`): the host's merge reload and the client's new month log the line, never doubled (`no_doubling`: within 25 %), `airunits` = the saves' counts, no capacity box on either copy (`BOX_SEEN`); f) no air-unit WARNING. Every live battle load (the start, the resyncs, the client's return) is checked by `lockstep_air` (`air_after_load`) | ~13 | 2 (another campaign's) | 70 s on dist_ll_15 (2026-10-18: all pass, 232 units through two merged months); 0.3.3: part of the 596/596 run; dist_ll_16 (the full default run): 10/10 in 61 s; dist_vp_2 (2026-10-18): the default run 10/10, `--stress` 12/12 (three months) |
| `lockstep_strike` | air strikes through the real strike dialog, the long variant (group `air`, `--stress`; the 1935 campaign, SKIP without it; the runner sets the battle preference FloatplaneLaunch=0, Automatic, for the run: with "Ask before launch" no search floatplane flies in a live battle). Battle 1, the host option "Computer flies carrier strikes" off: a) a fresh 1935 session (slot 9 put back from the run's backup: a test battle leaves its results in the campaign); a battle the game starts at 14:00 or later (or before 06:00) is ended at once (`ls endsoon`) and replaced, up to 3 battles (`strike_battle`, a NOTE each): air search finds nothing by night in January. b) the battle; both chats name the option; every force's search sectors toward the enemy (`strike_sectors`: the bearing from the carriers to the enemy's ships from `pocdump` positions, +-25 degrees, the distance + 40 nm), Fastest available until both screens have a report (`strike_report_probe`: the reports list's item count, an owner-drawn list box, and the first item `ls strikedlg <i>` opens; `ls strikedlg` is asynchronous: its logged result counts); a battle without a report on a screen within 150 minutes is replaced once (a NOTE: the forces out of the search's reach, a dark or short day; since 0.4.1 RC2). c) A's strike for a report: a bomber row (else an unready row) with the mission "Naval strike" picked in the list's own popup (`lv_popup_pick`: WM_CONTEXTMENU at the Mission sub-item), its check box, Launch, the dialog's boxes: launched on A (`launch from the screen: ... mission 3`, side 0's AFId range), replayed on B, `ls airlist` identical, no resync. d) B's strike: "Auto select strike", Launch (rows picked by hand when the game selects nothing: a NOTE): launched on B, replayed on A. e) A and B launch in one pause: both launched, B's launches of the boundary in one order (`... formations launched at this boundary put into one order`), no resync. In d) and e) the game's "CV <name> cannot perform flight operations!" (a carrier hit by the airfields' strikes) is retried 5 minutes later, up to 4 times. f) the whole strike cycle (up to 330 minutes, `ls airlist` identical every 3 minutes): every minute equal, no DESYNC (a check of its own); every formation launched from the screen flies (status 6 or later; the cycle runs a minute at a time while one is in the air), at least one attacks (7) unless they reached their target point (within 10 nm) and found nothing there (a `SKIP-note`; the re-run of 2026-10-22: four flew, 6 -> 8, none seen at 7 in 3-minute samples), except one the game takes away before take-off on both copies alike (its carrier hit or no longer able to fly it, the strike called off: a NOTE with its base, states and aircraft, the bases at the end; a side none of whose formations flew: a `SKIP-note`) or one the dark holds on deck at the end (0.4.1 RC2: B's formation readying -> gone, with the hardware maths too); with the option off the computer's strikes of the players' sides come only from airfields (`computer_strikes`: launched formations, with a parent AFId; `ls airbase`; a NOTE when the computer launched none). Battle 2, a fresh session, `ls autostrike 1`: g) both chats name the option and say that "Order an airstrike" still launches the player's own strikes (engine level 12), `ls airlist` auto=1; the strike dialog opened through the tool bar's "Order an airstrike" (sbStrike, two posted clicks; the bridge's `ls strikedlg` doesn't say it) says so once a battle on each copy ("The computer flies both sides' carrier strikes in this battle (...); the strikes you launch here fly as well, besides the computer's."); with the search sectors and every division on the AI (the forces seek contact), within 240 minutes the computer launches a carrier strike, checked when the battle can show one: a report on a screen (`strike_report_probe`) by day (08:00..15:00) with an hour of the window left; a battle without (no contact: the forces out of reach, a dark start, no flying weather; the search aircraft aloft are named) is replaced once (a NOTE, the check `g) the replacement battle runs`), then the check is a `SKIP-note` with the reasons (0.4.1 RC2: no computer strike at all in 240 minutes of a 07:00 battle, with the hardware maths too); every minute equal in each of them (a check of its own). h) the option put back; no lockstep error, no LEAK | ~14 | 0 | ~6 min on dist_ll_16 (D7, 2026-10-18), plus ~1 min per replaced battle. The default run's short variant is `lockstep_air` g). The first dist_ll_16 runs (D1-D6) found only suite issues: the asynchronous `ls strikedlg` taken for a report, item 0 of the reports list, night battles, no search floatplanes with "Ask before launch", the bridge's dialog without the option's note, squadrons counted as the computer's strikes. dist_vp_2 (2026-10-18): skipped in the whole `--stress` run (lockstep_air's battle left slot 9 with a battle in progress; the run's backup is accepted since 6bfca49), then 20/20 after `lockstep_air` in one run (a night battle replaced; its quick end at minute 0 timed out: `ls endsoon` from minute 2 since f893051) |
| `lockstep_missiles` | missiles in live battles (engine level 9; group `missiles`, `--stress`) on the 1958 missile test campaign ([below](#the-1958-missile-test-campaign): Italy hosts, the client claims the Soviet Union; `--missile-fixture <folder>\Game48` copies it into save slot 48 for the run, `--missile-slot` uses a slot as it is; `SKIP lockstep_missiles: <why>` without one). a) the session moves to that campaign. b) `settension 0 3 50`, `pocbattle 3 1 1` (the Cruiser battle in The Mediterranean): running on both copies, missile ships on both sides (`ls missilestat`); the start chat once on each copy (engine level 12: "Missiles: "Fire at will" on the missile panel is each player's own (...); "Manual fire" works while the battle is paused; "Fire mode" is the computer's for both sides (the game applies it to one side only)."). c) the panel shows your own side (`panel_check`): "Fire mode" disabled with its hint (`ls ctl`'s last field: "Fire mode: the game applies it to one side only, so in a live battle the computer sets the salvo size of both sides' automatic fire alike. ..."), "Fire at will" enabled with its hint and each copy's own side's setting (faw 11). Then, in the order they run (the manual salvoes come before the automatic fire, which empties the launchers): e) each player's own Fire at will at the start: B off (its chat, faw 10 agreed on both, `automatic missile fire of side 1: held (its Fire at will is off)` on both, the SSM aboard its held ships unchanged for 6 minutes), A off (00); then both held through the approach under the computer (`ls_all_ai`, Fastest available, until A can order a salvo): no SSM leaves a held ship of either side, `ls missilestat` identical at every sample, every minute equal. The held ships (`msl_held_ssm`) are those of the side's forces under its player (`ls forces` ai=0, `ls mounts`): a computer-controlled force of a nation below the game's threshold fires whatever the box says, as in single player (`ls missilestat` nations: gate 0 for both 1958 nations; the resolve line's "< 2 fires"), so a side's total SSM can fall while it is held. f) manual salvoes (`ls missiles <division> first 1`, the divisions with SSM aboard read anew at every try; Fire at will off on both) from A (the approach's first), from B and from both at one boundary (`missile_release_check`: the same `deferred missile salvo of side s carried out ...` lines on both, side 0's first, the givers' chats, `ls missilestat` identical; a step whose side has nothing left to fire, every division answering `nothing to fire (no missiles are left in that launcher|the launcher is destroyed)` or no shooter listed with every shooter at 0, is a `SKIP-note` with the replies: `msl_spent`). i) a resync with a pending heavy-missile target (`ls testpmt <ship>`: A's next snapshot carries one, a real one is used up within its minute): A logs the test target, both `1 pending missile target(s) as before the load`, `ls missilestat` pending equal, Fire at will 00 kept, used up in the next minute on both, 3 minutes equal. e) both turn Fire at will on at one boundary (11). d) automatic fire for both sides (up to 450 minutes): both fire (SSM stocks fall or more missiles in the air; a side that fired nothing is tried once by hand at the end: a salvo is a FAIL, every launcher spent a `SKIP-note`, e.g. after the manual salvos and the both-step's lone ones emptied it; at least one side fires), `ls missilestat` (stocks, pending targets, missiles in the air, `intercepted shotDown= bySAM= decoyed=`) identical on both copies at every sample, every minute equal, `missile fire under the computer's rules for both sides` on both; side 1's PC logs once that the panel's counts are not updated there. g) a Fire at will click while a minute runs (B off, speed -1): not refused, in effect from the minute after (faw 10 on both). h) a manual salvo while the side is on auto-run is refused ("Your ships are on auto-run: the computer fires their torpedoes and missiles."), auto-run off again. j) A drops B; no lockstep error, no LEAK | ~23 | 0 | ~6 min on dist_ll_16 (2026-10-18: 23/23 PASS, the battle 264 s with g) at speed -3; the approach 56 minutes). The first run (09:40) ran the automatic fire first: Italy's 40 heavy SSM were gone before the manual salvoes, and a side's SSM fell while held (a computer-controlled force): both fixed in the suite; dist_vp_2 (2026-10-18, `--stress`): 24/24 |

Runs of the 0.3.3 hotfix (2026-10-17, v0.3.3's runner; `--dist` = a frozen copy): full default **596/596** (game open
1295 s; before the error-box check: the client had answered "Error in SaveBuildCampaign procedure! Access violation ..."
at `air_units`' first submit and "... Out of memory" twice later, see Known traps; not seen again) and **592/598**
(1294 s; `lockstep_ui` k) host, a runner issue fixed since (the report's pending order); `lockstep_month`'s aftermath VP
check twice, the open issue below; `lockstep_air` f)/g)/i): one DESYNC at minute 14 of the 1935 battle next to "Unknown
ship sighted!", resynced at once: 6da8bfe does not remove every such DESYNC); `--only lockstep_stress,air_units --stress` **112/113** (1963 s; the fail:
`lockstep_stress` f)'s month, see Known traps: a test battle in the next month) after a first run cut short by the same
cause; `--only lockstep_ui,lockstep_month --stress` **105/105** (654 s). The 0.3.2 release under the same runner:
561/594, every 0.3.3 check failing as it should (no air-unit repair, the host's Turn stopped by "CV Furious aircraft
capacity exceeded!", the identification different at READY, no `ls testcurspeed`) plus the open VP check twice. Mixed
versions, with a second game folder outside the Steam library running 0.3.2: a 0.3.2 client with a 0.3.3 host 8/8 (the
join, a month, a 10-minute live battle equal with `ident=` differing, the host's 1935 campaign keeping 232 air units
through a merged month) and a 0.3.3 client with a 0.3.2 host 7/7 (the join, a month, the live battle equal).
Full default runs of the 0.3.2 candidates (2026-10-17, `--dist` = a frozen copy): the final candidate 919b40d
**559/559** (game open 1022 s), and before it 561/567 (959 s) and 565/567 (1110 s): `lockstep_month`'s aftermath VP
check twice (the client's load of a correct package adds 80 or 100 VP to nation 0, open, see MODLOG and Known traps),
`battles` b) twice (a test race, fixed in the runner) and `lockstep_ui` e)+f) once (one DESYNC at minute 22, see MODLOG);
`--only lockstep_month --stress` **70/70** (437 s) and `--only lockstep_stress --stress` **101/101** (1326 s). 05b81f6
**574/575** (game open 1034 s; the fail: `lockstep_month`'s VP check, host 980 vs client 1040 for nation 0, open, see
MODLOG) and `--only lockstep,lockstep_ui,lockstep_month,lockstep_stress --stress` **213/213** (1940 s); d99e357
**556/558** (game open 1141 s; the 2 fails in `treaty` g): nation 1 also gained Norway that month, not by the treaty
and not from nation 2 (the month's own processing gave it; no log line names it), unrelated to the live battles) and
`--only lockstep_ui,lockstep_stress --stress` **127/128** (1561 s; the fail: `lockstep_stress`
g) date check, the chained test battles). 3cc0628: **558/558** (16 processed
months plus the copy's single-player month, game open 1052 s) and earlier **549/550** (1062 s; the fail: the host's month
moved by a live battle's load, `lockstep_pvp` M1 `NOT applied: submission is for another month`, see Known traps);
`--only lockstep_ui,lockstep_stress --stress`: **121/122** (6 months, game open 1531 s; the fail: the same in
`lockstep_stress` f)). With d042d71: 537/540 (926 s; the 3 fails were the mod issues fixed in 3cc0628).
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

## The 1935 test campaign
`lockstep_air` and `lockstep_strike` need a campaign with aircraft; the slot 1 fixture (1900) has none. The 1935 test
campaign is a second fixture, kept outside the repository and used in save slot 9 of the test PC.

- **What it is:** a new 1935 campaign of Great Britain (nation 0; Germany 1, France 2, Soviet Union 3, USA 4, Japan 5,
  Italy 6, China 7, Spain 8), made on 2026-10-16 with the game's own title screen and default options, saved once and
  never played: January 1935, no battle in progress. Its starting fleets put Great Britain's and France's carriers in
  Northern Europe, so `pocbattle 2 0` (France's mission 0, the Fleet battle at Plymouth) has carriers and air stations
  on both sides: 43 air formations, 23 of Great Britain (naval air stations Grimsby and Plymouth, airbase Cork, the
  carriers' squadrons, the battleships' floatplanes) and 20 of France (naval air stations Brest and Dunkerque, the
  carriers Bearn and Painleve).
- **Why Great Britain:** a USA campaign was tried first, but `pocbattle` needs an area: the opponent's build area if it
  is one of the host nation's home areas (or the reverse), else the possession the mission names. The USA has no such
  neighbour, and its war missions against Japan, Great Britain, Germany and France name no possession (`pocbattle: no
  area for mission m against K`); against the Soviet Union only Guam resolved and the game found no battle there.
- **How it was made** (posted messages only): on the title screen `TfrmSelectNation2` a click on `lbTNewGame` (a label
  on the title panel, (100, 470)), then on `lb1935` (on `pn00OR20`, (864, 458)); on the nation panel the nation
  buttons of the map panel (SpeedButton1..10) clicked one by one until the description memo `mmNation` names the
  wanted nation (Great Britain: the third button, at (74, 133)), then `bnOK`; on the slot panel `LB_SETCURSEL` 8 on
  `lbGameSlots` ("Game 9 (empty)") and `bnSlotOK`; the start dialogs answered; then the bridge `save` (it writes only
  the open slot; slots 1 and 2 were compared before and after). The scratch scripts stay in the developers' session
  folder.
- **The pristine copy** is kept outside the repository (on the test PC in the developers' session folder,
  `fixture1935\Game9`). Copy it to `<Documents>\My Games\Rule the Waves 3\Save\Game9` when slot 9 is missing or not as
  made; never commit it and never let a test save into it outside a backed-up run (`pocbattle` alone autosaves into
  the open slot).
- **How the runner uses it:** `fixture1935()` checks slot 9 (start year 1935, January 1935, no battle in progress,
  nation 0 Great Britain, nation 2 France). When `lockstep_air` or `lockstep_strike` is selected and the slot passes,
  the runner copies the slot to `%TEMP%\rtw3mp_regression_backup_Game9` before the launch and puts it back after the
  run, verified by hash (the session start, the battle and every resync write the slot; `lockstep_strike` also puts it
  back from that backup before each of its battles, `strike_session`, and keeps the backup); a leftover backup from an
  interrupted run that differs from the slot makes it refuse to start. Without the fixture both log `SKIP <scenario>:
  <why>` and the summary shows `SKIP`. Nothing else uses slot 9 (`player_copy`'s real "Copy game" button takes the
  highest slot that was free before the run: 8; the missile-era fixture may not use slot 9 in a run with them). The
  fixture has `[AirUnits] AirUnitNo=232` and `[General] IDNo=823`; the 1900 slot 1 fixture has no air units
  (`airunits` -> `ok count=0`).

## The 1958 missile test campaign
Missiles need a 1950s campaign. The missile fixture (stage C live proof, 2026-10-17) is kept outside the repository,
like the 1935 one, in the developers' session folder (`fixture_missile\`, with a README and the scripts that made it).

- **What it is:** the 1935 test campaign played forward in single player to April 1958 (ResearchSpeed 400), then
  turned into a host campaign of **Italy** (nation 0; the joined player takes the **Soviet Union**, nation 3; Great
  Britain is AI nation 6): the mod's own `saveio::MakeNationPlayer` (a console harness that links `saveio.cpp` and
  `util.cpp`) with K = 6, then a scripted edit: Italy's missile ships in the Mediterranean active (14: 11 destroyers and
  the CA Fiume with heavy SSM, the CLs Brindisi and Libia with 4 heavy SSM + 1 medium SAM), the Soviet SAM cruiser
  Chkalov (4 heavy SSM + 2 medium SAM) and 7 Soviet missile cruisers and destroyers moved from the Baltic to the
  Mediterranean (12 Soviet missile ships there), two missile submarines (SubType 4) per side, Italy's carriers' air
  units cut to the game's capacity rule (else the host's Turn stops at "CV Aquila aircraft capacity exceeded!"). A
  variant adds "Improved medium SAM" (a 1970 tech) to both nations: only then can their medium SAMs shoot down missiles
  (in a real 1958 no SAM stops a missile; see GAME-INTERNALS "Missiles").
- **Slot 48:** the host's campaign must be in a regular slot (1..49: the session refuses others as the host's
  campaign); 48 is free and hidden (the title screen lists 1-9). Install it for a backed-up run only and delete it
  after (every battle and month writes it).
- **How the runner uses it:** `--missile-fixture <folder>\Game48` (env `RTW3_MISSILE_FIXTURE`) copies the campaign
  into save slot 48 for the run and deletes the copy afterwards; the preflight refuses to start when slot 48 holds other
  files (never overwritten) or is one the suite writes itself (1, 9 with the air scenarios, 47, 76-80, 86, 87); when it
  holds the same files they are used and put back. `--missile-slot <n>` (env `RTW3_MISSILE_SLOT`) uses a slot that holds
  the campaign as it is: backed up (`rtw3mp_regression_backup_Game<n>` in the temp folder) and put back, verified by
  hash. The defaults fit this campaign: `--missile-nations 0,3` (the host loads it as Italy, the client claims the
  Soviet Union by the name in the save's `[Nation3]`), `--missile-mission 1` and `--missile-area 1`; another
  missile-era campaign needs its own. `missile_fixture()` checks the save before the scenario: [General] Year 1945 or
  later, no battle in progress (ScenarioInProgress -1), both nations named (with the default nations: Italy and the
  Soviet Union); anything else is `SKIP lockstep_missiles: <why>`. `--dry-run` shows the plan. The scenario fails (does
  not skip) when the battle has no missile ships on both sides.
- **A live missile battle:** host `load 48 0`, `host Admiral 47624 0`; client `fightbattles 1`, `ls enable 1`, `join
  Captain 127.0.0.1 47624`, `claim Soviet Union`; host `start`, then `settension 0 3 50` and `pocbattle 3 1 1` (the
  Cruiser battle in The Mediterranean; the two nations share no home area, so the area index is needed: without it the
  mod logs `pocbattle: no area for mission <m> against 3` and no battle starts). Both sides
  have SSM ships, SAM ships when Chkalov and Brindisi/Libia are drawn (most battles), missile submarines in some. The
  battle starts at a random time of day: by day both sides fire within ~25 minutes once every division is on the AI
  (`ls order <i> -1 -1 1` for each own division of each copy), by night after up to ~370 minutes.
- **Driving the panel and the dialog** (posted messages only): the panel (`pnMissiles`) is hidden on every new
  battle screen (A's comes back by itself once its ships have targets in range, B's never: its counts are not updated):
  a posted click at (242, 19) of the tool bar (`sbMissilePanel`) shows it; *Fire at will*: `WM_COMMAND BN_CLICKED` to
  the panel; *Manual fire*: a posted click at (48, 34) of the panel opens `TdlgMissileFire` (modal); its target list:
  `LB_SETCURSEL`, then `WM_COMMAND LBN_SELCHANGE` to the dialog (VCL's Click -> the shooters for that target); the
  shooters: the list view's check boxes (`LVM_SETITEMSTATE` state image 2), rows `ship, range, heavy SSM loaded, medium
  SSM, M/H SAM, force` (missile submarines `SSG <name>` at the end); *Fire one / half / all*: `BM_CLICK`; boxes: "Fire
  medium/heavy SAM as well?" and the summary "<n> missiles fired! [<k> ships with mounts not bearing will turn to fire
  SSM.]". Keep the copy out of the dialog answerer while the dialog is driven (it presses *Close*).
- **Traps:** `LB_GETTEXT` is marshalled by Windows across processes: read list boxes into a local buffer. Before 1959
  the SSM launchers are fixed: most salvoes partly become "turn to fire" pending targets. A test battle in the planning
  phase changes the joined nation's ships on the host only: the player's turn (merged after it) puts back the player's
  [Nation3Ships] (real battles run after the merge).

## Mixed versions
How a release candidate is checked against the released version (0.4.0 against 0.3.3, 2026-10-18). Every copy
loads the `RTW3MP.dll` of the folder its `RTW3.exe` starts from, so two builds need two game folders:
- Copy the game folder outside the Steam library (`robocopy`, ~130 MB; `SteamAppId=2008100` in the environment is
  enough to start it), put the released DLLs into the copy and the candidate into the Steam folder (only while no
  `RTW3.exe` runs; compare the hashes). Both folders share the saves, the logs and the Preferences in Documents.
- A driver starts each copy from its folder (`launch` replaced in the runner's module, so `k2_start` and the other
  helpers start the right build), backs up and restores slot 1, deletes slots 76/77/86/87 and the event cooldown
  files, and closes only its own PIDs. The 0.4.0 driver (`mixed_040.py`, kept with the developers' session files) is
  built on the runner's helpers; `--case a|b|c|d --new-dir <candidate folder> --old-dir <copy>`. Test battles started by
  `pocbattle` are not representative when the host's battle is not live: the battle screen's close then runs the rest
  of a turn (`DoStrategicTurn`, `DoEndOfTurn`), so the host's campaign moves a month on its own. Use real months
  (`ls_war01`, then `ls_month` until the game's CheckForBattles2 starts the battle).
- Afterwards: delete the folder copy and put the released DLLs back into the Steam folder.
- The 0.4.0 results (dist_ll_16 against dist_033):
  - a) A 0.4.0 host with a 0.3.3 player: the join (the host logs `Captain: mod 0.3.3, live-battle engine 1 (this
    host: 14)`), a month, war 0-1: the game's own battle of the host against the player's nation in the next month is
    fought against the computer (`battle against Captain: fought against the AI (live-battle engine level 1, the host's
    14)`), the host's chat "The battle against <nation> is fought against the computer: live battles need the same mod
    version on both PCs: this host has 0.4.0 (live-battle level 14), Captain has 0.3.3 (level 1)." and the player's
    notice "The host fights its battle against <nation> against the computer: ... the host has 0.4.0 (live-battle level
    14), you have 0.3.3 (level 1)."; the month goes on (one month, the player's turn merged), peace and a normal month:
    20/20.
  - b) A 0.3.3 host with two 0.4.0 players at war with each other: their battle (`battles injectpvp 1 2`) is **fought
    live**, the 0.3.3 host relaying it (`battle request 1: nation 1 vs 2 ... fought live by Captain and Bot`, `battle 1
    sent to Captain ..., live against Bot`, `lockstep relay 1 ended: the battle's result arrived`): 10 minutes equal on
    both players' PCs, no DESYNC, the result merged by the host (`battle 1 applied: ...`), the month goes on: 19/19.
    The players' chats are the 0.4.0 ones; A's drop chat promises B its ships back if B joins again, which a 0.3.3 host
    can't arrange (the rejoin of a relayed battle needs a 0.4.0 host).
  - c) A 0.4.0 host with two 0.3.3 players at war: their battle is resolved by the game (`battle 1 vs 2 in Northern
    Europe: not fought live: live-battle engine levels 1/1, host 14`), with the host's chat and each player's notice
    naming both versions; the month goes on: 19/19. The debug `battles injectpvp 1 2` is refused there ("both players
    must be connected and fight live battles with this host's live-battle engine"); the game's own battle phase made
    the battle.
- The second release candidate (dist_vp_2, the VP fix, against dist_033; 2026-10-18):
  - d) A 0.3.3 host's own battle against a 0.4.0 player (`--case d`; real months after war 0-1, the game's battle in
    the next month): **fought against the computer**, as the README says, but by this path: 0.3.3 knows no engine
    levels, so it holds the battle and sends it (`battle held at its start (waiting for Captain (Germany) to join the
    battle)`, `battle 100000 epoch 1 sent`); the 0.4.0 PC refuses it at once and never loads it (`battle 100000
    refused: Admiral's mod plays live battles at level 1, this PC's at level 14 (a live battle needs the same level on
    both PCs: the same mod version)`, its chat "The live battle with Admiral could not start: ..." and the notice "A
    live battle could not start: Admiral's mod plays live battles at another level"); the host's AI then commands the
    player's nation ("Captain left the live battle (Captain: <the client's reason>). The AI commands Germany now; the
    battle goes on here."). The month goes on (one month, the player's turn merged), then peace and a normal month:
    20/20 (the first run's check expected no hold and no send: 19/20, corrected in the driver). Wording seen on the
    PCs: the 0.3.3 host shows the client's reason verbatim, so its chat says "this PC's at level 14" about the
    player's PC; and 0.3.3 sends its battle chat lines to every player, so the 0.4.0 player also reads the host's
    "waiting for Captain (Germany) to join the battle", "sending the battle..." and "Captain left the live battle".
  - b) again (the loads changed with the VP fix): 19/19, the same as on dist_ll_16: the battle of the two 0.4.0
    players is fought live, relayed by the 0.3.3 host (10 minutes equal on both players' PCs, no DESYNC, `battle 1
    applied: ... VP 0/246`), the month goes on; A's drop chat still says B gets the ships back by joining again, which
    a 0.3.x host can't arrange (README, Known limitations).
- The 0.4.1 release candidate (dist_rc041_1, engine level 16, against the released 0.4.0 dist_vp_2, engine level 14;
  2026-10-21; the driver `mixed_041.py` in the developers' session files, `--case a|r`, built on the reg-041b runner):
  - a) A 0.4.1 host with a 0.4.0 player: the join (`Captain: mod 0.4.0, live-battle engine 14 (this host: 16)`), a
    month, war 0-1: the game's battle of the host against the player's nation in the next month is fought against the
    computer (`battle against Captain: fought against the AI (live-battle engine level 14, the host's 16)`), the host's
    chat "... this host has 0.4.1 (live-battle level 16), Captain has 0.4.0 (level 14)." and the player's notice "...
    the host has 0.4.1 (live-battle level 16), you have 0.4.0 (level 14)."; the month goes on, peace, a normal month;
    the 0.4.1 host gives the 0.4.0 player its own intelligence in its month (`intel: nation 1's own intelligence in its
    month`) and shows its own relations (`relview`): 22/22, 99 s with the game open.
  - r) A 0.4.0 host with a 0.4.1 player: the join (`Captain: mod 0.4.1, live-battle engine 16 (this host: 14)`), the
    same battle fought against the computer with both levels in the chat and the notice; the 0.4.1 player keeps the
    0.4.0 intelligence (`client month: nation 1, per-player intelligence off (the host's mod keeps the 0.4.0
    behaviour)`, `intelinfo` client=0) and sees its own relations at peace, at war and at peace again (`relview`):
    25/25, 157 s. Not checked: the 0.4.0 player's relations view (the 0.4.0 bridge has no `relview`).
- The 0.5.0 release candidate (dist_rc050_1, rel-050 dc2cbfc, engine level 17, against the released 0.4.1 dist_rc041_2,
  engine 17, and 0.4.0 dist_vp_2, engine 14; 2026-10-22; the driver `mixed_050.py` in the developers' session files,
  `--case a|b|c --new-dir <0.5.0 folder> --old-dir <older folder> [--debt <funds>]`, built on the reg-050 runner; the
  helper `mixed_run.ps1` makes the folder copy with the older DLLs, takes the game lock, runs the driver, puts 0.4.1 back
  and deletes the copy). Every case: the join, real months with the runner's month checks (`kwar_month_check` replaced:
  an older PC has no own war counter, `kwar` unknown there), war 0-1 and the game's battle of the host against the
  player's nation in the next month, peace and a normal month. `--debt <funds>` puts nation 1 in the red (`nfield 1 Funds`
  on the client before its last normal month) so that the player's money rules give it messages (they come in the
  months after: "With pay for the men being late again, fleet morale suffers!").
  - a) A 0.5.0 host with a 0.4.1 player: `Captain: mod 0.4.1, live-battle engine 17 (this host: 17), player's relation
    rules 0`; two months; nation 1 keeps the computer's relation rules (`relrules` without 1, no `(player's rules)`
    mask line, no relation turn on the 0.4.1 PC) but follows the player's money rules (its `economy: nation 1 ...` line
    each month with the computer's subsidy withheld and `month's end funds F (other changes +0)`, `econ 1` ran=1);
    the host's battle against the 0.4.1 player is **fought live** (held, the start check equal, 415 and 424 minutes
    equal on both PCs with the identification, no DESYNC, ended by agreement), the month goes on; with `--debt` the host
    sends 4 message lines of the money rules (`economy: 1 message(s) of nation 1's rules go with state seq S`) and the
    0.4.1 PC shows none: 66/66 (353 s), 67/67 with `--debt` (132 s).
  - b) A 0.4.1 host with a 0.5.0 player: `Captain: mod 0.5.0, live-battle engine 17 (this host: 17)`; the 0.4.1 host
    keeps nation 1 under the computer's rules: no relation turn on the 0.5.0 PC (no `rel` in the month), its game keeps
    the host's war counter (`kwar` on the client `own=unknown`), no money messages, no `relations v2`/`economy:` lines
    on the host; the host's battle **fought live** (416 minutes equal, no DESYNC, agreed end), the month goes on: 39/39
    (293 s).
  - c) A 0.5.0 host with a 0.4.0 player: `Captain: mod 0.4.0, live-battle engine 14 (this host: 17), player's relation
    rules 0`; the host's battle against the player's nation is refused live and fought against the computer (`battle
    against Captain: fought against the AI (live-battle engine level 14, the host's 17)`, nothing held or sent), the
    host's chat "* The battle against Germany is fought against the computer: live battles need the same live-battle
    engine on both PCs: this host has 0.5.0 (engine 17), Captain has 0.4.0 (engine 14)." and the player's notice "* The
    host fights its battle against Germany against the computer: live battles need the same live-battle engine on both
    PCs: the host has 0.5.0 (engine 17), you have 0.4.0 (engine 14)."; nothing stuck (the month goes on, the player
    plans, no battle screen open), peace and a normal month; the money rules' messages (`--debt`) not shown on the
    0.4.0 PC: 52/52 (181 s; the first run's 50/51 expected the messages in the month of the debt itself: driver).
- The 0.5.0 release candidate 2 (dist_rc050_2, rel-050 b5b0da7, 2026-10-23; the same driver and helper with the
  reg-050b runner): a) a 0.5.0 host with a 0.4.1 player again, since the host's AI-peace path changed (`--debt -30000`):
  the join (`Captain: mod 0.4.1, live-battle engine 17 (this host: 17), player's relation rules 0`), two months, the
  host's battle fought live (424 minutes equal, no DESYNC, ended by agreement), 4 money message lines sent and none
  shown on the 0.4.1 PC, peace and a normal month, no game error box: 67/67 (148 s). Nation 1 under the computer's
  relation rules has no relations v2 mask, so the host logs no AI-peace mask or skip line there.
- The 0.6.0 release candidate (dist_rc060_1, 2026-10-23): the driver `mixed_060.py` (`--case a|b`, the reg-060 runner)
  and `mixed_run.ps1` are in the developers' session files (`r60`), validated offline only: the candidate was stopped
  by the client's SaveBuildCampaign box (Known traps) before the mixed runs. a) a 0.6.0 host with a 0.5.0 player: the
  join (`... player's relation rules 1, battle generator 0`), the player's relation rules, two months at war of nation 1
  with a computer nation J: B5's blockade (TEST `warfxblk J 1`) and its message in the 0.5.0 PC's turn report
  (`economy: showing N message(s)`), no B6 for nation 1 (no player's generator run, no kind 1/2 request, nothing "left to
  the player's generator"), injected kind-0 battles declined on the 0.5.0 PC resolved by the game on the host with the
  box captured; then the host's battle fought live, peace and a normal month. b) a 0.5.0 host with a 0.6.0 player:
  `battles state` hostgen=0, a month at war with J without the allies' war help, building for war or surprise moves on
  the 0.6.0 PC, a declined battle going alone ("You declined the battle; the game resolves it."), the host's battle live.
- The 0.6.0 release candidate 2 (dist_rc060_2, int-060 d0964a3, against the released 0.5.0 dist_rc050_2, both engine 17;
  2026-10-23; `mixed_060.py` and `mixed_run.ps1` in the developers' session files `r62`, on the reg-060b runner):
  - a) A 0.6.0 host with a 0.5.0 player: 66/66 (261 s with the game open): the join (`Captain: mod 0.5.0, live-battle
    engine 17 (this host: 17), player's relation rules 1, battle generator 0`), the player's relation rules, two months
    at war 1-J with B5's blockade for nation 1 on the host and its message in the 0.5.0 PC's turn report, no B6 for
    nation 1, a battle declined in its preview on the 0.5.0 PC resolved by the game on the host (its box captured, none
    shown); the host's battle against the player's nation in a real month **fought live** (514 minutes equal, no
    DESYNC, ended by agreement), peace and a normal month, no game error box.
  - b) A 0.5.0 host with a 0.6.0 player: the join (`Captain: mod 0.6.0, live-battle engine 17 (this host: 17), player's
    relation rules 1`), `battles state` hostgen=0, a month at war 1-J: the relation turn without the allies' war help,
    building for war or surprise moves, a battle declined in its preview going alone ("* You declined the battle; the
    game resolves it."), the host's battle **fought live** (575 minutes equal, no DESYNC, agreed end), peace and a normal
    month: 54/55 (421 s); the one fail was the driver's: its "no new behaviour" pattern matched the bare word
    `surprise` in B6 round 3's battle-slot state lines (`battle N: the month's state in the battle slot: ... surprise
    attack 0 -> 0`, `month reset variables for the battle slot (DoTurn2): surprise attack <address>`), which every
    0.6.0 PC logs, also under a 0.5.0 host; the pattern now asks for `relations v2: surprise` or `surprise move`, and
    the case run again with it: 55/55 (354 s; the host's battle 436 minutes equal, agreed end).
- The 0.6.1 release candidate (dist_rc061_1, a2d918d: engine 18, against the released 0.6.0 dist_rc060_2, engine 17;
  2026-10-26; the drivers `mixed_061.py` (`--case a|b|z`, `--quick`, `--no-battle87`) and `i61_tmp_r61.py` (case c) in
  the developers' session files `r61`, run through `mixed_run.ps1`, on the reg-061 runner):
  - a) A 0.6.1 host with a 0.6.0 player: 91/91 (190 s): the join (`Captain: mod 0.6.0, live-battle engine 17 (this host:
    18), player's relation rules 1, battle generator 2, campaign divisions 0`), the host's battle against the player
    fought against the computer (`battle against Captain: fought against the AI (live-battle engine level 17, the host's
    18)`), the host's chat "* The battle against Germany is fought against the computer: live battles need the same
    live-battle engine on both PCs: this host has 0.6.1 (engine 18), Captain has 0.6.0 (engine 17).", the player's notice
    "... the host has 0.6.1 (engine 18), you have 0.6.0 (engine 17).", nothing held, sent twice or stuck, 5 months.
    Divisions: the host logs `divisions of nation 1: an empty list in its month`, `divstore 1` stays empty, the host keeps
    its own one (no duplicate), the 0.6.0 player's slot 77 has CampDivNo 0 (its division gone from the slot, kept in its
    memory). The 0.6.0 player that made a division got the released 0.6.0's own boxes in its battles against the
    computer: "Error in CreateDivisionForBCBattle procedure! Access violation at address 00EB908C ... Read of address
    00000082" (3), "Error in SelectForce procedure! Access violation at address 00EA97B6 ... Write of address 0000003B"
    (2), "Warning: Saving campaign divisions. Missing ship in RT K Div 1, shipindex: 0/1" (22). The control z) with both
    PCs on 0.6.0 has the same boxes (46/48, 199 s): 0.6.0's own fault, which 0.6.1 fixes (a known issue for 0.6.0
    players: upgrade). The case's count of 91/91 predates the runner's recording of boxes answered in a player's battle
    (Known traps, "Game error boxes"); with it the case counts them: 89/91.
  - b) A 0.6.0 host with a 0.6.1 player: 89/89 (183 s), and a short re-run b2 for a fought battle: 57/57 (151 s). The join
    (`Captain: mod 0.6.1, live-battle engine 18 (this host: 17), ...`), the battles fought against the computer with the
    notices naming 0.6.0 (engine 17) and 0.6.1 (engine 18); the player logs `divisions: an older host sends its own;
    nation 1's own 2 from this slot written instead (...)`, its `divs` and slot 77 hold exactly its own two, the host's
    slot 1 none of them, the host's own CampDivNo 1 -> 2 -> 4 (0.6.0's known doubling); b2's battle slot 87: `battle: an
    older host sends its own divisions; nation 1's own 2 from its slot instead`, exactly its two, no box.
  - c) Both on 0.6.1 from two game folders with different `Data\tmpShip.ttp`: 24/24 (44 s): both scans 1415 files, hash
    aebbfdfd9daeb498, `the game data files are the same on both PCs`, 4/4 minutes equal, no differ line.
  - Every case: the released 0.6.0 put back, slots 1/9 and the Preferences identical to their backup, the folder copies
    deleted.
- The 0.6.1 release candidate 2 (dist_rc061_2, int-061 c7955f9: engine 18, against the released 0.6.0 dist_rc060_2;
  2026-10-26; `mixed_061.py` copied to the developers' session files `r63` and run on the reg-061b runner through its
  `mixed_run.ps1`): a) a 0.6.1 host with a 0.6.0 player, `--no-battle87` (the 0.6.0 player's battles against the
  computer after it made a division show 0.6.0's own boxes, above): 78/78 (180 s). The same join line, the battle
  against the player fought against the computer (`... (live-battle engine level 17, the host's 18)`), the host's chat
  and the player's notice naming 0.6.1 (engine 18) and 0.6.0 (engine 17), nothing held or sent, nothing stuck (Aug ->
  Sep 1900, the player planning in slot 77 with no battle window), the peace month, the divisions as in RC1 (an empty
  list for nation 1, `divstore 1` empty, the host's own one kept), no game error box, exception or data-file "differ"
  line; the folder copy deleted.

## Two-PC conditions
Every run of the suite plays both PCs on one PC, so the copies share what two players' PCs don't: one Preferences file
(each copy reads it at its start), one Windows locale, one screen and DPI, one game folder with the same data files,
and a localhost connection without latency. And until 0.4.1 no test looked at B's battle map (the leak checks cover
lists, reports and messages). A report from two PCs on 0.3.2 ("the non host player is loading in as the host nation's
side ... Synchronising the battle again (the loaded battles differ)"; in the one battle that started, the joined
player's own ships were not visible on their map) led to these pieces ([MODLOG](../MODLOG.md#investigation-2026-10-19-live-battles-between-two-pcs-v041)).
- **Per-copy Preferences, through the game's own reader.** Back up `<Documents>\My Games\Rule the Waves 3\Preferences`.
  Write variant X, start copy A, wait until its `TfrmCampaign` window exists (its `FormCreate` has run the reader,
  about 3 s), write variant Y, start copy B, wait the same, and put the original back (verify the hash). The game
  writes the file only from the new-game dialog and the nation selection (`SavePrefs` from `TdlgCampaignStart.FormClose`
  and `TfrmSelectNation2.FormClose`), never in a battle or at its exit, so the file's hash at the run's end proves that
  nothing wrote it. Keep the key names of the file (`RealismSetting`, `AIControl`, `FOW`, `LOScalc`, `BreakOnSunk`,
  ...). The session folder's driver (`nb_repl.py --prefsA/--prefsB key=value,...`) does this.
- **Per-copy Preferences in memory:** `RTW3MP_TEST_PREFS` (see [Environment variables](#environment-variables));
  no file juggling, but the copy's writer is off.
- **A comma locale:** `RTW3MP_TEST_LOCALE=comma` on one copy. **Another x87 control word:** `RTW3MP_TEST_FPUCW`
  (1.01.44 resets it to 0x27F before any battle; since engine level 15 B loads and runs with A's word anyway, and `ls
  testcwload 0` gives back level 14's load with B's own).
- **Window sizes:** `SetWindowPos` on a copy's `TfrmMain` (the battle screen, from its first battle) between minutes.
  A copy's first battle window and every resync load take the Preferences' `MainFormWidth` x `MainFormHeight` (1000x719
  on the suite's PC), so those loads draw at that size and only the display between minutes sees another; the start of
  a later battle in the same game keeps the size the window had (`twopc`, 2026-10-20: the month battle started at the
  test battle's 900x650 and 1560x980). Give B its own `MainFormWidth`/`MainFormHeight` in its Preferences for loads at
  another size (`twopc`: 1400x880).
- **Latency and bandwidth:** `tools/net_delay_proxy.py`. The host hosts on 47624 as always; the proxy listens on
  another port and forwards to it; the client joins the proxy's port (`join Captain 127.0.0.1 47634`). In the runner,
  `NET_PROXY[C] = 47634` makes the client's joins go through it (`twopc`; the session folder's `j_nb6_start.py` wrapped
  `cmd` around `scenario_session` instead). A live battle's start and every resync carry about 2 MB (the battle files):
  4 s at 4000 kbit/s.
- **B's map:** `ls mapview` on both copies, and `PrintWindow` captures of the copies' `TfrmMain` (the game's own
  window only; zoom out with posted clicks on the tool bar's zoom-out button, at (189, 47) of the window). B's map shows
  B's own ships and division flags, A's ships only while B's side sights them and never A's flags, and B's ensign at
  the top left; A's map the mirror. During B's load and resync load its battle screen is transparent: the game shows
  it only 40-175 ms before the load's end, clear (layered, alpha 0) while the mod puts the map and the ensign right, and
  shown 1-4 ms after "battle loaded from slot" (`twopc`'s watch, 2026-10-20, every load of three runs).
- **The suite's scenario `twopc`** (group `twopc`; in the default run, the full variant with `--stress`; catalog
  above) puts these pieces together in one run: B started again with its own Preferences (kept in the shared file from
  B's start to the scenario's end, so the file's hash at the end is "B's own Preferences file unchanged") and a comma
  locale, the delay proxy, the windows at different sizes, a test battle and the host's battle against B in a real
  month, with the start check, the minutes, `ls prefs`, B's ensign, B's map and B's loads checked; then the run's own
  conditions again.
- **What B's map check compares.** `ls mapview` counts what each copy's map shows; the reference is the simulation's
  own sighting state at the same boundary, from B's DESYNC report (`ls desyncreport` on B, the state section: every
  ship's `SightingLevel`, `RadarSightingLevel`, `SeenByHumanControlledShips` and `Sunk` by its division's `Side`; both
  copies' states are equal, so one report serves both maps). By the game's rule a side's map shows the other side's
  ship when `max(SightingLevel, RadarSightingLevel) > 0` and `SeenByHumanControlledShips` (right after a load the
  levels alone), its own ships always, but a computer force's own ships only once the enemy spotted them (`ls forces`,
  `ls forces div`), and never the other side's division flags. The battle file is no reference: the load's own
  sighting routine changes the levels (the first `twopc` run: the resync's file said 3 of A's 16 ships sighted, the
  loaded state 4, and B's map showed 4). A probe the product could offer instead, so a test needs no report file and
  can tell which ships differ: `ls mapview ships` with one entry per ship (`side:index:shown:sight:radar:seen:sunk`)
  or `sighted0=`/`sighted1=` counts in `ls mapview` (each side's ships the other side sights by the rule above).
- **The combined run of 2026-10-19 (dist_nb_2, MODLOG):** B with a comma locale and 34 preferences different from A's
  (Realism, AI control, FOW, LOScalc, floatplane launch, every break and message option, sound, scale, dark mode,
  English ranks, German characters, ...), the copies at 900x650 and 1560x980, the client through the proxy (120 ms
  +-60 ms each way, 4000 kbit/s): a test battle (READY equal, 12 minutes, a resync, 6 minutes: equal, no LEAK) and the
  host's battle against the client in a real month (READY equal, 12 minutes equal, no LEAK, no DESYNC, the month on to
  September), B's map as above, the Preferences file unchanged.
- **Another CPU (engine level 16):** `RTW3MP_TEST_X87=ext|dbl|ppm[:N]` on B nudges its own x87 transcendental results;
  with `ls x87 soft 0` on A both PCs use their own x87 (as level 15). `ext` (a real Intel/AMD difference) and `dbl`
  changed nothing in 40 minutes; `ppm` makes every load differ ("orders"); with the software maths (the default) READY
  and 40 minutes stay equal and B's `nudged` count doesn't move during the battle. `RTW3MP_TEST_CPU=Intel` on one copy
  makes the vendors differ in the hello and the READY diagnostics (`ls testreadydiff` then shows `orders; CPUs AMD
  (Admiral), Intel (Captain)` in both chats and notices).
- **Other game data files (engine level 16):** `ls testdatafile Data\Gundata.dat;Data\Extra.dat` on B (or
  `RTW3MP_TEST_DATAFILE`), then a battle: B refuses before loading, A names the two files in both chats and notices and
  fights the battle against the computer, no resync (`ls testdatafile off` puts it back). `RTW3MP_TEST_FILELOG=1` lists
  the files the game opens (GAME-INTERNALS, "Two PCs: CPUs and data files").
- **The game's temporary files (engine level 18, 0.6.1):** each copy's scan line `data files: <n> files of Data (but
  Data\Graphics and the game's temporary files tmp*: Data\tmpMapData.dat, Data\tmpShip.ttp) hashed in <ms> ms:
  <hash>` names the `tmp*` files it left out (`none` without one). The suite's `lockstep_data` checks the scan
  against the game folder and plays a live test battle with B's `ls testdatafile Data\tmpShip.ttp;...` (the switch
  leaves `tmp*` names out as the scan does). A real difference needs two game folders: the developers' driver
  `i61_tmp.py` (session files) starts the client from a copy of the game folder outside the Steam library whose
  `Data\tmpShip.ttp` differs and which has `Data\IDes\tmpProbe.txt`: both scans list their own `tmp*` files and agree
  on the hash, the live battle starts live ("the game data files are the same on both PCs"), and
  `RTW3MP_TEST_FILELOG=1` shows no `tmp*` file opened by the battle's load, minutes or close; with the released 0.6.0
  in the copy the host's battle against it is fought against the computer with the engine notice ("... this host has
  0.6.1 (engine 18), Captain has 0.6.0 (engine 17)"), no data-file notice. The copy is deleted afterwards.
- **For the suite (proposal):** a scenario `two_pc` with that setup (B's Preferences variant at its launch, the comma
  locale, the proxy, a resize before a resync) and lockstep's and lockstep_month's checks, plus: `ls prefs` on B shows
  A's 14 battle preferences during the battle and B's own after it; B's `ls mapview` shows `flags0=0/` and every own
  ship (`side1=n/n`) after the load and after minutes; B's log has `the battle screen shows side 1's ensign`; the
  Preferences file's hash is unchanged.

## Bridge command reference
The bridge exists only when `RTW3MP_BRIDGE_PORT` is set at launch, and it listens on 127.0.0.1 only. Send one
command per line, for example `python tools\rtw3ctl.py cmd 47701 status`. A reply is one line, `ok ...` or
`error ...`. Commands run on the game's main thread; the bridge waits at most 120 s for them. Commands that can
open a modal game dialog are asynchronous: they reply `ok` at once and log their result, and the test answers the
dialogs. Sources: `src/mod/bridge.cpp`, `src/mod/lockstep.cpp` (`ls ...`), `src/mod/poc_battle.cpp` (`poc*`).

`ping`, `status` (role, phase, myNation, date, status, players, submitted, submitting (0.4.1), remaining,
campaignReady, slot, startBuild, playerIdx, busy, battle),
`nations`, `host <name> <port> <limitMin>`, `join <name> <ip> <port>`, `claim <name|index>`, `start`,
`submit` (asynchronous since 0.4.1, see below), `turn`, `advance`, `leave`, `load <slot> <player> [mod]` (as the
player's own load: outside a multiplayer session the game's blockade credit stays, as in single player; `mod`: as one
of the mod's own reloads, whose load keeps the saved victory points also outside a session, 0.4.0), `loadasync <slot>
[player]` (the same load without waiting, logged as `loadasync <slot> <player> -> ok|error load`: a battle in progress
may ask "Select autostart point." first), `save` (writes the slot: safety rule 6),
`mpwindow`, `windows`,
`nfield <nation> <IntegerField> [value]`, `areas`, `shipinfo/shiporder`, diplomacy: `diplo <action> <n>`,
`diploas <from> <action> <n> [extra]` (host; extra = the claim list for `claim`), `reserve <n> <player>` (host:
stand-in human nation), `rel <a> <b>`, `relview` (0.4.1, read-only: what the strategic screen's tension panel and war state show now: `ok shown=<the nation this PC plays when it is not nation 0, else 0> slots=<i>:<flag's hint>~<the tension bar's hint, "Tension with X is N" or "You are at war with X">~<war><alliance><blockade marks, 0/1>; ... war=<War results box><war line visible>~<war line>~<the player name's hint>`; parse with `regression.relview`), `diploview` (rows `nation/name/player/relation/tension/[pending]/flags`, since
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
memory; a clean list has count == unique), air units: `airunits` -> `ok count=<units in the game's global list, all
nations>` (host or client; stays the campaign's own count across loads since 0.3.3; a load logs `air units: <n> from
before the load dropped; the campaign's <m> kept` and, for a save of an earlier version, `air units: <k> repeated units
of the save dropped (<a> -> <b>; a save of an earlier version)`; the 1900 test campaign has none: count 0 and no
line, the 1935 one 232), doctrine: `doctrine <nation>` -> `ok training=a,b,c,d pending=a,b,c,d
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
`eventlast`, `eventlist`, `windows`, `diploview`, `relview`, `rellast`, `peacelast`, `kwarvp` (everything else: `error an event dialog is open`;
the same while the player's relation routines run in the nation's view, relations v2).
Victory points (0.2.6): `vpinfo` -> `ok enemy=<EnemyVP> seen=<S> vp=0:v0,1:v1,...,8:v8,` (host: the game's EnemyVP = the
host's enemies' score and S = the part of it the host already credited to its human enemies, -1 = not known yet;
client: `enemy` = the "Enemy VP" it shows, `seen` always -1), `battlevp <opp> <vpHost> <vpOpp> [Major|Minor|Marginal]`
(host, synchronous: sets BattleOpponentIdx and calls the game's own `HandleBattleAftermath` through the mod's hook, as
after a tactical battle; raw VP, the game log-adjusts them -> `ok host=a->b enemy=c->d opp=e->f`), `enemyvpadd <n>`
(host: EnemyVP += n outside a battle, like a blockade or a sinking; a negative n imitates the game's reset; replies
with the `vpinfo` text).
War counters (relations v2, B1): `kwar` -> host `ok stepped=<the month sequence whose processing last stepped them> host=<the
host's own war counter> kwar=K:n,...,` (every joined nation, reserved ones included; months at war > 0, at peace <= 0),
`kwar <K> <n>` (host: sets nation K's counter); client `ok own=<its nation's counter as the host sent it, or unknown>
game=<the game's war counter in memory> nation=<K>` (`regression.own_war_counter`, `kwar_counters`).
The disarmament treaty (0.6.0): `armstreaty` -> `ok months=<TreatyTimeRemaining> disp=<TreatyDisplacementLimit>
cal=<TreatyCalibreLimit> subban=<SubBanTime> base=<TreatyTonnageBasePercentage> war=<this PC's game war counter>
host=<a joined PC: 1 when the host ends the treaty for a joined nation's war (header armstreaty)> kept=<CancelTreaty
calls left to the host on this PC> endedhere=<ends on this joined PC with the host's rule> ends=<the host's ends for
joined nations' wars> last=<nations/months left/submarine ban/why of the last end, or ->` (MaxInt 2147483647 = no
limit; `regression.arms_treaty`). TEST ONLY: `armstreaty set <months> [<displacement> <calibre>]` (a treaty in force on
this PC, every nation's TreatyTonnageLimit MaxInt: no tonnage limits), `armstreaty war <n>` (this PC's game war
counter), `armstreaty host <0|1>` (a joined PC: the host's rule as assumed until the next month's header), async
`armstreaty refresh` (the screen's refresh: `UpdateUIValues`, at war the game's CancelTreaty with its box on the host)
and `armstreaty conference <chance>` (host: the game's CreateArmsControlTreaty now, through B2's mask and its view of the
joined nations' wars; its dialogs answered by the test; `econtreaty <chance>` runs the same call at the month's end).
The last battle's area and possession (0.6.0): `battleptrs` (read-only, also while an event dialog is open) -> `ok
area=<0|1> poss=<0|1> names=<area>/<possession>` (TfrmBuildCamp.BattleArea/BattlePossession, set by `DoBattle`, cleared
by every load; `battle_area_month_check`). Diagnosis: `pocheapcheck` (read-only) walks the campaign's objects from
frmBuildCamp and the air unit list through RTTI and answers `ok objects=<n> strings=<n> bad=<n>` (strings that are no
valid UnicodeString, references that are no live object, with their paths), with the two battle pointers when set (`of
this campaign` or `STALE`); RTW3MP_DIAG runs it before every save and after every load.
Campaign divisions (0.6.1, read-only, but refused like the others while an event dialog is open):
`divs` -> `ok n=<count> nation=<PlayerNation's index> list=<CDivId>:<name>:<ship count>:<CShipId>.<CShipId>...|...`
(the game's division list in memory, the Fleet organization's; a nil division or ship reads `nil`, `|` and `:` in a
name read `_`; nation K on a joined player's PC, 0 on the host and in a battle slot) or `error unavailable` (no
division hook); `divstore <K>` (host) -> `ok n=<n> list=...` (the same format without `nation`: nation K's list as the
host keeps it in `rtw3mp_nations.ini` from the player's turns; `ok n=0 list=` when there is none) or `error the joined
nations' records are not loaded (host only)` (`regression.parse_divs`).
The player's relation routines (relations v2, B3): `relturn` (client, async: runs them now, with their dialogs, also a
second time in a month; normally once a month after the event roll), `rellast` (read-only: client `ok tension=a:b:t0:t1;...
alliance=... will=... budget=... vp=d kwar=n ran=tension,previous wars,war msgs=N` or `ok none`, with ` pending` while this
month's turn is still to run; host `ok nation=K <what it applied>`).
The player's rules (relations v2, B2): `relrules` (host) -> `ok rules=<joined nations whose relations follow the
player's rules now>, forced=<switched on by a test> saved=<remembered for players who are away>`; `relrules <K> <0|1>`
switches nation K on or off for a test (`regression.relrules`).
The war line between players (0.6.1): `warline` (read-only) -> `ok people=<nations whose wars with this PC's nation come
only from the players: their tension bars carry the hint> above=<host: the pairs of people's nations above the war line
(tension 11..40) at the last check>`; TEST ONLY (host): `warline forget` clears that memory (the next check tells every
pair above the line again), `warline check` runs the month's check now (` told=<n>` chat lines; `relations_war`).
Peace with the computer's nations (relations v2, B4): `peacetest <chance> <lossGain> <force>` (client, async, `ok` at
once: the game's `NegotiatePeace(chance, lossGain, ForceBadPeace)` now in the nation's view, through the "Propose peace"
path, with the game's dialogs; any time, no month limit; the client then logs `peace for nation K sent (test): ...` or
`peace: <why none>`, the host `relations v2: peace of nation K with ...`; `regression.wait_peace` answers the dialogs,
`answer_possessions` the "Select possessions as part of peace agreement" list), `peaceevent <33..36> [chance]` (client:
the next relation turn shows that peace event past the decision; 0 clears; with a chance (1..200) the event's
NegotiatePeace runs with it instead of its answer's, e.g. 100: the computer's offer is taken; with `relturn` in a month
whose turn the host applied already, the host ignores the second turn), `peacelast` (read-only: client `ok ran=1 code=c
event=0|1 args=ch:lg:f accepted=0|1 winner=0|1|2 enemies=j, lands=n ships=n taken=n delta=n:Field:d; lost=n:v; treaty=n
kwar=n shipids=id:from>to, landnames=name~from>to| host=0|1 month=used|open forced=c[:chance]` or `ok none`; host `ok nation=K
kind=turn|peace code=.. event=.. args=.. accepted=.. winner=.. enemies=.. lands=.. ships=.. states=.. nations=..
skipped=.. blockade=b vp=v j:war|peace,blockade=b ...`; `regression.peacelast`), `kwarvp <K> [J vpK vpJ]` (host,
read-only without values: nation K's war scores with the computer's nations -> `ok nation=K rules=0|1 vp=<sum> evp=<sum>
pairs=J:K's:J's, (wars on now) kept=J:K's:J's, (every score kept) peace=used|open (a proposal this month) kwar=n`; with J
and two values sets that war's score; `regression.kwarvp`), `findship <id>` (the nations whose ship list holds the ship
with this `CShipId`: `ok n:index:name; ...`; `regression.ship_holders`), `nflag <n> <BooleanField> [0|1]` (a Boolean
nation field, e.g. `HasLostWar`, `Blockaded`: `ok 0|1`; `regression.nflag`). `peacelast` and `kwarvp` also answer while
an event dialog is open. The Diplomacy list's computer rows show in `diploview` with player `computer` and flags `P` only
(`diplo peace <J>` is the row's "Propose peace": the game's `NegotiatePeace(40, 0)`, once a month).
The player's economy rules for joined nations (relations v2, B5 money; `src/mod/economy.cpp`): `econ <K>` (host; read-only
apart from the game's own `GetMonthlyBalance`, called twice) -> `ok nation=K joined=0|1 funds=F bm=<BudgetModifier>
br=<BaseResources> balance=<GetMonthlyBalance at the player's prices> balanceAI=<the same at the computer's prices>
maintenance=<balanceAI - balance> fs=<the fleet-size multiplier> date=YYYY-MM rec=YYYY-MM|none pending=0|1 expenses=0|1
before=a after=b applied=c appliedAI=d subsidy=s brFloor=n brDecay=n bb=n spy=n spyFunds=n aiWar=n armsRace=n
aiWarStart=n hostWar=n coup=n january=n keepUp=n preem=n preemApplied=n halt=n resume=n warStarts=n warGame=n
warPlayer=n shared=n rules=<the rules in place>`. `rec` is nation K's last processed month (`pending=1`: not logged yet):
ComputeExpenses gave K `after - before == applied` (its balance at the player's prices) and withheld the computer's
subsidy `s` (25 or 50 x `fs`); `bb` CheckBBNumbers skipped, `spy`/`spyFunds` the host's spy findings left out, the
BudgetModifier +1s left out by rule (`aiWar`, `armsRace`, `aiWarStart`, `hostWar`, `coup`), `january` its -1s,
`keepUp` keep-up rolls skipped, `preem`/`preemApplied` the preeminence rule's player branch and its +1s, `halt`/`resume`
the computer's automatic halts and resumes of K's ships left out, `warGame`/`warPlayer` the BudgetModifier increments of
the host's wars with K (the computer's, the player's given instead), `shared` the change of K's funds in
`AdvanceShipConstruction` by the rules every nation shares (construction delays pay the month back, the hoarding
reclaim; `before` = the funds the client submitted + `shared`). Host log: `economy: the player's economy rules for
joined nations: ok (20 of 20; ...)` at the start, then one line a month for each joined nation, `economy: nation K
(<name>) YYYY-MM: funds a -> b (balance +c at the player's prices, maintenance +m); withheld: ...; the player's rules:
...; shared construction rules +n`, and `economy: nation K's war with the host's nation: BudgetModifier a -> b (the player's rule; ...)`.
0.5.0 (the player's own rules, part 2): after `shared`: `ran kwar debt debtPrestige morale corruption hoard loan
complaint complaintPrestige budget budgetBM tensionEdits notice treatyCut ruleFunds ruleBM rulePrestige news kao kpd
aoCut ended fundsEnd otherFunds asw techPartner shipBuyer amcKept china`, then `aoNow pdNow noticeShown newsPending
prestige corruptionNow econrand`, then `rules=` (+ `armyoffensive aobudget asw techpartner shipbuyer amc treatycut
china playerfunds turnevents`; the start line says `ok (32 of 32; texts 51/51; ...)`). Codes: `debt` 1 in the red, 2
finance ministry, 3 losing confidence, 4 outraged, 5 late pay (`morale` 1); `corruption` 1 the 1000 taken, 2 the
question only; `hoard` 1 army offensive (war), 2 opera, 3 parliament, 4 defence committee, 5 reallocation; `budget`
1..4 = SmallFleetCheck's D1..D4; `kao`/`kpd` K's army-offensive months and ParliamentDelay after the month, `aoCut`
K's budget x 0.9 this month. Then (0.6.0, B5 round 4) `treaty treatyBM conference`: the arms-control conference
(`CreateArmsControlTreaty`) after ComputeExpenses: its change of K's Funds (its ships under construction over a new
treaty's limits scrapped for their scrap value) and BudgetModifier (-2 at the signing), and how often it ran; host log
`economy: nation K: the arms-control conference changed its funds by +f and its BudgetModifier by -2 (after
ComputeExpenses: in the month's record)` and in the month's line `arms-control conference n (funds +f, BM -2)`. The month's
end: `fundsEnd == after + ruleFunds + treaty + otherFunds` (`otherFunds` 0 expected).
TEST ONLY `econrand <v>` (host): the K rules' Random(n) draw min(v, n - 1); -1 (default) the game's Random. TEST ONLY
`econtreaty [<chance> [blind]|cancel]` (0.6.0): `<chance>` 1..100 runs the game's `CreateArmsControlTreaty(chance)`
once at the next month's end (after ComputeExpenses, before the record's month's end; through B2's mask; host log
`economy: TEST: the arms-control conference ran: treaty months a -> b`); `blind` without the conference's view of the
joined nations' wars (the conference before 0.6.0; a treaty it makes is ended at once); `cancel` the game's
`CancelTreaty` without its box (both: game.cpp's `EndArmsTreatyForWar`, also on a joined PC); the reply `ok armed=c blind=0|1 months=m limit=l calibre=c` (TreatyTimeRemaining, the limits). `ffield
<Field>`: a campaign (TfrmBuildCamp) integer field, read only (e.g. `ArmyOffensive`, `ParliamentDelay`). Host log per
message `economy: nation K news: <text>`; client `economy: showing N message(s) of this nation's money and war rules` and
`economy: army offensive of nation K: n month(s) (ArmyOffensive set n|kept m)`. The host log `relations v2: campaign
saved after a relation turn|a peace proposal of nation K (ok; n save(s) of this kind)` follows every apply of a joined
nation's relations (`rellast` on the host ends in `saved=1`, `saved=pending` while the host's game is busy).
The player's war effects for joined nations (0.6.0, B5; `src/mod/warfx.cpp` blockades on the host, game.cpp the allies'
war help on the player's PC): `warfx [K]` (host; read only) -> `ok on=1 flags=<the nations blockaded now> check=<the last
check: load|month: K(war W, enemies e,e[+host]): not blockaded|blockaded by b,b, blockading j,j (its view|the host's frame); flags f (the game's g)> |
stop=<the last StopBlockadedMoves: war 1 (the host at peace)|the host at war n:stopped/under way ...> | nation=K frame=0|1
kwar=W blockaded=0|1 blockers=.. enemies=.. rec=<the month's credits: g+v/l (effects: ...)> news=<messages waiting>
weary=<K's war weariness of the month, spaces as _> build=subs:w/n,delays:w/n,budget:w/n,grants:g|-` (the build rules' War tests this month answered "at war" w of n times by K's counter, the war budget's grants) ` sea=<K's last sea war frame, the host log's line with spaces as _, or ->` ` | wearyrule=1|0 buildrules=1|0 searule=1|0` (0.6.0, B5 (c): `searule=0` when the routines or the hook point are not found: no frame). TEST ONLY `warfxsea <R> <n>` (host, 0.6.0): in the next sea war frame up to n operational ships of nation R in active service (Status 0, not Used) raid in the defender's build area (R = K: against its first enemy; R an enemy of K: against K) -> `ok warfxsea=R:n` (`-1:0` off); the frame puts back Status and LocationArea of those still on raider status (one the game interned, sent home or scuttled stays as the game left it). Host log `warfx: sea war of nation K (enemies e,e): war counter W, war score a:b, its view[, blockaded (no enemy raiders)]; calls c, faults f; (raiders K/enemy, subs K/enemy, VP K/enemy) j: raiders m/m, subs s/s, VP +v/+v; [TEST: n raider(s) ...;] messages n (pictures p, boxes b, turn messages t); unattributed VP u, K's VP field 0, EnemyVP 0; fuel of others changed: none|n..`, `warfx: sea war: box recorded for the nation's report: '<text>'`, `warfx: sea war: the computer's submarine sinkings dropped this month for joined nations' computer pairs: n (a-b ...)`, at the start `...: raiders, submarines and the trade war ok (DoStrategicTurn ...)`. TEST ONLY `shipbuilder <n> [<ship> <builder>]` (host or client): nation n's ships under construction `ok index:CShipId:BuildingNationIdx ...`, after setting one ship's BuildingNationIdx (a ship built in another nation's yard).
`warfx strength <E> <N> [K]` (host; read only, 0.6.0): the game's `GetBlockadeStrength` of E against N in N's build and blockade areas on the host's frame and in K's view (default: N, else E, under the player's rules) -> `ok E=e N=n host: build=v [account] blockade=v [account] | view K: build=.. [..] blockade=.. [..]|view -` (account: the game's ExplString, the nations it added). TEST ONLY `warfxblk <n> <-1|0|1>` (host): the war effects' blockade test of nation n (also K's per-enemy test) answers
1 / 0, -1 the game's `IsBlockaded`; `warfxrand <v>` (host): the war effects' own draws (points, unrest, war weariness)
min(v, n - 1), -1 the game's. Host log: `warfx: the player's war effects for joined nations: blockades ok (...)` and `...:
war weariness ok (the rule at 0x...)` at the start, `warfx: war weariness of nation K (the player's rule): war counter W,
war score a:b, government g, BM m, blockaded 0|1 -> unrest u -> u+1|no (at peace|government|not losing|BudgetModifier|draw)`
and `warfx: war weariness this month: the player's rule for nations K, the game's own for 0,2,...`, `...: building for war ok (...)` at the start, `warfx: building for war: nation K's submarine construction|construction delays|war budget test answers at war|at peace (its war counter W; the host's ...)` (where it differs from the host's, capped), `warfx: building for war: nation K's war budget: BudgetModifier a -> a+1 (its war counter W)`; client `Building for war (ship seizures) for a joined nation: ok` and in the relation turn `relations v2: building for war for nation K (war counter W[, a new war]): ships under construction between it and a computer nation (id:owner@yard) ...; handed over id:from>to ...` (the host's `relations of` line: `seized ship id a->b`), `warfx:
blockade check (load, flags only|month): ...`, `victory points: nation G +v (blockade, in its war with nation L) -> t[, war
score a-b x:y]`, `warfx: nation N's unrest from the blockade: a -> b (war counter W)`, `warfx: message for nation K:
<text>`, `warfx: StopBlockadedMoves (run with War 1: the host is at peace|the host at war): ... n:stopped/under way`,
`warfx: n message(s) of nation K's war effects go with state seq s`; client `warfx: blockade flags from the host (list):
unchanged|changed n+ m-`. The allies' war help: client log `War help from allies for a joined nation: ok` at the start,
`relations v2: war help from allies for nation K (war counter W): VP v, funds of others n:d|none, s lend-lease ship(s),
LendLeaseDone 0|1` in the relation turn (the host's `relations of ...` line then has `ally help` in its routines and
`allyvp K-J+v`, `funds n+d`, `lend-lease ship id a->b`, `LendLeaseDone`). `setally <a> <b> <months>` (an alliance, raw;
0 ends it); TEST ONLY `allyrand <0|-1>` (client): the allies' war help draws 0 (GetRandomEnemy's Random(8) goes round
0..7), -1 the game's. TEST ONLY `revotest <0|-1> [n:unrest ...]` (client, 0.6.0): the next enemy-revolution run of
the nation's turn: its draws 0 (-1 the game's) and these nations' UnrestLevel (real indices) in this copy first (a
people's nation's value is put back afterwards).
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
Per-player intelligence, trade protection and foreign stations (0.4.1, `src/mod/nationplay.cpp`): `intelinfo` -> `ok
fields=0:s/e,1:s/e,...,8:s/e cost=<GetIntelCostForPlayerNation, through the hook> budget=<the budget's Intelligence
line> balance=<the monthly balance line> labels=<the eight flag labels, through the hook> reports=<intel list count>
client=<1: per-player intelligence on this PC>` (s/e: the IntelligenceSpending/IntelEffort fields of the nation object
at that index: on a joined player's PC its own spending on and effort against that nation; on the host the host's),
on the host followed by `month<K>=intel:<what the month adds for intelligence: minus K's cost>/balance:<K's
GetMonthlyBalance>` and `rec<K>=spend:../effort:../reports:N/fsWarned:0|1` per joined nation; `tradecheck [nation]`
(the client's own nation by default; on the host any nation, computed as for the player) -> `ok nation= war= blockaded=
patrols= required= short= requiredTonnage= areas= fs= insufficient= prestige= fsWarned= year= month= panel=<the budget
panel's trade protection text, spaces as _>`; `shipstatus <nation> <ship> [status]` (read or set a ship's
`TShipStatus`: 3 trade protection, 6 foreign stations; in memory only, the next save writes it); `intelbtn <tag>` (the
politics panel's +/- speed button with that Tag through the game's `SpeedButton1Click`: 1-8 raise, 9-16 lower the
spending on the nation under flag Tag (Tag-8); on a joined player's PC the flag of its own nation is the host's nation);
`intelcheck` (client, async: the monthly spying check now, the game's spy dialogs may show; logs `intel: monthly check
for nation K: spy event|no spy event, N new reports, effort ..`; a second result in a month is ignored by the host, as
a second event); `fsmonth <nation> [count]` (host in a session: the month's trade protection / foreign stations check
for that joined nation now, count times (1-50), with the game's chances: `ok runs=N news=M prestige=a->b
fsWarned=0|1`; the news lines are logged `nation K month: ...` and go to the player with the next month). `submit` is
asynchronous since 0.4.1: a joined player's Submit can ask the Turn's questions first (client log `turn checks for
nation K: at war|at peace, trade protection N of M, foreign stations short of S, covered C`, `turn checks: ...
question -> Yes|No`, `submit: cancelled at a Turn warning`); `status` shows `submitting=1` until it has returned, so
wait for `submitted=1` and `submitting=0` (a resubmit keeps `submitted=1` from the earlier one; reading the client's
slot meanwhile collided with the game's save once: "Error in SaveBuildCampaign procedure! Cannot create file").
Other log lines: host `intel: nation K's own intelligence in its month (...)`, `intel: nation K's turn: spending ..
(cost N a month) ..`, `event of <player> (nation K, #-1 'Intelligence'): ... intel: N reports, effort ..`, `AI
suppressed: CheckAISpying for nation K`, `nation K month: at peace, foreign stations short of ..` / `at war, N ships on
trade protection of M required`, the month's texts or `nation K month: no consequence this month (<why>; war counter
W, yyyy/mm)`; client `client month: nation K, per-player intelligence on`, `intel: spending on <nation> a -> b`,
`submit: own intelligence (..)`.
Joined players' battles against AI nations (0.3.0, developed after 0.2.8; `session.cpp`/`game.cpp`):
host `battles` / `battles list` -> `ok phase=0|1 timeout=S battles=id:K-J:area:state[:why]; ...` (state `queued`,
`fighting` = sent, `fought` = merged, `to the game`, `resolved by the game`; the area may contain spaces: parse with
`battles_list`), `battles inject <K> <J> <area name with spaces>` -> `ok <id>` (planning only, else `error not while
the month or its battles are processed`; a request as if the game had found it this month, processed at the end of
the next processed month; refused: `error no sea area '<name>'`, `error K must be a joined player's nation and J an AI
nation` (also for a reserved stand-in nation as J), `error no connected player who fights battles for nation K`),
`battles resolve <id|all>` -> `ok <n>` (the game resolves it, a host decision), `battles timeout <s>` (default 1800,
env `RTW3MP_BATTLE_TIMEOUT`); 0.6.0 (B6): `battles list` adds `:kind=1:att=N` for a request of the player's generator
and `:declined` / `:avoided`; `battles gen <K> [queue|fault]` -> `ok id=<0|id> war=<K's counter> attempts=a>b area; a>b (area
by the game); ...` (the player's battle generator for K now, in K's view on the host; a dry run unless `queue`, which
adds its request; refused unless it is on for K: player's rules, client hello `gen=1`, fights battles; all of the
generator's attempts, in its order, at most 64; `fault`: the first CheckAttackLocation in the view raises an access
violation inside the game's try block -> `error exception in the game's battle generator: Error in CheckForBattles
procedure! Access violation ...`, the box recorded instead of shown, no request), `battles
injectgen <K> <J> [area]` -> `ok <id>` (a request of kind 1 with one attempt, K attacks J; no area: the game picks),
`battles injectsa <A> <B>` -> `ok <id>` (0.6.0, B6 piece 2: a request of kind 2, a surprise attack of A on B, one of
them a joined nation under the player's battle generator whose PC announced hello `gen=2`, the other a computer
nation; played with the game's CheckSurpriseAttack in the battle slot),
`battles aitest <K> <J> <area>` -> `ok fought=0|1 captured=N | <text> ...` (the game's resolution of the battle now,
its result box captured instead of shown; risk 4; since 0.6.1 with both nations' ships that can fight there, host log
`ResolveAIBattle K-J in <area>: ships that can fight there a/b (when found 3/3)`, and no battle, the game's routine not
called, when either is 0: `... : no battle (ships that can fight there: a/0; when found 3/3)`); client `battles state` -> `ok state=none|pending|starting|fighting id= nation= opp= area=
slot=87 slotMode=0|1 aftermath=0|1 fight=0|1 hostgen=N` (`id=-1` while `starting`: the preview is up; `aftermath=1` once
HandleBattleAftermath ran in battle slot mode, until the slot mode ends; `hostgen`: the host's welcome `gen`, 0 for a
0.5.0 host), client `battles hostgen <n>` -> `ok hostgen=<n>` (tests, 0.6.0: as if the host's welcome had `gen` n; 0 =
a 0.5.0 host: a decline goes alone and the game resolves it; 1 = no surprise attacks or takeovers in the view; a new
join sets it from the welcome again), `battles monthstate [set]` -> `ok flags=<6 hex> surprise=n possession=n heroic=n
loss=n convoy=n` (0.6.0, B6 round 3: the month's battle state on this PC; `set`, client only, TEST ONLY: the five month
fields to 1 as an earlier battle leaves them; the battle slot logs `battle N: the month's state in the battle slot:
...`, the month's load `month: ship-type flags A -> B ...`), client tests of 0.6.0 piece 2 (surprise attacks): `battles saretry <n>` -> `ok
saretry=<n>` (CheckSurpriseAttack's random gates, 20 % and 60 % aborts: a False with nothing else happening is asked
again up to n times; 0 = once, as single player; logs `battle N: test: CheckSurpriseAttack asked again (i of n)`),
`battles testsa <J> [k]` -> `ok testsa=<J>[ k] hostgen=N` (the next relation turn: computer nation J plans its war on
this nation, WillGoToWar 1, with the SurpriseAttack trait for the turn; `k`: this nation's trait too, so it strikes;
the takeover picks the area's first unowned possession: CheckForWar's landing block -> takeover, war, DoSurpriseAttack;
needs nation K at peace, a gen 2 host), `battles sadry <J> [k]` -> `ok ran=1 pairs=a>b;...` (DoSurpriseAttack alone in
this nation's view now, J the target, the traits as with testsa: the pairs CheckSurpriseAttack was asked for, recorded
and not run; nothing sent), `fightbattles 0|1` (before `join`: sent with
the hello; a rejoin with another value changes it). Logs: both copies `Joined players' battles: ok (frmMain=..
checkAttack=..)`, 0.6.0 `Surprise attacks and takeovers at a war's start for a joined nation: ok (CheckForWar's call
sites 1/3/1)`; client `relations v2: surprise attack: nation A may strike nation B at the war's start (recorded for the
battle slot)`, `relations v2: surprise move: nation J takes over <p> (owner -1; recorded for the host)`, the turn's
summary `...; surprise attacks: A>B; takeovers: <p> -1>J`, in the slot `StartSurpriseAttack a attacks b -> True|False`;
host `battle request N (surprise attack): nation K vs J, n attempt(s): A>B, played by <player>` (+ chat `* Surprise
attack? <A> may strike <B> at the war's start. ...`), `relations of <player> (...): ... takeover <p> -1->J surprise
A>B` (+ chat `* In a surprise move, <J> has taken control of <p>!`), `battle N (kind 2): no battle (<why>)` (+ chat
`* No surprise attack of <A> on <B> (<why>).`); host `battle request N: nation a vs b in <area> (x:y ships), fought by <player>` (+ chat `* Battle!
<K> meets <J> in <area>. <player> fights it after the month.`), `battle request N injected: nation K vs J in <area>`,
`battle phase: N battle(s)`, `battle N sent to <player> (B bytes)`, `battle N applied: X ships changed (F fields), R
removed, VP a/b, prestige p (player: VP a/b, prestige p, morale m)`, `battle N: resolved by the game (<why>)` and after
the merge `battle N resolved by the game: battle|no battle`, `battle result N from <player> ignored`; client `player
copy in slot 87: ...`, `LoadCampaign slot 87 player 0`, `StartPlayerBattle <a> attacks <b> in <area> -> battle|none`,
`battle N: fighting nation K vs J in <area> (slot 87)`, `battle slot: DoStrategicTurn skipped`, `battle N:
fought|declined|nobattle|failed - why`, `notice: The host resolved this battle without you (<why>). ...`. 0.6.0: host
`Player's battle generator: its error box in a joined nation's view goes to the log` (startup), `the player's battle
generator for nation K: exception in the game's battle generator: <box text>`, `battle N: nation K's decline counts
x/y (were u/v)` (every result with `dc`); client `battle N: the nation's decline counts a/b in the battle slot (the
save's were c/d)`, `battle N: attempt i (a attacks b in <area>) found no battle`.
The POC commands below work on whichever copy shows a battle (the suite uses `pocstate`, `pocrun`, `pocsideai`,
`pocspeed` on the client's battle).
Battle research POC (2026-10-14, `src/mod/poc_battle.cpp`; nothing is hooked or changed until one of these is used;
not part of the suite; drivers in the research report): `pocresolve` (the POC's runtime lookups: `ok` or `error
<missing>`), `pocmissions <opp>` (`ok m:title~possession~vp<n>~loc|noloc; ...`, the war missions against nation opp),
`pocbattle <opp> <mission> [map area index]` (async: the debug "Test battle" without its input dialogs = `DoBattle`; the
`TdlgBattlePreview` "Battle preview" follows, answer **Accept**; it autosaves into the open slot: backed-up runs only;
the area index (the bridge's `areas` list, 0.4.0, test only) is for nations that share no home area and whose
missions name no possession, e.g. Italy against the Soviet Union in the missile fixture: `pocbattle 3 1 1` = the
Cruiser battle in The Mediterranean; without it: `pocbattle: no area for mission <m> against <opp>`),
`pocstate` (`ok running=<tmrRunMinute on> moving=<tmrMoveInterval on> frm=1 visible= t=<battle hh:mm> min=<battle
minute> left=<MinutesLeft> finished=<0 before the first run and while a minute runs, 1 at a boundary> pause= autorun=
endgame= engaged= speed=<GameSpeed -3..5> intervals= ivsec=<seconds per interval> playerSide= forces= divs= allships=
af= torps= draws=<sim>/<ui>/<other> gate=<on>/<tokens>`; idle at a boundary = running=0 moving=0 finished=1; 0.3.2:
it ends with `fastest=<0|1>`, the "Fastest available" button's Down, -1 unknown),
`pocrun <minutes>` (async: the Run buttons' StartRun; a battle break (PauseGame) stops it early), `pocpause`,
`pocspeed <-3..5>` (the speed box; it changes the simulation, not only the display), `pocrng <seed> [iso]` / `pocrng
off` (seeded battle RNG behind Random32Proc; `iso` puts display-only draws on a separate, not reproducible stream),
`pocrngstat` (top callers of display/other draws as RVA*count), `pochash on|off|now` (per-minute state hash in the log:
`poc hash #n t=.. min=.. all=<16 hex> pos= dmg= ammo= sight= ident= ord= air= torp= ships=n(side0/side1) ... draws sim=
ui= other= rng=<state> speed=`; `ident` is not part of `all`; in the live battles' `lockstep hash A|B` lines it is the
simulation's identification, equal on both copies since 0.3.3 and engine level 11 (up to 0.3.2 it differed in
every minute: see GAME-INTERNALS "The load's own drawing identifies ships"); after every live load both copies log `the
simulation's ship identification as the battle save leaves it (type 22, no class); this PC's display had identified
<n> ship(s) during the load and keeps that`), `pocdump <file>` (the hashed fields per
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
(`fightbattles 1`) and announced live battles (hello `ls=<live-battle engine level>`: 0.3.x sends 1, the live-battle
limits branch sends its development level, 11 now;
env `RTW3MP_LIVE_BATTLES=0` turns them off, `ls=0`) is fought live, if its level is the host's (`MP_LS_LEVEL`; else the
host fights against the AI and tells that player: "live battles need the same mod version"):
the host holds the battle at minute 0, saves it (the Save button's files, into the host slot: backed-up runs only),
sends the slot's files, both copies load them (the client into its battle slot 87) and run in lockstep. Outside a
month (tests) the host's battle runs in battle slot mode (its close runs no month). Bridge `ls ...` on either copy:
`ls status` -> `ok on=0|1 role=A|B st=idle|holding|loading|waitready|running|ended bid= epoch= side= minute= t=
boundary=<minute of the last boundary> running=<a minute runs> left=<minutes A still releases> speed= fastest=0|1
auto0=<side 0's ships on auto-run 0|1> auto1= peer= peerAI= muted= seq=<own>/<other's batches> pending=<unreleased
own>/<other's> overlay= verified= peerDone= resyncs= ok= end=0|1/0|1 proposal= busy= leave= atb= broken= failed=
peerEnded= lost= rejoin=<A: 0, 1 a dropped battle is remembered, 2 a rejoin waits for the minute boundary>
rj=<this live battle is a rejoin> frames= disp=<dispatcher depth>/<base> hash=<state>-<rng>`
(idle between minutes: both `st=running running=0`, A `left=0`, A `boundary`
= B `boundary` = A `peerDone`); `ls run 1|5|until|<n>`, `ls pause`, `ls speed <-3..5>`, `ls end` (toggles this
player's end request; both requested = the agreed end at the next boundary: the game's own End routine, which refuses
before minute 400 / with enemies near: `The battle can't end yet: <reason>`; after the end the battle screens stay open:
press their "View results" button, then "Exit battle"), `ls order <division index> <course> <speed> [ai]` (as the
course/speed boxes write; becomes an order batch), `ls detach <division index> <ship index>` (async; between minutes
only), `ls divs` (`ok i:side:name:course/speed:ships[:ai]; ...`), `ls hash`, `ls desync` (this copy's next minute
differs: expect `DESYNC` and a resync), `ls drop` (A: side 1 to the AI now), `ls abort` (B: leave), `ls resync` (A,
async), `ls mute 0|1` (this copy drops all lockstep traffic: a silent player), `ls timeout <boundary s> [lost s]
[load s] [heartbeat s] [rejoin s]` (defaults 30 30 120 5 60; kept across battles; the reply lists all five),
`ls trace 0|1` (files `ls_trace_<A|B>_<bid>_<epoch>_<minute>.txt`
(the call sites of the shared random draws, plus state-hash markers at the entry/exit of every outer simulation step and
shared section; decode with the session folder's `ls_tracediff.py <bid> <epoch> <minute>`) and `ls_dump_...` (pocdump)
in the RTW3MP data folder per minute; slows the frames), `ls desyncreport` (engine level 13; between minutes: this
copy's DESYNC report of its boundary now, as a DESYNC writes one on each copy: `lockstep: DESYNC report
ls_desync_<A|B>_<bid>_<epoch>_<minute>.txt written (<n> kB ...)`; compare two with `tools/desync_diff.py`; sections
`draws minute <m> count <n>` (call-site rvas, the last 4 minutes), `fields <class>: <offset>=<name>:<TTypeKind>`
(level 14: the kind; 7 class, 8 method, 13 static array, 14 record, 18 string), `raw boundary <m>` (each minute's
end), `raw start <m>` (level 14: the start of the minute that parted, after this copy's display ran), `state`; since
level 15 both copies also write one when A finds that the loaded battles differ at a start, resync or rejoin: the
line `why the loaded battles differ`, the hashes the READY check compared, the loaded objects as `raw boundary <m>`),
`ls testreadydiff` (test only, level 15: this copy's next live load differs, the first division's course, `lockstep:
TEST desync: ...`; A logs `the loaded battles differ`, both copies write their reports, and the resync after it
loads equal battles), `ls hash`
(hashed in the simulation's set of the dual fields),
`ls view 0|1` (the display swap to the own side from the next load; 0 = diagnosis only).
Engine level 15 (two PCs): `ls mapview [on|off|apply]` -> `ok view=<this copy's side> on=<0|1: the map pass> passes=<n>
side0=<ships shown>/<ships afloat> wake0=<wakes shown> flags0=<division flags shown>/<divisions> side1=... wake1=
flags1=... playerSide=<the game's PlayerSide now>` (what this copy's map shows of each side: B's own ships and flags,
side 0's ships only while sighted and never their flags; `off` gives the game's own picture of the first side from the
next pass, as 0.4.0, `on` and `apply` run a pass now), `ls prefs` -> `ok AIControl=<n> FloatWarning= ReadyPopup=
BreakOnNewSighting= AirApproachMessage= AirSurfaceMessageLvl= LetAIControlForce= LOScalc= SoundOption= Autosave=
FloatplaneLaunch= RealismSetting= NoFriendlyTorpHits= BSSMHC= | DarkMode= Scale= SoundVolume= MainFormState=[ | after the
battle: <Key a -> b, ...>]` (this copy's preferences record now: the 14 battle preferences a live battle takes from A,
four display ones that stay each PC's own, and this PC's own values that come back after the battle), `ls realism`
(0.6.1) -> `ok realism=<RealismSetting in this copy's record: B's is A's during a live battle> name=<Admiral's_mode |
Rear_admiral's_mode | Captain's_mode> torps=<1: the game offers Fire torpedoes (Realism above 1)> said=<this battle's
Realism line shown> a_says=<B: A's start carried rz=1> noted=<the Fire-torpedoes-off notice shown>`, `ls testcwload
0|1` -> `ok testcwload=<0|1> cw=<this thread's x87 control word> battle=<the battle's, A's>` (test only: 0 = B loads
with its own control word, as 0.4.0; 1, the default, sets A's before B's load). Level 15's log lines: `lockstep: battle
preferences: <A's name>'s for the load and the simulation (<Key b -> a>, ...); this PC's own come back after the
battle` (B, a first load whose preferences differ), `lockstep: this PC's own battle preferences again (...)` (the
battle's end on either PC), `lockstep: the Preferences dialog changed battle preferences the simulation uses (...):
this battle's values put back` (the battle screen's Preferences, with a chat line), `lockstep: x87 control word <b> here,
<A's name>'s <a>: the load and the battle use <A's name>'s` (B, only when they differ), `lockstep: map of this PC's
side: ok (...)` (once),
`lockstep: map of side <n> (load|minute|minute start|detach|probe): own ships shown <a> of <b>, the other side's <c> of
<d>, flags shown: own <e>, other <f>` (when the counts change) and `lockstep: the battle screen shows side 1's
ensign` (B, once per battle).
Engine level 16 (CPUs and data files): `ls x87 [bench|selftest|check|reset|soft 0|1|pert ext|dbl|ppm|off [N]]` -> `ok
installed=<0|1: all 11 routines hooked and the stub check passed> soft=<0|1: the software maths now> battleSoft=<this
battle's choice> cpu=<this copy's vendor> peerCpu= cw=<x87 control word now> cwFixes=<times the battle's word was put
back> calls=<Routine=n,... since the load or reset> test=<the perturbation, nudged=<n>> data=<the data files' hash>
files=<n> readyDiag=<the last READY diagnostics>[ bench=<sin a/b cos ... (ns per call, this CPU/software)>][ selftest=
...][ check=stub check ok ...]` (`soft` is A's choice for its next start or resync, test only; `pert` this copy's
nudge, as `RTW3MP_TEST_X87`; the hooks are installed with the first live battle; `selftest` since 0.4.1's fix: the
battle's control word 0x27F, exp, cos, sin, ln, atan, tan, sincos and atan2 (FSINCOS; FPATAN in every quadrant) on
random arguments plus multiples of pi/2 and 45 degrees: `software vs this CPU (max ulps): exp 0 cos 1 sin 1 ln 1 atan 1
tan 1 sincos 1 atan2 1` on an AMD CPU; the offline check is MODLOG "Fix 2026-10-22"), `ls testdatafile <name>[;<name>...]|off`
-> `ok data=<hash> files=<n>` (test only: those data files hash as if they differed on this copy; a name starting with
`tmp`, the game's temporary files, stays out as in the scan (engine level 18; logged "(not hashed, the game's temporary
files: ...)"); `off` puts the scanned list back). Log lines: `x87: 11
of 11 transcendental routines hooked; CPU <vendor>` and `x87: stub check ok (...); software vs this CPU (max ulps):
...` (the first live battle), `lockstep: loaded (x87 routines during the load: <Routine=n,...>; transcendental maths:
software|this CPU's)` (both PCs, each load), `lockstep: CPU <a> here, <b> at <name>; transcendental maths: ...` (B),
`lockstep: x87 routines since the load: ... ; control word put back <n> times` (the battle's end), `lockstep: FPU
control word <w> at <the step's end|the hash|step|minute|release>, the battle's (A's) is <a>: put back (<n> times in
this battle)` (the first 20, then every 100th), `data files: <n> files of Data (but Data\Graphics) hashed in <ms> ms:
<hash>`, `lockstep: battle <bid>: the game data files are the same on both PCs (<n> files, <hash>)` or `... were not
compared (...)` (B), `lockstep: battle <bid> refused: the game data files differ (...)` (B), `lockstep: <B>'s game data
files differ from this PC's: <n> files: <names>` (A), `lockstep: the loaded battles differ: here <h>, <B> <h> (<diag>)`
(A) and `lockstep: <A>: the loaded battles differ (<diag>)` (B), `notice: Live battle: the loaded battles differ:
<diag> (loading again)` (both). The session logs `<player>: CPU <v> (this host: <v>); game data files <the same as this
host's|DIFFERENT|not compared yet>` (host) and `joined <host>: CPU <v> there (this PC: <v>); game data files ...`
(player).
Battle screen probes (0.3.2, in a live battle, either copy): `ls airwin [show]` (the "Show air formations" window;
`show` opens it as the tool bar button does, `TfrmMain.sbAircraftClick`) -> `ok visible=<0|1> items=<rows of its list>
af0=<side 0's formations in the battle> af1=<side 1's> afx=<others> | <col0>~<col1>~<col2> | ...` (type and name,
numbers a/b/c, mission; the runner reads the window's base and status columns cross-process, `air_rows`; statuses seen:
`Unready` at the base, `Ready>Flying`, `Readying>Flying`, `Spotting>Flying`, `Flying`; missions `None`, `CAP`,
`Recon`); `ls ctl <form class> <field>` (a control of `TfrmMain` or of a battle dialog, found through the form variable
the program creates at startup) -> `ok class=<control class> enabled=<0|1> visible=<0|1>[ down=<0|1>
group=<GroupIndex>][ hint=<the control's hint, the rest of the line>]` (down/group for speed buttons; hint since 0.4.0
when the control has one), e.g. `ls ctl TfrmMain sbAutoRunBattle`, `ls ctl
TdlgDivisionReport sbLandCAP` (group 6), `ls ctl TfrmMain cbFireAtWill`. 0.3.2 log lines: `lockstep: <nation> set the
game speed to N (from the next minute).` (the speed arrows, both copies), `lockstep: Fastest available is not available
in a live battle yet; use the speed box.`, `lockstep: Auto-run is not available in a live battle; use Run (until a
break).`, `lockstep: sbLandCAPClick is not available in a live battle (this version).`, before every reload `lockstep:
<class> hidden before the battle is loaded again (ok|FAILED)` (an open modeless battle dialog) and `lockstep: battle
data cleared (ok|FAILED), air formations left <n>` (the game's battle teardown, 3cc0628), once per process `lockstep:
battle dialogs resolved[, not found: <classes>]`.
The live-battle rules of engine level 2 (phase 0 step 2; `ls level`, `ls simside` and `ls airstat` also outside a live battle):
`ls level` -> `ok level=<this copy's live-battle engine> mod=<version> peer=<the other copy's level once a live battle
told it (B: the start's lv=, A: ready's lv=; 0.3.x sends none = 1) | -> announced=<the level this copy announces:
the same unless env RTW3MP_TEST_LS_LEVEL>`; `ls session` adds `level=<this copy's>
levels=<player>:<level>;...` (host); `ls simside` -> `ok sim=<the simulation's PlayerSide> playerSide=<the game's now>
view=<this copy's side, -1 outside a live battle> viewOn= simDepth= uiDepth=`; `ls airstat` -> `ok af= side0= side1=
other= m0=<8 counts by TAirMission: none,recon,CAP,naval attack,ground attack,escort,support,special> m1= st0=<15 counts
by TAircraftStatus> st1= confirm=<formations still to confirm their target> aistrike=<formations the computer's
CheckAIAirstrike made for side 0>/<side 1> ainaval=<of them naval attack> aicalls= cleared=<confirmations cleared>
planSkipped=<search re-plans skipped> missileCalls= searchCalls=<forces' air searches with PlayerSide 2, engine level 6>
landcapCalls=<land-CAP checks of the mod's copy, level 6> rules=<6 digits: strikes, search plan, missile fire, missile
panel, land-CAP airfield side, scout question hooks in place>` (the counters cover the live battle since it started on
this copy); `ls fastest 0|1` (live battle: asks the clock for Fastest available off/on, as the button does; A's chat
`<side> turned Fastest available on (from the next minute).`; the next release carries `fastest=` and both copies set
the button and the speed: on = 5, off = 2; a speed request turns it off) and `ls fastest` -> `ok fastest=<applied>
down=<the button> want=<A: for the next release | -> announced=<B: A's last> shared=<the button's routines were
found>`. Log lines: `lockstep resolve (live-battle engine <level>): CheckAIAirstrike=... SetAIAirSearchPlan=...
CheckMissileFiringForAllShips=... UpdateMissilePanel=... Fastest(SetDown=... ImageIndex=...)`, `lockstep hooks: ...;
live-battle rules: air strikes on, air search plan on, missile fire on, missile panel on, land CAP airfield side on,
scout question on; ...`, `the computer's air strikes (CheckAIAirstrike <side>) made <n> formation(s) for side 0 and <n>
for side 1 ...`, `the computer's air search re-plan skipped (both sides are human)`, `missile fire under the
computer's rules for both sides`, `the missile panel's counts are not updated on this PC (side 1)`, `Fastest available
on|off`; a level mismatch: B `battle <bid> refused: the live battle engines differ (level 1 there, 2 here): live
battles need the same mod version`, A (level 2 or later, B older) drops B at its `ready` with the same reason.
Force orders and Save of engine level 3 (phase 1; `ls forces` also outside a live
battle, the others only in one): `ls forces` -> `ok <i>:<side>:<name>:divs=<division indexes in the force's list
order>:lead=<the first of them with ForceLeadDivision and ships | ->:changed=<HasChangedLead>:turnaway=<TimeToTurnAway>
:ai=<AIControlled>;...` (indexes into the battle's forces and divisions lists; no game routine is called); `ls forces
div` -> `ok <i>:<side>:<name>:force=<myForce's index>:admin=<Admin>:lead=<LeadDivision's index | ->:role=<Role>
:flead=<ForceLeadDivision>:ai=<AIControlled>;...`; `ls chforce <division index> <force index>`, `ls lead <force index>
<division index>` and `ls turnaway <force index> <division index>` (this copy's own side; the same queue as the ship
tree's Change force menu after its question and as the Select lead dialog's OK; `ok` or `error <the reason the dialog's
player would read>`, e.g. `The lead of this force was changed once already.`, `... is ordered already.`) record a
deferred action: chat `<what> ordered; it takes effect when the next minute starts.`, log `orders #<n> (deferred, 1
items) ... D<kind>...`; at the next release both copies log `deferred <what> of side <s> carried out at minute <m>
(given for minute <g>)` (or `not carried out ...: <reason>`, and only the giving copy's chat says `Not carried out:
<what> (<reason>).`), the giving copy's chat shows the game's own message (`Battle: Force lead division changed! ...`,
`Battle: Battle turn away performed! ...`), the other copy logs it as `deferred action's box '...' answered 1 (quiet)`;
`ls pending` -> `ok n=<count> #<batch>:<kind>:<minute given for>:<object>><object>;...` (this copy's deferred actions
no release has carried yet); `ls save` (as the Save button: a clock request; A, the host's own battle: at the next
boundary without a running minute `battle saved into slot <n> ...` and both chats `Battle saved (minute <m>) in the
host's campaign slot: if the host's game stops, loading that slot resumes the battle, and it waits for <B's player> to
join the host's session again (or goes on against the computer).` (since 0.4.0; before: `... finishes the battle
against the computer.`); a
battle between two joined players: `<who> pressed Save: the host keeps battles between two players, so nothing is
saved.`). A resync drops pending deferred actions with the chat `Not carried out (the battle was synchronised again):
<what>. Give the order again.` (the end of the live battle: `(the live battle ended)`), and after the resync's load
`side 1's computer control as before the load: <n> of <m> forces, <n> of <m> divisions` (engine level 13: after every
load also `side 0's computer control as before the load: ...`, from the start message's `ai0=`). The state hash has a
ninth
group `force=` (each force's name, side, division names in order, HasChangedLead, TimeToTurnAway, ForceWithdraw,
AIControlled, Rallied, FlotillaAttack; each division's myForce, LeadDivision, Role, ForceLeadDivision, Admin). After a
campaign load that resumed a battle, the log says `a battle in progress was resumed from a save; side 1 is the
computer's ...` (or, if side 1 still had ships flagged human, `... the computer commands side 1`).
Auto-run per player of engine level 4 (phase 1b; `ls level` says `level=4`; only in a live battle). The log at
startup: `lockstep resolve (auto-run): on (dialog ..., home orders ForceWithdraw+0x75 := 6 ForceMission+0x1c := 2, also
'Support Force', flag division ..., button +0x610)` (`OFF`: the button keeps the old chat line `Auto-run is not
available in a live battle; use Run (until a break).`). `ls autorun on|home|off` (this copy's own side; the same queue
as the auto-run button after its question: `ok` or `error <reason>`, e.g. `Your ships are on auto-run already.`, `Your
ships are not on auto-run.`) records a deferred `autorun <side> <mode>`: chat `Auto-run on ordered; it takes effect when
the next minute starts.` (`Auto-run on, all forces to the closest friendly base ...`, `Auto-run off ...`); at the
release both copies log `auto-run <mode> for side <s> at minute <m> (<n> forces ordered to the closest friendly base;
auto-run <a0><a1>)`, `side <s> ai=1 human=0 (...)` (off, since engine level 12: `side <s> back to its player as in
single player: the lead divisions of <n> forces; <k> forces stay with the computer as before auto-run; <d> divisions
stay with the computer (<s> ships)`) and, when side 0's auto-run changes, `the
simulation's PlayerSide is 2 (auto-run 10|11)` (back: `is 0`); the giving copy's chat `The computer commands your ships
now (auto-run)[, and all forces head for the closest friendly base]. Press Auto-run again to take them back.` (off: `You
command your ships again (auto-run off). As in single player, you get each force's lead division back (<n>); the
computer keeps its other divisions until you clear "AI controlled" in their division reports.`) and its auto-run button
down (off: up); the other copy's chat `<player>
handed their ships to the computer (auto-run).` / `<player> commands their ships again (auto-run off).`. Off gives back
what single player's Auto-run off gives back (`sbAutoRunBattleClick`: the flag division of every force that is not
computer-controlled; the other divisions stay `AIControlled`, `ls forces div` `ai=1`), and the side's forces get the
computer control they had before auto-run (both copies record it at the release; a resync carries it as `pa=<side
0>/<side 1>`, one `0`/`1` per force; `ls forces` `ai=` as before auto-run). `ls autorun` ->
`ok auto0=<side 0 on auto-run> auto1= mine=<this copy's side, counting its pending autorun orders> announced=<B: A's
last info, a0a1> sim=<the simulation's PlayerSide> clock=<A: both sides' continuous run> feature=<the pieces were
found>`; `ls status` has `auto0=`/`auto1=`; `ls forces` adds `:withdraw=<ForceWithdraw>:mission=<ForceMission>` per
force (home: 6 and 2 for the side's forces that were not computer-controlled and its 'Support Force's; `ai=1` for every
force of a side on auto-run, `ls forces div` `ai=1` for its divisions). Both sides on auto-run: A's chat for both `Both
players' ships are on auto-run: the battle runs to its end at the fastest speed. ...`, `ls status` A `left=1073741823`,
the next release carries `fastest=1 speed=5`, a break logs `a break at minute <m> ignored (both sides on auto-run): ...`
instead of `The battle paused (...)`, and the game's "... Do you want to end the scenario?" is answered 6 on both copies
(log `minute box '...' answered 6 (both sides on auto-run)`, chat `The game asks: "..." Both players' ships are on
auto-run: yes, the battle ends.`), then the battle's normal end; an `off` of either player: `A player took back command:
the battle pauses after this minute.`, `left=0` and the clock as before. A resync carries `auto=<a0><a1>` (A's log
`battle <bid> epoch <e> sent (... auto-run <a0><a1>)`); after each load both copies log `side <s> is on auto-run: the
computer commands it again after the load (its forces' control before auto-run: <flags>)` and `battle loaded from slot
<n> (...), auto-run <a0><a1>`; the snapshot's save logs `(the sides on auto-run under their players)` (the host's crash
checkpoint has every side under its player, its forces as before auto-run).
When the live battle ends on A with side 0 on auto-run (`ls drop`, B gone, a failed load), side 0 goes back to A as an
Auto-run off does (`side 0 back to its player as in single player: ...`; the chat above unless the battle is over). The
state hash's `force=` group also covers each force's ForceMission.
Manual torpedo salvoes of engine level 5 (phase 2; `ls level` says `level=5`; `ls mounts` in any battle, `ls torp`
only in a live battle). The log at startup: `lockstep resolve (torpedo salvoes): on (FireMount ...: submerged tubes up
to 25 knots unless ship type 13; hook possible, dialog fire 1, missile test 1; mounts +0x9e, TorpsLeft +0xf1,
FiredMountThisTurn +0xb1a; bridge list slots 0x16c/0x170, items +0x2d8 object slot 0x18)` and `lockstep hooks: ...;
torpedo salvoes on` (`OFF`: the two buttons keep a "not available" chat line). The division report's *Fire torpedoes*
(`TdlgDivisionReport.bnTorpsClick`) and the torpedo dialog's *Fire* (`TdlgTorpTargets.bnFireClick`) work between minutes
only (else chat `Pause the battle first: torpedoes are fired between minutes.`) and not while this copy's own side is on
auto-run (engine level 12: chat `Your ships are on auto-run: the computer fires their torpedoes and missiles.`, the same
notice as *Manual fire*, from the player's auto-run order on; the release carries each player's orders out in the order
given, so a salvo ordered before the auto-run order, in the same pause too, fires ahead of it, and a `torp` of a side
already on auto-run at its item is skipped: `...
not carried out at minute <m>: the side is on auto-run`); the game enables *Fire torpedoes* only
with the battle preference Realism above its second level (A's preferences on both copies; the game's default is 1,
Rear admiral's mode). Since 0.6.1 both copies' chat names the battle's Realism when it starts: A `Realism: <mode> (your
battle preference, for both players). ...`, B `Realism: <mode> (<A's name>'s battle preference, for both players;
yours isn't used). ...` (`... Fire torpedoes in the division report works while the battle is paused.` at Captain's
mode, else `... needs Captain's mode, which <you|A> can choose in the Preferences before the next battle. Your ships
still fire torpedoes on their own.`); B gets A's line through A's info message when A's start carries `rz=1` and says
it itself otherwise (A on 0.6.0); a rejoin repeats it for B only. Below Captain's mode the first division report on
screen with *Fire torpedoes* off logs `lockstep: a division report shows Fire torpedoes off (the battle's
RealismSetting <n>)`, says once per battle `Fire torpedoes is off: it needs the battle preference Realism at Captain's
mode, and this battle uses <yours|A's>, <mode>...` and sets the notice `Live battle: Fire torpedoes needs Realism at
Captain's mode (see the chat)` (`ls realism`). `ls mounts <division
index>.<ship index>` (or the index in the battle's list of all ships) -> `ok ship=<name> side= sunk=
speed=<CurrentSpeed> type=<ShipType> fired=<FiredMountThisTurn> mounts=<n> <i>:type=<MountType, 0 =
torpedoes>:pos=<Position>:tubes=<Tubes>:left=<TorpsLeft>:reload=<ReloadTime>:reloads=<Reloads
aboard>:destroyed=<Destroyed>:sub=<IsSubmerged>;...` (the mount index is the `torp` order's). `ls torp <division index>
[ship index in the division | -] [target index | first] [mount index | first]` (async: the reply is `ok`, the result is
logged as `ls torp -> ...`; this copy's own division; between minutes) takes a player's path: the division report
(`ShowDivisionDialog`), its *Fire torpedoes* (the hooked handler; the game's own "Ship has fired mount this turn! ..."
box if the selected ship fired a mount this minute), the dialog's ship box (`cbShipsInDivisionChange`), the target list
(`lbTargetsClick`: the game's firing solutions; `first` = the first target with a solution, for the given mount if one
is given), the mount list (`lbMountsClick`: the line with that mount, or the first HIGH/LOW line) and *Fire* (the hooked
handler: the dialog's own course jitter, then FireMount's hook) -> `ok queued=<0|1> target=<i> mount=<i> | result:
queued #<batch> | refused: <reason> | the game's own refusal (...) | solution: <the line fired> | ship=<name>
button=<Fire torpedoes enabled 0|1> | solutions: <i> <line> [mount <i>]; ... | targets <n>: <i> <line>; ...`, or `ok
queued=0 target=<i> (no firing solution) ...` (ships out of torpedo range: run to closer contact first), or `error
<reason>` (since engine level 12 also `error the division report's Fire torpedoes is disabled (button=0: ...)`: the
probe respects the player's gate, the button the game disables below Realism 2, and calls its handler only when the
button is enabled). A queued salvo: chat `Torpedo salvo ordered; it fires when the next minute starts.`, log
`torpedo salvo
ordered (#<batch>): <ship> mount <i> at <target>, course <c>, speed <s>, range <r>` and `orders #<batch> (deferred, 1
items) ... Dtorp...`, `ls pending` `#<batch>:torp:<minute>:<ship>><mount>><target>><angle
bits>><speed>><range>><manual>;`; a second order for the same mount before the release: chat `A torpedo salvo from that
mount is ordered already; it fires when the next minute starts.`; a fast ship's submerged tubes (above 25 knots, not an
MTB): the game's own box `Submerged tubes not useable at speeds above 25 knots!` on the giving copy (answer it with a
posted click), result `the game's own refusal (...)`, nothing queued. At the release both copies log `deferred torpedo
salvo of side <s> carried out at minute <m> (given for minute <m>): <ship> mount <i> at <target>, course .., speed ..,
range .., manual 1: <n> torpedoes in the water (<left before> -> <left after> left, reload <ReloadTime>)` (or `deferred
Torpedo salvo of <ship> of side <s> (given for minute <g>) not carried out at minute <m>: <reason>`, e.g. `it was meant
for minute <g> and arrived too late` when the order missed its release: the giving copy's chat `Not carried out: Torpedo
salvo of <ship> (<reason>).`), and the giving copy's chat says `Torpedo salvo fired: <ship> launched <n> torpedoes.`;
`ls mounts` then shows the lower `left=` (and a `reload=` count-down with reloads aboard) on both copies. When a minute
starts with the torpedo dialog open, that copy logs `the torpedo dialog hidden as the minute from <m> starts (ok)` and
its chat says `The torpedo dialog closed as the minute started: its firing solutions hold only between minutes.`. The
state hash's `ammo=` group covers every ship's torpedo and missile mounts (TorpsLeft, ReloadTime, Reloads, Destroyed),
the `torp=` group each torpedo's Speed, Calibre, RunDist, TorpedoReliability, CheckedFail, Close and SettingLow too; `ls
trace` dumps list the mounts (`M<i>` lines under each ship) and the torpedoes (`T<i>`).
Routine air operations and land-based CAP of engine level 6 (phase 3a; `ls level` says `level=6`; `ls airforce`
and `ls landcap` without an argument in any battle, `ls airops` and `ls landcap <division>` only in a live battle). The
log at startup: `lockstep resolve (routine air ops and land CAP): air ops on (commit ...: 9 of 9 stores,
ProcessSearchDirectons 1, 18 sectors 1; SearchPatternOrdered +0xac 19 bytes, SearchPatternCurrent +0xbf 76 bytes; scout
question's PlayerSide test found), land CAP on (slots ..., CheckLandBasedCAP ... read ok, fighter test ...)` and in
`lockstep hooks: ...` `land CAP airfield side on, scout question on` and `routine air ops on, land-based CAP on` (`OFF`:
the buttons keep the chat line `Routine air operations is not available in a live battle (this version).` / `Land-based
CAP is not available ...`). The battle screen's *Routine air ops* and the ship tree's *Search pattern* open the game's
dialog for this copy's own force; for the other side's force (a selected enemy map flag) it doesn't open and the chat
says `That force is <player>'s: you can set the routine air operations of your own forces only.`. Its OK makes an order
batch (`orders #<n> (paused|step|..., <k> items)` with `VF<force>#<i>\x1f<field>\x1f<hex>` items, the field indexes
10..19 of `g_oForce`), released like a course change. `ls airforce <force index>` -> `ok force=<i> name=<name> side=
cap=<CapLevel 0-3: Light, Medium, Heavy, Maximum> nightcap= left=<SearchLimitLeft> right= dist=<SearchDistance, 0 = no
search> interval= twophase= repeat= nightsearch= ordered=<SearchPatternOrdered: 19 digits, sector i = bearing i*20>
current=<SearchPatternCurrent: 19 count-downs, a launched sector waits 180 (two-phase) or 300 minutes> flagship=<the
ForceFlagship cache | -> feature=<0|1> | <the ls landcap text>`. `ls airops <force index> [cap=0-3] [nightcap=0|1]
[left=-10..370] [right=-10..360] [dist=0..1000] [interval=0..350] [twophase=0|1] [repeat=0|1] [nightsearch=0|1]` (this
copy's own force; the dialog's OK: the given fields, the others as they are, then ProcessSearchDirectons; the nation's
two-phase tech is not checked) -> `ok <the ls airforce text> | batch=#<n>|none` (none: nothing changed), or `error That
force is <player>'s: ...`; `raw=1` (tests) writes another side's force too, as a display write would: `batch=none`, the
next step puts the agreed values back on that copy and nothing desyncs. `ls landcap` -> `ok landcap0=<division
index>:<name>|- landcap1=... swapped=<1 on side 1's copy between steps: the game's slot 0 holds this player's request>
mine=<this copy's side> feature= rule=<the airfield-side hook> calls=<land-CAP checks of the mod's copy> other=<of them,
the game alone would have tasked the other side's airfield> airfields=<division index>:<side>:<CappedDivision's index |
->;...` (landcap0/1 always in the game's layout). `ls landcap <division index>|-` (this copy's own request, as the
division report's OK with its land-CAP button down; `-` clears it, which the game's dialog can't) -> `ok <the ls landcap
text> | batch=#<n>|none`. At the minute's start both copies log `land CAP of side <s> over <division>: <airfield
division> covers it[; the game alone would have tasked <division> of side <t>] (t=..)` when the assignment changes (`no
airfield of this side with fighters` when it has none), and one airfield division's `CappedDivision` shows the request
in `ls landcap`'s `airfields=` on both copies. The forces' air search runs with the simulation PlayerSide 2 (`the forces'
air search with the simulation PlayerSide 2 (no scout question for either side) from t=..`, once per battle): carriers
launch their scouts from both sides' settings with no "Do you want <ship> to launch scout aircraft?"; floatplane
questions (battle preference *Floatplane launch* = Ask before launch) are answered No for both sides and only logged
(`air search box '...' answered 7 (not shown)`). A resync carries `ncap=<one digit per force>`; after each load both
copies log `night CAP as before the load: on for <n> of <m> forces (<k> put back)`. The state hash's `ord=` group also
covers each force's ten air settings, both land-CAP requests and each division's CappedDivision; `air=` each force's
SearchPatternCurrent; the `ls trace` dumps list them (`F<i>` lines, an `L landcap0=.. landcap1=..` line). For the
1935 campaign: the recon formations (`ls airstat` `m<side>=` second count, mission recon) leave in the ordered sectors;
`ls airwin` lists this copy's formations. **The real dialogs without the mouse** (stage B proof): the ship tree's popup
*Search pattern* on a force node (`ob_tab`, TVM_SELECTITEM on the node, `popup_pick(pid, tree, 'Search pattern')`) opens
`TdlgSearch` modally for that force: take the copy out of the dialog answerer first (it presses the OK of a `Tdlg*`
window while the battle screen is disabled), click a `rgCAP` radio (`Light`..`Maximum`) with BM_CLICK (or WM_COMMAND
BN_CLICKED to the group box), toggle `Night CAP`, `Two phase search`, `Night search` with WM_COMMAND BN_CLICKED to their
parent (leave a disabled box alone: two-phase needs the nation's tech, the posted message would toggle it anyway), set
the spin edits (`seLeft`, `seRight`, `seDistance`, top to bottom) with WM_SETTEXT from a local buffer (marshalled; the
window text read back cross-process stays the old one), then BM_CLICK `OK`. Land CAP: the tree's *Status...* on a
division node opens its report; a posted click at the land-CAP button (`LANDCAP_AT`), the game's "Request land CAP for
this dvision instead of <division>?" (Yes), then the report's `OK`.
Air strikes of engine level 7 (phase 3b; `ls level` says `level=8` since phase 5a; `ls airlist` and `ls strike` in
any battle,
`ls airlaunch` and `ls strikedlg` only in a live battle). The log at startup: `lockstep resolve (air strikes): on
(LaunchSomeAircraft 00EA4A18: both dialog calls 1, error message 1, stack arguments 1; AFID counter 00FB0120, ScoutCount
00FB011C; dlgStrike ..., styles ..., Execute ..., FillSquadronList ...; Stand down's weapons return 1; TPointLL 12 bytes
1; fields 1)` and in `lockstep hooks: ...` `air strikes from the strike dialog on` (`OFF`: *Order an airstrike* keeps
the chat line `The strike dialog is not available in a live battle (this version).` and the computer flies both sides'
strikes). At the first start both chats say `Air strikes: launch your carriers' strikes with "Order an airstrike" while
the battle is paused; the computer flies only the airfields' strikes.` (host option on: `Air strikes: the computer flies
both sides' carrier and airfield strikes (the host's option); "Order an airstrike" still launches your own strikes while
the battle is paused, besides the computer's.`; the strike dialog itself still launches then, and the first time a
player opens it in the battle their chat says `The computer flies both sides' carrier strikes in this battle (the host's
option "Computer flies carrier strikes"); the strikes you launch here fly as well, besides the computer's.`). Session
level (host): `ls autostrike [0|1]` -> `ok
autostrike=<0|1>` (the Multiplayer window's check box *Computer flies carrier strikes in live battles*, kept in
`config.ini` as `autostrike=`; the next live battle's start carries it as `autostrike=` and both copies use A's value);
`ls session` shows `autostrike=`. `ls airlist` -> `ok n=<formations> mark=<the air list count the release's sort
starts from> afid=<the AFID counter> scout=<ScoutCount> launches=<this copy's launches from the screen> replays=<the
other copy's, replayed here> failed=<replays not carried out> sorts=<releases that reordered launches> next=<side 0's
next k>/<side 1's> feature=<0|1> auto=<the battle's autostrike> fresh=<the strike list was filled after the last minute>
| <AFId>:<side>:m<Mission: 0 none, 1 recon, 2 CAP, 3 naval attack, 4 ground attack, 5 escort, 6 support, 7
special>:s<Status: 0 unready, 1 readying, 2 ready, 3 spotting, 4 spotted, 5 taking off, 6 flying, 7 attacking, 8
returning, 9 landing, 10 striking below, ...>:o<OrderedState>:<AircraftNumber>/<LoadedNumber>:l<Loadout>:p<parent
AFId|->:L<lead AFId|->:g<GroupIndex>:c<ConfirmTargetLoc>:t<target lat,lon>:at<location lat,lon>:<name>; ...` (air list
order; compare both copies: the same ids, order, numbers, names). Launches from the screen get AFIds 1000000+k (side 0)
and 1100000+k (side 1); the game's own formations keep the counter's ids. `ls airlaunch <AFId> <number> <mission>
<lat> <lon>|@<division>.<ship>|@<ship list index> [loadout] [spot 0|1] [state 6|2]` (this copy's own formation,
between minutes; mission by number or name: recon, cap, naval, ground, escort; the target in degrees (TGlobe units:
5,400,000 per degree) or a ship's location; loadout default the source's (1 if none: 1 light, 2 medium, 3 heavy, 4
torpedo, 5 missile, 6 two torpedoes, 7 two missiles); state 6 = Launch (asFlying, default), 2 = Ready (asReady)) calls
the game's launch routine as the strike dialog does (none of the dialog's own settings afterwards) -> `ok afid=<the new
AFId> name=<the generated name> recorded=1 batch=#<n>`, `ok none (no undamaged aircraft to launch)`, or `error <the
reason the dialog's player would read>`, e.g. `Pause the battle first: aircraft are launched, readied and stood down
between minutes.`, `that formation is not this player's`. Logs: the launching copy `launch from the screen:
A<source AFId>#0 -> AF<new AFId> '<name>' (<n> aircraft, mission <m>, ordered state <s>, loadout <l>, spot <0|1>) at
t=..` and `orders #<n> (action, 1 items) ... Alaunch...`, then `orders #<n> (paused, <k> items)` with
`VA<AFId>#0\x1f<field>\x1f<hex>` items (the dialog's settings: field indexes of `g_oAir` 0 Mission, 1 Loadout, 2
LoadedNumber, 3 OrderedState, 4 Status, 5 StatusCount, 6 GroupIndex, 7 LeadAirFormation, 8 CombinedStrikeSpeed, 9
PriorityTarget, 10 ConfirmTargetLoc, 11 TargetLoc, 12 Location, 13 AircraftNumber) and `VS<carrier>#<n>\x1f7|8\x1f..`
(AirTorpedoes / AirToSurfaceMissiles after a Stand down); `"confirm target location" cleared on A<AFId>#0 (not
available in a live battle)` when the dialog's check box was on; at the release the other copy `replayed launch from
A<source>#0 -> AF<AFId> '<name>'` (`NOT carried out: <why>` / `made no formation`: a hash mismatch and a resync follow),
and when both launched at that boundary, the copy whose own launch had the higher id `<n> formations launched at this
boundary put into one order (AF<first> first)`; no resync. A launch that a resync drops (B's, not yet released): B's
chat `Not carried out (the battle was synchronised again): Launch <name>. Give the order again.`. `ls strike` -> `ok
visible=<0|1> division=<index>:<name>|- target=<lat,lon> fresh=<0|1> ready=<1 | 0 (<why>)> hwnd=<the dialog> lv=<its
squadron list view> launch=<bnLaunch> ready=<bnReady> standdown=<bnStandDown> autoselect=<bnAutoSelectStrike>
cancel=<bnCancel> rows=<row>:<AFId>:<selected 0|1>;...` (handles in hex). `ls strikedlg [report index]` (async: logged
as `ls strikedlg -> <the ls strike text>`) opens the dialog as *Order an airstrike* does (with its pause request while
the clock runs) or for that report of the battle screen's reports list (`lbReports.Items.Objects[i]`, a `TReport`) as its
*Launch strike* (the report is the target); the game may answer `No airstrike capable units available!` (OK).
**The real dialog without the mouse** (the live proof's path): `ls pause` and wait for the boundary on both copies;
`ls strikedlg <report>` (a report of an enemy force; or `ls strikedlg` and a target from the dialog's own Select target
button); `ls strike` for the handles and rows; mark squadrons with the list's own item check boxes: cross-process
LVM_SETITEMSTATE on `lv=` with the state image 2 (`state=0x2000, stateMask=0xF000`; the selection alone, `lv_select`,
gives the game's "No air formations are selected!": Launch, Ready and Stand down act on the checked rows), or BM_CLICK
`autoselect=` (the game picks squadrons, missions and loadouts; it may pick none); a row's mission, loadout or number:
post WM_CONTEXTMENU to the list view with the screen point of that sub-item (LVM_GETSUBITEMRECT 0x1038 into a remote
buffer, ClientToScreen; column 2 = Mission: "Naval strike", "Ground strike", "Escort", "Recon") and pick the popup's item
with VK_DOWN/VK_RETURN as `popup_pick` does (a checked row keeps the dialog's default mission None otherwise); BM_CLICK
`launch=` and answer the handler's own message boxes from the
test (posted messages: "Do you want to coordinate the strike?", "... could carry heavy load ... Do you want to have it
changed?", "... will land at night/dusk ...", "... target is on land. Do you want to change to Ground attack?", "...
Is this your intention?"; Yes/No as the test wants); toggle the dialog's check boxes (`cbCoordinated`,
`cbConfirmTargetLoc`) with a posted `WM_COMMAND` BN_CLICKED to their parent (BM_CLICK doesn't toggle them, see Known
traps). *Ready*: check an unready squadron's row, BM_CLICK `ready=`. *Stand down*: check a ready formation's row
(`ls airlist` `s2`), BM_CLICK `standdown=`, answer "Stand down selected squadron(s)?" Yes. While a minute runs (or before
the boundary was processed) the three buttons only say `Pause the battle first: aircraft are launched, readied and stood
down between minutes.` in the chat; if the list was not filled after the last minute (`fresh=0`), the click refills it
and says `The strike list was out of date and has been refreshed: select the squadrons again.`. The game's own run start
closes the dialog (StartRun 0xdae9e0, on both copies at every release), so it is open after a minute only when it was
opened while that minute ran (`ls strikedlg` then also asks for the pause); at the boundary its list is filled again
on its copy (`fresh=1`); before every reload it is hidden (`TdlgStrike hidden before the battle
is loaded again`; `ls strike` then shows `?` for rows whose formation is gone). The state hash's `air=` group now covers each formation's StatusCount and parent and the AFID and
ScoutCount counters, `ord=` its strike fields (Mission, Loadout, LoadedNumber, OrderedState, GroupIndex, lead,
CombinedStrikeSpeed, PriorityTarget, ConfirmTargetLoc, TargetLoc), `ammo=` each ship's AirTorpedoes and
AirToSurfaceMissiles; the `ls trace` dumps list them (`A<i>` lines, a `C afid=.. scout=..` line). `ls airstat`'s
`aistrike=` counts show the computer's strikes per side: with the host option off only airfield formations of a human
side.
Rejoin of engine level 8 (phase 5a; `ls rejoinable` on any copy, also outside a live battle; `ls rejoin` on the
host): after A drops B (`ls drop`; `ls mute 1` on B, then A's lost timeout; killing the client copy, which the host sees
as a disconnect at once; `leave` on the client) the battle goes on against the computer on A and A logs `lockstep:
rejoin: battle <bid> (epoch <e>) can be rejoined by <name> (<side 1>) while it goes on against the computer (minute <m>,
t=..: <why>)`; A's chat adds `If <name> joins the session again during this battle, <name> gets their ships back.`
(engine level 12: only when a rejoin can work; `... The AI commands <side 1> to the end of this battle; it goes on
here.` when the battle could not be remembered for a rejoin, and `<name> can take their ships back only with a mod that
plays live battles at level <A's> (theirs: <n>).` when the drop came from another live-battle level). A relayed
battle's `norejoin` carries the reason in the returning player's words too (`why2=`, e.g. `<first player>'s game has
no battle of yours going on without you`), which the host's notice uses.
`ls rejoinable` -> `ok on=0|1 bid= opp=<the nation> peer=<name> side=1 dropMinute= minute=<now> age=<s since the drop>
epoch=<the battle's last epoch | -1> pending=<a rejoin waits for the boundary 0|1> why=<the drop's reason | why there
is nothing to rejoin: no live battle was dropped on this PC, the battle screen was closed, another battle started, the
battle is over, a live battle runs on this PC, ...>` (the reason last: it has spaces). B comes back with `join` (the same
name; after killing it, relaunch the copy first): the host's hello gives the nation back and logs `rejoin: <name> is
back as nation <n>: live battle <bid> starts again with them at its next minute boundary` (or `rejoin: <name> (nation
<n>): <why not>`, e.g. after the battle ended `no live battle of this nation goes on without its player`; another
live-battle level, `fightbattles 0` or live battles off: the player's notice `Your live battle against <nation> goes on
without you: the computer keeps commanding <nation> (...)` and a host chat line). Without a reconnect (B dropped while
connected) the host's `ls rejoin [nation]` -> `ok <name> nation=<n>` | `error <why>` does the same at once. Then A:
`lockstep: rejoin: <name> is back for battle <bid> (dropped at minute <m>: <why>): no new minute starts; the battle starts
again at the next minute boundary (minute <m>, t=..)` and chat `<name> is back: the battle stops at the next minute and
starts again with <name> commanding <side 1>.`; a minute held by the gate `lockstep: rejoin: the next minute is held
(minute <m>, t=..)`; Run meanwhile only says `The battle waits: <name> is rejoining it; it runs again once both PCs have
loaded it.`; waiting `lockstep: rejoin: waiting for the minute boundary (<a minute runs | a dialog is open | ...>; minute
<m>, t=..)` (every 5 s); then `lockstep: rejoin: battle <bid> starts again with <name> at minute <m> (t=..) after <ms>
ms[ (the game's auto-run of side 0 becomes side 0's auto-run)]`, chat `Sending the battle to <name> (minute <m>)...`,
`battle <bid> epoch <last+1> sent (..., rejoin)` and the normal load (`side 1 ai=0 human=1 ...` on both copies). B
(fresh, also a restarted copy at the title screen): `lockstep: rejoin: battle <bid> epoch <e> accepted (minute <m>)`,
chat `Back in the live battle: <side 0> against <side 1>, minute <m>. <host> gives you <side 1> back (the computer
commanded it meanwhile). Loading...`; B still in its old copy (`ls mute 0` before `ls rejoin`): `lockstep: rejoin: battle
<bid> starts again here (this PC still had it at epoch <e>, <state>; now epoch <e2>)`; a previous battle's discarded
copy no longer refuses it (`live battle: the battle slot is used again (no return to the own game in between)`). At
`go`: A `lockstep: rejoin: <name> commands side 1 again (battle <bid> epoch <e>, minute <m>)`, both chats `<name> is back
in the live battle at minute <m> and commands <side 1> again. ...`. Check on both copies: `ls status` `st=running
epoch=` equal, A `rj=1 peerAI=0 rejoin=0`; `ls divs` without `:ai` on side 1's divisions; `ls forces` side 1's
`ai=0`; `ls autorun` `auto1=0`; `ls hash` equal, then `ls run` and equal per-minute hashes. Failures keep the computer
on side 1 and the battle rejoinable: `lockstep: rejoin: given up (<the battle did not reach a quiet minute boundary
within 60 s | the battle is over | <name> disconnected | ...>); the computer keeps side 1` (chat `<name> could not
rejoin the battle (<why>): the computer keeps commanding <side 1>; the battle goes on here.`), or after the start A's
drop with the same chat and `can be rejoined ... again`. `ls timeout`'s fifth value is the rejoin's wait for the minute
boundary (default 60 s). A relayed battle (two joined players): the host `rejoin: <name> is back as nation <n>: <A's
player>'s PC is asked to
start relayed battle <id> again` (the relay registered again), A runs the same steps; if A can't, A logs `rejoin of
battle <bid> by <name> refused: <why>`, the host `lockstep relay <bid>: <A's player>'s PC could not start the battle
again: <why>` and B gets a notice. The memo is dropped with `lockstep: rejoin: battle <bid> can no longer be rejoined
(<why>)` when the battle screen closes, the battle ends, a new DoBattle runs or a new live battle is held.
Missiles of engine level 9 (0.4.0 phase 4; `ls level` says `level=9`; `ls missilestat` in any battle, `ls fireatwill`
and `ls missiles` only in a live battle; a battle needs missile ships, so a 1950s or later campaign). The log at
startup: `lockstep resolve (missiles): fire at will per side on (gate 1: nations 00FACB4E, record 334 bytes, field
+0x149 < 2 fires; HoldMissileFire +0x69), manual salvoes on (FireMissileMount 00E04718: head 1, count 1, result 1,
any-bearing ship type 12, maximum range 00E4ACCC; FireSSM 00DA6374 1; dialog: list 1, data 1, pending target 1,
submarine branch 1; Manual fire 1, dlgMissileFire 00F53270; PendingMissileTarget +0xa93, 12 bytes 1), submarines
00F53188` and in `lockstep hooks: ...` `missiles: fire at will per side on, manual salvoes on` (`OFF`: *Fire at will*
stays disabled and both sides fire on their own as before engine level 9; *Manual fire* keeps the chat line `Manual missile fire is
not available in a live battle ...`). At the first start of a battle with missile ships both chats say `Missiles: "Fire
at will" on the missile panel is each player's own (on at the start: your missile ships fire on their own, under the
computer's rules); "Manual fire" works while the battle is paused; "Fire mode" is the computer's for both sides (the
game applies it to one side only).`. The
missile panel (shown from 1943; it may need the panel button): *Fire mode* disabled on both copies with the hint `Fire
mode: the game applies it to one side only, so in a live battle the computer sets the salvo size of both sides'
automatic fire alike. Manual fire chooses one, half or all.`; *Fire at will* enabled, checked as this copy's side's
setting, hint `Fire at will: your own missile ships fire on
their own (the computer's rules), from the next minute. The other player sets theirs.`; after the live battle (an end, a
drop) both are the game's again. **Fire at will:** a click (or `ls fireatwill 0|1`: the box and its click handler
`cbShowBaseFlagsClick`, then the guard at once) -> chat `Fire at will off: from the next minute your missile ships fire
only the salvoes you order (Manual fire).` (on: `Fire at will on: your missile ships fire on their own from the next
minute.`), log `Fire at will off for side <s> (this player's order)` and an order batch `orders #<n> (...)` with the
item `VMfireatwill#<s>\x1f0\x1f00` (`01` on); `ls fireatwill` -> `ok mine=<this copy's side's value> faw=<side 0><side
1> box=<the box's FState: 0 unchecked, 1 checked> batch=#<n>|none`. A click while a minute runs is this player's order
for the minute after the next boundary, like a course order (stage C: heldCalls show it from the second minute of a
3-minute run); it is refused with a chat line and the box shows the setting in effect only while the battle can't take
orders (not running yet, a load, a simulation step) or when this copy has no side. A click while the own side is on
auto-run is kept for when the player takes the ships back (the computer's side is never held). From the
minute the batch is released, both copies log once per change `automatic missile fire of side <s>: held (its Fire at
will is off) (<n> division(s) held) from t=..` (back: `at will (0 division(s) held)`); with both on nothing is held. The
first call of the battle logs `missile fire under the computer's rules for both sides (simulation PlayerSide 2), each
side's own Fire at will from t=..`. A held side's ships launch no automatic SSM or SAM salvo (the SAMs at snoopers still
fire: the game's second loop doesn't look at the hold), the other side's do; the hashes stay equal. **Manual fire**
(`sbFireSalvoClick`) while the clock runs: a pause request and the chat `Pausing the battle: open Manual fire again when
it is paused (missiles are fired between minutes).` (the boundary not processed yet: `The next minute is starting:
...`); paused: the game's modal missile dialog (A doesn't release while it is open; B reports busy: `ls status`
`busy=1`). Its *Fire one*, *Fire half*, *Fire all* while a minute runs or (B) a release waits: chat `Pause the battle
first: missiles are fired between minutes.`; this copy's side on auto-run: `Your ships are on auto-run: the computer
fires their torpedoes and missiles.` (the torpedo dialog's notice too, engine level 12). Checked missile submarines
are unchecked first: chat `Missile submarines can't fire salvoes in a
live battle (the game fires them for the first side only): they were left out.`, log `<n> missile submarine(s) unchecked
in the missile dialog (no submarine salvoes in a live battle)`; a `FireSSM` from the screen anyway: log `FireSSM from
the screen refused (side <s>, a missile submarine | not a submarine)` and a chat line. Each launcher's
`FireMissileMount` becomes an order: log `missile salvo ordered (#<n>): <ship> mount <i> (type <t>) at <target | a
location>, bearing <b>, <c> of <N> to fire`, or `missile salvo of <ship> mount <i>: nothing to fire (<why>)` (the
routine would fire nothing: electrics out, launcher destroyed, no missiles left (counting this player's earlier orders
on it), can't bear, inside the minimum range, out of range) or `missile salvo refused: <why>` (e.g. `That ship is not
yours.`); the dialog's summary box counts the predicted missiles (`<n> missiles fired!`, the turn/too-close/abort
counts) and the chat says (usually once per Fire) `Missile salvo ordered: <n> missiles from <ships>; they fire when the
next minute starts.`. `ls pending` lists them (`msl`). At the release both copies log `deferred missile salvo of side
<s> carried out at minute <m> (given for minute <g>): <ship> mount <i> (type <t>) at <target>, bearing <b>, <c> of <N>:
<r> fired, <k> missiles in the air (<before> -> <after> left, reload <r>)`; the giving copy's chat `Missile salvo fired:
<ship> <n>, ... (<total> missiles).`; a skipped one `deferred Missile salvo of <ship> of side <s> (given for minute <g>)
not carried out at minute <m>: <why>` (the ship, its target or the launcher gone or sunk, the side on auto-run, the
routine's preconditions; at another boundary: `it was meant for minute <g> and arrived too late`) and the giving copy's
chat `Not carried out: Missile salvo of <ship> (<why>).` (once per ship and reason). A launcher that can't bear: the
dialog writes the ship's PendingMissileTarget, an order (`VS<ship>#<n>\x1f9\x1f<24 hex digits>`); `ls missilestat`'s
`pending=` counts such ships on both copies after the release, and the simulation fires them (heavy SSM): FireAllGuns
calls ShipFirePendingHeavySSM for every afloat ship at each interval, which fires (up to 4 per launcher) and clears the
target whether anything fired or not, so a pending target is normally gone by the next boundary (stage C: always). A
resync drops unreleased salvoes (`Not carried out (the battle was synchronised again): Missile salvo of <ship>. Give the
order again.`; an unreleased pending target alone, a field order, is dropped without a chat line, like a course order)
and carries `faw=<side 0><side 1>` and `pmt=<ship list index>:<24 hex digits>;...` (the agreed pending targets) in the
start message; after each load both copies log `Fire at will per side <faw>` and `<n> pending missile target(s) as before
the load`. `ls testpmt <all-ships index>` (A, TEST ONLY, 0.4.0 stage C) makes A's next start or resync message carry a
pending target for that ship (the location of the nearest enemy ship afloat; log `TEST: the snapshot carries a pending
missile target for ship <i>: <entry>`), the only way to see the carry-over: both copies then show `pending=1` for that
side after the load and clear it in the next minute. `ls
missilestat` -> `ok faw=<this copy's values, side 0 then 1> agreed=<the guard's agreed values, - outside a live battle>
box=<the box's FState> boxEnabled=<0|1> feature=<fire at will per side><manual salvoes> calls=<automatic fire calls>
held=<divisions held in the last call, side 0/side 1> heldCalls=<calls with a hold, side 0/side 1> | side<s>
ships=<missile ships afloat> heavySSM= mediumSSM= lightSSM= heavySAM= mediumSAM= lightSAM=<missiles aboard: TorpsLeft +
Reloads> pending=<ships with a PendingMissileTarget> air=<missiles in the air> msub=<missile submarines>/<ready:
Downtime <= 0> | ... | otherAir=<missiles of no side> subs=<submarines> | intercepted shotDown=<side 0>/<side 1>
bySAM=<s0>/<s1> decoyed=<s0>/<s1> (0.4.0 stage C: missiles shot down / by SAM / decoyed while attacking that side's
ships, from the "Missile shot down by ..." / "decoyed" lines ResolveMissileHit writes into the target ship's myHits) |
nations 0:<the battle nation of side 0's first ship>:msam<+0x8c>:antimissile<+0x90>:c8=<+0xc8>:gate<the fire-at-will
gate value +0x149> 1:... (the battle's nation records: a battle numbers its two nations 0 and 1; antimissile = medium
SAM can engage missiles) | orders=<this copy's salvo orders>
pendingOrders=<unreleased> replays=<salvoes carried out here> fired=<missiles they launched> skipped= ssmRefused=
subsUnchecked= last=<this copy's last salvo order>` (compare both copies: the stocks, `pending=`, `air=`, `msub=` and
`intercepted` equal at a boundary). `ls missiles <division index> [target index | first] [1|2|4] [all]` (async: the reply is `ok`, the
result is logged as `ls missiles -> ...`; this copy's own division, between minutes) takes a player's path without
showing the dialog: Manual fire's three fills, the target through the dialog's own target click (its shooter list for
that target), the check boxes of that division's ships (`all`: every listed shooter, missile submarines included, to
test their exclusion), then *Fire one* (1), *half* (2) or *all* (4, default) through the hooked handler; its message
boxes are answered quietly ("Fire medium/heavy SAM as well?" with No, the summary with OK) -> `ok queued=<orders>
missiles=<predicted> target=<i> | result: <queued #<batch> (<n>) | nothing to fire (<why>) | refused: <why>> | summary:
<the dialog's summary box> | shooters: <i>:<name>:<checked 0|1>; ... | targets <n>: <i> <the target list's line>; ...`
(`queued=0 (no target ...)`: the dialog lists no target). The state hash has a tenth group `sub=` (each submarine's
Downtime, Location, TorpsFired, and their count); `ord=` also covers each ship's PendingMissileTarget (its first 8
bytes) and both sides' Fire at will; the `ls trace` dumps list the submarines (`U<i>` lines) and an `M faw=<side 0><side
1>` line.
Resume after the host's restart (0.4.0 phase 5b; the engine level stays 9; `ls resumable` on any copy, `ls resume run`
and `ls autostart` on the host's copy). Every snapshot of the host's own live battle (the start, a resync, `ls save`, a
rejoin) also writes `rtw3mp_livebattle.ini` into the slot: log `lockstep: resume: marker of slot <s> written (battle
<bid> epoch <e>, minute <m>, <peer>, sac <hash>-<size>)`. It is deleted by a normal end (`lockstep: resume: marker of
slot <s> deleted (the live battle is over)`), the end of a battle that can be rejoined (`... (the battle is over |
another battle started)`) and a new live battle's hold (`... (a new battle)`); the game's own Save of a battle that can
be rejoined, outside a live battle (`pocsavebtn`, then answer "Exit game?" No), makes it follow the new battle save:
`lockstep: resume: marker of slot <s> follows the game's own save (battle <bid>, minute <m>, sac <hash>)`. **The test:**
a host battle against a joined player's nation (`pocbattle 1 0` in planning, or a real month), a few minutes, then kill
the host copy (its PID; the client logs `lockstep: leaving the battle: the connection to the host was lost` and loads
slot 77 again). Start the host copy again (the same bridge port and log tag) and `load <slot> 0` (a real month's battle
asks "Select autostart point." first: `ls autostart 2` and then `loadasync <slot> 0`, or answer the dialog's second
choice): the host logs `lockstep: a battle in progress was resumed from a save; side 1 is the computer's (...)` and
`lockstep: resume: battle <bid> (epoch <e>) of slot <s> resumed at minute <m> (t=..; marker of <date time>): a live
battle against <peer> (<side 1>, nation <n>); held until <peer> rejoins or Run is pressed` (a test battle also
`lockstep: resume: a test battle outside a month: battle slot mode on (its close runs no month)`; the state the battle
save dropped: `Fire at will per side <faw>`, `night CAP as before the load: ...`, `<n> pending missile target(s) as
before the load`, or `lockstep: resume: the battle save is from minute <m>, the marker's state from minute <m2>: ...`).
Chat: `* The battle loaded from the save is a live battle against <peer> (<side 1>, minute <m>): it waits for <peer>.
Host the session (Host game in this window), and <peer> gets <side 1> back when they join. Or press Run in the battle to
go on against the computer (<peer> can still rejoin it later).` and (not for a test battle) `* Host the session before
this battle ends: ...`; the status line `Live battle against <peer> resumed: it waits for them. ...`; the Multiplayer
window opens without the focus (`windows` lists the `RTW3MP_Window`). A marker that doesn't fit the battle save:
`lockstep: resume: the battle resumed from slot <s> is not taken up as a live battle (<why>)`, and the battle is the
game's own as before 0.4.0. `ls resumable` -> `ok on=1 held=1 rejoinable=1 bid=<bid> epoch=<e> opp=<nation> slot=<s>
slotMode=0|1 serial=<k> loadedAt=<minute> minute=<now> age=<s> pending=0|1
marker=match:bid<bid>:epoch<e>:min<m>:faw<11>:slotmode<0|1> peer=<name> why=the battle was loaded from the host's save`
(`marker=none`, `stale:...` or `-` for a slot above 49; `on=0 held=0` when no resumed battle waits, `why=` then says why
nothing can be rejoined). **Held:** no minute starts; a Run button (or `pocrun`, or auto-run) releases it and runs:
`lockstep: resume: Run pressed: battle <bid> goes on against the computer (minute <m>, t=..); <peer> can still rejoin
it` (a key that runs one minute: `lockstep: resume: a key ran one minute: ...`), chat `The battle goes on against the
computer, which commands <side 1> for now. If <peer> joins the session during this battle, <peer> gets <side 1> back at
the next minute.`; `ls resume run` releases it without a run (`ok released` | `error no resumed battle is held`); a run
started some other way: `lockstep: resume: the next minute is held (minute <m>, t=..)`. **The session:** `host <name>
<port> <limit>` (before or after the `load`): the next tick logs `resume: the campaign in slot <s> is in the middle of
its month (battle <bid> against <peer>, <side 1>, resumed from the save[; a test battle outside a month]): the session
takes the month up (processing)`, chat `* Campaign open: host plays <nation>` and `* The host's month is in progress:
its battle against <side 1> (<peer>) is on screen. The new month follows when the battle and the rest of the month are
over.` (a test battle: `The current month is sent again when the battle is over.`); `status` `phase=processing`, `ls
session` ends with `turn=1 hostSlot=<s>`. The client `join`s with the same name: the hello gives the nation back and the
rejoin of phase 5a runs (host `rejoin: <name> is back as nation <n>: live battle <bid> starts again with them at its
next minute boundary (resumed from the host's save)`, the player's notice `Your live battle against <host nation> waits
for you: the host's game was restarted and resumed it from its last save (minute <m>). You get <side 1> back now; ...`;
A `lockstep: rejoin: battle <bid> starts again with <name> at minute <m> (t=..) after <ms> ms (resumed from the host's
save, held until now)` and a marker with the next epoch; B `lockstep: rejoin: battle <bid> epoch <e> accepted (minute
<m>) (the host's game was restarted)`, chat `Back in the live battle: <side 0> against <side 1>, minute <m>. <host>'s
game was restarted; the battle goes on from its last save, and you command <side 1> again. Loading...`; at `go` A
`lockstep: rejoin: <name> commands side 1 again (battle <bid> epoch <e>, minute <m>) (resumed from the host's save)`). A
player connected before the campaign was taken up is offered the battle with it; during a month the host offers a
resumed battle once to a connected player of that nation (`resume: <name> (nation <n>) is connected: the resumed battle
<bid> is offered`; in planning only a new `join` or `ls rejoin` does). Check as for a rejoin (equal hashes, side 1 human
on both copies, `ls resumable` `on=0` once it runs), then after the battle: a real month runs to its end with the
session's hooks (`DoEndOfTurn finished`, the battle phase if any, `* New month: ...`, the client loads it) and the
marker is gone; a test battle's close runs no month (`battle slot: DoStrategicTurn skipped`) and the session resends the
current month after 4 s (`* The turn did not advance; resending the current month`).
Since 0.3.3 a division report (or the torpedo dialog's Close or Fire) that lowered ships' current speed on this copy
logs `lockstep: the division report lowered <ship>'s current speed a -> b on this PC only: put back (the next minute
lowers it on both PCs)` (up to three ships per opening). `ls testcurspeed <ship index in the all-ships list> <knots>`
(TEST ONLY, live battle, between minutes, given on both copies alike: a ship's CurrentSpeed, e.g. above its maximum as
a worse weather leaves it -> `ok ship=<name> was=<old>`, log `TEST <ship>'s current speed a -> b`; the leak check's
baseline takes the write in): opening that ship's division report on one copy then logs the line above, the speed is
the same on both copies afterwards and nothing desyncs.
Test commands of the review/round-3 fixes (work on either copy; `leakcheck` also outside a battle, the others only in
a live battle): `ls leakcheck 0|1` (a per-process switch, kept across battles: the simulation state (hashed ship,
torpedo and air fields except orders (and, before engine level 17, the identification), plus the division/ship list
order, and since engine level 3
the forces' division lists and each division's myForce and Admin, since level 5 each ship's torpedo and missile
mounts, since level 6 each force's SearchPatternCurrent, since level 9 the submarines' Downtime, Location and
TorpsFired, since level 17 the simulation's set of IdentifiedAsType: a display write of the identification into the
simulation's set is a LEAK `(ident)`) at the end of
every simulation step must be the same at the start of the next one, else `lockstep: LEAK the simulation state changed
outside the simulation (<group>) between <X> and <Y> at t=..; display routines in between: <names>; <ship.field
old->new>` (up to 50 per switch-on); the suite turns it on in every copy and asserts no LEAK line; `ls leakcheck ident
0|1` -> `ok leakident=<n>` takes the identification out of it and back, tests only), `ls testident <ship index> <type>`
(TEST ONLY, between minutes, on one copy: writes the ship's identification into the simulation's set as a leaking
display path would -> `ok ship=<name> was=<type>`; the next step logs `LEAK ... (ident) ... <ship>.IdentifiedAsType`),
`ls shiptypes [<6 hex digits>|sync|nosync|hash|nohash]` (engine level 17, works without a battle: the campaign's
ship-type flags (a battleship, a battlecruiser, a carrier exists; GAME-INTERNALS) -> `ok shiptypes=<hex> sync=<n>
hash=<n>`; a value sets this copy's own (as a processed month leaves them); TEST ONLY: `nosync` B keeps its own in
live battles, `nohash` the flags out of the state hash; both = the 0.4.1 RC), `ls guard 0|1`
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
boundary and `closing the battle screen ("Exit battle" in the results)`: the deferred close), `ls testclock <hours>`
(stage A proof, 2026-10-17; also before a battle: the next start or resync snapshot of this copy as A moves the battle
clock that many hours on, so its `.sac` [Environment] date is later -> `ok testclock=<hours armed>`, log `TEST: the
battle clock moved by <h> hours before the snapshot: t=..`; e.g. 744 for a battle dated a month later, a December one
in January: both copies then log `the load: campaign date <the campaign's> kept (the battle save said <later>)`),
`ls testcurspeed <ship index in the all-ships list> <knots>` (between minutes, given on both copies alike: a ship's
CurrentSpeed, e.g. above its maximum as a worse weather leaves it -> `ok ship=<name> was=<old>`, log `TEST <ship>'s
current speed a -> b`; the leak check's baseline takes it in; opening that ship's division report on one copy then logs
`the division report lowered <ship>'s current speed a -> b on this PC only: put back (the next minute lowers it on both
PCs)` and nothing desyncs), `ls airbase` (stage B proof; any battle, read-only: `ok | <AFId>:<side>:<ScenHomeBase's
name>:field|ship; ... | bases <all-ships index>:<name>:field|ship:torps=<AirTorpedoes>:asm=<AirToSurfaceMissiles>;
...`: which formations a carrier or an airfield launched, e.g. the computer's strikes with the host option off come only
from airfields, and the carrier's air weapons after Ready / Stand down; a sunk carrier's squadrons stay listed with
index -1), `ls flagsel <division index> [0|1]` (stage B proof, TEST ONLY: that division's map flag selected as a click
on the map does (its `TDivLocation` state word +0x28, bit 0x40) -> `ok division=<name> selected=<was>-><now>`, log
`TEST the map flag of <division> selected`: the tool bar's *Routine air ops* (a posted click at (1105, 18) of Panel1)
then opens the dialog for that division's force, which for the other side's force gives the refusal chat line; clear it
again with 0). More than 3 resyncs without 10 good minutes in between (or more than 3 failed minutes without a
good one) drop the client (`dropping Captain: the battles could not be kept in sync`). Session level `ls enable
0|1`, `ls session` (`ok enabled= available= role= hostOpp= hostBid= testSlotMode= leave= clientA= slotMode= relays=
players=<name>(<nation>[,b][,ls]);`), host `battles injectpvp <K1> <K2> <area>` (a live battle between two joined
players' nations for the end of the next month; K1's copy is A, K2's is B, the host relays). Logs (`lockstep: ...`):
`battle <bid> epoch <e> sent (<n> files, <bytes> bytes, seed <s>, speed <v>)`, `battle loaded from slot <s> (seed ..,
minute .., t=..) in <ms> ms` (engine level 10: right before it `the load: campaign date <Y-MM-DD> kept (the battle save
said <Y-MM-DD>)` or `... (as the battle save: ...)`, every live load on both copies), `battle <bid> epoch <e> running at
minute <m>`, per minute `lockstep hash A|B t=.. min=<m> all=.. ...` (same format as `poc hash`; engine level 10 adds
`date=<Y-MM-DD>` at the end: the campaign date), `orders #<seq> (<why>, <n> items) ...`, `orders #<seq> of <peer>
received`, `released orders side0<=#a side1<=#b: ...`, `DESYNC at minute <m>: here <h>, <peer> <h>`, `Synchronising
the battle again (<why>)...`, `dropping <peer>: <why>`, `<peer> left the live battle (<why>). The AI commands <nation>
now; ...`, `The live battle ended on this PC: <why>`, `The battle is over (<why>).` (B at a normal end, engine level
12: `the result box shows this player's side: '<B's text>' (the game's: '<the first side's>')`: the box the battle
screen's close shows, the victory from B's side, the points swapped, without the host's prestige lines; the host's
test battle outside a month logs `battle slot mode ended (the host's test battle outside a month is over)` when its
screen closes), `minute box '<text>' answered
<n>` (the game's questions inside a minute get a fixed answer on both copies: keep the battle going; a sighting box adds
` (side <s>'s sighting)`), `quiet box ...`
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
  meanwhile; only then do `month_checks` and the month hooks run. Relations v2 (B3): while the host runs the player's
  rules for nation 1 (`relrules`), the client then runs the player's relation routines (`relations for nation 1 sent:`
  in its log; their questions are `TdlgEventAnswer` too, boxes and the game's turn report `TdlgCampTurnRep` follow),
  so `run_month`, `ls_month` and `session` (the first month) wait for that line and answer the client's dialogs until
  none came for 2 s (`wait_relations_turn`, `relturn_lines`, `ctx.relturn`). Layout (form resource `TDLGEVENTANSWER`):
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
  `diplomacy` may process months too. Without `diplomacy` its random path takes the last planned month, and
  `battles` (nation 1 at war with an AI nation), `victory_points` and `treaty` process theirs with nation 1 at war:
  select `events` together with them only when `diplomacy` is selected too. The random path waits for a month in which
  nation 1 is at war with nobody (`rel 1 j` on the host) when its planned month comes at war (a scenario that processed
  more months than planned, e.g. `battles`' retries, puts `lockstep_pvp`'s war month there).
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
    Germany"). The runner still claims by name (`nation1()`) as a fallback and fails the check if it had to. Since
    engine level 8 that player also gets the live battle back if it still runs on the host (`rejoin: Captain is back
    as nation 1: ...`; see Rejoin in the bridge reference). In a test battle the month arrives first (`state seq N ->
    Captain`, loaded into slot 77), then the battle (slot 87); after the battle the client loads slot 77 again. A
    `claim` by name while the host's battle screen is open only makes a lobby wish (`CampaignLoaded()` is false) and
    drops the nation until `RematchReservations` gives it back, which offers the rejoin again (nothing happens while the
    rejoin is on its way or running).
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
    (`k2_start`); `ls_log_scan` asserts no `lockstep: LEAK` line (three checks per scan since 0.3.2: errors/hooks, the
    battle's chat lines (pause reasons and "Battle: ..." without a file path or a "Game saved" box), leak). Lines a test
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
  - Since 0.4.0 (phase 5b) a live battle's last snapshot left in a host slot together with its marker
    (`rtw3mp_livebattle.ini`: the battle was dropped or the game closed during it, not finished) makes the next load of
    that slot a resumed live battle: held until a Run (`pocrun` releases it), and a host in the lobby takes its month up
    (`status` `phase=processing`; `start` then does nothing). Finish or end such battles, restore the slot from the
    run's backup (a restored battle save no longer matches the marker: `lockstep: resume: ... is not taken up as a live
    battle`) or delete the marker. To test the resume, keep the killed host's slot as it is (no restore in between).
  - A host battle in which nation 1 scores no VP logs no `victory points: nation 1 +X in its battle against the host`
    line; EnemyVP of the month (trade war) still reaches nation 1 through the month-end share line. Not a failure.
  - Memory: every campaign load costs a copy ~3-4 MB private memory (game level); a live battle loads once at its start
    and once per resync on both copies, so S7 (about 7 resyncs) adds ~25-32 MB and nine battles in a row 91/103 -> 179/203
    MB (host/client; 156/227 -> 258/344 MB later in the full `--stress` run). g) asserts the average growth per battle
    (<= 25 MB), it does not level off.
  - The text of a game box (`TMessageForm`) is in a `TLabel`, which has no window: WM_GETTEXT reads only the caption.
    `box_text` reads it from the copy's memory (read-only): the VCL's `ControlOfs<HInstance><thread id>` window
    property gives the form object, its VMT is checked (TMessageForm, RVA 0x1d5a80), then `Message: TLabel` (+0x438)
    and `TControl.FText` (+0x18c, a PWideChar); offsets from the game's RTTI (1.01.44). Picture messages
    (`TdlgPictureMessage`, RVA 0x2aab44) keep theirs in `lbMessage` (+0x43c); the turn report (`TdlgCampTurnRep`) is a
    TListBox, read with LB_GETTEXT. A box right after a battle
    preview is usually the game's "N destroyers unable to take part in battle due to lack of fuel ...".
  - The host's `status` date is its slot's save file (`HostDate`), the client's is the date the host last announced;
    the host merges a joined player's turn only when the submitted save's [General] Year/Month equal its own save's,
    written just before (`MergeSubmission`: `NOT applied: submission is for another month`).
  - The game's month can hand a neutral possession to a nation ("Germany has taken over control of Shanghai." as a
    picture message and in the turn report; MapData: `Shanghai (MapArea5Possession9): Neutral -> Germany`). In the
    `treaty` month that makes g) ("nothing else moved") fail (d99e357's run: Norway). Every month that moves a
    possession logs `possessions that changed hands this month (slot 1 MapData, ...)`.
  - A real month's live battle is dated by the game's generator, possibly in the next month (the Aug 1900 turn's battle:
    `[Environment]` 1900/9/10 in its `.sac`, logged as `the battle's own date`); [General] keeps the campaign's date.
    A live load puts the battle's day and month into the date variables (the battle runs with them on both PCs); since
    919b40d the host puts the campaign's own date back when the battle's results open (`the battle's results:
    campaign date 1900-09-01 put back (the battle's date was 1900-09-13)`) and when the month runs (`the month runs:
    campaign date ... put back, the record is dropped`), so the aftermath (VP, prestige, the month's records) reads the
    campaign's month. Up to 05b81f6 it read the battle's. That was a guess for the VP mismatch of 05b81f6's default run,
    but it came back with 919b40d (see the next point). Since engine level 10 (0.4.0) every live load keeps the
    campaign's own date on both copies (`the load: campaign date 1900-09-01 kept (the battle save said 1900-09-13)`),
    so the battle itself runs with the campaign's month and day and the results find nothing to put back.
  - Open since 05b81f6 (2 of 3 full default runs with 919b40d, never in isolated `lockstep_month` runs): after
    `lockstep_month`'s `drop12` battle, nation 0's VP in the client's memory are 60/80/100 above the host's. The host's
    memory, its saved slot 1 and the client's slot 77 file as received agree; the client's own `LoadCampaign slot 77
    player 1` gives the higher value (its `victory points shown: own X, enemy Y` line, logged right after the load,
    already has it). Each time the host's side had scored 0 VP in that battle. `vp_mismatch_probe` logs the next one.
    On dist_ll_16 (2026-10-18) it did: the host's own `load 1 0` of its saved slot 1 gave nation 0 +100 (1402 ->
    1502) and the client's second `load 77 1` +20 more (1462 -> 1482; its file holds 1402): every load of that save
    adds points to nation 0 on either PC, so it is the load path, not the package (a product issue, reported).
    **Explained and fixed in 0.4.0** (2026-10-18,
    [MODLOG](../MODLOG.md#fix-2026-10-18-victory-points-added-on-every-reload-v040)): every load credits the blockades
    between nation 0 and its enemies once more (`CheckBlockadeStatus`, 60/80/100 with the fixture's FleetSize 3; each
    load starts from the file's value, so the client's second load was 1402 + 80). Late in a full run GB blockades
    Germany (both build in Northern Europe; Germany's fleet is worn down by the run's battles); the host's 0 VP was a
    coincidence (run 16's failing battle: "Own VP: 246"). `vp_reload_check` makes the blockade on purpose in every
    `lockstep_month` run. The mod puts the load's credit back in multiplayer sessions and for its own reloads only: a
    load without a session (the bridge's plain `load` after `leave`, as the title screen's) keeps it, as in single
    player.
  - `battles` b): the client may copy its next battle into slot 87 within a second of "battle N: fought". The bundle
    comes first, in the host's numbering, then the transform; its write waits while the runner holds the file
    ("succeeded after 1 retries"). Reading that copy gave "34 sunk" (all of nation K) and failed b). `BattleDriver`
    now reads the file's write time before the read: written after "fought" (the mod saves the slot before it logs
    it) = the next battle's copy (`slot 87 was rewritten for the next battle before it was read`). Then the
    cross-check is named NOT cross-checked, and the merge's own changes (as sent -> merged) stand in for the ship counts
    and the month after.
  - The battle screen's modeless tool windows (`LS_MODELESS`: `TdlgShowAirFormations`, `TdlgDivisionReport`,
    `TdlgForceOrders`, ...) are `Tdlg*` classes: the answerer would press their first or OK button (the division report's
    Ok writes the land-CAP setting). It skips them while the battle screen is enabled, and (since 2026-10-21) while the
    tool window itself is disabled: a game box in front of both (the 1935 battle's "You now have reconnaisance aircraft
    at your disposal!" at the first daylight minute) disables the battle screen too, and the answerer once closed A's
    Air formations window right after answering that box (`lockstep_air` c) "after a minute": bases A [], dist_rc041_1).
    `answer_dialogs`/`dismiss_dialogs`
    still don't: close them (`close_modeless`) before `run_month` and before a case ends.
  - 0.4.1 (fix-intel): when the month's roll on the client finds no event, the client runs its spy check (the game's
    monthly event slot: an event or `CheckIntelligence`), and the game's own spy dialogs may show on the client (a
    `TdlgPictureMessage` "Our top spy has managed to get hold of the blueprints for ...", then `TdlgShipStatus`): the
    client's bridge refuses most commands until they are answered ("an event dialog is open"). `session` waits for the
    roll with `wait_event_roll` (2026-10-21; before, `--only lockstep_month --stress` once failed 9 checks behind
    them). A spy check that found something is sent to the host as the player's event of the month (event=-1): the
    host then ignores another event of that month ("second event of Captain in <date> ignored (one per month)"), so
    `events`' forced `eventnow` is ignored after such a spy check; the scenario expects that and checks the event's
    application on the host only when the month's slot was free (the random path checks it in a later month).
  - A 1935 month can stop at the aircraft types screen as a question (`TfrmAircraftTypes` captioned "Do you want to
    continue development of the aircraft type at the top of the list for 640?", buttons Save to file/No/OK), after the
    answerer bought a private venture aircraft (`TdlgPictureMessage` "The Supermarine company has developed a Dive bomber
    as a private venture..."): the month's answerer presses No (2026-10-21; before, `air_units` M1 stalled 240 s and
    `lockstep_air`/`lockstep_strike` could not start their battles behind it).
  - The game's progress box `TdlgLoading` (a progress bar, no buttons) shows during every battle load and hides within
    a second. Once (2026-10-18, dist_vp_2, `lockstep_month`'s `drop12` battle) it was still visible on the host after
    the live battle's start: `autorun_click` waited while any `Tdlg*` window was visible, so `ls_finish_host` stalled
    until it was hidden by hand (`ShowWindow` SW_HIDE; a posted `WM_CLOSE` didn't close it). Not seen in the rerun or
    before. Since 2b44526 `autorun_click` logs a `TdlgLoading` still visible after 15 s with its evidence (caption,
    place, owner, whether the battle screen under it is enabled, the copy's windows, `ls status` and its last load
    lines: `LOADING_STUCK`, also in the run's summary) and no longer waits for it.
  - Popup menus (`popup_pick`, `lv_popup_pick`): a posted key normally moves the highlight within 0.05 s (a live
    probe on dist_vp_2, both copies, force and division nodes). Twice in the dist_vp_2 runs a copy's menu took the keys
    late: once they piled up and overshot ("could not highlight "Status..." (at 2)", `lockstep_limits` f)), once it took
    none for 18 s, and the escape didn't close it either: the open menu kept the client in its menu loop, the next
    resync waited 4 minutes ("waiting for the outermost message loop: a menu or a window move is active") and the
    scenario aborted. Since 7175467 the keys go at a 0.25 s pace with the highlight read before each one (an overshoot
    is corrected), a menu that can't be used is closed with `close_menus` (VK_ESCAPE, then MN_CANCELMENUS sent to the
    menu window) and its thread's GUI state reported, and `close_modeless` and every `popup_pick` close a menu left
    open first. A coastal battery's tree node has the division's menu, but its "Status..." opens no division report:
    `open_divreport` skips batteries (S8 cycles through the tree).
  - A division report needs the ship tree visible: switch Panel2's tab control to "OB" first (`ob_tab`: TCM_SETCURFOCUS 0
    notifies like a click). Selecting a node of the hidden tree raised the game's own box "Cannot focus a disabled or
    invisible window." (#32770, OK). The selection also makes an order batch on that copy (`orders #n (paused, 1 items)
    ... VD<division>`): normal, both copies apply it.
  - The speed arrows `TfrmMain.UpDown1` are a windowed TUpDown (Max 100000, Wrap): a posted WM_LBUTTONDOWN/UP in its
    upper half is "up", in the lower half "down". A battle minute lasts ~8 s at speed -1 and ~15 s at -2: long enough to
    click inside a running minute (`ls status running=1` on the clicking copy). In contact the game sets the speed back
    to 1 after every break, so a boundary at speed >= 2 rarely comes; the arrow rule in contact is checked by clicking
    "up" 4 times: the requests stop at 2.
  - The battle window can be narrower than its form resource (Panel1 988 px on the test PC): tool bar buttons beyond it
    (Auto-run at 1218) can't be seen, but posted clicks at the form resource's coordinates still reach them.
  - A resync handed side 1's divisions to the game's AI in 0.3.0-0.3.2 (`ls divs` `:ai` on both copies afterwards, side
    0 unchanged): after the first resync of a live battle B's divisions were under the AI's control. Since engine
    level 3 both copies put side 1's computer-control flags back after the load (`side 1's computer control as before
    the load: ...`); `lockstep_ui` still reports the counts around its resync as a NOTE.
  - "The battle paused (<text>)" names the last box the mod answered, which could be stale: up to 3cc0628 the first
    break after a resync often read "Game saved! <the host's save path>" (the resync's own quiet save box), and that
    line went to both players' chat. Since d99e357 the mod's quiet boxes are no pause reason and chat texts cut paths to
    the file name (`ChatSafe`); `ls_log_scan` checks the chat lines (122 pause lines in two runs, none with a path).
    Up to engine level 11 a break still named the last box of any earlier minute (after a resync every break read
    `Log lines out of sync! 29/23`) and the second player read the first side's texts. Since engine level 12 a break
    names only a game text of its own minute, each player's own (`The battle paused (an event).` when the minute had
    none for them), and A's `Battle: ...` lines leave out the second side's texts (`the game's text is the other
    player's (not shown here): ...` in A's log). Up to then the host's live-battle chat lines also went to every
    player's Multiplayer window (the session's chat hook sent the host's "* " lines to all); since level 12 they stay on
    the host's PC, and a client's window shows only the lines its own battle gives it.
  - The game's own "Log lines out of sync! <n>/<m>" box (EOTP message routine 0xd8fd5c) followed every live load until
    engine level 11: the battle save splits a battle log entry with a line break into two lines. Since level 12 the
    snapshot writes them as one (`battle saved into slot <n> ... (<k> battle log entries with line breaks written as one
    line)`) and every load aligns the two lists (`the battle log after the load had <n> lines and <m> time stamps:
    aligned (ok; ...)` for an older save); a box that still comes is answered without a chat line (`minute box 'Log
    lines out of sync! ...' answered 1 (the battle log's two lists differ: display data, not shown)`).
  - Dawn and dusk: up to engine level 13 the map's sighting circles (0xd8d084, between minutes) wrote the flagship's
    `LightState` of this PC's own divisions, which StartMinute's division routine reads (GAME-INTERNALS): at a light
    change the PCs could part (`lockstep_ui` e) at minute 32, 05:41; a dusk DESYNC at 16:48 whose reports parted at
    0xdabbc3 on A only). Since level 14 the guard keeps it out: at a light change a copy logs `the display wrote 1
    sighting fields since the last boundary (kept out of the simulation)` (2026-10-17: 19:12-20:01 of an August battle,
    3 lines on A, 4 on B, 308 minutes equal). The test battle's start time varies (05:00, 12:00, 14:17 seen); to reach
    dusk, `ls testclock <hours>` before `ls resync` moves the battle clock.
  - Live battles with aircraft: up to 0.3.2 d042d71 the air formations piled up at every load (A, which built the battle,
    loaded it again: 43 -> 86 vs B's 43, then doubling at every resync; `the loaded battles differ` until the client was
    dropped after 4 epochs, af 688/645). Since 3cc0628 every reload first runs the game's battle teardown (`battle data
    cleared (ok), air formations left 0`); a load then costs ~1-6 MB private memory per copy (1900 and 1935 alike), and
    `lockstep_stress`'s nine battles in a row went 81/81 -> 157/169 MB (8.7/8.4 MB per battle; 0.3.0: 91/103 -> 179/203).
  - The 1935 battle's air activity depends on its random setup: one run had only CAP in the air (1 + 2 formations from
    minute 12), the others recon searches, CAP and a naval strike readying from the first minute (af 23/20 -> 45/38 or
    49/39, up to 27 + 19 formations in the air). `lockstep_air` counts any formation with a mission or a status other
    than idle at its base as "in the air". Its battle save has `ScenarioInProgress=2` (1900 test battles: 1).
  - **The host's campaign date moves with every live battle load** (0.3.0-0.3.2, the game's resume path): after A
    loads a battle (its start, every resync) the host's campaign [General] date is the battle's own date: Day 1 -> 5,
    14, 8, 27, 27 in battles in a row on 3cc0628, 27, 21, 5, 10 on 0.3.1 (each battle a new random day; the next
    battle save, the next turn's save and `status` carry it). Now and then that date is in the next month: the host's
    slot 1 then says Sep while the client plans Aug (full default run on 3cc0628: lockstep_ui's battle saved as Sep
    1901, the auto-run prompt said "Event - September 1901"; `--stress` f): the host's turn after the long battle), and
    the next turn rejects the joined player's orders (`NOT applied: submission is for another month`); when the host
    merged another player's turn that month it reloaded the Sep save and the month after was Oct (the client skipped
    Sep). Not a race of the tests: battles started from a settled strategic screen (`host_settled`) do the same, and an
    extra box at a battle's start is the game's fuel message ("N destroyers unable to take part in battle due to lack
    of fuel ..."). `ls_after_checks` j) fails when the host's month differs from the client's; `gen_date` lines show
    the Day moving. **Fixed in 0.3.2 d99e357**: `Hold` records the campaign date (`lockstep: campaign date recorded:
    Y-MM-DD`), every snapshot saves it into [General], and a battle slot's close puts it back (`campaign date Y-MM-DD put
    back (was ... after the battle)`); the battle itself still runs with the `.sac`'s day and month. Checked by
    `lockstep_ui` h), `lockstep_air` i) and `lockstep_stress` g). In d99e357 a `pocbattle` that replaced a battle
    screen still open on the host (no close in between: the stress cases S1-S7 chain their battles that way) recorded
    the previous battle's day (`recorded: 1900-08-08, -31, -15, ...`). **Fixed in 05b81f6:** `Hold` keeps a record
    that is still set (`campaign date 1900-12-01 kept from the battle before (the variables had 1900-12-26)`), and the
    record is dropped when a month runs (`the month runs: ... the record is dropped`, checked by `lockstep_month`
    per host battle) or a campaign is loaded outside a live battle. **Since engine level 10 (0.4.0)** the battle doesn't
    run with the `.sac`'s day and month any more: after every live load both copies write the campaign's own date
    back, decoded from the date the load built from [General] (`the load: campaign date ... kept`), the host's resume
    of a live battle too (`resume: campaign date recorded from the loaded save: ...`); the hash lines end with
    `date=<Y-MM-DD>`, the same on both copies.
- **Stage A live proof (2026-10-17, engine level 10; MODLOG "Testing 2026-10-17: stage A live proof").**
  - *Fire torpedoes* needs the battle preference Realism above its second level (A's: B gets A's preferences with the
    start). The suite's PC has `RealismSetting=1` in `<Documents>\My Games\Rule the Waves 3\Preferences` (an INI file):
    a torpedo test sets it to 2 for its run, after backing the file up, and puts it back (verified by hash) after the
    copies are closed. Realism 2 also changes the battle: SetForceLeadDiv and DoBattleTurnAway give the old lead to the
    computer only below it.
  - The dialog answerer (`LsAnswerer`) leaves the modeless battle windows alone only while the battle screen is enabled:
    while a game box is up it presses OK on `TdlgForceOrders`. Pause it (`ls_quiet`) and answer the boxes yourself when
    a test drives Force orders, Change lead or Battle turn away.
  - An order a joined player gives right before the host's Run can miss that release; it rides the next one (`carried
    out at minute 3 (given for minute 2)`); a torpedo salvo is dropped instead (`it was meant for minute N and arrived
    too late`). Wait for `orders #<n> of <player> received` on A before `ls run` when a test needs the order in a
    given minute.
  - Every `ls torp` opens the division report: its Execute writes the division's DivisionSpeed (an order: `orders #<n>
    (paused, 1 items) ... VD<division>#0 1 <speed>`), and its FillShipList lowered ships' CurrentSpeed on that copy only
    until dist_ll_3 (a LEAK `<ship>.CurrentSpeed`, then a DESYNC); the mod now puts the speeds back.
  - A `pocbattle` whose live battle is refused before it starts (the player's level differs, `fought against the AI
    (live-battle engine level 9, ...)`) is not in battle slot mode: its close runs the host's month processing
    (commissions, events of the next month) on the host alone. Don't rely on the host's campaign after such a test.
  - Torpedo salvoes in the fixtures: the 1900 battle's fleets meet late (ranges above 20,000 yards for 60+ minutes);
    the 1935 battle (every division on the AI) is in contact from about minute 130 and had torpedo hits (`minute box
    'Enemy ship ... is hit by a torpedo!'`). No ship of either fixture sails above 25 knots with loaded submerged tubes
    (the battlecruisers stay at 24), so the game's "Submerged tubes not useable at speeds above 25 knots!" was not seen.
- Stage B live proof (2026-10-17):
  - A test battle (battle slot mode) leaves its results in the host's campaign in memory: a second `pocbattle 2 0` in
    the same 1935 session gave "Enemy unable to challenge our forces in the area! Gain 102405 VP!" and no battle. Put
    slot 9 back from the run's backup (copy it, keep the backup), let both copies leave, load it again and start a new
    session for the next 1935 battle.
  - `pocbattle` battles start at a random time of day (06:47 in one 1935 battle, 11:xx in the next): at night the
    scouts report nothing until dawn, so a strike test may wait an hour of battle time for a report (or set the search
    sectors toward the enemy; `pocdump <file>` gives every ship's position to aim them).
  - In the first 1935 battle France's airfield strikes sank GB's only carrier (Argus) after about 100 minutes: its
    squadrons stay in the air list (`ls airbase` base index -1) and the strike dialog lists nothing for GB. Launch the
    test strikes early.
  - The dialog answerer presses the OK (or first button) of any `Tdlg*` window while the battle screen is disabled, so a
    test that drives a modal dialog (`TdlgSearch`) or keeps a copy's results up must take that copy out of
    `LsAnswerer.pids` (or pause it); `ls_ans(ctx)` (called by `ls_relaunch`, `k2_start`, `ls_start_test`) puts every copy
    back.
  - `ls_drive_live`'s `killB` predates the rejoin: since engine level 8 the killed client's join gives it the host's
    battle back, so the month loop meets the same battle live again.
  - Killing and starting copies again: `ls_relaunch` keeps the old log as `rtw3mp_<port>.<tag><n>.log` in the mod's data
    folder; take marks of the new log from 0. A killed host's slot keeps its battle save and `rtw3mp_livebattle.ini`
    until the run's restore (the resume test needs exactly that); a third copy started by a test writes slots 76/86.
- The 0.4.0 scenarios (`lockstep_limits`, `lockstep_rejoin`, `lockstep_resume`, `lockstep_torpedo`, `lockstep_strike`,
  `lockstep_missiles`, `air_units`, and the 0.4.0 parts of `lockstep_ui` and `lockstep_air`) were written offline on
  2026-10-17 from `src/mod` and the stage A-C logs. Their first live runs (dist_ll_11, engine level 11: `--group limits`
  twice, `lockstep_rejoin`, `lockstep_resume` to its part g)) found the test bugs fixed in c6cef99, 9d8eaf5 and ea9baa8;
  `lockstep_torpedo`, `air_units`, `lockstep_strike` and `lockstep_missiles` have not run live yet, and the engine level
  12 texts (ll-polish) not at all. The catalog's notes say per scenario what passed.
  - Stopping a run in the middle (the game is needed elsewhere): killing the game copies is not enough, later parts start
    new ones (`ll_relaunch`). Stop the runner's `python` first (its PID), then `taskkill /PID` each `RTW3.exe` whose
    parent it was (and, in `twopc`, the delay proxy: the runner's other `python` child, its PID in the line "the delay
    proxy (PID n; ...)"), then put back what the run changed with the runner's own helpers (`restore(<slot 1 backup>)`,
    `remove_client_slot()`, `remove_slot(BATTLE_SLOT)`, the `SNAPSHOT_DIRS`, `remove_event_files`, and slot 9's backup,
    the Preferences' backup, slot 48/49 and the third copy's slots when the run used them).
  - The dialog answerer and restarts: `ls_relaunch` (and `ls_ans(ctx)`) put every copy back into `LsAnswerer.pids`, and
    the answerer presses "Exit battle" on a results screen. To keep a copy's results up across another copy's restart
    (`lockstep_rejoin` e)), pause the answerer (`ans.paused = True`) around the restart and pop that copy afterwards.
  - `ls_relaunch` passes its `extra` environment to either copy (until ea9baa8 only to the client: `lockstep_resume` g)
    restarted the host without `RTW3MP_TEST_LS_LEVEL`, the battle started normally and the part waited forever for the
    client's refusal).
  - `pocrun` in a battle that is not live stops at the game's own boxes ("Unknown ship sighted!", ...): the answerer
    closes them and `run_alone_until` presses `pocrun` again until the wanted minute.
  - In a 1900 test battle the game itself keeps many divisions under the computer and changes their AI flags within
    minutes: compare the AI counts of the two copies, never with a fixed number. After a Fastest available release the
    speed can be capped at 1 by the contact rule.
  - A formation readied in the strike dialog took more than 20 minutes to be ready in the 1935 battle; `lockstep_air`
    g) waits up to 90 minutes before its Stand down.
  - Engine level 12 (ll-polish): Auto-run off gives a side back as single player does (each non-computer force's lead
    division; the other divisions and the forces that started under the computer stay with it), so a side is not "all
    human" after it. Torpedo and missile salvoes are refused from the auto-run order on, already when it is only pending
    (`EffectiveAuto`); the release's skip "the side is on auto-run" is a safety net: a salvo ordered before the auto-run
    order in the same pause fires first (the release keeps the order given). The battle's chat lines stay on each PC
    (the host no longer broadcasts them): read each copy's own log or chat for its lines.
  - Every live test battle's start (and every resync) saves the host's campaign slot with the battle in progress
    (`sip=1`): only the next processed month's save clears it. Don't `load 1 0` between a test battle and the next month
    unless the test wants that battle back (`lockstep_resume` puts slot 1 back from its own copy first).
  - A resync renumbers the battle's divisions: look forces and divisions up by name after one (`ll_force_pick`,
    `div_index`). A turn-away after a lead change of the same force fails at the release with `the divisions' lead
    chain loops` (the game's own check), until a resync rebuilds the chains.
  - Async bridge commands (`ls torp`, `ls strikedlg`, `ls missiles`) acknowledge with `ok` at once and log their result
    later (`ls torp -> ...`, `ls strikedlg -> ...`): read the logged line (`torp_try`, `strike_dlg`), never the
    acknowledgment. Until 2026-10-18 the strike scenario's report probe took the `ok` of `ls strikedlg 0` for a report
    (`error no report 0 in the reports list` came later), so `lockstep_strike` c)-e) opened an empty dialog.
  - A dialog under another one (the division report under its torpedo dialog) is disabled and takes no click until the
    top one is closed: `close_modeless` closes again while one of the battle screen's dialogs stays visible. A
    division report left open blocks the battle screen's tool bar (`ls_finish_host` closes them first: the host's
    auto-run click did nothing for 3 minutes in a run on dist_ll_16).
  - Torpedo firing solutions come and go with the fleets: `lockstep_torpedo` takes each part at the first boundary that
    allows it (`torp_here` tries every division of a side without running the battle) and does the resync with
    torpedoes in the water right after a release.
  - DESYNC reports (engine level 13: `ls_desync_<A|B>_<battle>_<epoch>_<minute>.txt` in the mod's data folder, the
    newest 12 kept by the mod): the runner deletes those of the battles in which it injected a desync
    (`inject_desync`, RUN['desync_bids']) after the run and keeps any other one as evidence (logged).
  - `ls torp` refuses when *Fire torpedoes* is disabled (engine level 12: `error the division report's Fire torpedoes is
    disabled (button=0: ...)`; before it the bridge bypassed the button). `lockstep_limits` f) checks both.
  - "Auto select strike" may select nothing ("No air formations are selected!" on Launch); `lockstep_strike` d) then
    picks the rows itself. The strike list's columns: name, type, mission, number, H, status, readiness, base.
  - Air search in the 1935 battle (dist_ll_16): the game starts a test battle at a random hour, and a January battle
    that starts in the afternoon gets no report before night (`lockstep_strike` replaces a battle that starts at 14:00
    or later). With the battle preference *Floatplane launch* on "Ask before launch" the live battle answers the
    floatplane question No for both sides (`air search box 'Catapult launch search floatplane(s) from BB ...?'
    answered 7 (not shown)`): only carriers and air stations search, and side 0 often had no report in 150 minutes, so
    the runner sets FloatplaneLaunch=0 for runs with `lockstep_strike`. The reports list is owner-drawn (no item texts):
    count its items (`report_count`) and try `ls strikedlg <i>`; item 0 is not always a report.
  - The option note of the strike dialog ("The computer flies both sides' carrier strikes ...") comes only from the
    real "Order an airstrike" button or the reports list's "Launch strike", not from the bridge's `ls strikedlg`.
  - A squadron at its base whose mission the strike dialog's popup set has mission 3 like a strike formation: the
    computer's strikes are the launched formations (with a parent AFId, `computer_strikes`). The game can refuse a
    carrier ("CV Bearn cannot perform flight operations!", a carrier hit by the airfields' strikes): retry later.
  - Missiles: a computer-controlled force of a nation whose record value is below the game's threshold fires whatever
    the box says, as in single player (`ls missilestat` nations `gate0` for both 1958 nations, the resolve line's "< 2
    fires"), so a side's total SSM can fall while its Fire at will is off: compare the held ships only
    (`msl_held_ssm`: the side's forces under its player). Automatic fire empties the launchers within an hour (Italy's
    40 heavy SSM): manual salvoes and the pending-target resync run before it.
  - A copy whose battle is no longer live runs on its own clock, and a game box stops it (`pause=1`, no results):
    press `pocrun` again (`run_alone_until`; `lockstep_rejoin` e)'s A finishing alone stopped at 17:47 and its B
    rejoined the unfinished battle).
  - `ls trace 1` minute files: the frame markers carry 24 bits of the state hash at that point; they differed on every
    marker in the stage A/B traces (dist_ll_2 to _6, before c9b74b9's identification fix) while every call site was
    equal. `trace_compare` compares the call sites and the marker kinds and counts the hash bits apart (`hash_diff`).
  - `RTW3MP_TEST_LS_LEVEL` changes only what a copy announces; a copy started with it is a different engine level for
    the other copies until it is started again without it (`ll_relaunch` without `extra`).
- 0.3.3's traps (its checks are in this suite since the 0.4.0 release candidate, see the
  [Scenario catalog](#scenario-catalog)):
  - The simulation's identification: `ident=` of the per-minute hash lines is not part of `all`, so a difference never
    desynced by itself; up to 0.3.2 each copy kept the identification its own display drew during a load and `ident=`
    differed in every minute (now and then the battles then drifted apart after "Unknown ship sighted!"). `ls hash`
    resets the draw counters of the copy's next hash line (`draws sim= ui= other=`, never compared). Every live load
    logs `the simulation's ship identification ...` right before `battle loaded from slot` (`ls view 0` would suppress
    both).
  - The division report test: `ls testcurspeed` takes an index of the battle's all-ships list, not a division's ship;
    `ship_index` tries the indexes in order with the test speed and gives each ship its own speed back at once
    (two `TEST ... current speed` lines per index tried, in that copy's log; the hotfix's run found Germany's first
    division ship at index 2). The tree node "Germany Battle Division 2 (Ind)" is the `ls divs` division "Germany Battle
    Division 2". The put-back line names at most three ships per opening (the first three changed). `pocdump <file>`
    takes a path without spaces (the bridge splits its arguments at spaces).
  - Air units: `airunits` counts the game's global list, every nation's units. Up to 0.3.2 every campaign load in a
    process added the save's units to the list again (the host after every merged turn, a joined player every month
    and around its battles, both at every live battle load and resync): the count after a load equals the loaded
    save's `[AirUnits] AirUnitNo` only since 0.3.3, and a save of such a process holds every unit several times with
    the same `AirUnitId` (0.3.3 keeps the first of each at the load). A copy whose campaign had no aircraft (the 1900
    fixture) logs no `air units:` line at its next load (nothing to drop). Make a doubled save for a test offline from
    a copy of the fixture, never in slot 9 itself, and use the bridge `save` only on that throwaway slot. A host's
    campaign must be in slots 1-49 (the mod's `CampaignLoaded()`; from 50 on the slots are the joined players' own): a
    host that loaded slot 69 never left the lobby (a client's `claim` stayed a lobby wish, `start` waited without a log
    line), so `air_units`' copy is slot 49.
  - Game error boxes: the dialog answerers answer every box, also the game's own "Error in <procedure> procedure!"
    ones (access violation, out of memory), so they pass unnoticed unless the runner looks for them: the runner fails
    the scenario and the run's `session` check on any of them (`game_error_boxes`; a run of 0.3.3 showed three on the
    client: `SaveBuildCampaign` at `air_units`' first submit and later). Seen again on the second 0.4.0 release
    candidate (dist_vp_2, 2026-10-18, the full default run): the client's "Error in SaveBuildCampaign procedure! Access
    violation at address 00C5B9D4 in module 'RTW3.exe'. Read of address 00300028" at its first save in the 1935 session
    of `lockstep_air` (the save that keeps the plans right before the live battle loads). Both times: the same code
    address (`@UStrAddRef` with a bad string pointer: -1 in 0.3.3, 0x00300030 now), on the client, at its first
    `SaveBuildCampaign` after it joined a session on another campaign in the same process (1900 -> the 1935 copy; the
    1935 copy of `air_units` -> slot 9). The battle and the rest of the scenario ran normally (the client's return to
    slot 77 loaded its 232 air units). A product issue that predates 0.4.0 (reported; not seen in the other runs of
    the same steps).
    Boxes answered inside a joined player's battle (`battle_dialogs`' own presses) reached `game_error_boxes` only
    since the 0.6.1 RC1 run (`_press` records `GAME_ERROR_RE` and "Missing ship" boxes in `BOX_SEEN` like the other
    answerers): before, a 0.6.0 player's "Error in CreateDivisionForBCBattle procedure! Access violation ..." in its
    battles against the computer went unnoticed (found by the mixed driver's own scan).
  - "Error in SaveBuildCampaign procedure! Cannot create file "...\Save\Game1\RTWGame1.bcs". The process cannot access
    the file because it is being used by another process": the game creates a save file without sharing, so any open
    handle at that moment (the runner's own read, OneDrive, a virus scanner) fails the game's save with its box. The
    runner reads the host's slot right after `battle N applied` (`BattleDriver.poll`'s merged save, `StageWatcher`), and
    after a month's last battle the host's month-end save follows within about a second: dist_rc061_1's default run
    had the box once on the host, one second after `battle 24 applied` in `battle_generator` M2 (the logs were not
    kept); its `--stress` and later runs did not have it. The client's slot collided once the same way (`submit`, in
    the bridge reference). Environmental; count it only when it recurs, with both logs.
  - A test command that changes a copy's memory after its save makes the memory differ from the slot: `relations_war`
    ends with `peacetest` on the client after slot 77's save, and the game's peace changes the loser's and its ally's
    funds ("Russia has scrapped the ship ..."), so `player_copy` f), whose copy is made from slot 77, compares the
    other nations' funds with slot 77's `[NationJ] Funds` (dist_rc061_1: Russia 9694 in memory, 9646 in the slot).
  - A game error box that shows during a bridge command (the game's own except handler, a modal TMessageForm on the
    main thread) holds the command until it is answered: the runner's call times out (`error timeout (main thread
    busy)`, after 120 s), the copy's log is silent meanwhile, and the box is answered by the next answerer that runs,
    so its box check fails in the next scenario. dist_rc061_1's second relations run: `battle_generator` e)'s `battles
    aitest 1 7 Northern Europe` met "Error in DoBattleBetweenAINations procedure! Access violation at address 00603D64
    ..." on the host, and `battle_surprise` (its first answer) and `session` reported it (MODLOG 2026-10-26). Fixed in
    0.6.1 (the game's routine needs a ship of each side that can fight in the area; `ResolveAIBattle` now counts them
    first), guarded by `battle_generator` e2).
  - `ls testcurspeed` (the division report test, `divreport_speed_check`) calls the leak check's re-baseline between
    minutes while this PC's display set of the sighting guard's fields is in place: where the display sees an enemy
    ship otherwise than the simulation (at dawn: t=05:12 in dist_vp_2's run), the next step logs a LEAK of
    `SightingLevel`/`RadarSightingLevel` that is the command's own. The check excuses that copy's first LEAK when it
    names only those fields (`testcur_leak`, also for the scenario's scan); any other LEAK fails. A test-command
    artifact, reported (the command could re-baseline with the simulation's set in place).
  - A test battle the game puts together may have no carrier division on a side (dist_vp_2's default run: the Fleet
    battle at Plymouth with air stations only on both sides), and the strike dialog lists only a side's divisions whose
    flagship is a carrier with aircraft ("No airstrike capable units available!"). `lockstep_air` b) and
    `strike_battle` end such a battle at once and start another in a fresh session (`carrier_divs`, up to 3 battles,
    a NOTE each).
- A test battle's date is random and can lie in the next month (1900-09-13 in an August campaign). Up to 0.3.3 a client
  that rejoined while such a battle was open on the host (`lockstep_stress` S6) got a package saved with the battle's
  date (the hello path saves and sends at once; the reservation path waits while a battle is active): it then planned
  the next month, the host's battle close put the campaign's own date back, and the next real month rejected its turn
  (`NOT applied: submission is for another month`; f) and P1 fail, the scenario aborts). Present since 0.3.0; the
  0.3.2 runs only passed because all their test battles fell in August. 0.4.0 doesn't have it: engine level 10 writes
  the campaign's own date back after every live load, so the hello path's save has it (the polish round's live check,
  item 8).
- Two-PC conditions (`twopc`, 0.4.1):
  - `ls desyncreport` logs `lockstep: DESYNC report <file> written`: a check that looks for a DESYNC must match
    `DESYNC at minute` and "the loaded battles differ" (`TWOPC_DESYNC_RE`), not `DESYNC`; and the command must never
    run on A during a month: `ls_drive_live` counts every `DESYNC` line of A's log (`r['desync']`, `host_battle_checks`).
    `state_ships` runs it on B only and deletes the report at once (the mod keeps its newest 12 reports: a test's
    reports must not push out a real one).
  - A battle file's sighting levels are not the loaded battle's: the load's own sighting routine sets them again (the
    first `twopc` run: the resync's file said 3 of A's 16 ships sighted, the loaded state 4, B's map 4). Compare a map
    with the simulation's state (the DESYNC report's state section), not with the `.sac`.
  - A side's own ships of a computer force are hidden until the enemy spots them (the game's SetHiddenDiv rule, which
    the map pass keeps for the PC's own side): in the month battles A had 9 of its 11 ships in computer forces (coastal
    forces). `map_ref` requires only the own ships of human forces (`ls forces`, `ls forces div`).
  - B's battle window is hidden for most of a load and shown only at its very end: a watch every 30 ms with a margin
    of 0.4 s before the load's end saw nothing (the first `twopc` run). `CoverWatch` polls the form's handle (found
    among the hidden windows too) every ~3 ms and keeps the state changes; `cover_report` requires the window clear
    (the cover) when the game shows it (40-175 ms before "battle loaded from slot" in the runs) and never shown opaque
    from the old picture's end to 0.05 s before that line (it turns opaque 1-4 ms after it). A load without the cover
    shows the window opaque for that last ~0.1 s: a margin as wide as that misses it.
  - A battle window keeps its size from one battle to the next in the same game ([Two-PC conditions](#two-pc-conditions),
    window sizes): check that the two windows differ, not that every load sets the Preferences' size.
  - The client is closed to start it with another environment (the comma locale): it leaves the session first, so the
    host gives nation 1 back at its join (`state seq N -> Captain`); its unsubmitted plans of that month are lost, so
    `twopc` sets its month plans after the restart.
- The 0.5.0 release candidate dist_rc050_1 (2026-10-22):
  - Product issues to report when they recur (MODLOG, Testing 2026-10-22, the 0.5.0 release candidate): the host's game
    box "Error in HandleAIPeace procedure! Access violation at address 01058F0B in module 'RTW3.exe'. Read of address
    000001A5" at `diplomacy`'s forced AI peace (`aipeace 2 1`: nation 2's only war is the players' war with nation 1),
    and `victory_points` h) without any `AI peace: VP of player nation K kept at P` line (the VP of 1 and 2 zeroed). Both
    come from the relations v2 mask around HandleAIPeace (`relations v2: HandleAIPeace runs without the pairs of
    nation(s) 1`): the mod's own AI-peace guards (the players' wars, 0.2.x; the players' VP, 0.4.0) saw nation 1's wars
    as peace. Fixed in b5b0da7 (MODLOG, Fix 2026-10-22: the game's AI peace and the relations mask); a recurrence is a
    regression (the release candidate 2 below). Keep both logs.
  - A month's own changes by the player's side: the joined player's event of the new month (its random event, its
    intelligence result `#-1 'Intelligence'`) changes nation 1's pairs after the host's month, like its relation turn
    (`relrules_month_check` excuses both); a war of the host's nation with nation 1 declared at the month's end opens an
    economy record of its own with the month's date (`econ 1` `pending=1 expenses=0`, logged with the next month as
    `<date>: no ComputeExpenses`): `econ_month_check` then reads the month's own log line.
  - Battle conditions, not regressions (each passed when run again): `battles` b) seven attempts in a row avoided or
    declined by the enemy; `lockstep_air` g) "a formation B readied became ready (status 2) within N minutes (for Stand
    down)" (the check now says why its wait ended: the battle over, the minute, B's list).
- The 0.5.0 release candidate 2 dist_rc050_2 (2026-10-23; MODLOG, Testing 2026-10-23, the 0.5.0 release candidate 2):
  - The game's AI peace under the relations v2 mask (b5b0da7) leaves two host log lines a run must have: `diplomacy`'s
    forced AI peace (`aipeace 2 1`, nation 2's only war the players' war with nation 1) `AI peace (badLoss=1) skipped
    for nation 2: its wars are with players' nations` (a check since 8896b72; the older guard's `... its wars were
    declared by the players` counts too, and nation 2 at war with anyone else is a SKIP-note), and `victory_points` h)
    `AI peace: VP of player nation K kept at P (the game set 0)` for 1 and 2. A missing line, the players' VP at 0 or
    the HandleAIPeace box is a regression.
  - `battles` b) is the game's choice: on the same setup (Spain, nation 7, at war with nation 1 in Northern Europe and
    blockaded by it in both runs of 2026-10-23) the client's `StartPlayerBattle 0 attacks 7 in Northern Europe` gives
    `-> none` ("The Spanish navy declines battle! You gain 360 VP!", outcome `avoided`; or the client's side declines:
    `declined`) or `-> battle`; in the five 0.5.0 runs so far a run fought at its first attempt or not at all (six
    attempts in the default run of 2026-10-23, then the first attempt fought in the next run). Re-run `battles` before
    calling it a regression.
- The 0.6.0 release candidate dist_rc060_1 (2026-10-23; MODLOG, Testing 2026-10-23, the 0.6.0 release candidate
  dist_rc060_1):
  - **A product issue (NO-GO for that candidate):** the client's box "Error in SaveBuildCampaign procedure! Access
    violation at address 00C2B9D4 in module 'RTW3.exe'. Read of address 00000029" (RVA 0xB9D0 is `@UStrAddRef`: `mov
    edx, [eax-8]` with a string pointer of 0x31) at `arms_treaty`'s M2 submit, right after c)'s client steps (TEST
    `armstreaty` host 0/1 with a refresh at war, the quiet end "arms treaty: ended on this PC for its nation's war"), in
    the second `--group relations` run; B5 round 4's run_g1 had boxes at the same submit (00C25D4B, read 00000000) and
    at `battle_generator`'s start (00C2751B). The client never joined another campaign there (not the 0.3.3 trigger
    below), and no 0.4.x or 0.5.0 run had it. A recurrence is this issue: keep both logs and report it.
  - The game's own spying can report the same thing twice in one month (the open question of 0.4.1 RC2, B6 round 1,
    the treaty fix and the int-060 merge): `SpySuccess` (RVA 0x46c744) compares gun qualities in a loop of up to 11
    draws of a calibre (`Random(16) + 2`, with replacement; "Our N in guns are superior to X guns of the same calibre."
    or "X N in guns have better performance than ours.", then a 40 % chance to go on), so one spy success can make the
    same report twice: in the host's turn report and list (`CheckTurnEvents` -> `CheckIntelligence`, which calls
    `CheckIntelligenceForNation` once per nation) or in a joined player's own check (one call of `CheckIntelligence` in
    its view: "spy event, 2 new reports", both the same). No routine of the mod runs twice. The month checks allow
    such a repeat (`GUN_REPORT_RE`, a NOTE); the next load drops it (0.2.2).
  - `relations_peace` b) right after `treaty` h) in the same month: a peace between players hands nation 1's ship over
    on the host only (the client's copy gets it with the next month), so the reparation of `peacetest 100 -3 1` can pick
    that ship on the client; the host logs "ship N (...) skipped: not among nation 1's ships" and keeps it where it is
    (`PEACE_SHIP_SKIPPED`, a NOTE; after the month both copies agree).
  - `--stress` only: `victory_points` a) after 12 months of `battles`' war with Spain the player's own peace (the month's
    relation turn, `relations v2: peace: DoPeace`) zeroed nation 1's VP after the last load (`vp_shown_ok`);
    `war_effects` had no ship of nation 1 under construction left (a `SKIP-note`; the setup takes each nation's newest
    ship since a `--group relations` run took an old one of J that was commissioned in M1); `lockstep_resume` i) counted
    the re-sent month's "already rolled" (`session_again` now waits for that roll).
  - `--stress` only, battle conditions (each passed when run before; re-run them): "Enemy unable to challenge our forces
    in the area! Gain 102405 VP!" (the message's VP is the form's +0x32fd + 5, RVA 0x3b70bc: the game's DoBattle found
    no ship of nation 1 for the test battle) at `lockstep_limits`' and `lockstep_stress` f)'s `pocbattle 1 0` after a
    chain of test battles (their results stay in the host's campaign in memory, Stage B above): `ls_start_test` now ends
    at once with the box's text instead of waiting 150 s. Not only in `--stress`: the default run of dist_rc061_2 had it
    at `lockstep_limits`' and `lockstep_data` b)'s `pocbattle 1 0` right after `lockstep`'s and `lockstep_ui`'s test
    battles (Germany with 11 ships in each, none sunk in their live minutes; the campaign at March 1903), and `--only
    lockstep_limits,lockstep_data` passed (83/83). `lockstep_missiles` with a battle whose sides never sighted each
    other ("No opposing ships are in sight of each other and scenario length is reached!" at minute 500; it began in
    the late afternoon): e) and f) find no target, d) runs into the battle's end; the recovery then waits 120 s for the
    client as nation 1 (`ls_client_back`; it plays nation 3 there): one more fail line.
- The 0.6.0 release candidate 2 dist_rc060_2 (2026-10-23; MODLOG, Testing 2026-10-23, the 0.6.0 release candidate 2
  dist_rc060_2): no SaveBuildCampaign box or other game error box in any run, and none in the old trigger either (the
  joined player's game on another campaign in the same process: `other_campaign.py` in the developers' session files
  `r62`: the client loads slot 9, plays a live battle in a session on it, moves to the host's slot 1 session without a
  restart and submits two months; `battleptrs` area=0 poss=0 and `pocheapcheck` bad=0 after the move; with
  `RTW3MP_TEST_KEEPBATTLEPTRS=1` the battle's area pointer survives both loads, its freed block re-allocated for the
  1900 campaign's own area of that name). Suite issues it found, fixed: the client save's repeat of its own monthly
  check of the month before (the host sends the player's reports back; e.g. "Sep 1901  Spain - The Spanish navy is
  reportedly doing a lot of night training." three times: not only the gun comparison repeats), B5's blockade points
  in a war between players' nations (`CreditWarVP` lines in `lockstep_pvp` f) and `treaty` k)), the area of the player's
  generator's request (`battles` b)), a war the session leaves in `player_copy`'s copy (its month's battle preview once
  stopped the whole `--stress` run at `lockstep`), the mixed driver's `surprise` pattern. Each passed when run again
  (battle and game conditions):
  - `battle_surprise` b) without the takeover: the game's CheckForWar runs its landing/takeover step in 50 % (with the
    trait 95 %), so `battles testsa J` can give the surprise attack alone (now a `SKIP-note`).
  - `air_units` M2: the 1935 campaign's arms-control conference of its first month bound the host's Great Britain ("We
    have more capital ship tonnage in service than permitted by treaty! You must scrap 141,500 tons of B, BB, BC or
    CV!", the game's PlayerChecksBeforeTurn): the host's Turn is refused until the player scraps, the month never
    advances (240 s timeout).
  - `lockstep_stress` f) ended by the game ("No opposing ships are in sight of each other and scenario length is
    reached!"): the battle's last hash line (minute 800, written after the end's fast-forward and the results) had the
    same `all=` and components on both copies but rng and `draws sim` 3 apart (72279/72276); no DESYNC, the result is
    A's. Every earlier run with that end had it equal. If it recurs, keep both logs.
  - `lockstep_stress` S8: the host's `ls drop` found the client inside a nested message loop (a Win32 box "Rule the
    Waves III", `#32770`, that no answerer closes; the mod logs "waiting for the outermost message loop" every 5 s), so
    the client never came back and the scenario stopped. `describe_windows` now shows a `#32770` box's text.
  - `lockstep_missiles` e): two held ships of side 0 lost their SSM in the approach under the computer (Fiume 4 -> 2,
    Espero 4 -> 0; dist_rc060_1 had Ugolino Vivaldi and Alfredo Oriani 4 -> 0), every minute equal; the missile code is
    unchanged since 0.5.0 (whose runs passed it) and the re-run passed (another force, 40 SSM). Watch it: if it
    recurs, find which division fired (`ls forces`, `ls mounts`) and whether the AI had split it off a held division.
- The 0.6.1 release candidate 2 dist_rc061_2 (2026-10-26; MODLOG, Testing 2026-10-26, the 0.6.1 release candidate 2
  dist_rc061_2): no game error box in any run. The one access violation in its logs is `battle_generator` b)'s own
  `battles gen 1 fault` (decision 3: "Error in CheckForBattles procedure! Access violation at address ... in module
  'RTW3MP.dll'. Write of address 00000000", the box recorded, none shown). The default run's `lockstep_limits` and
  `lockstep_data` b) met the game's "Enemy unable to challenge our forces in the area!" after two test battles (the
  battle condition under dist_rc060_1 above) and passed when run again.

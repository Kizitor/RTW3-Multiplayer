# RTW3 Multiplayer: nightly build

This is the `nightly` branch: the next versions' changes **before their full testing**. Each change passed its own
tests, but not yet the full release regression that every release on the main branch passes. Try it if you want the
new features early and don't mind finding problems. For a campaign you care about, stay on the latest release, or at
least keep a copy of your campaign's save slot before you play it with a nightly build.

- Build: **0.9.0-nightly.20261115** (2026-11-15), on top of [v0.8.1](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.8.1).
- Download: the pre-release of the same name on the
  [releases page](https://github.com/Kizitor/RTW3-Multiplayer/releases). The latest full release stays the
  recommended download.

## What's in it

- **Research messages for joined players** (coming in 0.8.2; reported on 0.8.1: no research pop-ups for the non-host
  player). With the new month, a joined player sees their own nation's research dialogs ("Research breakthrough:
  ...", the scientists' reports, gun and airbase news) and answers the air/sea rescue question on their own PC, never
  on the host's screen. Their nation now researches under the player's rules, as in single player (before, it got
  the computer's). Needs the nightly on the host, and on the player's PC for the dialogs.
- **Aircraft for joined players** (coming in 0.9.0; asked for on 0.8.1: the non-host player can't design or request
  planes). The *Aircraft types* and *Air groups* screens work for your own nation: requests, the prototype choice,
  licences and your squadrons. The month's aircraft decisions (prototypes, private ventures, old-type reminders) come
  on your PC, **Submit** asks the game's air questions, and your changes reach the host with your turn. The computer
  no longer develops your aircraft or gives your new carriers their air groups. Needs the nightly on the host and on
  your PC.
- **Unsubmit turn** (coming in 0.9.0; asked for by players): a button in the Multiplayer window takes a submitted
  turn back until the host starts the month; the month then waits for you again and never uses the withdrawn turn.
  Needs the nightly on the host and on your PC.

## Not in it yet

- The governments' offers to buy or sell technology for joined players (0.8.2; in testing).

## Testing status

- Each change passed its own test scenario and the relations scenarios on its own branch. The combined build's first
  test run passed, with failures only in the test scripts (since fixed).
- Still to come before the releases: the full default run, the stress run and mixed-version games for 0.8.2 and for
  0.9.0.
- Known limits: the month's aircraft decisions come at the start of your month, not during the Turn as in single
  player; aircraft lost in a joined player's own battles against the computer don't reach the host (as before).

## Compatibility

- Protocol 3 and live-battle engine 18, as in v0.6.1 to v0.8.1: players on the nightly and on a release play a
  campaign and fight their live battles together. Each new feature works only where every PC it needs runs the
  nightly; elsewhere things work as in v0.8.1.

## Install and going back

- Install: extract `RTW3MP-0.9.0-nightly.20261115-win32.zip` into the game folder next to `RTW3.exe`, replacing the
  older files, as for a release ([Install](README.md#install)).
- Going back: extract the v0.8.1 zip the same way. Keep a copy of your save slot from before the nightly if you may
  want to continue the campaign on v0.8.1.
- Problems: add your log (see [Troubleshooting](README.md#troubleshooting)) and say that you play the nightly build.

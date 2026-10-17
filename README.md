# RTW3 Multiplayer (unofficial mod for Rule the Waves 3)

Play one Rule the Waves 3 campaign with friends. Each human commands a different navy; every other nation stays
AI-controlled, as in single player. The host's game is authoritative: it runs all game logic and the turn timer.
Connect with Hamachi or any VPN/LAN (ZeroTier, Radmin VPN, a real LAN).

Built for Rule the Waves 3 **1.01.44** (Steam). What changed in each version: [CHANGELOG.md](CHANGELOG.md).

## Contents
- [How it plays](#how-it-plays)
- [Requirements](#requirements)
- [Install](#install)
- [Uninstall](#uninstall)
- [Network setup](#network-setup)
- [Hosting a session](#hosting-a-session)
- [Joining a session](#joining-a-session)
- [Diplomacy between players](#diplomacy-between-players)
- [Tactical battles](#tactical-battles)
- [Known limitations](#known-limitations)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)
- [Credits](#credits)
- [License](#license)

## How it plays
- One monthly turn at a time, with everyone planning at once:
  1. The host sends the current month to every player.
  2. Each player plans their own nation: design and build ships, deploy fleets, set budget, research, training and
     bases.
  3. Players press **Submit**, which replaces the Turn button for joined players.
  4. When everyone has submitted, or the host's time limit runs out, the host merges all orders.
  5. The host runs the real month, with the AI switched off for human-controlled nations, and sends the new month to
     everyone.
- The host plays their campaign's own nation (the one chosen at New Game). Joining players pick any of the 8 AI
  opponents in that campaign.
- Players can ally with each other, declare war and make peace with buttons in the Multiplayer window (see
  [Diplomacy between players](#diplomacy-between-players)). A war between players ends with peace terms for the
  winner, as in single player: land, ships and reparations.
- Every player fights their own nation's tactical battles on their own PC; battles between players are fought live by
  both players at the same time (see [Tactical battles](#tactical-battles)).

## Requirements
- Rule the Waves 3 version **1.01.44** from Steam, on Windows. Steam must be running when you play.
- **Every player needs the same game version and the same mod version.** The host refuses a player whose game version
  differs, or whose mod version can't play with the host's (see [CHANGELOG.md](CHANGELOG.md)).
- A network between the players: Hamachi, ZeroTier, Radmin VPN or a real LAN. The host's PC must accept incoming
  connections (see [Network setup](#network-setup)).

## Install
Every player:
1. Close the game.
2. Download `RTW3MP-<version>-win32.zip` (for example `RTW3MP-0.3.0-win32.zip`) from the
   [latest release](https://github.com/Kizitor/RTW3-Multiplayer/releases/latest) and extract all its files into the
   game folder, next to `RTW3.exe`. When you update, replace the older files.
   On Steam, the game folder is Library > right-click Rule the Waves 3 > Manage > Browse local files.
3. Start the game normally from Steam. The title screen gets a **Multiplayer** button above *Exit*. You can also
   press **Ctrl+Shift+M** anywhere in the game.

The zip holds `version.dll` (the loader), `RTW3MP.dll` (the mod), `RTW3MP_SecondCopy.bat` (starts a second copy of
the game to try multiplayer on one PC), `RTW3MP_README.txt` and `RTW3MP_LICENSES.txt`. The release also offers the
two DLLs on their own, for updating an existing install. Updating from an older version? See
[Upgrading from an older version](#upgrading-from-an-older-version).

## Uninstall
1. Close the game.
2. Delete `version.dll`, `RTW3MP.dll` and the other `RTW3MP_*` files from the game folder.
3. If you like, also delete the mod's settings and logs (the folder `Documents\My Games\Rule the Waves 3\RTW3MP`)
   and, on a joined player's PC, the save slots `Game77` and `Game87` in `Documents\My Games\Rule the Waves 3\Save`
   (the copies of the multiplayer campaign).

The mod never modifies `RTW3.exe` or the Steam DRM. A campaign you host stays a normal save in its own slot; the mod
keeps its session settings next to it, in `rtw3mp_session.ini`.

## Network setup
Hamachi example:
1. Every player installs LogMeIn Hamachi. The host creates a network; everyone else joins it.
2. The host opens the Multiplayer window. Under the buttons, "Your VPN address" shows the host's Hamachi IP
   (25.x.x.x). ZeroTier and Radmin VPN addresses are shown the same way.
3. Allow `RTW3.exe` through Windows Firewall on private networks; the host's PC must accept incoming connections.
   The game uses TCP port **47624** (the **Port** box), and UDP port 47625 (the port + 1) for **Find LAN games**.

## Hosting a session

**Video walkthrough (1 minute):** [docs/hosting.mp4](docs/hosting.mp4). It shows the host's screen from the title
screen to the first processed month; a second copy of the game plays the joined player "Captain". The video and the
pictures below were made with version 0.1: today's Multiplayer window also has the **Fight my battles** checkbox and
the Diplomacy list. The addresses in them are examples.

1. Open **Multiplayer**, enter your name, optionally a password and a turn time limit in minutes (0 = none), and
   press **Host game**. Players can join and pick nations right away. Give them the VPN address shown under the
   buttons (Hamachi addresses start with 25.).

   ![The host's Multiplayer window: a player has joined and picked Germany](docs/hosting-1-lobby.jpg)
2. Optionally pick the nation you plan to play and press **Claim nation**, so others see it. You play the nation you
   choose on the New Game screen.
3. Start a **New Game** (or load a save). When the campaign opens, everyone's picks are matched to it automatically.
   If a pick isn't in that campaign, the chat asks that player to pick another.
4. Press **Start session**. You can press it any time after hosting: if the campaign isn't ready yet (no campaign
   open, or a new campaign still in its Start/Continue setup steps), the window tells you what's left and the session
   starts by itself once the campaign is ready.

   ![Start session pressed before a campaign is open: the window explains the next steps](docs/hosting-2-start-session.jpg)

   ![The campaign is open and the session has started: the host plays Great Britain, Captain plays Germany](docs/hosting-3-session-started.jpg)
5. Each month, press **Turn** when you're ready (or **Advance turn now** in the Multiplayer window). If players are
   still planning, you can wait (the turn starts by itself when everyone submits) or advance now. Any nation that
   hasn't submitted keeps last month's orders. The game asks its usual end-of-turn questions on the host's screen.

   ![After the turn: the next month has been sent to every player](docs/hosting-4-next-month.jpg)
6. If the players have battles to fight after the month, your screen shows "Battles in progress" and waits for them
   (see [Tactical battles](#tactical-battles)).
7. To continue later, load the same save and host again. Nation assignments are remembered per campaign
   (`rtw3mp_session.ini` in the save slot), and returning players get their nation back automatically.

- **Time limit:** when it runs out, every player's current orders are submitted automatically and the month starts.
  You can change the limit during the session with **Set**.
- **Players who leave:** a player who joins again with the same name gets their nation back. Until then the nation
  keeps its last orders; the computer doesn't take it over. To hand a nation to the computer, select it under
  **Nation** and press **Give to AI**.

## Joining a session
1. Open **Multiplayer**, enter your name, the host's IP (or use **Find LAN games**) and the password. Leave **Fight my
   battles** ticked to fight your own battles (see [Tactical battles](#tactical-battles)); you can change it only
   before you join. Then press **Join game**.
2. Choose a nation in the list and press **Claim nation**. You can do this before the host has a campaign; your pick
   is matched when the host opens one. A refused pick is explained in the chat.
3. When the host starts, the month loads automatically and you play as your nation. Your copy lives in save slot
   `Game77` (your battles in `Game87`), so your own saves are never touched.
4. Plan your month and press **Submit**. You can keep changing orders and submit again until the host processes the
   turn.
5. To keep your own copy of the campaign, use the game's **Copy game** button. The copy is made for your nation: you
   play your nation in it, and the host's nation is run by the computer. It is a normal single-player save and doesn't
   affect the multiplayer session. (This also works after you leave the session, as long as slot 77 still holds the
   multiplayer game.)
   - You keep your nation, its fleet, designs, possessions, budget, research, relations and wars.
   - The host's officers come with the copy, unassigned; their divisions don't.
   - Tech-sharing deals start over.
6. If you leave or lose the connection, join again with the same name: you get your nation back.

To test on one PC, run two copies of the game and join `127.0.0.1` (`RTW3MP_SecondCopy.bat` from the release starts
the second copy). Both copies share one settings file, so the joining copy gets a numbered name (e.g. "Friend 2").

## Diplomacy between players
Human players decide war and alliances with each other themselves; they don't have to wait for the relation
(tension) bar. The **Diplomacy** list in the Multiplayer window shows every other player's nation with your current
relation, the tension value of the relation bar, and anything pending. Select a nation, then:

| Button | Effect |
|---|---|
| **Declare war** | One-sided. Asks for confirmation; you can withdraw it until the month is processed. Breaks an alliance between you. |
| **Propose alliance** / **Accept alliance** | Offers an alliance. When the other player offered one to you, the button reads *Accept alliance*. |
| **Propose peace** / **Accept peace** | Offers peace while you are at war; *Accept peace* when they offered it. The winner of an agreed peace gets **Claim land...** here (see [Peace terms](#peace-terms-between-players)). |
| **Leave alliance** | One-sided. Ends the alliance (or calls off an agreed alliance that hasn't started yet). |
| **Withdraw** / **Decline** | Takes back your own offer or declaration, or declines the other player's offer. |

- Everything takes effect when the host processes the month, the same way a war breaks out at month end in single
  player. Everyone sees the result in the chat and in the Diplomacy list of the new month; on the host's screen the
  relation bars and ally flags change too.
- Diplomacy is possible while players plan a month (not in the lobby or while the turn is processed).
- Agreements made with the buttons are kept: an alliance between players doesn't run out and the AI never cancels it.
  A war declared with the buttons lasts until both players agree to peace, even if the game's own peace routines
  would end it (for example a peace treaty the host signs with an AI nation, which in Rule the Waves ends all of the
  host's wars at once).
- The relation bar keeps working as in single player: tension still rises and falls, and the game can still start
  wars on its own (rising tension, or an ally being drawn into a war). Those wars can be ended with **Propose peace**
  too.
- AI nations are unchanged: they get no buttons, and their diplomacy (with each other and with the players) is decided
  by the game as in single player.
- When the host makes peace with its last enemy, the game's normal end-of-war routine runs on the host (budget cut,
  released prisoners, and the usual messages).

### Peace terms between players
A war between two players ends like a war against the AI in single player: the side that is clearly ahead wins
concessions.
- **War score:** the **Tension** column of a war shows the victory points (VP) both sides scored in this war (*VP
  yours:theirs*), counted from every battle between the two nations: the host's battles, live battles between
  players and the battles the game resolves on its own. The label above the list tells you what a peace would bring
  now.
- **Who wins:** the game's own rules for "winning the war" are used. A side with at least twice the enemy's points and
  a clear margin wins a *major victory*; a side with a smaller lead but enough points wins a *minor victory*;
  otherwise the peace is a compromise without terms. The margin grows with the era, as in single player.
- **The terms are fixed when the peace is accepted** and shown in the chat and the Pending column. They take effect
  when the month is processed:

  | | Minor victory | Major victory |
  |---|---|---|
  | Possessions the winner may take (total value) | up to 4 | up to 8 |
  | Ships handed over as war reparations | up to 1 | up to 3 |
  | Winner's prestige | +1 | +2 |
  | Loser's base resources | −100 | −500 |

  The loser also loses prestige, as after a lost war in single player, and never more than half of its base
  resources. The rules of single player apply: only possessions worth less than 11 can be taken, and only in sea
  areas next to the winner's own possessions; ships handed over are recent, large ships that are not under repair
  (if the loser has none, fewer ships change hands).
- **Claim land:** after the peace is accepted, the winner's **Propose peace** button reads **Claim land...**. It
  opens a list of the loser's possessions that may be taken, with their value and oil. Tick the ones you want, up to
  the limit, and press **Claim selected**. You can change the choice until the month is processed. If you don't
  choose (or press **Let the conference choose**), the most valuable possessions that fit the limit are picked for you
  when the month ends.
- When the month is processed, both players get the game's peace treaty window with the terms, and everyone sees them
  in the chat. Withdrawing an agreed peace before then also drops its terms.

## Tactical battles
Every player fights their own nation's battles on their own PC, with the game's own battle screen. **Fight my
battles** (next to the port, on by default; set it before you join) turns this on for you. Without it, the game
resolves your battles as before, and the host fights a battle between the host's nation and yours against the
computer, as in single player.

### Battles against AI nations
- When the game finds a battle between a joined player's nation and an AI nation during the month, the battle is kept
  for that player. After the month, before the new month is sent, the host sends each player their battles, one at a
  time. The host's screen shows "Battles in progress" and is locked until they are done.
- On your PC the battle opens with the game's battle preview, as in single player. Fight it to its end, close the
  results, and the result goes back to the host: damage, sinkings, crew experience, victory points and prestige. Then
  your own game loads again, and the new month follows.
- If you decline the battle in the preview, leave it before its end, disconnect, or don't finish within the time
  limit (30 minutes per battle), the game resolves the battle the old way.

### Battles between players (live)
When the host's nation fights a joined player's nation, or two joined players' nations fight each other, both players
fight the same battle **at the same time**, each on their own PC and each commanding their own ships. The joined
players in the battle need **Fight my battles** on (the host's own box makes no difference; it only applies when
joining a session). If it is off, the host fights its battle against that nation's ships under computer control, and a
battle between two joined players' nations is resolved by the game.
- **How it works:** both PCs run the same battle, minute by minute, and exchange their orders. If the two ever
  disagree, the battle is reloaded on both from the leading PC's state (a short pause; orders given in the last minute
  are dropped).
- **The clock is shared.** *Run one minute*, *Run 5 minutes*, *Run until break*, *Pause* and the game speed work for
  both players: the battle advances when both are ready, and stops for both when one pauses or the game stops for a
  sighting. Orders given while a minute is running take effect at the next minute.
- **Fog of war:** each player sees their own ships and only what they have sighted of the enemy.
- **Ending:** both players press *End game* (the game's own rule still applies: at least half the battle and 400
  minutes, and no enemy close). The game's own end conditions (both fleets far apart, one side gone, the time limit)
  end it for both. The result counts like any battle and the month goes on.
- **If a player leaves:** if a player disconnects or doesn't respond for 30 seconds, the computer takes over their
  ships for the rest of the battle.
- **Not available in live battles yet:** air strikes and routine air operations, missiles, manual torpedo salvoes,
  emergency activation, change force, try for home, auto-run and save. Pressing them shows a line in the chat.
- Both PCs hold the whole battle, so a determined player could read the enemy's positions from memory: play with
  people you trust. The battle log on the second player's PC is written from the first side's point of view.

## Known limitations
- **Battles:** raider interceptions and submarine attacks are still resolved by the game. The battles of a player who
  doesn't fight their battles are resolved by the game too, or fought by the host against the computer when the
  enemy is the host's nation. For what live battles can't do yet, see
  [Battles between players](#battles-between-players-live).
- **Victory points:** a joined player's *VP* counts what their nation scored in its wars, including battles against
  the host; *Enemy VP* counts the points of the nations at war with them. (The game itself only keeps the host's view,
  so the mod fills in a joined player's.) Points the host's enemies score outside battles (blockades, sinkings,
  control of sea areas) are shared evenly among the host's enemies when the host fights several nations at once. VP
  go back to 0 when a nation's last war ends.
- **Decided by the game for joined nations:** diplomacy with AI nations and new aircraft types. Diplomacy between
  players is decided with the buttons.
- **Design studies of joined players** count down every month on the host, including the game's occasional one-month
  "technical issues" delay, and the player is told in the Multiplayer chat when a design is ready for construction.
  The game's random committee and Air Force events about design studies only happen for the host's nation.
- **Doctrine changes of joined players** (new training priorities, missile storage policy) take effect after the same
  number of months as in single player, and the player is told in the Multiplayer chat.
- **Random events of joined players** ("Event - <month>" dialogs) appear on the joined player's own screen at the
  start of a month, about one month in three, and the player answers them as in single player. The answer changes
  their own nation (budget, prestige, unrest, ...) and their relations with the other nations. Events about the host's
  own war (peace offers, army offensives), arms-control treaties, colonial rebellions and sabotage of another navy's
  ships only happen for the host's nation. While their nation is at war, the game only has war events, so joined
  players at war get no random events. Officer, intelligence and other monthly messages also stay with the host.
- **Not transferred from joined players:** officers, divisions, and changes to air units (1920s+). Ships, designs,
  budget, research, training, doctrine, deployments, forts and base improvements are transferred.
- **Player-centric screens:** the relations bars and intel screens show the campaign from the host nation's point of
  view. The Diplomacy list in the Multiplayer window shows your own relations with the other players.
- **Resigning** is not possible during a session: leave the session first.
- **Versions and slots:** the host refuses players with another game version, and players whose mod version can't play
  with the host's (0.1, 0.2 and 0.3 can't play together). Joined players use save slot 77, and slot 87 for their
  battles.
- **Memory over long sessions:** the game's memory use grows by about 3 MB per processed month (the game doesn't fully
  free a campaign when it's reloaded, and multiplayer reloads it every month and for every battle a joined player
  fights). The game is limited to 2 GB, so after a few hundred months in one sitting, save, close the game and rejoin.

## Troubleshooting
- **The log** is `Documents\My Games\Rule the Waves 3\RTW3MP\rtw3mp.log`; `rtw3mp.prev.log` is the log of the start
  before. Add it when you report a problem.
- **No Multiplayer button:** check that `version.dll` and `RTW3MP.dll` are both next to `RTW3.exe`. Some antivirus
  programs quarantine `version.dll` (an unsigned DLL with a Windows DLL's name); restore it or add an exception for
  the game folder.
- **You can't connect:** check that you're in the same Hamachi network and that the firewall allows `RTW3.exe`. Then
  test with the host's 25.x.x.x IP.
- **"The host refused the connection: ..."**
  - "different game version than the host": update both games through Steam.
  - "protocol mismatch (host mod X)": install mod version X, the host's (or ask the host to update).
  - "wrong password": ask the host for the password.
- **The host sees "<player>'s turn could not be applied":** another program (usually antivirus or OneDrive) held a
  save file for more than 3 seconds, so that nation keeps last month's orders. If it happens repeatedly, exclude
  `Documents\My Games\Rule the Waves 3\Save` from scanning/syncing.
- **The host's screen stays locked with "Battles in progress":** a player is fighting a battle. Each battle has a
  30-minute limit; after it (or if the player disconnects) the game resolves the battle and the month goes on.
- **A live battle pauses and reloads:** the two PCs disagreed and were brought back in step. If it happens often, the
  computer takes over the second player's ships. The log lines start with `lockstep:`.
- **"Out of memory" after a long session:** see *Memory over long sessions* in
  [Known limitations](#known-limitations).

### Upgrading from an older version
- **0.3.0 needs everyone to update:** older versions can't play with it (protocol 3).
- **From 0.2.8 or older:** a joined player's research priorities sometimes fell back to Medium after a month (the
  game's AI research choices reset them); 0.3.0 keeps them.
- **From 0.2.7 or older:** a joined player's **Research** screen showed the host's research priorities and budget,
  and changes there didn't stick. After updating, open the Research screen once and set your priorities and budget
  again.
- **From 0.2.6 or older:** a joined player's **Copy game** made a copy that plays the host's nation. Copies made with
  those versions stay that way; make a new copy.
- **From 0.2.2 or older:** a joined player's doctrine or training change stayed pending forever. Pending changes
  continue from where they are after the update.
- **From 0.2.1 or older:** an "out of memory" error, or "Disconnected from host: protocol error" / "message too large"
  at the end of a month. Those versions duplicated the campaign's intelligence reports every month until the save no
  longer fit in memory. Update everyone; loading the campaign with 0.2.2 or newer repairs it automatically.
- **From 0.2.0:** joined players' design studies never progressed. Stuck designs start counting down from the next
  month the host processes.

## Building from source
- Requires Visual Studio Build Tools with the C++ x86 tools (built with Visual Studio 2022). Run `build.bat`; the two
  DLLs go to `dist\`. Install them by copying `dist\version.dll` and `dist\RTW3MP.dll` into the game folder while the
  game is closed.
- `tools\package_release.ps1 -OutDir <folder>` builds the release zip from `dist\` and `extras\`.
- Tests: `python tools\regression.py` starts a host and a client copy of the game, runs the test scenarios (session,
  turn merge, events, research, doctrine, design studies, diplomacy, battles, victory points, peace treaties, Copy
  game, live battles) without touching your mouse or keyboard, and restores the save slots it uses. How to run it
  safely, and what it checks: [docs/TESTING.md](docs/TESTING.md). The developer test bridge is only enabled when
  `RTW3MP_BRIDGE_PORT` is set, and it listens on 127.0.0.1 only.
- How the mod works: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md); the game internals it relies on:
  [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md); the engineering log: [MODLOG.md](MODLOG.md).

## Credits
- Rule the Waves 3 is by NWS / Matrix Games. This is an unofficial fan mod and ships no game files.
- Hooking uses [MinHook](https://github.com/TsudaKageyu/minhook) (see [License](#license)).
- Built with AI assistance (GitHub Copilot): reverse engineering through the game's Delphi RTTI, the code, the tests
  and the documentation.

## License
- The mod's code is available under the MIT license: see [LICENSE](LICENSE).
- MinHook, in `third_party/minhook/`, is under the BSD 2-Clause license: see
  [third_party/minhook/LICENSE.txt](third_party/minhook/LICENSE.txt).
- The release zip has both licenses in `RTW3MP_LICENSES.txt`.
- Rule the Waves 3 and its artwork belong to NWS / Matrix Games. The screenshots and the video in `docs/` show the
  game; the MIT license covers only the mod's own code and text.

# Changelog

What changed in each version of RTW3 Multiplayer, newest first. Every version needs Rule the Waves 3 **1.01.44**
(Steam). Downloads and the full release notes are on the
[releases page](https://github.com/Kizitor/RTW3-Multiplayer/releases); the engineering details of each change are
in [MODLOG.md](MODLOG.md).

The *protocol* is the version of the network messages: a host refuses players whose mod uses another protocol.

## 0.3.0 (2026-10-16)
Tactical battles for every player, and live battles between players. **Everyone (host and joined players) must
update:** protocol 3, so older versions can't play with 0.3.0.
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.3.0)

### New
- **Fight your own battles against AI nations.** When your nation meets an AI nation at sea during the month, you
  fight that battle yourself, on your PC, with the game's own battle preview and battle screen. After the month the
  host sends each player their battles and waits ("Battles in progress"). The result (damage, sinkings, crew
  experience, victory points, prestige) goes back into the campaign.
  - If you decline the battle, leave it before its end, disconnect or don't finish within 30 minutes, the game
    resolves the battle the old way.
- **Live battles between players.** When the host's nation fights a joined player's nation, or two joined players'
  nations meet, both players fight the same battle at the same time, each on their own PC with their own ships.
  - Shared clock: *Run one minute*, *Run 5 minutes*, *Run until break*, *Pause* and the game speed work for both
    players; the battle stops for both at sightings. Orders given during a running minute take effect at the next
    minute.
  - Fog of war: each player sees their own side and what they have sighted.
  - Ending: both players press *End game* (the game's own 400-minute rule applies), or the game's own end
    conditions end the battle for both.
  - If a player disconnects or doesn't respond for 30 seconds, the computer takes over their ships.
  - Both PCs compare the battle every minute. If they ever disagree, the battle is reloaded on both (a short pause).
- **Fight my battles** checkbox in the Multiplayer window (on by default; set it before joining). Without it, the
  game resolves your battles as before.

### Not available in live battles yet
Air strikes and routine air operations, missiles, manual torpedo salvoes, emergency activation, change force, try for
home, auto-run and save (a chat line says so). Both PCs hold the whole battle, so play with people you trust.

### Fixed
- A player who rejoins while the host is in a battle gets their nation back.
- The game's AI research choices no longer reset a joined player's research priorities (now and then all
  priorities fell back to Medium after a month).

## 0.2.8 (2026-10-14)
Research screen for joined players. **Everyone should update.**
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.2.8)

### Fixed
- **Research screen for joined players.** The game's Research button always opened the screen for the first nation
  of the campaign, which in multiplayer is the host's nation. A joined player saw the host's research priorities and
  research budget, and changes made there never reached their own nation. The screen now opens for the player's own
  nation; priorities and budget % take effect when the host processes the month. Nothing changes for the host.

### Upgrade note
- Joined players: open the Research screen once after updating and set your priorities and budget again.

## 0.2.7 (2026-10-14)
Copy game for joined players. **Everyone should update.**
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.2.7)

### Fixed
- **Copy game for joined players.** The game always plays the first nation of a save, which in a multiplayer
  campaign is the host's nation, so a joined player's copy played the host's nation. Now:
  - you play your own nation in the copy, with its fleet, designs, possessions, budget, research, relations, wars
    and victory points;
  - the host's nation is run by the computer;
  - the copy is a normal single-player save and doesn't affect the multiplayer session. It also works after leaving
    the session, as long as slot 77 still holds the multiplayer game;
  - the host's officers come along, unassigned; divisions of the host's ships are dropped; tech-sharing deals start
    over.

## 0.2.6 (2026-10-12)
Victory points of joined players. **Everyone should update.**
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.2.6)

### Fixed
- **VP against the host.** In a war against the host, everything a joined player's nation achieved went only into
  the host's "Enemy VP": the player's own *VP* never changed, and their *Enemy VP* showed their own points. Now:
  - the points a joined player's nation scores in battles against the host count in that nation's VP;
  - points the host's enemies score outside battles (blockades, sinkings, control of sea areas) go to the enemy
    nation; when the host fights several nations at once, they are shared evenly among them;
  - a joined player's *Enemy VP* shows the points of the nations at war with them;
  - VP go back to 0 when a nation's last war ends. A peace between AI nations elsewhere no longer resets a joined
    player's VP.
- AI nations' victory points are unchanged.

## 0.2.5 (2026-10-12)
Peace terms for wars between players. **Everyone should update.**
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.2.5)

### New
- **War score:** the Diplomacy list shows the victory points both sides scored in the war (*VP yours:theirs*). The
  label above it says what a peace would bring now.
- **Terms are fixed when the peace is accepted**, with the game's own "winning the war" thresholds:
  - minor victory: the loser cedes land worth up to 4 and hands over 1 ship; the winner gains 1 prestige; the
    loser's base resources drop by 100;
  - major victory: land worth up to 8, 3 ships, +2 prestige, base resources −500;
  - otherwise a compromise peace without terms.
- **Claim land...:** the winner picks the loser's possessions in a list, as in single player (worth less than 11, in
  sea areas next to the winner's own possessions). If the winner doesn't choose before the month ends, the most
  valuable possessions that fit are picked automatically.
- When the month is processed, both players see the game's *Peace treaty* window, and everyone sees the terms in the
  chat.

### Fixed
- When the host's last war ended (including the game's own peace events), every joined player's prestige was reset
  to 0. It is now kept.

## 0.2.4 (2026-10-06)
Random events for joined players. **Everyone should update.**
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.2.4)

### New
- **Random events for joined players.** Until 0.2.3 only the host's nation got the game's random events ("Event -
  <month>" dialogs). Now:
  - at the start of a month (about one month in three, like the game's own roll) the event appears on the joined
    player's own screen, and the player answers it as in single player;
  - the answer changes the player's own nation (budget, prestige, unrest, funds, ...) and its relations with the other
    nations, including the host's nation;
  - at most one event per player per month, with the game's usual cooldowns.
- Events that only the host's game can resolve stay with the host's nation: peace offers and army offensives during
  the host's wars, arms-control treaties, colonial rebellions and sabotage of another navy's ship. While a joined
  player's nation is at war, the game only has war events, so that player gets no random events until peace.
  Officer, intelligence and other monthly messages stay with the host.

## 0.2.3 (2026-10-05)
**The host must update; joined players should update too.**
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.2.3)

### Fixed
- **Doctrine and training of joined players.** New training priorities and a new missile storage policy wait some
  months before they take effect, but the game counted that time down only for the host's nation, so a joined
  player's change stayed pending forever. The host now counts it down for every player's nation, and the player gets
  a message in the Multiplayer window when the change takes effect. Changes already pending continue from where they
  are.
- **Lost turns.** A player's whole turn could be lost without notice when antivirus or OneDrive briefly held a save
  file while the host merged the turns. The host now retries for a few seconds and applies a merge completely or not
  at all. If a turn still can't be applied, the host is told, and it's in the log.

## 0.2.2 (2026-10-05)
**Everyone should update.**
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.2.2)

### Fixed
- **"Out of memory", then "Disconnected from host: protocol error".** Each reload added the campaign's intelligence
  reports again, and multiplayer reloads the campaign every month, so the report list doubled every month until the
  save no longer fit in memory or in a network message. The list is now cleared before every load, and affected
  campaigns are repaired automatically the next time they are loaded.
- Clearer messages when a connection is dropped ("message too large", "protocol error (bad frame length ...)", "out
  of memory while receiving"). Running out of memory while receiving no longer crashes the game, and the host
  refuses to send an oversized month instead of breaking the connection.

### Known limitation
- The game's memory use still grows by about 3 MB per processed month in one sitting (the game doesn't fully free a
  campaign when it's reloaded). After a few hundred months, save, close the game and rejoin.

## 0.2.1 (2026-10-05)
**The host must update.** Joined players can keep 0.2.0 (it plays with a 0.2.1 host), but updating is recommended.
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.2.1)

### Fixed
- **Design studies of joined players never progressed**, so a ship designed by a joined player could never be built.
  The host now counts them down every month for every player's nation, including the game's occasional one-month
  "technical issues" delay. When a design is ready, the player gets a message in the Multiplayer window. The game's
  committee and Air Force events about design studies still only happen for the host's nation. Stuck designs start
  counting down from the next month the host processes.

### Changed
- The Multiplayer window of a 0.2.1 player flashes for design-study messages.

## 0.2.0 (2026-10-05)
The first public release. Protocol 2.
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.2.0)

### New
- **Multiplayer** button on the title screen (or Ctrl+Shift+M). Players connect over Hamachi, ZeroTier, Radmin VPN
  or a LAN.
- The host's game runs the campaign and the turn timer. Each player commands one nation; every other nation stays
  AI-controlled, as in single player.
- Monthly turns are planned at the same time: players press **Submit**, and the host merges everyone's orders and
  runs the month.
- **Diplomacy between players:** declare war, propose or accept alliances and peace with buttons, without waiting
  for the relation bar. Diplomacy with AI nations is unchanged.
- Tactical battles are fought on the host's PC.

## 0.1.0 (2026-10-04, development version, never released)
The development version before the first release; its notes are in [MODLOG.md](MODLOG.md). Protocol 1.
- A `version.dll` loader starts the mod once the Steam DRM has unpacked the game; the mod finds the game's code by
  name through the game's own type information.
- **Multiplayer** button on the title screen and the Multiplayer window: host, join, LAN discovery, nation picks,
  chat and a turn time limit.
- Monthly state sync, a per-nation merge of the players' orders, and the AI switched off for human nations on the
  host.
- Later the same day: nation picks before the host has a campaign, **Start session** and **Give to AI** usable at any
  time with guidance, a fix for the window's close button, and the one-minute hosting video.

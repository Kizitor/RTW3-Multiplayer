# Changelog

What changed in each version of RTW3 Multiplayer, newest first. Every version needs Rule the Waves 3 **1.01.44**
(Steam). Downloads and the full release notes are on the
[releases page](https://github.com/Kizitor/RTW3-Multiplayer/releases); the engineering details of each change are
in [MODLOG.md](MODLOG.md).

The *protocol* is the version of the network messages: a host refuses players whose mod uses another protocol. Live
battles also have their own version, the live-battle engine: a live battle is fought only when both of its players
have the same one, and a host on 0.4.0 or newer also requires its own.

## 0.5.0 (2026-10-23)
Each joined player's nation now follows the player's rules of single player instead of the computer's, after the
reports of two players who played on two PCs ("the tension bars are supposed to be independent for each player", "the
wars were shared amongst players"): its own tension changes, war warnings, declarations and peace offers with the
computer's nations, answered on the joined player's screen at the start of their month; **Propose peace** to the
computer's nations at war with it, with the game's peace terms; its own months at war; and the player's money rules
instead of the computer's bonuses and decisions. **Update the host and the joined players:** a joined nation's own
relations need 0.5.0 on the host and on the player's PC (otherwise the nation keeps the computer's rules, as in
0.4.1), and its money rules need 0.5.0 on the host. The protocol is still 3, and 0.5.0 keeps the live-battle engine of
0.4.1 (17): players on 0.4.1 and 0.5.0 fight their live battles together, but not with players on 0.4.0 or older. Not
built yet: a joined nation's battles against computer nations still come from the computer's battle rules, and some
effects of its wars are still missing (see Known issues).
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.5.0)

### New for joined players
- **Your nation's relations follow the player's rules.** At the start of your month, after the roll for your random
  event, your game runs the player's relation routines of single player for your nation, and you answer their
  dialogs:
  - random tension changes with their explanations, your leader's speeches, and other nations' questions and alliance
    offers (only in a month without a random event, as in single player);
  - the effect of previous wars on tension;
  - war plans with your spies' warnings and rumours, declarations of war ("Russia has declared war on us!"), and
    nations that join a war against you or on your side.

  The game's turn report then lists the month's messages. The host applies what changed at once, before it runs the
  month; its screen shows none of your dialogs, and everyone's chat gets a line for a new war ("* War: Russia and
  Germany are now at war."). If the host's game stops during the month, the host keeps what it has applied: it saves
  its campaign right after applying your relation turn or peace (while one of its game's dialogs is open, as soon as
  it closes).
  The host's month no longer runs the computer's relation rules on your nation's relations with the computer's
  nations: no wars, peace, alliances or unexplained tension steps in your nation's name, and a war of the host's
  nation no longer keeps your wars from starting or ending. The players' diplomacy, the events and the computer
  nations that join the host's wars work as before.
- **Your own months at war.** The host keeps a war counter for each joined nation and sends it with the month. The
  line under your nation's name reads "At war for N months." with the *War results* box, and at peace its hint "At
  peace for N months."; your events, your relation routines and your peace decision use it, and your battles and
  **Copy game** get it too. In 0.4.1 the line read "At war with Spain.", and your game used the host's counter.
- **Peace with the computer's nations.** A war against computer nations ends as in single player, with a peace with
  all of them at once:
  - at the start of a month at war, the game can bring you its peace offer (an event), whose answers negotiate;
  - the Diplomacy list of the Multiplayer window has a row for each computer nation at war with you, with your war
    score against it (*VP yours:theirs*) and only **Propose peace**: after a confirmation, the game's own peace talks
    start at once, with their dialogs on your screen. Once a month, after the month's relation dialogs and before you
    submit, and not in a month in which you answered a peace offer. (Single player has no such button.)

  The terms are the game's: possessions gained or ceded ("Select possessions as part of peace agreement" when you
  win), ships you hand over when you lose, base resources, prestige, and your months at war and the blockades ended.
  The host applies them at once, so the possessions and ships change hands for everyone, and the chat shows the treaty
  ("* Peace treaty between Germany and Spain: Germany wins." and its lines) or "* Germany's peace negotiations with
  Spain came to nothing.". Your war score against a computer nation counts the battles since the war began, as in a
  war between players, and goes into the game's peace decision. A peace with the computer's nations doesn't end your
  wars with other players; while one goes on, your nation stays at war (its months at war, budget, unrest and
  blockade).
- **Your nation's money follows the player's rules.** The host's month gives your nation the balance of its budget
  at the player's prices and the player's money rules of single player, with your nation's own months at war: war
  loans during a war, penalties for debts (lost prestige and morale), the money lost to corruption, cuts for hoarding
  money (budget cuts, money taken, and army offensives that take a tenth of your budget for some months), the
  rules for a small fleet at peace (complaints and budget changes), and the player's budget cut under a naval treaty
  (two steps). Their messages appear in your game's turn report at the start of the new month, and your budget screen
  shows an army offensive's cut. The computer's own money rules are gone (see Fixed).

### Fixed
- **Joined nations got the computer's money** (a known issue of 0.4.1): the monthly sum that the game gives the
  nations it plays (it grows with the fleet), "... has increased the naval budget to remain a global naval power.",
  the computer's rules for base resources (a minimum, and a decline at a low budget), its cheaper maintenance of the
  naval academy, aircraft and light carriers, its budget rules (keeping up with the host's budget, the January cuts,
  raises when computer nations go to war, in an arms race, after a coup and when the host's nation goes to war), and
  the money behind the host's spy report "secretly spending an additional 10%". Existing campaigns keep the money they
  have: nothing is taken back.
- **The host's month managed a joined nation's ships under construction** as it does for the computer's nations: it
  resumed the ships the player had halted and, while the balance was negative, halted the most expensive ones. Your
  ships now stay halted or building as you set them.
- **The computer made other decisions for joined nations** in the host's month: their doctrine on new research, their
  ammunition settings each January, ships sent home from colonies, escorts and ASW patrols ordered and paid for with
  their money when their merchant ships were sunk (and their ASW research priority), the air groups on their carriers
  and bases, technology deals offered to the host in their name, the purchase of another nation's scrapped ship with
  their money, the scrapping of their armed merchant cruisers after a peace, and the cuts to China's base resources
  and budget that the concessions rule makes for a computer-played China, when a player plays China. These are now
  the player's alone.

### Compatibility
- Protocol 3: players on 0.3.x, 0.4.0, 0.4.1 and 0.5.0 can join each other's hosts and play the campaign together.
- 0.5.0 has the same live-battle engine as 0.4.1 (17), so players on 0.4.1 and 0.5.0 fight their live battles live,
  with each other and with a host on either version. A player on 0.4.0 (engine 14) or older can't fight a live battle
  with a host or player on 0.5.0: the battle is fought against the computer or resolved by the game, and the chat and
  the notices say why, for example "live battles need the same live-battle engine on both PCs: the host has 0.5.0
  (engine 17), you have 0.4.0 (engine 14)".
- A joined nation's own relations, its war counter and its peace with the computer's nations need 0.5.0 on the host
  and on the player's PC:
  - A host on 0.5.0 and a player on 0.4.x: the player's nation keeps the computer's rules for its relations, as in
    0.4.1, but its money follows the player's rules and the computer no longer decides for it; the player doesn't get
    the messages of those rules, and their budget screen doesn't show an army offensive's cut. The player's screen
    shows no counter of its own ("At war with Spain." on 0.4.1).
  - A host on 0.4.x and a player on 0.5.0: as in 0.4.1, the computer's rules for the nation's relations and money,
    the line "At war with Spain." under the nation's name, no relation dialogs at the start of the month, and no
    computer nations in the Diplomacy list.
- Existing campaigns go on from where they are. A joined nation's months at war or at peace start from a guess: the
  host's count when the host's nation is also at war (or also at peace), else 1 month at war or 12 months at peace.
  The war score of a war with a computer nation that is already going on starts at 0:0, and the effect of previous
  wars counts only the wars that end under a host on 0.5.0. Ships that the computer halted for a joined nation stay
  halted until its player resumes them.

### Known issues
- **Not built yet for joined nations** (planned for a later version):
  - the effects of a war that the game runs only for the player's nation: victory points for blockades, the raider and
    submarine war on merchant shipping, the help of allies ("The operations of our ally ... adds N VP"), and surprise
    attacks and landings, which are switched off for joined nations until then. In a war with the host's nation, the
    game's rules for a revolution in an enemy nation and for the war help of allies still treat a joined nation as a
    computer nation. A joined nation's army offensive gives no victory points;
  - the player's battle generator: a joined nation's battles against computer nations still come from the computer's
    battle rules (when their ships meet in an area), not from the player's (invasions, raider interceptions, battles
    in home waters and at possessions). A battle that the computer's navy declines has been avoided since 0.4.1.
- A joined nation has no total war of its own (the game keeps that state for the host's nation): its peace offers
  follow the rules of a war that isn't total.
- The player's money rules for joined nations leave out two of the game's questions: corruption's (the money is lost,
  and "improve accounting" isn't available) and the Air Force's event of 1950 to 1975.
- Still the computer's for joined nations: the development of aircraft types and the first air groups of newly
  launched carriers, budget cuts that come with high unrest, the world economy's changes of base resources, coups
  (the government changes without the player's dialog), and a few smaller budget rules, which follow the computer's
  version or the host's months at war.
- If the host's game stops after applying a joined player's relation turn or peace but before saving it (for example
  while one of its dialogs is open), that change is lost for the month, never applied twice. Ships that a joined
  player scraps in a peace come back if the host restarts in the same month.
- The relation dialogs at the start of a month count only while the host still plans that month: if the host
  processes the month first (its time limit, or **Advance turn now**), their changes are lost for that month.
- Windows Defender flagged one test build of this version's `RTW3MP.dll` as `Trojan:Win32/Phonzy.A!ml` (a guess of
  its machine learning) and removed it; the next build of the same code was not flagged. If an antivirus removes a
  file of the mod, restore it and add an exception for the game folder.
- The known issues of 0.4.1 remain, except the two about joined nations' relations with computer nations and their
  money.

## 0.4.1 (2026-10-22)
Fixes from the reports of two players who played 0.3.2 on two PCs: live battles between two PCs, each player's own
relations, and each player's own intelligence and trade protection warnings. **Both players of a live battle need
0.4.1** (live-battle engine 17): until both have it, their battles are fought against the computer or resolved by the
game, and the chat says why. **Update the host and the joined players:** a joined player on 0.4.1 sees their own
relations under any host, but their own intelligence and a battle that the computer's navy declines need 0.4.1 on
the host and on the player's PC, and the month's trade protection and foreign station warnings for joined players
come from a host on 0.4.1. The protocol is still 3.
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.4.1)

### Added
- **The same maths on every CPU.** Intel and AMD processors can round the game's sine, cosine and arctangent slightly
  differently, and the battle uses them for bearings, formations, hit chances and the sun. In a live battle both PCs
  now compute them the same way, so two different processors load and play the same battle. The results match the
  processor's own to the last digit of a double for every argument, including the processor's own rounding of angles
  near right angles and its handling of special values (checked on millions of arguments). In tests this cost no
  measurable speed.
- **The game data files are compared** when a live battle starts. If a file of the game's `Data` folder differs
  between the two PCs (a mod, a damaged or different install), the battle isn't tried: it is fought against the
  computer, and both players' chat and notice name the files (for example "your game data files differ from
  Admiral's: Data\Gundata.dat"). Ship drawings (`Data\Graphics`) don't count. When a player joins, the host's and the
  player's logs also say whether their game data files are the same and name the other PC's processor maker.
- **When the loaded battles differ,** the chat line and the notice on both PCs now say in a few words what differed,
  short enough to read out: for example "the loaded battles differ: orders; CPUs AMD (Admiral), Intel (Captain)"
  (positions, damage, ammunition, sightings, identification, orders, aircraft, torpedoes, forces, submarines, the
  clock, the random numbers; the two processors' makers and floating-point settings when they differ). Both PCs also
  write their reports (`ls_desync_*.txt`, next to the log), as they do when a running battle gets out of step: send
  both with the logs when you report it.

### Fixed
- **Live battles with battleships, battlecruisers or carriers could go out of step** (a "DESYNC" and a short
  resynchronisation). Whether a sighted ship can be mistaken for a battlecruiser or a carrier depends on which ship
  types exist in the world, which the game works out only when it processes a month. The joined player's game never
  does that, so it identified some sighted ships differently from the host's (a carrier as a heavy cruiser), and the
  two battles drifted apart. Both PCs now use the host's.
- **The second player's map showed the first player's view.** Their own ships stayed hidden under their division
  flags until the first player's ships had seen them, the first player's ships were shown with their names, and both
  maps showed every division's flag, so each player could see where the other's fleet was. Each player's map now
  shows their own ships and flags, and the other side's ships only while they are sighted, never their flags.
- **The second player's screen showed the first player's side while the battle loaded:** the map on the first
  player's fleet and flags, and their nation's ensign at the top left. The battle screen now appears when the load is
  done, with your own nation's ensign.
- **Battle preferences.** The second player's PC took the first player's display options too (sound volume, colours,
  range circles, dark mode) and missed *AI control*. Now exactly the first player's battle preferences that the
  battle's rules use count on both PCs: *Realism*, *AI control*, *Floatplane launch*, line of sight, friendly torpedo
  hits, autosave, and some message and sound options that change the battle's random numbers. Every other option
  stays each player's own. The second player's PC holds the first player's in memory only and has its own back when
  the battle ends.
- **The battle screen's *Preferences* during a live battle** changed a battle preference on that PC only, which could
  make the two PCs disagree. The battle preferences now stay as they are until the battle ends, and the chat says so;
  display options change at once.
- **Floating-point settings.** The second player's PC loaded the battle with its own processor settings for
  floating-point numbers (the x87 control word, which drivers and other programs can change) and took the first
  player's only after the load. Both PCs now use the first player's from the load on and put them back whenever
  something changes them during the battle (the log says so).
- **A joined player's tension bars showed the host's relations.** The tension panel, its war and alliance marks, the
  flags' names and the *War results* box now show your own nation's relations: your tension with every other nation
  (the host's nation takes the place of your own), your wars and your alliances. Before, you saw the host's: a war of
  the host looked like yours, and your own wars didn't show. The map's strength bars are coloured by your tensions,
  and a flag opens the data of the nation shown there. The line under your nation's name reads, for example,
  "At war with Spain." (the game keeps the months at war for the host's nation only).
- **An enemy that declined a battle still fought it.** When the computer's navy declines a battle against you ("The
  ... navy declines battle! You gain N VP!"), there is now no battle and you keep the points, as in single player.
  Before, the host's game then resolved the battle as one between computer nations, often with losses on both sides,
  so a stronger player's navy never got to fight. This needs 0.4.1 on the host and on the player's PC.
- **A joined player's intelligence was the host's.** The intelligence levels under the flags, the budget's
  *Intelligence* line and the intelligence reports showed the host's; a change with + or - was lost, or changed the
  host's spending on your own nation; your nation paid nothing for intelligence and got the computer's intelligence
  money instead; and the monthly spying ran for your nation as for a computer nation. Now the spending, its effort,
  the reports and the cost are your own, and your monthly spying check runs on your PC with the game's own messages,
  for example about a caught enemy spy.
- **Joined players got no trade protection or foreign station warnings.** *Submit* now asks the game's questions of
  the *Turn* button when too few ships are on trade protection (at war) or on foreign stations; *No* goes back to
  your planning. During the month the host runs the game's checks for every joined player's nation, with the game's
  chances and consequences (for example lost prestige), and you get its messages as notices with the new month. The
  budget panel's trade protection line shows your own nation's requirement.

### Compatibility
- Protocol 3: players on 0.3.x, 0.4.0 and 0.4.1 can join each other's hosts and play the campaign together.
- Live battles need the same version on both players' PCs (live-battle engine 17), and a host on 0.4.0 or 0.4.1
  requires its own: its own battle against a player on the other version is fought against the computer, and a
  battle between two joined players is fought live only when both have the host's version, otherwise the game
  resolves it. The chat and the players' notices name both versions. A host on 0.3.x checks no versions, as with
  0.4.0 (see 0.4.0's *Compatibility*).
- Your own relations need 0.4.1 only on your PC. A battle that the computer's navy declines needs 0.4.1 on the host
  and on the player's PC; otherwise the game resolves it as before.
- Intelligence and warnings:
  - A host on 0.4.1 and a player on 0.4.0: the player's month holds their own intelligence (empty at first), but
    there is no spying check and no question at *Submit*, and their spending changes are lost; their nation pays
    nothing for intelligence and no longer gets the computer's intelligence money. The month's trade protection and
    foreign station checks apply to their nation, and their messages arrive as chat lines.
  - A host on 0.4.0 and a player on 0.4.1: the player sees the host's intelligence as in 0.4.0, and *Submit* asks the
    trade protection and foreign station questions, but the month brings no warnings or consequences for their
    nation.

### Known issues
- **"The loaded battles differ" on every attempt**, as the two players reported it, is not explained: tests ruled out
  different CPUs, number formats, preferences, window sizes and network delays, and a difference in the game data
  files now stops the battle before it loads. If it happens to you, the chat line and the notice say what differed:
  send both PCs' `ls_desync_*.txt` reports and logs, or copy or read out that line.
- The battle's message log tab lists the messages of both sides on both PCs, from the first player's point of view
  (as before).
- A few message and sound options are battle preferences in a live battle (the first player's count on both PCs),
  because the game draws the battle's random numbers for them.
- A joined nation's relations with computer nations still follow the game's rules for computer nations: small steps
  without explanations on the player's screen, and wars that break out and end by the computer's rules, only while
  the host's nation is at peace. A larger rework, with the player's own rules, peace terms and battles for joined
  players, is being built for a later release.
- The computer's money bonuses still reach joined nations: the monthly money that the game gives computer nations,
  and "... has increased the naval budget to remain a global naval power."
- For joined players, the intelligence reports that the game makes outside the spying check (for example about
  another nation's inventions) reach only the host, what their nation knows about other navies' ships (classes,
  aircraft) is still the host's, and the game moves ships on foreign stations to the areas that lack them for the
  host's nation only.
- Seen once in tests, at the start of a live battle between two joined players: the game's own box "Error in
  AdjustForcesForBattle procedure! Access violation" on one player's PC. The game went on and the battle ran normally;
  the cause is not known yet.
- The known issues of 0.4.0 remain.

## 0.4.0 (2026-10-18)
Live battles between players without most of their limits: air strikes and routine air operations, missiles, torpedo
salvoes, force orders, auto-run, *Fastest available* and *Save* work for both players, and a player who drops out can
rejoin the battle, also after the host's game restarts. Victory points no longer grow with every load during a
blockade, and 0.4.0 contains the fixes of 0.3.3. **Everyone should update, the host too:** the protocol is still 3,
so 0.3.x and 0.4.0 play a campaign together, but a live battle needs 0.4.0 on both players' PCs, and only a host on
0.4.0 gives a dropped player their ships back.
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.4.0)

### New in live battles
- **Air strikes.** Each player launches their own carriers' strikes with the game's strike dialog (*Order an
  airstrike*, or *Launch strike* in the reports list) while the battle is paused; *Launch*, *Ready* and *Stand down*
  work as in single player. The computer flies the airfields' strikes of both sides, as in single player. The host's
  new option **Computer flies carrier strikes in live battles** (off by default) has the computer fly both sides'
  carrier strikes as well; the strike dialog still launches the players' own strikes besides them.
- **Routine air operations and land-based CAP.** *Manage CAP and air recon*, *Search pattern* and the division
  report's land-based CAP button work for each player's own forces. CAP and scouts fly from each player's own
  settings, and a land-based CAP request is covered only by the player's own airfields.
- **Missiles.** Both sides' missile ships fire on their own by the computer's rules (SAMs and decoys defend as in
  single player). *Fire at will* on the missile panel is each player's own setting, on at the start. *Manual fire*
  orders salvoes while the battle is paused, and both PCs fire them when the next minute starts; missile submarines
  are left out. *Fire mode* stays disabled for both players: the computer picks the salvo size.
- **Manual torpedo salvoes** with the division report's *Fire torpedoes*, while the battle is paused: the torpedoes
  enter the water on both PCs when the next minute starts. As in single player, the button needs the battle
  preference *Realism* on Captain's mode; the first player's preferences count for both PCs.
- **Force orders:** *Change force*, *Change lead* and *Battle turn away* take effect for both players when the next
  minute starts.
- **Fastest available** is a shared clock control, like the speed box.
- **Auto-run per player.** It hands only your own ships to the computer (optionally sending all forces to the closest
  friendly base), and the computer then fires their torpedoes and missiles too (salvoes ordered before still fire).
  Pressed again, it gives command back as in single player: each force's lead division, and the other divisions once
  you clear *AI controlled* in their division reports. With both players on auto-run, the battle runs to its end at
  the fastest speed.
- **Save** in a battle against the host (by either player) saves a checkpoint into the host's campaign slot; the
  battle goes on.
- **Rejoining.** When the second player disconnects, their game stops or their PC stops responding, the computer
  commands their ships until that player joins the session again during the battle; then they get them back. This
  works in battles against the host and, with a host on 0.4.0, in battles between two joined players (see Known
  issues).
- **Resuming after the host's game restarts.** Loading the host's campaign slot opens its live battle at the last save
  and holds it for the player: once the host hosts again and the player joins, the battle goes on live, and then the
  rest of the month. *Run* goes on against the computer instead.

### Fixed
- **Victory points grew with every load during a blockade** (seen since 0.3.2; older versions too). The game credits
  a blockade between the player's nation and a nation at war with it not only in the month but also every time it
  loads the campaign, and multiplayer loads campaigns again and again: the host's every month and at every live
  battle, a joined player's for every battle it fights. So the host's score (or its enemies'), a joined player's score
  from its battles and the war score between players could grow with points nobody earned, and a joined player saw
  the host's nation with more points than the host had. In a multiplayer session a load now gives exactly the points
  of the save; a load outside a session (single player, the host's first load before hosting) is as in the game
  itself. Points already added to a save stay.
- **Reloads of live battles at dawn and dusk.** Drawing the map's sighting circles changed a ship's light conditions
  on one PC only, so when the light changed the two PCs could disagree and the battle was brought back in step. The
  map no longer changes them.
- **What each player reads about the other side.** The second player read the first player's messages as the reason
  of a pause (for example the first player's damage reports and sightings), and the first player's chat showed the
  second player's messages ("Battle: ..."). Each player now reads only the messages about their own side; a pause for
  a message about the other side says "an event".
- **Live-battle chat lines reached every player** (since 0.3.0). In the host's live battle, the host's game sent its
  battle lines to all connected players, so the opponent and the other players could read them. They now stay on
  each player's PC.
- **The host's divisions after a reload.** A division of a computer-controlled force that the host had taken back went
  back to the computer every time the battle was reloaded. It now stays under the host's command.
- **"Log lines out of sync!"** The game showed this box after a live battle was reloaded, and later pauses repeated
  it. It no longer appears.
- **The second player's result box** at the end of a live battle showed the first player's result. It now shows the
  second player's own victory or defeat.
- **A month lost after joining again during the host's battle** (since 0.3.0). A joined player who joined the session
  again while the host was in a battle dated in the next month planned that next month, and their orders were
  rejected ("submission is for another month"). The player now always gets the campaign's month.
- **The date of live battles.** After every load (the start, a resync, a rejoin), a live battle ran with the battle's
  own day and month, which can fall into the next month and decide, for example, the weather. It now runs on the
  campaign's date, like a battle that is never reloaded.
- **The second player's forces and divisions after a resync** were left under computer control (since 0.3.0). They
  are the player's again after every resync.
- **Carriers.** The computer flew the second player's carrier strikes but never the first player's, and every 2 hours
  after minute 600 it reset the second player's air search and CAP. Each player now launches their own strikes and
  keeps their own air settings.
- **Missiles.** The second player's missile ships fired by the computer's rules, the first player's only with *Fire at
  will* ticked on both PCs (locked off in 0.3.2), and the second player's missile panel counted the first player's
  ships. Both sides now fire alike, and the second player's panel no longer shows the first player's counts.

### Not available in live battles
Adjusting a strike's target during its flight; salvoes of missile submarines from *Manual fire* (they still fire on
their own) and the missile panel's *Fire mode*. Search floatplanes don't launch with the battle preference
*Floatplane launch* on *Ask before launch*. Emergency activation, try for home and enter port aren't player commands
in the game, in single player either.

### Compatibility
- Protocol 3: players on 0.3.x can join a host with 0.4.0, and the other way round, and play the campaign together
  (turns, diplomacy, battles against AI nations).
- A live battle needs 0.4.0 on both players' PCs (a new live-battle engine), and a host on 0.4.0 checks this for
  every live battle: its own battle against a player on 0.3.x is fought against the computer, and a battle between
  two joined players is resolved by the game unless both have 0.4.0. The host's chat and the players' notices name
  both versions, and the month goes on.
- A host on 0.3.x checks no versions: when it fights its own battle against a player on 0.4.0, the player's game
  refuses the battle with a notice, its chat naming both live-battle levels, and the host's computer fights the
  player's side. A battle between two joined players on 0.4.0 is passed on by such a host and fought live (see Known
  issues).

### Known issues
- Under a host on 0.3.x, when a player drops out of a live battle between two joined players, the other player's chat
  says the dropped player gets their ships back by joining the session again, but the computer keeps them to the end
  of the battle: only a host on 0.4.0 can start such a battle again. Update the host.
- A player who joins a session on a different campaign from the one their game had loaded before, without restarting
  the game, can get "Error in SaveBuildCampaign procedure! Access violation ..." when their game first saves the new
  campaign. Seen in tests, also with 0.3.3. Restart the game before you join a session on a different campaign.

## 0.3.3 (2026-10-17)
A hotfix of 0.3.2. **Hosts should update first:** their game added another copy of every squadron each time it
loaded the campaign again (every month and every live battle), until the host's turn stopped with "aircraft capacity
exceeded!". 0.3.3 plays with 0.3.0, 0.3.1 and 0.3.2 (same protocol, nothing new on the network), but the live-battle
fixes need the players of a live battle on 0.3.3, so everyone should update.
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.3.3)

### Fixed
- **Squadrons doubled with every reload** (every version since 0.1.0, campaigns with aircraft). The game expects to
  load a campaign once after it starts, but multiplayer loads it again in the same game: the host after merging the
  players' turns every month, a joined player for every new month, and both players at every live battle start and
  every time a live battle is brought back in step. Each of these loads added another copy of every squadron. The
  host's *Turn* then stopped with "CV ... aircraft capacity exceeded!", the air costs ran away and the save kept
  growing. Squadrons are now kept once, and a campaign that already holds copies is repaired the first time 0.3.3
  loads it. A host on 0.3.3 keeps the campaign right even when joined players still run 0.3.2, whose game multiplies
  the squadrons only in its own copy of the campaign (that copy never goes back to the host). Joined players should
  update too: their game sets up their own battles from that copy.
- **Opening a division report during a live battle could reload the battle** (since 0.3.0). The report lowered its
  ships' current speed to their maximum speed in the current weather, on that player's PC only, so the two PCs
  disagreed and the battle was brought back in step. The report no longer changes the battle; the ships slow down in
  the next minute on both PCs. This works on the PC that runs 0.3.3: a player on 0.3.2 still causes the reload with
  their own reports.
- **A rare reload of a live battle after "Unknown ship sighted!"** (since 0.3.0). When a live battle was loaded, each
  PC kept the ship identifications its own screen had made while drawing the battle, and the battle then used them,
  so now and then the two PCs disagreed and the battle was brought back in step. Both PCs now start every load of a
  live battle with the same identifications; what each player sees on screen is unchanged. This needs both players of
  the battle on 0.3.3. It removes one cause of these reloads, not every one: a live battle can still be brought back
  in step now and then, automatically, in a few seconds.

## 0.3.2 (2026-10-17)
Fixes for live battles: battles with aircraft now start, the second player's ship list and air formations window show
their own side, the speed controls, buttons that stayed pressed, the missile panel and battle windows left open. Each
fix works on the PC that runs 0.3.2, so 0.3.1 and 0.3.2 play together (same protocol), but both players of a live
battle should update to get all the fixes.
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.3.2)

### Fixed
- **Live battles with aircraft never started.** From about 1915, when ships, seaplanes or airfields bring aircraft, a
  live battle couldn't start: each time the battle was loaded, the aircraft of the previous load stayed, so the two
  PCs never had the same battle, and after a few tries the computer took over the second player's ships. The battle's
  data is now cleared before each load, as the game does when a battle screen closes (this also frees memory).
- **The second player's ship list.** After every minute the second player's list of forces and ships showed the first
  player's forces. It now shows your own.
- **A month could be skipped after a live battle.** Loading a live battle moves the campaign's day and month to the
  battle's own date. When the battle was brought back in step (a resync), the host's saved campaign kept that date, so
  a battle dated in the next month skipped a month for everyone (the joined player's orders were rejected as "for
  another month"). The host's saves now keep the campaign's own date, and the battle's results and the rest of the
  month count in the campaign's month again (only the host needs this fix).
- **The host's folders in the chat.** When the game saved the battle during a minute, the line "The battle paused
  (Game saved! ...)" showed both players the full path of the host's save. Chat lines now show only the file name.
- **The arrows next to the game speed box.** They changed the speed at once on one PC only, even in the middle of a
  minute, which could make the two PCs disagree and reload the battle. They now work for both players like the speed
  box: one step, from the next minute, and at most *Fast* while ships are in contact (the game's own rule).
- **Fastest available.** The button is still not available in live battles, but it stayed pressed, and the game reads
  it between minutes, so the two PCs could run a minute at different speeds. It now pops back up, and the chat says
  "Fastest available is not available in a live battle yet; use the speed box."
- **Other unavailable buttons that stayed pressed.** *Auto-run* and the division report's *land-based CAP* button
  stayed pressed although nothing happened; the division report's *Ok* then requested land-based CAP on one PC only,
  which could make the two PCs disagree. They now pop back up.
- **The missile panel's *Fire at will* and *Fire mode*.** The game reads them on each PC for the first player's
  ships, so different settings on the two PCs made them disagree in battles with missiles. They are now locked in live
  battles until both PCs share them.
- **Show air formations.** After a minute had run, the second player's window listed the first player's air
  formations. It now lists your own.
- **Battle windows left open during a reload.** Before a live battle is loaded again (at its start, when the two PCs
  are brought back in step, or when the second player leaves it), the air formations window, a division report, the
  force orders and the battle's other windows are now closed. They still pointed to ships and air formations that the
  reload removes, which could crash the game.

### Not available in live battles yet
Air strikes and routine air operations; missiles (manual fire and the missile panel's settings); manual torpedo
salvoes; change force, change lead and battle turn-away; the *Fastest available* button; auto-run and save. This
corrects the list of 0.3.0: emergency activation and try for home don't exist in campaign battles (the game hides
them, also in single player), and change lead, battle turn-away and *Fastest available* were missing.

## 0.3.1 (2026-10-16)
War score of live battles between joined players. Only the host's game changes: joined players on 0.3.0 can keep
playing with a 0.3.1 host (same protocol), but updating everyone keeps things simple.
[Release notes](https://github.com/Kizitor/RTW3-Multiplayer/releases/tag/v0.3.1)

### Fixed
- **War score of live battles between joined players.** In 0.3.0, a live battle between two joined players' nations
  was saved in the campaign but didn't count towards the war score that decides the peace terms between them. It now
  counts like every other battle between them.

### Changed
- The mod's code is available under the MIT license (see [LICENSE](LICENSE)).

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
  ..." dialogs). Now:
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

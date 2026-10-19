RTW3 Multiplayer {VERSION} - unofficial multiplayer mod for Rule the Waves 3
https://github.com/Kizitor/RTW3-Multiplayer

Requirements
- Rule the Waves 3 version 1.01.44 (Steam), Windows.
- Every player needs the same game version. All players (host and joined players) should run {VERSION}.
- Hamachi, ZeroTier, Radmin VPN or a real LAN between the players.

Install (every player)
1. Close the game.
2. Extract all files of this zip into the game folder, next to RTW3.exe (replace older files).
   (Steam: Library > right-click Rule the Waves 3 > Manage > Browse local files.)
3. Start the game from Steam. The title screen has a "Multiplayer" button; Ctrl+Shift+M also opens it.

Hosting, joining, diplomacy and tactical battles are explained in the README on the GitHub page.
What changed in each version: CHANGELOG.md on the GitHub page.

Uninstall
Delete version.dll, RTW3MP.dll and the RTW3MP_* files from the game folder. The mod never modifies
RTW3.exe or the Steam DRM. Its settings and logs are in Documents\My Games\Rule the Waves 3\RTW3MP, and a
joined player's copies of the campaign are in the save slots Game77 and Game87: delete them too if you
don't need them any more. The host's campaign is a normal save in its own slot; the mod keeps its
session settings next to it (rtw3mp_session.ini, and rtw3mp_livebattle.ini to resume a live battle).

Files
- version.dll                loader: forwards to the Windows version.dll and loads RTW3MP.dll
- RTW3MP.dll                 the mod
- RTW3MP_SecondCopy.bat      optional: starts a second copy of the game to try multiplayer on one PC
- RTW3MP_README.txt          this file
- RTW3MP_LICENSES.txt        licenses: the mod's code (MIT) and MinHook (BSD 2-Clause)

Log file: Documents\My Games\Rule the Waves 3\RTW3MP\rtw3mp.log

Rule the Waves 3 is by NWS / Matrix Games. This is an unofficial fan mod; it contains no game files.
Built with AI assistance (GitHub Copilot).

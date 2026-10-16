# RTW3 Multiplayer: project instructions

RTW3 Multiplayer is an unofficial native mod that lets several players share one Rule the Waves 3 campaign over a LAN
or VPN. A `version.dll` proxy loads `RTW3MP.dll`, which finds the game's code by name (Delphi RTTI) and hooks it at
runtime; the game itself (1.01.44, Steam, 32-bit Delphi/VCL) is never modified.

## Where things are
- For players: [README.md](../README.md), [CHANGELOG.md](../CHANGELOG.md).
- For developers: [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md) (how the mod works),
  [docs/GAME-INTERNALS.md](../docs/GAME-INTERNALS.md) (game facts), [docs/TESTING.md](../docs/TESTING.md) (tests,
  bridge commands, known traps), [MODLOG.md](../MODLOG.md) (the engineering log).
- Code in `src/proxy/` and `src/mod/`; `build.bat` builds `dist\`; test and release tools in `tools/`; release texts
  in `extras/`.

## Agents
Each agent reads its own file first.
- **RTW3 Research** ([rtw3-research.agent.md](agents/rtw3-research.agent.md)): reverse engineering, design, proofs of
  concept.
- **RTW3 Regression** ([rtw3-regression.agent.md](agents/rtw3-regression.agent.md)): builds the mod, runs the live
  regression suite, adds tests.
- **RTW3 Docs** ([rtw3-docs.agent.md](agents/rtw3-docs.agent.md)): documentation, git, releases and housekeeping.

## Rules for every session
- **The repository is public: no personal data.** No absolute user paths, no user or account names other than the
  project identity, no IP addresses other than 127.0.0.1 and clearly fictitious examples, no e-mail addresses other
  than the project identity and the Copilot trailer. Scan the changed files before every commit.
- **The game runs only in backed-up test runs,** under the safety rules of the regression agent file: no real mouse or
  keyboard, no screen capture, close only the processes you started, never touch a game the user is playing, never
  save outside a backed-up run. Never modify `RTW3.exe`, the Steam DRM stub or the game's data files.
- **Commits** use the project identity (`git -c user.name=RTW3MP -c user.email=rtw3mp@localhost commit ...`) and end
  with the trailer `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`.
- **Nothing is pushed or published without the user's approval:** no `git push`, no tags pushed, no GitHub release
  created or edited, no remote branch deleted. Never rewrite published history; keep the local branch
  `local-history`.
- **Docs state facts:** every statement must be true for the current code; check it in the source first.

@echo off
rem Starts an extra copy of Rule the Waves 3 (for testing multiplayer on one PC: host in one copy,
rem join 127.0.0.1 from the other). Steam must be running. Place this file in the game folder.
set SteamAppId=2008100
set SteamGameId=2008100
if not exist "%~dp0RTW3.exe" (
  echo Copy this file into the Rule the Waves 3 game folder first.
  pause
  exit /b 1
)
start "" /D "%~dp0" "%~dp0RTW3.exe"

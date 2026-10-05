@echo off
rem Builds dist\version.dll (loader proxy) and dist\RTW3MP.dll (the mod) for 32-bit RTW3.exe.
setlocal
set ROOT=%~dp0
set VCVARS=
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if "%VSPATH%"=="" (
  echo Visual Studio C++ build tools not found.
  exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
if not exist "%ROOT%build" mkdir "%ROOT%build"
if not exist "%ROOT%dist" mkdir "%ROOT%dist"
cd /d "%ROOT%build"

set CFLAGS=/nologo /O2 /MT /W3 /Zi /Zc:threadSafeInit- /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS

cl %CFLAGS% /LD "%ROOT%src\proxy\version_proxy.cpp" /Fe"%ROOT%dist\version.dll" /link /DEF:"%ROOT%src\proxy\version.def" /DEBUG /OPT:REF /PDB:"%ROOT%build\version.pdb" user32.lib kernel32.lib || exit /b 1

cl %CFLAGS% /EHsc /std:c++17 /LD ^
  "%ROOT%src\mod\main.cpp" "%ROOT%src\mod\util.cpp" "%ROOT%src\mod\delphi.cpp" "%ROOT%src\mod\game.cpp" ^
  "%ROOT%src\mod\saveio.cpp" "%ROOT%src\mod\net.cpp" "%ROOT%src\mod\session.cpp" "%ROOT%src\mod\ui.cpp" ^
  "%ROOT%src\mod\bridge.cpp" ^
  "%ROOT%third_party\minhook\src\buffer.c" "%ROOT%third_party\minhook\src\hook.c" ^
  "%ROOT%third_party\minhook\src\trampoline.c" "%ROOT%third_party\minhook\src\hde\hde32.c" ^
  /Fe"%ROOT%dist\RTW3MP.dll" /link /DEBUG /OPT:REF /PDB:"%ROOT%build\RTW3MP.pdb" ^
  ws2_32.lib iphlpapi.lib user32.lib gdi32.lib comctl32.lib shell32.lib advapi32.lib || exit /b 1

del /q "%ROOT%dist\*.exp" "%ROOT%dist\*.lib" 2>nul
echo Build OK: %ROOT%dist

@echo off
rem Builds and runs the offline test of the battle-result merge (tools\merge_test.cpp with src\mod\saveio.cpp; no game
rem needed): build\merge_test.exe [<snapshot.bcs> <month-end.bcs> [K J]]. Exit code 0 when every check passes.
setlocal
set ROOT=%~dp0..\
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if "%VSPATH%"=="" (
  echo Visual Studio C++ build tools not found.
  exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
if not exist "%ROOT%build\merge_test" mkdir "%ROOT%build\merge_test"
pushd "%ROOT%build\merge_test"
cl /nologo /O2 /MT /W3 /EHsc /std:c++17 /DWIN32 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
  /D_WINSOCK_DEPRECATED_NO_WARNINGS "%ROOT%tools\merge_test.cpp" "%ROOT%src\mod\saveio.cpp" "%ROOT%src\mod\util.cpp" ^
  "%ROOT%third_party\minhook\src\buffer.c" "%ROOT%third_party\minhook\src\hook.c" ^
  "%ROOT%third_party\minhook\src\trampoline.c" "%ROOT%third_party\minhook\src\hde\hde32.c" ^
  /Fe"%ROOT%build\merge_test.exe" /link ws2_32.lib user32.lib shell32.lib advapi32.lib >build.log || (type build.log & popd & exit /b 1)
popd
"%ROOT%build\merge_test.exe" %*

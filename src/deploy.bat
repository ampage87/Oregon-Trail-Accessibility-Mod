@echo off
rem =====================================================================
rem  Oregon Trail Accessibility Mod - build and install
rem  Run through deploy.vbs (which shows progress), or directly from a
rem  command prompt.  Lines starting with STEP, OK, WARN and ERROR are
rem  read by deploy.ps1.
rem =====================================================================
setlocal EnableExtensions EnableDelayedExpansion
set "SRC=%~dp0"
for %%I in ("%SRC%..") do set "ROOT=%%~fI"
set "GAME=%ROOT%\Game Files"
set "BUILD=%ROOT%\build"
set "LOGS=%ROOT%\Logs"
if not exist "%LOGS%" mkdir "%LOGS%"

echo STEP 1/6: Checking the game folder
if not exist "%GAME%\OREGON.EXE" (
  echo ERROR: OREGON.EXE was not found in "%GAME%".
  exit /b 1
)
if not exist "%GAME%\OREGON.GXL" (
  echo ERROR: OREGON.GXL was not found in "%GAME%".
  exit /b 1
)
echo OK: Game files found.

echo STEP 2/6: Finding the Visual Studio C++ compiler
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%P in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if exist "%%P\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%P\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if not defined VCVARS (
  for %%V in (2022 2019) do for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
    if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if not defined VCVARS (
  echo ERROR: Visual Studio with the "Desktop development with C++" workload was not found.
  exit /b 1
)
echo OK: !VCVARS!
call "!VCVARS!" >nul
if errorlevel 1 (
  echo ERROR: vcvars64.bat failed.
  exit /b 1
)

echo STEP 3/6: Building OTAccess.exe
if not exist "%BUILD%" mkdir "%BUILD%"
pushd "%BUILD%"
cl /nologo /O2 /EHsc /std:c++17 /W3 /MT /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
   "%SRC%companion\guest.cpp" "%SRC%companion\resources.cpp" "%SRC%companion\gamestate.cpp" ^
   "%SRC%companion\config.cpp" "%SRC%companion\mod.cpp" "%SRC%companion\patcher.cpp" ^
   "%SRC%companion\platform_win.cpp" ^
   /Fe:OTAccess.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib ole32.lib oleaut32.lib shell32.lib winmm.lib
set "CLERR=%errorlevel%"
popd
if not "%CLERR%"=="0" (
  echo ERROR: The compiler reported errors ^(see the messages above^).
  exit /b 1
)
copy /y "%BUILD%\OTAccess.exe" "%ROOT%\OTAccess.exe" >nul
if errorlevel 1 (
  echo ERROR: Could not copy OTAccess.exe. Is the game still running? Close it and run deploy again.
  exit /b 1
)
echo OK: OTAccess.exe built.

echo STEP 4/6: Installing the DOS helper
rem OTMOD.COM is prebuilt; it is rebuilt only when NASM is installed.
where nasm >nul 2>nul
if not errorlevel 1 (
  nasm -f bin -o "%BUILD%\OTMOD.COM" "%SRC%dos\otmod.asm"
  if not errorlevel 1 copy /y "%BUILD%\OTMOD.COM" "%ROOT%\bin\OTMOD.COM" >nul
)
rem A fresh clone of the repository has only the text copy (OTMOD.COM.b64).
if not exist "%ROOT%\bin\OTMOD.COM" if exist "%ROOT%\bin\OTMOD.COM.b64" (
  certutil -f -decode "%ROOT%\bin\OTMOD.COM.b64" "%ROOT%\bin\OTMOD.COM" >nul
)
copy /y "%ROOT%\bin\OTMOD.COM" "%GAME%\OTMOD.COM" >nul
if errorlevel 1 (
  echo ERROR: Could not copy OTMOD.COM into the game folder.
  exit /b 1
)
echo OK: OTMOD.COM copied to Game Files.

echo STEP 5/6: Settings
if not exist "%ROOT%\OTAccess.ini" (
  copy /y "%SRC%OTAccess.default.ini" "%ROOT%\OTAccess.ini" >nul
  echo OK: Created OTAccess.ini with default settings.
) else (
  echo OK: Kept your existing OTAccess.ini.
)

echo STEP 6/6: Preparing the game and checking DOSBox Staging
"%ROOT%\OTAccess.exe" --prepare
set "PREP=%errorlevel%"
if exist "%LOGS%\prepare.log" type "%LOGS%\prepare.log"
if "%PREP%"=="4" (
  echo WARN: DOSBox Staging was not found. Install it from a terminal with: winget install DOSBoxStaging.DOSBoxStaging
  exit /b 0
)
if not "%PREP%"=="0" (
  echo ERROR: Preparing the game failed ^(code %PREP%^).
  exit /b 1
)
echo OK: Ready. Start the game with OTAccess.exe in the mod folder.
exit /b 0

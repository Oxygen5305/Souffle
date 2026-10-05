@echo off
REM ============================================================================
REM SOUFFLE build entry point.
REM   build.bat [target] [action]
REM
REM   target : uno | ipopt | all              (default: all)
REM   action : configure | build | clean      (default: build)
REM
REM   Each target has its own build tree and publishes its own binary, so the
REM   two can coexist and be A/B compared:
REM
REM     uno     -> build_cheese\  -> bin\EMTGv9.exe          (Uno only)
REM     ipopt   -> build_ipopt\   -> bin\EMTGv9_ipopt.exe    (Uno + Ipopt)
REM
REM   bin\EMTGv9.exe is the Uno half of union search; bin\EMTGv9_ipopt.exe is
REM   the Ipopt half.  Run them together through united\united.py rather than
REM   expecting one process to do both: their mingw runtimes collide in the DLL
REM   namespace, so a single process cannot hold both solvers.
REM
REM Configuration (any of these may be preset in the environment):
REM   SOUFFLE_SRC       source tree                     default: this script's folder
REM   SOUFFLE_VCVARS    vcvars64.bat                    default: newest VS 2022 found
REM   SOUFFLE_WINSDK    Windows SDK root                default: standard install path
REM   SOUFFLE_WINSDKVER SDK version under that root      default: newest present
REM   SOUFFLE_UNO_ROOT  Uno install (include/, bin/)    required for the uno target
REM   IPOPT_INCLUDE_DIR Ipopt include/                  required for the ipopt target
REM   IPOPT_LIBRARY     Ipopt lib/ipopt.lib             required for the ipopt target
REM   IPOPT_BIN_DIR     Ipopt bin/                      required for the ipopt target
REM
REM Two toolchain facts worth knowing:
REM   * vcvars64 looks for the Windows SDK in the registry. If the SDK is a
REM     portable copy instead, INCLUDE/LIB must be set here or the build dies with
REM     "cannot open stdio.h" / LNK1104 kernel32.lib. This script always sets them.
REM   * Rebuild clean after touching a header: nmake has been seen to miss a
REM     changed header and link stale objects.
REM ============================================================================
setlocal EnableDelayedExpansion

REM ---- paths (all overridable) ----
if "%SOUFFLE_SRC%"=="" for %%I in ("%~dp0.") do set "SOUFFLE_SRC=%%~fI"
set "SRC=%SOUFFLE_SRC%"

if "%SOUFFLE_WINSDK%"=="" set "SOUFFLE_WINSDK=C:\Program Files (x86)\Windows Kits\10"
if not defined SOUFFLE_WINSDKVER (
  for /f "delims=" %%V in ('dir /b /ad /o-n "%SOUFFLE_WINSDK%\Include" 2^>nul') do (
    if not defined SOUFFLE_WINSDKVER set "SOUFFLE_WINSDKVER=%%V"
  )
)

if "%SOUFFLE_VCVARS%"=="" (
  for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
    set "SOUFFLE_VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if "%SOUFFLE_VCVARS%"=="" set "SOUFFLE_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
for %%I in ("%SOUFFLE_VCVARS%") do set "VSCMAKE=%%~dpI..\..\..\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "VCVARS=%SOUFFLE_VCVARS%"
set "UNO_ROOT=%SOUFFLE_UNO_ROOT%"
set "IPOPT_INC=%IPOPT_INCLUDE_DIR%"
set "IPOPT_LIB=%IPOPT_LIBRARY%"
set "IPOPT_BIN=%IPOPT_BIN_DIR%"

set TARGET=%~1
if "%TARGET%"=="" set TARGET=all
set ACTION=%~2
if "%ACTION%"=="" set ACTION=build

REM ---- toolchain ----
if not exist "%VCVARS%" (
  echo [err] vcvars64.bat not found. Set SOUFFLE_VCVARS to your copy, e.g.
  echo       set SOUFFLE_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
  exit /b 1
)
call "%VCVARS%" >nul 2>&1
if errorlevel 1 ( echo [err] vcvars64 failed & exit /b 1 )

set "WINSDK=%SOUFFLE_WINSDK%"
set "WINSDKVER=%SOUFFLE_WINSDKVER%"
if not exist "%WINSDK%\bin\%WINSDKVER%\x64\rc.exe" (
  echo [err] Windows SDK not found at %WINSDK%\bin\%WINSDKVER%\x64\rc.exe
  exit /b 1
)
set "WindowsSdkDir=%WINSDK%\"
set "WindowsSDKVersion=%WINSDKVER%\"
set "UniversalCRTSdkDir=%WINSDK%\"
set "UCRTVersion=%WINSDKVER%"
set "WindowsSdkBinPath=%WINSDK%\bin\%WINSDKVER%\x64"
set "PATH=%WINSDK%\bin\%WINSDKVER%\x64;%PATH%"
set "INCLUDE=%WINSDK%\Include\%WINSDKVER%\ucrt;%WINSDK%\Include\%WINSDKVER%\um;%WINSDK%\Include\%WINSDKVER%\shared;%WINSDK%\Include\%WINSDKVER%\winrt;%INCLUDE%"
set "LIB=%WINSDK%\Lib\%WINSDKVER%\ucrt\x64;%WINSDK%\Lib\%WINSDKVER%\um\x64;%LIB%"
echo [sdk] %WINSDK% (%WINSDKVER%)

if exist "%VSCMAKE%\cmake.exe" set PATH=%VSCMAKE%;%PATH%
where cmake >nul 2>&1
if errorlevel 1 ( echo [err] cmake not found on PATH & exit /b 1 )

cd /d "%SRC%" || ( echo [err] cannot cd to %SRC% & exit /b 1 )

if /i "%TARGET%"=="all" (
  call :one "uno"    || exit /b 1
  call :one "ipopt"  || exit /b 1
  echo === all targets OK ===
  exit /b 0
)
call :one "%TARGET%" || exit /b 1
exit /b 0

REM ---------------------------------------------------------------------------
:one
setlocal EnableDelayedExpansion
set T=%~1
REM Assign on separate lines, never with "set X=value & set Y=value": the space
REM before "&" becomes part of the value, which silently produced a publish
REM target named "EMTGv9.exe        " and clobbered bin\EMTGv9.exe.
if /i "%T%"=="uno"    set TREE=build_cheese
if /i "%T%"=="uno"    set PUB=EMTGv9.exe
if /i "%T%"=="uno"    set EXTRA=
if /i "%T%"=="ipopt"  set TREE=build_ipopt
if /i "%T%"=="ipopt"  set PUB=EMTGv9_ipopt.exe
if /i "%T%"=="ipopt"  set EXTRA=-DSOUFFLE_WITH_IPOPT=ON
if not defined TREE ( echo [err] unknown target "%T%" ^(uno^|ipopt^|all^) & exit /b 1 )

set CLEAN=
if /i "%ACTION%"=="clean" set CLEAN=1
if defined CLEAN (
  echo === removing !TREE! ===
  if exist "!TREE!" rmdir /s /q "!TREE!"
)

echo === configuring (!T%) into !TREE! ===
cmake -S . -B !TREE! -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ^
      -DSOUFFLE_UNO_ROOT="%UNO_ROOT%" ^
      -DIPOPT_INCLUDE_DIR="%IPOPT_INC%" ^
      -DIPOPT_LIBRARY="%IPOPT_LIB%" ^
      -DIPOPT_BIN_DIR="%IPOPT_BIN%" ^
      !EXTRA!
if errorlevel 1 ( echo [err] configure FAILED [!T!] & exit /b 1 )
echo === configure OK (!T!) ===

if /i "%ACTION%"=="configure" exit /b 0

echo === building (!T!) ===
cd /d "%SRC%\!TREE!" || exit /b 1
nmake
if errorlevel 1 ( echo [err] build FAILED [!T!] & exit /b 1 )
cd /d "%SRC%" || exit /b 1

REM Publish through a staging name.  The destination can be held open by zombie
REM EMTGv9 processes, and in this sandbox neither Stop-Process nor taskkill can
REM clear them, so a locked destination must NOT be treated as a build failure:
REM the freshly built image is left beside it as <name>.new and named in the
REM output, ready to be selected through ESFO_EMTG_BIN.
REM
REM Keep every message inside an "if (...)" block free of parentheses: an
REM unescaped ")" closes the block early, which puts "& exit /b 1" outside the
REM conditional and makes a successful build report failure.
copy /y "!TREE!\src\EMTGv9.exe" "bin\!PUB!.new" >nul
if errorlevel 1 ( echo [err] staging copy failed [!T!] & exit /b 1 )
move /y "bin\!PUB!.new" "bin\!PUB!" >nul 2>&1
if errorlevel 1 (
  echo === build OK !T! - but bin\!PUB! is LOCKED, new image left at bin\!PUB!.new ===
  echo     use it with:  set ESFO_EMTG_BIN=%SRC%\bin\!PUB!.new
  exit /b 0
)
echo === build OK !T! to bin\!PUB! ===
exit /b 0

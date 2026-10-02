@echo off
REM ============================================================================
REM  SOUFFLE launcher
REM
REM  Double-click  -> opens the PyEMTG graphical user interface (the same
REM                   "EMTG Python Interface" as the SNOPT build of EMTG),
REM                   already wired to this package's SOUFFLE solver.
REM
REM  With argument -> runs the solver directly on an options file, no GUI:
REM                       run_souffle.bat EVVEU_LTGA.emtgopt
REM
REM  Self-contained: no compiler, no Python installation, no environment
REM  variables and no SNOPT license are required.
REM
REM  Layout:
REM      bin\              EMTGv9.exe (SOUFFLE) + its runtime DLLs + default.emtgopt
REM      python\           bundled Python 3.13 + wxPython/numpy/scipy/matplotlib
REM      PyEMTG\           the PyEMTG GUI ("EMTG Python Interface")
REM      Uno\bin, Uno\deps Uno shared library and dependencies
REM      Universe\         full ephemeris/universe set (for the GUI)
REM      Universe_EVVEU\   the EVVEU mission's universe definition
REM      HardwareModels\   thruster/power/propulsion libraries
REM ============================================================================

setlocal EnableExtensions

set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"

REM --- Uno runtime first: a stale MinGW runtime earlier in PATH causes an
REM --- immediate exit with 0xC0000139 and no output at all.
set "PATH=%HERE%\bin;%HERE%\Uno\bin;%HERE%\Uno\deps;%HERE%\python\Scripts;%HERE%\python\Library\bin;%PATH%"

REM --- Where the Uno interface looks for libuno.dll, independent of PATH order.
set "SOUFFLE_UNO_ROOT=%HERE%\Uno"

REM --- Select the Uno solver (the same binary also contains the SNOPT path).
set "SOUFFLE_NLP_SOLVER=Uno"

REM --- Uno preset: filtersqp (default, SNOPT-like SQP) or ipopt (interior point).
REM --- Override from outside with:  set SOUFFLE_UNO_PRESET=ipopt
if "%SOUFFLE_UNO_PRESET%"=="" set "SOUFFLE_UNO_PRESET=filtersqp"

REM ============================================================================
REM  Direct-run mode: an options file was supplied
REM ============================================================================
if not "%~1"=="" goto :run_solver

REM ============================================================================
REM  GUI mode (double-click, no argument)
REM ============================================================================
if not exist "%HERE%\python\Scripts\pythonw.exe" (
    echo [SOUFFLE] ERROR: bundled Python not found:
    echo [SOUFFLE]        "%HERE%\python\Scripts\pythonw.exe"
    echo [SOUFFLE] This package was built without the GUI. Run a case directly:
    echo [SOUFFLE]     run_souffle.bat EVVEU_LTGA.emtgopt
    pause
    exit /b 2
)
if not exist "%HERE%\PyEMTG\PyEMTG.pyw" (
    echo [SOUFFLE] ERROR: PyEMTG GUI not found: "%HERE%\PyEMTG\PyEMTG.pyw"
    pause
    exit /b 2
)

REM ---------------------------------------------------------------------------
REM  Regenerate PyEMTG.options for THIS location.
REM  The GUI reads absolute paths from this file, so writing it at launch time is
REM  what lets the whole folder be moved to another disk or machine and still work.
REM ---------------------------------------------------------------------------
> "%HERE%\PyEMTG\PyEMTG.options" (
    echo EMTG_path %HERE:\=/%/bin/EMTGv9.exe
    echo default_universe_path %HERE:\=/%/Universe
    echo de_file de440s.bsp
    echo leapseconds_file naif0012.tls
    echo default_small_bodies_file %HERE:\=/%/Universe/ephemeris_files/AllAsteroids.SmallBody
    echo default_HardwarePath %HERE:\=/%/HardwareModels
    echo default_ThrottleTableFile AEPS.ThrottleTable
    echo default_LaunchVehicleLibraryFile default.emtg_launchvehicleopt
    echo default_PowerSystemsLibraryFile default.emtg_powersystemsopt
    echo default_PropulsionSystemsLibraryFile default.emtg_propulsionsystemopt
    echo default_SpacecraftOptionsFile default.emtg_spacecraftopt
)

echo [SOUFFLE] starting the PyEMTG graphical interface ...
echo [SOUFFLE] solver : %HERE%\bin\EMTGv9.exe
echo [SOUFFLE] python : %HERE%\python\Scripts\pythonw.exe
echo [SOUFFLE] preset : %SOUFFLE_UNO_PRESET%

REM PyEMTG resolves its data files relative to the process working directory.
pushd "%HERE%\PyEMTG"
start "" "%HERE%\python\Scripts\pythonw.exe" "PyEMTG.pyw"
popd
exit /b 0

REM ============================================================================
REM  Direct-run mode
REM ============================================================================
:run_solver
set "OPTS=%~1"
if not exist "%OPTS%" (
    if exist "%HERE%\%OPTS%" (
        set "OPTS=%HERE%\%OPTS%"
    ) else (
        echo [SOUFFLE] ERROR: options file "%OPTS%" not found.
        pause
        exit /b 2
    )
)
if not exist "%HERE%\bin\EMTGv9.exe" (
    echo [SOUFFLE] ERROR: "%HERE%\bin\EMTGv9.exe" not found.
    pause
    exit /b 2
)

echo [SOUFFLE] solver : Uno   (SOUFFLE_NLP_SOLVER=%SOUFFLE_NLP_SOLVER%)
echo [SOUFFLE] preset : %SOUFFLE_UNO_PRESET%
echo [SOUFFLE] options: %OPTS%
echo.

REM Run from bin\ exactly like the original EMTG distribution: the executable's
REM default paths are relative to its own directory.
pushd "%HERE%\bin"
"%HERE%\bin\EMTGv9.exe" "%OPTS%"
set "RC=%ERRORLEVEL%"
popd

echo.
echo [SOUFFLE] exit code: %RC%
echo [SOUFFLE] results  : see "forced_working_directory" in your .emtgopt
if not "%RC%"=="0" pause
exit /b %RC%

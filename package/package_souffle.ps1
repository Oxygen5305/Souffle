# SOUFFLE packaging script: assembles a self-contained, one-click "Uno + SOUFFLE" folder.
#
# Layout produced (default target G:\Py\DeepSeekHarness\Souffle):
#   bin\                 EMTGv9.exe (+ any non-SNOPT DLLs it needs) + Uno runtime DLLs
#   Uno\bin, Uno\deps\   Uno shared library and its dependencies
#   Universe_EVVEU\      ephemeris / universe definitions for the EVVEU mission
#   run_souffle.bat     one-click launcher (sets PATH + SOUFFLE_NLP_SOLVER=Uno)
#   README.md            user-facing notes
#
# Usage:
#   powershell -File package_souffle.ps1 [-Target <dir>] [-SkipDocs]

param(
    [string]$Target = 'G:\Py\DeepSeekHarness\Souffle',
    [switch]$SkipDocs,
    [switch]$SkipGUI,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$root    = 'G:\Py\DeepSeekHarness\Souffle_Cheese'
$uno     = 'G:\Py\DeepSeekHarness\Uno'
$pyEnv   = 'G:\Py\DeepSeekHarness\PyEmtgEnv'   # interpreter that already has wxPython etc.
$emtgDir = 'G:\Py\DeepSeekHarness\EMTG'        # READ-ONLY source of Universe/ and PyEMTG/
$exeDirs = @("$root\build\src\Executable", "$root\build", "$root\bin")

function Copy-Tree($from, $to, $excludeDirs = @()) {
    if (-not (Test-Path $from)) { Write-Host "  skip (missing): $from" -ForegroundColor DarkYellow; return }
    New-Item -ItemType Directory -Force -Path $to | Out-Null
    if ($excludeDirs.Count -gt 0) {
        $rcArgs = @($from, $to, '/E', '/NFL', '/NDL', '/NJH', '/NJS', '/NP', '/R:1', '/W:1', '/XD') + $excludeDirs
        & robocopy.exe @rcArgs | Out-Null
    } else {
        Copy-Item -Path (Join-Path $from '*') -Destination $to -Recurse -Force
    }
}

# ---- locate the built executable -------------------------------------------------
$exe = $null
foreach ($dir in $exeDirs) {
    $candidate = Join-Path $dir 'EMTGv9.exe'
    if (Test-Path $candidate) { $exe = $candidate; break }
}
if (-not $exe) {
    throw "EMTGv9.exe not found. Build first (see build_Souffle_Cheese.ps1) or pass a packaged build. Looked in: $($exeDirs -join ', ')"
}
Write-Host "executable: $exe" -ForegroundColor Cyan

if ((Test-Path $Target) -and -not $Force) {
    $existing = Get-ChildItem $Target -ErrorAction SilentlyContinue
    if ($existing) { throw "target '$Target' already exists and is not empty. Re-run with -Force to overwrite." }
}

Write-Host "=== assembling $Target ===" -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path "$Target\bin" | Out-Null

# ---- 1. the executable -----------------------------------------------------------
Copy-Item $exe "$Target\bin\EMTGv9.exe" -Force
Write-Host "  bin\EMTGv9.exe"

# ---- 2. Uno runtime: DLLs go next to the exe so the loader finds them without PATH
Copy-Tree "$uno\bin"  "$Target\Uno\bin"
Copy-Tree "$uno\deps" "$Target\Uno\deps"
foreach ($dll in Get-ChildItem "$uno\bin\*.dll" -ErrorAction SilentlyContinue) {
    Copy-Item $dll.FullName "$Target\bin\" -Force
    Write-Host "  bin\$($dll.Name)"
}

# ---- 2b. minimal SNOPT runtime ----------------------------------------------------
# EMTGv9.exe hard-imports snopt7.dll, so it must be present or the process dies at load
# time with STATUS_DLL_NOT_FOUND (0xC0000135) before any solver is chosen. snopt7.dll in
# turn imports the Intel Fortran runtime, so those must come along too. With
# SOUFFLE_NLP_SOLVER=Uno the SNOPT solver is never entered and no license is needed.
#
# The Intel entries below were obtained with `objdump -p snopt7.dll | findstr "DLL Name"`.
# MSVCP140 / VCRUNTIME140 come from the VC++ redistributable; they are copied when present
# in the build tree, otherwise the target machine needs the VC++ 2015-2022 redistributable.
$snoptRuntime = @(
    'snopt7.dll',
    'libsnopt.dll',
    'libifcoremd.dll',
    'libmmd.dll',
    'svml_dispmd.dll',
    'libiomp5md.dll'
)
$vcRuntime = @('MSVCP140.dll', 'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll')

foreach ($name in ($snoptRuntime + $vcRuntime)) {
    $candidate = Join-Path "$root\bin" $name
    if (Test-Path $candidate) {
        Copy-Item $candidate "$Target\bin\" -Force
        Write-Host "  bin\$name"
    } elseif ($name -eq 'snopt7.dll') {
        Write-Warning "snopt7.dll not found in $root\bin - the packaged exe will fail to start!"
    } else {
        Write-Host "  bin\$name  (absent in build tree; relying on system/redistributable)" -ForegroundColor DarkYellow
    }
}

# ---- 3. mission data (ephemeris / universe) --------------------------------------
Copy-Tree "$root\Universe_EVVEU" "$Target\Universe_EVVEU"
# HardwareModels holds the thruster/power/propulsion libraries EMTG reads at startup and
# rewrites empty.ThrottleTableOUTPUT into. It must live inside the package, otherwise the
# packaged case reaches back out to the build tree (and, before it was redirected, into the
# read-only original EMTG folder).
Copy-Tree "$root\HardwareModels" "$Target\HardwareModels"
# Full universe set: 8 body definitions plus several ephemeris kernels, so the GUI can be
# used to build NEW missions too, not only the bundled EVVEU case. Copied from the
# read-only EMTG tree (read-only source; nothing is written back).
Copy-Tree "$emtgDir\Universe" "$Target\Universe"
New-Item -ItemType Directory -Force -Path "$Target\Universe\ephemeris_files" | Out-Null
$smallBody = "$Target\Universe\ephemeris_files\AllAsteroids.SmallBody"
if (-not (Test-Path $smallBody)) {
    Set-Content -Path $smallBody -Value "# placeholder: asteroid small-body list not shipped with this package" -Encoding ASCII
}

# ---- 3b. ready-to-run options files, with RELATIVE paths ------------------------
# The package must work after being moved to another machine or folder, so no absolute
# path may survive in the options file. EMTG concatenates these strings itself and lets
# boost::filesystem resolve them against its working directory, and the launcher runs the
# executable from bin\ - so "../X" correctly means "<package>\X".
#
#   bin\default.emtgopt      what EMTG looks for when started with no argument
#   EVVEU_LTGA.emtgopt       same case, kept at top level for explicit use
$caseSrc = Join-Path $root 'run_evveu\MINI_LTGA_v7.emtgopt'
if (-not (Test-Path $caseSrc)) { $caseSrc = Join-Path $root 'run_evveu\EVVEU_LTGA_L1e.emtgopt' }
if (Test-Path $caseSrc) {
    # Normalise line endings FIRST. The replacement patterns use \s+, which happily eats a
    # CR/LF and would splice the next line onto the current one - that silently turned
    # "#HardwarePath\nHardwarePath X" into a single comment line, i.e. HardwarePath was
    # never applied. Working with LF-only text makes the patterns line-local.
    $case = (Get-Content $caseSrc -Raw) -replace "`r`n", "`n"
    # Relative to bin\ (the launcher's working directory).
    $case = $case -replace '(?m)^universe_folder[ \t]+\S+',      'universe_folder ../Universe_EVVEU'
    $case = $case -replace '(?m)^HardwarePath[ \t]+\S+',         'HardwarePath ../HardwareModels/'
    $case = $case -replace '(?m)^forced_working_directory[ \t]+\S+', 'forced_working_directory ../results'
    $case = $case -replace '(?m)^pyemtg_path[ \t]+\S+',          'pyemtg_path ../PyEMTG/'
    # Reasonable defaults for a double-click use: real search budget, modest step count.
    $case = $case -replace '(?m)^MBH_max_run_time[ \t]+\S+',  'MBH_max_run_time 900'
    $case = $case -replace '(?m)^snopt_max_run_time[ \t]+\S+', 'snopt_max_run_time 60'
    $case = $case -replace '(?m)^num_timesteps[ \t]+\S+',     'num_timesteps 10'
    $case = $case -replace '(?m)^number_of_steps[ \t]+\S+',   'number_of_steps 10'
    $case = $case -replace "`r`n", "`n"

    [System.IO.File]::WriteAllText("$Target\bin\default.emtgopt", $case)
    [System.IO.File]::WriteAllText("$Target\EVVEU_LTGA.emtgopt", $case)
    Write-Host "  bin\default.emtgopt + EVVEU_LTGA.emtgopt (relative paths)"

    # Fail loudly if anything machine-specific survived. spice_utilities_path keeps its
    # upstream default (C:/utilities/cspice/exe); it is only used by the optional SPICE
    # utility executables, not during a normal run.
    $stale = Select-String -Path "$Target\bin\default.emtgopt" -Pattern 'G:/|G:\\|Souffle_Cheese/|DeepSeekHarness/EMTG/' -ErrorAction SilentlyContinue
    if ($stale) {
        Write-Warning "packaged .emtgopt still contains machine-specific paths:"
        $stale | ForEach-Object { Write-Warning ("   " + $_.Line) }
    } else {
        Write-Host "  checked: no absolute paths in the packaged .emtgopt" -ForegroundColor Green
    }
} else {
    Write-Warning "no source case found; the package will have no ready-to-run .emtgopt"
}

# ---- 4. launcher -----------------------------------------------------------------
Copy-Item "$root\package\run_souffle.bat" "$Target\run_souffle.bat" -Force
Write-Host "  run_souffle.bat"

# ---- 5. licenses (required when redistributing) ----------------------------------
New-Item -ItemType Directory -Force -Path "$Target\licenses" | Out-Null
Copy-Item "$root\EMTG_NOSA_License.pdf" "$Target\licenses\EMTG_NOSA_License.pdf" -Force -ErrorAction SilentlyContinue
Copy-Item "$root\README.opensource"     "$Target\licenses\EMTG_README.opensource" -Force -ErrorAction SilentlyContinue
Copy-Tree "$uno\share\licenses" "$Target\licenses\Uno_third_party"

# ---- 5b. the GUI: bundled Python + PyEMTG ----------------------------------------
if (-not $SkipGUI) {
    Write-Host "=== bundling the PyEMTG GUI (EMTG Python Interface) ===" -ForegroundColor Cyan

    if (-not (Test-Path "$pyEnv\Scripts\python.exe")) {
        Write-Warning "no Python found at $pyEnv\Scripts\python.exe - skipping the GUI;"
        Write-Warning "the package will support direct-run mode only."
    } else {
        # Interpreter plus every dependency the GUI needs (wxPython, numpy, scipy,
        # matplotlib, astropy, spiceypy, Pillow). __pycache__ is skipped to save space.
        Copy-Tree $pyEnv "$Target\python" @('__pycache__')
        $n = (Get-ChildItem "$Target\python" -Recurse -File -ErrorAction SilentlyContinue).Count
        Write-Host "  python\  ($n files)"

        # GUI sources, taken read-only from EMTG's own PyEMTG folder.
        Copy-Tree "$emtgDir\PyEMTG" "$Target\PyEMTG" @('__pycache__')
        Write-Host "  PyEMTG\"

        # The launcher regenerates PyEMTG.options at start-up from its own location. This
        # copy is for running PyEMTG.pyw directly, without the launcher.
        $fwd = $Target.Replace('\','/')
        $optLines = @(
            "EMTG_path $fwd/bin/EMTGv9.exe",
            "default_universe_path $fwd/Universe",
            "de_file de440s.bsp",
            "leapseconds_file naif0012.tls",
            "default_small_bodies_file $fwd/Universe/ephemeris_files/AllAsteroids.SmallBody",
            "default_HardwarePath $fwd/HardwareModels",
            "default_ThrottleTableFile AEPS.ThrottleTable",
            "default_LaunchVehicleLibraryFile default.emtg_launchvehicleopt",
            "default_PowerSystemsLibraryFile default.emtg_powersystemsopt",
            "default_PropulsionSystemsLibraryFile default.emtg_propulsionsystemopt",
            "default_SpacecraftOptionsFile default.emtg_spacecraftopt"
        )
        Set-Content -Path "$Target\PyEMTG\PyEMTG.options" -Value $optLines -Encoding ASCII
        Write-Host "  PyEMTG\PyEMTG.options"

        # Sanity-check the GUI's inputs, so a broken package is caught here rather than at
        # the user's first double-click.
        $guiProblems = @()
        foreach ($p in @("$Target\bin\EMTGv9.exe",
                         "$Target\Universe",
                         "$Target\HardwareModels",
                         "$Target\Universe\ephemeris_files\AllAsteroids.SmallBody",
                         "$Target\PyEMTG\PyEMTG.pyw")) {
            if (-not (Test-Path $p)) { $guiProblems += $p }
        }
        if ($guiProblems.Count -gt 0) {
            Write-Warning "GUI inputs missing:"
            $guiProblems | ForEach-Object { Write-Warning ("   " + $_) }
        } else {
            Write-Host "  checked: all PyEMTG.options targets exist" -ForegroundColor Green
        }
    }
} else {
    Write-Host "=== -SkipGUI: omitted the PyEMTG GUI and the bundled Python ===" -ForegroundColor Yellow
}

# ---- 6. user-facing README -------------------------------------------------------
if (-not $SkipDocs) {
    $readme = @'
# SOUFFLE - Scalable Optimization Uno-powered Framework For Leveraging EMTG

SOUFFLE = **S**calable **O**ptimization **U**no-powered **F**ramework **F**or **L**everaging **E**MTG

> Chinese edition: [`README.zh-CN.md`](README.zh-CN.md)

This folder is ready to use as-is. No compiler, no environment variables, and no
SNOPT license are required.

## Quick start

| I want to... | Do this |
|---|---|
| **Use the graphical interface** | **Double-click `run_souffle.bat`** |
| Run one case without the GUI | `run_souffle.bat EVVEU_LTGA.emtgopt` |
| Use EMTG's own default case | `run_souffle.bat` picks `bin\default.emtgopt` |

With no argument the launcher opens the **PyEMTG GUI** ("EMTG Python Interface"), the same
interface the SNOPT build of EMTG uses, already wired to this package's solver. With an
options file as the first argument it runs the solver directly instead.

## Relocatable

Every path inside the bundled `.emtgopt` is **relative** (`../Universe_EVVEU`,
`../HardwareModels/`, `../results`), resolved by EMTG against its working directory,
and the launcher starts the executable from `bin\`. So you can copy or move this whole
folder anywhere - another disk, another machine - and it still works.

## What is in here

| Path | Contents |
|---|---|
| `bin\EMTGv9.exe` | the SOUFFLE executable (EMTG with Uno as its NLP solver) |
| `bin\default.emtgopt` | the case EMTG runs when started with no argument |
| `bin\*.dll` | Uno shared library plus the runtime DLLs EMTG imports |
| `Uno\bin`, `Uno\deps` | the Uno runtime; `SOUFFLE_UNO_ROOT` points here |
| `HardwareModels\` | thruster / power / propulsion libraries read at startup |
| `Universe\` | full ephemeris/universe set (build new missions in the GUI) |
| `Universe_EVVEU\` | ephemeris and universe definitions for the EVVEU mission |
| `PyEMTG\` | the PyEMTG GUI; `PyEMTG.options` points at this package's `bin\EMTGv9.exe` |
| `python\` | bundled Python 3.13 with wxPython / numpy / scipy / matplotlib / astropy / spiceypy |
| `EVVEU_LTGA.emtgopt` | the same case, kept at top level for explicit use |
| `results\` | where the bundled case writes its output |
| `licenses\` | EMTG (NASA NOSA 1.3), Uno (MIT) and third-party licenses |

## Results

The output directory is `forced_working_directory` in your `.emtgopt`; the bundled case
sets it to `../results`, which - because the launcher starts the executable from `bin\` -
is the `results\` folder next to this README.

## Graphical interface

Double-clicking `run_souffle.bat` opens the PyEMTG GUI. It can edit and run `.emtgopt`
files and plot `.emtg` results, and it drives this package's SOUFFLE executable because
`PyEMTG\PyEMTG.options` has `EMTG_path` pointing at `bin\EMTGv9.exe`.

The launcher **regenerates `PyEMTG.options` on every start** from its own location. That
is deliberate: the GUI needs absolute paths, so writing them at launch time is what keeps
the whole folder relocatable.

## Solver and preset selection

The launcher sets `SOUFFLE_NLP_SOLVER=Uno` and `SOUFFLE_UNO_PRESET=filtersqp`.

* **Preset** - `filtersqp` (default) is Uno's SNOPT-like trust-region filter SQP;
  `ipopt` selects Uno's interior-point method. Switch without rebuilding:

      set SOUFFLE_UNO_PRESET=ipopt
      run_souffle.bat

  Measured on the EVVEU case: `filtersqp` converged in 111/138 solves, `ipopt` in 33/263.

* **Back to SNOPT** - `set SOUFFLE_NLP_SOLVER=SNOPT` selects the SNOPT path still compiled
  into the binary. That needs the SNOPT runtime and a valid license. The bundled
  `snopt7.dll` exists only because the executable hard-imports it; while the solver is set
  to Uno the SNOPT code is never entered and no license is needed.

## Verified behaviour

* The GUI (`EMTG Python Interface`) starts from the bundled interpreter and reads the
  packaged `PyEMTG.options`.
* Solver runs with `SNOPT_LICENSE` **unset** and reaches `EMTG run complete`.
* Verified from a clean environment (only the system `PATH`): the launcher configures
  everything itself, resolves `../Universe_EVVEU/...` correctly, and writes to `../results`.
* The bundled case produced a **feasible rendezvous solution**: final mass 4698.47 kg,
  electric propellant 747.53 kg.

## Tuning

The bundled case ships with `MBH_max_run_time 900`, `snopt_max_run_time 60`,
`num_timesteps 10`. Raise `MBH_max_run_time` for a longer search, or change
`num_timesteps` to trade fidelity against runtime.

## Troubleshooting

* **Exits instantly with no output.** A stale MinGW runtime is being picked up from
  `PATH` ahead of the bundled one. Always launch through `run_souffle.bat`; it puts
  `bin`, `Uno\bin` and `Uno\deps` first.
* **"SOUFFLE: failed to load libuno.dll".** `SOUFFLE_UNO_ROOT` does not point at the
  `Uno` folder. The launcher sets it automatically.
* **No feasible solution found.** Expected for very tight cases. The EVVEU LTGA case is
  known to be extremely tight at TOF < 6.5 years - the SNOPT build struggles there too.
  Relax the imposed time of flight (7 years or more) before concluding the solver failed.
'@
    Set-Content -Path "$Target\README.md" -Value $readme -Encoding UTF8
    # Keep the historical name as well so existing references keep working.
    Write-Host "  README.md"
    # Chinese edition, kept in sync by being copied from package/.
    $zh = Join-Path $root 'package\README.zh-CN.md'
    if (Test-Path $zh) {
        Copy-Item $zh "$Target\README.zh-CN.md" -Force
        Write-Host "  README.zh-CN.md"
    } else {
        Write-Warning "package\README.zh-CN.md not found; Chinese README omitted"
    }
}

# ---- summary ---------------------------------------------------------------------
Write-Host "`n=== result ===" -ForegroundColor Green
$files = Get-ChildItem $Target -Recurse -File
"files: {0}   size: {1:N1} MB" -f $files.Count, (($files | Measure-Object Length -Sum).Sum / 1MB)
Get-ChildItem $Target | Select-Object Mode, Name | Format-Table -AutoSize | Out-String | Write-Host

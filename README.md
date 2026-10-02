# SOUFFLE — Scalable Optimization Uno-powered Framework For Leveraging EMTG

[![License](https://img.shields.io/badge/License-NASA%20NOSA%201.3-blue.svg)](https://opensource.org/license/nasa1-3-php)
[![Solver](https://img.shields.io/badge/solver-Uno%20%7C%20SNOPT-lightgrey)](https://github.com/cvanaret/Uno)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)](https://www.microsoft.com/en-us/windows)
[![Built with](https://img.shields.io/badge/built%20with-MSVC%20%2B%20NMake-lightgrey)](https://visualstudio.microsoft.com/)

NASA's [EMTG](https://github.com/nasa/EMTG) with its inner-loop NLP solver swapped from the
commercial **SNOPT** to the open-source **[Uno](https://github.com/cvanaret/Uno)** — same MGALT
transcription, same mission modelling, same outputs, **no commercial license required**.

> 中文说明见 [README_zh.md](README_zh.md)

| Layer | What it is |
|---|---|
| **Solver** | Uno ≥ 2.9.0 (filterSQP by default; also ipopt, funnel, Penalty), loaded at run time |
| **Transcription** | EMTG MGALT, with MBH global search — unchanged from upstream |
| **GUI** | PyEMTG (wxPython), the same interface the SNOPT build uses |
| **Default build** | needs **no SNOPT installation, no SNOPT headers, and does not import `snopt7.dll`** |

## Why this exists

SNOPT is commercial software. Its evaluation licenses expire and its runtime cannot be
redistributed. EMTG has no other NLP solver in-tree, so an expired license means the tool stops
working.

SOUFFLE removes that dependency without touching EMTG's physics, transcription, or mission model:
only the solver interface is new.

## Features

- **Drop-in solver swap** — the inner loop sits behind an abstract `NLP_interface`; SNOPT and Uno
  are interchangeable implementations selected by an environment variable.
- **SNOPT is opt-in** — `SOUFFLE_WITH_SNOPT` defaults to `OFF`. In that configuration CMake does
  not ask for a SNOPT directory, no SNOPT header is included, and the linked executable does not
  import `snopt7.dll`.
- **No link-time dependency on Uno either** — Uno's Windows release is MinGW-built and cannot be
  linked by MSVC, so SOUFFLE calls it through `LoadLibraryExW` + `GetProcAddress`. The build needs
  only `Uno_C_API.h`.
- **Same GUI** — PyEMTG edits and runs `.emtgopt` files and plots `.emtg` results, exactly as in
  the SNOPT build.
- **Same physics** — MGALT, forward/backward shooting, match-point constraints, the journey/phase
  tree, and the Monotonic Basin Hopping global search are untouched.

## Installation

### 1. Packaged binary

Download the self-contained release and double-click the launcher. It bundles the executable, the
Uno runtime, the **PyEMTG GUI with its own Python interpreter**, SPICE kernels, and hardware
models — nothing to install, no environment variables to set.

| I want to… | Do this |
|---|---|
| **Open the graphical interface** | double-click `run_souffle.bat` |
| Run one case without the GUI | `run_souffle.bat EVVEU_LTGA.emtgopt` |

### 2. Build from source

Requires Visual Studio 2022 (MSVC v143), CMake, the Windows SDK, and Boost. Uno and the SPICE
kernels are fetched separately.

**Full step-by-step instructions — including where to download Boost, Uno and `de440s.bsp` — are
in [BUILDING.md](BUILDING.md).** The short version:

```powershell
cmake -S . -B build -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release `
    -DSOUFFLE_WITH_UNO=ON `
    -DSOUFFLE_UNO_ROOT=<path to Uno> `
    -DSOUFFLE_DEFAULT_SOLVER=Uno `
    -DBOOST_ROOT=<path to Boost>
cmake --build build          # -> build\src\EMTGv9.exe
```

There is no SNOPT path in this command.

### Build switches

| Option | Default | Meaning |
|---|---|---|
| **`SOUFFLE_WITH_SNOPT`** | **`OFF`** | compile the SNOPT interface. Off ⇒ no SNOPT directory required, no SNOPT header included, no `snopt7.dll` import |
| `SOUFFLE_WITH_UNO` | `ON` | compile the Uno interface |
| `SOUFFLE_UNO_ROOT` | — | Uno install directory (`include/uno`, `bin`, `deps`) |
| `SOUFFLE_DEFAULT_SOLVER` | `Uno` | solver used when `SOUFFLE_NLP_SOLVER` is unset |

## Usage

### Graphical interface

Double-click the launcher. PyEMTG opens, already wired to the bundled solver through
`PyEMTG\PyEMTG.options`. The launcher regenerates that file at start-up from its own location,
which is what keeps the folder relocatable.

### Command line

```bat
set SOUFFLE_NLP_SOLVER=Uno
set SOUFFLE_UNO_ROOT=<path to Uno>
EMTGv9.exe my_case.emtgopt
```

| Environment variable | Default | Meaning |
|---|---|---|
| `SOUFFLE_NLP_SOLVER` | `Uno` | `Uno`, or `SNOPT` when the build enables it |
| `SOUFFLE_UNO_ROOT` | baked in at configure time | where `libuno.dll` lives |
| `SOUFFLE_UNO_PRESET` | `filtersqp` | Uno preset: `filtersqp`, `ipopt`, `funnel`, `Penalty` |

Switch to the interior-point method without rebuilding:

```bat
set SOUFFLE_UNO_PRESET=ipopt
EMTGv9.exe my_case.emtgopt
```

## Architecture

```
EMTG inner loop (MBH / FilamentWalker / problem.cpp)
        │  depends only on the abstract base
        ▼
   NLP_interface
        ├── SNOPT_interface        (compiled only with SOUFFLE_WITH_SNOPT=ON)
        └── Souffle_interface      (Uno mapping)
                │
                ▼
           SouffleApi              LoadLibraryExW + GetProcAddress
                │
                ▼
           libuno.dll              Uno 2.9.0, MinGW-built, stable C ABI
```

Why dynamic loading: MSVC cannot link Uno's MinGW import library, but `libuno.dll` exports
undecorated C symbols, so the C ABI is stable across compilers. The cost is that the DLL has to be
located at run time (`SOUFFLE_UNO_ROOT`) instead of being resolved at link time.

Two mapping details:

- **Scaling.** EMTG works in scaled variables. Uno is given `lower = 0` and
  `upper = (X_upper − X_lower) / X_scale`, and results are unscaled with
  `X_unscaled = X_scaled · X_scale + X_lower`.
- **Termination callbacks are deliberately not registered.** With one registered, every solve
  returned `opt_status=5` (`UNO_USER_TERMINATION`) after a single iteration. Passing `nullptr`
  instead let the iteration count go from 1 to 16000.

The full interface mapping is in [docs/DESIGN.md](docs/DESIGN.md).

## Verification

### The SNOPT-free claim

| Check | Result |
|---|---|
| `dumpbin /imports` on the default build | 14 imported DLLs, **no `snopt7.dll`** (a SNOPT-enabled build imports 15, including it) |
| Run with **zero files matching `snopt`** in the directory and `SNOPT_LICENSE` unset | reaches `EMTG run complete` and solves normally |

### Solver presets

Measured on the EVVEU LTGA case:

| Preset | Convergence |
|---|---|
| `filtersqp` (default) | **111 / 138** `UNO_SUCCESS` |
| `ipopt` | 33 / 263 |

### Against the SNOPT reference

Same 2044 launch window, same sequence, same initial mass (5305 kg), same `num_timesteps` (40),
same tolerances (1e-6 / 1e-5):

| | SOUFFLE / Uno | SNOPT reference |
|---|---|---|
| Final mass | **4598.48 kg** | 4438.15 kg |
| Electric propellant | **706.52 kg** | 866.85 kg |

The same comparison on an external pipeline — ESFO_Uranus's tier-2 MGALT pipeline driven by
SOUFFLE — gives **4150.98 kg** against its SNOPT reference of **4150.07 kg**.

Full numbers are in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Layout

```
src/                    EMTG source + the SOUFFLE additions
  InnerLoop/
    NLP_interface.h         solver abstraction
    NLP_solver_factory.*    picks SNOPT or Uno at run time
    SouffleApi.*            dynamic loading of libuno.dll
    Souffle_interface.*     EMTG <-> Uno model / bound / Jacobian mapping
    SNOPT_interface.*       upstream, compiled only when SOUFFLE_WITH_SNOPT=ON
PyEMTG/                 PyEMTG GUI (wxPython)
package/                packaging script, launcher template, GUI config template
HardwareModels/         thruster / power / propulsion libraries
Universe*/              universe definitions (SPICE kernels fetched separately)
run_evveu/              example .emtgopt cases
docs/                   DEVELOPMENT.md, DESIGN.md, PLAN.md, Uno option dump
BUILDING.md             build guide
tier2_driver.py         drives an external EMTG pipeline with SOUFFLE
```

## Notes and pitfalls

| Symptom | Cause |
|---|---|
| Exits instantly, no output | a stale MinGW runtime earlier in `PATH`; put Uno's `bin` and `deps` first |
| `failed to load libuno.dll` | `SOUFFLE_UNO_ROOT` is wrong or unset |
| Enormous log files | Uno options are case-sensitive — lowercase tokens are rejected silently and fall back to defaults |
| Solver stops after 1 iteration | a termination callback was registered with Uno (see Architecture) |

## License

SOUFFLE is a fork of NASA's EMTG and is distributed under the same terms:
the **[NASA Open Source Agreement 1.3](https://opensource.org/license/nasa1-3-php)**.

Copyright © 2024 United States Government as represented by the Administrator of the National
Aeronautics and Space Administration. All Rights Reserved.

SNOPT is **not** distributed with this project, and the default build does not need it. Uno is
MIT-licensed. See [LICENSE](LICENSE) for the full third-party component table.

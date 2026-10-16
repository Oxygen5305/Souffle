# SOUFFLE — Scalable Optimization Uno-powered Framework For Leveraging EMTG

[![License](https://img.shields.io/badge/License-NASA%20NOSA%201.3-blue.svg)](https://opensource.org/license/nasa1-3-php)
[![Solver](https://img.shields.io/badge/solver-Uno%20%7C%20Ipopt-lightgrey)](https://github.com/cvanaret/Uno)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)](https://www.microsoft.com/en-us/windows)
[![Built with](https://img.shields.io/badge/built%20with-MSVC%20%2B%20NMake-lightgrey)](https://visualstudio.microsoft.com/)

NASA's [EMTG](https://github.com/nasa/EMTG) with its inner-loop NLP solver replaced by the
open-source **[Uno](https://github.com/cvanaret/Uno)** (default) and
**[Ipopt](https://github.com/coin-or/Ipopt)**. Same MGALT transcription, same mission modelling,
same outputs, **no commercial license required**.

> Chinese documentation: [README_zh.md](README_zh.md)

| Layer | What it is |
|---|---|
| **Solvers** | [Uno](https://github.com/cvanaret/Uno) ≥ 2.9.0 (preset `filtersqp` by default; Uno also offers `ipopt`, `funnel` and `Penalty` presets of its own) and [Ipopt](https://github.com/coin-or/Ipopt) 3.14. `united/` runs both and keeps the better answer. |
| **Transcription** | EMTG MGALT, with MBH global search — unchanged from upstream |
| **GUI** | PyEMTG (wxPython), the same interface the SNOPT build uses |
| **Default build** | needs **no SNOPT installation, no SNOPT headers, and does not import `snopt7.dll`** |

## Why this exists

SNOPT is commercial software: its evaluation licenses expire, and its runtime cannot be
redistributed. EMTG ships no other NLP solver, so an expired license stops the tool from working.

SOUFFLE removes that dependency. EMTG's physics, transcription and mission model are unchanged;
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
- **Ipopt as a second inner-loop solver** — linked normally, because its import library is a COFF
  archive MSVC can use. Selected with `SOUFFLE_NLP_SOLVER=IPOPT`.
- **Union search** — cold-started Uno and Ipopt settle in **different basins**, so `united/` runs
  both, plus a third Ipopt warm-started from Uno's answer, as separate processes, and keeps the
  best result. On 130 tasks across four mission families, that averaged about 19 kg more final
  mass than the better of Uno and a cold Ipopt alone, and 87 kg more than Uno by itself. Union
  search is also far quicker than the SNOPT build: the per-solve limit drops from 60 s to 15 s /
  8 s, and a run finishes in about 62 s, against up to 900 s with the shipped case defaults. The
  parallel leg costs nothing extra: Uno takes 62 s and Ipopt 33 s, yet running both takes the same
  62 s, because Ipopt runs behind Uno. Separate processes are a requirement, not a style choice —
  the two solvers ship same-named MinGW runtimes built by different toolchains, and Windows
  resolves DLLs by name.
- **Same GUI** — PyEMTG edits and runs `.emtgopt` files and plots `.emtg` results, exactly as in
  the SNOPT build.
- **Same physics** — MGALT, forward/backward shooting, match-point constraints, the journey/phase
  tree, and the Monotonic Basin Hopping global search are untouched.

## Installation

### 1. Packaged binary

**[Download `Souffle.zip` from the Releases page »](../../releases/latest)**

Unzip it anywhere and double-click `run_souffle.bat`. The folder is self-contained: the
executable, the Uno runtime, the **PyEMTG GUI with its own Python interpreter**, the SPICE
kernels and the hardware models are all inside — nothing to install, no environment
variables to set.

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

No SNOPT directory, header or library is involved in this build.

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
`PyEMTG\PyEMTG.options`. The launcher **regenerates that file from its own location on every
start**, which is what keeps the folder relocatable.

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
| `SOUFFLE_UNO_PRESET` | `filtersqp` | Uno preset: `filtersqp`, `ipopt`, `funnel`, `Penalty`. Values are **case-sensitive** |

Switch to the interior-point method without rebuilding:

```bat
set SOUFFLE_UNO_PRESET=ipopt
EMTGv9.exe my_case.emtgopt
```

---

# Technical details

## Design

How the solver is wired into EMTG: the dynamic loading layer, the mapping between the solver
interface and EMTG's data structures, callback and termination handling, and the constraints
confirmed by measurement.

---



### 1. Overall structure

```
EMTG inner loop (MBH / FilamentWalker / problem.cpp)
        │  depends only on the abstract base
        ▼
   NLP_interface                ← abstract base (this work adds a virtual destructor + getInform())
        │
        ├── SNOPT_interface     ← pre-existing, kept
        └── Souffle_interface   ← new, implements the Uno mapping
                │
                ▼
           SouffleApi           ← dynamic loading layer (LoadLibraryExW + GetProcAddress)
                │
                ▼
           libuno.dll           ← Uno 2.9.0 (MinGW-built, C ABI)
```

**Dynamic loading is mandatory**: upstream EMTG is built with MSVC, while Uno's Windows release is
in MinGW format (`libuno.dll.a`), which the MSVC linker cannot consume. Because `libuno.dll`
exports **undecorated C symbols**, the C ABI is stable across compilers, so SOUFFLE resolves the
entry points at run time through `LoadLibraryExW` + `GetProcAddress` and **links no Uno library at
all**.

**Cost**: `libuno.dll` must be located at run time (`SOUFFLE_UNO_ROOT`), and there is no
compile-time symbol checking, so start-up reports an explicit error instead of failing silently.

---

### 2. Decoupling changes (minimal intrusion into upstream EMTG)

| File | Change |
|---|---|
| `NLP_interface.h` | adds `virtual ~NLP_interface() {}` (the base had no virtual destructor, so polymorphic deletion was UB); promotes SNOPT's `getInform()` to a base-class virtual function returning `99` by default |
| `SNOPT_interface.h` | marks `getInform()` as `override` |
| `monotonic_basin_hopping.h/.cpp` | `SNOPT_interface*` → `NLP_interface*` (constructor, `initialize`, member) |
| `FilamentWalker.h/.cpp` | same as above |
| `NLPoptions.h/.cpp` | adds `solver_name`, defaulted from the CMake `SOUFFLE_DEFAULT_SOLVER` |
| `NLP_solver_factory.h/.cpp` | **new**: `create_NLP_solver()` returns `std::unique_ptr<NLP_interface>` |
| `problem.cpp` | the three instantiation points (MBH / NLP / FilamentWalker) go through the factory |

**Principle**: with `SOUFFLE_NLP_SOLVER=SNOPT`, every behaviour of upstream EMTG stays unchanged.
The SNOPT path remains inside the same executable so the two solvers can be compared.

---

### 3. Scaling contract

EMTG's NLP variables are **scaled**. The bounds Uno sees must be recomputed:

```
Uno.lower = 0
Uno.upper = (X_upperbounds[i] - X_lowerbounds[i]) / X_scale_factors[i]
```

Unscaling (handing Uno's solution back to EMTG):

```
X_unscaled[i] = X_scaled[i] * X_scale_factors[i] + X_lowerbounds[i]
```

Derivation: EMTG defines scaling internally as `X_unscaled = X_scaled * scale + Xlower`, so the
scaled lower bound is 0 and the upper bound is `(Xu - Xl) / scale`.

Dividing by `scale` is mandatory: passing `X_upperbounds` directly makes the bound too large by
that factor, and the solver then searches a physically meaningless region — the symptom is
"always finds a solution but the constraints are poor".

---

### 4. Constraint and Jacobian mapping

EMTG splits its constraints into several segments (the F vector) and provides a **sparse
Jacobian** `G`. Uno's C API needs:

| What Uno needs | Where it comes from |
|---|---|
| Number of constraints | `myProblem->F.size()` |
| Constraint bounds | `F_lowerbounds` / `F_upperbounds` |
| Number of Jacobian nonzeros | `G.size()`, or the `constraint_jacobian_source` struct |
| Row/column indices | `iGfun` / `jGvar` (already provided by EMTG) |

**Key point**: Uno **compresses** the Jacobian, dropping explicit zeros. The callbacks therefore
map Uno's compact indices back to positions in EMTG's `G` through `constraint_jacobian_source`;
otherwise the Jacobian is misaligned, and the solver still "finishes" but converges to a wrong
point.

---

### 5. Uno interface options

| Option | Value | Reason |
|---|---|---|
| `logger` | `"SILENT"` | **must be upper case**. A lower-case `"silent"` is silently rejected and falls back to the default, producing a 22 MB / 373,772-line log and making run time I/O-bound |
| `dual_tolerance` | ≤ `1e-4` | measured: Uno rejects anything above that |
| `max_iterations` | `2 × major_iterations_limit` | EMTG's "major iteration" semantics differ from Uno's, so the limit must be relaxed |
| `time_limit` | `max_run_time_seconds` | kept consistent with the EMTG configuration |
| solver preset | `SOUFFLE_UNO_PRESET`, default `filtersqp` | an illegal value must **warn explicitly and then fall back**, never silently |

#### 5.1 Termination callback: **do not register**

`uno_set_solver_callbacks(..., notify, nullptr, this)` — the third argument (termination callback)
**must be `nullptr`**.

**Why (measured)**: with a termination callback registered, every solve returned `opt_status=5`
(`UNO_USER_TERMINATION`) with `iters=1`, terminating on the first iteration. Inside the callback
`elapsed=0 limit=60 stop_on_goal=0`, and it never returned a non-zero value: Uno's calling
convention for that callback does not match this implementation.

Passing `nullptr` raised the iteration count from 1 to 16000, and `filtersqp`'s success rate became
111/138.

> Termination is therefore controlled by `time_limit` and `max_iterations`, no longer by a
> callback.

---

### 6. Factory and environment variables

```cpp
std::unique_ptr<NLP_interface> create_NLP_solver();
```

Selection logic:

1. read the `SOUFFLE_NLP_SOLVER` environment variable
2. when it is unset, use the compile-time default (CMake `SOUFFLE_DEFAULT_SOLVER`, `Uno` in this package)
3. the value `Uno` → `Souffle_interface`; `SNOPT` → `SNOPT_interface`; anything else → report an error and exit

`Souffle_interface` reads `SOUFFLE_UNO_ROOT` to locate `libuno.dll`, falling back to the
compile-time macro `SOUFFLE_UNO_ROOT_DEFAULT` when it is unset.

---

### 7. Logging and observability

Solver diagnostic line:

```
SOUFFLE[solve]: preset=filtersqp opt_status=4 sol_status=0 iters=2012 cpu=23.79 f=-0.864531
```

**This line is not controlled by `quiet_NLP`.** `quiet_NLP 1` suppresses EMTG's own solve output,
which leaves no way to tell whether Uno is running at all, so the diagnostic line is printed
unconditionally.

Field meanings:

| Field | Meaning |
|---|---|
| `opt_status` | Uno's `UNO_OPTIMIZATION_STATUS`: `2` = success, `4` = iteration limit, `5` = user termination |
| `sol_status` | `UNO_SOLUTION_STATUS`: `0` = feasible |
| `iters` | iteration count (normally hundreds to thousands; 1 means a termination callback cut it short) |
| `cpu` | solve CPU seconds |
| `f` | objective value |

---

### 8. Union search (united)

A single solver stops in whichever local basin it reaches first. Uno and Ipopt differ in starting
directions, trust-region strategies and stopping criteria, and **measurements show they land in
different places**, so running both and keeping the better answer pays off against either one
alone.

#### 8.1 Why it must be two processes

Uno and Ipopt each ship **same-named MinGW runtimes built by different toolchains**
(`libwinpthread-1.dll`, `libgcc_s_seh-1.dll`, `libgomp-1.dll`, `libatomic-1.dll`,
`libquadmath-0.dll`). Windows resolves modules **by name**, so once the first-loaded side has
claimed a name, the other side's delay-load resolves to the wrong version — an in-process
arrangement was measured to exit at Ipopt's first iteration with `0xc06d007f`
(`ERROR_DELAY_LOAD_FAILED`). Reordering `PATH` merely transfers the failure to the other side.

Union search is therefore orchestrated by `united/united.py` as **separate processes**, rather than
by switching solvers inside one `run_NLP`.

#### 8.2 Two orchestrations

| `--mode` | Orchestration | Wall clock per task |
|---|---|---|
| `parallel` | Uno ‖ separate Ipopt → keep the better | ≈ 62 s |
| `both` (default) | the two legs above, then one Ipopt warm-started from **Uno's solution** → best of three | ≈ 100 s |

`both` is the default: a cold-started Ipopt and a warm-started Ipopt **land in different basins**,
and the best of three averages about **19 kg** more than `max(Uno, separate Ipopt)`, at the cost of
one extra serial Ipopt leg.

#### 8.3 Wall clock is set by the per-solve limit

Wall clock ≈ `max(MBH budget, per-solve limit)`, because MBH cannot interrupt a solve that is
already running. The real knob is the **per-solve limit**, not the total budget.

#### 8.4 One binary, two solvers

The release ships a single `SOUFFLE.exe`. `united.py` uses `SOUFFLE_NLP_SOLVER` to decide which
solver each leg runs, and `SOUFFLE_IPOPT_LIB` to point at the directory holding `ipopt-3.dll`. Data
paths inside a case are made **absolute** by `geometry.py` relative to the package root, because
union search runs the executable in per-stage directories while the case's paths were originally
relative to `bin/`.

---

## Development and verification record

SOUFFLE is based on NASA's [EMTG](https://github.com/nasa/EMTG) (Evolutionary Mission Trajectory
Generator). It replaces the inner-loop nonlinear optimizer, **SNOPT**, with the open-source solver
**[Uno](https://github.com/cvanaret/Uno)**, so the tool remains usable after an SNOPT evaluation
license expires.



### 1. Feasibility

| Conclusion | Evidence |
|---|---|
| Uno has a complete **C interface** | `Uno\include\uno\Uno_C_API.h`: model, variable/constraint bounds, objective, sparse Jacobian, initial point, options, presets, callbacks, result queries — 60+ functions |
| The C API symbols really are exported | `libuno.dll` exports **74 undecorated C symbols**; in `libuno.a`, `uno_create_model` / `uno_optimize` / `uno_set_solver_preset` and others are all `T` |
| **A preset with SNOPT-like semantics exists** | `filtersqp` ("TR Fletcher-filter restoration inequality-constrained **SQP** method"); plus `ipopt`, `funnel`, `Penalty` |
| **Callbacks corresponding to EMTG's chaperone exist** | `uno_set_solver_callbacks(...)`: each iteration can yield the primal, feasibility, stationarity and complementarity residuals |
| No Hessian needed (EMTG does not provide one) | measured: Uno falls back automatically — `An exact Hessian was not provided, setting an L-LBFGS Hessian instead` |
| Subproblem solvers and linear algebra are complete, **with no license restrictions** | `Uno\deps\`: BQPD, HiGHS, MUMPS, METIS, OpenBLAS, SPRAL; `libhsl.dll` is the license-free dummy build described in the official documentation |
| End-to-end success on the MinGW side | A standalone Uno 2.9.0 program solves the test NLP correctly (`x=(1,0)`, objective `998`, the theoretical optimum, feasibility `0`); `filtersqp` takes effect and the iteration callback fires |
| **MSVC can drive Uno** (the architectural crux) | A probe program compiled with MSVC: `LoadLibraryExW` + `GetProcAddress` all succeed, `uno version 2.9.0`, solver and model creation succeeds |

---

### 2. Toolchain and architecture decisions

#### 2.1 Toolchain facts

**Upstream EMTG is built with MSVC + NMake**, not MinGW:

- compiler MSVC v143 (`cl.exe`) with the `NMake Makefiles` generator
- Boost built against the MSVC ABI (`libboost_filesystem-vc143-*`)
- CMake as shipped with Visual Studio
- Windows SDK 10.0.28000.0 (a portable copy also works)

**Uno's prebuilt Windows package is MinGW format** (`libuno.dll.a`), which the MSVC linker cannot
consume.

#### 2.2 Decision: keep MSVC and call Uno through **dynamic loading**

Because `libuno.dll` exports **undecorated C symbols**, the C ABI is stable across compilers, so
SOUFFLE drives Uno with `LoadLibraryExW` + `GetProcAddress` and **links no Uno library**.

**Benefits**

- Boost vc143, SPICE, SNOPT and the Windows SDK are reused unchanged
- the SNOPT comparison path stays inside the same executable
- no MinGW toolchain in the main build

**Costs**

- `libuno.dll` must be located at run time (given by `SOUFFLE_UNO_ROOT`)
- no compile-time symbol checking; start-up reports an explicit error instead

#### 2.3 Code structure

| File | Role |
|---|---|
| `src\InnerLoop\SouffleApi.h/.cpp` | dynamic loading layer: `LoadLibraryExW` + `GetProcAddress`, 74 C API call stubs |
| `src\InnerLoop\Souffle_interface.h/.cpp` | solver interface: implements `NLP_interface`, mapping EMTG's model/bounds/Jacobian onto Uno |
| `src\InnerLoop\NLP_interface.h` | solver abstract base (previously SNOPT-specific; the virtual destructor and `getInform()` were pulled up) |
| `src\InnerLoop\NLP_solver_factory.h/.cpp` | factory: returns `std::unique_ptr<NLP_interface>` according to `SOUFFLE_NLP_SOLVER` |
| `src\InnerLoop\NLPoptions.h/.cpp` | adds `solver_name`, defaulted from CMake |

#### 2.4 Environment variables

| Variable | Default | Meaning |
|---|---|---|
| `SOUFFLE_NLP_SOLVER` | `Uno` | picks `Uno` or `SNOPT`. **SNOPT is available only when the build enables `SOUFFLE_WITH_SNOPT=ON`**; the default build contains Uno only |
| `SOUFFLE_UNO_ROOT` | baked in at compile time | Uno install directory (containing `bin\`, `deps\`) |
| `SOUFFLE_UNO_PRESET` | `filtersqp` | may also be set to `ipopt` / `funnel` / `Penalty` |

> ⚠️ Values of `SOUFFLE_UNO_PRESET` **must be upper case** (for instance `SILENT` for logger).
> Lower case is silently rejected by Uno and falls back to the default, producing a
> 22 MB / 373,772-line log and making run time I/O-bound.

---

### 3. Verification results

#### 3.1 Solver preset A/B measurements

| preset | Convergence rate | Feasible solution |
|---|---|---|
| `filterSQP` (default) | **111/138 `UNO_SUCCESS`** | yes |
| `ipopt` | 33/263 | no |

The `ipopt` preset's 33/263 convergence with no feasible solution contrasts with `filterSQP`'s
111/138. Uno's `ipopt` preset runs the same algorithm as native IPOPT and performs comparably to it
(see Uno's official documentation), so the gap reflects the algorithm's fit to EMTG rather than
Uno's implementation.

The interior-point method performs well when the sequence is fixed, there are few variables and the
start point is feasible — conditions a broad LTGA search does not provide. SQP with a filter is
instead robust to infeasible start points and matches EMTG's analytic gradients and sparse
structure naturally.

#### 3.2 Like-for-like comparison with SNOPT (2044 window, E-V-V-E-U)

**Every setting must be aligned before the comparison means anything.**

Alignment was verified on a case whose options match the reference run item by item:

| Parameter | Aligned value | Reference |
|---|---|---|
| `maximum_mass` / `allow_initial_mass_to_vary` | 5305.0 / 0 | same |
| `final_mass_constraint_bounds` | 3705.0 5305.0 | same |
| `maximum_electric_propellant` | 1317.6405 | same |
| `initial_impulse_bounds` | 0.0 – √15 | same |
| `num_timesteps` / `number_of_steps` | 40 / 40 | 40 / 40 |
| `snopt_feasibility_tolerance` | 1e-06 | 1e-06 |
| `snopt_optimality_tolerance` | 1e-05 | 1e-05 |
| `integrator_tolerance` | 1e-08 | 1e-08 |

Model size: **518 variables / 206 constraints / 4721 Jacobian nonzeros** (only 86 variables at
3 steps).

| | **SOUFFLE/Uno** | **SNOPT reference** `b2044_EVVEU_0317_03486_t85_c12` |
|---|---|---|
| Initial mass (read from the solution file) | 5305.0 kg | 5305.0 kg |
| TOF | 8.4873 yr | 8.4863 yr |
| **Final mass** | **4598.48 kg** | 4438.15 kg |
| **Electric propellant** | **706.52 kg** | 866.85 kg |
| Launch date | 2044-02-25 | 2044-03-14 |
| MBH budget | 5400 s | longer |

**Conclusion**: with transcription, physics, initial mass, discretization and tolerances aligned,
SOUFFLE/Uno's final mass is **160.3 kg (+3.61%)** higher than the comparable SNOPT reference:
SOUFFLE can find a better solution than SNOPT on this case.

#### 3.3 Uranus interplanetary transfer sequence broad-search measurement (real engineering pipeline)

An **independent** two-stage engineering pipeline and its **own** case definition were used — 2044
window, `t0=16144.5`, `tofs=[150,380,52,1610]`, TOF range [2136.7, 2374.1] — with only the
pipeline's in-memory executable path pointed at SOUFFLE:

| | **SOUFFLE/Uno** | **SNOPT reference** `b2044_EVVEU_0416_01122_t65_c15` |
|---|---|---|
| Final mass | **4150.98 kg** | 4150.07 kg |
| Electric propellant | **1154.02 kg** | 1154.93 kg |
| TOF | 6.500 yr | 6.5 yr bracket |
| Difference | **+0.91 kg (+0.022%)** | — |

Solver statistics: 23 Uno solves, 12 "New global best", worst constraint violation **6.4e-13**.

**The two results almost coincide** — the strongest consistency evidence in this document: a real
pipeline, a real case definition, and no hand-tuned parameter.

The driver does not modify the project it drives; it overrides only the executable path in memory.

---

### 4. The packaged release `Souffle\`

#### 4.1 Double-click = open the graphical interface

**Double-click `run_souffle.bat` → PyEMTG (`EMTG Python Interface`) opens**, the same wxPython GUI
that the SNOPT build of EMTG uses, already wired to this package's SOUFFLE solver.

| I want to… | Do this |
|---|---|
| **Open the graphical interface** | **double-click `run_souffle.bat`** |
| Run a case directly (no GUI) | `run_souffle.bat EVVEU_LTGA.emtgopt` |

Measured (clean environment: system `PATH` only, all `SOUFFLE_*` and `SNOPT_LICENSE` cleared):

```
[SOUFFLE] starting the PyEMTG graphical interface ...
[SOUFFLE] solver : ...\Souffle\bin\EMTGv9.exe
GUI window found after 2 s: hwnd=8914012 title='EMTG Python Interface'
RESULT : GUI opened and closed cleanly
```

#### 4.2 Relocatable design

The GUI needs **absolute paths** (`PyEMTG.options`), yet the package must stay movable as a whole.
The launcher reconciles the two: **it regenerates `PyEMTG.options` from its own location on every
start**. The `.emtgopt` files inside the package use relative paths (`../Universe_EVVEU`,
`../HardwareModels/`, `../results`), which EMTG resolves against its working directory (`bin\`, set
by the launcher).

#### 4.3 Directory structure

```
Souffle\
├── bin\                        EMTGv9.exe (SOUFFLE) + runtime DLLs + default.emtgopt
├── python\                     bundled Python 3.13 + wxPython/numpy/scipy/matplotlib/astropy/spiceypy
├── PyEMTG\                     the PyEMTG graphical interface
├── Uno\bin, Uno\deps\          the Uno solver and its dependencies (SOUFFLE_UNO_ROOT points here)
├── Universe\                   full ephemeris/universe set (new missions can be created in the GUI)
├── Universe_EVVEU\             universe definition for the EVVEU mission
├── HardwareModels\             thruster / power / propulsion libraries
├── EVVEU_LTGA.emtgopt          example case
├── results\                    output directory
├── run_souffle.bat             double-click to run
├── README.md                   user-facing documentation
└── licenses\                   EMTG (NOSA 1.3) / Uno (MIT) / third parties
```

**The default build needs no SNOPT at all**: `SOUFFLE_WITH_SNOPT` defaults to `OFF`, so
`SNOPT_interface.cpp` is not compiled, no SNOPT header is included, and **the executable does not
import `snopt7.dll`** — verified twice, with `dumpbin /imports` and by running to completion with no
SNOPT file anywhere in the directory. Neither a SNOPT installation nor a license is required.
Only `SOUFFLE_WITH_SNOPT=ON` restores the SNOPT comparison path, and only then must you supply
SNOPT and a license yourself.

#### 4.4 Packaging

```powershell
powershell -File package\package_souffle.ps1 -Force
```

Script flow: executable → Uno runtime → full `Universe` → `HardwareModels` → cases (paths
redirected) → bundled Python + PyEMTG → licenses. It also self-checks that every target in
`PyEMTG.options` exists and that no `.emtgopt` inside the package contains a machine-specific
absolute path. `-SkipGUI` produces a slim package without the GUI (direct-run mode only).

---

### 5. Union search measurements

SOUFFLE carries **two solvers** (Uno and Ipopt). They **cannot run in the same process**: each
ships same-named MinGW runtimes built by different toolchains, Windows resolves DLLs by name, and
the second-loaded side fails with `0xc06d007f`. Union search therefore orchestrates both as
**separate processes**.

#### 5.1 Why take the best of three

Across four mission families and 130 cases in total, differences from the SNOPT reference (final
mass, kg):

| Orchestration | Median | Mean |
|---|---|---|
| Uno alone | −4.57 | +34.07 |
| Ipopt alone (cold start) | **+11.22** | **+65.64** |
| Ipopt alone (warm-started from Uno's solution) | +4.36 | +47.67 |
| `max(Uno, cold Ipopt)` | +40.90 | +102.46 |
| `max(Uno, warm Ipopt)` | +42.77 | +81.71 |
| **`max(three)`** | **+76.76** | **+121.41** |

Two conclusions:

- **A cold-started Ipopt beats a warm-started Ipopt**, so a warm start **cannot replace** an
  independent leg;
- the two **land in different basins**, so keeping both is what pays off.

Running Ipopt only "when confident" is therefore counterproductive: that criterion would skip
exactly the solves that bring the real gains.

#### 5.2 One measured run (release package, `--mode parallel`)

```
Completed 1, produced 1 solution, total time 69 s
best-source distribution: {'uno': 1}
final mass 4698.394 kg   wall clock 69.1 s
  uno    ok  wall_s=69.1   SOUFFLE[solve]: preset=filtersqp opt_status=2 sol_status=0 iters=5721
  ipopt  ok  wall_s=36.7   SOUFFLE[ipopt-solve]: status=0 inform=1 iters=781 callbacks=782
```

---

## Appendix: Uno 2.9.0 options

```
# Uno 2.9.0 options
# type: 0=integer 1=double 2=bool 3=string
2	MA57_use_scaling
3	libhsl_path
0	SOC_max_iterations
3	option_file
3	QP_solver
3	BQPD_kmax_heuristic
1	least_square_multiplier_max_norm
1	barrier_damping_factor
1	barrier_push_variable_to_interior_k2
1	barrier_small_infeasibility_factor
1	barrier_push_variable_to_interior_k1
1	barrier_small_direction_factor
1	barrier_regularization_exponent
1	barrier_k_sigma
3	barrier_function
1	l1_constraint_violation_coefficient
1	TR_decrease_factor
1	TR_increase_factor
1	TR_radius
1	primal_regularization_fast_increase_factor
1	primal_regularization_lb
1	switching_objective_exponent
0	quasi_newton_memory_size
1	barrier_initial_parameter
1	switching_delta
3	linear_solver
3	LP_solver
1	barrier_theta_mu
0	threshold_unsuccessful_attempts
2	funnel_require_acceptance_wrt_current_iterate
1	armijo_tolerance
1	TR_radius_reset_threshold
3	inertia_correction_strategy
3	progress_norm
3	globalization_strategy
1	barrier_default_multiplier
3	globalization_mechanism
1	protected_actual_reduction_macheps_coefficient
2	print_subproblem
1	primal_regularization_slow_increase_factor
1	primal_tolerance
3	preset
1	residual_scaling_threshold
1	armijo_decrease_fraction
1	SOC_infeasibility_fraction
1	TR_aggressive_decrease_factor
0	filter_capacity
1	dual_regularization_fraction
1	primal_regularization_decrease_factor
1	dual_tolerance
1	TR_activity_tolerance
3	residual_norm
1	loose_dual_tolerance
1	funnel_fact
1	loose_primal_tolerance
1	function_scaling_threshold
1	filter_beta
2	switch_to_optimality_requires_linearized_feasibility
0	max_iterations
1	time_limit
0	loose_tolerance_iteration_threshold
1	filter_fact
3	constraint_relaxation_strategy
2	protect_actual_reduction_against_roundoff
1	funnel_kappa
3	logger
2	use_function_scaling
1	diverging_iterate_threshold
1	barrier_k_epsilon
1	barrier_k_mu
1	unbounded_objective_threshold
0	max_number_filter_resets
1	gamma_alpha
1	switching_infeasibility_exponent
1	sufficient_infeasibility_decrease_ratio
3	filter_type
3	inequality_handling_method
1	filter_gamma
1	TR_min_radius
1	filter_ubd
1	filter_sufficient_infeasibility_decrease_factor
0	filter_reset_iteration_threshold
1	funnel_beta
1	funnel_gamma
2	print_solution
1	funnel_ubd
3	hessian_model
0	funnel_update_strategy
1	LS_backtracking_ratio
2	LS_scale_duals_with_step_length
1	quasi_newton_delta_lower_bound
2	write_solution_to_file
1	quasi_newton_delta_upper_bound
0	LBFGS_max_skips_before_reset
1	regularization_increase_factor
1	LSR1_pivot_max_magnitude
1	barrier_tau_min
1	regularization_failure_threshold
1	barrier_update_fraction
1	primal_regularization_initial_factor
# total 100 options
```

---

## License

This project is released under the NASA Open Source Agreement (NOSA 1.3), the same terms as
upstream EMTG. The full terms are in [LICENSE](LICENSE) and
[EMTG_NOSA_License.pdf](EMTG_NOSA_License.pdf).

Uno is released under the MIT license and Ipopt under EPL 1.0; the license texts of both are
distributed with the release package.

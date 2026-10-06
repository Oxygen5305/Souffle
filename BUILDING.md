# Building SOUFFLE

SOUFFLE builds with **MSVC + NMake**, the same toolchain as the original EMTG.
A default build requires **no SNOPT installation and no license** — SNOPT support is
compiled out unless you explicitly turn it on.

---

## 1. Prerequisites

| Component | Notes |
|---|---|
| Visual Studio 2022 (MSVC v143) | `cl.exe` — the reference build used `14.40.33807` |
| CMake | the one bundled with VS works: `<VS>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| Windows SDK | reference build used `10.0.28000.0` (a portable copy is fine) |
| **Boost** | ≥ 1.60, MSVC ABI (`libboost_filesystem-vc143-*`). Not vendored — download separately |
| **Uno** ≥ 2.9.0 | Windows release (MinGW build). See below |
| SPICE kernels | `de440s.bsp`, `naif0012.tls`, `pck00010.tpc` — see §3 |

> Uno's MinGW runtime DLLs are needed at run time, not at build time.

---

## 2. Getting Uno

Download the Windows release from <https://github.com/cvanaret/Uno/releases> and unpack it
so that the directory contains:

```
Uno/
├── include/uno/Uno_C_API.h
├── bin/libuno.dll
└── deps/                       (MUMPS, OpenBLAS, BQPD, HiGHS, SPRAL, ...)
```

SOUFFLE **does not link** against Uno: MSVC cannot link Uno's MinGW import library, so the
`souffle` interface loads `libuno.dll` at run time with `LoadLibraryExW` + `GetProcAddress`
(see `src/InnerLoop/SouffleApi.cpp`). The build only needs `Uno_C_API.h` plus the path
recorded for the runtime lookup.

---

## 3. Getting SPICE kernels

Ephemeris kernels are large and are **not** in this repository. Fetch them from NASA NAIF:

| File | Source |
|---|---|
| `de440s.bsp` | <https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/planets/> |
| `naif0012.tls` | <https://naif.jpl.nasa.gov/pub/naif/generic_kernels/lsk/> |
| `pck00010.tpc` | <https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/> |

Place them under `Universe/ephemeris_files/` (and `Universe_EVVEU/ephemeris_files/` if you
want to run the bundled EVVEU case).

---

## 4. Configure and build

```powershell
$vs     = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$msvc   = "$vs\VC\Tools\MSVC\<version>"
$cmake  = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$sdk    = 'C:\Program Files (x86)\Windows Kits\10'

$env:PATH    = "$msvc\bin\Hostx64\x64;$env:PATH"
$env:INCLUDE = "$msvc\include;$sdk\Include\<ver>\ucrt;$sdk\Include\<ver>\um;$sdk\Include\<ver>\shared"
$env:LIB     = "$msvc\lib\x64;$sdk\Lib\<ver>\ucrt\x64;$sdk\Lib\<ver>\um\x64"

& $cmake -S . -B build -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release `
    -DSOUFFLE_WITH_UNO=ON `
    -DSOUFFLE_UNO_ROOT=<path to Uno> `
    -DSOUFFLE_DEFAULT_SOLVER=Uno `
    -DBOOST_ROOT=<path to Boost> `
    -DBoost_INCLUDE_DIR=<path to Boost> `
    -DBoost_LIBRARY_DIR_RELEASE=<path to Boost>\stage\lib `
    -DBoost_LIBRARY_DIR_DEBUG=<path to Boost>\stage\lib

& $cmake --build build
```

The executable lands in `build\src\SOUFFLE.exe`.

### CMake options

| Option | Default | Meaning |
|---|---|---|
| `SOUFFLE_WITH_UNO` | `ON` | compile the Uno solver interface |
| `SOUFFLE_UNO_ROOT` | — | Uno install dir (`include/uno`, `bin`, `deps`) |
| `SOUFFLE_DEFAULT_SOLVER` | `Uno` | solver used when `SOUFFLE_NLP_SOLVER` is unset |
| **`SOUFFLE_WITH_SNOPT`** | **`OFF`** | compile the SNOPT interface. **Off by default**: no SNOPT directory is required, no SNOPT header is included, and the executable does not import `snopt7.dll` |

With `SOUFFLE_WITH_SNOPT=OFF` (the default) the build needs nothing from SNOPT. Turning it
`ON` restores EMTG's original behaviour, but then you must supply `-DSNOPTDIR_OVRD=<snopt>`
with a valid SNOPT installation.

---

## 5. Running

```bat
set SOUFFLE_UNO_ROOT=<path to Uno>
set SOUFFLE_NLP_SOLVER=Uno
SOUFFLE.exe my_case.emtgopt
```

| Environment variable | Default | Meaning |
|---|---|---|
| `SOUFFLE_NLP_SOLVER` | `Uno` | `Uno`, `IPOPT`, or `SNOPT` / `WORHP` (the latter two only if compiled in) |
| `SOUFFLE_UNO_ROOT` | baked in at configure time | where `libuno.dll` lives |
| `SOUFFLE_IPOPT_LIB` | — | directory holding `ipopt-3.dll`; only `united/` needs it |
| `SOUFFLE_UNO_PRESET` | `filtersqp` | **Uno's own** preset: `filtersqp`, `ipopt`, `funnel`, `Penalty`. Not the Ipopt solver — use `SOUFFLE_NLP_SOLVER=IPOPT` for that |

> Uno option tokens are case-sensitive. Lowercase values are rejected silently and fall
> back to Uno's defaults.

---

## 5b. The graphical interface (optional)

The build produces a command-line executable only. PyEMTG, the GUI, is Python, and the source
tree does not vendor an interpreter — install one and the packages it imports:

```bat
python -m pip install -r requirements.txt
```

`requirements.txt` lists wxPython, numpy, scipy, matplotlib, astropy and spiceypy. Then point
the GUI at the executable you just built by editing `PyEMTG\\PyEMTG.options`:

```
EMTG_path <path to SOUFFLE.exe>
default_universe_path <path to your Universe folder>
```

The packaged release skips all of this: it ships an interpreter with those packages already
installed, and its launcher regenerates `PyEMTG.options` on every start.

---

---

## 5c. Union search (optional)

One binary holds both solvers, but they must run as **separate processes**: Uno and Ipopt ship
same-named MinGW runtimes built from different toolchains, and Windows resolves DLLs by name, so
whichever loads first wins and the other dies with `0xc06d007f`. `united/united.py` runs them
side by side and keeps the better answer:

```bat
set SOUFFLE_UNO_ROOT=<path to Uno>
set SOUFFLE_IPOPT_LIB=<directory holding ipopt-3.dll>
python united\\united.py --case my_case.emtgopt --out run\\ --mode both
```

`--mode parallel` runs Uno and a cold Ipopt; `--mode both` (the default) adds a third solve that
warm-starts Ipopt from Uno's answer. Cold and warm-started Ipopt land in different basins, so
taking all three beats the best of the first two by roughly 19 kg on average.

Wall clock is `max(MBH budget, per-solve limit)` — MBH cannot interrupt a solve in progress, so
the per-solve limit is the real knob.

---

## 6. Verifying the SNOPT-free build

Two independent checks:

```powershell
# 1. the import table must not mention snopt7
dumpbin /imports build\src\SOUFFLE.exe | findstr /I snopt

# 2. it must run with snopt7.dll absent and no license
#    (rename or remove snopt7.dll next to the exe, then run a case)
```

`dumpbin` should print nothing for `snopt`, and the executable should solve normally.

---

## 7. Pitfalls

| Symptom | Cause / fix |
|---|---|
| `0xC0000139`, no output at all | a stale MinGW runtime earlier in `PATH`. Put Uno's `bin` and `deps` **first** |
| `0xC0000135` at startup | a hard-imported DLL is missing. With `SOUFFLE_WITH_SNOPT=OFF` this should not be `snopt7.dll` |
| `failed to load libuno.dll` | `SOUFFLE_UNO_ROOT` is wrong or unset |
| Huge log files | `SOUFFLE_UNO_PRESET`-related Uno options are case-sensitive; use uppercase |
| Solver returns after 1 iteration | a termination callback is registered with Uno. SOUFFLE deliberately passes `nullptr` — see `docs/DESIGN.md` §5.1 |

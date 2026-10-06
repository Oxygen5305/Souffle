# run_evveu

Example `.emtgopt` cases for an Earth–Venus–Venus–Earth–Uranus low-thrust rendezvous.

| Prefix | Purpose |
|---|---|
| `MINI_*` | small, fast cases used while developing the solver interface |
| `EVVEU_LTGA_L1*` | progressively longer searches on the full trajectory |
| `EVVEU_LTGA_run` | the standard case |
| `RETRY_t20`, `RETRY_t40*` | fixed `num_timesteps` runs, for comparisons that need matched discretisation |

## Before running

Paths inside these cases are **relative** (`../Universe_EVVEU`, `../HardwareModels/`,
`../results`), so they resolve against the working directory you launch from. Run with
`run_evveu/` as the working directory and they line up with the repository layout:

```bat
cd run_evveu
set SOUFFLE_UNO_ROOT=<path to Uno>
set SOUFFLE_NLP_SOLVER=Uno
..\build\src\EMTGv9.exe EVVEU_LTGA_L1.emtgopt
```

Two prerequisites:

1. **Ephemeris kernels.** `../Universe_EVVEU/ephemeris_files/` must contain `de440s.bsp`,
   `naif0012.tls` and `pck00010.tpc`. They are not in this repository; see
   [`../Universe/README.md`](../Universe/README.md) for download links.
2. **Hardware models.** Already present in `../HardwareModels/`.

If you would rather run from elsewhere, set `universe_folder`, `HardwarePath`,
`forced_working_directory` and `pyemtg_path` in the case to absolute paths for your machine.

## Choosing a case

`MINI_*` cases finish in minutes. `EVVEU_LTGA_L1*` run the full trajectory and take much
longer. `RETRY_t40_aligned.emtgopt` matches the discretisation and tolerances used for the
solver comparison in [`../docs/DEVELOPMENT.md`](../docs/DEVELOPMENT.md).

## Output

Written under `../results/`, as set by `forced_working_directory` in each case.

---

## Running these with union search

Union search runs **two solvers** (Uno and Ipopt) as separate processes and keeps the better answer:

The cases below work with a plain `SOUFFLE.exe` run, but they are also what `united/` is for.
Union search runs Uno and Ipopt as separate processes and keeps the better answer — cold and
warm-started Ipopt settle in different basins, so taking all three beats either solver alone by
roughly 19 kg on average.

```bat
set SOUFFLE_UNO_ROOT=<Uno install>
set SOUFFLE_IPOPT_LIB=<directory holding ipopt-3.dll>
python ..\\united\\united.py --case EVVEU_LTGA_L1.emtgopt --out ..\\united_out\\ --mode both
```

`--mode parallel` skips the warm-start leg and is roughly twice as fast. Keep in mind that wall
clock is `max(MBH budget, per-solve limit)`; lower `snopt_max_run_time` if you want a quicker
answer rather than a longer search.

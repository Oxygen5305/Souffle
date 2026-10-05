# SOUFFLE patches applied in this checkout

This tree (`Souffle_Cheese`) is the **editable** copy of SOUFFLE used for the ESFO_Uranus
project. `G:\Py\DeepSeekHarness\Souffle` (the shipped install) and `SOUFFLE_Git` are
**read-only** and must not be modified.

All changes live in **one file** — `src/InnerLoop/Souffle_interface.cpp` — and every hunk is
tagged `SOUFFLE FIX (Dn)`. Patches are described with the identifier used throughout the
analysis so the tags and the write-ups can be cross-referenced.

Build: `build_cheese.bat` (see "Building" below). The previous binary is kept as
`bin/EMTGv9.exe.bak_original`.

---

## D1 — objective gradient was identically zero for `objective_type = 1` (MinTOF)

**Symptom.** The MinTOF case never reached the Δv cap and settled ~77 days above the SNOPT
answer. 1234 of 1482 logged solves reported `opt_status=0 sol_status=1`
(SUCCESS + FEASIBLE_KKT_POINT) after a **median of 3 iterations / 0.064 s**, with J frozen.

**Cause.** EMTG registers the MinTOF objective's only non-zero derivatives in the **linear**
Jacobian `A` (`MinimizeTimeObjective.cpp` fills `iAfun=0 / jAvar=<time column> / A=<scale>`);
row-0 entries in `G` are only created for `objective_type == 0`
(`TwoPointShootingLowThrustPhase.cpp`). SNOPT is handed `A` through `setA()`, but
`Souffle_interface.cpp` never read `A` at all — Uno has no linear-part API. With ∇f ≡ 0 every
feasible point is a KKT point, so Uno "converged" wherever MBH left it.

**Fix.** Merge the objective rows of `A` into the gradient in `objective_gradient_callback`.

**Result (measured).** MinTOF 2044, seed = the 7-year tier-2 solution, 1800 s MBH:

| | ToF | Δv | J | iterations per solve |
|---|---|---|---|---|
| before | 2327 d | 27.834 | 1.3596 | median **3** (bogus SUCCESS) |
| **after** | **2248 d** | 27.826 | **1.2982** | **201–825** |
| SNOPT reference | 2250 d | 28.0 | — | — |

**MinTOF now beats the SNOPT baseline.** Report: `ESFO_Uranus/results/mintof_d1fix_2044.md`.

**Note.** Only objective-row `A` entries exist anywhere in EMTG today, so this simple merge is
sufficient. If constraint-row `A` entries are ever added, they must also be merged into the
model Jacobian — and `(row,col)` pairs must be de-duplicated, because EMTG de-duplicates only
*within* `G` (`EMTG_solver_utilities.cpp`).

---

## D4 — Uno was asked to stop far further from stationarity than SNOPT

**Cause.** Three independent loosenings, all in the solver options:

1. the requested `dual_tolerance` was **clamped up to 1e-4** while SNOPT gets 1e-5;
2. the `filtersqp` preset sets `residual_norm = L2` while SNOPT uses max-norm (≈√207 ≈ 14×
   looser for a uniformly spread residual), and EMTG never overrode the norm;
3. Uno divides the stationarity residual by a **multiplier-norm-relative** factor
   (`residual_scaling_threshold * total_size`), a mechanism SNOPT has no analogue for.

Uno's `dual_tolerance` governs stationarity **and** complementarity, so all three act on the
same test.

**Fix.** Honour the requested tolerance (`dual_tolerance = optimality`), restore
`residual_norm = INF`, and push `residual_scaling_threshold` to 1e100 (effectively disabling
the relative test).

**Cost (expected).** Solves take more iterations and hit the time limit more often; raise the
per-solve / MBH budget when measuring. Symptoms move from a false SUCCESS to an honest
`TIME_LIMIT`, which is the precondition for a fair comparison with SNOPT.

**Result (measured, 1800 s MBH, shape-mode seeds from the tier-1 pool):**

| band | before D4 | after D4 | SNOPT (5305 kg) | gap before → after |
|---|---|---|---|---|
| 10 yr | 4767.98 | **4776.29** | 4785.99 | −18.0 → **−9.7 kg** |
| 7 yr | 4323.33 | **4324.66** | 4328.46 | −5.1 → −3.8 kg |
| 6.5 yr | 4151.58 | **4152.23** | 4151.58 | 0 → **+0.65 kg** (now ahead) |

So aligning the tolerance semantics recovered about half of the 10-year gap. The remaining
~9.7 kg is attributed to D2/D5 (83 % of mass-objective solves abandoned inside Uno) rather
than to the stopping test.

Reports: `ESFO_Uranus/results/d4_{10y,7y,6p5y}.md`.

---

## D12 — `logger` was hard-coded to SILENT, hiding why Uno abandons solves

**Cause.** 83 % of the mass-objective solves ended in `opt_status=4`
(`UNO_ALGORITHMIC_ERROR`), which Uno sets in exactly one place — the `catch (std::exception&)`
in `uno_solve` — i.e. **Uno threw and abandoned the whole NLP**. The exception text is printed
only when the logger level is `INFO`, which this interface disabled (a 22 MB log per phase).

**Fix.** Keep `SILENT` as the default, but let the environment override it:

```bat
set SOUFFLE_LOG_LEVEL=INFO     :: diagnostic run only; very verbose
```

---

## D5 — solver-option hooks for the trust-region collapse (diagnostic, off by default)

**Symptom (measured with `SOUFFLE_LOG_LEVEL=INFO`).** In the 10-year case, 83 % of the
mass-objective solves end in `UNO_ALGORITHMIC_ERROR`. The log shows exactly why:

```
1371  1  1.00e-04  OPT  ...  v (f-type)
1372  1  1.00e-04  FEAS ...  v (f-type)
1373  1  1.00e-04  FEAS ...  x (current)
-     -  -         -    -    -    -    Small radius
Optimization status:  Algorithmic error
  | Primal infeasibility:  9.263162e-12     (feasible)
  | Stationarity residual: 0.001722338      (asked for 1e-5)
  Solution status:        Suboptimal point
```

The trust-region radius decays and then Uno gives up — a **subproblem conditioning**
failure, not a time limit. `filtersqp` receives EMTG's raw km/kg/s model with no scaling
(Uno's `use_function_scaling` only takes effect on the interior-point path), which is the
prime suspect.

**What was added.** Environment-settable hooks so these knobs can be swept without rebuilding:

| variable | Uno option | default |
|---|---|---|
| `SOUFFLE_TR_RADIUS` | `TR_radius` | 1.0 |
| `SOUFFLE_TR_MIN_RADIUS` | `TR_min_radius` | 1e-12 |
| `SOUFFLE_L1_COEFF` | `l1_constraint_violation_coefficient` | preset |
| `SOUFFLE_PRIMAL_TOL` | `primal_tolerance` | 1e-6 |
| `SOUFFLE_MAX_ITER` | `max_iterations` | 2 x major limit |

**Sweep results (10-year band, 600 s MBH, so compare only against the 600 s baseline):**

| setting | m_f | note |
|---|---|---|
| `TR_radius = 10` | 4761.44 kg | no gain |
| `TR_min_radius = 1e-16` | 4760.36 kg | no gain (slightly worse) |
| preset `funnelsqp` | 4755.22 kg | worse than filtersqp |
| preset `filterslp` | no solution | unusable |

**Conclusion: neither the trust-region knobs nor the alternative presets help** — the
remaining gap is not addressed from these directions. The hooks are kept because they make
such experiments a one-line change rather than a rebuild, but nothing is enabled by default.

### D5b — constraint-row scaling (attempted, does not work yet; default OFF)

The mechanism above points at conditioning, so an interface-level row scaling was implemented:
each constraint row is normalised by `max(|c_i(x0)|, |lower_i|, |upper_i|)` with the factor
applied consistently to the bounds, the constraint values and that row's Jacobian entries
(feasible set unchanged). Rows whose bounds look like EMTG's "unbounded" sentinels (>1e10) are
skipped, and the factor is clamped to [1e-8, 1e8].

**It does not work**: with scaling enabled EMTG dies during/just after the layout probe
(`no XFfile produced`, ~4-5 s) on the 10-year case, both in the first implementation and after
the sentinel/clamp fix. Scaling is therefore **off by default**; enable for debugging with
`SOUFFLE_CONSTRAINT_SCALING=1`. Next step would be to log the per-row factors and the first
Uno callback values to find the mismatch.

### D6 — restoring the optimality-phase gate (attempted, does not work; default unchanged)

The INFO log of a failing 10-year solve shows the algorithm oscillating `OPT <-> FEAS` until the
trust region collapses, i.e. it never returns to optimising, so the switch-back gate was made
configurable:

```bat
set SOUFFLE_SWITCH_GATE=0     :: switch_to_optimality_requires_linearized_feasibility = false
```

**It does not work either**: with the gate disabled EMTG again dies within ~4 s of the layout
probe, which suggests Uno rejects the option in this configuration. The hook is kept (inert
unless the variable is set) so the experiment can be repeated against a different Uno build.

**Summary of what actually moves the needle:** only **D1** (a genuine bug: a missing objective
gradient) and **D4** (tolerance semantics). Everything else tried — alternative presets,
trust-region knobs, constraint scaling, the restoration gate — was neutral or fatal.

### D9 — trial-point incumbent pooling (attempted five times, does NOT work; default OFF)

**Why it was worth trying.** SNOPT's chaperone runs on every `needG` evaluation
(`SNOPT_interface.cpp:423`), so the SNOPT build searches with every line-search trial point
available as an incumbent. Under Uno the accepted-iterate callback fires about once per solve:
on a recorded 2198-iteration / 2857-evaluation 10-year solve it banked **exactly one point**.
Uno's C API offers no trial-point hook (`uno_set_solver_callbacks` exposes only
`notify_acceptable_iterate` and `termination`), so banking has to happen inside the evaluation
callbacks. `Souffle_interface::bank_candidate()` does that, armed from `objective_callback` and
`objective_gradient_callback`, gated by `SOUFFLE_TRIAL_INCUMBENTS=1`.

**Five variants were tried, all fatal or neutral:**

| variant | result |
|---|---|
| call `update_chaperone()` from `ensure_evaluated` | died in the layout probe (~4 s) |
| same, but only when a gradient is cached | died in the layout probe |
| call it from the gradient/Jacobian callbacks instead | died in the layout probe |
| `bank_candidate()` with normalised feasibility + full bookkeeping update | **passed the probe**, then MBH failed at 80 s with no feasible solution |
| `bank_candidate()` with strict feasibility, full bookkeeping | died in the layout probe |
| `bank_candidate()` with strict feasibility, point-only update | died in the layout probe |

**Root cause of the first three:** `update_chaperone()` calls `myProblem->check_feasibility()`,
which **re-enters the problem evaluation**. Uno splits one EMTG evaluation into four separate
callbacks, so re-entering from inside one corrupts the evaluation cache. SNOPT can do this
because its user function is a single synchronous call.

**Root cause of the last three is not fully established.** The strict feasibility test
(`worst_violation <= 0` against bounds that already carry the tolerance) should simply never
fire, yet enabling the path kills EMTG within 4 s; the one variant that survived the probe
wrote EMTG's feasibility bookkeeping variables and then broke MBH. The honest reading is that
**Uno's callback architecture does not provide a safe place to admit trial points**, and this
asymmetry with SNOPT is architectural rather than a fixable defect.

**Correction (measured later, when the hybrid interface was built).** Part of the "dies within
4 s" signature above was not caused by D9 at all. Two separate faults produced the same
`opt_status=3 / iters=0 / inform=13` picture, and neither was the trial-point code:

1. **Stale objects.** `bin\EMTGv9_hybrid.exe` and `bin\EMTGv9.exe` were at one point linked from
   objects compiled from an older `Souffle_interface.cpp` that still had a per-solve `cerr`
   debug line and `constraint_scale` enabled. An ASCII scan of the image proved it (`ZZP X0u`
   present in the binary, absent from the source). **Any rebuild in this tree must start from a
   deleted build directory** — incremental nmake did not pick up every touched header.
2. **`X0` forwarding.** `Hybrid_interface::run_NLP` originally called `unscaleX0()` while
   `X0_scaled` was still all zeros, so every child solver started at the *variable lower bounds*
   instead of MBH's point. The result is an instantaneous `UNO_EVALUATION_ERROR` — the same
   signature. It now forwards MBH's start point to the child explicitly.

So the D9 conclusion stands (trial-point admission has no safe home inside Uno's callback
architecture), but the specific crash timings recorded in the table above should be read with
those two faults in mind.

The function is kept (inert unless `SOUFFLE_TRIAL_INCUMBENTS=1`) because the mechanism it
targets is real and a future Uno release with a trial-point callback would make it usable.

---

## Retired — Hybrid (Uno and Ipopt in one process)

`Hybrid_interface.{h,cpp}` ran both solvers inside a single `run_NLP` call, with
`should_refine()` deciding per solve whether Ipopt was worth running. **The code is gone**, and
`SOUFFLE_NLP_SOLVER=HYBRID` now reports that and points at `united/`.

It cannot work here. The two solvers ship same-named mingw runtimes built from different
toolchains (`libwinpthread-1.dll` 326,509 B in `Uno\deps` against 63,150 B in the conda
`Library\bin`, plus four more), and Windows resolves DLLs by name, so whichever loads first wins
and the other dies. Measured: `SOUFFLE_NLP_SOLVER=HYBRID` killed the process at Ipopt's first
iteration with `0xc06d007f` (ERROR_DELAY_LOAD_FAILED), while a pure-Ipopt process was fine.
Reordering PATH only moves the failure to the other side.

Union search now runs the two solvers as separate processes; see `united/README.md`.

### Findings that outlived the code

| finding | where it still matters |
|---|---|
| `inform` cannot carry Uno's convergence verdict: `opt_status` in {SUCCESS, ITERATION_LIMIT, TIME_LIMIT, USER_TERMINATION} all map to `inform = 1`, so a time-limited solve that never approached stationarity looks converged. Only `solution_status == UNO_FEASIBLE_KKT_POINT` means converged | `Souffle_interface::get_solution_status()` |
| Ipopt's default `bound_push=1e-2` (scaled units) moves every variable 1% toward its bounds and discards a feasible-ish start; `warm_start_init_point=yes` with `bound_push=bound_frac=1e-8` preserves it | the `IPOPT_WARM_START` / `IPOPT_BOUND_PUSH` hooks |
| On these models Ipopt's incumbent pool is far richer than Uno's (1919 incumbents over 209 MINI solves, median 3, max 116) but only 3/209 were usable from a cold start — 70.9% of callbacks burned in restoration | why a warm-start path is worth having at all |

### One conclusion did not survive

The hybrid's premise was *never run Ipopt unconditionally; always warm-start it*. Measured later
on four families (130 tasks) against SNOPT:

| structure | median | mean |
|---|---|---|
| Uno alone | −4.57 | +34.07 |
| Ipopt alone, cold from the original geometry | **+11.22** | **+65.64** |
| Ipopt alone, warm-started from Uno's geometry | +4.36 | +47.67 |
| `max(Uno, cold Ipopt)` | +40.90 | +102.46 |
| `max(Uno, warm Ipopt)` | +42.77 | +81.71 |
| `max(all three)` | **+76.76** | **+121.41** |

Cold Ipopt beats warm-started Ipopt, and the two land in **different basins** — so taking the
max of both beats either alone by ~19 kg on average. Hence `united/united.py` runs both instead of
gating on a predicate: the predicate would have skipped exactly the runs that pay off.

---


---

## Building

```bat
build_cheese.bat configure   :: CMake configure only (validates the toolchain)
build_cheese.bat build       :: configure + nmake
```

The script exists because the toolchain is non-obvious on this machine:

* **Windows SDK is not in the registry path.** `D:\Windows Kits\10\` holds only the installer;
  the SDK actually lives in `G:\Py\DeepSeekHarness\winsdk` (10.0.28000.0). `vcvars64.bat`
  therefore leaves `WindowsSDKVersion` empty and `rc.exe`/`mt.exe` are not found. The script
  sets `WindowsSdkDir` / `WindowsSDKVersion` / `UniversalCRTSdkDir` and, crucially, prepends
  the real SDK to `INCLUDE`/`LIB` — without that the link fails with
  `LNK1104: cannot open kernel32.lib`.
* **Uno root** is `G:/Py/DeepSeekHarness/Uno` (has `include/uno`, `bin/libuno.dll`, `deps`,
  `lib/libuno.dll.a`) — *not* `Souffle\Uno`, which only has `bin`/`deps`.
* **Boost/GSL/CSpice** are resolved by `EMTG-Config.cmake`, which points at
  `G:/Py/DeepSeekHarness/EMTG/depend`.
* The build tree is `build_cheese/`; the stale `build/` copied from `SOUFFLE_Git` is left
  untouched because its `CMakeCache.txt` still points at that other tree.

---

## Not (yet) changed

Ranked by expected impact in the analysis; listed so future work does not have to re-derive
them.

| id | issue | why it is not patched yet |
|---|---|---|
| D2 | Uno treats an evaluation/algorithmic failure as fatal (abandons the NLP) while SNOPT's `*Status = -1` makes it recoverable | needs the D12 diagnostics first: the remedy differs depending on whether the exception is an evaluation error or a subproblem/trust-region failure |
| D3 | MBH accepts any *feasible* point when the chaperone is on; `inform` maps `opt_status∈{0,1,2,5}` to success regardless of `solution_status` | changes the accepted-solution stream for **both** solvers, so all A/B results would have to be regenerated |
| D5 | `filtersqp` gets **no** problem scaling (Uno's `use_function_scaling` only takes effect for the interior-point path), while SNOPT scales automatically | prime suspect for D2's 83 % failure rate; setting the flag alone does nothing under `filtersqp` |
| D6 | in the feasibility-restoration phase the objective multiplier is 0 and the switch-back test is hard to pass | disabling the gate trades feasibility for objective; needs its own A/B |
| D7 | neither solver gets an exact Hessian; Uno falls back to L-BFGS(6) | supplying an exact Hessian is a research project |
| D9 | Uno's incumbent pool is smaller than SNOPT's (accepted iterates only, no line-search trial points) | no fix needed beyond D3 gating |
| D10 | `FilamentFinder` mode leaves the critical-inequality Jacobian rows at zero | that solver mode is not used by this project (`NLP_solver_mode 1`) |
| D11 | single-slot evaluation cache can force redundant evaluations | performance only; measure with the bound-but-unused evaluation counters first |

Also worth knowing: `SOUFFLE_Git/docs/DEVELOPMENT.md` §7 documents the Uno status codes
incorrectly. The real v2.9.0 enum is
`0 SUCCESS, 1 ITERATION_LIMIT, 2 TIME_LIMIT, 3 EVALUATION_ERROR, 4 ALGORITHMIC_ERROR,
5 USER_TERMINATION` — so the earlier "111/138 UNO_SUCCESS" A/B figure counted time-limit
terminations as successes.

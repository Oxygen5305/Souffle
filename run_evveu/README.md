# run_evveu

Example `.emtgopt` cases for an Earth–Venus–Venus–Earth–Uranus low-thrust rendezvous.

| Case | Use it for |
|---|---|
| `EVVEU_LTGA_run.emtgopt` | the standard example: 40 steps, 1800 s of MBH |
| `MINI_LTGA_smoke.emtgopt` | a quick check that the install works — 2 steps, finishes in well under a minute |
| `RETRY_t40_aligned.emtgopt` | the matched-discretisation baseline (40 steps, SNOPT's tolerances) used for the comparison in [`../docs/DEVELOPMENT.md`](../docs/DEVELOPMENT.md) |

## Before running

Paths inside these cases are **relative** (`../Universe_EVVEU`, `../HardwareModels/`,
`../results`), so they resolve against the working directory you launch from. Run with
`run_evveu/` as the working directory and they line up with the repository layout:

```bat
cd run_evveu
set SOUFFLE_UNO_ROOT=<path to Uno>
set SOUFFLE_NLP_SOLVER=Uno
..\\build\\src\\SOUFFLE.exe EVVEU_LTGA_run.emtgopt
```

Two prerequisites:

1. **Ephemeris kernels.** `../Universe_EVVEU/ephemeris_files/` must contain `de440s.bsp`,
   `naif0012.tls` and `pck00010.tpc`. They are not in this repository; see
   [`../Universe/README.md`](../Universe/README.md) for download links.
2. **Hardware models.** Already present in `../HardwareModels/`.

If you would rather run from elsewhere, set `universe_folder`, `HardwarePath`,
`forced_working_directory` and `pyemtg_path` in the case to absolute paths for your machine.

## Output

Written under `../results/`, as set by `forced_working_directory` in each case.

## Running these with union search

Union search runs **two solvers** (Uno and Ipopt) as separate processes and keeps the better
answer — cold and warm-started Ipopt settle in different basins, so taking all three beats
either solver alone by roughly 19 kg on average.

```bat
set SOUFFLE_UNO_ROOT=<Uno install>
set SOUFFLE_IPOPT_LIB=<directory holding ipopt-3.dll>
python ..\\united\\united.py --case EVVEU_LTGA_run.emtgopt --out ..\\united_out\\ --mode both
```

`--mode parallel` skips the warm-start leg and is roughly twice as fast. Keep in mind that wall
clock is `max(MBH budget, per-solve limit)`; lower `snopt_max_run_time` if you want a quicker
answer rather than a longer search.

### 用时参考

| 编排 | 单任务墙钟 | 说明 |
|---|---|---|
| Uno 单独 | 62 s | 上面那个 `--mode` 之外的默认跑法 |
| Ipopt 单独（冷） | 33 s | 最快，但质量最低 |
| `parallel` | 62 s | 与只跑 Uno 同价：Ipopt 藏在 Uno 后面 |
| `both`（默认） | 100 s | 多串一段热启动，多花约 38 s |

#### 比出厂设置快得多

出厂算例是 `MBH_max_run_time 900` + `snopt_max_run_time 60`，即一次运行最长 **900 s**；
联合寻优用 60 s 预算、15 s / 8 s 单次上限，`parallel` **62 s**、`both` **100 s**。
换过来等于是**提速约 9–14 倍**，质量还更高。

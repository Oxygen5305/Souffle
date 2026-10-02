# Drive ESFO_Uranus's tier-2 (EMTG MGALT) pipeline with the Souffle build instead of the
# original SNOPT EMTG, WITHOUT modifying the ESFO_Uranus project.
#
# How: import its `config` module and override EMTG_BIN (and the work root) in memory.
# `emtg_pipeline` reads `C.EMTG_BIN` at call time, so the override takes effect.
#
# Usage:
#   python tier2_driver.py [mbh_time_seconds] [--probe-only]
import os
import sys

ESFO = r"G:\Py\DeepSeekHarness\ESFO_Uranus"
CODE = os.path.join(ESFO, "code")
sys.path.insert(0, CODE)

SOUFFLE = r"G:\Py\DeepSeekHarness\Souffle"
WORK_ROOT = r"G:\Py\DeepSeekHarness\Souffle_Cheese\tier2_work"

# --- make the child EMTG process use Souffle + Uno --------------------------------
os.environ["SOUFFLE_NLP_SOLVER"] = "Uno"
os.environ["SOUFFLE_UNO_ROOT"] = os.path.join(SOUFFLE, "Uno")
os.environ["SOUFFLE_UNO_PRESET"] = os.environ.get("SOUFFLE_UNO_PRESET", "filtersqp")
os.environ["PATH"] = ";".join([
    os.path.join(SOUFFLE, "bin"),
    os.path.join(SOUFFLE, "Uno", "bin"),
    os.path.join(SOUFFLE, "Uno", "deps"),
    os.environ.get("PATH", ""),
])

import config as C                       # noqa: E402
C.EMTG_BIN = os.path.join(SOUFFLE, "bin", "EMTGv9.exe")
C.EMTG_WORK_ROOT = WORK_ROOT
print("[driver] EMTG_BIN  =", C.EMTG_BIN)
print("[driver] WORK_ROOT =", WORK_ROOT)
print("[driver] solver    =", os.environ["SOUFFLE_NLP_SOLVER"],
      "preset =", os.environ["SOUFFLE_UNO_PRESET"])

import emtg_pipeline as P                # noqa: E402

# --- the case: 2044 window, EVVEU, 6.5 yr class (same as tier2_plan_2044_window.json)
CASE = dict(
    seq="E-V-V-E-U",
    t0_mjd2000=16144.5,                  # 2044-03-15
    tofs=[150.0, 380.0, 52.0, 1610.0],
    tof_class_yr=6.5,
    tof_lo_days=2136.7125,
    tof_hi_days=2374.125,
    tag="t2_2044_EVVEU_tof065_ltga_souffle",
)

mbh_time = 600
probe_only = False
for a in sys.argv[1:]:
    if a == "--probe-only":
        probe_only = True
    elif a.isdigit():
        mbh_time = int(a)

os.makedirs(WORK_ROOT, exist_ok=True)

if probe_only:
    work = os.path.join(WORK_ROOT, "runs", CASE["tag"] + "_probe")
    os.makedirs(work, exist_ok=True)
    import emtg_options as EO
    mo = EO.build_options(CASE["seq"], CASE["t0_mjd2000"], CASE["tof_class_yr"] * 365.25,
                          mbh_time=30, out_dir=work, rng_seed=-1, loose_time=True)
    opt = os.path.join(work, "probe.emtgopt")
    EO.write_options_file(mo, opt)
    print("[probe] wrote", opt)
    xf = P.run_emtg_layout_probe(opt, work, "probe.log")
    print("[probe] XFfile:", xf)
    sys.exit(0 if xf else 1)

res = P.run_case(
    CASE["seq"], CASE["t0_mjd2000"], CASE["tofs"], CASE["tof_class_yr"] * 365.25,
    CASE["tag"], mbh_time=mbh_time,
    tof_lo_days=CASE["tof_lo_days"], tof_hi_days=CASE["tof_hi_days"],
    work_root=WORK_ROOT, arrival_type=3,
)
print("[driver] RESULT:", res)

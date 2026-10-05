# -*- coding: utf-8 -*-
"""SOUFFLE 联合寻优：Uno 与 Ipopt 的多进程路由。

为什么不是一个进程：Uno 与 Ipopt 各自携带同名但不同工具链编译的 mingw 运行时
（libwinpthread-1.dll 等 5 个），Windows 按名字解析 DLL，先加载的一方占住名字后
另一方必死（实测 Ipopt 在第 0 次迭代以 0xc06d007f 退出）。所以联合只能靠多进程，
本模块就是那层路由。

两种结构（`--mode`）：
  parallel  Uno ‖ 独立 Ipopt -> 取优。
            两者互不依赖，且 Uno 慢得多（62 s vs 33 s），所以并行几乎是白赚。
  both      在 parallel 之上，再用 Uno 的解热启动一次 Ipopt -> 三者取优。
            热启动那一路从 Uno 找到的几何出发，落进与独立跑不同的盆地，
            实测平均多拿约 19 kg；又因它串在 Uno 之后，墙钟与 parallel 相差
            仅一段 Ipopt（约 32 s）。默认。

用法：
    python united.py --case case.emtgopt --out run/ --mode both
    python united.py --dir cases/ --jobs 8 --mode parallel
"""
import argparse
import concurrent.futures as cf
import io
import json
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geometry as G                                    # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SOUFFLE_ROOT = os.path.dirname(HERE)
BIN_DIR = os.path.join(SOUFFLE_ROOT, 'bin')

# 快速档：墙钟 ≈ max(MBH 预算, 单次求解上限)，实测 Uno 62 s、Ipopt 33 s。
# Uno 收敛慢，预算给小了会找不到解；Ipopt 30 s 内已定局，再给是浪费。
UNO_TIME = dict(mbh_time=60, solve_time=15)
IPOPT_TIME = dict(mbh_time=30, solve_time=8)

STAGE_UNO = 'uno'
STAGE_IPOPT = 'ipopt'
STAGE_WARM = 'warm'
PICK_ORDER = (STAGE_UNO, STAGE_IPOPT, STAGE_WARM)


def solver_env(solver):
    """运行时路径 + 该阶段要用的求解器。

    SOUFFLE_UNO_ROOT 缺了 Uno 会以 126 退出；SOUFFLE_NLP_SOLVER 决定同一次构建里跑哪个
    求解器——发行包只有一个 SOUFFLE.exe，靠这个变量切换。
    """
    env = dict(os.environ)
    env['PYTHONIOENCODING'] = 'utf-8'
    env['SOUFFLE_NLP_SOLVER'] = solver
    uno_root = env.get('SOUFFLE_UNO_ROOT', '')
    head = [BIN_DIR, os.path.join(uno_root, 'bin'), os.path.join(uno_root, 'deps')]
    ipopt_lib = env.get('SOUFFLE_IPOPT_LIB')
    if not ipopt_lib:
        raise SystemExit('SOUFFLE_IPOPT_LIB is not set: point it at the directory holding '
                         'ipopt-3.dll and its MUMPS/BLAS/LAPACK dependencies.')
    head.append(ipopt_lib)
    env['PATH'] = os.pathsep.join(head + [env.get('PATH', '')])
    return env


def run_solver(exe, workdir, solver, timeout=900):
    """在 workdir 里跑一个求解器 -> (ok, 秒数, 说明)。"""
    t0 = time.time()
    try:
        with io.open(os.path.join(workdir, 'main.log'), 'w', encoding='utf-8',
                     errors='replace') as log:
            proc = subprocess.run([exe, 'case.emtgopt'], cwd=workdir, env=solver_env(solver),
                                  stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, time.time() - t0, '超时'
    except OSError as exc:
        return False, time.time() - t0, '无法启动 %s: %s' % (os.path.basename(exe), exc)
    wall = time.time() - t0
    return (proc.returncode == 0), wall, ('' if proc.returncode == 0
                                          else '退出码 %d' % proc.returncode)


def stage_case(template, workdir, **times):
    """把模板 case 铺到一个阶段的工作目录，并改写工作目录与时间设置。"""
    os.makedirs(workdir, exist_ok=True)
    text = io.open(template, encoding='utf-8', errors='replace').read()
    text = G.rewrite_options(text, workdir, root=SOUFFLE_ROOT, **times)
    out = os.path.join(workdir, 'case.emtgopt')
    io.open(out, 'w', encoding='utf-8', newline='').write(text)
    return out


def solve_one(name, template, root, mode, uno_exe, ipopt_exe):
    """一个任务的联合寻优：并行两路 -> （both 时）热启动第三路 -> 取优。

    墙钟按实际编排算：并行段取两者较大者，热启动段串在其后。
    """
    task_dir = os.path.join(root, name)
    os.makedirs(task_dir, exist_ok=True)
    stages = {}

    def record(stage, ok, wall, note):
        stages[stage] = dict(ok=ok, wall_s=round(wall, 1), note=note)

    def run_stage(stage, exe, times):
        # The stage name decides which solver the shared binary runs.
        stage_solver = {'Uno': 'Uno', 'ipopt': 'IPOPT', 'warm': 'IPOPT'}.get(stage, 'Uno')
        workdir = os.path.join(task_dir, stage)
        stage_case(template, workdir, **times)
        record(stage, *run_solver(exe, workdir, stage_solver))

    # 并行段：Uno 与独立 Ipopt 互不依赖。Uno 慢一倍多，所以并行近乎白赚一个盆地。
    first = [(STAGE_UNO, uno_exe, UNO_TIME), (STAGE_IPOPT, ipopt_exe, IPOPT_TIME)]
    with cf.ThreadPoolExecutor(max_workers=len(first)) as pool:
        list(pool.map(lambda a: run_stage(*a), first))
    wall_s = max(stages[s]['wall_s'] for s, _, _ in first)

    # 热启动段：拿 Uno 的几何让 Ipopt 从另一个盆地起步。case 由 make_warmstart_case 写好。
    if mode == 'both':
        seed = G.best_solution(os.path.join(task_dir, STAGE_UNO))[1]
        warm_dir = os.path.join(task_dir, STAGE_WARM)
        if not (stages[STAGE_UNO]['ok'] and seed):
            record(STAGE_WARM, False, 0.0, 'Uno 没有可用解做种子')
        else:
            good, info = G.make_warmstart_case(
                seed, template, os.path.join(warm_dir, 'case.emtgopt'),
                workdir=warm_dir, **IPOPT_TIME)
            if not good:
                record(STAGE_WARM, False, 0.0, '热启动未生成：' + info)
            else:
                record(STAGE_WARM, *run_solver(ipopt_exe, warm_dir, 'IPOPT'))
        wall_s += stages[STAGE_WARM]['wall_s']

    best_mass, best_path, best_stage = None, None, None
    for stage in PICK_ORDER:
        mass, path = G.best_solution(os.path.join(task_dir, stage))
        if mass is not None and (best_mass is None or mass > best_mass):
            best_mass, best_path, best_stage = mass, path, stage
    if best_path:
        shutil.copy2(best_path, os.path.join(task_dir, 'solution.emtg'))

    result = dict(task=name, mode=mode, best_stage=best_stage,
                  best_mass_kg=round(best_mass, 3) if best_mass else None,
                  wall_s=round(wall_s, 1), stages=stages)
    io.open(os.path.join(task_dir, 'result.json'), 'w', encoding='utf-8').write(
        json.dumps(result, indent=1, ensure_ascii=False))
    return result


def collect_tasks(args):
    """--case 单个；--dir 下每个子目录里的 case.emtgopt 各算一个任务。"""
    if args.case:
        return [('case', os.path.abspath(args.case))]
    return [(entry, os.path.join(args.dir, entry, 'case.emtgopt'))
            for entry in sorted(os.listdir(args.dir))
            if os.path.isfile(os.path.join(args.dir, entry, 'case.emtgopt'))]


def main():
    ap = argparse.ArgumentParser(description='SOUFFLE 联合寻优（Uno + Ipopt 多进程路由）')
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument('--case', help='单个 case.emtgopt')
    src.add_argument('--dir', help='任务目录：其下每个子目录含 case.emtgopt')
    ap.add_argument('--out', default='united_run', help='输出根目录')
    ap.add_argument('--mode', choices=('parallel', 'both'), default='both',
                    help='parallel=Uno ‖ Ipopt 取优；both=再加一次热启动后三者取优')
    ap.add_argument('--jobs', type=int, default=4, help='并发任务数')
    # One binary holds both solvers; the stage decides which by environment variable. The
    # separate --ipopt-exe is kept for a two-executable layout if one is ever built.
    default_exe = os.path.join(BIN_DIR, 'SOUFFLE.exe')
    ap.add_argument('--uno-exe', default=default_exe)
    ap.add_argument('--ipopt-exe', default=default_exe)
    ap.add_argument('--limit', type=int, default=0)
    args = ap.parse_args()

    for exe in (args.uno_exe, args.ipopt_exe):
        if not os.path.isfile(exe):
            print('缺少求解器：%s' % exe)
            return 2
    if not os.environ.get('SOUFFLE_UNO_ROOT'):
        print('提示：未设 SOUFFLE_UNO_ROOT，Uno 会因找不到 libuno.dll 而退出')

    tasks = collect_tasks(args)
    if args.limit:
        tasks = tasks[:args.limit]
    if not tasks:
        print('没有找到任务')
        return 2
    os.makedirs(args.out, exist_ok=True)

    print('联合寻优: %d 个任务  模式=%s  并发=%d' % (len(tasks), args.mode, args.jobs))
    t0 = time.time()
    results, done = [], 0
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = [pool.submit(solve_one, name, case, args.out, args.mode,
                            args.uno_exe, args.ipopt_exe) for name, case in tasks]
        for fut in cf.as_completed(futs):
            results.append(fut.result())
            done += 1
            if done % 5 == 0 or done == len(tasks):
                print('  [%d/%d] 用时 %.0f s' % (done, len(tasks), time.time() - t0),
                      flush=True)

    found = [r for r in results if r['best_mass_kg']]
    print('\n完成 %d 个，出解 %d 个，总用时 %.0f s' % (len(results), len(found), time.time() - t0))
    if found:
        by = {}
        for r in found:
            by[r['best_stage']] = by.get(r['best_stage'], 0) + 1
        masses = sorted(r['best_mass_kg'] for r in found)
        print('取优来源分布: %s' % by)
        print('末质量中位 %.2f kg   墙钟中位 %.1f s'
              % (masses[len(masses) // 2],
                 sorted(r['wall_s'] for r in found)[len(found) // 2]))
    io.open(os.path.join(args.out, 'united_summary.json'), 'w', encoding='utf-8').write(
        json.dumps(results, indent=1, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())

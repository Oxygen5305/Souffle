# -*- coding: utf-8 -*-
"""解文件几何 ↔ case.emtgopt 的 TRIALX 互转。

联合寻优的"串行热启动"要把 Uno 找到的几何交给 Ipopt。几何只存在于两处：
  - 解文件的事件表：每行 `序号 | JulianDate | 日期 | 事件类型 | 天体 | ...`
  - case.emtgopt 的 BEGIN_TRIALX 段：一行出发历元 + 若干行各段飞行时间

所以这里只做纯文本的读写，不碰任何转录代码，也不依赖上层流水线。

历元换算：TRIALX 用 MJD，解文件给的是 JD，而 t0 定义为相对 J2000 的天数，
故 `TRIALX_epoch = t0 + 51544.5`（MJD2000）。
"""
import io
import os
import re

MJD2000 = 51544.5
JD_J2000 = 2451544.5

# 事件表只认这几列，避免依赖整张表的列数。必须有 (?m)：没有它时 `^` 只匹配
# 整个文本的开头，findall 会一行都取不到（单行 match 却看不出问题）。
RE_EVENT_ROW = re.compile(
    r'(?m)^\s*\d+\s*\|\s*([\d.]+)\s*\|\s*[\d/]+\s*\|\s*(\w+)\s*\|\s*([\w\- ]+?)\s*\|')
RE_EPOCH = re.compile(r'(?m)^(.*event left state epoch,)([\d.eE+-]+)\s*$')
RE_FLIGHT_TIME = re.compile(r'(?m)^(p\d+MGALT: phase flight time,)([\d.eE+-]+)\s*$')
RE_MASS = re.compile(r'Spacecraft:\s*Final mass[^\n:]*:\s*([0-9.eE+-]+)')

ARRIVAL_EVENTS = ('LT_rndzvs', 'rendezvous')


def events(emtg_path):
    """解文件的事件表 -> [(jd, event, location), ...]（按出现顺序）。"""
    try:
        text = io.open(emtg_path, encoding='utf-8', errors='replace').read()
    except OSError:
        return []
    return [(float(jd), ev, loc.strip())
            for jd, ev, loc in RE_EVENT_ROW.findall(text)]


def solution_geometry(emtg_path):
    """解文件 -> (seq, t0_mjd2000, [tof...], total_days)，解析不出返回 None。

    seq 只由飞掠顺序决定（E-<各飞掠天体首字母>-U），与原始 case 的命名无关，
    所以能用它判断"解出来的序列是否与要热启动的那个 case 一致"。
    """
    ev = events(emtg_path)
    if not ev:
        return None
    launch = next((e for e in ev if e[1] == 'launch'), None)
    arrival = next((e for e in reversed(ev) if e[1] in ARRIVAL_EVENTS), None)
    flybys = [e for e in ev if 'flyby' in e[1]]
    if launch is None or arrival is None:
        return None
    chain = [launch] + flybys + [arrival]
    tofs = [chain[i + 1][0] - chain[i][0] for i in range(len(chain) - 1)]
    seq = 'E-' + '-'.join(f[2][:1] for f in flybys) + '-U'
    return seq, launch[0] - JD_J2000, tofs, arrival[0] - launch[0]


def solution_mass(emtg_path):
    """解文件的末质量（kg）。"""
    try:
        text = io.open(emtg_path, encoding='utf-8', errors='replace').read()
    except OSError:
        return None
    m = RE_MASS.search(text)
    if not m:
        return None
    try:
        return float(m.group(1))
    except ValueError:
        return None


def best_solution(root):
    """目录树下最好的解 -> (mass, path)；没有解返回 (None, None)。

    FAILURE_*.emtg 是求解器放弃时写的中间态，不算解。
    """
    best, path = None, None
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in filenames:
            if not fn.endswith('.emtg') or fn.startswith('FAILURE_'):
                continue
            p = os.path.join(dirpath, fn)
            m = solution_mass(p)
            if m is not None and (best is None or m > best):
                best, path = m, p
    return best, path


def read_trialx(case_path):
    """case.emtgopt 的 TRIALX 几何 -> (epoch_mjd, [flight_time...])，读不到返回 None。"""
    try:
        text = io.open(case_path, encoding='utf-8', errors='replace').read()
    except OSError:
        return None
    m = RE_EPOCH.search(text)
    fts = [float(v) for _, v in RE_FLIGHT_TIME.findall(text)]
    if not m or not fts:
        return None
    return float(m.group(2)), fts


def apply_geometry(case_text, epoch_mjd, tofs):
    """把几何写进 TRIALX 文本 -> (新文本, 写入的段数)。"""
    text = RE_EPOCH.sub(lambda m: '%s%.20f' % (m.group(1), epoch_mjd), case_text, count=1)
    it = iter(tofs)
    written = [0]

    def sub(m):
        try:
            value = next(it)
        except StopIteration:
            return m.group(0)
        written[0] += 1
        return '%s%.20f' % (m.group(1), value)

    return RE_FLIGHT_TIME.sub(sub, text), written[0]


def rewrite_options(case_text, workdir=None, mbh_time=None, solve_time=None):
    """改写工作目录与两个时间设置。"""
    if workdir:
        case_text = re.sub(r'(?m)^forced_working_directory\s+.*$',
                           'forced_working_directory ' + workdir.replace('\\', '/'),
                           case_text)
    if mbh_time:
        case_text = re.sub(r'(?m)^MBH_max_run_time\s+\d+',
                           'MBH_max_run_time %d' % mbh_time, case_text)
    if solve_time:
        case_text = re.sub(r'(?m)^snopt_max_run_time\s+\d+',
                           'snopt_max_run_time %d' % solve_time, case_text)
    return case_text


def make_warmstart_case(seed_emtg, template_case, out_path, workdir=None,
                        mbh_time=None, solve_time=None):
    """用 seed_emtg 的几何改写 template_case，写出热启动 case。

    返回 (ok, 说明)。段数不匹配时拒绝写出——那说明解与模板不是同一个问题，
    硬套会把热启动点写歪。
    """
    got = solution_geometry(seed_emtg)
    if not got:
        return False, '种子解里没有事件表'
    seq, t0, tofs, _total = got
    geo = read_trialx(template_case)
    if not geo:
        return False, '模板里没有 TRIALX 几何'
    if len(geo[1]) != len(tofs):
        return False, '段数不匹配：模板 %d 段，种子 %d 段' % (len(geo[1]), len(tofs))

    text = io.open(template_case, encoding='utf-8', errors='replace').read()
    text, n = apply_geometry(text, t0 + MJD2000, tofs)
    if n != len(tofs):
        return False, '只写入了 %d/%d 段' % (n, len(tofs))
    text = rewrite_options(text, workdir, mbh_time, solve_time)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    io.open(out_path, 'w', encoding='utf-8', newline='').write(text)
    return True, '%s  %s' % (seq, [round(x, 1) for x in tofs])

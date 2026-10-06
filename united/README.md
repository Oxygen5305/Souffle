# 联合寻优（united）

Uno 与 Ipopt 各自求解，再取较优解。

## 为什么是多进程

两个求解器各自携带**同名但不同工具链编译的 mingw 运行时**（`libwinpthread-1.dll`、
`libgcc_s_seh-1.dll`、`libgomp-1.dll`、`libatomic-1.dll`、`libquadmath-0.dll`）。
Windows 按名字解析 DLL，先加载的一方占住名字后，另一方的延迟加载导入就会解析到错的
运行时——实测 Ipopt 在第 0 次迭代以 `0xc06d007f`（ERROR_DELAY_LOAD_FAILED）退出。
换 PATH 顺序无效，只是把失败转移给另一方。

所以联合只能靠**两个进程**，本目录就是那层路由。原先的 `Hybrid_interface`
（同进程）已因此退役，`SOUFFLE_NLP_SOLVER=HYBRID` 现在会明确报错并指向这里。

## 两种结构

| `--mode` | 编排 | 单任务墙钟 | 说明 |
|---|---|---|---|
| `parallel` | Uno ‖ 独立 Ipopt → 取优 | ≈ 62 s | 两者互不依赖；Uno 慢一倍多，并行近乎白赚 |
| `both`（默认） | 上面两路，再用 **Uno 的解**热启动一次 Ipopt → 三者取优 | ≈ 100 s | 热启动那路从另一个盆地起步 |

`both` 值得默认：实测三者取优相对 `max(Uno, 独立 Ipopt)` 平均多拿约 **19 kg**，
而它只多串一段 Ipopt——热启动必须排在 Uno 之后，多出的就是这个。

**另一种编排（只有 Uno → 热启动，不跑独立 Ipopt）已移除**：它在四族 130 个任务上
与"并行取优"中位持平、平均更差，却同样要串一段，没有存在理由。

## 用法

```bat
REM 先设好 Uno 的运行时位置，否则 Uno 会以 126 退出（找不到 libuno.dll）
set SOUFFLE_UNO_ROOT=<Uno 安装目录>

REM 单个算例
python united\united.py --case case.emtgopt --out run\ --mode both

REM 一批算例：--dir 下每个子目录里的 case.emtgopt 各算一个任务
python united\united.py --dir cases\ --jobs 8 --mode both
```

Ipopt 的 DLL 位置由 `SOUFFLE_IPOPT_LIB` 指定（发行包里就是 `bin\`，因为 MKL 必须与
`libblas.dll` 同目录）；未设置时会报错退出。

## 时间设置

墙钟 ≈ `max(MBH 预算, 单次求解上限)`，因为 MBH 打断不了进行中的求解。
所以真正的旋钮是**单次求解上限**，不是预算：

| 求解器 | 预算 | 单次上限 | 实测墙钟 |
|---|---|---|---|
| Uno | 60 s | 15 s | 62 s |
| Ipopt | 30 s | 8 s | 33 s |

原先把 `snopt_max_run_time` 留在 120 s，等于强制每次求解空转满 120 s——
实测 Ipopt 在 120 s 里跑了 11847 次迭代，而 1352 次就达到了完全相同的解。
两档都在 `united.py` 顶部的 `UNO_TIME` / `IPOPT_TIME` 里，改这两处即可。

## 输出

```
<out>/<任务>/
    uno/     case.emtgopt, main.log, <时间戳>/<解>.emtg
    ipopt/   同上（独立跑）
    warm/    同上（热启动；case 的 TRIALX 已换成 Uno 的几何）
    solution.emtg   三者中最优的那份
    result.json     best_stage / best_mass_kg / wall_s / 各阶段状态
<out>/united_summary.json
```

## 两个模块

| 文件 | 职责 |
|---|---|
| `geometry.py` | 解文件几何 ↔ `case.emtgopt` 的 TRIALX 互转；末质量解析。纯文本，不依赖转录代码 |
| `united.py` | 进程编排、取优、报告 |

`geometry.py` 里 `TRIALX_epoch = t0 + 51544.5`（TRIALX 用 MJD，解文件的 t0 相对 J2000）。
注意别把"种子与解的出发时刻之差"当成换算误差：实测该差值出现 51524.5 / 51564.5 两个峰，
那是种子与解本身差 ∓20 天。

## 取优为什么划算

冷启动的 Ipopt 与热启动的 Ipopt 落在**不同盆地**，因此"三者取优"不能被"热启动"替代。
四族 130 个算例相对 SNOPT 基准的差值（末质量，kg）：

| 编排 | 中位 | 平均 |
|---|---|---|
| Uno 单独 | −4.57 | +34.07 |
| Ipopt 单独（冷） | +11.22 | +65.64 |
| Ipopt 单独（热启动） | +4.36 | +47.67 |
| `max(Uno, 冷 Ipopt)` | +40.90 | +102.46 |
| `max(Uno, 热 Ipopt)` | +42.77 | +81.71 |
| **`max(三者)`** | **+76.76** | **+121.41** |

注意第一、二行：**冷启动的 Ipopt 明显优于热启动**，所以独立跑一路不能省。

## 性能

单任务墙钟（同一算例，四个任务族的实测中位）：

| 编排 | 墙钟 | 相对 Uno 单独 | 末质量（相对 SNOPT 基准） |
|---|---|---|---|
| Uno 单独 | 62 s | 1.00× | +34.07 kg |
| Ipopt 单独（冷启动） | **33 s** | 0.53× | **+65.64 kg** |
| `parallel`（两路并行取优） | **62 s** | **1.00×** | **+102.46 kg** |
| `both`（默认，再加一段热启动） | 100 s | 1.61× | +121.41 kg |

三点值得注意：

- **并行几乎不花时间**。Uno 要 62 s、Ipopt 要 33 s，串行是 95 s；并行后仍是 62 s，因为
  Ipopt 完全藏在 Uno 后面。所以 `parallel` 相对"只跑 Uno"**是白赚的**。
- **`both` 多花的 38 s 换回约 19 kg**。它相对 `max(Uno, 冷 Ipopt)` 的增益来自热启动那一路
  落在另一个盆地。是否值得看任务，所以两种模式都保留。
- **Ipopt 单跑最快但质量最低**。33 s 拿到 +65.64 kg，比 Uno 单跑的 +34.07 kg 好，但仍明显
  不如 `parallel` —— 因为它是**单独一个盆地**的答案。

### 与 SNOPT 版的时间对比（这才是关键）

上面那张表容易让人以为联合寻优"要付时间代价"。**相对 SNOPT 版恰恰相反**：

| | MBH 预算 | 单次求解上限 | 一次运行最长 |
|---|---|---|---|
| SOUFFLE 出厂算例设置（SNOPT 风格） | `900` s | `60` s | **900 s** |
| 联合寻优 `parallel` | `60` s | Uno 15 s / Ipopt 8 s | **62 s** |
| 联合寻优 `both` | `60 + 30` s | 同上 | **100 s** |

所以从 SNOPT 版换过来的人，**等待时间缩短约 9–14 倍**，而末质量还更高。

原因不神秘：联合寻优把**单次求解上限**从 60 s 降到 15 s / 8 s。原先把上限留在 120 s 时，
实测 Ipopt 在 120 s 里跑了 11847 次迭代，而 **1352 次就达到了完全相同的解**——多出来的时间
纯属空转。降上限几乎不损失质量，却直接砍掉墙钟。

（"Uno 单独 62 s → `both` 100 s" 那笔账是**联合寻优内部**的比较：多串一段热启动换约 19 kg。
两者不矛盾——相对 SNOPT 它快得多，相对"只跑 Uno"它多花 38 s。）

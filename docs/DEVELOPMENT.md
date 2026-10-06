# SOUFFLE — Scalable Optimization Uno-powered Framework For Leveraging EMTG

SOUFFLE 基于 NASA 的 [EMTG](https://github.com/nasa/EMTG)（Evolutionary Mission Trajectory Generator），
把内层非线性优化器从 **SNOPT** 换成开源求解器 **[Uno](https://github.com/cvanaret/Uno)**，
以便在 SNOPT 试用许可到期后继续使用。

## 1. 任务目标（Definition of Done）

1. `Souffle_Cheese\build\src\EMTGv9.exe` 能用 **Uno** 求解器跑通 EVVEU LTGA 用例
2. 原 `EMTG\` 目录**逐字节与基线一致**
3. 发行包在新机器上解压后**双击即用**：不装编译器、不设环境变量、不改 `PATH`

---

## 2. 可行性依据

| 结论 | 证据 |
|---|---|
| Uno 有完整的 **C 接口** | `Uno\include\uno\Uno_C_API.h`：模型、变量/约束边界、目标、稀疏雅可比、初始点、options、presets、回调、结果查询，60+ 函数 |
| C API 符号确实导出 | `libuno.dll` 导出 **74 个未修饰 C 符号**；`libuno.a` 中 `uno_create_model` / `uno_optimize` / `uno_set_solver_preset` 等均为 `T` |
| **有对应 SNOPT 语义的 preset** | `filtersqp`（"TR Fletcher-filter restoration inequality-constrained **SQP** method"）；另有 `ipopt`、`funnel`、`Penalty` |
| **有对应 EMTG chaperone 的回调** | `uno_set_solver_callbacks(...)`：每轮迭代可得 primal、可行性、稳定性、互补性残差 |
| 不需要 Hessian（EMTG 不提供） | 实测 Uno 自动回退：`An exact Hessian was not provided, setting an L-LBFGS Hessian instead` |
| 子问题求解器与线性代数齐备，**无许可限制** | `Uno\deps\`：BQPD、HiGHS、MUMPS、METIS、OpenBLAS、SPRAL；`libhsl.dll` 是官方文档所述的免授权 dummy 版 |
| MinGW 侧端到端跑通 | `_probe\smoke_uno.c`：Uno 2.9.0 正确解出 NLP（`x=(1,0)`、目标 `998` 即理论最优、可行性 `0`），`filtersqp` 生效，迭代回调触发 |
| **MSVC 可驱动 Uno**（架构关键） | `_probe\msvc_probe.c`：MSVC 编译，`LoadLibraryExW` + `GetProcAddress` 全部成功，`uno version 2.9.0`，solver/model 创建成功 |

---

## 3. 工具链与架构决策

### 3.1 工具链事实

**原版 EMTG 是用 MSVC + NMake 构建的**，不是 MinGW：

- `EMTG\build\CMakeCache.txt` → `CMAKE_CXX_COMPILER = D:\Visual_Studio\VC\Tools\MSVC\14.40.33807\bin\Hostx64\x64\cl.exe`
- `CMAKE_GENERATOR = NMake Makefiles`
- Boost 用 `libboost_filesystem-vc143-*`（MSVC ABI）
- cmake 用 VS 自带：`D:\Visual_Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- Windows SDK（参考构建使用 `10.0.28000.0`，亦可用便携版）

而 **Uno 的 Windows 预编译包是 MinGW 格式**（`libuno.dll.a`），MSVC 链接器无法直接链接。

### 3.2 决策：保持 MSVC，通过**动态加载**调用 Uno

由于 `libuno.dll` 导出的是**未修饰 C 符号**，C ABI 跨编译器稳定，因此用
`LoadLibraryExW` + `GetProcAddress` 驱动 Uno，**不链接任何 Uno 库**。

**收益**

- Boost vc143、SPICE、SNOPT、Windows SDK 全部沿用，零迁移成本
- 同一个可执行文件内即可保留 SNOPT 对照路径
- 不需要 MinGW 工具链参与主构建

**代价**

- 需要在运行时定位 `libuno.dll`（由 `SOUFFLE_UNO_ROOT` 指定）
- 缺少编译期符号校验，靠启动时的显式报错兜底

### 3.3 代码结构

| 文件 | 作用 |
|---|---|
| `src\InnerLoop\SouffleApi.h/.cpp` | 动态加载层：`LoadLibraryExW` + `GetProcAddress`，74 个 C API 调用桩 |
| `src\InnerLoop\Souffle_interface.h/.cpp` | 求解器接口：实现 `NLP_interface`，把 EMTG 的模型/边界/雅可比映射到 Uno |
| `src\InnerLoop\NLP_interface.h` | 求解器抽象基类（原为 SNOPT 专有，已抽出虚析构与 `getInform()`） |
| `src\InnerLoop\NLP_solver_factory.h/.cpp` | 工厂：按 `SOUFFLE_NLP_SOLVER` 返回 `std::unique_ptr<NLP_interface>` |
| `src\InnerLoop\NLPoptions.h/.cpp` | 新增 `solver_name`，默认值来自 CMake |

### 3.4 环境变量

| 变量 | 默认 | 说明 |
|---|---|---|
| `SOUFFLE_NLP_SOLVER` | `Uno` | 选 `Uno` 或 `SNOPT`。**SNOPT 仅在编译时开启 `SOUFFLE_WITH_SNOPT=ON` 才可用**；默认构建只含 Uno |
| `SOUFFLE_UNO_ROOT` | 编译期内置 | Uno 安装目录（含 `bin\`、`deps\`） |
| `SOUFFLE_UNO_PRESET` | `filtersqp` | 也可设 `ipopt` / `funnel` / `Penalty` |

> ⚠️ `SOUFFLE_UNO_PRESET` 的值**必须大写**（例如 logger 用 `SILENT`）。
> 小写会被 Uno 静默拒绝并回退默认，曾导致 22 MB / 373,772 行日志、运行时间被 I/O 主导。

---

## 4. 验证结果

### 4.1 求解器 preset A/B 实测

| preset | 收敛率 | 可行解 |
|---|---|---|
| `filterSQP`（默认） | **111/138 `UNO_SUCCESS`** | 有 |
| `ipopt` | 33/263 | 无 |

由表格可知，IPOPT预设性能并不突出、FilterSQP预设性能更优。

Uno的IPOPT预设是指使用IPOPT相同的算法下进行优化，与原生IPOPT的性能极其相似（见Uno官方文档），所以也证明了IPOPT对EMTG的适配性不强。

其原因可能是IPOPT在序列固定、变量少、内点法在初始可行时才有很好的性能，但LTGA广泛寻优不具备这类条件。而SQP/filter 对不可行初始点更鲁棒，且和 EMTG 的解析梯度、稀疏结构天然匹配。

### 4.2 与 SNOPT 的同口径对比（2044 窗口，E-V-V-E-U）

**必须所有设置对齐才可比。**

对齐后（`run_evveu\RETRY_t40_aligned.emtgopt`，与 `ESFO_Uranus\code\emtg_options.py` 逐项一致）：

| 参数 | 对齐值 | 基准 |
|---|---|---|
| `maximum_mass` / `allow_initial_mass_to_vary` | 5305.0 / 0 | 同 |
| `final_mass_constraint_bounds` | 3705.0 5305.0 | 同 |
| `maximum_electric_propellant` | 1317.6405 | 同 |
| `initial_impulse_bounds` | 0.0 – √15 | 同 |
| `num_timesteps` / `number_of_steps` | 40 / 40 | 40 / 40 |
| `snopt_feasibility_tolerance` | 1e-06 | 1e-06 |
| `snopt_optimality_tolerance` | 1e-05 | 1e-05 |
| `integrator_tolerance` | 1e-08 | 1e-08 |

模型规模：**518 变量 / 206 约束 / 4721 雅可比非零元**（3 步时仅 86 变量）。

| | **SOUFFLE/Uno** | **SNOPT 基准** `b2044_EVVEU_0317_03486_t85_c12` |
|---|---|---|
| 初始质量（读自解文件） | 5305.0 kg | 5305.0 kg |
| TOF | 8.4873 yr | 8.4863 yr |
| **末质量** | **4598.48 kg** | 4438.15 kg |
| **电推进剂** | **706.52 kg** | 866.85 kg |
| 发射日 | 2044-02-25 | 2044-03-14 |
| MBH 预算 | 5400 s | 更长 |

**结论（谨慎）**：在转录、物理模型、初始质量、离散化、容差全部对齐的条件下，
SOUFFLE/Uno 的末质量比同档 SNOPT 基准高 **160.3 kg（+3.61%）**，证明SOUFFLE有时能找到比SNOPT更优的解。

### 4.3 ESFO_Uranus 天王星行星际转移序列二次高精度广泛寻优实测（真实工程流水线）

用 ESFO_Uranus **自己的**二级流水线（`code\emtg_pipeline.py`）、**自己的** case 定义
（`emtg\tier2_plan_2044_window.json`：2044 窗口，`t0=16144.5`，`tofs=[150,380,52,1610]`，
TOF 区间 [2136.7, 2374.1]），仅把 `config.EMTG_BIN` 在内存中指向 SOUFFLE：

| | **SOUFFLE/Uno** | **SNOPT 基准** `b2044_EVVEU_0416_01122_t65_c15` |
|---|---|---|
| 末质量 | **4150.98 kg** | 4150.07 kg |
| 电推进剂 | **1154.02 kg** | 1154.93 kg |
| TOF | 6.500 yr | 6.5 yr 档 |
| 差 | **+0.91 kg（+0.022%）** | — |

求解器统计：23 次 Uno 求解，12 次 "New global best"，最坏约束违反 **6.4e-13**。

**两者几乎重合** —— 这是最强的一致性证据：走真实流水线、真实 case 定义，未手工调任何参数。

驱动脚本：`Souffle_Cheese\tier2_driver.py`（**不修改 ESFO_Uranus**，通过覆盖 `config.EMTG_BIN` 实现）。

---

## 5. 发行包 `Souffle\`

### 5.1 双击 = 打开图形界面

**双击 `run_souffle.bat` → 打开 PyEMTG（`EMTG Python Interface`）**，
即 SNOPT 版 EMTG 使用的同一个 wxPython 图形界面，已接好本包的 SOUFFLE 求解器。

| 我要… | 操作 |
|---|---|
| **打开图形界面** | **双击 `run_souffle.bat`** |
| 直接跑算例（无界面） | `run_souffle.bat EVVEU_LTGA.emtgopt` |

实测（干净环境：仅系统 `PATH`，清除全部 `SOUFFLE_*` 与 `SNOPT_LICENSE`）：

```
[SOUFFLE] starting the PyEMTG graphical interface ...
[SOUFFLE] solver : ...\Souffle\bin\EMTGv9.exe
GUI window found after 2 s: hwnd=8914012 title='EMTG Python Interface'
RESULT : GUI opened and closed cleanly
```

### 5.2 可搬迁设计

GUI 需要**绝对路径**（`PyEMTG.options`），而包要能整体搬走——两者本质冲突。
解法：**启动器每次启动时按自身位置重新生成 `PyEMTG.options`**。
包内 `.emtgopt` 则全部使用相对路径（`../Universe_EVVEU`、`../HardwareModels/`、`../results`），
由 EMTG 按其工作目录解析（启动器以 `bin\` 为工作目录）。

### 5.3 目录结构

```
Souffle\
├── bin\                        EMTGv9.exe (SOUFFLE) + 运行时 DLL + default.emtgopt
├── python\                     自带 Python 3.13 + wxPython/numpy/scipy/matplotlib/astropy/spiceypy
├── PyEMTG\                     PyEMTG 图形界面
├── Uno\bin, Uno\deps\          Uno 求解器及其依赖（SOUFFLE_UNO_ROOT 指向此处）
├── Universe\                   完整星历/宇宙集（可在 GUI 中新建任务）
├── Universe_EVVEU\             EVVEU 任务的宇宙定义
├── HardwareModels\             推力器/电源/推进库
├── EVVEU_LTGA.emtgopt          算例
├── results\                    输出目录
├── run_souffle.bat             双击即用
├── README.md                   面向使用者的说明
└── licenses\                   EMTG(NOSA 1.3) / Uno(MIT) / 第三方
```

**默认构建完全不需要 SNOPT**：`SOUFFLE_WITH_SNOPT` 默认为 `OFF`，此时
`SNOPT_interface.cpp` 不参与编译、不引用任何 SNOPT 头文件、
**可执行文件也不导入 `snopt7.dll`**（已用 `dumpbin /imports` 与
"目录内无任何 snopt 文件仍能跑完" 双重验证）。
因此既不需要 SNOPT 安装，也不需要许可证。
开启 `SOUFFLE_WITH_SNOPT=ON` 才会恢复 SNOPT 对照路径，那时才需要自备 SNOPT 与许可证。

### 5.4 打包

```powershell
powershell -File package\package_souffle.ps1 -Force
```

脚本流程：可执行文件 → Uno 运行时 → 完整 `Universe` → `HardwareModels` → 算例（路径重定向）
→ 自带 Python + PyEMTG → 许可。并自检 `PyEMTG.options` 全部目标存在、包内 `.emtgopt`
无机器相关绝对路径。`-SkipGUI` 可产出不含 GUI 的精简包（仅直接运行模式）。

---

## 6. 命名迁移记录

项目原名 UnoMTG / Uno_EMTG，已全量迁移为 SOUFFLE / Souffle。

| 项 | 迁移前 | 迁移后 |
|---|---|---|
| 环境变量 | `UNOMTG_*` | **`SOUFFLE_*`** |
| 接口类 | `Uno_interface` / `UnoApi` | **`Souffle_interface` / `SouffleApi`** |
| 源文件 | `Uno_interface.*` / `UnoApi.*` | **`Souffle_interface.*` / `SouffleApi.*`** |
| 启动器 | `run_uno_emtg.bat` | **`run_souffle.bat`** |
| 日志前缀 | `UnoMTG: building model` | **`SOUFFLE: building model`** |
| CMake 选项 | `UNOMTG_WITH_UNO` 等 | **`SOUFFLE_WITH_UNO` 等** |

迁移后重新编译通过（`[100%] Built target EMTGv9`，0 个 C/LNK 错误），
并实测 `SOUFFLE_NLP_SOLVER=Uno` + `SOUFFLE_UNO_ROOT` 能正常求解。

**未随迁移改动的**（刻意保留）：

- `src\InnerLoop\monotonic_basin_hopping.*` 中的成员名 `mySNOPT`、字符串
  `"SNOPT has crashed ... Creating dumpfile."`、文件名 `.SNOPTcrash`
  —— 这是 EMTG 原有的 MBH **try-catch 兜底分支**，触发条件是**求解抛异常**（与用哪个求解器无关）。
  改动会影响 ESFO_Uranus 的归档/扫描逻辑，故保留。
      > 探测阶段（宽松时间 + 无种子）首轮 NLP 发散会触发它并留下 .SNOPTcrash 文件。
      > 该文件与求解器选择无关。

---

## 7. 已知问题与注意事项

| 问题 | 说明 |
|---|---|
| `.SNOPTcrash` 文件名 | 见 §7，属历史命名，触发与求解器选择无关 |
| 长时管道下退出码不可靠 | 二级驱动脚本已改为 `*> $log` 文件重定向 + `PYTHONUNBUFFERED=1`；此前 30 分钟运行配合管道会给出虚假的 `exit=-1` 并丢失全部缓冲输出 |
| `ESFO_Uranus` 的 `config.EMTG_BIN` | 硬编码指向原版 EMTG，无环境变量覆盖。用 `tier2_driver.py` 在内存中覆盖，**不修改该工程** |
| `SOUFFLE_UNO_PRESET` 大小写 | 值必须大写，否则被静默拒绝 |
| 探测阶段的 `SNOPTcrash` | 首轮 NLP 发散的正常副产物 |
| **写文件务必指定 UTF-8** | 本机 PowerShell 默认 ANSI(GBK)，用 `Set-Content` 不带 `-Encoding UTF8` 会**永久破坏** UTF-8 中文。见 §11 |

---

## 8. 开发计划与设计文档

| 文档 | 内容 |
|---|---|
| [`docs/PLAN.md`](docs/PLAN.md) | 阶段划分、硬约束、只读保护、风险登记册、工具链速查 |
| [`docs/DESIGN.md`](docs/DESIGN.md) | 动态加载层、求解器接口与 EMTG 的逐项映射、回调处理 |
| [`docs/uno-2.9.0-options.txt`](docs/uno-2.9.0-options.txt) | Uno 全部选项名与类型（实测导出） |
| `_probe\` | 冒烟测试、探针、构建与运行驱动脚本 |

---

## 9. 许可

- EMTG：NASA Open Source Agreement 1.3（见 `EMTG_NOSA_License.pdf`）
- Uno：MIT
- 第三方：见 `Souffle\licenses\`

---

## 10. 附：本项目曾发生的一次文档损坏事故（供后续参考）

在批量重命名过程中，用 PowerShell 的 `Get-Content -Raw` + `Set-Content -NoNewline`
（未指定 `-Encoding`）处理含中文的 Markdown，导致 UTF-8 字节被按 ANSI(GBK) 解码后又写回，
**三个文档的中文内容永久损坏**（既非有效 UTF-8 也非有效 GBK；反向恢复尝试恢复出的 CJK 字符数为 0），
且项目无版本控制可回滚。

**教训与规则**

- 读写 UTF-8 文本一律显式指定编码：`-Encoding UTF8`，或改用能保证 UTF-8 的工具
- 批量文本替换前先备份
- 事后用脚本校验：**凡含非 ASCII 的文件，必须能被 UTF-8 解码**
  （`_probe\find_encoding_damage.py` 即为此用途）

---

## 11. 联合寻优实测

SOUFFLE 现在带**两个求解器**（Uno 与 Ipopt）。它们**不能同进程运行**：两者各自携带同名但不同工具链编译的 MinGW 运行时，Windows 按名字解析 DLL，后加载的一方会以 `0xc06d007f` 失败。因此联合寻优把两者作为**独立进程**编排。

### 11.1 为什么取三者之优

四族任务、共 130 个算例，与 SNOPT 基准的差值（末质量，kg）：

| 编排 | 中位 | 平均 |
|---|---|---|
| Uno 单独 | −4.57 | +34.07 |
| Ipopt 单独（冷启动） | **+11.22** | **+65.64** |
| Ipopt 单独（以 Uno 的解热启动） | +4.36 | +47.67 |
| `max(Uno, 冷 Ipopt)` | +40.90 | +102.46 |
| `max(Uno, 热 Ipopt)` | +42.77 | +81.71 |
| **`max(三者)`** | **+76.76** | **+121.41** |

两个结论：

- **冷启动的 Ipopt 优于热启动的 Ipopt**，所以热启动**不能替代**独立跑一路；
- 两者**落在不同盆地**，因此同时保留才是最划算的。

这不支持早期"只在有把握时才跑 Ipopt"的思路——那个判据恰好会跳掉真正带来收益的那些求解。

### 11.2 实测一次（发行包、`--mode parallel`）

```
完成 1 个，出解 1 个，总用时 69 s
取优来源分布: {'uno': 1}
末质量 4698.394 kg   墙钟 69.1 s
  uno    ok  wall_s=69.1   SOUFFLE[solve]: preset=filtersqp opt_status=2 sol_status=0 iters=5721
  ipopt  ok  wall_s=36.7   SOUFFLE[ipopt-solve]: status=0 inform=1 iters=781 callbacks=782
```

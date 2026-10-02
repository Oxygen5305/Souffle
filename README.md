# SOUFFLE — Scalable Optimization Uno-powered Framework For Leveraging EMTG

**SOUFFLE** : **S**calable **O**ptimization **U**no-powered **F**ramework **F**or **L**everaging **E**MTG

SOUFFLE 基于 NASA 的 [EMTG](https://github.com/nasa/EMTG)（Evolutionary Mission Trajectory Generator），
把内层非线性优化器从 **SNOPT** 换成开源求解器 **[Uno](https://github.com/cvanaret/Uno)**，
以便在 SNOPT 试用许可到期后继续使用。

## 目录约定

| 目录 | 内容 |
|---|---|
| `Souffle_Cheese\` | **开发树**：源码、构建脚本、文档、验证记录 |
| `Souffle\` | **自包含发行包**：双击 `run_souffle.bat` 打开 PyEMTG 图形界面 |

---

## 1. 与原版 EMTG 的关系（最重要的一条）

| 目录 | 状态 | 说明 |
|---|---|---|
| `G:\Py\DeepSeekHarness\EMTG\` | 🔒 **只读，零改动** | 原版 SNOPT 版 EMTG。**不得修改其中任何一个字节。** 它是始终可用的回退与对照基准。 |
| `G:\Py\DeepSeekHarness\Souffle_Cheese\` | ✏️ 可写 | 本项目：EMTG 的 fork + Uno 求解器接口层 |
| `G:\Py\DeepSeekHarness\Souffle\` | ✏️ 可写 | 发行包（由 `Souffle_Cheese\package\` 打包产出） |
| `G:\Py\DeepSeekHarness\Uno\` | ✏️ 可写 | Uno v2.9.0 Windows (MinGW) 预编译发行包 |

**开发纪律**

- 所有改动只发生在 `Souffle_Cheese\`
- 需要复用原 EMTG 的资源（星历、Universe、脚本模板）一律**复制**，绝不就地修改
- 构建一律 `-S Souffle_Cheese -B Souffle_Cheese/build`（禁止 in-source 构建污染源码树）
- 只读性由基线清单校验，见 §5.3

---

## 2. 任务目标（Definition of Done）

1. `Souffle_Cheese\build\src\EMTGv9.exe` 能用 **Uno** 求解器跑通 EVVEU LTGA 用例
2. 原 `EMTG\` 目录**逐字节与基线一致**
3. 发行包在新机器上解压后**双击即用**：不装编译器、不设环境变量、不改 `PATH`

---

## 3. 为什么可行（全部为实测证据，非推测）

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

## 4. 工具链与架构决策

### 4.1 关键事实（纠正一个常见误判）

**原版 EMTG 是用 MSVC + NMake 构建的**，不是 MinGW：

- `EMTG\build\CMakeCache.txt` → `CMAKE_CXX_COMPILER = D:\Visual_Studio\VC\Tools\MSVC\14.40.33807\bin\Hostx64\x64\cl.exe`
- `CMAKE_GENERATOR = NMake Makefiles`
- Boost 用 `libboost_filesystem-vc143-*`（MSVC ABI）
- cmake 用 VS 自带：`D:\Visual_Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- Windows SDK：便携版 `G:\Py\DeepSeekHarness\winsdk`（`10.0.28000.0`）

而 **Uno 的 Windows 预编译包是 MinGW 格式**（`libuno.dll.a`），MSVC 链接器无法直接链接。

### 4.2 决策：保持 MSVC，通过**动态加载**调用 Uno

由于 `libuno.dll` 导出的是**未修饰 C 符号**，C ABI 跨编译器稳定，因此用
`LoadLibraryExW` + `GetProcAddress` 驱动 Uno，**不链接任何 Uno 库**。

**收益**

- Boost vc143、SPICE、SNOPT、Windows SDK 全部沿用，零迁移成本
- 同一个可执行文件内即可保留 SNOPT 对照路径
- 不需要 MinGW 工具链参与主构建

**代价**

- 需要在运行时定位 `libuno.dll`（由 `SOUFFLE_UNO_ROOT` 指定）
- 缺少编译期符号校验，靠启动时的显式报错兜底

### 4.3 代码结构

| 文件 | 作用 |
|---|---|
| `src\InnerLoop\SouffleApi.h/.cpp` | 动态加载层：`LoadLibraryExW` + `GetProcAddress`，74 个 C API 调用桩 |
| `src\InnerLoop\Souffle_interface.h/.cpp` | 求解器接口：实现 `NLP_interface`，把 EMTG 的模型/边界/雅可比映射到 Uno |
| `src\InnerLoop\NLP_interface.h` | 求解器抽象基类（原为 SNOPT 专有，已抽出虚析构与 `getInform()`） |
| `src\InnerLoop\NLP_solver_factory.h/.cpp` | 工厂：按 `SOUFFLE_NLP_SOLVER` 返回 `std::unique_ptr<NLP_interface>` |
| `src\InnerLoop\NLPoptions.h/.cpp` | 新增 `solver_name`，默认值来自 CMake |

### 4.4 环境变量

| 变量 | 默认 | 说明 |
|---|---|---|
| `SOUFFLE_NLP_SOLVER` | `Uno` | 选 `Uno` 或 `SNOPT`（后者需许可证） |
| `SOUFFLE_UNO_ROOT` | 编译期内置 | Uno 安装目录（含 `bin\`、`deps\`） |
| `SOUFFLE_UNO_PRESET` | `filtersqp` | 也可设 `ipopt` / `funnel` / `Penalty` |

> ⚠️ `SOUFFLE_UNO_PRESET` 的值**必须大写**（例如 logger 用 `SILENT`）。
> 小写会被 Uno 静默拒绝并回退默认，曾导致 22 MB / 373,772 行日志、运行时间被 I/O 主导。

---

## 5. 验证结果

### 5.1 求解器 preset A/B 实测

| preset | 收敛率 | 可行解 |
|---|---|---|
| `filtersqp`（默认） | **111/138 `UNO_SUCCESS`** | 有 |
| `ipopt` | 33/263 | 无 |

### 5.2 EVVEU LTGA 可行解

```
发射 2044-02-29 (Earth) → 金星 2044-09-05 → 金星 2045-10-08
                        → 地球 2045-12-05 → 天王星 2052-08-25 (LT_rndzvs)
TOF = 3100 d = 8.49 yr
末质量 = 干质量 = 4626.67 kg   (下限 3846)
电推进剂 = 819.33 kg           (上限 1352.66)
```

> 这是**早期粗离散化（`num_timesteps 3`）+ 宽松容差（1e-3）**下的解，只用于证明功能可用，
> **不可用于与 SNOPT 比较**（见 §5.4）。

### 5.3 只读性校验

```
基线: 36,775 文件
唯一差异: EMTG\HardwareModels\empty.ThrottleTableOUTPUT
          → 内容 SHA 与基线完全相同（57564E13C9D3A80B）
          → 该文件是 EMTG 每次运行都会重写的派生文件，且在修复 HardwarePath 之前产生
结论: 原版 EMTG 未被实质性修改
```

### 5.4 与 SNOPT 的同口径对比（2044 窗口，E-V-V-E-U）

**必须所有设置对齐才可比。** 早期曾误报「Uno 高 4.2%」，该结论**已撤回**——
因为当时初始质量多了 141 kg、离散化 3 步 vs 40 步、容差 1e-3 vs 1e-6。

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
SOUFFLE/Uno 的末质量比同档 SNOPT 基准高 **160.3 kg（+3.61%）**。

**两个未消除的变量，故不构成"Uno 算法强于 SNOPT"的证明**：

1. **发射日差 17 天**（同窗口、同 C3 区间、同 TOF，但行星相位不同 → 几何不同）
2. **Uno 的搜索预算更短**（5400 s）——这一项使结果**偏保守**

**可确认的**：把内层求解器从 SNOPT 换成 Uno 后，能在同等设置下产出**质量不低于** SNOPT 基准的
可行解，即替换在功能与解质量上均成立。

### 5.5 ESFO_Uranus 二级任务实测（真实工程流水线）

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

## 6. 发行包 `Souffle\`

### 6.1 双击 = 打开图形界面

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

### 6.2 可搬迁设计

GUI 需要**绝对路径**（`PyEMTG.options`），而包要能整体搬走——两者本质冲突。
解法：**启动器每次启动时按自身位置重新生成 `PyEMTG.options`**。
包内 `.emtgopt` 则全部使用相对路径（`../Universe_EVVEU`、`../HardwareModels/`、`../results`），
由 EMTG 按其工作目录解析（启动器以 `bin\` 为工作目录）。

### 6.3 目录结构

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

**不需要 SNOPT 许可证**：`bin\snopt7.dll` 存在只是因为 `EMTGv9.exe` 硬导入它；
求解器为 Uno 时从不进入 SNOPT 代码路径（已实测：不设 `SNOPT_LICENSE` 仍完整跑完）。

### 6.4 打包

```powershell
powershell -File Souffle_Cheese\package\package_unomtg.ps1 -Force
```

脚本流程：可执行文件 → Uno 运行时 → 完整 `Universe` → `HardwareModels` → 算例（路径重定向）
→ 自带 Python + PyEMTG → 许可。并自检 `PyEMTG.options` 全部目标存在、包内 `.emtgopt`
无机器相关绝对路径。`-SkipGUI` 可产出不含 GUI 的精简包（仅直接运行模式）。

---

## 7. 命名迁移记录

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
  > 注意：探测阶段（宽松时间 + 无种子）首轮 NLP 发散会正常触发它并留下 `.SNOPTcrash` 文件，
  > **这不代表真的调用了 SNOPT**。

---

## 8. 已知问题与注意事项

| 问题 | 说明 |
|---|---|
| `.SNOPTcrash` 文件名 | 见 §7，属历史命名，触发与求解器选择无关 |
| 长时管道下退出码不可靠 | 二级驱动脚本已改为 `*> $log` 文件重定向 + `PYTHONUNBUFFERED=1`；此前 30 分钟运行配合管道会给出虚假的 `exit=-1` 并丢失全部缓冲输出 |
| `ESFO_Uranus` 的 `config.EMTG_BIN` | 硬编码指向原版 EMTG，无环境变量覆盖。用 `tier2_driver.py` 在内存中覆盖，**不修改该工程** |
| `SOUFFLE_UNO_PRESET` 大小写 | 值必须大写，否则被静默拒绝 |
| 探测阶段的 `SNOPTcrash` | 首轮 NLP 发散的正常副产物 |
| **写文件务必指定 UTF-8** | 本机 PowerShell 默认 ANSI(GBK)，用 `Set-Content` 不带 `-Encoding UTF8` 会**永久破坏** UTF-8 中文。见 §11 |

---

## 9. 开发计划与设计文档

| 文档 | 内容 |
|---|---|
| [`docs/PLAN.md`](docs/PLAN.md) | 阶段划分、硬约束、只读保护、风险登记册、工具链速查 |
| [`docs/DESIGN.md`](docs/DESIGN.md) | 动态加载层、求解器接口与 EMTG 的逐项映射、回调处理 |
| [`docs/uno-2.9.0-options.txt`](docs/uno-2.9.0-options.txt) | Uno 全部选项名与类型（实测导出） |
| `_probe\` | 冒烟测试、探针、构建与运行驱动脚本 |

---

## 10. 许可

- EMTG：NASA Open Source Agreement 1.3（见 `EMTG_NOSA_License.pdf`）
- Uno：MIT
- 第三方：见 `Souffle\licenses\`

---

## 11. 附：本项目曾发生的一次文档损坏事故（供后续参考）

在批量重命名过程中，用 PowerShell 的 `Get-Content -Raw` + `Set-Content -NoNewline`
（未指定 `-Encoding`）处理含中文的 Markdown，导致 UTF-8 字节被按 ANSI(GBK) 解码后又写回，
**三个文档的中文内容永久损坏**（既非有效 UTF-8 也非有效 GBK；反向恢复尝试恢复出的 CJK 字符数为 0），
且项目无版本控制可回滚。

**教训与规则**

- 读写 UTF-8 文本一律显式指定编码：`-Encoding UTF8`，或改用能保证 UTF-8 的工具
- 批量文本替换前先备份
- 事后用脚本校验：**凡含非 ASCII 的文件，必须能被 UTF-8 解码**
  （`_probe\find_encoding_damage.py` 即为此用途）

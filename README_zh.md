# SOUFFLE — Scalable Optimization Uno-powered Framework For Leveraging EMTG

[![License](https://img.shields.io/badge/License-NASA%20NOSA%201.3-blue.svg)](https://opensource.org/license/nasa1-3-php)
[![Solver](https://img.shields.io/badge/solver-Uno%20%7C%20Ipopt-lightgrey)](https://github.com/cvanaret/Uno)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)](https://www.microsoft.com/en-us/windows)
[![Built with](https://img.shields.io/badge/built%20with-MSVC%20%2B%20NMake-lightgrey)](https://visualstudio.microsoft.com/)

把 NASA 的 [EMTG](https://github.com/nasa/EMTG) 的内层非线性优化器，从商业求解器 **SNOPT**
换成开源求解器：默认 **[Uno](https://github.com/cvanaret/Uno)**，同时集成
**[Ipopt](https://github.com/coin-or/Ipopt)**。MGALT 转录不变、任务建模不变、输出不变，
**且不再需要任何商业许可证**。

> English docs: [README.md](README.md)

| 组成 | 说明 |
|---|---|
| **求解器** | [Uno](https://github.com/cvanaret/Uno) ≥ 2.9.0（默认预设 `filtersqp`；Uno 自身另有 `ipopt`、`funnel`、`Penalty` 预设）与 [Ipopt](https://github.com/coin-or/Ipopt) 3.14。`united/` 会同时跑两者并取较优解。 |
| **转录方式** | EMTG MGALT + MBH 全局搜索，与原版完全一致 |
| **图形界面** | PyEMTG（wxPython），与 SNOPT 版使用的是同一个界面 |
| **默认构建** | **不需要 SNOPT 安装、不引用 SNOPT 头文件、可执行文件不导入 `snopt7.dll`** |

## 为什么要做这个

SNOPT 是商业软件：试用许可会到期，其运行时也不能再分发。而 EMTG 树内没有第二个 NLP 求解器，
所以许可一到期，工具就无法使用。

SOUFFLE 在不触碰 EMTG 的物理模型、转录方式和任务定义的前提下，去掉这个依赖——新增的只有求解器接口。

## 特性

- **可替换的求解器** —— 内层位于抽象基类 `NLP_interface` 之后；SNOPT 与 Uno 是两种可互换实现，
  用环境变量选择。
- **SNOPT 变成可选项** —— `SOUFFLE_WITH_SNOPT` 默认为 `OFF`。该配置下 CMake 不要求 SNOPT 目录、
  不引用任何 SNOPT 头文件、链接出的可执行文件也不导入 `snopt7.dll`。
- **与 Uno 之间同样没有链接期依赖** —— Uno 的 Windows 发行包是 MinGW 构建的，MSVC 无法链接，
  因此 SOUFFLE 用 `LoadLibraryExW` + `GetProcAddress` 调用它；构建时只需要 `Uno_C_API.h`。
- **Ipopt 作为第二种内层求解器** —— 正常链接，因为它的导入库是 MSVC 可用的 COFF 归档。
  用 `SOUFFLE_NLP_SOLVER=IPOPT` 选择。
- **联合寻优** —— 冷启动的 Uno 与 Ipopt 会落在**不同盆地**，因此 `united/` 把两者（外加一个
  用 Uno 解热启动的 Ipopt）作为**独立进程**同时跑，取最优解。四个任务族、130 个算例实测：
  末质量平均比"Uno 与冷 Ipopt 各自取优"再多约 **19 kg**，比 Uno 单独多约 **87 kg**。
  它同时**比 SNOPT 版快得多**：单次求解上限从 60 s 降到 15 s / 8 s，一次运行约 62 s，
  而出厂算例设置最长 900 s。并行那一路还**不额外花时间**：Uno 要 62 s、Ipopt 要 33 s，
  但一起跑仍是 62 s（Ipopt 藏在 Uno 后面）。之所以必须独立进程，是因为两个求解器各自携带**同名但不同工具链编译的 MinGW 运行时**，
  而 Windows 按名字解析 DLL。
- **同一个图形界面** —— PyEMTG 可编辑并运行 `.emtgopt`、绘制 `.emtg` 结果，与 SNOPT 版一致。
- **同一套物理模型** —— MGALT、前向/后向打靶、匹配点约束、journey/phase 树、
  Monotonic Basin Hopping 全局搜索，全部原样保留。

## 安装

### 1. 使用打包好的二进制

**[从 Releases 页面下载 `Souffle.zip` »](../../releases/latest)**

解压到任意位置，双击 `run_souffle.bat` 即可。整个文件夹自包含：可执行文件、Uno 运行时、
**带独立 Python 解释器的 PyEMTG 图形界面**、SPICE 星历与硬件模型都在里面 ——
无需安装任何东西，也无需设置环境变量。

| 我想要… | 操作 |
|---|---|
| **打开图形界面** | 双击 `run_souffle.bat` |
| 不开界面跑一个算例 | `run_souffle.bat EVVEU_LTGA.emtgopt` |

### 2. 从源码构建

需要 Visual Studio 2022（MSVC v143）、CMake、Windows SDK 与 Boost；Uno 与 SPICE 星历需另行获取。

**完整的分步说明（含 Boost / Uno / `de440s.bsp` 的下载地址）见 [BUILDING.md](BUILDING.md)。**
简要版本：

```powershell
cmake -S . -B build -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release `
    -DSOUFFLE_WITH_UNO=ON `
    -DSOUFFLE_UNO_ROOT=<Uno 路径> `
    -DSOUFFLE_DEFAULT_SOLVER=Uno `
    -DBOOST_ROOT=<Boost 路径>
cmake --build build          # 产物: build\src\EMTGv9.exe
```

命令中没有任何 SNOPT 路径。

### 编译开关

| 选项 | 默认 | 含义 |
|---|---|---|
| **`SOUFFLE_WITH_SNOPT`** | **`OFF`** | 是否编译 SNOPT 接口。关闭 ⇒ 不要求 SNOPT 目录、不引用 SNOPT 头文件、不导入 `snopt7.dll` |
| `SOUFFLE_WITH_UNO` | `ON` | 是否编译 Uno 接口 |
| `SOUFFLE_UNO_ROOT` | — | Uno 安装目录（含 `include/uno`、`bin`、`deps`） |
| `SOUFFLE_DEFAULT_SOLVER` | `Uno` | 未设 `SOUFFLE_NLP_SOLVER` 时使用的求解器 |

## 使用

### 图形界面

双击启动器即打开 PyEMTG，它已通过 `PyEMTG\PyEMTG.options` 接好本包的求解器。
启动器**每次启动都会按自身位置重新生成**该文件——这正是整个文件夹可以随意搬移的原因。

### 命令行

```bat
set SOUFFLE_NLP_SOLVER=Uno
set SOUFFLE_UNO_ROOT=<Uno 路径>
EMTGv9.exe my_case.emtgopt
```

| 环境变量 | 默认 | 含义 |
|---|---|---|
| `SOUFFLE_NLP_SOLVER` | `Uno` | `Uno`；若构建时启用了 SNOPT，也可填 `SNOPT` |
| `SOUFFLE_UNO_ROOT` | 配置时写入 | `libuno.dll` 所在目录 |
| `SOUFFLE_UNO_PRESET` | `filtersqp` | Uno 预设：filtersqp、ipopt、funnel、Penalty |

无需重新编译即可切换到内点法：

```bat
set SOUFFLE_UNO_PRESET=ipopt
EMTGv9.exe my_case.emtgopt
```

---

# 技术细节

## 设计

本节说明 **求解器如何接入 EMTG**：动态加载层、求解器接口与 EMTG 数据结构之间的逐项映射、
回调与终止条件的处理，以及开发过程中确认的关键约束。

---

### 1. 总体结构

```
EMTG 内层 (MBH / FilamentWalker / problem.cpp)
        │  只依赖抽象基类
        ▼
   NLP_interface                ← 抽象基类（本次新增虚析构 + getInform()）
        │
        ├── SNOPT_interface     ← 原有，保留
        └── Souffle_interface   ← 本次新增，实现 Uno 映射
                │
                ▼
           SouffleApi           ← 动态加载层（LoadLibraryExW + GetProcAddress）
                │
                ▼
           libuno.dll           ← Uno 2.9.0（MinGW 构建，C ABI）
```

**为什么必须动态加载**：原版 EMTG 用 MSVC 构建，而 Uno 的 Windows 发行包是 MinGW 格式
（导入库为 `libuno.dll.a`），MSVC 链接器无法直接链接。由于 `libuno.dll` 导出的是
**未修饰的 C 符号**，C ABI 跨编译器稳定，因此改用运行时 `LoadLibraryExW` + `GetProcAddress`，
**不链接任何 Uno 库**。

**代价**：需要运行时定位 DLL（`SOUFFLE_UNO_ROOT`），且缺少编译期符号校验，
因此启动时必须显式报错而非静默失败。

---

### 2. 解耦改动（对原 EMTG 的最小侵入）

| 文件 | 改动 |
|---|---|
| `NLP_interface.h` | 新增 `virtual ~NLP_interface() {}`（原缺虚析构，多态删除会 UB）；把 SNOPT 专有的 `getInform()` 提为基类虚函数，默认返回 `99` |
| `SNOPT_interface.h` | `getInform()` 标记 `override` |
| `monotonic_basin_hopping.h/.cpp` | `SNOPT_interface*` → `NLP_interface*`（构造函数、`initialize`、成员） |
| `FilamentWalker.h/.cpp` | 同上 |
| `NLPoptions.h/.cpp` | 新增 `solver_name`，默认值来自 CMake `SOUFFLE_DEFAULT_SOLVER` |
| `NLP_solver_factory.h/.cpp` | **新增**：`create_NLP_solver()` 返回 `std::unique_ptr<NLP_interface>` |
| `problem.cpp` | 三处实例化点（MBH / NLP / FilamentWalker）改走工厂 |

**原则**：原 EMTG 的所有行为在 `SOUFFLE_NLP_SOLVER=SNOPT` 时必须保持不变。
同一个可执行文件内保留 SNOPT 路径，便于对照。

---

### 3. 缩放契约

EMTG 的 NLP 变量是**缩放后**的。Uno 看到的上下界必须重算：

```
Uno.lower = 0
Uno.upper = (X_upperbounds[i] - X_lowerbounds[i]) / X_scale_factors[i]
```

反缩放（把 Uno 的解还原给 EMTG）：

```
X_unscaled[i] = X_scaled[i] * X_scale_factors[i] + X_lowerbounds[i]
```

推导依据：EMTG 内部以 `X_unscaled = X_scaled * scale + Xlower` 定义缩放，
因此缩放下界为 0、上界为 `(Xu - Xl) / scale`。

**踩坑**：早期直接把 `X_upperbounds` 传给 Uno，导致所有变量上界偏大 `scale` 倍，
求解器在物理无意义的区域内搜索，表现为"总能找到解但约束很差"。

---

### 4. 约束与雅可比映射

EMTG 把约束分成若干段（F 向量），并提供**稀疏雅可比** `G`。Uno 的 C API 需要：

| Uno 需要的量 | 来源 |
|---|---|
| 约束个数 | `myProblem->F.size()` |
| 约束上下界 | `F_lowerbounds` / `F_upperbounds` |
| 雅可比非零元数 | `G.size()` 或按 `constraint_jacobian_source` 结构体 |
| 行/列索引 | `iGfun` / `jGvar`（EMTG 已提供） |

**关键**：Uno 会对雅可比做**压缩**（去掉显式的零元）。因此回调中必须按
`constraint_jacobian_source` 的映射把 Uno 的紧凑索引还原回 EMTG 的 `G` 位置，
否则雅可比错位，求解器仍能"跑完"但收敛到错误点。

---

### 5. Uno 接口选项

| 选项 | 取值 | 原因 |
|---|---|---|
| `logger` | `"SILENT"` | **必须大写**。小写 `"silent"` 被静默拒绝并回退默认，产生 22 MB / 373,772 行日志，运行时间被 I/O 主导 |
| `dual_tolerance` | ≤ `1e-4` | 实测超过该值 Uno 拒绝 |
| `max_iterations` | `2 × major_iterations_limit` | EMTG 的"major iteration"语义与 Uno 不同，需放宽 |
| `time_limit` | `max_run_time_seconds` | 与 EMTG 配置一致 |
| solver preset | `SOUFFLE_UNO_PRESET`，默认 `filtersqp` | 非法值需**显式警告后回退**，不能静默 |

#### 5.1 终止回调：**不要注册**

`uno_set_solver_callbacks(..., notify, nullptr, this)` —— 第 3 个参数（termination callback）
**必须传 `nullptr`**。

**原因（实测）**：注册终止回调后，每次求解都返回 `opt_status=5`（`UNO_USER_TERMINATION`），
且 `iters=1` —— 即第一次迭代就被终止。诊断显示回调内
`elapsed=0 limit=60 stop_on_goal=0`，且它从未返回非零值，说明是 Uno 侧对该回调的调用约定
与我们的实现不匹配。

改为传 `nullptr` 后，迭代数从 1 直接升到 16000，`filtersqp` 的成功率变为 111/138。

> 终止条件改由 `time_limit` 与 `max_iterations` 控制，不再依赖回调。

---

### 6. 工厂与环境变量

```cpp
std::unique_ptr<NLP_interface> create_NLP_solver();
```

选择逻辑：

1. 读环境变量 `SOUFFLE_NLP_SOLVER`
2. 未设置时用编译期默认值（CMake `SOUFFLE_DEFAULT_SOLVER`，本包为 `Uno`）
3. 值为 `Uno` → `Souffle_interface`；`SNOPT` → `SNOPT_interface`；其他 → 报错退出

`Souffle_interface` 内部再读 `SOUFFLE_UNO_ROOT` 定位 `libuno.dll`，
未设置时回退到编译期宏 `SOUFFLE_UNO_ROOT_DEFAULT`。

---

### 7. 日志与可观测性

求解器诊断行：

```
SOUFFLE[solve]: preset=filtersqp opt_status=4 sol_status=0 iters=2012 cpu=23.79 f=-0.864531
```

**这一行不受 `quiet_NLP` 控制**。原因：`quiet_NLP 1` 会吞掉 EMTG 自身的求解输出，
导致无法判断 Uno 是否在工作（曾因此误判"求解器没跑"）。因此该诊断行无条件输出。

字段含义：

| 字段 | 说明 |
|---|---|
| `opt_status` | Uno 的 `UNO_OPTIMIZATION_STATUS`：`2` = 成功，`4` = 迭代上限，`5` = 用户终止 |
| `sol_status` | `UNO_SOLUTION_STATUS`：`0` = 可行 |
| `iters` | 迭代数（正常应为数百至数千；若为 1 说明被终止回调打断） |
| `cpu` | 求解 CPU 秒 |
| `f` | 目标函数值 |

---

### 8. 已知残留

| 项 | 位置 | 原因 |
|---|---|---|
| 成员名 `mySNOPT` | `monotonic_basin_hopping.*`、`FilamentWalker.*`、`problem.cpp` | 类型已改为 `NLP_interface*`，但成员名沿用。纯命名问题，改动面大且无功能收益 |
| 文本 `"SNOPT has crashed ..."` | `monotonic_basin_hopping.cpp:362` | MBH 的 try-catch 兜底分支，触发条件是**求解抛异常**，与求解器选择无关 |
| 文件名 `.SNOPTcrash` | 同上，`:366` / `:371` | 改它会影响按该文件名做归档/扫描的外部流程 |

---

### 9. 复现与验证入口

| 脚本 | 用途 |
|---|---|
| `_probe\smoke_uno.c` | MinGW 侧 Uno C API 冒烟测试 |
| `_probe\msvc_probe.c` | MSVC 动态加载冒烟测试（架构可行性验证） |
| `_probe\dump_options.c` | 导出 Uno 全部选项名与类型 |
| `_probe\build_uno.ps1` | 配置 + 编译（Uno 版） |
| `_probe\run_build.ps1` | 仅增量编译 |
| `_probe\gui_window_check.py` | 按窗口标题验证 GUI 是否真的打开 |
| `_probe\find_encoding_damage.py` | 校验含非 ASCII 的文件是否为有效 UTF-8 |

---

### 10. 联合寻优（united）

单个求解器会停在它先走到的那个局部盆地。Uno 与 Ipopt 的起步方向、信赖域策略和停止判据都不同，
**实测它们落点不同**，因此同时跑两者、取较优解，比任何一个单独跑都划算。

#### 10.1 为什么必须是两个进程

Uno 与 Ipopt 各自携带**同名但不同工具链编译的 MinGW 运行时**
（`libwinpthread-1.dll`、`libgcc_s_seh-1.dll`、`libgomp-1.dll`、`libatomic-1.dll`、
`libquadmath-0.dll`）。Windows 按**名字**解析模块，先加载的一方占住名字之后，另一方的延迟
加载就会解析到错误版本——同进程方案实测在 Ipopt 第一次迭代即以
`0xc06d007f`（`ERROR_DELAY_LOAD_FAILED`）退出。调整 `PATH` 顺序只是把故障转移给另一方。

所以联合寻优由 `united/united.py` 编排**独立进程**，而不是在同一个 `run_NLP` 里切换求解器。

#### 10.2 两种编排

| `--mode` | 编排 | 单任务墙钟 |
|---|---|---|
| `parallel` | Uno ‖ 独立 Ipopt → 取优 | ≈ 62 s |
| `both`（默认） | 上面两路，再以 **Uno 的解**热启动一次 Ipopt → 三者取优 | ≈ 100 s |

`both` 值得作默认：冷启动的 Ipopt 与热启动的 Ipopt **落在不同盆地**，三者取优相对
`max(Uno, 独立 Ipopt)` 平均多拿约 **19 kg**，代价只是多串一段 Ipopt。

#### 10.3 墙钟由单次求解上限决定

墙钟 ≈ `max(MBH 预算, 单次求解上限)`，因为 MBH 打断不了进行中的求解。真正的旋钮是
**单次求解上限**，不是总预算。

#### 10.4 一个二进制、两种求解器

发行包只带一个 `SOUFFLE.exe`。`united.py` 通过 `SOUFFLE_NLP_SOLVER` 决定每一路跑哪个求解器，
`SOUFFLE_IPOPT_LIB` 指向 `ipopt-3.dll` 所在目录。算例里的数据路径由 `geometry.py`
按包根**绝对化**，因为联合寻优在各自的阶段目录里运行可执行文件，而算例里的路径原本是相对
`bin/` 的。

---

## 开发与验证记录

SOUFFLE 基于 NASA 的 [EMTG](https://github.com/nasa/EMTG)（Evolutionary Mission Trajectory Generator），
把内层非线性优化器从 **SNOPT** 换成开源求解器 **[Uno](https://github.com/cvanaret/Uno)**，
以便在 SNOPT 试用许可到期后继续使用。



### 1. 可行性依据

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

### 2. 工具链与架构决策

#### 2.1 工具链事实

**原版 EMTG 是用 MSVC + NMake 构建的**，不是 MinGW：

- `EMTG\build\CMakeCache.txt` → `CMAKE_CXX_COMPILER = D:\Visual_Studio\VC\Tools\MSVC\14.40.33807\bin\Hostx64\x64\cl.exe`
- `CMAKE_GENERATOR = NMake Makefiles`
- Boost 用 `libboost_filesystem-vc143-*`（MSVC ABI）
- cmake 用 VS 自带：`D:\Visual_Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- Windows SDK（参考构建使用 `10.0.28000.0`，亦可用便携版）

而 **Uno 的 Windows 预编译包是 MinGW 格式**（`libuno.dll.a`），MSVC 链接器无法直接链接。

#### 2.2 决策：保持 MSVC，通过**动态加载**调用 Uno

由于 `libuno.dll` 导出的是**未修饰 C 符号**，C ABI 跨编译器稳定，因此用
`LoadLibraryExW` + `GetProcAddress` 驱动 Uno，**不链接任何 Uno 库**。

**收益**

- Boost vc143、SPICE、SNOPT、Windows SDK 全部沿用，零迁移成本
- 同一个可执行文件内即可保留 SNOPT 对照路径
- 不需要 MinGW 工具链参与主构建

**代价**

- 需要在运行时定位 `libuno.dll`（由 `SOUFFLE_UNO_ROOT` 指定）
- 缺少编译期符号校验，靠启动时的显式报错兜底

#### 2.3 代码结构

| 文件 | 作用 |
|---|---|
| `src\InnerLoop\SouffleApi.h/.cpp` | 动态加载层：`LoadLibraryExW` + `GetProcAddress`，74 个 C API 调用桩 |
| `src\InnerLoop\Souffle_interface.h/.cpp` | 求解器接口：实现 `NLP_interface`，把 EMTG 的模型/边界/雅可比映射到 Uno |
| `src\InnerLoop\NLP_interface.h` | 求解器抽象基类（原为 SNOPT 专有，已抽出虚析构与 `getInform()`） |
| `src\InnerLoop\NLP_solver_factory.h/.cpp` | 工厂：按 `SOUFFLE_NLP_SOLVER` 返回 `std::unique_ptr<NLP_interface>` |
| `src\InnerLoop\NLPoptions.h/.cpp` | 新增 `solver_name`，默认值来自 CMake |

#### 2.4 环境变量

| 变量 | 默认 | 说明 |
|---|---|---|
| `SOUFFLE_NLP_SOLVER` | `Uno` | 选 `Uno` 或 `SNOPT`。**SNOPT 仅在编译时开启 `SOUFFLE_WITH_SNOPT=ON` 才可用**；默认构建只含 Uno |
| `SOUFFLE_UNO_ROOT` | 编译期内置 | Uno 安装目录（含 `bin\`、`deps\`） |
| `SOUFFLE_UNO_PRESET` | `filtersqp` | 也可设 `ipopt` / `funnel` / `Penalty` |

> ⚠️ `SOUFFLE_UNO_PRESET` 的值**必须大写**（例如 logger 用 `SILENT`）。
> 小写会被 Uno 静默拒绝并回退默认，曾导致 22 MB / 373,772 行日志、运行时间被 I/O 主导。

---

### 3. 验证结果

#### 3.1 求解器 preset A/B 实测

| preset | 收敛率 | 可行解 |
|---|---|---|
| `filterSQP`（默认） | **111/138 `UNO_SUCCESS`** | 有 |
| `ipopt` | 33/263 | 无 |

由表格可知，IPOPT预设性能并不突出、FilterSQP预设性能更优。

Uno的IPOPT预设是指使用IPOPT相同的算法下进行优化，与原生IPOPT的性能极其相似（见Uno官方文档），所以也证明了IPOPT对EMTG的适配性不强。

其原因可能是IPOPT在序列固定、变量少、内点法在初始可行时才有很好的性能，但LTGA广泛寻优不具备这类条件。而SQP/filter 对不可行初始点更鲁棒，且和 EMTG 的解析梯度、稀疏结构天然匹配。

#### 3.2 与 SNOPT 的同口径对比（2044 窗口，E-V-V-E-U）

**必须所有设置对齐才可比。**

对齐后（`run_evveu\RETRY_t40_aligned.emtgopt`，与基准算例的选项逐项一致）：

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

#### 3.3 天王星行星际转移序列广泛寻优实测（真实工程流水线）

用一条**独立的**二级流水线（`code\emtg_pipeline.py`）、**独立的** case 定义
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

驱动脚本：`tier2_driver.py`（**不修改被驱动的工程**，通过覆盖 `config.EMTG_BIN` 实现）。

---

### 4. 发行包 `Souffle\`

#### 4.1 双击 = 打开图形界面

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

#### 4.2 可搬迁设计

GUI 需要**绝对路径**（`PyEMTG.options`），而包要能整体搬走——两者本质冲突。
解法：**启动器每次启动时按自身位置重新生成 `PyEMTG.options`**。
包内 `.emtgopt` 则全部使用相对路径（`../Universe_EVVEU`、`../HardwareModels/`、`../results`），
由 EMTG 按其工作目录解析（启动器以 `bin\` 为工作目录）。

#### 4.3 目录结构

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

#### 4.4 打包

```powershell
powershell -File package\package_souffle.ps1 -Force
```

脚本流程：可执行文件 → Uno 运行时 → 完整 `Universe` → `HardwareModels` → 算例（路径重定向）
→ 自带 Python + PyEMTG → 许可。并自检 `PyEMTG.options` 全部目标存在、包内 `.emtgopt`
无机器相关绝对路径。`-SkipGUI` 可产出不含 GUI 的精简包（仅直接运行模式）。

---

### 5. 已知问题与注意事项

| 问题 | 说明 |
|---|---|
| `.SNOPTcrash` 文件名 | 见 §7，属历史命名，触发与求解器选择无关 |
| 长时管道下退出码不可靠 | 二级驱动脚本已改为 `*> $log` 文件重定向 + `PYTHONUNBUFFERED=1`；此前 30 分钟运行配合管道会给出虚假的 `exit=-1` 并丢失全部缓冲输出 |
| 外部工程的 `config.EMTG_BIN` | 硬编码指向原版 EMTG，无环境变量覆盖。用 `tier2_driver.py` 在内存中覆盖，**不修改该工程** |
| `SOUFFLE_UNO_PRESET` 大小写 | 值必须大写，否则被静默拒绝 |
| 探测阶段的 `SNOPTcrash` | 首轮 NLP 发散的正常副产物 |
| **写文件务必指定 UTF-8** | 本机 PowerShell 默认 ANSI(GBK)，用 `Set-Content` 不带 `-Encoding UTF8` 会**永久破坏** UTF-8 中文。见 §11 |

---

### 6. 联合寻优实测

SOUFFLE 现在带**两个求解器**（Uno 与 Ipopt）。它们**不能同进程运行**：两者各自携带同名但不同工具链编译的 MinGW 运行时，Windows 按名字解析 DLL，后加载的一方会以 `0xc06d007f` 失败。因此联合寻优把两者作为**独立进程**编排。

#### 6.1 为什么取三者之优

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

#### 6.2 实测一次（发行包、`--mode parallel`）

```
完成 1 个，出解 1 个，总用时 69 s
取优来源分布: {'uno': 1}
末质量 4698.394 kg   墙钟 69.1 s
  uno    ok  wall_s=69.1   SOUFFLE[solve]: preset=filtersqp opt_status=2 sol_status=0 iters=5721
  ipopt  ok  wall_s=36.7   SOUFFLE[ipopt-solve]: status=0 inform=1 iters=781 callbacks=782
```

---

## 附录：Uno 2.9.0 选项

```
# Uno 2.9.0 options
# type: 0=integer 1=double 2=bool 3=string
2	MA57_use_scaling
3	libhsl_path
0	SOC_max_iterations
3	option_file
3	QP_solver
3	BQPD_kmax_heuristic
1	least_square_multiplier_max_norm
1	barrier_damping_factor
1	barrier_push_variable_to_interior_k2
1	barrier_small_infeasibility_factor
1	barrier_push_variable_to_interior_k1
1	barrier_small_direction_factor
1	barrier_regularization_exponent
1	barrier_k_sigma
3	barrier_function
1	l1_constraint_violation_coefficient
1	TR_decrease_factor
1	TR_increase_factor
1	TR_radius
1	primal_regularization_fast_increase_factor
1	primal_regularization_lb
1	switching_objective_exponent
0	quasi_newton_memory_size
1	barrier_initial_parameter
1	switching_delta
3	linear_solver
3	LP_solver
1	barrier_theta_mu
0	threshold_unsuccessful_attempts
2	funnel_require_acceptance_wrt_current_iterate
1	armijo_tolerance
1	TR_radius_reset_threshold
3	inertia_correction_strategy
3	progress_norm
3	globalization_strategy
1	barrier_default_multiplier
3	globalization_mechanism
1	protected_actual_reduction_macheps_coefficient
2	print_subproblem
1	primal_regularization_slow_increase_factor
1	primal_tolerance
3	preset
1	residual_scaling_threshold
1	armijo_decrease_fraction
1	SOC_infeasibility_fraction
1	TR_aggressive_decrease_factor
0	filter_capacity
1	dual_regularization_fraction
1	primal_regularization_decrease_factor
1	dual_tolerance
1	TR_activity_tolerance
3	residual_norm
1	loose_dual_tolerance
1	funnel_fact
1	loose_primal_tolerance
1	function_scaling_threshold
1	filter_beta
2	switch_to_optimality_requires_linearized_feasibility
0	max_iterations
1	time_limit
0	loose_tolerance_iteration_threshold
1	filter_fact
3	constraint_relaxation_strategy
2	protect_actual_reduction_against_roundoff
1	funnel_kappa
3	logger
2	use_function_scaling
1	diverging_iterate_threshold
1	barrier_k_epsilon
1	barrier_k_mu
1	unbounded_objective_threshold
0	max_number_filter_resets
1	gamma_alpha
1	switching_infeasibility_exponent
1	sufficient_infeasibility_decrease_ratio
3	filter_type
3	inequality_handling_method
1	filter_gamma
1	TR_min_radius
1	filter_ubd
1	filter_sufficient_infeasibility_decrease_factor
0	filter_reset_iteration_threshold
1	funnel_beta
1	funnel_gamma
2	print_solution
1	funnel_ubd
3	hessian_model
0	funnel_update_strategy
1	LS_backtracking_ratio
2	LS_scale_duals_with_step_length
1	quasi_newton_delta_lower_bound
2	write_solution_to_file
1	quasi_newton_delta_upper_bound
0	LBFGS_max_skips_before_reset
1	regularization_increase_factor
1	LSR1_pivot_max_magnitude
1	barrier_tau_min
1	regularization_failure_threshold
1	barrier_update_fraction
1	primal_regularization_initial_factor
# total 100 options
```

---

## 许可

本项目基于 NASA 开源协议（NOSA 1.3）发布，与上游 EMTG 一致。完整条款见
[LICENSE](LICENSE) 与 [EMTG_NOSA_License.pdf](EMTG_NOSA_License.pdf)。

Uno 以 MIT 协议发布，Ipopt 以 EPL 1.0 发布；两者的许可证文本随发行包一并分发。

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

## 架构

```
EMTG 内层 (MBH / FilamentWalker / problem.cpp)
        │  只依赖抽象基类
        ▼
   NLP_interface
        ├── SNOPT_interface        仅当 SOUFFLE_WITH_SNOPT=ON 时编译
        └── Souffle_interface      Uno 映射
                │
                ▼
           SouffleApi              LoadLibraryExW + GetProcAddress
                │
                ▼
           libuno.dll              Uno 2.9.0，MinGW 构建，C ABI 稳定
```

**为什么必须动态加载**：MSVC 无法链接 Uno 的 MinGW 导入库，但 `libuno.dll` 导出的是未修饰的
C 符号，C ABI 跨编译器稳定。代价是 DLL 必须在运行时定位（`SOUFFLE_UNO_ROOT`），
而不是在链接期解析。

改这个接口时注意两处映射约定：

- **缩放**：EMTG 使用缩放后的变量。传给 Uno 的是 lower = 0 、upper = (X_upper − X_lower) / X_scale，结果按 X_unscaled = X_scaled · X_scale + X_lower 还原。
- **刻意不注册终止回调**：注册后每次求解都返回 `opt_status=5`（`UNO_USER_TERMINATION`）
  且只迭代 1 次；改传 `nullptr` 后迭代数从 1 升到 16000。

完整的接口映射见 [docs/DESIGN.md](docs/DESIGN.md)。

## 验证

### "不需要 SNOPT" 这一条

| 检查 | 结果 |
|---|---|
| 对默认构建执行 `dumpbin /imports` | 导入 14 个 DLL，**不含 `snopt7.dll`**（启用 SNOPT 的构建为 15 个，含它） |
| 在**目录内没有任何名字含 `snopt` 的文件**、且未设 `SNOPT_LICENSE` 的情况下运行 | 正常走到 `EMTG run complete` 并完成求解 |

### 求解器预设

在 EVVEU LTGA 算例上实测：

| 预设 | 收敛率 |
|---|---|
| `filtersqp`（默认） | **111 / 138** 次 `UNO_SUCCESS` |
| `ipopt` | 33 / 263 |

### 与 SNOPT 基准的对比

同一 2044 发射窗口、同一序列、同一初始质量（5305 kg）、同一 `num_timesteps`（40）、
同一容差（1e-6 / 1e-5）：

| | SOUFFLE / Uno | SNOPT 基准 |
|---|---|---|
| 末质量 | **4598.48 kg** | 4438.15 kg |
| 电推进剂 | **706.52 kg** | 866.85 kg |

同样的对比换到外部流水线上——用 SOUFFLE 驱动 ESFO_Uranus 的二级 MGALT 流水线——得到
**4150.98 kg**，其 SNOPT 基准为 **4150.07 kg**。

完整数据见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)。

## 目录结构

```
src/                    EMTG 源码 + SOUFFLE 新增部分
  InnerLoop/
    NLP_interface.h         求解器抽象
    NLP_solver_factory.*    运行时选择 SNOPT 或 Uno
    SouffleApi.*            动态加载 libuno.dll
    Souffle_interface.*     EMTG <-> Uno 的模型/边界/雅可比映射
    SNOPT_interface.*       上游代码，仅 SOUFFLE_WITH_SNOPT=ON 时编译
PyEMTG/                 PyEMTG 图形界面（wxPython）
package/                打包脚本、启动器模板、GUI 配置模板
HardwareModels/         推力器 / 电源 / 推进系统库
Universe*/              宇宙定义（SPICE 星历需另行获取）
run_evveu/              示例 .emtgopt 算例
docs/                   DEVELOPMENT.md、DESIGN.md、PLAN.md、Uno 选项清单
BUILDING.md             构建指南
tier2_driver.py         用 SOUFFLE 驱动外部 EMTG 流水线
```

## 注意事项

| 现象 | 原因 |
|---|---|
| 一闪而过、没有任何输出 | `PATH` 中靠前的位置有陈旧或版本不符的 MinGW 运行时；把 Uno 的 `bin` 与 `deps` 放到最前 |
| 提示 `failed to load libuno.dll` | `SOUFFLE_UNO_ROOT` 未设置或指错 |
| 日志文件异常巨大 | Uno 的选项值区分大小写，小写会被静默拒绝并回退默认值 |
| 求解只迭代 1 次就结束 | 向 Uno 注册了终止回调（见"架构"一节） |

## 许可

SOUFFLE 是 NASA EMTG 的 fork，按相同条款分发：
**[NASA Open Source Agreement 1.3](https://opensource.org/license/nasa1-3-php)**。

Copyright © 2024 United States Government as represented by the Administrator of the National
Aeronautics and Space Administration. All Rights Reserved.

本项目**不包含** SNOPT，默认构建也不需要它。Uno 采用 MIT 许可。
完整的第三方组件清单见 [LICENSE](LICENSE)。

# SOUFFLE 设计文档

本文件说明 **Uno 求解器如何接入 EMTG**：动态加载层、求解器接口与 EMTG 数据结构之间的逐项映射、
回调与终止条件的处理，以及开发过程中确认的关键约束。

---

## 1. 总体结构

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

## 2. 解耦改动（对原 EMTG 的最小侵入）

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

## 3. 缩放契约

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

## 4. 约束与雅可比映射

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

## 5. Uno 接口选项

| 选项 | 取值 | 原因 |
|---|---|---|
| `logger` | `"SILENT"` | **必须大写**。小写 `"silent"` 被静默拒绝并回退默认，产生 22 MB / 373,772 行日志，运行时间被 I/O 主导 |
| `dual_tolerance` | ≤ `1e-4` | 实测超过该值 Uno 拒绝 |
| `max_iterations` | `2 × major_iterations_limit` | EMTG 的"major iteration"语义与 Uno 不同，需放宽 |
| `time_limit` | `max_run_time_seconds` | 与 EMTG 配置一致 |
| solver preset | `SOUFFLE_UNO_PRESET`，默认 `filtersqp` | 非法值需**显式警告后回退**，不能静默 |

### 5.1 终止回调：**不要注册**

`uno_set_solver_callbacks(..., notify, nullptr, this)` —— 第 3 个参数（termination callback）
**必须传 `nullptr`**。

**原因（实测）**：注册终止回调后，每次求解都返回 `opt_status=5`（`UNO_USER_TERMINATION`），
且 `iters=1` —— 即第一次迭代就被终止。诊断显示回调内
`elapsed=0 limit=60 stop_on_goal=0`，且它从未返回非零值，说明是 Uno 侧对该回调的调用约定
与我们的实现不匹配。

改为传 `nullptr` 后，迭代数从 1 直接升到 16000，`filtersqp` 的成功率变为 111/138。

> 终止条件改由 `time_limit` 与 `max_iterations` 控制，不再依赖回调。

---

## 6. 工厂与环境变量

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

## 7. 日志与可观测性

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

## 8. 已知残留

| 项 | 位置 | 原因 |
|---|---|---|
| 成员名 `mySNOPT` | `monotonic_basin_hopping.*`、`FilamentWalker.*`、`problem.cpp` | 类型已改为 `NLP_interface*`，但成员名沿用。纯命名问题，改动面大且无功能收益 |
| 文本 `"SNOPT has crashed ..."` | `monotonic_basin_hopping.cpp:362` | MBH 的 try-catch 兜底分支，触发条件是**求解抛异常**，与求解器选择无关 |
| 文件名 `.SNOPTcrash` | 同上，`:366` / `:371` | 改它会影响 ESFO_Uranus 的归档/扫描逻辑 |

---

## 9. 复现与验证入口

| 脚本 | 用途 |
|---|---|
| `_probe\smoke_uno.c` | MinGW 侧 Uno C API 冒烟测试 |
| `_probe\msvc_probe.c` | MSVC 动态加载冒烟测试（架构可行性验证） |
| `_probe\dump_options.c` | 导出 Uno 全部选项名与类型 |
| `_probe\build_uno.ps1` | 配置 + 编译（Uno 版） |
| `_probe\run_build.ps1` | 仅增量编译 |
| `_probe\gui_window_check.py` | 按窗口标题验证 GUI 是否真的打开 |
| `_probe\find_encoding_damage.py` | 校验含非 ASCII 的文件是否为有效 UTF-8 |
| `tier2_driver.py` | 用 SOUFFLE 驱动 ESFO_Uranus 二级流水线（不修改该工程） |

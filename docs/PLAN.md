# SOUFFLE 开发计划

记录阶段划分、硬约束、验证阶梯与风险登记册。已完成的条目保留，作为过程记录。

---

## 1. 硬约束

| 编号 | 约束 | 校验方式 |
|---|---|---|
| H1 | **原版 `EMTG\` 目录零改动**，一个字节都不能动 | 36,775 文件基线清单 + 逐文件 SHA 比对；见 §4 |
| H2 | 所有改动只落在 `Souffle_Cheese\` | 构建命令 `-S Souffle_Cheese -B Souffle_Cheese/build` |
| H3 | 复用原 EMTG 资源一律**复制**，不就地修改 | 代码审查 |
| H4 | 发行包在新机器上**双击即用**：不装编译器/Python、不设环境变量 | 干净环境实测（仅系统 `PATH`） |
| H5 | `SOUFFLE_NLP_SOLVER=SNOPT` 时行为与原版一致 | 同二进制内保留 SNOPT 路径 |
| H6 | 读写 UTF-8 文本必须显式指定编码 | 见 §6 事故记录 |

---

## 2. 阶段划分

| 阶段 | 内容 | 状态 |
|---|---|---|
| A | 环境勘察：确认 EMTG 工具链（MSVC + NMake）、Uno 发行包格式（MinGW） | ✅ |
| B | 证明 MSVC 可驱动 Uno（`msvc_probe.c`：`LoadLibraryExW` + `GetProcAddress`） | ✅ |
| C | 解耦：抽出 `NLP_interface`，把 `SNOPT_interface*` 替换为基类指针 | ✅ |
| D | 实现 `SouffleApi`（动态加载层）与 `Souffle_interface`（求解器映射） | ✅ |
| E | 引入工厂 `NLP_solver_factory`，`problem.cpp` 三处实例化点改走工厂 | ✅ |
| F | 跑通 EVVEU LTGA 可行解 | ✅ |
| G | 打包为自包含发行包 + PyEMTG 图形界面 | ✅ |
| H | 与 SNOPT 同口径对比 | ✅ |
| I | 接入真实工程流水线（ESFO_Uranus 二级） | ✅ |
| J | 命名迁移 UnoMTG → SOUFFLE | ✅ |

---

## 3. 验证阶梯

| 级别 | 用例 | 通过标准 | 结果 |
|---|---|---|---|
| L0 | `_probe\smoke_uno.c` / `msvc_probe.c` | 能链接、能解、C API 可达 | ✅ 通过 |
| L1 | 单次 `run_NLP`，现有 `trialx` | 从零推力初值到可行 | ✅ 通过（修复终止回调后 Uno 迭代 1→16000） |
| L2 | 单次 `run_NLP`，`Optimize` 模式 | 目标函数不差于 SNOPT | ✅ 同口径超基准（§5） |
| L3 | 全 MBH 流程 | 产出可行解 | ✅ EVVEU LTGA，见 README §5.2 |
| L4 | `arrival_type 2`（拦截）与 `3`（交会） | 两种到达类型都能跑通 | ✅ 均通过 |
| L5 | 发行包干净环境双击 | GUI 打开 | ✅ hwnd 实测，见 README §6.1 |
| L6 | ESFO_Uranus 二级流水线 | 与 SNOPT 基准一致 | ✅ 差 0.022%，见 README §5.5 |

---

## 4. 只读保护机制

**基线**：`_guard\EMTG_baseline.txt` —— 36,775 个文件的路径 / 大小 / mtime。

**校验方式**：重新生成清单并与基线 `Compare-Object`。

**当前状态**：

```
baseline = 36,775 文件
唯一差异 = EMTG\HardwareModels\empty.ThrottleTableOUTPUT
          内容 SHA 与基线完全相同（57564E13C9D3A80B）
          时间戳停在 2026-10-01T14:20:32Z（修复 HardwarePath 之前）
结论     = 原版 EMTG 未被实质性修改
```

**该文件的性质**：EMTG 每次运行都会重写它（`src\Core\missionoptions.cpp:2633-2639`），
属派生文件。修复 `HardwarePath` 指向本 fork 之后，所有后续运行都不再触碰原版目录，
时间戳也因此不再变化。

**注意**：`_guard\` 目录是校验依据，**不要删除**，否则无法再证明原版零改动。

---

## 5. 风险登记册

| 风险 | 影响 | 应对 | 状态 |
|---|---|---|---|
| Uno 为 MinGW 构建，MSVC 无法链接 | 无法集成 | 动态加载（C ABI 稳定） | ✅ 已解决 |
| `EMTGv9.exe` 硬导入 `snopt7.dll` | 缺 DLL 时 `0xC0000135` 秒退 | 打包时一并带上 `snopt7.dll` + Intel Fortran 运行时 | ✅ 已解决 |
| Uno 运行时与 MinGW 运行时 PATH 顺序冲突 | `0xC0000139` 秒退且无输出 | 启动器把 `bin`、`Uno\bin`、`Uno\deps` 置于 PATH 最前 | ✅ 已解决 |
| `logger` 小写被静默拒绝 | 22 MB 日志，运行时被 I/O 主导 | 强制大写 `SILENT` | ✅ 已解决 |
| 终止回调导致每次求解 `iters=1` | 完全无法求解 | 改传 `nullptr` | ✅ 已解决 |
| 雅可比压缩后索引错位 | 收敛到错误点 | 按 `constraint_jacobian_source` 还原 | ✅ 已解决 |
| 缩放上界未除以 scale | 搜索区域无物理意义 | 显式按契约重算 | ✅ 已解决 |
| 长时运行 + 管道导致退出码不可靠 | 误判失败、丢失输出 | 改文件重定向 + `PYTHONUNBUFFERED=1` | ✅ 已解决 |
| **用 ANSI 编码写 UTF-8 中文文件** | **文档永久损坏** | 显式 `-Encoding UTF8`；事后用 `find_encoding_damage.py` 校验 | ⚠️ 已发生一次，见 §6 |

---

## 6. 文档编码损坏事故

**经过**：批量重命名时用 `Get-Content -Raw` + `Set-Content -NoNewline`（未指定 `-Encoding`）
处理含中文的 Markdown。本机 PowerShell 默认 ANSI(GBK)，UTF-8 字节被按 GBK 解码后又写回，
**README.md、docs\DESIGN.md、docs\PLAN.md 的中文内容永久损坏**（既非有效 UTF-8 也非有效 GBK）。
反向恢复尝试恢复出的 CJK 字符数为 **0**，且项目无版本控制可回滚，只能重写。

**规则**（已纳入 H6）：

1. 读写 UTF-8 文本一律显式指定编码，或改用能保证 UTF-8 的工具
2. 批量文本替换前先备份
3. 事后校验：凡含非 ASCII 的文件必须能被 UTF-8 解码
   （`_probe\find_encoding_damage.py`）

**误报辨析**：`src\Astrodynamics\HarmonicGravityField.cpp` 也不是有效 UTF-8，
但它与原版 EMTG **内容哈希完全相同**、mtime 为 2026-08-26，属**原版即 GBK 编码**，
不是本次事故造成的。

---

## 7. 工具链速查

| 项 | 值 |
|---|---|
| 编译器 | Visual Studio 2022 的 MSVC v143（`cl.exe`），由 Developer Command Prompt 提供 |
| CMake | 任意 ≥ 3.20；VS 自带的那个即可 |
| 生成器 | `NMake Makefiles` |
| Windows SDK | 10.0.19041 或更高 |
| Boost | ≥ 1.60，MSVC ABI；用 `-DBOOST_ROOT` 指定 |
| SNOPT | `SNOPTDIR_OVRD` 指定（仅编译期需要） |
| Uno | ≥ 2.9.0 Windows 发行包（含 `include\uno`、`bin`、`deps`）；用 `-DSOUFFLE_UNO_ROOT` 指定 |
| 图形界面依赖 | 一个装了 wxPython、numpy、scipy、matplotlib、astropy、spiceypy 的 Python |
| ESFO_Uranus 用 Python | `G:\miniforge3\envs\pykep-env\python.exe` |

**配置命令**

```powershell
cmake -S . -B build -G 'NMake Makefiles' `
  -DCMAKE_BUILD_TYPE=Release `
  -DSNOPTDIR_OVRD=<snopt dir> `
  -DSOUFFLE_WITH_UNO=ON `
  -DSOUFFLE_UNO_ROOT=<Uno 路径> `
  -DSOUFFLE_DEFAULT_SOLVER=Uno `
  -DBOOST_ROOT=<Boost 路径> -DBoost_INCLUDE_DIR=<boost> `
  -DBoost_LIBRARY_DIR_RELEASE=<boost>\stage\lib -DBoost_LIBRARY_DIR_DEBUG=<boost>\stage\lib
```

增量编译：`_probe\run_build.ps1`（只跑 `cmake --build`）。

---

## 8. 后续可做

| 项 | 说明 |
|---|---|
| `.SNOPTcrash` 改名 | 需连同 `best_emtg` 的过滤规则与 ESFO_Uranus 的扫描逻辑一起改 |
| 成员名 `mySNOPT` → `mySolver` | 纯命名清理，涉及 5 个文件 |
| 同口径消除发射日差异 | 把发射历元钉死到基准的 2044-03-14 再各跑一次 |
| 批量运行二级流水线 | 驱动脚本已可用（`tier2_driver.py`），可扩展到多 case |
| 为 `Souffle_Cheese` 建 Git 仓库 | 避免再次出现不可回滚的损坏 |

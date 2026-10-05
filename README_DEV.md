# Souffle_Cheese — 开发说明

## 这个目录是什么

EMTG 的**可编辑副本**，也是唯一允许修改的 EMTG 源码树。

```
G:\Py\DeepSeekHarness\
├─ Souffle\          只读 —— 参考副本，勿改
├─ SOUFFLE_Git\      只读 —— 上游 git 版本，勿改
└─ Souffle_Cheese\   可编辑 —— 本文件所在处，所有改动都在这里
```

`build\` 是从 SOUFFLE_Git 复制过来的遗留构建树（其 `CMakeCache.txt` 指向那个目录），
与本项目无关，保留原样不参与构建。

## 目录结构

| 路径 | 内容 |
|---|---|
| `src\InnerLoop\` | **主要改动区**：`Souffle_interface.*`（Uno）、`Ipopt_interface.*`、`SNOPT_interface.*`、MBH |
| `united\` | **联合寻优**：Uno 与 Ipopt 的多进程路由与取优。用法见 `united\README.md` |
| `bin\` | 构建产物 + `archive\` 存放历史实验件 |
| `build_cheese\` `build_ipopt\` | 两个构建树，各自对应一个目标 |
| `docs\` | `DESIGN.md`（设计）、`DEVELOPMENT.md`（开发记录）、Uno 选项表 |
| `SOUFFLE_PATCHES.md` | 补丁清单、实测结果、失败尝试记录 |

## 构建

```
build.bat [uno|ipopt|all] [build|configure|clean]
```

| 目标 | 构建树 | 产物 | 说明 |
|---|---|---|---|
| `uno` | `build_cheese\` | `bin\EMTGv9.exe` | 纯 Uno |
| `ipopt` | `build_ipopt\` | `bin\EMTGv9_ipopt.exe` | Uno + Ipopt |

两个必须记住的坑（都写进了 `build.bat` 的注释）：

- **Windows SDK 不在注册表里**。`vcvars64` 查的是 `D:\Windows Kits\10\`，那里只有安装器，
  于是 `WindowsSDKVersion` 为空、`rc.exe`/`ucrt.lib` 找不到。真实 SDK 在
  `G:\Py\DeepSeekHarness\winsdk`，必须显式设 `INCLUDE`/`LIB`，否则报
  `cannot open stdio.h` 或 `LNK1104 kernel32.lib`。
- **改了头文件必须 clean 重建**。增量 nmake 曾漏掉改动过的头文件，把陈旧对象烤进已发布
  的二进制，导致一整天误诊。用 `build.bat all clean`。

发布走暂存名：目标被僵尸进程锁住时，新镜像留在 `bin\<名>.new` 且以退出码 0 结束——
锁住不该算构建失败。

## 求解器

三种接口，都继承 `NLP_interface`，由 `NLP_solver_factory.cpp` 按 `SOUFFLE_NLP_SOLVER`
选择（不设则按 SNOPT → Uno → Ipopt 取本构建里编进去的第一个）：

| 名称 | 实现 | 说明 |
|---|---|---|
| `SNOPT` | `SNOPT_interface.cpp` | EMTG 原有路径（本环境未编译） |
| `Uno` | `Souffle_interface.cpp` | 默认；含两处关键修复（见下） |
| `IPOPT` | `Ipopt_interface.cpp` | Ipopt 3.14.19；每次迭代都回调，incumbent 池远大于 Uno |

### Uno 接口的两处关键修复

- **D1 目标梯度**：MinTOF（`objective_type=1`）的唯一非零导数在**线性**雅可比 `A` 里，而
  `G` 中该行恒为零。原实现只读 `G`，于是 ∇f ≡ 0、任何可行点都是 KKT 点。修复后 MinTOF
  从 2327 d 降到 2248–2253 d。
- **D4 容差语义**：`dual_tolerance` 原被钳到 ≥1e-4（SNOPT 用 1e-5），且过滤器用 L2 范数、
  Uno 还额外除以乘子范数。对齐后 10 年档提升 8.3 kg。

其余尝试（约束缩放、修复阶段闸门、试试点入池）**全部失败**，原因与证据记录在
`SOUFFLE_PATCHES.md`，不要重复尝试。

## 联合寻优为什么必须是两个进程

Uno 与 Ipopt 各自携带**同名但不同工具链编译的 mingw 运行时**：

| DLL | `Uno\deps` | Ipopt (`Library\bin`) |
|---|---|---|
| `libwinpthread-1.dll` | 326,509 B | 63,150 B |
| `libgcc_s_seh-1.dll` | 958,591 B | 1,022,009 B |
| `libgomp-1.dll` | 1,830,310 B | 1,921,291 B |
| `libatomic-1.dll` | 257,431 B | 281,890 B |
| `libquadmath-0.dll` | 1,204,247 B | 1,271,528 B |

Windows 按**名字**解析模块，先加载的一方决定版本，另一方随即失败。实测同进程的
`Hybrid_interface` 在 Ipopt 第一次迭代即 `0xc06d007f`（ERROR_DELAY_LOAD_FAILED），
而纯 Ipopt 进程正常；调整 PATH 顺序只会把故障转移到 Uno。

**所以 `Hybrid_interface` 已退役**（源码删除，`SOUFFLE_NLP_SOLVER=HYBRID` 会明确指向
`united\`），联合寻优改由 `united\united.py` 编排两个进程并取较优解。

## 运行注意

即使只跑 Ipopt，也必须设 `SOUFFLE_UNO_ROOT` —— 二进制启动时会构造 Uno 接口，找不到
`libuno.dll` 会直接以 `GetLastError=126` 退出。

```
set SOUFFLE_UNO_ROOT=G:\Py\DeepSeekHarness\Uno
set SOUFFLE_NLP_SOLVER=Uno
bin\EMTGv9.exe <case>.emtgopt
```

联合寻优的完整用法（含时间设置与输出结构）见 `united\README.md`。

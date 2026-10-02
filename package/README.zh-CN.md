# SOUFFLE — Scalable Optimization Uno-powered Framework For Leveraging EMTG

**SOUFFLE** = **S**calable **O**ptimization **U**no-powered **F**ramework **F**or **L**everaging **E**MTG

> 本文件是英文版 [`README.md`](README.md) 的中文对照版。

本目录**开箱即用**：无需编译器、无需安装 Python、无需设置任何环境变量、
**无需 SNOPT 许可证**。

---

## 快速开始

| 我想要… | 操作 |
|---|---|
| **使用图形界面** | **双击 `run_souffle.bat`** |
| 不开界面、直接跑一个算例 | `run_souffle.bat EVVEU_LTGA.emtgopt` |
| 用 EMTG 自带的默认算例 | 不带参数时用 `bin\default.emtgopt` |

不带参数时，启动器会打开 **PyEMTG 图形界面**（窗口标题 `EMTG Python Interface`）——
与 SNOPT 版 EMTG 使用的是同一个界面，已接好本包的求解器。
带一个算例文件作为参数时，则直接运行该算例、不打开界面。

---

## 可整体搬迁

包内 `.emtgopt` 中的路径**全部是相对路径**：

```
universe_folder          ../Universe_EVVEU
HardwarePath             ../HardwareModels/
forced_working_directory ../results
```

它们由 EMTG 按其工作目录解析，而启动器始终以 `bin\` 为工作目录启动可执行文件。
因此**把整个文件夹拷到别的盘符或别的机器上都能直接用**，不需要改任何配置。

> 图形界面需要**绝对路径**（写在 `PyEMTG\PyEMTG.options` 里），这与"包要能搬走"本质冲突。
> 解法是：**启动器每次启动时按自身所在位置重新生成 `PyEMTG.options`**。

---

## 目录内容

| 路径 | 内容 |
|---|---|
| `bin\EMTGv9.exe` | SOUFFLE 可执行文件（用 Uno 作内层 NLP 求解器的 EMTG） |
| `bin\default.emtgopt` | 不带参数启动时 EMTG 读取的算例 |
| `bin\*.dll` | Uno 共享库，以及 EMTG 硬导入的运行时 DLL |
| `Uno\bin`、`Uno\deps` | Uno 运行时；环境变量 `SOUFFLE_UNO_ROOT` 指向此处 |
| `HardwareModels\` | 推力器 / 电源 / 推进系统库（启动时读取） |
| `Universe\` | 完整星历与宇宙模型集（可在界面中新建任务） |
| `Universe_EVVEU\` | EVVEU 任务所用的宇宙定义 |
| `PyEMTG\` | PyEMTG 图形界面；`PyEMTG.options` 指向本包的 `bin\EMTGv9.exe` |
| `python\` | 自带 Python 3.13，含 wxPython / numpy / scipy / matplotlib / astropy / spiceypy |
| `EVVEU_LTGA.emtgopt` | 同一个算例，放在顶层供显式指定 |
| `results\` | 内置算例的输出目录 |
| `licenses\` | EMTG（NASA NOSA 1.3）、Uno（MIT）及第三方许可 |

---

## 结果输出位置

输出目录由你 `.emtgopt` 里的 `forced_working_directory` 决定。
内置算例把它设为 `../results`；由于启动器以 `bin\` 为工作目录，
它解析为与本 README 同级的 `results\` 目录。

---

## 图形界面

双击 `run_souffle.bat` 会打开 PyEMTG 图形界面。它可以编辑并运行 `.emtgopt` 文件、
绘制 `.emtg` 结果；它驱动的是本包的 SOUFFLE 可执行文件，因为
`PyEMTG\PyEMTG.options` 中的 `EMTG_path` 指向 `bin\EMTGv9.exe`。

启动器**每次启动都会依据自身位置重新生成 `PyEMTG.options`**。
这是刻意设计：界面需要绝对路径，在启动时写入才能保证整个文件夹可搬迁。

---

## 求解器与预设选择

启动器会设置 `SOUFFLE_NLP_SOLVER=Uno` 与 `SOUFFLE_UNO_PRESET=filtersqp`。

* **切换预设** —— `filtersqp`（默认）是 Uno 中与 SNOPT 同族的信赖域滤子 SQP 方法；
  `ipopt` 则是内点法。切换无需重新编译：

      set SOUFFLE_UNO_PRESET=ipopt
      run_souffle.bat

  在 EVVEU 算例上的实测：`filtersqp` 收敛 111/138 次，`ipopt` 为 33/263 次。

* **切回 SNOPT** —— 只有以 `SOUFFLE_WITH_SNOPT=ON` 构建的版本才包含该路径。本发行包
  是纯 Uno 版，因此 `set SOUFFLE_NLP_SOLVER=SNOPT` 会报错而不是切换。包内不含任何
  SNOPT 二进制或许可证，也不需要它们。

---

## 已验证的行为

* 图形界面（`EMTG Python Interface`）能用自带的解释器启动，并读取包内的 `PyEMTG.options`。
* 求解器在**未设置** `SNOPT_LICENSE` 的情况下运行，并走到 `EMTG run complete`。
* 在干净环境（仅保留系统 `PATH`）下验证：启动器自行完成全部配置，
  正确解析 `../Universe_EVVEU/...`，并把结果写入 `../results`。
* 内置算例产出了**可行的交会解**：末质量 4698.47 kg，电推进剂 747.53 kg。

---

## 调参

内置算例出厂设置为 `MBH_max_run_time 900`、`snopt_max_run_time 60`、`num_timesteps 10`。
想要更长的搜索就调大 `MBH_max_run_time`；想用精度换运行时间就调整 `num_timesteps`。

---

## 疑难排查

* **一闪而过、没有任何输出。**
  说明 `PATH` 中排在前面的是过时或版本不匹配的 MinGW 运行时。
  请始终通过 `run_souffle.bat` 启动——它会把 `bin`、`Uno\bin`、`Uno\deps` 放在最前面。

* **提示 `SOUFFLE: failed to load libuno.dll`。**
  `SOUFFLE_UNO_ROOT` 没有指向 `Uno` 目录。启动器会自动设置它。

* **找不到可行解。**
  对非常极限的算例这是正常现象。EVVEU LTGA 算例在 TOF 小于 6.5 年时已知极其紧张——
  用 SNOPT 构建的版本在那里同样吃力。在判定"求解器失败"之前，
  先放宽飞行时间约束（例如允许 7 年或更长）。

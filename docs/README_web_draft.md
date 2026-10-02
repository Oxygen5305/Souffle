# Web-drafted README (archived)

These are the README drafts that were written directly on GitHub's web editor on branch
`Cheese` (commits `1d98543` … `a93b183`, 2026-10-02), before the SOUFFLE source tree was
pushed to that branch. They are kept here for reference only — the current, canonical
documents are [`README.md`](../README.md) and [`README_zh.md`](../README_zh.md).

---

## Archived: `README.md` (English draft)

```markdown
### SOUFFLE——Scalable Optimization Uno-powered Framework For Leveraging EMTG
[![License](https://img.shields.io/badge/License-GPL--v3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.txt) [![Python](https://img.shields.io/badge/python-3.13%20%7C%203.14-blue)](https://www.python.org/) [![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%28Debian%29%20x64-lightgrey)](https://www.microsoft.com/en-us/windows)
---
SOUFFLE:a community fork of EMTG, replaces SNOPT with Uno. Uno is an actively maintained, emerging optimizer with relatively strong performance. Since its default FilterSQP algorithm is similar to SNOPT, using Uno as the optimizer will not cause a decline in SOUFFLE's computational quality or efficiency. On the contrary, SOUFFLE's computational performance is sometimes even better than EMTG's.

Official website of the Uno optimizer:https://unosolver.readthedocs.io/en/latest/

> 中文说明见 [README_zh.md](README_zh.md) · Chinese docs in `README_zh.md`

### Features
Its usage is the same as that of the original EMTG. It also supports two Uno optimizer presets, FilterSQP and IPOPT, which you can invoke programmatically. Switching presets through a visual graphical interface is not supported. The default preset is FilterSQP. Testing shows that FilterSQP achieves the best computational quality and speed.
### Installed
Grab the binary file from the release page and it works out of the box.

Open the user interface: Double-click to run run_souffle.
### Using
The method of use is the same as that for EMTG.
### license: [GPL-3.0 License](https://www.gnu.org/licenses/gpl-3.0.txt)

![GPLv3-logo](https://www.gnu.org/graphics/gplv3-with-text-136x68.png)
```

## Archived: `README_zh.md` (Chinese draft)

```markdown
### SOUFFLE——Scalable Optimization Uno-powered Framework For Leveraging EMTG
[![License](https://img.shields.io/badge/License-GPL--v3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.txt) [![Python](https://img.shields.io/badge/python-3.13%20%7C%203.14-blue)](https://www.python.org/) [![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%28Debian%29%20x64-lightgrey)](https://www.microsoft.com/en-us/windows)
---
SOUFFLE，一个EMTG的社区分支，将SNOPT替换为Uno. Uno是一个更新活跃的、性能比较优秀的新兴优化器. 因其FilterSQP预设算法于SNOPT类似，因此使用Uno作为优化器不会导致SOUFFLE的运算质量、运算效率下滑。相反，有时候SOUFFLE的运算性能甚至强于EMTG.

Uno非线性优化器官网:https://unosolver.readthedocs.io/en/latest/

### 特点
使用方式于原版的EMTG相同，同时还有Uno的FilterSQP、IPOPT两种优化器预设支持，你可以通过编写程序来调用她们。不支持使用可视化图形界面来切换预设，默认预设为FilterSQP，测试得出FilterSQP的运算质量、运算速度都是最优.
### 安装
在“Release”界面下载，即装即用
### 使用
使用方法和原版EMTG相同.

若想使用GUI，双击运行“run_souffle”.
### license: [GPL-3.0 License](https://www.gnu.org/licenses/gpl-3.0.txt)

![GPLv3-logo](https://www.gnu.org/graphics/gplv3-with-text-136x68.png)
```

---

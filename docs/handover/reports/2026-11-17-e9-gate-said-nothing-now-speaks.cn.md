# E9 门：一次 exit 0 的漏报，改成了 exit 1 的报警（lane-e9gate，commit 7cf791928）

口径：`skip` ≠ `pass`；未测到的写「未测」，不写 0。基线 `fe1bf3dd2`，树 `D:\Projects\Camel-wt\e9gate`。

## 一、同一棵树的一对读数：`-Check e9-full`

| | 改前（pin `c65817e91`） | 改后（pin `7cf791928f`） |
|---|---|---|
| wrong / refused / crashed | 12 / 3 / 1 | 12 / 3 / 1 |
| cells | 521 / 135 / 6 | 521 / 135 / 6 |
| 人口 | 275 rejects / 225 in matrix | 275 / 225 |
| 退出码 | **0** —— `not compared -- the program population changed: programs_in_matrix 227->225, generator_rejects 273->275 ... not one trend` | **1** —— `ABOVE THE BASELINE: 23 program(s) worse and 0 lost, on the intersection` |

改后那一跑：500/500 programs，1152 s，`--jobs 4`，host `out=/tmp/e9-full-20261117-083334`。
这一跑当时 gate 收尾行是 `wrong=12 refused=3 crashed=1 seconds=1152 verdict=the-comparison-cannot-be-made exit=1`
—— 那时 `baseline.json` 还没有逐程序记录（§四）；把那份记录补上之后，对**同一跑**复判
（`e9diff.py compare`，走 `run` 末尾同一个 `judge`）红的就是逐程序的那一条，§一的表按它写。
改前那一跑的三个数与 cells 见 `2026-11-17-e9-full-measurement.cn.md`（同一 pin，同一读数）。

## 二、两组名单（工具 stdout 原文，不再只给计数）

```
[E9] wrong: 131 141 292 303 311 313 361 388 417 419 429 484
[E9] RUN_ERROR: 15 27 32 82 122 136 179 203 204 231 248 258 320 354 362 437 453 468 471
[E9] refused: 66 255 319
[E9] crashed: 153
[E9] NOT_RUN: 153
```

- **6 个新错值**：131 141 311 417 419 484（在这一跑里都是 `ok -> wrong`）。
- **19 个 RUN_ERROR 种子**：其中 15 与 320 在 baseline 的 pin 上就有 RUN_ERROR（198 格就是这 2 个），
  16 个是这一跑新得的（27 32 82 122 136 179 203 204 231 248 258 354 362 437 468 471），
  1 个是 453 由 `wrong` 换成 `run_error` —— 它离开 wrong 类不是修好，逐程序比较照旧把 453 记为变差。
- 292 303 313 361 388 在 pin 上就是 wrong，429 由 `wrong+crashed` 变 `wrong`（那 4 格是崩溃变不匹配）。

## 三、`left` / `lost`：旧工具把两者叫同一个「人口变了」

- **`left`**（合法减员）：baseline 矩阵里有它、这一跑没有它，而这一跑对它的记录是 generator reject
  **带诊断** ⇒ 打印、计数、**不算差**。这一跑两条：165、202（`SemanticDiag::ClosureAssignsCapturedName`，
  设计原则 3.5，作者 2026-11-16 定案）。它们变 reject 就是 227->225 与 273->275 的全部来源。
- **`lost`**（漏跑）：baseline 矩阵里有它、这一跑**根本没有它的记录** ⇒ **算差、exit 1**。
  判据一句话：`left` 看的是「这一跑为什么不要它」，`lost` 看的是「这一跑有没有测它」；
  「没跑过」不是「过了」。
- **交集为空 ⇒ exit 1**：「比不了」本身是失败。`NOT_RUN` 单列一行，类消失不会被读成修好。

## 四、`baseline.json` 补了逐程序记录，且没有抬任何数

- 新增 `programs_by_class`（227 条，seed -> 它到达的类，`[]` 为干净）与 `programs_by_class_from`（来源）。
- 来源是**该 baseline 自己的 pin**（`d6b2fff1f`）那一次跑（host `/tmp/e9-full-20261116-073933`）逐程序读出；
  **不是**用今天这一跑重建（那会把 wrong 12 写成基线）。原有 17 个键一个字节没动，
  由脚本对 `git show HEAD:test/tools/e9diff/baseline.json` 逐键断言，`programs_crashed` 仍是 3（最好状态）。
- 判据（作者/合并位给的条件）：补完后**同一跑仍 exit 1**，且打印
  `[E9] ABOVE THE BASELINE: 23 program(s) worse and 0 lost, on the intersection` —— 已实测。
- 补记录**之前**，同一跑也是 exit 1，但红的是 `the comparison cannot be made`（基线没有逐程序记录，
  交集算不出来）。两种红都不是绿，而后者能指出 23 个程序，所以记录值得补。

## 五、怎么复现（都是树内入口）

```
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <tree> -Check e9-full -Jobs 8
python test/tools/e9diff/e9diff.py compare --out <一份跑出来的 --out 目录>
python test/tools/e9diff/e9diff.py compare --out <同上> --baseline <另一份基线文件>
```

`compare` 读一次已经跑完的记录、走 `run` 末尾同一个 `judge`，秒级复判：换基线、换人口，
不必再花 19 分钟。这一笔的 7 个判据（合法减员、漏跑、空交集、旧基线同人口、旧基线换人口、
自身记录通过、自身记录之外的基线报红）在 `D:\Temp\e9gate\check_compare.py` 里，
但临时目录会消失，判据的结论在本页。
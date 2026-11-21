# e9-full 测量报告（lane-e9full，HEAD c65817e91，干净树）

口径：`skip` ≠ `pass`；`declared > started` 是缺陷；未测到的一律写「未测」，不写 0。

## 一、三个数与 cells（`-Check e9-full`，pin `c65817e913` clean，built 2026-11-17T07:34:27）

| | wrong | refused | crashed |
|---|---|---|---|
| programs | **12** | **3** | **1** |
| cells | **521** | **135** | **6** |

人口：500 programs / **275 generator rejects** / **225 in matrix** / 22275 cells。
`skip` 类桶：`NOT_RUN 75` cells（全部来自 seed 153，`MAX_CRASHES=6` 之后剩余 75 格未跑）。
其余桶：OK 18555、REFUSED（located，合法拒绝）1102、MISMATCH 521、REFUSED_UNLOCATED 135、HANG 6、RUN_ERROR 1881。

## 二、与上一次已知读数 `7 / 3 / 1`（cells 276/135/6）的差

- wrong 7 → **12**（+5 programs，cells 276 → **521**，+245）
- refused 3 → **3**（无变化，cells 135 → 135）
- crashed 1 → **1**（无变化，cells 6 → 6）

## 三、逐 seed 归因

| 类 | seed | 新/已知 | cells |
|---|---|---|---|
| wrong | 131 141 311 417 419 484 | **新出现**（24 次历史跑从未 wrong） | 48/48/56/48/45/48 |
| wrong | 292 303 313 361 388 429 | 已知 | 8/16/9/99/4/92 |
| wrong | 453 | 已知，**本次离开**（48 格全变 RUN_ERROR） | 0 |
| refused | 66 255 319 | 已知，未动 | 54/27/54 |
| crashed | 153 | 已知，未动 | 6 |

## 四、三个具体问题

- **(a) 剩下那 1 个 crashed 是 seed 153**：只有 `std::jit` 一列，`p00/p01/p02 × {generational, mark-sweep}` = 6 格，每格 30 s 超时 `HANG`；18 格 OK，75 格 NOT_RUN。它**在**任务引用的那份已知名单里（5/52/85/70/134/153/176/214/427），所以上一个车道「不在那 12 个已知种子里」的说法与数据相反。
  点名的办法：`run` 把每个 seed 写成 `--out/e9_cells.jsonl` 的一行（`seed` 就是记录键），`analyze --out DIR` 按类打印成员 seed。工具不是没有 seed，只是不往 stdout 打。
- **(b) 229 / 262 不是「现在」变成 reject 的**：在 baseline 的 pin（`d6b2fff`）那一次跑里它们**已经是** `ClosureAssignsCapturedName` 拒绝。真正新变成 reject 的是 **165 和 202**，人口 273/227 → 275/225 就是这两条造成的（两条此前都是 `CRASH`，exit −11，各 6 格）。
  判定：**被合法拒绝**。四个程序（165/202/229/262）都是「闭包给外层名字赋值」——`var v25`/`var v8`/`var v6`/`var c8` 在闭包内 `-=`/`%=`，这正是设计原则 3.5 由作者 2026-11-16 定案的编译错误，诊断自己给出三条改法（返回新值 / 循环携带状态或 fold / 用 `Ref`）。
  而且 3.1 明写「给 `var` 赋值不是效应」，所以「写入外部状态的东西必须执行」这条裁定**不适用**：这里被拒的是一个名字赋值，不是外部写，没有丢掉任何必须发生的执行（202 的 `println` 在闭包之外，程序本身违法）。要改的是**生成器**，不是编译器。
- **(c) 153 不是 pass**：它仍然是 crashed（`std::jit` 的 HANG），而且从不 wrong。真正「不再 crashed 但仍 wrong」的是 **429**：073933 时它同时 wrong(88) 与 crashed(4)，现在 crashed 掉出、wrong 升到 92——那 4 格是崩溃变成了不匹配，不是修好。

## 五、baseline 可比性（照抄工具原话）

```
[E9] baseline baseline.json: not compared -- the program population changed: programs_in_matrix 227->225, generator_rejects 273->275 -- the generator decides which seeds are rejects, so 225 programs today and 227 when the baseline was taken are not one trend
```

工具拒绝比较，`-Check e9-full` 因此 exit 0：**wrong 从 7 涨到 12，棘轮没有说话**。

## 六、三个数之外的大新闻（同一跑）

- `RUN_ERROR` 198 → **1881** cells，2 个 seed → **19 个 seed**（15 27 32 82 122 136 179 203 204 231 248 258 320 354 362 437 453 468 471），全部 `InternalDiag::UnknownInternalError`，每条 99 格。detail 是图不变式：`graph 'main' node 103 lies on a cycle of inputs`、`graph 'f10' breaks the branch-arm invariant: FUNC 30 (calls 'f4') runs between BRCH 8 and its JOIN 7 and belongs to no arm`。
- HEAD 那一笔正是「an arm whose value the JOIN does not take exits through a GATE of its own」。干净对干净地看（`044117`，pin `d4e58eda4`，是 HEAD 的祖先，24 个提交之前，同样人口 275/225）：wrong 7 → 12、crashed 3 → 1（52、85 现在 pass）、RUN_ERROR 198 → 1881。**crash 类的下降里有一部分是重分类不是修复**（165/202 从 CRASH 变 generator reject）。

## 七、可重复性与工具缺口

- 前两次尝试**没测到任何数**（不是 0）：跑到 200/500 与 25/500 时 `~/camel-work/e9full` 被并发闸门运行的「worktree 已消失」清理整批删掉，Python 抛 `FileNotFoundError: .../out/latest/bin/camel`。`e9full` 在每条 `git worktree list` 里都在，所以正确判定的清理不该删它——一个 `git worktree list` 失败的运行会把别的 checkout 全删（`internal/tools/camel-remote.ps1` 的 catch 把 LIVE 塌成自己一个名字）。
- 未测：**gpu 层**（`gpu-plans` exit 1，`the sweep printed no summary, so the gpu layer was not read`，report-only，不决定退出码）。
- 工具缺口：三个数只按类打印，成员 seed 只在 `--out` 里；而 `--out` 必须在树外，闸门不回传，`-FetchFile` 也拿不到。建议 `e9diff.py run` 直接打印每个类的成员 seed（或 `--list-seeds`），否则每一条读数的归因都要另开一次 host 会话。
- 读数条件：`--jobs 4`、1064 s、1m load 22.45（host 与他人共用，153 的 HANG 是 30 s 超时）。
# 标准后端提速：交接一（只量，不改）

本目录是车道 `backend-speed`（D-1125-35）交接一的读数与脚本。计划页：
`internal/plans/2026-11-25-backend-speed.cn.md`（现在在 `origin/integration` 里；见下）。

**机器：`KOGLER-WORKSTATION`（工作站的读数，不与 88 主机构建机的读数相比）。**

## 三个测量点，以及它们是怎么定死的

派工要求「把你怎么确定它的写出来」。三个点都从 `git log` 与 `internal/` 的既有记录里取，
没有猜的部分；取不到的就不取。

| 点名 | 提交 | 怎么确定的 |
|---|---|---|
| 11-13 审计 | `15165a9333436377fe02c1a15b41c4de5008ec28` | `git log --all --since=2026-11-12 --until=2026-11-15` 里唯一的审计笔：`2026-11-13 15:04:00 +0000 Record the architecture audit and schedule a consolidation phase`。同日无第二笔审计提交。 |
| 阶段 2 之前 | `1fa180c4fbfb174547021817d7aa9e88d5db3106` | `internal/reports/2026-11-22-round-failures.cn.md:11` 记 `45001457388` 跑在 `1fa180c4f` 上、红腿「**无**」、注明「搬家窗口关闭的那一跑，五腿全绿」。阶段 1 就是搬家窗口（`internal/plans/2026-11-22-refactor-stage1-move.cn.md`），其关窗步骤第 6 条要求「宣布关窗」。`1fa180c4f` 的子提交 `3c20f91c7` 是 `docs(review): the move window closes on five green legs`（纯文档），再下一笔 `a5ef403bea`/`fb314bd282` 起才是 `merge lane/stage2-*`。所以 `1fa180c4f` 是**第一笔阶段 2 内容之前**的树；`3c20f91c7` 与它代码相同（仅文档），计时等价。 |
| 11-25 integration | `6fcee16d0cb82c3b1a494823e5a094e44fe084e9` | `git fetch origin` 后 `origin/integration` 的头：`2026-11-25 18:01:31 +0800 merge fsh-dev: the daily review, the broken-graph ruling and the transpose-port ruling`。 |

`lane/backend-speed` 工作树从 `origin/integration` 开（`git worktree add ... -b lane/backend-speed origin/integration`）。

**2026-11-26 重定基**：这条车道被错档过一次，恢复出来时已经落后 300 多笔，于是把它重放到
当时的 `origin/integration` 头 `250346d8d`（旧基底 `6fcee16d0`）。四笔提交只加本目录的文件，重放没有冲突；
重放之后的树是 `c542b0666`。同一天用 `run-timing.mjs` 在 `6fcee16d0c` 与重定基后的头各量了一轮，
两个点、同一台机器、同一套选项、同一段安静窗口，那份读数在 `reading-2026-11-26.cn.md`。

**计划页的位置变过一次**：它一开始只在 `origin/fsh-dev` 上（`9f6d8840b2` 引入），当时那一笔
`merge fsh-dev` 没带它，所以交接一是照着 `origin/fsh-dev` 读的；之后它随 fsh-dev 进了 `origin/integration`，
现在从本树读同一页。除此之外没有改过本目录以外的文件。

## 基准集（五个，全部在三个点上都能跑）

从 `git cat-file -e <sha>:<path>` 逐点确认过，五个文件在三个提交上都在。

| 类别 | 程序 | 说明 |
|---|---|---|
| 调用密集 | `benchmarks/parallel/cases/fib.cml` | `fib(30)`，两个递归调用之间无路径。 |
| 循环密集 | `benchmarks/parallel/cases/small_calls.cml` | 2e6 次尾递归迭代、每次两个小函数调用；刻意全串行，调度器只能加成本。 |
| 数组/张量算子 | `benchmarks/parallel/cases/tensor_train.cml` | 784-256-128-10 MLP、batch 64、60 步 SGD；需 `std::macro`（自动微分）在后端之前。 |
| 分支密集 | `showcase/d4-mcts/search.cml` | MCTS，`--arg seed=42 --arg playouts=2000 --arg position=XX.OO....`。 |
| 闭包与高阶调用 | `examples/functions/closures.cml` | 返回闭包、组合函数、函数表、`with` 参数。 |

## 后端与线程

`std::nvm`、`std::fvm`、`std::jit`、`std::prl`（`--threads 1 / 4 / 16`，`std::prl` 是
`std::taskflow` 的别名；线程数由命令行 `-t/--threads` 给，不是 pass 参数）。

## 计时的形状

见 `run-timing.mjs` 的头注释。三个点上**同一条命令、同一个选项集**。

- `--timeit <pass>/<N>`：在**一个进程内**把那个 pass 跑 N 次、每次都从一张全新的图开始，逐次给出
  `samples_ms`。第 1 次 = 「第一次」（JIT 在这一趟编译，prl 的池在这一趟起工人），第 2..N 次的中位数 =
  「稳定后」。**两者分开记，不合并成一个数。**
- `nvm`/`fvm`/`jit`/`prl` 的 pass 时间不含起动与解析。装不进 `--timeit` 的那一格（见下）改用整进程墙钟，
  并在表里单独标明。
- 线程数走命令行 `-t/--threads`（`--timeit` 会把它传进被计时的那一趟，`timeit.cpp` 有那句注释）。

### 一处 `--timeit` 量不了：MCTS

`showcase/d4-mcts/search.cml` 在 `--timeit` 下**第 2 次迭代编译失败**，不是跑得慢，是编译不过：

```
{"kind":"timeit","failed":true,"iteration":2,"pass":"std::nvm",
 "samples_ms":[50.9857],
 "diagnostics":[{"severity":"Error","code":"0x0300000F",
   "message":"No matching function found for argument types: <(place: int) => bool pure> (Range).",
   "data":{"stage":"typecheck","name":"SemanticDiag::NoMatchingFunction",
           "suggestion":"... prelude::filter: <f: (item: int) => bool> (r: Range) => int[]"}}]}
```

同一个 lambda，第 1 次编译是 `(place: int) => bool`，第 2 次成了 `(place: int) => bool **pure**`，
于是 `prelude::filter` 的 Range 重载不再匹配。**这是 `--timeit` 复用上下文/重编译的结果，不是 MCTS 程序的问题**
（普通运行连跑 5 次全部 exit 0，输出一致）。交接一只量不改，故这里只记录、不修；这一格改用
「整进程跑 5 次」，并注明那一列是整进程墙钟（含起动与解析，约 +65 ms 的常量）。

**已验证的端点**：同一句 `--timeit std::nvm/2` 在阶段 2 之前（`1fa180c4f`）能跑完两次、两次都有采样；
在 11-25 的 integration（`6fcee16d0c`）第 2 次迭代编译失败。**所以它是这个区间引入的编译缺陷，与「量不了」无关。**
审计点也曾跑过一次同一句探针，输出里没有失败标记 —— 但那次只看了有没有 `failed` 字样，**没有逐条核对采样**，
所以这里不把它算作已验证的端点。

2026-11-26 在重定基后的头 `250346d8d` 上再跑一次同一句：**仍然失败，仍是第 2 次迭代，诊断一字不差**
（`<(place: int) => bool pure> (Range)`，可用重载仍是 `prelude::filter: <f: (item: int) => bool> (r: Range) => int[]`，
那句 `filter` 的定义在 `src/stdlib/prelude.cml`）。所以这个端点在今天也成立：**它没有在这个区间里被修掉**，
这一格至今只能用整进程墙钟量。

## 这一目录里有什么

| 文件 | 是什么 |
|---|---|
| `README.cn.md` | 本页：三个点的提交是怎么定的、五个程序、量法 |
| `build-stamps.md` | 三个点的 `build-info.json` 戳（源码数、指纹、选项哈希）与**选项差异** |
| `run-timing.mjs` | 取数的脚本（`timeit` / `process` / `stdout` 三种模式） |
| `load-gate.ps1` | 60 秒负载闸：每 4 秒一个采样，安静线是核数的 15% |
| `analyze.mjs` | 把三个点摊成表、算 >20% 的变化、比 stdout |
| `bisect.mjs` | 二分驱动（每步 checkout → 构建 → 量一格） |
| `table.cn.md` | **60 格表 + >20% 清单 + stdout 一致性**（`analyze.mjs` 生成） |
| `table.expanded.json` | 同一张表，一格一行（含每格全部采样） |
| `bisect.cn.md` | 二分：`small_calls` × `std::jit` 的 16 倍落在哪一组提交上 |
| `reading-2026-11-26.cn.md` | 重定基后的一轮读数：`6fcee16d0c` 与 `250346d8d` 两点、同机同选项、安静窗口 |
| `cause-fib-2026-11-26.cn.md` | 查因：`fib(30)` 的 nvm/fvm 从 ~50 ms 到 ~155 ms 落在哪几笔——16 个点今天重建重量、第一台阶定位到 `06bb6a2e53`，同图不同时 |
| `fix-2026-11-26.cn.md` | 修因：`small_calls × std::jit` 的 ~15 倍（读自己的字节码那一路），前后读数 |
| `results/` | 各点的原始 JSON/CSV、负载日志、构建日志尾巴与 `build-info.json`、二分日志 |
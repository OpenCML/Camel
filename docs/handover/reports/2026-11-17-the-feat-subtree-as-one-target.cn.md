# 整棵 feat 子树当一次目标：9553 entries、26 条红，和那套账差在哪

**状态：已量；两条已落，一条归文档。**

这一页记的是**读数**，不是修法。12 个「手挑的 `-Plan` 从不点名」的目录逐个跑了一遍，
然后意外发现 `-Plan test/plans/feat` **一次就把整棵 feat 子树跑完了**——
354 个计划文件、9553 条用例、26 条红。**「这些我们从来没跑过」这句话到今天为止结束了：它们能跑，而且红着。**

## 一、怎么跑的

| 项 | 值 |
|---|---|
| 工作树 | `D:\Projects\Camel-wt\cover`，分支 `lane-cover`，基线 `fe4dd1afa` |
| 跑前 / 跑后 | `git status --porcelain` 均为空（主检出 `D:\Projects\Camel` 一个字节没动） |
| 命令 | `pwsh -NoProfile -File …\internal\tools\camel-remote.ps1 -Path …\cover -Plan <一个目录> -Jobs 8` |
| 在哪跑 | 远程构建主机；**本机不构建**。每次构建 3–137 s（footprint 复用后 3 s） |
| 主机负载 | 全程 1m load **28–45**；工具每次都印 `load xx > 8 -- readings taken now are not comparable to quiet-host readings` |

三条纪律，逐条守：

- **没有 `[SUMMARY]` 的那一行不进表。** `perf` 在 `--tier fast` 下选 0 条时，runner 自己拒绝并印
  `this run selected no test at all`，没印 `[SUMMARY]`——那一次就没进表，改用 `-Tier all` 重量。
- **`total = pass + fail + skip (+ known)` 每条都成立**，下面每个 `[SUMMARY]` 都可以自己加。
- **`skip > 0` 的目录，「0 红」不算绿。** `gpu` 就是这一条的例子（第八节）。

## 二、12 个目录，逐个跑（一次只给一个目录）

| 目录 | 计划文件 | blocks | total | pass | fail | known | skip | 红名单（按名字） |
|---|---|---|---|---|---|---|---|---|
| `test/plans/feat`（顶层 10 文件，按文件名跑） | 10 | 166 | 445 | 440 | 3 | 0 | 2 | `tutorial.examples.06_let_to_var`、`tutorial.examples.13_graph_rgir`、`docs.links.citations` |
| `test/plans/feat/transforms` | 3 | 34 | 未重跑 | — | — | — | — | 按要求不重跑（沿用 48/40/0/8）；整棵 feat 那次里它 **0 fail** |
| `test/plans/feat/para` | 3 | 62 | 96 | 95 | 1 | 0 | 0 | `para.exec_pred:linear.predecessors` |
| `test/plans/feat/format` | 3 | 31 | 31 | 31 | 0 | 0 | 0 | — |
| `test/plans/feat/runtime` | 4 | 9 | 9 | 9 | 0 | 0 | 0 | — |
| `test/plans/feat/gate` | 3 | 3 | 3 | 3 | 0 | 0 | 0 | — |
| `test/plans/feat/bindings` | 1 | 2 | 2 | 2 | 0 | 0 | 0 | — |
| `test/plans/feat/docs` | 1 | 2 | 2 | 2 | 0 | 0 | 0 | — |
| `test/plans/perf`（`-Tier all`） | 22 | 69 | 69 | 63 | 5 | 1 | 0 | `startup.cost`、`collfuzz.many_small`、`collfuzz.large`、`showcase_c3.probes`、`showcase_d4.small` |
| `test/plans/probe` | 4 | 18 | 13 + 20 | 33 | 0 | 0 | 0 | —（fast 层 13 条，`refleak` 的 5 blocks 用 `-Config Debug -Tier debug` 补量：20/20） |
| `test/plans/debug`（`-Config Debug -Tier debug`） | 2 | 3 | 3 | 3 | 0 | 0 | 0 | — |
| `test/plans/gpu`（`-Tier all`） | 6 | 125 | 146 | 22 | 0 | 0 | **124** | **本轮不能量**（124/146 skip） |

- 上表是**修之前**在 `fe4dd1afa` 上的读数。第九节那两条落地后，`test/plans/feat` 顶层再跑一次读
  `[SUMMARY] total: 445, pass: 442, fail: 1, skip: 2`（剩下的那条 `fail` 是 `06_let_to_var`，归文档账）。
- **`test/plans/feat` 的目录目标会递归**：给目录名会跑整棵子树（354 文件）。要那 10 个顶层文件，
  只能把文件名逐个点名（点名的计划文件按「整份跑」处理，不受 tier 过滤）。
- `perf` 的 22 个文件全是 `milestone` / `slow`，**没有一个是 fast**——所以 `--tier fast` 选 0 条，
  那一次的 `[tier] fast: 0 plan file(s), 0 declared test entries, 0 started` 是真实读数，不是空跑。
- `test/plans/feat/transforms` 那条 48/40/0/8 不在本轮重跑之列；整棵 feat 那一次里它 0 fail，
  与它自己的读数一致。

## 三、整棵 feat 一次跑（意外收获，比任务本身重要）

```
-Plan test/plans/feat              （目录目标，--tier fast 默认）
[tier] fast: 354 plan file(s), 9553 declared test entries, 9553 started
[SUMMARY] total: 9553, pass: 9479, fail: 26, skip: 11, known failures: 37
RUN_EXIT=1
```

- feat 子树下共有 **359 个**计划文件、**6427 个 blocks**；fast 层选中其中 **354 个**文件的 **9553 条**。
- 26 条红里，**4 条**落在第二节那 12 个目录里（`feat` 顶层 3 条 + `para` 1 条），
  **22 条**在那 12 个目录之外，分布在 `opt`、`compile`、`trans`、`parse`、`linear`、`consistency`、
  `mm`、`modules`。
- 这 26 条**一条都没有 `known_fail`**（核对方式：按 `[[tests]]` 块匹配用例名，再找块内的
  `known_fail` 标记——0 条命中；`stream.generator` 与 `poison` 那两组名字是 backends 展开出来的，
  它们的父块同样没有标记）。

## 四、26 条红，逐条

| # | 计划 | 用例 | 判定行（runner 原文） | known_fail |
|---|---|---|---|---|
| 1 | `feat.parse.frontend` | `frontend.ast_snapshot` | `stdout does not contain 'files read as recorded, 0 not in the snapshot yet'` | 没有 |
| 2 | `feat.opt.camel_passes` | `camel_passes.cse.subject` | `verification exited 1` / `Error: the two products of 'written' do not read two separate reads of the cell: none, none`（`cse_subject.mjs`） | 没有 |
| 3 | `feat.opt.camel_passes` | `camel_passes.cse.by_cpp` | `snapshot differs from golden` | 没有 |
| 4 | `feat.opt.camel_passes` | `camel_passes.cse.by_camel` | `Error: the Camel pass's graph is not the graph camel_passes.cse.dot records: node count mismatch: actual=43, expected=49` | 没有 |
| 5 | `feat.opt.camel_passes` | `camel_passes.cse.property` | `Error: a read of the cell is read by more than the one product it was written for` | 没有 |
| 6 | `feat.opt.camel_passes` | `camel_passes.cse_written_variable.by_cpp` | `snapshot differs from golden` | 没有 |
| 7 | `feat.opt.camel_passes` | `camel_passes.cse_written_variable.by_camel` | `node count mismatch: actual=52, expected=67` | 没有 |
| 8 | `feat.compile.stream` | `stream.generator.nvm` | `expected exit 0, got exit 1` / `expect_stdout_exact differs: first difference at offset 0 (expected 17 chars, got 767 chars)` | 没有 |
| 9 | `feat.compile.stream` | `stream.generator.fvm` | 同 8 | 没有 |
| 10 | `feat.compile.stream` | `stream.generator.jit` | 同 8 | 没有 |
| 11 | `feat.compile.stream` | `stream.generator.prl` | 同 8 | 没有 |
| 12 | `feat.compile.stream` | `stream.generator_default` | 同 8 | 没有 |
| 13 | `feat.trans.cml` | `print_and_recompile` | `expected exit 0, got exit 1` / `AssertionError: … 'DIFFERENT: the two canonical documents are not equivalent\na structural class has 1 vertex/vertices on …'` | 没有 |
| 14 | `feat.trans.cml` | `print_then_run_agrees_with_the_origin` | `expected exit 0, got exit 1` | 没有 |
| 15 | `feat.trans.cml` | `print_then_run_negative_swap_is_seen` | `expected exit 0, got exit 1` | 没有 |
| 16 | `feat.trans.print_determinism` | `gir_prints_one_hash` | `expected exit 0, got exit 1` / `AssertionError: run 1 exited 1:` | 没有 |
| 17 | `feat.linear.var_reference` | `element_assigned_append.in_place_form` | `Error: one :var/take, got 0` | 没有 |
| 18 | `feat.para.exec_pred` | `linear.predecessors` | `Error: predecessors differ at line 9 / expected # 3 <- [control:1, sync:1, data:2] / got # 3 <- [data:2]` | 没有 |
| 19 | `feat.consistency.new_values` | `new_values.sort_rebinding.graph` | `stdout does not contain 'array:sort_d_in_place'` | 没有 |
| 20 | `feat.mm.capacity` | `capacity.perm_query_types` | `expected exit 0, got exit 1` / `Error: no statistics line in stderr:` | 没有 |
| 21 | `feat.mm.gc_poison_sweep` | `poison:modules.resources:core` | `Error: missing: RESOURCES footprint main rmw ambient rng; write ambient stdout; read ambient fs; rmw object (any); external …` | 没有 |
| 22 | `feat.modules.resources` | `core` | 同 21 | 没有 |
| 23 | `feat.modules.array_sort_kernels` | `array_sort_kernels.paths` | `Error: array:sort_i ran 2 times, not 1: {"array:argsort_d":2,"array:sort_i":2, …}` | 没有 |
| 24 | `feat.tutorial_examples` | `tutorial.examples.06_let_to_var` | `output does not contain 'SemanticDiag::IgnoredSideEffect'` | 没有 |
| 25 | `feat.tutorial_examples` | `tutorial.examples.13_graph_rgir` | `edge count mismatch          actual=14, expected=16` | 没有 |
| 26 | `feat.docs_links` | `docs.links.citations` | `expected exit 0, got exit 1` / `…split.plan.toml:127: 'split.cpp:3473': names a line; cite 'split.cpp#Symbol' instead` / `2 unverified citation(s)` | 没有 |

**25 与 26 已在第九节落地。** 24 归文档账，不动（第九节）。

## 五、`9356 entries` 被证伪

那套账说：`[[tests]]` block 6652 / 394 文件，loader 展开 **9791** entries（fast **9356** / slow 164 /
milestone 248 / debug 23）。

实测：**光 `test/plans/feat/**` 一个目录**，`--tier fast` 就 **9553 started**。

- 差别是 **+197**，方向是「账小、树大」：一个子树的 fast 比号称的整树 fast 还多。
- 结论要写清：**那个 9356 不是量出来的，是 loader 算的；而 loader 算出来的值与这里的实测差 197。**
  以后引用 entry 数，要么写「loader 说」，要么写「某棵树实测」。

## 六、524 blocks / 329 fast：骨架是对的，错在 entry 那一层

- **524 完全对得上**：166 + 34 + 62 + 31 + 9 + 3 + 2 + 2 + 69 + 18 + 3 + 125 = **524**。
- **329 fast 也成立**：524 − perf 69 − `probe/refleak` 5 − `debug` 3 − gpu 非 fast 118 = **329**
  （gpu 的 fast 是 `profile` 6 + `reading` 1 = 7）。
- 两处 ±3 互相抵消：`feat` 顶层实测 **166**（账 167）、`probe` 实测 **18**（账 15）。
- 所以那套账的**骨架（目录 → blocks）可以当读数用**，错的是 entry 那一层的数（第五节）。

## 七、本轮不算作红的四条（perf 预算）

| 用例 | 判定行（runner 原文） |
|---|---|
| `startup.cost` | `19.59 s cpu of 17.50 s budget, 20.70 s wall` |
| `collfuzz.many_small` | `198.24 s cpu of 151.00 s budget, 65.85 s wall` |
| `collfuzz.large` | `106.40 s cpu of 100.00 s budget, 39.44 s wall` |
| `showcase_d4.small` | `6.54 s cpu of 6.00 s budget, 6.67 s wall` |

**为什么不算红**：这四条断言的是**成本上界**——`cpu_budget_ms` 是「这次运行值多少 CPU 秒」的界，
`timeout_ms` 是「它还会不会回来」的界（runner 的 schema 把两者分开：预算需要超时，
因为预算是成本的界、超时是存活性界，管的是挂死不是慢）。四条都是「量到的 cpu 秒数超过预算」，
而**量它们的主机 1m load 是 31–45**，工具当场印了 `readings taken now are not comparable`。
同一棵树、同一个二进制，在安静主机上这四条未必红。**记进红单，但不算代码红；要在安静主机上重测才算。**

**第五条 perf 红不是预算红**：`showcase_c3.probes` →
`probes pinned=18 mismatched=1 unclassified=(none)` / `probes mismatch 01_grad_scalar:refused!=ok`。
这是「一个本该被回答的探针现在被拒绝」，是真红。

## 八、量不了的（不写 0 冒充）

| 目录 | 为什么不能量 |
|---|---|
| `test/plans/gpu` | `-Tier all` 下 146 entries：**pass 22 / skip 124 / fail 0**。同一工具在 load 19 时读到的是 **pass 131 / fail 15 / skip 0**——**`skip` 掩掉了 15 条红**，「0 红」在这里是假绿。本轮主机降不到那个负载，`gpu` 本轮不能量。 |
| `test/plans/perf` 的四条预算 | 见第七节：能在安静主机上重测才有读数。 |

其它目录都量到了：`probe` 的 `refleak`（debug 层）用 `-Config Debug -Tier debug` 补量，20/20 通过；
`debug` 目录用 Debug 构建跑通，3/3。

## 九、落地的两条，和没落地的那条

| 提交 | 内容 | 读数 |
|---|---|---|
| `41860384a` | `docs: cite split.cpp by symbol in the split_branch_arms reading`——`split.plan.toml` 里 `split.cpp:3473`/`:3539` 改引 `split.cpp#buildSplitGraph`（两处都在该函数内） | `feat/docs_links` 21/21 |
| `abaeaea65` | `test(golden): re-record 13_graph_rgir after the chain-place refactor`——删掉纯调用 `add` 到 `println` 的两条非数据边（`CTRL` + `SYNC`） | `tutorial_examples` 244：242 → 243 pass |

`13_graph_rgir` 为什么是**记录过期**而不是**图少了两条边**：`add` 是纯的（它自己的 cluster 写着
`(a: int, b: int) => int pure`），而 `docs/design/effects/edges.cn.md` 4.6（`6b105528a` 加的那一条）说
**同步边是一条链、没有冗余，纯计算上不再有任何非数据边**。`println` 已经用数据边
`N05 -> N06 [NORM]` 读了调用的值，那条 `CTRL` + `SYNC` 什么也没多说；效果节点上的那一对
（`N06 -> N08`）原样保留。**记录是 `6b105528a` 漏掉的**：它的正文说「Goldens, tutorial images and
exemplar run logs are regenerated from the tree that produced them」，它重生成了
`test/plans/smoke/golden/rgir.dot`、`feat/opt/golden/*`、`feat/trans/golden/*`，**漏了这一个**。

**`06_let_to_var` 不改，归文档。** 判据两条：

- `docs/design-principles.cn.md`「**给 `var` 赋值不是效应**」（负责人 2026-11-15），代码见 `8b541bdba`；
- `docs/handover/pending-docs.cn.md` 第 23 行把这一条记成**文档账**，并明写
  「**一条都不许用「改期望」的方式消掉**……要**改正文**，不是改断言」「等作者定「新语义该怎么向初学者讲」，再动手」。

教程正文 `docs/tutorial/tutorial.cn.md` §6.5 里**还写着**：

> 不在同步块里的写仍排在它之前写着的读之后……编译器给出 `SemanticDiag::IgnoredSideEffect` 警告。

所以这条红是「教材还在教一条已经不存在的规则」，实现线不动它。**要动的是那句话，和这个用例
`desc` 里「the un-synced write above it is a warning」那句叙述**，等作者定措辞。
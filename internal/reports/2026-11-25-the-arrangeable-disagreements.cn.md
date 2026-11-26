# 可自由安排：转换的那一对类型，和 `oper=-` 剩下的 4238

2026-11-25 ｜ `lane/oper-kinds` ｜ 改动：`src/kernel/facts/facts.cpp#termsOfOperNode`（新）、
`src/stdlib/test/facts_compare.cpp`（基线在转换节点上按对重述、`tally` 表）、
`scripts/check-arrangeable.mjs`（全树扫）、`test/plans/feat/tools/arrangeable.plan.toml`（新）

读数出处（本节全部数字同此）：主机 `88`，树 `/home/zjwei/camel-work/oper-kinds`，
`tip = lane/oper-kinds`，`tier fast`，`jobs 8`，`[TREE]` 自检通过，
`pwsh internal/tools/camel-remote.ps1 -Plan test/plans/feat/tools/arrangeable.plan.toml -NoGpuWatch`。
那两次跑的树上是这一版改动的补丁（后来提交为 `fb179de70f`），机器负载在 18–38 之间（`1m`），
两次都在同一台主机上、相隔二十分钟内取的。
扫的规模：`files=4376 read=1892 skipped=2484 silent=0`——树里 4376 个 `.cml`，1892 个不带自己的
计划参数也能编译并通过 `test::facts_compare`，其余 2484 个要它们各自计划给的参数或模块根
（`check-plan-refs.mjs` 对它扫的 3776 个用例也跳过 1235 个，同一个原因；另有少数是
`test::facts_compare` 自己抛异常，见第七节）。**这是这份读数的前提：
下面所有计数都是这 1892 个用例、128049 个节点上的。** 换手时那份 4397 类来自更宽的网（2736 个文件、
210583 节点），比例与本表接近（3.4% 对 3.75%），两个类别的成因见 1.1/1.2。

## 一、动手前的分歧归因表（1892 个用例，128049 个节点）

按 `(节点种类, 操作)` 分，只列有分歧的行，四列是"事实说可自由安排"与"遍所问的谓词说可移"的四种相遇：

| 行 | 节点 | both_free | both_kept | kind_free_pred_kept | kind_kept_pred_free |
|---|---:|---:|---:|---:|---:|
| `oper` `:op/cast` | 220 | 0 | 61 | 0 | **159** |
| `oper` `-`（FILL/ACCS） | 4238 | 0 | 0 | 0 | **4238** |
| `gate` `-` | 2200 | 0 | 2148 | 52 | 0 |
| `loop` `-` | 277 | 0 | 225 | 52 | 0 |

`kind_kept_pred_free` 共 4397：**`-` 占 4238（96.4%）**，`:op/cast` 占 159（3.6%）。
所以换手时那句"这个分歧类是转换"在比例上不成立——换手给的那一个样本（`oper=:op/cast`）是小的那一块。
两个类别的成因完全不同，下面分开写。

### 1.1 `:op/cast`：kind 读的是算子记录，谓词读的是节点的那一对类型

每行的实测样本（`FACTS_COMPARE reader_diff`，`terms` 是 kind 答的四项 `pure/noExternal/cannotFault/terminates`）：

```
docs/design/cpp-standalone/proto/m5.cml  graph=main node=9  oper=:op/cast kind=0 moved=1 terms=1101 waits=0
showcase/c3-char-transformer/grow/params_after_20_steps.cml  graph=initial node=1 kind=0 moved=1 terms=1101 waits=0
```

- kind 的 `cannotFault` 来自 `termsOfOperator`：`:op/cast` 的记录写了 `panic`，所以对**每一个** cast
  都为"可能故障"——这是算子的答案，不是节点的。
- 谓词（`runtime::may_be_rearranged`）对 `OperShape::Conversion` 走的是
  `castMayFault(结果类型, 输入类型)`：`f32[...]` → `f32[2, 3]` 要检查值的形状（可能失败），
  `float[6]` → `f32[]` 每一个值都留得住（不会失败）。
- `waits=0`：**图上早就这么答了**——编译器只在 `castMayFault` 为真时给转换写顺序边
  （`src/compile/src/gir/builder_expr.cpp#visitCastNode`：`if (synced_ &&
  camel::runtime::castMayFault(targetType, sourceType))`）。
  一个能安全转换的 cast 节点在它自己那张图上没有 wait，kind 说"留"是在与它读的图矛盾。

220 个 cast 里 61 个是谓词也说留的（必须失败的那些，例如开秩进定形），159 个是谓词说可移的。

### 1.2 `oper=-`：kind 读不到"这个节点做哪个操作"，谓词按 kind 的形状答

```
docs/tutorial/examples/07_closures.cml  graph=main node=15  oper=- kind=0 moved=1 terms=0000 waits=0
docs/tutorial/examples/07_closures.cml  graph=adder node=2  oper=- kind=0 moved=1 terms=0000 waits=0
```

- `NodeKind::Oper` + `operatorOf == kNoOper` 是 FILL 或 ACCS（`graph_view.h` 的文件注释：视图把这两种
  还没迁移的种类折进 `Oper`，`operatorOf` 如实答 `kNoOper`）。视图**没有**"这个节点做哪个操作"的查询
  （`graph_view.h`：`runtime::shapeOfOperation` 答这个，而"this face has no counterpart yet"）。
- kind 于是走 `termsOfOperator(kNoOper)`：`kNoOper` 的直接返回——四项全 false，即"不知道，所以我答留"。
- 谓词走 `nodeMayFault` 里的 `shapeOfKind`：`Construction`/`Access` → 不会故障 → 可移。
  两边对"`kNoOper` 是什么意思"答的相反，正是
  `internal/questions/2026-11-23-is-this-node-a-real-oper-record.cn.md` 记的那件事。

### 1.3 `gate` / `loop` 的 52：反方向，kind 更慷慨

`kind=1 moved=0 terms=1111 waits=0`（`docs/tutorial/examples/06_sync_body.cml` 等）：kind 对分支/循环
把两条臂/循环体里节点的四项做了合取（这里都成立），谓词只问这个节点本身（锚点，可能有效应）。
这是"谓词不走进区域"的老缝，不在本次范围，读数留在表里。

## 二、改了什么

`core.arrangeable` 在一个 OPER 节点上问**节点**的那对类型，而不是只问算子记录：

- 新函数 `facts.cpp#termsOfOperNode`：先取算子记录的四项（`termsOfOperator`），**仅当**
  `runtime::shapeOfOperator(oper) == OperShape::Conversion` 时，把 `cannotFault` 改成
  `!runtime::nodeMayFault(GCNodeKind::Oper, oper, view.typeOf(ref), view.typeOf(唯一输入))`；
- `ArrangeWalk::at` 的 Oper 分支改为调它。

为什么不另写一份从类型推的答案：内核读"这对类型能不能失败"的地方只有一个
（`runtime::nodeMayFault`，里面的 `castMayFault`），而谓词本身就是它拼出来的；kind 问那里，
两者对转换就**不可能**分歧——一个函数、一对类型。同时 `nodeMayFault` 对每一个非转换算子的答案
与记录的四项相同（元素级的那条细化要 `fixedShapes`，两个调用方都不传），所以别的算子行不会动。

`ReadLog` 在转换节点上多记了一个 `log.node(输入)`：输入的类型是这次回答读的东西的一部分。

## 三、读数（同一台主机、同一棵树、同一次扫：`20261125_023439` 改前 / `20261125_025258` 改后）

每个种类的四列（`both_free / both_kept / kind_free_pred_kept / kind_kept_pred_free`）：

| 种类 | 节点 | 改前 | 改后 |
|---|---:|---|---|
| `call` | 3828 | 0 / 3828 / 0 / 0 | 0 / 3828 / 0 / 0 |
| `data` | 37589 | 37589 / 0 / 0 / 0 | 37589 / 0 / 0 / 0 |
| `func` | 6059 | 0 / 6059 / 0 / 0 | 0 / 6059 / 0 / 0 |
| `gate` | 2200 | 0 / 2148 / 52 / 0 | 0 / 2148 / 52 / 0 |
| `loop` | 277 | 0 / 225 / 52 / 0 | 0 / 225 / 52 / 0 |
| `oper` | 50865 | 13962 / 32506 / 0 / **4397** | 13962+159 / 32506 / 0 / **4238** |
| `port` | 27231 | 27231 / 0 / 0 / 0 | 27231 / 0 / 0 / 0 |

`oper` 里只有一行动了：

| 行 | 改前 | 改后 |
|---|---|---|
| `:op/cast`（220） | 0 / 61 / 0 / **159** | **159** / 61 / 0 / **0** |
| `-` FILL/ACCS（4238） | 0 / 0 / 0 / 4238 | 0 / 0 / 0 / 4238 |

也就是：转换的 159 个从"kind 说留、谓词说可移"走到"两边都说可自由安排"，61 个两边都说留的不变，
两个方向的分歧都是 0；**同一个种类里另一个类（`oper=-`）一位没动，其余种类一位没动**。
`ARRANGEABLE_KIND` 的列逐字相同（`call`…`port` 七行），这是这句话的读数形式。

## 四、基线 `facts.compare_arrangeable` 动了哪一条，为什么

`compareArrangeable` 比的是"现在的事实"与"它替换掉的那个答案"，而那个答案对 OPER 节点只有
`operatorArrangeable(oper)`——**它手里没有类型**，对转换只能说"一个 cast 可能故障"。这不是节点
的四项，只是算子的那一半。改了两处，逐条说明：

1. 对转换节点，旧答案在比较处按节点重述：`cannotFault = !castMayFault(结果类型, 输入类型)`，
   并新增计数 `conversions=`（走的节点数）。这不是把基线写松：替换掉的算法本来就在节点上读
   `castMayFault`（`graph.h` 的 `nodeMayFault`），所以这份重述更忠于"它替换掉的那个答案"，
   而 `operatorArrangeable(oper)` 从来没答过这个问题。位置照 `OldRegionWalk` 的惯例：
   基线写在比较它自己的文件里。
2. 既有断言一条没改（`diff === 0`、`same + no_old === nodes`），比较的范围反而变大：现在每个
   转换节点也在比较之内。**改前用这条重述去比，新用例 `test/cases/facts/conversions.cml` 读
   `graphs=3 nodes=15 same=6 diff=1 no_old=8 conversions=2`，改后 `diff=0`**——这条基线在改前
   是红的，改后才是绿的。它自己原来的两行程序一位没动：
   `tensors_and_loops.cml` 的 `same=11 no_old=29 diff=0 conversions=0`（那个程序一个转换都没有）。

对应的用例（`facts.arrangeable_conversion_pair`，同一棵树、同一次跑）：改前
`FACTS_COMPARE tally node_kind=oper oper=:op/cast nodes=2 both_free=0 both_kept=1 kind_kept_pred_free=1`
而 `readers nodes=15 agree=14 diff=1`；改后 `both_free=1 both_kept=1 kind_kept_pred_free=0`、
`diff=0`。断言是：可安全转换的那个必须两边都可移，会检查形状的那个必须两边都留——**同一个算子
两个答案，只有那一对类型能把它们分开**。

## 五、剩下 4238：这一车道不修，因为不是"类型"的问题

`oper=-` 的 4238（96.4%）要求 kind 知道"这个节点是 FILL 还是 ACCS"，而它读的视图答不了；
谓词答得了，因为它拿的是 `GCNodeKind`。修法与本次同形（问同一个缝），但它要的是**视图新增一个
`shapeOfOperation` 对应物**（或让 `ArrangeWalk` 在 `kNoOper` 时走到持有图的运行时查询），
再加草稿那一半的对应物，是一份独立改动。判断：**两边的答案里，kind 这一侧该改**（FILL/ACCS
不是"不知道"，视图只是没把它的形状交出来；说"不知道"会让 4238 个节点留在书写的位置，而这些
节点一个都不会故障）。但那不是本车道交出去的那一类，也不是本次可以顺带做的尺寸——
这一节把大小和方向留在表里，交给下一份派工。

## 六、没动的

`termsOfOperator`、`operatorArrangeable`、`arrangeableKind`、`nodeArrangeable` 的语义；
`src/kernel/runtime/graph.cpp`（只读）；`docs/`、`src/compile/`、`src/stdlib/autodiff/`。
`is_chain_statement(kind, oper)` 那一版仍问 `operatorArrangeable(oper)`，对转换是盲的——
另一处缝，记在这里，不在本次。

## 七、扫的覆盖面里还有一件不是本车道的事

`read=1892 skipped=2484` 里，大部分是"不带各自计划的参数/模块根就编译不过"，但也有一条不是：
有些程序上 `test::facts_compare` 自己抛 `PassInternalError`，`{1}` 以
`fact kind 'core.arrangeable'` 开头——即这个事实种类在被读的时候抛了异常，而这个现象在**改前
的那一次扫里就有**（两次 `read`/`skipped` 都是 1892/2484，同一张网的两侧）。它把那些程序从表里
拿掉了；换手时那份 4397 类是在 2736 个文件的网上取的，本来就更宽。记在这里，不修：它不是本车道
动了的那条缝，但下一份派工要先把这条弄明白，否则"全树"永远只有 43%。

## 八、验收读数

提交那一轮（同一台主机、`jobs 8`、`tier fast`）：

- 提交之后在同一棵树上又跑了一次（`[provenance] tip` 就是这一提交，`build-info.json` 标
  `(clean)`，`[SUMMARY] total: 11, pass: 11, fail: 0`）：`facts` 9/9、
  `facts.arrangeable_every_operator_agrees` 93 s 绿。下面几条来自提交前那一轮，代码相同。
- `feat/tools/facts`：9/9 `[PASS]`（`facts.compare_arrangeable`、`..._readers`、`..._readers_calls`、
  `..._draft` 都在）。
- `feat/tools/arrangeable`：2/2 `[PASS]`。`facts.arrangeable_conversion_pair` 在改前是红的
  （`both_free=0`），改后绿；`facts.arrangeable_every_operator_agrees` 92 s，读的就是第三节那张表。
- `feat/para` + `feat/linear`：`[SUMMARY] total: 998, pass: 941, fail: 53, skip: 2,
  known failures: 2`。53 条红逐个按 `(计划, 用例)` 名对
  `internal/reports/2026-11-24-the-base-fast-layer-reds.tsv` 比过：**0 条在名单外**
  （名单里 para/linear 共 86 行，这一轮 53 行发生）。
- `-Check all,plan-debt`：除 `plan-debt` 外全部 `exit 0`（`check-doc-links`、`check-grammar`
  是打印了 `exit 0` 的那两条，其余成功时静默）。`plan-debt` 读
  `13 claim(s) in 431 plan file(s) ... 0 claim(s) contradicted by the run`，红在两行
  **`(no entry)`**：`test/plans/feat/compile/chain.plan.toml` 与
  `test/plans/feat/compile/state_escape.plan.toml` 各有一句 `RED TODAY` / `not yet` 写在条目之外，
  "not attributable to one entry, so nothing can check it"——两个文件本车道一位没碰，是分支点上
  就有的账，不是这次带进来的；其余 11 条 `unmeasured at this revision` 是那一轮只选了 36 个计划，
  `plan-debt` 自己说不算失败（"the run that did not select the plan is named beside it"）。

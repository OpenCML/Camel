# 旧节点种类清零：计数、两条语言侧问题、停在哪

路线三剩下的最后一步是「旧节点种类的构造点与引用清零」。`CAST`/`COPY` 已经清零（
`:op/cast`、`:op/copy`），仍在的是 `FILL` 与 `ACCS`。这一页给三样东西：可重跑的计数、
清零真正卡住的两条语言侧问题、以及为什么这不是一条车道能定的。

基线：`origin/integration` `449a12bb84040f30083c5cb62502772f853f1033`；下列数字都在这个提交上量。

## 一、计数（一条命令，构造点与读者都数）

```powershell
node scripts/count-graph-readers.mjs --rev HEAD src
```

```
src  graph=2523  kind=118  addNode=96
total  graph=2523  kind=118  addNode=96  dirs=1  rev=449a12bb8404
exit 0
```

这条命令**两个都数**，两个是不同的东西：

- `kind=` 是**读者**：`(NodeKind|GCNodeKind|Kind)::(Cast|Copy|Fill|Accs|CAST|COPY|FILL|ACCS)`
  的匹配行数；
- `addNode=` 是**构造点**：`add(Cast|Copy|Fill|Accs)Node` 的匹配行数（含声明、转发与注释，
  不只调用）。

一个必须写下来的限制：脚本数的是 `--rev` 指的**那个提交**（`git grep <rev>`），不是工作树。
没有提交的改动它读不到 —— 所以「改前/改后」只有在提交之间才有意义，工作树里的改动要
先提交再量。

### 三个数拆开

`kind=118` 不是一件事，是三件事：

| 读数 | 数 | 是什么 |
|---|---:|---|
| `GCNodeKind::Fill` / `::Accs`（C++ 读） | 97 | 这两件真在清零范围内的读者 |
| `NodeKind::Fill` / `::Accs`（`src/stdlib/opt.cml`） | 1 | 同一个运行期枚举，经 `registerNativeType("graph","NodeKind",…)` 交给 Camel 读 |
| `GCT::NodeKind::CAST` / `::ACCS` | 20 | **前端 GCT 自己的枚举**（`src/compile/include/camel/compile/gct/node.h`），与运行期节点种类不是一件事 |

⇒ 旧节点种类的读者是 **98**，不是 118；那 20 处换成什么都不会到 0，它们本来就不属于这件事。
判据若照字面要求 `kind=0`，是要求一件做不到的事。

97 处 C++ 读者的形状：

| 形状 | 数 | 说明 |
|---|---:|---|
| `switch` 的 `case` 臂 | 51 | 每一臂有自己的体：读载荷、出 dump 的列、决定形状 |
| 判定/比较（`nodeIsKind`、`kind ==/!=`） | 38 | 判完跟着一段体，是同一个读法的入口 |
| 构造写入（`.kind = GCNodeKind::Fill/Accs`） | 4 | 在 `draft_construct.cpp`，就是构造点本身 |
| 名字表（`{GCNodeKind::Fill, "Fill"}`） | 2 | `stdlib/graph/nodekind.cpp` |
| 注释 | 2 | 不计 |

机械判等（38）与 `case` 臂（51）都是真读者：拿掉 kind 之后它们要么变成空转的一臂、要么
判永远为假，得逐处改成问算子记录的形状（`shapeOfAccess`/`shapeOfOperation`）—— 这一步是
**减法**，可以做完，但它要等构造点先清零，否则同一件事在读法上还有两种形状。

`addNode=96` 的拆开：

| 读数 | 数 |
|---|---:|
| `addFillNode` 的匹配行 | 28 |
| `addAccsNode` 的匹配行 | 68 |
| `addCastNode` / `addCopyNode` | 0 / 0 |

96 行里含 4 个 `draft.h` 声明、4 个 `draft_graph_builder.h` 声明、4 个 `draft_construct.cpp`
定义、4 个 `DraftGraphBuilder::add*Node` 转发体、2 行注释。真正的调用点 75 个（`x.add*Node(`），
其中 4 个是转发（`draft_graph_builder.cpp` 里转给 `draft()`），所以**生产者 71 个**：
`addFillNode` 17、`addAccsNode` 54。

| 生产者落点 | `addFillNode` | `addAccsNode` | 合计 |
|---|---:|---:|---:|
| `src/stdlib/autodiff/`、`src/stdlib/transforms/`（冻结） | 11 | 48 | **59** |
| 冻结之外 | 6 | 6 | **12** |

冻结之外的 12 个：前端 `builder_control.cpp` 2+2、`builder_expr.cpp` 1；`stdlib/test/loop_collect.cpp`
2+1；`stdlib/tiles/src/graph_values.cpp` 1、`region_outline.cpp` 3。

**这 12 个今天一个也清不了**，原因不是它们难改，是下面两条问题在它们身上同样成立：
它们是「这条种类还是唯一可用的形状」的 12 个落点，不是 12 个可以逐个擦掉的笔误。

## 二、卡住的两条，都是语言侧

### (a) 一个构造「会不会故障」，两侧答得不一样

- 现在按**种类**算的一侧：`nodeMayFault` 对 `Fill`/`Accs` 直接答 `false`
  （`src/kernel/runtime/graph.cpp:135-137`：`Construction`、`Access` 都归在「不会故障」这一臂）。
  因此 `may_be_rearranged` 为真，这些节点**可以自由重排**。
- 现在按**算子记录**算的一侧：`:op/fill`、`:op/access`、`:op/access_key` 的记录写的是
  `.mayFault = true`（`src/stdlib/std/builtin/executors/builtin.cpp:815-817`）；一旦节点是
  OPER，`nodeMayFault` 走记录（同文件 `:161`），答案是 `true`，`may_be_rearranged` 为假，
  节点**被留在书写的位置**。

两侧的读数差：不是「一部分节点变了」，是**全部**。一个节点从种类形状换成算子形状，它的
`mayFault` 就按形状翻转，没有中间态。2026-11-25 在 1892 个程序上量过这一类节点是
**4238 个**（`oper=-` 那一栏，`internal/reports/2026-11-25-the-arrangeable-disagreements.cn.md`
第 140–148 行）；打开构造点，就是这 4238 个一起从「立即数、可搬」变成「可能故障、不许搬」。

建议：**以记录为准**，并且承认记录说的是「这条算子可能故障」，不是「这一次构造可能故障」。
代数的 `fill(template, …fields)`、`access(place, x)` 是程序自己写的调用，`template` 装不进目标类型、
成员不在，都是运行期事实，`true` 是对的；而编译器自己写的构造在编译期已经把类型对过了。
同一个算子、两种来源、两个答案 —— 要分开它们，得由语言说「故障是不是这条算子所有书写共同的
性质」。`builtin.cpp:795-807` 的注释已经把这个代价写明了（转换那一臂能按类型对答，记录不能）。

为什么是语言侧：设计原则 3.9 把「会不会故障」定为算子记录的一项。两侧并存时选哪一侧，
改的是**程序允许被怎样重排**，即程序的意思，不是实现的写法。

### (b) FILL 的载荷能说「哪几个成员填了值」，调用说不了

- 种类形状：`GCFillBody` 带着 `slotCount` 与一列 `slots`（`src/kernel/include/camel/runtime/graph.h:553-581`），
  值以 `With` 输入按同一顺序给出。前端对一个有计算成员的复合字面量**可以换序、可以稀疏**：
  `compositeWithHolesInType(…, holeSlots)` 的结果就是「计算出的成员落在目标类型的第几位」
  （`src/compile/src/gir/builder_expr.cpp:160-171`，用它的地方在同文件 `:298`）。
- 算子形状：`:op/fill(template, …fields)` 的第 i 个 `fields` 就是目标的第 i 个成员
  （读者按这个读，`src/compile/src/gir/builder_helpers.h:118-121`）。没有位置参数，就没有办法说
  「只填第 1、第 3 位」。

两侧的读数差：71 个 Fill 生产者里，能产出**非 `0..n-1`** 槽表的只有 1 个 —— `builder_expr.cpp:300`
那一处（前端复合字面量）。其余 16 处写的就是 `0..n-1`，`jvp.cpp:1001` 与 `split_internal.h:597-599`
的注释也这么写（「模板在最前，值跟在后面」），亦即**它们本来就已经是调用形状，只是存成了种类**。
换句话：这次卡住的是**语言里的一种写法**（命名位置给出另一种字段顺序、或有计算成员的字面量），
不是 71 个调用点。

`builder_control.cpp:359-362` 的注释已经承认这件事：「一种填序与目标排布不同的构造，没有
`fill` 的调用能命名它，它就还是 FILL，直到算子能说出那个顺序。」

建议：把「填哪几位」做进算子的**声明**（一个位置/顺序参数），让调用能说出这个排列；不要把它
塞成第二个载荷 —— 那就是第二份真相。这一条改的是算子的签名，正是路线三说「只在一处」的那一处。

为什么是语言侧：签名是语言的声明，车道不能自己发明一个语言没声明的参数；而保留两种形状
正是今天没有第二份真相的唯一办法。

## 三、这一轮没做的

- **0 个源码提交**：分支 `lane/kinds-oper` 的源码树与基线逐字相同（只有本页）。
  `src/stdlib/autodiff/`、`src/stdlib/transforms/` 一行未动。
- **`:op/` URI 改名没有做**，也不在这条分支上。尺寸：`git grep -l ':op/'` = **676 个文件**，
  按扩展名约 **4558 处**（`.md` 1869、`.cpp` 898、`.cml` 539、`.toml` 529、`.txt` 179、`.h` 174、
  `.dot` 145、`.mjs` 97、`.py` 50，其余零星）。而且**改到哪去还没有定**：算子记录那条裁定只说
  「编译期声明记录带上 `OperId`」，明说「`:op/…` 归核心还是归模块不因此改变」。一次改 676 个文件
  的机械扫替换，会把「名字归谁」这个语义问题藏进 diff 里，不适合和清零放在一条分支。

## 四、控制跑（源码未改的树上）

```
pwsh -NoProfile -File <tree>\internal\tools\camel-remote.ps1 -Path <tree> `
  -Plan test/plans/feat/runtime/oper_shape.plan.toml -Jobs 4 -NoGpuWatch
```

- 退出码 **0**；`[SUMMARY] total: 2, pass: 2, fail: 0`；
  `[PASS] runtime.oper_shape.the_operator_and_the_kind_answer_one_shape`、
  `[PASS] runtime.oper_shape.the_registry_states_one_shape`。
- provenance：`tip 449a12bb8`（未提交改动 1 个：本页）、binary 构建于 2026-11-26T16:19:41+08:00、
  `tier fast`、`jobs 4`、`[TREE] clean`；`check-doc-links` exit 0。
- 与晋级 9 红表（`internal/reports/2026-11-26-promotion-9-reds.tsv`，630 行，从 `5450962ad` 读）比：
  **`runtime.oper_shape` 不在表里**，所以这一跑没有可比的失败行；两份读数互不矛盾。
- 这一跑的宿主 load 61–63（同一台机上有 7 条别的运行），所以**时间不作基线**，只当名字集用。

## 五、顺带读到的一条，不属本车道的改动

`runtime.oper_shape` 的第一个用例今天**没有跑到它要钉的地方**：它自己的 NOTE 写明了
（`test/plans/feat/runtime/oper_shape.plan.toml:15-30`），`access`/`access_key` 的位置与键是
`with` 参数，而用例仍当第二个普通实参传（`test/cases/runtime/oper_shape/two_forms_one_shape.cml:28`
与 `:32`），所以在语义分析就停住，`expect_stdout_not_contains = ["shape=none"]` 那一格是空转的。
「一个构造的两种写法答同一个形状」这个守卫，现在没有真正执行。

要它真执行，是它自己的一笔改动：把两处调用改成 `with` 实参，`via_cast` 那条按 NOTE 第 3 点处理，
再按新的打印重写期望。修好之后它才是「两种形状同答案」的读法 —— 也就是 (a)(b) 两条被裁决之后，
第一步应该落的地方。
# 内联器那条环守卫已经走过 `order` 边，而今天的语料没有任何一处需要它

`resolvePlaceForks`（`src/runtime/draft_inline.cpp#resolvePlaceForks`）把一条语句挪到它所在块的链尾
之前，先问「这个链尾等不等这条语句」。这个走法早期只读四组输入，漏了 `order` 一组，于是链尾只沿
`order` 边回到这条语句时看起来是自由的，挪动就闭合了一个输入环，封存再把它当成「输入环」报出来。

这一处**已经补过**：`bf12366ba`（合并 `d603372fc`）在 `waitsOn` 的输入组链上加了
`draft.orderInputsOf(node)` 一行，与 `norm`/`with`/`ctrl`/`sync` 并列，形状与
`src/runtime/control.cpp#reduceOrderingEdges` 走 `orderUsersOf` 的写法一致。所以本条车道没有第二行
可补，再补一次就会出现两条相同的分支。

`include/camel/runtime/draft.h#DraftEdgeKind` 列四类（`Data`/`Sync`/`Order`/`Ctrl`），`Data` 按
`DraftEdgeSlot` 的两个槽（普通端口、上下文端口）分开，于是可走的输入槽是五类；
`include/camel/runtime/cycle.h#CycleEdgeKind` 直接列五类（`Norm`/`With`/`Ctrl`/`Sync`/`Order`），
`kCycleEdgeKindCount` 是 5。守卫的五组与这五类一一对应：五类、走了五类、差 0 类。

## 执行读数：这一支会被走到，但从不决定答案

用 `std::opt::inline` 跑 `test/cases` 下的 3660 个程序，跑了其中 2745 个（四片里三片跑完，第四片
卡在一个不返回的程序上，已停），在守卫的走法上记下每组投进栈里的输入条数（探针不提交）：

| 程序 | norm | with | ctrl | **order** | sync |
|---|---|---|---|---|---|
| `test/cases/compile/static_expansion/static_tuple_indices.cml` | 172 | 8 | 63 | **16** | 51 |
| `test/cases/consistency/entering/arm_cell.cml` | 636 | 120 | 781 | **750** | 124 |
| `test/cases/opt/adb67e081/rc04_seed72.cml` | 0 | 0 | 0 | **0** | 0 |

第一、二行说明这一支确实被执行过；第三行说明 `rc04_seed72` 一次都没走到它。

为了看这一组是否**决定**答案，同一次运行里并排跑了两份走法：一份五组（`waits5`，即今天的代码），
一份去掉 `order`（`waits4`，即补之前的代码）。这 2745 个程序里一共出现 52 对（分叉语句，链尾）：

| 读数 | 对数 |
|---|---|
| `waits5=0 waits4=0` | 26 |
| `waits5=1 waits4=1` | 26 |
| `waits5=1 waits4=0` | **0** |

`waits5=1 waits4=0` 才是「只有 `order` 边让它拒绝」的形状，一处也没有。也就是说：这一支被走过，
但在今天的语料上，它一次都没有把答案从「自由」改成「不自由」。

## `rc04_seed72` 已经不再分叉

`rc04_seed72` 在 2026-11-02 的老构建（`out/latest`，四类边的树）上是红的，报
`node 24 (11): lies on a cycle of inputs: 24(11) <- 27(13) <- 38(13) <- 24`；在当前树上 exit 0、
无输出。但这道绿今天不由守卫解释：`a1723fbb9`（「The cause is in the splice, not in the check」）
在 `bf12366ba` 之后才合入，它把拼接的位置改成「调用所在那条语句之后」，于是分叉不再产生——
`resolvePlaceForks` 照常被调用，但每次 `forked=0`，守卫的移动分支整段不进入。

实测：把 `draft.orderInputsOf(node)` 那一行去掉、在当前树上重建，`rc04_seed72` 仍然 exit 0、
仍然 `forked=0`，与保留那一行逐字相同。`test/cases/opt/inline_cycle/seed23_fold_inline.cml` 在
`std::opt::inline`、`std::opt::fold std::opt::inline`、`std::opt` 三条流水线下同样 `forked=0`。

所以 `test/plans/feat/opt/adb67e081.plan.toml` 里「Green since the place-fork guard walks the order
edges」这句话今天只对一半：那道绿现在由 `a1723fbb9` 解释；把守卫的 `order` 一行删掉，那个条目不会
变红——它守不住这一行。这条留给出题人裁定，不在这里改计划。

## 这一行的状态

代码在，形状对，且被走过（`order` 投过 16 条、750 条）；但没有任何一条读数是「它必须存在」。
照规矩说清楚：**它是被遍历的，不是被判定的**。「走不到也要补」在这里的形态是「走到了，但今天不
决定任何答案」——不许把它写成「已覆盖」，因为覆盖的证据本该是那 0 行的 `waits5=1 waits4=0`。

## 这一轮的快层闸门

`npm run test`（fast 层，`f56c65587` 的树加这一页）：

```
[SUMMARY] total: 8855, pass: 8736, fail: 46, skip: 41, known failures: 32
```

`killed` 是 0：`[SUMMARY]` 只有 `crashed > 0` 时才追加 `, killed by a signal: N`
（`test/tools/run-tests.mjs#printRunSummary`），这里没有那一截。46 个红分在 14 个计划里
（`modules.threads`、`consistency.new_values`、`modules.rules`、`tools.load_class`、
`docs.links` 的 `docs.snippets_compile` 等），都不是这一页能碰到的：读 `docs/handover/reports`
的计划只有 `test/plans/feat/showcase_s2.plan.toml`（这一层跑了，绿）与
`test/plans/perf/showcase_c2.plan.toml`（perf 层，不在这一轮）。`docs.snippets_compile` 自己点名了
它的两处（Windows 上绝对路径渲染成 `D:<file>`、教程 17 的一条 pending 片段），都不是这一页。
`docs.links.questions_index` 在这一轮是 `[PASS]`——这一层在本机不是绿的，但红的位置与本车道无关。
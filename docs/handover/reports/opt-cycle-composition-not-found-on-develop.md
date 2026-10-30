# 环：`develop` 上造不出那条用例，以及与之相关的两个计数

本轮基于 `origin/develop` = `75e632c8d`（会话开始时是 `76689285d`，期间前进了两次）。

## 结论

`75e632c8d` 上**找不到**把多条各自合法的改写合成环的程序。把守卫那一支拿掉后，整套 fast 层
`7898` 条**一条都不红**；再加 276 个按边分类生成的形状，也没有一次 `lies on a cycle of
inputs`。`draftHasCycle` 现在在 `develop` 自己的语料上**从未触发**——缺的不是用例，是能合出环的
那对改写。

## 计数器（同一棵树，只差守卫那一支）

| 树 | SUMMARY |
|---|---|
| 带守卫（`75e632c8d`） | `total 7898, pass 7889, fail 0, skip 7, known 2` |
| 拿掉 `if (draftHasCycle(...)) return program;` | 与上一行完全相同 |

图快照类（`std::gir` / `std::jgir` 的 golden）也全照旧。守卫一旦触发就丢弃会话、留下未优化的图，
那些 golden 必然变。所以这张表既说明守卫零触发，也说明「拿掉守卫 → 用例红」在今天的 `develop`
上没有可红的对象。

## 每类边各试了什么

| 边 | 产生它的改写 | 试法 | 结果 |
|---|---|---|---|
| `norm` / `with` | cse 合并里 `replaceNode` 的 `replaceAllValueUses`，把用户改接到被保留的节点 | 240 个形状：5 个 body（纯值 / 带 `println` / 带 `var` 写 / `sync` 块）× 3 个等价重复表达式 × 4 个前置 × 4 个后置语句，各走 5 遍流水线与 `std::opt` | 0 |
| 同上，经函数值 | `std::defunctionalize` 把闭包调用降成直接调用，inline 才有东西可搬 | 36 个形状：具名函数经高阶函数（`apply(worker, 2)`）× 3 种重复表达式，三条流水线 | 0 |
| `order` / `ctrl` | 内联 splice：`draft_inline.cpp:1413 addSpliceOrderEdges` 把搬进调用者块里的 body 节点排到调用语句前后 | 与上面同一批形状，调用一定夹在两个语句之间，两个方向的 order 边都出现 | 0 |
| `ctrl` | `pruneBranch` 给失去读者的效果补 `appendControlDependency`（`generic.cpp:904`，本身有 `isComputedAfter` 守卫） | 由整套 fast 层的分支/折叠用例覆盖 | 0 |
| `sync` | `replaceNode` 的 `chainTo` / `handOverSyncWait`，erase 时把链位交给后继 | 由 `sync` 块、链与效果的用例覆盖 | 0 |

本机还拿 `main`（`7bc47d6a2`，v0.2.37）当探针：`test/cases/opt` 的 143 个用例与上面 276 个形状
都是 0。`main` 的 `reaches` 比 `develop` 少走 `sync` 边、合并更放得开，所以它不报比 `develop`
不报更弱，不能反推。

## 为什么 `develop` 上造不出来

不是守卫挡住（上表），是**这种改写组合不存在**：`develop` 里唯一写 `order` 边的是内联 splice，
它只把 body 节点排在调用者同一块内前后两个语句上；能被它这样排的一对，在 cse 的 key（op / 去掉
gate 后的输入 / 区域 / 权限）和 `reaches`（`norm`/`with`/`ctrl`/`sync`）两处都过不去，合并不会
跨过那条 order 边。报过环的树（`lane-eff-rule` 合入 `develop` 前的 `4af274872`）自带建图侧改动
（`builder_helpers.h`、`draft_inline.cpp` 的 `waitsOn` 补 order、`gates.cpp`、
`graph_regions.cpp`）：边在建图阶段就多出来了。守卫的 note 与它的 7 次 witness A/B 结论相同，
本报告只是把「零触发」用整闸坐实。

## 要造出来需要什么条件

1. 一次改写写出 `order` 或 `ctrl` 边，跨越另一对**值等价**的节点（今天 `develop` 只在内联 splice
   里写，且不跨越值等价对）；
2. 其后一次合并让某个用户改读被跨越的那一端，把「顺序」翻成「值」；
3. `reaches` 与 cse 的 key 都看不见这一对——两边只能靠 `order` 相连。

满足第 1 条要动的不是测试，是建图或内联里多写一条 order 边；那正是那条车道做的事。

## 一个会咬人的细节

守卫 `draftHasCycle` 在 `src/passes/opt/generic/rewrite.cpp:470`，调用点是同一文件的 `:552`；它的遍历走 **5** 类边（`norm` / `with` / `ctrl` / `sync` / `order`，`while (top.group < 5)` 在
`rewrite.cpp:489`）。冻结的 `findCycle` 在 `src/runtime/graph_edges.cpp:489`，只走 **4** 类
（`norm` / `with` / `ctrl` / `order`，`while (top.group < 4)` 在 `graph_edges.cpp:508`），**不含
`sync`**。所以「守卫触发」不等于「拿掉守卫会报 `PassInternalError`」：只在 `sync` 上成的环，守卫会静默丢弃
会话，程序照常跑。将来写这条用例时，A/B 要红必须是 4 类边上的环；只在 `sync` 上成的环要换判据。

## 没做的

- 全量 `.cml` 语料的本机逐个扫描没跑完（本机只有 `main` 的 v0.2.37，部分程序读 stdin 会挂）。
  替代证据是 head 上拿掉守卫的整闸全绿。
- 没有为「只在 `sync` 上成环」造检测工具，那需要另一次构建与逐图比较。
# 待决：`probe.machine` 这条自称「用例集完成前会被删掉」的探针计划，留下还是删掉

一问：**`test/plans/feat/compile/probe.plan.toml` 里的 `probe.machine`，现在是留下（它还守着什么），
还是删掉（它守的东西已经有人守了）？**

它自己写着 `desc = "A probe, removed before the case set is finished."`，作者 `c5f84f918`
（"feat(gir): a stream is one value across the closures that captured it, and one read per path"）
加进来，之后只被 `859b80d2a`（模块声明那笔大扫除）碰过一次，**今天仍在跑，而且 PASS**。

## 三个事实

**一、它测什么。** 一条 `kind = "behavior"`、`tier = "fast"` 的用例，编译并执行
`test/cases/compile/stream/probe_machine.cml`，要 `stdout` 恰好是 `3\n3\n`。那个程序**不是**生成器，
是**手写的、生成器降级之后长出来的那个形状**：把续跑点、实参和循环自己的状态放进一个
`type CountState = { pc: int, n: int, i: int }`；把 body 拆成按 `pc` 分派的段，每段回一个
`(元素, 新状态)?`；再用一个 `step` 函数和一个 `pull` 把状态变成 `int..` 这个流值。所以它固定的是
**降级的目标形状**（状态机 + 段 + 步进函数）在语言层面确实可写、可跑、语义正确。

**二、为什么当年是探针。** 它随那笔 GIR 改动一起进来，那笔改动的主题正是「流是跨捕获它的闭包的
**一个**值，每条路径只读一次」——也就是流/生成器降级正在成形的时候。那时把目标形状手写出来跑一遍，
是给降级器一个能对照的样板；作者当时打算在用例集补齐后删掉它，所以才在 `desc` 里写了那句话。

**三、今天有没有别的用例守同一件事。** 有，而且是从另一头守的：`test/cases/compile/stream/` 下现有
**12 条 `generators_*.cml`**（`generators`、`generators_body_fault`、`generators_cell_retype`、
`generators_cell_type`、`generators_destructured_local`、`generators_early_stop`、
`generators_error_element`、`generators_local_across_yield`、`generators_loop`、`generators_match`、
`generators_no_sync`、`generators_sync_order`），在 `test/plans/feat/compile/stream.plan.toml` 里有
**16 条计划项**，它们**直接写 `yield` 的生成器、真让编译器去降级、再跑结果**。此外没有第二棵树上的
了——`git ls-tree` 里没有 `stream` 相关的 `gir` 快照 golden，也就是说**降级出来的图长什么样，今天没有
任何用例固定**。

## 判断的落点

- 若这一问要的是「降级的目标形状还可写、还能跑」——`generators_*` 那 12 条已经从**输入端**守住了，
  而且守得更强：它们跑的是真降级，`probe.machine` 跑的是一份**手写的复刻**，降级器若漂移，它不会红。
- 若这一问要的是「降级出来的**图**是这个形状」——今天谁都没守，`probe.machine` 也没守（它没有经过
  降级器）。要守它，得给一个 `count(3)` 的 `std::gir` 快照，那是新用例，不是这条探针的复活。
- 因此**按现状它守不住任何会坏的东西**：它是「这份手写样板仍然符合语言」的回归用例，而这句话在今天
  已被 `stream.plan.toml` 的 16 条覆盖。删掉不会减少覆盖；留下也不算错，只是它 `desc` 里那句
  「用例集完成前会被删掉」已经过期，谁读计划都会以为这是一笔未完成的收尾。

**建议（供作者定）**：删掉这条计划项与用例，或在 `desc` 里把它改成它实际守的东西（手写样板仍是合法
程序），二选一都行；**别让它继续挂着一句已经不成立的自我声明**。若作者想固定降级的图，那就把这一问
变成一条新用例：`count(3)` 的 `std::gir` 快照。
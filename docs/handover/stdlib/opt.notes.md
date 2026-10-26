# `opt` 重写说明

稿件：`opt.cml`。**未编译**。三个遍 `fold`、`cse`、`dce` 都是 `::<F>(f: F): F! when is_function::<F>()`，不再有 `module opt` 行与 `Program`。在 `graph` 重写（`graph.notes.md`）之上。

## 变化

| 项 | 处理 | 理由 |
|---|---|---|
| `fold` `cse` `dce` | 签名由 `(p: Program) => Program!` 改为 `::<F>(f: F): F!`；`loss->opt::fold->opt::cse` 可直接调用 | H233 |
| 递归与错误 | `?->` 改普通 `->`（左边是 `F!`，函数形参只收 `F` 时错误原样传下去；末尾用 `!!` 处理） | 作者的裁定：没有 `?->`、`!->`。风险：`cse`、`dead_values`、`fold_rounds` 的形参是泛型 `F`，按“形参收被包装的类型（含泛型 `T`）就原样传入”的规则，`F!` 可能绑成 `F := F!` 而被 `when is_function` 拒绝；若如此需让递归处显式拆开（Q3） |
| 助手函数的名字 | camelCase 改 snake_case（`opText` → `op_text`…） | 约定 3 |
| `sameIntValue`/`sameI32Value`/`sameBoolValue`、`sameIntConstant`/`sameI32Constant`/`sameBoolConstant`、`bitEqualType`（共 8 个） | 合为 `same_value::<T>` 与一个 `same_constant`（`match` 在类型名上分三种），`mergeableConstant` 不再单独判类型 | 一个定义覆盖所有类型；`constant` 现在是 `constant::<T>(n)` |
| `sameNodes` | 用 `zip(xs, ys)->all<((a, b)) => a == b>` | 不再手写下标 |
| `anyWritten` | 删除，直接 `->any<is_written_slot>` | 单次使用的一行包装 |
| `allNodes(p)` | 改为 `all_nodes(gs: Graph[])`，实参 `graphs_of(f)` | |
| `hasHit(p)`、`graphHasHit` | 保留 `graph_has_hit`，`has_hit` 内联为 `graphs_of(f)->any<graph_has_hit>` | |
| `fold` 的 `foldRound`/`foldOne`/`putValue`/`replaced`/`committed`/`abandoned` | 保留（注释里记着 `match` 臂是自己的图，臂里的语句不跑，所以编辑与提交各成一个调用），改泛型 | 语义不变 |
| `dce` | 仍是 `dce` + 私有 `dead_values`（带 `sync`） | `when` 与 `sync` 同在一个签名上的写法没有例子，不冒险 |
| `foldRounds` 里的 `foldHits(p)` | 内联为 `all_nodes(graphs_of(f))->filter<evaluated>` | |

没有改变的：全部算法（合并条件、`keep_of` 取最早的同值节点、`dead_value` 的判据、折叠不动 `is_anchor` 与 `is_written_slot` 的节点）。

## 与 C++ 遍的关系

`std::opt::fold`、`std::opt::cse`、`std::opt::dce` 是 `std.cml` 里的无体声明，默认执行 C++ 版本。本文件的 Camel 版本是它们的语义与参考（H227 / 设计原则 10.4 的“遍上的 `#impl`”）；差分测试保证一致。`#impl` 想连到另一个模块的声明，现有机制只允许本模块私有的无体声明，所以头注释只写了对应关系，没写 `#impl(std::opt::cse)`（Q1）。`std::opt::dce` 另有 `values_only` 参数，本文件的 `dce` 就是 `values_only: true` 的那一半（头注释已写“the value half”）。

## 调用点

`test/cases/modules/graph_pass/`（21 个文件）里的 `opt::fold`/`cse`/`dce` 用例；`test/plans/feat/modules/` 里对应计划；`docs/language/05_modules.md`（及英文）里 `(p: Program) => Program!` 的说法；`docs/stdlib-reference.md` 重生。用例里自己写遍的（`(p: Program) => Program!`）全部改成 `::<F>(f: F): F!`。

## 开放问题

**Q1. `#impl` 跨模块。** 推荐：允许 `#impl(std::opt::cse)` 直接指名 `std` 里的原生遍（它们已经是无体声明），不再要求“本模块私有声明”；否则每个遍要在 `opt.cml` 里再抄一条私有无体声明。

**Q2. 取消 `dce` 的 `values_only`。** `std::opt::dce` 还有“连带门与同步一起删”的另一半；Camel 版只写了值的一半。推荐保持现状，等 H242 之后门与同步的概念稳定再补。

**Q3. 递归处的传播与泛型形参。** 见上表“递归与错误”一行：`merge_hits(f) -> cse` 里，`cse::<F>(f: F)` 的形参是泛型 `F`，传播规则“形参收被包装类型就原样传入”会让 `F!` 被当作 `F`。推荐规则里补一句“带 `when` 条件的泛型形参，条件不满足被包装类型时按只收 `T` 处理，继续传播”；否则这三处要写成 `match` 拆开。

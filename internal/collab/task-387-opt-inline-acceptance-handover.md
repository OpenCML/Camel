# D-1128-51 交接：`opt::inline` 验收还差什么

树 `D:\Projects\Camel-wt\op-names`（`lane/opt-inline`），本卡写成时的枝尖见 `git log -1`。

## 已完成：第 3 节那一对 differential 用例

`test/plans/feat/opt/cxx_isolation.plan.toml` 末尾加了四条语料 × 一对（`by_cpp` 行为 + `by_camel`
differential、`compare_to` 指前者、`compare_fields = ["exit_code","stdout_normalized"]`）：

```
语料            无遍读数（CALL/nodes 多重集）        两边内联后              判据
figure1         0/3 1/2 1/7 1/9        (3 个调用)    0/10 0/15 0/3 1/2 (1)   减少（断言绝对数）
closure         0/3 0/3 0/3 0/5 1/2 4/26 (6)        不变                    不变
budget_zero     0/3 1/2 1/4 1/7        (4)          不变 (growth_budget=0)   不变
recursion       1/11 1/2 1/7           (3)          不变                    不变
```

- 语料：`test/cases/opt/cxx_isolation/inline_figure1.cml`、`inline_closure.cml`、
  `inline_budget_zero.cml`，递归用现成的 `test/cases/opt/camel_inline/self_recursion.cml`。
- **坑一（实测）**：`main(x)` 取参数的写法在 `std::jgir` 下读不到东西；语料照 `camel_inline/`
  的写法把实参写在程序里（`main()` + `let x = 3`）。
- **坑一之二**：一层小函数调用在**前端就展开**（`inc(x)*3 + (x+1)*4` 直接写在 `main` 里，
  无遍读数 `0/3`）。要让图里真有 CALL，被调函数得**再往下一层**（`main → f → inc`）——
  `inline_figure1.cml` 就是照这个改的。
- 读数：`test/plans/feat/opt/cxx_isolation.plan.toml` 单跑 `23/23 pass`，
  provenance 见提交 `93eb4c6038` 那次的 `[provenance] tip ... | binary built ...`。

## 半成：第 4 节读数页的三张图

新加 `test/plans/feat/opt/figure1_pipeline.plan.toml`（6 条全绿），四条 `std::gir` 条目给出
无遍/内联/内联+cse/内联+cse+algebra 的 dot 输出，两条 `by_cpp_inline` / `by_camel_inline`
differential 证明 **`opt::inline opt::cse opt::algebra` 与 `std::cxx::inline opt::cse opt::algebra`
的 dot 逐字节相同**（`compare_fields = exit_code + stdout_normalized`）；algebra 那条还断言
dot 里有 `label="7"`，即结尾是 `x → (+1) → (*7)`。

**没做成的**：把三张 dot 原文取回本地贴进
`internal/reports/2026-11-28-figure1-inline-final.md`，以及算 `sha256`。
- `-FetchFile` 只能取**失败**用例的 `stdout.txt`（`fail/<plan>/<test>/`）；通过的用例在
  `pass/...` 下没有这个文件（实测：`run ... has no pass/opt.figure1_pipeline/...`）。
- 用 `verify_script` 抛整段 dot 也不成：日志把长消息截断，`DOTEND` 根本不出现，
  重建出来的文本是残的（别再用这条路）。
- 下一步：把这四条改成**故意失败**的 `expect_exit=1` 条目（或加一个 `-FetchFile` 能取到的
  fixture 写盘机制），`-FetchFile fail/.../stdout.txt -FromThisRun` 取回 dot，
  本地 `sha256` 后贴进读数页。三张图的节点数从 dot 里数，不要用 `GRAPH_STATS` 的 nodes 混着写。

## 没开始：第 3 项那四条 unaligned（`src/stdlib/opt.cml` 头上 393–409 行）

四条各自的现状与下一手该看哪一行：

1. **调用图强连通分量**（`unaligned 1`）：现在只有 `recurses`（`opt.cml#recurses`，一次可达性走），
   C++ 还读"环在哪"并在**环内**内联 —— `test/plans/feat/opt/camel_inline.plan.toml` 里
   `mutual_recursion` 两边不一致（C++ 9→16 节点，Camel 9）就是这个坑，
   验收要求 `mutual_recursion` 与 C++ 一致。下一手：在 Camel 里用 `callees_of`（`opt.cml#callees_of`）
   算 SCC（不加内核读），把"调用与被调在同一 SCC"和"从环外调进环"分开。
2. **调用是不是分支臂首**（`unaligned 2`，`isDraftBranchArmHead`）：**没查**A/B/C 三处，
   下一手先按 camel-lane §2½ 的四步搜 `graph_view.h` / `graph.cml` 有没有现成读法，再决定是否补内核读。
3. **内联历史**（`unaligned 3`）：遍自己记 —— 在 `inline_rounds` 里记"哪些节点来自哪次内联"，
   下一手看 `expand_calls` / `do_expand`（`opt.cml#expand_calls`）能不能把这份记录带进下一轮。
4. **每图直接调用数**（`unaligned 4`）：Camel 里算即可：`len(calls_of(g))`（`opt.cml#calls_of`
   已经是这个读法），把它接进 `expandable` 的预算判断，不需要内核读。

## 未验

- `@example`：`check-doc-snippets.mjs` 没有 `-Check` 条目（`internal/tools/camel-remote.ps1`
  的 `$checkers` 里没有它），远端跑不了这条检查；`opt.cml:522` 的示例形状与同文件其它过的示例
  一致（`(func (x: int) { ... })->pass`），但这一条**没有读数**。
- 新增红：`feat/opt` 整目录 `1486 total, 1478 pass, 8 fail`（8 个名字见下），
  **promotion-66 的红表不在树里**（`git grep promotion-66` 无命中），无法逐名比；
  8 个名字：`loop_wait_after_inline.non_contiguous_loop.refused`、
  `loop_wait_after_inline.two_loops_interleaved.refused`、`fold_cast.failing_fails_at_run_time`、
  `progen_seed_4`、`order_edge_on_a_pure_node_is_refused`、`commit_refusal.loop_body_read`、
  `opt.writings.greedy_fusion_reads_no_writing`、`opt.writings.two_effects_wall_clock`。
  `feat/modules`、`feat/compile` 两个目录**没跑**。
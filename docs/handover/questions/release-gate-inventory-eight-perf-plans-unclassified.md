# `npm run release-gate` 在 develop tip 上任何层都跑不起来：8 个 perf 计划既不在 `SLOW_PLANS` 也不在 `FORBIDDEN`（2026-10-25，h497 lane）

本文件只记读数与两条修法的代价。派工原文：「若你读完代码发现正确修法不是加 `FORBIDDEN`（例如该计划本该被
某个层覆盖、或 `tier` 写错了），先别改，写问题文件并把两种修法与代价列出来。」本 lane 因此一字未改
`scripts/release-gate.mjs`。

## 一、读数（剥掉别的层，只看工具自己）

```
树的 tip b61068e48（本 lane；develop tip 75e480b9b，本 lane 只改 scripts/install-stamp.mjs、
scripts/stdlib-reference.mjs、scripts/stdlib-declarations.mjs、docs/npm-scripts.md）
$ node scripts/release-gate.mjs --only static
[gate] test/plans/perf/gemm_canonical_shapes.plan.toml: no layer reaches it (not a fast target, a
       SLOW_PLANS file, FORBIDDEN, or a debug plan); its 1 test(s), 1 slow, are UNKNOWN, not green
exit 1
```

**与本 lane 无关，已用未改的原版复核**：`git show HEAD~1:scripts/release-gate.mjs >
scripts/release-gate-develop.mjs` 后在同一棵树上跑 `--only static`，逐字同一条失败、同 exit 1（临时文件用完删掉）。

## 二、只看见一条，是因为 `die` 在第一条就退出

`planInventoryProblems()`（`scripts/release-gate.mjs`）把全部问题收进数组，调用处是
`for (const problem of inventoryProblems) die(problem, 1)`，`die` 立即 `process.exit`。问题按 `walkPlans`
的序（目录内文件名字典序）产生，所以只有 `perf/` 里第一个未分类的文件被打印出来。**实际是 8 条。**

## 三、清单（`test/plans/perf/` 共 18 个计划；`FORBIDDEN` 里 10 个、`SLOW_PLANS` 里 0 个）

未分类的 8 个（各自的落地提交）：

| 计划 | 落地 | `FORBIDDEN`/`SLOW_PLANS` 里 |
|---|---|---|
| `test/plans/perf/gemm_canonical_shapes.plan.toml` | `be527d26d` 2026-10-23 | 都没有 ← 被打印的那条 |
| `test/plans/perf/instruction_counts.plan.toml` | `38d61f0e2` 2026-10-24 | 都没有 |
| `test/plans/perf/instruction_counts_load.plan.toml` | `38d61f0e2` 2026-10-24 | 都没有 |
| `test/plans/perf/gemm_threads.plan.toml` | `04c451728` 2026-10-24 | 都没有 |
| `test/plans/perf/para_speedup.plan.toml` | `04c451728` 2026-10-24 | 都没有 |
| `test/plans/perf/reduce_layout.plan.toml` | `04c451728` 2026-10-24 | 都没有 |
| `test/plans/perf/showcase_d4.plan.toml` | `04c451728` 2026-10-24 | 都没有 |
| `test/plans/perf/step_budget.plan.toml` | `04c451728` 2026-10-24 | 都没有 |

两个列表最后一次被改是 `1fb0b425d`（2026-10-22，写下 `FORBIDDEN` 的那次），此后这 8 个计划各自落地时都没
带上门禁列表的更新；`planInventoryProblems()` 的注释要求"树里每个计划属于四种命运之一"，这 8 个是第五种。

## 四、8 个不是同一种命运（读数取自每个计划自己的注释，非裁定）

- **`para_speedup`：被造出来就是要进闸门的。** 它的头注释逐字引作者 2026-10-23 的裁定（
  `docs/handover/reviews/questions-2026-10-23.md`，"perf-para-gate"）："**选 B**：把已量过的便宜子集
  （`monte_carlo_pi.prl`，约 1.6 s）立成**可进闸门的计划**。度量取**同一次运行里并行对串行的加速比**（与绝对
  墙钟相比对负载不那么敏感），并记 CPU 时间；整条 `perf.para` 仍在发布前跑。"同注释写它由"命名这个文件"来跑
  （`test/plans/perf/**` 不在 `npm run test` 与 `--changed` 里，而**命名文件即全量跑**）——正是闸门
  `SLOW_PLANS` 那一层的跑法。把它按 `FORBIDDEN`（policy skipped）处理等于**把一条检查移出闸门**，与裁定相反。
- **`step_budget` / `reduce_layout`：各有一条"只断言计数与性质、不断言时间"的守卫**（`step.count` 断言每步的
  算子调用表、`reduce_layout.count` 断言分配块数与固定 checksum），外加一条测量；`step_budget` 头注释写
  "the count test … runs in about two seconds"。它们能不能整体进闸门，是"测量那半条会不会量到邻居"的问题。
- **`instruction_counts` / `instruction_counts_load`：未在闸门跑的理由是重**：要 privileged container +
  `/usr/bin/perf`（`perf_event_paranoid=4`），`_load` 那条还要 96 个 `yes` spinner、600 s 诱导负载。
- **`gemm_canonical_shapes` / `gemm_threads`：测速率曲线，不断言墙钟**（数值只写在注释里，量到的是"每个点都
  被量过且有限"）。
- **`showcase_d4`：Camel 对 Python 的 MCTS 逐行比较**（`skip_unless` python），`perf/` 下的 showcase；同族的
  `showcase_c1/c2/c6/d5` 因 PyTorch 已进 `FORBIDDEN`。

## 五、两条修法与代价（供裁定，本 lane 未做）

- **A（最小、立刻可用）**：8 个都加进 `FORBIDDEN`，每条一句 reason。代价：一处连续列表编辑即可让工具跑起来；
  但把裁定"可进闸门"的 `para_speedup` 也标成 policy-skipped（= 把一条检查移出闸门），且第 9 个 perf 计划落地
  时会再坏一次（`die` 仍在第一条退出）。
- **B（逐条定命运，与裁定一致）**：`para_speedup` 进 `SLOW_PLANS`（照裁定 B；头注释量到 ≈4.4–4.8 s），
  `step_budget`/`reduce_layout` 由你决定整体进 `SLOW_PLANS` 还是只把守卫那半条留下（闸门按文件跑，选不了单条），
  其余进 `FORBIDDEN`。代价：要一次逐计划裁定；slow 层变长（`para_speedup` 秒级，`reduce_layout`/`step_budget`
  未量）。
- **C（机制）**：让 inventory 把 `test/plans/perf/` 当一类（例如 `FORBIDDEN` 支持目录项，或另立一个"可进闸门的
  perf"白名单），第 9 个计划不再弄坏工具。代价：改 `release-gate.mjs` 的列表语义（现在 19 条逐文件、理由逐条写，
  含 H154 的 ONNX 那行），要一次评审；收益是一次性消除复发。

## 六、没做的与影响

本 lane 未改 `scripts/release-gate.mjs`（按派工）。同树其余读数见本 lane 报告：闸门
`smoke,test/plans/feat -Tier fast -Jobs 8` = `total 6177 / fail 0`，`release-gate` 七项静态检查逐个单独跑
7/7 exit 0。影响：`npm run release-gate` 自 2026-10-23 起在任何层都跑不起来，release 证据路径被堵，直到上面
A 或 B 落地。
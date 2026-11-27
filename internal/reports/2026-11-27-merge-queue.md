# 合并队列的账（2026-11-27）

读数时刻：2026-11-27 15:37:42。读数基线：origin/integration = b797f82b01（用 git fetch <url> '+refs/heads/integration:refs/remotes/origin/integration' 拉的）。

**这是一张快照**：其他车道仍在改枝（本次读数的几分钟内 lane/edit-session 就多了一笔），数字会变；并入前请按同一命令重取。

入选范围：lane/* 里 ahead > 0 的枝，共 19 条。ahead = 枝上未在 integration 的笔数；behind = integration 上它还没有的笔数。树 = D:\Projects\Camel-wt\<名>；树是否干净 = 该树 git status --porcelain 的行数（0 行 = 干净）。判词列留空，由协调者填。

| 枝 | tip | ahead/behind | 树 | 树是否干净 | 最后一笔 | 作者·日期 | 判词 |
|---|---|---|---|---|---|---|---|
| lane/edit-session | 8d39ee9637 | 1/0 | enter-shape | 干净 | test(graph): the two readings 8.1's region face can give | Zhenjie Wei·2026-11-27 |  |
| lane/abort-fix | 653a2722ce | 2/15 | abort-fix | 干净 | compile: a block root finds the group of the module the compiler imported | Zhenjie Wei·2026-11-27 |  |
| lane/probe-evidence | 70fbfad2d4 | 1/42 | misc | 干净 | probe: keep the measurement artifacts in the repository -- their working trees are to be retired, and nothing else holds them | Zhenjie Wei·2026-11-27 |  |
| lane/test-tiers | f92f98192e | 1/55 | stdlib-reds | 干净 | tests: cost outranks the directory a case lives in | Zhenjie Wei·2026-11-27 |  |
| lane/onnx-model | 1b51020096 | 11/83 | timeit-iter | 干净 | onnx: take the case fixtures through the model/save spelling | Zhenjie Wei·2026-11-27 |  |
| lane/pass-registry | a3ebba304c | 2/86 | pass-registry | 脏(3) | pass: a pass that reads tag facts states it on the operator, not in a list beside it | lane-witness·2026-11-27 |  |
| lane/task-229 | 20743265a9 | 26/87 | cli-rework | 干净 | TEMP probe: what the export reports about the migrated kinds | Zhenjie Wei·2026-11-27 |  |
| lane/pass-registry-salvaged | 34f83155ce | 3/88 | —（无树） | — | ops: a pass written under another spelling is the same operator, said on its record | lane·2026-11-27 |  |
| lane/loop-tape-values | 4af09f5a67 | 1/125 | mm-capacity | 干净 | autodiff(tape): name the pair's value slot where it was still a bare 0 | Zhenjie Wei·2026-11-27 |  |
| lane/pass-decl-gate | 07f63bad6d | 10/127 | console2 | 脏(4) | compile: a pass that names no operator still loads, pinned from the other end | Zhenjie Wei·2026-11-27 |  |
| lane/space-hint | c4ec6bba04 | 1/131 | —（无树） | — | reports: keep the record of the main-checkout write, dated, instead of a loose file in the temp directory | Zhenjie Wei·2026-11-27 |  |
| lane/task-216-probe | 6d114cb579 | 2/176 | —（无树） | — | TEMP(probe): are inner captures among the forward graph's results | Zhenjie Wei·2026-11-27 |  |
| lane/task-216-residuals | 53a6c4b433 | 1/176 | —（无树） | — | autodiff(tape): hand a pullback's internal values back beside the pair | Zhenjie Wei·2026-11-27 |  |
| lane/loop-tape-values-wip | 6c6e94efec | 3/177 | —（无树） | — | TEMP: compare closure slots with the site's capture types | Zhenjie Wei·2026-11-27 |  |
| lane/pass-registry-from-e0 | e0e31424d9 | 2/186 | —（无树） | — | pass: a pass that reads tag facts states it on the operator, not in a list beside it | lane-witness·2026-11-27 |  |
| lane/compile-view | c1bd265dc1 | 2/186 | —（无树） | — | compile: the registry key and the handle's draft are named by what they are for | Zhenjie Wei·2026-11-27 |  |
| lane/cc-c2 | 254eb5ba59 | 9/188 | perf | 干净 | docs(opt): record where the pass-as-function lane stands, in the plan it stands in | Zhenjie Wei·2026-11-27 |  |
| lane/pass-classify | 4c32ee2955 | 3/188 | —（无树） | — | test: a declaration that is not shaped like a pass is checked, not swallowed | Zhenjie Wei·2026-11-27 |  |
| lane/cc-blocktail | afa7a84f87 | 11/197 | editview-d | 干净 | compile: a block's commit takes the type the block stands at as its type argument | Zhenjie Wei·2026-11-27 |  |

## 最该换底进队列的候选（ahead > 0 且 树干净 且 behind >= 50）

| 枝 | ahead | behind | 树 | 作者·日期 |
|---|---|---|---|---|
| lane/cc-blocktail | 11 | 197 | editview-d | Zhenjie Wei·2026-11-27 |
| lane/cc-c2 | 9 | 188 | perf | Zhenjie Wei·2026-11-27 |
| lane/loop-tape-values | 1 | 125 | mm-capacity | Zhenjie Wei·2026-11-27 |
| lane/task-229 | 26 | 87 | cli-rework | Zhenjie Wei·2026-11-27 |
| lane/onnx-model | 11 | 83 | timeit-iter | Zhenjie Wei·2026-11-27 |
| lane/test-tiers | 1 | 55 | stdlib-reds | Zhenjie Wei·2026-11-27 |

## 有树但树脏（不能合，先让车道交回）

| 枝 | 树 | 脏行数 | ahead/behind |
|---|---|---|---|
| lane/pass-registry | pass-registry | 3 | 2/86 |
| lane/pass-decl-gate | console2 | 4 | 10/127 |

## 无工作树（没有车道在等，纯枝）

共 8 条：`lane/pass-registry-salvaged`、`lane/space-hint`、`lane/task-216-probe`、`lane/task-216-residuals`、`lane/loop-tape-values-wip`、`lane/pass-registry-from-e0`、`lane/compile-view`、`lane/pass-classify`

## 标注

- lane/edit-session 的那条线（10 处 noteGraphWideEdit() 调用）已进 integration，它留在表里的只是新加的一笔（读数时枝尖 8d39ee9637，仍在动）；表内既含同族枝尖 12530a2357、又补上定义 src/kernel/runtime/draft_edit.cpp:131 的是 lane/abort-fix ⇒ 换底时优先。
- 尖端 b797f82b01 的 draft.h 只有 10 处调用、没有声明行（git grep -n 'void noteGraphWideEdit' origin/integration -- src/kernel/include/camel/runtime/draft.h 零命中），定义却在 draft_edit.cpp:131 ⇒ 尖端编不过的原因仍成立。
- lane/editview-gir 不在本表（ahead=0）；其枝已被批准删除，但枝被检出在 wt/edit-gir（干净），守卫『无工作树』未过，等协调者一句再动。

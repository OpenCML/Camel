# `known failures: 2` 是哪两条，各自在等什么

两条都在 `test/plans/feat/linear/effects_model_edges.plan.toml`（`tier = "fast"`，每次闸门都跑），都是 `known_fail = ["gir"]`，都是 `docs/design/effects/edges.cn.md` 的条款，都是**活的债务**——不是用例写错，是代码侧整批没做。

| 用例（原样） | 判的是哪一条 | 失败形态（`[XFAIL]` 的原文） | 一句结论 |
|---|---|---|---|
| `edges.no_ctrl_edge_kind.gir` | edges.cn.md 1 + 设计原则 3.1：一条边只排序、不传递"被用到"；图的边只三种 | `the kind "control edge" no longer exists; the graph has 4 CTRL edge(s): N01->N03, N03->N04, N0D->N0E, N0F->N11` | 建图器还在造控制边（`src/compile/gir/` 里 20 处 `LinkType::Ctrl`），53 个文件读这一种类、26 张存档图里 143 条 `CTRL` 边要同一笔重录——**它不能单独烧** |
| `edges.gate_has_no_control_input.gir` | edges.cn.md 5.3 + 作者 2026-10-24 裁定（`questions-2026-10-30-gate-control-edge.md` 顶部：`GATE` 只认同步前驱，代码侧归第 9 条收口） | `GATE no longer has a control input; N04 has 1 CTRL input(s) from N03` | 同一个前提的一半：`GATE` 要丢掉那个端口，得先让同步链接住这次等待；`coerceArmExit`/`setCtrlInputs`/`appendControlDependency` 共约 50 个调用点，跨建图器、三个 opt pass、内联引擎和 `include/camel/runtime/` |

**两条各自在等什么**：都在等**第 9 条的第二半**——`batches-2026-10-29.cn.md` 记的"收掉控制边重建的最后一半：删控制边与 `SYNC`；`std::opt::order` 改读同步边与顺序边；技术页按新模型重写"，状态写"进行中"，车主是 `lane-order-region`（它交的前半是 `788391f5e` "Delete the SYNC node kind"（2026-10-30），plan 文件末尾也写了"`SYNC` 已经没了"；这一行原文写的 `0d1af56f3` 是一条训练步用例的提交，与本批无关，已改）。**但那棵车道已经不在**：本地与 `origin` 都没有 `lane-order-region`，主机上也没有对应工作树，`h-eff`/`lane-eff-rule` 是效应规则那条，不是这一批。所以这两条现在**没有认领人**，它们是这一批唯一可见的两个标记。

**为什么它们必须是 `known_fail` 而不是跳过**：`known_fail` 是"登记了失败形态、但不许悄悄变绿"的机制——runner 见到标记了 `known_fail` 却通过的用例会**直接判红**（`marked known_fail on '%s' but passes: remove the mark`），所以修复它的那一笔必须同时摘掉标记，不能靠"它自己好了"。跳过则相反：跳过不登记形态，也不会有任何东西在它变好时叫你。
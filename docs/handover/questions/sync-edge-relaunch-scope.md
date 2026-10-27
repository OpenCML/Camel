# 同步边整批重落：规模与授权（2026-10-27）

基线 `origin/develop` = **`cbae559be`**（300 条计划，`git fetch` 后核对一致）。下面的读数都在这个提交上量得，
或在这个提交上叠了未合并分支那一笔之后量得。方法一行：`cherry-pick -n` **只把 `src/**` 叠到 `cbae559be`**，
`test/**` 保持 develop 的，量完即弃（不改任何文件、不建分支、不 push）。

## 我量到的

### 1. develop 上只有 1 处写 sync 边，语句链走 ctrl

```
$ git grep -n 'LinkType::Sync' cbae559be -- src
cbae559be:src/compile/gir/builder_control.cpp:2754:            linkNodes(LinkType::Sync, lastSyncedNode_, joinNode);
cbae559be:src/compile/gir/builder_support.h:89:    case LinkType::Sync:
```

（同一命令在 develop 上查 `writeSyncStatement` 是 0 命中。）唯一写点即分支汇合处
`src/compile/gir/builder_control.cpp:2751-2754`：

```
2751:         if (lastSyncedNode_ != nullptr && linkCheek(lastSyncedNode_, joinNode)) {
2752:             markSyncStatement(lastSyncedNode_);
2753:             markSyncStatement(joinNode);
2754:             linkNodes(LinkType::Sync, lastSyncedNode_, joinNode);
```

语句链目前**走 ctrl**：`shouldAddSyncCtrlLink` 后接 `linkNodes(LinkType::Ctrl, …)`，站点在
`src/compile/gir/builder_control.cpp:2736`、`:3590`、`:3992`，
`src/compile/gir/builder_expr.cpp:650`、`:1369`、`:1476`，
`src/compile/gir/builder_operators.cpp:4021`、`:4109`，
以及 `src/compile/gir/builder.cpp:1472`（它的 ctrl 写在同块的
`linkNodes(LinkType::Ctrl, lastSyncedNode_, gate)`）。
实测 `test/cases/opt/generic/static_branches.cml`：**104 条边 = control 12 / sync 2**，两条 sync 都是分支汇合。

### 2. 那批能力只在未合并的 `handover/edges-land` 的 `18668d4de` 上

```
$ git log --oneline cbae559be..18668d4de
18668d4de feat(gir): a sync block's chain is a sync edge, and every picture and contract it moves
$ git log --oneline 18668d4de..cbae559be | Measure-Object
310
```

单提交、语义只有这一项，但动 **46 个文件**：

- `src/**` 10（`builder.cpp`、`builder_control.cpp`、`builder_expr.cpp`、`builder_helpers.h`、
  `builder_operators.cpp`、`builder_support.h`、`passes/opt/generic/gates.cpp`、
  `runtime/draft.cpp`、`runtime/draft_construct.cpp`、`runtime/draft_inline.cpp`）；
- `include/camel/runtime/draft.h` 1、`modules/tiles/src/region_outline.cpp` 1；
- `test/**` 24：**重录快照 20 个**（17 个 `.dot` + 2 个 `.jgir` + 1 个 `.md`）、
  2 个校验脚本（`test/cases/modules/rename/identity.mjs`、`test/cases/trans/jgir_contract.py`）、
  2 个 plan 文件（`test/plans/feat/compile/gir.plan.toml`、`test/plans/feat/linear/effects_model_edges.plan.toml`）；
- `docs/tutorial/images` 4（2 个 `.dot` + 2 个 `.svg`）、`exemplars` 6（3 个 `.dot` + 3 个 `.log`）。

另一条车道按 **19 处重录**（16 个 golden `.dot` + 2 个 `.jgir` + 1 个 `.md`）计；我按 `18668d4de` 的
diff 复数是 20（多 `test/plans/feat/golden/tutorial_examples/13_graph_rgir.dot`），全树 `.dot` 共 22 个。

### 3. 只叠 `src/**`、`test/**` 不动时的快层读数

```
$ pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path <实验树> -Plan all -Tier fast -Jobs 8
[SUMMARY] total: 7449, pass: 7367, fail: 72, skip: 7, known failures: 3
```

270 个计划。基线（`cbae559be` 原样）是 **13 条红，含 `device_region.nvm` 的环境红** ⇒
**新红 59 条、消失 0 条**，三分类加一条文档快照：

- **快照 26**（golden 图的边数、节点编号）
- **图上断言 23**
- **行为错 9**：`static_branches.simplify` 输出变 `taken|big`；`nn_grad_check.simplified.nvm`、
  `.simplify.fvm`、`.simplify.jit` 数值漂；`pass_export_loop` 导出失败；
  exemplars 的 `01_scalar_boundary`、`02_branch_shape_contract`、`05_effect_order`、`08_structured_control_flow`
- **文档快照 1**

### 4. 重录件的变化形态：纯增

每条链位**加一条 `SYNC` 边、`CTRL` 仍写**（纯增、无删除），节点编号随之后移，所以多数是
`edge count mismatch actual=expected+1/+4`：`gir_snapshot` 41 vs 37、`sync_ctrl_dedup` 9 vs 8、
`if_without_else` 12 vs 11。

### 5. 编译漂移：能 build，但需 1 处补丁 + 1 个符号前置

10 个 `src` 文件里 9 个 `--3way` 干净，**1 个真冲突在 `src/compile/gir/builder_expr.cpp`**——
"`var` 读是否进链"：develop 已裁定**不进**（`docs/handover/reviews/questions-2026-10-26-late.md`：
"`var` 读仍由数据边绑到最近的写"），该提交仍写成进，所以按 develop 的语义留空。
另有 1 个符号搬家：`controlRegionOf` 在该提交里定义在 `src/runtime/draft.cpp:1288`，
develop 定义在 `src/runtime/draft_inline.cpp:1331`——补 1 行前置声明即过。

### 6. 因此"双写落地"不是纯增量

那 9 条行为错里 `unread_gate.*` 与 `nn_grad_check.*` 再次变红：它改的内联逻辑与我们已落地的链位修复、
6 项读者迁移**相互影响**。**`18668d4de` 不能照搬**——必须 rebase 到 develop 上重新落一遍，并逐条重判。

## 要问的一句

**这 19 处重录（golden／文档快照）与那 9 条行为差异，是否授权我们"按新形状重录 + 逐条判定"落地？**

代价都已在上面量过：**59 条新红**、**19 处重录**（20 个快照文件）、
**1 处 `builder_expr.cpp` 语义冲突**（按 develop 的裁定留空）、**1 个符号搬家**
（`controlRegionOf` 的前置声明）。不替你选。

## 没量到的

- 那 9 条行为差异各自是**形状变化**还是**真回归**，逐条未判（只量到它们红了；`nn_grad_check` 是数值漂）。
- 20 个重录件只量了形态（`SYNC` 纯增、节点编号后移），没逐条读 diff 的语义。
- 只跑了快层；slow／perf／debug 层在那棵实验树上没跑。
- 没在"不含 C1 与 `var` 读取改动"的树上复现（上一页 `progen` 那条裁决要求的对照树）。
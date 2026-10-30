> 已由作者答复，裁定原文见 `docs/handover/reviews/questions-2026-10-29.md` 末节与 `questions-2026-10-30-gate-control-edge.md`；本页留档，不再更新。

# tiles：裁定说 22 条期望应回到 10，改完读到 12

日期：2026-10-30　车道：`lane-eff-rule`（第 2 条按裁定实现后未提交）

## 现象

裁定 `64af875f1`：`graph_regions.cpp:106` 的 `ordered → host` 读法错了，顺序是调度约束不是放置
约束；`inChain`、`syncPredsOf`、`ctrlInputsOf` 从区域资格里删掉，按资源判（效应落在 host 资源上
的、以及没有设备实现的算子才留 host）；**两边改完 22 条 tiles 的期望应回到 10**。

按此实现（`modules/tiles/src/graph_regions.cpp`：`eligibility` 删掉 `ctrlInputsOf`/`ctrlOutputsOf`/
`syncPredsOf`/`uses().sync`/`inChain` 五项，`chainMembers`/`inChain` 两个函数一并删掉，固定元组
投影那条准入也去掉同样的条件），读数：

| 状态 | `TILES_OUTLINED` | tiles 计划（101 条） |
|---|---|---|
| base | 10 | 绿 |
| 规则那 5 笔（建图写长链后） | 5 | 22 红 |
| 只改 tiles | **12** | **25 红** |
| 建图与 tiles 都按裁定改 | **12** | — |

多出来的 2 个区域，是**原先只被 ctrl / sync 边挡在 host 外的值节点**。同一张图上带 CTRL 或 SYNC
边的节点（`std::gir`，`test/cases/modules/tiles/region_execution.cml`）与它们所属的形状：

| 算子 | 个数 | 出处 |
|---|---|---|
| `tensor:sum` | 6 | `main` 里五处 `println(sum(...))` 与 `constant_axis` 的体 |
| `:io/println` | 5 | `main`（这类是效应，仍然留 host，不参与这 2 个区域） |
| `GATE` | 2 | `sync` 体与分支的链尾 |
| `tensor:add` | 2 | `chain`、`ordered`、`detour` 的体 |
| `tensor:reshape` | 1 | `main` 的第一条 |
| `chain` / `ordered` / `detour` / `shared_values` / `constant_axis` | 各 1 | 函数定义本身 |

按裁定这 2 个区域**应该**能上设备：它们的成员既没有落在 host 资源上的效应，也不是没有设备实现
的算子，base 把它们留在 host 的唯一理由就是它们身上挂着一条 `Ctrl`（块的控制边）或一条 `Sync`
（链边），而这两者按裁定只是调度约束。**所以 12 是这条规则的直读**，不是少删了一项：

- `ctrlInputsOf` / `ctrlOutputsOf` / `syncPredsOf` / `uses().sync` / `inChain` 五项都已不在资格里
  （`graph_regions.cpp` 的 `eligibility` 只在 `!table.isPure(oper) || table.hasExternalEffect(oper)`
  和 `operations().find(oper)` 两处返回非空原因）；
- 唯一还在拦的是"效应"与"没有设备实现"两支，而 2 个新区域的成员都不在这两支里（`sum`/`add`/`reshape`
  都是纯算子且有设备实现）。

**把 22 条期望改成 10 现在做不到；改成 12 属于重录，未获授权。** 我没有动任何期望。

一条工具限制要说清：`region_test_pass.cpp:263` 只打印 `TILES_OUTLINED <n>`，全仓没有一个遍打印
区域成员（`inspectRegions` 只经 `tiles:regions` 算子由程序调用，见 `modules/tiles/src/module.cpp:207`）。
所以上面是按"带 ctrl/sync 边的节点 + 两支排除"指认的**成员集合**，不是区域编号；要按区域逐个点名，
需要给测试遍加一行成员清单（一处代码改动），没有授权我不加。

## 为什么不能自己定

`docs/design/gpu/plan.cn.md` 2.5 只说"块与臂是持有节点的段"，`edges.cn.md` 1 与 5.3 只说链是
顺序的写法。**"顺序不是放置约束"这句是裁定新给出的口径**，它决定了 10 还是 12；而 22 条期望记的是
10。要么口径按 12 落地（那就得授权重录 22 条），要么 `ordered` 里还该留一项（例如只留
`uses().sync`：链上的语句留 host，块的控制边不留），要么"10"这个数是在别的假设下算的。
三种都不是改这一处的人能定的。

## 可执行的选项

**选项 A（接受 12，授权重录 22 条）**：按本页的资格实现保留，把 22 条 tiles 期望从 10 改成 12
（重录时必须逐条给出"新增的两个区域是哪两个节点集合"，按重录判据：不删边、不挪端口、只是放置
结论变化）。派工影响：tiles 22 条回绿，建图不变；代价是区域结论与 base 不同，需要作者确认
"多两个区域"正是这条规则要的结果。

**选项 B（`ordered` 里留一项：`uses().sync`）**：资格里保留"链上有后继（`sync` 出边）的节点留
host"，其余四项（`ctrlInputsOf`/`ctrlOutputsOf`/`syncPredsOf`/`inChain`）删掉。派工影响：
链尾的语句留 host、块的控制边不再拦，读数预计回到 10 附近（需实测确认）；代价是"链上的节点"
仍然是一种放置约束，与裁定的口径半开半合，要在 2.5 里写清留的是哪一半。

**选项 C（先按 10 冻结，等读数对齐）**：本条不落地，tiles 保持现状（5），把"12 vs 10"并进
建图那一半一起定。派工影响：tiles 22 条继续红，等另一页（嵌套调用的值靠什么存活）裁定后重测
——两个改动会互相影响区域划分，分开落地可能各测一次都不准。

**默认建议**：**选 A**，但**在重录前先做一次交叉验证**：把建图那一半按它自己的裁定落地（或退回），
两种情况下各测一次 `TILES_OUTLINED`，若都是 12，则 12 与建图无关、可以单独授权重录；若随建图变化，
则应选 C，两个改动一起定。理由：区域划分同时看链与效应，先落地再重录会把一个还不稳的读数写进记录，
而这正是本轮一再避免的事。
# A 项的两条正路与它们的裁定；以及"表格里没有读数的格子不算结论"

## 1. A 项：不变量被查在了一个看不见理由的层

**事实**：`src/runtime/graph_edges.cpp:236` 的这条检查跑在**已提交的 `GCGraph`** 上：

```
if ((pred->flags & kGCNodeFlagSyncStatement) == 0) {
    bad("starts at a node no `sync` block wrote ...");
}
```

而"这个节点是被 `keep` 保活的"是 **draft 才知道的事实**（`carriesOrdering` 在
`src/runtime/graph.cpp:1295`，要 draft 才读得到 ctrl/sync/可自由调度）。

**我上一轮提的结构条件（`kind == Gate` 的 `normUser` + `syncUsers` 为空）作废**，理由就是作者 23:30 那句：
*"审查清单写的是『同步链上的位置，读边，不读标记』：标记是谁都能打的。"* 一个结构形状虽然不像标记那样
是一行 flag，但它的性质一样——**任何遍都能造一个 `Gate`，也都能把它的 `syncUsers` 变空**，
用一个"谁都能满足"的形状当凭据，正是这次审查要撤掉的东西。

**两条正路，都不加结构判据**：
1. **把这条检查移到 draft 层验证**（`GraphDraft` 已有 `carriesOrdering`、有 ctrl/sync/可自由调度）；
2. 或者让提交携带理由 —— **这就是"标记"，作者已否**。

**裁定（默认已定，作者可否）**：*"『sync 边的起点必须由某个 `sync` 块写过』这条不变量，是在 draft 层的
验证里查，还是在提交后的图上查；我方默认后者不够——它看不见『被 keep 保活』这个理由，建议移到 draft 层
验证。已按此方向做。"*

**这一笔不落代码**（负责人这一轮第 1 条）：A 只挡内联与折叠臂那 17 条，不挡②的 73 条，所以②的顺序不变。
**下一笔要做的第一步**是：把 `checkSyncEdges` 里"起点"那一支搬成 `GraphDraft` 的提交前检查
（`draft.cpp` 的提交口径里已经有 `checkSyncEdges` 的邻居），提交后的图上只留"ends on"那一支。

## 2. 表格里没有读数的格子不算结论

"按代码推"和"量出来"的差，今晚已经出现五次：

| # | 那一次 | 推的是什么 | 量出来是什么 |
|---|---|---|---|
| 1 | `payloadAs` | 以为节点的载荷读法就那样 | 名不对，读法另有一套 |
| 2 | `setSyncInputs` | 以为它就是"设同步前驱" | 用的是 `setSyncPredecessor`，拒绝条件也不同 |
| 3 | 悬垂 `span` | 以为取出的 `span` 能留到改完之后 | 改一次就悬垂 |
| 4 | `bin/camel` | 以为探针字面量在 CLI 可执行文件里 | 在 `libcamel.so` 里 |
| 5 | **这张 ctrl 表的③** | **按代码把 `pruneBranch` 的 `brchCtrlUsers` 列成"没有替代"** | **114 条红里没有一条是它** |

**规则**：**表格里没有读数的格子不算结论**。写"没有替代""不可能""只有一处"这类判断时，要么贴读数，
要么写明"这是读代码推的，未量"。第 5 条已经按这条改了：①②有读数，③那一格现在写的是"未量，且 114 条里
为 0"。

## 3. 片 3 那条用例的结论（不是待办）

`test/cases/opt/effects/inline_reduce_in_folded_arm.cml`（片 3 为此加的）在凭据一改就红 8 条
（`inline_fold` 与 `opt` 各后端）。**这不是一条待办，是一个结论**：

- 折叠臂里那个 `:mark/reduce_arr` 的"位置"，**今天仍然由 ctrl 边给出**，不是 sync 给的；
- 片 3 的修复把位置交给它读的节点、把后继接上去，但**接的是 `appendControlEdge`**——
  也就是说，**片 3 是把问题挪了一个位置**，没有把它换成 sync；
- 这条联系解释了为什么片 4（`keepThroughValue` 换 `GATE`）和 P0-2（删 ctrl）会撞在同一处：
  **它们要的是同一个东西的两半**——把"位置"从 ctrl 边换成 sync 边。片 4 卡在 `checkSyncEdges`
  的起点检查（见第 1 节），P0-2 卡在凭据检查（`pass.cpp:1925`），**两处都是"ctrl 现在是位置的唯一表达"**。

## 4. ②族一的入口（下一笔的起点）

族一 = `var_param`(32) + `var_params`(4) + `partial_application_written_var`(2) = **38 条**，
最小用例 `test/cases/linear/nvm/var_params.cml`（计划 `test/plans/feat/linear/closures_and_state.plan.toml:180`
起的 `.nvm/.fvm/...` 一族）。

它今天靠的 ctrl 写点，已经定位到两处：

| 位置 | 它在干什么 |
|---|---|
| `src/runtime/writings.cpp:2199` | `draft_.appendControlEdge(made, wait);` —— 写入改写把一个写节点换成 `made` 之后，**把它原来的 ctrl 等待逐个抄到新节点上**（`takeStatementPlace` + `replaceAllCtrlUses` 之后） |
| `src/runtime/writings.cpp:2047` | `draft_.ctrlUsersOf(node).empty() && draft_.syncUsersOf(node).empty()` —— 判"这个写入节点有没有人等我"的**唯一依据是 ctrl 或 sync** |

**改法（下一笔）**：把第 2199 行那个"抄 ctrl"换成"抄 sync 前驱"——即在 `made` 上写它的同步前驱
（`setSyncPredecessor`），前提是被抄的那个 `wait` 本身已经有 sync 位置；没有的，说明它是②里更靠内的
一层，要单独看。**这一笔没有开始**：它是 `writings.cpp` 的写入/版本链模型，38 条红一起动，
本轮没有余量做完并量两侧。
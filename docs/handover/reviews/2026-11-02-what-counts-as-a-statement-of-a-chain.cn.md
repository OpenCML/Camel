# 链上语句包含 `Func`/`Join` 吗？

**问：`edges.cn.md` 的"链上语句"是否包含 `Func` 与 `Join`，是否排除"带 `kGCNodeFlagSyncStatement` 却按算子性质算不上链上语句"的 `Oper`？默认建议：`Call` 算，`Func`/`Join` 不算，标记宽出来的部分按新谓词走。**

## 为什么要问：标记推不出来，而且推不出两个方向

作者 04:50 的裁定说"标记记的东西能从图推出来，就不该存"。**实测的反证**：把"链上语句"写成谓词（`!isPure || hasExternalEffect || mayFault` 的 `Oper`，加 `Brch`/`Loop`/`Call`），再配"块的值"（同区域 ∧ 无别的使用者）与 `Gate`，**与旧标记并排跑，不一致 17753 处**（`faults.plan.toml`，180 个进程，判据未改、176/176 仍绿）：

| 方向 | 采样行 | 含义 | 出现在 |
| --- | --- | --- | --- |
| `old=1 new=0` | 556 | 标记说"是链上语句"，谓词说"不是" | `Func`(385)、`Join`(104)、`Oper`(若干：`293`/`181`/`144`…) |
| `old=0 new=1` | 222 | 标记说"不是"，谓词说"是" | `Call`(198)、少量 `Oper` |

⇒ **两个方向同时存在**：谓词比标记既宽又窄。**所以"换个实现"不成立，这是改判据。**

## 默认与理由

- **`Call` 算**：作者原话"含这些的结构语句（臂、循环、**调用**）"——"调用"就是 `Call`。
- **`Func` 不算**：`Func` 是函数值，不是调用。
- **`Join` 不算**：原话一个字没提。
- **`old=1 new=0` 的 `Func`/`Join`/`Oper` 按新谓词走**，即**这些边本来就不该建**——理由正是 23:30 那句"标记是谁都能打的，`rewrite.h` 的 `markSyncStatement` 随手就能调"。

## 只改检查是不够的

只改检查，那 778 条（556+222）边会变成红。**正解是让建边侧不建这些边，让检查退化成断言**——这与 `Ctrl` 探针同向：**删掉控制边只红 601，换成 sync 边红 4192。**

## 这份页不改代码

按默认继续，下一步是"建边侧：终点不满足 `isChainEnd` 就不建这条 sync 边"的探针（一次性分支），读数是"三档计划各红多少，与 601 对照"。
## 为什么这一步要跨层：建边侧与检查侧用的是两套节点身份

**检查侧**（`src/runtime/graph_edges.cpp`）手里是 `GCGraph` 与 `GCNode*`/`gc_node_ref_t`，能问 `nodeBodyAs<GCOperBody>`、`nodeRegion`、`normOutputsOf`。

**建边侧**（`src/compile/gir/`）拿的是 `node_handle_t`，走 `linkNodes(LinkType::Sync, from, to)` 写边；那些调用点是 `builder.cpp:1807`、`builder_control.cpp:1310/2823/2893`、`builder_helpers.h:678`。**编译期的节点种类（`GCT::NodeKind`）与运行时的 `GCNodeKind` 不是一套**，`isChainEnd` 不能直接搬过去问。

⇒ **"终点不满足 `isChainEnd` 就不建这条 sync 边"这一步必须在能同时看见两侧的地方做**（提交前的图封存处，或把判断放到 `DraftGraph` 的 `appendInput` 那一层，那里两者都有 ref）——**这也解释了为什么这一步不是"改一行谓词"**。

**不为了跑通探针去改建边侧的数据结构**：探针只允许"在某处少建一条边"。
**已裁（2026-11-02 05:30）：留档。** 作者裁：不变量按节点性质判，链上语句含 Call、不含 Func/Join。

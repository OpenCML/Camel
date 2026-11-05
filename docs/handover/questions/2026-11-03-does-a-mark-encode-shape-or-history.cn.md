# 一处标记编码的是"形状"还是"已发生过的事件"？

## 问什么

`kGCNodeFlagSyncStatement` 正在退役（作者 2026-11-02 04:50：能推导的标记不该存）。三处已经换掉了：

| 位置 | 原来 | 现在 | 读数 |
| --- | --- | --- | --- |
| `src/runtime/graph_edges.cpp:194` | fork 判据读标记（`isWaitingCarrier`） | `nodeIsChainStatement` 那一跳 | 658 → 658，`sync_chain_fork_is_refused` 由"夹具喂标记才过"变成"节点形状自己过" |
| `src/runtime/draft.cpp:1519/1650/1669` | `handChainPlaceTo` 打标记、`replaceStatement` 读标记 | `GraphDraft::isChainStatement` | 652 → 650，**转绿两条**：`branch.graph`、`generic.gate_branch_wait` |
| `src/passes/opt/generic/generic.cpp:342` | 见下 | **没有换，进例外清单** | 换了之后 5 绿 5 红、总数不动 ⇒ 已回退 |

第三处换不动，是因为它读的不是形状，是历史。

## `generic.cpp:342` 读的是什么

`joinBlockFragments` 挑"链头"：

```cpp
if (header == nullptr || !draft.alive(n) ||
    (header->runtimeFlags & kGCNodeFlagSyncStatement) == 0 ||
    !camel::runtime::sameSyncBlock(draft, n, anchor)) {
    continue;
}
bool waits = false;
for (gc_node_ref_t pred : draft.syncInputsOf(n)) {
    if (inBlock(pred)) { waits = true; break; }
}
if (!waits) {
    heads.push_back(n);
}
```

三段式：**(a) 带标记**、**(b) 与锚点同 `sync` 块**、**(c) 块内没有 sync 前驱**。(b)、(c) 都是形状；(a) 是"构建器把它挂上过链"这一**已发生过的事件**。

两个判据的定义域不同，所以换不动：

- 标记 = "构建器把它挂上过链"；
- `nodeIsChainStatement` = "跑它是可观察的"。

只有在"一个 `sync` 块里每条语句都被挂上链"时二者才重合，而这在今天这棵树上**不成立**（`1e1c1dbdb`，探针原文）：

```cml
func main() {
    sync {
        g(1);
        g(2);
    }
}
```

```
N02 -> N04 [label="0|0", type="CTRL"]      ← 两条副作用调用之间只有 CTRL，零条 SYNC
```

于是换成推导后，"是语句但没被挂上链"的节点会新增成为链头、被并进块链：`generic.effect_order_folded`、`effect_order_folded.simplify.{nvm,fvm,jit}`、`nn.lrit` 转绿，`export_gated_gru_forward`、`export_gated_gru_step`、`ort_gated_gru_step`、`residual_mlp_large_bytecode`、`stdlib.math_numbers.inline_opt` 转红，总数 650 不动。**净总数交换了缺陷类，不是读数。**

> **更正（2026-11-03）：上面这段的证据撤回，结论留下。** 同一晚 `println(1); println(2)` 的 `std::gir` 首导里，`N01 -> N03` 上 `ORDER` 与 `SYNC` **并见** —— 一个 `sync` 块里有副作用的相邻语句之间**有**链边；上一段那张"零 SYNC"的图是纯函数 `g(x) = x` 那次，不是这一条。这条前提（"每条语句都被挂上链"不成立）另有一个证人：`src/runtime/draft_inline.cpp#inlineCallableInDraft` 拼进来的函数体里，`id=13` 是语句却没有标记 —— 那不是 `sync` 块，是拼进来的一段体。见 [`a-structural-nodes-chain-position-belongs-to-its-join.cn.md`](2026-11-03-a-structural-nodes-chain-position-belongs-to-its-join.cn.md)。

## 例外清单

| 位置 | 为什么留着 |
| --- | --- |
| `src/passes/opt/generic/generic.cpp:342` | 读的是"构建器做过什么"，不是节点的形状：**这一处标记编码的是已发生过的事件** |
| `include/camel/runtime/effect_liveness.h:35` | P0-4 的伪单点，已单独排队 |

## 两句结论

- **这一处标记编码的是已发生过的事件**，不是形状；形状能推导，事件不能。
## 默认

- **一个生效但判错的守卫比不留更糟**：`draft.cpp` 那一处不只是冗余，它还在拒绝本该合法的链位交让 —— 删掉它，`branch.graph` 与 `generic.gate_branch_wait` 自己就绿了。

## 与另一页的关系

`sync` 块里两条副作用语句之间没有链边，本身是一桩独立的事，已另立一页（`is-there-a-chain-edge-between-two-effects-in-a-sync-block`），默认应当有、缺的是建图。**若那一页的缺陷被修好，"每条语句都被挂上链"就成立，`generic.cpp:342` 的标记届时可以退役** —— 也就是说这一条例外是**有条件的例外**，不是永久例外。
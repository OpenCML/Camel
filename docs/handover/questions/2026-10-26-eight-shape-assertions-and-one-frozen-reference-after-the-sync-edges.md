# 同步边落地后，8 条"形状断言"和 1 条"冻结参照"该怎么改？（施工方不动断言，等你口径）

车道 `handover/h242-edges`（同步边行，H242）这一批：链位点改**同步边**（对齐 `58d4cb77f` 的 BRCH→JOIN 写法）＋一条"GATE 必须有一个同步前驱"的运行期约束（`draft.cpp`/`appendInput`，原则 5.3、4.6）＋`gates.cpp:26 bypassValueGate` 认 `syncInputsOf`；38 条红被修掉、**0 条新增**、`known`/`skip` 不增。下面两类红是它**按纪律没动**的：

## 一、8 条形状断言（`unread_gate` / `control_data` / `integrated` / `other_wait`）
逐字：
- `unread_gate.graph.{inline_dce,inline_dce_twice,opt,opt_dce}`：`unread/data-covered gates survived or return anchor removed`；
- `control_data.graph`（同上族）；
- `integrated.graph.{true,false}`：`integrated algebra policy not applied`；
- `other_wait.graph`：`ordinary consumer gates remained`。

原因：新的 5.3「**会等待的 GATE 不可删**」＋"链尾要有自己的 GATE"会**合法地**改变这些图的形状 ⇒ 这些断言是旧形状的口径，**它们该怎么改（改成新形状？还是改成"只断言不变量"？）请你说**，施工方不自行削弱断言。

## 二、1 条冻结参照
`algebra_rules.divergence.differs`：计划原文自己写着 `.deleted_table.` 那张金标是"**过去参照，从这个树里再也产不出来**"，而库侧现在带同步边 ⇒ `pairs.mjs` 的 `signature()` 比 `e.kind`，**结构性必红**。你要的是：换掉这张参照、还是给它一个"已知不可再生产"的正式标记（类似 `known_fail` 之外的口径）？施工方没动。

## 三、同一批里已确认不能单独落的一件事（与你的已知口径一致，仅备案）
返工清单第 2 条要求"同批删掉被取代的控制边"，实测**不能单独落**：`fail 89→302`（+219）。因果＝链的 ctrl 边目前是消费方唯一能读到的用户引用；逐字样本 `consecutive_sync_calls.inline.*` 输出变 `""`（4 个效果全丢）、`std::opt` 报 `Runtime graph '__root__' breaks its edge invariants … node 2 (11): lies on a cycle of inputs`。这与你 `edges.no_ctrl_edge_kind.known_fail_note` 写的"53 个读 ctrl 的文件 + 26 张图 143 条 CTRL 边必须同一次提交"是同一件事 ⇒ 施工方**保留了双写**（过渡只在车道分支上），把它作为**一件独立的大活**排在后面做。
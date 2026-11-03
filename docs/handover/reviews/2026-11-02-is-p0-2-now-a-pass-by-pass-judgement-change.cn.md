# P0-2 是否按实测改成两件一起做？

**问：P0-2 是否改成同时做两件 —— ① 修正"sync 边的终点必须是 `sync` 块写过的节点"这条不变量；② 让依赖控制边的 pass 改读 sync 边？默认建议：甲。**

## 证据（全是读数）

1. **一个 pass 的锚点从控制边改读 `syncInputsOf`** ⇒ `fault` 族 **177 → 40**（`1c97c4792`；两份 `fault` 计划 `176/176`、`28/28`，全量 `8447/8435/0`、`known 2`）。**这一片已经落地。**
2. **剩下 40 条是同一个根因**（同诊断首行）：`stdout does not start with ...` + `expected diagnostic RuntimeDiag::DivisionByZero`，而 stdout 里是 `Runtime graph 'pick' breaks its edge invariants` / `sync edge 5 -> 6 ends on a node no 'sync' block wrote` —— **程序在开跑前被边不变量否掉**。
3. **只换边、不改不变量的整树读数 = 4192 条红**（`total 8447 / pass 4245 / fail 4192`）—— **这个数是 P0-2 的上界，不是终局读数**：终局还包括"把这条不变量该说的东西说对"。

## 那条不变量（原文，`src/runtime/graph_edges.cpp#EdgeChecker`）

```cpp
if ((user->flags & kGCNodeFlagSyncStatement) == 0 && user->kind != GCNodeKind::Gate) {
    bad("ends on a node no `sync` block wrote ...");
}
if ((pred->flags & kGCNodeFlagSyncStatement) == 0) {
    bad("starts at a node no `sync` block wrote ...");
}
```

**它曾经是对的**：sync 边原本只由 `sync` 块产生，而 `sync` 块写过的节点由 `src/compile/gir/builder_helpers.h#isFailCall` 打上这个 flag。**P0-2 之后它要回答的是"这条链的终点是不是某个等待的承担者"，而不是"它是不是 sync 块写的"。**

## 两条路与代价

- **甲（默认建议）**：**先修不变量，再逐 pass 换判据。** 一次把墙挪对；**代价**：改的是"图能不能成立"的判据，**改错会让整棵树在开跑前崩**（今天的探针演示过）。
- **乙**：**先逐 pass 换判据、不变量最后动。** 每一步都可验证；**代价**：每一步都在撞那堵墙，**单步读数会长期停在 40 条**。

**默认里的限定**：**先只把不变量的检查点写清（它要求什么、为什么曾经对），再改它的判据 —— 不许一步放宽到只查 `flags`。**

## 与那两条 `known_fail` 的关系

`test/plans/feat/linear/effects_model_edges.plan.toml` 的两条标记（owner 已改成 `P0-2 (batch item 9, ruled 23:30)`）**在 P0-2 落地的同一笔里摘掉**。
**裁定于 2026-11-02 04:50**：甲——先把 sync 边终点不变量改对（按节点性质判），再逐遍改判据。见 eviews/questions-2026-10-29.md。

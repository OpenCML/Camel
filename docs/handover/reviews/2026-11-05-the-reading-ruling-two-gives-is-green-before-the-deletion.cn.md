# 第二条裁定给的那条读数，在删除前就是绿的

**判据一句话**：裁定把 `branch.graph` 与 `generic.gate_branch_wait`「转绿」当作删守卫的读数，可这两处在删之前就是绿的（`[PASS] 200.63 ms`、`[PASS] 206.02 ms`），删之后仍是绿的（`[PASS] 201.65 ms`、`[PASS] 202.18 ms`）——所以这两处不能当判据，而这四行也**不**说明守卫无用。

状态：待裁。基线 `origin/develop` `8e2159034`，量在 `D:\Projects\Camel-wt\lane-markguard`（树已还原、无提交）。

## 要你裁的是什么

**第二条单独先删那处守卫，还是照裁定原文与第三条在同一次合并里一起做？**

## 量出来的读数

裁定原文（`origin/fsh-dev` 的 `faf95b781`，2026-11-05 11:30，第二节）说：「`draft.cpp` 那一处判错的守卫直接删掉，`branch.graph` 与 `generic.gate_branch_wait` 转绿就是读数。」

在 `8e2159034` 上把那段守卫删掉，删前删后各量一次（`test/plans/feat/opt/gate_value.plan.toml#branch.graph`、`test/plans/feat/opt/screens.plan.toml#generic.gate_branch_wait`）：

| 时点 | 用例 | 读数 |
| --- | --- | --- |
| 删前 | `branch.graph` | `[PASS]` 200.63 ms（同一计划 `total 187, pass 187, fail 0`） |
| 删前 | `generic.gate_branch_wait` | `[PASS]` 206.02 ms |
| 删后 | `branch.graph` | `[PASS]` 201.65 ms |
| 删后 | `generic.gate_branch_wait` | `[PASS]` 202.18 ms |

被删的那段守卫在 `src/runtime/draft.cpp#GraphDraft::takeStatementPlace` 里，原文四行：

```cpp
if ((oldHeader->runtimeFlags & kGCNodeFlagSyncStatement) == 0) {
    // Nothing stood in a chain, so there is no chain place to take.
    return;
}
```

`kGCNodeFlagSyncStatement` 在 `draft.cpp` 里三处：两处**写**——`src/runtime/draft.cpp#GraphDraft::handChainPlaceTo` 的 `markStatement` lambda，与 `src/runtime/draft.cpp#GraphDraft::takeStatementPlace` 里把标记交给替代节点的那一处；一处**读**——同一个 `#GraphDraft::takeStatementPlace` 开头，就是上面这四行。裁定第三条说「写标记的 8 处一起删掉」，`draft.cpp` 这两处写就在那 8 处里。

## 我认为的解释

**第二条的读数只在第三条一起做完之后才出现。** 第三条要删的 `kGCNodeFlagSyncStatement` 正是守卫读的东西，而写标记的 8 处要一起删：标记还在写、检查还在问标记，单独删掉这四行，等于把唯一的读者拿掉，写的人照写，那两处用例照旧绿。

所以这四行读数的意思要拆成两句，不能合成一句：

- `branch.graph` 与 `generic.gate_branch_wait` **今天绿，是因为别的原因**；守卫删不删，这两处都绿。
- **「删了没变化」不能推出「守卫无用」，只能推出「这两处不能当判据」。**

## 默认

**不单独做第二条；第二条与第三条按裁定原文「同一次合并」一起做，届时那两处若有变化，用那时的读数。**

这条默认**已经开始执行**：`lane-markguard` 已停手、树已还原、无提交（HEAD 仍是 `8e2159034`，`git status --short` 空）。

## 判据

这两处不能用来说明第二条做没做：它们删前删后都绿，所以「删了没变化」只说明这两处对这个判据不敏感。第二条与第三条一起做完、在那一棵树上量的读数才算数；在那之前，不要拿这两处的绿去换一条「守卫无用」的结论。
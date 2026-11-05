# 分支之后，链位由谁承接？

## 问什么

一个 `sync` 块里，分支之后的语句接在谁后面？三条自然写法都试过了，没有一条能同时满足"seal 不拒、语义对、红更少"：

| 写法 | 链位落到 | 读数（tier fast, jobs 8） | 结果 |
| --- | --- | --- | --- |
| (a) 交给值的后继 | 值的 sync 后继（可能是 join） | 8521 / pass 5377 / **fail 3124**，forks 0 | join 不是语句，seal 拒；`10_sync_block.{nvm,fvm,jit,prl}` 四条红 |
| (b) 链位不动、边照写 | 分支前那条语句，但 join 仍拿 sync 入边 | 8521 / pass 5518 / **fail 2983**，forks 0 | 分支体**没人等**，`The branch has side effects but is never executed` 出现 **21** 次，`10_sync_block` 跑起来但只打出 `the body is a sync block` 和 `0` |
| (c) 链位不动、边不写 | 分支前那条语句，值不写 sync 边 | 8521 / pass 5447 / **fail 3054**，forks 0 | 目标绿、警告 **6** 次（半一 10 次）；`linear.cml` 仍 2 条违例 |

基线 `e26d8381d` 是 8515 / 4736 / 3759，`linear.cml` 3 条违例。**(c) 是唯一同时做到"目标绿 + 红更少 + 不丢副作用"的一组**，已落 `22c95bf3c`（半一 `8585d8de6` 之上）。

## 为什么这不是写法问题

`builder_control.cpp` 建完分支后有这么一段（注释说明这条边非写不可："the layout reads the join's sync predecessor as the statement written before the branch"）：

```cpp
if (lastSyncedNode_ != nullptr && linkCheek(lastSyncedNode_, joinNode)) {
    markSyncStatement(lastSyncedNode_);
    markSyncStatement(joinNode);
    linkNodes(LinkType::Sync, lastSyncedNode_, joinNode);
}
lastSyncedNode_ = joinNode;      // (b)/(c) 试的就是去掉这一行
```

- `JOIN` 在 `nodeTakesAnOrder` 里**是**能排序的节点，在 `nodeIsChainStatement` 里**不是**语句；
- 让 seal 接受 join（给它加一个 `case`）读数更差：**8519 / 4701 / 3798**，比不放开多 39 条红；
- 让链位不动（b）读数最好但语义错；让值不写边（c）折中。

三组摆在上面，缺的是**一条规则**，不是第三种写法：`edges.cn.md` 1.5 / 5.3 没有说"分支的 join 在链上算谁"。

## 要作者裁一句

**分支之后，链位该由谁承接？**

1. **分支的 join** —— 那就得说明 join 为什么可以占链位（它不是语句，但它是块值的承接点），并让 seal 的"起点必须是语句"对它放行；
2. **分支前那条语句** —— 那就得回答分支体里的语句由谁等（(b) 的实测是没人等，副作用被安静丢掉）；
3. **别的**（例如分支自己也占一格、join 从分支接）。

裁完要改的地方是一处：`src/compile/gir/builder_helpers.h` 的 `writeSyncLink` / `releaseChainPlaceOfValue`（以及 `builder_control.cpp` 那一行 `lastSyncedNode_ = joinNode`）。

## 默认

> 基线 `e26d8381d` 是 8515 / 4736 / 3759，`linear.cml` 3 条违例。**(c) 是唯一同时做到"目标绿 + 红更少 + 不丢副作用"的一组**，已落 `22c95bf3c`（半一 `8585d8de6` 之上）。

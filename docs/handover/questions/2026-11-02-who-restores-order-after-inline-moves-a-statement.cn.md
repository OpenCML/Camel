# 内联把语句搬进别的块之后，次序该由谁补？

**答案：这一页记的是一条**没有做成**的判断。`05_effect_order` 要求 `inline + NodeVM` 的副作用轨迹与 NodeVM、FastVM 逐行相同，而内联改变块归属之后，被搬语句之间、以及被搬语句与调用者块下一条副作用语句之间的次序，今天没有任何一处补。补它有两种可能的位置，选哪个不是读代码能定的。**

## 现象

`src/runtime/draft_inline.cpp:1670 inlineCallableInDraft(…)` 是拼接函数体的地方。它已经在算"体前要等什么"（`:1878-1888`）：

```cpp
// What the body waits for: the call's predecessors, and the effects of arguments it never
// read.
std::vector<gc_node_ref_t> waitPreds = ctrlPreds;
for (gc_node_ref_t effect : unreadArgumentEffects) {
    if (std::ranges::find(waitPreds, effect) == waitPreds.end()) {
        waitPreds.push_back(effect);
    }
}
const bool bodyWaits = callFacts.hasWaitEdge || !unreadArgumentEffects.empty();
```

**体前的次序在这里。体后的次序不在这里，也不在别的任何地方。** 于是内联之后，同一个 `sync` 块里原来的两条语句，一条被搬进来、一条留在外面，它们之间的先后没有任何东西记着。

`src/passes/opt/inline/engine.cpp:1201-1205` 重接的是"调用节点的**使用者**"（`norm`/`with`/`ctrl`/`sync` 四组），**不**处理被搬语句彼此之间，也**不**处理链尾该接回调用者的哪一条。

## 两条路

- **A 内联自己补**：拼接完成后，取被搬语句链的链尾（沿 `syncUsersOf` 走，走到没有同步使用者为止），接回调用者块里 `syncUsersOf(funcNodeId)` 的第一个。零新字段，写边用现成的 `draft.setSyncInputs(id, span)`（`include/camel/runtime/draft.h:679`，先例 `src/passes/opt/generic/generic.cpp:439`）。
- **B 图定型之后有一条统一的重推通道**：内联什么都不补，由"图已定型、调度之前"那一点统一重算次序。这一条与另一页（`2026-11-02-when-should-an-underivable-sync-edge-be-dropped`）是同一个问题：**次序只在图定型之后才有定义。**

## 代价

- **A** 的代价是"每个会搬语句的遍都得自己补一次"；**今天只有一个 inline**，所以便宜，但它把"次序"这件事分散到了遍里。
- **B** 的代价是"冻结图没有重发 payload 的通路"（见上引那一页）；要做得先有那条通路。

**A 今天就能写完，B 要作者裁。** 这一页没有落 A，因为在 `inlineCallableInDraft` 里找"被搬语句集合"这个变量需要读完 `:1670` 到 `:2200` 整段（局部状态五六个），而在改错位置时**编译期看不出来、只会改变次序**。

## 要作者答的一句

**「内联改变块归属之后，次序该由内联自己补（A），还是由图定型后的一条统一通道补（B）？」**

**我方默认：A**，因为今天只有一个遍会搬语句，而 B 需要一条目前不存在的通路。
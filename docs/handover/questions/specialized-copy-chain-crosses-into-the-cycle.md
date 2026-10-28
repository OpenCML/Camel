# 副本的调用指向下一份副本之后，链跨进了调用环

上一页问的是"副本体内的调用能不能指向自己"。作者的回答是：按它自己的特化键解析，"指向自身"只是键相同时的特例。照这条实现完之后，量到的不是"目标不绿"，而是**链一长出来，下游就垮**。这一页只放证据和一句要问的话。

## 我量到的

### 这个位置本来就有一道门挡着

副本体内的那条调用同时命中两件事：它所在的分支臂，条件是运行期才知道的；它的 callee 又在调用环上。`src/runtime/draft_inline.cpp#collectDirectFuncSpecializations` 在两者同时成立时，会把按值绑定全部擦掉，只留类型（否则这条调用会一级一级展开下去）。实测打印：

```
PROBE copy run inner call 23 inArm=1 cycle=1
PROBE copy countdown inner call 12 inArm=1 cycle=1
```

门在的时候，第一版改动——在编码副本之前，把副本体内所有 `callee` 仍是基函数的 FUNC 交回 `specializeDirectFuncInDraft`，也就是作者说的"同一段调用点代码"——**完全无效**：按值绑定被擦光，没有键可以解析，调用原样留在基函数上。读数 `feat/opt/generic` **total 97 / pass 95 / fail 2**，与改动前同名同句；红的两条是 `static_branches.simplify`（基线红）和 `shape_unroll.simplify.rgir`（原句 `forward calls run, not an instance of the recursion`）。

### 门放下之后，链成立，下游全垮

只对"正在编码的副本体内的调用"放开那道门，链确实长出来：`run$spec0` 的调用指向 `run$spec1`。同一份计划的读数变成 **total 97 / pass 77 / fail 20**（基线 95/2）。

- `shape_unroll.simplify.rgir`：从 `forward calls run, not an instance of the recursion` 退化成 `forward not in the dump` —— 连 `forward` 这张图都不在转储里了。
- 其余受影响的用例（`effect_order_folded.simplify.nvm` / `.fvm` / `.jit`、`arm_tail_gate.*`、`reshape_view_gc.fold_specialize.nvm`、`specialize_growing_shape.*`、`recursion_unroll.simplify`、`shape_unroll.cse_simplify.nvm` 等）失败形态一致：

```
[INFO] expected exit 0, got null
[stdout] (empty)
[stderr] (empty)
```

`got null` 加两栏皆空是进程被杀，不是断言差异。

### 反证：不是预算太小，掐到一步读数不变

裁定说终止靠特化预算。这里的预算在 `src/passes/opt/inline/engine.cpp#applyRuntimeOptimizeRewrite` 里是**节点数**（`growthSpent` 与 `growthBudget`），不是链的级数。一份 `run` 副本约 20 个节点，四千的预算就意味约两百级链。我把剩余预算接进会话、每造一份副本按它的节点数扣减，得到上面那 20 条红。

决定性的反证是把链**掐到只走一步**（预算压到一份副本的量：`run$spec0 → run$spec1` 之后，`run$spec1` 的调用停在基函数上）——**读数一字不变，仍是 total 97 / pass 77 / fail 20**。所以坏的既不是链的长度，也不是预算的大小，而是"副本的调用指向另一份副本"这件事本身。

## 为什么会这样

`run$spec0 → run$spec1 → run`，而 `run` 自己就在调用环上：它的体内直接调用 `run`。副本的调用一旦指向下一份副本，这份副本就已经跨进了那个环。

第 1 笔的门和随后的内联都建立在"环里的 callee 不复制"上。副本体内的调用指向另一份副本之后，链尾那份再指回基函数，整条链就是一个环，链上每一份都在环里——而这条线的前提正是"环里的 callee 不被复制"。

裁定给的退路是"预算用尽时那条调用留在基函数上"。按这个形态，退路要等链停下来才出现，而环在链长出第一步之后就已经成立：`run$spec0` 指向 `run$spec1` 的那一刻，`run$spec0` 已经经由 `run` 进了环。所以预算取任何值都不是终止条件，只是把崩溃推迟到第几级。

## 要问的一句

副本体内的调用指向下一份副本之后，**那个环由谁负责收**——是折掉（把链上的调用一路折成确定的值），还是封住（让环上的副本不再被复制和内联，只在环外那一份上保留调用）？

这一句没有答案之前，预算取什么值都改变不了上面的读数；代码停在第 1 笔，读数仍是 7476 / `fail 7`（与基线同名）。

## 答复在哪

作者对上一页的回答不在这条线的任何提交里：它是 `dbfae2cf1`，主机 `camel-work/h-edges` 仓库里的一个不可达提交（唯一父 `6ff81e46e`，只追加 `docs/handover/reviews/questions-2026-10-26-late.md` 的 `## specialized-copy-cannot-self-call` 一节，没有源码）。现在它由 `probe/recover-dbfae2cf1` 钉住，并取到本地 `recover/dbfae2cf1`；要看全文用 `git show recover/dbfae2cf1`，`docs/handover/questions/specialized-copy-cannot-self-call.md` 也记了这一处。
# `mul_tensor_t` 一族律要读的操作数值，由谁交过来

**判据一句话**：删掉零载体之后 `erf`/`erfc`/`linear` 三条梯度用例从绿变红，红的方式是**具名拒绝**而不是错答案 —— `skip=law-reads-a-value uri=tensor:multiply position=0 reason=the rule's right side reads a value the caller does not carry`。**这说明载体冒充的是值，不是形状**；而「删掉它以后还全绿」这件事，今天不成立。

## 读数（原样，`2026-11-05`，`lane-seed23` 上 `a5bbafbc2` 之后的一次试验）

只删 `modules/transforms/transpose.cpp` 里「把 `shapeFromTypeOf` 造的零数组当作参数的正向值交给律」那一段（`buildTransposeMember` 的 `!bundlesResiduals` 分支），未动别处：

```
TRANSPOSE function=split_of_jvp_of_total_plain_backward skip=law-reads-a-value uri=tensor:multiply position=0 reason=the rule's right side reads a value the caller does not carry
error[RuntimeDiag::RuntimeError]: ... transports: transpose has no transpose for this function: the rule's right side reads a value the transposed function is not handed, and no declaration of a shape supplies it.
```

`node scripts/test.js test\plans\feat\compile\transforms.plan.toml`：**`total: 195, pass: 179, fail: 3, known failures: 13`**，红的三条是
`erf_gradient_both_legs.nvm`、`erfc_gradient_both_legs.nvm`、`linear_gradient_both_legs.nvm`。
同一棵树上不删那一段时是 `pass: 182, fail: 0`。试验已回滚，树留在 `a5bbafbc2`。

## 结论：那三条绿是零载体补位补出来的

- 那三条用例的库半边都走 `vjp` → 再转置一层（`split_of_jvp_*_backward`），律是 `mul_tensor_t`：
  `mul_tensor(x, w) => (ct: T[...]) => (sum_to(ct * w, shape(x)), sum_to(ct * x, shape(w)))`。
- 右式里 `shape(x)`/`shape(w)` 要的是**形状**，`modules/transforms/rule_expand.cpp#answerTheShapeOf`
  已经把「只被 `tensor:shape` 读的 capture」用类型所报的 extents 答掉（作者 2026-11-03 17:35/17:55
  的裁定），**那一半不需要载体**。
- 但同一句里 `ct * w` 要的是 `w` 的**值**。`valueOf(argument)` 答不出来时，
  `rule_expand.cpp` 给出这句 `why`，`transpose.cpp` 把它记成 `LawReadsAValue`。
- 零载体让 `valueOf` 在那种情形下**答出了一个零数组** —— 于是展开「成功」了。所以那三条绿不是
  「律拿到了 `w`」，而是「律拿到了一个不是 `w` 的东西，而这条路径上恰好没被读出来」。
  **载体冒充的是值，不是形状。**

## 问题

`mul_tensor_t` 一族律要读的操作数值（`ct * w` 里的 `w`、`ct * x` 里的 `x`），**应该由谁交过来**：

1. **律的签名加一个真正携带该值的端口**（值由调用方作为操作数交过来，与 `shape` 的 `at` 端口同一条思路）；
2. **`jvp` 的 backward 半边把该值携带过来**（让它成为残差端口一路传下去）；
3. 别的方式。

## 默认建议（按它继续）

**(1)**。理由三条：

- 作者 `c3308d18d` 的原话是「形状由 jvp 规则作为已知实参交过来」。**把同一条思路推广到「值」，
  就是「值由调用方作为操作数交过来」**，而不是让某个半边偷偷携带；
- (2) 会让 `jvp` 的携带规则变复杂，而**携带规则正是 B1（`tangentArgs`）要删掉的东西**，两条互相冲突；
- (3) 没有具体形态，等于不选。

## 判据（两条要同时成立，缺一条就不算做完）

1. 那三条**回到绿**，且 `skip=law-reads-a-value uri=tensor:multiply` 不再出现；
2. **删掉零载体之后仍然全绿**。

> 最容易骗过自己的地方：加上端口之后那三条回绿，就以为零载体可以删了。
> **绿可能是别的机制补出来的** —— 今天的读数就是这句话的实例。只有两条一起成立，
> 才是「零载体可以被删」的证明，而不是「删了会红所以留着」。
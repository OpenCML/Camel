# `mul_tensor_t` 一族律要读的操作数值，由谁交过来

> **此默认建议已被实测推翻**（`lane-capturectx`，2026-11-05 晚）：形状只能以**已知操作数**的身份交过来；
> 捕获端口这条路对「体里的调用点」可行，对本页那三条用例**永远不可用**。推翻过程、两条硬读数和


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

**(3)，形态是「形状以已知操作数的身份交过去」** —— 律与它自己的节点操作数之间的事，不是律与调用方之间的事：

- `jvp` 侧写 `matmul_stated(a, b, shape(a), shape(b))` / `mul_stated(x, w, shape(x), shape(w))`
  （`stdlib/rulesets/jvp.cml#jvp_linear`、`#jvp_erf`、`#jvp_erfc`），转置侧才轮到
  `stdlib/rulesets/transpose.cml#matmul_stated_t` / `#mul_stated_t`；
- **不新增签名端口**。多出来的两个操作数是**图里已经有的值**：`shape(x)`/`shape(w)` 都是从残留量
  算出来的 `int[]`，转置时随图一起重算，所以 `transpose(lin)` 那个 `(ct) => …` 的调用约定一个字都不用改；
- 这也正是作者 `c3308d18d` 的原话「形状由 jvp 规则作为已知实参交过来」落到乘法族上的样子，
  与 `jvp_matmul` / `jvp_mul_tensor` 已有的写法一致（`transpose.cml` 的注释早就点名
  「`#jvp_mul_tensor` 和 `#jvp_matmul` 这么做，`#jvp_linear` 还没有」）。

原先的默认是 **(1) 律的签名加一个真正携带该值的端口**，理由三条：推广作者那条「已知实参」的思路；
(2) 会让携带规则变复杂，而携带规则正是 B1（`tangentArgs`）要删掉的东西；(3) 没有具体形态，等于不选。
**这三条理由今天仍成立，但 (1) 的做法被实测否掉了**，见下一节。

## 推翻：(1) 为什么不可行，以及两条硬读数

试验：`lane-capturectx` 上真把「读到未携带的 capture 时记一条 need、让探针重来、签名长出端口」实现了
（探针改成不动点循环；`modules/transforms/transpose.cpp#buildTransposeMember` 两处 `LawReadsAValue`
前先走 `parametersBehindAnUncarriedValue`）。**中间读数全是好的**：

- 探针自然收敛：`erf` 1 轮（`need param=0 type=float[4]`）、`linear` 2 轮（`param=0`、`param=1`），无重复问；
- 端口真的长出来了：`TRANSPOSE function=split_of_jvp_of_total_plain_backward graph=393 type=(float[4], float) => (float[4],)`；
- `skip=law-reads-a-value uri=tensor:multiply position=0` 不再出现。

**但交付不出去**，于是三条用例仍红（`total: 195, pass: 178, fail: 5, known failures: 12`，另两条只是诊断文字变了）：

```
TRANSPOSE function=split_of_jvp_of_total_plain_backward ports=2 promised=1 cause=parameter-shape param=0 param-type=float[4]
```

建出了两个端口，可 `stdlib/vjp.cml#vjp` 的调用点是 `transpose(lin)`，操作符的类型 `(ct) => …`
**只承诺一个端口**，第二个值没有位置可放。**分界就在这里：捕获端口 / need 对「体里的调用点」可行
（那里调用方就是图里的一个 Call，多给一个实参是合法的），对 `transpose(lin)` 这个操作符永远不可行。**

为什么那两条律**真的**需要那条 need —— 两条硬读数，都不是「实现漏了」：

1. `stdlib/rulesets/transpose.cml#matmul_t` 的右式是 `(ct_of_a(ct, a, b), ct_of_b(ct, a, b))`：
   被作答的操作数**只作为调用实参**出现，reader 是 **Call** 而不是 `tensor:shape`。
   `modules/transforms/rule_expand.cpp#answerTheShapeOf` 只答「直接被 `tensor:shape` 读」的 capture，
   所以 `a` 的类型明明是 `float[2, 2]`，`shape(a)` 也答不出来 —— 形状在类型里，读的语法却在别的函数体里。
2. `#mul_tensor_t` 那条（`erf`/`erfc`）：被作答操作数的**节点类型是 `float[...]`**，而它底下的叶子
   全是 `float[4]`（实测链：`9→8→7→{float 常量, 6}→{Port float[4] 残留, Port float[4] 参数}`）。
   `T[...]` 模式的实例化只固定了 `T`，秩没有进类型，所以**静态形状根本不存在**，律只能要值。

两条合起来说明：这三条用例缺的是**形状**，不是值；而形状一旦写进节点操作数（`*_stated`），
两条律的右式里就**一次 `shape()` 读都没有了**，问题从「谁携带值」变成「谁写下列表」——答案是 `jvp` 侧。

试验的补丁留在树外：`D:\Projects\Camel-wt\a-attempt-lane-capturectx.patch`（**能编过**；
红在上面的 `ports=2 promised=1`；树已还原、未提交）。它是这一节的证据，不要删。

## 判据（两条要同时成立，缺一条就不算做完）

1. 那三条**回到绿**，且 `skip=law-reads-a-value uri=tensor:multiply` 不再出现；
2. **删掉零载体之后仍然全绿**。

> 最容易骗过自己的地方：探针收敛、端口长出来了、`skip=law-reads-a-value` 不见了 —— 三个好读数合起来
> **仍然可能没用**，因为值交付不出去。判据永远是那三条绿的，不是中间读数干净。
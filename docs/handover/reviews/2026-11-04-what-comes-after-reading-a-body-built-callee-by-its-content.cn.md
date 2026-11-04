# 调用的被调值是体自己算出来的函数值：按内容读完之后，答案从哪儿来？

**状态**：**已按默认甲落地**（2026-11-04，本条车道，派工授权）。这一页记的是一个**两条裁定相交处的缺口**：04:20 说这种调用“按内容处理、现在就做”，做完之后它仍然只能拒绝——而拒绝的理由是 16:15 的另一条裁定，不是没做。

## 1. 两条裁定相交的位置

- 2026-11-04 04:20（`docs/handover/reviews/questions-2026-10-29.md`，`## 裁定（2026-11-04 04:20）：48 页一次答完`，第 16:15 条）：`who-reads-the-first-operand-as-a-callee` —— 删掉“第一个操作数是被调方”那条读法是对的；**剩下的 `mk(x)(2.0)` 按内容处理：调用一个函数值，转置成调用 `transpose(v)`，现在就做**。
- 2026-11-03 16:15 + 2026-11-02 22:30：`Transpose<F>` 的形状是 `(ct) => (..Tangent<ParamsOf<F>>)`，**转置后的函数只拿余切，没有值参数、没有 `with` 列表**（`stdlib/types.cml#Transpose`，构建是 `modules/transforms/transpose.cpp#transposeSignature`）。

把这两条放在同一个程序上，`f(x) = mk(x)(2.0)`、`mk(x) = (y) => x * y`：

1. `f` 的图里，那个 CALL 节点的 `with` input 是 FUNC 节点 `mk(x)`（即 `mk` 的调用），普通输入是 `2.0`；
2. 按内容，这次调用的被调值是 **`mk(x)` 这个值**，它的转置是 `transpose(v)`；
3. `transpose(v)` 需要一个 `v`。`v` 是 `f` 的体**算出来的**，不在任何闭包里；
4. 转置后的成员只拿余切（第 2 条裁定），所以它**没有地方拿到 `v`**：
   - 在成员里重算 `mk(x)`？那要 `x`，而 `x` 是“参与”的参数，没有端口（16:15：`x` 由余切带走，不传）；
   - 让成员收 `v` 当一个值端口？那正是 22:30 明令不要加的东西。

所以“按内容处理”在这一格上给出的不是答案，是**一条按内容说出来的拒绝**。这就是落地的内容。

## 2. 落地与读数

改一处：`modules/transforms/transpose.cpp#buildTransposeMember` 的 CALL 分支里，问 `with` 输入的那一问从 `GCGraph::calleeIdOf`（它对**任何** FUNC 节点作答，答的是“那次调用的被调方”，于是 `mk(x)` 被读成 `mk`）换成 `GCGraph::staticFunctionGraphOf`（答“这个**值**命名哪张图”，对调用的答案回 `not-a-data-node`）。

改前 / 改后（树 `D:\Projects\Camel-wt\withport2` @ `d7f15f2b3`+本条车道改动；宿主 `/home/zjwei/camel-work/withport2`；二进制 `2026-11-03T23:18:19-05:00`，1m load 22–24；`-v`；`std::nvm`；用例 `test/cases/compile/transforms/transpose_callee_is_a_returned_function.cml`）：

| | 读数 | exit | 出数 |
|---|---|---|---|
| 改前 | `TRANSPOSE function=mk skip=signature` + `TRANSPOSE function=f skip=call-no-transpose node=3 callee=mk` | 1 | 前向 `6.0`，转置不给数 |
| 改后 | `TRANSPOSE function=f skip=call-no-transpose node=3 callee=none value=2:0 with=1:10 reason=not-a-data-node` | 1 | 前向 `6.0`，转置不给数 |

改后那句话是：`transforms: transpose has no transpose for this function: this call's callee has no transpose of its own, so the call cannot be turned around.`

判决没变（拒绝，不给数），**名字对了**：这个图里没有任何节点命名被调方的那张图，`reason=not-a-data-node` 就是这句话；`mk` 不再被指到头上。手算的答案是 `(2.0)`，这一格仍然是“欠着的答案”，不是“答错的答案”。

## 3. 答案在谁那儿

`transpose(v)` 这条路**对它能拿到 `v` 的形状是通的**，而且今天已经有数钉着：`v` 是图**收到的**值时（`with` 参数，或 cut 交给后一半的闭包端口），成员照原样读同一份闭包，`modules/transforms/transpose.cpp#buildTransposeMember` 走的就是这一支。

- `test/cases/transforms/transpose/residuals_in_the_closure.cml`：`jvp(f) → split → transpose(lin)`，`f(x, y) = x*x*y` 在 `(3, 2)`：`value= 18.0`，`back dx= 12.0  back dy= 9.0`（手算 `2xy = 12`、`x² = 9`），实测逐字相同。
- 体自己算出来的那种值，由 `split` 当**残差**带进 `lin` 的闭包，`lin` 的转置才有地方读它——那是 `stdlib/vjp.cml#vjp` 的组装（`split(jvp(f))` 之后 `transpose(lin)`），不是“把一个具名图转置”。

所以第 1 节第 4 步的缺口不是编译器的缺陷：它是 16:15 那条裁定（转置函数只拿余切）在这一格上的**直接后果**。要答 `(2.0)`，得让 `f` 的转置成员收下 `v`——那要先改 22:30/16:15 的声明。

## 4. 我方默认（已落地，无需再裁）

**甲：保留拒绝，名字按内容说。**

理由按次序：

1. **对**（`(2.0)`）在这一格上要求的是一个值端口，而值端口是 22:30 明令不加的——两条裁定不能同时满足，选“不加端口”那条，因为它是声明（契约），另一条是读法。
2. **明确拒绝**是第二好的结果，而且现在名字是对的、理由是内容（这个值在图里不命名任何图）。
3. 错数和崩都不出现：改后的读数 exit 1，前向 `6.0` 之后停，转置不出数。

如果作者要的是“这一格答 `(2.0)`”，那要改的是 `stdlib/types.cml#Transpose` 的声明（加一个“体算出来的被调值”的值端口），并且 `Transpose<F>`、`transposeSignature`、`split` 的残差打包要一起动——请在那条裁定时写明，这一页的默认在那之前继续有效。
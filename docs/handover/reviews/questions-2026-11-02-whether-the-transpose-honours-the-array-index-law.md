# 问：`transpose` 该不该认 `idx_arr_t` 这条 law —— 即数组元素读的余切

11-02 晚，`h-m4b` 移交的 P0：`grad` 对数组形参的那条路"打完前向值之后无声中止"。
**我这棵树上的读数与移交单不同，先把两边的形状都摆出来。**

## 一、我这棵树的读数（`lane-lit2`，`origin/develop = 3927ee8f3`+ 一笔合并）

用 `vjp`（`grad` 今天只声明标量，见 `stdlib/vjp.cml`）跑移交件与四个缩小件，
**全部止在同一行**，五个形状一字不差：

```
[stdout] REINTERP split function=jvp_of_g forward=88 backward=89 type=(float[]) => (float, (int, float, int, float)) pure residuals=4
[stdout] 6.0
[stdout] TRANSPOSE function=rulesets.jvp::index_into<float> skip=law rule=idx_arr_t uri=:op/idx_arr
[stdout] TRANSPOSE function=split_of_jvp_of_g_backward skip=call node=5 callee=rulesets.jvp::index_into<float>
[stdout] (vjp)…/stdlib/vjp.cml:72:47: [Error]: transforms: transpose has no transpose for …
[stderr] (empty)      ← exit 1，不是 got null
```

| 形状 | 实参 | `rc` | 止在 |
|---|---|---|---|
| `xs[0]`（单元素） | `[3.0]` | 1 | `skip=law rule=idx_arr_t` |
| `xs[0]`（两元素） | `[3.0, 2.0]` | 1 | 同上 |
| `xs[0] + xs[1]` | `[3.0, 2.0]` | 1 | 同上 |
| `xs[0] * xs[1]` | `[3.0, 2.0]` | 1 | 同上 |
| `xs[1] * xs[0]`（移交件） | `[3.0, 2.0]` | 1 | 同上 |

**⇒ 不是某个形状出事，是"数组元素读"这条 law 没有 arm。**

**关于"无声中止"**：移交单上是 `got null` + `[stderr] (empty)`；**我这棵树两次运行都是 `got 1`**，
**plan 日志里搜 `signal`/`Segmentation`/`Aborted`/`core dumped` 一处都没有**。
**"静默中止"我没有在自己的树上复现出来**，所以我不把它当结论，只把它当移交单上的读数记在这里。

## 二、止在哪一层（定到了）

`modules/transforms/transpose.cpp:1354-1358` 的兜底 `else`：

```cpp
                } else if (law == "mul_t" || law == "div_t") {
                    …
                } else {
                    os << "TRANSPOSE function=" << function->name() << " skip=law rule=" << law
                       << " uri=" << uri << '\n';
                    return false;
                }
```

**转置器只认算术那几条 law。** 而 `idx_arr_t` 在规则库里写着（`stdlib/rulesets/transpose.cml:73`）：

```camel
export rule idx_arr_t::<T>(xs: T[], i: int) {
    index_into(xs, i) => (ct: T) => scatter_into(zeros_like(xs), i, ct)
}
```

**⇒ 缺口是"规则库声明了一条 law，转置器没有 arm"**；`else` 的注释写明拒绝是有意的
（"refused by name rather than answered with a cotangent from nowhere"）——**拒绝对，缺的是这条 law 的转置。**

**同一条路的另一侧已经有实现**：`modules/autodiff/pullback.cpp:2146-2161` 对数组元素读就散一个 slot，
发的正是 `:tangent/scatter_add_arr`；`tensor:zeros_like` 也是现成的（`modules/tensor/ops/gradient_ops.cpp`）。

## 三、问题与默认建议

> **`transpose` 是否应当认 `idx_arr_t`：对 `xs[i]` 的余切发出 `scatter_add_arr(zeros_like(xs), i, ct)`？**

**默认建议：应当。** 三条理由：

1. 规则库已经把这条 law 写出来，转置器不认它就是"声明了不实现"；
2. `pullback` 那条路已经这么做，两条路对同一个值给出同一个余切；
3. 这一条 arm 不会改变任何现有读数——今天它在 `else` 里直接 `return false`。

**代价（这也是我没有直接动手的原因）**：转置图的余切入口今天是 `Type::Float64()`
（`transpose.cpp:263-265` 的 `cotangentIn`），**数组元素读的余切是 `float[]`**。
补这条 arm 会**第一次让转置器处理非标量余切**，`addCotangent` / `transposeSignature` 的"一律浮点"假设
要跟着改成"跟着值走"。**这是设计层面的改动，不是补一个分支。**

## 四、本轮做了什么

- 应用了 `h-m4b` 的端口类型修复（`transpose.cpp:252-253`：`bundlesResiduals` 之外取 `normTypeAt(i)`，
  原先一律 `Type::Float64()`）——**读数一字未变**，说明它不是本条的原因，但它本身是真缺陷（端口把 `float[]` 记成 `float`）。
- 五条形状钉成用例（`expect_exit = 1` + `skip=law rule=idx_arr_t`、`transpose has no transpose` 两条断言）。
  **将来那条 arm 落地时这五条会一起红**，正好提醒把它们改成数值断言（`[2.0, 3.0]` 等）。

## 五、arm 试过的结果（11-02 晚补）

**这不是新增特例，是 `beeb887f4` 的同一根因在余切类型上的第二次发作**：那边是"参数端口一律 `float`"，这边是"余切一律 `float`"——
**同一个坏形状：类型写成了字面量，而不是被微分那个东西的切空间。**

`transposeSignature` 那一层**不用改**（`transpose.cpp:107-119` 已经逐参数取 `tangentSpaceOf`，`index_into<float>` 的转置签名早已是 `(float[], int, float) => (float[])`）。
按上面"三处改动"落地后，**`skip=law` 的拒绝消失**，露出来的是调用点：

```
node 14 (10): calls 'transpose_of_rulesets.jvp::index_into<float>', whose value is a (float[],),
    and is a (float, float)
node 6 (10): passes a int as ordinary argument 0 of 'rulesets.jvp::index_into<float>', which takes a float[]
```

**⇒ 调用点的实参列表与结果类型都还是"一律浮点"的老形状**（同一个根因的第三次发作）。两处一起改才绿，
**下一笔从这里开始**：先结果类型、再实参列表、最后落 arm、最后把五条用例换成数值断言。

`transpose.cpp:1354` 的兜底 `else` 的行为（点名拒绝 `skip=law rule=…`）**是对的**，记在这里免得有人把它当成缺陷删掉。

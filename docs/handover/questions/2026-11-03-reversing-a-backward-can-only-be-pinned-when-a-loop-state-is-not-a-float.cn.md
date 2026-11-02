# 反传倒着走要能钉住，先要循环状态不是浮点标量

## 问什么

`modules/transforms/transpose.cpp#transposeNodesInDraft` 的 `case rt::GCNodeKind::Loop:`（`:1704`）把原 range 照抄给反向循环，所以它正着走、不保序（设计 4.2/4.3 要求倒着走）。`split` 那一半已经倒过来（`e8c78f495`），`transpose` 这一半没动。问的是**它被钉住之前先要什么**：改 `:1704`，还是抬升先能配非标量态？这一页把钉住顺序的那条程序记下来，等先决条件落地。

## 1 钉住顺序的程序（纸面：今天跑不起来）

```cml
func f(s: float[]): float[] {
    return loop x = s for k in [1.0, 10.0] do [x[1], x[0] + x[1] * k]
}
```

逐轮映射 `M_k = [[0,1],[1,k]]`，即 `(a, b) -> (b, a + k b)`；两轮 k=1 后 k=10。`M₁₀ M₁ = [[1,1],[10,11]]`，`M₁ M₁₀ = [[1,10],[1,11]]`，两者不同，所以转置（伴随）依赖走法。取余切 `c = (1, 0)`：

- **正确**（倒着走：先 `M₁₀ᵀ` 后 `M₁ᵀ`）：`M₁ᵀ M₁₀ᵀ c = (1.0, 1.0)`
- **照抄原 range**（正着走：先 `M₁ᵀ` 后 `M₁₀ᵀ`）：`M₁₀ᵀ M₁ᵀ c = (1.0, 10.0)`

两个数差得开，一行 `expect_output_contains` 就钉得死。

## 2 它今天为什么跑不起来：两处，都在抬升

原式跑得通：`primal= [3.0, 32.0]`（`[1,2]` 过两轮得 `[3,32]`，与手算一致）。`jvp` 当场拒，报 `REINTERP jvp function=f skip=body`。临时探针（已撤）读出两处：

```
PROBE pairTypeOf-not-scalar float[]        ← pairTypeOf 不给 float[] 写对
PROBE oper-no-tangent uri=:op/idx_arr ref=5 ← 真正让 complete=false 的那一处
```

- `modules/transforms/jvp.cpp#pairTypeOf`（`:59-72`）先问 `tangentSpaceOf` 是不是 `Self`，再用 `isFloatingScalar` 拦掉聚合：它连 `float[]` 的对都不写。这是**形式限制**，不是语义未定 —— `docs/design-principles.cn.md` 第 666 行已经裁过：数字与布尔有切空间，结构体与元组"当且仅当每个字段都有"，且"循环状态、捕获的元组、反向 tape 这类机件内部的聚合只带真正需要梯度的成员，是**实现的选择**"。
- **更正（2026-11-03）**：这里原来写的是"`x[1]` 是 `:op/idx_arr`，`stdlib/rulesets/jvp.cml` 里没有它的前向规则"。**错**，那条规则本来就在（`jvp_idx_arr`，连同 `index_into`，见本页附录）。上面 `oper-no-tangent` 那一行是**症状**：探针读的是"操作数的切线在不在"，而数组 `x` 是循环状态端口、它的对没写出来，第一个读状态的操作就把这件事报了出来。缺的不是规则，是**聚合值在体里的副本与切线**：体的结果是 `FILL`，前向变换的拷贝段根本没有 `Fill` 这一格；状态端口要成为"对端口"，`pairTypeOf` 才写得出来；循环的 iterable 不能跟着变成对（它是轮次走的序列，不是参数的值）。
- 元组态对照（`loop s = t for i in 0..2 do (s.1, s.0 + s.1)`）：仍拒在 `skip=body`，命中 `:op/add_d` 的一个 **Port** 型 `float` 操作数没有副本 —— **元组态与数组态不是同一件事**，两处的失败点不同。
- 另有一面与走法无关：`transpose_loop_escape.*` 六条今天红的根因是 `REINTERP jvp function=read_one skip=loop`（带 escape 的循环抬升不了），不是顺序。

## 3 先决条件：三条路（（甲）已落地，见 §5）

- **（甲，已落地 2026-11-03）先抬升：`pairTypeOf` 给**数组**写对、`Fill` 的副本与切线补上、iterable 不配对，再改 `:1704`。** 结果见 §5：数组态循环现在抬升得起来，用例 `jvp_loop.array_state_is_lifted` 钉住整个 Jacobian（两列）。元组/结构体/带 capture/带 escape 的一律不动，仍红。
- **（乙）先补 `:op/idx_arr` 的前向规则**。作废：规则早就在（§2 更正），这条从来不是缺口。
- **（丙）先不碰顺序**：`transpose` 那一半继续照抄 range，等聚合态切线空间整体落地再说。代价：`vjp` 过循环这条路继续既不肯算也不肯错，6 条 `transpose_loop_escape.*` 继续红。

## 4 结论

**顺序这一修的先决条件是"抬升先能配非标量态"，不是改 `:1704`。** 改完 `:1704` 今天也没有一条程序看得出差别：`split_loop_taped_rounds` 的 `loop d= 30.0` 在任何走法下都是 30.0（`jvp` 只给浮点标量配对 ⇒ 逐轮映射是乘标量 ⇒ 乘法交换）。同理 `e8c78f495` 那一笔（`split` 的 backward 半程倒过来走）**今天也没有可观测差异**，"没变坏"是唯一钉得住的东西；顺序要等本页第 1 节那条程序跑得起来再钉。

## 5 抬升那一步落地之后（2026-11-03）

第 1 节那条程序现在**算得出来**（`modules/transforms/jvp.cpp`，用例 `test/cases/compile/transforms/jvp_loop_array_state.cml`）：`f((1,2)) = (3, 32)`，两个种子的切线是 `(1, 10)` 与 `(1, 11)` —— 就是 `M = M₁₀ M₁ = [[1,1],[10,11]]` 的两列，纸面与读数一致。**但顺序仍然钉不住**：那条程序走 `jvp`/`split` 的正向，逐轮映射是逐元素线性的，任何走法都给这两个数；要看见顺序，得让 `transpose` 也倒着走（`:1704`），而它要先有 `vjp` 过循环这条路（今天 `transpose_loop_escape.*` 全红）。所以 §4 的结论不变，只是"抬升"这一格从"没做"变成了"做了、有读数"。

## 附录：那条"本来就在"的规则

`stdlib/rulesets/jvp.cml` 末尾，两段原样：

```cml
func index_into::<T>(x: T[], i: int): T {
    return x[i]
}

export rule jvp_idx_arr::<T>(xs: T[], i: int) {
    index_into(xs, i) => (dxs: T[]) => index_into(dxs, i)
}
```
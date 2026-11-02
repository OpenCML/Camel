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
- 但**只有对还不够**：`x[1]` 是 `:op/idx_arr`，`stdlib/rulesets/jvp.cml` 里没有它的前向规则，`tangentOf` 拿不到 ⇒ `complete=false` ⇒ `skip=body`。所以这条程序的拒绝**不在** `pairTypeOf`。
- 元组态对照（`loop s = t for i in 0..2 do (s.1, s.0 + s.1)`）：只报 `PROBE pairTypeOf-not-scalar (float, float)`，**没有** `oper-no-tangent` —— 元组那边缺的只是"对"，数组那边缺"对"**和**一条前向规则。
- 另有一面与走法无关：`transpose_loop_escape.*` 六条今天红的根因是 `REINTERP jvp function=read_one skip=loop`（带 escape 的循环抬升不了），不是顺序。

## 3 先决条件：三条路

- **（甲，默认）先抬升：`pairTypeOf` 给聚合写对、聚合态的零按原值形状造（`modules/autodiff/pullback.cpp` 的 `addZeroTangent` 就在做这件事），再改 `:1704`。** 代价：抬升两处 + 一条新用例；好处是那条用例从此钉住顺序，`:1704` 的改动也才有读数可看。
- **（乙）先补 `:op/idx_arr` 的前向规则**（切线数组按同一索引读）。代价小，但它单独不解决问题：对还是没有，`skip=body` 照旧。
- **（丙）先不碰顺序**：`transpose` 那一半继续照抄 range，等聚合态切线空间整体落地再说。代价：`vjp` 过循环这条路继续既不肯算也不肯错，6 条 `transpose_loop_escape.*` 继续红。

## 4 结论

**顺序这一修的先决条件是"抬升先能配非标量态"，不是改 `:1704`。** 改完 `:1704` 今天也没有一条程序看得出差别：`split_loop_taped_rounds` 的 `loop d= 30.0` 在任何走法下都是 30.0（`jvp` 只给浮点标量配对 ⇒ 逐轮映射是乘标量 ⇒ 乘法交换）。同理 `e8c78f495` 那一笔（`split` 的 backward 半程倒过来走）**今天也没有可观测差异**，"没变坏"是唯一钉得住的东西；顺序要等本页第 1 节那条程序跑得起来再钉。
# 反传倒着走要能钉住，先要循环状态不是浮点标量

**已裁定（作者 2026-11-03，见 `origin/fsh-dev` `e83fabea2`）**：作者要求反向逐轮走**现在**落地，钉子就是本页第 1 节那条两轮矩阵程序的 `(1.0, 1.0)`。本页的默认建议因此被取代，正文保留为当时的判断与理由。
**已派（`lane-splitprimal`，`a7c41f15`）**：反向逐轮走在那一支上落地，钉子仍是本页第 1 节那条两轮矩阵程序的 `(1.0, 1.0)`。

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

- **这一条已经过时（2026-11-03 重测）**：`542ba0dda`（develop）的 `pairTypeOf` 只问一个问题 —— `tangentSpaceOf` 是不是 `Self` —— 注释里记着作者 2026-11-02 的裁定："聚合有没有对"是变换自己的政策，不是类型的答案，第二道门已撤（本支全树 grep 不到那道门的名字）。
- **更正（2026-11-03）**：这里原来写的是"`x[1]` 是 `:op/idx_arr`，`stdlib/rulesets/jvp.cml` 里没有它的前向规则"。**错**，那条规则本来就在（`jvp_idx_arr`，连同 `index_into`，见本页附录）。上面 `oper-no-tangent` 那一行是**症状**：探针问的是"操作数的切线在不在"。
- §2 原来的结论"缺的是聚合值在体里的副本与切线"也只对了一半。在 `542ba0dda` 上逐处量过，真正缺的是两处、都在 `Fill`：**(a)** 它的**切线块只对元组建**（`if (typeIsTuple && pairOf(...))`）—— 数组态体里的结果是一个 `Fill`，值有了、切线没有 ⇒ `skip=loop`；**(b)** `fillWithPayload` 无论什么类型都造 `::Tuple(n)` 当模板，把元组模板写进数组值 ⇒ 运行期 `Array flat index out of range: write of element 1 of an array that holds 1`。按类型造模板（`Array::create`）之后，两列数值就与纸面一致了。
- iterable 不配对那条**在 `542ba0dda` 已经存在**（`loopHasIterable(body.kind)` 与 `stateStart`），本笔没有重写它。
- 元组态对照（`loop s = t for i in 0..2 do (s.1, s.0 + s.1)`）：**在 `542ba0dda` 已经绿** —— `f(1,2) = (3, 5)`，两个种子读 `(1, 1)` 与 `(1, 2)`（纸面 `J = [[1,1],[1,2]]`）。上一版在旧基座上量到的 `skip=body` 是旧基座的事，不是这一笔的边界。
- 另有一面与走法无关：`transpose_loop_escape.*` 六条今天红的根因是 `REINTERP jvp function=read_one skip=loop`（带 escape 的循环抬升不了），不是顺序。

## 3 先决条件：三条路（（甲）已落地，见 §5）

- **（甲，已落地 2026-11-03）先抬升。** 落地的是 `Fill` 那两处（切线的分派、模板的形状），`pairTypeOf` 一个字没改 —— 它在 `542ba0dda` 上早就只问一个问题。结果见 §5：数组态循环抬升得起来，用例 `jvp_loop.array_state_is_lifted` 钉住整个 Jacobian（两列）。
- **（乙）先补 `:op/idx_arr` 的前向规则**。作废：规则早就在（§2 更正），这条从来不是缺口。
- **（丙）先不碰顺序**：`transpose` 那一半继续照抄 range，等聚合态切线空间整体落地再说。代价：`vjp` 过循环这条路继续既不肯算也不肯错，6 条 `transpose_loop_escape.*` 继续红。

## 4 结论

**顺序这一修的先决条件是"抬升先能配非标量态"，不是改 `:1704`。** 改完 `:1704` 今天也没有一条程序看得出差别：`split_loop_taped_rounds` 的 `loop d= 30.0` 在任何走法下都是 30.0（`jvp` 只给浮点标量配对 ⇒ 逐轮映射是乘标量 ⇒ 乘法交换）。同理 `e8c78f495` 那一笔（`split` 的 backward 半程倒过来走）**今天也没有可观测差异**，"没变坏"是唯一钉得住的东西；顺序要等本页第 1 节那条程序跑得起来再钉。

## 5 抬升那一步落地之后（2026-11-03）

第 1 节那条程序现在**算得出来**（`modules/transforms/jvp.cpp`，用例 `test/cases/compile/transforms/jvp_loop_array_state.cml`）：`f((1,2)) = (3, 32)`，两个种子的切线是 `(1, 10)` 与 `(1, 11)` —— 就是 `M = M₁₀ M₁ = [[1,1],[10,11]]` 的两列，纸面与读数一致。**但顺序仍然钉不住**：那条程序走 `jvp`/`split` 的正向，逐轮映射是逐元素线性的，任何走法都给这两个数；要看见顺序，得让 `transpose` 也倒着走（`:1704`），而它要先有 `vjp` 过循环这条路（今天 `transpose_loop_escape.*` 全红）。所以 §4 的结论不变，只是"抬升"这一格从"没做"变成了"做了、有读数"。

**这一格落了两次，第二次才是对的**：`b0b733ba5` 落在旧基座上，除了 `Fill` 还顺手装回了一道第二道门（"浮点标量，或元素可配对的数组"才写对）—— 那正是作者 2026-11-02 要求撤掉的那道门，等于把"一个问题"又写回成两个。合并车道在 `jvp.cpp` 上停下不替人挑，是对的；现在这一笔在 `542ba0dda` 上重做，那道门全树为 0，只留 `Fill` 的两处（切线分派、模板形状），并在同一个 base 上量过：数组两列仍逐位对、`transforms` 红名单不变、全量 8536 里没有新增红。

**同一天又补了结构体态（2026-11-03）**：它卡的地方跟数组态不同 —— 卡在 `case rt::GCAccsKind::StructKey` 的投影，`jvp.cpp` 的 `case Accs:` 原来只认 `TupleIndex`，按键的成员读直接 `break`，于是循环的状态输入（参数 `s.a` / `s.b` 两个投影）从来没被拷贝过，`skip=loop`。打印 GIR 才看得清的一件事是：**循环状态本身在 GIR 里已经是元组**（体里的 `{a:…, b:…}` 被降成 `(float, float)` 的 FILL），所以"结构体与元组的差别"只剩成员读的**寻址方式**（按键 vs 按号），补一格分派即可，`pairOf`/`pairTypeOf` 一个字没动。用例 `test/cases/compile/transforms/jvp_loop_struct_state.cml` 用两条程序钉住：恒等态（`loop x = s for k in [1.0, 10.0] do x`，Jacobian 是 I，两个种子原样回来）与含字面量的 `swap`（`M = [[1,1],[10,11]]`，`swap(1,2) = (3, 32)`，种子读 `(1, 10)` / `(1, 11)`）。

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

## 我方默认（按它往下做）

**这条默认已被裁定取代**：作者要求现在就换走法（`origin/fsh-dev` `e83fabea2`），钉子是本页第 1 节那条两轮矩阵程序答出 `(1.0, 1.0)`。下面留着，因为它记的是我们当时的判断与理由。

**不动 `:1704`，先把"带 escape 的循环抬不起来"这一格修掉** —— 今天 `transpose_loop_escape.*` 六条全红在 `REINTERP jvp function=read_one skip=loop`，走法在这一层之上还看不见；等那条程序（§1）能在 `transpose` 侧看见 `(1.0, 1.0)` 与 `(1.0, 10.0)` 的差别，再动 `:1704` 换走法，并用 `expect_output_contains` 钉住。

**理由**：§5 的读数是"抬升落地了、顺序仍钉不住"—— 现在改 `:1704` 没有任何程序看得出差别（§4：`split_loop_taped_rounds` 的 `loop d = 30.0` 在任何走法下都是 30.0），"没变坏"是唯一钉得住的东西。

**若作者说不（要求先改走法）**：回退一层 —— 只回退 `:1704` 那一笔（`transposeNodesInDraft` 的 `case rt::GCNodeKind::Loop:`），不碰抬升侧的 `Fill` 两处；两侧的读数各自独立，回退不互相牵连。

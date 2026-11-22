# `grad(f)` 的类型能不能写成库层类型函数？（删引擎 D2）

日期：2026-11-03　状态：**默认按 A 落地（D2 记为永久例外），并请你裁是否要走 B**

## 出处（只写有出处的）

- `stdlib/signatures/autodiff.cml#grad` 的页头原话：**"`grad` has no declaration in standard library
  types: its result type is computed here, so there is no one place where its shape is written down
  to be pinned against"**；`Gradient<F>` / `ValueAndGradient<F>` / `…AndAux<F>` 只是**名字**，类型由
  C++ `GradResolver` 算（`gradientSignature`，`modules/autodiff/pullback.h#gradientSignature`）。
- 对比：`Vjp<F>`（`stdlib/types.cml#Vjp`）与 `Jvp<F>`（`stdlib/types.cml#Jvp`）**是**库层类型函数，
  所以 `vjp`/`jvp` 能在 `stdlib/signatures/transforms.cml` 里正常声明。
- 作者 2026-11-01 对"从一长元组里取元素"的否决：见 `stdlib/vjp.cml#vjp` 的注释——"no type function
  that takes an element out of a one-long tuple"。
- 施工方默认：下面三条路与代价、D2 的定位，都是本轮审计（`c57097b74`）的施工方判断，无作者出处。

## 现象

删引擎第 7 条要求"对齐后删 `pullback.cpp` / `buildGradientGraph`"。审计（`c57097b74` D2）发现：
即使 D1（~42 个算子的库层 `*_t` 规则）全部写完，**库层 `grad` 也写不出来**，因为它的结果类型
`Tangent<With 的第一个>` 在类型层无法表达——`WithOf<F>` 给的是元组，取第一个元素正是被否决的那个
操作。没有声明，就没有库层 `grad`；没有库层 `grad`，C++ 的 `GradResolver` 必须留，`grad` 就仍是
一个 C++ 算子。

## 三条路与代价

1. **A：保留 `GradResolver`，D2 记为"永久例外"。** 代价：删引擎只删掉"建图那半边"（`pullback.cpp`
   的 walk 与 `buildGradientGraph`），`grad`/`with_grad` 仍由 C++ 计算签名；收益是**一套实现**
   （所有微分都走库层律），不是"零 C++"。
2. **B：给类型层加一个"取 with 元组首个元素"的算子**（例如 `FirstOf<T>`），让
   `Gradient<F> = <..WithOf<F>> (..ParamsOf<F>) => Tangent<FirstOf<WithOf<F>>>` 可写。代价：
   **直接撞 2026-11-01 那条否决**，要你先改裁决；之后 `grad`/`with_grad` 四张签名都能落进
   `stdlib/signatures/autodiff.cml`，C++ 只剩算子 kernel 和它的规则。
3. **C：库层 `grad` 只接受"声明过梯度类型"的函数**（限制面，不新机制）。代价：`grad` 的适用范围
   从"任意函数"缩到"作者手工声明过的函数"，与 `vjp` 的现状（声明 `Vjp<F>` 即可）不对称；对被
   拒绝的形状要按名拒，词不许与"没有 `with` 参数"合并。

## 默认建议：A 先守着，但写清前提

**默认 A**：删引擎的收益是"一套实现"，不是"零 C++"；为 D2 去撞一条已裁决的否决，代价大于收益。
所以本轮把它记为**永久例外**，并把第 7 条的判据从"删 `pullback.cpp`"改写成"删 `pullback.cpp` 的
建图，保留 `GradResolver` 与四个算子名"。

**但这不是"不用想了"**：若你要达成 D2，**必须先动 2026-11-01 对一长元组取元素的否决**（B），
否则 B 写不出来；C 不需要动那条否决，但要接受 `grad` 的适用范围收窄。
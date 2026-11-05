# 第二个 family 怎么被问：批处理的规则右边该拿什么实参

**状态：待裁（默认建议在最后，已按它继续）。车道 `lane-nestedlit`，2026-10-31 深夜。**

## 一句话

`rulesets/jvp.cml` 里每条规则的右边都是两层箭头 `x + w => (dx, dw) => add_tangent(x, w, dx, dw)`：**外那层拿原语的实参**，内那层拿"提升后的那一半"（切线）。`reinterp<vmap>` 的提升是"整个值变成一批"，于是批处理的图里**没有未批的值**可以喂给外那层——这不是缺一行代码，是规则右边拿什么实参的问题。

## 文档与代码怎么说

- 设计稿 `docs/design/program-transforms/README.cn.md` 4.1：`reinterp<F,H>` 把 `f` 的图里每个原语换成规则集 `H` 给它的含义，`H` 带一个类型函数 `Lift<T>`。`vmap` 的 `Lift<T>` 是 `RowsOf<T>`（`stdlib/types.cml`：值前面多一个未知长度维）。
- 机器：`modules/transforms/jvp.cpp#buildRuleTangent` 是唯一一份"把规则变成节点"的代码。它 `lookupRule` 拿到规则名，`genericRuleInstance` 实例化，然后**先**调外那层（实参是原语的取值节点），**再**用返回的函数值调内那层（实参是切线）。
- 规则右边点名的函数读得出来：`include/camel/parse/ast/rule_head.h` 的 `rhsCalleeOf(family, rule)`（`add_tangent` 就是这么被读到的）。

## 为什么 `jvp` 能过、`vmap` 过不去

`jvp` 的变换**复制了整条值链**（`JvpGraph` 的 `copyOf`），所以值节点在，外那层的实参是现成的；只有切线是规则算的。`vmap` 的值链本身**整体被换成了批**：`x` 是 `f32`、批里是 `f32[]`，图上根本没有一个 `f32` 节点可以给外那层。把批塞进外那层是类型错误（规则实例化在 `T = f32`，外那层的元组是 `(f32, f32)`）。

## 两条路

**（a）变换不调外那层，改调规则右边点名的函数。** 规则写

```camel
rule vmap_add::<T>(x: T, w: T) when is_numeric::<T>() {
    x + w => (bx: Lift<T>, bw: Lift<T>) => vmap_add(bx, bw)
}
```

变换按 `(family, head, arity)` 问索引（`lookupRule("rulesets.vmap", head, arity)`）、用 `rhsCalleeOf` 读出 `vmap_add`、直接以批为实参建一个普通调用节点。规则机制一行不改，`rhsCallee` 这条通道已经被 `jvp` 的文档写着，只是 `jvp` 没用到它（`jvp` 的右边调 `add_tangent` 是在**内**那层）。

**（b）规则机制允许右边是"无实参的函数值"**：即在没有未批值时跳过外那层。这要动 `genericRuleInstance` 的形状（拒绝一个"外那层不被调用"的实例），动的是机制本身。

## 默认建议（已按它往下做，作者可推翻）

**走 (a)。** 理由三条：不动规则机制；右边点名的就是库里的普通函数，能单独调、能对答案（"规则说的事"与"变换做的事"是同一个函数，不是两份算术）；`rhsCalleeOf` 已经存在并被 `std::rulesets` 印出来，读的人能一眼看到这条规则的右手指向谁。

这一笔只落了 (a) 的**前置**：`forwardBindingOf(uri)`（`include/camel/core/forward.h`）——绑定表一个算子只挂一个 family（`jvp` 占了五个键），第二个 family 要问同一个算子的**键**（head/arity）就必须能读出来，而不是从 `forwardRuleDescriptions()` 的字符串里解析。消费者本身（`modules/transforms/vmap.cpp` + `vmap_operator.cpp`，照 `jvp` 的脚手架）与 `rulesets/vmap.cml` 的右边改成点名函数，是下一笔，两处必须同一笔改完——只改一边，规则就"写对了但没被用上"。

## 判据（自这条起）

**规则集写对了不等于用上了——要有一格读数证明它真的被问过。** 索引今天有五条 `LOOKUP rulesets.vmap …` 的读数（`+/2 -> vmap_add` 等），那证明**写得进**；"被问过"要有第二格：变换为每个原语打印它问到的键与规则名，以及 `asked=N found=M missing=K`。

## 影响面

- 不动 `modules/transforms`/`autodiff`/`stdlib/rulesets` 的接口：这一笔只加了一个只读访问器（core 里，`forward.h`/`forward.cpp`），加完 `jvp` 的路径一字未变。
- 不动 `stdlib/array.cml` 的 `vmap`：它今天是逐行函数，读数是 `[1.5, 4.0, 6.5, 0.25]` 与 `[0.375, 6.0, -9.0]`（各两遍，batched 与逐行一致）；变换落地的那一笔要把同一个程序再跑一遍、逐位相同才算数。
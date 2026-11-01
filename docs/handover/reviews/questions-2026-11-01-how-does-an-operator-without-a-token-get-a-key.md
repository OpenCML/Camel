# 没有 token 的算子怎么按键：`tensor:multiply` 与 `:op/idx_arr` 的批处理规则

**状态：已按默认建议落地（两个原语的规则都进了 `rulesets.vmap`），调用节点仍缺变换侧一笔。车道 `lane-vmaparr`，2026-11-01。**

## 一句话

`reinterp<vmap>` 的键来自绑定表（`forwardBindingOf(uri)` → `(family, head, arity)` → `lookupRule`），而键的写法只有三种：`a <op> b`、`<op> a`、`f(a, b)`。数组乘法的算子是 `tensor:multiply`，取下标是 `:op/idx_arr`——**两个都没有自己的 token**（`*`/2 已经被标量 `:op/mul_d` 占用，一个键只装一条规则；`xs[i]` 是括号形式，三种都不是）。所以它们的规则按 **call** 形状落到 `mul_tensor/2` 与 `index_into/2`，构造函数就是算子的写法，算子在哪声明就在哪把键绑上。

## 落地的三件事

1. `stdlib/rulesets/vmap.cml`：`mul_tensor` + `vmap_mul_tensor` + `vmap_rule_mul_tensor`；`index_into` + `idx_batch` + `vmap_rule_idx`。右边点名的函数必须与左边同名函数**不同名**（`RuleRightUsesOuterVariable`：左边 bind 的名字在右边是模式变量）。
2. `modules/tensor/module.cpp`、`src/builtin/operators/derivative.cpp`：两条 `bindForwardRule`，family 都是 `rulesets.vmap`——因为批处理规则就是这个算子的唯一前向规则。
3. `src/parse/ast/rule_head.cpp` 两个读规则的缺陷（call 形状此前从没有规则用过，所以一直没暴露）：
   - `leftHeadOf` 把实参之间的逗号数成了操作数：`f(a, b)` 出来是 3 个。现按实参结点数。
   - `rightCalleeOf` 对 `f::<T>(a)` 读不出被调名（`TypeArguments` 挡在中间）。现接受它，且不再要求该结点无 token（`::`、`<`、`>` 就是它的 token）。

## 秩的问题：规则实例化在"原语的结果类型"

`modules/transforms/vmap.cpp:328` `elementType = node->dataType()`。对 `r * r` 结果就是行 `f32[3]`，`Lift<T>` 正好是一批；对 `r[0]` 结果是**元素** `f32`，而行是 `f32[3]`、批是 `f32[?, 3]`——一个秩之差。所以下标那条规则把行写成 `T[]`（元素的数组），把批写成 `Lift<T[]>`（`f32[?, ?]`）。这不改变换，但它是"下标是唯一降秩的原语"这件事的代价，写在这里免得下一个人重推。

- 交出什么：`mutiply` 那条交出 `Lift<T>`（整批一次相乘，同一个算子，逐位相同）；下标那条交出 `Lift<T>`（每行一个元素）。
- 成本：乘法是原语本身作用在批上，没有逐行的中间量；下标是 `len(xs)` 次跨行取一个元素，不复制任何一行。

## 绑定表里现在有两个 family

一个算子只挂一个 family，这是既有设计；这一笔之后 `forward=true` 的读数里有 10 条 `rulesets.jvp` 和 2 条 `rulesets.vmap`。**这是必然**：变换按绑定读键，而这两个数组原语的唯一前向规则就是批处理规则，写进 `rulesets.jvp` 是错的（那里是切线）。代价：`jvp` 读到这两条会拒在"规则的右边不是切线的函数"，而不再是"这个算子根本没挂键"——两个都是拒，后者更准但前者才是事实。`test/cases/compile/rulesets/rulesets_lookup.py` 把这件事按名字钉住了（哪十条属于 jvp，哪两条属于 vmap）。

## 还是缺的：调用节点

实测（`vmap(g)`，`g(x) = f(x) + 1.0`）：`skip=body construct=a call asked=0 found=0`。`asked=0` 是决定性的——变换在**问之前**就拒，所以**规则集无论怎么写都够不着**，写一条按函数名的规则只会是"写对了但没被用上"。修在 `modules/transforms/vmap.cpp`（本车道不碰），两条路：

- **（甲）走进去批**：把被调函数的图内联进批处理图再走一遍。`#inline` 之类的既有台阶能前置一部分，但"函数值参数"内联完还是调用。
- **（乙）按函数名问规则**：现在 call 形状的键已经能装规则了（这一笔刚修好两个读规则的缺陷），所以"被调名 + arity"是现成的键；变换要做的只是在 call 结点上问一次，问不到再按名拒。

建议（乙）：不动规则机制，一条规则能同时说"这个函数批处理时调谁"，而且 `asked` 读数会立刻从 0 变成 1，缺口可见。

## 判据

规则写对了不等于用上了：这两条都以 `asked/found/missing` 读数 + 一个手算得出的程序 + 与逐行 `array.vmap` 逐位相同为准，四后端一致才算数。
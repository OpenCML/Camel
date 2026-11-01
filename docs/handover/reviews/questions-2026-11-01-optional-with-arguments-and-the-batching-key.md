# 一个可选的角度参数，让同一个批规则键有了两种元数

## 0. 这条要什么

`softmax` / `log_softmax` 的轴可以省。省的写法和写的写法在批规则里**共用一个键 `softmax/1`**，但调用节点的端口数不同：省掉时 1 个，写了时 2 个。规则只能挑一种，另一种在运行时变成内部错。要作者裁一件事：**省掉的可选参数，在节点上是一个端口还是不写**。

## 1. 事实（每条都有读数或原文）

**声明**：`modules/tensor/ops/catalog.cpp:50-58`

```cpp
// softmax(t, axis = -1), log_softmax(t, axis = -1): the axis may be left out.
for (const char *name : {"softmax", "log_softmax"}) {
    table.declareDefinition(
        std::string("tensor:") + name,
        Definition{.module = library, .function = name, .params = {operand(0), operand(1, int64_t{-1})}});
}
```

`OpDef` 同一条轴也是可选的（`reduction_ops.cpp:601-620`）：`{"axis", ParamKind::Int, true}` + `withAfterNorm = true` + `ordinaryParams = 1`。

**两种写法两种端口数（实测）**：同一份规则，`softmax<0>(x)` 命中并算对：

    REINTERP vmap function=soft_axis ... asked=1 found=1 missing=0 rules=vmap_rule_softmax elements=float

`softmax(x)` 同样命中，却在运行时炸：

    Runtime indirect call norm-arity mismatch: caller graph 'vmap_of_soft_no_axis' node 2 has 1 args,
    target graph 'rulesets.vmap::vmap_softmax<float>' has 2 norm ports.

**键的口径**：键数的是**括号里的操作数**（`reshape<dims>(x)` 的键是 `reshape/1`），角度括号里的参数不进键。所以两种写法的键都是 `softmax/1`，而索引里一个键只放一条规则（`lookupRule(family, head, arity)`）。这就是"一个键、两种形状"的来源。

**今天为什么干净**：没有键时诊断是 `the operator has no batching rule key bound at all`——一句实话。加了规则以后，常见的省略写法会变成内部错，比没有规则更差，所以规则**没有落地**（这一笔只留测量，不留代码）。

## 2. 为什么"提升时把默认值填上"不是一处改动

三条读出来的理由：

1. **声明的默认值今天是只写的。** `DefinitionParam::fallback`（`include/camel/core/operator_semantics.h:353-366`）的注释写着"the value of an optional operand the call leaves out"，但 `include/`、`modules/` 里**没有任何地方读它**（grep `fallback` 只命中它的声明和别的模块里同名但无关的局部量）。真正生效的默认在各内核自己的 `optInt(norm, 1, -1)`（`modules/nn/tensor_ops.cpp:44-45`）。
2. **端口角色由调用的写法决定，不由声明决定。** 写在括号里的是普通操作数，写在角度括号里的是 with 参数；括号里多写的还会被拒（`modules/tensor/ops/registry.cpp:125`：`withAfterNorm && norm.size() > ordinaryParams` → `nullopt`）。所以节点是**源码的镜像**，默认值是"展开定义图时"的事实（`Definition` 的文档就这么说：nothing expands a definition unless a pass asks）。变换看到的节点从来没有那个端口。
3. **变换手里没有默认值可填。** `vmap.cpp` 只通过 `camel::core::vmapBindingOf(uri)` 拿到键；它读不到 `Definition`，也没有按 URI 查声明默认的接口。要在提升时填，得先加一个读者，再在节点上凭空造一个常量端口——那是**语义变更**：`withInputsOf` 对所有消费者（`std::jgir` 的端口、`split`/`jvp` 两个变换、`inspect --ops`）都会多出一个源码没写的端口，内核里 `ctx.has(1)` 从此恒真。

可选的两条路，都不该由我一个人定：

- **A（前端口物化）**：调用省略可选 with 参数时，前面就按声明把默认值写成常量节点。于是一个算子只有一个端口数，键不再有两种形状。代价：节点的端口表不再是源码的镜像，上面点到的消费者都要跟着改，`DefinitionParam::fallback` 终于有人读。
- **B（规则语言长出可选端口）**：规则的右面允许一个可选端口（省略时用默认值）。代价：批规则的语言表面变大，`std::rulesets` 的表和索引要能表达"一个键、端口可缺"。

我倾向 A 的一个窄版本：**只对 with 位置**物化（普通操作数的省略已经有了不同的键，`linear/2` 与 `linear/3`、`softmax_grad/2` 与 `/3` 各自成键，从来不是问题），并且只在算子声明了默认值时做。这样键的唯一性恢复，而源码里"没写"这件事仍然可以从常量节点的来源看出来。

## 3. 盘点

树里 `operand(i, default)` 的声明**只有 1 处**（`catalog.cpp:58`），覆盖 **2 个算子**（`softmax`、`log_softmax`），其中**已有批规则键的：0 个**。

可选参数另有 6 处，都是**可选操作数**（不是 with 参数），各自改变括号里的个数，因此各有各的键，不存在一键两形：`linear` 的 `bias`（`linalg_ops.cpp:163`）、`softmax_grad` 的 `axis`（`reduction_ops.cpp:625`）、`layer_norm` 的 `eps`（`reduction_ops.cpp:637`）、`layer_norm_grad` 的 `eps`（`gradient_ops.cpp:457`）、`expand_axis` 的 `keepdims`（`gradient_ops.cpp:438`，内部算子）、`reduceAxisDef` 的 `keepdims`（`reduction_ops.cpp:131`，`sum_axis` 一族——顺带量到：`sum_axis(x, 0)` 在签名层面就被拒，`<tensor:sum_axis>: (xs: T[..], axis: int, keepdims: bool)`，所以这条路径今天根本走不到)。

## 4. 需要作者回答的

1. 选 A 还是 B？A 的窄版本（只物化 with 位置、只对声明了默认值的算子）是否可以？
2. 若选 A：物化出的常量节点，`origin` 记在调用处还是无来源？`inspect --ops` 要不要把它画出来？
3. 若选 B：可选端口的规则在 `std::rulesets` 的表里怎么印（一个键两行，还是一行带"可缺"）？

在裁下来之前，`softmax` / `log_softmax` 保持**没有键**：省略写法的诊断是真话，比一个只在一种写法上成立的规则好。
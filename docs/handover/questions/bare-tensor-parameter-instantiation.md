# 裸 `Tensor` 形参要不要按调用点实例化（导出的图没有秩可写）

提交给作者裁决，2026-10-24，由协调者指示，随本轮 `sum_to` 运行时轴实现一起提交。

## 现象：两例 flip 回归，develop 在同一条路径上是绿的

`feat.modules.onnx.export_grad_matmul_vector`（`export_grad_matmul_vector` 与
`pass_export_grad_matmul_vector`）在本分支上红，在 `origin/develop` 上绿——**实测**：在
`~/camel-work/Camel`（develop，`147e2d2d4`）上同一条用例的两条路径都成功：

```
ONNX_OUT=... camel test/cases/modules/onnx/export_grad_matmul_vector.cml std::fvm      # export 成功
camel test/cases/modules/onnx/export_grad_matmul_vector.cml std::macro std::opt \
      "onnx::export out=..."                                                          # wrote ...
```

所以这两例是 flip 带来的回归，不是 develop 的既有缺陷；验收必须包含「develop 的行为没有丢」。

## 原因：图里根本没有秩，而不是某一处取错了秩

`loss`/`step` 的形参写的是裸 `Tensor`，于是整个前向——连同反传——的节点类型都是
`ArrayType::numericUnknown()`（开放秩 + Void 元素）。在 `gradientTypeFor`
（`modules/tensor/ops/vjp.cpp`）里加临时打印（已回退，未提交）后，VJP 规则**拿到的每一个前驱**
都是：

```
[h388vjp] primal=array rank=0 open elem=5 states=0 inputs=2     ← 运行时导出路径 30 次
[h388vjp] primal=array rank=0 open elem=5 states=0 inputs=3     ← pass 导出路径 30 次
```

`statesTensorElementType` 全为假，所以反向节点只能取到元素类型（`f32`）、取不到秩，打印出来是
`f32[..]`。同一段管线（`std::macro std::opt std::gir`）在 develop 上把这些节点标成
`value_type="Tensor<float32>"`，在本分支上标成 `f32[..]`：**秩不是被弄丢了，是从没被写下来过**。

## 实测：导出侧还有 8 处要求「秩已写出」

`modules/onnx/lowering_grad.cpp` 里要求静态秩的位置共 8 处（`sum_to`、`expand_axis`、
两个 `matmul_grad_*`、`conv2d_kernel_grad`、`conv2d_grad` 等）。本轮把 `sum_to` 改成运行时求轴
（`Shape`/`Range`/`NonZero`/`Concat`，opset ≥ 13，`noop_with_empty_axes=1`）：这是「程序没写下的秩
就地问模型」，与同文件 `shapeOperand` 对未知维度已有的做法一致。改完之后这条用例的下一个拒绝是

```
onnx.export_model: tensor:matmul_grad_rhs: the rank of an operand is not known statically
```

**而这一处没有运行时版本可以顶替**：这两个用例自己的校验脚本数的是
`count('Squeeze') >= 2 && count('Unsqueeze') >= 4`——即 matmul 对向量操作数的**静态**扩轴/收轴。
开放秩下产生的图不可能有这一对，所以即使把剩下 7 处都改成运行时形式，这两例仍然过不了。

## 需要作者裁决

**裸 `Tensor` 形参是否必须按调用点实例化，让形参拿到实参的秩？**

- 若「必须」：本轮的运行时 `sum_to` 就从「修法」降为「兼容路径」，`docs/handover/reports/tensor-flip-integration.md`
  里那一行要照此改写；出口图中 `f32[..]` 会变少而不是被容忍。
- 若「不必」（`Tensor` 一律就是开放秩）：那么这两例的校验脚本（数 `Squeeze`/`Unsqueeze`）期待的
  静态秩本身就是这次翻转不再保证的东西，该由作者决定是改用例还是补实例化。

## 若裁决为「必须」，代价在哪（给作者的回答材料）

- 落点在编译器的泛型调用/实例化（`src/compile/**`），不在本 lane，也不在 `modules/onnx`：
  导出侧只能看到已经编译好的图。需要确认的是「`with <m: P>` 绑定与函数体实例化」这条路径上，
  形参类型是否按实参类型替换（develop 的行为说明它曾经是）。
- 成本：每个不同的实参类型组合实例化一份函数体（体积与编译时间），以及在过渡期内与
  `ArrayType::numericUnknown()` 的关系（`docs/handover/questions/tensor-alias-transition.md`
  已记：过渡类型会被删，新代码不应依赖它）。
- 收益（可量）：出口图里 `f32[..]` 的站点减少、`modules/onnx` 里 8 处「要秩」的降级需求减少、
  以及 `export_*` 这一族不必再各自长运行时兜底。

现状：本 lane 只落了运行时 `sum_to`，两例保持红，等这一条裁决；不自行选择语义。
## 附记（协调者 2026-10-24：同一根因的第二处症状）

这条问题不是导出器的小节：`var Tensor` 一族 **38 个用例**（41 条 entry）从另一条路径撞上同一个根因。`test/cases/modules/autodiff/var_tensor_read.cml` 报

`passes a var f32[?, ?] as ordinary argument 0 of 'step', which takes a var Tensor`

数组的 `assignableFrom` 接受这一对（`numericUnknown()` 接受任何数值元素），但**实参是一个 cell**，而 cell 的规则要求内容类型**相等**（"cell 会被每一处引用写穿"，`src/core/type/composite/tuple.cpp`）——这条不变式是对的，不该放宽。develop 上 `reshape` 与形参回答的是同一个 `TensorType`，所以 38 个用例全绿；翻转把"一个形状未知的张量"拼成了两种写法（`f32[?, ?]` 与 `Tensor`）。

因此 (b) 的答案同时决定两族：若答案是"裸 `Tensor` 形参按调用点实例化"，调用侧的同一条规则（调用点的 cell 取声明的形参类型）就能让两族一起落地；若答案相反，这里需要一条只做**加宽**、不放宽 cell 不变式的兼容路径。

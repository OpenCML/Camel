# 裸 `Tensor` 形参实例化：实测不是这两例的根因（补测，2026-10-24，h410 lane）

承 `docs/handover/questions/bare-tensor-parameter-instantiation.md`（`8d9095f5b` 已 cherry-pick 到
develop）与作者在 `docs/handover/reviews/questions-2026-10-23.md`
（`## bare-tensor-parameter-instantiation`）的裁定「**必须**按调用点实例化」。本文件补的是那条
裁定所依据的**前提**的实测结果，以及两例在**不实例化**的情况下已经转绿的证据。裁定本身要不要改，
仍然请作者定。

## 一、两例已经绿了，靠的不是实例化

`handover/h410-instantiate`（`086181606` + `f20039abe`）整层实测：5973 / **290** red / 11 skip，
**2 red 转绿、0 新增红、0 signal**（集合差 over `notes/h410-base-reds.txt`、
`notes/h410-p2-layer-reds.txt`），转绿的正是 `export_grad_matmul_vector` 与
`pass_export_grad_matmul_vector`。导出的模型与 develop 逐项一致：55 个节点、`Squeeze` 3 /
`Unsqueeze` 8 / `MatMul` 8，三个输入 `[3,2] [3] [2]`、三个输出 `[1] [3,2] [3]`，
**没有任何未述形状**（`value_info` 0 条，输入输出都带 shape）。所以验收里「导出回到 develop 的行为」、
「导出结果里不出现 `f32[..]` 之类未述形状」、「38 个 `var Tensor` 保持绿」（实测 46 个
`var_tensor*`/`var_reference*` 全绿）三条都已满足。

## 二、根因是把秩 0 张量的 facts 弄丢了，`CAMEL_ONNX_TRACE=1` 一行就能看出

`std::macro std::opt` 下两棵树逐行相同，直到秩 0 那一段：

```
develop : tensor:matmul(sym tensor[3], sym tensor[3])  ->  sym tensor[]     ← 秩 0 的 facts 还在
flip    : tensor:matmul(sym tensor[3], sym tensor[3])  ->  sym tensor?     ← 全丢了
  之后 (develop / flip)：multiply -> sum_to -> matmul_grad_rhs
```

原因：秩 0 就是元素类型本身（秩 0 裁定），所以秩 0 张量的**静态类型**是 `f32`，
`isTensorType(f32)` 为假。两个「再读一遍已编译好的图」的地方于是问了类型回答不了的问题：
`anyTensor`（`modules/tensor/ops/support.cpp`）对「实参全是数值标量的 `tensor:` 节点」答「不是张量」，
导出的 facts（`modules/onnx/exporter.cpp`）只在 `isTensorType` 收下时取推断结果，而秩 0 的答案就是
元素类型。核函数一侧本来就是按秩 0 张量处理的（`tensorArg` 把那个标量槽装箱成「无维度的数组」），
所以这两处改成同一条规则即可——**落点在算子/推理层，`modules/onnx` 只有一行同规则的读法，
不是给 ONNX 打补丁**。

## 三、裁定所依据的前提：「develop 是 `Tensor<float32>`，所以秩在 develop 上是有的」——实测不成立

develop（`~/camel-work/Camel`，`147e2d2d4`，同一管线点 `std::macro std::opt std::gir`）里反传节点是

```
N60 [label="tensor:sum_to",          value_type="Tensor<float32>"]
N64 [label="tensor:matmul_grad_lhs", value_type="Tensor<float32>"]
N48 [label="tensor:reshape",         value_type="Tensor<float32, [1]>"]
```

**`Tensor<float32>` 没有写秩**，它就是「元素类型有了、秩开放」；同一个类型在本分支打印成 `f32[..]`，
就是这两个拼法的差别（写出来的秩在 develop 上打成 `Tensor<float32, [1]>`，在本分支上打成
`f32[1]`）。也就是说：

- develop 的反传图**同样是开放秩**，develop 没有按调用点实例化裸 `Tensor` 形参这件事；
- develop 的导出能过，是因为它的**秩 0 张量仍然是 `TensorType`**（`Tensor<float32, []>`），
  `isTensorType` 收下、`tensorFactsOf` 给出 `{f32, {}}`，秩 0 的 facts 没丢；
- 本分支把 `TensorType` 删掉之后，秩 0 张量按裁定就是元素类型，facts 只有在上述两处按秩 0 的
  规则读回来才不会丢。

所以「develop 的行为没有丢」这句验收，靠的是**留住秩 0 张量的 facts**，不是实例化。

## 四、实例化剩下的收益，以及它为什么不是本 lane 该做的

- 这两例的验收**不再需要**实例化（第一节）。裁定里给实例化写的理由——「翻转后整段前向反传都是
  `numericUnknown()`，秩丢了，所以要实例化」——实测是把 `Tensor<float32>` 读成了「秩已知」；
  秩从来没被写下来，develop 也没写过。
- 只实例化 `Tensor` 形参也**不足以**去掉出口图里的 `f32[..]`：`loss` 的 `with <m: P>` 形参走的是
  **具名类型 `P`**（`type P = { w: Tensor, v: Tensor }`），`m.w` 是 `P` 的字段，跟 `step` 自己的
  形参无关；要它变具体，得连「具名类型的字段」也按实参替换，那是另一条更大的语义改动。
  实测：本分支该用例 219 个节点的 GIR 里 `f32[..]` 36 处（base 也是 36 处），develop 0 处
  ——这 36 处是反传自己的定型（`tensorOf(dtype, nullopt)`），不是形参带来的。
- 因此本 lane **没有落**实例化：它要改语言语义（每个不同实参类型组合实例化一份函数体、实例键与
  实例名要扩、`isGenericHere` 要让「签名里有裸 `Tensor` 形参」也成立，从而把 `shape`/`numel`/
  `to_bytes`/`show` 这类库函数全变成模板），而它现在**没有一条测量支撑**；按「一条车道一个目的」
  与「不自行选择语义」，这属于请作者重新裁定的事。

## 五、请作者定的两件事

1. 这条裁定要不要按上面的实测改写？**若实例化的目的是这两例**，实测已达成，可以不落。
   **若目的是给 tiles 归约那条线把改写面收窄**（`docs/handover/questions/tensor-reduction-element-type-slice.md`：
   「先落实例化、再落归约」），那它是一条
   **独立的、面向未来的**语义改动，值得单独立项，本文件把它的真实代价与边界写在这里：
   范围是 `src/compile/gir/builder_generics.cpp` / `builder.h`（实例键、实例名、`isGenericHere`
   的谓词）与 `src/compile/gir/builder_operators.cpp` 的调用点，**不含** `with <m: 具名类型>`
   的字段替换（那要另裁）。
2. 秩 0 张量在「再读一遍图」的场景里按什么读：本 lane 落的是**「离源层级（pass / 导出 / 草案重推）
   时，`tensor:` 节点的数值标量实参就是它核函数要的秩 0 张量」**（`anyTensor`），源层级不动，
   所以 `tensor:multiply(1.0, 2.0)` 这类源码调用不会多出一个张量重载。若作者认为这条读法该在源
   层级也成立（或反之，秩 0 张量在再读图时应当另有一个形式承载），这条规则要改，改的位置就在
   `modules/tensor/ops/support.cpp` 与 `modules/onnx/exporter.cpp` 的各一行。

现状：本 lane 两例绿、整层 290、无新红无 signal，`f32[..]` 的站点数与 base 逐处相同，实例化未落。
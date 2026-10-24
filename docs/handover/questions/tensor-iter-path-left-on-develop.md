# `tensor:iter` 那条路径：develop 上只剩未别名化的 `Tensor` 类型

（对 `docs/handover/reviews/questions-2026-10-24-evening.md` §recursion-specialization-fixed-point-followups
第 5 条的回报；上一轮裁定是同一份文件的 §loop-domain-through-the-tensor-iterator：`for` 对数组由核心负责，
`core.loopDomain` 的 `extent` 就是 `shape[0]`，tiles 里的看穿是过渡，核心事实补上后去掉。）

## 结论

1. 生成 `tensor:iter` 的路径只有一条：`for` 的序列**静态类型不是核心数组**时，循环构建器去问该类型的
   `__iter__`，张量模块的 `iter` 作答（`src/compile/gir/builder_control.cpp:2661-2681` 与
   `modules/tensor/ops/utility_ops.cpp:220-228`，节点落点 `currGraph_->addOperNode(*elements, 0)`）。
   develop 上只有模块的 `Tensor<...>`（TensorType）走它；`int[]`、`f32[2]` 这类数组不走。
2. 翻转线上它曾经连**数值数组**都走：`613eb92c0` 把条件写成 `type->code() != TypeCode::Array ||
   isNumericArrayType(type)`，于是 `resident_loop.cml` 的 `for k in [1, 2]`（`int[2]`）也生成 `tensor:iter`，
   tiles 的 `#gpu` 作用域检查拒绝它（`modules/tiles/src/region_outline.cpp:159-169`，
   文案在 `src/core/error/diagnostics/messages/runtime.cpp:283`），而 tiles 为了拿循环域还加了一次看穿
   （`modules/tiles/src/compiler.cpp` 的 `loopSequence`，`64a62c527`）。裁定说的"你们在 tiles 里的看穿"
   就是它；看穿依赖的那个节点由构建器生成，不在 tiles 里。
3. 整批集成树的当前 tip 已经修掉：`902960d5f`（= 工作树 `handover/h453-core-loop-domain`）与它在翻转线上的
   一次落地 `5b99cc569` 让核心对任何数组取第一维（`ArrayType::firstDimensionElement`），构建器只对非数组
   问 `__iter__`，tiles 的看穿删除。本 lane 在 develop 上没有可改的生成点（见"交回的问题"）。

## 逐字证据（主机 `88zjwei`，各树自己的 `out/latest`，`--devices cpu` + `tiles::compile std::jgir`）

生成点原文（develop，序列写 `Tensor<f32, [2]>`，`/tmp/probe_shaped.cml`）：

```
error[RuntimeDiag::GpuScopeUnsupported]: `#gpu` scope 'probe_gpu': operator 'tensor:iter' cannot run on the device (no device implementation).
```

同一探针在集成树 tip 上：`exit 0`，jgir 文档里 `tensor:iter` 0 处。

用例本身（序列是数组字面量 `[1, 2]`）：两侧 jgir 文档里 `tensor:iter` 都是 **0 处**，
`node test/cases/modules/tiles/resident_loop.mjs` 两侧都打印 `Resident loop: PASS`。

## 两侧读数

| 读数 | develop `fe2c7f6bd9` | 集成树 `6cb57ebc5` |
|---|---|---|
| `resident_loop_declares_its_blocks` | PASS（289 ms） | PASS |
| `gpu.tiles` 计划 | `[SUMMARY] total: 101, pass: 101, fail: 0` | 未跑（不是本 lane 的工作树） |
| `for row in <Tensor<f32, [2]>>` + `#gpu` | `tensor:iter` 1 处，`GpuScopeUnsupported` | 0 处，exit 0 |
| autodiff 的 `loop_grad_refuse_tensor_iterable` | exit 1，逐字 `no derivative rule for operator 'tensor:iter' of type 'Tensor<float32, [3]>[]'`（计划的负例为绿） | exit 0，`d=[-6755404000000000.0, -6755133000000000.0]`（负例变红，梯度是垃圾值） |

## 交回的问题

develop 上那条路径**必须**存在到 P4 别名落地为止：`Tensor<f32, [2]>` 今天还是模块的 `TensorType`（不是数组），
`for` 只能由模块的 `__iter__` 实现，图上就写着 `tensor:iter`；而且 `f32[2]` 与 `Tensor<f32, [2]>` 今天不可互相
赋值（`reshape` 的返回类型是 `Tensor<float32, [2]>`，传给 `f32[2]` 形参要显式转换）。所以本 lane 没有硬改。

1. 是否确认 develop 上保持现状——模块的 `iter` 随 P4 的删除步一起走，这条路径记为过渡？若是，本 lane 无改动。
2. 或者现在就要让核心接管 `Tensor<...>` 序列的 `for`？那需要先落 P4 别名（h438），否则
   `tt::as_ptr<ArrayType>` 不成立、`isNumericArrayType` 也不存在。
3. 集成树 tip 上还剩两处，都不再生成节点：张量模块的 `iter` 注册已无人可达（构建器不再对数组问 `__iter__`，
   它接受的类型现在都是数组），等 P4 删；`test/plans/feat/modules/autodiff.plan.toml:1670-1683` 的
   `loop_grad_refuse_tensor_iterable` 仍断言 `tensor:iter` 的拒绝，实测已不拒绝且给出垃圾梯度——归 autodiff
   车道（`902960d5f` 的提交信息已把它交回该车道）。
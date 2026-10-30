# `argmax_axis` 进表之前：一页纸

这是 A 组最后一条（`tensor:argmax_axis`）的前置笔记，**只写判断，不写代码**。理由：它要动的是
类型系统与归约契约，不是设备程序；先有一页纸，才有一笔代码。这一条今天**不在**
`modules/tiles/src/graph_regions.cpp#operations` 表里，缺口账本
（`docs/handover/reports/gpu-g4-g5-gap-list.md`）因此停在 26（transformer_step 13 + C2 13），A 组剩它 1 条。

## 一、`i64` 结果在 device 侧需要什么

三样东西，缺一样都走不通：

1. **类型表**。`modules/tiles/include/tiles/dtype.h#DType` 今天只有 `F32`、`F64`，全仓按它分支的地方
   13 个文件（`git grep -c "DType::F64" modules/tiles`）。加 `I64` 意味着每个 `switch` 多一处宽度：
   `modules/tensor/dtype.cpp#elementSize` 那条按元素类型取 `sizeof` 的路由、CUDA 侧的标量类型、
   标量表达式（`modules/tiles/include/tiles/internal/kernel_ir.h#ScalarValue`）的类型集合。
   注意 **`Store` 也只能存今天这几种**：`kernel_ir.h#Store{Access, ScalarExpr}`，i64 的位置要存得进去，
   先得让 `ScalarExpr` 能是 i64。
2. **缓冲区**。`modules/tiles/include/tiles/device.h#DeviceValue{dtype, address, …}` 与
   `uploadValues`/`downloadValue` 都按 `dtype` 算字节数；`DeviceValue` 的 `dtype` 是唯一的地方告诉
   后端"这 8 字节是一个下标"。另有一个反直觉的事实：**类型层今天已经放它进来了**——
   `modules/tiles/src/device.cpp#DeviceType::get` 只要求 `tensor::isTensorType(host)`，而
   `modules/tensor/type.cpp#isTensorType` 认任何数值数组（`int64[..]` 也算张量），所以
   `Device<int64[2]>` 今天可以**被写出来**，只是没人知道它的元素类型是什么。真正的闸门是
   `device.cpp#deviceScalarDType`（只认 Float32/Float64，标量用）和下面的回读。
3. **回读**。`modules/tiles/src/tree_transfer.cpp#materialize` 是设备值变成 Camel 值的唯一出口：
   非标量叶子今天只认 `DType::F32`（否则 `download tensor type mismatch`），且
   `tensor::createTensor` 写死 `TypeCode::Float32`。要按叶子的 dtype 建张量
   （`tensor::createTensor(TypeCode::Int64, …)`）。

还有三处按 f32 判的闸门，会在这条算子走到它们之前就拒：

- `modules/tiles/src/graph_regions.cpp#shape`：结果的静态形状只从 f32 张量读得出（f64 读成标量，
  其余 `nullopt`），所以 i64 结果今天在 `eligibility` 里是 **`unresolved_type`**，连 lowering 都到不了。
- `modules/tiles/src/schedule.cpp#validate`：`require(n.dtype == DType::F32)` 与操作数的同一条要求。
- `modules/tiles/src/compiler.cpp:1094`：`operator '…' needs a statically known f32 tensor result`——
  这是把 i64 结果落在运行图里的那道门的原话。

## 二、带下标的归约在契约里怎么表达

**契约这边已经有了，不用新写**：`modules/tensor/ops/indexing.cpp#positionLaw` 给 `argmax_axis` 挂了
三条 law（f32 / i64 / bool 输入），字段是

- `kind = LawKind::ArgMax`（`argmin_axis` 是 `ArgMin`）
- `accumulator = 输入的元素类型`，`result = TypeCode::Int64`
- `init = LawInit::FirstContribution`（第一个贡献者起算，不是单位元）
- `empty = LawEmpty::Fault`（空轴是错，不是 0）
- `exactAssociative`/`exactCommutative` 都为真，注释写明前提是"**carrying the positions**"

也就是说语义层表达的是"域上第一个极值的**位置**"，而不是"先算极值再回头找位置"。设备侧要表达的是
同一件事的计算形式：循环走归约域时，位置**就是**归约坐标本身——`_axis` 只有一个归约维
（`reduceAxisIndexing`），`reductionAccess` 的 `r` 已经是这一维的线性坐标。所以"带下标的归约"在
设备契约里不需要新的语义概念，需要的是一个**两槽累加器**：值（用同一个 IEEE maximum 序）与位置
（更早者胜），最后把位置写进 i64 结果。

## 三、`reductionAccess` 要长出哪一种入口

**顺序版本不需要新入口。** `modules/tiles/include/tiles/internal/kernel_ir.h#reductionAccess` 返回
`ReductionAccess{count, address}` 已经够：位置由 `r` 给出，值由 `address` 读。今天挡路的是归约内核
里对 law 的三选一——`modules/tiles/src/reduction_kernel.cpp` 的
`require(… law.kind == Max/Add …, "unsupported numerical law")`——`ArgMax`/`ArgMin` 会落在这条
`require` 之外。要加的就是那一条分支加一个内核体（`modules/tiles/src/serial_kernel.cpp` 的归约分支
里已经分出 `k_acc`，把它换成 `k_best` + `k_pos` 两个绑定即可）。

**并行/分块版本需要新入口。** 两段各自算出 (值, 位置) 之后要合并成一对，这不是
`reductionAccess` 能表达的；形态照着已有的"成员归约器"来——`serial_kernel.cpp` 的
`launch.implementation == "ordered_reduce"` 走的就是"归约器在一个成员节点上"的路
（`lower.reduction` / `reducer`），把合并规则改成"比较值、值相同取更早位置"即可，入口与它同级。
建议先做顺序版本 + 一次读数，再谈并行：今天 `argmax_axis` 在主机上就是串行 `kernels/reduce.cpp`
的语义，位置逐位可对。

## 四、"不放进表"这句话如何继续成立

表里的名字是"**有 device 程序**"的宣称（`operations()` 是 `deviceCapable` 的唯一定义处，
`modules/tiles/src/region_outline.cpp` 用它决定接受还是拒绝一个 `#gpu` scope）。所以顺序是死的：

1. `DType::I64` 走通"上传 → 内核 → 回读"（上面第一项的三处闸门一起改）；
2. `ArgMax` law 的内核分支落地（顺序版）；
3. `eligibility` 的归约分支已经能接（`_axis` 走 `DimRole::Reduction` 求 `attributes`，
   再 `reductionAccess`），但 `graph_regions.cpp#shape` 要能读 i64 结果，否则永远停在 `unresolved_type`；
4. **一条 device-free 读数**：`--devices cpu` 下 `#gpu` scope 里的 `argmax_axis` 位置与主机逐位相同
   （照 `test/cases/modules/tiles/gpu_scope_views.cml` 的写法钉值）；
5. 这之后才把 `tensor:argmax_axis` 写进 `operations()`，C2 的读数才允许从 21 = 8 + 13 变成 21 = 9 + 12。

在第 4 步有读数之前把名字填进表，只会得到两种结果：`eligibility` 在 `unresolved_type` 处拒（数字好看、
能力没变），或者更糟——不拒，于是把 4 字节的 f32 结果按 8 字节下标读出去。**这就是这一条始终不进表的
全部理由**，与"今晚不想做"无关。

## 附：这一笔之后账本上的位置

- A 组 5 条：4 条视图已进表（`a3d119690`），剩 `tensor:argmax_axis` 这 1 条，卡在本文第一、三项。
- 缺口 26 = transformer_step 13（`nn:sgd`、`gelu`、`gelu_grad`、`layer_norm`、`layer_norm_grad`、
  `matmul_grad_lhs`、`matmul_grad_rhs`、`numel`、`permute_inverse`、`shape`、`softmax`、`softmax_grad`、
  `zeros_like`）+ C2 13（`nn:adam`、`nn:adam_state`、`softmax_cross_entropy_labels`、
  `softmax_cross_entropy_labels_grad`、`argmax_axis`、`cast_like`、`gt`、`matmul_grad_lhs`、
  `matmul_grad_rhs`、`numel`、`shape`、`strict_eq`、`take`）。其中 12 条是 B 组（等作者的语义声明那页），
  `argmax_axis` 是本文这一条。
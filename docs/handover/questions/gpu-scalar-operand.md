# `#gpu` 作用域里的标量字面量（`x * 2.0`）

问题一句话：**在一个静态形状的 `#gpu` 函数里，`x * 2.0` 今天在编译期被拒（`GpuScopeNoDeviceProgram`），
因为 `2.0` 在图上留成 `float`（f64），而算子表里"f64 标量作张量算子操作数"没有条目。** 这是 C2 与
`transformer_step` 里最常见的写法之一（缩放、偏置、`1 - x`），所以它挡在验收前面。是补设备侧的支持，
还是让类型系统在张量算子的位置上把字面量定成 f32——**这条不是施工方能定的**。

## 测量（2026-10-24，`origin/develop` `e0446c76e` + 本分支，主机 88zjwei）

探针（`--devices cpu`，`CUDA_VISIBLE_DEVICES=-1`，不碰 CUDA 上下文；只用来看编译期答案）：

```cml
#gpu
func choose(flag: bool, x: Tensor<f32, [2, 3]>): Tensor<f32, [2, 3]> {
    let y = x * 2.0
    if flag then y + 1.0 else y / 0.0
}
```

- `tiles::compile` 退出 1：``#gpu` scope 'choose': operator 'tensor:multiply' has no device program
  (no device program for this shape)``（`RuntimeDiag::GpuScopeNoDeviceProgram`, `0x040001A7`）。
- 同一个程序去掉 `#gpu`、或把参数写成 **开放形状** `Tensor<f32>`，都不拒：开放形状先撞
  `unresolved_type`，`gpuRefusal` 对它是空的（留给运行期按实参重试，h251）。所以今天的现象是
  **"静态形状 + 标量字面量"才红**，而 CI 里没有一条用例走这条路（`nofallback.*` 用的都是 `x * x`）。
- `std::gir` 里那个常量是 `N02 "2.0" value_type="float"`，即 f64；区域分析给该 `multiply` 的理由是
  `scalar_tensor_operand`（`modules/tiles/src/graph_regions.cpp`：输入的元素类型是 `Float64` 就拒绝，
  注释写着 "f64 scalar arithmetic has its own lowering and is not selected by this table yet"）。
- 设备侧并非完全没有 f64 标量：`modules/tiles/src/compiler.cpp` 的 `host()` 有 `runtimeInput && Float64` 的
  `input` 节点（`shape={}`、`dtype=F64`、`scalarBits`），`modules/tiles/src/compiler.cpp` 的 `scalarOps` 表里
  `:op/*_d` 的 `scalar_*` 算子也在，循环的 f64 标量状态走的就是它。

## 选项与代价

1. **设备侧接受"常量 f64 标量作张量算子操作数"**：区域分析不再拒 `scalar_tensor_operand` 里的常量一类，
   把它建构成标量输入（`shape={}`、`F64`、`scalarBits`），点式/归约核把它当广播标量。
   代价：`graph_regions.cpp` 的算子表 + `kernel_ir`/`pointwiseAddress` 的标量一侧 + 代码生成；运行期才知道
   的 f64 标量（`let a = 2.0; x * a`）仍要单独决定（今天的 `host()` 只在 `runtimeInput` 时接受它）。
   收益：C2/transformer 的常见写法直接可用，和已在用的循环 f64 标量同一套机制。
2. **类型系统在张量算子的位置上把字面量定成 f32**（`2.0` 变成 `Tensor<f32>` 的标量，f64 只是显式 `f64` 时才有）：
   代价：属于类型系统与字面量上下文的裁定，改动会牵到 `literal_context` 一类既有期望；收益：图上根本没有 f64
   操作数，planner 的拒绝条目自然消失。风险：`float` 在 Camel 里是 f64，`x * 2.0` 的精确语义（是否提升到 f64
   计算、再截回 f32）是语言语义问题。
3. **保持拒绝，只把诊断写准**（"这个算子在这里是 f64 标量操作数，设备侧不支持"，并把出路写成 `#cpu`）：
   代价最小；收益是用户能看懂。但它把"全部算子在 GPU 上"的验收挡在 H50 之后。

## 倾向（施工方）

**选项 1**，作为 G1/G2 之间的一步（它不需要新的语言语义，且与循环 f64 标量共用机制）；选项 2 请作者裁定，
因为"字面量在张量算子位置的类型"是语言语义，不由施工方决定。选项 3 可以立刻做（诊断文本），但它不解验收。

## 相关

- 计划与本项的关系：[`docs/design/gpu/plan-modules.cn.md`](../../design/gpu/plan-modules.cn.md) 的 M0。
- `#gpu` 作用域拒绝的其它两处实测：`docs/design/gpu/plan-modules.cn.md` §1 末尾（同一条诊断只列了一个算子；
  `NoDeviceProgram` 的消息里 "operator" 出现了两次）。
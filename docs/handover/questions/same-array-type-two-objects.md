# 同一个数组类型的两个对象：谁该修（tensorOf 与 tiles 的类型匹配）

## 红

开工 tip `98067e3cb`，并入 `origin/develop` 后 `588b3159b`。`test/plans/gpu/tiles.plan.toml` 在同一棵树上
101 / pass 98 / 3 红：`graph_facts.nvm`、`graph_facts.fvm`、`graph_facts.jit`，三份失败在同一条断言上
（`static graph facts not reused`），断言要求前两个 plan 的 `graph_fact_instances === 2 &&
specialized_instances === 0`。

## 读数

`modules/tiles/src/compiler.cpp` 的 `graphInstance` 把「图的操作数类型就是这次调用传的类型」判成了**对象指针相等**。
在 `fixed`、`dynamic` 三个图上临时打印（已复原）后，同一棵树上是：

```
g=fixed n=1 same=0
  arg0 gt=0x…a7f8 at=0x…db58
```

其中 `at` 恰是本次运行里 `modules/tensor/ops/support.cpp` 的 `tensorOf` 刚返回的那个对象；`tensorOf`
每次调用都用 `type::Type::create(*facts.dtype)` 造一个**新的**基本类型当元素类型，所以
`f32[2, 3]` 每次都是新对象。两边的文本与 mangle 相同、`elemId` 一个 5 一个 0。

## 本车道修的那一半（reader）

`sameType`（`src/core/type/text.cpp`）对数组的规矩是：固定秩与开放秩是不同类型，两个不同的常量 extent
也是不同类型，非常量 extent 互相视为同一维。这正好是这条断言要的判据：`fixed` 的 `f32[2, 3]` 与传来的一致，
`dynamic` 的 `f32[..]` 与 `f32[2, 3]` 不一致。而按对象比较与这条规矩无关，也违反
`include/camel/core/type/intern.h` 的说明（「转换到它自己的类型是否合法由结构决定，绝不由各自是哪个对象决定」）。

改法：`modules/tiles/src/compiler.cpp` 两处判据合并为 `sameOperandTypes`，用 `Type::equals`（结构相等）比较。
`test/plans/gpu/tiles.plan.toml` 随之为 101 / pass 101，`dynamic` 与 `axis` 的 0 条 facts 一格未动。

## 留在这里的另一半（producer，跨车道）

`tensorOf` 用新建的基本类型当元素类型，于是同一个 `f32[2, 3]` 可以有两个对象：按
`include/camel/core/type/intern.h`，复合类型的 intern key 取分量地址，分量都 interned 时同一个类型才是同一个对象。
把 `modules/tensor/ops/support.cpp` 的 `tensorOf` 改成取 core 的那一个对象（`Type::Float32()` 等）后，
`test/plans/gpu/tiles.plan.toml` 也 101 / pass 101。但同一次全量闸门
（`smoke,test/plans/feat`，`-Tier fast -Jobs 8`）从 15 红变成 16 红，新增三条：

```
[FAIL] specialize_growing_shape.fold_specialize.bounded   Error: unexpected graph count: 5
       （test/plans/feat/opt/generic.plan.toml 的 stats.graphs > 5，同一条里 stats.specializations === 1 仍然成立）
[FAIL] profile.alloc_step
[FAIL] fusion_group.profile.none   FAIL: the report is the base commit's report, byte for byte apart from the measured numbers
```

同时 `export_fold` 与 `rules.tensor_saturation_keeps_values` 由红转绿。也就是说这个生产者的修法会改到
opt / trans / profile 三个族钉住的数（图数、分配、profile 报告），不是本车道能替它们重推的读数。
所以本车道只修 reader，`tensorOf` 这一处按原样留着，交给裁定。

同一类写法（按元素编码新建基本类型，再拼复合类型）在本车道读到的还有：
`modules/tensor/ops/support.cpp`（`tensorOf`，秩 0 装箱）、`modules/tensor/tensor.cpp`（`createTensor`）、
`modules/tensor/module.cpp`（值类型精化器与 `.npy` 入口参数装载器）、`src/core/array/dlpack.cpp`、
`src/core/type/base.cpp`（`Type::clone` 对基本类型也新建对象，被
`src/core/type/composite/array.cpp` 的深拷贝用到）。core 没有「按元素编码取那一个类型对象」的入口，
tensor 模块的辅助函数也不能被 core 依赖；这条要修在哪一层，由语言作者裁定。
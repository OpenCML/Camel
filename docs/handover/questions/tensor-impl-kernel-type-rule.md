# 第一批标准库 `#impl`：`tensor:log_softmax` 的内核声明写不出忠实的签名

## 问题

作者已裁定（`questions-2026-10-23.md` 的 `impl-kernel-site` 节）：声明模块可以有函数体、泛型函数可以带
`#impl`。裁定落地的第一批里，`array::sum`/`min`/`max`（泛型 + 按类型执行器）已经在语言层具备条件，
但 **`tensor` 的 `log_softmax` 卡在签名上**：`stdlib/tensor.cml` 自己的头部写着
「元素类型跟随参数的签名不能作为声明」（H151）；`softmaxInfer` 在 `Float64` 时保留、否则返回 `Float32`；
而规范定义（`stdlib/tensor_definitions.cml` 的 `tensor:log_softmax`）是 `Tensor<f32>`。
于是同一个名字下要写的 "Kernel + `#impl`" 对只有三种写法：

- **(a)** `(Tensor<f32>, int?) => Tensor<f32>`：**收窄公开的名字**，今天 f64 的结果没了。
- **(b)** 对元素类型泛型：对 C++ 内核接受的整数张量**是假的**。
- **(c)** 给语言加一点东西：让声明能直接写**算子自己的类型规则**（不必把类型规则抄成一条签名）。

判别用例：`let t: Tensor<f64> = …; log_softmax(t)`——**(a) 拒绝它，(b) 返回 `Tensor<f64>`，今天返回 `Tensor<f64>`**。

## 选项与代价

- **(c)（建议，若要让这个位置忠实）**：为"声明可以命名算子自己的类型规则"留一个写法。代价：一次语言扩展，
  但它同时解决 `softmax`、以及其它"类型规则由内核自己决定"的算子，一次付清。
- **(a)**：最省事，今天就能落地第一个标准库 `#impl`。代价：`log_softmax` 在 f64 上退化，
  而**今天它是 f64**——等于用一个迁移把已有行为收窄。
- **(b)**：不推荐：整数张量走不通，等于给一个假签名。

## 影响

挡住**第一个标准库 `#impl` 内核**，以及第一批想要的 ONNX/融合对照。注意性质本身（内核与函数体一致）
**已经有钉子**：`test::expand_definitions` + `test/cases/definitions/kernels.cml` 覆盖
`tensor_definitions::log_softmax`，所以这不是"没测"，而是"位置没法定"。

另一处已定位、未改：把 Camel 定义搬进 `tensor.cml` 还要让导出的守卫
（`src/compile/gir/builder_modules.cpp:927` 的 `if (module_->getNativeEntity(ref).has_value()) continue;`）
允许**同名 Camel 定义遮蔽 C++ 组**。

## 来源

- 裁定：`docs/handover/reviews/questions-2026-10-23.md` → `## impl-kernel-site`。
- 施工方报告：`handover/h336-decl-module-body`（语言改动已落地：两个诊断删除，256 → 254 码）。
- 相关：`stdlib/tensor.cml` 头部（H151）、`softmaxInfer`、`stdlib/tensor_definitions.cml` 的
  `tensor:log_softmax`、`modules/onnx/lowering.cpp` 的 `LogSoftmax` 降级。
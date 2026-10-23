# `array::sum`/`min`/`max` 的内核形式需要一个 C++ 的 `array:` 算子组吗？

## 问题

裁定（`docs/handover/reviews/questions-2026-10-23.md`「## impl-kernel-site」）点名的第一批标准库 `#impl`
是 `array::sum`/`min`/`max`（泛型 + 按类型执行器）。语言侧已经具备条件（声明模块可以有函数体、泛型函数
可以带 `#impl`，已合入），但**这一批的 kernel 形式今天写不出来**，原因不是语言而是一个模块边界：

- 内核只能命名**同一个模块**的算子：`src/compile/gir/builder_modules.cpp:498-507` 用
  `SemanticDiag::NativeDeclarationForeignOperator` 拒绝别的模块的协议。
- `array` 只有一个执行器组，里面没有任何 min/max/sum 算子（`src/builtin/operators/array.cpp:405-423`；
  协议在 `src/builtin/modules/globals.cpp:2488` 注册）。
- min/max/sum 的执行器都在**另一个模块**里：`math:min_bf`/`math:max_bf`/`math:abs_bf` 等
  （刚由 h343 落地，`getMathNumericOpsMap`）。

于是 `array::sum`/`min`/`max` 若写成"无体内核 + 函数体"，**对任何类型都解析不到执行器**：每个实例都会
跑函数体，`std::profile` 里看不出第二条路径——也就是"有 kernel 形式"这件事在行为上不可观测，等于没做。

## 选项与代价

- **(A)（施工方建议）加 C++ 的原生 `array:sum`/`min`/`max` 算子**，让 `array` 的执行器组里有名字可指。
  代价：同一份比较/求和会有**第三份陈述**（摆在 `modules/math/minmax.h` 与
  `modules/tensor/kernels/minmax.h` 旁边），而且刚加好的 `math:*_bf` 执行器对数组路径而言成了多余的一层。
  收益：`array` 的两个名字（函数名与内核名）同模块、可观测。
- **(B) 放宽"内核只能命名本模块算子"**（允许内核命名另一模块的算子）。代价：动模块边界的规则，
  影响所有模块的封装判断；收益：不必新增算式陈述，`array` 直接指 `math:min_bf`。
- **(C) 这一批的 `array` 只保留函数体**（不写内核），把"泛型 + 按类型执行器"的验收改到 `tensor`
  的 `log_softmax`（已裁定 `when is_float::<T>()`）或别的算子上。代价：`array::sum`/`min`/`max`
  没有内核路径；收益：不新增第三份陈述，也不必动模块边界。

## 影响

只挡"第一批 `array` 内核"这一项。相关但独立的另一处：`tensor` 的 `log_softmax` 需要导出守卫
（`builder_modules.cpp:927`）允许同名 Camel 定义遮蔽 C++ 组，那件事的签名问题已裁定，可以单独做。

## 来源

- `handover/h343-half-math` 的施工方报告（Job 2 停手理由与代码位置）。
- 裁定：`docs/handover/reviews/questions-2026-10-23.md`「## impl-kernel-site」。
- 代码：`src/compile/gir/builder_modules.cpp:498-507`、`src/builtin/operators/array.cpp:405-423`、
  `src/builtin/modules/globals.cpp:2488`、`modules/math/minmax.h`。
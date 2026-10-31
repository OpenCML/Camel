# M4 准备件：autodiff 用例按“库层能不能过”分类

对象：`test/plans/feat/modules/autodiff.plan.toml`。
计数（可复现）：`grep -c '^\[\[tests\]\]' test/plans/feat/modules/autodiff.plan.toml` = **213 条测试项**；
其中 `case = ` 去重后 **91 个用例文件**（同一用例有 nvm / macro / simplified / fvm / jit / tf 等变体）。

“库层能不能过”的判据（来自 `docs/design/program-transforms/README.cn.md` 与设计原则 6.x）：
**原语只写局部规则（`#jvp` / `#transpose` / `#vjp`），调用、分支、循环、效应由 `reinterp` / `split` / `transpose`
各处理一次**。所以一个用例能过，当且仅当它检查的东西落在这两样里之一：原语的规则，或三个算子的结构处理。

| 类号 | 含义 | 条数 |
|---|---|---|
| **A** | 只考原语规则（直线代码、数学、张量/nn、数组邻接、二阶穿过数组邻接） | 34 |
| **B** | 考结构处理（调用/闭包/分支/循环/效应，含各后端变体） | 33 |
| **C** | 考拒绝（缺规则/无切空间/名字删除，编译期或运行期诊断） | 18 |
| **D** | 机制特定：测现有 C++ pullback、macro 展开或融合分组，新路线里没有对应物 | 4 |
| **E** | 支撑件，不是用例（NumPy 对照脚本） | 2 |
| 合计 | | **91** |

## A 类：原语规则（34）

| 用例 | 依据 |
|---|---|
| `scalar_grad.cml` | 直线标量 + helper 调用，全部是原语规则 |
| `tensor_grad_check.cml` | 逐条张量求导规则对有限差分 |
| `nn_grad_check.cml` | conv/pooling/embedding/cross-entropy 规则对有限差分 |
| `custom_rule.cml` | `@vjp` 自定义规则，正是规则集的形状 |
| `math_sqrt.cml` | `erf`/`erfc` 的 `#vjp` 规则 |
| `math_erf.cml` | 同上，标量 f32/float 规则 |
| `grad_through_as.cml` | `as` 转换的规则（常量、计算值、开口 extent） |
| `grad_first_with_parameter.cml` | `with` 参数的选择规则，不涉及图结构 |
| `grad_int_zero.cml` | `int` 的切空间是它自己、梯度取零 |
| `grad_struct_all_discrete.cml` | 全离散字段结构的切空间逐个字段取 |
| `grad_tuple_discrete_member.cml` | 元组保留离散成员，梯度零 |
| `grad_data_input.cml` | 普通实参是数据，规则只作用于 `with` 参数 |
| `discrete_zero.cml` | `one_hot`/`argmax`/比较掩码的零规则 |
| `grad_scalar_width.cml` | f32 标量宽度，规则里加的 `:op/dtof` |
| `f64_broadcast_grad.cml` | 广播的 `sum_to` 规则，f64 |
| `f64_matmul_grad_width.cml` | `matmul_grad_lhs/rhs` 规则，累加宽度 |
| `vjp_cross_module.cml` | 跨模块 `#vjp` 规则，规则集按名取 |
| `err_vjp_annotation.cml` | 跨模块规则在泛型化简后仍可读 |
| `transform_rules.cml` | `#jvp`/`#transpose`/`#vjp` 按结构读回，规则机制本身 |
| `slice_grad.cml` | `t[a:b]` 的邻接是补零 scatter |
| `slice_grad_negative_step.cml` | 同上，负步长/嵌套 |
| `slice_open_end.cml` | 开口 end 的邻接，end 由 `dim(x,0)` 得来 |
| `array_take_grad.cml` | 行 take 的邻接是 `scatter_take_arr` |
| `adjoint_reshape_array.cml` | reshape 的邻接是 reshape 回来 |
| `adjoint_concat_array.cml` | concat 的邻接是切分上游梯度 |
| `array_slice_grad.cml` | 数组切片的邻接是 range scatter |
| `adjoint_cast_array.cml` | cast 的邻接是转回元素类型 |
| `concat_array_part.cml` | concat 数组部分的邻接，常量部分跳过 |
| `second_order_array_add.cml` | 二阶穿过 gather，`zeros_arr` 规则 |
| `gather_discrete_index_zero.cml` | gather 不对索引累加，离散零 |
| `second_order_array_zeros.cml` | 二阶穿过运行期索引 gather，`scatter_add_arr` |
| `second_order_array_scatter.cml` | 二阶穿过计算边界的切片，`scatter_slice_arr` |
| `second_order_array_slice.cml` | 二阶穿过切片规则链 |
| `second_order_array_append_init_blocked.cml` | `append` 邻接拆成 `init_arr`/`tail_arr` |

> 说明：`second_order_*` 五条与 `concat_array_part` 等在本栏里是**规则的组合**（一阶规则再穿一次），
> 不是新的图结构，所以归 A 而不是 B。

## B 类：结构处理（33）

| 用例 | 依据 |
|---|---|
| `control_flow_grad.cml` | 数据相关分支 + 递归，分支由 `reinterp`；递归按调用处理 |
| `grad_closure.cml` | 被求导函数里建闭包并调用，闭包是调用结构 |
| `grad_function_values.cml` | 函数值（具名/别处构造的闭包）被调用 |
| `grad_function_values_arms.cml` | 分支臂里建闭包再调用 |
| `grad_function_values_effects.cml` | 函数值 + 效应（效应只在正向执行） |
| `grad_unexpanded_inline.cml` | 分支/递归/函数值参数，macro 不展开时也成立 |
| `grad_specialized_callee.cml` | 被特化到常量实参的被调函数 |
| `grad_unexpanded_specialize.cml` | 递归被调函数 + 常量起始下标 |
| `loop_grad_sum.cml` | 求和循环，每轮残差收集、反向按轮走 |
| `loop_grad_product.cml` | 循环携带累加器 `acc ← acc·0.5 + w` |
| `loop_grad_affine.cml` | 两值状态 `(round, acc)`，int 无切线 |
| `loop_grad_tuple_state.cml` | 元组状态，切线同形 |
| `loop_grad_struct_state.cml` | 结构状态，正向重写 |
| `loop_grad_zero_rounds.cml` | 零轮循环，tape 空、反向不跑 |
| `loop_grad_nested.cml` | 循环套循环，内层在外层 tape 里 |
| `loop_grad_capture_table.cml` | 循环体读捕获数组元素，梯度在读取处停 |
| `loop_grad_iterable.cml` | 迭代数组求导，fvm 调度反向循环 |
| `loop_grad_iterable_records.cml` | 记录数组迭代，与元组版逐位相同 |
| `loop_grad_iterable_tensors.cml` | 同上，fvm 跑 |
| `loop_grad_record_tensor_fd.cml` | 记录数组循环梯度的有限差分，fvm |
| `loop_grad_iterable_length.cml` | 数组字面量循环，反向循环无数组梯度算子 |
| `loop_grad_literal_array.cml` | 数组字面量循环，`w + w*(1+2+3)` |
| `loop_grad_parameter_array.cml` | 同上，参数数组 |
| `loop_grad_refuse_no_element_tangent.cml` | `for row in w` 取行，行邻接 scatter |
| `loop_grad_tensor_iterable.cml` | 同上，macro 预构建梯度 |
| `loop_grad_refuse_slice_iterable.cml` | 捕获数组依赖参数，读 scatter 回去 |
| `loop_grad_refuse_capture.cml` | 同上，macro 收集 |
| `loop_grad_in_place.cml` | 原地写数组状态的循环，需要 tape 时退化 |
| `loop_grad_composite_read.cml` | 复合状态，梯度来自循环外计算的值 |
| `loop_grad_closure_in_body.cml` | 循环体里建闭包并调用，区域结果是那次调用 |
| `grad_effects_kept.cml` | 带效应的函数求导：效应只在正向执行、按顺序各一次 |
| `grad_captured_var_written.cml` | 被求导函数调用写模块级 `var` 的函数：写是效应，版本是资源状态 |
| `grad_module_var_sync.cml` | 同上，`std::opt` 在调度之前，段错误那条排列 |

## C 类：拒绝（18）

| 用例 | 依据 |
|---|---|
| `grad_refuse_no_with.cml` | 没有 `with` 参数 → `NotDifferentiable` |
| `grad_refuse_var.cml` | 首个 `with` 参数是 `var`（3.5） |
| `grad_refuse_string.cml` | `with` 参数是 `string`，无切空间 |
| `grad_refuse_function.cml` | `with` 参数是函数，无切空间 |
| `grad_refuse_struct.cml` | 结构某字段无切空间，诊断点名该字段 |
| `grad_refuse_tuple.cml` | 元组某成员无梯度，诊断点名位置 |
| `grad_refuse_return.cml` | 返回 `int` 而非 float |
| `discrete_zero_refuse.cml` | 离散名取的零，规则里拒绝累加 |
| `refuse_container_reshape.cml` | 非数值数组容器的 reshape 无邻接 |
| `refuse_container_concat.cml` | 同上，concat 的容器部分 |
| `refuse_gradient_element_type_rule.cml` | 9.9 要求拒绝时不许改动的零 |
| `with_grad_old_name.cml` | 旧名删除 → 普通未解析引用（诊断，不是求导） |
| `grad_function_value_branch.cml` | 运行期分支选函数 → 拒绝，报一次 |
| `grad_function_value_static_branch.cml` | 常量条件也仍是分支 → 拒绝 |
| `grad_function_value_parameter.cml` | 被调函数是被求导函数的参数 → 拒绝 |
| `grad_function_value_container.cml` | 函数值从元组读出 → 拒绝 |
| `grad_function_value_recursion.cml` | 递归把自己传下去 → 拒绝 |
| `grad_function_value_passed.cml` | 依赖参数的函数值传给无参数函数 → 拒绝 |

## D 类：机制特定，新路线没有对应物（4）

| 用例 | 为什么不能照搬 |
|---|---|
| `pb_statics_gc.cml` | 测的是**现有 C++ pullback 子图的静态常量在 GC 后仍活着**：新路线输出图里没有回传闭包子图（设计原则 6.x），这条随 `pullback.cpp` 一起消失，不能迁 |
| `collapse.cml` | 断言“折叠后的梯度图与 **pullback 形式**同值”：`pullback` 这个对照物在新路线里不存在，得改写成“与 `vjp` 形式同值”才有意义，不是迁移 |
| `second_order.cml` | 断言二阶梯度图**化简后没有直接/间接调用**——这是现有宏展开 + 化简的产物形状，新路线的 `transpose` 图本来就没有调用，断言会退化成恒真 |
| `fused_grad_check.cml` | 检查的是 `tensor::fuse` 的分组与 `tensor.fusion` 属性，**不是求导**；它跟着 tensor 融合那条线走，不属于 M4 |

## E 类：支撑件（2，不是用例）

| 文件 | 用途 |
|---|---|
| `grad_structures.py` | Python 侧结构描述，被 `grad_function_values_reference.py` 读 |
| `grad_function_values_reference.py` | 把每个 loss 用 NumPy float64 重写并求导，做**独立对照** |

## 迁移顺序建议（按类号）

1. **A 类 33 条**：先做，规则层是 `reinterp` 的地基，且每条都必须与有限差分或 NumPy 对照（已有对照件）。
2. **B 类 32 条**：按 分支 → 调用/闭包 → 循环 的顺序；循环那一组（`loop_grad_*` 20 条）建议一次迁完，因为 tape 形状是同一件事。
3. **C 类 18 条**：诊断文案与位置要对齐，放在规则与结构都稳定之后，否则文案会反复改。
4. **D 类 5 条**：不迁，按上表逐条改写成新路线的等价断言（或删除），需要作者确认改写口径。
5. **E 类 2 个文件**：保留，作为对照件。

## 判据

「一条用例说不清“它考的是规则还是结构”的时候，它还没准备好迁——先把它拆成两条。」

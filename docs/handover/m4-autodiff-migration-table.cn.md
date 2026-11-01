# M4 迁移：autodiff 用例的库层分类表

对象：`test/cases/modules/autodiff/` 下全部 **127** 个 `.cml`。
问题：**只 `import vjp`/`grad` 能不能过？** 答案逐条量出来，不是估的。

## 分母（计划里实际跑的）

| 计划 | `[SUMMARY]` |
| --- | --- |
| `test/plans/feat/modules/autodiff.plan.toml` | `total: 263, pass: 263, fail: 0` |
| `test/plans/feat/modules/autodiff_value_with.plan.toml` | `total: 6, pass: 6, fail: 0` |
| `test/plans/feat/modules/autodiff_inspection.plan.toml` | `total: 36, pass: 36, fail: 0` |
| `test/plans/feat/modules/remat.plan.toml` | `total: 2, pass: 2, fail: 0` |
| 合计 | **307 条执行，0 fail** |

127 个文件跑出 307 条执行（一个用例多后端、多参数各算一条）。

## 怎么量的

1. 每个用例复制成同目录下的 `<name>.probe.cml`，把 `from autodiff` 改成 `from vjp`，**只保留库里有的
   名字**（`vjp`、`grad`、`with_grad`）；兄弟模块仍能解析，用例本身一行没改，跑完即删。
2. `std::nvm` 跑一遍，退出码 0 记"过"，否则记诊断原文的第一条。
3. 诊断里说得出原因的按诊断记（库里没有某个名字、参数是数组、`with` 写法），说不出的按源码形态记。

## 结论

**库层今天能过：1 / 127。**

| 缺什么 | 条数 | 用例 |
| --- | --- | --- |
| 数组/张量参数 + `grad(f)<x>()` 写法 | 35 | `adjoint_*`、`grad_tuple_read`、`var_*` 等 |
| `with` 函数 + `grad(f)<x>()`（库签名是 `grad(f)(x)`，`f: (x: float) => float`） | 51 | `collapse`、`loop_grad_*`、`grad_module_var_sync` 等 |
| 只用数组/张量参数 | 19 | `slice_grad`、`tensor_grad_check` 等 |
| 库里没有这个名字 | 9 | `remat`、`stop_gradient`、`value_and_grad`、`*_tangent` |
| `@vjp` 注解写法（现在是 `#vjp(rule)` 声明标签） | 1 | `err_vjp_annotation` |
| 不在 `vjp` 这条路上（`prelude` 的 `jvp`/`vmap`，或只测名字解析） | 11 | `jax_vmap*`、`jax_names/jvp` 等 |
| **过** | **1** | `grad_refuse_no_with` |

## 写法/类型之后的两道墙（两条最小探针量的，不是估计）

- `grad(x => x*x*x)` 过；`grad(x => x + 1.0)` **不过**：
  `TRANSPOSE function=autodiff::add_tangent<float> skip=output` → `transpose has no transpose for this function`。
  即：**纯乘积链能过，带加法的就不行**。
- `grad(x => x*x + 3.0*(x+1.0)*(x+1.0))` 同理死在加法那一支；
  一旦函数体里调了别的函数（`sq(x) + 3.0*sq(x+1.0)`），更早死在
  `split does not build this function (the value half reads the tangent of parameter 0, which the forward half does not take)`。

所以"过半"的真正账不是每条用例缺一条规则，而是三件事：库的**声明面**（`Vjp<F>` 泛型，设计 6.1）、
**`add_tangent` 的转置**（`skip=output`）、**函数体里有调用时 `split` 不建**。这三件不在用例那边。

## 逐条（127 行）
| # | 用例 | 库层 | 缺什么 |
| --- | --- | --- | --- |
| 1 | `adjoint_cast_array` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 2 | `adjoint_concat_array` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 3 | `adjoint_reshape_array` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 4 | `array_slice_grad` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 5 | `array_take_grad` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 6 | `collapse` | 不过 | **试点 1/5：需要新能力** — `with <x: float>` 改成参数是纯拼写，`func h(x: float)` + `grad(h)(2.0)` 编得过（老读数 `collapse dh=22.0`，改写后同一输入应仍是 22.0）；但函数体里调了 `sq`，`split` 拒绝：`REINTERP split function=jvp_of_h skip=forward construct=a tangent port uri=- why=the value half reads the tangent of parameter 0, which the forward half does not take` |
| 7 | `collapse_random` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 8 | `collapse_runtime` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 9 | `concat_array_part` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 10 | `control_flow_grad` | 不过 | **试点 2/5：需要新能力** — 五条里最简单的一条（`leaky`，单个 `with <x: float>`）改成参数后仍然过不去：`REINTERP split function=jvp_of_leaky skip=exit (a lifted function answers with the pair (result, tangent); this graph's exit is not a two-member tuple fill, so there is no pair to cut)`——函数体里有分支。其后还有一件库补：`power`/`pick`/`walk` 是 `(x, n)` 多参，库只有一元 `grad`。老读数 `a=6.0 b=0.5 c=80.0 d=12.0 e=0.0 w=-10.0` |
| 11 | `custom_rule` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 12 | `discrete_zero` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 13 | `discrete_zero_refuse` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 14 | `err_vjp_annotation` | 不过 | 写法：@vjp 注解（现在是 #vjp(rule) 声明标签） |
| 15 | `f64_broadcast_grad` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 16 | `f64_matmul_grad_width` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 17 | `flat_element_pullback` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 18 | `fused_grad_check` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 19 | `gather_discrete_index_zero` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 20 | `grad_captured_var_written` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 21 | `grad_closure` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 22 | `grad_data_input` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 23 | `grad_effects_kept` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 24 | `grad_first_with_parameter` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 25 | `grad_function_value_branch` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 26 | `grad_function_value_container` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 27 | `grad_function_value_parameter` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 28 | `grad_function_value_passed` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 29 | `grad_function_value_recursion` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 30 | `grad_function_value_static_branch` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 31 | `grad_function_values` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 32 | `grad_function_values_arms` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 33 | `grad_function_values_effects` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 34 | `grad_int_zero` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 35 | `grad_module_var_sync` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 36 | `grad_refuse_function` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 37 | `grad_refuse_no_with` | **过** | — |
| 38 | `grad_refuse_return` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 39 | `grad_refuse_string` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 40 | `grad_refuse_struct` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 41 | `grad_refuse_tuple` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 42 | `grad_refuse_var` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 43 | `grad_scalar_width` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 44 | `grad_specialized_callee` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 45 | `grad_struct_all_discrete` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 46 | `grad_through_as` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 47 | `grad_tuple_discrete_member` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 48 | `grad_tuple_read` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 49 | `grad_unexpanded_inline` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 50 | `grad_unexpanded_specialize` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 51 | `jax_batching` | 不过 | 不在 `vjp` 这条路上：不 import `autodiff` 的名字（`prelude` 的 `jvp`/`vmap`） |
| 52 | `jax_derivatives` | 不过 | 库里没有 `stop_gradient` |
| 53 | `jax_has_aux` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 54 | `jax_names/argnums` | 不过 | 库里没有 vjp |
| 55 | `jax_names/checkpoint` | 不过 | 不在 `vjp` 这条路上：不 import `autodiff` 的名字（`prelude` 的 `jvp`/`vmap`） |
| 56 | `jax_names/has_aux_false` | 不过 | 库里没有 vjp |
| 57 | `jax_names/has_aux_plain` | 不过 | 库里没有 vjp |
| 58 | `jax_names/jvp` | 不过 | 不在 `vjp` 这条路上：不 import `autodiff` 的名字（`prelude` 的 `jvp`/`vmap`） |
| 59 | `jax_names/remat` | 不过 | 库里没有 `remat` |
| 60 | `jax_names/vjp` | 不过 | 不在 `vjp` 这条路上：不 import `autodiff` 的名字（`prelude` 的 `jvp`/`vmap`） |
| 61 | `jax_names/vmap_in_axes` | 不过 | 不在 `vjp` 这条路上：不 import `autodiff` 的名字（`prelude` 的 `jvp`/`vmap`） |
| 62 | `jax_names/vmap_two_parameters` | 不过 | 不在 `vjp` 这条路上：不 import `autodiff` 的名字（`prelude` 的 `jvp`/`vmap`） |
| 63 | `jax_vmap` | 不过 | 不在 `vjp` 这条路上：不 import `autodiff` 的名字（`prelude` 的 `jvp`/`vmap`） |
| 64 | `jax_vmap_tensor` | 不过 | 不在 `vjp` 这条路上：不 import `autodiff` 的名字（`prelude` 的 `jvp`/`vmap`） |
| 65 | `literal_pullback` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 66 | `loop_grad_affine` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 67 | `loop_grad_capture_table` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 68 | `loop_grad_closure_in_body` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 69 | `loop_grad_composite_read` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 70 | `loop_grad_in_place` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 71 | `loop_grad_iterable` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 72 | `loop_grad_iterable_length` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 73 | `loop_grad_iterable_records` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 74 | `loop_grad_iterable_tensors` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 75 | `loop_grad_literal_array` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 76 | `loop_grad_nested` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 77 | `loop_grad_parameter_array` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 78 | `loop_grad_product` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 79 | `loop_grad_record_tensor_fd` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 80 | `loop_grad_refuse_capture` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 81 | `loop_grad_refuse_no_element_tangent` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 82 | `loop_grad_refuse_slice_iterable` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 83 | `loop_grad_struct_state` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 84 | `loop_grad_sum` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 85 | `loop_grad_tensor_iterable` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 86 | `loop_grad_tuple_state` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 87 | `loop_grad_zero_rounds` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 88 | `macro_round_lifetime_gc` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 89 | `math_erf` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 90 | `math_sqrt` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 91 | `nn_grad_check` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 92 | `pb_statics_gc` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 93 | `probe_library_grad` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 94 | `refuse_container_concat` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 95 | `refuse_container_reshape` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 96 | `refuse_gradient_element_type_rule` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 97 | `remat_identity` | 不过 | 库里没有 `remat` |
| 98 | `scalar_grad` | 不过 | 库里没有 `stop_gradient` |
| 99 | `second_order` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 100 | `second_order_array_add` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 101 | `second_order_array_append_init_blocked` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 102 | `second_order_array_scatter` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 103 | `second_order_array_slice` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 104 | `second_order_array_zeros` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 105 | `slice_grad` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 106 | `slice_grad_negative_step` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 107 | `slice_open_end` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 108 | `tensor_grad_check` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 109 | `transform_rules` | 不过 | 库里没有 `add_tangent`、`div_tangent`、`mul_cotangent`、`mul_tangent`、`neg_tangent`、`sub_tangent` |
| 110 | `two_contexts_grad` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 111 | `value_with_grad` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元）；且写法是 `grad(f)<x>()` |
| 112 | `value_with_reduce` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 113 | `value_with_refusal` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 114 | `var_array_append` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 115 | `var_tensor_local` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 116 | `var_tensor_read` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 117 | `var_tensor_read_after_call` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 118 | `var_tensor_read_after_write` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 119 | `var_tensor_read_in_callee` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 120 | `var_through_with_grad` | 不过 | 参数是数组/张量（库只声明 `float` 一元与二元） |
| 121 | `vjp_cross_module` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 122 | `vjp_lib` | 不过 | 不在 `vjp` 这条路上：不 import `autodiff` 的名字（`prelude` 的 `jvp`/`vmap`） |
| 123 | `vjp_user_a` | 不过 | 不在 `vjp` 这条路上：不 import `autodiff` 的名字（`prelude` 的 `jvp`/`vmap`） |
| 124 | `vjp_user_b` | 不过 | 不在 `vjp` 这条路上：不 import `autodiff` 的名字（`prelude` 的 `jvp`/`vmap`） |
| 125 | `whole_extrema_grad` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 126 | `whole_extrema_tie_grad` | 不过 | 写法：`grad(f)<x>()` + 被求导的是 `with` 函数（库的签名是 `grad(f)(x)`，`f` 是 `(x: float) => float`） |
| 127 | `with_grad_old_name` | 不过 | 库里没有 value_and_grad |
## 这一笔没做的

- 没有改任何用例（`git diff test/cases/modules/autodiff/` 为空），迁移哪一批等点名。
- 没有把"库层能过"的判定推广到 `compile/transforms/` 之外的目录：只量了这一个目录。

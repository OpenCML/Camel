# M4 自动微分用例三栏普查：库路 / 引擎路 / 旧 ABI

**读数树**：`origin/develop = 639619f95`（`lane-m4census`，未 push）。**只普查，未改任何用例与代码。**

## 一、口径（先把"哪些用例算自动微分用例"定死）

扫描：`test/cases/**/*.cml` 中出现 `grad(` / `grad_aux(` / `with_grad(` / `with_grad_aux(` / `vjp(` / `jvp(` / `forward_grad(` 任一个的文件 —— **279 个**。这 279 个文件在全部计划里共有 **580 个入口**（`macro` 入口 253 个 / 无 `macro` 的运行时入口 327 个），另有 **16 个文件不在任何计划里**。

三栏的判定口径（一栏可以重叠，一个文件可以同时属两栏）：

- **① 库路**：文件有 `std::macro` 入口（宏把 `grad`/`with_grad` 展开成库里的 `vjp`/`transpose` 规则），**或**源码直接用 `vjp`/`jvp`/`split(jvp(...))` 这套库 API。
- **② 引擎路**：文件有**不带 `std::macro`** 的入口 —— 那条路最终落到 `buildGradientGraph`。
- **③ 旧 ABI**：源码里出现已退休的调用形状 —— `transpose(f)(值…, ȳ)`（含 `back(1.0, 1.0)` 这类多实参余切）。

## 二、三栏表

### ③ 旧 ABI（23 个文件，全部逐个列出）

```
compile/transforms/a_refusal_keeps_stdout_to_the_program.cml     back(1.0, 1.0)                    :28
compile/transforms/transpose_branch.cml                          back(1.0, 1.0) / back(-1.0, 1.0)  :31,32,34,35
compile/transforms/transpose_call.cml                            transpose(f)(1.0, 1.0)            :22
compile/transforms/transpose_function_value_no_capture.cml       back(1.0, 1.0) / back(1.0, 2.0)   :41,43
compile/transforms/transpose_loop.cml                            three_back/quad_back(1.0, …)      :26,27,30
compile/transforms/transpose_loop_escape_int.cml                 backI/backG/backH(…, …)           :51,52,55,56,59,60
compile/transforms/transpose_loop_escape_reads_one.cml           back(1.0, 1.0)                    :21
compile/transforms/transpose_loop_escape_reads_outer.cml         back(1.0, 1.0)                    :25
compile/transforms/transpose_loop_escape_reads_twice.cml         back(1.0, 1.0)                    :19
compile/transforms/transpose_loop_escape_two_states.cml          back(1.0, 1.0)/(1.0, 2.0)         :48,51
compile/transforms/transpose_loop_state_second.cml               back(1.0, 1.0)/(1.0, 2.0)         :25,28
compile/transforms/transpose_many_parameters.cml                 transpose(mt)(3,2,1,0,1)          :20
compile/transforms/transpose_many_parameters_in_ct.cml           transpose(mt)(3,2,1,0,1|2)        :18,19
compile/transforms/transpose_no_tangent_bool.cml                 back(2.0, 1.0)                    :31,32
compile/transforms/transpose_no_tangent_closure.cml              back(1.0, 1.0)/(1.0, 3.0)         :36,37
compile/transforms/transpose_no_tangent_tuple_member.cml         back(2.0, 1.0)/(2.0, 3.0)         :28,29
compile/transforms/transpose_straight_line.cml                   back/chain_back(2.0, 1.0)         :25,27
compile/transforms/transpose_tuple_both_slots.cml                back(1.0, 1.0)/(1.0, 2.0)         :22,24
compile/transforms/transpose_tuple_component_first.cml           back(1.0, 1.0)/(1.0, 2.0)         :24,28
compile/transforms/transpose_tuple_component_second.cml          back(1.0, 1.0)/(1.0, 2.0)         :22,25
compile/transforms/transpose_tuple_nested.cml                    back(1.0, 1.0)/(1.0, 2.0)         :22,26
compile/transforms/transpose_unread_parameters.cml               transpose(at)(3,2,1,0,1|2)        :19,20
transforms/transpose/residuals_in_the_closure.cml                back(0.0, 0.0, 1.0)               :35
```

**每一处该改成新形状，还是该被删？**（本轮只判不改）

| 文件组 | 改成什么 | 重复吗 |
| --- | --- | --- |
| 单参数 `f`、`back(值, 余切)` 的 19 个文件 | 值要被捕获：`let (_, lin) = split(jvp(f))(值)`，再 `transpose(lin)(余切)` —— 余切是**唯一**实参 | **不重复**，各钉一种形状（分支、循环逃逸、元组槽位、无切线、直线） |
| `transpose_many_parameters{,_in_ct}.cml`、`transpose_unread_parameters.cml` | `transpose((dx, dw) => NAME(值…, dx, dw))(余切)` | **不重复，但这三条的论点是"已知值优先"，该论点已被收回** ⇒ 改写时要连论点一起改成"这些参数必须被捕获"，数字不变 |
| `transforms/transpose/residuals_in_the_closure.cml:35` | `back(1.0)`（`lin` 本来就是线性半边，收一个余切） | **不重复**：它是唯一钉"转置保留闭包里的残差"的用例 |
| `a_refusal_keeps_stdout_to_the_program.cml:28` | 同第一组；**但它测的是"拒绝不吞 stdout"**，改写法时不许把它从"拒绝"改成"通过" | 不重复 |

### ② 引擎路（221 个文件 / 327 个入口）

其中 **147 个文件没有任何 `macro` 入口**（②-only），六个计划里落下来是 **42 个 `.cml`**，逐个列在这里（这是"删不删"的关键名单）：

```
modules/autodiff/{concat_array_part, discrete_zero_refuse, f64_broadcast_grad, f64_matmul_grad_width,
  flat_element_pullback, fused_grad_check, literal_pullback, loop_grad_affine, loop_grad_capture_table,
  loop_grad_closure_in_body, loop_grad_composite_read, loop_grad_in_place, loop_grad_iterable_length,
  loop_grad_iterable_records, loop_grad_iterable_tensors, loop_grad_iterable, loop_grad_nested,
  loop_grad_product, loop_grad_record_tensor_fd, loop_grad_refuse_no_element_tangent,
  loop_grad_refuse_slice_iterable, loop_grad_struct_state, loop_grad_tuple_state, loop_grad_zero_rounds,
  pb_statics_gc, refuse_container_concat, refuse_container_reshape, second_order_array_add,
  second_order_array_append_init_blocked, second_order_array_scatter, second_order_array_slice,
  second_order_array_zeros, slice_grad_negative_step, value_with_grad, value_with_reduce,
  value_with_refusal, whole_extrema_grad, whole_extrema_tie_grad, with_grad_matches_grad}.cml
transforms/compositions/{grad_vmap_f, vmap_grad_f}.cml
compile/transforms/transpose_unread_parameters.cml
```

### ① 库路（116 个文件有 `macro` 入口；另有 62 个文件直接调 `vjp`/`jvp`/`split(jvp)`）

代表：`modules/autodiff/` 里的 `scalar_grad`、`slice_grad`、`adjoint_reshape_array`、`grad_function_value_*`（7 个都有 `macro` 孪生）、`vjp_api.*`、`compile/transforms/` 的 `jvp_*`、`vjp_*`、`transforms/split/*`。

## 三、三个数

| 栏 | 用例（入口）数 | 文件数 | 红/绿（`639619f95`，Release fast） |
| --- | --- | --- | --- |
| ① 库路 | 253（`macro` 入口）+ 62 个文件直接用库 API | **116**（+62 重叠） | `modules.autodiff` 263/263 全绿；`split` 13/17（4 红）；`compile.transforms` 39/60（21 红） |
| ② 引擎路 | **327** 个运行时入口（其中 ②-only 42 个文件） | **221** | `modules.autodiff` 全绿（它同时是①）；②-only 里 `refuse_container_*`、`loop_grad_*` 全绿 |
| ③ 旧 ABI | 23 个文件 | **23** | 全部**绿**：`residuals_in_the_closure.nvm` PASS、`transpose_*` 全 PASS（`compile.transforms` 的 21 红是 `vjp_api`/`grad_array`，不是它们） |

**计划读数（Release fast，机器空闲时取的）**
```
feat/modules/autodiff            [SUMMARY] total: 263, pass: 263, fail: 0
feat/modules/autodiff_inspection [SUMMARY] total: 36,  pass: 36,  fail: 0
feat/modules/autodiff_value_with [SUMMARY] total: 7,   pass: 7,   fail: 0
feat/transforms/split            [SUMMARY] total: 17,  pass: 13,  fail: 4
feat/transforms/compositions     [SUMMARY] total: 10,  pass: 0,   fail: 2, known failures: 8
feat/compile/transforms          [SUMMARY] total: 60,  pass: 39,  fail: 21
feat/tools/autodiff_migration_ledger [SUMMARY] total: 1, pass: 0, fail: 1
```
合计 **394 个入口：绿 358、红 28、已知失败 8**。28 条红全部同一个根：`stdlib/vjp.cml:78:47`（`vjp` 的旧签名对上新的单余切调用），即 `split_branch_author_vjp`×4 + `compositions.vjp_single`×2 + `vjp_api.*`/`grad_array.*`×21 + 台账 1。

## 四、② 栏逐条："换成库路 `grad`/`vjp` 能不能表达？"

- **能（39 条）**：只断言数值的那些 —— `loop_grad_*` 的 20 条、`second_order_array_*` 的 5 条、`value_with_*` 3 条、`whole_extrema_*` 2 条、`slice_grad_negative_step`、`concat_array_part`、`f64_*` 2 条、`flat_element_pullback`、`literal_pullback`、`discrete_zero_refuse`、`with_grad_matches_grad`、`refuse_container_*` 2 条。**理由：同一程序加 `std::macro` 就有孪生入口，且它们已经在 `modules.autodiff` 里绿**（38 个文件同时有 `macro` 入口即是证据）。
- **不能（4 条，理由各自不同）**：
  - `pb_statics_gc.cml` —— 它钉的是"运行时建图时 pullback 子图的静态常量没有 GC 根"，**这个缺陷只在运行时建图那条路上存在**；
  - `fused_grad_check.cml` —— 注释写明"run it after `tensor::fuse` and **without `std::macro`**"，论点就是"宏不参与时 fuse 组不影响求导"；
  - `loop_grad_closure_in_body.cml` —— 钉的是 tape/region 重写（"the rewrite that differentiates a closure call erased it"），运行时建图独有；
  - `loop_grad_iterable_length.cml` —— 同族（可迭代长度在运行时才读出）。
- `transforms/compositions/{grad_vmap_f,vmap_grad_f}.cml` 是**已知失败**（8 条 XFAIL 之二），说明库路自己还做不出来；它们既不是"可换"，也不是引擎路的守卫。

## 五、结论（一句话）

**`buildGradientGraph` 部分可删**：定义在 `modules/autodiff/pullback.cpp:5044`，**调用点 2 个** —— `modules/autodiff/operators.cpp:160`（运行时内核，函数值不是静态常量时走这里）与 `modules/autodiff/pullback.cpp:2772`（pullback pass 内，函数值静态时走这里）。

- **`pullback.cpp:2772` 那一条：不可删** —— ② 栏 327 个入口全压在它上面，其中 4 条（`pb_statics_gc`、`fused_grad_check`、`loop_grad_closure_in_body`、`loop_grad_iterable_length`）换成库路就换了论点。
- **`operators.cpp:160` 那一条：可删的候选** —— 走到它的只有"`grad` 的函数值是运行时的值"这一族（`grad_function_value_*` 7 个文件、`grad_module_var_sync`、`cml_var_holding_a_lambda`），而**这 7 个文件都有 `macro` 孪生并且都绿** ⇒ 没有一条用例"非它不可"。删它需要同笔把那 7 个文件的 runtime 入口改成 `macro` 入口，并把 `grad_function_value_*` 的论点从"运行时才拿到函数值"改成"编译期就知道"。

**另注：一处与你读数的冲突（必须先对齐）**：你说 `residuals_in_the_closure.cml:35` 因新类型律而报 `'(float) => (float, float)' cannot be called with argument types: <>(float, float, float)`、`split` 12/17。**在 `639619f95` 上它 PASS，`split` 是 13/17**，4 条红是 `split_branch_author_vjp`（`stdlib/vjp.cml:78:47`）。原因：`stdlib/types.cml:112-113` 的 `Transpose<F>` **仍是旧写法** `(..ParamsOf<F>, ct) => (..Tangent<ParamsOf<F>>)`，`origin/fsh-dev = c77d0b518` 也一样。所以那条诊断来自**另一棵树**（已落新类型律的那条）——请给我那个 sha，我按它重扫 ③ 栏；在 `639619f95` 上 ③ 栏 23 条**今天都是绿的**。
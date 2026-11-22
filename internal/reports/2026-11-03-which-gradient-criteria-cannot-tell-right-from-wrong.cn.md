# 哪些梯度判据分不出对错，以及今天改了哪一条

**一句话**：作者 15:15 的裁定是"**判据分辨不了对错——不算判据，先改造**：一条错律也能印出 `6.0`，这条用例转绿就不能证明律是对的"。全库扫过之后，**只断言聚合值（和、积、内积）的梯度判据有 13 条**；今天只改作者点名的那一条（`rules_angle_args.mean_axis_transpose`），其余**只报不改**。

## 1. 判据

一条梯度判据有分辨力，当且仅当**它排除的错答案集合非空且可枚举**：说得出"错律会给出什么"，并且那个值和正确值不同。

- **逐元素**（断言数组本身、断言 `sum(abs(g - expected))`、analytical-vs-numeric 逐元素比对）→ 有分辨力。
- **聚合值**（`sum(g)`、`sum(g*g)`、`sum(g*c)`、单个内积）→ **严格说没有**：把正确梯度的两个元素对调，和不变、平方和不变；把误差加到与 `c` 正交的方向，内积不变。**这些错答案都是"错律也能印出来的同一个数"。**

## 2. 今天改的那一条（作者点名）

`test/cases/modules/autodiff/rules_angle_args_mean_transpose.cml#main`

| | 改之前 | 改之后 |
| --- | --- | --- |
| 打印 | `format<float(sum(back.0))>` | `format<back.0>` |
| 计划断言 | `rules_angle_args mean transpose=6.0` | `rules_angle_args mean transpose=[[0.5, 1.0, 1.5], [0.5, 1.0, 1.5]]` |
| 错律 `(ct) => ct` 给出 | `[1.0, 2.0, 3.0]`，**和也是 6.0** → 旧判据分不出 | 元素不同：首元素 `1.0` 对 `0.5` → 分得出 |
| 今天的状态 | `known_fail`（`skip=arity uri=tensor:mean_axis operands=3`） | **仍然 `known_fail`，同一道墙** |

用例头与计划 `desc` 都写进了"错律给什么、为什么旧判据分不出"。**改造后它仍然是红的**：卡的是 `mean_axis` 三操作数那道路径墙，不是断言；`skip=arity` 报错原样，因此这次改造**没有把红变成绿**，只是把判据换成有分辨力的那一个。

## 3. 无分辨力的判据（13 条，只报不改）

"错律会给出什么"一栏里的**置换**指：把正确梯度的两个元素对调——这是最容易发生的错律形态（规则把轴取错、把下标写反），而 `sum` 对它是瞎的。

| 判据 | 断言的值 | 错律会给出什么、是否与正确值相同 |
| --- | --- | --- |
| `cases/modules/autodiff/pb_statics_gc.cml#main` | `gqw`、`gkw` = `sum(vg.1.q_w)`、`sum(vg.1.k_w)` | 正确梯度的任意置换：**和相同**，分不出 |
| `cases/modules/autodiff/var_tensor_read.cml#main` | `grad` = `sum(vg.1.p0)` | 同上 |
| `cases/modules/autodiff/var_tensor_read_after_call.cml#main` | `grad` = `sum(vg.1.p0)` | 同上 |
| `cases/modules/autodiff/var_tensor_read_after_write.cml#main` | `grad` = `sum(vg.1.p0)` | 同上 |
| `cases/modules/autodiff/var_tensor_read_in_callee.cml#main` | `grad` = `sum(vg.1.p0)` | 同上 |
| `cases/modules/autodiff/var_tensor_local.cml#main` | `grad` = `sum(vg.1.p0)` | 同上 |
| `cases/modules/autodiff/var_array_append.cml#main` | `grad` = `sum(vg.1.p0)` | 同上 |
| `cases/modules/autodiff/slice_grad_negative_step.cml#main` | `println(sum(g))` | 置换或"权重落到相邻槽"：**和相同**（该文件要的是 `[0,0,0,0,2,3]` 这种稀疏梯度，落错槽之和不变） |
| `cases/modules/autodiff/concat_array_part.cml#main` | `sum(g)` 与 `sum(g*g)` | 两个统计量都对置换不变：`[2,3]` 与 `[3,2]` 同和同平方和 |
| `cases/modules/autodiff/grad_data_input.cml#main` | `sum(grad(weighted)<w>(labels))` | 置换：**和相同** |
| `cases/modules/autodiff/macro_round_lifetime_gc.cml#main` | `sum(df<a>())`、`sum(dn<a>())`、`sum(dk<a>())`、`sum(dw<…>())` | 置换：**和相同**；三条导数同为 0 的情形下任何错律都可能给 0 |
| `cases/modules/autodiff/slice_open_end.cml#main` | `dot = sum(g*c)`、`sq = sum(g*g)`、`sum(g)` | 对置换：`sum`/`sq` 不变；`dot` 在 `c` 各分量不同时会变，所以它比裸和强——但仍是一个标量，把误差加到与 `c` 正交的方向就同值 |
| `cases/compile/transforms/jvp_vjp_array_product_cross.cml#main` | 两个内积 `6.0`、`6.0` | 是伴随性恒等式；任何在所有种子上与正确伴随同内积的错律都过。**但对 `(ct) => ct` 能分辨**：那会给 `2.0` 而不是 `6.0` |

## 4. 分辨力弱或需要当场的读数才能判断（4 条，待查）

| 判据 | 为什么待查 |
| --- | --- |
| `cases/modules/autodiff/discrete_zero.cml#main` | 断言的是 `sum(g1.weights)` 一族，**看来是"离散量梯度为零"的检查**：零的和对任意错律都成立，只有非零才红；但 `+1, -1` 也同为零，所以严格说也只是"没有整体泄漏"，不是"每个位置都是零" |
| `cases/modules/autodiff/gather_discrete_index_zero.cml#main` | 同上（`sum(g.weights)`、`sum(f32(g.labels))`） |
| `cases/modules/autodiff/grad_tuple_discrete_member.cml#main` | `sum(gp)`、以及离散成员的零；需要逐元素才能说清"整段梯度对" |
| `cases/modules/autodiff/grad_tuple_read.cml#main` | `sum(vg.1.p0)`、`sum(d.p0)` 与另一个 `sum(vg.1.p0)` 并列，五行里只有和 |

## 5. 有分辨力的判据（机制清单，不是逐条）

| 机制 | 判据（`file#Symbol` 或计划条目） |
| --- | --- |
| 断言整个输出（`expect_stdout` 全等） | `cases/compile/transforms/library_grad_is_the_composition.cml`、`cases/modules/autodiff/grad_closure.cml` |
| 断言梯度数组本身 | `grad_array_sum`、`grad_array_product`、`grad_array_no_int`、`grad_array_with_int`、`grad_of_an_array_parameter`、`grad_array_one_element`、`grad_array_two_elements_first`、`grad_array_int_parameter_first` |
| 断言 `sum(abs(g - expected))`（逐元素误差） | `slice_grad`、`f64_broadcast_grad`、`f64_matmul_grad_width`、`grad_through_as`、`loop_grad_iterable_tensors`（`grad_gap` 是逐元素差） |
| analytical-vs-numeric，逐元素比对并计数 | `tensor_grad_check`（21 项）、`nn_grad_check`、`fused_grad_check`（每个参数一行 `ok`）、`grad_structures`（96 项） |
| 逐元素期望值 + 分布检查 | `first_step_tensor`、`first_step_tensor_vjp`、`matmul_transpose_law`、`matmul_transpose_rank_three`、`matmul_transpose_vector_lhs`、`matmul_transpose_vector_rhs`、`transpose_array_product.answers_both_operands`、`transpose_product_fixed_shape.matches_an_open_rank` |
| 同一律的两个读数并排（线性性/加倍） | `transpose_many_parameters.is_linear_in_the_cotangent`、`split_linear_in_dx` |
| 逐元素（今天改的） | `rules_angle_args_mean_transpose` |

## 6. 一句话的边界

**改动会改变现读数的那一条，单独标出**：以上 13 条**没有一条被我改动**，所以我也不知道其中哪几条"其实逐元素是错的"——今天它们只声称"和对了"。若某条的逐元素值真有错，改成逐元素会让它由绿转红；**那不是失败，是判据开始有分辨力**。按作者的话，改造是一条一条的事，本笔不动。

## 7. 读数（基点 `a17769388`，实时）

| 判据 | 值 |
| --- | --- |
| `test/plans/feat/compile/rules_angle_args.plan.toml` | **6 = 4 过 / 0 红 / 2 known**（改造后仍是 2：`mean_axis_transpose` 的 nvm 与 fvm，墙同为 `skip=arity uri=tensor:mean_axis operands=3`） |
| `feat/compile` | **2215 = 2210 过 / 2 红 / 3 known**（红仍是 `transpose_loop_escape.two_values_of_two_kinds` 与 `.second_state_of_the_same_loop`，与改造前逐条相同） |
| `feat/compile/transforms` | **85 = 82 过 / 2 红 / 1 known**（同样两条红 + `vjp_loop.rounds_are_undone_in_reverse` 的 known） |
| `casts` | 17/17（前一笔的读数，本笔未动） |
| 产品代码 | **零改动**：本笔只改一条 `.cml` 的打印与一条计划期望，另加本报告 |
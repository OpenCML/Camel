# A 类 34 条：现在能迁几条，各自在等什么

对象：`docs/handover/m4-autodiff-case-classes.cn.md` 的 A 类（34 条，只考原语规则）。
判据：库层路线是 `vjp = transpose ∘ split ∘ reinterp<jvp>`（设计稿 4.4）。一条用例“能迁”当且仅当
**它断言的数，今天的库层已经能算出来**。

## 结论：能迁 0 条，要等 34 条

读数（可复现，在 `lane-guard` 上）：

```
camel test/cases/modules/autodiff/scalar_grad.cml transforms::reinterp std::nvm
REINTERP target=sq primitives=1 other=1 graph=51 nodes=1
REINTERP target=f primitives=4 other=3 graph=52 nodes=1
```

`nodes=1` 是 `transforms::reinterp` 的**第一形态**：它给每个函数建一张 transform 自己的图，
图里只有一个端口直通输出（`stdlib/transforms.cml` 头注释：“Building a graph is not transforming one”）。
它不计算切线，也不计算梯度。

而 A 类 34 条断言的**全部是梯度值**（`grad` / `gradient` / `with_grad` 的反向结果），
一条也没有断言“前向切线”。反向需要 `split` + `transpose`（M2、M3），今天两样都还没有。

**所以：现在能迁 0 条；要等 34 条。** 这不是“A 类挑不出只依赖 `reinterp<jvp>` 的”，
而是 A 类里根本没有只依赖前向的用例——前向的那些在 B 类（`control_flow_grad` 等也不过是前向结构）。

## 34 条各自在等什么（分三组）

### 第一组：等 M2 + M3 的直线代码（26 条）

依据：这些用例只走直线代码与调用，`split`/`transpose` 的直线部分一通就能迁。

| 用例 | 一句依据 |
|---|---|
| `scalar_grad.cml` | 直线标量 + helper 调用，无分支无循环 |
| `tensor_grad_check.cml` | 逐条张量规则对有限差分，全是直线 |
| `nn_grad_check.cml` | conv/pooling/embedding/cross-entropy，规则 + 直线 |
| `custom_rule.cml` | `@vjp` 规则在调用点使用，不需要拆函数体 |
| `math_sqrt.cml` | `erf`/`erfc` 的标量规则 |
| `math_erf.cml` | 同上，f32/float 宽度 |
| `grad_through_as.cml` | `as` 转换的邻接，直线 |
| `grad_first_with_parameter.cml` | `with` 参数选择，图结构是直线 |
| `grad_int_zero.cml` | `int` 的零切线，直线 |
| `grad_struct_all_discrete.cml` | 全离散字段结构，逐字段规则 |
| `grad_tuple_discrete_member.cml` | 元组离散成员，逐位置规则 |
| `grad_data_input.cml` | 普通实参是数据，直线 |
| `discrete_zero.cml` | `one_hot`/`argmax`/掩码的零规则 |
| `grad_scalar_width.cml` | f32 宽度 + `:op/dtof`，直线 |
| `f64_broadcast_grad.cml` | 广播的 `sum_to`，直线 |
| `f64_matmul_grad_width.cml` | matmul 的左右梯度规则，直线 |
| `slice_grad.cml` | `t[a:b]` 邻接是补零 scatter，直线 |
| `slice_grad_negative_step.cml` | 负步长/嵌套切片，直线 |
| `slice_open_end.cml` | 开口 end 的邻接，直线 |
| `array_take_grad.cml` | 行 take 的 scatter，直线 |
| `adjoint_reshape_array.cml` | reshape 邻接，直线 |
| `adjoint_concat_array.cml` | concat 邻接，直线 |
| `array_slice_grad.cml` | 数组切片的 range scatter，直线 |
| `adjoint_cast_array.cml` | cast 邻接，直线 |
| `concat_array_part.cml` | concat 数组部分，常量部分跳过，直线 |
| `gather_discrete_index_zero.cml` | gather 不对索引累加，直线 |

### 第二组：等 M3 能对已转置的图再走一次（5 条二阶）

依据：二阶 = 对一阶的图再 `reinterp`+`split`+`transpose` 一次；依赖 `transpose` 的循环形式与残差的再收集。

| 用例 | 一句依据 |
|---|---|
| `second_order_array_add.cml` | 二阶穿过 gather，`zeros_arr` 规则要再转置一次 |
| `second_order_array_zeros.cml` | 二阶穿过运行期索引 gather，`scatter_add_arr` 再转置 |
| `second_order_array_scatter.cml` | 二阶穿过计算边界的切片，`scatter_slice_arr` 再转置 |
| `second_order_array_slice.cml` | 二阶穿过切片规则链 |
| `second_order_array_append_init_blocked.cml` | `append` 邻接拆成 `init_arr`/`tail_arr` 后整体再转置 |

### 第三组：等 M1 的规则读回 + M3（3 条）

依据：它们断言的不是数值，而是“规则/标签按结构读得回来”，需要 M1 收尾时把读回做实，M3 的规则索引与之共用一套。

| 用例 | 一句依据 |
|---|---|
| `transform_rules.cml` | `#jvp`/`#transpose`/`#vjp` 按结构读回，`graph.rule_of` 给出一张变换的图 |
| `vjp_cross_module.cml` | 跨模块 `#vjp` 规则在图里按名取到 |
| `err_vjp_annotation.cml` | 跨模块规则在泛型化简后仍按结构读得到 |

## 迁移纪律（照 72 小时计划的第三条）

- 一笔迁一组，**迁过的用例在新路线上数不变**：贴改动前后同一条断言的读数。
- 不碰 `modules/autodiff` 的接口；C 类（拒绝）最后做；D 类 4 条等作者口径。

## 判据

「一条用例给不出“它等的是哪一个算子”，就还没到迁移那一步——先把它挂到 M1/M2/M3 的某一个验收上。」
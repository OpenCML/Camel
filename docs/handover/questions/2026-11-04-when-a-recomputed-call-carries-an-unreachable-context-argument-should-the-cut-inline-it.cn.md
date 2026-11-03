# 重算的调用带着出口不可达的参数时，`cut` 应当把它们算作可达，还是拒绝？

日期：2026-11-04　状态：**默认"算作可达"（在后半边内联这些参数）；代价见下，未开工**

## 出处（只写有出处的）

- 拒绝原文（本轮实测，`lane-softmax` `f5dbca977` 起）：
  `REINTERP split function=jvp_of_total skip=backward construct=a node whose input was not copied
  uri=- why=an operand of the node is not reachable from the half's exit`。
- 同形拒绝，树上已有：`test/plans/feat/compile/transforms.plan.toml#vjp_loop.rounds_are_undone_in_reverse`
  的 XFAIL 说明——"the refusal is one step before the walk, at the call the backward half recomputes
  (`index_into(dxs, 0)`), whose array argument is not a residual the cut carries"。
- 对照（三例都过）：`jvp_relu` 的掩码、`jvp_exp`、`jvp_sigmoid`/`jvp_tanh` 的切线——它们的重算都只
  依赖输入 `x`，`split` 把重算值当残差带走（`lane-exp` `ab1e7113c` 实测 `residuals=2`、
  `rhs=sum_t,mul_tensor_t,exp_t`；`lane-sigmoid` `b93074169`、`lane-tanh` 同）。
- 施工方判断：下面的默认、代价、以及"先量什么"都是本轮判断，无作者出处。

## 这条墙今天能被写成一句可判的话

**重算的调用是否带着"出口不可达"的参数。** 带上就拒，没带就过——四例读数：

| 后半边重算的调用 | 它的参数 | 结果 |
|---|---|---|
| `exp(x)`（`jvp_exp`） | 只有输入 `x` | 过（`residuals=2`） |
| `sigmoid(x)` / `tanh(x)` | 只有输入 `x` | 过 |
| `where(x > 0, 1, 0)`（`jvp_relu`） | 输入 `x` + 常量 | 过 |
| `softmax<1>(x)`、`sum<1, true>(…)` | 输入 `x` + **`<>` 里的轴与 `keepdims`** | 拒 |
| `index_into(dxs, 0)`（树上 XFAIL） | 数组参数不是残差 | 拒 |

## 问题

`softmax<1>(x)` 里的 `1`、`sum<1, true>` 里的 `1` 与 `true`，是**编译期常量/形状信息**，不是前半边
算出来的值。`cut` 应当把它们当作可达（在后半边里直接内联成常量节点），还是维持"拒"？

## 默认建议：算作可达

三条理由，都由上面的读数支撑：

1. **它们不需要残差。** 常量在后半边重建是免费的；把它们当残差带着走反而会让每个 softmax 的
   pullback 多存一个整数——`residuals=` 这个诊断数应该**不变**。
2. **机制已在。** `exp`/`sigmoid`/`tanh` 证明"后半边重算 + 残差"这条路是通的，缺的只是"重算的调用
   里有一个不可达但可常数化的参数"这一个判断。
3. **不这么做，D1 关不上。** `softmax`、`log_softmax` 这一族在审计表里会永远是"缺一次 cut 的裁定"，
   而缺的原因与它们的律毫无关系——律本身一条就够（`jvp_softmax` 已在）。

## 代价（写清，别写成"应该能行"）

- `split` 要能区分"出口不可达**但可常数化**"的参数与"出口不可达**且不可常数化**"的参数（后者正是
  `vjp_loop` 那一例数组参数），并在后一半里内联前者。**这是 `modules/transforms/split.cpp` 的一处
  pass 改动**，不是律的改动。
- **残差表不能变**：`residuals=` 是几个用例钉住的读数（本轮 `exp` 的 `residuals=2` 就是一条），内联
  常量不得让它 +1；若实现上顺手把常量也算残差，那几条用例要一起改，得先说清为什么。
- **回传端口契约会被碰到**：`modules/transforms/transpose.cpp` 里 `mean_axis` 那条 `skip=arity` 注释
  写着"a law whose right side reads the forward value of an *intermediate* node makes the walk declare a
  port the caller was never told to fill"。内联常量不增加端口，所以这一条**应当不受影响**——但这是
  判断，不是实测；改完必须重跑 `vjp_arities` 那一组用例确认端口数没变。
- **`index_into` 那一例可能顺带被解开，也可能不会**：它的 `0` 也是常量，但它的数组参数不是。改完必须
  重看那条 XFAIL：若一起解开，说明两处是同一堵墙；若仍拒，说明"常量参数"与"重算的数组参数"确实是两堵
  不同的墙，这一页的结论只覆盖前一种。

## 开工前先量一件事（决定这是不是同一件事）

`softmax<1>` 的轴**到底是不是节点的一个操作数**，还是已经烘进了算子实例的 key（`transpose.cpp` 里
`normOperands` 之外的那部分）。若是后者，那么"内联"根本无从谈起——拒的原因就不在这条路上，得先
改这一页的默认。**这一条必须最先量**：跑一次 `skip=backward` 的运行，读一次图里那个节点的操作数表
即可（`std::gir` 或 `REINTERP` 的节点读数都行）。

## 不做的事

- 不为了让 `softmax` 转绿去放宽任何判据（本轮的判据是逐元素、写全数组，见
  `test/cases/compile/transforms/softmax_gradient_stops_at_split.cml` 与邻近的引擎用例）。
- 不在这一页里改 `split`：这一页只定"应当算作可达"与代价，改动是下一件。
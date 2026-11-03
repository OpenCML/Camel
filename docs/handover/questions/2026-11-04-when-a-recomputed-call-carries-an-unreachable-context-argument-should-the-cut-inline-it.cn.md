# 重算的调用带着出口不可达的参数时，`cut` 应当把它们算作可达，还是拒绝？

日期：2026-11-04　状态：**默认"算作可达"（在后半边内联这些参数）；已量：轴是 `context` 边上的 `DATA` 常量节点，默认不变；未开工**

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

## 量过了（2026-11-04 00:35，只读，未改一行代码）

**结论一句话：轴是操作数——具体地，是节点上的一条 `context` 边，另一头是图里一个真实的 `DATA` 常量
节点；它不在算子实例 key 里，也不是节点属性。**

三样原文（本机 `camel.exe probe.cml std::jgir`，`CAMEL_HOME=D:\Projects\Camel\out\latest`）：

1. **`softmax<1>(x)` 在 `jgir` 里**（`func total(x) { sum(softmax<1>(x)) }`）：

   ```
   g2n0 PORT x out=float[..]
   g2n1 DATA 1 out=int
   g2n2 OPER tensor:softmax out=float[..]
   g2n3 OPER tensor:sum out=float
     normal  g2n0 -> g2n2 [0]     <- x 是普通操作数
     context g2n1 -> g2n2 [0]     <- 轴是 context 边，另一端是一个 DATA int 节点
   ```

   节点对象只有 `id/kind/label/origin/outputType/roles/source`，**没有**存轴的属性。

2. **实例 key 里没有轴**：同一张图里 `softmax<0>` 与 `softmax<1>` 的 `label` 都是 `tensor:softmax`
   （`g2n2` 与 `g2n4`，各自挂一条 `context` 边）；`sum<0, true>` 的 label 是 `tensor:sum_axis`，挂
   **两条** `context` 边（`DATA 0` 与 `DATA true`）。所以轴与 `keepdims` 都在操作数表里，key 里只是
   算子名。

3. **`jvp_softmax` 今天怎么拿到它**：不绑，**写字面量**——左值是 `softmax<1>(x)`，轴 `1` 直接写在
   模式里，所以这条律只匹配轴 1 的那一种实例（`softmax<0>` 至今没有任何前向律，原因在此，不是缺陷
   而是这条写法）。与之配套，`modules/transforms/jvp.cpp` 在复制/提升一个调用时显式复制 `<>`
   参数（`function.withInputsOf(ref)` → `draft.setWithInputs`），注释里写清了不复制会怎样：
   "`softmax<1>(x)` copied without its axis is `softmax(x)` … That is a wrong number, not a missing
   number."

**默认不变**：原默认就是"算作可达、在后半边内联"，这三样读数**支持**它——轴不是 key 的一部分，
所以不存在"key 改了律就对不上"的危险；而且它比原估计便宜：轴不是要凭空常数化的值，**它本来就是图里
的一个节点**，`split` 要做的只是把这条 `context` 边的生产者一起复制过去，与 `jvp.cpp` 已有的写法
同形。页里若改成"`split` 无法内联一个不在操作数表里的参数"就是错的，**没有那样改**。

**但这条读数把问题挪了一格（下一步的量）**：拒绝句里是 `uri=-`——
`REINTERP split function=jvp_of_total skip=backward construct=a node whose input was not copied
uri=- why=an operand of the node is not reachable from the half's exit`——**没有算子名的节点**说明被拒
的不是 `tensor:softmax` 本身，而是它上游某个非算子节点（`DATA` 没有输入，所以更像 `CAST`/`PORT` 或
别的读法）。而 `split.cpp` 的 `halfInputsOf` 走 `dataInputsOf`，**普通与 context 输入都收**，并且有
一段专门重写常量的分支（"A constant has no producer, so either half can write it again"，
`writeConstant`）。所以真正要查的是：**那条常量能不能被 `writeConstant` 重写**——不能，就是这堵墙的
实际卡点；能，则卡点在别处，得再读一层。这一条是一行 `grep` 加一次带 id 的 `REINTERP` 就能定的，
不动代码。

## 不做的事

- 不为了让 `softmax` 转绿去放宽任何判据（本轮的判据是逐元素、写全数组，见
  `test/cases/compile/transforms/softmax_gradient_stops_at_split.cml` 与邻近的引擎用例）。
- 不在这一页里改 `split`：这一页只定"应当算作可达"与代价，改动是下一件。
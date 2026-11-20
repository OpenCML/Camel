# 当一条裁定把成功行读成了失败原因？

**日期**：2026-11-20
**提出者**：账页车道（`lane-qpage`，基线 `origin/develop` = `5f115a90d`）
**状态**：等决策者**一次更正**（不是要一份新裁定）。**默认建议：按实测走——补 `add` / `subtract` 的 `*_stated` 拼写（前向把两个形状当操作数交过去，反向读那两个列表）；不改裁定页原文（它是历史记录），由本页作为它的更正附注；若决策者仍认为 `mul_tangent` 缺 law，请指认「哪条 walk 路径没有建出转置」与它的读数。** 默认建议在 §6，要决策者回答的那一句在 §7。
**来源与限制**：本页**没有构建、没有跑任何计划、没有跑任何用例**。页里的东西分三类，逐条标明出处：**（甲）转述**——读数车道在 `28c3cad1a` 的未改树上量到、本页只转述的读数（带树）；**（乙）提交原文**——`git show` 里逐字抄的律、提交信息；**（丙）源码原文**——本页自己读的 `.cml` / `.cpp` / `.plan.toml` 行。**本页没有一处数是我自己跑出来的。**

## 1. 量到的是什么

裁定页（`docs/handover/reviews/2026-11-20-the-second-reading-and-what-is-left.cn.md`，第 1 张表第 1 行）把下面两条拼在一格里，并判「`mul_tangent` 没有转置规则」：

> | `showcase_s2_first_step_gradient / first_step.tensor_vjp_answers_the_step.nvm` 等 **3 条** | `stdlib/vjp.cml:137:13 [Error]: transforms: transpose has no transpose for this function: the ru...` / `TRANSPOSE function=autodiff::mul_tangent<float> graph=509 type=(float, float, float) => (float, float) nodes=7 arms=0 rhs=add_t,mul_t` | **真缺口**：`mul_tangent` 没有转置规则，matmul_t 的梯度路径跑不起来。三条都是它 |

同一份日志（**甲**：量自 `28c3cad1a` 的未改树，`feat/showcase_s2` = **total 10 / pass 7 / fail 3**）里，那两行是**两条不同的行，而且被当成原因的那一条不是失败的那一条**：

**A —— 裁定页引的那行（`function=mul_tangent`）**

```
TRANSPOSE function=autodiff::mul_tangent<float> graph=509 type=(float, float, float) => (float, float) nodes=7 arms=0 rhs=add_t,mul_t
```

**B —— 紧接着的下一行（`function=split_of_jvp_of_step_plain_backward`）**

```
TRANSPOSE function=split_of_jvp_of_step_plain_backward skip=law-reads-a-value uri=tensor:subtract position=0 operand-types=float[...],float[2, 2] reason=the rule's right side reads a value the caller does not carry
```

**这一页的全部争点就在这两行的形状上**：

| 行 | `skip=` | `rhs=` | `function=` | 这一行是 |
| --- | --- | --- | --- | --- |
| **A** | 无 | `add_t,mul_t` | `autodiff::mul_tangent<float>` | **建成**（右半边建出来，用了两条律） |
| **B** | `law-reads-a-value` | 无 | `split_of_jvp_of_step_plain_backward` | **拒绝**（在 `tensor:subtract` 上，`position=0`） |

裁定页截断的那句「the ru...」接着的是 **B 的 `reason=`**（也是用户句的首句）：`the rule's right side reads a value…`，**不是 `mul_tangent`**。而「transpose has no transpose for this function」是**每一次**拒绝都用的前缀，它不指认被转置的是哪个函数——指认函数的是同一行里的 `function=`，而在 B 里那个名字是 `split_of_jvp_of_step_plain_backward`。

## 2. 为什么 A 是成功行

**（一）这两个字段在源码里互斥。** `rhs=` 由 `modules/transforms/transpose.cpp` 在右半边**建成之后**拼出（`usedRules` 连成 `add_t,mul_t` 这种形式），紧接一句 `return true`；右侧空时它印 `rhs=-`，仍然算「建成了」。**每一次**拒绝都走同一个 lambda，印的是 `TRANSPOSE function=… skip=<词>`，词由 `modules/transforms/transpose.cpp#transposeRefusalWord` 从原因表里取（`law-reads-a-value` 是 `TransposeRefusalCause::LawReadsAValue` 的拼写，见 `modules/transforms/transpose.h#LawReadsAValue`）。⇒ **一行不可能同时有 `rhs=` 和 `skip=`；有 `rhs=` 的行就是建成的那一行。**

**（二）A 自己的字段说的就是「建出来了」。** `graph=509` 是这次建出的成员；`nodes=7` 是它的节点数；`arms=0` 说这一片没有分支区；`type=(float, float, float) => (float, float)` 说这个成员**收三个值、答两个余切**；`rhs=add_t,mul_t` 是它右半边用到的律。用例自己的注释把这三个参数两个答案逐字写成 `(x, w, ct) => (t̄dx, t̄dw)`（`test/cases/transforms/split/split_branch_author_vjp.cml`）。

**（三）正面对照：这一行被一条绿计划按原样钉住。** `test/plans/feat/transforms/split.plan.toml#split_branch_author_vjp` 的期望里就有 `TRANSPOSE function=autodiff::mul_tangent<float>` 与 `type=(float, float, float) => (float, float)`，并且读 `author vjp value=4.0 dfdx=4.0` / `author vjp value=-6.0 dfdx=3.0`，四个后端（nvm / fvm / jit / prl）一致。那条计划所在的 `feat/transforms/split` 是 **30/30/0**（**甲**，同一次读数；条目数×后端数=30 一节本页按 `split.plan.toml` 数出，**丙**）。⇒ **一条被绿计划钉住的 `TRANSPOSE` 行，正是「转置建出来了」的读数**，而不是「缺一条律」。

**（四）「规则文件里没有 `mul_tangent` 的 law」是真的，但它不等于缺转置。** `stdlib/rulesets/transpose.cml` 里确实没有一条 `mul_tangent_t`。walk 不需要它：一次规则展开在它造出的调用节点上记下哪些实参是切线（`GCNode::tangentArgs`），转置按这个参与集把 `dx` / `dw` 当「答案要为之答余切的位置」、把 `x` / `w` 当端口，于是建出 `(x, w, ct) => (t̄dx, t̄dw)`——这正是 A 的 `type=`。**（丙）** 同一段在 `split_branch_author_vjp` 的用例注释里也写着（它记的四条里第 4 条，讲 `tangentArgs` 的那一条）。

**（五）B 这个字段只可能出自今天的树。** `operand-types=` 是 `2b446e307` 才加的（**乙**，它的提交信息逐字写：「Both sites that refuse with `law-reads-a-value` now print the operands' own types (`operand-types=`)」）。A 里没有这个字段，B 里有——这与「两行都出自含这一笔的树」一致。

## 3. 真红归因到今天的哪两笔

| 提交 | 时刻 | 主题 | 对 `subtract` 做了什么（**乙**，`git show` 逐字） |
| --- | --- | --- | --- |
| `2e78f29c0` | 2026-11-20 09:42:35 +0800 | `transform: an answer is checked against the operand it answers for, and the two broadcast laws reduce` | `subtract_t` 从 `(ct, -ct)` 改成 `(sum_to(ct, shape(x)), sum_to(-ct, shape(w)))` |
| `2b446e307` | 2026-11-20 10:01:57 +0800 | `transform: a sum of two values of one shape is not a sum that broadcasts` | 同型拆回 `(ct, -ct)`（`subtract_t`），混合型另立 `subtract_broadcast_t`，`when is_float::<T>() && !is_same::<X, W>()`，右端仍是那两个 `sum_to(..., shape(...))` |

**这条红怎么来的，一步一步都在树里。**

1. 红的用例是 `test/cases/compile/transforms/first_step_tensor_vjp.cml`，它的步是 `let e = matmul(x, w) - t`，`x = [[1.0, 2.0], [3.0, 4.0]] as float[2, 2]`、`t = [[0.0, 0.0], [1.0, 1.0]] as float[2, 2]`（**丙**）。
2. 前向律 `stdlib/rulesets/jvp.cml#jvp_subtract` 的右端是 `dx - dw`（**丙**），所以线性半边里就有一次 `tensor:subtract`。
3. 那个减法的第一个操作数是 `d(matmul)`，它来自 `stdlib/rulesets/jvp.cml#jvp_matmul` 的 `matmul_stated(...)`；`stdlib/tensor.cml#matmul_stated` 声明的结果类型是 `T[...]`，即 **`float[...]`（开放秩）**。第二个操作数落在 `t` 上，其型是 `float[2, 2]`（B 的 `operand-types` 第二项就是它）。
4. 两个操作数**不同型** ⇒ `stdlib/types.cml#is_same` 那一问为假 ⇒ 落到 `stdlib/rulesets/transpose.cml#subtract_broadcast_t`，它的右端是 `(sum_to(ct, shape(x)), sum_to(-ct, shape(w)))`。
5. `shape(x)` 要读 `x` 的**值**，而这一侧 `x` 是切线、类型 `float[...]` 又没写 extents ⇒ 具名拒绝，**B 的每个字段都对上了**，包括 `position=0` 是 matmul 那一侧。

⇒ **改之前它读不到值**：旧的 `subtract_t` 右端是 `(ct, -ct)`，**不提 `shape(..)`、一个值也不读**，所以这一步**不可能**停在 `law-reads-a-value`——这个原因的名字就是「右端读了一个调用者不带的值」。⇒ **这是一次今天自己造的回归**：红不是缺一条律，因为缺的那条律（`mul_tangent_t`）与这一步无关（§2 的 A 已证明它建得出来）。

**而且它不是失手，是这两笔的声明行为。** `2b446e307` 的提交信息逐字写着：**「two operands of different types whose declarations state no extents (`float[...]` with `float[3]`) still stop the step by name, because an answer that may not be the operand's shape is refused and not guessed」**（**乙**）。两笔都按自己写的在做；它们没算到的是：**一个训练步的 `matmul - t` 正好就是这个型对**。

**一笔旁证，说明为什么只有这一侧还红。** `2e78f29c0` 自己的提交信息记着它的代价：**「nine entries of `test/plans/feat/compile/transforms.plan.toml` that pass on `origin/develop` now refuse at `skip=law-reads-a-value uri=tensor:add position=0`」**（**乙**）。`2b446e307` 把这九条接住了，因为它写明「every one of them adds two operands of the type `float[...]`」（**乙**）——**同型**，同型律 `(ct, ct)` 不需要形状。`subtract` 这一侧是 `float[...]` 与 `float[2, 2]`，**混合型**，于是留在原地。

## 4. 要补什么

**模板已经在树里，而且它把设计说死了。** `stdlib/rulesets/transpose.cml#mul_stated_t` 的注释逐字写着：**「the operand whose cotangent this is has no value on this side (design principles 4.3), and its type is `float[...]`, which states no extents. The forward half computes both lists, where both operands are values (`stdlib/rulesets/jvp.cml#jvp_mul_tensor`)」**（**丙**）。⇒ 形状要**当操作数交过去**，不在反向侧读它。这正是 `mul_stated` / `matmul_stated` 这一族拼写存在的理由。

所以这一页要的手艺是**同一招，用到 `add` / `subtract` 上**：

- `subtract_stated(x, w, ax, aw)` 与 `add_stated(x, w, ax, aw)`，四参数，照 `stdlib/tensor.cml#mul_stated` 的形（`ax` = `shape(x)`，`aw` = `shape(w)`，值是那个减法/加法本身，两个列表是校验与交付）；
- 前向律 `jvp_subtract_stated` / `jvp_add_stated`：右端写成 `subtract_stated(dx, dw, shape(x), shape(w))`——**前向两个操作数都是值**，所以 `shape` 在这里读得到（这与 `jvp_matmul` 交出 `shape(a)`、`shape(b)` 是同一个动作）；
- 反向律 `sub_stated_t` / `add_stated_t`：读 `ax` / `aw` 而不是 `shape(x)` / `shape(w)`，右端 `(sum_to(ct, ax), sum_to(-ct, aw))`，照 `mul_stated_t` 的形状排。

**这不是 `.cml`-only，要动 C++ 三处**（**丙**）：

1. `modules/tensor/ops/elementwise_ops.cpp`：`add` / `subtract` 今天是 `binaryDef<BinaryOp::Add>` / `binaryDef<BinaryOp::Sub>`，**两个操作数**；`mul_stated` 是手写的四参数 `OpDef`（`Params` 两 `TensorOrScalar` + 两 `IntArray`，`.infer = binaryInfer(...)`，`.kernel = &mulStatedKernel`）。新增两个拼写就是新增两个 `OpDef` 与两个 kernel。
2. `modules/tensor/module.cpp#kDeclaredOperators`：结果类型要由声明说出来就得进这张表——`mul_stated`、`matmul_stated`、`sum_to`、`sum_axis_stated` 都在里面，`add_stated` / `subtract_stated` 要照着加一行。
3. `stdlib/tensor.cml` 的声明与 `@example`（`mul_stated` 的声明就在那里，四参数带文档）。

**不要把那两笔撤回去。** 撤回就把它们要修的那处错值放回来——律自己的注释记着读数（`[[1, 2, 3], [4, 5, 6]]` where the answer is `[5, 7, 9]`，measured 2026-11-20，**乙**）。修法是**让形状随前向走**，不是让反向再猜一次。

## 5. 这一页要的不是裁定，是一次更正

三句话，逐条落到裁定第 1 条上：

1. **数没错**：`showcase_s2` 的三条红是真的——裁定自己那次跑批（`8fab0d3e1`）报 3 条，`28c3cad1a` 的未改树也报 3 条（**甲**：total 10 / pass 7 / fail 3）。
2. **归因错了**：它把 A（`mul_tangent` 的**成功行**）当成了失败原因，而失败的是 B（`split_of_jvp_of_step_plain_backward` 在 `tensor:subtract` 上的拒绝）。「the ru...」是两个函数共用的那句开头（用户句 `the rule's right side reads a value …`），它不指认 `mul_tangent`。
3. **「规则文件里没有 `mul_tangent` 的 law」是真的，但它不等于缺转置**：`transpose(mul_tangent)` 有，`feat/transforms/split` 的 30/30/0 把它钉在四个后端上（**甲**）。

⇒ **所以「四条实的」的第一条应改写为**：

> **「`subtract_t` / `subtract_broadcast_t` 的读值改动带来的回归：`matmul - t` 的线性半边在 `tensor:subtract` 上停在 `law-reads-a-value`」**——不是「`mul_tangent` 缺转置」。

**一处必须说清的边角**：那一格写「`…_step.nvm` 等 **3 条**」，而那条计划自己写的是 `backends = ["nvm"]`（**丙**），所以 `.nvm` 是**一条**用例，另外两条是这格**没有点名**的别的条目。⇒ 「三条都是它」这句，同时也给两个没点名的红指定了原因。

**裁定页原文不改**：它是 `8fab0d3e1` 在 13:31 那一刻的读数记录，改它等于改历史。本页是它的**更正附注**。

## 6. 默认建议（写死，不许含糊）

1. **按实测走：补 `add_stated` / `subtract_stated` 与它们的前向律、反向律**（§4），让 `matmul - t` 这一步的形状随前向走。这是本页建议开工的第一件事，也是把「四条实的」第一条真正清掉的那件事。
2. **不改裁定页原文**（`docs/handover/reviews/2026-11-20-the-second-reading-and-what-is-left.cn.md`）：它是历史记录，由本页作为它的更正附注；「四条实的」第一条在后续读数里按 §5 改写。
3. **若决策者仍认为 `mul_tangent` 缺 law：请指认「哪条 walk 路径没有建出转置」与它的读数。** 今天手上的两句是相反方向的读数——A 有 `rhs=add_t,mul_t`、`nodes=7`、`arms=0`（建成了），B 的 `function=` 是 `split_of_jvp_of_step_plain_backward`（拒的是另一个函数）。**没有一句把拒绝归到 `mul_tangent` 上。**

## 7. 要决策者回答的一句

裁定第 1 条的归因按 §5 改成「`subtract_t` / `subtract_broadcast_t` 的读值改动带来的回归」、并按 §4 补 `add` / `subtract` 的 `*_stated`（裁定页原文不动，本页作更正附注）——认不认？

## 附：本页每个读数指回哪一次测量

| 读数 | 出处 | 类 |
| --- | --- | --- |
| A 行、B 行（两条 `TRANSPOSE` 原文） | 读数车道在 `28c3cad1a` 的未改树上跑 `feat/showcase_s2` | 甲（转述） |
| `feat/showcase_s2` = total 10 / pass 7 / fail 3 | 同上 | 甲（转述） |
| `feat/transforms/split` = 30/30/0 | 同上 | 甲（转述） |
| `split.plan.toml` 条目×后端 = 30 | 本页按 `test/plans/feat/transforms/split.plan.toml` 数 | 丙 |
| 两笔提交的时刻、主题、`subtract_t` 的前后右端、`operand-types=` 的引入、`nine entries ... uri=tensor:add`、`every one of them adds two operands of the type float[...]`、`[[1, 2, 3], [4, 5, 6]]` 那条注释 | `git show 2e78f29c0`、`git show 2b446e307` | 乙 |
| `rhs=` 只在成功路径、`skip=` 只在拒绝 | `modules/transforms/transpose.cpp`、`modules/transforms/transpose.h#LawReadsAValue` | 丙 |
| `mul_tangent` 的体是 `dx * w + x * dw`；`jvp_mul` 与 `jvp_subtract` 的右端 | `stdlib/autodiff.cml#mul_tangent`、`stdlib/rulesets/jvp.cml#jvp_mul`、`stdlib/rulesets/jvp.cml#jvp_subtract` | 丙 |
| `mul_stated` 四参数、`add`/`subtract` 是两参数 `binaryDef` | `modules/tensor/ops/elementwise_ops.cpp`、`modules/tensor/module.cpp#kDeclaredOperators` | 丙 |
| 用例的步是 `let e = matmul(x, w) - t`，`x` 与 `t` 都是 `float[2, 2]` | `test/cases/compile/transforms/first_step_tensor_vjp.cml` | 丙 |
| `first_step.tensor_vjp_answers_the_step` 的 `backends = ["nvm"]` | `test/plans/feat/showcase_s2.plan.toml#first_step.tensor_vjp_answers_the_step` | 丙 |
| `(x, w, ct) => (t̄dx, t̄dw)` 与四个后端的 4.0 / 3.0 | `test/cases/transforms/split/split_branch_author_vjp.cml`、`test/plans/feat/transforms/split.plan.toml#split_branch_author_vjp` | 丙 |
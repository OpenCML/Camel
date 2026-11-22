# 从计划注释里搬出来的四处读数（2026-11-04）

**为什么有这一页。** 计划文件的注释与 `desc` 里曾经写着「Measured 2026-11-04 on host …（load …）：
`value= 3.0` …」这样的句子。那句话本身不假，但它是**一个只在某一天成立的测量**，装在一条**每天都会被读、
而且会被当成“不许动”的理由**的注释里：读到它的人没有日期以外的线索，而它一旦过期就会变成谎言的形状。
作者 2026-11-04 的规矩是：**注释只写“这一条钉住什么”，读数搬进页，页里带宿主、时间、load 与树。**
这一页就是那四处的落脚点——**读数搬家，不蒸发**：下面每一段先抄原记录，再写今天在本树上的复读。

页里的读数**只在这页成立**；引用它们时请连宿主与 load 一起引。

## 1 两个后端的 `value` / `grad`，与切出来的残差（搬自 `transforms.plan.toml`）

- 原地：`test/plans/feat/compile/transforms.plan.toml`，`vjp_loop.rounds_are_undone_in_reverse` 上方的注释块。
- 原记录（注释原文）：**Measured 2026-11-04 on host 88zjwei（1 m load 20.8–21.0，一台有负载的宿主）**：
  `value= 3.0` 与 `grad= [1.0, 1.0]`，在 `std::nvm` 与 `std::fvm` 上都一样；另有读数
  `REINTERP split function=jvp_of_g … residuals=2 loop=1 tape=5 invariants=0 after=1`；
  并记着 `transpose_loop_escape.two_values_of_two_kinds` 与 `second_state_of_the_same_loop`
  两条仍是拒绝（一条是“拿着没有余切的参数”，一条是切不下来的两状态循环）。
- 今天复读（**宿主 `88zjwei`，2026-11-04 08:40 +08:00（00:40Z），load 24.25/18.96/18.55，树 `841cd9d1f`**；
  代码与 `a122cfd6e` 相同，后者只改计划文字）：

  ```
  [nvm] value= 3.0      [nvm] grad= [1.0, 1.0]
  [fvm] value= 3.0      [fvm] grad= [1.0, 1.0]
  REINTERP split function=jvp_of_g forward=368 backward=369 type=(float[]) => (float, (float[]) => float pure)
    pure residuals=2 loop=1 tape=5 invariants=0 after=1
  ```

  三条读数与 11-04 的原记录一致（`invariants=0 after=1` 就是“残差在循环建好之后再取”那次改动的证据）。
- 注释里现在留的：这条用例钉的是什么（两轮循环的复合映射 `M = M_10 * M_1`、反向要按 `M_1^T` 再 `M_10^T`
  的顺序走、两个数 `(1.0, 1.0)` 与 `(1.0, 10.0)` 把两条走法分开）。

## 2 `pow` 在库那条路上停在哪一步（搬自 `transforms.plan.toml` 的 `desc`）

- 原地：`test/plans/feat/compile/transforms.plan.toml`，`pow_gradient_refused_by_the_library` 的 `desc`。
- 原记录：**Measured 2026-11-04**：这次走查比 `skip=rule uri=math:pow_d reason=unbound` 那一次**早停一步**——
  旧读数从闭包里取不到的那个值不再被问，于是走查越过乘积，停在 `pow` 实现里的那个分支上；
  **判决没变**（缺的反向律仍然缺），变的是这一行点到的构造。
- 今天复读（**`88zjwei`，2026-11-04 08:40–08:42 +08:00，load 24.25/18.96/18.55，树 `841cd9d1f`**）：

  ```
  TRANSPOSE function=rulesets.jvp::pow_tangent<float> skip=condition
  TRANSPOSE function=split_of_jvp_of_p_backward skip=call-no-transpose node=6 callee=rulesets.jvp::pow_tangent<float>
  ```

  与计划的 `expect_stderr_contains` 逐字一致；`desc` 里现在留的是判决与它钉的两条流（stdout 的整句、
  stderr 的两条 needle），以及“哪天写了 `pow_t`，这条会红并说出替代它的是什么”。

## 3 一个被路径子串弄红的条目的来历（搬自 `entry_args.plan.toml` 的注释）

- 原地：`test/plans/feat/compile/entry_args.plan.toml`，`entry_args.rgir_open_parameters` 的注释。
- 原记录：**Measured 2026-11-04 on the tree `arityrefuse`**：同一棵树在这个名字下 `feat/compile` 读 3 红，
  换个名字读 2 红——因为 needle 曾是裸词 `arity`，而 rgir 的 dump 会打印
  `srcfile=".../camel-work/<worktree>/..."`，工作树名字里带 `arity` 就把这条弄红，dump 本身是对的。
- **这一条不能在本树复读**：它说的是**另一个工作树名字下的那次运行**，是历史观察，不是可重复的读数。
  所以这里只搬家、不改写、不假装它是今天的数。
- 注释里现在留的：为什么 needle 必须是“语义字符串”而不是裸词（带上面的路径机制），以及这条实际禁止的
  三样东西（`[Error]` / `error[RuntimeDiag::` 两种诊断渲染、`arity mismatch` 的 norm-arity 文字、
  `runtime arity` 这个注册表措辞）。

## 4 四个算子的律改成“点形状”，与那次 `git grep`（搬自 `rules_angle_args.plan.toml` 的两条 note）

- 原地：`test/plans/feat/compile/rules_angle_args.plan.toml`，`rules_angle_args.mean_axis_transpose` 与
  `transpose_needs_parameter_shape` 的 `known_fail_note`。
- **动作（留在注释里）**：`sumshape` 的那次改写把 `sum_t` / `take_t` / `mean_axis_t` 换成了 `*_stated`
  的 twins——它们只服务 `jvp` 写出的那次调用，读的是前一半**点名的** extents，**从不读操作数的形状**。
  两个用例走的都是**直接取值那条路**（自己写 `sum(xs)` 再 `transpose`），那条路因此缺一个单操作数 `sum` 的律。
- **测量（搬到这里）**：
  - `git grep 'shape(xs)\|ones_like' -- stdlib/rulesets/transpose.cml` → **无输出（exit 1）**；
    复读：`88zjwei`，2026-11-04 08:40 +08:00，load 24.25/18.96/18.55，树 `841cd9d1f`。
  - **Measured 2026-11-04**：两条用例都停在 `TRANSPOSE function=loss_mean skip=rule uri=tensor:sum reason=unbound`
    ——就在这条用例要问的形状问题**前一步**。今天复读（同上宿主与时刻，load 26.47/19.61/18.76）：
    两个用例逐字相同的这一行，并给出整句
    `a step of `loss_mean` (node 6, operator `tensor:sum`) is one the transpose family `rulesets.transpose`
    cannot turn around: the walk found no rule key for that operator…`。
- 两条 note 里现在留的：这条用例钉的是什么（直接取值路径需要单操作数 `sum` 的律）、为什么今天是红
  （没有这个律），以及“恢复取值那条路需要一个被交给 extents 的律，那是一个决定，不是一次合并”。

## 5 复读这页的读数要带的东西

| 项 | 值 |
| --- | --- |
| 宿主 | `88zjwei`（32 核，Ubuntu） |
| 时刻 | 2026-11-04 08:40–08:42 +08:00（00:40–00:42Z） |
| load（1m/5m/15m） | 24.25/18.96/18.55；26.47/19.61/18.76（两次，宿主不静） |
| 树 | `841cd9d1f`（代码与 `a122cfd6e` 相同：后者只改计划文字） |
| 安装 | `out/latest`（`camel-remote.ps1` 建的，非本地构建） |

**再读数时**：同样一次一个用例、`stdlib` 与安装都不变，把宿主、时刻、load 一起记下来；**只把结论写回
计划注释，把数字留在这页**。第 3 节那条是唯一一处不可复读的记录，请原样引用它的出处。
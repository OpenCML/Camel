# 求导格（`task-446`）交接 —— 已数出的清单、四处落点、下一步只做一件

交回原因：本会话上下文已耗尽，**要换人**。枝已建好、清单已数出、覆盖面的入口已定位；**没有写任何规则、没有加任何用例、没有改代码**。

## 1. 枝与尖

```
$ cd D:\Projects\Camel-wt\perf
$ git fetch origin integration        → 08612081ba backlog: the flag bit is gone on both sides, …
$ git switch -c lane/deriv-rules origin/integration
Switched to a new branch 'lane/deriv-rules'
$ git log -1 --format='%h %s'
08612081ba backlog: the flag bit is gone on both sides, and canonical is the only document that carries it
$ git status --porcelain
(空)
```

- 枝：**`lane/deriv-rules @ 08612081ba`**，从 `origin/integration` 尖端切出，`dirty 0`，未提交、未 push。
- 面板已绑：`oversight_bind(slot=dev, lane=lane/deriv-rules, worktree=D:\Projects\Camel-wt\perf)` → `bound 29e53810-… to dev (lane/deriv-rules)`。

## 2. 那十条规则（已数出，正好 10，不凑）

口径来源：`internal/reviews/questions-2026-10-31-who-consumes-the-ruleset-index.md:20-30` 打印的十个索引键
（`rulesets.jvp +/2 -> jvp_add` … `rulesets.transpose -/1 -> neg_t`），按这十个键在树里逐条找。

| # | 规则名 | `file:line` | 现状 |
|---|---|---|---|
| 1 | `jvp_add` | `src/stdlib/rulesets/jvp.cml:137` | 已实现（`export rule`） |
| 2 | `jvp_sub` | `src/stdlib/rulesets/jvp.cml:142` | 已实现 |
| 3 | `jvp_mul` | `src/stdlib/rulesets/jvp.cml:147` | 已实现 |
| 4 | `jvp_div` | `src/stdlib/rulesets/jvp.cml:152` | 已实现 |
| 5 | `jvp_neg` | `src/stdlib/rulesets/jvp.cml:157` | 已实现 |
| 6 | `add_t` | `src/stdlib/rulesets/transpose.cml:61` | 已实现 |
| 7 | `sub_t` | `src/stdlib/rulesets/transpose.cml:66` | 已实现 |
| 8 | `mul_t` | `src/stdlib/rulesets/transpose.cml:71` | 已实现 |
| 9 | `div_t` | `src/stdlib/rulesets/transpose.cml:76` | 已实现 |
| 10 | `neg_t` | `src/stdlib/rulesets/transpose.cml:81` | 已实现 |

⇒ **这一格不是"从零写规则"，是"接线 + 验数"。**

查询面已有覆盖（所以补用例要谨慎，先查差集）：

- `test/cases/compile/rulesets/rulesets_lookup.py`（`:8`、`:65`、`:70-74` 有各键的 `LOOKUP` 期望行）；
- `test/cases/compile/rulesets/ruleset_index_agrees_with_the_transform.mjs`（索引与遍一致）；
- 同目录另有 `ruleset_index_bodies.cml`、`ruleset_index_only.cml`、`rulesets_missing.cml`、`rulesets_signature_interned.cml`、`rulesets.probe_*.cml` 等。

## 3. `pow` 前向与 `idx_take` 的四处 `file:line`

```
pow 前向（`^`）   src/stdlib/rulesets/jvp.cml:1195   export rule jvp_pow::<T>(x: T, w: T) when is_float::<T>() { x ^ w => func (dx: T, dw: T) { … }
pow 前向（pow_d） src/stdlib/rulesets/jvp.cml:1228   export rule jvp_pow_d(x: float, y: float) { pow_d(x, y) => func (dx: float, dy: float) { … }
idx_take 前向     src/stdlib/rulesets/jvp.cml:1065   export rule jvp_idx_take::<T>(xs: T[...], positions: int[]) { index_take(xs, positions) => … }
                  （`:1042` 的注释标题是 "the forward rule of `:op/idx_take`, the read at many positions"；`:1060` 是 `#op(':op/idx_take')`）
```
`pow` 的转置侧（供对照，不是本格要交的）：`src/stdlib/rulesets/transpose.cml:1320`（`pow_t`）、`:1350`（`pow_tensor_t`）、`:1295`（`pow_d_t`）。

⇒ **三样都"在树里"，不是缺**；本格要做的是"同一程序两条路，两侧数值都打出来对照"。

## 4. 覆盖差集（只报，不动手补）

已经定位到现有 `with_grad` 数值用例的**所在面**：

```
$ git grep -ln 'with_grad' -- test/cases | Measure-Object → 149 个用例
前 12 个（全在 test/cases/compile/transforms/）：
  broadcast_difference_through_vjp.cml        first_step_library.cml
  first_step_tensor.cml                       library_grad_is_the_composition.cml
  library_grad_of_a_with_weight.cml           matmul_transpose_law.cml
  matmul_transpose_vector_lhs.cml             matmul_transpose_vector_lhs_stated_extent.cml
  matmul_transpose_vector_rhs.cml             matmul_transpose_vector_rhs_stated_extent.cml
  scalar_extremum_gradient.cml                stop_gradient_gradient_through_the_engine.cml
$ git grep -ln 'with_grad' -- test/cases/modules/transforms → 0
```

**差集（十条第几条已有"规则实现 vs `with_grad` 数值"用例）这一格没做完** —— 需要逐条判 149 个用例里哪几个真的把 `+ - * / 取负` 的标量梯度与 `with_grad` 对齐了。**这是下一格的第一件只读活**，别凭目录名猜。已确认的事实只有两条：① `test/cases/modules/transforms/` 里 **0** 个 `with_grad` 用例；② 十条规则的**查询面**已有覆盖（第 2 节三处）。

## 5. 下一步只做哪一件

**只做 `pow` 前向 + `idx_take` 的数值对照，然后交回。十条规则的用例先别写**（负责人已撤销"分两轮补十条"那一步：`rulesets_lookup.py` 与 `ruleset_index_agrees_with_the_transform.mjs` 已覆盖查询面，再加十条新用例大概率是重复覆盖、给套件添噪音）。

做法：

1. 同一个程序两条路：`x->std::jvp` 与 `with_grad` **同式**，把两侧数值都打出来、贴原文（不写"一致"两个字）。
2. 容差抄 `test/cases/compile/transforms/` 里既有用例的写法（`f64`/`f32` 各自），不自创。
3. **数值不一致就停手报**：可能是规则错，也可能是 `with_grad` 错，两边都可以怀疑。
4. 不造新工具（造工具是元层，归决策者）；不改 `with_grad` 语义去迁就规则；不碰调度遍与后端。

## 6. 这一格做过/没做

- 做过：建枝、数出十条清单、定位 `pow`/`idx_take` 四处落点、定位 `with_grad` 用例的所在面（149 个，`test/cases/modules/transforms/` 为 0）。
- **没做**：任何代码/规则/用例改动、`pow`/`idx_take` 的数值对照、十条的逐条覆盖判定。
- 树：`lane/deriv-rules @ 08612081ba`，`dirty 0`，未 push。
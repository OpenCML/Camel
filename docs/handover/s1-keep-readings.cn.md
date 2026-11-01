# `:op/keep` 生效前要读的两处（2026-11-01）

作者 05:10 裁定：效应保活改为**数据保活**——凡删掉受约束节点最后一个用处的遍，都要把那个节点经
`:op/keep` 接到删除点的结果上，使它仍在活性函数的数据使用里。这一页只记"读到了什么"，
不写代码。

## 1. 3.3 的新文字

`docs/design-principles.cn.md:160` 现在仍写：

> 完全没有用处的效应不执行（`EffectNotExecuted` 警告）。"用到"在建图时定下，遍不得改变：遍不把
> 效应或可能出故障的计算移进或移出分支与循环，**去掉一个效应的最后一个用处时它由指向后继的顺序边
> 保活（9.7）**。

**与裁定不符**：这句说的是"顺序边保活"，裁定要的是"数据保活（`:op/keep`）"。**文档这半还没改。**
第 161 行起是 3.4，未涉及。

## 2. 9.7 的现有用例

`docs/design-principles.cn.md:641-645`（9.7 图重写与效应）**只有两条原则性叙述**：效应摘要由每次提交
重算并校验；改写不得改变哪些效应执行（纯且不故障才可丢）。**没有逐条用例清单**，也没有一条点名
"靠顺序边保活"的用例。

"9.7 的用例"因此不在 9.7 里，而在别处（`git grep -l 顺序边` 命中 10 个文档与若干计划）。**要逐个确认，
得先把它们定位出来**；这一页不假装已经确认。

## 2.5 `:op/keep` 本身还不存在

`git grep 'op/keep\|:op::keep' -- src include` **零命中**：这个算子还没写。所以"把 fold 改成数据保活"
之前，先要有这个算子本身。

## 2.6 候选用例位置（不是 N，也不是 M）

`git grep -E 'EffectNotExecuted|顺序边' -- test/plans test/cases` 命中 **17 个文件、17 行**，其中用例：
`test/cases/compile/diag_json/dropped_effects.cml`、`diag_options/warning.cml`、
`unused_values/effect_body.cml`、`consistency/partial_application/var_parameter_effect.cml`、
`effects/effect_through_parameter.cml`、`effects/generic_parameter_takes_instantiation.cml`、
`modules/rules/bench.cml`、`modules/tiles/gpu_scope_unused_effect.cml`，以及
`feat/compile/diag_json.plan.toml`(6)、`diag_options.plan.toml`(9)、`effects_syntax.plan.toml`(4)。

**这 17 处里哪几条真的"靠顺序边保活"要逐条读**——这一轮读到定位为止，**`N`、`M` 仍不给**。

## 3. 一句结论

**`:op/keep` 还不存在（`src` 里零命中），用例已定位到 17 处但未逐条判**；所以 `N`、`M` 这一轮仍不给，
先写算子、再逐条判、再改遍。

## 4. 下一步

1. **写算子（四处，一笔只做算子本身 + 一处插入）**：
   `include/camel/core/operator_table.h`（`OperId` 与 `mayBeRecomputed`——**新算子不参与重算**，
   否则 DCE 会把它当"可重算的纯计算"再删一次）→ `src/builtin/executors/builtin.cpp:39-43`
   （`keep(x, v)` 的执行就是返回 `v`）→ `include/camel/core/operator_semantics.h:68` 起的 URI 格式 →
   插入点 `src/passes/opt/generic/rewrite.cpp` 的 `replaceNode`（273/277/313 附近）与
   `generic.cpp:1464` 的 `dceDraft`。
   **提交信息里要写一行"为什么插入点选在 `replaceNode` 而不是别处"**——下一个人读代码时的第一个问题。
2. **验收**：`std::opt` 前后 × 四后端 = **8 行都是 `1`**（程序：`let a = println(1) return if 1 > 2 then a else 0`）；
   **A/B：把插入拿掉 → `1` 必须消失**；两轮全量。
3. **逐条判那 17 处**：每处一行——`文件:行 / 它现在靠什么成立 / 数据保活下还成不成立 /
   不成立就改用 `:op/keep` 后钉什么`，给出 `N`、`M`。
4. 然后才按 `fold` → `dce` → 内联清理，一笔一处地改代码。
5. **9.7 的增补只写本页**（"建议 9.7 增补如下"），**不改 `docs/design-principles.cn.md`**——原则文件是作者的。
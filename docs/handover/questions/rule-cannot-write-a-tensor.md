# 一条规则写不出"匹配形状的零张量"：要不要给重写语言补一个聚合常量？

## 问题

H226 的测量里，逐元素（张量）这一族有三个形状对不上，其中两个不是"库欠一条规则"（第三个是 `tensor_combine`，旧表因浮点权限与 `exclusive()` 条件拒绝、库重写：见 `rule-conditions-and-policy.md`）：

- `tensor_fold`（`(t * 3.0) * 2.0`）：两侧都不动（各 7 节点 / 7 边、`tensor_mul` 2）。规则 `tensor_f32_fold` 带 `#requires<reassociate, no_infs>`（`stdlib/rules.cml:407`），语料里的函数没有 `#[reassociate]` / `#[no_infs]` 标注，所以没有授权；**这是一个权限/调度选择，不是缺规则**（标量 `float_fold` 同理，测量里 `fold_d` 两侧也都不动）。
- `tensor_zero`（`t * 0.0`）：两侧都不动（各 5 节点 / 5 边、`tensor_mul` 1；`main` 之外的 4 张图里 `tensor_double` 是唯一两边都做的）。删掉的 C++ 法表**从来没有**张量零律（`e8c290554` 的表里没有 `x*0` / `x-x`，实测它在这张图上也不动），所以这里**没有丢失任何 C++ 能力**——缺的是库自己的一族：`tensor_f32_*` 有中性元、有 combine/fold/double，唯独没有 `t * 0.0 => <该形状的零张量>`。

缺的那一块是**语言特性**，库自己也是这么写的（`stdlib/rules.cml:351-355`）：

> What a tensor law cannot do is the `x*0`/`x-x` family: an f32 zero *tensor* cannot be written in a rule (the program has no tensor constant of the right shape), and a rule's replacement is what it writes.

也就是说：规则的右侧写什么、图里就出现什么，而规则语言今天写不出"与匹配值同形状的全零张量"（既不能写聚合常量，也没有一条能按 `x` 的形状求值的替换表达式）。**顺带一个文档缺陷**：同一段接着说 "Those laws stay in `algebra.cpp`"——这句在 `a510676bb` 删掉 C++ 法表之后已经不成立，而且那张表从来没有这两条律（无论按哪个方向都不对）。施工方没有改 `stdlib/rules.cml`（属 H230/stdlib-rules 的工作），只在这里报告。

## 选项与代价

- **(A) 什么都不加**：与删掉的 C++ 法表对齐，`t * 0.0` 保持不动。代价：`f32` 张量的零律与整数的 `int_mul_zero` 不对称（文档要说清这是有意的）；`rules.cml:351-355` 那句错话要么留着误导读者、要么由 H230 顺手删掉。收益：不动语言。
- **(B) 给重写语言补"按匹配值形状写常量"的能力**（例如右侧可以写一个由匹配值 `x` 的形状求值的零张量构造，或让右侧能调用一个纯构造子），随后加 `tensor_f32_mul_zero` / `tensor_f32_sub_self`。代价：语言与匹配器的新工作（形状在编译期还是运行期可知、构造子是否算纯、效果/纯性检查怎么算——正好与 H243 第 2 项同一条链）；而且零律在张量上还要按 f32 语义判定（±0、NaN），不是照抄整数律。收益：整数与 f32 的律表对齐，`t * 0.0`、`t - t` 这些形状也能化简。
- **(C) 只在注释里承认现状**：把 `rules.cml:351-355` 改成实话（"这两族在两侧都没有；补它需要语言能写同形状常量"）。代价：零收益于能力，只是不再误导。收益：最便宜，且不影响任何计划。

## 施工方建议

**(C) 立刻做、(A) 作为默认、(B) 只作为语言层面的候选**：测量显示 H226 的"与 C++ 代数 pass 对齐"这一问上**没有缺欠**（那张表没有的，库也没有；那张表有的，库都有），所以今天没有必须补的理由。若作者想让 f32 张量的律表与整数对称，那就是 (B)，属于新的语言工作，建议单独立项而不是塞进 H226 或规则库的清理。

## 来源

- 测量：`test/cases/opt/algebra_rules/tensor_forms.cml`、`test/plans/feat/opt/algebra_rules.plan.toml`（`tensor.by_cpp` / `tensor.table`），以及逐行对比脚本 `test/cases/opt/algebra_rules/tables.mjs`（`agreed` 里记着 `tensor_fold` 与 `tensor_zero`，`differs` 里记着 `tensor_combine`）。
- 库自己的说法：`stdlib/rules.cml:340-355`、`387-434`（`tensor_f32_*` 一族）。
- 被删的法表：`e8c290554:src/passes/opt/generic/algebra.cpp`；删除提交 `a510676bb`。
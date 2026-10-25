# 规则的 `with` 参数：右侧读值与"类型参数 + `with` 同时出现"两处还没做

## 结论

**做了的那一半（有测试钉住）**

- `with <...>` 可以写在规则声明前，与函数同一种文法与默认值规则：`with <tile: int = 1> rule …`。
- 绑定得到 `Rule`：`tile_matmul<64>` 是一个 `Rule` 值，可以放进规则集（`rewrite(p, [add_zero_tiled<1>, mul_one_scaled])`）。
- 参数有默认值时，裸名字就是 `Rule` 值（`mul_one_scaled` 即 `mul_one_scaled<1>`）。
- 未绑定且没有默认值，放进规则集是编译错误：`SemanticDiag::RuleWithParameterUnbound`，位置在使用处，exit 1，程序不运行。
- `with` 参数在条件里可见：同一个规则、同一个程序、只换绑定值（`<1>` 生效、`<0>` 不生效），见 `test/cases/modules/rules/with_rules.cml` 与 `test/cases/modules/rules/with_target.cml`。

**没做的那两处（都在写下的位置拒绝，不留到运行时）**

1. 规则的**两侧**读 `with` 参数 → `SemanticDiag::RuleWithParameterOnASide`（左、右各一个用例）。
   原因在引擎一侧：`src/runtime/rules.cpp` 的 `SideReader::term()` 只接受 `Port`、`Data`（静态槽）与 `Oper` 三种节点，也就是"两侧都是模式"；`RuleSource` 的右侧没有闭包通道（`source.rhs = rule.rhs()->graphId()`），右侧还要满足"端口数与左侧相同、右侧变量 ⊆ 左侧变量"。要把调用方的值送进某一侧，只能按绑定把这条规则**特化**成实例（值进静态槽、GC 根、实例缓存键），与泛型实例化通道是同一件事。
2. 头部同时写类型参数与 `with` 参数（规则语言设计稿第 1 节的 `tile_matmul::<T>` 正是这个形状）→ `SemanticDiag::RuleWithParameterGenericHead`。
   原因：泛型规则的实例化通道（`GenericRuleEntry`、`instantiateGeneric`）没有携带 `with` 值的通道，而 `partialApplicationOf` 拒绝泛型 dref。

所以"`with` 参数在 `when` 与右边可见"目前只做到 `when` 那一半；右边（以及左边）是编译期拒绝，措辞是"which is not implemented yet"。

## 需要裁定的

1. **右侧只读 `when` 可以作为第一步吗？** 还是这一批就要求两侧能读（那意味着先做"按绑定特化规则实例"，并和泛型实例化通道一起设计）？
2. **头部同时带类型参数与 `with` 参数现在做不做？** 设计稿的例子就是这个形状，现在按名字拒绝。
3. **文档与实现的三处不一致**：语言参考（`docs/language/`）关于规则 `with` 参数没有写；设计原则 5.5 与规则语言设计稿第 1 节写了。要不要由这里补齐语言参考的散文（本批次按约定没有改 `docs/language/` 的散文）。
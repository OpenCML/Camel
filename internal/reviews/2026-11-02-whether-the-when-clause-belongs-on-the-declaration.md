# 重载的 `when` 条件要不要挂到声明上

**问句**：重载的 `when` 条件文本要不要挂到声明上（`OperatorTable::CallForm` 加一个字段，或声明记录带上它），让打印器能把它写回去？

## 今天的事实

`compile/when_overloads/values.cml` 里两个同名声明：

```
with <fast: bool, wide: bool>
func pick(x: int): string when fast { return 'fast' }

with <fast: bool, wide: bool>
func pick(x: int): string when wide { return 'wide' }
```

打印器写出的是：

```
with <fast: bool, wide: bool>
func pick(x: int): string { return 'fast' }

with <fast: bool, wide: bool>
func pick(x: int): string { return 'wide' }
```

`with <fast: bool, wide: bool>` **一字不差**——所以这不是"打印器没打 `with` 槽"。少的是 `when` 条件，而它今天**过不了图这道门**：

1. **图上没有它**：`std::gir` 里两张图都叫 `pick`，`with` 端口 `fast`/`wide` 与普通参数 `x` 一模一样，**签名完全相同**，没有任何条件节点或属性。
2. **声明表里没有它**：打印器唯一能问的是 `camel::core::OperatorTable::callFormOf(uri)`，而 `CallForm` 的字段只有 `name`、`minArgs`、`withCount`、`declaredByModule`、`kernelDeclaration`（`include/camel/core/operator_table.h#CallForm`）——**没有条件文本**。
3. **条件只活在编译期的树上**：`camel::compile::gct::Tree::whenOf` 给出 `WhenClause`（带 `condition` 与 `text`，`include/camel/compile/gct/tree.h#whenOf`），建图器用它的 `whenClauses_` 判两条重载是否互斥（`src/compile/gir/builder.cpp#whenClauses_`）——**图建完它就没了**。

**后果**：打印出来的两条声明一模一样，重编译就是 `Redeclaration: 'pick'`。这是 `unfaithful` 十条里唯一一条"签名侧"的，本车道做不到把它清掉。

## 两条路

**A（建议）**：`CallForm` 加一个 `whenText` 字段，建图时从 `gct::Tree::whenOf` 抄进去。
- **代价**：一个新字段（按项目要求要写清三条：为什么推不出、为什么不能用已有机制、通用在哪里）；并且它让"声明文本"多一处来源——同一句话以后有两个地方能改，必须写清谁是权威。
- 收益：打印器写得出 `when fast`，两条声明重新可区分，`unfaithful` 那条能清掉。

**B**：打印器接受"有条件重载时不判等，具名拒绝"。
- **代价**：又一个具名拒绝；且 `unfaithful` 那一族**永远停在 10**——这条声明永远是"打印不出来的程序"，而不是"打印器还缺一块"。
- 与作者已立的规矩一致（"表达不了的具名拒绝"），但它把一块可修的东西永久记成不可修。

**默认 A**，理由一句：**`when` 是程序写下的东西，不是优化产物；丢了它就不是"重排"而是"丢语义"**。

## 相关的分工表（同一批 `unfaithful` 十条）

| 条 | 卡在哪 |
| --- | --- |
| `when_overloads/values`（本页） | 声明层：要条件文本 |
| `static_expansion/comprehensions`、`parse/constructs/comprehensions` | 声明层：要"同作用域唯一"的起名规则（M7） |
| `array_shape/literal_length`、`array_shape/rank_zero_parameter_slot`、`module_operators/anon_models` | 打印器侧（类型、动态转换、生成名） |
| `consistency/values/closure_var`、`linear/nvm/var_params`、`trans/cpp/exec/variables` | 打印器侧，但**不是同一个缺陷**——见下 |

## 附：那三条"`cmlN` 出作用域"的实况（本页记下，避免下一个人重走）

查证结论：**它们不是同一个缺陷**，而且**在名字修好之后有两条会变成"能跑但答错"的打印**——比原来那条编译错误更危险。

- `trans/cpp/exec/variables`：读的版本是 `:var/after`（一次调用留下的版本），旧代码只把 `:op/assn*` 剥到 cell、不剥 `:var/after`，于是打印器给那个 cell 现编了名字 `cml7`，`println(cml5, cml6, cml7)` 里 `cml7` 无人声明。改用 `cellOf`（能剥 `:var/after`）之后它**回程判等**（95 顶点、355 边）。
- `consistency/values/closure_var`、`linear/nvm/var_params`：名字修好以后**能编译、能跑，但答案不同**——`closure_var` 源程序打印 `3 / 24 / 249`，打印出来的程序打印 `1 / 2 / 4`。差额来自**被丢掉的调用**：`twice(x)`、`add(100)`、`f(g)` 这些"结果没人读、而它们的被调函数通过捕获写了一个变量 cell"的调用，打印器当成纯调用丢了（`isEffectful` 对 lambda 图返回 false；把 lambda 图加进候选、并按"被调图里有 `:op/assn`"另判一次，这两条都没变）。
- 因此这三条**不能靠改名字清掉**：名字只是先暴露出来的那一层。真缺陷是"通过捕获写 cell 的调用被当纯调用丢掉"，要单独一刀；在它修好之前，那两条程序宁可**不打印**（具名拒绝），也不能印出一个能跑但答错的程序。

**这一条也是"打印不忠实 = 假打印"的又一个实例：名字修好把一条响的失败变成了一条哑的失败——所以在修"丢调用"之前，这一刀不该合。**
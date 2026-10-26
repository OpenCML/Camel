# 9. 规则与事实

[English](en/09_rules_and_facts.md) | 中文

前八章讲语言自己怎么算。这一章讲读者**教给编译器**的那部分知识：一条 `rule` 说"这两种写法是同一个值"，一项 `fact` 说"这个值有什么性质"。两者都是**值**——一条规则是一个 `Rule`，一项事实是一个可以查询的函数——写下来不会自己生效：谁要用，就把它们交给谁（[9.4](#94-规则集是显式交出去的)）。

---

## 9.1 一条规则

```
rule 名字(形参) when 条件? { 左边 => 右边 }
rule 名字(形参) { 左边 <=> 右边 }
```

`左边`是模式，`右边`是它可以被换成的写法；`<=>` 表示两个方向都可以用。形参有两种：

| 形参 | 匹配 |
|---|---|
| `x: T` | 图上任意一个值 |
| `const c: T` | 一个类型为 `T` 的常量；条件与右边里拿到的是常量的**值** |

形参的声明类型由匹配器检查，所以一条规则不必自己写 `isI32(c)` 一类的守卫：`const c: int` 只匹配 `int` 常量，`const c: f32` 只匹配 `f32` 常量。

一条 `rule` 声明的是一个 `Rule` 值（像 `let` 声明的常量，不是函数）：它可以被传进函数、放进数组、按条件过滤。**规则集就是普通的 `Rule[]`**，调度也是普通的 Camel 函数（[规则语言第二版 8.5](../design/rewrite/v2.cn.md)）。

```cml
module law

import { Program } from graph
import { rewrite } from rewrite

// x - x is zero for every int: no condition, so nothing has to hold first.
rule sub_self(x: int) { x - x => 0 }

// c1 + c2 on the right is computed when the rule fires, so the two coefficients become one.
rule fold_scales(x: int, const c1: int, const c2: int) { x * c1 + x * c2 => x * (c1 + c2) }

/// A rule set is an array; a pass is an ordinary function that is handed one.
export func simplify(p: Program): Program! {
    return rewrite(p, [sub_self, fold_scales])
}
```

一条规则还带着它的名字与要求：`rule_name`、`rule_two_way`、`rule_needs`、`rule_consts` 读一个 `Rule` 值（[标准库参考](../stdlib-reference.md)）。

## 9.2 条件与许可

`when` 是**对匹配到的那一处的一个查询**：引擎找到了一个匹配，用它问这条规则在这里成不成立。

- 条件的形参就是规则自己的形参：普通形参在那里是**节点**，`const` 形参是**值**（于是守卫就是值的算术，不必先取出节点）。要读 `const` 形参所绑定的那个节点，写 `node_of(c)`。
- 条件里只写**没有外在效果**的表达式。`println` 一类做了 IO 的调用写在条件里会被拒（`SemanticDiag::EffectInRuleCondition`）：条件是引擎拿着改写会话求值的，一次提交或改写在那里会动到引擎正在读的程序。效应要写在规则的右边，或写在改写之外的程序里。
- 条件里可以调用别的函数与别的 `fact`，如同任何表达式；被调用的函数本身有没有外在效果，照样要被检查。

反例：条件里调用了一个打印的函数，编译器在声明处就拒绝。

<!-- camel-snippet: counter-example: the condition calls a function that prints, which the compiler refuses -->

```cml
func reports(c: float): bool sync {
    println('guard ', c)
    return c > 0.0
}

rule keep_positive(x: float, const c: float) when reports(c) { x * c => x }
```

```text
error[SemanticDiag::EffectInRuleCondition]: The call of 'println' in the function 'reports' has an effect, and it is inside the condition of the rule 'keep_positive'.
```

**许可。** 规则之前可以写 `#requires(许可, ...)`：规则只在它匹配到的节点所在的数值策略允许这些许可时才被应用（[7.6.1](07_statements.md#761-数值许可标签)）。实参是许可名（`reassociate`、`contract`、`approx_math`、`change_precision`、`nondeterministic`，以及 `finite_math` 的四个分项）、核心的组 `finite_math`，或者本模块用 `tag 名字 = [许可, ...]` 声明的组。`reassociate` 与 `contract` 是默认许可，写出来只是把它写明（[设计原则 7.3](../design-principles.cn.md)）。

```cml
// A sum of two scaled copies of one value is one scaled copy only where regrouping is allowed.
#requires(reassociate)
rule combine_scales(x: float, const c1: float, const c2: float) { x * c1 + x * c2 => x * (c1 + c2) }
```

## 9.3 类型参数与 `with` 参数

一条规则可以在名字后面声明**类型参数**，在声明之前写 **`with` 参数**：

```
with <名字: 类型 = 默认值, ...>
rule 名字::<T>(形参...) when 条件? { 左边 => 右边 }
```

几种参数来源不同（[设计原则 5.5](../design-principles.cn.md)）：

| 参数 | 谁决定它的值 |
|---|---|
| 模式里的形参 | 匹配到的值 |
| `const c: T` | 匹配到的常量 |
| `::<T>` | 匹配到的类型：泛型规则按类型实例化 |
| `with <tile: int>` | 选用这条规则的人绑定一次，之后按这个值用在整段改写上 |

`with` 参数的文法与函数的 `with` 参数相同（[7.4.2](07_statements.md#742-with-形参)）：可以写默认值，可以按名字给。**绑定写在用它的地方**，与函数的部分应用同一种写法：

```cml
module tiled

import { Program } from graph
import { rewrite } from rewrite

with <tile: int>
rule add_zero_tiled(x: int) when tile > 0 { x + 0 => x }

with <scale: int = 1>
rule mul_one_scaled(x: int) when scale > 0 { x * 1 => x }

/// add_zero_tiled<1> binds tile; mul_one_scaled's parameter has a default, so its name alone is a Rule.
export func apply(p: Program): Program! {
    return rewrite(p, [add_zero_tiled<1>, mul_one_scaled])
}
```

- 参数都有默认值时，规则的名字本身就是 `Rule`；有没有绑定，看声明就知道。
- 没有默认值又没绑定就放进规则集，是编译错误（`SemanticDiag::RuleWithParameterUnbound`）：这个规则值里没有任何值给它的条件读。
- **类型实参与 `with` 实参走同一条实例化通道，实例的键就是这一对**：同一对键命中同一个实例，不同的绑定是不同的实例。这与泛型函数的实例化是同一件事，两个都写出来时的写法也与函数相同：`名字::<T><值>`。
- **规则的两边与条件都读 `with` 参数**：左边、右边与 `when` 都是它的函数，按名字读它——`{ x + 0 => x + shift }` 与 `when shift > 0` 是同一条规则的两半，绑定的是同一个值，右边的 `shift` 就是绑定给这个实例的那个。
- 上一条与这一条是同一件事：一条规则可以同时写 `::<T>` 与 `with <tile: int>`，类型那一半由匹配到的类型给出，调度那一半由用它的地方绑定，合起来是实例的键。验收形态是[规则语言第二版第 1 节](../design/rewrite/v2.cn.md)的 `tile_matmul`：`with <tile: int> rule tile_matmul::<T>(a: T[m, k], b: T[k, n]) when m % tile == 0 { a @ b => tiled_matmul<tile>(a, b) }`。

## 9.4 规则集是显式交出去的

- **一条改写遍总是显式拿到规则集**：语言不会因为 import 某个模块就往流水线里加遍，也不会自动收集规则。在一个模块里写下一条 `rule`，不会悄悄改变别的遍做什么（[规则语言第二版 8.5](../design/rewrite/v2.cn.md)、[设计原则](../design-principles.cn.md) 第 9 节的遍模型）。
- Camel 里的用法见[标准库参考](../stdlib-reference.md) 的 `rewrite` 模块：`rewrite(p, rules)` 单向应用到不动点；`saturate(p, rules, rounds)` 连重排的规则一起用；`run<rules>` 把轮数与代价显式带出来（`explain`、`why_not` 读它）。遍就是函数（[4.7](04_execution_model.md#47-遍与流水线)），组合就是函数组合与管道。
- 标准优化遍 `std::opt` 用标准库那批定律（`stdlib/rules.cml`）化简。要把自己的规则也交给它：Camel 里写 `f->std::opt<rules: [negate]>`，命令行写 `std::opt rules=[law::negate]`（`Rule[]` 是一个常量表达式，按遍参数的规则读）。

**尚未实现。** 今天的 `std::opt` 没有 `rules` 参数：命令行写它会得到 `SemanticDiag::ParamArgumentUnknown`（"'rules' is not a parameter of 'std::opt'"），源码里也还解析不到 `std::opt` 这个名字。今天要应用自己的规则，就写一条自己的遍——遍是一个取程序、返回程序的函数——再把它交给命令行。这条遍的名字是模块名（这里是文件名），所以下面这样调用：

```cml
// counts.cml
module counts

import { abs } from math
import { Program } from graph
import { rewrite } from rewrite

/// What this program may rely on: its values are counts, so a count is never negative and a
/// difference of two counts may be.
fact nonnegative(v: int): bool match v {
    case const c => c >= 0
    case x - y   => false
    case x + y   => nonnegative(x) && nonnegative(y)
    case _       => true
} join (lhs: bool, rhs: bool) => lhs | rhs

/// A count is its own absolute value.
rule abs_of_count(x: int) when nonnegative(x) { abs(x) => x }

/// The rule set is handed to the pass here, and nowhere else.
export func simplify(p: Program): Program! {
    return rewrite(p, [abs_of_count])
}

func distance(a: int, b: int): int {
    return abs(a + b)
}

func main(): int sync {
    println(distance(3, 4))
    return 0
}
```

```bash
camel counts.cml counts::simplify std::nvm
```

```text
7
```

## 9.5 事实

`fact` 声明图上一个值的一项**事实**：它由值的**构造方式**算出，规则的 `when` 与别的事实按函数调用查询它。

```
fact 名字(参数...): 事实类型 match 被匹配的值或元组 {
    case 模式 => 值
    ...
} join 函数 meet 函数 widen 函数 narrow 函数
```

- **头**：`fact 名字(参数...): 事实类型`，输入显式；事实类型限定在平凡数据上（数值、布尔、结构体一类）。
- **主体**：与普通的 `match` 同一种写法（[6.8.2](06_expressions.md#682-match)），只是被匹配的是值**在图上的构造方式**：`case x + y` 把"由加法得到的值"拆成两个操作数，如同按变体拆一个代数数据类型。模式与 `rule` 左边同一种写法。参数不止一个时匹配元组（[9.6](#96-事实的参数与-with-实参)）。
- **`case _` 必须有**：没有任何其它 case 匹配时这项事实是什么值，必须有话说；它也是"值依赖自己"时停下来的取值。少了它，声明被拒（`SemanticDiag::FactCaseDefaultMissing`）。
- **尾随子句**：每个接一个二元函数——已有函数的名字，或 lambda `(a, b) => ...`；顺序不限，可以换行。省略 `meet`、`widen`、`narrow` 时，那几处都用 `join`。

| 子句 | 含义 | 必需 |
|---|---|---|
| `join` | 分支汇合："两者之一"，最小上界 | 必需 |
| `meet` | 同一个值的等价写法之间："两者都成立"，最大下界；省略时等价写法也用 `join`（正确但不精确） | 可选 |
| `widen` | 循环里迭代时加速收敛；省略时迭代到上限退回 `case _` 的事实 | 可选 |
| `narrow` | `widen` 收敛之后再收紧，找回它丢掉的那部分精度 | 可选 |

理论上它们就是抽象解释的 ⊔/⊓/∇/Δ。

`join`、`meet`、`widen`、`narrow` 接的是一个**函数**，所以这里写库里真有的名字（`max` 一类）或 lambda。**`or`/`and` 不是标准库里的函数**，也不会新加：布尔的"或"就是 `|`（两边都求值、可重载的普通逻辑运算，[设计原则 4.2](../design-principles.cn.md)），所以布尔事实的合并写成 `join (lhs: bool, rhs: bool) => lhs | rhs`。`||` 是"转成分支"的保留运算，不是能传出去的函数值，不能写在这里。

```cml
module bounds

import { max, min } from math

type Interval = { lo: int, hi: int }

/// The wider of two intervals: what `join` and `widen` use here.
func hull(a: Interval, b: Interval): Interval {
    return { lo: min(a.lo, b.lo), hi: max(a.hi, b.hi) }
}

/// The overlap of two intervals: what `meet` and `narrow` use here.
func intersect(a: Interval, b: Interval): Interval {
    return { lo: max(a.lo, b.lo), hi: min(a.hi, b.hi) }
}

/// Where a value can be: a constant is itself, a sum adds the two ends, anything else is unknown.
/// The four clauses are written out so their spelling is visible; this example reuses the two
/// helpers, so `widen` grows and `narrow` narrows.
fact range(v: int): Interval match v {
    case const c => { lo: c, hi: c }
    case x + y   => { lo: range(x).lo + range(y).lo, hi: range(x).hi + range(y).hi }
    case _       => { lo: 0, hi: 0 }
} join hull meet intersect widen hull narrow intersect
```

反例：`case _` 是必需的，少了它就没有话说什么都没匹配上的值是什么。

<!-- camel-snippet: counter-example: the fact has no case _, which the compiler requires -->

```cml
fact sign(v: int): int match v {
    case const c => c
} join max
```

```text
error[SemanticDiag::FactCaseDefaultMissing]: The fact 'sign' has no 'case _', so nothing says what it is for a value no other case matched.
```

## 9.6 事实的参数与 `with` 实参

**参数。** 事实可以有参数，查询时把它们当实参传进去。参数是值的**那几个部分**（节点）：`fact mul_by_one(a: Node, b: Node): bool match (a, b) { ... }` 由规则的条件用 `mul_by_one(x, node_of(c))` 查询。没有参数时是单值形式 `range(v)`。类型参数写在名字后（`fact range::<T>(v: T): Interval`），与规则的写法相同——见 [9.3](#93-类型参数与-with-参数)。

```cml
module parts

import { max } from math
import { Node, Program } from graph
import { rewrite } from rewrite

fact sign(v: int): int match v {
    case const c => c
    case _       => 0
} join max

/// The fact of a product, read from the two parts a rule's condition matched.
fact mul_by_one(a: Node, b: Node): bool match (a, b) {
    case x * y => sign(y) == 1
    case _     => false
} join (lhs: bool, rhs: bool) => lhs | rhs

rule mul_one_by_parts(x: int, const c: int) when mul_by_one(x, node_of(c)) { x * c => x }

export func apply(p: Program): Program! {
    return rewrite(p, [mul_one_by_parts])
}
```

**`with` 实参。** 事实可以有 `with` 参数：加宽阈值、精度、截断深度、容差一类的调节项。声明与函数、规则相同；查询时像调用函数那样写出实参，有默认值时可以省略。

```cml
module capped_fact

import { max } from math
import { Program } from graph
import { rewrite } from rewrite

with <cap: int = 5>
fact capped(v: int): int match v {
    case const c => c
    case _       => 0
} join max

/// The argument is left out: this is the fact `capped`, at the default.
rule at_default(x: int) when capped(x) == 5 { x + 0 => x }

/// The argument is written out here: this is a different fact, `capped<3>`.
rule at_three(x: int) when capped<3>(x) == 3 { x * 1 => x }

export func apply(p: Program): Program! {
    return rewrite(p, [at_default, at_three])
}
```

- **不同的实参是不同的两项事实**：引擎按"事实 + 实参"分别计算与缓存，`capped<3>` 与 `capped` 各有一份值，写下一个实参不改变别处读到的那一项事实。
- **实参必须是编译期常量**：一项事实的值只由程序与写出来的实参决定，所有规则看到同一个答案。写出不是常量的实参会被拒（`SemanticDiag::FactArgumentNotConstant`）。
- **查询的实参是节点**：一项事实说的是程序图上一个值**是怎么得来的**，它属于图，不属于运行期的值，所以头部 `fact range::<T>(v: T)` 里的 `v: T` 意思是**产出类型 `T` 的那个节点**（[规则语言第二版](../design/rewrite/v2.cn.md) 第 1 节）。于是查询写在有节点可用的地方：规则的条件（`when`）、另一项事实的主体（[9.5](#95-事实)），或者任何拿到 `Node` 的函数。普通函数体在运行期，那里没有"图上的这个值"可问，所以查询不写在里面：今天把值传给查询得到的是 `SemanticDiag::NoMatchingFunction`（帮助里列出 `名字: (v: Node) => 事实类型`）；按裁定，这里该是一条点名规则的诊断，尚未实现。

反例：事实的实参写了规则的形参 `y`，它不是常量。

<!-- camel-snippet: counter-example: the argument of the fact is a rule parameter, not a constant -->

```cml
with <cap: int = 5>
fact capped(v: int): int match v {
    case const c => c
    case _       => 0
} join max

rule reads_constant(x: int, y: int) when capped<y>(x) > 0 { x * y => x }
```

```text
error[SemanticDiag::FactArgumentNotConstant]: The argument 'cap' of a fact is not a compile-time constant of type 'int'.
```

**`with` 参数的可见范围与实例化通道。** `with` 参数对事实的**所有**片段可见：子句（`case`、`join`/`meet`/`widen`/`narrow`）与别处读到的都是同一组实参——生成出来的这些函数各自声明同样的 `with` 参数，与函数的 `with` 参数是同一个机制。查询写出别的实参就是另一项事实：引擎按"事实 + 实参"分开计算与缓存。事实与规则**共用同一条实例化通道**：实例的键 = 类型实参 + `with` 实参。带类型参数的事实头（`fact range::<T>(v: T): Interval`）语法读得下，编译还没有（[9.8](#98-尚未实现的形态)）。

## 9.7 检查、解释与"哪个规则开火了"

- `check(rules, samples, seed)` 抽查规则：随机造实例，把两边都算一遍再比较，给出一张判定表。它只抽样：给出反例说明规则错了，不报错只说明这一次没抽到反例。标准库的规则入库前都要过 `check`。

```cml
module checked

import { check } from rewrite

rule sub_self(x: int) { x - x => 0 }

rule fold_scales(x: int, const c1: int, const c2: int) { x * c1 + x * c2 => x * (c1 + c2) }

func main(): int sync {
    println(check([sub_self], 100, 1))
    println(check([fold_scales], 100, 1))
    return 0
}
```

```text
sampled sub_self: proposition equality, evidence samples 100, fired 8 of 8, coverage integer scalar int: (x - x) = 0

sampled fold_scales: proposition equality, evidence samples 100, fired 8 of 8, coverage integer scalar int: ((x * c1) + (x * c2)) = (x * (c1 + c2))
```

- 读取器：`rule_name`、`rule_two_way`、`rule_needs`、`rule_consts` 读一条 `Rule`；`run<rules>` 返回的 `Run` 由 `explain`、`explain_all`、`why_not` 读（[标准库参考](../stdlib-reference.md)）。
- **读图转储断言"哪个规则开火了"**：这类断言比较的是转储里的 `text` 块，比较前要**先把与位置有关的属性归一化**——`origin`、`span`、`srcfile` 一类——再比；节选按"在输出里连续出现"来比。否则改一行无关的代码、换一个临时目录，断言就会红：同一个节点在改写前后连 `origin` 都不同。

下面用一个只做这件事的小程序看它的读法：`distance` 的源码是 `abs(a + b)`，跑过 `fired::simplify` 之后这个簇里
没有 `math:abs_l` 节点了（`N05` 是 `:op/add_l`，它的出口边直接连到簇的出口 `O04`），这就是"这条规则开火了"。
节选是产出里连续的一段，与位置无关的那几行省掉了（`origin` 正是上面说的那类会变的值）：

<!-- camel-snippet: graph-dump: the text block is one excerpt of the dump after the pass, not the program's stdout -->

```cml
// fired.cml
module fired

import { abs } from math
import { Program } from graph
import { rewrite } from rewrite

// abs(x) => x, with no condition, so the dump is about the rule firing and nothing else.
rule drop_abs(x: int) { abs(x) => x }

export func simplify(p: Program): Program! {
    return rewrite(p, [drop_abs])
}

func distance(a: int, b: int): int {
    return abs(a + b)
}

func main(): int sync {
    println(distance(1, -2))
    return 0
}
```

```bash
camel fired.cml fired::simplify std::gir
```

```text
            label="distance";
            F04 [label="ARGS", type="ARGS"];
            O04 [label="EXIT", type="EXIT"];
            N03 [label="a", type="PORT", param_name="a", param_kind="normal", param_index="0", value_type="int"];
            N04 [label="b", type="PORT", param_name="b", param_kind="normal", param_index="1", value_type="int"];
            N05 [label=":op/add_l", type="OPER", origin=4343, span="14:17-14:18", srcfile="fired.cml", value_type="int"];
            F04 -> N03 [label="0", type="NORM_PORT"];
            F04 -> N04 [label="1", type="NORM_PORT"];
            N05 -> O04 [label="0", type="EXIT"];
            N03 -> N05 [label="0|0", type="NORM"];
            N04 -> N05 [label="0|1", type="NORM"];
        }
```

不跑这条遍时（`camel fired.cml std::gir`），同一个簇里还有 `math:abs_l` 那个节点：它接在加法那个节点
后面，再把结果交给出口；跑过之后，出口的输入直接来自加法那个节点，`math:abs_l` 的节点没有了。

## 9.8 尚未实现的形态

下面这些形态都已经裁定（[规则语言第二版](../design/rewrite/v2.cn.md) 第 1 节与[设计原则 5.5](../design-principles.cn.md)），本页在用到的地方都标了"尚未实现"。它们今天的读数：

| 形态 | 今天 |
|---|---|
| 带类型参数的事实头（`fact range::<T>(v: T): Interval`） | `SemanticDiag::ConstructNotSupported`：引擎拿到的是构成这项事实的那些函数，带类型参数的函数不是一个值它拿得住 |
| 命令行 `std::opt rules=[law::negate]`，源码 `f->std::opt<rules: [negate]>` | `SemanticDiag::ParamArgumentUnknown`；源码里的 `std::opt` 这个名字今天还解析不到 |

## 相关文档

- [规则语言第二版（设计稿）](../design/rewrite/v2.cn.md)：规则与事实的目标写法、`check`、调度组合子
- [46 规则](../technical/46_rules.md)：规则与事实在实现里的样子（技术记录）
- [标准库参考](../stdlib-reference.md)：`rewrite` 模块的全部函数（`rewrite`、`saturate`、`extract`、`check`、`run`、`explain`、`why_not`、四个读取器）
- [第 17 章 · 教编译器一条定律](../tutorial/17_teaching_a_law.cn.md)：从零写一条规则，并用 `check` 检查它
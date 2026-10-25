# 第 17 章 · 教编译器一条定律

[English](17_teaching_a_law.en.md) | 中文 · [返回目录](tutorial.cn.md)

第 1 章里编译器把 `x * 2 + x * 2` 改成了 `x * 4`，结果不变。它凭什么敢改？因为它手里有一批**定律**：
"两个同底数的乘法可以合成一个"。这批定律写在标准库里，是有限的；**你自己领域里的定律它不知道，
得你告诉它**。这一章先写一条定律并让编译器检查它，再看它怎么用到你的程序上。

## 写一条定律

[`examples/17_rule.cml`](examples/17_rule.cml)：

```cml
// The law of this program's arithmetic, written the way the compiler reads laws.
// Run: camel docs/tutorial/examples/17_rule.cml
module law

import { check } from rewrite

// Multiplying by -1 is negating. A `rule` names a pattern and the writing it may be replaced by.
rule negate(x: int) when nonnegative(x) { x * -1 => 0 - x }

func negated(x: int): int {
    return x * -1
}

// Laws the compiler's own library already knows: a sum of two scaled copies of one value is one
// scaled copy, so this is one multiplication.
func combined(x: int): int {
    return x * 3 + x * 5
}

// The same value written twice: `std::opt::saturate` keeps both writings in one class.
func doubled(x: int): int {
    return x * 2 + x * 2
}

func main(): int sync {
    // `check` draws random instances and compares the two sides of every rule it is given.
    println(check([negate], 100, 1))
    println(negated(7), combined(7), doubled(7))
    return 0
}

// What the compiler may rely on: the values this program works with are counts, so a count is
// never negative and a difference of two counts may be. `join` says how the two writings of one
// value are merged.
fact nonnegative(v: int): bool match v {
    case const c => c >= 0
    case x - y   => false
    case _       => true
} join (lhs: bool, rhs: bool) => lhs | rhs
```

```bash
camel docs/tutorial/examples/17_rule.cml
```

```text
sampled negate: proposition equality, evidence samples 100, fired 8 of 8, coverage integer scalar int: (x * -1) = (0 - x)

-7 56 28
```

一条 `rule` 由三部分组成：**名字**、**模式**（`=>` 左边）、**可以换成什么**（右边）。

- 形参里写 `x: int` 的名字是"这里可以是任意表达式"；写 `const c: int` 的名字是"这里必须是一个常量"。
  `x * c1 + x * c2 => x * (c1 + c2)` 就是标准库那条合成定律的写法（右边的 `c1 + c2` 在编译期算掉）。
- 一条 `rule` 是一个**值**，类型是 `Rule`。它可以被传进函数、放进数组，也可以交给改写引擎。
- `check` 拿它去抽查：随机造实例，把两边都算一遍，再比较。上面那行 `fired 8 of 8` 说的是这条定律被
  试了 8 次、8 次两边相等；最后 `(x * -1) = (0 - x)` 是它对这条定律的理解。
- `check` 的判定是**抽样**：它给出反例就说明定律错了，它不报错只是说没抽到反例。

## 把定律交给优化器

一条 `rule` 是一个**值**，不会自己进任何遍：模块里写下的规则不被收集（[语言参考 9.4](../language/09_rules_and_facts.md)）。
`std::opt` 用的是标准库那批定律（`stdlib/rules.cml`）；要把自己写的定律也交给它，写法是把规则集**显式**传进去：
Camel 里写 `f->std::opt<rules: [negate]>`，命令行写 `std::opt rules=[law::negate]`。今天的 `std::opt` 还没有
`rules` 参数（写它会得到 `SemanticDiag::ParamArgumentUnknown`），所以今天要让自己的定律生效，就写一条自己的遍再交给
命令行——上面 `check([negate], …)` 就是"显式交出去"的例子。下面这份文件交给 `std::opt` 时，`negated` 这一半
**没有变**：`x * -1` 原样留着，因为这条定律是你的，优化器不知道它。

<!-- camel-snippet: graph-dump: the text block is one excerpt of std::opt std::gir's DOT, not the program's stdout -->

```cml
// The law of this program's arithmetic, written the way the compiler reads laws.
// Run: camel docs/tutorial/examples/17_rule.cml
module law

import { check } from rewrite

// Multiplying by -1 is negating. A `rule` names a pattern and the writing it may be replaced by.
rule negate(x: int) when nonnegative(x) { x * -1 => 0 - x }

func negated(x: int): int {
    return x * -1
}

// Laws the compiler's own library already knows: a sum of two scaled copies of one value is one
// scaled copy, so this is one multiplication.
func combined(x: int): int {
    return x * 3 + x * 5
}

// The same value written twice: `std::opt::saturate` keeps both writings in one class.
func doubled(x: int): int {
    return x * 2 + x * 2
}

func main(): int sync {
    // `check` draws random instances and compares the two sides of every rule it is given.
    println(check([negate], 100, 1))
    println(negated(7), combined(7), doubled(7))
    return 0
}

// What the compiler may rely on: the values this program works with are counts, so a count is
// never negative and a difference of two counts may be. `join` says how the two writings of one
// value are merged.
fact nonnegative(v: int): bool match v {
    case const c => c >= 0
    case x - y   => false
    case _       => true
} join (lhs: bool, rhs: bool) => lhs | rhs
```

```bash
camel docs/tutorial/examples/17_rule.cml std::opt std::gir
```

```text
            label="negated";
            F0D [label="ARGS", type="ARGS"];
            O0D [label="EXIT", type="EXIT"];
            N33 [label="x", type="PORT", param_name="x", param_kind="normal", param_index="0", value_type="int"];
            N34 [label="-1", type="DATA", origin=2078, span="10:15-10:17", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int"];
            N35 [label=":op/mul_l", type="OPER", origin=2079, span="10:13-10:14", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int"];
            F0D -> N33 [label="0", type="NORM_PORT"];
            N35 -> O0D [label="0", type="EXIT"];
            N33 -> N35 [label="0|0", type="NORM"];
            N34 -> N35 [label="0|1", type="NORM"];
        }
```

`negated` 还是 `x * -1`（`:op/mul_l`，常量 `-1`），运行结果不变（`negated(7)` 还是 `-7`）——这是"定律没有自己
进去"的读数。要让它在图上生效，就把它显式交出去：`check` 是这样，规则集进 `std::opt` 也是这样（`std::opt rules=[…]`）。

这条定律只在事实成立的地方用：`x` 是非负的计数时，`x * -1` 才允许写成 `0 - x`。这个事实写在文件的
末尾，下一节讲它。

## 编译器自己的那批定律

`std::opt` 手里是标准库那一批（你写的那条不在里面）：`x * 3 + x * 5` 这类式子标准库早就认识。同一份程序，
`combined` 这一段在
`camel docs/tutorial/examples/17_rule.cml std::opt std::gir` 之后是（节选）：

<!-- camel-snippet: graph-dump: the text block is one excerpt of std::opt std::gir's DOT, not the program's stdout -->

```cml
// The law of this program's arithmetic, written the way the compiler reads laws.
// Run: camel docs/tutorial/examples/17_rule.cml
module law

import { check } from rewrite

// Multiplying by -1 is negating. A `rule` names a pattern and the writing it may be replaced by.
rule negate(x: int) when nonnegative(x) { x * -1 => 0 - x }

func negated(x: int): int {
    return x * -1
}

// Laws the compiler's own library already knows: a sum of two scaled copies of one value is one
// scaled copy, so this is one multiplication.
func combined(x: int): int {
    return x * 3 + x * 5
}

// The same value written twice: `std::opt::saturate` keeps both writings in one class.
func doubled(x: int): int {
    return x * 2 + x * 2
}

func main(): int sync {
    // `check` draws random instances and compares the two sides of every rule it is given.
    println(check([negate], 100, 1))
    println(negated(7), combined(7), doubled(7))
    return 0
}

// What the compiler may rely on: the values this program works with are counts, so a count is
// never negative and a difference of two counts may be. `join` says how the two writings of one
// value are merged.
fact nonnegative(v: int): bool match v {
    case const c => c >= 0
    case x - y   => false
    case _       => true
} join (lhs: bool, rhs: bool) => lhs | rhs
```

```bash
camel docs/tutorial/examples/17_rule.cml std::opt std::gir
```

```text
            label="combined";
            F0E [label="ARGS", type="ARGS"];
            O0E [label="EXIT", type="EXIT"];
            N36 [label="x", type="PORT", param_name="x", param_kind="normal", param_index="0", value_type="int"];
            N37 [label="8", type="DATA", value_type="int"];
            N38 [label=":op/mul_l", type="OPER", origin=2088, span="16:17-16:18", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int"];
            F0E -> N36 [label="0", type="NORM_PORT"];
            N38 -> O0E [label="0", type="EXIT"];
            N36 -> N38 [label="0|0", type="NORM"];
            N37 -> N38 [label="0|1", type="NORM"];
        }
```

`x * 3 + x * 5` 剩下一乘：`3 + 5` 先算成常量 `8`，两个乘法合成一个。这条定律不在你的文件里，
在标准库里；`std::opt` 用它，是因为流水线里挂着那批定律。

## 等价图：一个值的多种写法

优化不总是马上决定"用哪种写法"。`std::opt::saturate` 把标准库的定律用到不动为止，**把同一个值的几种
写法都留着**，让后面的阶段再挑。同一份程序的 `doubled` 那一段，在
`camel docs/tutorial/examples/17_rule.cml std::opt::saturate std::gir` 之后是（节选）：

<!-- camel-snippet: graph-dump: the text block is one excerpt of std::opt::saturate std::gir's DOT, not the program's stdout -->

```cml
// The law of this program's arithmetic, written the way the compiler reads laws.
// Run: camel docs/tutorial/examples/17_rule.cml
module law

import { check } from rewrite

// Multiplying by -1 is negating. A `rule` names a pattern and the writing it may be replaced by.
rule negate(x: int) when nonnegative(x) { x * -1 => 0 - x }

func negated(x: int): int {
    return x * -1
}

// Laws the compiler's own library already knows: a sum of two scaled copies of one value is one
// scaled copy, so this is one multiplication.
func combined(x: int): int {
    return x * 3 + x * 5
}

// The same value written twice: `std::opt::saturate` keeps both writings in one class.
func doubled(x: int): int {
    return x * 2 + x * 2
}

func main(): int sync {
    // `check` draws random instances and compares the two sides of every rule it is given.
    println(check([negate], 100, 1))
    println(negated(7), combined(7), doubled(7))
    return 0
}

// What the compiler may rely on: the values this program works with are counts, so a count is
// never negative and a difference of two counts may be. `join` says how the two writings of one
// value are merged.
fact nonnegative(v: int): bool match v {
    case const c => c >= 0
    case x - y   => false
    case _       => true
} join (lhs: bool, rhs: bool) => lhs | rhs
```

```bash
camel docs/tutorial/examples/17_rule.cml std::opt::saturate std::gir
```

```text
            label="doubled";
            writings_cost="weighted";
            F0F [label="ARGS", type="ARGS"];
            O0F [label="EXIT", type="EXIT"];
            N36 [label="x", type="PORT", param_name="x", param_kind="normal", param_index="0", value_type="int"];
            subgraph cluster_class_C02 {
            label="class c1";
            style=dashed;
            C02 [label="class c1", type="CLASS"];
                N37 [label="2", type="DATA", origin=2092, span="21:15-21:16", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c1", chosen="true"];
                N38 [label="2", type="DATA", origin=2095, span="21:23-21:24", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c1"];
            }
            subgraph cluster_class_C03 {
            label="class c2";
            style=dashed;
            C03 [label="class c2", type="CLASS"];
                N39 [label=":op/mul_l", type="OPER", origin=2093, span="21:13-21:14", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c2", chosen="true"];
                N3A [label=":op/mul_l", type="OPER", origin=2096, span="21:21-21:22", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c2"];
            }
            subgraph cluster_class_C04 {
            label="class c5";
            style=dashed;
            C04 [label="class c5", type="CLASS"];
                N3B [label=":op/add_l", type="OPER", origin=2097, span="21:17-21:18", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c5"];
                N3C [label=":op/mul_l", type="OPER", origin=2097, span="21:17-21:18", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c5", chosen="true"];
                N3D [label=":op/mul_l", type="OPER", origin=2097, span="21:17-21:18", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c5"];
                N3E [label=":op/mul_l", type="OPER", origin=2097, span="21:17-21:18", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c5"];
            }
```

每个虚线框是**一类等价的值**（`class`）：框里的节点是同一个值的不同写法，标了 `chosen="true"` 的那一个
是后面抽取时会留下的。`doubled` 的 `x * 2 + x * 2` 因此同时留着 `x * (2 + 2)`、`x * 4` 等写法——定律
已经用过，选择留到后面。这样优化不会因为"先用了哪条定律"而丢掉另一种可能。

## 领域知识：`fact`

定律说的是"这两个写法等价"。程序里还有一类知识不是等式，而是**关于值的性质**：某个值永远非负、
只在 0 和 1 之间、是某个范围的计数。这类知识用 `fact` 声明，`rule` 的 `when` 读它，改写就只在事实
成立的地方发生。

[`examples/17_fact.cml`](examples/17_fact.cml)：

```cml
// The knowledge of your field that is not an equation, handed to the compiler.
// Run: camel docs/tutorial/examples/17_fact.cml
module domain

import { check } from rewrite
import { abs } from math

// A `fact` says what the compiler may rely on for a value: a square is never negative. The match
// reads how the value was built; `join` says how the two writings of one value are merged.
fact nonnegative(v: int): bool match v {
    case const c => c >= 0
    case x * x   => true
    case _       => false
} join (lhs: bool, rhs: bool) => lhs | rhs

// The law holds only where the fact does: on a value that is never negative, `abs` changes nothing.
rule abs_of_nonnegative(x: int) when nonnegative(x) { abs(x) => x }

func squared(x: int): int {
    return abs(x * x)
}

func main(): int sync {
    // `check` samples the law together with its premise, so the fact decides where it may fire.
    println(check([abs_of_nonnegative], 100, 1))
    println(squared(-3))
    return 0
}
```

```bash
camel docs/tutorial/examples/17_fact.cml
```

```text
sampled abs_of_nonnegative: proposition equality, evidence samples 100, fired 8 of 8, coverage integer scalar int: abs(x) = x

9
```

`fact` 的头写明它给哪个类型算哪一项知识（这里是 `int` 的 `nonnegative`）；主体是一个 `match`，但它读的
是值**在图上是怎样构造出来的**：平方构造出来的值非负、常量可以在编译期判掉，其余写 `case _`。`join` 说
一个值有两种写法时，两者的知识怎么合并（这里 `(lhs, rhs) => lhs | rhs`：任一种写法说它非负，就非负）。

`fact` 与 `rule` 一起，是把"我这门领域里成立的东西"交给编译器的两个入口：`rule` 给等价，`fact` 给前提。
它们的完整规则见语言参考[第 9 章 规则与事实](../language/09_rules_and_facts.md)。

## 在图上

- **一条 `rule` 是图上一小块模式**：`=>` 左边是一张小的模式图，右边是一张替上去的图；应用定律就是找到
  匹配、把子图换掉。改写过的节点还留着原来的源码位置（`span`），所以图仍然指得回你的源码。
- **`saturate` 之后，图上一个值可以有几个节点**：它们分成一类一类的等价类，`chosen` 标出抽取时会留下
  哪一个。这时候的图不是"程序"，而是"所有等价写法的集合"。
- 定律来自标准库还是来自你的文件，在图这一层没有区别；区别只在流水线里挂了谁。
- **`fact` 不往图上加节点**：它是遍对图上值的断言，一条定律能不能用，由遍读到的知识决定。

## 练习

写一条自己的定律：`x * c1 + x * c2 => x * (c1 + c2)`（两个常量与同一个值的乘法可以合成一个），用
`check` 看它的判定，再想一个问题：**右边为什么要写 `x * (c1 + c2)`，而不能写 `x * c1 + x * c2`？**
（前者在编译期就能算掉一个乘法，后者只是把式子换了个写法。）

参考答案 [`examples/17_exercise.cml`](examples/17_exercise.cml)：

```cml
// The answer to the exercise of chapter 17: a law written as a `rule`, and the verdict `check`
// gives it.
// Run: camel docs/tutorial/examples/17_exercise.cml
module exercise

import { check } from rewrite

rule fold_scales(x: int, const c1: int, const c2: int) { x * c1 + x * c2 => x * (c1 + c2) }

func main(): int sync {
    println(check([fold_scales], 100, 1))
    return 0
}
```

```bash
camel docs/tutorial/examples/17_exercise.cml
```

```text
sampled fold_scales: proposition equality, evidence samples 100, fired 8 of 8, coverage integer scalar int: ((x * c1) + (x * c2)) = (x * (c1 + c2))
```

## 参考

- [语言参考 4.7 遍与流水线](../language/04_execution_model.md#47-遍与流水线)
- [语言参考 8.5 完整文法](../language/08_top_level_and_grammar.md#85-完整文法)
- [设计原则 9.3 遍参数](../design-principles.cn.md#93-遍参数)
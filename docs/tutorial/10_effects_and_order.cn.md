# 第 10 章 · 效应与顺序

[English](10_effects_and_order.en.md) | 中文 · [返回目录](tutorial.cn.md)

第 9 章那句话有一个代价：**一个表达式会被求值，是因为有人需要它的结果**。那"打印一行字"为什么一定会发生？
两次打印谁先谁后？这一章讲 Camel 对这两个问题的回答：效应与顺序都不是默认的，**要写在程序里**，而图上有专门
的边表示它们。

## 没人要的结果不算

[`examples/10_unused_effect.cml`](examples/10_unused_effect.cml)：

```cml
// An effect whose result nothing needs: `greet`'s body is not a sync block, so its `println`
// never runs.
// Run: camel docs/tutorial/examples/10_unused_effect.cml

func greet(): int {
    println('hello from greet')
    return 1
}

func main(): int sync {
    println(greet())
    return 0
}
```

```bash
camel docs/tutorial/examples/10_unused_effect.cml
```

```text
warning[SemanticDiag::EffectNotExecuted]: The call of 'println' has side effects but is never executed: nothing depends on it and it is not in a sync block.
  --> docs/tutorial/examples/10_unused_effect.cml:7:5
   |
 7 |     println('hello from greet')
   |     ^^^^^^^
help: Evaluation is driven by dependencies: outside a sync block an expression whose result (or the variable it writes) nothing needs does not run. This is legal. To run it, put it in a sync block ('sync { ... }', or a function body 'func f(): T sync { ... }'), or use its result.

1
```

只打印了 `1`。`greet` 的返回值 `1` 不依赖那个 `println`，所以它在图上是一个没有人要的节点：**没人要的效应不
执行**。这不是错误，是语言的一条规则；编译器给这条警告，是为了让忘写的 `sync` 被看见。

规则与理由：程序能观察到的先后只有两种——**值的传递**与**写出来的顺序**。除此之外一切都可以并行、可以重排、
可以删掉，所以编译器不必假设任何顺序。要顺序，就写 `sync`。

## `sync` 函数体

`sync` 写在返回类型之后，表示这个函数体是**同步块**：它直接写着的语句按书写顺序执行，函数交出结果时它们都已
完成。和上一节同一个程序，只把 `greet` 的函数体改成同步块（[`examples/10_sync_body.cml`](examples/10_sync_body.cml)）：

```cml
// The same two statements as 10_unused_effect.cml, in a sync body: now both run.
// Run: camel docs/tutorial/examples/10_sync_body.cml

func greet(): int sync {
    println('hello from greet')
    return 1
}

func main(): int sync {
    println(greet())
    return 0
}
```

```bash
camel docs/tutorial/examples/10_sync_body.cml
```

```text
hello from greet
1
```

两行都出现了，警告也没有了。**函数的行为由它自己决定**：`main` 的同步块管不到 `greet` 里面的语句，反之亦然
——调用只是把一个函数的子图放进另一个图里，不改变它的结构（[语言参考 4.3.2](../language/04_execution_model.md#432-函数的行为由它自己决定)）。

## `sync` 不向内继承

同步块只管**它自己直接写着的**语句。写在里面的一条 `if`、一个循环体、一个嵌套块，按它们自己的写法执行：
要同步就在那里再写一次 `sync`。[`examples/10_sync_block.cml`](examples/10_sync_block.cml)：

```cml
// A sync function body orders its own statements. What is written inside it -- an arm, a loop
// body -- is ordered only if it says `sync` itself.
// Run: camel docs/tutorial/examples/10_sync_block.cml

func report(second: bool): int sync {
    println('the body is a sync block')
    if second then { println('three'); println('four') }
    if second then sync { println('five'); println('six') }
    return 0
}

func main(): int sync {
    println(report(true))
    return 0
}
```

```bash
camel docs/tutorial/examples/10_sync_block.cml
```

```text
warning[SemanticDiag::EffectNotExecuted]: The call of 'println' has side effects but is never executed: nothing depends on it and it is not in a sync block.
  --> docs/tutorial/examples/10_sync_block.cml:8:22
   |
 8 |     if second then { println('three'); println('four') }
   |                      ^^^^^^^
help: Evaluation is driven by dependencies: outside a sync block an expression whose result (or the variable it writes) nothing needs does not run. This is legal. To run it, put it in a sync block ('sync { ... }', or a function body 'func f(): T sync { ... }'), or use its result.

the body is a sync block
four
five
six
0
```

`three` 没有打印：那条 `if` 的臂不是同步块，`println('three')` 的结果没人要，所以不执行；`println('four')` 是
这条臂的值，也就是 `if` 的值，所以执行。第二个 `if` 的臂写了 `sync`，两句都在。

这条规则的理由是**一个块的含义不随它写在哪里而变**：如果臂会继承外层的 `sync`，那么同一段代码写在这里和写在
别处就有两种意思，读代码的人得往上找才知道它是不是有序的（[设计原则 3.4](../design-principles.cn.md#34-sync-块)）。
值得记住的一句：`sync` 是"这个块本身按顺序"，不是"从这里开始都按顺序"。

## `var` 与写入的顺序

`var` 是能写的格子；写与读的先后同样来自写它的那个同步块。[`examples/10_var_order.cml`](examples/10_var_order.cml)：

```cml
// A `var` and the order of its writes: in a sync body the statements run in the order written, a
// read takes the value of the most recent write before it, and a write nobody reads does not run.
// Run: camel docs/tutorial/examples/10_var_order.cml

func bump(var counter: int): int sync {
    counter += 1
    return counter
}

func twice(): int sync {
    var c = 0
    bump(c)
    bump(c)
    return c
}

func latest(): int sync {
    var c = 1
    c = 2
    let before = c
    c = 3
    return before
}

func main(): int sync {
    println(twice())
    println(latest())
    return 0
}
```

```bash
camel docs/tutorial/examples/10_var_order.cml
```

```text
2
2
```

两条规则各出一个 `2`：

- `twice`：两次 `bump` 都在同一个同步块里，按书写顺序执行，第二次看到第一次写完的值，所以是 `2`。
- `latest`：`let before = c` **读到的是它前面最近一次写的值**，也就是 `c = 2` 写下的那个版本；后面那句
  `c = 3` 在它之后，改不到它。所以 `before` 是 `2`，与最后的 `c` 无关。

第三条规则在反面上：**写之后没有任何地方再读这个变量，这次写不执行**。同一个格子、两种函数体，输出一样，
警告不一样（[`examples/10_write_nobody_reads.cml`](examples/10_write_nobody_reads.cml)）：

```cml
// A write nobody reads is not executed, and outside a sync block nothing orders it after the
// earlier uses of the variable. Both programs below print the same `1`: what the sync block adds is
// the guarantee, not the value.
// Run: camel docs/tutorial/examples/10_write_nobody_reads.cml

func unordered(): int {
    var c = 1
    let before = c
    c = 5
    return before
}

func ordered(): int sync {
    var c = 1
    let before = c
    c = 5
    return before
}

func main(): int sync {
    println(unordered())
    println(ordered())
    return 0
}
```

```bash
camel docs/tutorial/examples/10_write_nobody_reads.cml
```

```text
warning[SemanticDiag::IgnoredSideEffect]: The call of '<__assn__>: (var int, int) => int' is not in a sync block, and it takes a var argument.
   --> docs/tutorial/examples/10_write_nobody_reads.cml:10:5
    |
 10 |     c = 5
    |     ^^^^^
help: Put it in a sync block ('sync { ... }', or a function body 'func f(): T sync { ... }'): only then is the write ordered after the earlier uses of the variable. Without one, the write runs only if the variable is read afterwards.

warning[SemanticDiag::EffectNotExecuted]: The assignment has side effects but is never executed: nothing depends on it and it is not in a sync block.
   --> docs/tutorial/examples/10_write_nobody_reads.cml:10:7
    |
 10 |     c = 5
    |       ^
help: Evaluation is driven by dependencies: outside a sync block an expression whose result (or the variable it writes) nothing needs does not run. This is legal. To run it, put it in a sync block ('sync { ... }', or a function body 'func f(): T sync { ... }'), or use its result.

1
1
```

两个函数都打印 `1`：读到的是写之前的版本，写又没人读。区别在保证——同步块里的那次写**一定发生**，并且排在
它前面的读之后；不在同步块里的那次写，编译器只能告诉你它没有顺序（`SemanticDiag::IgnoredSideEffect`），
而且因为没有地方再读它，整条写入不执行（`SemanticDiag::EffectNotExecuted`）。

一句话：**输出对不等于程序对**。要顺序的地方写 `sync`，编译器就不能再挪它了。

## 同一个资源的效应自己有序

效应作用的对象叫**资源**：标准输出是一个，文件系统是一个，某个 `var` 也是一个。**同一个资源上两个都会执行的
效应，按书写顺序发生，不需要同步块**（[设计原则 3.3](../design-principles.cn.md#33-资源uses-与-pure)）。
[`examples/10_same_resource.cml`](examples/10_same_resource.cml) 里两个打印都写向标准输出，两个的结果都被用到
（`println` 返回它的参数），于是它们按书写顺序发生，一条警告都没有：

```cml
// Two effects on the same resource (stdout) keep the written order without a sync block.
// Run: camel docs/tutorial/examples/10_same_resource.cml

func report(): int {
    return 5->println->println
}

func main(): int sync {
    println(report())
    return 0
}
```

```bash
camel docs/tutorial/examples/10_same_resource.cml
```

```text
5
5
5
```

`5->println->println` 是两次打印：第一次的结果（还是 `5`）接着喂给第二次，所以两次都被需要。这段代码没有
`sync`，也没有警告——**同一个资源上的顺序是资源本身带来的**。

反过来：不同资源上的两个效应之间**没有**先后，除非你把它们写进同一个同步块。所以"我要先写文件再打印"这类
要求，写法是同步块，而不是指望编译器猜到。

## 效应与资源：函数签名上的 `uses`

一个调用会碰到哪些资源，是它**类型的一部分**：`func save(p: Params): unit uses fs`。Camel 里可以手写，也可以
不写——编译器按函数体调用了谁推出来（递归时求不动点），并把结果记在函数类型的显示里。下面这个程序把几种写法
都放了一处（[`examples/10_resources.cml`](examples/10_resources.cml)）：

```cml
// What a call touches: a resource of the module, a group of them, a handle type and `uses` on a
// parameter. The bodies touch nothing -- a clause is a promise the compiler checks where it can.
// Run: camel docs/tutorial/examples/10_resources.cml

resource db
resource console = [db, stdout]

#resource type Stream = { id: int }

func read_id(s: Stream): int uses s {
    return s.id
}

func tick(): void uses console {
}

func bump(var cell: int): int uses cell sync {
    cell += 1
    return cell
}

func main(): int sync {
    let s: Stream = { id: 7 }
    println(read_id(s))
    tick()
    var cell = 0
    println(bump(cell))
    return 0
}
```

```bash
camel docs/tutorial/examples/10_resources.cml
```

```text
7
1
```

`uses` 后面跟的可以是什么（文法是[语言参考 8.5](../language/08_top_level_and_grammar.md#85-完整文法) 里的
`resourceClause`，含义见[设计原则 3.3](../design-principles.cn.md#33-资源uses-与-pure)）：

- **模块声明的资源**：`resource db`。一个模块可以声明它自己的资源，也可以把几个资源凑成一组：
  `resource console = [db, stdout]`。
- **每个程序都有的资源**：`stdout`、`stderr`、`stdin`、`clock`、`env`、`fs`。
- **一个形参**：`uses s`、`uses cell`。这是"这个调用会读写我传进去的那个值"——和 `var` 形参一样，是句柄式
  的写法：同一个句柄的操作按书写顺序，不同句柄之间互不约束。
- **`#resource type` 声明的句柄类型**：`#resource type Stream = { id: int }`，它的**每个值**都是一个独立的
  资源，所以 `read_id` 声明的是"我会碰你给我的那个 `Stream`"，而不是"我会碰所有 Stream"。

把这张图打出来，函数节点上的 `tooltip` 就是编译器推出的资源。上面这个程序里：

```text
            tooltip="(s: Stream) => int uses s";
            tooltip="() => void uses db, stdout";
            tooltip="(var cell: int) => int uses cell";
```

三行都在说"这个调用会碰到什么"：`read_id` 碰它收到的那个句柄，`tick` 碰 `console` 这一组（展开成 `db` 与
`stdout`），`bump` 碰调用方给它的那个格子。**这就是资源推断**：不必手写，图上看得到；手写出来的是承诺，函数体
超出就是编译错误（下面几条）。

写错了会看到什么（都是编译错误）：

- 子句里写了一个既不是形参也不是资源的名字：`SemanticDiag::UnknownResource`。
- 同一个签名上又写 `pure` 又写 `uses`：`SemanticDiag::PureAndUses`——`pure` 说的是"一个资源都不碰"，
  `uses` 说的是"碰这些"，两者只能写一个。
- 没有函数体的声明（原生算子）写的子句与算子不符：`SemanticDiag::NativeResourceMismatch`——那种声明就是
  算子的全部签名，资源也在里面。
- 函数体超出了它自己的 `pure`/`uses` 承诺：`SemanticDiag::UsesPromiseViolated`——`uses` 是上界，`pure` 是空集，
  报在声明处，帮助指向第一个超出的使用点（`pure` 带 `var` 形参也在这里报：`var` 形参本身就是资源）。

## `pure`：一个资源都不碰

`pure` 是"这个调用一个资源都不碰"的承诺。写在**函数类型**上时它是要求：谁要传函数进来，就得传一个**推断出的资源集
不超过它**的函数——资源少的函数可以用在要求资源多的地方，反之不行（[设计原则 3.3](../design-principles.cn.md#33-资源uses-与-pure)）。
**手写的是承诺，函数体超出就是编译错误**：具名函数写了子句就照子句核对，写了 `pure` 就必须推成空集；匿名函数没有
子句，它的资源是推出来的，所以一个不碰资源的 lambda、一个没写子句却不碰资源的具名函数，都能传给 `pure` 形参。
[`examples/10_pure_callback.cml`](examples/10_pure_callback.cml) 里的 `apply` 只接受纯函数：

```cml
// `pure` in a function type is part of the requirement: what is compared is the resources the
// argument's body infers against the empty set the parameter asks for, and `twice` touches nothing.
// Run: camel docs/tutorial/examples/10_pure_callback.cml

func apply(f: (x: int) => int pure, x: int): int {
    return f(x)
}

func twice(x: int): int pure {
    return x * 2
}

func main(): int sync {
    println(apply(twice, 4))
    return 0
}
```

```bash
camel docs/tutorial/examples/10_pure_callback.cml
```

```text
8
```

把一个碰资源的函数传进去，就不是"值不对"，而是**签名不匹配**——比较的是**实参推断出的资源 ⊆ 形参要求的资源**，
这类错误在调用处就挡住（[`examples/10_pure_callback_wrong.cml`](examples/10_pure_callback_wrong.cml)）：

<!-- camel-snippet: counter-example: 这个程序故意不编译：调用处就是不匹配，下面的 text 块是诊断，不是输出 -->

```cml
// A counter-example: `noisy` prints, so the resources its body infers do not fit the empty set the
// parameter asks for. The error names the signature that would have matched.
// Run: camel docs/tutorial/examples/10_pure_callback_wrong.cml

func apply(f: (x: int) => int pure, x: int): int {
    return f(x)
}

func noisy(x: int): int sync {
    println(x)
    return x * 2
}

func main(): int sync {
    println(apply(noisy, 4))
    return 0
}
```

```bash
camel docs/tutorial/examples/10_pure_callback_wrong.cml
```

```text
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> ((x: int) => int, int).
   --> docs/tutorial/examples/10_pure_callback_wrong.cml:16:18
    |
 16 |     println(apply(noisy, 4))
    |                  ^^^^^^^^^^
help: Check the number and types of arguments to ensure they match one of the available overloaded function signatures. Available overloads:
          apply: (f: (x: int) => int pure, x: int) => int

camel: 1 error
```

注意错误里那条 `Available overloads`：它把要求的签名原样写出来（`(f: (x: int) => int pure, x: int) => int`）。
`pure` 出现在函数类型里的意思就是它在类型里——不是注释，不是约定，是匹配规则的一部分。

## 在图上

把 [`examples/10_var_order.cml`](examples/10_var_order.cml) 的 `twice` 画出来（`camel docs/tutorial/examples/10_var_order.cml std::rgir`，本书的图都由第 1 章那个脚本生成）：

![twice 的子图：`var c` 是一个格子，两次写入之间有同步边](images/10_var_order.svg)

- `N07` 是 `var c = 0` 建出来的那个**格子**（`:var/new`）：`var` 在图上是"一个建格子的节点 + 普通的读写"，
  不是一个会自己变的变量。
- `N08`、`N09` 是两次 `bump` 调用，**它们之间有一条同步边**（虚线，`type="CTRL"`）：写在同步块里，所以
  后一次在前一次之后。这就是 `2` 的来源。
- `N0A`（`:var/get`）读的是那个格子，`N0B` 是 `GATE`：同步链的末尾把值经它交出去，它一头读值、一头接链尾，
  值原样放行（[设计稿：图上的三种边](../design/effects/edges.cn.md)）。

图上有三类边，这一章把后两类认全了：

| 边 | 管什么 | 从哪来 |
|---|---|---|
| 数据边 | 值从哪来；被用到的节点把"被用到"传给它读的节点 | 值的传递 |
| 同步边 | 先后，并且前一条因此被用到 | 同步块里相邻的语句 |
| 顺序边 | 只排先后，**不**传递"被用到" | 同一资源上的效应之间 |

**只有数据边与同步边决定"执行不执行"**；顺序边只管"都执行时谁在前"。这一条推论很值钱：一个效应只要没人要、
又不在同步块里，它可以整个不执行——这一章开头那个 `1` 就是这么来的。

## 练习

1. 写 `sum_upto(n: int): int sync`：用 `var` 从 1 加到 `n`。为什么这里必须写 `sync`（提示：写在循环体里的那句
   怎么办）？
2. 写 `apply(f: (x: int) => int pure, x: int): int`，用它算 `5` 的平方。把 `println` 传进去会看到什么？
3. 上面 [`examples/10_write_nobody_reads.cml`](examples/10_write_nobody_reads.cml) 的两个函数输出一样。把
   `ordered` 改成**非**同步函数体，输出会不会变？为什么？

参考答案 [`examples/10_exercise.cml`](examples/10_exercise.cml)：

```cml
// The answer to the exercise of chapter 10: a `var` written inside a sync block, and a callback
// that has to be pure.
// Run: camel docs/tutorial/examples/10_exercise.cml

func sum_upto(n: int): int sync {
    var total = 0
    loop i = 1 while i <= n do sync {
        total += i
        i + 1
    }
    return total
}

func apply(f: (x: int) => int pure, x: int): int {
    return f(x)
}

func square(x: int): int pure {
    return x * x
}

func main(): int sync {
    println(sum_upto(4))
    println(apply(square, 5))
    return 0
}
```

```bash
camel docs/tutorial/examples/10_exercise.cml
```

```text
10
25
```

1. `sum_upto` 的累加写在循环体里，而**循环体是它自己的块**：要顺序就得在那里写 `sync`（`loop ... do sync { ... }`）。
   外层的 `sync` 管不到它。
2. `apply(square, 5)` 是 `25`；换成 `println` 就是 `SemanticDiag::NoMatchingFunction`，错误里列出要求的
   `(x: int) => int pure`。
3. 输出还是 `1`：读的仍然是写之前的版本。变的是**保证**——那次写不再有序（多一条
   `SemanticDiag::IgnoredSideEffect`），而且因为它之后没人读这个变量，整条写不执行
   （`SemanticDiag::EffectNotExecuted`）。

## 参考

- [语言参考 4.2 按依赖求值](../language/04_execution_model.md#42-按依赖求值)
- [语言参考 4.3 `sync`](../language/04_execution_model.md#43-sync)
- [语言参考 4.4 `var` 与写入的顺序](../language/04_execution_model.md#44-var-与写入的顺序)
- [设计原则 3.3 资源、`uses` 与 `pure`](../design-principles.cn.md#33-资源uses-与-pure)
- [设计原则 3.4 `sync` 块](../design-principles.cn.md#34-sync-块)
- [设计稿：图上的三种边（数据、同步、顺序）](../design/effects/edges.cn.md)
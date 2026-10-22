# 7. 语句与声明

[English](en/07_statements.md) | 中文

块由语句组成，一行一条；一条语句在哪里结束见 [2.1](02_lexical.md#21-行空白与分隔)。语句是：表达式、`let`/`var` 声明、`return`/`throw`、嵌套的块，以及函数与类型的声明。

---

## 7.1 表达式语句

任何表达式都可以作语句。在同步块里它按顺序执行；在同步块之外，值没有被使用的表达式不执行（[4.2](04_execution_model.md#42-按依赖求值)）。

**被求值了却没有使用的 `T!` 是警告。** 同步块里的语句无论有没有东西取它的值都会执行；这个值是 `T!`（[3.4](03_data_model.md#34-可能为空-t-与可能出错-t)）时，它带着的错误会无声无息地丢掉，编译器为此给出警告（`SemanticDiag::UnusedFallibleValue`）：

<!-- camel-snippet: fragment: the body of save is elided; the four uses of a fallible value are the subject -->

```cml
func save(path: string): int! sync { ... }

func main(): int sync {
    save('a.txt')              // 警告：这个 int! 没有被使用
    let r = save('b.txt')      // 警告：r 从未被使用
    let _ = save('c.txt')      // 有意丢弃：不警告
    return save('d.txt') !! -1 // 被使用
}
```

**纯运算的结果没有被使用是错误。** 一条语句是一个声明为纯的算子的调用（`xs->append<5>`、`sort(xs)`、`a + b`），而它的值没有被使用时（它不是块的最后一条语句，或者块的值被丢弃：声明为 `: void` 的函数体、写在 `foreach` 上的元素函数、没有 `else` 的 `if` 的臂、写成语句的块），它什么也不做：这样的算子返回新值、不修改参数（设计原则 11.3）。这是编译期错误 `SemanticDiag::UnusedPureResult`；`xs` 是变量时，诊断提示改写为重新绑定 `xs = xs->append<5>`（[3.7](03_data_model.md#37-letvar-与引用)）。

使用这个值指用 `!!`、`?->` 或 `match` 拆开它、把它传下去，或者返回它。有意丢弃写 `let _ = ...`（[7.2](#72-let-与-var)）。没有 `else` 的 `if`，其臂给出的值也同样警告；**没有声明返回类型**的 `main` 以一个值结尾时也一样：只有声明了返回类型的 `main` 才有结果（[4.1](04_execution_model.md#41-程序与入口)），没有声明的 `main` 结尾处的错误值无处可去。同步块之外没有什么可警告的：没有人使用的值根本不求值。可以用 `-Wno-unused-fallible-value` 关掉这个警告。

## 7.2 `let` 与 `var`

```
(let | var) 名字 (: 类型)? = 表达式
```

```cml
let n = 42
let ratio: float = 0.75
let names: string[] = []
var count = 0
```

- 必须有初值。类型可以省略，由初值推出；写出类型时初值必须是这个类型（字面量按所写的类型解释：`let a: i32 = 3`、`let b: f32 = 1`、`let c: float = 0.1 + 0.2`；字面量之间的运算按精确值算完再放进去，见 [3.2](03_data_model.md#32-数值)）。其他表达式必须本来就是这个类型，或是它接受的类型：`var acc: V = p`（`p: V`）直接成立，途中不做任何转换（`let x: float = n`，`n: int`，是 `SemanticDiag::DeclaredTypeMismatch`，应写 `float(n)`）。**文本也不转换**：`let x: int = '42'` 是 `DeclaredTypeMismatch`（消息说 `string` 不能作为 `int`），取数应写 `int('42')` 或 `parse_int('42')`。写出的类型是 `T?`、`T!` 时，`T` 的值装入这个形式（[3.4](03_data_model.md#34-可能为空-t-与可能出错-t)）。
- 作用域是所在的块（或模块顶层）。同一作用域里不能重复声明同一个名字；内层的块、lambda 可以遮蔽外层的名字。
- **解构**：元组可以解构，也可以一次声明多个名字：

  ```cml
  let (q, r) = (7 / 2, 7 % 2)
  let a, b = 1, 'x'
  ```

  结构体解构 `let { x, y } = p` 与数组解构 `let [a, b] = xs` 保留，尚未实现（`SemanticDiag::FeatureNotSupported`）；用 `p.x`、`xs[0]`。
- **`let _ = e` 有意丢弃 `e` 的值。** `_` 不是名字：它不绑定任何东西，所以同一个块里可以写任意多次，也不能读它。表达式与任何 `let` 的初值一样求值（在同步块里按顺序）。这是表明有意忽略一个 `T!` 的写法（[7.1](#71-表达式语句)）。只有这一种写法：`var _`、在多个名字中间写 `_` 都是语法错误。
- 模块顶层的 `let` 是全局常量，顶层的 `var` 可以被函数读写（写它的函数声明为 `sync func`），包括被导入的函数读写它所在模块的（[5.3](05_modules.md#53-导出)）。**顶层的声明与书写顺序无关，只看依赖**：`let area = pi * r * r` 可以写在 `let pi` 之前；顶层的值之间成环是 `SemanticDiag::ModuleValueCycle`（[5.6](05_modules.md#56-模块的加载)）。函数体里的局部 `let` 仍是先声明后使用。

## 7.3 `return` 与 `throw`

- `return e` 给出**所在的块**的值。函数体是一个块，所以写在函数体顶层的 `return` 给出函数的返回值；写在分支臂的块里的 `return` 给出的是那个分支的值，不是离开函数（[6.8.1](06_expressions.md#681-if)）。
- 不带值的 `return`、`return null` 用于没有值的块。
- `throw e` 给出一个错误值，与 `return` 同构：它不展开调用栈，没有“传播”与“捕获”。
  - 所在的函数必须把返回类型声明为 `T!`，否则是 `SemanticDiag::ThrowOutsideFallible`。
  - `e` 必须是错误值：`error(kind, message)` 构造的，或 `case error e` 绑定的；否则是 `SemanticDiag::ThrowNotAnError`。
- `raise` 不属于语言，也不是保留字（它是一个普通的名字）；返回错误值用 `throw`。

```cml
func check_age(age: int): int! {
    return if age < 0 then {
        throw error('InvalidAge', 'age is negative')
    } else {
        return age
    }
}

func main(): int sync {
    println(check_age(30) !! 0, check_age(-1) !! 0)
    println(check_age(-1))
    return 0
}
```

```text
30 0
error InvalidAge: age is negative
```

## 7.4 函数

### 7.4.1 声明

```
标注* (with <with 形参>)? export? 修饰符* func 名字(形参): 返回类型 sync? { 语句 }
```

```cml
func add(a: int, b: int): int {
    return a + b
}

func hello(name: string) sync {      // 没有返回值；同步的函数体
    println('hello', name)
}
```

- 每个形参写 `名字: 类型`。前面加 `var` 的形参是可变的引用（[4.4](04_execution_model.md#44-var引用与-wait)）。
- 返回类型可以省略，由函数体推出；递归函数必须写出返回类型。函数体里没有 `return` 时，函数的值是函数体最后一条语句的值：那是一个有值的表达式时返回它的值，否则返回 `void`。注意 `println(x)` 的值是 `x`（便于 `x->println->f` 这样接着写），所以 `func main() sync { println('a') }` 返回字符串 `'a'`，默认流水线又把 `main` 的结果写到输出，这一行会出现两次；把返回类型写成 `: void`，或以 `return 0` 结束。
- 函数体前的 `sync` 使函数体成为同步块；`func` 前的 `sync` 是函数修饰符。两者含义不同（[4.3](04_execution_model.md#43-sync)）。
- 函数可以声明在模块顶层、命名空间里，也可以声明在块里（局部函数）。函数之间可以相互递归，不要求先声明后使用。
- 形参默认值 `b: int = 5` 保留，尚未实现（`SemanticDiag::FeatureNotSupported`）；用重载或 `with` 形参。

### 7.4.2 `with` 形参

Camel 区分两种参数：函数要处理的**输入**（普通形参，流过函数的数据）和函数被配置的**属性**（`with` 形参，即上下文参数）。别的语言里靠对象的成员持有的配置与状态，在 Camel 里是函数的 `with` 形参；模型的权重适合写成 `with` 形参。

```cml
with <lo: int, hi: int>
func clamp(x: int): int {
    return if x < lo then lo else if x > hi then hi else x
}

func main(): int sync {
    println(clamp<0, 10>(42))
    println([-5, 3, 99]->map<(x: int): int => x->clamp<0, 10>>)
    return 0
}
```

```text
10
[0, 3, 10]
```

`with` 形参不是泛型参数，不能用来声明类型参数。

### 7.4.3 重载

同一个块里同名而形参类型不同的函数是重载，各有自己的子图。调用按书写顺序取第一个形参接受实参的。

```cml
func show(x: int): string { return 'int ' + string(x) }
func show(x: string): string { return 'string ' + x }

func main(): int sync {
    println(show(1), show('a'))
    return 0
}
```

```text
int 1 string a
```

- 形参类型完全相同的第二个同名函数是 `SemanticDiag::Redeclaration`（返回类型与形参名不参与区分）。
- 泛型函数不能与其他函数同名。
- 代表一组重载的名字不能当值用（[6.9](06_expressions.md#69-lambda-与函数类型)）。

### 7.4.4 修饰符

| 修饰符 | 含义 |
|---|---|
| `sync` | 函数有副作用、需要被同步（[4.3.3](04_execution_model.md#433-sync-func函数修饰符)） |

没有别的修饰符。C++ 实现的函数写成无函数体的 `export func` 声明（[6.4](../design-principles.cn.md)），只在模块内使用的辅助函数写成不导出的 `func`：`func` 本身就是模块私有。

`#macro` 不是修饰符，是写在函数声明前的标准标签（[4.5](04_execution_model.md#45-macro-函数)）。

### 7.4.5 运算符重载

```
func operator 运算符(形参): 返回类型 { 语句 }
```

（写法暂定。）运算符是函数的调用（`a + b` 是 `__add__(a, b)`，[2.4](02_lexical.md#24-关键字)），程序可以为自己的类型添加这些函数的重载：

```cml
type Vec = { x: float, y: float }
type Mask = { bits: int }

func operator +(a: Vec, b: Vec): Vec { return { x: a.x + b.x, y: a.y + b.y } }
func operator -(a: Vec): Vec { return { x: 0.0 - a.x, y: 0.0 - a.y } }    // 一元
func operator *(k: float, v: Vec): Vec { return { x: k * v.x, y: k * v.y } }
func operator [](v: Vec, i: int): float { return if i == 0 then v.x else v.y }
func operator &(a: Mask, b: Mask): Mask { return { bits: a.bits * b.bits } }

func main(): int sync {
    let v: Vec = { x: 1.0, y: 2.0 }
    var acc: Vec = { x: 0.0, y: 0.0 }
    acc += v                         // acc = acc + v
    let w = 2.0 * (v + acc)
    println(w.x, w[1], (-v).x, 1 + 2)
    return 0
}
```

```text
4.0 8.0 -1.0 3
```

- **可以重载的**：二元的 `+` `-` `*` `/` `%` `^` `@` `==` `<` `<=` `>` `>=` `&` `|`，下标 `[]`（被取下标的值与至少一个下标），一元的 `-` `!` `~`。`-` 有一个形参是取负，两个是减法。
- **重载 `==` 即得到 `!=`**（设计原则 4.3）：用到这个 `==` 重载的 `a != b` 就是 `!(a == b)`，两者不会不一致；这个重载要返回 `bool`，否则在用到 `!=` 的地方是 `SemanticDiag::OperatorOverloadInvalid`。`!=` 本身不能单独重载（`SemanticDiag::OperatorNotOverloadable`）。
- **不能重载的**：`&&`、`||`（分支）、`??`、`!!`、`?->`（对空值与错误的分支）、`->`（把值交给函数）、`..`、`as`、赋值、`!=`（跟随 `==`），以及 `===`、`!==`。写了是 `SemanticDiag::OperatorNotOverloadable`，诊断给出原因。复合赋值没有自己的重载：`a += b` 就是 `a = a + b`，跟随 `+` 的重载。
- 运算符的形参个数不对、带 `with` 形参或 `var` 形参，是 `SemanticDiag::OperatorOverloadInvalid`。泛型的重载把类型参数写在运算符名字后的 `::<…>` 里（`func operator +::<T>(a: T[], b: T[]): T[]`），不写在形参类型里。
- **各层的重载合成一组。** 其他名字只取最近的一层（局部 > 本模块 > 导入的 > 内建），运算符例外（设计原则 10.2）：本模块的、导入的模块导出的、内建的与 C++ 模块的重载是同一组，一次调用取接受这些操作数的那一个。
- **更具体的重载优先**（设计原则 4.3）。“更具体”的定义：一个程序的重载，形参类型恰好就是操作数的类型（不经装入 `T?`、`T!`），而其他接受这些操作数的都只是把它们当作某一族类型中的一员——内建或 C++ 模块中按类型规则接受多种类型的算子（数组、元组、结构体的按内容比较，张量上的运算），或泛型的重载——这时调用取这个程序的重载。于是结构体可以有自己的相等：`func operator ==(a: V, b: V): bool` 让 `p == q`（`p`、`q` 是 `V`）调用它，而不是按内容比较；其他结构体类型仍按内容比较。内建算子中为恰好这些类型声明的（`int + int`、`float == float`）与之同样具体：声明这样的重载是 `SemanticDiag::OperatorOverloadConflict`。
- 除此之外有两个都接受时是 `SemanticDiag::AmbiguousCall`，诊断列出它们（两个泛型的重载、两个模块为同样的形参类型各给一个）；同一模块里形参类型相同的两个重载是 `SemanticDiag::Redeclaration`。普通调用与部分应用（`f<w>` 作值）遇到同一遍里有多个候选时也报这个诊断。
- **导出**：`export func operator +(...)`。导入一个模块（`import { V } from m`，或 `import m`）就带来它导出的全部运算符重载，与 C++ 模块的运算符一样，不必也不能按名字导入。
- **命名空间块里声明的运算符属于模块**（设计原则 4.3）：它的名字不带命名空间（运算符没有可以限定的名字），与写在模块顶层的一样导出、一样参与重载；它的签名与函数体在所在的块里查找名字，可以不带前缀使用同一块里的类型。
- 运算符的重载在图上就是一次普通的函数调用。`__x__` 形式的名字仍是保留的，程序不直接写。

## 7.5 泛型函数

泛型函数的类型参数在函数头声明，写在函数名后的 `::<…>` 里。函数名后有 `::<…>` 就是泛型，没有就不是。

```cml
func first::<T>(xs: T[]): T {
    return xs[0]
}

func pick::<T>(c: bool, a: T, b: T): T {
    return if c then a else b
}

func my_map::<T, U>(xs: T[], f: (x: T) => U): U[] {
    return xs->map<f>
}

func main(): int sync {
    println(first([1, 2, 3]), first(['a', 'b']))
    println(pick(false, 1.5, 2.5))
    println(my_map([1, 2, 3], (x: int): string => string(x) + '!'))
    return 0
}
```

```text
1 a
2.5
[1!, 2!, 3!]
```

- 泛型函数是一张**子图模板**。每用一组具体类型调用，就实例化出一张类型全部具体的子图（同一组类型只实例化一次；`std::rgir` 里可以看到名为 `first<int>`、`first<string>` 的子图）。图上只有具体类型，运行时不需要类型信息。
- **类型变量从实参得到类型。** 每个实参按它所在形参的类型与形参统一；实参什么都没说时（是 `null`，或是一个自己还在等类型的泛型函数），取它在别处出现的位置上的类型。所有实参给出的约束收集完再检查：每个形参按代入后的类型检查，不做隐式转换（值进入 `T?`/`T!` 形参除外，见 [3.4](03_data_model.md)）。`pick(true, 1, 2.5)` 是错误（`T` 已绑定为 `int`）；定不下来的变量是编译期错误，诊断指出是哪一个。
- 函数头声明的名字可以在签名的任何类型位置上写：数组元素、元组元素、结构体字段、函数类型的形参与返回值、`T?`/`T!` 的内层、`Map`/`Set` 与类型函数的实参。不能作为模块类型的实参。
- 只有函数头声明的名字才是类型变量。形参类型里一个不指向任何类型、也没有在函数头声明的名字是未知类型错误（`SemanticDiag::UnresolvedTypeReference`），不会被自动绑定。同一个名字在函数头声明两次是 `SemanticDiag::TypeVariableRedeclared`；声明了却不在签名里写是警告 `SemanticDiag::TypeVariableUnused`，声明仍然可用。函数头声明的名字与已有类型同名时，在这个函数里它是类型参数：头部的声明优先。
- 只出现在返回类型中的变量由期望类型推出：`let xs: int[] = empty()`、已知形参类型的位置、声明的返回位置。没有足够上下文时写显式类型实参：`empty::<int>()`。类型实参按函数头里变量声明的顺序给出，也可以按名字给出，不依赖位置：`pair::<A = int, B = string>(..)`。同一处要么全按位置、要么全按名字，不能混写；按名字时每个类型变量都要给出，名字写错或重复是编译错误；内建算子（`tree`、`json::decode_as` 等）只接受按位置。类型实参只在编译期参与实例化；`f<w>` 继续表示值上下文参数，合用时为 `f::<T><w>(x)`。显式实例也可以作函数值：`let f = id::<int>`。
- **函数体在实例化时检查。** 函数体里的类型错误在实例化时报告，诊断给出错误在函数体里的位置，并指出是哪个实例、由哪一处调用实例化的。从未被调用的泛型函数不会被检查。
- 递归的泛型函数可以用（要写出返回类型）。每次递归都换成更大类型的函数无法终止实例化，嵌套到 8 层时报错。
- 函数体里可以用 `T`：lambda 的形参类型、`let z: T = 0`、`e as T`。
- **导出**：泛型函数可以 `export`，别的模块 `import` 之后照常调用（也可以写限定名 `lib::f(x)`）。导出的是模板；全程序每组类型只有一个实例，不论哪个模块调用它。函数体里的名字在声明它的模块里解析，所以可以使用那个模块里没有导出的函数、别的泛型函数、它自己的导入和它顶层的值。

`with` 形参的类型同样可以使用函数头声明的名字：

```cml
with <f: (acc: U, x: T) => U, init: U>
func my_fold::<T, U>(xs: T[]): U {
    return xs->reduce<f, init>
}

func main(): int sync {
    println(['a', 'bb']->my_fold<(acc: int, s: string) => acc + len(s), 0>)
    return 0
}
```

```text
3
```

**泛型函数可以出现在任何期望一个函数的位置**，包括传给另一个泛型函数。编译器把它的实例化延迟到类型变量能从上下文确定时；延迟只发生在编译期，图上只有具体的实例。

```cml
func apply::<T, U>(f: (x: T) => U, x: T): U { return f(x) }
func id::<A>(x: A): A { return x }
func pair::<A>(x: A): (A, A) { return (x, x) }

func main(): int sync {
    println(apply(id, 3), apply(pair, 'a'))   // T 来自 3：id 实例化为 id<int>，于是 U = int
    println([[1, 2], [3, 4]]->map<pair>)      // 交给内建的高阶算子
    let f: (x: int) => int = id               // 写出的类型决定实例
    println(f(4))
    return 0
}
```

```text
3 (a, a)
[([1, 2], [1, 2]), ([3, 4], [3, 4])]
4
```

- 上下文可以是：另一个函数的形参（函数类型）、泛型函数的其他实参、声明的类型（`let f: (x: int) => int = id`）、声明的返回类型（`return id`）、内建的高阶算子（`map`、`filter`、`reduce` 等）。内建算子的类型规则是 C++ 代码，说不出“期望哪个函数”，编译器用其余实参的元素类型去试，取算子接受的那个实例。
- 确定不了是编译期错误 `SemanticDiag::GenericFunctionAsValue`（或作为实参时的 `SemanticDiag::GenericArgumentsMismatch`），指出哪个变量定不下来：`let g = id` 没有上下文（写出类型）；`apply(id, id)` 两个实参互相等对方。
- 导入的泛型函数同样可以作值（`xss->map<reverse>`，`reverse` 来自 `array` 模块）。

**剩余参数（rest parameter）**：函数声明的最后一个形参可以写 `..ts`，它没有类型，代表调用时剩下的全部实参。调用点写出的固定实参依次绑定到固定形参，其余实参按顺序成为 `ts` 的元素；`ts` 是编译期已知的元组，可以用 `indices ts`、`ts.[j]` 和 `..ts`。

```cml
func sum(..ts): int {
    return static loop acc = 0 for (j) in indices ts do acc + ts.[j]
}

func main(): int sync {
    println(sum(1, 2, 3))
    println(sum(..(4, 5)))
    return 0
}
```

```text
6
9
```

- **调用在编译期展开。** 带剩余参数的函数没有运行时的参数个数，因此不为它建实例图，也不生成调用结点；每个直接调用都把函数体展开在调用者的图里，固定形参绑定到实参结点，`ts` 绑定到剩下的结点。`ts` 可以是 0 个元素。
- **`ts` 是编译期元组，不是值。** 它只有三种用法：`indices ts` 取位置表、`ts.[j]` 取一个元素、`..ts` 把元素展开成实参。三种用法都不为 `ts` 建 `FILL`，也不为元素建 `ACCS`。把 `ts` 本身放到需要运行期值的位置（打印、返回、赋给变量、作为普通实参）报 `SemanticDiag::StaticTupleEscapes`，不会悄悄物化成一个运行期元组；需要运行期元组时写元组字面量 `(a, b)`。
- **剩余实参**写作 `..e`，且必须是调用实参表的最后一项。`e` 必须是编译期元组：元组字面量、元组推导式，或外层剩余函数的 `..ts`。运行时的元组值没有固定个数，是 `SemanticDiag::RestArgumentNotTuple`；`..e` 后面还有实参是 `SemanticDiag::RestArgumentNotLast`。
- 剩余形参必须在最后（`SemanticDiag::RestParameterNotLast`），不能写类型（`SemanticDiag::RestParameterTypeNotSupported`），只能出现在函数声明里（lambda、函数类型和规则里写 `..` 是语法错误）。固定形参少给实参是 `SemanticDiag::RestArgumentsMismatch`。
- 带剩余参数的函数**不是值**：不能赋给变量、作为实参或导出后当函数值用（`SemanticDiag::RestFunctionNeedsExpansion`），只能直接调用。调用可以跨模块：函数体仍然在声明它的模块里解析（和泛型实例一样），展开在调用者的图里；函数体里用到声明模块的顶层**值**时，那个值在调用者的图里没有结点，会报 `SemanticDiag::RestFunctionNeedsExpansion`，函数和它自己的实参不受影响。
- `with` 实参命名泛型函数或一组重载时（`tree_map<twice>`、`tree_map<bump>`），调用在被调函数里按每处调用的实参类型解析它；这要求被调函数带剩余参数（实例在调用点展开），普通的泛型函数仍按参数类型选定一个函数值。

**常量值参数（`constas`）**：普通形参里可以写 `constas n: int`，它的值在编译期已知，是声明身份的一部分，而不是运行时的实参。

```cml
import { Tensor, ones, sum } from tensor

func total(constas n: int, x: Tensor<f32, [n]>): float {
    return sum(x)
}

func main(): int sync {
    println(total(3, ones([3])))
    println(total(5, ones([5])))
    return 0
}
```

```text
3.0
5.0
```

- **值进入实例键。** 每个不同的常量值实例化出一张子图（`total<3>`、`total<5>`），同一组类型和值只实例化一次；值与类型合起来才是键，只差一个字符串也是两个实例（`label<"w">`、`label<"h">`）。
- **任意个、任意位置。** 一个声明可以有多个 `constas` 形参，位置在普通形参之间任意；调用把值写在它所在的位置（`place(1, 2)` 给 `x = 1`、`n = 2`）。每个值各自进入实例键。
- **类型不止 `int`。** `string`、`int`、`float`、`bool` 等有编译期常量的类型都可以；值必须与形参类型相容。类型要写出来（`SemanticDiag::ConstasTypeRequired`）。`enum` 成员目前还不能作为编译期值传递（`SemanticDiag::ConstasValueNotConstant`），要等 `enum` 值有了编译期表示。
- **值由函数体读取。** `n` 在实例体里是一个编译期常量：可以当类型里的具名维度（`Tensor<f32, [n]>`、`[n]`），可以当字段名（`t.[n]`），也可以当普通值用于算术、条件、打印。它不需要被任何类型读到：只有函数体读它也是合法的。
- **不是运行时参数。** 调用点要给出常量，但它不进入调用结点：实例的形参表里没有 `n`，调用只传其余的实参（`total(3, xs)` 的调用只有一个实参 `xs`）。
- **值可以转手。** 一个 `constas` 值可以传给另一个 `constas` 参数，也可以来自 `static for` 绑定的字段名、`indices` 的位置这类编译期名字。`func columnOf(constas n: string, ..ts) { return column(n, ..ts) }` 合法：`tree_map` 正是这样把字段名和树一起交给 `column`。
- **和剩余参数一起用。** `func column(constas n: string, ..ts)` 合法：值在展开前绑定，`ts` 得到剩下的实参。设计稿 3.1.1 的 `column` 今天可以写成

  ```cml
  func column(constas n: string, ..ts): (int, int) {
      return ( static for (j) in indices ts => ts.[j].[n] )
  }
  ```

  两个只差字符串的调用是两张图，各自取 `n` 命名的字段。
- 带 `constas` 参数的声明**没有函数值**：常量必须在写调用时给出，不能赋给变量或作为实参传走（`SemanticDiag::ConstasFunctionNeedsCall`）。无体声明（C++ 算子的声明）也不能带 `constas`（`SemanticDiag::ConstasInNativeDeclaration`）。
- 值必须是编译期常量（`SemanticDiag::ConstasValueNotConstant`）：运行期的值不行，因为它要选实例。目前常量可以是字面量、另一个 `constas` 值、`static for` 的 `n` 和 `indices` 的 `j` 这类编译期名字。
- `n` 只在实例体里绑定；函数体里的类型位置（`let z: Tensor<f32, [n]>`）还没有这个作用域。数组写法 `f32[n]` 同样可以。

**目前的限制**：泛型函数不能加标注；带上下文实参的泛型函数（`f<w>`）不能作值；带 `constas` 参数的函数不能作值，也不能和无体声明组合，`enum` 值还不能作为 `constas` 实参。设计稿 3.1.1 的 `column(constas n, ts)` 省写了类型，今天要写成 `column(constas n: string, ..ts)`。

## 7.6 标注

函数声明之前可以写 `@名字` 或 `@名字<with 实参>`。标注是一个接受函数、返回函数的函数；声明的名字绑定到标注返回的那个函数上。

```cml
with <n: int>
func times(f: (x: int) => int): (x: int) => int {
    return (x: int): int => f(x) * n
}

@times<3>
func inc(x: int): int { return x + 1 }

func main(): int sync {
    println(inc(1))        // (1 + 1) * 3
    return 0
}
```

```text
6
```

对带标注的函数的调用按标注返回的那个函数的形参检查。带标注的函数可以参与重载。

标注也可以写在块之前。这时块是一个没有形参的函数，交给标注，标注返回的函数就在写下块的地方被调用。除此之外，块的行为与不带标注时相同：块里的名字在块后不可见，块里的 `return` 给出块的值；块的值没有被使用时，其中的语句按不带标注的块那样执行（[7.8](#78-块)）。

```cml
func doubled(f: () => int): () => int {
    return () => f() * 2
}

func next_doubled(x: int): int {
    @doubled
    {
        return x + 1
    }
}

func main(): int sync {
    println(next_doubled(4))    // (4 + 1) * 2
    return 0
}
```

```text
10
```

库可以声明一种**授予属性**的标注：它不包装所写的代码，而是在建图时（实参是常量时）给那个函数或块的节点（连同其中写下的分支臂、循环体与 lambda，不含它们调用的函数）加上一个属性，供这个库的遍与后端读取。属性的值与合并规则由库为它的键声明（`camel/core/node_attrs.h`）。这样的标注不改变代码计算的结果。

### 7.6.1 数值许可标签

数值许可是标签（设计原则 7.2、7.5）：写在函数声明或块之前的 `#reassociate`、`#contract` 等，授予那段代码改变浮点结果的许可。它们不是函数的标注：不改变函数的值，只放宽规范算法（[3.2.1](03_data_model.md#321-浮点的数值契约)）。

`reassociate` 与 `contract` 是 7.3 的默认许可，不写标签也允许，写出来只是把它写明；其余许可都要写标签才生效。`#strict`（7.4）相反：它让那段代码按规范算法逐位计算，不使用任何许可，默认许可也一并撤销。

| 标签 | 授予 | 默认 |
|---|---|---|
| `#reassociate` | 改变归约（一串加、乘，`sum`、矩阵乘的内积）的次序或结合方式 | 允许 |
| `#contract` | 把一次乘与一次加收缩为一次乘加融合 | 允许 |
| `#approx_math` | 使用近似的数学函数（包括近似的除法与开方） | 不允许，要写明 |
| `#change_precision` | 降低或提高输入或累加的精度（如 TF32、bf16，或用 f64 累加 f32） | 不允许，要写明 |
| `#finite_math` | 假设没有 NaN、没有无穷、不区分零的符号、次正规数可以当作零；四项也可以分开写：`#no_nans`、`#no_infs`、`#no_signed_zeros`、`#flush_subnormals` | 不允许，要写明 |
| `#nondeterministic` | 允许结果随调度变化的做法（如浮点原子加） | 不允许，要写明 |

```cml
import { sqrt } from math

#[reassociate, contract]
func dot(a: float, b: float, c: float, d: float): float {
    return a * b + c * d          // 可以求值为 fma(a, b, c * d)
}

func norm(x: float, y: float): float {
    let s = x * x
    #approx_math
    {
        return sqrt(s + y * y)    // 块里的代码可以用近似的开方
    }
}

#strict
func canonical(x: float, y: float): float {
    return x * y + 1.0            // 规范算法：没有默认许可，也不使用任何许可
}
```

- **作用范围是写下它的那段代码**：函数体或块，以及词法上嵌在其中的分支臂、循环体与 lambda。块上的许可与外面函数上的合起来。
- **被调用的函数不继承**：`dot` 调用的函数仍按规范算法，除非它自己写了许可标签。内联之后各节点仍带各自的许可。
- **`#strict` 撤销一切**：它标记的函数或块（连同其中的分支臂、循环体与 lambda）的有效许可为空集——默认的 `reassociate`、`contract` 不再可用，内层写下的许可也不生效。外层与内层同时写着时，`#strict` 优先。
- **许可只是允许，不是要求**。CPU 后端（`std::nvm`、`std::fvm`、`std::jit`、`std::prl`、`std::cpp`）总是给出规范结果；以后的后端（如 Tiles）可以在有许可的节点上用别的算法。`std::opt` 默认使用 7.3 的默认许可，`std::opt(safe_fp=true)` 按 `#strict` 一样的策略工作。
- 没有模块级的默认许可，也没有命令行或流水线开关；后端的入口（如 `tiles.run`）不接受许可。
- 许可记在图的每个节点上，`std::rgir`、`std::gir` 在有许可的节点上显示 `numeric="..."`；`#strict` 是代码上的标签，出现在节点的 `attrs="strict"` 里。公共子表达式合并与张量的融合不把许可不同的节点合在一起。

### 7.6.2 标签：`#`

`#` 不再开始注释（文件第一行的 `#!` 除外），而是**标签**（设计原则 6.2）。标签是与函数、类型同一个命名空间里的名字：在本模块声明、按名字导入、或用 `模块::标签` 限定。

标签在模块顶层声明，`on` 之后是它可以写的位置：

```cml
export tag kernel on nodes                  // 写在块、函数或类型之前，属于其中的节点
export tag fast on func, nodes              // 也属于函数的声明
export tag width<w: int> on type, field     // 带参数，参数是编译期值
tag finite = [kernel, fast]                 // 标签组：成员一起生效
```

位置有 `func`（函数声明）、`type`（类型声明）、`field`（结构体字段）、`nodes`（区域内写下的节点）与 `param`（保留）。`read_by` 说明哪些遍读取这个标签，流水线里一个都没有时，在写下标签的地方报告一条警告；只有声明自己读取标签事实的遍（内置的 GIR/JSON 输出遍与 `std::opt`，模块遍如 `tiles::compile`、`tensor::fuse`）才算读取者；用 Camel 写的遍也可以在函数上用 `#reads<标签, ...>` 声明自己读哪些标签，与 `read_by` 同样算数。程序不得声明与核心许可（以及 `finite_math`、`strict`）同名的标签（如 `tag reassociate`）：那样的名字始终指核心的标签，声明永远读不到，`SemanticDiag::TagShadowsPermission` 在声明处拒绝。

语言核心自带一组**数值许可标签**（设计原则 7.2），不必导入，写在 `on nodes`：`reassociate`、`contract`、`approx_math`、`change_precision`、`no_nans`、`no_infs`、`no_signed_zeros`、`flush_subnormals`、`nondeterministic`（允许结果随调度变化，如浮点原子加），以及把它们前四项合在一起的组 `finite_math`；`strict`（7.4）也是核心的标签，写在 `on func, nodes`。`#contract` 落在节点的许可列上（dump 里的 `numeric="..."`），`#strict` 落在节点的属性集里（`attrs="strict"`），读者通过同一个查询（`camel/core/tags.h` 的 `permissionsOf` / `effectivePermissions`）取得许可：`effectivePermissions` 对 `#strict` 返回空集。默认值见 7.3：`reassociate` 与 `contract` 本来就允许，其余要写明才生效。

规则的声明之前可以写 `#requires<许可, ...>`：实参是许可名、核心的组 `finite_math`，或本模块用 `tag 名字 = [许可, ...]` 声明的组（成员递归展开，组名等于其中各许可的并集）。规则只在节点所在的数值策略满足这些许可时才被应用（[46 规则](../technical/46_rules.md)）。要求写成普通的标签、写成未声明的名字，或者在规则之外写 `#requires`，都是定位到该处的错误，不会静默放过。

`#` 有三种写法，可以写在函数、类型声明、结构体字段或块之前：

<!-- camel-snippet: fragment: the three tag forms are illustrative; width is a tag a module declares, and the bodies are elided -->

```cml
#kernel
func matmul(a: T, b: T): T { ... }
```

<!-- camel-snippet: fragment: the tag forms are illustrative; width is a tag a module declares -->

```cml
#width<32>
type Packed = {
    #width<8> a: int,
    b: int,
}

func main(): int {
    #fast
    {
        // 块里的节点带着 fast
    }
    return 0
}
```

`#name`、`#name<实参>` 与 `#[a, b<v>]`（标签组）。标签的参数是编译期值：字面量、顶层常量或函数名。实参必须是声明的形参类型所指的那种常量——`int`、`float`、`bool`、`string`，或 `func` 的实参（函数名）；浮点实参按位记为 real 属性，`1.5` 就打印为 `1.5`。写到声明没有列出的位置是错误，位置在标签上。参数个数与声明的参数不符、实参不是编译期值、实参的种别与形参声明的类型不符（`SemanticDiag::TagArgumentType`）、形参的类型标签还载不了（`SemanticDiag::TagParameterType`），也都在标签上报告。

`on nodes` 的标签是**节点的属性**（`camel/core/node_attrs.h`）：写在前面的标签对那个函数体或块里写下的节点生效，与外面同名的标签按属性的合并规则累积，内层带参数的标签取内层的值。`on func`、`on type`、`on field` 的标签属于声明本身，C++ 模块用 `tag::factsOf` 一处读取（标签、许可、读者；`tag::ofDeclaration` / `tag::ofField` 仍在）；在 Camel 代码里读它们要等泛型的第一阶段。

形参类型还可以是 `func`：这时实参是一个函数名，标签的事实是那个函数的**图引用**（`attr::graphRef`），不是函数值。GIR 构建器把该图记进函数图的引用表（`kGraphRefRule`），于是冻结后的图直接指向这个函数，运行期不必再按名字解析，也不需要闭包捕获；因此 `func` 实参只能是程序里的静态函数，载不了捕获了外围值的闭包。自动微分模块就是这样声明 `export tag vjp<rule: func> on func` 的：`#vjp<rule>` 写在函数声明之前，调用这个函数时用规则而不是函数体的导数（[26 自动微分](../technical/26_autodiff_as_graph_rewrite.md)）。`@vjp<rule>` 在标注处报 `SemanticDiag::InvalidAnnotation`，应写 `#vjp<rule>`。

`@名字(实参)` 不是标签的写法：它按标注报告，并指出应写 `#名字`。

## 7.7 类型声明

```
export? type 名字 (<参数, ...>)? = 类型
```

```cml
type Point = { x: int, y: int }
type Signal = (int, int)
type Pred = (p: Point) => bool
```

`type` 给类型起名字；名字与它代表的类型可以互换使用。

**带参数的类型声明是类型函数。** 在类型位置上用尖括号“调用”它，得到右侧把参数替换成实参之后的类型。它在建图时求值，图上不留节点。

```cml
type Pair<T> = (T, T)
type Entry<K, V> = { key: K, value: V }
type PairList<T> = Pair<T>[]

func sum_pair(p: Pair<int>): int { return p.0 + p.1 }

func swap::<T>(p: Pair<T>): Pair<T> {          // 与函数头的类型参数组合
    return (p.1, p.0)
}

func key_of::<K, V>(e: Entry<K, V>): K {
    return e.key
}

func main(): int sync {
    println(sum_pair((1, 2)), swap(('x', 'y')))
    println(key_of({ key: 'id', value: 7 }))
    return 0
}
```

```text
3 (y, x)
id
```

- 实参只能是类型，个数必须与参数一致；不带实参使用是错误。
- 右侧只能引用已经声明的类型，所以类型函数不能引用自身。
- 可以 `export`，在别的模块里以 `模块::Pair<int>` 使用。
- 模块类型（如 `Tensor<f32, const [2, 3]>`）的尖括号由该类型自己解释，实参可以是类型或 `const 数据`。

类型表达式里保留、尚未实现的部分：类型运算 `&`、`\`、`+`、`-`、`^`，以及 `keyof`、`名字 of T`（`SemanticDiag::FeatureNotSupported` 或语法错误），它们的含义尚未决定。`typeof e` 已实现：它是一个完整的类型，取表达式 `e` 的值的类型，在使用这个类型的位置由编译器展开；`typeof` 不能嵌在更大的类型里（`typeof(1)[]` 报 `SemanticDiag::TypeOfNotWholeType`），操作数没有值（`void`）时报 `SemanticDiag::TypeOfNotAValue`。

## 7.8 块

```
sync? { 语句 ... }
```

块是表达式：它的值由块里的 `return` 给出，没有 `return` 时没有值。块引入一个作用域。带 `sync` 的块按书写顺序执行其中的语句（[4.3.1](04_execution_model.md#431-同步块块修饰符)）；不带 `sync` 的块按依赖求值。

块用作函数体、分支臂、lambda 体，也可以单独作语句。




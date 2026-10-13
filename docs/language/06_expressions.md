# 6. 表达式

[English](en/06_expressions.md) | 中文

Camel 里几乎一切都是表达式：`if`、`match`、块、赋值都有值。本章逐类说明。

---

## 6.1 求值

表达式是图上的节点，操作数是它的输入边。一个表达式是否求值、何时求值，由依赖决定（[4.2](04_execution_model.md#42-按依赖求值)）；运算符的优先级与结合性只决定图的形状（[6.12](#612-运算符优先级)）。`&&`、`||`、`??`、`!!`、`?->` 与 `if`、`match` 是分支：没有被选中的一侧不求值。

## 6.2 字面量与构造

```cml
42          3.14        'text'      true        null
[1, 2, 3]                   // 数组
(1, 'one')                  // 元组；单元素写 (x,)
{ x: 1, y: 2 }              // 结构体
(x: int): int => x + 1      // lambda，见 6.9
```

- 数组字面量的元素类型必须相同。空数组 `[]` 的元素类型来自上下文：`let xs: int[] = []`、`[] as Map<string, int>`。
- 结构体字面量的键是标识符。
- **列表推导** `[e for x in xs if c]` 就是 `xs->filter<(x) => c>->map<(x) => e>`：`x` 的类型是 `xs` 的元素类型，`if c` 可以省略，结果是 `e` 的数组。图上与手写这两个调用完全相同（两个算子节点，各带一张元素函数的子图），所以它的性质也相同：元素之间没有顺序约定（并行调度器可以并发地算），`e`、`c` 里的副作用与 `map`、`filter` 的元素函数里的一样处理（[6.10](#610-高阶算子)）。可以嵌套，也可以出现在任何需要表达式的位置。`xs` 不是数组是 `SemanticDiag::ComprehensionSourceRefused`。
- 展开 `...xs` 保留，尚未实现（`SemanticDiag::ConstructNotSupported`）。写成 `concat(xs, ys)`。

## 6.3 访问与下标

| 写法 | 含义 |
|---|---|
| `p.x` | 结构体字段。不存在的字段是编译期错误 `SemanticDiag::InvalidAccessIndex` |
| `t.0` | 元组成员，下标是字面量，越界是编译期错误 |
| `xs[i]` | 数组元素。越界（含负数）是故障 `RuntimeDiag::IndexOutOfRange` |
| `m[k]` | 映射查找，类型是 `V?`（[3.5](03_data_model.md#35-映射与集合)） |
| `mod::name` | 限定名 |

- 对数组不能用 `.`（`xs.1` 是 `SemanticDiag::TypeNotIndexable`）。
- 下标紧贴被访问的表达式，不能有空白。
- 切片写法 `xs[a:b]`、`xs[a:b:c]` 保留，尚未实现：它没有专门的诊断，编译器按“下标运算符没有接受这些实参的重载”报告 `SemanticDiag::NoMatchingFunction`。用 `xs->slice<a, b>`。
- **字符串下标按字节**：`s[i]` 是第 `i` 个字节，结果是只含这一个字节的字符串；越界同样是故障。多字节字符的单个字节本身不是合法的 UTF-8 文本，按字符读取用 `str` 模块的 `char_at`、`codepoints`（见 [标准库参考](../stdlib-reference.md)）。下标的结果是单字节字符串还是数值，是当前实现的行为，尚待确定。

## 6.4 调用

```cml
with <factor: int>
func scale(x: int): int { return x * factor }

func main(): int sync {
    println(scale<3>(5))        // with 实参写在尖括号里
    println(5->scale<3>)        // 管道：左边的值作实参
    return 0
}
```

```text
15
15
```

- 普通实参写在圆括号里，`with` 实参写在尖括号里；两者都必须紧贴函数名。函数没有 `with` 形参时不写尖括号。
- 实参按位置对应形参，类型必须一致：数值之间不做隐式转换；只有进入 `T?`、`T!` 形参的值会自动装入（[3.4](03_data_model.md)）。不匹配是 `SemanticDiag::NoMatchingFunction` 或 `SemanticDiag::ArgumentsMismatch`，诊断里列出可用的签名。
- 同名函数有多个时按重载规则选择（[7.4.3](07_statements.md#743-重载)）。
- **`f<w>` 是部分应用。** 不带圆括号的 `f<w>` 是一个值：一个已经配好上下文参数的函数。`let g = inc<1>` 之后 `g(2)` 等同 `inc<1>(2)`；`w` 在写下 `f<w>` 的地方求值一次。它可以绑定到名字、作实参、作返回值，也可以直接交给高阶算子（`xs->map<inc<10>>`）。
  - 只配了前几个上下文参数时，剩下的仍是结果的上下文参数：`let above = clamp<0>` 之后写 `above<5>(9)`。已经配齐的函数不能再配（`g<2>` 是错误）。
  - `f` 必须是能由这些实参确定的一个函数。同名函数有多个时，取唯一一个接受这些上下文实参的；仍有多个时由所在位置的函数类型（形参）决定，都定不下来是 `SemanticDiag::PartialApplicationRefused`。
  - 图上它是一个携带了捕获值的函数值，与闭包同构：自己的一张子图，配好的实参通过端口进入，没有新增节点种类。
  - 尚未支持：带 `var` 上下文参数的函数（`SemanticDiag::ConstructNotSupported`）、类型规则随实参变化的内建算子（如 `len`；`SemanticDiag::FunctionNameAsValue`）。这两种情形把调用写在 lambda 里。
- 关键字实参 `f(a: 1)` 保留，尚未实现（`SemanticDiag::FeatureNotSupported`）。
- 同一个实参表里写两个带尖括号的调用（`f<a>(x), g<b>(y)`）会被读成比较；带尖括号的调用后面再接带尖括号的管道（`f<a>(x)->g<b>`）也可能如此。把它们分成两条语句。
- 写在括号里的 lambda 可以直接调用：`((x: int): int => x + 1)(3)`；也可以直接写在 `->`、`?->` 的右边：`3->(x: int): int => x + 1`、`parse_int(s) ?-> (x: int): int => x * 2`。

## 6.5 管道

`a->f` 把 `a` 作为 `f` 的实参：`a->f` 就是 `f(a)`，`a->f<w>` 就是 `f<w>(a)`。左结合，适合把数据流写成一条链：

```cml
func main(): int sync {
    let total = range(1, 6)
        ->map<(x: int): int => x * x>
        ->filter<(x: int): bool => x % 2 == 1>
        ->reduce<(acc: int, x: int): int => acc + x, 0>
    println('total = {}'->format<total>)
    return 0
}
```

```text
total = 35
```

- `x ?-> f` 见 [6.6](#66-空值与错误的运算符)。
- `f..g`（函数组合）与 `?..` 保留，尚未实现，含义也没有确定：写了是 `SemanticDiag::ConstructNotSupported`。用 lambda：`(x: int): int => g(f(x))`。

## 6.6 空值与错误的运算符

`T?` 与 `T!` 见 [3.4](03_data_model.md#34-可能为空-t-与可能出错-t)。下面三个运算符在图上都是一个分支，条件是值上的一位标记。

| 写法 | 左侧 | 右侧 | 结果 |
|---|---|---|---|
| `a ?? b` | `T?` | `T` | `a` 为空取 `b`，否则取 `a` 里的值 |
| `a !! b` | `T!` | `T` | `a` 出错取 `b`，否则取 `a` 里的值 |
| `x ?-> f` | `T!` | 接受 `T` 的函数 | `x` 出错时把错误原样传下去，否则调用 `f`；结果是 `U!`（`f` 返回 `U` 或 `U!` 都可以） |

```cml
func half(x: int): int! {
    return if x % 2 == 0 then x / 2 else { throw error('Odd', 'not even') }
}

func main(): int sync {
    println(parse_int('12') !! 0, parse_int('twelve') !! 0)
    println(parse_int('12') ?-> half ?-> half !! -1)
    println(parse_int('10') ?-> half ?-> half !! -1)
    return 0
}
```

```text
12 0
3
-1
```

- 右侧只在被选中时求值。
- **右侧可以是可空的。** `a ?? b` 的 `b` 是 `T` 时结果是 `T`；`b` 是 `T?` 时结果是 `T?`。所以 `a ?? b ?? 9` 是“先试 `a`，再试 `b`，最后给默认值”，不论怎样加括号结果都一样。`!!` 同理（`b` 是 `T!` 时结果是 `T!`）。图上每个 `??` 仍是一个分支。右侧是 `[]` 时取左侧的元素类型。
- `??`、`!!` 的右侧一直延伸到表达式结束：`x ?? 0 + 1` 是 `x ?? (0 + 1)`。要先取值再运算，加括号：`(x ?? 0) + 1`。
- 左侧不是所要求的形式（对普通的 `int` 用 `??`，对 `T?` 用 `!!` 或 `?->`），或右侧类型不对，是 `SemanticDiag::TaggedOperandMismatch`。

## 6.7 其他运算符

### 6.7.1 算术

| 运算符 | 类型 | 说明 |
|---|---|---|
| `+` | 四种数值类型、`string` | 字符串是连接 |
| `-` `*` `/` | 四种数值类型 | 整数除法向零截断；除数为零是故障 |
| `%` | `int`、`i32` | 余数与被除数同号；除数为零是故障 |
| `^` | 四种数值类型 | 乘方 |
| `@` | 由模块定义 | `tensor` 模块：矩阵乘 |
| 一元 `-` | 四种数值类型 | |
| `~` | `int`、`i32` | 按位取反：`~5` 是 `-6` |

两个操作数的类型必须相同。没有按位与、或、异或、移位运算符；用 `math` 模块的 `bit_and`、`bit_or`、`bit_xor`、`bit_not`、`shl`、`shr`。

**两个容易出错的地方：**

- `^` 与 `*`、`/` 同一优先级，左结合：`2 * 3 ^ 2` 是 `36`，不是 `18`。乘方请加括号：`2 * (3 ^ 2)`。
- 一元 `-` 比 `^` 结合得紧：`-n ^ 2` 是 `(-n) ^ 2`。

### 6.7.2 逻辑

`a && b`、`a || b`、`!a`，操作数是 `bool`。`&&`、`||` 短路：右侧只在需要时求值。

### 6.7.3 比较

- `==`、`!=`：数值、`bool`、`string`、`Map`、`Set`，以及数组、元组、结构体，两边类型必须相同（否则是编译期错误 `SemanticDiag::NoMatchingFunction`，不做转换）。
  - **数组、元组、结构体按内容逐个比较**，递归进行：`[1, 2] == [1, 2]`、`(1, 'a') == (1, 'a')`、两个字段都相等的结构体，结果都是 `true`；长度不同的数组不相等。
  - 元素类型本身要能比较：数值、`bool`、`string`、`Map`、`Set`、`T?`、`T!`，以及由它们组成的数组、元组、结构体。含函数值的（`[f] == [f]`）不能比较，是带位置的编译期错误；张量不在此列。
  - 浮点元素按浮点的规则：含 `nan` 的数组不等于它自己，`[0.0] == [-0.0]`。
  - `T?`、`T!` 本身（不在数组、元组、结构体里）仍然没有 `==`，先用 `??`、`!!` 或 `match` 拆开。
- `<`、`>`、`<=`、`>=`：数值与 `string`（按字节）。比较不能连写：`1 < 2 < 3` 是类型错误。
- `===`、`!==` 暂不支持：词法上认得这两个记号，写了是带位置的编译期错误 `SemanticDiag::ConstructNotSupported`。比较值用 `==`、`!=`。

### 6.7.4 类型转换 `as`

`e as T` 用于：

- 进入可空或可能出错的形式：`5 as int?`、`null as int?`、`v as int!`（目标类型已知的位置不必写，见 [3.4](03_data_model.md)）；
- 构造映射与集合：`[...] as Map<K, V>`、`[...] as Set<T>`；
- 模块类型的检查式转换：`t as Tensor<f32, const [3]>`，在转换处检查形状；
- 给 `json::decode` 等返回未定类型的结果指定类型：`decode(s) as int[]`。

数值之间的转换用 `float(x)` 等函数（[3.2](03_data_model.md#32-数值)）。`e is T` 保留，尚未实现（运行时没有类型信息；`SemanticDiag::ConstructNotSupported`）。后缀的非空断言 `x!` 同样保留未实现；用 `x ?? 默认值`。

## 6.8 `if` 与 `match`

### 6.8.1 `if`

```
if 条件 then 臂 else 臂
```

`if` 是表达式。臂可以是一个表达式，也可以是一个块（`{ ... }` 或 `sync { ... }`）；块的值由块里的 `return` 给出。两臂的类型必须相同。

```cml
func sign(x: int): string {
    return if x > 0 then 'positive' else if x < 0 then 'negative' else 'zero'
}
```

- **没有 `else` 的 `if` 本身没有值。** 它只能作语句；它的臂可以是任何类型，臂给出的值被丢弃：`if debug then println('checking')` 合法，不必再为了凑类型在臂里写 `return null`。把它用作值（`let x = if debug then 1`、作实参、作 `return` 的操作数）是编译期错误 `SemanticDiag::IfWithoutElseAsValue`。图上不变：一个分支加两条臂，没写的 `else` 是一条空臂，汇合处不取值；臂照常求值，所以臂里做带副作用的事时通常要写在同步块里。写了而留空的 `else {}` 与不写相同。
- 条件的类型是 `bool`；没有“真值”转换。

> **块里的 `return` 给出的是这个块的值，不是离开函数。**
>
> 分支的每个臂是它自己的一张子图。臂里的 `return v` 结束的是这条臂，`v` 成为这个 `if`（或 `match`）的值；`throw e` 同理。如果这个 `if` 只是一条语句、它的值没有被使用，那么臂里的 `return` 什么都没做，函数继续执行后面的语句。

```cml
// 错误的写法：if 是一条值没人用的语句，两个 return 都没有效果
func wrong(x: int): string {
    if x < 0 then { return 'negative' } else { return 'not negative' }
    return 'reached the end'
}

// 正确的写法：if 的值就是函数的返回值
func right(x: int): string {
    return if x < 0 then { return 'negative' } else { return 'not negative' }
}

func main(): int sync {
    println(wrong(-1))
    println(right(-1))
    return 0
}
```

```text
reached the end
negative
```

编译器对 `wrong` 里的每个 `return` 给出警告 `SemanticDiag::ExitInUnusedBranch`：

```text
warning[SemanticDiag::ExitInUnusedBranch]: This 'return' gives the value of the 'if' it is in, and nothing uses that value: it does not leave the function.
  --> ret.cml:3:21
   |
 3 |     if x < 0 then { return 'negative' } else { return 'not negative' }
   |                     ^^^^^^^^^^^^^^^^^
note: the value of this 'if' is not used
```

所以 Camel 里没有“提前返回”：把分支写成函数体的最后一个表达式，或在它前面写 `return`。多个条件依次判断时，把 `if ... else if ... else ...` 连成一个表达式。警告可用 `-Wno-exit-in-unused-branch` 关闭。`return null` 与不带值的 `return` 同样会报：臂不需要靠它来凑类型，写了多半是以为它能提前离开函数。

### 6.8.2 `match`

```
match 名字 {
    case 模式 | 模式 ... => 臂
    ...
}
```

- 被匹配的必须是一个**名字**（变量或形参），不能是任意表达式；先用 `let` 绑定。
- 模式是字面量（整数、浮点数、字符串、`true`/`false`）或通配 `_`。一个 `case` 可以用 `|` 列出多个模式，任一匹配即进入。
- 按书写顺序取第一个匹配的分支。各臂类型必须相同。臂里的 `return` 与 `if` 的臂一样，给出的是 `match` 的值。

```cml
func kind(c: string): string {
    return match c {
        case 'a' | 'e' | 'i' | 'o' | 'u' => 'vowel'
        case ' ' => 'space'
        case _ => 'other'
    }
}

func main(): int sync {
    println(kind('e'), kind(' '), kind('x'))
    return 0
}
```

```text
vowel space other
```

**对 `T?` 与 `T!` 的固定模式。** 被匹配的值是 `T?` 或 `T!` 时，`match` 恰好有两个分支：

- `T?`：`case null`，加上 `case v`（把取出的值绑定为 `v`，类型 `T`）或 `case _`。
- `T!`：`case error e`（把错误绑定为 `e`，有 `e.kind`、`e.message`、`e.location`），加上 `case v` 或 `case _`。

```cml
func show(x: int?): string {
    return match x {
        case null => 'nothing'
        case v => 'value ' + string(v)
    }
}

func report(r: int!): string {
    return match r {
        case error e => e.kind + ': ' + e.message
        case v => 'ok ' + string(v)
    }
}

func main(): int sync {
    println(show(5), show(null))
    println(report(parse_int('7')), report(parse_int('x')))
    return 0
}
```

缺分支、多分支或混入字面量分支是 `SemanticDiag::TaggedMatchCases`。

**`match` 必须有默认分支。** 没有 `case _` 是编译期错误 `SemanticDiag::MatchWithoutDefault`，定位在 `match` 上。两种情形不必写：对 `bool` 写全了 `case true` 与 `case false`；对 `T?`、`T!` 用上面的固定模式。编译器不会追加程序里没有写的分支臂，所以图上的臂与源码里的 `case` 一一对应。

元组模式、结构体模式，以及对普通类型的值的标识符模式（`case n => ...`）保留，尚未实现（`SemanticDiag::ConstructNotSupported`）；用 `if` 链或先取成员再匹配。

`try`/`catch`/`finally` 不属于 Camel（它在图上没有稳定的映射）：写了得到 `SemanticDiag::TryCatchNotInLanguage`。可能失败的操作返回 `T!`，用 `!!`、`?->` 与 `case error e` 处理。

## 6.9 lambda 与函数类型

lambda 的写法是 `(形参): 返回类型 => 体`，返回类型可以省略；体是一个表达式或一个块：

```cml
(x: int) => x + 1
(x: int): int => { let y = x * 2; return y + 1 }
(s: string) => sync { println(s) }              // 同步块作函数体
sync (x: int) => sync { total = total + x }     // 写入被捕获的变量：加函数修饰符 sync
```

函数类型写作 `(x: T) => R`，形参要写名字：`(x: int) => int`、`(acc: int, x: int) => int`。

**函数是值。** 函数的名字可以用在任何能写值的位置：作实参、`with` 实参、`let g = f`、`return f`、数组或元组的元素、分支臂的值。

```cml
func inc(x: int): int { return x + 1 }
func dbl(x: int): int { return x * 2 }

func apply_twice(f: (x: int) => int, x: int): int {
    return f(f(x))
}

func adder(n: int): (x: int) => int {
    return (x: int): int => x + n          // 捕获 n
}

func main(): int sync {
    let g = inc
    let add5 = adder(5)
    println(g(1), apply_twice(dbl, 3), add5(10))
    println([inc, dbl]->map<(f: (x: int) => int): int => f(10)>)
    return 0
}
```

```text
2 12 15
[11, 20]
```

- lambda 捕获外层的变量，捕获是引用：外层的 `var` 被修改后 lambda 看到新值，lambda 对它的写入外层也看到。
- 只有一个签名、且签名不随实参变化的库算子与用户函数一样是函数值：`xs->map<trim>`、`let t = trim`。
- 代表一组重载（几个同名的用户函数，或有几个签名的算子，如 `abs`）的名字，在被调用者只有一个签名、且那个形参是函数类型的位置上，取签名与之完全相同的那一个：`func on_int(f: (x: int) => int, v: int)` 的调用 `on_int(abs, -3)`。其他位置上它没有单一的函数值，把它当值用是 `SemanticDiag::FunctionNameAsValue`，诊断列出各个签名；写一个写明形参类型的 lambda。签名随实参变化的算子（`println`、`len`、`map`）以及原地修改实参的算子（`append`）同样没有函数值。泛型函数的名字同理（`SemanticDiag::GenericFunctionAsValue`）。
- 把这样的名字交给内建的高阶算子时（`xs->map<abs>`、`xs->reduce<max, 0>`、`xs->filter<is_even>`），算子说不出它期望哪个函数，编译器就逐个去问：恰好有一个被算子接受时取它；一个都不接受或有几个都接受时，名字没有值（`SemanticDiag::FunctionNameAsValue`，诊断列出候选）。
- 带函数类型的声明 `let f: (x: int) => int = inc` 目前不可用（`SemanticDiag::FeatureNotSupported`）；省略类型即可。

## 6.10 高阶算子

| 写法 | 结果 | 顺序 |
|---|---|---|
| `xs->map<f>` | 每个元素经 `f` 得到的新数组 | 元素之间无序，可并行；结果按位置排列 |
| `xs->filter<p>` | `p` 为真的元素 | 同上 |
| `xs->reduce<f, init>` | 从 `init` 起依次 `f(acc, x)` 的最终值 | 按元素顺序 |
| `xs->foreach<f>` | 无；对每个元素调用 `f` | 按元素顺序 |

```cml
func main(): int sync {
    let xs = range(1, 6)
    println(xs->map<(x: int): int => x * x>)
    println(xs->filter<(x: int): bool => x % 2 == 0>)
    println(xs->reduce<(acc: int, x: int): int => acc + x, 0>)
    xs->filter<(x: int): bool => x > 3>->foreach<(x: int) => sync { println('item', x) }>
    return 0
}
```

```text
[1, 4, 9, 16, 25]
[2, 4]
15
item 4
item 5
```

`foreach` 的元素函数若要产生副作用，函数体必须是同步块（`=> sync { ... }`），理由见 [4.3.2](04_execution_model.md#432-函数的行为由它自己决定)。带 `var` 形参的函数不能交给这些算子。

### 6.10.1 故障隔离 `attempt`

错误与故障是两回事：只有声明为返回 `T!` 的操作才产生错误值；除零、下标越界、递归过深等是故障，带位置终止程序，`!!` 接不住它。`attempt(f, a, b)` 调用函数值 `f`，实参是 `a, b`；这次调用里（包括它直接或间接调用的一切）发生的故障，在 `attempt` 处变成一个错误值，再用 `!!`、`?->` 或 `match ... case error e` 处理。任意表达式用闭包包起来：`attempt(() => expr)`。

```cml
func risky(d: int): int { return 100 / d }

func main(): int sync {
    println(attempt(risky, 4) !! -1)
    println(attempt(risky, 0) !! -1)
    println(attempt(() => [1, 0, 4]->map<risky>) !! [-1])
    let r = attempt(risky, 0)
    println(match r {
        case error e => e.kind + ' at ' + e.location
        case v => 'no fault'
    })
    return 0
}
```

```text
25
-1
[-1]
DivisionByZero at main.cml:1:38
```

- 类型：`f: (A...) => T` 得到 `T!`；`f` 本身返回 `U!` 时得到 `U!`（它自己返回的错误值原样传出）。带 `with` 参数或 `var` 参数的函数不能直接传入，用闭包。
- 错误值的 `kind` 是故障的名字（如 `DivisionByZero`、`IndexOutOfRange`、`MaxRecursionDepthExceeded`），`message` 是故障的消息，`location` 是故障发生处的 `文件:行:列`。
- 边界就是那一次调用：实参的求值在边界之外；边界之内在故障之前已经发生的效应不收回（打印的行、写过的 `var`、写过的文件都保留）。
- `exit(n)` 不是故障，不被隔离；内存耗尽与编译器自身的错误也不被隔离。
- 并行调度器下，`map` 的一个元素故障时，已经开始的元素运行到结束，尚未开始的不再开始；`attempt` 在已开始的全部结束之后才给出错误值。
- 在图上它是一个 `OPER` 节点（`:mark/isolate`），第一个输入是函数值，其余是实参。实现与尚待裁定的问题见 [技术说明](../technical/33_fault_isolation.md)。

## 6.11 赋值与 `wait`

- `x = e`、`x += e` 等是表达式，值是所赋的值。目标必须是 `var` 变量或 `var` 形参本身（[3.6](03_data_model.md#36-letvar-与引用)）。
- 复合赋值可用的类型：

  | 运算符 | 类型 |
  |---|---|
  | `+=` | 四种数值类型、`string`（连接） |
  | `-=`、`*=`、`/=`、`^=` | 四种数值类型 |
  | `%=` | `int`、`i32` |
  | `&=`、`\|=` | `bool`（`a = a && b`、`a = a \|\| b`） |
  | `@=` | 内建类型都没有 |

- **复合赋值不原地修改。** `x op= e` 是算出新值再让 `x` 重新绑定到它。模块定义的类型（如张量）没有自己的复合赋值算子，对它们 `t += u` 就是 `t = t + u`（`-=`、`*=`、`/=`、`%=`、`@=`、`^=` 同理，只要对应的二元运算符对这些类型有定义）：`let a = t` 之后再写 `t += u`，`a` 不变。要原地修改用显式的算子。

- `wait e` 见 [4.4](04_execution_model.md#44-var引用与-wait)。

## 6.12 运算符优先级

从低到高：

| 级 | 运算符 | 说明 |
|---|---|---|
| 1 | `wait` | 前缀，作用于整个赋值表达式 |
| 2 | `=` `+=` `-=` `*=` `/=` `%=` `^=` `@=` `&=` `\|=` | 不能连写 |
| 3 | `\|\|` | 左结合 |
| 4 | `&&` | 左结合 |
| 5 | `==` `!=`（`===` `!==` 暂不支持） | |
| 6 | `<` `>` `<=` `>=` | |
| 7 | `+` `-` | 左结合 |
| 8 | `*` `/` `%` `^` `@` | 左结合；`^` 与 `*` 同级 |
| 9 | `??` `!!` | 右侧是一个完整的表达式 |
| 10 | 一元 `!` `-` `~`；`as` | `-a->f` 是 `-(a->f)`；带符号的数字字面量是一个操作数，`-2->f` 是 `(-2)->f` |
| 11 | `->` `?->` | 左结合 |
| 12 | `..` `?..` | |
| 13 | 后缀：`.字段`、`[下标]`、`(实参)`、`<with 实参>` | 必须紧贴 |

`if`、`match` 可以出现在任何需要表达式的位置；它们的臂一直延伸到表达式结束，嵌在更大的表达式里时加括号。

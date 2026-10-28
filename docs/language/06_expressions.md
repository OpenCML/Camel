# 6. 表达式

[English](en/06_expressions.md) | 中文

Camel 里几乎一切都是表达式：`if`、`match`、块、赋值都有值。本章逐类说明。

---

## 6.1 求值

表达式是图上的节点，操作数是它的输入边。一个表达式是否求值、何时求值，由依赖决定（[4.2](04_execution_model.md#42-按依赖求值)）；运算符的优先级与结合性只决定图的形状（[6.12](#612-运算符优先级)）。`&&`、`||`、`??`、`!!`、`->` 与 `if`、`match` 是分支：没有被选中的一侧不求值。

## 6.2 字面量与构造

<!-- camel-snippet: fragment: a list of literal and lambda forms, not a program -->

```cml
42          3.14        'text'      true        null
[1, 2, 3]                   // 数组
(1, 'one')                  // 元组；单元素写 (x,)
{ x: 1, y: 2 }              // 结构体
(x: int): int => x + 1      // lambda，见 6.9
```

- 数组字面量的元素类型必须相同。元素是**有期望类型的位置**（[3.3.1](03_data_model.md#331-数组)）：有上下文时每个元素取那个类型，`let xs: float[] = [1, 2.0]` 的元素都是 `float`；没有上下文时，同一个数组里的数字字面量取最宽的一种（整数 < 小数），所以 `[1, 2.0]` 是 `float[]`（与 Go 的无类型常量相同）。空数组 `[]` 的元素类型来自上下文：`let xs: int[] = []`、`[] as Map<string, int>`。
- 结构体字面量的键是标识符。字段同样是有期望类型的位置：`let p: P = { a: 1, b: 2.0 }`（`P = { a: float, b: float }`）里 `1` 是 `float`。字段的值写成名字时是变量，不转换，类型不符就是 `SemanticDiag::DeclaredTypeMismatch`。
- **列表推导** `[e for x in xs if c]` 就是 `xs->filter<(x) => c>->map<(x) => e>`：`x` 的类型是 `xs` 的元素类型，`if c` 可以省略，结果是 `e` 的数组。图上与手写这两个调用完全相同（两个算子节点，各带一张元素函数的子图），所以它的性质也相同：元素之间没有顺序约定（并行调度器可以并发地算），`e`、`c` 里的副作用与 `map`、`filter` 的元素函数里的一样处理（[6.10](#610-高阶算子)）。可以嵌套，也可以出现在任何需要表达式的位置。`xs` 不是数组是 `SemanticDiag::ComprehensionSourceRefused`。
- **展开不在语言里**：写在字面量里的 `...xs` 是普通语法错误（`...` 只出现在相对模块名 `import ...模块` 里）。要合并数组写 `concat(xs, ys)`。

### 6.2.1 格式化字符串

反引号字符串 `` `...` `` 里的 `${表达式}` 是插值，可以带格式说明 `${表达式:格式说明}`（词法见 [2.5.4.1](02_lexical.md#2541-格式化字符串)）。它与 `'...'->format<...>` 是**同一棵树**：把 `${...}` 换成 `format` 的占位符（`{}` 或 `{:格式说明}`），把文字里的 `{`、`}` 写成 `{{`、`}}`，再以各插值表达式为 with 实参、以这段文字为普通实参调用 `format`。

```cml
func main(): int sync {
    let name = 'Camel'
    let n = 42
    println(`hello, ${name}!`)          // 'hello, {}!'->format<name>
    println(`n = ${n}, twice = ${n * 2}`)
    println(`pi = ${3.14159:.2f}`)      // 'pi = {:.2f}'->format<3.14159>
    return 0
}
```

- 插值表达式在它写的位置、按它所在的词法作用域求值；名字查找、类型检查、副作用与写在字符串外完全一样。它们是 `format` 的 with 实参，何时求值由依赖与副作用规则决定（[4.2](04_execution_model.md#42-按依赖求值)），与同一批表达式写在字符串外没有区别。
- 格式说明与值的类型不符是**编译期**错误 `SemanticDiag::ArgumentsRejected`，位置指向那一处插值（与 `format` 对常量格式串的检查相同）。
- 插值表达式的语法错误按它在文件里的位置报告，不会因为在一个字符串里而被吞掉。
- 由于是同一棵树，所有执行方式（`std::nvm`、`std::fvm`、`std::jit`、`std::prl`）与 `std::cpp` 翻译看到的就是 `format` 调用，输出逐字节相同。
- 格式化器不改变反引号字符串的内容（插值表达式也按原样保留）；要格式化插值里的代码，先把它写成 `format` 调用或单独一行。

## 6.3 访问与下标

| 写法 | 含义 |
|---|---|
| `p.x` | 结构体字段。不存在的字段是编译期错误 `SemanticDiag::InvalidAccessIndex` |
| `t.0` | 元组成员，下标是字面量，越界是编译期错误 |
| `xs[i]` | 数组元素。越界（含负数）是故障 `RuntimeDiag::IndexOutOfRange` |
| `xs[idx]` | `idx` 是整数数组：按 `idx` 的顺序取这些位置上的元素（`take`），见下 |
| `xs[a:b]`、`xs[a:b:s]`、`xs[:b]`、`xs[a:]`、`xs[:]` | 切片，见下 |
| `m[k]` | 映射查找，类型是 `V?`（[3.6](03_data_model.md#36-映射与集合)） |
| `mod::name` | 限定名 |

- 对数组不能用 `.`（`xs.1` 是 `SemanticDiag::TypeNotIndexable`）。
- 下标紧贴被访问的表达式，不能有空白。
- **下标从 0 开始，负数不是从尾部数**：`xs[-1]` 是故障 `RuntimeDiag::IndexOutOfRange`。最后一个元素写 `last(xs)`。
- **`xs[idx]` 是 `take`**：`idx` 的类型必须是 `int[]`，结果是 `idx` 各位置上的元素，按 `idx` 的顺序；同一个位置出现两次就取两次。越界与负数都是故障 `RuntimeDiag::IndexOutOfRange`。结果是一份新数组，不是视图（[5.4](../design-principles.cn.md)）。
- **切片沿第一维，左闭右开**：`xs[a:b]` 是位置 `a` 到 `b-1`。`a` 省略表示从头，`b` 省略表示到末尾，`xs[:]` 是整个数组；`s` 是步长。切片是**共用数据块的视图**（[5.4](../design-principles.cn.md)），不复制元素。
  - 两端都是 0 到 `len(xs)` 之间的位置（`b` 可以等于长度，表示取到最后一个）；负数或越界是故障 `RuntimeDiag::SliceOutOfRange`，与下标越界一致（[3.9](../design-principles.cn.md)）。`b < a` 是空数组，不是故障。长度在编译期已知而边界是常量时，越界是编译期错误（[3.9](../design-principles.cn.md)）。
  - 步长写在第二个冒号之后：`xs[a:b:s]`，得到的仍是共用数据块的视图；要一份拷贝写 `slice(xs, 0, a, b, s)`。
  - 元素是对象的数组（`[[1, 2], [3, 4]]` 这类）按行复制，槽数组没有可共用的数据块。
- **多维下标**：`xs[:, 0]`、`xs[a:b, c]` 沿各维取坐标，得到视图；某一维整个取用 `:`（[5.4](../design-principles.cn.md)）。
- **字符串下标按码点**：`s[i]` 是第 `i` 个码点，结果是只含这一个字符的字符串（`'中文'[0]` 是 `'中'`）；越界同样是故障。要数值用 `str` 模块的 `codepoints`（见 [标准库参考](../stdlib-reference.md)）。

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
- 实参按位置对应形参，类型必须一致：数值变量之间不做隐式转换；**字面量取形参的类型**（`f(2)` 在 `f(x: float)` 里是 `2.0`，见 [3.2](03_data_model.md#32-数值)），进入 `T?`、`T!` 形参的值会自动装入（[3.4](03_data_model.md)）。不匹配是 `SemanticDiag::NoMatchingFunction` 或 `SemanticDiag::ArgumentsMismatch`，诊断里列出可用的签名。
- 同名函数有多个时按重载规则选择（[7.4.3](07_statements.md#743-重载)）。
- **`f<w>` 是部分应用。** 不带圆括号的 `f<w>` 是一个值：一个已经配好上下文参数的函数。`let g = inc<1>` 之后 `g(2)` 等同 `inc<1>(2)`；`w` 在写下 `f<w>` 的地方求值一次。它可以绑定到名字、作实参、作返回值，也可以直接交给高阶算子（`xs->map<inc<10>>`）。
  - 只配了前几个上下文参数时，剩下的仍是结果的上下文参数：`let above = clamp<0>` 之后写 `above<5>(9)`。已经配齐的函数不能再配（`g<2>` 是错误）。
  - `f` 必须是能由这些实参确定的一个函数。同名函数有多个时，取唯一一个接受这些上下文实参的；仍有多个时由所在位置的函数类型（形参）决定，都定不下来是 `SemanticDiag::PartialApplicationRefused`。
  - 图上它是一个携带了捕获值的函数值，与闭包同构：自己的一张子图，配好的实参通过端口进入，没有新增节点种类。
  - 把 `with` 形参声明成 `var` 的函数照样能部分应用：`f<w>` 捕获的是那个变量本身，不是它的值，所以调用它时函数对这个变量做的写落在原来的变量上（[3.5](03_data_model.md)）；这样的调用是一次有副作用的调用，写在 `sync` 块里才运行。
  - 名字下不止一个函数时没有函数值：重载的内建算子（如 `len` 有三个签名）就是这样，报 `SemanticDiag::FunctionNameAsValue`。把调用写在 lambda 里。
- 实参可以按名字给（`f(x: 1)`）：位置实参写在前面，按名字给的写在后面；名字不是被调函数声明的形参时是 `SemanticDiag::ParamArgumentUnknown`。
- 同一个实参表里写两个带尖括号的调用（`f<a>(x), g<b>(y)`）会被读成比较；带尖括号的调用后面再接带尖括号的管道（`f<a>(x)->g<b>`）也可能如此。把它们分成两条语句。
- 写在括号里的 lambda 可以直接调用：`((x: int): int => x + 1)(3)`；也可以直接写在 `->` 的右边：`3->(x: int): int => x + 1`、`parse_int(s) -> (x: int): int => x * 2`。

## 6.5 管道

`a->f` 把 `a` 作为 `f` 的实参：`a->f` 就是 `f(a)`，`a->f<w>` 就是 `f<w>(a)`。左结合，适合把数据流写成一条链：

```cml
func main(): int sync {
    let total = 1..6
        ->map<(x) => x * x>
        ->filter<(x) => x % 2 == 1>
        ->reduce<(acc: int, x: int): int => acc + x, 0>
    println(`total = ${total}`)
    return 0
}
```

```text
total = 35
```

- 管道 `x -> f` 作用在 `T?`、`T!` 上时把空值或错误传下去，见 [6.6](#66-空值与错误的运算符)。
- `f..g` 是函数组合：`(f..g)(x)` 即 `g(f(x))`，与管道 `x->f->g` 同向；两边是整数时 `a..b` 是左闭右开的区间，得到 `int[]`，带步长写 `range(a, b, step)`。`for i in 0..n` 由编译器融合，不物化数组。`..` 比 `+`、`-` 松（6.12）：`1..n+1` 是 `1..(n+1)`，与人在别的语言里的读法一致；它也比 `->` 松，`x->f..g` 读作 `(x->f)..g`，管道里要组合直接写 `x->f->g`，二者等价。

## 6.6 空值与错误的运算符

`T?` 与 `T!` 见 [3.4](03_data_model.md#34-可能为空-t-与可能出错-t)。下面三个运算符在图上都是一个分支，条件是值上的一位标记。

| 写法 | 左侧 | 右侧 | 结果 |
|---|---|---|---|
| `a ?? b` | `T?` | `T` | `a` 为空取 `b`，否则取 `a` 里的值 |
| `a !! b` | `T!` | `T` | `a` 出错取 `b`，否则取 `a` 里的值 |
| `x -> f` | `T?` 或 `T!` | 不接受 `T?` / `T!` 本身的函数 | `x` 为空就得到空、出错就把错误原样传下去，否则用里面的值调用 `f`；结果是 `U?` 或 `U!`（`f` 返回 `U` 或已经是该形式的值都可以） |

```cml
func half(x: int): int! {
    return if x % 2 == 0 then x / 2 else { return error('Odd', 'not even') }
}

func main(): int sync {
    println(parse_int('12') !! 0, parse_int('twelve') !! 0)
    println(parse_int('12') -> half -> half !! -1)
    println(parse_int('10') -> half -> half !! -1)
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
- **`->` 先按值本身的类型找 `f` 的重载。** 在同名函数里按 `x` 自己的类型（`T?` 或 `T!`）找，连同 `when` 条件：有函数接受它（参数类型是 `T?`，或者是条件允许它为 `T?` 的泛型 `T`），就把整个 `x` 传进去。都不接受时才拆开：`x` 为空得到空，`x` 出错得到原样的错误，否则用里面的值调用 `f`。调用里写的其他实参属于被选中的那次调用，只在那时求值。同时带两层标记的值（`T?!`）逐层同样处理。
- `??`、`!!` 的右侧一直延伸到表达式结束：`x ?? 0 + 1` 是 `x ?? (0 + 1)`。要先取值再运算，加括号：`(x ?? 0) + 1`。
- 左侧不是所要求的形式（对 `T?` 用 `!!`，对 `T!` 用 `??`），或右侧类型不对，是 `SemanticDiag::TaggedOperandMismatch`。`??` 用在不能为空的值上（普通的 `int`，或一个已被收窄成 `T` 的名字）是允许的：空值那条臂不会走到，结果就是那个值；编译器给出警告 `SemanticDiag::KnownNotNull`（设计原则 3.8）。

## 6.7 其他运算符

下面各节的运算符（`&&`、`||`、`as` 除外）都是函数的调用：`a + b` 是 `__add__(a, b)`，表里列的是内建的重载。程序可以为自己的类型添加重载（`func operator +`，[7.4.5](07_statements.md#745-运算符重载)）；它与内建的、C++ 模块的重载合成一组，按操作数的类型选择。

### 6.7.1 算术

| 运算符 | 类型 | 说明 |
|---|---|---|
| `+` | 四种实数值类型、`c64`、`c128`、`string` | 字符串是连接 |
| `-` `*` `/` | 四种实数值类型、`c64`、`c128` | 整数除法向零截断；除数为零是故障（复数是复数零） |
| `%` | `int`、`i32` | 余数与被除数同号；除数为零是故障。复数没有 `%` |
| `^` | 四种实数值类型、`c64`、`c128` | 乘方：复数的整数指数是反复平方（精确），复数的复数指数用平台的 `pow`（[3.2](03_data_model.md#32-数值)） |
| `@` | 由模块定义 | `tensor` 模块：矩阵乘 |
| 一元 `-` | 四种实数值类型、`c64`、`c128` | |
| `~` | `int`、`i32` | 按位取反：`~5` 是 `-6` |

两个操作数的类型必须相同。没有按位与、或、异或、移位运算符；用 `math` 模块的 `bit_and`、`bit_or`、`bit_xor` 和 `x->shl<n>`、`x->shr<n>`（移位量写在尖括号里）；按位取反是 `~x`。

**两个容易出错的地方：**

- `^` 比 `*`、`/` 高一级，从右到左结合：`2 * 3 ^ 2` 是 `18`，`2 ^ 3 ^ 2` 是 `2 ^ (3 ^ 2)`，即 `512`。
- 一元 `-` 比 `^` 结合得松，比 `->` 和其余二元运算符结合得紧，对字面量与对名字一样：`-n ^ 2` 是 `-(n ^ 2)`，`-2 ^ 2` 是 `-4`（要 `4` 写 `(-2) ^ 2`）；`-n->f` 是 `(-n)->f`；`-a * b` 是 `(-a) * b`。`^` 的右边可以带符号：`2.0 ^ -1.0`。`!`、`~` 与一元 `-` 同级（设计原则 4.2）：`!x->f` 是 `(!x)->f`，`!a ^ b` 是 `!(a ^ b)`；要对一条链的结果取反写 `!(xs->empty)`。

### 6.7.2 逻辑

`a && b`、`a || b`、`!a`，操作数是 `bool`。

**`&&`、`||` 是分支，不是运算符。** `a && b` 就是 `if a then b else false`，`a || b` 就是 `if a then true else b`：右侧是一条臂，只在左侧不能决定结果时求值，否则它的效应不发生、它的故障也碰不到（`n != 0 && 10 / n > 1` 是安全的）。在图上它们就是那个 `if` 的 `BRCH`/`JOIN`，右侧是一张自己的子图。两者都是保留的，不能重载；操作数不是 `bool` 是 `SemanticDiag::LogicalOperandNotBool`。因为 `a && b` 就是那个 `if`，左侧对空值的判断会收窄右侧（[6.8.1](#681-if)）：`x != null && x > 0`。

```cml
func noisy(tag: string, v: bool): bool sync {
    println(tag)
    return v
}

func main(): int sync {
    println(noisy('l1', false) && noisy('r1', true))
    println(noisy('l2', false) & noisy('r2', true))
    return 0
}
```

```text
l1
false
l2
r2
false
```

**`a & b`、`a | b` 两边都求值**：普通的二元运算符（与其他运算符一样对应一个函数），可以重载。只做一点不需要分支的逻辑运算时用它们；张量的逐元素逻辑运算、自己的类型重载的也是这一组。优先级：`&` 高于 `|`，两者都高于 `&&`、低于比较，所以 `a == b & c == d` 是 `(a == b) & (c == d)`。

### 6.7.3 比较

- `==`、`!=`：数值、`bool`、`string`、`Map`、`Set`，以及数组、元组、结构体、张量，两边类型必须相同（否则是编译期错误 `SemanticDiag::NoMatchingFunction`，不做转换），结果是 `bool`。
  - **数组、元组、结构体按内容逐个比较**，递归进行：`[1, 2] == [1, 2]`、`(1, 'a') == (1, 'a')`、两个字段都相等的结构体，结果都是 `true`；长度不同的数组不相等。
  - **整体比较就是逐元素比较再归约成一个 `bool`**：`a == b` 是形状相同、并且逐元素相等处处成立（见 [4.2](../design-principles.cn.md)）。数值数组（`f32[]`、`f32[..]` 这类）也一样，结果同样是 `bool`。
  - 元素类型本身要能比较：数值、`bool`、`string`、`Map`、`Set`、`T?`、`T!`，以及由它们组成的数组、元组、结构体。含函数值的（`[f] == [f]`）不能比较，是带位置的编译期错误。
  - 浮点元素按浮点的规则：含 `nan` 的数组不等于它自己，`[0.0] == [-0.0]`。
  - `T?`、`T!` 本身（不在数组、元组、结构体里）仍然没有 `==`，先用 `??`、`!!` 或 `match` 拆开。
- **`===`、`!==` 是逐元素的相等与不等**：`a === b` 对两个数组逐个元素比较，得到同形状的 `bool` 数组：`[1, 2, 3] === [1, 2, 4]` 是 `[true, true, false]`；`a !== b` 是它的逐元素否定。形状规则与别的逐元素运算相同（形状不能广播是带位置的运行期错误）。标量上 `===` 就是 `==`（`1 === 1`、`'a' === 'a'`）。
  - 与 `<`、`>`、`<=`、`>=` 一样，它在数值数组上定义：`f32[]`、`f32[..]`、`int[]`，元素类型是数值的数组。整体比较（上一条）在任何元素类型能比较的数组上都有定义。
- **`x == null`、`x != null`** 对 `T?` 类型的 `x` 有定义，两边顺序不限，得到 `bool`：它判断 `x` 的标记，不是比较两个值（与 `??` 分支所依据的是同一个节点）。它是普通的表达式：`let has = x != null`。可空的函数值也可以这样与 `null` 比较。一个永远不为空的值（`int`、`T!`、已经收窄成 `T` 的名字）与 `null` 比较也可以写，结果是固定的（`==` 恒为 `false`，`!=` 恒为 `true`），编译器给出警告 `SemanticDiag::NullTestNeverNull`，这个值本身不求值。两个 `T?` 互相比较仍是 `SemanticDiag::NoMatchingFunction`；两个函数值不能比较。
- `<`、`>`、`<=`、`>=`：数值与 `string`（按码点顺序）；**数值数组上逐元素比较**，得到同形状的 `bool` 数组。比较不能连写：`1 < 2 < 3` 是类型错误。

### 6.7.4 类型转换 `as`

`e as T` 用于：

- 进入可空或可能出错的形式：`5 as int?`、`null as int?`、`v as int!`（目标类型已知的位置不必写，见 [3.4](03_data_model.md)）；
- 构造映射与集合：`[...] as Map<K, V>`、`[...] as Set<T>`；
- 模块类型的检查式转换：在转换处检查该类型自己的条件。
- 数组的检查式转换：`a as f32[4]`，形状在转换处检查；`f32[3]` 是数值数组的另一种拼写（过渡期保留），`t as f32[3]` 是同一种转换。
- 把一个值转成它自己的类型，什么也不做（`n as int`）。

`as` 不在数值之间转换，字面量与变量一样：`5 as float` 与 `n as float` 都是 `SemanticDiag::DynamicCastForbidden`，`true as int`、`n as bool` 也是。数值之间的转换用目标类型的转换函数（`float(n)`、`int(x)`、`i32(n)`、`f32(x)`），它们的损失规则（越界是故障）写在设计原则 5.1。

`as` 不会不经检查地改变一个值的类型。`json::decode` 的结果是 `Json`，不能 `as` 成别的类型；已知形状的 JSON 用 `json::decode_as::<int[]>(s)`，它在解码时检查形状，结果是 `T!`。

数值之间的转换用 `float(x)` 等函数（[3.2](03_data_model.md#32-数值)）。`is` 不是关键字：`e is T` 里的 `is` 是一个普通的名字，写出来是语法错误（运行时没有类型信息）。后缀的 `!` 不是语言里的写法：`T!` 的那个 `!` 属于类型，一个 `T?` 的值用 `x ?? 默认值` 取出来，所以 `x!` 是普通语法错误（在 `!` 处）。

## 6.8 `if` 与 `match`

### 6.8.1 `if`

```
if 条件 then 臂 else 臂
```

`if` 是表达式。臂可以是一个表达式，也可以是一个块（`{ ... }` 或 `sync { ... }`）；块的值由块里的 `return` 给出。两臂的类型必须相同，或者一条臂能进入另一条的形式（[3.4](03_data_model.md#34-可能为空-t-与可能出错-t)）：一条臂是 `T`、另一条是 `null` 或没有值时，分支是 `T?`。

臂的值本身是**有期望类型的位置**（设计原则 5.1）：整个 `if` 有期望类型时（带类型的声明、函数的返回位置等），臂里的字面量取那个类型，所以 `let x: float = if c then 1 else 2` 的两条臂都是 `float`。写成名字的臂值是变量，不转换；它与另一条臂的类型不符时仍按臂的类型不一致报告。

```cml
func sign(x: int): string {
    return if x > 0 then 'positive' else if x < 0 then 'negative' else 'zero'
}
```

- **没有 `else` 的 `if` 本身没有值。** 它只能作语句；它的臂可以是任何类型，臂给出的值被丢弃：`if debug then println('checking')` 合法，不必再为了凑类型在臂里写 `return null`。把它用作值（`let x = if debug then 1`、作实参、作 `return` 的操作数）是编译期错误 `SemanticDiag::IfWithoutElseAsValue`。图上不变：一个分支加两条臂，没写的 `else` 是一条空臂，汇合处不取值；臂照常求值，所以臂里做带副作用的事时通常要写在同步块里。写了而留空的 `else {}` 与不写相同。
- 条件的类型是 `bool`；没有“真值”转换。
- **收窄。** 在 `if x != null then A else B` 里，`x` 在 `A` 中是 `T`；`if x == null then B else A` 同理。它与 `match x { case null => B  case v => A }` 是同一个分支：排除了空值的那条臂一开始就把值从 `x` 里取出来，臂内 `x` 这个名字指的就是这个值。

  ```cml
  func len_or_zero(s: string?): int {
      return if s != null then len(s) else 0
  }

  func sum_or_minus(a: int?, b: int?): int {
      return if a != null && b != null then a + b else -1
  }
  ```

  规则有意做得很小：
  - 只收窄 `let` 绑定与参数，不收窄 `var`（它可能在判断与使用之间被别处改写；先用 `let` 绑定它的值）；
  - 只认直接的写法，两边顺序不限（`x != null`、`null != x`）：判断是整个条件，或是 `&&` 连起来的一项时，收窄 then 臂（`if x != null && ready then ...`）。else 臂只在整个条件是 `x == null` 时收窄，`&&` 连起来的条件不收窄 else 臂。`||` 或 `!` 之下的判断（`x != null || ready`、`!(x == null)`）、存在 `let` 里的判断都不收窄；需要时嵌套 `if`，或用 `match`、`??`；
  - 只在臂内：`if` 之后 `x` 又是 `T?`。在臂内再写 `let x = ...` 是重复声明。
  - 收窄之后，臂内对 `x` 仍可写针对 `T?` 的 `??` 与带 `case null` 的 `match`：它们作用于重新装回 `T?` 的值，检查与收窄之前相同，只是空值那条臂不会走到；编译器给出警告 `SemanticDiag::KnownNotNull`（对不是被收窄、本来就非可空的值用 `??`，同样允许并给这条警告）。在臂内再判 `x == null`、`x != null`，按上一条（非可空的值与 `null` 比较）给出警告，结果恒定。

  因为 `a && b` 就是 `if a then b else false`，`x != null && ...` 的右侧就是这样一条臂，`x == null || ...` 的右侧也是。判断了一个名字、又在不收窄的地方把它当 `T` 用时，报出的错误带一条说明，讲明为什么没有收窄。

> **块里的 `return` 给出的是这个块的值，不是离开函数。**
>
> 分支的每个臂是它自己的一张子图。臂里的 `return v` 结束的是这条臂，`v` 成为这个 `if`（或 `match`）的值；交出一个错误值的 `return e` 同理。如果这个 `if` 只是一条语句、它的值没有被使用，那么臂里的 `return` 什么都没做，函数继续执行后面的语句。

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

臂里的 `return` 和臂里其他表达式一样，按**臂所在的位置**取类型（[3.3.1](03_data_model.md#331-数组)、设计原则 5.1）：`let v: float = if c then { return 1 } else { 2.0 }` 里 `1` 是 `float`（位置是 `v` 的声明），而 `let v = if c then { return 1 } else { 2 }` 里它是 `int`（位置没有类型，由各臂自己决定）。函数的声明返回类型**不是**臂里 `return` 的期望类型：`func f(c: bool): float { let v = if c then { return 2 } else { 3 } … }` 里 `v` 是 `int`，函数在需要时再显式转换。函数体最后一个表达式（包括整个 `if`/`match`）是函数返回值的位置，它的类型是函数的声明返回类型，臂里的 `return` 也就从那里取。

### 6.8.2 `match`

```
match 名字 {
    case 模式 | 模式 ... => 臂
    ...
}
```

- 被匹配的可以是任意表达式（`match f(x) { ... }`），不只是一个名字。
- 模式是字面量（整数、浮点数、字符串、`true`/`false`）或通配 `_`。一个 `case` 可以用 `|` 列出多个模式，任一匹配即进入。**模式里的字面量取被匹配值的类型**（它有期望类型，设计原则 5.1）：`x: f32` 时 `case 3` 的 `3` 是 `f32`，`x: int` 时 `case 5000000000` 是 `SemanticDiag::LiteralNotRepresentable`。**每个模式的值必须能被被匹配值的类型接受**（设计原则 3.7）：它用该类型的比较与被匹配的值比较，所以 `string` 上的 `case 1` 是 `SemanticDiag::MatchCaseTypeMismatch`。
- 按书写顺序取第一个匹配的分支。各臂类型必须相同。臂里的 `return` 与 `if` 的臂一样，给出的是 `match` 的值，并按臂所在的位置取类型（见 6.8.1）；臂的值本身也一样，整个 `match` 有期望类型时取那个类型。

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

**对 `enum` 的模式。** 被匹配的值是枚举时，模式写它的变体（[3.5](03_data_model.md#35-枚举-enum)）：`case Shape::Circle(r)`、`case Shape::Rect { w, h }`、`case Shape::Empty`、`case _`。元组变体要给全位置，结构体变体只写要绑定的字段（可改名、可简写），无数据的变体写它的限定名（`case Shape::Empty`；裸名字是绑定名，写成 `case Empty` 是 `EnumPatternMismatch`）。列全所有变体就不必写 `case _`；漏了变体又没有 `case _` 是 `SemanticDiag::EnumMatchNotExhaustive`，诊断列出漏掉的变体名；同一个变体写两次给 `EnumVariantCaseRepeated` 警告，`case _` 之后的臂给 `MatchCaseAfterWildcard` 警告，列全所有变体后还写 `case _` 给 `MatchWildcardRedundant` 警告。字段位置只写名字或 `_`：嵌套模式（`case Shape::Circle(Shape::Square(x))`）是普通语法错误。

**`match` 必须有默认分支。** 没有 `case _` 是编译期错误 `SemanticDiag::MatchWithoutDefault`，定位在 `match` 上。三种情形不必写：对 `bool` 写全了 `case true` 与 `case false`；对 `T?`、`T!` 用上面的固定模式；对枚举列全了所有变体。编译器不会追加程序里没有写的分支臂，所以图上的臂与源码里的 `case` 一一对应。

元组模式与结构体模式不在语言里：`case (...)`、`case {...}` 是普通语法错误。对普通类型的值的标识符模式（`case n => ...`），以及配普通类型的 `error e`，报 `SemanticDiag::IdentifierPatternNeedsNullableOrErrorType`：能拆开的只有 `T?` 与 `T!` 这两个标记（3.8），要读值或接住错误就写 `??`、`!!`，或 `match` 的字面量模式与 `_`。枚举变体的元组、结构体模式已经实现，但字段位置只能写名字或 `_`。

`try`/`catch`/`finally` 不属于 Camel（它在图上没有稳定的映射），它们也不是关键字：这三个词是普通的名字，用它们写出的 `try { ... }` 是普通的语法错误。可能失败的操作返回 `T!`，用 `!!`、`->` 与 `case error e` 处理。

### 6.8.3 `match type`

`match type` 按**类型的形状或身份**选一条分支，用于泛型函数（[5.5](../design-principles.cn.md)）：`T` 在实例化时已经是具体类型，编译器只把选中的分支建进图里，其余分支不建图、不做类型检查（名字仍然解析）。

```
match type 类型 {
    case struct => 分支
    case array  => 分支
    case nullable => 分支
    case fallible => 分支
    case map    => 分支
    case set    => 分支
    case tuple  => 分支
    case func   => 分支
    case #leaf  => 分支
    case int    => 分支
    case MyStruct => 分支
    case T      => 分支
    case _      => 分支
}
```

- `struct` 匹配结构体；`tuple` 匹配**普通**元组；`array` 匹配所有数组类型（不论元素是什么）；`func` 匹配函数类型；`nullable` 匹配所有 `T?`，`fallible` 匹配所有 `T!`，`map` 匹配所有映射，`set` 匹配所有集合；`#leaf` 匹配其余所有类型（标量、`string`、数组、函数、`T?`、`T!`、映射、集合、单元格等）。`struct` 和 `tuple` 是 [R3 内部节点](../design/generic-structure/README.cn.md)的两种形状：`T?`、`T!` 是带标记的元组，和可变单元格一样落在 `#leaf`，不算 `tuple`。
- 类型位置写一个类型名就是**身份模式**：`case int`、`case Json`、`case MyStruct`、`case T`。身份是**精确**的：`case float` 不选 `int`（`float` 与 `f64` 是同一个类型，`int` 与 `i64` 也是；`int` 与 `i32` 不是）；`case f32` 不选 `f64`；`case MyStruct` 按声明的名字选，不选另一个字段形状相同的结构体；`case T` 在实例化时先代入 `T` 的绑定，所以它选的正是那个类型。形状与身份可以写在同一个 case 里（`case tuple | MyStruct`），按书写顺序判断：前面的 case 已经选中的类型，后面的同一身份是 `SemanticDiag::StaticMatchPatternCannotMatch`。
- `_` 与 `#leaf` 一样匹配一切，只作为“兜底”写法；两者都必须是最后一个 case。`case struct | tuple` 用 `|` 合并几种形状。`case int` 写在 `case #leaf` 之前是正常的；写在 `#leaf`/`_` 之后是 `SemanticDiag::StaticMatchUnreachableCase`。
- `match type` 后面是类型位置：紧跟的 `(` 一律读作类型的开头，所以 `match type () { ... }` 匹配空元组类型；函数类型要再包一层括号，写成 `match type ((x: int) => int) { ... }`。`case` 后面也是类型位置，同样的规则：`case (int, int)`、`case ()` 是元组，函数类型写成 `case ((x: int) => int)`。
- 身份模式里的类型名在这里解析：未知的名字是 `SemanticDiag::UnresolvedTypeReference`；`void` 和 `typeof` 表达式的值不是一个类型身份，是 `SemanticDiag::StaticMatchPatternCannotMatch`。同一个 case 里写了两次同一个类型，也是 `StaticMatchPatternCannotMatch`。
- 没有 case 命中时是 `SemanticDiag::StaticMatchNotExhaustive`；`#leaf`/`_` 后面还有 case 是 `SemanticDiag::StaticMatchUnreachableCase`。`match type` 比较具体类型名：`case int`、`case MyStruct` 是这个构造的一部分，标量类型直接写类型名。
- 选中的分支直接成为当前位置的直线代码，运行时没有分支节点。分支的值就是整个 `match type` 的值；空分支的值为 `void`。
- 泛型函数的返回类型可以省略，由每个实例选中的分支推出（返回类型推断）。如果递归调用回到**同一个类型实例**、而该实例的返回类型还没有推出，报 `SemanticDiag::GenericInstantiationCycle`；递归到严格更小的类型参数则照常先完成子实例。

**类型函数。** 类型也可以由类型算出（设计原则 5.6）：类型函数用 `type` 声明，`type F<T> = 体`，在类型位置写成 `F<...>` 调用；函数体是类型表达式，各 case 给出类型的 `match type` 也是：`type ElemOrSelf<T> = match type T { case array => ElementOf<T>  case _ => T }`。`types` 声明了五个 `match type` 取不出或造不出的部分：`ElementOf<T>` 是数组或集合的元素类型，`PayloadOf<T>` 是 `U?` 或 `U!` 里的 `U`，`KeyOf<T>` 与 `ValueOf<T>` 是映射的键类型与值类型，`WithElement<A, E>` 是把数组类型 `A` 的元素类型换成 `E`、形状不变的数组类型（`f32(xs)` 的结果就是 `WithElement<A, f32>`）。调用本身就是一个类型，和别的类型一样用：`is_integer::<ElementOf<T>>()` 里它是类型实参，`func first::<T>(xs: T): ElementOf<T>` 里它是结果类型。`T` 还是类型变量时调用等着；实例绑定 `T` 之后，调用被它算出的类型取代。函数不接受的类型（`ElementOf<int>`，或没有 case 选中的 `match type`）是 `SemanticDiag::TypeFunctionNotApplicable`：参数是具体类型时在类型写出处报，否则在造出实例的调用处报。

```cml
func duplicate::<T>(t: T) {
    return match type T {
        case struct => (t, t)
        case tuple  => (t, t)
        case #leaf  => (t, t)
    }
}
```

### 6.8.4 `static for` 与 `t.[n]`

`static for n, F in fields T => 块` 把 `T` 的**字段表**在实例化时展开（[5.5](../design-principles.cn.md)）：每个字段执行一次块，展开出的代码直接成为当前位置的直线代码，运行时没有循环节点。

- `T` 必须是具体类型，而且是 [R3 内部节点](../design/generic-structure/README.cn.md)：结构体或**普通**元组。`T?`、`T!`、可变单元格、标量、数组和函数都是叶子，报 `SemanticDiag::StaticForNotComposite`；先用 `match type` 处理它们。
- `n` 是字段名；元组的字段名是十进制下标（`"0"`、`"1"`，……），按声明顺序。`F` 是字段类型。两者只在块内可见；`F` 可以写在类型位置（`let x: F`），不能当值用。其中任何一个写成 `_` 表示这一轮不绑定它，与 `match` 的 `_` 同义：`static for n, _ in fields T => e` 只取字段名，`static for _, F in fields T => e` 只取字段类型（`_` 不是名字，块里写 `_` 仍然未定义）。
- `fields` 后面也是类型位置：紧跟的 `(` 一律读作类型的开头，所以空元组直接写 `fields ()`，不需要先 `type EmptyTuple = ()`；函数类型要再包一层括号，写成 `fields ((x: int) => int)`。
- 字段按声明顺序展开，每个字段的块有独立的块作用域。空结构体或空元组展开零次，块一次也不建。
- `static for` 作为语句时块的值总是被丢弃，整个构造的值是 `void`；需要值的位置写 comprehension 形式（见下）。
- `t.[n]` 取结构体的字段或元组的元素，`n` 必须是编译期常量：字符串或整数字面量、外层 `static for` 的 `n`/`j`、绑定了常量的 `let` 名字（`let first = …; t.[first]`；`var` 不算，它可以在访问前改掉）、一个编译期能算出常量的表达式（如 `( static for n, F in fields S => n ).[0]`），或者一个纯函数在所有实参都是编译期常量时的调用（`t.[first_field::<S>()]`），编译期把它折叠成那个值。函数体在编译期读不出来时按调用自己的名字拒绝（`SemanticDiag::FoldCallNotEvaluable`，其余 `FoldCall*` 诊断说明停在哪一步）。只有选中的那个字段建进图里（一个 `ACCS`），运行时不选择字段，所以运行期的值不能当键（`SemanticDiag::StaticAccessNotConstant`）。
- 结构体没有这个字段报 `SemanticDiag::StaticAccessUnknownField`；元组的下标越界（或键不是下标）报 `SemanticDiag::StaticAccessIndexOutOfRange`；目标不是结构体或普通元组报 `SemanticDiag::StaticAccessNotInternal`。`t.["0"]` 与 `t.[0]` 对元组是同一个元素。
- 字段的值被丢弃时（`t.[n];` 单独成句），只检查字段，不建 `ACCS`；目标表达式仍然求值，以保留它的副作用。

```cml
func show::<T>(t: T) {
    static for n, F in fields T => {
        print(n, t.[n])
    }
}

func first(t: (int, float)) {
    return t.[0]
}
```

**`indices`：按元组的位置展开。** `static for j in indices ts => 块` 把元组值 `ts` 的**位置**在实例化时展开：每个位置执行一次块，`j` 是这一轮的位置（`int`），轮数是元组的元数。

- `ts` 必须是元组类型（普通元组；`T?`、`T!`、可变单元格是叶子）。不是元组时报 `SemanticDiag::StaticIndicesNotTuple`；类型还没具体化时报 `SemanticDiag::StaticTypeNotConcrete`。轮数在实例化时由 `ts` 的类型确定。
- **操作数只用来取类型。** 操作数是编译期元组（剩余参数的 `ts`，或元组推导式）时不建容器结点：`indices ts` 不建 `FILL`，块里的 `ts.[j]` 直接取元素结点、不建 `ACCS`。运行期元组变量或参数本来就有自己的结点，`indices` 不会为它再建一个。
- `j` 可以当值用（例如 `print(j)` 或参与计算），也可以当 `t.[j]` 的键：每轮它都是编译期常量。块里再声明同名变量仍会遮蔽它，此时当键报 `SemanticDiag::StaticAccessNotConstant`（与 `fields` 的 `n` 相同）。
- `indices` 只绑定一个名字（没有 `F`）；元素的类型由 `ts` 的类型给出，目前不另给名字。写 `static for j, F in indices ts` 是语法错误。
- `indices` 只遍历元组：结构体有字段名，用 `fields T`。`{ static for j in indices ts => e }` 没有字段名可给，报 `SemanticDiag::StructComprehensionWithoutFields`；需要值的位置写 `( static for j in indices ts => e )`。
- 图与手写的展开相同：生成器本身没有节点。编译期元组的 `ts.[j]` 直接是元素结点，与手写的 `ts.[0]`、`ts.[1]` 的选择相同；运行期元组值的 `ts.[j]` 仍建一个 `ACCS`。

```cml
func sum(t: (int, int, int)): int {
    return static loop acc = 0 for j in indices t do acc + t.[j]
}

func positions(t: (int, string)): (int, int) {
    return ( static for j in indices t => j )
}
```

**Comprehension：把各轮的值装回同一形状。** `static for` 也可以写在需要值的位置：`{ static for n, F in fields T => e }` 得到一个结构体，`( static for n, F in fields T => e )` 得到一个元组，字段或元素与 `T` 同名、同序，每个来自一轮的值。

- 块的**值就是那一轮的值**：与语句形式不同，这里不丢弃。块里可以用 `let`，最后一句话的值就是字段的值；没有值（空块，或以一条没有值的语句结尾）报 `SemanticDiag::StaticComprehensionVoidElement`。
- 每个字段/元素的**类型取自那一轮的值**，不强制等于 `F`：`F` 是 `int` 而值是 `float` 时结果字段就是 `float`。字段名与顺序仍来自 `T` 的声明顺序，所以 `tree_map` 这类改变叶子类型、保持形状的展开可以直接写。
- **同一形状写成 `T { ... }`**：`T { static for n, F in fields T => e }` 用一个类型 `T` 同时给出字段名和种类（结构体或元组），所以 `case struct | tuple` 一个分支就够。`T` 必须与生成器遍历的类型是同一个类型，否则报 `SemanticDiag::SameShapeComprehensionMismatch`；`T` 是叶子（没有字段）时报 `SemanticDiag::SameShapeComprehensionNotComposite`。`{ ... }` 与 `( ... )` 仍分别只装结构体和元组。
- 空结构体、空元组没有轮次，结果是 `{}` / `()`（一个零槽的 `FILL`）。
- 图上是直线代码：每轮按声明顺序建出它读的 `ACCS`，comprehension 本身没有节点。元组推导式的结果先保持为编译期元组：只被 `indices`、`..` 或 `.[j]` 使用时元素保持独立、不建 `FILL`；作为值用（打印、返回、赋给运行期位置）时，在使用处一个 `FILL` 把各轮的值装回同形状。结构体推导式总是装成一个 `FILL`。`t.[n].[m]` 里对父的访问只建一次，内层各字段共用同一个 `ACCS`，因此与 C++ `tree_map` 对同一棵树建出的图相同。
- `{ static for ... }` 在需要块的位置（`case struct => { ... }`、`if c then { ... }`）也读作 comprehension，所以语句形式不能再套一层花括号：直接写 `static for n, F in fields T => 块`。
- 要把各轮的值折成一个结果（而不是装回同一形状），用 [6.8.5](#685-static-loop) 的 `static loop`。

```cml
func doubled(t: {a: int, b: int}): {a: int, b: int} {
    return { static for n, F in fields {a: int, b: int} => t.[n] * 2 }
}

func rebuild::<T>(t: T) {
    return match type T {
        case struct | tuple => T { static for n, F in fields T => rebuild(t.[n]) }
        case _ => t
    }
}
```

### 6.8.5 `static loop`

`static loop 状态 = 初值, ... for n, F in fields T do 表达式` 是 `static for` 的折叠形式：字段表在实例化时展开，每个字段把状态推进一轮，展开后没有循环节点。

```
static loop 名字 = 初值 (, 名字 = 初值)* for 名字 (, 名字)* in (fields 类型 | indices 值 | 序列) do 表达式

头 : n, F in fields T       // 按字段走：n 是字段名，F 是该字段的类型
   | n, F, tags in fields T // 三个名字：再加上该字段的标签
   | j in indices ts        // 按元组的位置走：j 是位置（int）
   | x in 序列               // 按序列的元素走：见下
```

头与 `static for` 相同（同一件事一种写法）：两者都是普通 `loop` 的 `for 名字 in 序列` 那一段，不写括号；`fields` 与 `indices` 各绑定它自己的那些名字，见下。

- 状态头与 [6.8.6](#686-loop) 的 `loop` 相同：一个名字时循环的值就是这个值；几个名字时循环体给出按书写顺序的元组，循环的值也是这个元组。状态的名字只在循环体内可见；初值在循环所在处求值一次，其中看不到状态的名字。名字不能重复（`SemanticDiag::LoopNameRepeated`）。
- 循环体每轮的值就是下一轮的状态，其类型必须是状态的类型（能自动装入的除外，如 `int` 进入 `int?`），否则是 `SemanticDiag::LoopBodyTypeMismatch`。空结构体或空元组一轮也不展开，循环的值是初值。
- 遍历的写法与 [6.8.6](#686-loop) 的 `loop` 头相同（不写括号）：`for n, F in fields T`（按字段）、`for j in indices ts`（按元组的位置，规则与 6.8.4 的 `indices` 相同），或 `for x in 序列`（按序列的元素走）。没有 `while`，也没有 `break`/`continue`。遍历外层泛型的类型参数时，轮数由实例化时的类型决定——这就是“实例化时已知的轮数”。
- 序列必须在编译期求出，由**一条规则**决定，而不是一张写法表：`in` 后的表达式只由编译期常量（字面量、绑在常量上的名字、外层 `static` 展开的迭代变量）经过**可自由排布的运算**（纯、不会出错——设计原则 3.4 的那一个谓词）组成，编译器就把它求成值再展开。所以这些都能走：常量区间 `a..b`（半开，与 `..` 到处一样，`0..5` 是 0…4；两端都是 `int`，`..` 不另造浮点或字符区间）、`range(a, b, step)`（三个实参都是常量，步长必须写出来，迭代规则与设计原则 4.2 的区间相同，`step` 为 0 是 `SemanticDiag::RangeStepZero`）、数组字面量与常量数组（按元素走）、编译期元组 `ts`（按元素走：每轮绑定的是那个位置的**元素** `ts.[i]`，它的类型就是第 i 个位置的类型——这正是 `static loop` 区别于普通 `loop` 的地方：普通 `loop` 每轮的元素类型是同一个，而这里随位置变；要在类型位置用它就写 `typeof x`，要名字与类型一起拿就写 `fields T`；`indices ts` 走位置、给的是 `int`）。`fields T` / `indices ts` 的规则与 6.8.4 相同（`T` / `ts` 必须是具体的结构体或普通元组）；求不出来时报 `SemanticDiag::StaticLoopSequenceNotKnown`，消息里带上写下的那个表达式——没有固定轮数的序列（比如由参数给出）就是这一种。没有运行期序列的 `for`。
- 头上的名字个数由序列的元素决定：元素有几部分，头就能点名到第几个——`fields T` 的元素是三部分（字段名、字段类型、字段标签），所以 `n, F` 与 `n, F, tags` 都合法；`indices ts` 与普通序列的元素只有一部分，所以写第二个名字是 `SemanticDiag::StaticHeadNameCount`。少写几部分合法（`for n in fields T` 只要字段名），多写就是那条诊断；名字按位置读，`_` 和其他名字一样占一个位置。
- 展开成直线代码：每轮按字段顺序建出它读的 `ACCS`，上一轮的状态直接是下一轮读到的值，`static loop` 本身没有节点。块里的 `return` 与普通函数一样离开函数，不是“跳出循环”。
- 效应就是直线代码的效应：循环体是不是同步块由它自己决定（3.2），每轮的效应按字段顺序执行。

```cml
func count::<T>(t: T): int {
    return static loop n = 0 for name, F in fields T do n + 1
}

func sum3(t: (int, int, int)): int {
    return static loop acc = 0 for i in 0..3 do acc + t.[i]
}

func total_and_max(t: {a: int, b: int}): (int, int) {
    return static loop total = 0, best = 0
        for n, F in fields {a: int, b: int} do {
            (total + t.[n], if best < t.[n] then t.[n] else best)
        }
}

func sum_to(): int {
    let n = 4
    return static loop acc = 0 for i in 0..n do acc + i
}

func stepped(): int {
    return static loop acc = 0 for i in range(0, 10, 3) do acc + i
}

func from_array(): int {
    let xs = [1, 2, 3]
    return static loop acc = 0 for x in xs do acc + x
}

func sum_args(..ts): int {
    return static loop acc = 0 for x in ts do acc + x
}
```

### 6.8.6 `loop`

循环是一个带状态的表达式（设计原则 3.10）。状态写在循环头上，每个名字只声明一次并给出初值；`do` 后面的表达式是循环体，它的值是下一轮的状态；整个循环的值是最后的状态。

```
loop 名字 = 初值 (, 名字 = 初值)* (for 名字 in 序列)? (while 条件)? do 表达式
```

<!-- camel-snippet: fragment: the loop forms; xs, n and ds are the surrounding example's values -->

```cml
let total    = loop acc = 0 for x in xs do acc + x * x
let (i, acc) = loop i = 1, acc = 0 while i <= n do (i + 1, acc + i)
let p        = loop p = { x: 0 } for d in ds do { x: p.x + d }
let found    = loop hit = -1 for x in xs while hit < 0 do if x > 9 then x else hit
```

- **状态**：一个名字时，循环体的值与循环的值就是这个值；几个名字（逗号分开）时，循环体给出它们按书写顺序组成的元组，循环的值也是这个元组，用 `let (a, b) = loop ...` 或 `.0`、`.1` 取出。状态的类型是初值的类型；循环体的值必须是这个类型（能自动装入的除外，如 `int` 进入 `int?`），否则是 `SemanticDiag::LoopBodyTypeMismatch`。`null` 与空数组 `[]` 作初值时没有自己的类型，写 `null as T?`，或先把空数组带类型绑定到一个名字（`let none: int[] = []`）再用这个名字（`SemanticDiag::LoopStateWithoutType`）。
- **`for x in xs`** 每轮按顺序取序列 `xs` 的一个元素；`xs` 是数组（张量就是数值数组）、字符串或映射，别的类型是 `SemanticDiag::LoopSequenceNotIterable`。
  - **数组**：元素本身，`a..b` 就是一个数组。
  - **字符串**：按码点，每个元素是只含这个字符的字符串，与 `s[i]` 相同（11.1）：`for c in '中文'` 的两轮是 `'中'`、`'文'`；多字节的字符按一个码点算一次。
  - **映射**：`(键, 值)` 元组，按键的顺序（11.3 的遍历顺序，与 `items(m)` 相同），不是插入顺序：`for kv in m` 取到的键严格递增，元组用 `kv.0`、`kv.1` 取出。
  - **张量**：张量是数值数组（`f32[2, 3]` 就是一种数值数组类型），遍历与数组同一条规则：沿第一维，每个元素是去掉第一维后的子数组（与这一维上的一段共享同一份数据，不复制）：rank 2 的 `f32[2, 3]` 给出两个 `f32[3]`，rank 1 给出元素本身（`sum(x)` 是它的数）。元素类型没写在类型里时，给出的元素仍是那个类型；维数不定的 `f32[..]` 给出的元素还是 `f32[..]`。rank 0 的数组没有第一维，不是序列。
  空数组、空字符串、空映射、第一维为 0 的数组一轮都不执行，循环的值是初值。**`while c`** 在条件成立时继续；条件是 `bool`（`SemanticDiag::LoopConditionNotBool`）。两者都写时，每一轮先看条件，再取下一个元素：条件不成立或元素取完即结束。至少写其中一个。
- **没有 `break` 与 `continue`**：提前结束写成 `while` 条件，跳过一轮的其余部分写成 `if`。
- **作用域**：初值与序列在循环所在处求值一次，其中看不到循环头的名字；条件看得到状态的名字，循环体看得到状态的名字与 `for` 的名字。循环头的名字不能重复（`SemanticDiag::LoopNameRepeated`）。条件与循环体看得到外面的名字，读到的是循环开始时的值（`var` 每轮读其当前值）。
- **`do` 之后**是一个表达式，读法与 `then` 之后相同：`{ x: 1 }` 是结构体，`{ 5 }` 与 `sync { ... }` 是块。块里 `return` 结束这个块并给出下一轮的状态，不离开函数。
- **效应**：循环体是不是同步块由它自己决定（3.2）。写成 `do sync { println(x); acc + x }` 时每轮按顺序执行其中的效应。循环所在的块不是同步块、而循环的值又没有被用到时，整个循环不执行，并给出 `EffectNotExecuted`（与没有被用到的调用相同）。
- **顺序与深度**：各轮严格按顺序。循环不是递归：任意多轮都不占调用深度，`--max-recursion-depth` 不计循环的轮数；不终止的循环一直运行。
- **数值许可**：写在函数或块上的许可标签（`#reassociate`、`#contract` 等）对词法上嵌在其中的循环体与条件同样有效（7.2、7.5）；`#strict` 同样覆盖它们。
- `loop`、`while`、`do`、`for`、`in` 是关键字。

在图上，一个循环是外层图里的一个 `LOOP` 节点，条件与循环体各是一张子图，两者共用一份捕获表（设计原则 3.10；`docs/design/graph-model` 3.5）。字符串与映射在循环所在处按其类型一次转换成元素数组（字符串的码点、映射的键值对）；张量就是数值数组，不用转换，`LOOP` 节点直接遍历它。`LOOP` 节点本身只遍历数组：各后端与下降都只有这一种情形。

## 6.9 lambda 与函数类型

lambda 的写法是 `(形参): 返回类型 => 体`，返回类型可以省略；体是一个表达式或一个块：

<!-- camel-snippet: fragment: a list of lambda forms; total comes from the surrounding example -->

```cml
(x: int) => x + 1
(x: int): int => { let y = x * 2; return y + 1 }
(x) => x + 1                                    // 形参类型从所在位置推出（见下）
(s: string) => sync { println(s) }              // 同步块作函数体
(x: int) => sync { total = total + x }          // 写入被捕获的变量：函数体写成同步块
```

函数类型写作 `(x: T) => R`，形参要写名字：`(x: int) => int`、`(acc: int, x: int) => int`。

**形参类型从哪里来。** lambda 不写类型的形参，其类型从 lambda 所在位置的函数类型推出，省略的返回类型同样如此。这样的位置有三种：

- 实参：调用一个形参是函数类型的函数，`with` 实参同理。内建函数也一样，它们的签名写在标准库的 `stdlib/prelude.cml` 里：`xs->filter<(x) => x > 0>`、`xs->foreach<(x) => println(x)>` 的 `x` 是 `xs` 的元素类型。`->` 之后直接写的 lambda 取左边那个值的类型（`x->(v) => v + 1`）。

  <!-- camel-snippet: fragment: a call shown inside the block it would be written in -->

  ```cml
  func apply_twice(f: (x: int) => int, x: int): int { return f(f(x)) }
  apply_twice((x) => x + 1, 3)         // x: int
  ```

- 带标注的绑定：`let` 或 `var` 的类型是函数类型；往这样的变量再赋一个新值同理。

  <!-- camel-snippet: fragment: bindings shown inside the block they would be written in -->

  ```cml
  let g: (x: int) => int = (x) => x + 1    // x: int
  var h: (x: int) => int = (x) => x - 1
  h = (x) => x + 100                       // x: int
  ```

- 返回值：函数的结果类型是函数类型。

  ```cml
  func adder(n: int): (x: int) => int {
      return (x) => x + n              // x: int，返回类型也是 int
  }
  ```

- `->` 后面的 lambda：形参就是接收者，所以它的类型是接收者的类型，或者从 `T?`、`T!` 里取出的值的类型（3.9 的流水线）：

  ```cml
  func main(): int sync {
      let a = 5 -> (y) => y + 1                  // y: int
      println(a)
      return 0
  }
  ```

期望的类型确定不了的形参是一个错误（`SemanticDiag::LambdaParameterWithoutType`，指到那个形参）：lambda 不在这样的位置上（`let g = (x) => x + 1`、`((x) => x)(1)`），期望的类型不是函数类型，形参个数对不上，或者期望的类型还带着这个位置绑定不了的类型变量。没有期望类型时它不会变成一个 `any`（5.10）：写出类型即可。

- 推断不改变重载决议。lambda 在实参位置上推出类型这件事，与调用选中哪个重载无关：带类型的 lambda 仍按它自己的类型选重载（`pick((x: int) => x + 1, 1)` 与只有这一个重载时选中的相同）；不带类型的 lambda 面对一组重载时没有唯一的期望类型，于是报上面那个错误，而不是替调用挑一个重载。
- 泛型高阶函数（`tree_reduce`、`map` 这类形参类型里带类型变量的）目前推不出来：实参位置的期望类型要等泛型实例化，而 lambda 总在实例化之前降低，所以那里的形参类型要写出来（`tree_reduce<(acc: int, x: int) => acc + x>(...)`）。这一项目前仍是待决问题。

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
- 代表一组重载（几个同名的用户函数，或有几个签名的算子，如 `abs`）的名字，在被调用者只有一个签名、且那个形参是函数类型的位置上，取签名与之完全相同的那一个：`func on_int(f: (x: int) => int, v: int)` 的调用 `on_int(abs, -3)`。其他位置上它没有单一的函数值，把它当值用是 `SemanticDiag::FunctionNameAsValue`，诊断列出各个签名；写一个写明形参类型的 lambda。签名随实参变化的算子（`println`、`len`、`map`、`append`）同样没有函数值。泛型函数的名字同理（`SemanticDiag::GenericFunctionAsValue`）。
- 把这样的名字交给内建的高阶算子时（`xs->map<abs>`、`xs->reduce<max, 0>`、`xs->filter<is_even>`），算子说不出它期望哪个函数，编译器就逐个去问：恰好有一个被算子接受时取它；一个都不接受或有几个都接受时，名字没有值（`SemanticDiag::FunctionNameAsValue`，诊断列出候选）。
- 带函数类型的绑定 `let f: (x: int) => int = inc` 中，值按标注的函数类型检查；lambda 的形参类型也可以从它推出（见上）。

## 6.10 高阶算子

| 写法 | 结果 | 顺序 |
|---|---|---|
| `xs->map<f>` | 每个元素经 `f` 得到的新数组 | 元素之间无序，可并行；结果按位置排列 |
| `xs->filter<p>` | `p` 为真的元素 | 同上 |
| `xs->reduce<f, init>` | 从 `init` 起依次 `f(acc, x)` 的最终值 | 按元素顺序 |
| `xs->foreach<f>` | 无；对每个元素调用 `f` | 按元素顺序 |

```cml
func main(): int sync {
    let xs = 1..6
    println(xs->map<(x) => x * x>)
    println(xs->filter<(x) => x % 2 == 0>)
    println(xs->reduce<(acc: int, x: int): int => acc + x, 0>)
    xs->filter<(x) => x > 3>->foreach<(x) => sync { println('item', x) }>
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

错误与故障是两回事：只有声明为返回 `T!` 的操作才产生错误值；除零、下标越界、递归过深等是故障，带位置终止程序，`!!` 接不住它。`attempt` 是**函数变换**：`attempt(f)` 得到一个函数，参数与 `f` 相同，结果类型是 `f` 的结果加上 `!`；调用它是 `attempt(f)(a, b)`。这次调用里（包括它直接或间接调用的一切）发生的故障，在边界处变成一个错误值，再用 `!!`、`->` 或 `match ... case error e` 处理。任意表达式用闭包包起来：`attempt(() => expr)()`。这个函数值可以像别的函数值一样传递、放进尖括号：`rows->map<attempt(parse_row)>`。

```cml
func risky(d: int): int { return 100 / d }

func main(): int sync {
    println(attempt(risky)(4) !! -1)
    println(attempt(risky)(0) !! -1)
    println(attempt(() => [1, 0, 4]->map<risky>)() !! [-1])
    let r = attempt(risky)(0)
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

- 类型：`f: (A...) => T` 得到 `(A...) => T!`；`f` 本身返回 `U!` 时得到 `(A...) => U!`（它自己返回的错误值原样传出）。带 `with` 参数或 `var` 参数的函数不能直接传入，用闭包。
- 错误值的 `kind` 是故障的名字（如 `DivisionByZero`、`IndexOutOfRange`、`MaxRecursionDepthExceeded`），`message` 是故障的消息，`location` 是故障发生处的 `文件:行:列`。
- 边界就是那一次调用：实参的求值在边界之外；边界之内在故障之前已经发生的效应不收回（打印的行、写过的 `var`、写过的文件都保留）。
- `exit(n)` 不是故障，不被隔离；内存耗尽与编译器自身的错误也不被隔离。
- 并行调度器下，`map` 的一个元素故障时，已经开始的元素运行到结束，尚未开始的不再开始；`attempt` 在已开始的全部结束之后才给出错误值。
- 在图上，`attempt(f)` 的值持有它自己的一个小图，图体是一个 `OPER` 节点（`:mark/isolate`），第一个输入是捕获的 `f`，其余是实参。实现与尚待裁定的问题见 [技术说明](../technical/33_fault_isolation.md)。

## 6.11 赋值

- `x = e`、`x += e` 等是表达式，值见下。目标是 `var` 变量或 `var` 形参本身，或从它出发的一条**路径**：字段、下标、任意组合（[3.7](03_data_model.md#37-letvar-与引用)）。`p.x = e`、`xs[0] = e`、`m[i].f = e` 都是对 `p`、`xs`、`m` 整体的重新绑定：`xs[i].f = e` 就是 `xs = set(xs, i, set(xs[i], f, e))`，`set` 是只改一个位置、返回新值的纯算子。路径赋值因此可以写在任何表达式位置；目标的根不是变量时（如 `f().x = e`、`{ x: 1 }.x = e`）是 `AssignmentTargetNotVariable`，下标越界是故障 `IndexOutOfRange`。
- 路径赋值的值是**重新绑定后的整体**：`xs = xs->append<…>` 的值是新数组，`xs[0] = 5` 的值是改过之后的那个 `xs`，不是 `5`。只有目标就是变量本身时，值才是所赋的值。
- 在 `sync` 里写一个 `var` 数组的一个元素**不复制**：写回用与 `xs = xs->sort` 同一套“未被共享”判据（[10.5](../design-principles.cn.md#105-库边界)），即 `:var/take` 在变量被别处读过之后才交出副本。所以 `let a = xs` 之后写 `xs[0]` 会复制，`a` 保持不变；没有读过 `xs` 时直接改它自己的存储。
- 复合赋值可用的类型：

  | 运算符 | 类型 |
  |---|---|
  | `+=` | 四种实数值类型、`c64`、`c128`、`string`（连接） |
  | `-=`、`*=`、`/=`、`^=` | 四种实数值类型、`c64`、`c128` |
  | `%=` | `int`、`i32` |
  | `&=`、`\|=` | `bool`（`a = a & b`、`a = a \| b`：右侧总是求值） |
  | `@=` | 内建类型都没有 |

- **复合赋值先算新值，再重新绑定。** `x op= e` 是算出新值再让 `x` 重新绑定到它，所以数值上的结果与 `x = x op e` 相同。路径形式的复合赋值（`xs[0] += 1`、`p.n += 1`）把新值写回那个位置，写回按上面的原地规则：`let a = xs` 之后 `xs[0] += 1` 复制，`a` 不变。模块定义的类型（如张量）以及程序重载了运算符的类型（[7.4.5](07_statements.md#745-运算符重载)）没有自己的复合赋值算子，对它们 `t += u` 就是 `t = t + u`（`-=`、`*=`、`/=`、`%=`、`@=`、`^=`、`&=`、`|=` 同理，只要对应的二元运算符对这些类型有定义）。

## 6.12 运算符优先级

从低到高：

| 级 | 运算符 | 说明 |
|---|---|---|
| 2 | `=` `+=` `-=` `*=` `/=` `%=` `^=` `@=` `&=` `\|=` | 不能连写 |
| 3 | `\|\|` | 左结合；是分支，右侧只在需要时求值 |
| 4 | `&&` | 左结合；是分支，右侧只在需要时求值 |
| 4a | `\|` | 左结合，两边都求值 |
| 4b | `&` | 左结合，两边都求值 |
| 5 | `==` `!=` `===` `!==` | 整体比较与逐元素比较（见 [6.7.3](#673-比较)） |
| 6 | `<` `>` `<=` `>=` | |
| 6a | `..` | 区间（两边是数）与函数组合（两边是函数）；比加减松，`1..n+1` 是 `1..(n+1)` |
| 7 | `+` `-` | 左结合 |
| 8 | `*` `/` `%` `@` | 左结合 |
| 8a | `^` | 右结合，比乘除高一级 |
| 9 | `??` `!!` | 右侧是一个完整的表达式 |
| 10 | `as` | |
| 10a | 一元 `-` `!` `~` | 比 `->` 与 `^` 松：`!xs->is_empty()` 是 `!(xs->is_empty())`，`-a->f` 是 `-(a->f)`，`-a ^ 2` 是 `-(a ^ 2)`；字面量与名字读法相同（`-1->abs` 是 `-(1->abs)`，先取负写 `(-1)->abs`） |
| 11 | `->` | 左结合 |
| 13 | 后缀：`.字段`、`[下标]`、`(实参)`、`<with 实参>` | 必须紧贴 |

`if`、`match` 可以出现在任何需要表达式的位置；它们的臂一直延伸到表达式结束，嵌在更大的表达式里时加括号。

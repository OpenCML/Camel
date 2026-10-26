# 3. 数据模型

[English](en/03_data_model.md) | 中文

本章描述 Camel 的类型与值：基本类型、复合类型、`let` 与 `var`、可能为空与可能出错的类型、映射与集合。

---

## 3.1 类型一览

每个表达式在编译期都有确定的类型；图上只有具体类型，运行时没有类型信息（不能在运行时询问一个值是什么类型）。

| 类型 | 写法 | 说明 |
|---|---|---|
| 整数 | `int`（即 `i64`）、`i32` | 64 位、32 位有符号 |
| 浮点数 | `float`（即 `f64`）、`f32` | 双精度、单精度 |
| 复数 | `c64`、`c128` | 单精度、双精度复数：实部、虚部各是一个浮点数 |
| 布尔 | `bool` | `true`、`false` |
| 字符串 | `string` | 合法的 UTF-8 文本，不可变；长度与下标按码点 |
| 字节串 | `Bytes` | 二进制数据，不可变；内建类型名，与 `string` 之间显式编码、解码 |
| 数组 | `T[]`、`T[4]`、`T[n]`、`T[..]` | 元素类型相同；方括号里是维度：常数、未知的 `?`，或尾部的包 `..s`；`T[][]` 是嵌套数组，rank 2 写作 `T[m, n]` |
| 结构体 | `{ x: T1, y: T2 }`，或 `type` 声明的名字 | 按名字，用 `.x` 访问；`type Name = { ... }` 声明的是命名类型，见 3.3.3 |
| 函数 | `(x: T) => R` | 见 [6.9](06_expressions.md#69-lambda-与函数类型) |
| 可能为空 | `T?` | 见 3.4 |
| 可能出错 | `T!` | 见 3.4 |
| 映射、集合 | `Map<K, V>`、`Set<T>` | 见 3.6 |
| 枚举 | `Shape`，`Option<int>` | 见 3.5 |
| 无值 | `void` | 没有返回值的函数的返回类型；它唯一的值打印为 `null` |
| 底类型 | `never` | 没有值的类型：`fail<'Kind'>(message)` 与 `exit(code)` 的类型是它，可以用在任何位置（赋给别的类型、进入分支的合并、被泛型推断），但没有表达式给出它的值 |
| 模块类型 | 由模块引入的类型名（如 `Json`）；数值数组类型不是模块类型（过渡期的 `Tensor` 名字已删除，写它是未知名字） | 由该模块引入；名字与尖括号里的实参由该类型自己解释，表示由该模块定义 |

没有 `any` 类型：编译之后每个值都有确定的类型（设计原则 5.10）。在任何类型位置写 `any`，与写别的未知类型名一样，是编译期错误 `SemanticDiag::UnresolvedTypeReference`；接受多种类型的形参用类型参数（见 [7.5](07_statements.md#75-泛型函数)），不同类型的值并排放用元组或结构体。

读取 JSON：形状事先知道时用 `json::decode_as::<T>(text)`，结果是 `T!`；形状事先不知道时用 `json::decode(text)`，它给出 `Json!`：文字不是 JSON 时是错误值 `JsonParseError`，嵌套超过深度上限时是 `JsonTooDeep`（都不是故障，设计原则 11.2；上限固定为 10000），否则是 `Json`，类型确定，内容运行时才知道；用 `get`、`as_int`、`as_array` 等取出，每个都给出类型确定的值（找不到时为空），并接受 `Json?`、`Json!`（以及 `Json!?`）以便链式取值，例如 `j->get<'server'>->get<'port'>->as_int ?? 8080`；对 `Json!` 取值时错误原样传下去，结果是 `Json!?` 这样的两层，用 `??`、`!!` 拆开；`as_float` 也接受整数，`get<下标>` 越界给 `null`（11.2 定下的访问器设计）。

标准库签名说明里的 `T`、`U...`（如 `println: (x: T, ...rest: U...) => T`）只是说明的记法：这些算子由类型规则按实参的类型检查，图上只有具体类型。

用 `type` 给类型起名字，见 [7.7](07_statements.md#77-类型声明)：

```cml
type Point = { x: int, y: int }
type Pair<T> = (T, T)
```

## 3.2 数值

- **数值之间不做隐式转换。** 两个不同类型的*变量*混算（`i32` 与 `int`、`int` 与 `float`）是编译期错误（`SemanticDiag::NoMatchingFunction`），`let y: float = n`（`n` 是 `int`）也是（`SemanticDiag::DeclaredTypeMismatch`）。需要时显式转换。**文本也不是数**：没有文本到数字的隐式转换，`let x: int = '42'` 是 `SemanticDiag::DeclaredTypeMismatch`（消息说 `string` 不能作为 `int`），`'42' as int` 是 `SemanticDiag::DynamicCastForbidden`（`as` 不做数值转换）；取数用 `int(s)`、`parse_int(s)`，浮点用 `float(s)`、`parse_float(s)`。会跟着上下文动的是**字面量**，不是变量：`1 + 2.0` 是 `1.0 + 2.0`，`let x: f32 = 1` 里的 `1` 是 `f32`（见本节末与设计原则 5.1）。
- 转换函数：`int(x)`、`i64(x)`、`i32(x)`、`float(x)`、`f64(x)`、`f32(x)`、`c64(x)`、`c128(x)`、`string(x)`。`c64`、`c128` 从实数类型与另一个复数类型取值：实数成为实部、虚部为 0，`c128` 转 `c64` 时两个部分都按 f32 舍入。复数转实数不自动，也不在转换函数里，用 `re()`、`im()`。
- 复数的运算：`+`、`-`、`*`、`/`、一元 `-`，按复数算术（`i * i` 是 `-1+0i`）；`==`、`!=` 按两个部分比较。没有大小比较、`%`：复数没有顺序。`^` 有（整数指数精确，见 3.2 末）。`abs(z)` 是模（`c64` 给 `f32`，`c128` 给 `float`），`re(z)`、`im(z)` 取实部、虚部，`conj(z)` 取共轭，`exp(z)` 是复指数；它们来自 `math` 模块。复数不能作 `Map` 的键或 `Set` 的元素（没有顺序）。打印与格式说明的规则见 [2.5.3](02_lexical.md#253-虚数字面量)。
- `i64`、`f64` 只是 `int`、`float` 的另一种写法。诊断、图（`std::rgir`、`inspect`）与编辑器里显示的类型一律用 `int`、`float`：泛型实例叫 `first<int>`，不是 `first<i64>`。

  | 写法 | 结果 |
  |---|---|
  | `float(3)` | `3.0` |
  | `int(3.99)`、`int(-3.99)` | `3`、`-3`（向零截断） |
  | `int(1.0e30)`、`int(nan)`、`int(inf)`、`i32(5000000000)` | 故障 `RuntimeDiag::ConversionOutOfRange`：值放不进目标类型（浮点数是 NaN、无穷，或截断后超出整数类型的范围；`int` 超出 `i32` 的范围）。消息里有这个值和目标类型 |
  | `f32(1.0e39)` | `inf`：浮点数转成更窄的浮点数按 IEEE 舍入，超出范围得到无穷，不是故障 |
  | `int('42')`、`float('1.5')` | `42`、`1.5`。**整个字符串必须是一个数**：可以带一个符号（`'-12'`、`'+12'`），整数只有十进制数字，浮点数可以有小数部分和指数（`'1e3'`、`'.5'`）或是 `inf`、`nan`；前后的空白（`' 7'`）、数后面跟着别的东西（`'12abc'`）、对整数写的 `'1.5'`、`'0x10'`、`'1_000'`，是故障 `RuntimeDiag::InvalidNumberText`（它不是数） |
  | `int('99999999999999999999')`、`float('1e999')`、`float('1e-9999')` | 它们是数，但目标类型放不下（上溢或下溢）：故障 `RuntimeDiag::ConversionOutOfRange`，消息引用这段文本、给出目标和范围，与"不是数"区分开 |
  | `parse_int('42')`、`parse_float('x')` | `int!`、`float!`：按同一条规则读，不是一个数时得到错误值（`ParseError`），不是故障（见 3.4） |
  | `string(42)`、`string(true)`、`string([1, 2])` | `'42'`、`'true'`、`'[1, 2]'` |
  | `string(3.5)`、`string(2.0)` | `'3.5'`、`'2.0'`：与 `println` 和 `format` 的 `{}` 相同的文字，最短且读回来是同一个数（[2.5.2](02_lexical.md#252-浮点数)）；`float(string(x))` 得回 `x` |

- **实参是字面量的转换在编译期检查。** `int(1.0e30)`、`i32(5000000000)` 是 `SemanticDiag::LiteralNotRepresentable`（原因：超出范围），`int(1.9)` 是同一个诊断（原因：会被截断），`int('12abc')` 也是同一个诊断（原因：文本不是一个数），`int('99999999999999999999')` 与 `float('1e999')` 则是同一个诊断、原因是超出范围并给出范围，与给字面量声明一个放不下它的类型一样；消息引用写下的源码与原因。实参是算出来的值时，上表里的故障仍然发生在运行期。
- **字面量按上下文取类型**（设计原则 5.1）：依次取期望的类型（带类型的 `let`/`var`、形参、返回位置、`match` 的模式取被匹配值的类型、分支臂里的 `return` 取臂所在位置的期望类型）、二元运算或比较里另一个操作数的类型、默认类型（整数 `int`、小数与带指数的写法 `float`、虚数字面量与带虚部的折叠值 `c128`）。数组字面量的元素、结构体字面量的字段也是这样的位置，所以 `let xs: float[] = [1, 2.0]` 是 `float[]`，`let p: P = { a: 1, b: 2.0 }`（`P = { a: float, b: float }`）里的 `1` 是 `float`。没有期望类型时，同一个数组里的字面量取最宽的一种（整数 < 小数）：`[1, 2.0]` 是 `float[]`。`let a: i32 = 3`、`let b: f32 = 0.1`、`f(3)` 在 `f(x: f32)` 里、`x * 2` 在 `x: f32` 时，`3`、`0.1`、`2` 都是目标类型。字面量之间的运算在编译期按精确值算完再放进类型：`0.1 + 0.2` 是离 0.3 最近的那个 double，`1 / 2` 是 `0`（整数除法），`1.0 / 2` 是 `0.5`，`9223372036854775807 + 1` 放不进 `int` 所以是 `SemanticDiag::LiteralNotRepresentable`（运行时的加法则是回绕）。`as` 不用于数值之间的转换（`1 as float` 是编译期错误），用上面的转换函数。
- **`0.1 + 0.2` 与 `let a = 0.1; a + 0.2` 不是一回事，这是有意的。** 字面量之间在编译期按精确值算，所以 `0.1 + 0.2` 是 `0.3`（离 0.3 最近的 double），而 `let a = 0.1; a + 0.2` 里 `a` 已经是一个 double，按 IEEE 双精度相加，结果是 `0.30000000000000004`。两者不同，因为前者没有"两个 double 相加"这一步：精确的和 `0.3` 直接成为 `0.3` 这个 double。要运行时的浮点行为，就把字面量放进变量；要在编译期得到精确的十进制值，就直接写字面量（设计原则 5.1）。
- 没有 `bool(x)`，也没有从 `bool` 到整数的转换；用比较得到布尔值（`n != 0`），用 `if` 得到整数。
- **整数运算回绕，不报错**：`9223372036854775807 + 1` 得到 `-9223372036854775808`，但那要两个数都在变量里（字面量之间这样写是编译期错误，见上）。`i32(n)` 对算出来的 `n` 截断到 32 位：`i32(3000000000)` 得到 `-1294967296`；写成字面量则是 `SemanticDiag::LiteralNotRepresentable`（`let b: i32 = 3000000000` 同样）。
- **整数除法向零截断**，余数与被除数同号：`7 / 2` 是 `3`，`-7 / 2` 是 `-3`，`-7 % 3` 是 `-1`。`%` 只用于整数。
- **除数为零是故障**（`RuntimeDiag::DivisionByZero`），整数与浮点数都是：`1.0 / 0.0` 不会得到无穷大。浮点运算溢出得到 `inf`。
- `^` 是乘方：`7 ^ 2` 是 `49`，`2.0 ^ 0.5` 是 `1.41421`；整数的负指数得到 `0`。复数也有 `^`：`z ^ n`（`n` 是整数类型）是反复平方，`z ^ 2` 与 `z * z` 逐位相同，负的 `n` 取倒数（不报故障，与实数负指数不报故障一致）；`z ^ w`（`w` 是复数）用平台的 `pow`，只承诺与平台实数 `pow` 同类的误差界（不承诺正确舍入）。指数是整数值的复数（虚部为 0、`|实部| <= 2^53`）也走反复平方，所以 `z ^ (2+0i)` 与 `z ^ 2` 相同。`%` 对复数没有定义。

### 3.2.1 浮点的数值契约

浮点运算的结果由**规范算法**决定，不由某台机器的实现决定（设计原则 7.1）：

- 加、减、乘、除、开方每一步按 IEEE 754 正确舍入，严格按书写的顺序，不把 `a * b + c` 收缩为一次乘加融合。在这个范围内，各后端、各设备的结果逐位相同。
- **`max`、`min` 是 IEEE 754-2019 的 `maximum`、`minimum`**：任一边是 NaN 结果就是 NaN，`-0` 小于 `+0`，与两边的位置无关。`math::max`/`math::min`、张量的 `maximum`/`minimum`、按轴的 `max`/`min`、最大池化、`softmax` 里的最大值都是这一条。求最大、最小值的归约按任何次序得到相同的位。
- **`relu(x)` 是 `maximum(x, +0)`**：`relu(NaN)` 是 NaN，`relu(-0)` 是 `+0`。
- **矩阵乘的规范算法**是每个元素一条按 k 从小到大的乘加融合链：`acc = +0`，然后对每个 k 做 `acc = fma(a[i][k], b[k][j], acc)`。乘加融合本身是一次正确舍入的运算，所以同样逐位可复现。`linear` 与 `x @ w + b` 先算完这条链再加偏置。
- **算子的定义是规范**：`softmax(x)` 就是 `exp(x - m) / sum(exp(x - m))`（`m` 是最大值，和在 f64 里累加、最后舍入为 f32），融合的实现与它逐位相同。
- 超越函数（`exp`、`log` 等）不要求逐位相同，只承诺写明的误差上界（`exp`、`log` 不超过 2 ulp），NaN、无穷、零等特殊值精确。各算子声明的语义可以用 `camel inspect --ops` 查看。
- **复数**：`re`、`im`、`conj` 是精确的（取一个部分、翻转一个符号位，没有算术）；`abs` 是 `hypot`，`exp` 是平台的复指数（它的模是 `exp`、方向是 `sin`/`cos`，是这两者的复合），两者只承诺平台的误差界（4 ulp），不承诺正确舍入，也不总传播 NaN（`hypot(inf, nan)` 是 `inf`，`exp(-inf+nan i)` 是 `0`）。复数的 `+`、`-`、`*`、`/` 是各部分的 IEEE 运算按书写顺序，跨后端逐位相同（`/` 的除数为复数零时是故障）；整数指数的 `^` 是这些乘法的固定序列，所以也逐位相同。

需要更快、而结果可以不同的计算，由程序在函数或块上用许可标签（`#reassociate`、`#contract` 等）显式授予许可（[7.6.1](07_statements.md#761-数值许可标签)）；`reassociate` 与 `contract` 是默认许可（7.3），`#strict` 撤销全部许可。没有授予就是规范算法；没有模块级的默认，也没有命令行开关。

## 3.3 复合值

### 3.3.1 数组

```cml
func main(): int sync {
    let xs = [1, 2, 3]
    let names: string[] = []          // 空数组要写出类型
    let grid = [[1, 2], [3]]          // int[][]
    println(xs[0], len(xs), grid[0][1])
    return 0
}
```

```text
1 3 2
```

- **元素是"有期望类型的位置"**（设计原则 5.1）：期望的类型就是数组的元素类型，字面量元素取它。`let xs: float[] = [1, 2.0]` 与 `let xs: float[] = [1, 2]` 都是 `float[]`；`[1, 2] as float[]` 也一样。
- 没有期望类型时，同一个数组里的字面量取最宽的一种（整数 < 小数）：`[1, 2.0]` 是 `float[]`，也就是 `[1.0, 2.0]`。
- 元素是**已经取好类型的值**（变量、调用结果）时不做隐式转换，类型不同就是错误：`let n = 1; let xs: float[] = [n, 2.0]` 与 `let xs: float[] = [n]` 都是 `SemanticDiag::ElementTypeMismatch`，报在元素上（`Cannot insert data of type 'int' into a(n) array with element type 'float'`）。元素类型之间没有共同的类型且不是字面量时同样是 `SemanticDiag::ElementTypeMismatch`。
- 下标从 0 开始；越界（包括负数）是故障 `RuntimeDiag::IndexOutOfRange`。按整数数组一次取多个位置写 `xs[idx]`（即 `take`），切片写 `xs[a:b]`（是共用数据块的视图），见 [6.3](06_expressions.md#63-访问与下标)。
- 内建函数：

  | 写法 | 结果 |
  |---|---|
  | `len(xs)` | 长度 |
  | `last(xs)` | 最后一个元素（空数组是故障 `RuntimeDiag::IndexOutOfRange`）。负数下标不是从尾部数，所以取末尾元素用它，不写 `xs[-1]` |
  | `a..b` | 区间 `[a, a+1, ..., b-1]`；`a >= b` 时为空。带步长写 `range(a, b, step)` |
  | `xs[a:b]` | 下标 `a` 到 `b-1` 的新数组。`a`、`b` 是 0 到长度之间的位置；越出数组（包括负数）是故障 `RuntimeDiag::SliceOutOfRange`，与下标越界一致。两端都在数组内而 `b < a` 时结果为空。要“至多取 n 个”用 `array` 模块的 `take`、`drop`，它们按定义截断 |
  | `concat(xs, ys)` | 连接成新数组 |
  | `xs->contains<v>` | 是否含有等于 `v` 的元素；“等于”就是元素类型的 `==`（数组、元组、结构体按内容比较，见 [6.7.3](06_expressions.md)） |
  | `zip(xs, ys)` | `(x, y)` 元组的数组；三个或更多数组得到相应元数的元组（`zip(xs, ys, zs)`）；长度必须相同 |
  | `strs->join<sep>` | 把 `string[]` 连成一个字符串 |
  | `map`、`filter`、`reduce`、`foreach` | 见 [6.10](06_expressions.md#610-高阶算子) |
  | `xs->append<v>` | 在末尾加上 `v` 的**新数组**，`xs` 不变；要追加到变量上写 `xs = xs->append<v>`（见 3.7）；连接两个数组用 `concat(xs, ys)` |

- 更多的数组函数在 `array` 模块里，使用前要导入（`import { reverse, sum } from array`）：`reverse`、`take`、`drop`、`enumerate`、`flatten`、`unique`、`index_of`、`find`、`any`、`all`、`count`、`sum`、`min`、`max`、`flat_map`、`sort_by`、`zip_with`，见 [标准库参考](../stdlib-reference.md)。

### 3.3.2 元组

```cml
func main(): int sync {
    let pair = (1, 'one')
    let (n, s) = pair              // 解构
    println(pair.0, pair.1, n, s)
    return 0
}
```

```text
1 one 1 one
```

成员下标是字面量，超出长度是编译期错误（`SemanticDiag::InvalidAccessIndex`）。嵌套的元组可以连写：`t.0.1` 是 `(t.0).1`。单元素元组写作 `(x,)`，空元组 `()`。

### 3.3.3 结构体

结构体类型有两种写法，它们是两种不同的类型（设计原则 5.2）：

- **匿名结构体类型**：直接写字段，如 `{ x: int, y: int }`。它的同一性就是写出来的字段：字段名、字段类型与**书写顺序**都是类型的一部分，`{ x: int, y: int }` 与 `{ y: int, x: int }` 是两个不同的类型（`==`、重载、`Map` 的键都按这个同一性）。
- **命名结构体类型**：`type Point = { x: int, y: int }` 声明一个类型，它的同一性是**声明本身**（声明所在模块 + 名字，泛型再加类型实参）。`Point` 与另一个字段完全相同的 `type Spot = { x: int, y: int }` 是两个不同的类型；打印、诊断、图里显示的都是名字 `Point`。`type P = Other` 不是这样的声明，它只是别名（`P` 与 `Other` 是同一个类型）。

结构体的字面量必须给出类型要求的全部字段。`type` 声明里的字段顺序就是这个类型的字段顺序：按名字访问，打印时按声明顺序，值的布局也按它。匿名结构体类型按书写顺序。

```cml
type Point = { x: int, y: int }

func main(): int sync {
    let p: Point = { x: 3, y: 4 }
    let q = { x: p.x + 1, y: p.y }     // “修改”就是造一个新的
    println(p, q.x)
    return 0
}
```

```text
{ x: 3, y: 4 } 4
```

- **字段是"有期望类型的位置"**（与数组元素相同，设计原则 5.1）：期望的类型就是字段类型，字面量字段取它。`type P = { a: float, b: float }` 时，`let p: P = { a: 1, b: 2.0 }` 的 `1` 是 `1.0`；写字段的顺序不必与声明相同，按名字对应，值按声明顺序存放。
- 字段是**已经取好类型的值**时不做隐式转换：`let x = 1; let p: P = { a: x, b: 2.0 }` 是 `SemanticDiag::DeclaredTypeMismatch`（`{ a: int, b: float }` 不是 `P`），而字面量字段照常取字段类型并可以计算：`let p: P = { a: x + 1.0, b: 2.0 }`。

匿名结构体类型的值可以直接用在命名结构体类型的位置（结构相同即可）：

- 字段名与类型一一对应（顺序可以不同）时它*进入*那个类型，需要时按名字逐个字段复制，结果值的类型就是命名类型；
- 字段集合对不上（多一个、少一个、某个字段类型不对）是编译期错误 `SemanticDiag::DeclaredTypeMismatch`（声明位置）或 `SemanticDiag::NoMatchingFunction`（调用位置）。

两个命名类型之间**没有**隐式转换，即使字段完全相同：`let q: Spot = p` 是编译期错误，要写 `p as Spot`。`as` 按名字逐字段对应，字段集合对不上是 `SemanticDiag::DynamicCastForbidden`；字段集合相同、只是顺序不同时，`as` 复制成新顺序（同序时无需复制）。泛型参数不因此变得宽松：`Box<Point>` 与 `Box<{ x: int, y: int }>` 是不同的类型（设计原则 5.8）。

### 3.3.4 字符串

字符串是不可变的文本，**总是合法的 UTF-8**，长度与下标按码点计：`len('中文')` 是 2，`'中'[0]` 是 `'中'`。二进制数据用内建类型 `Bytes`（和 `string` 一样不必导入），两者之间显式转换：`bytes::encode`、`bytes::decode`（解码非法 UTF-8 得到错误值）。这是字符串与字节之间唯一的转换：没有别的名字能用非法字节造出字符串。内建的操作：

| 写法 | 结果 |
|---|---|
| `len(s)` | 码点数 |
| `s->contains<sub>` | `sub` 是否出现在 `s` 里（逐字节比较，结果与按码点相同）；空串总是出现：`'hello'->contains<'ell'>` 是 `true`。写法与数组的 `contains` 相同 |
| `a + b` | 连接 |
| `==`、`!=`、`<`、`>`、`<=`、`>=` | 按码点顺序比较（与 UTF-8 字节序相同），真前缀在前 |
| `string(x)`、`int(s)`、`float(s)`、`parse_int(s)`、`parse_float(s)` | 转换，见 3.2 |
| `fmt->format<a, b, ...>` | 格式化：`{}` 依次取实参，`{:.2f}`、`{:5d}`、`{:.17g}` 等指定格式；实参不够是故障 |
| `` `x = ${x}` `` | 格式化字符串，是 `'x = {}'->format<x>` 的简写（[2.5.4.1](02_lexical.md#2541-格式化字符串)） |
| `strs->join<sep>` | 连接字符串数组 |
| `s[i]` | 第 `i` 个码点，结果是只含这一个字符的字符串；越界是故障 |

其余的字符串函数在 `str` 模块里（[标准库参考](../stdlib-reference.md)）：`split`、`trim`、`to_upper`、`to_lower`、`replace`、`index_of`（位置按码点；切片是核心的 `s[a:b]`）、`starts_with`、`ends_with`、`repeat`、`codepoints`、`from_codepoints`。

```cml
import { split, trim, to_upper } from str

func main(): int sync {
    println('a, b ,c'->split<','>->map<(w: string): string => trim(w)>)
    println('héllo'->to_upper, len('héllo'), 'héllo'[1], 'hello'[1])
    return 0
}
```

```text
[a, b, c]
HÉLLO 5 é e
```

`s[i]` 的结果是单字符的字符串，不是数值；取码点的数值用 `str::codepoints`。字符串字面量里的转义 `\xHH` 只能拼出合法的 UTF-8（`'\xE4\xB8\xAD'` 是 `'中'`，单独的 `'\xE4'` 是编译错误）；`\u{4E2D}` 直接写码点。

### 3.3.5 不能比较的值

`==`、`!=` 用于数值、`bool`、`string`、`Map`、`Set`、枚举，以及数组、元组、结构体和张量（数值数组）：整体比较是形状相同并且逐元素相等处处成立，结果是 `bool`；数组、元组、结构体按内容逐个比较，元素类型本身要能比较（[6.7.3](06_expressions.md#673-比较)）。`===`、`!==` 是把同一件事逐元素做，见 [6.7.3](06_expressions.md#673-比较)。函数、含函数值的复合值、单独的 `T?`/`T!` 没有 `==`（编译期错误）。

## 3.4 可能为空 `T?` 与可能出错 `T!`

两种专用的内建形式，各带一位标记：

- **`T?`**：值要么是 `null`，要么是一个 `T`。
- **`T!`**：值要么是一个 `T`，要么是一个错误。错误是固定的内建结构 `{ kind: string, message: string, location: string }`（种类名、消息、发生处的 `文件:行:列`），所有可能出错的函数共用这一种。

```cml
func positive(x: int): int? {
    return if x > 0 then x else null
}

func safe_div(a: int, b: int): int! {
    return if b == 0 then { return error('DivByZero', 'the divisor is zero') } else a / b
}

func main(): int sync {
    println(positive(4) ?? 0, positive(-4) ?? 0)
    println(safe_div(9, 3) !! -1, safe_div(9, 0) !! -1)
    println(safe_div(9, 0))
    return 0
}
```

```text
4 0
3 -1
error DivByZero: the divisor is zero
```

规则：

- `T?`、`T!` 与 `T` 是不同的类型。这两种值可以保存、传递、返回、打印，但**不能当作 `T` 使用**：算术、比较等运算符与接受 `T` 的函数都不接受它们，也不能对它们取成员。只能用 `??`、`!!`、`->`（[6.6](06_expressions.md#66-空值与错误的运算符)）或 `match` 的固定模式（[6.8.2](06_expressions.md#682-match)）拆开。
- **`T?` 可以与 `null` 比较**：`x == null`、`x != null` 得到 `bool`。这样的比较作 `if` 的条件时，在排除了空值的那条臂里 `x` 是 `T`（[6.8.1](06_expressions.md#681-if)）。两个 `T?` 之间不比较；永远不为空的值（`int`、`T!`）与 `null` 比较可以写，结果恒定，给出警告（[6.7.3](06_expressions.md#673-比较)）。
- **一个被求值了、却没有任何东西使用的 `T!` 是警告**（`SemanticDiag::UnusedFallibleValue`）：它可能带着的错误会无声无息地丢掉。见 [7.1](07_statements.md#71-表达式语句)。
- **值进入 `T?`、`T!` 的位置时自动装入**，只要目标类型已知；`null` 同理。这些位置是：
  - 调用的实参：形参是 `x: int?` 时可以写 `f(5)`、`f(null)`。有重载时精确匹配的优先：`f(5)` 在 `f(x: int)` 与 `f(x: int?)` 之间取前者；没有精确匹配时字面量进入形参类型，这一遍仍有多个候选就是歧义（`SemanticDiag::AmbiguousCall`），要写明字面量的类型（如 `f32(2)`）或加转换；
  - 带类型的声明 `let x: T? = v`；
  - 数组的元素：`let xs: (int?)[] = [1, null, 3]`；没有写类型时，元素里有 `null` 的数组字面量是 `T?` 的数组（`[1, null, 3]` 是 `(int?)[]`）；
  - 分支的两条臂：一条臂是 `T`、另一条是 `null` 或 `T?` 时，分支是 `T?`（`if c then 1 else null` 是 `int?`）；`T` 与 `T!` 同理。一条臂根本没有值（`void`：调用了不返回值的函数、空块）时按 `null` 算：分支是 `T?`，那条臂给出 `null`；
  - 返回类型声明为 `T?`/`T!` 的函数里的 `return v`（`return e` 交出一个错误值）。

  也可以显式写 `v as T?`。可以一次进入多层：`let x: int?! = 5`。图上每装入一层是一个转换节点（`CAST`），自动装入就是由编译器补上这个节点，与手写 `as` 得到的图相同。
- **反方向永不自动。** `T?`、`T!` 要当 `T` 用，必须拆开；`int?` 也不会变成 `float?`，数值之间仍不做隐式转换。内建算子（`append`、`println` 等）的实参不在此列：它们的形参类型由 C++ 的类型规则从实参算出，没有可供装入的目标类型，需要时写 `as`。
- **空数组字面量 `[]` 的元素类型取自它所在的位置**：声明的类型（`let xs: int[] = []`）、形参（`count([])`）、分支的另一条臂、`??`/`!!` 的左侧（`attempt(f) !! []`）、声明的返回类型、`as`（`[] as Map<string, int>`）。把 `[]` 绑定到没有写类型的名字上（`let xs = []`）是编译期错误 `SemanticDiag::EmptyArrayWithoutType`，诊断提示写出类型。
- 两种标记可以组合：`T?!`、`T!?`。标记从最内层写起：`T?!` 是 fallible(nullable(T))，`T!?` 是 nullable(fallible(T))；`match` 只拆开最外层。同一种不能重复。数组元素等位置需要括号：`(int?)[]`。
- 与泛型配合：函数头声明 `T` 后，形参类型 `T?` 从可空实参里绑定 `T`（[7.5](07_statements.md#75-泛型函数)）。
- `let x: int! = v` 中 `!` 与 `=` 之间要有空格。
- 内建的可能出错的函数：`parse_int(s)`、`parse_float(s)`，`fs` 模块的 `read_text(path)`、`remove(path)`、`remove_dir(path)`，`json` 模块的 `decode`、`decode_as`。`error(kind, message)` 构造错误值并记下调用处的位置。

**错误与故障是两回事。** 只有声明为返回 `T!` 的操作才产生错误值。除零、下标越界、递归过深是故障：程序带位置终止，`!!` 接不住它（见 [4.6](04_execution_model.md#46-故障退出与资源上限)）。

要在函数体里主动产生一个故障，写 `fail<'Kind'>('消息')`：它是类型 `never` 的表达式（没有值），故障的种类由尖括号里的名字给出，`attempt` 能把它隔离成错误值。

**当前实现的行为，尚待确定**：`match` 对这两种值恰好两个分支；它们可以打印，但除了 `T?` 与 `null` 比较之外不能用 `==` 比较。

## 3.5 枚举 `enum`

`enum` 是一个**名义类型**：一个值恰好是声明里列出的变体之一，每个变体可以不带数据、带一个元组或带一个结构体。两个同形的 `enum` 是不同的类型。

```cml
type Shape = enum {
    Circle(float),
    Rect { w: float, h: float },
    Empty,
}

type Option<T> = enum { Some(T), None }
type Tree<T> = enum { Leaf, Node(Tree<T>, T, Tree<T>) }
```

**声明。** `type 名字 = enum { 变体, ... }`，末尾逗号可有可无。变体三种形状：`Empty`（无数据）、`Circle(float)`（一个或多个位置字段）、`Rect { w: float, h: float }`（具名字段，字段之间用 `,` 分隔）。同一个 `enum` 里变体名不能重复（`DuplicateEnumVariant`），结构体变体内字段名不能重复（`DuplicateEnumField`）。类型可以带参数（`Option<T>`），变体的字段类型可以引用这个 `enum` 本身（`Tree<T>`），也可以引用别的 `enum`。`enum` 是关键字，不能用作名字。

**构造。** 变体写在类型名后面，用 `::` 限定：`Shape::Circle(1.0)`、`Shape::Rect { w: 2.0, h: 3.0 }`、`Shape::Empty`。结构体变体要写全字段，顺序不限；元组变体按位置给全。无数据的变体就是那个值本身，不写括号。构造出的值有类型 `Shape`；泛型实例的参数由字段的实参推出（`Option::Some(42)` 是 `Option<int>`），推不出时（`Option::None`）由期望的类型推出（`let x: Option<int> = Option::None`、实参位置、返回类型），仍推不出是编译期错误（`EnumTypeArgumentNotInferred`）。字段的类型必须与声明一致（`EnumFieldTypeMismatch`），个数与字段名也必须一致（`EnumVariantFieldCount`、`EnumConstructionMismatch`）；字段的实参按与声明字段相同的规则接受自动装入（`int` 进入 `float` 字段不自动，数值之间仍不做隐式转换）。

**`match`。** 枚举靠 `match` 取出数据，不能直接取字段（`b.w` 是 `EnumFieldAccess`，诊断提示用 `match`）：

<!-- camel-snippet: fragment: continues the Shape enum declared in the example above -->

```cml
func area(s: Shape): float {
    return match s {
        case Shape::Circle(r) => 3.14159 * r * r
        case Shape::Rect { w, h } => w * h
        case Shape::Empty => 0.0
    }
}
```

- 元组变体的模式给出全部位置，不要的字段写 `_`：`case Shape::Circle(_)`；结构体变体的模式只写要绑定的字段，没写的字段忽略，可以改名 `{ w: width }`，简写 `{ w }` 绑定同名变量；无数据的变体只写名字 `case Shape::Empty`；`case _` 匹配剩下的所有值。
- **穷尽**：列全了所有变体就不必写 `case _`；漏了变体又没有 `case _` 是编译期错误 `EnumMatchNotExhaustive`，诊断列出漏掉的变体名。已被前面的臂覆盖的变体再写一次给警告（`EnumVariantCaseRepeated`）；`case _` 之后的臂永远不执行，给警告（`MatchCaseAfterWildcard`）；列全所有变体之后还写 `case _`，这个通配符永远不会命中，给警告（`MatchWildcardRedundant`）。
- 模式必须写现在匹配的类型的变体，前缀（`Shape::`）与变体名都要对：写错是 `EnumPatternMismatch`，诊断列出该类型声明的变体。
- **模式里不带 `::` 的名字是绑定名**（`case v` 用于 `T?`／`T!`），不是变体：单元变体要写全限定名 `case Shape::Empty`。写成 `case Empty` 是 `EnumPatternMismatch`，诊断指出 `Empty` 是该枚举的变体、并给出 `case Shape::Empty`。
- **嵌套模式不在语言里**：字段位置只写名字或 `_`，所以 `case Shape::Circle(0.0)` 这种把模式写在字段位置的写法是**普通语法错误**（在写下的那个 `(` 或 `0.0` 处），为每个字段写名字或 `_`，在臂里再匹配。
- `match` 的值可以是任意表达式，包括返回 `enum` 的调用；各臂结果类型的统一与现有的 `match` 相同（[6.8.2](06_expressions.md#682-match)）。

**相等与打印。** 同一个 `enum` 实例类型的两个值可以 `==`、`!=`：先比变体，再按声明顺序逐字段比较；任一字段类型不能比较时，这个 `enum` 也不能比较（编译期错误）。打印与源码写法一致：`Shape::Circle(1.0)`、`Shape::Rect { w: 2.0, h: 3.0 }`、`Shape::Empty`；泛型实例不打印类型参数（`Option::Some(42)`，不是 `Option::Some<int>(42)`）。

**泛型。** `Option<int>` 与 `Option<float>` 是两个不同的类型，各是各的值。字段类型可以再用参数：`type Pair<A, B> = enum { Both(A, B), Neither }`，`Pair<int, string>` 的 `Both` 收一个 `int` 和一个 `string`。

**图上。** 不增加节点种类：构造是"装入一个标签和数据"，`match` 是"取标签、按标签分支、在臂里取出数据"，与 `T?`、`T!` 相同。取标签与取字段是普通的内部算子（`__enum_tag__`、`__enum_field__`），调度器不对它们按名字特判。`std::nvm`、`std::fvm`、`std::jit`、`std::prl`、`std::macro` 与 `std::cpp` 都支持 `enum`；ONNX 导出与自动微分给出带位置的拒绝。

## 3.6 映射与集合

`Map<K, V>` 与 `Set<T>` 是内建类型，不需要 `import`。没有专门的字面量，用数组加 `as` 构造：

```cml
func main(): int sync {
    let ages = [('bob', 31), ('alice', 28)] as Map<string, int>
    let more = ages->put<'carol', 45>->remove<'bob'>
    println(ages)
    println(more, len(more), keys(more))
    println(ages['alice'] ?? 0, ages['dave'] ?? 0)

    let seen = [3, 1, 2, 3, 1] as Set<int>
    println(seen, seen->contains<2>, union(seen, [9] as Set<int>))
    println([] as Map<string, int>)
    return 0
}
```

```text
{alice: 28, bob: 31}
{alice: 28, carol: 45} 2 [alice, carol]
28 0
{1, 2, 3} true {1, 2, 3, 9}
{}
```

- **值不可变**：每个操作返回新值，原来的不变。
- **`m[k]` 是查找**，类型是 `V?`，键不存在时为 `null`，用 `??` 取出。集合没有下标。
- **元素类型必须完全一致**：`[('a', 1)] as Map<string, float>` 是错误，写 `1.0`。带类型的声明不是转换，必须写 `as`。`Map`、`Set` 必须带类型实参。
- 数组字面量里写两个相同的常量键是编译期错误 `SemanticDiag::DuplicateCollectionKey`；集合里的重复元素被去掉。

| 写法 | 结果 | 说明 |
|---|---|---|
| `m[k]`、`get<k>(m)` | `V?` | 查不到是 `null` |
| `get_or<k, d>(m)` | `V` | 查不到时为 `d` |
| `put<k, v>(m)` | `Map<K, V>` | 键已存在则替换 |
| `remove<k>(m)`、`remove<x>(s)` | 同类型 | 不存在时内容不变 |
| `has<k>(m)`、`contains<x>(s)` | `bool` | |
| `len(m)`、`len(s)` | `int` | |
| `keys(m)`、`values(m)`、`items(m)` | `K[]`、`V[]`、`(K, V)[]` | 按键的顺序 |
| `merge(m1, m2)` | `Map<K, V>` | 键相同时取 `m2` 的 |
| `add<x>(s)` | `Set<T>` | |
| `union(a, b)`、`intersect(a, b)`、`difference(a, b)` | `Set<T>` | |
| `is_subset(a, b)` | `bool` | |
| `items(s)` | `T[]` | 升序 |
| `a == b`、`a != b` | `bool` | 条目相同即相等 |

带 `with` 实参的操作只有一种调用写法，与标准库的约定一致：被处理的映射或集合是普通参数，其余写在尖括号里，`put<k, v>(m)`，或用管道写成 `m->put<k, v>`（两者是同一个调用）；两个参数地位对等的操作两个都是普通参数（`merge(m1, m2)`、`union(a, b)`）。把它们全写成普通参数（`put(m, k, v)`、`get(m, k)`、`has(m, k)`）是编译期错误 `SemanticDiag::NoMatchingFunction`，诊断会指出哪些实参应当写在尖括号里并给出改写后的形式；对其他标准库函数也一样（`split('a,b', ',')` 应写作 `'a,b'->split<','>`）。在一个实参表里写两个前缀形式的调用（`f(has<a>(m), has<b>(m))`）目前会被解析成比较，这时用管道形式 `f(m->has<a>, m->has<b>)`。

遍历先取数组，再用高阶算子：

```cml
func count_words(words: string[]): Map<string, int> {
    return words->reduce<(acc: Map<string, int>, w: string): Map<string, int> => acc->put<w, (acc[w] ?? 0) + 1>, [] as Map<string, int>>
}

func main(): int sync {
    println(count_words(['a', 'b', 'a']))
    return 0
}
```

```text
{a: 2, b: 1}
```

`put` 每次复制整张表，逐条构造很大的映射时，先得到条目数组再一次 `as` 转换。程序自己声明的名为 `Map`、`Set` 的类型，以及与上述算子同名的函数，优先于内建的名字。

**当前实现的行为，尚待确定**：

- 键的类型只能是 `int`、`i32`、`bool`、`string` 以及由它们组成的元组（其他类型是 `SemanticDiag::CollectionKeyNotAllowed`）。
- 顺序是按键排序（整数按数值，`false` 在 `true` 前，字符串按字节，元组逐个元素），`keys`、`values`、`items`、打印都按这个顺序。
- 运行期才知道的数组里有重复的键时，后面的条目覆盖前面的。
- 值类型本身可空时（`Map<string, int?>`），`m[k]` 的类型就是 `int?`：键不存在与值为 `null` 无法区分，需要时用 `has`。
- 三种调用写法并存；打印的格式；`json::decode_as` 用类型实参指定解码的目标类型。

实现与取舍见 [技术文档 31](../technical/31_map_and_set.md)。

## 3.7 `let`、`var` 与引用

- `let` 引入不可变的名字，`var` 引入可变的变量。同一个作用域里同一个名字不能声明两次（`SemanticDiag::Redeclaration`）。
- **可以给变量赋值，也可以给从它出发的一条路径赋值。** 路径是变量本身加任意多个字段与下标：`p.x = 5`、`xs[0] = 5`、`m[i].f = 5`、`xs[0].values[1].n = 5` 都合法，复合赋值同样（`xs[0] += 1`）。它们是给整个变量重新绑定：`xs[i].f = e` 就是 `xs = set(xs, i, set(xs[i], f, e))`（[6.11](06_expressions.md#611-赋值)）。不是变量的根（`f().x = 5`、`{ x: 1 }.x = 5`）才是编译期错误 `SemanticDiag::AssignmentTargetNotVariable`；下标越界是故障 `IndexOutOfRange`。在 `sync` 里，写 `var` 数组的一个元素不复制——除非这个数组被别处读过（`let a = xs`），此时先复制，`a` 不变。
- 对 `let` 赋值、或把 `let` 传给 `var` 形参，是编译期错误 `SemanticDiag::ImmutablePassedToVarParam`。这是编译期的检查，不是对内存上锁。
- **一切值按引用传递。** `var` 形参是引用：函数对它的写入就是对调用方变量的写入。lambda 与分支对变量的捕获也是引用。

```cml
func main(): int sync {
    var count = 0
    count = count + 1
    count += 10
    println(count)

    var xs = [1, 2]
    xs = concat(xs, [3])       // 给整个变量一个新数组
    let ys = xs->append<4>     // 新数组；xs 不变
    xs = xs->append<5>         // 追加到变量上：与 x = x + 1 一样，绑定到新数组
    println(xs, ys)
    return 0
}
```

```text
11
[1, 2, 3, 5] [1, 2, 3, 4]
```

Camel 崇尚不可变：`var` 与赋值只是为了方便留的口子，应当尽量少用。

**标准库的算子都返回新值，不修改参数**（设计原则 11.3）：`append`、`extend`、`apply`、`array` 模块的 `sort` 等都是如此，`let ys = xs->append<5>` 之后 `xs` 不变。要"追加到变量上"写 `acc = acc->append<5>`，与 `x = x + 1` 一样；没有另设显式的就地算子。

- **纯运算的结果没有被使用是编译期错误**（`SemanticDiag::UnusedPureResult`）：写成语句的 `xs->append<5>` 什么也不做，诊断提示改写为 `xs = xs->append<5>`（`xs` 不是变量时提示使用结果，或用 `let _ = ...` 有意丢弃）。这里的"纯运算"是算子表中声明为纯的算子（内建算子与 C++ 模块的算子，`camel inspect --ops` 列出纯度）作为一条语句、而这条语句的值没有被使用；用户函数的调用不在此列。块的最后一条语句是块的值：块的值被使用（被返回、绑定、传递）时不报；块的值被丢弃时——声明为 `: void` 的函数体、写在 `foreach` 上的元素函数、没有 `else` 的 `if` 的臂、写成语句的块——它的最后一条语句同样报这个错误。
- **就地追加只是实现层面的优化，不进入语义。** `acc = acc->append<v>` 写成一条语句、`acc` 是变量、`append` 是内建的那个时，编译器把它写成同一个重新绑定的另一种实现：变量里的数组只被这个变量引用（自上一次就地追加以来没有任何对 `acc` 的读取）时在原处追加，否则先复制（并留出增长的余量）。于是循环里反复追加仍是线性的。中间读一次 `acc` 而读到的值可能被留住（`let snap = acc`、把它传给函数、取出的元素本身是数组），下一次追加就会复制，`snap` 不变；读到的值只交给留不住数组的运算（`len(acc)`、取数值元素 `acc[i]`、比较、`format`），且这些运算在同步块里、排在下一次写之前，这样的读不影响原处追加。
- **`xs = xs->sort` 同理**（设计原则 11.3）：`sort` 返回新数组，返回新值与就地修改不用两个名字区分（没有 `sorted`，也没有显式的就地排序算子）。`xs = xs->sort`（或 `xs = sort(xs)`、`merge_sort` 等）写成一条同步块里的语句、`xs` 是变量时，变量是数组唯一的持有者就在原处排序，否则先复制再排序；`let before = xs` 这样留住旧值的仍得到旧值。这只是实现层面的优化，由算子声明“就地形式”（`OperatorTable::setInPlaceForm`）来开启，程序写不出也看不到它。张量另有显式的就地算子（11.3），数组没有。

赋值与顺序的关系见 [4.4](04_execution_model.md#44-var-与写入的顺序)。

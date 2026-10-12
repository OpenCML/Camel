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
| 布尔 | `bool` | `true`、`false` |
| 字符串 | `string` | 字节序列，不可变 |
| 数组 | `T[]` | 元素类型相同，长度在运行时确定 |
| 元组 | `(T1, T2, ...)` | 按位置，用 `.0`、`.1` 访问 |
| 结构体 | `{ x: T1, y: T2 }` | 按名字，用 `.x` 访问 |
| 函数 | `(x: T) => R` | 见 [6.9](06_expressions.md#69-lambda-与函数类型) |
| 可能为空 | `T?` | 见 3.4 |
| 可能出错 | `T!` | 见 3.4 |
| 映射、集合 | `Map<K, V>`、`Set<T>` | 见 3.5 |
| 无值 | `void` | 没有返回值的函数的返回类型；它唯一的值打印为 `null` |
| 模块类型 | 如 `Tensor`、`Tensor<f32, const [2, 3]>` | 由模块引入，尖括号里的实参由该类型自己解释 |

类型名 `any` 存在，供内建算子的签名使用（如 `println` 接受任何值）；用户代码里不能把值声明为 `any`，静态类型是 `any` 的值打印为 `<any>`。

用 `type` 给类型起名字，见 [7.7](07_statements.md#77-类型声明)：

```cml
type Point = { x: int, y: int }
type Pair<T> = (T, T)
```

## 3.2 数值

- **不做隐式转换。** `1 + 2.0`、`1 < 2.0`、`i32` 与 `int` 混算都是编译期错误（`SemanticDiag::NoMatchingFunction`）。需要时显式转换。
- 转换函数：`int(x)`、`i64(x)`、`i32(x)`、`float(x)`、`f64(x)`、`f32(x)`、`string(x)`。

  | 写法 | 结果 |
  |---|---|
  | `float(3)` | `3.0` |
  | `int(3.99)`、`int(-3.99)` | `3`、`-3`（向零截断） |
  | `int('42')`、`float('1.5')` | `42`、`1.5`；文本不是数字是故障 `RuntimeDiag::InvalidNumberText` |
  | `parse_int('42')`、`parse_float('x')` | `int!`、`float!`：不是数字时得到错误值，不是故障（见 3.4） |
  | `string(42)`、`string(true)`、`string([1, 2])` | `'42'`、`'true'`、`'[1, 2]'` |
  | `string(3.5)` | `'3.500000'`（固定 6 位小数；要别的格式用 `format`） |

- **带类型的声明给字面量定类型**：`let a: i32 = 3`、`let b: f32 = 0.1`。`as` 不用于数值之间的转换（`1 as float` 是编译期错误），用上面的转换函数。
- 没有 `bool(x)`，也没有从 `bool` 到整数的转换；用比较得到布尔值（`n != 0`），用 `if` 得到整数。
- **整数运算回绕，不报错**：`9223372036854775807 + 1` 得到 `-9223372036854775808`。`i32(3000000000)` 与 `let b: i32 = 3000000000` 得到 `-1294967296`（截断到 32 位）。
- **整数除法向零截断**，余数与被除数同号：`7 / 2` 是 `3`，`-7 / 2` 是 `-3`，`-7 % 3` 是 `-1`。`%` 只用于整数。
- **除数为零是故障**（`RuntimeDiag::DivisionByZero`），整数与浮点数都是：`1.0 / 0.0` 不会得到无穷大。浮点运算溢出得到 `inf`。
- `^` 是乘方：`7 ^ 2` 是 `49`，`2.0 ^ 0.5` 是 `1.41421`；整数的负指数得到 `0`。

## 3.3 复合值

### 3.3.1 数组

```cml
let xs = [1, 2, 3]
let names: string[] = []          // 空数组要写出类型
let grid = [[1, 2], [3]]          // int[][]
println(xs[0], len(xs), grid[0][1])
```

```text
1 3 2
```

- 字面量的元素类型必须相同（`[1, 2.0]` 是 `SemanticDiag::ElementTypeMismatch`）。
- 下标从 0 开始；越界（包括负数）是故障 `RuntimeDiag::IndexOutOfRange`。
- 内建函数：

  | 写法 | 结果 |
  |---|---|
  | `len(xs)` | 长度 |
  | `head(xs)`、`tail(xs)` | 第一个元素；去掉第一个元素的数组 |
  | `range(a, b)` | `[a, a+1, ..., b-1]`；`a >= b` 时为空 |
  | `xs->slice<a, b>` | 下标 `a` 到 `b-1` 的新数组；超出的部分被截掉 |
  | `concat(xs, ys)` | 连接成新数组 |
  | `xs->contains<v>` | 是否含有 `v` |
  | `zip(xs, ys)` | `(x, y)` 元组的数组 |
  | `strs->join<sep>` | 把 `string[]` 连成一个字符串 |
  | `map`、`filter`、`reduce`、`foreach` | 见 [6.10](06_expressions.md#610-高阶算子) |
  | `array` 模块 | `reverse`、`take`、`drop`、`enumerate`、`flatten`、`unique`、`index_of`、`find`、`any`、`all`、`count`、`sum`、`min`、`max`、`flat_map`、`sort_by`、`zip_with`，见 [标准库参考](../stdlib-reference.md) |
  | `xs->append<v>`、`xs->extend<ys>` | **原地修改**，`xs` 必须是 `var`（见 3.6） |

### 3.3.2 元组

```cml
let pair = (1, 'one')
let (n, s) = pair              // 解构
println(pair.0, pair.1, n, s)
```

```text
1 one 1 one
```

成员下标是字面量，超出长度是编译期错误（`SemanticDiag::InvalidAccessIndex`）。单元素元组写作 `(x,)`，空元组 `()`。

### 3.3.3 结构体

```cml
type Point = { x: int, y: int }

let p: Point = { x: 3, y: 4 }
let q = { x: p.x + 1, y: p.y }     // “修改”就是造一个新的
println(p, q.x)
```

```text
{ x: 3, y: 4 } 4
```

结构体类型按字段的名字与类型比较，`type` 只是起名字。字面量必须给出类型要求的全部字段。

### 3.3.4 字符串

字符串是不可变的字节序列，长度与下标都按字节。内建的操作：

| 写法 | 结果 |
|---|---|
| `len(s)` | 字节数 |
| `a + b` | 连接 |
| `==`、`!=`、`<`、`>`、`<=`、`>=` | 按字节逐个比较，真前缀在前 |
| `string(x)`、`int(s)`、`float(s)`、`parse_int(s)`、`parse_float(s)` | 转换，见 3.2 |
| `fmt->format<a, b, ...>` | 格式化：`{}` 依次取实参，`{:.2f}`、`{:5d}`、`{:.17g}` 等指定格式；实参不够是故障 |
| `strs->join<sep>` | 连接字符串数组 |
| `s[i]` | 第 `i` 个字节，结果是只含这一个字节的字符串；越界是故障 |

其余的字符串函数在 `str` 模块里（[标准库参考](../stdlib-reference.md)）：按字节的 `split`、`trim`、`to_upper`、`to_lower`、`replace`、`find`、`substr`、`starts_with`、`ends_with`、`repeat`、`bytes`；按码点的 `char_count`、`char_at`、`char_substr`、`codepoints`、`from_codepoints`。

```cml
import { split, trim, to_upper, char_count } from str

func main(): int sync {
    println('a, b ,c'->split<','>->map<(w: string): string => trim(w)>)
    println('héllo'->to_upper, len('héllo'), char_count('héllo'), 'hello'[1])
    return 0
}
```

```text
[a, b, c]
HéLLO 6 5 e
```

`s[i]` 的结果是单字节字符串还是数值，是当前实现的行为，尚待确定。

### 3.3.5 不能比较的值

`==`、`!=` 用于数值、`bool`、`string`、`Map`、`Set`。数组、元组、结构体、函数、`T?`/`T!` 没有 `==`（编译期错误）；需要时比较它们的成员。`===` 见 [6.7.3](06_expressions.md#673-比较)。

## 3.4 可能为空 `T?` 与可能出错 `T!`

两种专用的内建形式，各带一位标记：

- **`T?`**：值要么是 `null`，要么是一个 `T`。
- **`T!`**：值要么是一个 `T`，要么是一个错误。错误是固定的内建结构 `{ kind: string, message: string, location: string }`（种类名、消息、发生处的 `文件:行:列`），所有可能出错的函数共用这一种。

```cml
func positive(x: int): int? {
    return if x > 0 then { return x } else { return null }
}

func safe_div(a: int, b: int): int! {
    return if b == 0 then {
        throw error('DivByZero', 'the divisor is zero')
    } else {
        return a / b
    }
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

- `T?`、`T!` 与 `T` 是不同的类型。这两种值可以保存、传递、返回、打印，但**不能当作 `T` 使用**：算术、比较等运算符与接受 `T` 的函数都不接受它们，也不能对它们取成员。只能用 `??`、`!!`、`?->`（[6.6](06_expressions.md#66-空值与错误的运算符)）或 `match` 的固定模式（[6.8.2](06_expressions.md#682-match)）拆开。
- **`T` 不会自己变成 `T?`/`T!`。** 进入这两种形式的写法只有：
  - `v as T?`、`v as T!`；
  - 带类型的声明 `let x: T? = v`；
  - 返回类型声明为 `T?`/`T!` 的函数里的 `return v`（`throw e` 给出错误，`return null` 给出空）。

  调用的实参不转换：把 `5` 传给形参 `x: int?` 是错误，写 `f(5 as int?)`、`f(null as int?)`。
- 分支各臂的类型必须相同：`if c then 1 else null` 是错误。在返回 `T?` 的函数里把两臂都写成带 `return` 的块（如上例），或对两臂都用 `as`。
- 两种标记可以组合：`T?!`、`T!?`。同一种不能重复。数组元素等位置需要括号：`(int?)[]`。
- 与泛型配合：`(typeas T)?` 从可空实参里绑定 `T`（[7.5](07_statements.md#75-泛型函数)）。
- `let x: int! = v` 中 `!` 与 `=` 之间要有空格。
- 内建的可能出错的函数：`parse_int(s)`、`parse_float(s)`，`fs` 模块的 `try_read_text(path)`，`json` 模块的 `decode_as`。`error(kind, message)` 构造错误值并记下调用处的位置。

**错误与故障是两回事。** 只有声明为返回 `T!` 的操作才产生错误值。除零、下标越界、递归过深是故障：程序带位置终止，`!!` 接不住它（见 [4.6](04_execution_model.md#46-故障退出与资源上限)）。

**当前实现的行为，尚待确定**：`T` 不隐式流入 `T?`/`T!`；允许 `T?!` 与 `T!?`；`throw` 只接受 `error(...)` 造出的错误值；`?->` 只对 `T!` 有定义；`??`/`!!` 右侧的类型必须是 `T`；`match` 对这两种值恰好两个分支；它们可以打印但不能用 `==` 比较。早先预留的记号 `T ? U` 与 `T!` 的关系未定，使用它是编译期错误（`SemanticDiag::FeatureNotSupported`）。

## 3.5 映射与集合

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
| `has<k>(m)`、`has<x>(s)`、`contains<x>(s)` | `bool` | |
| `len(m)`、`len(s)` | `int` | |
| `keys(m)`、`values(m)`、`items(m)` | `K[]`、`V[]`、`(K, V)[]` | 按键的顺序 |
| `merge(m1, m2)` | `Map<K, V>` | 键相同时取 `m2` 的 |
| `add<x>(s)` | `Set<T>` | |
| `union(a, b)`、`intersect(a, b)`、`difference(a, b)` | `Set<T>` | |
| `is_subset(a, b)` | `bool` | |
| `items(s)` | `T[]` | 升序 |
| `a == b`、`a != b` | `bool` | 条目相同即相等 |

带 `with` 实参的操作有三种等价的写法：`put<k, v>(m)`、`m->put<k, v>`、`put(m, k, v)`。

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
- 三种调用写法并存；打印的格式；`json::decode_as` 用一个值的类型指定解码的目标类型。

实现与取舍见 [技术文档 31](../technical/31_map_and_set.md)。

## 3.6 `let`、`var` 与引用

- `let` 引入不可变的名字，`var` 引入可变的变量。同一个作用域里同一个名字不能声明两次（`SemanticDiag::Redeclaration`）。
- **只能给整个变量赋值。** 字段与元素不可赋值：`p.x = 5`、`xs[0] = 5` 是编译期错误 `SemanticDiag::AssignmentTargetNotVariable`。给变量一个新的值：`p = { x: 5, y: p.y }`。
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
    xs->append<4>              // 原地修改：要求 xs 是 var
    println(xs)
    return 0
}
```

```text
11
[1, 2, 3, 4]
```

Camel 崇尚不可变：`var` 与赋值只是为了方便留的口子，应当尽量少用。原地修改的算子（`append`、`extend`）修改的是对象本身，经别的名字拿到同一个对象的地方也会看到；优先使用返回新值的 `concat`。

赋值与 `wait` 的关系见 [4.4](04_execution_model.md#44-var引用与-wait)。

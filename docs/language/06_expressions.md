# 6. 表达式

本章详细描述 Camel 的**表达式**：求值顺序、原子、访问/下标/调用/泛型、管道、一元与类型断言、可空、各类运算、控制表达式（if/match；try 尚未实现）、wait、Lambda，以及**运算符优先级**。每类都配有示例。

---

## 6.1 求值顺序

- 表达式按**运算符优先级与结合性**求值；子表达式先于父表达式（除短路与特殊语义外）。
- **短路求值**：**`&&`**、**`||`** 在确定结果后不再求值右侧；**`??`**/ **`!!`** 依实现。
- **管道** **`a->b`**：先求值 **a**，再以该值为参数调用 **b**（或传入 b 的某一参数位）。
- **泛型/显式参数** **`expr<args>`**：先确定被调用对象与类型/值参数，再按调用语义求值。

---

## 6.2 原子（primaryData）

### 6.2.1 标识符与限定引用

- 变量名、函数名，或带 `::` 的限定引用。

```cml
n
fib
println
now
format
WIDTH
now_head
predicate
Tensor::ones
Parameter::new
tensor::shape
```

### 6.2.2 字面量

- 整数、实数、字符串、多行字符串、F 字符串、**true**、**false**、**null**。

```cml
0
30
1.0
'Hello, Camel!'
"O"
true
false
null
```

### 6.2.3 数组字面量

- **`[ indexValues ]`** 或 **`[ dataExpr for identRef in dataExpr (if dataExpr)? ]`**（列表推导）。

> **未实现。** 列表推导、元素前的展开记号 `...`（数组、结构体字面量与实参表中）语法接受，但含义尚未规定：使用它是编译期错误 `SemanticDiag::ConstructNotSupported`（定位在该字面量或 `...` 处）。列表推导请写成 `map` 与 `filter`。反引号的格式化字符串同样未实现，同一诊断。

```cml
[]
[(1, 2), (10, 20)]
[{ x: 5, y: 5 }, { x: 4, y: 5 }, { x: 3, y: 5 }]
[1.0, 2.0, 3.0]
[0.1, 0.2, 0.3]
['w', 'a', 's', 'd']
[new_head]
```

### 6.2.4 结构体字面量

- **`{ key: value, ... }`**，键为标识符。

```cml
{ x: 5, y: 5 }
{ x: now_head.x, y: now_head.y - 1 }
{ x: now_head.x - 1, y: now_head.y }
{ x: x, y: y }
```

### 6.2.5 元组字面量

- **`( dataList?, )`**。

```cml
(1, 2)
(10, 20)
(signal.0 * 2, signal.1 * 2)
(e.0 * e.1)
```

### 6.2.6 括号表达式

- **`( dataExpr )`**：用于分组或消除歧义。

```cml
(fib(n - 1) + fib(n - 2))
(len(arr) - 1)
```

### 6.2.7 Lambda（funcData）

- **`(参数) => 块或表达式`**；详见 6.11。

```cml
(p: Point) => bool
(x: int): string => { ... }
(y: int) => sync { ... }
(e: (float, float)) => e.0 * e.1
(acc: float, cur: float) => acc + cur
```

---

## 6.3 主表达式与访问

### 6.3.1 成员与元组下标

- **`primaryData ('.' (IDENTIFIER | INTEGER))*`**。
  - **IDENTIFIER**：字段名，如 **`p.x`**、**`now_head.y`**。
  - **INTEGER**：元组下标，如 **`signal.0`**、**`signal.1`**、**`e.0`**、**`e.1`**。超出元组长度是编译期错误。
  - 对数组不能用 `.`：`xs.1` 是编译期错误 `SemanticDiag::TypeNotIndexable`，数组元素用 `xs[1]` 读取。

```cml
p.x
p.y
now_head.x
now_head.y
signal.0
signal.1
e.0
e.1
shape(X)[0]
shape(X)[1]
```

### 6.3.2 下标

- **`expr [ dataExpr ]`** 或 **`[ start : end : step? ]`**（若实现支持切片）。
- 数组下标在运行期检查：越界（包括负数）是带位置的故障 `RuntimeDiag::IndexOutOfRange`，消息带下标与长度，各执行后端一致。

```cml
signals[0]
signals[1]
shape(y_pred)[0]
shape(X)[0]
shape(X)[1]
arr->slice<0, mid>
arr->slice<mid, len(arr)>
sorted->slice<0, m>
```

### 6.3.3 调用

- **`expr ( argumentList? )`**；参数可为位置参数与/或键值对。

```cml
println('Hello, Camel!')
now()
fib(idx)
format<idx>
render(snake)
move_snake(snake, dir)
hit_wall(now_head)
head(snake)
len(snake)
len(arr)
concat([new_head], snake->slice<0, len(snake) - 1>)
merge_sorted_arrays(lhs, rhs)
sort(arr)
range(0, HEIGHT)
range(0, WIDTH)
range(0, n)
set_terminal_raw_mode(true)
has_input()
get_char()
clear_input_buffer()
sleep(300)
exp(-x)
zip(a, b)
zeros([num_features + 1, 1])
ones([num_of_samples, 1])
concat(X, ones_col, 1)
transpose(X)
shape(X)[0]
```

### 6.3.4 泛型/显式参数

- **`expr < argumentList? >`**：将尖括号内作为类型或值参数传入。

```cml
format<idx>
format<idx, res, duration>
format<n, a, d>
format<res, duration>
format<len(arr), duration>
format<n, s>
format<m>
format<steps>
slice<0, mid>
slice<mid, len(arr)>
slice<0, len(snake) - 1>
slice<0, m>
join<''>
join<"">
my_filter<(p: Point): bool => { return p.x >= 0 && ... }>
foreach<(y: int) => sync { ... }>
foreach<(x: int): string => { ... }>
foreach<(p: Point) => sync { ... }>
map<(x: int): string => { ... }>
contains<p>
contains<ch>
append<p>
```

---

## 6.4 管道与组合

- **`a->b`**：将 **a** 的求值结果作为参数传给 **b**（方法或函数）；左结合。
- **`x ?-> f`**：`x` 出错时把错误原样传下去，否则调用 `f`，见 [6.6](#66-空值与错误)。
- **`..`**：组合。**`?..`** **未实现**：编译期错误 `SemanticDiag::ConstructNotSupported`（定位在运算符处）。

**示例**

```cml
'Start computing fibonacci({})...'->format<idx>->println
'Fibonacci({}) = {} (computed in {:.4f} seconds)'->format<idx, res, duration>->println
cols->map<(x: int): string => { ... }>->join<''>->println
arr->slice<0, mid>->mergesort
arr->slice<mid, len(arr)>->mergesort
range(0, n)->shuffle
range(0, HEIGHT)->foreach<(y: int) => sync { ... }>
zip(a, b)->map<(e: (float, float)) => e.0 * e.1>->
    reduce<(acc: float, cur: float) => acc + cur, 0.0>
signals->process->analyzer
points->my_filter<(p: Point): bool => { ... }>
snake->contains<p>
cleaned->contains<p>
['w', 'a', 's', 'd']->contains<ch>
```

---

## 6.5 一元与类型断言

- **一元**：**`!`**、**`-`**、**`~`** 后接 **linkExpr**。
- **类型**：**`linkExpr (as | is) typeExpr`**，表示类型转换或类型判断。**`is` 未实现**（没有运行期类型判断）：编译期错误 `SemanticDiag::ConstructNotSupported`（定位在 `is` 处）。后缀的非空断言 `x!` 同样未实现，同一诊断。

```cml
!condition
-x
exp(-x)
~mask
x as float
v is int
```

---

## 6.6 空值与错误

类型 `T?`（可能为空）与 `T!`（可能出错）见 [3.2.4](03_data_model.md)。这两种值只能用下面的运算符和 `match` 的固定模式（[6.9.2](#692-match)）拆开；它们在图上都是**一个分支**，条件是值上的一位标记，与 `if` 是同一种结构（设计原则 2.9）。

- **`a ?? b`**：`a` 的类型是 `T?`，`b` 的类型是 `T`，结果是 `T`。`a` 为空取 `b`，否则取 `a` 里的值。
- **`a !! b`**：`a` 的类型是 `T!`，`b` 的类型是 `T`，结果是 `T`。`a` 出错取 `b`，否则取 `a` 里的值。
- **`x ?-> f`**：`x` 的类型是 `T!`。`x` 出错时把错误原样传下去，否则调用 `f`。结果是 `U!`：`f` 返回 `U` 或 `U!` 都可以。可以连写：`x ?-> f ?-> g`。
- 右侧（`b`、对 `f` 的调用）是分支的一个臂，只在被选中时求值。
- 左侧不是所要求的形式（例如对普通的 `int` 用 `??`，对 `T?` 用 `!!` 或 `?->`），或右侧类型不是 `T`：编译期错误 `SemanticDiag::TaggedOperandMismatch`（`??`、`!!` 定位在运算符处，`?->` 定位在表达式开头）。

```cml
let n = parse_int(text) !! 0                   // 解析失败取 0
let v = lookup(key) ?? default_value           // 为空取默认值
let r = parse_int(text) ?-> check ?-> double   // 任何一步出错，r 就是那个错误
```

错误与故障是两回事：只有声明为返回 `T!` 的操作才产生错误值。除零、下标越界、递归过深是故障，带位置终止程序，`!!` 接不住它。

---

## 6.7 乘性、加性、关系、相等、逻辑

### 6.7.1 乘性

- **`*`**、**`/`**、**`^`**、**`@`**、**`%`**，左结合。

```cml
signal.0 * 2
signal.1 * 2
e.0 * e.1
2.0 / float(m) * dw
lr * dw
residuals ^ 2.0
X @ weights
X_with_bias @ weights
transpose(X) @ errors
len(arr) / 2
total_squared_error / float(num_of_samples)
1.0 / (1.0 + exp(-x))
```

### 6.7.2 加性

- **`+`**、**`-`**，左结合。

```cml
fib(n - 1) + fib(n - 2)
now() - start
dot(x, w) + b
acc + cur
acc + a
n - 1
a + d
num_of_features + 1
len(snake) - 1
w - lr * dw
pred - y
```

### 6.7.3 关系与相等

- 关系：**`<`**、**`>`**、**`<=`**、**`>=`**。用于四种数值类型与字符串；字符串按字节逐个比较，真前缀在前。
- 相等：**`===`**、**`!==`**、**`==`**、**`!=`**。

```cml
n <= 1
n == 0
p.x >= 0 && p.x < WIDTH
p.y >= 0 && p.y < HEIGHT
len(arr) <= 1e5
now_head.x
now_head.y - 1
```

### 6.7.4 逻辑与、或

- **`&&`**、**`||`**，短路。

```cml
p.x >= 0 && p.x < WIDTH && p.y >= 0 && p.y < HEIGHT
p.x < 0 || p.x >= WIDTH || p.y < 0 || p.y >= HEIGHT
```

---

## 6.8 赋值（表达式形式）

- **`x = e`**、**`x += e`** 等；赋值为表达式时，其值为所赋之值（依实现）。
- 复合赋值只用于下表列出的类型；其他组合是编译期错误 `SemanticDiag::NoMatchingFunction`（定位在该赋值处，消息写明运算符）。

| 运算符 | 类型 |
|---|---|
| `+=` | `i32`、`i64`、`f32`、`f64`、`string`（拼接） |
| `-=`、`*=`、`/=`、`^=` | `i32`、`i64`、`f32`、`f64` |
| `%=` | `i32`、`i64`（除数为零是故障 `RuntimeDiag::DivisionByZero`） |
| `&=`、`\|=` | `bool`（`a = a && b`、`a = a \|\| b`） |
| `@=` | 内建类型都没有 |

```cml
var c = 0
wait target += 1
wait dw = 2.0 / float(m) * dw
return w = w - lr * dw
```

---

## 6.9 控制表达式

### 6.9.1 if-then-else

- **`if logicalOrExpr then blockExpr (else blockExpr)?`**。
- **blockExpr** 可为**块**（**sync? '{' stmtList '}'**）或**单一 dataExpr**。

**表达式形式**

```cml
return if n <= 1 then n else fib(n - 1) + fib(n - 2)
```

```cml
let res = if len(arr) <= 1e5 then sorted(arr) else {
    let mid = len(arr) / 2
    let lhs = arr->slice<0, mid>->mergesort
    let rhs = arr->slice<mid, len(arr)>->mergesort
    return merge(lhs, rhs)
}
```

**分支臂里的 `return` / `throw`。** 分支臂是自己的子图：臂里的 `return`（或 `throw`）结束的是这条臂，
给出的是这个 `if`／`match` 的值，**不是离开函数**（设计原则 2.1、2.4）。所以当 `if`／`match` 只是一条
语句、它的值没有被使用时，臂里的 `return v` 什么都没做，函数继续执行后面的语句：

```cml
func safe_div(a: int, b: int): int! {
    if b == 0 then sync { throw error('DivByZero', '...') } else sync { return a / b }
    return 0            // 两条臂里的 throw / return 都没有效果：总是返回 0
}
```

编译器对这种写法给出警告 `SemanticDiag::ExitInUnusedBranch`（定位在臂里的 `return`／`throw`，次要位置
指向那个值未被使用的 `if`／`match`；可用 `-Wno-exit-in-unused-branch` 关闭）。程序的行为不变。正确的
写法是让分支成为函数体的最后一个表达式，或在它前面写 `return`：

```cml
func safe_div(a: int, b: int): int! {
    return if b == 0 then sync { throw error('DivByZero', '...') } else sync { return a / b }
}
```

判定条件：`if`／`match` 是一条表达式语句，且不是所在块的最后一条语句，或者所在块本身的值没有被使用
（例如它又是另一个未被使用的分支的臂）；值被绑定、作实参、作操作数、作 `return` 的操作数、作函数体的
最后一个表达式时都不报。`return null`（以及不带值的 `return`）不报：没有 `else` 的 `if`、或两臂的值
类型不同的 `if`，要靠它让臂没有值才能通过编译。臂里的 lambda 与嵌套函数有自己的函数体，不受影响。

**块形式**

```cml
if hit_wall(now_head) then sync {
    'Game over! You hit the wall after {} steps.'->format<steps>->println
    return null
} else sync {
    sleep(300)
    let d = if has_input() then sync {
        let ch = get_char()
        clear_input_buffer()
        return if ['w', 'a', 's', 'd']->contains<ch> then ch else dir
    } else dir
    game_loop(new_snake, d, steps + 1)
}
```

**单分支**

```cml
return if cleaned->contains<p> then 'O' else '.'
return if snake->contains<p> then "O" else "."
```

### 6.9.2 match

- **`match identRef '{' matchCase+ '}'`**。
- **matchCase**：**`case pattern ('|' pattern)* '=>' blockExpr`**。
- **pattern**：**identRef**、字面量、**`( dataList | identList )`**、**`{ pairedValues | identList }`**、**`_`**（通配符）。
- 对普通类型的值，已实现的模式只有字面量与 `_`。元组、结构体模式**未实现**，标识符模式只对 `T?`/`T!` 有定义（见下）：编译期错误 `SemanticDiag::ConstructNotSupported`。
- `case a | b => body`：任一模式匹配即进入该分支。按书写顺序取第一个匹配的分支，`_` 之后的分支不可达。
- **没有 `case _` 的 `match`**：是否必须写 `case _` 尚未裁定。目前：可以不写，编译期给出警告
  `SemanticDiag::MatchWithoutDefault`（可用 `-Wno-match-without-default` 关闭）；运行时遇到没有任何
  分支匹配的值是**故障** `RuntimeDiag::MatchNotExhaustive`，定位在这个 `match`，消息里带上那个值
  （此前是静默算错：取了一条不存在的臂，结果为 0 或崩溃）。四个执行后端一致。对 `bool` 写全了
  `case true` 与 `case false` 的 `match` 是完整的，不报警告。已匹配的情形行为不变。

**对 `T?` 与 `T!` 的固定模式。** 被匹配的值是 `T?` 或 `T!` 时，`match` 是对它的标记位的一个分支，恰好两个分支（`case v` 写在前写在后都可以；`_` 仍然要写在最后）：

- `T?`：`case null`，加上一个取值的分支：`case v`（把取出的值绑定为 `v`，类型 `T`）或 `case _`。
- `T!`：`case error e`（把错误绑定为 `e`），加上一个取值的分支：`case v` 或 `case _`。
- `e` 是固定的内建结构 `{ kind: string, message: string, location: string }`：种类名、消息、发生处的源码位置（`文件:行:列`）。
- 缺分支、多分支，或混入字面量分支：编译期错误 `SemanticDiag::TaggedMatchCases`。

```cml
func describe(x: int?): string {
    return match x {
        case null => 'none'
        case v => format<v>('some {}')
    }
}

func show(r: int!): string {
    return match r {
        case error e => format<e.kind, e.message>('{}: {}')
        case v => format<v>('ok {}')
    }
}
```

```cml
let new_head = match dir {
    case 'w' => { x: now_head.x, y: now_head.y - 1 }
    case 'a' => { x: now_head.x - 1, y: now_head.y }
    case 's' => { x: now_head.x, y: now_head.y + 1 }
    case 'd' => { x: now_head.x + 1, y: now_head.y }
    case _ => now_head
}
```

### 6.9.3 try（不属于语言）

`try`/`catch`/`finally` 不进入 Camel：它在图上的形状取决于块里写了什么，没有稳定的映射（设计原则 2.9）。文法仍然认得这个写法，只为了给出一条指引：编译期错误 `SemanticDiag::TryCatchNotInLanguage`（定位在 `try` 处）。

可能失败的操作返回 `T!`，用 `a !! b` 给出替代值，用 `x ?-> f` 把错误传下去，用 `match` 的 `case error e` 查看错误（见 [6.6](#66-空值与错误)）。

```cml
let text = try_read_text(path) !! ''
```

---

## 6.10 wait 表达式

- **`wait assignExpr`**：作用于其后的这一个表达式。`wait` 前后的操作如果用到了该表达式的值，或用到了被它
  修改的 `var`，就与它局部有序；排序依据是对 `var` 的使用，不是书写位置（见第 4 章 4.4.5）。表达式不涉及
  任何 `var` 时 `wait` 没有意义；在同步块内写不写 `wait` 都一样。
- **目前实现的范围**：`wait` 写在调用、赋值、运算符表达式（以及访问、`with` 绑定）之前。文法还接受
  `wait { ... }`（块语句）与写在名字、字面量、一元运算、lambda 之前的 `wait`，这些位置上它的含义
  尚未确定，此前标记被静默丢弃；现在是编译期错误 `SemanticDiag::ConstructNotSupported`（定位在该
  表达式或块处）。要让一个块里的语句按顺序执行，用 `sync { ... }`。

```cml
wait update(c)
wait target += 1
wait dw = 2.0 / float(m) * dw
```

---

## 6.11 Lambda 与函数类型

### 6.11.1 Lambda（funcData）

- **`modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr`**。
- **blockExpr**：**stmtBlock** 或 **dataExpr**。

**无返回类型注解、单表达式**

```cml
(p: Point) => p.x >= 0 && p.x < WIDTH && p.y >= 0 && p.y < HEIGHT
```

**带返回类型、块体**

```cml
(p: Point): bool => {
    return p.x >= 0 && p.x < WIDTH && p.y >= 0 && p.y < HEIGHT
}
```

**sync 块体**

```cml
(y: int) => sync {
    let cols = range(0, WIDTH)
    cols->map<(x: int): string => { ... }>->join<''>->println
}
```

**用于 foreach / map**

```cml
points->foreach<(p: Point) => sync {
    if predicate(p) then sync {
        result->append<p>
        return null
    }
}>
```

```cml
cols->map<(x: int): string => {
    let p = { x: x, y: y }
    return if cleaned->contains<p> then 'O' else '.'
}>
```

### 6.11.2 函数类型（用于注解与 with）

- **`modifiers? angledParams? parentParams '=>' typeExpr`**。

```cml
(p: Point) => bool
(acc: float, cur: float) => acc + cur
```

---

## 6.12 运算符优先级（从低到高）

| 优先级 | 运算符 / 类别 |
|--------|----------------|
| 1（最低） | 赋值 **=** **+=** **-=** **\*=** **/=** **%=** **^=** **@=** **&=** **\|=** |
| 2 | 逻辑或 **\|\|** |
| 3 | 逻辑与 **&&** |
| 4 | 相等 **===** **!==** **==** **!=** |
| 5 | 关系 **<** **>** **<=** **>=** |
| 6 | 加性 **+** **-** |
| 7 | 乘性 **\*** **/** **^** **@** **%** |
| 8 | 可空 **??** **!!** |
| 9 | 一元 **!** **-** **~**；类型 **as** **is** |
| 10 | 管道 **->** **?->**；范围 **..** **?..** |
| 11（最高） | 后缀：访问 **.**、下标 **[]**、调用 **()**、泛型 **<>**、**!** |

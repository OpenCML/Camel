# 6. 表达式

本章详细描述 Camel 的**表达式**：求值顺序、原子、访问/下标/调用/泛型、管道、一元与类型断言、可空、各类运算、控制表达式（if/match/try）、wait、Lambda，以及**运算符优先级**。每类都配有示例。

---

## 6.1 求值顺序

- 表达式按**运算符优先级与结合性**求值；子表达式先于父表达式（除短路与特殊语义外）。
- **短路求值**：**`&&`**、**`||`** 在确定结果后不再求值右侧；**`??`**/ **`!!`** 依实现。
- **管道** **`a->b`**：先求值 **a**，再以该值为参数调用 **b**（或传入 b 的某一参数位）。
- **泛型/显式参数** **`expr<args>`**：先确定被调用对象与类型/值参数，再按调用语义求值。

---

## 6.2 原子（primaryData）

### 6.2.1 标识符

- 变量或函数名。

```cml
n
fib
println
now
format
WIDTH
now_head
predicate
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
  - **INTEGER**：元组下标，如 **`signal.0`**、**`signal.1`**、**`e.0`**、**`e.1`**。

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
- **`?->`**：可选管道（若 **a** 为 null 则短路等，依实现）。
- **`..`** / **`?..`**：范围或组合（若实现支持）。

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
- **类型**：**`linkExpr (as | is) typeExpr`**，表示类型转换或类型判断。

```cml
!condition
-x
exp(-x)
~mask
x as float
v is int
```

---

## 6.6 可空

- **`a ?? b`**：若 **a** 为 null 则取 **b**。
- **`a !! b`**：非空断言或错误则取右（依实现）。

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

- 关系：**`<`**、**`>`**、**`<=`**、**`>=`**。
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
let res = if len(arr) <= 1e5 then sort(arr) else {
    let mid = len(arr) / 2
    let lhs = arr->slice<0, mid>->mergesort
    let rhs = arr->slice<mid, len(arr)>->mergesort
    return merge(lhs, rhs)
}
```

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

```cml
let new_head = match dir {
    case 'w' => { x: now_head.x, y: now_head.y - 1 }
    case 'a' => { x: now_head.x - 1, y: now_head.y }
    case 's' => { x: now_head.x, y: now_head.y + 1 }
    case 'd' => { x: now_head.x + 1, y: now_head.y }
    case _ => now_head
}
```

### 6.9.3 try

- **`try stmtBlock catchClause+ (finally stmtBlock)?`**。
- **catchClause**：**`catch identDef ':' typeExpr stmtBlock`**。

```cml
try {
    risky_op()
} catch e: IOError {
    println('Failed')
} finally {
    cleanup()
}
```

---

## 6.10 wait 表达式

- **`wait assignExpr`**：在 sync 块内，等待该表达式求值完成再继续。

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

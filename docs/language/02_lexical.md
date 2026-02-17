# 2. 词法分析

本章描述 Camel 源码的**词法结构**：行与分隔、注释、标识符与关键字、字面量、运算符与分隔符。对应文法见 `antlr/OpenCMLLex.g4`。

---

## 2.1 行与语句分隔

### 2.1.1 语句分隔符

- 分号 **`;`** 可作为语句之间的分隔符。
- 同一逻辑行内多条语句需用 `;` 分隔。

**示例**

```cml
let a = 1; let b = 2
return 0
```

### 2.1.2 空白与换行

- 空格、制表符、换行、换页等 **BLANK** 由词法分析器**跳过**（不参与语法分析）。
- 字符串与多行字符串、注释内部的换行按字面保留。

因此换行不强制作为语句结束；语句边界由 `;` 或后续语法规则确定。

---

## 2.2 注释

### 2.2.1 行注释

- **`//`** 至行末为行注释，内容放入 channel，不参与语法。

**示例**

```cml
// variables and sync for explicit
sync func update(var target: int) {
    wait target += 1
}

let n = 100000  // number of terms
let a = 1       // first term
```

（来自 `test/run/linear/var.cml`、`test/run/linear/acc.cml`）

### 2.2.2 块注释

- **`/*`** … **`*/`** 为块注释，可跨行，同样放入 channel。

**示例**

```cml
/**
 * tail-recursive accumulate function
 * This test checks that tail-call optimization is working correctly.
 */
module arithmetic_sum
```

（来自 `test/run/linear/acc.cml`）

### 2.2.3 命令/预处理行

- **`#`** 至行末由词法器识别并放入 channel(2)，不参与语法；用途可由实现或工具约定。

---

## 2.3 标识符与关键字

### 2.3.1 标识符（identDef）

- 以**字母**或**下划线**开头，后接零个或多个**字母、数字、下划线**。
- 规则：`[a-zA-Z_][a-zA-Z_0-9]*`。

**合法标识符示例**

```cml
n
fib
main
WIDTH
HEIGHT
now_head
merge_sorted_arrays
```

### 2.3.2 引用名（identRef）

- 用于命名空间限定时可含 **`::`**，如 `mod::name`；否则与标识符相同。

### 2.3.3 关键字（保留字）

以下为关键字，不可作为普通标识符使用：

| 类别 | 关键字 |
|------|--------|
| 声明与模块 | `as`, `let`, `var`, `use`, `from`, `func`, `type`, `enum`, `with`, `module`, `import`, `export` |
| 控制与异常 | `if`, `then`, `else`, `match`, `case`, `for`, `in`, `of`, `try`, `catch`, `finally`, `throw`, `raise`, `return` |
| 类型与值 | `wait`, `const`, `keyof`, `typeas`, `typeof`, `namespace` |
| 修饰符 | `sync`, `macro`, `inner`, `outer`, `atomic`, `shared` |
| 字面量 | `null`, `true`, `false` |

### 2.3.4 多字符记号

- **`...`**（省略号）在词法中优先于单个 **`.`**。
- **`..`** 为双点（可用于范围等）。

---

## 2.4 字面量

### 2.4.1 整数（INTEGER）

- **十进制**：`0`、`30`、`100000`。
- **八进制**：`0` 后接 `[0-7]+`，如 `077`。
- **十六进制**：`0x` 后接十六进制数字，如 `0xff`。
- **二进制**：`0b` 后接 `0`/`1`，如 `0b1010`。
- 可带前导 **`+`** / **`-`**；可接**指数**部分 `e`/`E`（如 `1e6` 表示 1000000）。

**示例**

```cml
let idx = 30
let n = 100000
let s = 1
sleep(300)
```

### 2.4.2 实数（REAL）

- 带小数点的数字，可选指数部分。

**示例**

```cml
1.0
1e-2
1e8
1e5
{:.4f}
{:.6f}
{:.2f}
```

（格式占位如 `{:.4f}` 在字符串中出现，由格式化函数解释。）

### 2.4.3 字符串（STRING）

- **双引号**：`"…"`。
- **单引号**：`'…'`。
- 内可转义 `\"`、`\'`、`\\` 等。

**示例**

```cml
'Hello, Camel!'
"O"
"."
'Start computing fibonacci({})...'
'Fibonacci({}) = {} (computed in {:.4f} seconds)'
'Game over! You hit the wall after {} steps.'
"\033[2J\033[H"
```

### 2.4.4 多行字符串（MULTI_STR）

- **`'''…'''`** 或 **`"""…"""`**，中间可含换行。

### 2.4.5 F 字符串（FSTRING）

- 反引号 **`` `…` ``**，用于格式化等；具体占位语义由库约定。

### 2.4.6 布尔与空值

- **布尔**：`true`、`false`。
- **空值**：`null`。

**示例**

```cml
set_terminal_raw_mode(true)
return null
```

---

## 2.5 运算符与分隔符

### 2.5.1 比较

| 符号 | 含义 |
|------|------|
| `===` | 严格相等 |
| `!==` | 严格不等 |
| `==` | 相等 |
| `!=` | 不等 |
| `<`, `>`, `<=`, `>=` | 小于、大于、小于等于、大于等于 |

**示例**

```cml
n <= 1
n == 0
p.x >= 0 && p.x < WIDTH
len(arr) <= 1e5
```

### 2.5.2 逻辑

- **`&&`**：逻辑与（短路）。
- **`||`**：逻辑或（短路）。

**示例**

```cml
p.x >= 0 && p.x < WIDTH && p.y >= 0 && p.y < HEIGHT
p.x < 0 || p.x >= WIDTH || p.y < 0 || p.y >= HEIGHT
```

### 2.5.3 算术与位

| 符号 | 含义（依上下文/类型） |
|------|------------------------|
| `+`, `-`, `*`, `/`, `%` | 加、减、乘、除、取模 |
| `^` | 幂或按位异或 |
| `@` | 矩阵乘等（如张量） |
| `~` | 按位取反 |
| `!` | 逻辑非 |

**示例**

```cml
fib(n - 1) + fib(n - 2)
now() - start
signal.0 * 2
residuals ^ 2.0
X @ weights
transpose(X) @ errors
```

### 2.5.4 赋值

- **`=`**, **`+=`**, **`-=`**, **`*=`**, **`/=`**, **`%=`**, **`^=`**, **`@=`**, **`&=`**, **`|=`**。

**示例**

```cml
var c = 0
wait target += 1
wait dw = 2.0 / float(m) * dw
return w = w - lr * dw
```

### 2.5.5 可空

- **`??`**：空则取右侧值。
- **`!!`**：非空断言或错误则取右（依实现）。

### 2.5.6 管道与范围

- **`->`**：管道（将左侧作为参数传给右侧）。
- **`?->`**：可选管道。
- **`..`**, **`?..`**：范围等（若实现支持）。
- **`.`**：成员访问或元组下标（如 `p.x`、`signal.0`）。

**示例**

```cml
'Start computing fibonacci({})...'->format<idx>->println
cols->map<(x: int): string => { ... }>->join<''>->println
arr->slice<0, mid>->mergesort
range(0, HEIGHT)->foreach<(y: int) => sync { ... }>
```

### 2.5.7 括号与结构

- **`( )`**：参数列表、元组、分组。
- **`[ ]`**：数组、下标、切片。
- **`{ }`**：结构体、块、模式。
- **`< >`**：泛型/显式参数，如 `format<idx>`、`slice<0, len(arr)>`。

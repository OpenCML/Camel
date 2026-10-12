# 7. 语句

本章描述 Camel 的**简单语句**与**复合语句**：表达式语句、return / throw（raise 尚未实现）、let/var、use、using namespace、块、if、match、try（不属于语言）、函数定义、类型定义，并配有完整程序片段示例。

---

## 7.1 简单语句

### 7.1.1 表达式语句

- 一条 **dataExpr** 可作为语句；其值可被丢弃。

**示例**

```cml
println('Hello, Camel!')
println(c)
println('')
render(snake)
game_loop(initial_snake, 'w', 0)
'Start computing fibonacci({})...'->format<idx>->println
'Fibonacci({}) = {} (computed in {:.4f} seconds)'->format<idx, res, duration>->println
'Analyzer: {}'->format<signals>->println
'Game over! You hit the wall after {} steps.'->format<steps>->println
'MergeSorted {} elements in {:.2f} seconds.'->format<len(arr), duration>->println
'output: {:.6f}'->format<output>->println
print('\033[2J\033[H')
set_terminal_raw_mode(true)
seed(s)
```

### 7.1.2 return / raise / throw 语句

- **retStmt**：**`(return | raise | throw) dataList?`**。
- **`throw e`** 返回一个错误值，与 `return` 同构：它是所在函数（或所在分支臂）的值，不是跳转。
  写在一个值未被使用的 `if`／`match` 的臂里时，`return v` 与 `throw e` 没有效果，编译器给出警告
  `SemanticDiag::ExitInUnusedBranch`（见 [6.9.1](06_expressions.md)）。
  - 所在函数必须把返回类型声明为 `T!`，否则是编译期错误 `SemanticDiag::ThrowOutsideFallible`。
  - `e` 必须是错误值：用内建的 `error(kind, message)` 构造（它记下这次调用的源码位置），或是
    `case error e` 绑定的那个；否则是 `SemanticDiag::ThrowNotAnError`。
  - 在这样的函数里，`return v` 把 `v` 作为成功的值返回。
- **`raise` 未实现**（去留未定）：使用它是编译期错误 `SemanticDiag::ConstructNotSupported`。
- **return** 可带多个表达式，或省略表示无值返回；可返回 **null**。

**示例**

```cml
return 0
return null
return n
return result
return res
return if n <= 1 then n else fib(n - 1) + fib(n - 2)
return [res1, res2]
return merge_sorted_arrays(lhs, rhs)
return concat([new_head], snake->slice<0, len(snake) - 1>)
return (signal.0 * 2, signal.1 * 2)
return sigmoid(z)
return dot(x, w) + b
return total_squared_error / float(num_of_samples)
```

```cml
func safe_div(a: int, b: int): int! {
    if b == 0 then {
        throw error('DivByZero', 'the divisor is zero')
    } else {
        return a / b
    }
}
```

### 7.1.3 数据声明（let / var）

- **dataDecl**：**`(let | var) carrier (':' typeList)? '=' dataList`**。
- **carrier**：单个标识符、或括号/花括号/方括号内的标识符列表（解构）。
- **dataList**：一个或多个表达式，与 carrier 结构对应。

**let：不可变绑定**

```cml
let idx = 30
let WIDTH = 20
let HEIGHT = 10
let start = now()
let res = fib(idx)
let duration = now() - start
let n = 100000
let a = 1
let d = 3
let signals: Signal[] = [(1, 2), (10, 20)]
let initial_snake = [{ x: 5, y: 5 }, { x: 4, y: 5 }, { x: 3, y: 5 }]
let input = [1.0, 2.0, 3.0]
let weights = [0.1, 0.2, 0.3]
let bias = 0.5
let cleaned = valid_points(snake)
let rows = range(0, HEIGHT)
let cols = range(0, WIDTH)
let now_head = head(snake)
let new_head = match dir { ... }
let new_snake = move_snake(snake, dir)
let mid = len(arr) / 2
let lhs = arr->slice<0, mid>->mergesort
let rhs = arr->slice<mid, len(arr)>->mergesort
let m = shape(X)[0]
let n = shape(X)[1]
```

**var：可变变量**

```cml
var c = 0
var result: Point[] = []
```

### 7.1.4 use 语句

- **useDecl**：**`use (identDef '=')? identRef`**；将某符号（或模块）以可选别名引入当前作用域（具体语义依实现）。

### 7.1.5 using namespace 语句

- **usingNamespaceDecl**：**`using namespace identRef`**；将已导入命名空间中的导出符号注入当前作用域。

```cml
import tensor
using namespace tensor
```

---

## 7.2 复合语句

### 7.2.1 块

- **stmtBlock**：**`sync? '{' stmtList? '}'`**。
- **blockStmt** 可为 **`wait? stmtBlock`**，即块前可带 **wait**。
- 带 `sync` 的块是**同步块**：块内表达式按书写顺序执行。不带 `sync` 的块按依赖驱动求值：结果未被使用的效应调用不执行（见第 4 章 4.4.1、4.4.2）。

**普通块**

```cml
{
    let a = 1
    let b = 2
    return a + b
}
```

**同步块**

```cml
sync {
    println('Hello')
    return 0
}
```

**函数体块（sync）**

```cml
func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

### 7.2.2 if 语句

- **`if ... then blockExpr (else blockExpr)?`**；then/else 可为块或单表达式。
- 见 [6.9 控制表达式](06_expressions.md#69-控制表达式)。

**示例**

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

### 7.2.3 match 语句

- **`match identRef '{' matchCase+ '}'`**；见 [6.9.2 match](06_expressions.md#692-match)。

**示例**

```cml
match dir {
    case 'w' => { x: now_head.x, y: now_head.y - 1 }
    case 'a' => { x: now_head.x - 1, y: now_head.y }
    case 's' => { x: now_head.x, y: now_head.y + 1 }
    case 'd' => { x: now_head.x + 1, y: now_head.y }
    case _ => now_head
}
```

### 7.2.4 try 语句（不属于语言）

`try`/`catch` 不进入 Camel；写了得到编译期错误 `SemanticDiag::TryCatchNotInLanguage`，它指向 `!!`、`?->` 与故障隔离。见 [6.9.3](06_expressions.md)。

### 7.2.5 函数定义

- **funcDecl**：**`(with angledParams)? export? implMark? modifiers? func identRef parentParams (':' typeExpr)? stmtBlock`**。
- **implMark**：**inner** | **outer**。**未实现**：能被解析并记录，编译器不读取，没有语义。
- **modifiers**：**atomic**、**shared**、**sync**、**macro** 等组合。其中 **atomic**、**shared** **未实现**：能被
  解析并记录在函数类型上，没有任何语义；有语义的只有 **sync**（函数修饰符）和 **macro**。
- 写在 `func` 之前的 **`sync`** 是函数修饰符：声明函数有副作用、需要被同步（带 `var` 形参的函数必须如此声明）；它**不会**使函数体成为同步块。要让函数体按书写顺序执行，把函数体写成同步块：`func f(): T sync { ... }`，或两者并用 `sync func f(): T sync { ... }`（见第 4 章 4.4.2、4.4.3）。
- `var` 形参是引用；把 `let` 对象传给 `var` 形参是编译期错误（见第 4 章 4.4.4）。
- **parentParams**：**`'(' pairedParams? ','? ')'`**；**keyParamPair**：**`var? identDef ':' typeExpr ('=' dataExpr)?`**。
- **重载**：同一个块里同名而形参类型不同的函数是重载，各有自己的子图；调用按书写顺序取第一个形参接受实参的。形参类型完全相同的第二个同名函数是编译期错误 `SemanticDiag::Redeclaration`（返回类型与形参名不参与区分）。泛型函数不能与其他函数同名。重载里可以有带装饰器的函数：它参与选择时用的是装饰器返回的那个函数的形参，被选中后调用的也是那个函数；对带装饰器的函数的调用按那个函数的形参检查实参（不匹配是 `SemanticDiag::ArgumentsMismatch`）。既有带装饰器的函数又有其他重载的名字目前不能导出（`SemanticDiag::ConstructNotSupported`）。
- **函数作为值**：只代表一个函数的名字可以当值用，凡是能写值的位置都可以——作实参、`with` 实参、`let g = f`、`var v = f`、`return f`、数组／元组／结构体字面量的元素（`[inc, dec]`）、`if` 与 `match` 分支臂的值（`if c then inc else dec`）、被 lambda 捕获。代表运算符或一组重载的名字没有单一的函数值：在上述位置使用它是编译期错误 `SemanticDiag::FunctionNameAsValue`，可改写成参数类型写明的 lambda。泛型函数的名字不能当值用（`SemanticDiag::GenericFunctionAsValue`）。

**无参、带返回类型与 sync**

```cml
func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

**单参数、返回值**

```cml
func fib(n: int): int {
    return if n <= 1 then n else fib(n - 1) + fib(n - 2)
}
```

**多参数、返回数组**

```cml
func move_snake(snake: Point[], dir: string): Point[] {
    let now_head = head(snake)
    let new_head = match dir { ... }
    return concat([new_head], snake->slice<0, len(snake) - 1>)
}
```

**with 参数、sync、可变引用参数**

```cml
with <predicate: (p: Point) => bool>
func my_filter(points: Point[]): Point[] sync {
    var result: Point[] = []
    points->foreach<(p: Point) => sync {
        if predicate(p) then sync {
            result->append<p>
            return null
        }
    }>
    return result
}
```

**with 多个参数、sync、var 参数**

```cml
with <X: Tensor, y: Tensor, lr: float, its: int>
sync func train(var weights: Tensor): void {
    let m = shape(X)[0]
    let n = shape(X)[1]
    let ones_col = ones([m, 1])
    let X_bias = concat(X, ones_col, 1)
    // ...
}
```

**泛型函数**

在形参类型里第一次出现类型变量的位置写 **`typeas T`**，把那个位置上的类型绑定为 `T`；之后（其余形参、
返回类型、函数体）直接写 `T`。

```cml
func first(xs: (typeas T)[]): T {
    return xs[0]
}

func pick(c: bool, a: typeas T, b: T): T {
    return if c then a else b
}

func my_map(xs: (typeas T)[], f: (x: T) => typeas U): U[] {
    return xs->map<f>
}

first([1, 2, 3])        // T = int
first(['a', 'b'])       // T = string
pick(true, 1, 2.5)      // 错误：b 的形参是 T，这里 T = int，实参是 float
```

- 泛型函数是一张**子图模板**。每用一组具体类型调用，就实例化出一张类型全部具体的子图（同一组类型只
  实例化一次，用 `std::rgir` 可以看到名为 `first<i64>`、`first<string>` 的子图）；图上只有具体类型，
  运行时不需要类型信息。
- **绑定只发生在 `typeas` 的位置**：变量取实参类型在该位置上的类型。其余位置是普通的形参，按代入后的
  类型检查，规则与普通函数相同，不做隐式转换。
- `typeas` 可以出现在数组元素、元组元素、结构体字段、函数类型的形参与返回值这些位置上，也可以作为类型
  函数的实参（`Pair<typeas T>`）。不能出现在联合类型里，也不能作为模块类型的实参。
- `typeas` 只能写在**函数声明的形参**（包括 `with` 形参）里。写在返回类型、lambda、`let` 里是错误；同一个
  变量写两次 `typeas`、或变量名与已有类型同名也是错误。变量必须先绑定后使用（按书写顺序，`with` 形参
  在前）。
- **函数体在实例化时检查。** 函数体里的类型错误在实例化时报告，诊断给出错误在函数体里的位置，并指出
  是哪个实例、由哪一处调用实例化的。从未被调用的泛型函数不会被实例化，它的函数体也就不会被检查。
- 递归的泛型函数可以正常工作：用同一组类型递归调用，调用的就是正在构造的那个实例（与普通递归函数
  一样需要写出返回类型）。每次递归都换成更大的类型（如 `f([x])`）的函数无法终止实例化，嵌套到 8 层时
  报错。
- 函数体里可以用 `T` 写 lambda 的形参类型、`let z: T = 0` 与 `e as T`；字面量到 `T` 的转换在实例化时
  按具体类型进行。
- **导出**：泛型函数可以 `export`，别的模块 `import` 之后照常调用（也可以写限定名 `lib::f(x)`）。导出
  的是模板；实例由调用它的模块构造，每个导入它的模块各有自己的实例。函数体里的名字在声明它的模块里
  解析，所以可以使用那个模块里没有导出的函数、别的泛型函数和它自己的导入。有一条限制：被别的模块
  实例化的函数体不能使用声明模块顶层的值（`let`/`var`），需要的常量写成函数。（"实例建在哪个模块"
  尚待语言作者裁定。）
- **目前的限制**：泛型函数只能被直接调用，不能当作值（赋给名字、作为实参传给 `map` 或别的函数）、不能
  加装饰器；需要时用一个写明形参类型的 lambda 包一层：`xs->map<(x: int) => id(x)>`。同一个
  名字不能既是泛型函数又是普通函数。调用时不能显式给出类型实参。

```cml
with <f: (acc: typeas U, x: typeas T) => U, init: U>
func my_fold(xs: T[]): U {
    return xs->reduce<f, init>
}

['a', 'bb']->my_fold<(acc: int, s: string) => acc + len(s), 0>    // 3
```

**无返回类型（void）**

```cml
func render(snake: Point[]): void sync {
    print('\033[2J\033[H')
    let rows = range(0, HEIGHT)
    rows->foreach<(y: int) => sync {
        let cols = range(0, WIDTH)
        cols->map<(x: int): string => {
            let p = { x: x, y: y }
            return if snake->contains<p> then "O" else "."
        }>->join<"">->println
    }>
    println("")
}
```

### 7.2.6 类型定义

- **typeDecl**：**`implMark? type identRef ('<' identDef (',' identDef)* ','? '>')? '=' (typeExpr | STRING)`**。

**结构体类型别名**

```cml
type Point = {
    x: int,
    y: int
}
```

**元组类型别名**

```cml
type Signal = (int, int)
```

**带参数的类型声明（类型函数）**

名字后面用尖括号写出参数，右侧是一个普通的类型表达式，其中可以使用这些参数。在类型位置上用尖括号
"调用"它，得到右侧把参数替换成实参之后的类型。它在建图时求值，图上不留节点。

```cml
type Pair<T> = (T, T)
type Entry<K, V> = { key: K, value: V }
type Mapper<A, B> = (x: A) => B
type PairList<T> = Pair<T>[]              // 右侧可以使用已声明的类型函数

func sum_pair(p: Pair<int>): int { return p.0 + p.1 }

// 与 typeas 组合：实参位置上绑定类型变量
func swap(p: Pair<typeas T>): Pair<T> {
    return (p.1, p.0)
}
func key_of(e: Entry<typeas K, typeas V>): K {
    return e.key
}
```

- 实参只能是类型（不接受 `const ...`），个数必须与参数一致；不带实参使用是错误。这些都在类型表达式
  处报告。
- 右侧只能引用已经声明的类型，所以类型函数不能引用自身。
- 可以 `export`，在别的模块里以 `模块名::Pair<int>` 使用。
- 模块类型（如 `Tensor<...>`）的尖括号不受影响，仍由该类型自己解释。

---

## 7.3 完整程序片段示例

**最小程序（hello.cml）**

```cml
module main

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

**斐波那契（fib.cml 精简）**

```cml
module fib

import { now } from time

func fib(n: int): int {
    return if n <= 1 then n else fib(n - 1) + fib(n - 2)
}

func main(): int sync {
    let idx = 30
    'Start computing fibonacci({})...'->format<idx>->println
    let start = now()
    let res = fib(idx)
    let duration = now() - start
    'Fibonacci({}) = {} (computed in {:.4f} seconds)'->format<idx, res, duration>->println
    return 0
}
```

**元组与管道（index.cml 精简）**

```cml
type Signal = (int, int)

func process(signals: Signal[]) sync {
    let res1 = signals[0]->comp1
    let res2 = signals[1]->comp2
    return [res1, res2]
}

func comp1(signal: Signal): Signal {
    return (signal.0 * 2, signal.1 * 2)
}

func main() {
    let signals: Signal[] = [(1, 2), (10, 20)]
    signals->process->analyzer
}
```

**归并排序主逻辑（msort.cml 片段）**

```cml
func mergesort(arr: int[]): int[] sync {
    let start = now()
    let res = if len(arr) <= 1e5 then sorted(arr) else {
        let mid = len(arr) / 2
        let lhs = arr->slice<0, mid>->mergesort
        let rhs = arr->slice<mid, len(arr)>->mergesort
        return merge(lhs, rhs)
    }
    let duration = now() - start
    println('MergeSorted {} elements in {:.2f} seconds.'->format<len(arr), duration>)
    return res
}
```

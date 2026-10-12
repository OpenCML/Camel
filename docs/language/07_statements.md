# 7. 语句与声明

[English](en/07_statements.md) | 中文

块由语句组成，一行一条；一条语句在哪里结束见 [2.1](02_lexical.md#21-行空白与分隔)。语句是：表达式、`let`/`var` 声明、`return`/`throw`、嵌套的块，以及函数与类型的声明。

---

## 7.1 表达式语句

任何表达式都可以作语句。在同步块里它按顺序执行；在同步块之外，值没有被使用的表达式不执行（[4.2](04_execution_model.md#42-按依赖求值)）。

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

- 必须有初值。类型可以省略，由初值推出；写出类型时初值必须是这个类型（字面量按所写的类型解释：`let a: i32 = 3`）。
- 作用域是所在的块（或模块顶层）。同一作用域里不能重复声明同一个名字；内层的块、lambda 可以遮蔽外层的名字。
- **解构**：元组可以解构，也可以一次声明多个名字：

  ```cml
  let (q, r) = (7 / 2, 7 % 2)
  let a, b = 1, 'x'
  ```

  结构体解构 `let { x, y } = p` 与数组解构 `let [a, b] = xs` 保留，尚未实现（`SemanticDiag::FeatureNotSupported`）；用 `p.x`、`xs[0]`。
- 模块顶层的 `let` 是全局常量。顶层的 `var` 可以被函数读写，写它的函数应声明为 `sync func`；这一用法没有完全实现，尽量避免。

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
| `macro` | 可以被 `std::macro` 遍提前求值（[4.5](04_execution_model.md#45-macro-函数)） |
| `atomic`、`shared`、`inner`、`outer` | 保留：能被解析，没有任何语义 |

## 7.5 泛型函数

在形参类型里第一次出现类型变量的位置写 `typeas T`，把那个位置上的类型绑定为 `T`；之后（其余形参、返回类型、函数体）直接写 `T`。

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
- **绑定只发生在 `typeas` 的位置**：类型变量取实参在该位置上的类型。其余位置是普通的形参，按代入后的类型检查，不做隐式转换：`pick(true, 1, 2.5)` 是错误（`T` 已绑定为 `int`）。
- `typeas` 可以出现在数组元素、元组元素、结构体字段、函数类型的形参与返回值、`T?`/`T!` 的内层、`Map`/`Set` 与类型函数的实参这些位置上。不能出现在联合类型里，也不能作为模块类型的实参。
- `typeas` 只能写在函数声明的形参（包括 `with` 形参）里。类型变量必须先绑定后使用（按书写顺序，`with` 形参在前）；同一个变量绑定两次、或与已有类型同名是错误。
- **函数体在实例化时检查。** 函数体里的类型错误在实例化时报告，诊断给出错误在函数体里的位置，并指出是哪个实例、由哪一处调用实例化的。从未被调用的泛型函数不会被检查。
- 递归的泛型函数可以用（要写出返回类型）。每次递归都换成更大类型的函数无法终止实例化，嵌套到 8 层时报错。
- 函数体里可以用 `T`：lambda 的形参类型、`let z: T = 0`、`e as T`。
- **导出**：泛型函数可以 `export`，别的模块 `import` 之后照常调用（也可以写限定名 `lib::f(x)`）。导出的是模板；实例由调用它的模块构造，每个导入它的模块各有自己的实例。函数体里的名字在声明它的模块里解析，所以可以使用那个模块里没有导出的函数、别的泛型函数和它自己的导入。有一条限制：被别的模块实例化的函数体不能使用声明模块顶层的值（`let`/`var`），需要的常量写成函数。“实例建在哪个模块”是当前实现的行为，尚待确定。

`with` 形参里同样可以绑定：

```cml
with <f: (acc: typeas U, x: typeas T) => U, init: U>
func my_fold(xs: T[]): U {
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

**目前的限制**：泛型函数只能被直接调用，不能当作值（需要时用写明形参类型的 lambda 包一层：`xs->map<(x: int) => id(x)>`）、不能加标注；调用时不能显式给出类型实参（是否允许尚未决定）。

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

func swap(p: Pair<typeas T>): Pair<T> {       // 与 typeas 组合
    return (p.1, p.0)
}

func key_of(e: Entry<typeas K, typeas V>): K {
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

类型表达式里保留、尚未实现的部分：类型运算 `&`、`\`、`+`、`-`、`^`，`typeof`、`keyof`、`名字 of T`、`T ? U`（`SemanticDiag::FeatureNotSupported` 或语法错误），它们的含义尚未决定。联合类型 `A | B` 能写出来，但成员类型的值并不能进入它（`int` 不能传给 `int | string` 的形参），目前没有实际用途。`enum` 是保留字，没有对应的语法。

## 7.8 块

```
sync? { 语句 ... }
```

块是表达式：它的值由块里的 `return` 给出，没有 `return` 时没有值。块引入一个作用域。带 `sync` 的块按书写顺序执行其中的语句（[4.3.1](04_execution_model.md#431-同步块块修饰符)）；不带 `sync` 的块按依赖求值。

块用作函数体、分支臂、lambda 体，也可以单独作语句。

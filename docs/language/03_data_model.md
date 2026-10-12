# 3. 数据模型

本章描述 Camel 的**值**、**类型**与**绑定**：对象与类型的概念、类型层次、`let`/`var` 语义、以及运算符与特殊方法（算子）的对应关系。

---

## 3.1 对象、值与类型

- 程序操作的是**值**；每个值具有**类型**。
- 类型在**编译期**用于类型检查，在**运行时**有对应表示（见 `core/type/`、`core/data/`）。
- **不可变绑定**：`let` 引入的名字绑定到初值，该绑定在其作用域内**不可重新赋值**。
- **可变变量**：`var` 引入的名字可被**重新赋值**（含复合赋值 `+=` 等）。

**示例：let 与 var**

```cml
// 不可变：常用于配置与中间结果
let WIDTH = 20
let HEIGHT = 10
let idx = 30
let start = now()
let res = fib(idx)

// 可变：需要修改的状态
var c = 0
var result: Point[] = []
wait target += 1
```

---

## 3.2 类型层次概览

### 3.2.1 基本类型

- 如 **`int`**、**`float`**、**`bool`**、**`string`** 等；名称与实现一致。

**示例**

```cml
func fib(n: int): int { ... }
func hit_wall(p: Point): bool { ... }
let duration = now() - start   // 浮点时间差
```

### 3.2.2 复合类型

**数组**

- **`T[]`**、**`T[][]`** 等。

```cml
let signals: Signal[] = [(1, 2), (10, 20)]
var result: Point[] = []
func mergesort(arr: int[]): int[] sync { ... }
func dot(a: float[], b: float[]): float { ... }
```

**结构体（具名字段）**

- 字面形式：**`{ x: int, y: int }`**。
- 类型别名：**`type Point = { x: int, y: int }`**。

```cml
type Point = {
    x: int,
    y: int
}
let p = { x: 5, y: 5 }
let new_head = { x: now_head.x, y: now_head.y - 1 }
```

**元组（按位置）**

- **`(T1, T2, ...)`**；访问用 **`.0`**、**`.1`** 等。

```cml
type Signal = (int, int)
func comp1(signal: Signal): Signal {
    return (signal.0 * 2, signal.1 * 2)
}
let res1 = signals[0]->comp1
```

**函数类型**

- **`(参数列表) => 返回类型`**，可带 **with** 参数与修饰符（如 **`sync`**）。

```cml
with <predicate: (p: Point) => bool>
func my_filter(points: Point[]): Point[] sync { ... }

// 函数类型注解
(p: Point) => bool
(x: int): string => { ... }
(y: int) => sync { ... }
```

### 3.2.3 泛型/参数化类型

类型位置上的 **`Name<A1, A2, ...>`** 有两种来源：

- **模块类型**（由 C++ 模块引入，如 `Tensor`）：尖括号里的实参是类型或写作 `const <data>` 的编译期常量，
  含义由该类型自己定义，例如 `Tensor<f32, const [3, 4]>`。
- **类型函数**：带参数的类型声明。使用处用实参替换参数，得到的就是右侧那个类型，图上不留任何痕迹。

```cml
type Pair<T> = (T, T)
type Entry<K, V> = { key: K, value: V }

let p: Pair<int> = (1, 2)             // 就是 (int, int)
func sum_pair(p: Pair<int>): int { return p.0 + p.1 }
```

类型函数的实参只能是类型，个数必须与参数一致；不带实参使用（`p: Pair`）是错误。详见 7.2.6。

**类型变量**不是一种值的类型，只出现在泛型函数的签名与函数体里，用 `typeas T` 绑定，见 7.2.5。

### 3.2.4 可能为空与可能出错

两种专用的内建形式（设计原则 2.9），各带一位标记：

- **`T?`**：可能为空。值要么是 `null`，要么是一个 `T`。
- **`T!`**：可能出错。值要么是一个 `T`，要么是一个错误。错误是固定的内建结构 `{ kind: string, message: string, location: string }`，所有可能出错的函数共用这一种。

规则：

- `T?`、`T!` 与 `T` 是不同的类型。`T?`/`T!` 的值可以保存、传递、返回、打印（`println` 打印 `null`、其中的值，或 `error <kind>: <message>`），但不能当作 `T` 使用：没有运算符或算子接受它，也不能对它取成员。只能用 `??`、`!!`、`?->`（[6.6](06_expressions.md)）和 `match` 的固定模式（[6.9.2](06_expressions.md#692-match)）拆开；否则是编译期错误（`SemanticDiag::TaggedValueNotOpened`、`NoMatchingFunction` 等）。
- **`T` 不会自己变成 `T?`/`T!`**（与数值类型不做隐式转换一致）。进入这两种形式的写法：
  - `v as T?`、`v as T!`；
  - 带类型的声明 `let x: T? = v`；
  - 返回类型声明为 `T?`/`T!` 的函数里的 `return v`（与 `throw` 相对：一个给出值，一个给出错误）。
  - 调用的实参不转换：`f(5)` 传给 `x: int?` 是错误，写 `f(5 as int?)`。
- **`null`** 是每个 `T?` 的字面量，在上述三处写 `null` 即可：`let x: int? = null`、`null as int?`、`return null`。传给形参同样要写 `f(null as int?)`。
- 分支的各臂类型必须相同：`if c then 1 else null` 是错误，把两臂都写成 `T?`（在声明了 `T?` 的函数里用 `return`，或用 `as`）。
- 两种标记可以组合：`T?!`（可能出错，成功时可能为空）、`T!?`。同一种标记不能重复（`T??`、`T!!`）：编译期错误 `SemanticDiag::TaggedTypeNesting`。数组元素等位置需要括号：`(int?)[]`。
- 与泛型配合：`(typeas T)?` 从可空实参里的类型绑定 `T`。

```cml
func positive(x: int): int? {
    if x > 0 then { return x } else { return null }
}

func or_else(x: (typeas T)?, d: T): T {
    return x ?? d
}
```

运行时没有类型信息：一个值是不是 `T?` 只由它的静态类型决定。实现见 `docs/technical/29_nullable_and_fallible_values.md`。

`T ? U` 是早先预留的另一个记号，与 `T!` 的关系未定，使用它是编译期错误。

### 3.2.5 类型运算

- 类型运算：联合 **`|`**、交集 **`&`**、差 **`\`**、键联合等（见类型表达式规则）。

### 3.2.6 映射与集合

两种内建类型（设计原则 2.9），不需要 `import`：

- **`Map<K, V>`**：键到值的映射。
- **`Set<T>`**：不含重复元素的集合。

没有专门的字面量，用数组字面量（或任何数组）加 `as` 构造：

```cml
let m = [('a', 1), ('b', 2)] as Map<string, int>     // (K, V)[] -> Map<K, V>
let s = [3, 1, 2, 3] as Set<int>                     // T[] -> Set<T>，重复元素被去掉
let e = [] as Map<string, int>                       // 空映射
let r = pairs(n) as Map<int, string>                 // 运行期算出的数组
```

规则：

- **值不可变。** 所有操作返回新值，原来的映射/集合不变；没有原地修改的操作。
- **键的类型**只能是 `int`、`i32`、`bool`、`string`，以及由它们组成的元组。浮点数（`NaN` 不等于自身，`0.0` 等于 `-0.0`）、数组（可以被原地修改）、结构体、函数、`T?`/`T!` 等都不能作键：编译期错误 `SemanticDiag::CollectionKeyNotAllowed`。值的类型不限。
- **顺序是确定的：按键排序。** 整数按数值，`false` 在 `true` 之前，字符串按字节，元组逐个元素比较。`keys`、`values`、`items`、打印与 `==` 都按这个顺序，与构造时的书写顺序、平台、执行后端无关。
- **重复的键**：数组字面量里写了两个相同的常量键是编译期错误 `SemanticDiag::DuplicateCollectionKey`；运行期才知道的数组里，后面的条目覆盖前面的。集合对重复元素不报错。
- **元素类型必须完全一致**，不做隐式转换：`[('a', 1)] as Map<string, float>` 是错误，写 `1.0`。带类型的声明（`let m: Map<string, int> = [...]`）不是转换，必须写 `as`。
- `Map`、`Set` 必须带类型实参书写。与泛型配合：`Map<typeas K, typeas V>`、`Set<typeas T>` 从实参的类型绑定类型变量（7.2.5）。
- 打印：映射 `{a: 1, b: 2}`，集合 `{1, 2, 3}`，空的都是 `{}`。

操作（均为纯算子；`K`、`V`、`T` 必须与映射/集合的类型完全一致）：

| 写法 | 结果 | 说明 |
|---|---|---|
| `m[k]`、`get<k>(m)` | `V?` | 查不到是 `null`，用 `??` 取出；见 [6.3.2](06_expressions.md) |
| `get_or<k, d>(m)` | `V` | 查不到时为 `d` |
| `put<k, v>(m)` | `Map<K, V>` | 键已存在则替换它的值 |
| `remove<k>(m)`、`remove<x>(s)` | 同类型 | 不存在时得到同样的内容 |
| `has<k>(m)`、`has<x>(s)`、`contains<x>(s)` | `bool` | |
| `len(m)`、`len(s)` | `int` | |
| `keys(m)`、`values(m)`、`items(m)` | `K[]`、`V[]`、`(K, V)[]` | 按键的顺序，每次是新数组 |
| `merge(m1, m2)` | `Map<K, V>` | 两者的条目；键相同时取 `m2` 的 |
| `add<x>(s)` | `Set<T>` | |
| `union(a, b)`、`intersect(a, b)`、`difference(a, b)` | `Set<T>` | |
| `is_subset(a, b)` | `bool` | `a` 的每个元素都在 `b` 中 |
| `items(s)` | `T[]` | 升序 |
| `a == b`、`a != b` | `bool` | 条目相同（与构造顺序无关） |

带 `with` 参数的操作有三种等价的写法：`put<k, v>(m)`、`m->put<k, v>`（便于链式书写）、`put(m, k, v)`。

```cml
let m2 = m->put<'c', 3>->remove<'a'>
let v = m2['c'] ?? 0
let n = if s->contains<2> then len(s) else 0

// 遍历：先取数组，再用 map / foreach / reduce
let doubled = items(m)->map<(kv: (string, int)): (string, int) => (kv.0, kv.1 * 2)> as Map<string, int>

func count_words(words: string[]): Map<string, int> {
    return words->reduce<(acc: Map<string, int>, w: string): Map<string, int> => acc->put<w, (acc[w] ?? 0) + 1>, [] as Map<string, int>>
}
```

值类型本身可空时（`Map<string, int?>`），`m[k]` 的类型就是 `int?`（不会出现 `int??`）：键不存在与值为 `null` 都得到 `null`，需要区分时用 `has`。

`put` 每次复制整张表（代价与条目数成正比），逐条构造很大的映射时，先得到条目数组再一次 `as` 转换。程序自己声明的名为 `Map`、`Set` 的类型，以及与上述算子同名的函数，优先于内建的名字。实现与取舍见 `docs/technical/31_map_and_set.md`。

---

## 3.3 类型定义示例

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

**在函数签名与 let/var 中使用**

```cml
func move_snake(snake: Point[], dir: string): Point[] { ... }
func process(signals: Signal[]) sync { ... }
let signals: Signal[] = [(1, 2), (10, 20)]
var result: Point[] = []
```

---

## 3.4 特殊方法名与算子

- 运算符在 GIR 中对应 **OperNode**，绑定到 **operator_t**。
- 运行时根据**操作数类型**分发到具体实现（如整型加法、浮点加法）；内部名如 **`__add__`**、**`__le__`** 等与实现一致。
- 用户代码通过 **`+`**、**`<=`**、**`@`** 等符号使用，无需书写特殊方法名。

**示例：运算符与类型**

```cml
fib(n - 1) + fib(n - 2)     // 算术
n <= 1                      // 比较
X @ weights                 // 矩阵乘（张量）
residuals ^ 2.0             // 幂
transpose(X) @ errors       // 线性代数
```

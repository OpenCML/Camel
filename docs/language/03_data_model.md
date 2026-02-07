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

- **`Ident<T1, T2, ...>`**，如 **`Point[]`**、**`(int, int)`**、**`Tensor`**。

```cml
type Point = { x: int, y: int }
type Signal = (int, int)
with <weights: Tensor>
func predict(X: Tensor): Tensor { ... }
```

### 3.2.4 可空与类型运算

- 类型后可跟 **`?`** 表示可空联合（语法见文法 **typeExpr**）。
- 类型运算：联合 **`|`**、交集 **`&`**、差 **`\`**、键联合等（见类型表达式规则）。

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

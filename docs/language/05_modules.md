# 5. 模块

[English](en/05_modules.md) | 中文

每个源文件是一个模块。本章描述模块声明、导入、导出与命名空间。

---

## 5.1 模块声明

```cml
module shapes
```

`module <名字>` 写在文件开头，给模块起名。可以省略；省略时模块名取自文件名。诊断信息里括号中的名字就是模块名。

## 5.2 导入

三种写法：

```cml
import { sqrt, floor } from math     // 引入列出的名字
import sqrt from math                // 引入一个名字
import math                          // 只引入模块的命名空间：用 math::sqrt 访问
```

- `import 模块` 不把模块里的名字直接注入当前模块；通过 `模块::名字` 访问，或再写 `using namespace 模块` 把它导出的名字全部注入。
- **模块可以互相导入**（`a` 导入 `b`、`b` 又导入 `a`，或更长的环）：两边的函数可以互相调用，值与类型可以互相使用，只要顶层的值之间不成环（[5.7](#57-模块的加载)）。找不到模块是 `SemanticDiag::ModuleNotFound`，定位在那条 `import`；被导入的模块有错误时，导入它的地方是 `SemanticDiag::ImportedModuleHasErrors`。
- **导入时可以改名**：`import { find as str_find } from str` 把模块里的 `find` 以 `str_find` 这个名字引入；改名的与不改名的可以写在同一条里（`import { find as str_find, trim } from str`）。改名之后原名在本模块不可见。两个模块有同名函数时，给其中一个改名，或只导入一个、另一个用限定名（`import array` 后写 `array::find`）。整个模块的改名（`import m as n`）尚未裁定，不支持。
- 两条按名字的导入带来同名、参数类型也相同的函数，是 `SemanticDiag::ImportNameConflict`，定位在后一条 `import`，并指出前一条；不会悄悄取其中一个。同名而参数类型不同的函数目前合成一组重载，按实参类型选择（这一点是否保留尚未确定）。
- **查找顺序**：与入口文件同目录的 `.cml` 文件、`-I <目录>` 给出的目录、环境变量 `CAMEL_PACKAGES` 列出的目录，以及随 Camel 安装的模块。

```cml
import math
using namespace math

func main(): int sync {
    println(math::sqrt(9.0), floor(2.5))
    return 0
}
```

```text
3.0 2.0
```

### 5.2.1 随 Camel 提供的模块

不需要导入就能用的名字（全局内建）：`print`、`println`、`input`、`format`、`join`、`len`、`range`、`zip`、`head`、`tail`、`slice`、`concat`、`append`、`extend`、`contains`、`map`、`filter`、`reduce`、`foreach`、`exit`、`sleep`、`error`、`parse_int`、`parse_float`、转换函数（`int`、`float`、`string` 等），以及映射与集合的操作（[3.5](03_data_model.md#35-映射与集合)）。

| 模块 | 内容 |
|---|---|
| `str` | 字符串：按字节的切分、修剪、大小写、替换、查找、截取；按码点的计数、读取、编解码 |
| `array` | 数组：`reverse`、`take`、`drop`、`unique`、`find`、`sum`、`min`、`max`、`sort_by`、`flat_map` 等（用 Camel 写的泛型函数） |
| `math` | 常量 `pi()`、`e()`；初等函数、取整、`min`/`max`/`clamp`、`gcd`/`lcm`、进制转换、位运算 `bit_and` 等 |
| `random` | `seed`、`rand`、`randint`、`randn`、`choice`、`sample`、`shuffle` |
| `time` | `now`、`strftime`、`strptime` |
| `fs` | `exists`、`is_file`、`is_dir`、`mkdir`、`mkdirs`、`read_text`、`try_read_text`、`write_text`、`file_size`、`read_chunk` |
| `json` | `encode`、`decode`、`decode_as` |
| `os`、`sys` | `whoami`、`cpu_count`、终端输入；`get_cwd`、`get_env` |
| `re` | `test` |
| `crypto` | `sha256`、`sha256_rounds` |
| `this` | `zen` |
| `algo` | `sort`、`sorted`、`merge_sorted_arrays` 等排序函数 |
| `statistics` | `mean`、`stdev`、`variance`、`median`、`quantile` |
| `para` | `unordered_foreach`、`unordered_reduce` |
| `tensor`、`nn`、`autodiff`、`onnx` | 张量、网络层与优化器、求梯度、ONNX 导出 |
| `python` | 嵌入 Python（需要构建时启用） |

除 `tensor`、`nn`、`autodiff`、`onnx` 与 `python` 之外，表中各模块的每个函数见 [标准库参考](../stdlib-reference.md)，约定见 [5.6](#56-标准库)。

## 5.3 导出

模块里的名字默认只在模块内可见。`export` 使它可以被别的模块导入：

```cml
// shapes.cml
module shapes

import { sqrt } from math

export type Vec = { x: float, y: float }

export func hypot(v: Vec): float {
    return sqrt(v.x * v.x + v.y * v.y)
}

export func area(w: float, h: float): float {
    return w * h
}
```

```cml
// main.cml，与 shapes.cml 在同一目录
import { hypot, Vec } from shapes
import shapes

func main(): int sync {
    let v: Vec = { x: 3.0, y: 4.0 }
    println(hypot(v))
    println(shapes::area(2.0, 5.0))
    return 0
}
```

```text
5
10
```

- `export` 写在 `func`、`type`、`let`/`var` 声明之前，或写成 `export { a, b }` 列出已声明的名字。
- 泛型函数可以导出（[7.5](07_statements.md#75-泛型函数)）。既有带标注的函数又有其他重载的名字目前不能导出（`SemanticDiag::ConstructNotSupported`）。

**导出的函数可以使用它所在模块顶层的 `let` 与 `var`**，直接用到或经它调用的本模块函数用到都一样（设计原则 2.11）：

```cml
// counter.cml
module counter
var cnt = 0
let step = 2
export sync func bump(): int sync { wait cnt += step; return cnt }
export func current(): int { return cnt }
```

```cml
// main.cml
import counter
let step = 1000                 // 与 counter 的 step 无关

sync func main(): int sync {
    println(counter::bump(), counter::bump(), counter::current())
    return 0
}
```

```text
2 4 4
```

程序的所有模块建在同一张 `__root__` 图上：每个模块顶层的值是这张图上的一个节点，函数经捕获端口取得它，捕获绑定在这个值上（"模块 + 名字"），不在调用处按名字再找一遍。所以导入方自己有同名的值（上例的 `step`）、调用处有同名的局部量，都不会顶替它。顶层的值与入口模块的一样，在 `main` 开始之前按静态依赖求值，被几个模块用到也只求值一次。

**模块可以导出值**（`let`）：

```cml
// geometry.cml
module geometry
export let pi = 3.14159
export let tau = pi * 2.0
```

```cml
import { pi } from geometry
import geometry

let area = pi * 2.0 * 2.0
func main(): int sync {
    println(area, geometry::tau)
    return 0
}
```

```text
12.56636 6.28318
```

导出的值就是公共 `__root__` 上的那个节点：导入方在顶层直接用它，在函数里经捕获端口取得它，不复制、不重新求值。没有被任何东西用到的导出值不求值（它的初始化表达式里有效应也不执行，被导入的模块里这也不给警告）。

- **不能导出 `var`**：`export var count = 0` 是 `SemanticDiag::ExportedVariable`，定位在这个变量。导出读写它的函数（上面的 `counter`）。
- 两条按名字的导入带来同名的值（或一个是值、一个是函数）是 `SemanticDiag::ImportNameConflict`；值不组成重载。

## 5.4 命名空间

`namespace 名字 { ... }` 把一组函数与类型放在一个限定名下，块内可以写 `func`、`type`、嵌套的 `namespace`、`import`、`export`、`using namespace`：

```cml
type Counter = { n: int }

namespace Counter {
    func new(): Counter { return { n: 0 } }
    func next(c: Counter): Counter { return { n: c.n + 1 } }
}

func main(): int sync {
    let c = Counter::next(Counter::new())
    println(c.n)
    return 0
}
```

```text
1
```

- 命名空间可以与类型同名，于是 `类型::函数` 成为这个类型的构造函数与工具函数的惯用写法（模块提供的类型也是如此：`Tensor::ones([2, 2])`）。
- 在命名空间里导出的名字保留限定名：`namespace Tools { export func inc(...) }` 在别的模块里是 `模块::Tools::inc`。
- `using namespace 模块::Tools` 只注入某个嵌套命名空间里的名字。

## 5.5 `use`

`use 名字 = 限定名` 保留，尚未实现（`SemanticDiag::FeatureNotSupported`）。替代写法：

- 直接引入这个名字：`import { sqrt } from math`。
- 要换一个名字时用 `let`：`let sq = math::sqrt`。
- 代表一组重载的名字没有单一的函数值，不能用 `let` 绑定；这时写一个 lambda：`let sq = (x: float): float => math::sqrt(x)`。

## 5.6 标准库

各模块导出的函数（签名、是否纯、失败时的行为、例子）见 [标准库参考](../stdlib-reference.md)，它由 `stdlib/signatures/*.cml` 与用 Camel 写的库（`stdlib/*.cml`）生成。约定：

- 函数要处理的值是它的普通形参，配置这次调用的值是 `with` 形参，因此可以链式书写：

  ```cml
  import { trim, split, to_upper } from str
  import { sort_by, take } from array

  let words = '  pear, fig ,apple '->trim->split<','>->map<(w: string) => trim(w)>
  let shortest = words->sort_by<(w: string) => len(w)>->take<2>     // ['fig', 'pear']
  ```

- 可能没有结果的函数返回 `T?`（`find`、`index_of` 找不到，`min`、`max`、`median` 遇到空数组），用 `??` 处理；只有声明为可能失败的操作返回 `T!`（如按码点解码一个不是合法 UTF-8 的字符串）；越界与不在定义域内的实参是故障。
- 用 Camel 写的库（如 `array`）导出的是泛型函数，在调用它的模块里按元素类型实例化（见 [7.5](07_statements.md#75-泛型函数)）。
- 用 Camel 写的模块可以导出值（[5.3](#53-导出)）；C++ 模块（如 `math`）的常量仍是无参函数：`pi()`、`e()`。
- 传给 `array` 模块高阶函数的函数用 `map` 施加：各元素上的调用之间没有顺序约定（并行调度器可以同时执行），结果按位置排列；需要顺序的副作用用内建的 `foreach`、`reduce`。
- `camel inspect --ops <文件>` 列出内建模块与该程序导入的每个模块的全部算子及其声明的纯度。

## 5.7 模块的加载

程序的所有模块建在同一张 `__root__` 图上（设计原则 2.11），加载分三步：

1. **声明**：从入口出发找到所有被导入的模块，每个模块只登记它声明了什么（函数的签名、顶层的值、类型）并填好导出表，不看任何函数体。所以模块之间怎样互相导入都可以。一个模块的类型在它的其他声明之前登记，互相导入的两个模块因此可以使用对方的类型（类型的定义本身不能互相依赖成环）。
2. **建图**：按导入的次序建各模块的函数体与顶层的值；一个函数或值在第一次被用到时就建出来，不论用到它的是哪个模块。所以**顶层的声明与书写顺序无关**：

   ```cml
   let area = pi * r * r      // pi、r 写在后面
   let pi = 3.14159
   let r = 2.0
   func twice() { return k * 2 }   // 没有写返回类型，也可以在写出它之前调用
   let k = 21
   ```

3. **链接**：把每个函数用到的顶层值经调用链一路接到 `__root__`，建对入口模块 `main` 的调用，算出效应的摘要并给出依赖它的警告。

- 顶层的值不能直接或间接地依赖自己——经过别的值、经过它的初始化表达式调用的函数、经过别的模块都算——否则是 `SemanticDiag::ModuleValueCycle`，定位在闭合环的那次引用，消息里列出整条链：`The value 'a' depends on itself: a (m.cml:1:1) -> b (m.cml:2:1) -> a`。函数之间互相调用不是环。
- 被导入的模块里叫 `main` 的函数是普通的函数，只有入口模块的 `main` 是程序的入口。
- 递归的函数在调用自己之后才用到的顶层值照常传给递归调用；但在调用自己之后才用到的**变量**是 `SemanticDiag::CaptureAfterRecursiveCall`：那次调用已经按当时知道的变量排好了读写的顺序。把对变量的第一次使用写在递归调用之前，或把它作为参数传入。

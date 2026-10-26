# 5. 模块

[English](en/05_modules.md) | 中文

每个源文件是一个模块。本章描述模块的名字、导入、导出与命名空间。

---

## 5.1 模块的名字

一个文件就是一个模块，模块名就是文件名（不含扩展名）：`shapes.cml` 是模块 `shapes`，别人写 `import shapes` 导入它。源码里不写模块声明，名字只有文件名这一个来源。诊断信息里括号中的名字就是模块名。

## 5.2 导入

<!-- camel-snippet: fragment: the forms of import; lib and its namespace are examples -->

```cml
import { sqrt, floor } from math     // 引入列出的名字
import sqrt from math                // 引入一个名字，与 import { sqrt } from math 相同
import math                          // 只引入模块：用 math::sqrt 访问
import math as m                     // 给模块起别名：用 m::sqrt 访问
import { inc } from lib::Tools       // 引入模块里某个命名空间中的名字
```

- `import 模块` 不把模块里的名字直接注入当前模块；通过 `模块::名字` 访问，或再写 `using namespace 模块`（[5.4](#54-命名空间)）。
- **模块可以互相导入**（`a` 导入 `b`、`b` 又导入 `a`，或更长的环）：两边的函数可以互相调用，值与类型可以互相使用，只要顶层的值之间不成环（[5.6](#56-模块的加载)）。找不到模块是 `SemanticDiag::ModuleNotFound`，定位在那条 `import`；被导入的模块有错误时，导入它的地方是 `SemanticDiag::ImportedModuleHasErrors`。
- **模块导入自己是允许的，没有效果**（设计原则 10.1）：`module m` 里写 `import m`、`import { f } from m` 不绑定任何名字，它的名字本来就是它自己的（带不带 `m::` 都可以，[5.4](#54-命名空间)）。入口文件按文件名导入自己时也不会再加载一份。
- **导入时可以改名**：`import { find as str_find } from str` 把模块里的 `find` 以 `str_find` 这个名字引入；改名的与不改名的可以写在同一条里（`import { find as str_find, trim } from str`），单个名字也可以（`import find as str_find from str`）。改名之后原名在本模块不可见。两个模块有同名函数时，给其中一个改名，或只导入一个、另一个用限定名（`import array` 后写 `array::find`）。整个模块的别名写 `import m as n`。
- 两条按名字的导入带来同名、参数类型也相同的函数，是 `SemanticDiag::ImportNameConflict`，定位在后一条 `import`，并指出前一条；不会悄悄取其中一个。同名而参数类型不同的函数合成一组重载，按实参类型选择（同一层里的同名函数是一个重载集，[5.4](#54-命名空间)）。
- **查找顺序**：模块的 `import` 先在模块自己的目录里找；找不到再按模块根找——`-I <目录>` 给出的目录按给出的顺序，然后是环境变量 `CAMEL_PACKAGES` 列出的目录，然后是 `CAMEL_MODULE_PATH`；最后是随 Camel 安装的标准库（设计原则 10.2）。入口程序与库模块同一条规则：入口文件自己的 import 先看与它同目录的文件，而这个目录不是别的模块的搜索根，所以标准库自己的 `import graph` 取的是标准库的 `graph`，不会被入口目录里的同名文件顶替，而入口程序自己的 `import graph` 仍然取入口目录里的那个；一个库无论被谁导入，它的 import 都解析到同一处。同一目录里 `xx.cmo` 优先于同名的 `xx.*`。import 写的是模块名，不是文件路径：`import ../lib` 是语法错误，这条规则不提供向上走出模块目录的写法；以点开头的名字按模块名解析，不是从目录向上走——点数是要从导入模块自己的名字里去掉的层数（`a.b` 里的 `..lib` 是 `lib`），点数超过名字的层数时这个名字不成立，导入以 `SemanticDiag::ModuleNotFound` 失败并说明原因，不会去搜索任何目录，也不会被当成路径。

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

不需要导入就能用的名字（全局内建）：`print`、`println`、`input`、`format`、`join`、`len`、`range`、`zip`、`head`、`tail`、`last`、`slice`、`concat`、`append`、`extend`、`contains`、`map`、`filter`、`reduce`、`foreach`、`exit`、`sleep`、`error`、`parse_int`、`parse_float`、转换函数（`int`、`float`、`string` 等），以及映射与集合的操作（[3.6](03_data_model.md#36-映射与集合)）。

| 模块 | 内容 |
|---|---|
| `str` | 字符串：按字节的切分、修剪、大小写、替换、查找、截取；按码点的计数、读取、编解码 |
| `array` | 数组：`sort`、`merge_sort`、`quick_sort`、`insert_sort`、`merge_sorted_arrays`、`argsort`、`reverse`、`take`、`drop`、`unique`、`find`、`sum`、`min`、`max`、`sort_by`、`flat_map` 等（大部分是用 Camel 写的泛型函数，排序的内核是原生算子） |
| `math` | 常量 `pi`、`e`、`inf`、`nan`（值）；初等函数、取整、`min`/`max`/`clamp`、`gcd`/`lcm`、进制转换、位运算 `bit_and` 等 |
| `random` | 类型 `Key`；`seed`、`rand`、`randint`、`randn`、`choice`、`sample`、`shuffle`，以及纯的键式随机 `key`、`split`、`permutation` |
| `time` | `now`、`strftime`、`strptime` |
| `fs` | `exists`、`is_file`、`is_dir`、`mkdir`、`mkdirs`、`remove`、`remove_dir`、`list_dir`、`read_text`、`write_text`、`append_text`、`file_size`、`read_chunk`、`read_bytes`、`write_bytes`、`append_bytes` |
| `json` | 类型 `Json`；`encode`、`decode`、`decode_as`；`kind`、`is_null`、`get`、`as_bool`、`as_int`、`as_float`、`as_string`、`as_array`、`as_object` |
| `process`、`env` | `user_name`、`cpu_count`、终端输入；`get_cwd`、`get_env` |
| `re` | `test` |
| `hash` | `sha256` |
| `this` | `zen` |
| `statistics` | `mean`、`stdev`、`variance`、`median`、`quantile` |
| `para` | `unordered_foreach`、`unordered_reduce` |
| `tensor`、`nn`、`autodiff`、`onnx` | 张量、网络层与优化器、求梯度、ONNX 导出；取行 `take`、`one_hot`，按目标类型重载的 `softmax_cross_entropy`（独热分布或整数类标） |
| `python` | 嵌入 Python（需要构建时启用） |

除 `tensor`、`nn`、`autodiff`、`onnx` 与 `python` 之外，表中各模块的每个函数见 [标准库参考](../stdlib-reference.md)，约定见 [5.5](#55-标准库)。张量的取行、类标与键式随机见 [43_rows_labels_and_keys.md](../technical/43_rows_labels_and_keys.md)，数组的下标与切片规则见 [设计原则 5.4](../design-principles.cn.md)。

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
5.0
10.0
```

- `export` 写在 `func`、`type`、`let`/`var` 声明之前，或写成 `export { a, b }` 列出已声明的名字。
- 泛型函数可以导出（[7.5](07_statements.md#75-泛型函数)）。一个名字既是被标注的函数（`@d<a> func f`）又有同名的其他函数时也能导出：导出的名字两者都带，导入方按重载规则选，与声明它们的模块里一样。

**导出的函数可以使用它所在模块顶层的 `let` 与 `var`**，直接用到或经它调用的本模块函数用到都一样（设计原则 10.1）：

```cml
// counter.cml
module counter
var cnt = 0
let step = 2
export func bump(): int sync { cnt += step; return cnt }
export func current(): int { return cnt }
```

```cml
// main.cml
import counter
let step = 1000                 // 与 counter 的 step 无关

func main(): int sync {
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

`namespace 名字 { ... }` 把一组声明放在一个限定名下。块内可以写 `func`、`type`、`let`、`var`、嵌套的 `namespace`、`import`、`export`、`using namespace`：

```cml
type Counter = { n: int }

namespace Counter {
    let start = 0
    func new(): Counter { return { n: start } }
    func next(c: Counter): Counter { return { n: c.n + 1 } }
    func twice(c: Counter): Counter { return next(next(c)) }   // 同一块里的名字不必带前缀
}

func main(): int sync {
    let c = Counter::twice(Counter::new())
    println(c.n)
    return 0
}
```

```text
2
```

- 命名空间不是一种对象：它是名字里的路径。声明在 `namespace A { namespace B { ... } }` 里的 `f` 叫 `A::B::f`；同一个命名空间可以写成几块。
- **块内不带前缀的名字**：在命名空间里写的名字依次在所在的命名空间、外层的命名空间、模块顶层里找，取第一个找到的；类型也一样（`namespace Models { type Box = ...; func mk(): Box }`）。
- **命名空间里可以有值**：`namespace Config { export let limit = 10 }`，用 `Config::limit`。它与模块顶层的 `let` 只差名字里的路径。`var` 可以写，不能导出。
- **模块可以用自己的名字限定自己的东西**：`module shapes` 里写 `shapes::area` 与写 `area` 相同（文件名作为模块名时也一样）。
- 命名空间可以与类型同名，于是 `类型::名字` 成为这个类型的构造函数与工具函数的惯用写法（`enum` 的变体就是这样：`Shape::Circle`、`Shape::Empty`）。
- 在命名空间里导出的名字保留限定名：`namespace Tools { export func inc(...) }` 在别的模块里是 `模块::Tools::inc`，也可以 `import { inc } from 模块::Tools`。
- 诊断、调试器与图里显示完整的名字：别的模块的函数显示为 `模块::路径::名字`，入口模块自己的省略模块名。

**`using namespace`** 让一个命名空间里的名字可以不带前缀使用：

<!-- camel-snippet: fragment: the forms of using namespace; lib, Tools and Inner are examples -->

```cml
import lib
using namespace lib::Tools          // 整个模块都可以写 inc

namespace A {
    using namespace Inner           // 只对这个块里的声明生效（Inner 是本模块的 A::Inner）
}

func f(): int {
    using namespace lib             // 只对这个函数生效
    return root()
}
```

- 对象可以是导入的模块或其中的命名空间，也可以是本模块的命名空间。
- **只在它所在的块里生效**：写在模块顶层对整个模块生效，写在命名空间块里对那一块的声明生效，写在函数体（或其中的块）里只对那里生效。块里的位置不影响。
- 两条 `using` 带来同名、无法区分的东西（同名同签名的函数、值、类型）时，**在使用处**报 `SemanticDiag::AmbiguousName`，消息用完整的名字 `模块::路径::名字` 列出各个候选（设计原则 10.2；不管导入时给模块起的别名是什么），写其中一个限定名即可。
- **重开同一个命名空间块时，前一块里的 `using` 不带到后一块**（设计原则 10.2）：`using` 只对写它的那一块生效。参数类型不同的同名函数合成一组重载。

**名字的优先级只有一条规则**：局部的名字 > 本模块的名字（所在命名空间、外层命名空间、模块顶层）> 按名字导入的（包括经 `模块::` 前缀访问的）> `using namespace` 带来的 > 内建的。取第一个有这个名字的层；同一层里同名的函数合成重载集，跨层不合并（所以本模块的 `inc(string)` 会挡住 `using` 带来的 `inc(int)`）。运算符例外，它的重载来自各层。

## 5.5 标准库

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
- 用 Camel 写的模块可以导出值（[5.3](#53-导出)）；C++ 模块也可以导出值（设计原则 10.1）：`math` 的常量 `math::pi`、`math::e`、`math::inf`、`math::nan` 不带括号，用到的地方就是那个常量，没有 root 上的节点，也不需要捕获。
- 传给 `array` 模块高阶函数的函数用 `map` 施加：各元素上的调用之间没有顺序约定（并行调度器可以同时执行），结果按位置排列；需要顺序的副作用用内建的 `foreach`、`reduce`。
- `camel inspect --ops <文件>` 列出内建模块与该程序导入的每个模块的全部算子及其声明的纯度。

## 5.6 模块的加载

程序的所有模块建在同一张 `__root__` 图上（设计原则 10.1），加载分三步：

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
- 类型也不能跨模块成环：`a` 的 `type A = { b: B }` 用到 `b` 的 `B`，而 `b` 的 `type B = { a: A }` 又用到 `A`，是 `SemanticDiag::ModuleTypeCycle`（设计原则 10.1），定位在闭合环的那次引用，消息列出整条环：`cycle_a::A -> cycle_b::B -> cycle_a::A`。
- 被导入的模块里叫 `main` 的函数是普通的函数，只有入口模块的 `main` 是程序的入口。
- 递归的函数在调用自己之后才用到的顶层值照常传给递归调用；但在调用自己之后才用到的**变量**是 `SemanticDiag::CaptureAfterRecursiveCall`：那次调用已经按当时知道的变量排好了读写的顺序。把对变量的第一次使用写在递归调用之前，或把它作为参数传入。

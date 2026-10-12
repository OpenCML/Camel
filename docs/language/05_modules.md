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
3 2
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
- 模块不能导出值，常量写成无参函数：`pi()`、`e()`。
- 传给 `array` 模块高阶函数的函数用 `map` 施加：各元素上的调用之间没有顺序约定（并行调度器可以同时执行），结果按位置排列；需要顺序的副作用用内建的 `foreach`、`reduce`。
- `camel inspect --ops <文件>` 列出内建模块与该程序导入的每个模块的全部算子及其声明的纯度。

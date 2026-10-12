# 5. Modules

English | [中文](../05_modules.md)

Every source file is a module. This chapter describes module declarations, imports, exports and namespaces.

---

## 5.1 Module declaration

```cml
module shapes
```

`module <name>` is written at the beginning of the file and names the module. It can be omitted; when it is, the module name is taken from the file name. The name in parentheses in diagnostic messages is the module name.

## 5.2 Imports

Three forms:

```cml
import { sqrt, floor } from math     // bring in the listed names
import sqrt from math                // bring in one name
import math                          // bring in only the module's namespace: access with math::sqrt
```

- `import module` does not inject the names in the module directly into the current module; access them through `module::name`, or additionally write `using namespace module` to inject all the names it exports.
- **Search order**: `.cml` files in the same directory as the entry file, the directories given by `-I <dir>`, the directories listed in the environment variable `CAMEL_PACKAGES`, and the modules installed with Camel.

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

### 5.2.1 Modules provided with Camel

Names that can be used without an import (global built-ins): `print`, `println`, `input`, `format`, `join`, `len`, `range`, `zip`, `head`, `tail`, `slice`, `concat`, `append`, `extend`, `contains`, `map`, `filter`, `reduce`, `foreach`, `exit`, `sleep`, `error`, `parse_int`, `parse_float`, the conversion functions (`int`, `float`, `string` and so on), and the operations on maps and sets ([3.5](03_data_model.md#35-maps-and-sets)).

| Module | Contents |
|---|---|
| `math` | `abs`, `sqrt`, `exp`, `floor`, `ceil`, `round`, `hex`, `bin`, `oct` |
| `random` | `seed`, `rand`, `randint`, `randn`, `choice`, `sample`, `shuffle` |
| `time` | `now`, `strftime`, `strptime` |
| `fs` | `exists`, `is_file`, `is_dir`, `mkdir`, `mkdirs`, `read_text`, `try_read_text`, `write_text`, `file_size`, `read_chunk` |
| `json` | `encode`, `decode`, `decode_as` |
| `os`, `sys` | `whoami`, `cpu_count`, terminal input; `get_cwd`, `get_env` |
| `re` | `test` |
| `algo` | `sort`, `sorted`, `merge_sorted_arrays` and other sorting functions |
| `statistics` | `mean`, `stdev` |
| `para` | `unordered_foreach`, `unordered_reduce` |
| `tensor`, `nn`, `autodiff`, `onnx` | Tensors, network layers and optimisers, gradients, ONNX export |
| `python` | Embedded Python (must be enabled at build time) |

## 5.3 Exports

By default the names in a module are visible only inside the module. `export` makes a name importable by other modules:

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
// main.cml, in the same directory as shapes.cml
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

- `export` is written before a `func`, `type` or `let`/`var` declaration, or in the form `export { a, b }` listing names already declared.
- Generic functions currently cannot be exported; a name that has both an annotated function and other overloads currently cannot be exported (`SemanticDiag::ConstructNotSupported`).

## 5.4 Namespaces

`namespace name { ... }` places a group of functions and types under one qualified name; inside the block one can write `func`, `type`, nested `namespace`, `import`, `export` and `using namespace`:

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

- A namespace can have the same name as a type, so `Type::function` becomes the idiomatic way to write the constructors and utility functions of that type (the same holds for types provided by modules: `Tensor::ones([2, 2])`).
- A name exported inside a namespace keeps its qualified name: `namespace Tools { export func inc(...) }` is `module::Tools::inc` in another module.
- `using namespace module::Tools` injects only the names in one nested namespace.

## 5.5 `use`

`use name = qualified-name` is reserved, not implemented (`SemanticDiag::FeatureNotSupported`). To give a function a short name, use `let`: `let sq = math::sqrt` does not work for a name that stands for a group of overloads; in that case write a lambda: `let sq = (x: float): float => math::sqrt(x)`.

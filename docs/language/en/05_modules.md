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
- A module cannot import itself: `import m` in the file `m.cml` is `SemanticDiag::ImportCycle`. A module that is not found is `SemanticDiag::ModuleNotFound`, located at the `import`.
- A name cannot be renamed when it is imported (there is no `import { a as b }`). When two modules have a function of the same name, import one of them and use the qualified name for the other: `import array`, then `array::find`.
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
| `str` | Strings: by byte, splitting, trimming, case conversion, replacing, searching, taking substrings; by code point, counting, reading, encoding and decoding |
| `array` | Arrays: `reverse`, `take`, `drop`, `unique`, `find`, `sum`, `min`, `max`, `sort_by`, `flat_map` and others (generic functions written in Camel) |
| `math` | The constants `pi()`, `e()`; elementary functions, rounding, `min`/`max`/`clamp`, `gcd`/`lcm`, base conversion, bit operations such as `bit_and` |
| `random` | `seed`, `rand`, `randint`, `randn`, `choice`, `sample`, `shuffle` |
| `time` | `now`, `strftime`, `strptime` |
| `fs` | `exists`, `is_file`, `is_dir`, `mkdir`, `mkdirs`, `read_text`, `try_read_text`, `write_text`, `file_size`, `read_chunk` |
| `json` | `encode`, `decode`, `decode_as` |
| `os`, `sys` | `whoami`, `cpu_count`, terminal input; `get_cwd`, `get_env` |
| `re` | `test` |
| `crypto` | `sha256`, `sha256_rounds` |
| `this` | `zen` |
| `algo` | `sort`, `sorted`, `merge_sorted_arrays` and other sorting functions |
| `statistics` | `mean`, `stdev`, `variance`, `median`, `quantile` |
| `para` | `unordered_foreach`, `unordered_reduce` |
| `tensor`, `nn`, `autodiff`, `onnx` | Tensors, network layers and optimisers, gradients, ONNX export |
| `python` | Embedded Python (must be enabled at build time) |

For every function of the modules in the table other than `tensor`, `nn`, `autodiff`, `onnx` and `python`, see the [standard library reference](../../stdlib-reference.md); for the conventions see [5.6](#56-standard-library).

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
- Generic functions can be exported ([7.5](07_statements.md#75-generic-functions)). A name that has both an annotated function and other overloads currently cannot be exported (`SemanticDiag::ConstructNotSupported`).

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

`use name = qualified-name` is reserved, not implemented (`SemanticDiag::FeatureNotSupported`). Alternatives:

- Bring the name in directly: `import { sqrt } from math`.
- To give it another name, use `let`: `let sq = math::sqrt`.
- A name that stands for a group of overloads has no single function value and cannot be bound with `let`; in that case write a lambda: `let sq = (x: float): float => math::sqrt(x)`.

## 5.6 Standard library

For the functions each module exports (signature, whether it is pure, behaviour on failure, examples) see the [standard library reference](../../stdlib-reference.md), which is generated from `stdlib/signatures/*.cml` and the libraries written in Camel (`stdlib/*.cml`). Conventions:

- The value a function works on is its ordinary parameter and the values that configure the call are `with` parameters, so calls can be chained:

  ```cml
  import { trim, split, to_upper } from str
  import { sort_by, take } from array

  let words = '  pear, fig ,apple '->trim->split<','>->map<(w: string) => trim(w)>
  let shortest = words->sort_by<(w: string) => len(w)>->take<2>     // ['fig', 'pear']
  ```

- A function that may have no result returns `T?` (`find` and `index_of` when nothing is found, `min`, `max` and `median` of an empty array), handled with `??`; only operations declared as fallible return `T!` (such as decoding by code point a string that is not valid UTF-8); out-of-range arguments and arguments outside the domain are faults.
- A library written in Camel (such as `array`) exports generic functions, which are instantiated by element type in the module that calls them (see [7.5](07_statements.md#75-generic-functions)).
- A module cannot export values; constants are written as functions without parameters: `pi()`, `e()`.
- A function passed to a higher-order function of the `array` module is applied with `map`: there is no agreed order between the calls on the elements (a parallel scheduler may execute them at the same time), and the results are arranged by position; for side effects that need an order use the built-in `foreach` and `reduce`.
- `camel inspect --ops <file>` lists all the operators of the built-in modules and of every module the program imports, together with their declared purity.

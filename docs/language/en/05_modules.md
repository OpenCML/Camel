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

```cml
import { sqrt, floor } from math     // bring in the names listed
import sqrt from math                // bring in one name, the same as import { sqrt } from math
import math                          // bring in the module only: reached as math::sqrt
import math as m                     // the module under another name: reached as m::sqrt
import { inc } from lib::Tools       // bring in names of a namespace of the module
```

- `import module` does not put the module's names into the current module; they are reached as `module::name`, or put in scope with `using namespace module` ([5.4](#54-namespaces)).
- **Modules can import each other** (`a` imports `b` and `b` imports `a`, or a longer ring): their functions can call each other and their values and types can be used by each other, as long as the top-level values do not depend on each other in a ring ([5.6](#56-loading-modules)). A module that is not found is `SemanticDiag::ModuleNotFound`, located at the `import`; where a module in error is imported, that is `SemanticDiag::ImportedModuleHasErrors`.
- **A module may import itself, to no effect** (design principles 10.1): `import m` or `import { f } from m` in `module m` binds nothing; its names are its own already (with `m::` in front or without, [5.4](#54-namespaces)). An entry file that imports itself by its file name is not loaded a second time.
- **A name can be renamed when it is imported**: `import { find as str_find } from str` brings the `find` of the module in under the name `str_find`; renamed and plain names may stand in one import (`import { find as str_find, trim } from str`). After a renaming the original name is not visible in this module. When two modules have a function of the same name, rename one of them, or import one and use the qualified name for the other (`import array`, then `array::find`). A single name can be renamed too (`import find as str_find from str`), and a whole module is renamed with `import m as n`.
- Two imports by name that bring functions of the same name and the same parameter types are `SemanticDiag::ImportNameConflict`, located at the later `import` and pointing to the earlier one; neither is chosen silently. Functions of the same name and different parameter types form one set of overloads, chosen by the argument types (functions of one name in one layer are one set of overloads, [5.4](#54-namespaces)).
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
3.0 2.0
```

### 5.2.1 Modules provided with Camel

Names that can be used without an import (global built-ins): `print`, `println`, `input`, `format`, `join`, `len`, `range`, `zip`, `head`, `tail`, `slice`, `concat`, `append`, `extend`, `contains`, `map`, `filter`, `reduce`, `foreach`, `exit`, `sleep`, `error`, `parse_int`, `parse_float`, the conversion functions (`int`, `float`, `string` and so on), and the operations on maps and sets ([3.6](03_data_model.md#36-maps-and-sets)).

| Module | Contents |
|---|---|
| `str` | Strings: by byte, splitting, trimming, case conversion, replacing, searching, taking substrings; by code point, counting, reading, encoding and decoding |
| `array` | Arrays: `reverse`, `take`, `drop`, `unique`, `find`, `sum`, `min`, `max`, `sort_by`, `flat_map` and others (generic functions written in Camel) |
| `math` | The constants `pi`, `e`, `inf`, `nan` (values); elementary functions, rounding, `min`/`max`/`clamp`, `gcd`/`lcm`, base conversion, bit operations such as `bit_and` |
| `random` | The type `Key`; `seed`, `rand`, `randint`, `randn`, `choice`, `sample`, `shuffle`, and the pure keyed `key`, `split`, `permutation` |
| `time` | `now`, `strftime`, `strptime` |
| `fs` | `exists`, `is_file`, `is_dir`, `mkdir`, `mkdirs`, `remove`, `remove_dir`, `read_text`, `try_read_text`, `write_text`, `file_size`, `read_chunk`, `read_bytes`, `write_bytes`, `replace` |
| `json` | the type `Json`; `encode`, `decode`, `decode_as`; `kind`, `is_null`, `get`, `as_bool`, `as_int`, `as_float`, `as_string`, `as_array`, `as_object` |
| `os`, `sys` | `whoami`, `cpu_count`, terminal input; `get_cwd`, `get_env` |
| `re` | `test` |
| `crypto` | `sha256`, `sha256_rounds` |
| `this` | `zen` |
| `algo` | `sort`, `merge_sort`, `merge_sorted_arrays` and other sorting functions (each returns a new array, under one name) |
| `statistics` | `mean`, `stdev`, `variance`, `median`, `quantile` |
| `para` | `unordered_foreach`, `unordered_reduce` |
| `tensor`, `nn`, `autodiff`, `onnx` | Tensors, network layers and optimisers, gradients, ONNX export; the row gather `take`, `one_hot`, and `softmax_cross_entropy` overloaded on the target's type (one-hot rows or integer class labels) |
| `python` | Embedded Python (must be enabled at build time) |

For every function of the modules in the table other than `tensor`, `nn`, `autodiff`, `onnx` and `python`, see the [standard library reference](../../stdlib-reference.md); for the conventions see [5.5](#55-standard-library). Tensor row gathering, class labels and keyed random are in [43_rows_labels_and_keys.md](../../technical/43_rows_labels_and_keys.md); array indexing and slicing are in [design principles 5.4](../../design-principles.en.md).

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

**An exported function can use the `let` and `var` values of the top level of its own module**, directly or through functions of its module that it calls (design principles 10.1):

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
let step = 1000                 // unrelated to counter's step

sync func main(): int sync {
    println(counter::bump(), counter::bump(), counter::current())
    return 0
}
```

```text
2 4 4
```

All modules of a program are built into one `__root__` graph: each value of the top level of a module is a node of that graph, and a function gets it through a capture port bound to that value ("module + name"), not by looking the name up again where the function is called. So a value of the same name in the importing module (`step` above), or a local of the same name at the call, does not take its place. Top-level values are evaluated as in the entry module: before `main` starts, by static dependence, and once even when several modules use them.

**A module can export values** (`let`):

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

An exported value is that very node of the common `__root__` graph: the importing module uses it directly at its top level and through a capture port in its functions; it is neither copied nor evaluated again. An exported value that nothing uses is not evaluated (an effect in its initializer does not run, and in an imported module this gives no warning).

- **A `var` cannot be exported**: `export var count = 0` is `SemanticDiag::ExportedVariable`, located at the variable. Export the functions that read and write it (`counter` above).
- Two imports by name that bring values of one name (or a value and a function) are `SemanticDiag::ImportNameConflict`; values are not overloaded.

## 5.4 Namespaces

`namespace name { ... }` puts a group of declarations under a qualified name. A block can contain `func`, `type`, `let`, `var`, nested `namespace`, `import`, `export` and `using namespace`:

```cml
type Counter = { n: int }

namespace Counter {
    let start = 0
    func new(): Counter { return { n: start } }
    func next(c: Counter): Counter { return { n: c.n + 1 } }
    func twice(c: Counter): Counter { return next(next(c)) }   // names of the block need no prefix
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

- A namespace is not an object: it is a path in a name. An `f` declared in `namespace A { namespace B { ... } }` is `A::B::f`; one namespace can be written in several blocks.
- **Unqualified names in a block**: a name written in a namespace is looked up in that namespace, then in the enclosing namespaces, then at the top level of the module, and the first one found is taken; types too (`namespace Models { type Box = ...; func mk(): Box }`).
- **A namespace can hold values**: `namespace Config { export let limit = 10 }`, used as `Config::limit`. It differs from a top-level `let` only by the path in its name. A `var` can be written, not exported.
- **A module can qualify its own names with its own name**: in `module shapes`, `shapes::area` is the same as `area` (also when the file name is the module name).
- A namespace may share its name with a type, which makes `Type::function` the usual way to write the constructors and helpers of a type (types provided by modules too: `Tensor::ones([2, 2])`).
- A name exported inside a namespace keeps its qualified name: `namespace Tools { export func inc(...) }` is `module::Tools::inc` in other modules, and `import { inc } from module::Tools` imports it.
- Diagnostics, the debugger and the graphs show full names: a function of another module is shown as `module::path::name`; the entry module's own names are shown without the module.

**`using namespace`** lets the names of a namespace be written without their prefix:

```cml
import lib
using namespace lib::Tools          // the whole module may write inc

namespace A {
    using namespace Inner           // only for the declarations of this block (Inner is A::Inner)
}

func f(): int {
    using namespace lib             // only in this function
    return root()
}
```

- What it opens can be an imported module or a namespace of one, or a namespace of this module.
- **It applies to the block it is written in**: at the top level of the module to the whole module, in a namespace block to the declarations of that block, in a function body (or a block of it) only there. Its position in the block does not matter.
- When two `using` bring things of one name that cannot be told apart (functions of the same parameter types, values, types), that is `SemanticDiag::AmbiguousName` **where the name is used**, listing the candidates by their full names, `module::path::name` (design principles 10.2; whatever alias an import gave the module); write one of them.
- **Reopening a namespace block does not carry the `using` of an earlier block of it** (design principles 10.2): a `using` holds only in the block it is written in. Functions of one name with different parameter types form one set of overloads.

**There is one rule of precedence for names**: local names > the module's own names (the namespace written in, the enclosing namespaces, the top level) > names imported by name (including those reached through a `module::` prefix) > names a `using namespace` brings > built-in names. The first layer that has the name is taken; within a layer, functions of one name form a set of overloads; nothing is merged across layers (so the module's own `inc(string)` hides an `inc(int)` a `using` brings). Operators are the exception: their overloads come from every layer.

## 5.5 Standard library

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
- Modules written in Camel can export values ([5.3](#53-exports)); so can C++ modules (design principles 10.1): the constants of `math`, `math::pi`, `math::e`, `math::inf`, `math::nan`, are written without parentheses, and each use is that constant itself, with no node on the root graph and no capture.
- A function passed to a higher-order function of the `array` module is applied with `map`: there is no agreed order between the calls on the elements (a parallel scheduler may execute them at the same time), and the results are arranged by position; for side effects that need an order use the built-in `foreach` and `reduce`.
- `camel inspect --ops <file>` lists all the operators of the built-in modules and of every module the program imports, together with their declared purity.

## 5.6 Loading modules

All modules of a program are built into one `__root__` graph (design principles 10.1), in three steps:

1. **Declare**: starting from the entry, every imported module is found, and each only records what it declares (the signatures of its functions, its top-level values, its types) and fills its export table, without looking at any function body. So modules may import each other in any way. A module's types are recorded before its other declarations, so two modules that import each other can use each other's types (type definitions themselves cannot depend on each other in a ring).
2. **Build**: the function bodies and top-level values of the modules are built in the order of the imports; a function or a value is built when it is first needed, by whichever module needs it. So **the top-level declarations do not depend on the order they are written in**:

   ```cml
   let area = pi * r * r      // pi and r come later
   let pi = 3.14159
   let r = 2.0
   func twice() { return k * 2 }   // no declared return type; may be called before it is written
   let k = 21
   ```

3. **Link**: every top-level value a function uses is passed along the calls up to `__root__`, the call of the entry module's `main` is made, and the summaries of effects are computed, with the warnings that depend on them.

- A top-level value cannot depend on itself, directly or not (through other values, through the functions its initializer calls, through other modules): that is `SemanticDiag::ModuleValueCycle`, located at the reference that closes the ring, with the whole chain in the message: `The value 'a' depends on itself: a (m.cml:1:1) -> b (m.cml:2:1) -> a`. Functions calling each other are not a cycle.
- Types cannot form a cycle across modules either: `type A = { b: B }` in `a` using `B` of `b`, whose `type B = { a: A }` uses `A`, is `SemanticDiag::ModuleTypeCycle` (design principles 10.1), located at the reference that closes the cycle, with the cycle in the message: `cycle_a::A -> cycle_b::B -> cycle_a::A`.
- A function named `main` in an imported module is an ordinary function; only the entry module's `main` is the entry of the program.
- A top-level value a recursive function uses only after it calls itself reaches the recursive call as usual; but a **variable** used only after the recursive call is `SemanticDiag::CaptureAfterRecursiveCall`: that call was already ordered among the reads and writes of the variables known then. Write the first use of the variable before the recursive call, or pass it as a parameter.

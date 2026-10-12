# Map and Set

This note describes the two built-in collection types `Map<K, V>` and `Set<T>`: what was
ruled, what was investigated, what was chosen and why, and what remains for the language
author. The normative text is `docs/design-principles.cn.md` section 2.9, "数据与控制"; the
user-facing description is `docs/language/` 3.2.6 and 6.3.5.

Choices the design principles do not make are collected in section 10. Each is implemented
the way that section recommends and marked in the code with `DESIGN-QUESTION(<tag>)`, so that
a ruling can be applied in one place.

## 1. What they are on the graph

| Construct | On the graph |
|---|---|
| `xs as Map<K, V>`, `xs as Set<T>` | a `CAST` node whose target is the collection type |
| `m[k]`, `get<k>(m)` | `OPER :coll/map_get`, of type `V?` |
| every other operation (`put`, `remove`, `has`, `len`, `keys`, `union`, ...) | one `OPER` node |
| `m1 == m2` | `OPER :coll/eq` |

Nothing here depends on what the collection holds. There is no literal and no new syntax:
`[('a', 1)] as Map<string, int>` is the existing array literal under the existing `as`.

Every operation is a pure operator: it reads its arguments and returns a new value. No
operation changes a map or a set that exists, so two values never alias observably.

## 2. Two routes, and the one taken

"Built-in type" can be implemented in two ways.

**Route 1: a composite type of the core.** A new `TypeCode` beside `Array`/`Tuple`/`Struct`,
a new `CompositeType` class, and every place that switches on a type code learns it. Counted
in the tree at the time of writing: 40 files mention `TypeCode::Tuple` or `TypeCode::Struct`
(the ONNX exporter 25 times, the type-variable algebra 10, `entry_args` 7, the graph encoder
6, the draft session 5, the C++ bridge 4, both FastVM interpreters, the JIT trampoline, the
generic optimiser, the Python module...). Most of those would need a new case that says "not
supported" or "an object reference like the others".

**Route 2: a module type.** An `OtherType` registered by a standard-library module, as
`Tensor` is. The core does not change; the four backends, the collector, the passes and the
exporters meet the type through the path they already have for module types: an opaque
GC-traced reference whose object answers `print`/`equals`/`clone`/`updateRefs` itself.
Costs: the names `Map` and `Set` are visible only after an `import`; a module type could not
take a type variable among its arguments (`Tensor<typeas T>` was rejected where written), and
`Map<typeas K, typeas V>` is a hard requirement; and other modules (JSON) would have to share
C++ classes with a `collections` module across `.cmo` boundaries loaded with `RTLD_LOCAL`.

**Taken: the classes live in the core and use the `OtherType` mechanism.** `MapType` and
`SetType` (`include/camel/core/type/collection.h`) are `OtherType`s registered by the core
itself and exported by the global built-in module, so:

- the names are global, like `int` and `string`: no `import`;
- backends, collector, passes and exporters need no change (verified, section 8);
- the classes are in `libcamel`, so any module can produce and consume the values (JSON does);
- the missing capability is added once and for every such type: an `OtherType` may declare
  that its arguments are plain types (`OtherType::typeArguments()` is true), and then type
  variables are accepted among them and the type-variable algebra (`containsTypeVar`,
  `substituteTypeVars`, `bindTypeVars`, `collectBindableTypeVars`) descends through
  `params()`. `Tensor` does not declare it and keeps its rule.

What route 1 would add over this: a fixed `TypeCode::Map` constant instead of a code
registered at start-up (nothing in the tree needs it; the code is obtained from
`MapType::typeCode()`), and a `switch` case available to code that wants to treat maps
specially. If the author prefers route 1, the type and object classes, the operators and all
tests stay as they are; what changes is the base class, the `TypeCode` enumeration, and one
case in each of the files counted above.

## 3. The types

`Map<K, V>` and `Set<T>`. Two arguments and one, always written. `Map` without arguments is
the prototype the type name resolves to; it is not the type of any value, and a position
written `Map` or `Set` alone is an `InvalidTypeArgument` error.

- **Equality** is structural (`K` and `V` equal). Concrete instances are interned by their
  mangled name; instances that still hold a type variable are not (a binding occurrence and a
  use of one variable mangle alike).
- **Assignability**: `Map<K, V>` from `Map<K, V>` only. No variance: `Map<string, int>` is
  not a `Map<string, int?>`.
- **Generics**: `func get_or(m: Map<typeas K, typeas V>, k: K, d: V): V` binds `K` and `V`
  from the argument's type and instantiates per pair, like any generic function. A key type
  that becomes inadmissible when a variable is replaced (`func f(x: typeas T): Set<T>` called
  with a float) is reported at the call (`CollectionKeyNotAllowed`), where the repeated-mark
  check of `T??` already runs.

### 3.1 Key types (`DESIGN-QUESTION(map-key-types)`)

A key (and a set element) must have a total order and an equality that cannot change:

| Type | Key? | Why |
|---|---|---|
| `int`, `i32`, `bool`, `string` | yes | total order; a string is compared by bytes, as its length and index are |
| a tuple of key types | yes | compared element by element |
| `float`, `f32` | no | `NaN != NaN` (a key that cannot be found again), `0.0 == -0.0` with different bits |
| `T[]` | no | an array can be changed in place through a `var` (`append`), so a key could change after it was inserted |
| struct | no | admissible in principle (field by field in declaration order); left out until asked for |
| function, `Tensor`, other module types, `any` | no | no equality the language defines |
| `T?`, `T!` | no | `==` is not defined for them (note 29, Q5) |
| `Map`, `Set` | no | admissible in principle (they are immutable and ordered); left out until asked for |

A type that is not allowed is `SemanticDiag::CollectionKeyNotAllowed` (0xA0) at the type
expression, with the reason. The value type of a map is unrestricted.

## 4. Order (`DESIGN-QUESTION(map-order)`)

**A map is ordered by its keys, a set by its elements.** `keys`, `values`, `items`, printing
and equality all follow that order: integers numerically, `false` before `true`, strings by
bytes, tuples lexicographically.

The order must not depend on the platform or on a hash function: tests, differential checks
of the four backends and the correctness of passes all compare output. That leaves insertion
order and key order.

| | Key order (chosen) | Insertion order |
|---|---|---|
| A value is determined by | its entries | its entries and their history |
| `m1 == m2` implies `items(m1) == items(m2)`, same print | yes | no |
| A pass may replace `put<k2,v2>(put<k1,v1>(m))` by the other order (`k1 != k2`) | yes | no |
| Needs a hash of the key | no | yes (and a hash of an object must not be its address: objects move) |
| Persistent structure | a balanced tree, or a sorted array | a hash trie *and* a persistent sequence with tombstones |
| JSON object decoded and encoded again | keys come out sorted | order kept |

Under key order a collection is a value in the mathematical sense, which is what a pure
operator on a graph wants: equal inputs, indistinguishable outputs.

## 5. Representation

### 5.1 The object

One class, `rtdata::Table` (`include/camel/core/rtdata/collection.h`), for both types:

```
Table { uint32 size; slot_t data[] }      one allocation
  Set<T>:    data[0 .. size)        elements, ascending
  Map<K,V>:  data[0 .. size)        keys, ascending
             data[size .. 2*size)   values, in the order of their keys
```

Whether a table is a map or a set, and what its slots hold, is known from the static type
only, like every Camel object.

- **Tracing.** `Table::updateRefs(relocate, type)` walks the key slots with `K` and the value
  slots with `V`, relocating those whose type is GC-traced: the contract of `Tuple` and
  `FixedArray` (note 23). Keys and values are ordinary Camel objects that the collector
  reaches through the table.
- **Nothing that hinders moving.** The object has no interior pointer (the slots follow the
  header), nothing derives from an address (lookup is by comparing keys, not by hashing), and
  no operator keeps a pointer across a call. Stores into a new table go through
  `mm::writeBarrier` with the table's type.
- **An empty collection is a table of size 0**, not the null slot: a null `Map` would print
  as `null`.

### 5.2 Persistence (`DESIGN-QUESTION(map-persistence)`)

Operations return new values. The implementation is the plain one: **a sorted array, copied
by every `put`**.

| | Sorted array (implemented) | Persistent balanced tree |
|---|---|---|
| `m[k]`, `has` | O(log n), no allocation | O(log n) |
| `put`, `remove`, `add` | O(n): one allocation, two `memcpy` | O(log n) allocations of a node each |
| `as` from an array of n | O(n log n) (O(n) when already sorted) | O(n log n) |
| `merge`, `union`, `intersect`, `difference`, `==` | O(n + m), one pass | O(n + m) through flattening |
| `keys`, `values`, `items` | O(n), one `memcpy` | O(n) tree walk |
| Memory per entry | 16 bytes (map), 8 (set) | about 56 bytes and one object for the collector to visit |

Nothing is mutated in place and passed off as a new value. Reusing the old table when
nothing else holds it is an optimisation pass's business (it needs to prove that), not this
type's.

The measured cost and the point at which the tree is needed are in section 9. The choice is
invisible to programs: the interface is the operators of section 6, and only
`src/core/rtdata/collection.cpp` knows the layout.

## 6. Construction and operations

### 6.1 Construction

```camel
let m = [('a', 1), ('b', 2)] as Map<string, int>
let s = [3, 1, 2] as Set<int>
let e = [] as Map<string, int>          // empty
let r = pairs(n) as Map<int, string>    // from an array computed at run time
```

`as` accepts `(K, V)[]` for `Map<K, V>` and `T[]` for `Set<T>` (element types equal: no
implicit numeric conversion), and the type of the empty literal `[]`. A literal is first
given the element type the target asks for by the rule for annotated literals, so
`[('a', 1)] as Map<string, float>` holds `1.0`.

**Duplicate keys (`DESIGN-QUESTION(map-duplicate-keys)`).**

- In an array *literal* written under `as Map<...>`, two equal constant keys are
  `SemanticDiag::DuplicateCollectionKey` (0xA1): it is a slip of the pen. The check is on the
  source text, so an optimisation level cannot change it.
- In an array known only at run time, **the later entry wins**: `xs as Map<K, V>` is the fold
  of `put` over `xs`. A fault would make every `map`-then-`as` pipeline partial.
- A set takes duplicates silently in both cases: removing them is what a set is for.

The conversion runs when the program runs (a `CAST` node), also for a literal. Completing it
at compile time needs a compile-time representation of a table (`core/data`) and its encoding
into a graph's static area; `std::opt::fold` leaves a `CAST` to a module type alone. Not
done; see section 11.

### 6.2 Operations

The calling form follows the built-in functions that exist (`contains<v>(xs)`,
`slice<a, b>(xs)`, `map<f>(xs)`, `concat(a, b)`): the collection that flows through is the
ordinary argument, what configures the operation is a `with` argument, and an operation on
two collections of equal standing takes both as ordinary arguments
(`DESIGN-QUESTION(map-call-form)`).

| Operation | Type | Notes |
|---|---|---|
| `m[k]`, `get<k>(m)` | `V?` | `null` when absent |
| `get_or<k, d>(m)` | `V` | `d` when absent; no box is allocated |
| `put<k, v>(m)` | `Map<K, V>` | replaces the value of an existing key |
| `remove<k>(m)` | `Map<K, V>` | the same entries when absent |
| `has<k>(m)` | `bool` | |
| `len(m)` | `int` | |
| `keys(m)` | `K[]` | ascending; a new array every time |
| `values(m)` | `V[]` | in the order of their keys |
| `items(m)` | `(K, V)[]` | |
| `merge(m1, m2)` | `Map<K, V>` | entries of both; `m2` wins on a common key |
| `m1 == m2`, `m1 != m2` | `bool` | same keys and equal values |
| `contains<x>(s)`, `has<x>(s)` | `bool` | |
| `add<x>(s)` | `Set<T>` | |
| `remove<x>(s)` | `Set<T>` | |
| `len(s)` | `int` | |
| `items(s)` | `T[]` | ascending |
| `union(a, b)`, `intersect(a, b)`, `difference(a, b)` | `Set<T>` | |
| `is_subset(a, b)` | `bool` | every element of `a` is in `b` |
| `s1 == s2`, `s1 != s2` | `bool` | |

All are declared pure.

**`map_values` and `filter` are not operators.** An operator that calls a function value is
a "marked" operator that each backend implements itself (`:mark/map_arr` in NVM, FVM, JIT,
PRL). They are compositions of what exists, visible to passes:

```camel
func map_values(m: Map<typeas K, typeas V>, f: (v: V) => typeas U): Map<K, U> {
    return items(m)->map<(kv: (K, V)): (K, U) => (kv.0, f(kv.1))> as Map<K, U>
}
```

A generic function cannot yet be exported from a module (note 28, section 5), so the library
cannot ship these two; a program writes them as above. This is the first thing to add once
generic functions can be imported.

### 6.3 Subscript

`m[k]` and `xs[i]` are both the operator group `__idx__`; the overload is chosen by the type
of the left operand, as every overload is. `xs[i]` has type `T` and faults out of range;
`m[k]` has type `V?` and never faults. The result type is on the node, and the node names
the operator (`:op/idx_arr`, `:coll/map_get`), so both the type checker and the graph tell
them apart. A set has no subscript (`s[x]` is `NoMatchingFunction`).

### 6.4 A map whose values may be null (`DESIGN-QUESTION(map-nullable-value)`)

For `m: Map<string, int?>` the type of `m[k]` would be `int??`, which the language rejects
(note 29, 4.3). Implemented: **the result type is `V` itself when `V` is already nullable**
(`int?`), and an absent key and a key bound to `null` both give `null`; `has<k>(m)` tells
them apart. This is the reason note 29 gives for rejecting `T??`: `??` could not tell the
two nulls apart.

### 6.5 Equality and printing (`DESIGN-QUESTION(map-equality-print)`)

`==` compares sizes, keys in order, and values with the equality of their type (objects
structurally; floats numerically, so a map holding `NaN` is not equal to itself, as `NaN`
is not).

`println`, `string()` and `format` print through `Object::print`:

```
{a: 1, b: 2}       a map       {}   an empty one
{1, 2, 3}          a set       {}   an empty one
```

Strings are not quoted, as in an array or a struct.

## 7. JSON (`DESIGN-QUESTION(json-typed-decode)`)

`json::decode` is unchanged (`any`). The typed entry is the existing conversion syntax on a
new operator:

```camel
let r = json::decode_as<[] as Map<string, int>>(text)      // sketch; see below
```

See section 7 of the implementation for the form actually adopted (it is recorded here once
implemented, with its alternatives).

## 8. The subsystems

- **Backends.** No backend was changed. The values are GC-traced references of a module
  type; the operators are ordinary operators; the conversion is the `CAST` every backend
  already executes through `Type::castSlotFrom`.
- **Collector.** Section 5.1. Verified with heap verification and allocation stress.
- **Passes.** The operators are pure operators without a registered folder, so
  `std::opt::fold` leaves them and CSE/DCE treat them like any pure operator.
- **`std::cpp`, `onnx::export`, autodiff.** Each refuses the graph at the node, with its
  position.

## 9. Measurements

Recorded when the implementation was measured; see the end of this file.

## 10. Questions for the language author

See the list at the end of this file.

## 11. Not done

See the end of this file.

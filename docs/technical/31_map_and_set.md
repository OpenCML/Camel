# Map and Set

This note describes the two built-in collection types `Map<K, V>` and `Set<T>`: what was
ruled, what was investigated, what was chosen and why, what was measured, and what remains
for the language author. The normative text is `docs/design-principles.cn.md` section 2.9,
"数据与控制"; the user-facing description is `docs/language/` 3.2.6 and 6.3.2.

Choices the design principles do not make are collected in section 10. Each is implemented
the way that section recommends and marked in the code with `DESIGN-QUESTION(<tag>)`, so that
a ruling can be applied in one place.

## 1. What they are on the graph

| Construct | On the graph |
|---|---|
| `xs as Map<K, V>`, `xs as Set<T>` | a `CAST` node whose target is the collection type |
| `m[k]` | `OPER :coll/map_idx`, of type `V?` |
| every other operation (`put`, `remove`, `has`, `len`, `keys`, `union`, ...) | one `OPER :coll/...` node |
| `m1 == m2` | `OPER :coll/eq` |

Nothing here depends on what the collection holds. There is no literal and no new syntax:
`[('a', 1)] as Map<string, int>` is the existing array literal under the existing `as`.

Every operation is a pure operator: it reads its arguments and returns a new value. No
operation changes a map or a set that exists, so two values never alias observably.

`let v = m[k] ?? 0` (`camel x.cml std::rgir`, ports and edges left out):

```
OPER :coll/map_idx      : int?    <- m, k
OPER :op/tag_is_null    : bool    <- OPER :coll/map_idx
BRCH                              <- OPER :op/tag_is_null
FUNC arm0  (body: 0)              <- [ctrl] BRCH
FUNC arm1  (body: :op/tag_null_value of the lookup)
JOIN                    : int
```

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
itself and named in the built-in type scope beside `int` and `string`, so:

- the names are global: no `import`;
- backends, collector, passes and exporters needed no change (verified, section 8);
- the classes are in `libcamel`, so any module can produce and consume the values (JSON does);
- the missing capability is added once and for every such type: an `OtherType` may declare
  that its arguments are plain types (`OtherType::typeArguments()` is true), and then type
  variables are accepted among them and the type-variable algebra (`containsTypeVar`,
  `substituteTypeVars`, `bindTypeVars`, `collectBindableTypeVars`, `findRepeatedMark`)
  descends through `params()`. `Tensor` does not declare it and keeps its rule.

What route 1 would add over this: a fixed `TypeCode::Map` constant instead of a code
registered at start-up (nothing in the tree needs it; the code is obtained from
`MapType::typeCode()`), and a `switch` case available to code that wants to treat maps
specially. If the author prefers route 1, the type and object classes, the operators and all
tests stay as they are; what changes is the base class, the `TypeCode` enumeration, and one
case in each of the files counted above.

**Existing programs.** A program that declares its own type `Map` or `Set`, or imports one,
keeps its meaning: its declaration replaces the built-in name in that module. A function the
program declares under the name of an operator (`keys`, `put`, `add`, ...) hides the
operator, as it hides any global operator.

## 3. The types

`Map<K, V>` and `Set<T>`. Two arguments and one, always written. `Map` without arguments is
the prototype the type name resolves to; it is not the type of any value, and a position
written `Map` or `Set` alone is an `InvalidTypeArgument` error.

- **Equality** is structural (`K` and `V` equal). Concrete instances are interned by their
  mangled name; instances that still hold a type variable are not (a binding occurrence and a
  use of one variable mangle alike).
- **Assignability**: `Map<K, V>` from `Map<K, V>` only. No variance: `Map<string, int>` is
  not a `Map<string, int?>`.
- **`any`**: a collection converts to `any` like every value; `any as Map<...>` is refused
  (nothing at run time could check it).
- **Generics**: `func get_or_else(m: Map<typeas K, typeas V>, k: K, d: V): V` binds `K` and
  `V` from the argument's type and instantiates per pair, like any generic function. A key
  type that becomes inadmissible when a variable is replaced (`func f(x: typeas T): Set<T>`
  called with a float) is reported at the call, where the repeated-mark check of `T??`
  already runs, and at a conversion written in the body (`xs as Set<T>`).

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
expression (or at the call that produces it by substitution), with the reason. The value
type of a map is unrestricted.

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
- **No allocation in the permanent space at run time.** The array, tuple and `V?` types an
  operator needs (`K[]`, `V[]`, `(K, V)[]`, `V?`) are made once, when the collection type is
  (note 23: "a question must not allocate").

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
type's. An operation that changes nothing (`remove` of an absent key, `add` of a present
element, `merge` with an empty map) returns its argument, which is the same value.

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

`as` accepts `(K, V)[]` for `Map<K, V>` and `T[]` for `Set<T>`, element types equal: no
implicit conversion, so `[('a', 1)] as Map<string, float>` is `DynamicCastForbidden` and is
written `1.0`. The empty literal `[]` has no element type and converts to every collection
type; because a `var` of that type may have been appended to, the conversion checks that the
array is empty when it runs (a fault, `RuntimeDiag::InvalidArgument`, otherwise).

The conversion is the only way in: an annotated declaration (`let m: Map<string, int> =
[...]`) is not a conversion and is an error, as the principles ask for `as`.

**Duplicate keys (`DESIGN-QUESTION(map-duplicate-keys)`).**

- In an array *literal* written under `as Map<...>`, two equal constant keys are
  `SemanticDiag::DuplicateCollectionKey` (0xA1): it is a slip of the pen. The check is on the
  source text, so an optimisation level cannot change it.
- In an array known only at run time, **the later entry wins**: `xs as Map<K, V>` is the fold
  of `put` over `xs`. A fault would make every `map`-then-`as` pipeline partial.
- A set takes duplicates silently in both cases: removing them is what a set is for.

The conversion runs when the program runs (a `CAST` node), also for a literal; see section
11.

### 6.2 Operations

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

All are declared pure (`registerBuiltinOperatorTraits`, the `coll/` family). Key and value
arguments must have exactly the key and value type.

**Calling form (`DESIGN-QUESTION(map-call-form)`).** The built-in functions that exist put
the collection that flows through as the ordinary argument and what configures the operation
as a `with` argument (`contains<v>(xs)`, `slice<a, b>(xs)`, `map<f>(xs)`), which also gives
the chained form `m->put<k, v>->remove<k2>`; an operation on two collections of equal
standing takes both as ordinary arguments (`concat(a, b)`). The operators here follow that.
The sketch in the principles writes `put(m, 'c', 3)`, and the grammar mis-parses two
`with`-form calls in one argument list (`f(has<a>(m), has<b>(m))` is read as comparisons;
found here, not caused by this work). So **a configuring argument is accepted in both
positions**: `put<k, v>(m)`, `m->put<k, v>` and `put(m, k, v)` are one operator node.

**`map_values` and `filter` are not operators.** An operator that calls a function value is
a "marked" operator that each backend implements itself (`:mark/map_arr` in NVM, FVM, JIT,
PRL). They are compositions of what exists, visible to passes:

```camel
func map_values(m: Map<typeas K, typeas V>, f: (v: V) => typeas U): Map<K, U> {
    return items(m)->map<(kv: (K, V)): (K, U) => (kv.0, f(kv.1))> as Map<K, U>
}

func filter_keys(m: Map<typeas K, typeas V>, f: (k: K) => bool): Map<K, V> {
    return items(m)->filter<(kv: (K, V)): bool => f(kv.0)> as Map<K, V>
}
```

A generic function cannot yet be exported from a module (note 28, section 5), so the library
cannot ship these; a program writes them as above (they are part of the tests). This is the
first thing to add once generic functions can be imported. The same holds for a `for`: there
is no loop syntax, so a program walks `keys(m)` or `items(m)` with `map`/`foreach`/`reduce`.

### 6.3 Subscript (`DESIGN-QUESTION(map-subscript)`)

`m[k]` and `xs[i]` are both the operator group `__idx__`; the overload is chosen by the type
of the left operand, as every overload is. `xs[i]` has type `T` and faults out of range;
`m[k]` has type `V?` and never faults. The result type is on the node, and the node names
the operator (`:op/idx_arr`, `:coll/map_idx`), so both the type checker and the graph tell
them apart; using `m[k]` as a `V` is the ordinary error for an unopened `V?`. A set has no
subscript (`s[x]` is `NoMatchingFunction`).

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

`json::decode` is unchanged (`any`). The typed entry is `json::decode_as`:

```camel
let r = json::decode_as<[] as Map<string, int>>('{"b": 2, "a": 1}')    // Map<string, int>!
let m = r !! ([] as Map<string, int>)
let xs = json::decode_as(text, [0])                                    // int[]!
```

A call cannot be handed a type (whether type arguments may be written at a call is undecided,
principles section 6), so the type is that of a **shape** argument whose value is not read.
The result is a `T!`: text that is not JSON is an error value of kind `JsonParseError`, JSON
that does not have the shape of `T` one of kind `JsonTypeError` whose message names the path
(`at $.b: expected an integer, found a string`). Neither is a fault.

| Target | JSON |
|---|---|
| `bool`, `int`, `i32`, `float`, `string` | the scalar of that kind (a number without a fraction is accepted for a float; nothing else is converted) |
| `T[]` | an array of `T` |
| `Set<T>` | an array of `T`; duplicates are dropped |
| `Map<string, T>` | an object whose values are `T` |
| `(A, B, ...)` | an array of exactly that length |
| `{ f: A, g: B }` | an object with exactly those members |
| `T?` | `null`, or a `T` |

A shape whose type has no JSON form (a function, `any`, a map with another key type) is
rejected when the call is compiled. `json::encode` writes a `Map<string, T>` as an object
and a `Set<T>` as an array, in key order.

Alternatives, for the author:

1. *Type arguments at the call*, `json::decode<Map<string, int>>(text)`, once the language
   decides whether they may be written. The cleanest form; the operator then needs a way to
   read its result type, which operators do not have today.
2. *`as` with a run-time check*, `json::parse(text) as Map<string, int>!`, over a
   self-describing `Json` value type. It needs a conversion that the *source* type defines
   (`Type::castSlotFrom` is asked of the target, and the targets here are core types that
   know nothing of JSON): a hook in the core and in the rule that admits a `CAST`.
3. *The shape argument* (implemented): needs nothing new.

## 8. The subsystems

- **Backends.** No backend was changed. The values are GC-traced references of a module
  type; the operators are ordinary operators; the conversion is the `CAST` every backend
  already executes through `Type::castSlotFrom`. The test programs give the same output on
  `std::nvm`, `std::fvm`, `std::jit` and `std::prl`.
- **Collector.** Section 5.1. A program that builds maps and sets entry by entry out of
  strings, arrays, tuples, `T?` boxes and other maps runs under `CAMEL_GC_VERIFY=both` with a
  collection requested every third allocation (`CAMEL_GC_STRESS_ALLOC=3`,
  `CAMEL_GC_STRESS_SAFEPOINT=1`), modes `major`, `minor` and `both`, with the non-moving
  collector and with `CAMEL_GC_ENABLE_YOUNG_COPYING=1`, on the four backends: the output is
  that of an undisturbed run and every verification is `ok`. Under the moving collector one
  such run relocated 864 map tables, 344 set tables and 1608 strings (`CAMEL_GC_LOG_MOVES=1`).
- **Passes.** The operators are pure operators without a registered folder, and a `CAST` to
  a module type is not folded, so `std::opt::fold` leaves them and CSE/DCE treat them like
  any pure operator. The test programs print the same through `std::opt`, `std::inline`,
  `std::macro std::opt`, `std::inline std::opt`, `std::opt::fold std::opt::cse std::opt::dce`
  and `std::opt::simplify`.
- **`std::cpp`, `onnx::export`.** Each refuses the graph with `TranslationRefused` and a
  position (at the array the collection is built from, which they do not translate either).
  `onnx::export` evaluates what does not depend on the model's input, so a program whose
  collections are all constant still exports as the constant model it is.
- **Autodiff.** A collection has no tangent. Left alone, activity analysis ended at one and
  what was read back out of it was taken for a constant: `grad` of `x * m[1]` with
  `m = [(1, x)]` returned `x` instead of `2x`, without a word. Now the node that puts a value
  depending on a differentiated parameter into a `Map` or `Set` of differentiable values is
  refused with its position (`Backward::refuseActiveCollection`). A collection that does not
  depend on a parameter is a constant like any other and is no obstacle.

## 9. Measurements

Release build, one run each, on a shared and busy 18-core container (so: orders of
magnitude, not benchmarks). Wall time of the whole `camel` process and its peak resident
memory; an empty program takes 0.02 s and 12.7 MB. `std::fvm` unless stated.

| Program | n | Time | Peak memory |
|---|---|---|---|
| `range(0, n)->map<...> as Map<int, int>` (keys in scrambled order), one lookup | 100,000 | 0.09 s | 24.5 MB |
| the same | 1,000,000 | 1.96 s | 182 MB |
| the same on `std::nvm` / `std::jit` / `std::prl` | 100,000 | 0.08 / 0.09 / 0.34 s | 24.5 / 24.6 / 55.3 MB |
| `Map<string, int>` of n string keys, `keys(m) as Set<string>`, `union`, `merge`, `==` | 100,000 | 0.36 s | 62 MB |
| n lookups (`get_or`, half of them misses) in a map of n | 100,000 | 0.05 s | 24.6 MB |
| the same | 1,000,000 | 0.60 s | 182 MB |
| n times `put` in a tail-recursive loop (the previous map becomes garbage) | 1,000 | 0.02 s | 21 MB |
| the same | 10,000 | 0.10 s | 83 MB |
| the same | 30,000 | 0.51 s | 121 MB |
| the same | 100,000 | 7.0 s | 172 MB |
| the same | 300,000 | 112 s | 253 MB |

Reading:

- Everything except entry-by-entry construction is linear or better and is comfortable at
  10^5 to 10^6 entries. A map of 100,000 integer entries is one object of 1.6 MB.
- Entry-by-entry construction is quadratic, as expected: it copies n²/2 entries. It is below
  a second up to about 30,000 entries, 7 s at 100,000 and unusable beyond. Memory stays
  bounded (the collector reclaims the dropped tables; the peak is a few live tables plus the
  large-object budget).
- **When the tree is needed:** when programs build or update maps of more than a few tens of
  thousands of entries one `put` at a time, which is what a `reduce` over `put` or a loop
  (once loops exist) does. Until then the fix on the program's side is to build the entries
  as an array and convert once (`as`), which is the 0.09 s row.

## 10. Questions for the language author

Each is implemented as recommended here; the tag is in the code.

- **`map-key-types`.** Keys: `int`, `i32`, `bool`, `string`, tuples of those. `Map<float,
  int>` is `CollectionKeyNotAllowed`. Candidates: also structs; also `Map`/`Set` themselves;
  floats with a total order (IEEE `totalOrder`), which makes `m[0.0]` and `m[-0.0]` differ.
- **`map-order`.** Key order. `println([('b', 1), ('a', 2)] as Map<string, int>)` prints
  `{a: 2, b: 1}`. Candidate: insertion order (table in section 4).
- **`map-duplicate-keys`.** `[('a', 1), ('a', 2)] as Map<string, int>` is a compile-time
  error; `pairs() as Map<...>` with equal keys keeps the later. Candidates: a fault at run
  time; the first entry kept; an error for a set literal too.
- **`map-equality-print`.** `m1 == m2` compares entries; print is `{a: 1, b: 2}` / `{1, 2}`.
  Candidates: no `==` (as for arrays today); quoted strings; a distinct empty map (`{:}`).
- **`map-subscript`.** `m['a']` is an `int?`; `xs[0]` is an `int`. Candidate: no subscript
  for maps, only `get`.
- **`map-nullable-value`.** For `Map<string, int?>`, `m[k]` is `int?` and does not tell an
  absent key from a null value (`has` does). Candidates: reject `m[k]` and `get` for such a
  map and require `has` + `get_or`; reject nullable value types altogether.
- **`map-call-form`.** `put<k, v>(m)`, `m->put<k, v>` and `put(m, k, v)` are all accepted.
  Candidates: only the `with` form (consistent with `contains<v>(xs)`); only the plain form
  (the sketch in the principles). Related, not decided here: `f(a<b>(x), c<d>(y))` is parsed
  as comparisons.
- **`map-persistence`.** Sorted array, O(n) `put` (section 9). Candidate: a persistent
  balanced tree behind the same operators.
- **`json-typed-decode`.** `json::decode_as<shape>(text): T!` (section 7, with the two
  alternatives).
- **Not tagged, the same kind of question:** should `let m: Map<string, int> = [...]`
  convert, as `let x: int? = 5` does? (Implemented: no; `as` is required.) Should a
  user-declared function named `keys` or `put` hide the operator of that name, as it does for
  every global operator today?

## 11. Not done

- **No compile-time construction.** `[...] as Map<...>` of a literal is checked at compile
  time (key type, duplicate keys) but built by the `CAST` each time it is executed. Building
  it once needs a compile-time form of a table (`core/data`) and its encoding into a graph's
  static area, or folding of a `CAST` to a module type in `std::opt::fold`; neither exists.
- **`map_values`, `filter` and friends are not in the library** (section 6.2): generic
  functions cannot be exported from a module.
- **No persistent tree** (sections 5.2 and 9).
- **`main` cannot take or return a collection on the command line** (no entry-argument
  loader or result saver is registered for the two types).
- **`std::cpp`, ONNX:** collections are refused, not translated. The Python bridge was not
  tried with a collection.
- **Not verified:** the Windows and macOS builds (the work was built and tested on Linux
  only); programs that hold more than 2^32 entries (the size field is 32 bits, as in `Tuple`).

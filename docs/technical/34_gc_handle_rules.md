# Handle Rules: What C++ Code May Do With Camel Objects

This is the contract between the collector and every piece of C++ that touches run-time values:
operators of the modules under `modules/`, the built-in operators, the schedulers, the macro
evaluator and the passes. It will become part of the documentation of the public module
interface. It implements the ruling of `docs/design-principles.cn.md` 2.9: Camel-native objects
may be managed by a generational collector and may move; foreign memory is owned by a
Camel-native object and stays outside the movable heap; **a module does not keep a raw pointer
to a Camel object across calls.**

The rules hold for both collectors (`--gc mark-sweep`, the default, and `--gc generational`).
Under mark-sweep an object keeps its address, so a violation of rules 2, 3 and 6 goes unnoticed
until the object is freed; under the generational collector it reads moved memory. Write code
for the generational collector.

Section 7 is the audit of the repository against these rules, section 8 the tools that find
violations.

## 1. Terms

**Object.** An `rtdata::Object` in the auto space (`mm::autoSpace()`): arrays, tuples, structs,
strings, function values, maps and sets, boxes of `T?` and `T!`, `var` cells, tensors and the
other module object types, foreign-resource wrappers. Graphs, types and frames are not objects
in this sense: they live in spaces that never move or collect them (see
`23_gc_runtime_lifetime.md`).

**Reference.** The address of an object: an `Object *` (or `Array *`, `String *`, ...), a
`slot_t` whose static type is GC-traced, and also every pointer *into* an object: `data()` of an
array or tensor, `view()` / `c_str()` of a string, a `slot_t *` to an element.

**Safepoint.** A point at which the collector may run. Objects are freed and moved at
safepoints and nowhere else. A safepoint is reached by exactly these:

| Reached by | Where |
|---|---|
| A call of a Camel function | `std::nvm`: entry of every graph. `std::fvm`, `std::jit` (interpreter): every `CALL`, `FUNC`, `TAIL`. `std::tf` / `std::prl`: whenever a worker is outside a region (below) |
| A higher-order operator calling its element function | the same, once per element: `map`, `apply`, `filter`, `reduce`, `foreach` and everything built on them |
| The macro evaluator | before every static operator, at every function entry, at every node (`src/execute/macro/eval.cpp`) |
| A pass boundary | before and after every pass (`src/execute/pass.cpp`) |
| `autoSpace().safepoint()`, `minorGC()`, `majorGC()` | explicit calls |
| Another worker of the parallel scheduler | while this worker is outside every `TaskflowGcRegion` |

**Not a safepoint:**

- **Allocation.** `autoSpace().alloc`, and therefore `Array::create`, `String::from`,
  `Tuple::create`, `clone`, `append` and every other constructor, never collects and never moves
  anything. It can only *ask* for a collection at the next safepoint. This is an invariant of
  the allocator, checked in release builds: a collection that starts outside a safepoint throws
  `GC invariant violated`. An object that has just been allocated and is not yet stored anywhere
  is safe until the next safepoint.
- **A write barrier, a typed store, a read.**
- **An operator call.** An operator (`slot_t op(ArgsView &with, ArgsView &norm, Context &)`)
  runs from entry to return without a safepoint, unless it does one of the things in the table
  itself (rule 5). Under the parallel scheduler an operator runs inside a region.
- **JIT-compiled code.** It neither calls out nor polls; a safepoint reached while compiled code
  is on the stack is refused (`GC invariant violated: ... JIT-compiled code is on the stack`).

**A call**, in "across calls": one invocation of an operator, one node of the macro evaluator,
one step of a pass.

## 2. The rules

1. **Between two safepoints, anything goes.** Read references from the arguments, follow them,
   keep them in C++ locals, in `std::vector`s, in lambdas you call before returning, take
   `data()` and `view()` pointers, allocate, build results with plain stores into objects you
   created in this call.

2. **Nothing survives a safepoint except through a root.** After a safepoint every reference
   obtained before it is invalid: the object may have moved or, if nothing else kept it, died.
   Re-read it from where the collector keeps it up to date: the frame (`ArgsView`,
   `Frame::get`), a `mm::RootHandle`, a registered root tracer. This includes interior pointers:
   `arr->data()` is as stale as `arr`.

3. **Nothing survives the call.** An operator must not keep a reference after it returns: not in
   a `static`, a global, a `thread_local`, a member of a module-state object, a cache keyed or
   valued by a reference, nor in a `std::function` or lambda that is stored for later. What must
   outlive the call is either returned to the program (then the program's frames keep it) or
   held in a `mm::RootHandle` (rule 7).

4. **An address is not an identity.** Do not use the address of an object as a key of a
   container that outlives a safepoint, as a hash, as an ordering, or as a name in something
   written out. Identity that must survive is the content, or an id the object carries
   (`ForeignResourceControlBlock::debugId()`).

5. **An operator that reaches safepoints says so by holding roots.** The only operators that
   reach a safepoint are those that run Camel code, a pass or the macro evaluator themselves.
   They must hold every reference they use afterwards in a `RootHandle` and re-read through it.
   When that is not practical because a large body of code works on raw slots (the ONNX exporter,
   which expands macros over the function it translates), the operator opens a
   `mm::NoCollectionScope` instead: safepoints inside it collect nothing. The scope bounds
   nothing, so it is only for work whose allocation is bounded by its input, never around the
   execution of a program, and it is not a substitute for a handle.

6. **A store of a reference into an object that existed before this call goes through a typed
   store.** `Array::set(i, v, arrayType)`, `append(v, arrayType)`, `Tuple::set(i, v, tupleType)`,
   `Struct::set(i, v, structType)`, `Frame::set`, or `mm::writeBarrier(owner, ownerType, value,
   valueType)` before a raw store. The collector must learn that an old object now points at a
   young one, and objects carry no type of their own: the barrier needs the owner's static
   type. Plain stores (`data()[i] = v`, `set<slot_t>(i, v)` without a type, `memcpy`,
   `std::copy`, a sort over `data()`) are allowed for scalars, for permuting the references an
   object already holds, and into an object created earlier in the same call (the collector
   tracks objects born since the last collection and scans them: see section 5). A module
   object type that holds a reference to another object in a field of its own calls
   `mm::writeBarrier(this, nullptr, target, targetType)` when it sets the field after
   construction.

7. **Keeping an object: `mm::RootHandle`.** While the handle exists the object is alive, and
   after every safepoint `handle.get()` is where the object is now.
   `include/camel/core/mm/root_handle.h` states the cost (one lock on creation and on
   destruction, no allocation) and the restrictions (not copyable, not movable: keep several in
   a `std::deque<RootHandle>` or behind `std::unique_ptr`). The handle carries the static type
   of the object, which the collector needs to trace what the object refers to; pass `nullptr`
   only for object types that describe themselves (`Function`, tensors, foreign wrappers).
   `ForeignRootedHandle` is the same thing with accounting; `ForeignPinnedHandle` additionally
   refuses an object whose address is not stable, for native code that needs the address itself.

8. **Foreign memory is owned by a wrapper, not held by address.** Memory and handles that come
   from outside (a `PyObject *`, a file, a buffer of another library) are owned by a
   `rtdata::ForeignResourceObject` created with a `ForeignResourceDescriptor` (`kind`,
   `deleter`, optional `clone`, `trace`, `onMoved`). The wrapper is an ordinary object: it
   moves, the resource does not. Never hand the address of auto-space memory to foreign code
   that keeps it after the call returns: copy (as the NumPy, DLPack and Python bridges do), or
   pin with `ForeignPinnedHandle`. The tensor module keeps its buffers inline in the tensor
   object; kernels hold pointers into them only while the calling operator is blocked.

9. **Callbacks of the collector.** They run inside a collection or right after it; none of them
   may reach a safepoint, run Camel code, or create or destroy a `RootHandle`.

   | Callback | Runs | Thread | May allocate objects | May take foreign locks |
   |---|---|---|---|---|
   | `Object::updateRefs`, descriptor `trace` | inside the collection, allocator locked | the one that reached the safepoint | no (deadlock) | no |
   | `Object::onMoved`, descriptor `onMoved` | inside the collection, on the copy, right after the bytes were copied | same | no | no |
   | `Object::finalize` | inside the collection, on a dead object, before its memory is reused | same | no | no |
   | descriptor `deleter` | at the end of the safepoint that found the wrapper dead, **after** the allocator is unlocked | same | allowed, not advised | yes (the Python deleter takes the GIL) |

   A wrapper dies when the collector finds it unreachable: a young one at the next minor
   collection, an old one at the next major one. There is no promptness guarantee and none at
   exit; a resource that must be released at a known point is released explicitly
   (`ForeignResourceObject::dispose`, `python:py_dispose`), after which the wrapper stays valid
   and reports the disposed state. `onMoved` exists so that an object can re-derive pointers
   into itself (`Array::dataPtr_`, the shape and data pointers of a tensor); nothing outside
   the object may hold its address in the first place (rule 4).

10. **Type objects of modules live in the permanent space** (`permSpace`), not in the auto
    space: a `Type` is not a collectable value.

## 3. What is forbidden, by example

```cpp
static Array *cache;                         // rule 3
static std::unordered_map<slot_t, int> seen; // rules 3 and 4
thread_local String *lastPath;               // rule 3

slot_t op(ArgsView &with, ArgsView &norm, Context &ctx) {
    auto *xs  = norm.get<Array *>(0);
    auto *out = Array::create(mm::autoSpace(), xs->size()); // fine: allocation is no safepoint
    slot_t *src = xs->data();                               // fine until the next safepoint

    runSomePass(ctx);              // reaches safepoints (rule 5) ...
    out->set(0, src[0], type);     // ... so xs, out and src are all stale here (rule 2)

    xs->data()[0] = toSlot(out);   // rule 6: xs existed before the call, the store is plain
    registry.onLater([xs] { use(xs); }); // rule 3
    return toSlot(out);
}
```

The same operator within the rules: hold `xs` and `out` in `RootHandle`s around
`runSomePass` and read them back with `getAs<Array>()`; store with
`xs->set(0, out, arrayType)`; give the registry a copy of the data or a `RootHandle`.

## 4. The schedulers

The schedulers are the code that crosses safepoints all the time, and they follow rule 2 by
construction: every value lives in a frame slot (`FramePool`, `TaskflowFramePool`: registered
root tracers), and nothing read before a call is used after it.

- **Higher-order operators** re-read the array and the function from the frame in every
  iteration, store each result through a re-read of the result array, and keep the accumulator
  of a `reduce` in the node's slot. A new one must do the same; an operator written in Camel on
  top of `map` inherits it.
- **Argument passing.** Between reading the argument slots of the caller and writing the ports
  of the callee there is no safepoint; the poll comes after the callee frame is filled and
  active. A result goes from the callee's return into the caller's slot without a poll in
  between.
- **`std::tf` / `std::prl`.** A worker reads and writes object slots only inside a
  `TaskflowGcRegion`; the collector runs only while no other thread is inside one. Outside
  regions a worker holds frames, graphs, static-area tuples, types and `RootHandle`s, all of
  which stay where they are. A collector that finds another worker inside a long operator waits
  up to 500 ms, gives up and retries at a later poll with a growing back-off; nothing fails
  meanwhile, because allocation never needs a collection (a full birth space sends new objects
  to the elder space, which grows).
- **`std::jit`.** Compiled code has no stack maps. It is sound because no collection can run
  while a native frame exists (compiled code calls nothing and polls nothing; the allocator
  refuses a safepoint under a native frame), and because, when objects can move, a graph whose
  static area holds an object constant is not compiled: the code would contain the constant's
  address as an immediate. Turning on lowerings that call out of compiled code (operators,
  indirect calls) requires stack maps first; this is stages 3 and 4 of the ruling.
- **The macro pass** evaluates all candidates of a round before it opens a graph draft: a draft
  holds plain copies of static slots, and evaluation reaches safepoints. Any pass that
  evaluates and rewrites must keep that order, or root what its drafts hold.

## 5. What the generational collector relies on

- Young objects (up to 64 KB, allocated in the 4 MB birth space) are copied at a minor
  collection: into a survivor space, and into the elder space after four survivals or when the
  survivor space is full. Elder and large objects never move.
- Roots are updated in place: frame slots, static areas of graphs (roots, and graphs reached
  through function values), `RootHandle`s, the evaluator's value roots, registered tracers.
- Objects carry no type. The collector traces an object with the static type of the slot it
  was reached through, so a reference in a slot typed `any` is neither traced nor updated (see
  the open question in section 9).
- An object born old (large, or small while the birth space was full) is tracked until the
  next minor collection and scanned when a typed reference reaches it, so its creator may fill
  it with plain stores (rule 6). After that collection it is an ordinary old object.
- The write barrier remembers old objects that point at young ones (or at objects born old
  since the last minor collection), with their type.

## 6. Checking a module

1. Run its tests with `CAMEL_GC_POISON=1` (section 8). Tests under `test/plans/feat/modules`,
   `compile`, `linear`, `para`, `trans` and `smoke` are run that way by the default test set
   (`mm.gc_poison_sweep`), so a module with tests there is covered as it is.
2. Run them with `CAMEL_GC=generational CAMEL_GC_VERIFY=both CAMEL_GC_STRESS_SAFEPOINT=1
   CAMEL_GC_STRESS_MODE=minor CAMEL_GC_PROMOTION_AGE=1`: everything is promoted at once, which
   is what shows a missing write barrier (rule 6) as "Old-to-young reference is missing from
   remembered set".
3. Search for `static`, `thread_local` and members of module state that hold `Object *`,
   `slot_t` or a pointer from `data()` / `view()`.

## 7. Audit of the repository (October 2026)

About 480 sites were examined: the nine object classes of `rtdata` and the three of the
modules (`onMoved`, `updateRefs`, finalizers), about 95 built-in operator bodies, the operators
of the 21 modules, about 110 stores into objects, about 70 `static` / `thread_local` / stored
lambdas, the three schedulers and the JIT entry, the macro evaluator, the graph drafts and the
passes that evaluate at compile time. 21 findings; "moving" says whether it breaks with the
generational collector as it was.

| # | Where | What is held or written | Crosses | Why it worked | Moving | Handling |
|---|---|---|---|---|---|---|
| 1 | `src/core/mm/gen.cpp` minor collection | a slot visited twice (static area as graph root and through each active frame) | — | early promotion hid the second copy | yes: roots left pointing at freed memory | fixed: an object in the To space is final |
| 2 | `src/execute/macro/macro.cpp`, `src/runtime/draft.cpp` (`staticSlots_`) | copies of static slots and of evaluated values in a draft | later evaluations of the round | objects never moved; the evaluator kept them alive | yes: committed constants dangle (the defect of `31_map_and_set.md` section 11) | fixed: evaluate, then rewrite, then commit |
| 3 | `gen.cpp` allocation fallback, large objects | object born old, filled with plain stores | next minor collection | nothing moved | yes | fixed in the collector (objects born old are scanned); covers 4 to 9 |
| 4 | `rtdata/array.h` Array constructor | `Array` → `FixedArray` edge without barrier | — | — | yes, when the birth space fills between the two allocations | by 3 |
| 5 | `modules/json/operators.cpp` (`decode`) | young values into a fresh array / struct, untyped | — | — | yes | by 3 for typed results; an `any` result: section 9 |
| 6 | `modules/random/operators.cpp` (`sample`) | references copied into a fresh array, untyped | — | — | yes above 512 elements | by 3; test `gc.born_old.*` |
| 7 | `modules/onnx/module.cpp` (`supported_operators`) | fresh strings into a fresh array, untyped | — | — | yes above 512 | by 3 |
| 8 | `modules/tensor/tensor.cpp` (`createView`) | `owner_` of a view, no barrier | — | — | yes, view born old and owner young | by 3 |
| 9 | `src/execute/entry_args.cpp` | loaded leaves into a fresh struct / tuple, untyped | — | — | yes, aggregate born old | by 3 |
| 10 | `gen.cpp` minor collection, `rtdata/func.cpp` | static area of a graph reached only through a `Function` | — | nothing moved | yes | fixed: such graphs are traced once per minor collection |
| 11 | `gen.cpp` scan of copied objects | an object copied without a static type was not scanned | — | — | yes for self-describing objects (tensor views, optimizer state, functions) | fixed |
| 12 | `gen.cpp` promotion | `std::bad_alloc` when the elder space was full | — | — | yes: the program ended | fixed: the elder space grows |
| 13 | `fastvm/jit/backend/x64` (`emitLoadStaticConst`) | an object constant baked into machine code as its address | every later collection | constants never moved | yes | fixed: such a graph is not compiled when objects move; a safepoint under a native frame is refused |
| 14 | `modules/onnx/module.cpp`, `exporter.cpp` | function, path, examples, constants as raw slots; `unordered_map<slot_t, ...>` keyed by address | macro expansion inside the operator | nothing moved | yes | `NoCollectionScope` around the export |
| 15 | `rtdata/foreign.cpp`, `modules/python/operators.cpp` | deleter ran inside the collection, allocator locked, and took the GIL | — | one thread | deadlock with a thread that holds the GIL and allocates; self-deadlock for a deleter that allocates (both collectors) | fixed: releases run after the collection, unlocked |
| 16 | `modules/onnx/emitter.cpp`, `autodiff/pullback.cpp` | rooted, but the raw slot is what is read later | none today | no safepoint inside translate / rewrite | no; latent | rule 2 applies if a safepoint is ever added there |
| 17 | `rtdata/array.h` untyped `reserve` / `append` / `resize` / `shrinkToFit` | new backing store without barrier | — | no caller on an existing array | no; latent | rule 6; left in place |
| 18 | `src/compile/gir/builder.cpp` (`typeFoldedValues_`) | raw copy next to its `RootHandle` | none today | — | no; latent | — |
| 19 | `rtdata/foreign.h` `movable` / `pinned` flags | never read | — | — | no; both descriptors use the defaults | left; a pinned wrapper needs `ForeignPinnedHandle` |
| 20 | `rtdata/collection.cpp` (`compareKeys` default) | would order keys by address | — | unreachable: key types are restricted | no; latent | — |
| 21 | slots typed `any` (frame, array element, tuple field) | a reference the collector does not see | any collection | large heap, rare collections | **use after free with both collectors** | not fixed: section 9 |

Found clean: every `onMoved` (arrays re-derive `dataPtr_`, tensors their shape and data
pointers; the others have no pointer into themselves); every `updateRefs` for typed slots; no
`static`, `thread_local`, module-state member or stored lambda holds a reference; the built-in
operators and the operators of `algo`, `crypto`, `fs`, `io`, `math`, `os`, `para`, `re`, `sim`,
`statistics`, `sys`, `this`, `time`, `pyplot`, `nn`, `autodiff` (run time) keep to rule 1; the
higher-order operators of all three schedulers re-read after every element call; no address of
auto-space memory is given to NumPy, DLPack or Python (they copy).

Not read line by line (covered by pattern searches and by the poisoned test runs only): most of
`modules/onnx/exporter.cpp` and `lowering*.cpp`, `modules/autodiff/pullback.cpp`, the tensor
kernels.

**The standard library added since** (`modules/str`, the additions to `algo`, `math` and
`statistics`, `stdlib/array.cml`, `rtdata::Table` and its operators, `json.decode_as`): 98
operator bodies under 151 registered names, the 15 table functions and the 21 functions of
`array.cml` were read. No violation of rules 2, 3, 5 or 6: every reference store goes into an
object created in the same call, the only in-place operators (`sort` and its variants) admit
scalar elements only, no operator calls Camel code (the higher-order functions are Camel
source on top of `map` and `reduce`, and `argsort` takes a key array, not a function), no
static holds a reference, and every raw copy into a new table is followed by a barrier pass.
Three places compare by address where they should compare by content (rule 4). They are wrong
with both collectors and are defects of the library, not of the collector; they are recorded
here and left to the standard-library work:

| # | Where | What | Effect |
|---|---|---|---|
| 22 | `modules/algo/sequence.cpp` (`unique`, composite elements) | `equals(..., deep=false)` compares nested references by address | two equal `(string, int)` or `string[]` elements are both kept; built-in `==` and `contains` on such values do the same |
| 23 | `modules/algo/sequence.cpp` (`unique`, element type `any`) | slots hashed as integers | references in an `any[]` are deduplicated by address (an instance of 21) |
| 24 | `rtdata/collection.cpp` (`Table::equals`, `deep == false`) | values compared with `memcmp` | reachable through 22 and through `contains` on an array of maps |

## 8. Tools

| Setting | Effect |
|---|---|
| `CAMEL_GC_POISON=1` | Safepoint poisoning. Turns the generational collector on; **every safepoint moves every young object** (nothing is promoted by age) and overwrites the memory the objects left with the address of an unreadable page. The addresses are not handed out again while room remains above them. A stale pointer faults in that page on its first virtual call or on the first use of a reference read through it, and the process ends with `[camel] GC poison: a pointer to a Camel object was used after a safepoint moved the object` (exit code 134). With `CAMEL_GC_VERIFY=both` a stale reference in a root or in an object is reported with its path before it is used |
| `std::gc::poison` | White-box self-test of the mode |
| `CAMEL_GC_PROMOTION_AGE=n` | Survivals before promotion (default 4). 1 promotes everything at once: the setting that shows missing write barriers |
| `CAMEL_GC_LARGE_THRESHOLD_KB=n`, `CAMEL_GC_NURSERY_MB=n` | Size above which an object is born old (4 KB with mark-sweep, 64 KB with the generational collector) and size of the birth space (4 MB). A small threshold makes ordinary arrays be born old, which is what the tests of such objects need |
| `CAMEL_GC_VERIFY`, `CAMEL_GC_STRESS_*` | Heap verification and forced collections (`24_gc_verification_guide.md`) |
| `CAMEL_TEST_TIMEOUT_SCALE=n` | Multiplies the timeouts of the test runner, for running whole plans under these modes |
| `[[sweeps]]` in a plan | Runs the tests of other plans again under an environment (`docs/test-plans.md`); `mm.gc_poison_sweep` does it for poisoning |

What poisoning does not catch: a stale pointer to an object that is old (it no longer moves);
a stale pointer that is never used again; code that no test runs.

## 9. Open questions (for the language author)

- **`any`.** A slot typed `any` may hold a reference (`json.decode` returns one), but the
  collector, which knows values only by static type, neither traces nor updates it. With both
  collectors a value kept in an `any` variable across a collection is freed while in use
  (`let d = decode(s); f(); d as string[]`), and what a typed value refers to through an `any`
  element or field is not kept alive either. Converting at once (`decode(s) as T`,
  `decode_as`) is safe on `std::nvm` and `std::fvm`, where no safepoint lies between the two
  nodes. Making `any` sound needs either a run-time tag on such values or their removal in
  favour of generics; design principles section 6 lists the position of `any` as undecided.
- **Whether the generational collector becomes the default** (measurements in
  `35_generational_gc_report.md`).

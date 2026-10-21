# Runtime GC Lifetime Model

Camel's runtime values live in four allocator domains with different ownership rules:

- `autoSpace` owns runtime `rtdata::Object` values and is the only domain swept by the runtime GC.
- `graphSpace` owns materialized `GCGraph` carriers and their in-place native payloads.
- `permSpace` owns `Type` metadata and other process-lifetime immutable descriptors.
- `metaSpace` owns manually managed compiler/runtime metadata.

`GCGraph` is an `rtdata::Object` only so the GC can use its `updateRefs` hook to trace static data.
The graph carrier itself is not an `autoSpace` object and is reclaimed by `GCGraphManager`, not by
the collector.

## Growable Program Spaces

`graphSpace`, `permSpace` and `metaSpace` hold what the program is made of, so their size
follows the size of the program. Each is a list of segments
(`include/camel/core/mm/alloc/growable.h`): `GrowableFreeListAllocator` for the graph and meta
spaces (one `FreeListAllocator` per segment), `GrowableBumpAllocator` for the permanent space
(one `BumpPointerAllocator` per segment). The first segment is as large as the whole space used
to be (64, 32 and 16 MB); when no segment has room, a further one is reserved, as large as
everything reserved so far (at most 256 MB, never smaller than the request), until the total
reaches the limit of the space (`CAMEL_GRAPH_SPACE_LIMIT_MB`, `CAMEL_PERM_SPACE_LIMIT_MB`,
`CAMEL_META_SPACE_LIMIT_MB`; 256 MB each by default).

Invariants:

- **Addresses are stable.** An object never moves and a segment is never released. `GCGraph *`,
  `Type *` and arena blocks are held as raw pointers all over the compiler and the schedulers;
  growth changes nothing for them.
- **The segment table is append-only and read without a lock.** It has a fixed capacity (1024
  entries); an entry is written before the count that publishes it. `contains`, `free` and
  `iterateAllocated` read the count and walk the entries at any time from any thread. The
  collector calls `graphSpace().contains` / `permSpace().contains` (address-stability checks,
  heap verification) and `graphSpace().iterateAllocated` (unmarking graph carriers before a
  mark) while other threads may allocate: a segment added meanwhile holds only objects
  allocated after the walk began, which are unmarked, as an object allocated at the end of the
  single block was before.
- **Growth is serialized, allocation is not.** A thread that finds no room takes the growth
  mutex, looks again at the segments added while it waited, and only then reserves one.
  Allocation inside a segment keeps the segment's own synchronization (a mutex for the free
  lists, compare-and-swap for the bump pointer).
- **The cross-space contract.** The collector reaches auto-space objects through the roots
  (`GCGraphManager::gcRoots()`: the `Program`s that are roots, see "Graph space lifetime"),
  wherever the graph lies, and never moves anything in these spaces. The graph space holds the
  `Program`s and graph versions, which `GCGraphManager` frees one by one, into the segment they
  came from, when nothing reaches them.
- **Allocation never returns null.** At the limit, or when the host has no memory for a
  segment, the space throws `mm::MemoryExhausted` (a `std::bad_alloc`) with the space, the
  request, the usage, the limit and the variable that raises it. The first caller that knows
  what was being built adds it (`during`): the graph builder names the function being compiled,
  the encoders name the graph. The pass driver, the graph builder and the command line report
  it as `RuntimeDiag::MemoryExhausted`. The auto space has no limit of its own unless `CAMEL_GC_HEAP_LIMIT_MB` sets one (the
  elder space grows by segments, the large-object space takes blocks from the host); the limit
  and a refusal by the host are reported the same way.

This does not stand in the way of a moving collector for small objects: nothing here hands out
pointers to collectable objects, and the spaces hold only what is never moved.

What a function costs (measured, release build, `CAMEL_GC_STATS=1`): about 0.8 KB of the graph
space for a function of one expression, 1.9 KB with an `if` (three graphs), 1.5 KB with a
lambda; 0.6 to 1.8 KB of the permanent space before the type table (5 to 17 type objects:
composite types were created anew at every use; see below for what is shared now); about 1.2 KB of the meta space when its graph owns a constant
object or a function value (the first block of its arena, see
`include/camel/compile/gir/arena.h`). That first block was 256 KB, which is what limited a
program to 62 such graphs.

The permanent space never releases, so **a question must not allocate in it**. Resolving an
operator overload builds a new function type every time; the rewrites ask "is this argument
passed to a var parameter" for every user of every value and again after every inlined call,
and re-infer the result type of every operator node at every commit. One program of 120 lines
left 1.4 million function types (more than 256 MB) behind under `std::opt::specialize std::opt`.
Two rules follow, and code on a query or probe path has to keep them:

- Overload questions that do not depend on constant argument values go through
  `runtime::operatorOverloadType` (`include/camel/runtime/draft_writes.h`), which keeps the
  answer per operator and argument types (types are permanent, so their addresses identify them
  for good). A resolution with constant arguments is still made anew each time.
- A commit that re-encodes a graph whose static area did not change keeps the graph's static
  tuple type (`draftStaticDataType` in `src/runtime/draft_session.cpp`) instead of creating an
  equal one for the size computation and another for the encoding.

Since 2026-10-13/14 (graph data model, migration step 2) the type table
(`include/camel/core/type/intern.h`) shares types at the constructor instead of at each
caller:

- Tuple types (`TupleType::create`, frame and static-area layouts among them) and array types
  (`ArrayType::create`) are interned by their component type objects: re-encoding an unchanged
  graph, graphs with equal layouts and every place a program writes `int[]` allocate nothing
  new. This became possible when converting a value to its own type was ruled legal (design
  principles 5.1): the cast rule of these types compares structure, not object identity.
- Function types (`FunctionType::create`, `FunctionTypeFactory::build`) are interned with their
  parameter names: an operator's signature for a call, a function type a program writes, a
  branch arm's signature, every signature a rewrite re-infers. The one exception is the
  signature of a function the front end is building (`OpenFunctionType`, one per function the
  program defines), whose result type the body decides after values of the function exist.
  Capture names are no longer part of a function type; they are held by the graph
  (`GCGraph::captureNames`).
- Struct types are interned by field names and field type objects.

Not interned: open signatures, unions and type variables. An interned type is immutable by
construction: no type class has a mutator except `OpenFunctionType::setExitType`, and the table
never holds an open signature. `CAMEL_TYPE_STATS=1` prints what a program allocated per kind
of type and how many constructions the table answered.

A graph's frame layout is not a type in the table at all: it is built at the end of the
graph's own block in the graph space (`TupleType::constructFrameLayout`) and freed with the
graph, so a graph a rewrite changes, whose slot types differ, leaves nothing behind in the
permanent space. (Before graph model step 3 it was an interned tuple type: one new permanent
object per changed graph per commit.) Static-area layouts are still interned.

`CAMEL_SPACE_SEGMENT_KB=<kilobytes>` sets the first segment of all three spaces. It is a test
control: with 64 KB a program of a few hundred functions makes every space grow several times,
so growth can be run under heap verification and collection stress (`test/plans/feat/mm/capacity.plan.toml`).

## Tracing Contract

`rtdata::Object::updateRefs` receives both a relocation callback and the static `Type` describing the
object layout. Composite values must pass the precise child `Type` for every GC-traced slot. This is
required because `Tuple`, `Struct`, and `Array` store raw `slot_t` payloads without per-field runtime
metadata.

The important consequences are:

- `Tuple`, `Struct`, `FixedArray`, and `Array` trace only fields whose `TypeCode` is GC-traced.
- Inline small-array storage is traced the same way as fixed backing storage.
- `Function` and compile-time `StaticFunction` relocate the closure tuple itself, then the tuple scan
  traces closure members with their tuple layout.
- Module object types such as `TensorType`, `PyObjectType`, and `ParameterType` must be allocated in
  `permSpace`, because they are `Type` descriptors, not collectable runtime objects.

## Graph space lifetime

The graph space holds the `Program`s of a context and the graph versions they hold, both objects
of the graph space (`include/camel/runtime/program.h`, `GCGraphManager` in `graph.h`). A graph
version lives as long as a live `Program` holds it, a `Program` as long as something reaches it:

- **Roots** (the `Program`s in `GCGraphManager::gcRoots()`): the installed program, the detached
  programs a back end runs (`trackDetached`), the pinned programs of the operator definitions,
  the programs the pipeline made since the last `settle()` (`reserve`; the pass driver settles
  after each pass, keeping the program the pass handed on), and the program of a running back end
  (`RunPrograms::begin`).
- **Edges**: `Program::updateRefs` traces its graphs and the program it copies (`original`),
  `GCGraph::updateRefs` its static values, `Function::updateRefs` the `Program` it was made in
  (slot `program`) and its graph. A frame keeps the graph it runs (`FramePool` tracer).
- **Two collections.** While a program runs, a major collection of run-time values marks through
  the heap and `GCGraphManager::sweepMarked()` (installed with `setGraphSweeper`, called after
  `markPhase`) frees the unmarked `Program`s and graph versions; the graph space also asks for a
  major collection when it holds a megabyte (`CAMEL_GRAPH_GC_MB`, default 64) and as much again
  as it held after the last sweep (`requestMajorCollection`). Between passes (nothing runs)
  `collect()` has the heap marked the same way, without sweeping or moving anything
  (`collectGraphSpace`), so a function value of the heap keeps its `Program` there too; it runs
  when a program is installed or detached programs are released, once the space has grown by a
  quarter and a megabyte since the last sweep, and is off while a back end runs (`enterRun`).
- **FastVM bytecode** of a graph made at run time is given back with the graph version: the plan of
  the version holds a lease on its range of the bytecode vector (`CodeLease`), the free ranges are
  kept by `CodeSpace`, and the next graph compiled at run time is placed in a free range that fits
  before the vector grows.
- **Commits** share what they did not change, so versions are shared by programs; a graph that
  was left as it was gets its function values retargeted to the new program
  (`retargetFunctions`), so that the programs before the commit are not reached through them.
- **Node chunks** (`GCNodeChunk`, shared by graph versions) are plain blocks of the graph space, not
  objects: they have no virtual table to trace through, and a chunk is reachable exactly when some
  live graph version holds it. Freeing a graph gives up its share (`releaseNodeChunks`); the last
  share frees the block. The count therefore stays: it is the reachability of a block that no
  tracer visits, not a second lifetime.
- A graph that is in no `Program` (one a session encoded and a commit did not bind) is not
  registered and not freed before the context ends.

## Root Sources

The GC reaches live objects through these roots:

- `Context` installs `GCGraphManager::gcRoots()` (`Program`s) as the process root set while runtime
  graphs are alive.
- `Program::updateRefs` traces the graphs of its table; `GCGraph::updateRefs` traces the graph static
  tuple using the graph's static tuple type; `Function::updateRefs` traces its `Program`.
- `FramePool` and `TaskflowFramePool` register external root tracers while schedulers are alive.
- Macro evaluation registers typed value roots for temporary compile-time/runtime bridge values.

Context teardown must clear the installed root set before destroying `GCGraphManager`, otherwise the
global `autoSpace` singleton can retain a dangling vector pointer.

## Collectors

Two collectors run the auto space; `--gc` / `CAMEL_GC` chooses (`docs/cli.md`, "The
collector"). The default is the non-moving one.

- **mark-sweep.** Small objects are allocated from the free lists of the elder space, objects
  above 4 KB as separate blocks in the large-object space. A major collection marks from the
  roots and sweeps both. No object ever moves.
- **generational.** Objects up to 64 KB are allocated by bumping a pointer in the birth space
  (4 MB). A minor collection copies what is reachable into a survivor space (1 MB), or into the
  elder space after four survivals or when the survivor space is full, and frees the rest by
  resetting the spaces; a major collection first runs a minor one, then marks and sweeps the
  elder and large-object spaces. A minor collection is asked for when the birth space is 90 %
  full. Its cost is what survives plus the roots: dead objects are not visited (those with a
  finalizer register themselves, `IAllocator::noteFinalizable`).

The elder space grows by segments (32 MB first, each further one as large as everything
reserved so far, at most 256 MB), so neither allocation nor promotion fails short of the host
refusing memory. Major collections are paced by volume for both old spaces: one is asked for
when a space has taken, since its last sweep, the larger of a floor (32 MB for the elder space,
`CAMEL_GC_LARGE_BUDGET_MB` for large objects) and `CAMEL_GC_GROWTH_PERCENT` percent of what
that sweep left alive. `CAMEL_GC_HEAP_LIMIT_MB` bounds what the two hold together.

What C++ code may do with objects under either collector is the subject of
`36_gc_handle_rules.md`. In short for this file: a minor collection updates the roots in place
(frame slots, static areas of graphs including graphs reached only through function values,
`RootHandle`s, the evaluator's roots, registered tracers); an object born old is tracked until
the next minor collection and scanned then, so its creator needs no barrier; later stores of
young references into old objects go through the typed barrier.

## Phase 2 Safepoint Contract

Collection, moving or not, is serviced only by explicit `autoSpace().safepoint(...)` calls.
Allocation paths may queue a minor or major request for the next safepoint, but they never collect:
an allocation site is not a safepoint, and its caller may hold objects that no root reaches yet
(`Array::create` allocates the array and then its backing store). The earlier design ran a
non-moving mark-sweep inside the failed allocation, which freed such half-built objects.

When the elder space has no block for a small object, the object is placed in the large-object
space instead, which grows on demand, and a major collection is requested for the next safepoint.
The request is made only if the elder space has taken at least a quarter of its capacity since the
last sweep: when it is full of live objects a sweep frees almost nothing, and the overflow is then
paced by the large-object budget (below) alone. Until that sweep, sizes that found no block skip
the elder space.

Runtime safepoints are installed at pass boundaries, NodeVM node/function-call boundaries, FastVM
bytecode boundaries, macro execution boundaries, and Taskflow's linear scheduler boundaries. At
those points, live runtime values must be recoverable from registered roots: graph roots, active
frames, macro value roots, or explicit `mm::RootHandle` instances for C++ locals.

The current non-moving domains are intentional:

- JIT machine code has no stack maps. No collection runs while a native frame exists (compiled
  code calls nothing and polls nothing; a safepoint under a native frame is refused), and with
  the generational collector a graph with an object constant is not compiled.
- Taskflow workers stop for a collection through a handshake (`TaskflowGcGate`), not by moving-safe
  stack maps. Work that can hold a value no root reaches yet (an operator and the store of its
  result, the fill of a callee frame, the store of a call result) runs inside a region; a worker
  polls a safepoint only outside every region, and a deferred collection runs only while no other
  thread is inside one. The higher-order operators keep what their tasks produce where the
  collector finds it (the result array of a `map`, the node slot of a `reduce`, a rooted scratch
  array). A collection can therefore run in the middle of a long flow. A collector that finds
  another worker inside a long operator waits up to 500 ms, then gives up and retries at a later
  poll. Workers still hold addresses across regions only for objects the collector does not move.
- Native module bridges that hold auto-space objects across a safepoint must use `mm::RootHandle` or
  store the value in a traced frame/static slot before crossing the boundary.

## Phase 3 Write Barrier Contract

`autoSpace` owns the single authoritative old-to-young write barrier. Typed composite mutation paths
call `mm::writeBarrier(owner, ownerType, storedSlot, storedType)` before storing GC-traced values
into tuples, structs, arrays, fixed-array backing stores, and frame static tuples. The barrier is
cheap when young copying is disabled, when the stored slot is null or not GC-traced, when the owner
is not an old/large auto-space object, or when the target is not young.

Remembered-set entries carry the old owner plus the owner's runtime layout. Minor GC treats those
entries as typed roots, relocates their young targets, and keeps an entry if the old object still
contains young references after relocation. Newly promoted objects are also scanned after copying and
remembered when promotion leaves old-to-young edges behind.

The verifier compares remembered entries against typed old/large objects whose layouts are known
from barriers, roots, or relocation metadata. Missing remembered entries report the owner slot, slot
type, region, and young target. `CAMEL_GC=generational` selects the moving collector;
the production default remains address-stable.

Intentionally unbarriered paths are limited to storage that is not an old auto-space container:
dynamic frame slots are traced roots, graph-space static tuples are graph roots rather than
auto-space owners, macro/static bridge buffers use their own lifetime contracts, and compile-time
data objects do not participate in runtime auto-space collection.

## Phase 4 Foreign Resource Contract

Camel slots remain plain 8-byte values. A slot that names an external/native resource stores an
ordinary `rtdata::Object *`, specifically a `ForeignResourceObject`. The wrapper is GC-managed and
may move under the generational collector; the native resource itself lives behind a
`ForeignResourceControlBlock` with explicit retain/release, idempotent disposal, kind metadata, and
debug counters.

Wrapper relocation copies only the wrapper bytes. It does not retain, release, or clone the native
resource. The control block is released only when a wrapper is finalized, when an explicitly cloned
resource reference is released after an allocation failure, or when a descriptor-specific shared
resource clone/retain operation is requested. `dispose` immediately runs the descriptor deleter,
marks the resource disposed, and leaves the wrapper object alive; later access fails with a
deterministic runtime error and a later GC finalizer only releases the control-block reference.

Large-object pacing: objects above the generational threshold (4 KB) bypass the birth and elder
spaces, so they never exhaust a space and never trigger a collection on their own. The allocator
therefore counts large-object bytes since the last sweep and requests a major collection at the
next safepoint once they exceed a budget. After every sweep the budget is the larger of a floor
(`CAMEL_GC_LARGE_BUDGET_MB`, default 4 MB) and `CAMEL_GC_GROWTH_PERCENT` percent (default 100) of
the large-object volume the sweep left alive. This is the usual heap-growth rule: with a live
volume `L` and a growth of `g` percent, a program is collected once per `L * g / 100` bytes it
allocates and the space holds at most `L * (1 + g / 100)`, plus whatever is allocated between the
request and the next safepoint (a whole operator: one array can be larger than the budget). The
floor only matters while `L * g / 100` is below it; a program that keeps hundreds of megabytes
alive is paced by the growth setting alone, whatever the floor. With the default, the space
doubles between two collections: a merge sort of 2e7 integers (2 GB of arrays allocated, about
750 MB alive at the deepest point) is collected 4 times and peaks a third above what it needs.
A lower setting trades collections for memory in proportion (25: four times the collections, a
quarter of the garbage). The collector's own work is proportional to the number of objects, not
to their bytes, so for a few large arrays the collections themselves cost almost nothing; under
`std::tf` each one also waits for the other workers to leave their operators (see the safepoint
contract above), and that wait is what a lower setting pays for. Freed large blocks are kept in
a size-class cache (up to 256 MB) and reused, so steady-state tensor workloads recycle hot memory
instead of faulting fresh pages in; cached blocks stay resident, so the resident peak sits up to
256 MB above what the space holds.

GC finalization now runs before mark-sweep frees unreachable elder or large objects. In the
young-copying path, dead nursery/from-space wrappers are finalized before the semispace reset, while
forwarded source wrappers are not finalized because their live copy still owns the same control
block. This preserves exactly-once native cleanup across promotion and movement.

The native handle ABI has three levels:

- `ForeignBorrowedHandle` documents a raw object pointer that is valid only for the current native
  call and across no safepoint.
- `ForeignRootedHandle` wraps `mm::RootHandle`, keeps the object live, and updates when copying GC
  moves it.
- `ForeignPinnedHandle` also roots the object, but it is deliberately conservative: it only accepts
  objects already in non-moving auto-space regions (`elder` or `large`) or non-moving runtime
  regions. Native code that exposes a raw address must use this handle or allocate the exposed data
  in a non-moving domain.

`ForeignResourceDescriptor` is the native type contract. It names the resource kind, owns the deleter
and optional clone operation, can trace embedded Camel references, can observe wrapper movement, and
declares flags such as `containsCamelReferences`, `finalizable`, `movable`, and `pinned`. A
descriptor without a trace callback is treated as unable to hold Camel object references.

The Python module now stores `PyObject` values as `ForeignResourceObject` wrappers instead of a
process-global holder vector. `python:py_dispose` exposes deterministic cleanup, and normal Python
access checks for the disposed state before borrowing the `py::object`.

Loaded `.cmo` DLL handles are still retained for the process lifetime. This is intentional: native
resource control blocks may hold descriptor callbacks compiled into the module DLL, and unloading the
DLL before every unreachable wrapper has been finalized would leave stale finalizer code pointers.
Context teardown drops graph roots so a later major GC can reclaim wrappers, but the code image stays
mapped to keep late finalization safe.

Diagnostics expose `foreignResources` in `std::gc::snapshot`, including created/disposed/finalized
control-block counters plus active rooted and pinned handle counts. `std::gc::foreign_resource`
runs a deterministic self-test covering movement, rooted survival, pinned address stability,
idempotent disposal, finalization after disposal, and handle accounting.

## Phase 1 Observability Controls

GC diagnostics are configured explicitly through environment variables so language-level test cases
do not need source changes:

- `CAMEL_GC_VERIFY=before|after|both|1` enables heap verification around every requested
  collection.
- `CAMEL_GC_STRESS_ALLOC=N` requests a collection after every `N` auto-space allocations. The
  request is serviced at the next GC safepoint, not inside the raw allocation path, so object
  constructors are not interrupted with half-initialized payloads.
- `CAMEL_GC_STRESS_SAFEPOINT=N` requests a collection every `N` explicit runtime safepoints.
- `CAMEL_GC_STRESS_MODE=minor|major|both` selects which collection kind stress requests use.
- `CAMEL_GC_LOG_MOVES=1` emits movement records when the copying path relocates an object.
- `CAMEL_GC=generational` (older spelling `CAMEL_GC_ENABLE_YOUNG_COPYING=1`) selects the generational collector for GC
  tests. It is off by default.
- `CAMEL_GC_PRINT_CONFIG=1` prints the effective GC diagnostic configuration to stderr when the
  process-level allocator is initialized.
- `CAMEL_GC_STATS=1` also prints the occupancy of the graph, permanent and meta spaces at exit
  (`[camel] spaces: ...`): for each, kilobytes in use and at the high-water mark, megabytes
  reserved, segments, allocations and the limit.
- `CAMEL_GC_STATS=1` prints one line of pacing statistics to stderr at exit (`[camel] gc: ...`):
  collections performed, requests by cause (large-object budget, elder space exhausted, other),
  megabytes allocated per space, the large-object volume the last sweep left, the high-water mark
  of the large-object space, and the budget and growth setting in force. `CAMEL_GC_STATS=2` also
  prints one line per major collection (`[camel] gc collection: ...`) with the budget that was in
  force, the bytes allocated since the previous sweep, the volume before and after the sweep, and
  the next budget. The numbers depend only on the allocation order, not on time.

The verifier checks allocator structure, object header validity, region tags, and every typed
GC-traced reference reachable from the named root sources. External roots are registered with stable
source names such as `FramePool.activeFrames`, `TaskflowFramePool.activeFrames`, and
`MacroEvaluator.valueRoots`; failures include the root path plus owner/slot/type information from
the tracing callback.

Runtime passes expose the same infrastructure:

- `std::gc::verify` validates the heap and leaves the graph available for later passes.
- `std::gc::minor` runs one explicit minor collection and leaves the graph available for later
  passes.
- `std::gc::major` runs one explicit major collection and leaves the graph available for later
  passes.
- `std::gc::config` prints the active GC diagnostic configuration as JSON and leaves the graph
  available for later passes.
- `std::gc::summary` prints a compact human-readable GC summary and leaves the graph available for
  later passes.
- `std::gc::snapshot` / `std::gcsnap` prints JSON with region object counts/bytes, collection
  counters, deferred-collection and elder-overflow counters, moved/promoted/freed counters,
  root-source data, remembered-set size, and foreign-resource diagnostics.
- `std::gc::remembered_set` runs a targeted old-to-young remembered-set self-test and leaves the
  graph available for later passes.
- `std::gc::foreign_resource` runs a targeted FFI resource and handle lifetime self-test and leaves
  the graph available for later passes.

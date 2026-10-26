# Resources and Definition Graphs of Operators

This note describes phase 4 of `docs/design/op-semantics` as implemented: the resources an
operator touches (design section 8) and the definition graphs of operators (design section 9).
The headers are `include/camel/core/resource_semantics.h` (the declarations),
`include/camel/runtime/resources.h` (their resolution in a graph and the scheduler's queries) and
`include/camel/runtime/definitions.h` (building and expanding definitions). Nothing here changes
how any graph is printed, and what a program computes only where a kernel was made to agree with
its definition (`log_softmax`, section 2.4): the declarations are read only by those who
ask, and no pass or back end expands a definition by default.

## 1. Resources

### 1.1 What is declared

Only some things have an identity a program can observe (design principles 10.5): a `var` cell,
an object changed in place, an object a module wraps. Besides those, some state is named by no
value: the file system, the console, a module's random generator. An operator's
`OperatorSemantics::resources` (`sem::ResourceSpec`) says, per access:

| Part | Values |
|---|---|
| resource | `Variable`, `Object`, `Foreign` (with a name: `tiles:device`), `Ambient` (with a name: `fs`, `stdout`, `stderr`, `stdin`, `rng`, `tensor:generator`); `Unknown` only in the conservative reading |
| binding | the ordinary or with operand that names it; none for an ambient resource (`Capture` appears in graph footprints only) |
| region | `Whole`; `Mapped` (the elements an access or the write map of the node's index instance reaches); `Unknown` (read as whole) |
| mode | `Read`, `Write`, `ReadModifyWrite` |

and for the operator: how its result relates to the operands' resources (`Fresh`, `Same`, `View`,
`MayAlias`, `Unknown`), what it allocates (`:var/new` a `Variable`, `tiles:upload` a
`Foreign tiles:device`), and whether running it is visible outside the program
(`externalEffect`).

`OperatorTable::declareResources(uri, spec)` adds the spec to the operator's record (a record of
its own, `uri@1.0`, when it has none; otherwise one minor version up). Check 5 of
`declareSemantics` refuses a spec that is inconsistent in itself (`sem::checkResources`: an
ambient resource has a name and no operand, the others an operand and no name; an `Unknown`
class is not a declaration; a `Same` or `View` result names its operand) or with the traits: a
pure operator touches nothing; any other touches, allocates or affects something; an external
effect only where the traits allow one. The converse is a refinement: the traits of an
assignment count it as external (no trait says "writes a variable"), its resources say it writes
the program's own cell and nothing outside. The effect summaries (`effectSummaryOf`) are still
computed from the traits.

An operator that declares nothing is read conservatively (`sem::conservativeResources`): a pure
one touches nothing; any other reads and writes every operand, whole and of unknown class, its
result may be any of them, and it has an external effect when its traits say so.

### 1.2 What is declared today

| Operators | Resources |
|---|---|
| `:var/new` | allocates a variable |
| `:var/get`, `:var/peek` | read the cell (operand 0); the value read may share what it holds |
| `:var/take` | read-modify-write of the cell (it hands an in-place form the cell's own array) |
| `:op/assn`, `:op/assn_<t>`, `:op/assn_owned` | write the cell; the result is the value stored |
| `:op/assn_<op>_<t>` | read-modify-write of the cell |
| `:struct/append_arr_in_place`, `:op/set_index_in_place` | read-modify-write of operand 0; the result is that object |
| `array:*_in_place` | read-modify-write of an object (operand 0); the result is that object |
| `:io/print`, `:io/println`, `tensor:show` | write `stdout`, external |
| `:io/eprintln` | write `stderr`, external |
| `:io/input`, `io:get_char(s)`, `io:clear_input_buffer` | read-modify-write `stdin`, external |
| `io:has_input` | read `stdin`, external |
| `io:set_terminal_raw_mode` | write `stdin`, external; the one resource of the terminal, so raw mode and a read order each other |
| `fs:*`, `nn:save_params`, `tensor:load_npy`, `tensor:save_npy`, `ckpt:save_tree` | read or read-modify-write `fs`, external |
| `ckpt:load_tree` | read `fs` and write `stderr`, external |
| `rand:*` | write or read-modify-write `rng`, external |
| `tensor:seed`, `tensor:random`, `tensor:randn` | write or read-modify-write `tensor:generator`, external |
| `time:now`, `process:sleep` | read `clock`, external |
| `time:strftime`, `time:strptime`, `env:get_env`, `env:get_cwd`, `process:user_name`, `process:cpu_count` | read `env`, external |
| `process:exit` | read-modify-write `process`, external |
| `:profiler/*`, `para:rendezvous`, `tensor:(set_)num_threads` | their ambient state, external |
| `tiles:upload` | allocates `tiles:device`; read-modify-write `tiles:driver`, external |
| `tiles:download`, `tiles:run_preferred_resident`, `tiles:benchmark` | read the device values among operand 0; `tiles:driver`, external |
| `tiles:run_preferred_resident` (resident form) | allocates `tiles:device`, the result may alias operand 0 |
| `tiles:source`, `inspect`, `available`, `device` | `tiles:driver`, external |

The higher-order `:mark/` operators (they run the functions they are given), `python`, `pyplot`,
`onnx` and `autodiff` declare nothing and are read conservatively, as they are ordered today.

### 1.3 Queries (`camel/runtime/resources.h`)

- `resourcesOf(program, graph, node)`: the accesses of one node, each with the node of the same graph whose
  value names the resource (`NodeAccess::value`), or `anyValue` when it cannot be told. An OPER
  reads its declaration; a FUNC, a LOOP, and a CALL whose callee is a constant function or a
  closure the graph builds from one read the footprints of the graphs they run, mapped onto
  their inputs (a capture of a closure onto the FILL's captured value; a loop's captures onto the
  LOOP's With inputs; its element and state change every round: any value); a CALL through an
  unknown value is conservative.
- `footprintOf(graph)`: what running a graph once does to resources it receives (its norm ports,
  with ports and closure ports), to ambient state and outside the program; resources it makes
  itself are not part of it, and its `result` says whether the value it returns is fresh, one of
  its norm ports, or unknown. Footprints of recursive graphs are a least fixpoint; a finished one
  is cached by the graph's serial and the declaration epoch.
- `originOf(program, graph, value)` and `mayAlias(program, graph, a, b)`: within one activation, a
  value is a fresh allocation of a node (an operator whose result is fresh, a COPY), a received
  value (a port), or unknown; a FUNC or CALL whose callee returns the value it was given (a
  `Same`/`View` result, from the callee's footprint or its function type) is its argument's
  origin. Two fresh allocations of different nodes, or a fresh one and a received one, are never
  one resource; two ports may be. The program is needed to resolve a callee.
- `regionBounds(instance, region)` and `regionsMayOverlap`: the bounding box of a mapped region
  over the instance's domain, and whether two may overlap.
- `resourceConflict(graph, a, b)`: `External` when both have external effects (the outside world
  is ordered as written), `Resource` when they touch overlapping regions of one resource and one
  writes.
- `dependsOn(graph, later, earlier)` (data edges only) and `orderedBefore(graph, a, b)` (any path,
  control edges included) are kept apart from conflicts: computation dependence is the graph's
  data edges, a resource conflict is answered here, asynchronous completion is not the core's.
- `requiredOrderings(graph)`: every conflicting pair of nodes of a graph, except nodes in
  different arms of one branch (they never both run), with whether the graph orders them and
  whether by data. A pair the graph leaves unordered is the case design principles section 15
  lists as undecided (writes through references outside a sync block).

The fact kind `core.resources` (Node, class ii, reads declarations) is the node's
`NodeResources`, at the nodes that touch something; `camel inspect --facts` prints it.

## 2. Definition graphs

### 2.1 Declaring one

`OperatorTable::declareDefinition(uri, sem::Definition{module, function, params})` names a
function exported by a library module written in Camel as the operator's definition, and how the
operator's operands bind its parameters (`DefinitionParam`: an ordinary or with operand, with the
value an optional operand left out gets: softmax's axis is `-1`). Declaring it adds a fact to the
operator's semantics: one minor version up. Check 6 refuses a definition without a module or
function. The tensor module declares, from `stdlib/tensor_definitions.cml`:

| Operator | Definition |
|---|---|
| `tensor:softmax` | `m = max(x, axis, keepdims)`, `e = exp(x - m)`, `e / sum(e, axis, keepdims)`; a tensor without elements is its own result |
| `tensor:log_softmax` | `d = x - max(x, axis, keepdims)`, `d - log(sum(exp(d), axis, keepdims))` |
| `tensor:mean` | `sum(x) / (numel(x) as float)`, in f64 |
| `tensor:relu` | `maximum(x, 0.0)` |
| `tensor:sigmoid` | `1.0 / (1.0 + exp(-x))` |

### 2.2 Building one

`Context::definitions().definitionOf(oper)` builds a definition the first time it is asked for:

1. The library is imported into the context (the program may have imported it itself: it was
   then built with the program) and, if it is not built, compiled on its own
   (`UserDefinedModule::compileAsLibrary`): parsed and declared by a graph builder that is not the
   program's, and built by `Builder::buildLibrary`, which is `buildProgram` from the library
   instead of the entry module and without a call of `main`. A library imports modules written in
   C++ only (a module written in Camel it imported would be declared to the program's builder).
2. The exported function's overload with one ordinary parameter per declared parameter is encoded
   (`encodeToRuntimeGraph`); the graph and everything it calls are pinned in the context's graph
   manager (`GCGraphManager::pin`): kept and traced for as long as the context, never part of the
   program (a rewrite that uses one encodes a copy).
3. Checks, each refusing the definition with a reason that names it: the signature (exactly the
   declared parameters, no with parameter, no capture; not generic); the effects (a pure operator's
   definition neither has an external effect nor writes a variable; an operator that never faults
   has a definition that cannot); recursion (no definition reaches its own operator through the
   operators it uses and their definitions; a cycle refuses every definition on it).
4. The built definition records the operator's key and the graph's portable content hash
   (`portableGraphHash`): what a cache of expansions, or of kernels checked against the
   definition, keys on.

A refusal is remembered; asking again costs nothing.

### 2.3 Expanding one

`expandDefinition(session, draft, node, {inlineBody})` rewrites one OPER node of a draft: a FUNC
of the definition graph takes its place (its value, its control edges, its source position), with
its operands bound to the parameters (an optional operand left out becomes a constant), each of a
type the definition takes (a softmax of an integer tensor is not expanded); then, unless asked not
to, the call is inlined. The definition's own operations get the expanded node's permissions in
addition to their own (design 6.4). Nothing calls it by default (design 9.3): a back end may
expand in its private copy of the program; the test pass `test::expand_definitions` expands every
node it can, to compare kernels with their definitions.

### 2.4 Kernels against definitions

On `std::nvm`, `std::fvm`, `std::jit` and `std::prl`, the kernels of `softmax`, `log_softmax`,
`mean`, `relu` and `sigmoid` give the same output as their expanded definitions, element for
element, on special values (NaN, infinities, signed zeros, subnormals), several shapes and axes,
an empty tensor and pseudo-random inputs (a NaN is compared as NaN: the printed text carries no
sign or payload).

The `log_softmax` kernel was changed to follow its definition (design Q10: the definition is
normative). It used to compute `(x - m) - f32(log(f64 sum))`, the logarithm taken in f64, which no
definition can say (tensors have no f64 elements); it now rounds the sum to f32, takes the
logarithm by the loop of the `log` operator and subtracts in f32. Its results may change in the
last bits; no expected output of the repository's tests changed.

Gradients: autodiff through a definition and the operator's own derivative rule are different
algorithms; they agree within a relative 1e-5 (measured: below 1.4e-7; `mean` and `relu`
exactly). Expanding before `std::macro` makes both sides go through the definition: equal.

## 3. Tests

- `test/plans/feat/modules/resources.plan.toml`: the registration checks, mapped regions, and the
  resolution in a program (`test::resources` on `test/cases/resources/effects.cml`): cells written
  through assignments, a var parameter, a capture and a recursion; an array sorted in place;
  ambient and external effects; the ordered pairs; `inspect --ops` shows the declared resources.
- `test/plans/feat/modules/definitions.plan.toml`: the definitions the tensor module declares and
  each refusal (`test::definitions`); the kernels against their expanded definitions on
  `std::nvm` and `std::fvm` (`test/cases/definitions/kernels.cml`, run with and without
  `test::expand_definitions`, the same golden output on four back ends); gradients through the definitions against the operators'
  own derivative rules; `inspect --ops` shows each definition.

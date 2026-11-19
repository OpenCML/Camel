# The case study: one training program, two rewrites, and the readings that pin them

`train.cml` is the paper's program: a linear model trained by gradient descent, its loss as a
library transform, and one gradient step that takes the model in and gives a new one back.
`edits.cml` is a pass library beside it holding the two rewrites the paper compares: one that loses
an effect, and one that breaks a type. `writings/` holds three more writings of the same program,
which change no meaning, for the comparison in the last section.

Every reading below was taken on 2026-11-15 from the tree at `96a27e435` with a Release build
(`npm run build`), and re-taken on the merge that brought this directory in. The program and both
rewrites are gated by `test/plans/feat/examples.plan.toml`
(`examples.case_study.train`, `.drop_print`, `.break_rate`).

`edits.cml` is a pass library, not an import of the program: a command names its directory as a
module root, which is what `CAMEL_PACKAGES` is. The commands below set it once:

```powershell
$env:CAMEL_HOME = "$PWD/out/latest"
$env:PATH = "$env:CAMEL_HOME/bin;$env:PATH"
$env:CAMEL_PACKAGES = "$PWD/examples/case-study"
```

## The program

```powershell
camel examples/case-study/train.cml std::nvm
```

```
loss before: 3.7500
loss after:  0.0000
[[1.9999925], [-0.99998784]]
```

The same three lines on `std::nvm`, `std::fvm`, `std::jit` and `std::prl` (the plan runs all four
and holds the output in `test/plans/feat/golden/examples/case_study_train.txt`). The loss goes from
3.75 to 0.0000 over 200 steps; the training step is `step`, and `train` is the recursion that
applies it.

Two computations in `main` do not depend on each other: `loss<w0, y>(X)` in the first line printed,
and `train(w0, X, y, 0.1, 200)`. The paper's claim about them is a claim about the graph, and the
third writing below is the measurement of it.

## Reading 1: what the program is made of

`edits::list` prints every node of every graph the entry function reaches: the operator it runs (or
`-` for a node that runs none) and the type it carries. It reads nothing but the graph.

```powershell
camel examples/case-study/train.cml edits::list std::nvm
```

```
- : () => int
- : int
- : Linear
- : f32[...]
- : f32[...]
- : f32[...]
tensor:matmul : f32[...]
tensor:subtract : f32[...]
tensor:multiply : f32[...]
tensor:sum : f32
:op/ftod : float
tensor:shape : int[]
- : int
:op/idx_arr : int
:op/ltod : float
:op/div_d : float
- : Linear
- : f32[...]
- : f32[...]
- : float
- : <layer: Linear, y: f32[...]> (X: f32[...]) => float pure
autodiff:grad : <Linear, f32[...]> (f32[...]) => { weight: f32[...] }
- : { weight: f32[...] }
nn:sgd : Linear
- : Linear
- : f32[...]
- : f32[...]
- : float
- : int
- : int
:op/eq_l : bool
- : Linear
- : int
- : Linear
- : int
:op/sub_l : int
- : Linear
- : int[]
- : f32[]
tensor:reshape : f32[4, 2]
- : int[]
- : f32[]
tensor:reshape : f32[4, 1]
- : int[]
- : f32[]
tensor:reshape : f32[2, 1]
- : Linear
- : Linear
- : float
- : int
- : Linear
- : float
- : string
:str/format : string
:io/println : string
- : float
- : string
:str/format : string
:io/println : string
- : f32[...]
:io/println : f32[...]
- : int
- : int
loss before: 3.7500
loss after:  0.0000
[[1.9999925], [-0.99998784]]
```

The listing walks the graphs in the order `graphs_of` gives them: the program's root (2 nodes), the
derivative's `loss` (14), `step` (8), `train` (13), and the entry (26). The last three lines are the
program's own output, because the pass hands the program on and it runs.

This inventory is the ground the two rewrites stand on. `:io/println` appears three times (two
printing a formatted string, one printing the weight, `f32[...]`), and the program holds exactly one
constant of type `float` (the learning rate, `0.1`) and exactly one constant of type `Linear` (the
starting model).

One interface gap this reading ran into, written down because the 9.5 work needs it as input:
`graph_of(p)` hands a pass the program's root (`__root__`), not the entry function, and `callee`
answers null for the root's indirect call, so the entry's body has no spelling-free accessor at all
-- a pass must walk `graphs_of(p)` and tell the graphs apart by their content. `stdlib/graph.cml` is
not changed here; filling that gap belongs to the spelling-invisible interface, not to this example.

## Reading 2: a rewrite that loses an effect — accepted

`edits::drop_print` erases every `:io/println` node of the program and commits.

```powershell
camel examples/case-study/train.cml edits::drop_print std::nvm
```

Nothing is printed and the exit code is 0: the commit takes the rewritten graph, and the program
then runs to its end with every print gone. The same on the other three back ends.

**Accepted, and nothing in section 4 of `docs/technical/17_graph_rewrite_api_contracts.md` says it
should not be.** The validator's clauses are about the shape of a graph that exists -- anchors,
ports, adjacency, branches, gates, callees, arity (clauses 1-7, and the branch-arm invariant of
4.1) -- and about the types on an edge (4.2). None of them asks whether the program still does what
it was declared to do. "The effects of the program are contained in what it declares" is R3
(`docs/design/effects/analysis.cn.md` 4.1), which is queued and not landed; until it is, a rewrite
that drops an effect is accepted, silently, and this reading is the evidence.

Stated plainly, because the paper needs the sentence: **today a pass may delete an external write
and the commit check accepts the graph.** That is not the pass's mistake -- 9.7 requires exactly the
opposite: "an external access, and the final write to a variable that outlives the current function,
can be deleted only explicitly, with an operation of the graph interface for deleting effects,
separate from `erase`, which deletes pure nodes". Neither that operation nor its refusal exists yet,
so `erase` deletes a `:io/println` like a pure node and the commit has nothing to say. This is the
honest state the paper has to report: the *refusal* half of the two examples is not available yet,
and the accepted half is what a rewrite can do today.

## Reading 3: a rewrite that breaks a type — refused

`edits::break_rate` puts the starting model where the learning rate goes: it selects the one
constant of type `float` and the one of type `Linear` by type and by being a constant, refuses when
either is not unique, and replaces one with the other.

```powershell
camel examples/case-study/train.cml edits::break_rate std::nvm
```

```
error[RuntimeDiag::UnhandledError]: An error value reached the exit of 'main' without being handled: GraphInvalid: Runtime graph 'main' breaks its edge invariants (1 violation(s)):
  graph 'main' node 13 (10): passes a Linear as ordinary argument 3 of 'train', which takes a float.
   --> examples/case-study/train.cml:24:1
    |
 24 | func main(): int sync {
    | ^^^^^^^^^^^^^^^^^^^^^^^
 25 |     let X = reshape<[4, 2]>([1.0, 0.0, 0.0, 1.0, 1.0, 1.0, 2.0, 1.0] as f32[])
    |     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
   ...
 33 | }
    | ^
note: the error was made by <graph::commit>, which records no position
help: A value of type T! that has failed must be opened before the program ends: give a replacement with '!!', pass it on with '->', or take it apart with 'match ... case error e'. The position is where the error was made (the definition of 'main' when the error records none).
```

Exit code 1; nothing runs. The refusal is **clause 2 of section 4.2**, "the types on an edge are
compatible" -- "调用的实参、结果与被调图的形参、结果相容": the commit's check of the edges
(`src/runtime/graph_edges.cpp`) finds a `Linear` in an argument slot `train` declares as `float`.

**One refusal, two error shapes, and a reader needs to know which one this is.** Section 4 says a
failure inside a pass is turned into `RuntimeDiag::PassInternalError` by the pass driver; that is
what a pass written in C++ gets, because the violation arrives as a thrown value. A pass written in
Camel gets the same violation back as an **error value** from `commit`, passes it on, and the
program stops at its exit with `UnhandledError` wrapping `GraphInvalid`. Both shapes are in the
repository: `test::drop_call_argument` (clause 7) is the C++ one,
`examples.case_study.break_rate` is the Camel one. The clause is the same; only the way the failure
travels differs.

## Writing freedom: three writings of the program that change no meaning

`writings/` holds three rewritings of `train.cml`:

| file | what changes |
| --- | --- |
| `writings/renamed.cml` | `w` is renamed `trained` (and `w0` `start`) |
| `writings/swapped.cml` | the `let X = ...` and `let y = ...` declarations are swapped |
| `writings/moved.cml` | `let w = train(...)` is moved after the first `println` |

None of the three depends on the `var`-to-SSA work, so all three are expected to have the same
`std::jgir canonical=true` output as `train.cml`, byte for byte. The comparison is one command:

```powershell
node test/tools/writing-freedom.mjs --group spelling
```

and it reports, for each writing, the first byte at which it parts from the program -- with context
on both sides and the shape of the difference -- or that it does not part at all (long payloads are
cut with `...` below; the tool prints them whole):

```
[graph] renamed identical bytes=29941 sha256=47df8ca7afa5f4e9
[graph] swapped differs first_difference_byte=2603 first_difference_offset=2604 line=132 column=36 original_bytes=29941 swapped_bytes=29941
  original  132 >                 "bits": "0x0000000040000000",
  swapped   132 >                 "bits": "0x000000003f800000",
  shape: graphs original=5 swapped=5
  shape: graph 1 nodes original=26 swapped=26
  shape: graph 1 node n4 original=DATA {"elems":[{"bits":"0x0000000040000000",...  swapped=DATA {"elems":[{"bits":"0x000000003f800000",...
[graph] moved differs first_difference_byte=7497 first_difference_offset=7498 line=356 column=12 original_bytes=29941 moved_bytes=30056
  original  356 >           "callee": "g4",
  moved     356 >           "id": "n14",
  shape: graph 1 nodes original=26 moved=26
  shape: graph 1 node n14 original=FUNC moved=DATA {"bytes":[108,111,115,115,32,98,101,102,111,114,101,...],"k":"string"}
```

Three readings, and only the first is what the paper says today:

1. **Renamed: identical.** 29941 bytes, same sha256. Design principles 9.5, "the graph interface
   does not see spelling by default": a change of names changes nothing in the canonical document.
2. **Swapped: not the same graph, and not meant to be.** Both documents hold 63 nodes and 93 edges.
   The two constants `[2, -1, 1, 3]` (the `y` array) and `[1, 0, 0, 1, 1, 1, 2, 1]` (the `X` array)
   stand at canonical positions `n4` and `n6` (with their shape nodes `n5` and `n7`) one way in one
   document and the other way in the other. That is not a permutation of one numbering: each constant
   reaches its value through a `tensor:reshape`, and a reshape may fault, so the two reshapes lie on
   the timeline in the order the declarations are written -- principle 3.4, an operation that may
   fault is not free to be arranged. Swapping the two `let`s swaps which of the two fails first, so
   the two programs are **not** equivalent and their canonical documents are rightly different.
   There is nothing left to repair here: `camel inspect --ties` reports no tie group in either
   writing (`0 group(s) wider than one node, widest 0`), so the numbering never had two
   interchangeable constants to choose between, and no canonicalization can make these two
   documents equal.
3. **Moved: one more edge, so not the same graph.** 63 nodes on both sides but **94 edges** against
   93, and the extra one is an ordering input on the `train` call, coming from `:io/println`. A sync
   block is a chain: "in a sync block the later statements depend on the earlier ones through sync
   edges" (9.5, and the fault-propagation bullet before it). Written first, the call precedes the
   print and takes no order input; written after the first print, the print comes first and the call
   waits on it. So the two computations are *not* independent in the graph today, and no
   canonicalization can make these two documents equal -- the paper's sentence about them needs the
   edge named, or a change that lets a call which cannot fault leave the block's chain.
   -> It is a red of rework item 4 (a pure call placed on a `sync` block's chain, principle 3.4:
   `train` is recursive, and 3.4 counts a recursive call as possibly faulting) --
   `docs/handover/reviews/questions-2026-10-29.md`, the rework section of 2026-11-15 night, item 4 --
   and not a defect of this example.

## The `var`-to-SSA acceptance (red, and red for one reason)

`test/cases/compile/var/step_var.cml` and `step_let.cml` write one gradient step two ways -- an
assignment to a `var`, and the equivalent chain of `let` bindings -- and
`test/plans/feat/compile/var.plan.toml` requires their canonical documents to be the same bytes,
with and without the derivative:

```powershell
node test/tools/writing-freedom.mjs --group var
camel test/cases/compile/var/step_var.cml std::jgir canonical=true
```

It is **red** on this tree, and red for one reason: the `var` writing still builds a unit. Its
document parts from the `let` writing at byte 4083 (line 201, column 18), 10930 bytes against 9179,
and the whole difference is `:var/new` plus three `:var/get` and one `:var/assn_d` -- 18 nodes
against 13 in the step's graph. `grad` refuses the `var` writing before it builds anything: "a
variable of a differentiable type that is declared inside the differentiated function is read there.
Gradients do not flow through variables: bind the value with `let` instead." The pair goes green when
a unit is lowered to SSA (the queue's item 1); it is not in `known_fail`, because a red here is the
statement that the work is not done.
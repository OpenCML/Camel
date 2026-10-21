# Values with several writings

Step 7 of the graph store (design: [graph store](../design/graph-store/README.cn.md) 3.1, 3.4-3.7,
4.1; rules: [rewrite](../design/rewrite/README.cn.md)). This note records what the code does.

## 1. The model

A node already is a value: node inputs name nodes. **Several writings of a value are several
nodes declared equivalent.** Nothing about the node record, the input tables or the chunks
changes. A graph that has any such declaration is not in the *executable state*; extraction
brings it back.

What a graph keeps is the **log of equivalences** (`WritingUnion` in `graph.h`: two nodes, the
numeric permissions the equivalence needs, a flag for the ones congruence closure found, the rule
that asserted it). It is a column of the graph block, `GCGraph::writings()`, copied by the
encoder and read back by `GraphDraft::decode`. The classes, the congruence index and everything
else are derived from the log whenever a draft needs them (`WritingStore`, `DraftWritings` in
`writings.h`). A graph without equivalences has no column and `kGraphFlagHasWritings` clear: it
pays one flag test.

Properties that make this safe:

- Rules only add nodes and never change the inputs of a node, so **the node-level graph stays
  acyclic**; only the classes can be cyclic (`x == x * 1`). Extraction never chooses a writing
  that reads its own value (section 5).
- The equivalence log is **content**: `sameContent` compares it, the content hash of
  `std::opt` includes it, the validator checks every entry (nodes exist, same type, same
  region, no control input). A commit that only adds writings is a new version that shares
  every node chunk but the last.

## 2. Classes (`WritingStore`)

Union-find per **permission context**: the layer of context `S` joins the nodes of every
equivalence whose permissions are within `S`. The context of a node is the numeric policy's
default grants plus the node's own permissions (`NumericPolicy::effective`). At most four
layers. The root of a class is its smallest node, members are listed ascending, and no hash
table is ever walked, so results do not depend on the order of the merges.

## 3. Congruence (`DraftWritings`)

Pure nodes with the same key (kind, own permissions, region, type, operator or constant bits,
classes of the inputs) are one value. Merging two values repairs the users of the smaller class
until nothing merges (egg's `rebuild`). Only a pure function of its inputs takes part: constants
(bit for bit, so `0.0` is not `-0.0`; not GC-traced), conversions, accesses, pure operators; not
a node with a control input, not one written in place, not one reading a variable. Every other
node is alone in its class. (A pure node with control inputs does not take part in congruence,
but a rule can be rooted at it: section 4.) Nodes of different permissions or regions are never merged.

While a draft has equivalences, every member that changes or removes a node refuses
(`std::logic_error`) unless it runs inside `GraphDraft::WritingScope`, which the engine opens.
Adding nodes is allowed.

## 4. Rules and saturation

`Rule { lhs, rhs, needs, when }` over `Pattern`s (variables, constant-only variables, literals,
operators, computed constants). `DraftWritings::saturate(rules, Budget)`:

1. all matches of a round are found before any is applied (matches are rooted at a node, and
   below the root any writing of the value read); a rule applies to a node only if the node's
   context has `needs` and only to children with the root's own permissions;
2. the right side is built with `lookupOrAdd` (existing writings are found, not duplicated)
   and merged with the matched node under `needs`;
3. stop when a round finds nothing new or a **count** budget (rounds, new writings, unions) is
   used up. There is no time budget: the same rules, graph and budget give the same writings.

**Control inputs.** A pure node that waits for something (a `sync` function's sum waits for the
`println` before it) can be the root of a rule, never an inner node of its left side. The
writing the rule adds has no control inputs; extraction gives the writing that stays the control
inputs of every writing of its value that had some (mapped to the writings chosen for them), so
the order the program wrote is kept. Rules are tried only at the nodes whose operator is the
head of their left side (an index built at the start of each round).

The rules of `test/passes` (`test::saturate`) are the local algebra of `std::opt::algebra`: exact
commutation, neutral elements of wrapping integers, `x*c + x*c`, `x*c1 + x*c2`, `(x*c1)*c2`
(the last two need `reassociate`, the last also `no_infs`, for floats). Extracted by node count
they leave the same operators as `std::opt::algebra` on the algebra corpus under the default and
the strict policy. The difference: `shared_product` (a `sync` function) keeps its sum, because a
node with a control input takes no part.

## 5. Extraction

`DraftWritings::extract(CostModel)`: the cost of a writing is its own positive cost plus the
costs of the values it reads (tree cost); repeated to a fixed point, ties to the smallest node.
Because a writing's cost exceeds that of each value it reads, a writing that reads its own value
is never chosen. Then: nodes that stay read the chosen writings of their inputs; the entry,
exit, output, return and region results take the chosen writing; the others are erased; the
equivalences are dropped. Cost models are replaceable and registered by name (`nodes`,
`weighted`); the hook receives the node and a `CostContext` (draft and context) so a target can
read facts.

**What extraction keeps** is the retention boundary of the design (graph store 3.5 and 3.6,
design principles 3.1): extraction does not decide which side effects happen, so it keeps every
node with a side effect, every node that carries the program's order, and every value written in
place; a pure node is kept only when it is reachable from what the graph names (ports, entry,
exit, return, region results). The code asks the first two questions with `writtenInPlace` and
the same `carriesOrdering` rule the generic rewrite pass uses: an operator that is not pure, a
function or call node that runs a graph, a branch, join, sync or loop, and a gate with a control
input. A pure node with control inputs is a pure node — the control input orders it, it does not
make it an effect — so it stays only when something that stays reads it, as a value or as a
control input. A pure writing that is a candidate of a class and read by nothing is erased.

## 6. The pipeline

Back ends and exporters keep refusing a graph with writings (`GCGraph::execPlan`,
`RunPrograms::begin`). The pass driver extracts first, with the `weighted` model, before any
pass that consumes the program (visible in the `Pass` info log). A rewrite pass that does not
declare `acceptsWritings()` gets a diagnostic instead of the program.

## 7. Open points

- **The retention boundary** (fixed, H63). Section 5 used to describe a boundary the code did not
  implement: `DraftWritings::extract` kept every node with `!multi[node] && !takesPart(node)`,
  which also selected pure nodes with control inputs (`shapeOf` returned 3) and kinds
  `shapeTakesPart` does not cover, notably a GC-traced constant. A pure node that nothing reached
  stayed in the extracted program; a rule whose right side writes a string constant left one dead
  constant per saturation round. `extract` now selects by the boundary itself
  (`writtenInPlace(node) || carriesOrdering(draft_, node)`) and then by reachability from what the
  graph names. Regression test: `rules.extract_keeps_no_unreachable_pure_node` saturates
  `s + 'y'` four rounds and checks that the extraction removes the 15 dead constants the added
  writings wrote; `writings.extract` and the other plans cover that effects, orders and written
  variables still stay.
- Matching tries each rule at every node with its head operator each round, rebuilding that index
  every round; an index kept up to date between rounds would only matter for graphs where most
  nodes are new each round.
- Measured on one function of 3n nodes (`wide_program(n)` of the scale tool, with the algebra
  rules): n = 10000, 40000, 80000 saturate in 89, 471 and 1008 ms (find 48/229/476, apply
  40/239/527, extraction 23/209/450): linear. Asking, for every candidate, whether a value is
  written in place was walking its users each time (1.6 s at n = 20000); it is asked once per
  node now.
- `jitFnToGraph_` entries of freed graphs and encoded graphs bound to no program stay until the
  context ends (graph space).

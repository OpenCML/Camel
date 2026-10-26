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
a node written in place, not one reading a variable. A control input takes no part in this
question: it orders the node, it does not make the value different (design principles 3.3, 3.4),
, so a pure node that waits for something takes part like any other writing of its value and
may be an inner node of a rule's left side (section 4). Extraction gives the writing that stays
the control inputs of every writing of its value (section 5). Every other node is alone in its
class. Nodes of different permissions or regions are never merged.

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
`println` before it) takes part in matching like any other writing: a law over data inputs is
sound whatever the node is ordered after, so the control input does not keep it out. What
the engine must not do is move the computation across the control edge's ordering, so a rule
match collects the control inputs of every operator term the law stops reading (the root
included), and the writing the rule builds waits for all of them. A rule whose right side is an
existing writing rather than a new operator is not applied to a match with waits, since it cannot
be given them; extraction would otherwise give the chosen writing the waits of the other writings
of its value (section 5), but a rewritten node is what carries the law's result here. Rules are
tried only at the nodes whose operator is the head of their left side (an index built at the start
of each round).

**Operand order.** A law is a statement about values, and `a + b = b + a` is a property of `+`,
declared per operator and per element type in `sem::OperatorSemantics::binary`
(`BinaryLaw.commutative`; op-semantics 7.4). The matcher reads a binary term in the order the
program wrote first, and tries the two inputs exchanged only when that order gives no accepted
match. So `x + 0 -> x` fires on `0 + x` when `+` declares commutativity for that element
type, while a law that writes its operands in a particular order still sees them there whenever
the program matches that order. The fallback is for a law that wrote only one order: a `<=>` rule
already states both directions, and a saturation applies its reverse as a rule of its own, so the
matcher does not commute a two-way rule again (doing so doubled the rearranging matches and took
`rules::saturate_all` past its 20 s budget). The replacement is built from the law's right side as
written: the matcher only decides which input is bound to which variable, it never reorders the
writing the law produces, and a law whose right side treats its variables asymmetrically is applied
to the same values, bound the other way round. Operators that declare no commutativity (`-`, `/`,
and integer tensor arithmetic) are never read the other way, because the exchange would change the
value. Mirrored rules in the library (`*_zero_add`, `*_one_mul`, ...) remain correct statements but
are redundant for matching: the first-written law now covers both orders.

The rules of `test/passes` (`test::saturate`) are the local algebra of `std::opt::algebra`: exact
commutation, neutral elements of wrapping integers, `x*c + x*c`, `x*c1 + x*c2`, `(x*c1)*c2`
(the last two need `reassociate`, the last also `no_infs`, for floats). Extracted by node count
they leave the same operators as `std::opt::algebra` on the algebra corpus under the default and
the strict policy. `shared_product` (a `sync` function) keeps two products because `t` is also read
by the `println`: the sum is rewritten to `x * 6` while `t` stays for the print, so both products
remain. (The old path went through `float_add_self` to `t * 2`, which `float_fold` could not
reduce without `no_infs`; with the matcher reading the operand order, `float_combine` or
`float_double` reaches `x * 6` directly.)

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
`RunPrograms::begin`). The pass driver asks the pass before it extracts: a pass that declares
`acceptsWritings()` is given the program as it is — the dumps read that state, and
`std::opt::saturate` is the pass that produces it — and every other pass is given one writing per
value, extracted first with the `weighted` model (visible in the `Pass` info log). A pass that
neither consumes the program nor declares that it accepts writings gets a diagnostic instead of
the program.

`std::opt::saturate` reaches that state from the command line: it runs the standard rules library
(`stdlib/rules.cml`) for `rounds` rounds (6 by default; `strict` keeps the laws that hold bit for
bit, as `safe_fp` does for `std::opt`) and extracts nothing. It is the same saturation
`saturate_all` runs, and the rules are one list (`saturateList`), so the pass and the library
cannot drift apart.

## 6.1 What a dump draws

`std::gir`, `std::rgir` and `std::jgir` declare `acceptsWritings()` — and so do the dumps that
share their pass, `std::gir_regions`, `std::rgir_regions`, `std::agir` and `std::argir` — so the
driver hands them the whole state and they read it without editing it:
`graphWritings(program, graph, model, ctx)`
decodes a copy of the graph and asks `DraftWritings::choiceByCost`, the cost fixed point
extraction itself applies. One implementation reads what extraction would do, so the picture and
the pipeline cannot disagree about it. A graph with one writing per value prints none of this, so
every existing dump is byte for byte what it was.

- Every value with more than one writing is a dashed cluster (`subgraph cluster_class_…`): a class
  node stands for the value, and one node per writing of it is inside, with `class="c<root>"`.
- The writing extraction would take carries `chosen="true"`, and a heavier border in the styled
  dump; the graph names the model that chose (`writings_cost="weighted"`), the model the pipeline
  itself extracts by.
- An input edge names a *value*, not a writing: it leaves the class node of the value the writing
  reads, keeping the label `"<out>|<in>"` of every other edge. The dumps draw the consumer as the
  source and the input as the target, where egg's `dot` draws the opposite direction; the shape of
  a class is egg's, the arrow direction is the dumps'. Control inputs and ports are untouched: a
  control input orders a node, it is not a value (design principles 3.3).
- `std::jgir` carries the classes too, as `classes` on each graph (`id`, `writings`, `chosen`,
  `cost`), and `std::jgir egraph=true` writes the document `egraph-serialize` reads instead:
  `nodes` (every writing, its operator, the classes of the values it reads, the class it belongs
  to, its own cost), `root_eclasses` (the value each graph returns) and `class_data` (what
  computing a value costs). That is the shape the community's `egraph-visualizer` opens; it was
  written from that crate's documented shape, which no machine here can fetch to compare against.
- The canonical document (`std::jgir canonical=true`) is a dump of one writing per value and
  carries no classes: other readers compare it byte for byte, so its shape does not move.

## 7. Open points

- **The retention boundary** (fixed). Section 5 used to describe a boundary the code did not
  implement: `DraftWritings::extract` kept every node with `!multi[node] && !takesPart(node)`,
  which also selected pure nodes that do not take part and kinds `shapeTakesPart` does not cover,
  notably a GC-traced constant. A pure node that nothing reached
  stayed in the extracted program; a rule whose right side writes a string constant left one dead
  constant per saturation round. `extract` now selects by the boundary itself
  (`writtenInPlace(node) || carriesOrdering(draft_, node)`) and then by reachability from what the
  graph names. Regression test: `rules.extract_keeps_no_unreachable_pure_node` saturates
  `s + 'y'` four rounds and checks that the extraction removes the 15 dead constants the added
  writings wrote; `writings.extract` and the other plans cover that effects, orders and written
  variables still stay.
- **Control inputs in matching** (fixed). Section 4 used to say a pure node that waits can
  be a rule root but never an inner term, and the code expressed that as a third shape
  (`shapeOf` returned 3) so congruence would not merge it. The guard was about ordering, not
  about shape or law soundness: `rewrite` only transferred the *root's* control inputs, so a
  rewritten inner term would have been moved across the effect's ordering. It now transfers the
  waits of every operator term the law stops reading, and the shape is gone: a control input
  neither keeps a node out of its class nor out of a term.
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

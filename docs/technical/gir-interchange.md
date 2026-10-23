# GIR exports

The standard export passes consume a materialized runtime graph and print it without running it.
They are registered directly, not through a historical chain of aliases.

| Pass | Format | Purpose |
| --- | --- | --- |
| `std::gir` | Compact DOT | Graph structure and semantic metadata; default DOT representation |
| `std::rgir` | Rich DOT | The same graph with Graphviz shapes, sizes and tooltips |
| `std::jgir` | JSON | Versioned, structured interchange for editors and other tools; `canonical=true` writes the canonical document of the equivalence checker |
| `std::agir` | Annotated compact DOT | `gir` plus node roles and a legend |
| `std::argir` | Annotated rich DOT | `rgir` plus role styling and a legend |

`r` means **rich**, and `a` means **annotated**. Pass-dump snapshots use `.gir.dot` for compact
DOT.

## JSON version 1

The top-level object contains `format: "camel.gir"`, `version: 1`, the root graph ID in
`root`, and an array `graphs`. Consumers must reject unsupported format versions.

Each graph contains:

- `id`, `name`, `signature`: export-local identity and human-readable information.
- `dependencies`: IDs of the graphs it references. References may be shared or recursive;
  this is not a nested tree, and names need not be unique.
- `nodes`: real runtime nodes. Each has a document-unique `id`, `kind`, display-only `label`
  and `outputType` (a source-spelled type, or `null` when unavailable).
- `edges`: `source` and `target` node IDs, `kind` (`normal`, `context`, `control`),
  `sourceIndex` in the source's unique-consumer list of that kind and `targetIndex` in the target's
  incoming list. Parallel/repeated inputs are separate edges.
- `normalPorts`, `contextPorts`, `capturePorts`: ordered input-node IDs.
- `exit`: the real output-node ID. Visual ARGS/EXIT markers belong to the viewer and are not
  serialized as runtime nodes.

Nodes may additionally contain:

- `parameter: { name, kind, index }`, where kind is `normal`, `with` or `capture`.
- `callee`: the graph ID of a direct FUNC call or a static function value, never a graph
  name extracted from a label.
- `loop: { kind, condition, body, conditionRegion, bodyRegion }`: `conditionRegion` and
  `bodyRegion` are the ids of the regions of the graph that hold the condition and the body;
  `condition` and `body` are null (the node names no graph); `conditionRegion` is 0 for a plain
  `for` loop. Kind is `while`, `for`, or `for-while`.
- `origin`: a decimal string preserving the complete uint64 source identity.
- `source: { file, start: { line, character }, end: { line, character } }`: zero-based
  source positions with an exclusive end, matching the DOT origin metadata.
- `roles`: semantic role names; `numeric`: explicitly granted numeric permissions.

IDs are consistent within one export; they are not persistent identities across recompilations.
Use source origins for debugger correlation. Output types describe the actual node value:
for example, a BRCH selector and its JOIN result need not have the same type. This export does
not define a new language-level interface declaration syntax.

JSON carries neither Vue Flow components nor coordinates. The editor validates its references,
creates its own synthetic boundaries and uses generated DOT only to obtain Graphviz layout.
Its text/copy/export view retains the original JSON, not the generated layout DOT.

Repeated reads of one source by the same consumer retain separate input edges and target
indices, sharing their source-side consumer index. They do not require duplicate use entries.

## Canonical documents (version 2)

`std::jgir canonical=true` writes a second, version 2 document, `canonical: true`. It is the
input of the equivalence checker used by the graph-to-source work; it is not a replacement for
the version 1 document, which is unchanged and stays the default.

It is a new version, not a new optional field of version 1: the fields version 1 requires
(`name`, `label`, `outputType`, `origin`, `edges`) are exactly the ones a canonical document
must not carry, so a reader of one version cannot be given the other. Readers already reject
unsupported versions, so version 2 is refused by every existing consumer instead of being
misread.

A canonical document keeps what equivalence is defined on, and nothing else:

- `format`, `version: 2`, `canonical: true`, `root` (the graph ID), `entry` (the graph the
  root's entry call names, or `null`).
- `graphs`: reachable graphs only, live nodes only.
  - A graph has `id`, `flags`, `signature` (an index into `types`), `nodes`, `regions`,
    `normalPorts`, `contextPorts`, `capturePorts`, `exit`, and `root: true` on the root graph.
  - A node has `id`, `kind`, `type` (an index into `types`), `inputs` (each
    `{kind, slot, source}` with kind `normal`, `context` or `control`; `source` is a node ID or
    `null`), and, when they apply, `attrs` (tag atom to value), `perms` (the numeric permission
    mask), `param` (`{kind, index}`), `callee` (a graph ID), `loop`
    (`{kind, collectionCount, condition, body, conditionRegion, bodyRegion}`, graph and region
    IDs), `value` (a static value) and `payload`.
  - A region has `id`, `kind`, `index`, `parent` (0 is the body), and, when they apply, `owner`,
    `result`, `ports`, `params` (`{with, norm}` type indices) and `attrs`. Annotation regions
    (`inline`, `block`) are dissolved: their nodes and sub-regions belong to the nearest
    enclosing control region, because an inlined body is not a different program.
- `tags`: the tag atoms. A node's and a region's attributes name one; the key names are not
  part of a program, so two dumps may number them differently and still be equal.
- `decls`: the named type declarations as atoms (`{id, kind, params, variants}`). Two
  declarations with equal fields stay two atoms (nominal typing); declaration names are not
  part of a program.
- `types`: a structural table. Types that are the same structure are one entry, even when the
  front end keeps them apart because their parameter names differ: parameter names are not part
  of a program. `{k: "int"|"float"|"bool"|"void"|"string"|...}` for scalars;
  `{k: "array", elem}`; `{k: "tuple", tag, elems}`; `{k: "struct", decl, args, fields}` or an
  anonymous `{k: "struct", fields}`; `{k: "func", mods, rest, with, norm, exit}`;
  `{k: "enum", decl, args, variants}`; `{k: "map"|"set", key, value}`; `{k: "bytes"}`. Every
  field and element reference is another table index.

Static values are written by value: integers as integers, floating-point by bit pattern,
strings by bytes, arrays, tuples, structs and variants element by element, collections as a
sorted entry list, and a function constant by the graph it names. Names, source origins and
source spans are not in the document, and neither are the original node, region or type
numbers: the IDs above are the export's own and carry no meaning across exports.

`entry` is the best available reading in this version: the entry graph is marked where the
root's exit chain calls a function constant directly. Hoisting an inlined entry into a virtual
`main` is not done yet; it belongs with the printer that makes it necessary.

## Equivalence checker

`test/tools/cml_roundtrip.py` compares two canonical documents, independently of the dump. It
refines vertex colors over the whole structure (graphs, nodes, regions, types, declarations,
tag atoms, with kinds, slots, payloads and values as labels), checks that every color class has
the same multiplicity on both sides, and searches for a structure-preserving bijection with
bounded backtracking. Names are not compared because they are not in the document.

    cml_roundtrip.py LEFT.json RIGHT.json [--max-nodes N] [--max-steps N]

It exits 0 when the documents are equivalent, 1 when they are not (printing the first
explainable difference: the vertex, its kind, neighbors, payload and type), and 2 when it ran
out of its vertex or step budget. A budget stop is reported and is never a pass.

# GIR exports

The standard export passes consume a materialized runtime graph and print it without running it.
They are registered directly, not through a historical chain of aliases.

| Pass | Format | Purpose |
| --- | --- | --- |
| `std::gir` | Compact DOT | Graph structure and semantic metadata; default DOT representation |
| `std::rgir` | Rich DOT | The same graph with Graphviz shapes, sizes and tooltips |
| `std::jgir` | JSON | Versioned, structured interchange for editors and other tools |
| `std::agir` | Annotated compact DOT | `gir` plus node roles and a legend |
| `std::argir` | Annotated rich DOT | `rgir` plus role styling and a legend |

`r` means **rich**, and `a` means **annotated**. The retired `dot`, `rdot`, `graphviz`,
`readable_graphviz`, `annotated_graphviz` and `readable_annotated_graphviz` pass names are not
accepted. Pass-dump snapshots use `.gir.dot` for compact DOT.

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
  `sourceIndex` in the source's outgoing list of that kind and `targetIndex` in the target's
  incoming list. Parallel/repeated inputs are separate edges.
- `normalPorts`, `contextPorts`, `capturePorts`: ordered input-node IDs.
- `exit`: the real output-node ID. Visual ARGS/EXIT markers belong to the viewer and are not
  serialized as runtime nodes.

Nodes may additionally contain:

- `parameter: { name, kind, index }`, where kind is `normal`, `with` or `capture`.
- `callee`: the graph ID of a direct FUNC call or a static function value, never a graph
  name extracted from a label.
- `loop: { kind, condition, body }`: explicit graph IDs; `condition` is null for a plain
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

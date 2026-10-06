# Entry Parameters and the Export of a Pure Program

This note describes how a `main` with parameters is compiled, run and exported. The normative
text is `docs/design-principles.cn.md` section 2.7 (the rulings on `main` with parameters,
`with` parameters, automatic module loading, entry and export). User-facing behaviour is in
`docs/cli.md`.

## 1. The entry graph takes the parameters of `main`

The Builder (`src/compile/gir/builder.cpp`, `Builder::build`) wraps the entry function in the
root graph `__root__`, which calls `main` and returns its result. `main` used to be called with
no arguments whatever its signature, so every pipeline stopped with an internal arity error for
a `main` with parameters.

The root graph now has the same parameters as `main`: one PORT per `with` parameter, then one
per ordinary parameter, with the declared names, types and `var` flags, and its function type
says so. The ports are linked to the call of `main` (`with` ports as `with` inputs after the
function value, ordinary ports as ordinary inputs). Nothing else changes for a `main` without
parameters.

Who supplies the values is not decided at compile time. The entry graph is a function of the
program's inputs, and each consumer says what an open parameter means:

| Consumer | An open parameter is |
|---|---|
| a rewriting pass (`std::opt::*`, `std::macro`, `std::inline`) | an unknown value, as in any function |
| a dump (`std::rgir`, `std::gir`) | a PORT node named like the parameter |
| a scheduling pass | an error: `EntryArgumentMissing` |
| `onnx::export` | an input of the model |

The origin of `main` is also registered under the fixed key `__entry__`
(`execute::kEntryFunctionOriginKey`), because passes replace the entry graph and its stable id;
`execute::entryFunctionOrigin(ctx)` is where a diagnostic about the entry function is located.

## 2. Binding arguments from outside (`include/camel/execute/entry_args.h`)

`bindEntryArguments(ctx, root, arguments)` is partial application of the entry graph. For each
`name=text` it finds the parameter, reads the value according to the declared type, and rewrites
the graph in one draft session: the value is materialized as a static constant, the port's uses
are redirected to it, the port is removed, the function type is rebuilt for the remaining ports,
and node types are re-inferred (a tensor read from a file has an exact shape where the
declaration may leave an extent open). The driver calls it before the first pass
(`tools/camel-cli/main.cpp`, `timeit.cpp`; the option is `--arg`).

Reading a value:

- scalars are literals; a struct or tuple is a directory with one file per leaf, named by the
  leaf's dotted path (the layout `nn.save_params` writes); nested aggregates do not nest
  directories;
- a module-defined type is read by the loader its module registered with
  `registerEntryArgumentLoader(typeCode, loader)`. The core knows no tensor: the tensor module
  registers a loader that reads a `.npy` file and checks it against the declared type with the
  type's own `castSlotFrom`.

Values are rooted while the others are read (reading allocates and may move objects), and every
bound object stays rooted for the life of the process. `boundEntryArguments()` lists them with
the parameter they were given for; a translation uses the list to name what it emits.

The pass driver (`applyPassesDetailed`, `src/execute/pass.cpp`) checks
`unboundEntryParameters(graph)` before a scheduling pass runs and reports
`RuntimeDiag::EntryArgumentMissing` at `main`, naming them. The check is in the driver, not in
each scheduler, so it holds for every scheduling pass, present and future.

The result goes out the same way: every scheduling pass already hands the entry result to
`Context::captureProcessExitCode`; the context passes it on to a handler when one is set, and
`writeEntryResultTo(ctx, path)` (`--result`) sets one that writes the value in the layout the
loaders read (`registerEntryResultSaver` for module-defined types).

## 3. Passes of modules the program does not import

`requireKnownPasses(passes, ctx)` (`src/execute/pass.cpp`) checks every pass name before
anything runs. For a name `<module>::<pass>` that is not known it first imports `<module>`
through the context, exactly as an `import` statement would; a module registers its passes
when it loads. Failure to find the module is not reported by itself: the name stays unknown and
gets the usual `UnrecognizedGraphPass`.

## 4. `onnx::export` on a program without a request (`modules/onnx/passes/export.cpp`)

The pass first looks for `onnx.export_model` requests in the entry graph, as before. When
there is none:

- if a request exists in a graph reachable from the entry graph, the program is a driver
  program whose `main` was not inlined: the old refusal naming `std::opt::simplify` is kept;
- otherwise the program makes no request, and the entry graph itself is translated
  (`translateEntryGraph`, `modules/onnx/exporter.cpp`).

`translateEntryGraph`:

1. declares the model inputs from the function type of the entry graph, `with` parameters
   first (`DeclaredInputBinder`): a tensor type must carry its element type and shape, a
   negative extent becomes a symbolic dimension `<name>_dim<axis>`, a scalar is a rank-0
   input, a struct or tuple is flattened to one input per leaf (`p_fc1_w`). Nothing is read
   from an example value, unlike `export_model`: the declared types are the examples;
2. names the constants that came from `--arg` after their parameter and fields
   (`boundEntryArguments`), so they appear as named initializers;
3. hands the graph to the part of the exporter shared with the two function entries
   (`translatePrepared`): the `std::opt::simplify` fixpoint with export settings (everything
   non-recursive inlined, which removes the call of `main` when no earlier pass did), the
   effect check, the node-by-node translation, and one output per leaf of the result.

The pass performs no purity analysis of its own. What makes a program untranslatable is found
where the translation meets it and reported there; the message of such a refusal adds that
the pass was translating `main` as a whole and names the other way to export
(`onnx.export_model`).

### What decides between initializer and input

Nothing in the exporter. A parameter is an input when it is still a parameter of the entry
graph and an initializer when it has become a constant, and it becomes a constant when its
value is given with `--arg` at export. The convention the language suggests (data as ordinary
parameters, weights as `with` parameters) therefore reads naturally on the command line:

    # weights baked in, data as input
    CAMEL_ONNX_OUT=m.onnx camel model.cml --arg p=weights std::macro std::opt::simplify onnx::export
    # weights as inputs (and, for a training step, as outputs)
    CAMEL_ONNX_OUT=m.onnx camel step.cml std::macro std::opt::simplify onnx::export

An ordinary parameter given with `--arg` is baked in the same way; the exporter does not treat
the two kinds differently beyond their order.

## 5. Limits

- An element type or shape that the declaration leaves out is not defaulted (no implicit
  float32); the parameter is refused unless its value is given.
- Dynamic axes come from the declared types only; there is no `dynamic_axes` or `target`
  option for the entry export (the target is the default, `onnxruntime-cpu`).
- `CAMEL_ONNX_OUT` is the only way to name the file, until passes take arguments.
- `--result` is not applied under `--timeit`.
- `std::cpp` does not translate a `main` with parameters: it refuses with `TranslationRefused` at
  `main` (the generated C++ program calls the entry function without arguments).
- An optimizer state (`nn` Adam state) is not accepted as a parameter type on the command line
  or as a declared model input.

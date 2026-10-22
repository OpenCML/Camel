// The H194 gap ledger: what each entry of `stdlib/signatures/*.cml` is, and what it would take to
// replace it with a declaration.
//
// The directory holds the reference-only entries of the signatures a declaration cannot write (the
// `computed` field of `scripts/stdlib-reference.mjs` names it per module). This file classifies
// every entry into exactly one of four buckets, with the mechanism that decides it, so the
// directory's size is a decision the author can take rather than an unexplained residue. The
// page is `docs/technical/49_computed_signatures.md`; the checker is
// `scripts/stdlib-declarations.mjs --check` (rule 7), which calls `classificationProblems()`.
//
// A record is `[name, uri, bucket, mechanism, evidence?]`. `uri` is the exact `#op` of the entry
// or `<module>:<name>` when the file writes none. `evidence` overrides the module's default; it
// is the C++ place whose behaviour the bucket names (a `file:line`, or a section of
// `docs/technical/47_declaration_syntax.md`). The order of the records of a module is the order of
// the entries in its file: a module may record the same name and URI twice with different
// classifications (`tensor:sum` is whole-tensor and axis forms), so the checker matches the file
// and the ledger position by position.
//
// The buckets:
//
//   1 `declarable-today`          a finite set of declarations with today's syntax (overloads per
//                                  arity and spelling, `typeas` for an open parameter type)
//                                  reproduces the resolver exactly. Nothing to decide; the entry is
//                                  only here because the name was not migrated (`unmigrated-module`)
//                                  or was written off as computed by design (`misjudged-computed`).
//   2 `needs-one-feature`         the result or a parameter type depends on an argument, on a
//                                  call-site type argument, or on the argument's structure; one of
//                                  the mechanisms below, each a question or a decision recorded in
//                                  47, is the smallest thing that would move the entry.
//   3 `permanently-reference-only` no declaration can be right: there is no declaration host
//                                  (`globals`) or the standard library's own one-name-one-source
//                                  rule makes a declaration a second signature (`range`, `sum`,
//                                  `mean`).
//   4 `not-a-signature`           a doc entry, or a name with no registered operator: it does not
//                                  belong in `signatures/` at all. Empty at this base: every one of
//                                  the 210 entries names an operator `inspect --ops` prints, and
//                                  the rest of the directory is comments.
//
// A bucket and a mechanism are tags, never numbers: the checker rejects an entry whose bucket is
// not a `BUCKETS` key or whose mechanism is not a `MECHANISMS` key, and a mechanism belongs to
// exactly one bucket (so a classification cannot name a bucket that has no tag). To classify a new
// entry, add a record and `node scripts/computed-gaps.mjs` prints the counts; to change a bucket,
// change `BUCKETS`/`MECHANISMS` here, not a numeric label elsewhere.

import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

export const BUCKETS = {
    'declarable-today': { id: 1, title: 'a finite set of declarations reproduces it today' },
    'needs-one-feature': { id: 2, title: 'one syntax feature or policy decision would move it' },
    'permanently-reference-only': { id: 3, title: 'no declaration can be right' },
    'not-a-signature': { id: 4, title: 'not a signature at all' },
}

export const MECHANISMS = {
    'unmigrated-module': {
        bucket: 'declarable-today',
        summary: 'the signature is fixed and a declaration could be written; the module was simply not migrated',
    },
    'misjudged-computed': {
        bucket: 'declarable-today',
        summary:
            'recorded as a computed signature, but a finite set of declarations with overloads and typeas reproduces the resolver exactly',
    },
    'result-from-argument': {
        bucket: 'needs-one-feature',
        summary: 'the result dtype, shape or axis is computed from an argument, which a declaration cannot write',
    },
    'type-argument-return': {
        bucket: 'needs-one-feature',
        summary: 'the result type is selected by the call-site type argument, which the declaration cannot see (47 section 3.4, Q1)',
    },
    'type-predicate': {
        bucket: 'needs-one-feature',
        summary: 'the accepted calls are a predicate on a type variable, element dtype or leaf type (47 section 3.5, Q3)',
    },
    'arbitrary-function-type': {
        bucket: 'needs-one-feature',
        summary: 'a parameter is a function of unknown parameter list and result (47 section 3.1, Q-F)',
    },
    variadic: {
        bucket: 'needs-one-feature',
        summary: 'any number of arguments, which needs the designed variadic parameter (47 section 3.2, Q-V)',
    },
    'module-type-argument': {
        bucket: 'needs-one-feature',
        summary: 'a type variable as the argument of a module type, such as PyObject<T> (47 section 3.3, Q-M)',
    },
    'structural-type-function': {
        bucket: 'needs-one-feature',
        summary: 'the result type is the argument structure with each leaf changed (47 section 4.4; design principles 5.5, phase 2)',
    },
    'no-operator-host': {
        bucket: 'permanently-reference-only',
        summary: 'a compiler builtin of the empty-name module: there is no module to declare, and the file cannot be imported',
    },
    'one-name-one-source': {
        bucket: 'permanently-reference-only',
        summary: 'the whole-tensor and axis forms share one name; a declaration would be a second signature (47 section 4.2)',
    },
}

// `module` is the module the entries belong to, `file` its signature file, `evidence` the default
// place for that module's rows. `entries` must list the file's entries in file order.
export const LEDGER = [
    {
        module: 'autodiff',
        file: 'stdlib/signatures/autodiff.cml',
        evidence: 'modules/autodiff/operators.cpp:55-98,150-153',
        entries: [
            ['grad', 'autodiff:grad', 'needs-one-feature', 'arbitrary-function-type'],
            ['with_grad', 'autodiff:with_grad', 'needs-one-feature', 'arbitrary-function-type'],
        ],
    },
    {
        module: 'ckpt',
        file: 'stdlib/signatures/ckpt.cml',
        evidence: 'modules/tree/module.cpp:30-60',
        entries: [
            ['tree_map', 'ckpt:tree_map', 'needs-one-feature', 'structural-type-function'],
            ['tree_reduce', 'ckpt:tree_reduce', 'needs-one-feature', 'structural-type-function'],
            ['tree_unzip', 'ckpt:tree_unzip', 'needs-one-feature', 'structural-type-function'],
            ['tree_leaves', 'ckpt:tree_leaves', 'needs-one-feature', 'structural-type-function'],
            ['tree_paths', 'ckpt:tree_paths', 'needs-one-feature', 'structural-type-function'],
            ['save_tree', 'ckpt:save_tree', 'needs-one-feature', 'type-predicate', 'docs/technical/47_declaration_syntax.md:§3.5'],
            ['load_tree', 'ckpt:load_tree', 'needs-one-feature', 'type-argument-return', 'docs/technical/47_declaration_syntax.md:§3.4'],
        ],
    },
    {
        module: 'globals',
        file: 'stdlib/signatures/globals.cml',
        evidence: 'src/builtin/modules/globals.cpp:2144; docs/stdlib-conventions.cn.md:40',
        entries: [
            ['std::mem::blocks', ':op/mem_blocks', 'permanently-reference-only', 'no-operator-host'],
            ['int', ':op/*tol', 'permanently-reference-only', 'no-operator-host'],
            ['int', ':op/*tol', 'permanently-reference-only', 'no-operator-host'],
            ['int', ':op/*tol', 'permanently-reference-only', 'no-operator-host'],
            ['int', ':op/*tol', 'permanently-reference-only', 'no-operator-host'],
            ['int', ':op/*tol', 'permanently-reference-only', 'no-operator-host'],
            ['i64', ':op/*tol', 'permanently-reference-only', 'no-operator-host'],
            ['i64', ':op/*tol', 'permanently-reference-only', 'no-operator-host'],
            ['i64', ':op/*tol', 'permanently-reference-only', 'no-operator-host'],
            ['i64', ':op/*tol', 'permanently-reference-only', 'no-operator-host'],
            ['i64', ':op/*tol', 'permanently-reference-only', 'no-operator-host'],
            ['i32', ':op/*toi', 'permanently-reference-only', 'no-operator-host'],
            ['i32', ':op/*toi', 'permanently-reference-only', 'no-operator-host'],
            ['i32', ':op/*toi', 'permanently-reference-only', 'no-operator-host'],
            ['i32', ':op/*toi', 'permanently-reference-only', 'no-operator-host'],
            ['i32', ':op/*toi', 'permanently-reference-only', 'no-operator-host'],
            ['float', ':op/*tod', 'permanently-reference-only', 'no-operator-host'],
            ['float', ':op/*tod', 'permanently-reference-only', 'no-operator-host'],
            ['float', ':op/*tod', 'permanently-reference-only', 'no-operator-host'],
            ['float', ':op/*tod', 'permanently-reference-only', 'no-operator-host'],
            ['float', ':op/*tod', 'permanently-reference-only', 'no-operator-host'],
            ['f64', ':op/*tod', 'permanently-reference-only', 'no-operator-host'],
            ['f64', ':op/*tod', 'permanently-reference-only', 'no-operator-host'],
            ['f64', ':op/*tod', 'permanently-reference-only', 'no-operator-host'],
            ['f64', ':op/*tod', 'permanently-reference-only', 'no-operator-host'],
            ['f64', ':op/*tod', 'permanently-reference-only', 'no-operator-host'],
            ['f32', ':op/*tof', 'permanently-reference-only', 'no-operator-host'],
            ['f32', ':op/*tof', 'permanently-reference-only', 'no-operator-host'],
            ['f32', ':op/*tof', 'permanently-reference-only', 'no-operator-host'],
            ['f32', ':op/*tof', 'permanently-reference-only', 'no-operator-host'],
            ['f32', ':op/*tof', 'permanently-reference-only', 'no-operator-host'],
            ['c64', ':op/*toc', 'permanently-reference-only', 'no-operator-host'],
            ['c64', ':op/*toc', 'permanently-reference-only', 'no-operator-host'],
            ['c64', ':op/*toc', 'permanently-reference-only', 'no-operator-host'],
            ['c64', ':op/*toc', 'permanently-reference-only', 'no-operator-host'],
            ['c64', ':op/*toc', 'permanently-reference-only', 'no-operator-host'],
            ['c64', ':op/*toc', 'permanently-reference-only', 'no-operator-host'],
            ['c128', ':op/*toz', 'permanently-reference-only', 'no-operator-host'],
            ['c128', ':op/*toz', 'permanently-reference-only', 'no-operator-host'],
            ['c128', ':op/*toz', 'permanently-reference-only', 'no-operator-host'],
            ['c128', ':op/*toz', 'permanently-reference-only', 'no-operator-host'],
            ['c128', ':op/*toz', 'permanently-reference-only', 'no-operator-host'],
            ['c128', ':op/*toz', 'permanently-reference-only', 'no-operator-host'],
            ['string', ':op/*tos', 'permanently-reference-only', 'no-operator-host'],
            ['parse_int', ':op/parse_int', 'permanently-reference-only', 'no-operator-host'],
            ['parse_float', ':op/parse_float', 'permanently-reference-only', 'no-operator-host'],
            ['error', ':op/error_new', 'permanently-reference-only', 'no-operator-host'],
            ['print', ':io/print', 'permanently-reference-only', 'no-operator-host'],
            ['println', ':io/println', 'permanently-reference-only', 'no-operator-host'],
            ['input', ':io/input', 'permanently-reference-only', 'no-operator-host'],
            ['sleep', 'process:sleep', 'permanently-reference-only', 'no-operator-host'],
            ['exit', 'process:exit', 'permanently-reference-only', 'no-operator-host'],
            ['format', ':str/format', 'permanently-reference-only', 'no-operator-host'],
            ['join', ':str/join', 'permanently-reference-only', 'no-operator-host'],
            ['len', ':struct/len_*', 'permanently-reference-only', 'no-operator-host'],
            ['len', ':struct/len_*', 'permanently-reference-only', 'no-operator-host'],
            ['range', ':struct/range', 'permanently-reference-only', 'no-operator-host'],
            ['head', ':struct/head_arr', 'permanently-reference-only', 'no-operator-host'],
            ['last', ':struct/last_arr', 'permanently-reference-only', 'no-operator-host'],
            ['tail', ':struct/tail_arr', 'permanently-reference-only', 'no-operator-host'],
            ['slice', ':struct/slice_arr', 'permanently-reference-only', 'no-operator-host'],
            ['concat', ':struct/concat_arr', 'permanently-reference-only', 'no-operator-host'],
            ['contains', ':struct/contains_arr', 'permanently-reference-only', 'no-operator-host'],
            ['contains', ':struct/contains_str', 'permanently-reference-only', 'no-operator-host'],
            ['zip', ':struct/zip', 'permanently-reference-only', 'no-operator-host'],
            ['append', ':struct/append_arr', 'permanently-reference-only', 'no-operator-host'],
            ['extend', ':struct/extend_arr', 'permanently-reference-only', 'no-operator-host'],
            ['map', ':mark/map_arr', 'permanently-reference-only', 'no-operator-host'],
            ['filter', ':mark/filter_arr', 'permanently-reference-only', 'no-operator-host'],
            ['reduce', ':mark/reduce_arr', 'permanently-reference-only', 'no-operator-host'],
            ['foreach', ':mark/foreach_arr', 'permanently-reference-only', 'no-operator-host'],
            ['apply', ':mark/apply_arr', 'permanently-reference-only', 'no-operator-host'],
            ['attempt', ':mark/isolate', 'permanently-reference-only', 'no-operator-host'],
            ['len', ':coll/len', 'permanently-reference-only', 'no-operator-host'],
            ['len', ':coll/len', 'permanently-reference-only', 'no-operator-host'],
            ['get', ':coll/map_get', 'permanently-reference-only', 'no-operator-host'],
            ['get_or', ':coll/map_get_or', 'permanently-reference-only', 'no-operator-host'],
            ['put', ':coll/map_put', 'permanently-reference-only', 'no-operator-host'],
            ['remove', ':coll/remove', 'permanently-reference-only', 'no-operator-host'],
            ['remove', ':coll/remove', 'permanently-reference-only', 'no-operator-host'],
            ['has', ':coll/has', 'permanently-reference-only', 'no-operator-host'],
            ['has', ':coll/has', 'permanently-reference-only', 'no-operator-host'],
            ['keys', ':coll/keys', 'permanently-reference-only', 'no-operator-host'],
            ['values', ':coll/map_values', 'permanently-reference-only', 'no-operator-host'],
            ['items', ':coll/map_items', 'permanently-reference-only', 'no-operator-host'],
            ['items', ':coll/set_items', 'permanently-reference-only', 'no-operator-host'],
            ['merge', ':coll/merge', 'permanently-reference-only', 'no-operator-host'],
            ['contains', ':coll/set_contains', 'permanently-reference-only', 'no-operator-host'],
            ['add', ':coll/set_add', 'permanently-reference-only', 'no-operator-host'],
            ['union', ':coll/set_union', 'permanently-reference-only', 'no-operator-host'],
            ['intersect', ':coll/set_intersect', 'permanently-reference-only', 'no-operator-host'],
            ['difference', ':coll/set_difference', 'permanently-reference-only', 'no-operator-host'],
            ['is_subset', ':coll/set_is_subset', 'permanently-reference-only', 'no-operator-host'],
        ],
    },
    {
        module: 'graph',
        file: 'stdlib/signatures/graph.cml',
        evidence: 'modules/graph/txn.cpp:370-386,401-447',
        entries: [
            ['program_of', 'graph:program_of', 'needs-one-feature', 'arbitrary-function-type'],
            ['graph_of', 'graph:graph_of', 'needs-one-feature', 'arbitrary-function-type'],
            ['open_function', 'graph:open_function', 'needs-one-feature', 'arbitrary-function-type'],
            ['to_function', 'graph:to_function', 'needs-one-feature', 'arbitrary-function-type'],
        ],
    },
    {
        module: 'json',
        file: 'stdlib/signatures/json.cml',
        evidence: 'modules/json/module.cpp:71-90',
        entries: [
            ['encode', 'json:encode', 'needs-one-feature', 'type-predicate'],
            ['encode', 'json:encode', 'needs-one-feature', 'type-predicate'],
            ['decode_as', 'json:decode_as', 'needs-one-feature', 'type-argument-return', 'modules/json/module.cpp:83-90'],
            ['decode_as', 'json:decode_as', 'needs-one-feature', 'type-argument-return', 'modules/json/module.cpp:83-90'],
        ],
    },
    {
        module: 'nn',
        file: 'stdlib/signatures/nn.cml',
        evidence: 'modules/nn/tensor_ops.cpp:653-853; modules/nn/optim.cpp:390-463',
        entries: [
            ['conv2d', 'nn:conv2d', 'needs-one-feature', 'result-from-argument'],
            ['conv2d_input_grad', 'nn:conv2d_input_grad', 'needs-one-feature', 'result-from-argument'],
            ['conv2d_kernel_grad', 'nn:conv2d_kernel_grad', 'needs-one-feature', 'result-from-argument'],
            ['conv2d_bias_grad', 'nn:conv2d_bias_grad', 'needs-one-feature', 'result-from-argument'],
            ['max_pool2d', 'nn:max_pool2d', 'needs-one-feature', 'result-from-argument'],
            ['avg_pool2d', 'nn:avg_pool2d', 'needs-one-feature', 'result-from-argument'],
            ['max_pool2d_grad', 'nn:max_pool2d_grad', 'needs-one-feature', 'result-from-argument'],
            ['avg_pool2d_grad', 'nn:avg_pool2d_grad', 'needs-one-feature', 'result-from-argument'],
            ['batch_norm', 'nn:batch_norm', 'needs-one-feature', 'result-from-argument'],
            ['batch_norm_train', 'nn:batch_norm_train', 'needs-one-feature', 'result-from-argument'],
            ['batch_norm_train_grad', 'nn:batch_norm_train_grad', 'needs-one-feature', 'result-from-argument'],
            ['embedding', 'nn:embedding', 'needs-one-feature', 'result-from-argument'],
            ['embedding_table_grad', 'nn:embedding_table_grad', 'needs-one-feature', 'result-from-argument'],
            ['softmax_cross_entropy', 'nn:softmax_cross_entropy', 'needs-one-feature', 'type-predicate'],
            ['softmax_cross_entropy', 'nn:softmax_cross_entropy_labels', 'needs-one-feature', 'type-predicate'],
            ['softmax_cross_entropy_grad', 'nn:softmax_cross_entropy_grad', 'needs-one-feature', 'type-predicate'],
            ['softmax_cross_entropy_grad', 'nn:softmax_cross_entropy_labels_grad', 'needs-one-feature', 'type-predicate'],
            ['sgd', 'nn:sgd', 'needs-one-feature', 'type-predicate'],
            ['adam_state', 'nn:adam_state', 'needs-one-feature', 'type-predicate'],
            ['adam', 'nn:adam', 'needs-one-feature', 'type-predicate'],
        ],
    },
    {
        module: 'onnx',
        file: 'stdlib/signatures/onnx.cml',
        evidence: 'modules/onnx/module.cpp:138-145',
        entries: [
            ['export_model', 'onnx:export_model', 'needs-one-feature', 'arbitrary-function-type'],
        ],
    },
        {
        module: 'python',
        file: 'stdlib/signatures/python.cml',
        evidence: 'modules/python/module.cpp:374-467',
        entries: [
            ['py_call', 'python:py_call', 'needs-one-feature', 'variadic'],
            ['py_call', 'python:py_call_kw', 'needs-one-feature', 'variadic'],
            ['py_print', 'python:py_print', 'needs-one-feature', 'variadic'],
            ['py_println', 'python:py_println', 'needs-one-feature', 'variadic'],
            ['py_wrap', 'python:py_wrap', 'needs-one-feature', 'module-type-argument', 'modules/python/module.cpp:476-503; modules/python/type.h'],
            ['py_unwrap', 'python:py_unwrap', 'needs-one-feature', 'module-type-argument', 'modules/python/module.cpp:491-503; src/compile/gct/builder.cpp:4871-4884'],
        ],
    },
    {
        module: 'tensor',
        file: 'stdlib/signatures/tensor.cml',
        evidence: 'modules/tensor/ops/op_def.h:55-72,139-161; modules/tensor/ops/registry.cpp:61-143',
        entries: [
            ['new', 'tensor:new', 'needs-one-feature', 'result-from-argument'],
            ['empty', 'tensor:empty', 'needs-one-feature', 'result-from-argument'],
            ['zeros', 'tensor:zeros', 'needs-one-feature', 'result-from-argument'],
            ['ones', 'tensor:ones', 'needs-one-feature', 'result-from-argument'],
            ['full', 'tensor:full', 'needs-one-feature', 'result-from-argument'],
            ['eye', 'tensor:eye', 'needs-one-feature', 'result-from-argument'],
            ['range', 'tensor:range', 'permanently-reference-only', 'one-name-one-source', 'modules/tensor/ops/creation_ops.cpp:219-228; docs/technical/47_declaration_syntax.md:§4.2'],
            ['random', 'tensor:random', 'needs-one-feature', 'result-from-argument'],
            ['randn', 'tensor:randn', 'needs-one-feature', 'result-from-argument'],
            ['add', 'tensor:add', 'needs-one-feature', 'result-from-argument'],
            ['subtract', 'tensor:subtract', 'needs-one-feature', 'result-from-argument'],
            ['multiply', 'tensor:multiply', 'needs-one-feature', 'result-from-argument'],
            ['divide', 'tensor:divide', 'needs-one-feature', 'result-from-argument'],
            ['pow', 'tensor:pow', 'needs-one-feature', 'result-from-argument'],
            ['maximum', 'tensor:maximum', 'needs-one-feature', 'result-from-argument'],
            ['minimum', 'tensor:minimum', 'needs-one-feature', 'result-from-argument'],
            ['where', 'tensor:where', 'needs-one-feature', 'result-from-argument'],
            ['neg', 'tensor:neg', 'needs-one-feature', 'result-from-argument'],
            ['abs', 'tensor:abs', 'needs-one-feature', 'result-from-argument'],
            ['exp', 'tensor:exp', 'needs-one-feature', 'result-from-argument'],
            ['log', 'tensor:log', 'needs-one-feature', 'result-from-argument'],
            ['sqrt', 'tensor:sqrt', 'needs-one-feature', 'result-from-argument'],
            ['rsqrt', 'tensor:rsqrt', 'needs-one-feature', 'result-from-argument'],
            ['sigmoid', 'tensor:sigmoid', 'needs-one-feature', 'result-from-argument'],
            ['tanh', 'tensor:tanh', 'needs-one-feature', 'result-from-argument'],
            ['relu', 'tensor:relu', 'needs-one-feature', 'result-from-argument'],
            ['gelu', 'tensor:gelu', 'needs-one-feature', 'result-from-argument'],
            ['erf', 'tensor:erf', 'needs-one-feature', 'result-from-argument'],
            ['reshape', 'tensor:reshape', 'needs-one-feature', 'result-from-argument'],
            ['flatten', 'tensor:flatten', 'needs-one-feature', 'result-from-argument'],
            ['unsqueeze', 'tensor:unsqueeze', 'needs-one-feature', 'result-from-argument'],
            ['transpose', 'tensor:transpose', 'needs-one-feature', 'result-from-argument'],
            ['permute', 'tensor:permute', 'needs-one-feature', 'result-from-argument'],
            ['concat', 'tensor:concat', 'needs-one-feature', 'result-from-argument'],
            ['slice', 'tensor:slice', 'needs-one-feature', 'result-from-argument'],
            ['take', 'tensor:take', 'needs-one-feature', 'result-from-argument'],
            ['one_hot', 'tensor:one_hot', 'needs-one-feature', 'result-from-argument'],
            ['sum', 'tensor:sum', 'permanently-reference-only', 'one-name-one-source', 'modules/tensor/ops/reduction_ops.cpp:313-330; docs/technical/47_declaration_syntax.md:§4.2'],
            ['sum', 'tensor:sum_axis', 'permanently-reference-only', 'one-name-one-source', 'modules/tensor/ops/reduction_ops.cpp:313-330; docs/technical/47_declaration_syntax.md:§4.2'],
            ['mean', 'tensor:mean', 'permanently-reference-only', 'one-name-one-source', 'modules/tensor/ops/reduction_ops.cpp:322-330; docs/technical/47_declaration_syntax.md:§4.2'],
            ['mean', 'tensor:mean_axis', 'permanently-reference-only', 'one-name-one-source', 'modules/tensor/ops/reduction_ops.cpp:322-330; docs/technical/47_declaration_syntax.md:§4.2'],
            ['max', 'tensor:max_axis', 'needs-one-feature', 'result-from-argument'],
            ['min', 'tensor:min_axis', 'needs-one-feature', 'result-from-argument'],
            ['argmax', 'tensor:argmax_axis', 'needs-one-feature', 'result-from-argument'],
            ['argmin', 'tensor:argmin_axis', 'needs-one-feature', 'result-from-argument'],
            ['softmax', 'tensor:softmax', 'needs-one-feature', 'result-from-argument'],
            ['log_softmax', 'tensor:log_softmax', 'needs-one-feature', 'result-from-argument'],
            ['softmax_grad', 'tensor:softmax_grad', 'needs-one-feature', 'result-from-argument'],
            ['layer_norm', 'tensor:layer_norm', 'needs-one-feature', 'result-from-argument'],
            ['matmul', 'tensor:matmul', 'needs-one-feature', 'result-from-argument'],
            ['linear', 'tensor:linear', 'needs-one-feature', 'result-from-argument'],
            ['zeros_like', 'tensor:zeros_like', 'needs-one-feature', 'result-from-argument'],
            ['ones_like', 'tensor:ones_like', 'needs-one-feature', 'result-from-argument'],
            ['cast', 'tensor:cast', 'needs-one-feature', 'result-from-argument'],
            ['from_bytes', 'tensor:from_bytes_*', 'needs-one-feature', 'type-argument-return', 'modules/tensor/ops/binary_ops.cpp:127-143'],
        ],
    },
    {
        module: 'tiles',
        file: 'stdlib/signatures/tiles.cml',
        evidence: 'modules/tiles/src/module.cpp:486-592',
        entries: [
            ['gpu', 'tiles:gpu', 'needs-one-feature', 'arbitrary-function-type', 'modules/tiles/src/module.cpp:486-500; modules/tiles/include/tiles/device.h:78'],
            ['upload', 'tiles:upload', 'needs-one-feature', 'structural-type-function'],
            ['download', 'tiles:download', 'needs-one-feature', 'structural-type-function'],
            ['run', 'tiles:run', 'needs-one-feature', 'result-from-argument'],
            ['run_device', 'tiles:run_device', 'needs-one-feature', 'result-from-argument'],
            ['run_preferred_resident', 'tiles:run_preferred_resident', 'needs-one-feature', 'result-from-argument'],
            ['source', 'tiles:source', 'needs-one-feature', 'arbitrary-function-type'],
            ['inspect', 'tiles:inspect', 'needs-one-feature', 'arbitrary-function-type'],
            ['benchmark', 'tiles:benchmark', 'needs-one-feature', 'arbitrary-function-type'],
            ['regions', 'tiles:regions', 'needs-one-feature', 'arbitrary-function-type'],
        ],
    },
]

/// Every classified entry, flattened, with the position of its module row.
export function entryRecords() {
    const records = []
    for (const module of LEDGER) {
        module.entries.forEach(([name, uri, bucket, mechanism, evidence], index) => {
            records.push({
                module: module.module,
                file: module.file,
                name,
                uri,
                bucket,
                mechanism,
                evidence: evidence || module.evidence,
                index,
            })
        })
    }
    return records
}

/// The entries of the signature files, in file order, read the way the reference generator reads
/// them: a `#op<'uri'>` binds the next declaration, and a declaration without one names
/// `<module>:<name>`.
export function signatureEntries(root) {
    const dir = `${root}/stdlib/signatures`
    const entries = []
    for (const file of fs.readdirSync(dir).filter((each) => each.endsWith('.cml')).sort()) {
        const module = file.slice(0, -'.cml'.length)
        const lines = fs.readFileSync(`${dir}/${file}`, 'utf8').replace(/\r/g, '').split('\n')
        let pendingUri = null
        for (let i = 0; i < lines.length; i++) {
            const trimmed = lines[i].trim()
            const tag = trimmed.match(/^#\[?op<['"]([^'"]+)['"]>/)
            if (tag) {
                pendingUri = tag[1]
                continue
            }
            const declaration = trimmed.match(/^export\s+func\s+([A-Za-z_][\w:]*)\s*[<(]/)
            if (declaration) {
                entries.push({
                    module,
                    file: `stdlib/signatures/${file}`,
                    line: i + 1,
                    name: declaration[1],
                    uri: pendingUri || `${module}:${declaration[1]}`,
                })
                pendingUri = null
            }
        }
    }
    return entries
}

/// Every problem that makes the ledger not a classification of the files: a bucket tag that does
/// not exist, a mechanism tag that does not exist or belongs to another bucket, an entry with no
/// record, and a record with no entry.
export function classificationProblems(root) {
    const problems = []
    for (const [tag, bucket] of Object.entries(BUCKETS)) {
        if (!Number.isInteger(bucket.id) || bucket.id < 1 || bucket.id > 4) {
            problems.push(`scripts/computed-gaps.mjs: the bucket '${tag}' has no id in 1..4`)
        }
    }
    const ids = new Set(Object.values(BUCKETS).map((bucket) => bucket.id))
    if (ids.size !== Object.keys(BUCKETS).length) {
        problems.push('scripts/computed-gaps.mjs: two buckets share an id')
    }
    for (const [tag, mechanism] of Object.entries(MECHANISMS)) {
        if (!BUCKETS[mechanism.bucket]) {
            problems.push(`scripts/computed-gaps.mjs: the mechanism '${tag}' names the bucket '${mechanism.bucket}', which has no tag`)
        }
    }

    const records = entryRecords()
    for (const record of records) {
        if (!BUCKETS[record.bucket]) {
            problems.push(
                `scripts/computed-gaps.mjs: '${record.name}' (${record.module}) names the bucket '${record.bucket}', ` +
                    `which has no tag; use one of ${Object.keys(BUCKETS).join(', ')}`,
            )
        }
        if (!MECHANISMS[record.mechanism]) {
            problems.push(
                `scripts/computed-gaps.mjs: '${record.name}' (${record.module}) names the mechanism '${record.mechanism}', ` +
                    `which has no tag; use one of ${Object.keys(MECHANISMS).join(', ')}`,
            )
        } else if (MECHANISMS[record.mechanism].bucket !== record.bucket) {
            problems.push(
                `scripts/computed-gaps.mjs: '${record.name}' (${record.module}) is in '${record.bucket}' but its mechanism ` +
                    `'${record.mechanism}' belongs to '${MECHANISMS[record.mechanism].bucket}'`,
            )
        }
    }

    const entries = signatureEntries(root)
    const unused = records.slice()
    for (const entry of entries) {
        const at = unused.findIndex(
            (record) => record.module === entry.module && record.name === entry.name && record.uri === entry.uri,
        )
        if (at < 0) {
            problems.push(
                `${entry.file}:${entry.line}: '${entry.name}' (${entry.uri}) is not classified in ` +
                    `scripts/computed-gaps.mjs; add it to the '${entry.module}' row with a bucket tag`,
            )
            continue
        }
        unused.splice(at, 1)
    }
    for (const record of unused) {
        problems.push(
            `scripts/computed-gaps.mjs: the record '${record.name}' (${record.uri}) of '${record.module}' has no ` +
                `entry in ${record.file}`,
        )
    }
    return problems
}

function main() {
    const problems = classificationProblems(path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..'))
    // An unknown tag has no column to count: report the problems before reading any tag.
    if (problems.length > 0) {
        for (const problem of problems) console.log(problem)
        console.log(`computed-gaps: ${problems.length} problem(s)`)
        process.exit(1)
    }
    const rows = entryRecords()
    const perModule = new Map()
    for (const row of rows) {
        if (!perModule.has(row.module)) perModule.set(row.module, [0, 0, 0, 0])
        const counts = perModule.get(row.module)
        counts[BUCKETS[row.bucket].id - 1] += 1
    }
    console.log('module      entries  declarable  needs-one  permanent  not-a-signature')
    for (const [module, counts] of [...perModule].sort((a, b) => a[0].localeCompare(b[0]))) {
        console.log(
            `${module.padEnd(12)}${String(counts.reduce((a, b) => a + b, 0)).padStart(6)}` +
                `${String(counts[0]).padStart(12)}${String(counts[1]).padStart(11)}${String(counts[2]).padStart(11)}${String(counts[3]).padStart(16)}`,
        )
    }
    const totals = [0, 0, 0, 0]
    for (const row of rows) totals[BUCKETS[row.bucket].id - 1] += 1
    console.log(
        `${'total'.padEnd(12)}${String(rows.length).padStart(6)}` +
            `${String(totals[0]).padStart(12)}${String(totals[1]).padStart(11)}${String(totals[2]).padStart(11)}${String(totals[3]).padStart(16)}`,
    )
    const perMechanism = new Map()
    for (const row of rows) {
        if (!perMechanism.has(row.mechanism)) perMechanism.set(row.mechanism, [])
        perMechanism.get(row.mechanism).push(row.name)
    }
    console.log('\nmechanism                 bucket                       entries')
    for (const [mechanism, names] of [...perMechanism].sort((a, b) => b[1].length - a[1].length || a[0].localeCompare(b[0]))) {
        console.log(
            `${mechanism.padEnd(26)}${MECHANISMS[mechanism].bucket.padEnd(29)}${String(names.length).padStart(4)}  ${[...new Set(names)].join(', ')}`,
        )
    }
    console.log('computed-gaps: every entry is classified')
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    main()
}
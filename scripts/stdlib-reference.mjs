// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The standard library reference, read from the signature files and the library sources.
//
//   node scripts/stdlib-reference.mjs --write       regenerate docs/stdlib-reference.md
//   node scripts/stdlib-reference.mjs --verify-doc  docs/stdlib-reference.md is what --write would produce
//   node scripts/stdlib-reference.mjs --verify-ops  the signature files list exactly the operators the
//                                                   modules register, with the result type each
//                                                   operator gives back (needs a built compiler)
//   node scripts/stdlib-reference.mjs --verify-docs every name has a description and an @example, and
//                                                   every example compiles (needs a built compiler)
//   node scripts/stdlib-reference.mjs --audit [--compile]
//                                                   the gap of each module as a table; --compile also
//                                                   compiles (needs a built compiler)
//   [--camel <path>]                                the install a mode that needs a compiler measures;
//                                                   this tree's out/latest/bin/camel without it
//
// A run that renders no entry or compares no registered operator is refused (exit 1): agreement
// over nothing is not a pass. A mode that needs a compiler is refused earlier (exit 2) when the
// install it would measure is not this tree's build: the message names the install and its stamp,
// and the run reads no operator from it (`scripts/install-stamp.mjs` holds the rule that
// `scripts/release-gate.mjs` applies too). `--camel <path>` names the compiler of a run.
//
// stdlib/signatures/<module>.cml declares the operators a C++ module exports (globals.cml: the
// built-in ones), one body-less `export func` each, with a doc comment:
//
//   /// What it does, in one or more lines.
//   /// @pure                      or  @effect <what the effect is>
//   /// @fails <fault or error and when>          (required when the result is `T!`)
//   /// @example <code>    // <result>
//   /// @example-fragment <why it is not a program of its own>   (optional; the example is then
//   ///                                                          shown but not compiled)
//   #op('protocol:operator')                       (only when the URI is not `<module>:<name>`;
//   with <a: A>                                     `*` stands for any text, for a group of
//   export func name(x: X): R                       overloads that differ by type)
//
// A declaration never carries a body. The old form wrote the URI in one — `func name(...): R {
// return '<uri>' }` — and it is gone: a `func` with a body is an ordinary function,
// and --verify-ops refuses a signature file whose entry still has one.
//
// A value a module exports (`math::pi`; design principles 10.1) needs neither @pure nor a URI
// (it is not an operator). A declaration module writes it with no right side, and the C++ module
// is the one source of the value itself:
//
//   /// What it is.
//   /// @example <code>    // <result>
//   export let name: T
//
// A library module written in Camel (stdlib/<module>.cml) is its own signature file: its
// `export func` declarations and their `///` comments are read the same way.
//
// When a declaration cannot express an operator — its result type comes from a type rule, or
// from a type argument — that operator stays in the C++ resolver and keeps its entry in
// stdlib/signatures/<module>.cml, for reference only (json: `encode`, `decode_as`). The module's `computed` field names
// that file; the reference reads the declaration
// file and the computed entries together, and --verify-ops checks both.
//
// --verify-ops runs `camel inspect --ops` on a program that imports every module and compares,
// per module: every registered operator (name, parameter counts) has a declaration whose URI
// pattern matches it, every declaration matches a registered operator, and @pure / @effect is
// what the module declared in the operator traits registry. So a signature file cannot list an
// operator that does not exist, miss one that does, or disagree on the number of parameters
// or on purity.

import fs from 'fs'
import os from 'os'
import path from 'path'
import { spawn, spawnSync } from 'child_process'
import { fileURLToPath } from 'url'
import { refuseInstallNotOfThisTree } from './install-stamp.mjs'
// The one default for the install a tool measures with no `--camel`: this tree's `out/latest`.
import { defaultCamel } from '../test/tools/camel-arg.mjs'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const SIGNATURE_DIR = path.join(REPO_ROOT, 'stdlib', 'signatures')

// The install a run measures: `--camel <path>` (parsed in the main section below), else this tree's
// `out/latest`. Every install a run measures passes the build stamp of `scripts/install-stamp.mjs`,
// named there or not.
let camelPath = null
const STDLIB_DIR = path.join(REPO_ROOT, 'stdlib')
const DOC_PATH = path.join(REPO_ROOT, 'docs', 'stdlib-reference.md')
const PROBE = path.join(REPO_ROOT, 'test', 'cases', 'modules', 'stdlib', 'registry_all.cml')

// The modules of the reference, in the order of the document. `source` is the file read:
// a signature file for a C++ module, the library itself for a module written in Camel.
const MODULES = [
    { name: 'globals', title: 'Built-in functions', registered: '<builtin>', kind: 'cpp', source: 'prelude.cml', declaration: true },
    {
        name: 'str',
        title: '`str`: strings',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles.
        source: 'str.cml',
        declaration: true,
    },
    {
        name: 'bytes',
        title: '`bytes`: immutable binary values and UTF-8 conversion',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles.
        source: 'bytes.cml',
        declaration: true,
    },
    { name: 'ckpt', title: '`ckpt`: parameter trees and checkpoints', kind: 'cpp', source: 'ckpt.cml', declaration: true, computed: 'signatures/ckpt.cml' },
    {
        name: 'tree',
        title: '`tree`: parameter trees written in Camel',
        kind: 'camel',
        // The walks are written with `match type`, `static for` and rest parameters, so a
        // function whose result shape follows its argument has no result type a declaration can
        // spell yet: it is rendered as `?` (an inferred result), not as `void`.
    },
    {
        name: 'transforms',
        title: '`transforms`: program transforms',
        kind: 'cpp',
        // The pass `reinterp` is declared in this .cml and registered in modules/transforms, so
        // `--verify-ops` passes over it. The operator `jvp` answers a function built from its
        // argument's type, so its entry is in the signature file named by `computed:`.
        source: 'transforms.cml',
        declaration: true,
        computed: 'signatures/transforms.cml',
    },
    {
        name: 'vjp',
        title: '`vjp`: the derivative, as a library',
        kind: 'camel',
        // `vjp` is the composition of three transforms, and all three are written: `jvp` lifts,
        // `split` answers the two halves, and `transpose` walks the backward one. The module
        // compiles and imports the two rulesets it composes, so a program that imports `vjp` gets a
        // derivative and a pullback never raises. It was listed as `pending` until the walk took a
        // callee of several parameters; that reason retired with the step.
    },
    {
        name: 'tensor',
        title: '`tensor`: multi-dimensional numeric arrays',
        kind: 'cpp',
        // The fixed signatures are declared; the ones whose element type or shape follows an
        // argument stay in C++ and are recorded as computed.
        source: 'tensor.cml',
        declaration: true,
        computed: 'signatures/tensor.cml',
    },
    {
        name: 'types',
        title: '`types`: the properties of types',
        kind: 'camel',
        // The predicates are head-only generic functions whose `match type` cases name the
        // types the language spells, so a `when` condition can ask a property of a type.
    },
    {
        name: 'array',
        title: '`array`: arrays, sorting and higher-order functions',
        kind: 'camel',
        // Most of the module is Camel; a body-less declaration is one of its native operators
        // (argsort and the kernels of sort, sum, min and max), and the page says so. Those declarations are
        // checked against the registered operators like a C++ module's.
        native: true,
    },
    {
        name: 'stream',
        title: '`stream`: producing and consuming streams',
        kind: 'camel',
        // A stream is an ordinary function value (its step function together with its state), so
        // its producers here are ordinary Camel functions that return one, and the consumer is
        // `for x in s`: no part of the module is a native operator of its own.
    },
    {
        name: 'math',
        title: '`math`: numbers and bits',
        kind: 'cpp',
        // The declarations only: the three names whose result type C++ computed from their
        // argument (sqrt; the mixed-integer shifts) are written out per spelling.
        source: 'math.cml',
        declaration: true,
    },
    {
        name: 'statistics',
        title: '`statistics`: variance, standard deviation, median and quantile',
        kind: 'camel',
    },
    {
        name: 'random',
        title: '`random`',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles. The
        // three operators whose result type comes from their array argument are generic
        // declarations too.
        source: 'random.cml',
        declaration: true,
    },
    {
        name: 'time',
        title: '`time`',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles.
        source: 'time.cml',
        declaration: true,
    },
    {
        name: 'fs',
        title: '`fs`: files',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles.
        source: 'fs.cml',
        declaration: true,
    },
    {
        name: 'io',
        title: '`io`: terminal input',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles.
        source: 'io.cml',
        declaration: true,
    },
    {
        name: 'process',
        title: '`process`: the process and the machine',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (
        // `exit` is not a group of this module and is not declared here.
        source: 'process.cml',
        declaration: true,
    },
    {
        name: 'env',
        title: '`env`: environment',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (
        // 
        source: 'env.cml',
        declaration: true,
    },
    {
        name: 'json',
        title: '`json`',
        kind: 'cpp',
        // The declarations, in stdlib/json.cml. `encode` and `decode_as` are computed
        // signatures — the value's type must have a JSON form, and the result type is the type
        // of the shape argument — so a declaration cannot express them; their entries, and the
        // check against the registered operators, come from the signature file that names them
        // 
        source: 'json.cml',
        declaration: true,
    },
    {
        name: 'hash',
        title: '`hash`',
        kind: 'cpp',
        // Written in Camel over one private native kernel: no exported declaration is an operator.
        source: 'hash.cml',
        declaration: true,
    },
    {
        name: 're',
        title: '`re`: regular expressions',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles.
        source: 're.cml',
        declaration: true,
    },
    {
        name: 'para',
        title: '`para`: unordered iteration',
        kind: 'cpp',
        // The declarations of `rendezvous` and of the two names that take the element type of
        // their array argument.
        source: 'para.cml',
        declaration: true,
    },
    {
        name: 'test_support',
        title: '`test_support`: the facilities of a test',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles.
        source: 'test_support.cml',
        declaration: true,
    },
    {
        name: 'this',
        title: '`this`',
        kind: 'cpp',
        // The declaration, not a signature file: it is what the module compiles.
        source: 'this.cml',
        declaration: true,
    },
    {
        name: 'graph',
        title: '`graph`: the graph of a function as a value',
        kind: 'cpp',
        // The declarations only: every signature is stated, a function-valued argument is `F`
        // with `when is_function::<F>()`.
        source: 'graph.cml',
        declaration: true,
    },
    {
        name: 'rewrite',
        title: '`rewrite`: rules applied to a program',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles.
        // The three reserved groups the `rule`/`analysis` declarations compile to have no
        // callable Camel name, so they are neither declared nor listed.
        source: 'rewrite.cml',
        declaration: true,
    },
    {
        name: 'tiles',
        title: '`tiles`: programs on a CUDA device',
        kind: 'cpp',
        // `available` and `device` are declared in stdlib/tiles.cml; the ten operators whose
        // signature C++ computes from an argument (the function value's parameter list and result,
        // or the leaves a transfer wraps in `Device<...>`) keep their reference entries in the
        // signature file named by `computed:`.
        source: 'tiles.cml',
        declaration: true,
        computed: 'signatures/tiles.cml',
    },
    {
        name: 'pyplot',
        title: '`pyplot`: draw an array',
        kind: 'cpp',
        // The one operator is declared in stdlib/pyplot.cml; the four element types and the two
        // arities are written out because a declaration cannot say "an array of one of four
        // numeric types" and cannot write an optional parameter.
        source: 'pyplot.cml',
        declaration: true,
    },
    {
        name: 'python',
        title: '`python`: the embedded interpreter',
        kind: 'cpp',
        // Seven fixed signatures are declared in stdlib/python.cml; `py_wrap`/`py_unwrap` move a
        // type through the module type `PyObject<T>`, so their reference entries live in the
        // signature file named by `computed:`.
        source: 'python.cml',
        declaration: true,
    },
    {
        name: 'nn',
        title: '`nn`: neural-network primitives',
        kind: 'cpp',
        // `save_params` is declared in stdlib/nn.cml; the eighteen operators whose signature C++
        // computes from the tensors they receive (the convolution, pooling, batch-normalization,
        // embedding and loss operators, and the three optimizers) keep their reference entries in
        // the signature file named by `computed:`.
        source: 'nn.cml',
        declaration: true,
        computed: 'signatures/nn.cml',
    },
    {
        name: 'optim',
        title: '`optim`: optimizers written in Camel over `tree`',
        kind: 'camel',
        // The schedules, the optimizations and `init`/`update` are Camel; the state a step gives
        // back is built from the parameter tree, so `init`'s result type is a shape a declaration
        // cannot spell yet and is rendered as `?` (an inferred result).
    },
    {
        name: 'autodiff',
        title: '`autodiff`: gradients of functions',
        kind: 'cpp',
        // `stop_gradient` is declared in stdlib/autodiff.cml; `grad` and `with_grad` answer a
        // function built from their argument's type, so their entries are in the signature file
        // named by `computed:`.
        source: 'autodiff.cml',
        declaration: true,
        computed: 'signatures/autodiff.cml',
    },
    {
        name: 'rulesets.jvp',
        title: '`rulesets.jvp`: how a forward transform lifts a value',
        kind: 'camel',
        // A ruleset is a module of the library, found under its dotted name: a dotted module name
        // is a subdirectory of the library, so `rulesets.jvp` is `stdlib/rulesets/jvp.cml`. Its
        // declarations are Camel, and `--verify-docs` compiles their examples like any other
        // module's.
        source: 'rulesets/jvp.cml',
    },
    {
        name: 'rulesets.vmap',
        title: '`rulesets.vmap`: how a batched transform lifts a value',
        kind: 'camel',
        // `Lift<T> = RowsOf<T>`: a batched value is the value with one dimension in front, so the
        // elementwise tier's rule for a primitive is the primitive itself (`stdlib/rulesets/vmap.cml`).
        source: 'rulesets/vmap.cml',
    },
    {
        name: 'sim',
        title: '`sim`: Monte Carlo simulation',
        kind: 'cpp',
        // Both operators have fixed signatures and are declared in stdlib/sim.cml; nothing is
        // computed, so the module has no signature file.
        source: 'sim.cml',
        declaration: true,
    },
    {
        name: 'onnx',
        title: '`onnx`: export a program as an ONNX model',
        kind: 'cpp',
        // Both operators are declared in stdlib/onnx.cml: `supported_operators`, and
        // `export_model` with the `with` parameters and the defaults a call reads its arguments
        // by (the module keeps the kernel, the executor and the purity).
        source: 'onnx.cml',
        declaration: true,
    },
]

// A module the reference knows about and cannot document yet, and the one sentence that says why.
//
// The case is real and it is not a file-name question: `vjp` is `transpose(split(reinterp<jvp>(f)))`
// written in Camel, and it compiles only when all three steps meet. Until then its declarations are
// still worth listing here -- the module exists, the design names it, and a reader who asks where
// the derivative lives should find the answer -- but an `@example` of a module that does not compile
// is code no reader can run, and a page is only worth trusting if everything on it compiles.
//
// So the module is in MODULES and out of the page, and `pending` is where that is written down.
// Nothing is silent about it: every mode prints the sentence below, so the omission is a fact on
// the screen rather than an absence. And nothing has to be remembered either: `--verify-docs`
// compiles a pending module's examples anyway, and when they compile it reports that as a problem
// -- "drop the pending entry" -- so the day the module builds, the reference says so itself.
function pendingReason(module) {
    return module.pending ? String(module.pending).replace(/\s+/g, ' ') : null
}

function announcePending() {
    for (const module of MODULES) {
        const reason = pendingReason(module)
        if (reason !== null) {
            console.log(`not in the reference yet: the module '${module.name}' -- ${reason}`)
        }
    }
}

function sourceOf(module) {
    if (module.source) {
        return path.join(STDLIB_DIR, module.source)
    }
    return module.kind === 'camel'
        ? path.join(STDLIB_DIR, `${module.name}.cml`)
        : path.join(SIGNATURE_DIR, `${module.name}.cml`)
}

// ---- reading declarations ------------------------------------------------------------------

// The parameters of a list written between brackets, split at the commas that are not nested.
function splitParams(text) {
    const parts = []
    let depth = 0
    let current = ''
    for (let i = 0; i < text.length; i++) {
        const c = text[i]
        if (c === '=' && text[i + 1] === '>') {
            current += '=>'
            i++
            continue
        }
        if ('([{<'.includes(c)) depth++
        if (')]}>'.includes(c)) depth--
        if (c === ',' && depth === 0) {
            parts.push(current.trim())
            current = ''
        } else {
            current += c
        }
    }
    if (current.trim()) parts.push(current.trim())
    return parts
}

// -1 for a list with a variadic parameter, as `camel inspect --ops` reports it. A parameter can
// be variadic without being spelled `...name`: an optional parameter collected into a variadic
// tuple, `args?: (...T) | T[]`, accepts any number of arguments too, and the C++ resolver
// reports its count as -1.
function countParams(params) {
    return params.some((p) => p.startsWith('...') || /\(\s*\.\.\./.test(p)) ? -1 : params.length
}

// The text between the bracket at `open` and its partner.
function bracketed(text, open) {
    const pairs = { '(': ')', '<': '>' }
    const close = pairs[text[open]]
    let depth = 0
    for (let i = open; i < text.length; i++) {
        if (text[i] === '=' && text[i + 1] === '>') {
            i++
            continue
        }
        if (text[i] === text[open]) depth++
        if (text[i] === close) depth--
        if (depth === 0) return { inner: text.slice(open + 1, i), end: i }
    }
    throw new Error(`unbalanced brackets in: ${text}`)
}

// How many `{` a text leaves open. A `match type` body is followed to its close with this, in a
// declaration and in the definition of a type function alike.
function braceDepth(text) {
    let depth = 0
    for (let i = 0; i < text.length; i++) {
        if (text[i] === '{') depth++
        else if (text[i] === '}') depth--
    }
    return depth
}

// The result type a declaration states, read from the line the declaration is on. A `match type`
// result is written over several lines -- its case list is the part that looks like a body -- so
// the brace group is followed to its close and the whole text is the result. Any other result is
// the text after the colon, with a body opening (when the declaration has a body) cut off.
function readResultType(after, lines, lineIndex) {
    const text = after.replace(/^\s*:\s*/, '').trim()
    if (!/^match\s+type\b/.test(text)) return text.replace(/\s*(sync\s*)?\{\s*$/, '').trim()
    let joined = text
    let depth = braceDepth(joined)
    for (let j = lineIndex + 1; depth > 0 && j < lines.length; j++) {
        joined += ` ${lines[j].trim()}`
        depth = braceDepth(joined)
    }
    return joined.trim()
}

// The index of the bracket that closes the one at `open`, or -1 when the line ends first.
function closingBracket(text, open) {
    const pairs = { '(': ')', '[': ']', '{': '}', '<': '>' }
    const close = pairs[text[open]]
    let depth = 0
    for (let i = open; i < text.length; i++) {
        if (text[i] === '=' && text[i + 1] === '>') {
            i++
            continue
        }
        if (text[i] === text[open]) depth++
        else if (text[i] === close) depth--
        if (depth === 0) return i
    }
    return -1
}

// `{ a: int, b: string }` is a struct type; the brace group an old-form declaration wrote after
// its result type (`{ return '<uri>' }`) is not.
function isFieldList(text) {
    const trimmed = text.trim()
    if (trimmed === '') return true
    const parts = []
    let depth = 0
    let current = ''
    for (let i = 0; i < trimmed.length; i++) {
        const c = trimmed[i]
        if ('([{<'.includes(c)) depth++
        if (')]}>'.includes(c)) depth--
        if (c === ',' && depth === 0) {
            parts.push(current.trim())
            current = ''
        } else {
            current += c
        }
    }
    if (current.trim()) parts.push(current.trim())
    return parts.every((part) => /^[A-Za-z_]\w*\s*:\s*\S/.test(part))
}

// Whether the text after a declaration's `)` carries a body. The old form is
// `: R { return '<uri>' }`; a body may also open with `sync {` or on the following line.
function declarationBody(rest, lines, lineIndex) {
    // A `match type` result is a type, not a body: the brace group it opens holds one case per line
    // and closes. Read as a body, every declaration that states a type function became a function
    // with an implementation, was left out of the comparison, and the operator it declares went
    // unchecked -- which is how a library writing type functions turned this check red 15 times
    // without one of those reds saying "a signature I cannot read".
    if (/^\s*(?::\s*)?match\s+type\b/.test(rest)) return false
    // A type has no statement keyword; a `sync` body marker is unambiguous.
    if (/\b(sync|return)\b/.test(rest)) return true
    const colon = rest.indexOf(':')
    if (colon < 0) return rest.trim() !== ''
    for (let i = colon + 1; i < rest.length; ) {
        const c = rest[i]
        if (c === '=' && rest[i + 1] === '>') {
            i += 2
            continue
        }
        if ('([{<'.includes(c)) {
            const end = closingBracket(rest, i)
            // An unclosed group is a body opening, and a brace group that does not read as a
            // field list is a body.
            if (end < 0 || (c === '{' && !isFieldList(rest.slice(i + 1, end)))) return true
            i = end + 1
            continue
        }
        if (')]}>'.includes(c)) return true
        i++
    }
    for (let j = lineIndex + 1; j < lines.length; j++) {
        const next = lines[j].trim()
        if (next === '' || next.startsWith('//')) continue
        return /^(sync\s*)?\{/.test(next)
    }
    return false
}

// The guard: a signature file is reference material for declarations, so none of its entries may
// carry a body. Without this check the parser would silently ignore the body and the reference
// page would still agree with the registered operators.
function signatureBodyProblems() {
    const problems = []
    for (const name of fs.readdirSync(SIGNATURE_DIR).sort()) {
        if (!name.endsWith('.cml')) continue
        const file = path.join(SIGNATURE_DIR, name)
        const relative = path.relative(REPO_ROOT, file).replace(/\\/g, '/')
        const lines = fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n').split('\n')
        for (let i = 0; i < lines.length; i++) {
            const trimmed = lines[i].trim()
            const decl = trimmed.match(/^(?:export\s+)?func\s+([A-Za-z_][\w:]*)\s*(?:::<[^>]*>)?\s*\(/)
            if (!decl) continue
            const close = closingBracket(trimmed, decl[0].length - 1)
            if (close < 0) continue
            if (declarationBody(trimmed.slice(close + 1), lines, i)) {
                problems.push(
                    `${relative}:${i + 1}: the declaration '${decl[1]}' has a body; a signature ` +
                        `file declares operators without one (the old \`return '<uri>'\` form is gone)`
                )
            }
        }
    }
    return problems
}

function readDeclarations(file, module, options = {}) {
    const lines = fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n').split('\n')
    const relative = path.relative(REPO_ROOT, file).replace(/\\/g, '/')
    const keyword = 'export func'
    const entries = []
    // The private operators of the module, when the caller asks for them: a body-less `func` a
    // library module declares without exporting is the kernel an `#impl` function runs (design
    // principles 10.4). They are not part of the reference page, so they are collected
    // separately instead of joining `entries`.
    const kernels = []
    entries.kernels = kernels
    const sectionNotes = new Map()
    const sections = []
    entries.sectionNotes = sectionNotes
    entries.sections = sections
    let section = ''
    let doc = []
    let withText = ''
    let pendingUri = null
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i]
        const trimmed = line.trim()
        const sectionMatch = trimmed.match(/^\/\/ ---- (.+?) ----$/)
        if (sectionMatch) {
            section = sectionMatch[1]
            sections.push(section)
            // The plain comment lines right under a section line are its introduction.
            const notes = []
            while (i + 1 < lines.length && /^\/\/(?!\/)/.test(lines[i + 1].trim())) {
                notes.push(lines[++i].trim().replace(/^\/\/ ?/, ''))
            }
            while (notes.length && notes[0] === '') notes.shift()
            sectionNotes.set(section, notes)
            continue
        }
        if (trimmed.startsWith('///')) {
            doc.push(trimmed.slice(3).replace(/^ /, ''))
            continue
        }
        if (trimmed.startsWith('with <')) {
            withText = bracketed(trimmed, trimmed.indexOf('<')).inner
            continue
        }
        // An operator whose result type C++ computes from its arguments cannot be a declaration
        // (a `DynamicFuncTypeResolver` stays the one source of its type). The
        // declaration file records its reference entry in a comment, with the doc comment above
        // it, so the reference page still documents it without inventing a declaration.
        const dynamicMatch =
            module.kind === 'cpp' && trimmed.match(/^\/\/ dynamic func (\w+)\(/)
        if (dynamicMatch) {
            const args = bracketed(trimmed, trimmed.indexOf('('))
            const result = trimmed.slice(args.end + 1).replace(/^\s*:\s*/, '').trim()
            const entry = {
                module: module.name,
                name: dynamicMatch[1],
                section,
                line: i + 1,
                file: relative,
                with: [],
                norm: splitParams(args.inner),
                result,
                doc: [],
                purity: null,
                effect: '',
                fails: [],
                examples: [],
                fragments: [],
                uri: `${module.name}:${dynamicMatch[1]}`,
            }
            for (const text of doc) {
                if (text.startsWith('@pure')) entry.purity = 'pure'
                else if (text.startsWith('@effect')) {
                    entry.purity = 'effect'
                    entry.effect = text.slice('@effect'.length).trim()
                } else if (text.startsWith('@fails')) entry.fails.push(text.slice('@fails'.length).trim())
                else if (text.startsWith('@example-fragment')) entry.fragments.push(text.slice('@example-fragment'.length).trim())
                else if (text.startsWith('@example')) entry.examples.push(text.slice('@example'.length).trim())
                else entry.doc.push(text)
            }
            entries.push(entry)
            doc = []
            withText = ''
            pendingUri = null
            continue
        }
        // A C++ module's value is declared with no right side (`export let inf: float`), as a function
        // with no body stands for its operator; it carries the same doc comment as an operator.
        const valueMatch = module.kind === 'cpp' && trimmed.match(/^export let (\w+)\s*:\s*([^=]+?)\s*(?:=|$)/)
        if (valueMatch) {
            const entry = {
                module: module.name,
                name: valueMatch[1],
                section,
                line: i + 1,
                file: relative,
                value: true,
                with: [],
                norm: [],
                result: valueMatch[2],
                doc: [],
                purity: 'value',
                effect: '',
                fails: [],
                examples: [],
                fragments: [],
                uri: null,
            }
            for (const text of doc) {
                if (text.startsWith('@example-fragment')) entry.fragments.push(text.slice('@example-fragment'.length).trim())
                else if (text.startsWith('@example')) entry.examples.push(text.slice('@example'.length).trim())
                else if (!text.startsWith('@')) entry.doc.push(text)
            }
            entries.push(entry)
            doc = []
            withText = ''
            continue
        }
        // A tag line belongs to the declaration below it, in a module written in C++ and in one
        // written in Camel alike: it is the declaration's front matter, and a doc comment written
        // above the declaration - above the tag, or between the tag and the declaration - is its
        // description. The line therefore does not end the pending comment, which is what the AST
        // builder does too (it reads the comment above the declaration's first token, and the tag
        // is that token: src/parse/ast/builder.cpp, docOf). A `#op('module:function')` names the
        // operator a C++ declaration implements when it does not follow the convention (design
        // principles 6.4, step 1). This used to end the comment for a module written
        // in Camel, which lost the description of every such declaration from the page.
        if (trimmed.startsWith('#')) {
            const tagged = trimmed.match(/^#\[?op\(['"]([^'"]+)['"]\)/)
            if (tagged) {
                pendingUri = tagged[1]
            } else if (/^#impl\(/.test(trimmed)) {
                // `#impl(kernel)`: a Camel definition that carries a native kernel for speed. The
                // definition is what the page documents; the kernel is private to the module.
            } else if (module.kind === 'cpp') {
                // A signature file writes `#op(...)` and nothing else: another tag there is a typo,
                // not a declaration.
                throw new Error(`${path.basename(file)}:${i + 1}: unknown tag on a declaration`)
            }
            continue
        }
    // A declaration of `operator ==` names a reserved operator group (`__eq__`), which has no
    // Camel name to list: `inspect --ops` hides those names, `--verify-ops` filters them out, and
    // the page introduces the operators of a type in the module's text. Reading the declaration as
    // an entry called `operator ==` would ask --verify-ops for an operator of that name, which
    // does not exist.
    if (/^export\s+func\s+operator\b/.test(trimmed)) {
        doc = []
        withText = ''
        pendingUri = null
        continue
    }
    if (trimmed.startsWith(keyword + ' ') ||
        (options.kernels === true && /^func\s+\S/.test(trimmed))) {
            const isKernel = !trimmed.startsWith(keyword + ' ')
            const rest = trimmed.slice(isKernel ? 'func'.length + 1 : keyword.length + 1)
            const name = rest.slice(0, rest.indexOf('(')).replace(/::<.*>$/, '')
            const args = bracketed(rest, rest.indexOf('('))
            const after = rest.slice(args.end + 1)
            // A private function with a body is not an operator at all: it ends the pending
            // comment as any other private declaration of a library does.
            if (isKernel && declarationBody(after, lines, i)) {
                doc = []
                withText = ''
                pendingUri = null
                continue
            }
            // A pass declaration (design principles 9.3): a body-less function whose condition
            // is the pass applicability. A pass is a function the pipeline applies to the
            // program, not an operator the module registers; it is documented in docs/cli.md,
            // and its parameters are the declaration's `with` list. Only a compiled declaration
            // can declare a pass: a file under stdlib/signatures/ is reference text that is never
            // compiled, and describes operators the C++ resolver gives a computed type, where the
            // same condition is the type's (`grad::<F>(f: F): Gradient<F> when is_function::<F>()`).
            // A pass has the shape of a function transform, `(f: F): F!`; a declaration of
            // another shape that is conditioned the same way is an operator of a function value
            // (`graph_of::<F>(f: F): Graph`), and one that names its operator with `#op` is an
            // engine the module implements (`rewrite`, called as `f->rewrite<rules>`).
            const passShaped = (() => {
                const one = /^\s*\w+\s*:\s*(\w+)\s*$/.exec(args.inner)
                // A pass may name the resources it uses (`uses driver`) before its condition.
                const answer = /^\s*:\s*(\w+)!\s*(?:uses\s+[\w:, ]+?\s+)?when\b/.exec(after)
                return one !== null && answer !== null && one[1] === answer[1]
            })()
            if (!relative.startsWith('stdlib/signatures/') && !declarationBody(after, lines, i) &&
                passShaped && pendingUri === null &&
                /\bwhen\s+is_function::<\w+>\(\)\s*$/.test(after)) {
                doc = []
                withText = ''
                pendingUri = null
                continue
            }
            const result = readResultType(after, lines, i)
            const entry = {
                module: module.name,
                name,
                section,
                line: i + 1,
                file: relative,
                with: splitParams(withText),
                norm: splitParams(args.inner),
                result,
                inferred: result === '' && module.kind === 'camel',
                // A body-less `export func` in a library module written in Camel declares one of
                // the module's native operators (`array`'s sorts and primitives): there is no
                // subgraph, and the kernel is the operator's.
native: !declarationBody(after, lines, i),
                private: isKernel,
                doc: [],
                purity: null,
                effect: '',
                fails: [],
                examples: [],
                fragments: [],
                uri: null,
            }
            for (const text of doc) {
                if (text.startsWith('@pure')) entry.purity = 'pure'
                else if (text.startsWith('@effect')) {
                    entry.purity = 'effect'
                    entry.effect = text.slice('@effect'.length).trim()
                } else if (text.startsWith('@fails')) entry.fails.push(text.slice('@fails'.length).trim())
                else if (text.startsWith('@example-fragment')) entry.fragments.push(text.slice('@example-fragment'.length).trim())
                else if (text.startsWith('@example')) entry.examples.push(text.slice('@example'.length).trim())
                else entry.doc.push(text)
            }
            // A native entry names its operator like a C++ module's declaration does.
            if (entry.native) {
                // `module:function`, or what the `#op` tag above says.
                entry.uri = pendingUri || `${module.name}:${name}`
            }
            if (isKernel) {
                kernels.push(entry)
            } else {
                entries.push(entry)
            }
            doc = []
            withText = ''
            pendingUri = null
            continue
        }
        if (trimmed !== '' && !trimmed.startsWith('//')) {
            // Anything else ends a pending comment (a private function of a Camel library).
            doc = []
            withText = ''
            pendingUri = null
        }
    }
    return entries
}

// The declarations of a module: its own file, and, when a declaration cannot express every
// operator, the signature file that keeps the computed ones. The computed entries come first:
// they are the module's entry points (`encode`, `decode_as`), and the rest follow in the order
// of the declaration file.
function parseDeclarations(module) {
    const files = [sourceOf(module)]
    if (module.computed) {
        files.unshift(path.join(STDLIB_DIR, module.computed))
    }
    const entries = []
    const sectionNotes = new Map()
    const sections = []
    for (const file of files) {
        const part = readDeclarations(file, module)
        for (const [name, notes] of part.sectionNotes) {
            // A section may be declared in both files (the computed file and the declarations):
            // the introduction is the one that has text, so a bare `// ---- name ----` used only to
            // keep the section in the module's order does not erase the other file's note.
            if (!sectionNotes.has(name) || (sectionNotes.get(name).length === 0 && notes.length > 0)) {
                sectionNotes.set(name, notes)
            }
        }
        sections.push(...part.sections)
        entries.push(...part)
    }
    entries.sectionNotes = sectionNotes
    entries.sections = sections
    return entries
}

// The private operators of a module: the kernels its `#impl` functions run. They are body-less
// declarations the module does not export, so the reference page does not list them (a private name
// is not the module's interface) and `--verify-ops` checks them against the operators the module
// registers, marked private in the listing.
//
// The module's own file is read whichever language implements the module: `str`, `re` and `hash`
// are compiled by C++ and still declare their kernels in Camel, with the `#op` tag that names the
// operator they answer. A signature file declares no private kernel at all (it writes `#op(...)`
// and exported prototypes only), so reading every module's source collects exactly the kernels
// that exist. The kind of the module says what compiles it, not what its declaration file is.
function parseKernels(module) {
    return readDeclarations(sourceOf(module), module, { kernels: true }).kernels
}

// The comment at the top of the file, without the two-line header, the blank line after it and the
// note that the file is not compiled.
function parseHeader(module) {
    const text = fs.readFileSync(sourceOf(module), 'utf8').replace(/\r\n/g, '\n')
    const body = text.replace(/^(?:\/\/[^\n]*\n){2}\n/, '')
    const match = body.match(/^\/\*\*\n([\s\S]*?)\n \*\//)
    if (!match) return []
    const lines = match[1].split('\n').map((l) => l.replace(/^ \* ?/, ''))
    const stop = lines.findIndex((l) => l.startsWith('For reference only'))
    const kept = stop >= 0 ? lines.slice(0, stop) : lines
    while (kept.length && kept[kept.length - 1].trim() === '') kept.pop()
    // The first line of a signature file says what the file is; the reference has its own title.
    if (kept.length && kept[0].startsWith('Signatures of')) {
        kept.shift()
        while (kept.length && kept[0].trim() === '') kept.shift()
    }
    return kept
}

// ---- the document -------------------------------------------------------------------------

function signatureText(entry) {
    if (entry.value) return `${entry.name}: ${entry.result}`
    const withPart = entry.with.length ? `<${entry.with.join(', ')}> ` : ''
    return `${entry.name}: ${withPart}(${entry.norm.join(', ')}) => ${entry.result || (entry.inferred ? '?' : 'void')}`
}

function renderDoc() {
    const out = []
    out.push('# Standard Library Reference')
    out.push('')
    out.push('<!-- Generated by scripts/stdlib-reference.mjs --write. Do not edit: change the declarations')
    out.push('     (stdlib/*.cml and stdlib/signatures/*.cml) or the comments of the library and')
    out.push('     regenerate. -->')
    out.push('')
    out.push('The functions of the core standard library, by module. The boundary is the one the design')
    out.push('principles draw (section 10.5): a primitive that touches the system or is sensitive to')
    out.push('performance is an operator written in C++, an opaque node of the graph; logic composed from')
    out.push('primitives is written in Camel, a subgraph that passes can see into.')
    out.push('')
    out.push('Every function follows the [standard library conventions](stdlib-conventions.cn.md): one fact')
    out.push('one place, the declaration is the signature, naming, parameters, error kinds, and where a')
    out.push('module lives.')
    out.push('')
    out.push('How to read an entry:')
    out.push('')
    out.push('- **Signature**: `name: <with-parameters> (parameters) => result`. The value a function works')
    out.push("  on is its ordinary parameter and what configures it is a with-parameter, so calls chain:")
    out.push("  `s->trim->split<','>`, `xs->filter<p>->sort_by<key>`. `int` is the 64-bit integer and `float`")
    out.push('  the 64-bit float; `i32` and `f32` are the 32-bit ones; numeric types are never converted')
    out.push('  implicitly.')
    out.push('- **Pure** operators depend on their arguments only: a pass may evaluate a call ahead of time,')
    out.push('  merge two equal calls or drop an unused one. An operator **with effects** is left alone, and')
    out.push('  its call runs only when it is reachable from a `sync` block or its result is used (design')
    out.push('  principles 3.1). Every operator of these modules declares which it is.')
    out.push('- **Failure**: a *fault* stops the program with a located diagnostic (an index out of range, an')
    out.push('  argument outside what the operator defines). An *error* is a value: the function returns')
    out.push('  `T!` and the program handles it with `!!`, `->` or `match`. A result that may not exist is')
    out.push('  `T?`, handled with `??`. An entry without a failure line does not fail.')
    out.push("- **Named arguments**: a call may give a parameter by name (`f(x: 1)`, `len(x: xs)`) when")
    out.push('  the name gives that position the same name in every form the callee has, whether the callee is')
    out.push('  a Camel declaration or a group of built-in rules; the type of the argument then decides which')
    out.push("  form runs (`len`'s one parameter is `x` for a string, an array, a map and a set, so one name")
    out.push('  reaches all four). The name is written in the list that holds the parameter: an ordinary one')
    out.push("  in the parentheses (`len(x: xs)`), a `with` one in the angle brackets (`j->get<key: 'port'>`).")
    out.push('  A form that gives a position no name, or leaves its count to the arguments (a rest list,')
    out.push("  as `println` takes), is one no named argument reaches: the call")
    out.push('  reports that the signature is not known where the call is written. `contains` refuses a named')
    out.push('  argument for a reason of its own: its position 0 means an element in two forms and the')
    out.push('  substring in the third, so the name would mean another thing in one of them.')
    out.push('- Write one call that has with-arguments per statement when several appear in one argument')
    out.push('  list: `f<a>(x), g<b>(y)` is read as comparisons.')
    out.push('')
out.push('Not in this reference: the profiler (`perf`).')
    out.push('')
    out.push('| Module | Import | Written in |')
    out.push('|---|---|---|')
    const parsed = MODULES.filter((module) => pendingReason(module) === null).map((module) => ({
        module,
        entries: parseDeclarations(module),
    }))
    for (const { module, entries } of parsed) {
        const anchor = module.name === 'globals' ? 'built-in-functions' : module.name
        out.push(
            `| [${module.name === 'globals' ? 'built-in' : module.name}](#${anchor}) | ` +
                `${module.name === 'globals' ? 'none' : '`import { ... } from ' + module.name + '`'} | ` +
                `${module.kind === 'camel' ? 'Camel' : 'C++'} |`,
        )
    }
    for (const { module, entries } of parsed) {
        const anchor = module.name === 'globals' ? 'built-in-functions' : module.name
        out.push(
            `| [${module.name === 'globals' ? 'built-in' : module.name}](#${anchor}) | ` +
                `${module.name === 'globals' ? 'none' : '`import { ... } from ' + module.name + '`'} | ` +
                ` |`,
        )
    }
    out.push('')
    for (const { module, entries } of parsed) {
        out.push(`<a id="${module.name === 'globals' ? 'built-in-functions' : module.name}"></a>`)
        out.push('')
        out.push(`## ${module.title}`)
        out.push('')
        const header = parseHeader(module)
        if (header.length) {
            out.push(...header)
            out.push('')
        }
        // Entries are grouped by section, in the order the `// ---- name ----` lines declare, and
        // a section may be written in both files of the module (its computed signatures and its
        // declarations): `tensor` writes `binary form` in both, and the two read as one section
        // Ordering by the declarations rather than by the first entry lets a
        // computed file keep the module's order even when only a few of its sections hold a
        // computed name. Within a section, overloads written one after another
        // under one comment are one entry.
        const sectionOrder = []
        const seenSections = new Set()
        const pushSection = (key) => {
            if (!seenSections.has(key)) {
                seenSections.add(key)
                sectionOrder.push(key)
            }
        }
        if (entries.some((entry) => !entry.section)) pushSection('')
        for (const section of entries.sections || []) pushSection(section)
        for (const entry of entries) pushSection(entry.section || '')
        const bySection = new Map()
        for (const entry of entries) {
            const key = entry.section || ''
            if (!bySection.has(key)) bySection.set(key, [])
            bySection.get(key).push(entry)
        }
        for (const key of sectionOrder) {
            // A section marker may order the module without holding an entry of its own (the
            // computed file repeats its declaration file's sections to keep their place).
            const sectionEntries = bySection.get(key)
            if (!sectionEntries) continue
            if (key) {
                out.push(`### ${key[0].toUpperCase()}${key.slice(1)}`)
                out.push('')
                const notes = entries.sectionNotes.get(key) || []
                if (notes.length) {
                    out.push(...notes)
                    out.push('')
                }
            }
            const groups = []
            for (const entry of sectionEntries) {
                const last = groups[groups.length - 1]
                if (last && last[0].name === entry.name && entry.doc.length === 0) last.push(entry)
                else groups.push([entry])
            }
            for (const group of groups) {
                const entry = group[0]
                out.push(`#### \`${entry.name}\``)
                out.push('')
                out.push('```camel')
                for (const overload of group) out.push(signatureText(overload))
                out.push('```')
                out.push('')
                out.push(entry.doc.join('\n'))
                out.push('')
                if (entry.value) {
                    out.push('- A value: written without parentheses, a constant where it is used.')
                } else if (module.kind === 'camel' && entry.native) {
                    out.push('- A native operator of this module: an opaque node, pure.')
                } else if (module.kind === 'camel') {
                    out.push('- Written in Camel: a subgraph, pure when the functions passed to it are.')
                } else if (entry.purity === 'pure') {
                    out.push('- Pure.')
                } else if (entry.purity === 'effect') {
                    out.push(`- With effects: ${entry.effect || 'not pure'}.`)
                }
                // A declaration that writes neither `@pure` nor `@effect` states nothing about
                // purity, and an operator's purity is registered in C++
                // (`OperatorTable::declare`), which this page is not generated from: the page
                // says nothing rather than guessing "not pure".
                for (const fails of entry.fails) {
                    out.push(`- Failure: ${fails}.`)
                }
                for (const example of entry.examples) {
                    const [code, result] = example.split(/\s{2,}\/\/\s*/)
                    if (entry.fragments.length > 0) {
                        out.push(result ? `- Example (a fragment, not compiled): \`${code}\` gives \`${result}\`` : `- Example (a fragment, not compiled): \`${code}\``)
                    } else {
                        out.push(result ? `- Example: \`${code}\` gives \`${result}\`` : `- Example: \`${code}\``)
                    }
                }
                for (const reason of entry.fragments) out.push(`- Why it is not compiled: ${reason}`)
                out.push('')
            }
        }
    }
    return out.join('\n').replace(/\n{3,}/g, '\n\n').trimEnd() + '\n'
}

const STATEMENT_STARTS = /^(let|var|if|for|while|return|sync|match)\b/
const BUILTIN_TYPES = new Set([
    'int', 'i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64', 'usize',
    'float', 'f32', 'f64', 'bool', 'string', 'char', 'void', 'null', 'any', 'never',
    'Map', 'Set', 'Bytes', 'T', 'U', 'V', 'K', 'P', 'R', 'A', 'F',
])

// The code an `@example` line shows, without the `// result` a page may append to it.
function exampleCode(example) {
    return example.split(/\s{2,}\/\/\s*/)[0].trim()
}

// The call of the entry's own name in an example, with the receiver of `->` and the text of the
// argument list. `j->get<'a'>` has receiver `j` and no arguments; `conv2d(x, w, b, 1, 0)` has
// arguments; `let t = open(p)` has the call on the right of `=`.
function findCall(code, name) {
    const pattern = new RegExp(`\\b${name}\\b`, 'g')
    let match
    while ((match = pattern.exec(code))) {
        let index = match.index + name.length
        while (index < code.length && /\s/.test(code[index])) index++
        if (code[index] === '<') index = skipBalanced(code, index, '<', '>')
        while (index < code.length && /\s/.test(code[index])) index++
        let args = null
        if (code[index] === '(') {
            const end = skipBalanced(code, index, '(', ')')
            args = code.slice(index + 1, end - 1)
        } else if (index < code.length && !/[\w:]/.test(code[index])) {
            // A bare name (a value, not a call) is not a call site.
        }
        const receiver = code.slice(0, match.index).match(/([A-Za-z_]\w*)\s*->\s*$/)
        return { receiver: receiver ? receiver[1] : null, args }
    }
    return null
}

function skipBalanced(text, start, open, close) {
    let depth = 0
    for (let index = start; index < text.length; index++) {
        if (text[index] === open) depth++
        else if (text[index] === close) {
            depth--
            if (depth === 0) return index + 1
        }
    }
    return text.length
}

function splitArguments(text) {
    if (text === null) return []
    const parts = []
    let depth = 0
    let quote = ''
    let start = 0
    for (let index = 0; index < text.length; index++) {
        const char = text[index]
        if (quote) {
            if (char === quote && text[index - 1] !== '\\') quote = ''
            continue
        }
        if (char === "'" || char === '"') quote = char
        else if ('([{<'.includes(char)) depth++
        else if (')]}>'.includes(char)) depth--
        else if (char === ',' && depth === 0) {
            parts.push(text.slice(start, index).trim())
            start = index + 1
        }
    }
    if (text.slice(start).trim() !== '') parts.push(text.slice(start).trim())
    return parts
}

// A parameter's type without the default it may be given: `keepdims: bool = false` states `bool`,
// and the default is the call's, not the port's. A cut is only made at the top level and only for an
// `=`, not for the `=>` of a function type.
function typeWithoutDefault(type) {
    let depth = 0
    for (let i = 0; i < type.length; i++) {
        const char = type[i]
        if ('([{<'.includes(char)) depth++
        else if (')]}>'.includes(char)) depth--
        else if (char === '=' && type[i + 1] !== '>' && depth === 0) return type.slice(0, i).trim()
    }
    return type.trim()
}

// The positions a declaration writes, in the order the operator registers them: the ordinary
// parameters, then the `<>` ones (`OpDef::params`, written `withAfterNorm`; the protocol
// `modules/transforms/jvp.cpp` states where it matches a call). One name for a reduction and its
// axis form (design principles 7.3) is exactly this: `sum` declares one ordinary parameter and two
// `with` ones, `sum<axis, keepdims>(t)`, and the operator registers three ports -- so the two lists
// are one list when the counts add up. When they do not (a declaration may write a `with` parameter
// the operator keeps out of its ports), the ordinary parameters alone are compared and the arity
// stop names the difference.
function declaredParams(entry, ruleParams = null) {
    const lists = [entry.norm]
    if (ruleParams !== null && entry.norm.length + entry.with.length === ruleParams.length) {
        lists.push(entry.with)
    }
    return lists
        .flat()
        .map((text) => {
            // A pack writes its dots before the name (`...rest: T`); the dots are not part of it.
            const match = text.match(/^(?:\.{2,3})?(\w+)\s*\??\s*:\s*(.+)$/)
            return match ? { text, name: match[1], type: typeWithoutDefault(match[2]) } : null
        })
        .filter(Boolean)
}

// The names the example uses that stand for the declaration's own parameters: the argument at
// position i takes parameter i's type, and the receiver of `->` takes parameter 0's. An argument
// that is a literal or an expression is not a name and binds nothing.
const NAME = /^[A-Za-z_]\w*$/

// A keyword is not a name: `set_terminal_raw_mode(true)` must not make `true` a parameter.
const RESERVED = new Set([
    'true', 'false', 'null', 'func', 'let', 'var', 'if', 'else', 'for', 'while', 'match', 'return',
    'sync', 'with', 'type', 'struct', 'enum', 'rule', 'export', 'module', 'import', 'as', 'is',
])

// The type to write for a parameter the declaration gives in terms of type variables, or null when
// the example has to supply its own value. A function type is what a lambda annotates, and a doc
// example counts in `float`, so each single-letter variable in one becomes `float`: `(x: T) => float`
// is the instance `f` gets beside the call. Every other use of a variable -- `T[]`, `Map<K, V>`, a
// bare `T`, `Device<T>` -- stays unwritable, and the compile reports the name as unresolved.
function typeAtScalar(type) {
    const text = (type || '').trim()
    if (!/\b[A-Z]\b/.test(text)) return text
    if (!text.includes('=>')) return null
    return text.replace(/\b[A-Z]\b/g, 'float')
}

function bindArguments(entry, code) {
    const params = declaredParams(entry)
    const call = findCall(code, entry.name)
    if (!call) return []
    const bindings = []
    const bind = (name, param) => {
        if (!param || !NAME.test(name) || RESERVED.has(name)) return
        const type = typeAtScalar(param.type)
        if (type === null) return
        if (!bindings.some((binding) => binding.name === name)) bindings.push({ name, type })
    }
    if (call.receiver) bind(call.receiver, params[0])
    splitArguments(call.args).forEach((argument, index) => {
        const shifted = call.receiver ? index + 1 : index
        bind(argument, params[shifted])
    })
    return bindings
}

// The type names an example needs in scope: the types of the parameters it binds (a qualified
// name is not imported) and the capitalized names the example writes itself, such as an enum
// (`NodeKind::Oper`) or a lambda's annotation (`(j: Json) => ...`). `Map` and `Set` are built in
// and resolve nowhere, which is not an error.
function exampleTypeNames(bindings, code) {
    const names = []
    for (const binding of bindings) {
        for (const name of binding.type.match(/\b[A-Z]\w*/g) || []) if (!names.includes(name)) names.push(name)
    }
    for (const name of code.match(/\b[A-Z]\w*/g) || []) if (!names.includes(name)) names.push(name)
    return names.filter((name) => !BUILTIN_TYPES.has(name))
}

// A module exports a type; which module is not written in a signature file, and a type name is
// not always the module's name (`Node` is `graph`'s). The resolver tries the entry's own module
// first (a module's own types are its own), then the modules whose declaration text mentions the
// name, then every module; the result is cached for the run.
const typeModuleCache = new Map()
const probeImportCache = new Map()
const moduleSources = new Map()

function moduleSource(module) {
    if (!moduleSources.has(module.name)) {
        const files = [sourceOf(module)]
        if (module.computed) files.push(path.join(STDLIB_DIR, module.computed))
        moduleSources.set(
            module.name,
            files
                .filter((file) => fs.existsSync(file))
                .map((file) => fs.readFileSync(file, 'utf8'))
                .join('\n'),
        )
    }
    return moduleSources.get(module.name)
}

async function resolveTypeModule(name, preferred) {
    if (typeModuleCache.has(name)) return typeModuleCache.get(name)
    // A type is usually exported by the module of its name (`Tensor`, `Json`, `Bytes`, `Node`);
    // otherwise by one whose declarations mention it. Every candidate is tried as a type, not as
    // an import: a module may export a *value* of the name and not the type.
    const named = MODULES.map((module) => module.name).filter((module) => module.toLowerCase() === name.toLowerCase())
    const mentioners = MODULES.map((module) => module.name).filter(
        (module) => module !== preferred && moduleSource(module).includes(name),
    )
    const candidates = [...new Set([...named, preferred, ...mentioners, ...MODULES.map((module) => module.name)])]
    for (const candidate of candidates) {
        if (await probeImport(name, candidate)) {
            typeModuleCache.set(name, candidate)
            return candidate
        }
    }
    typeModuleCache.set(name, null)
    return null
}

// Whether `name` is a type a module exports: the probe uses it as a type annotation, because an
// import of the same name may succeed and still bring only a value.
async function probeImport(name, moduleName) {
    const key = `${name}@${moduleName}`
    if (probeImportCache.has(key)) return probeImportCache.get(key)
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-doc-type-'))
    try {
        const file = path.join(dir, 'main.cml')
        // A type with parameters (`Run<F>`) cannot be written by its bare name, so the probe tries
        // it as a type constructor too: `Run<int>` is a type for any payload, and a name that is
        // not generic simply fails the second spelling after passing the first.
        for (const spelling of [name, `${name}<int>`]) {
            fs.writeFileSync(
                file,
                `import { ${name} } from ${moduleName}\nfunc __doc_type(v: ${spelling}): void {\n    let _ = v\n}\n` +
                    'func main(): int sync { return 0 }\n',
            )
            const result = await runCamelCompile(file, dir)
            if (result.ok) {
                probeImportCache.set(key, true)
                return true
            }
        }
        probeImportCache.set(key, false)
        return false
    } finally {
        fs.rmSync(dir, { recursive: true, force: true })
    }
}

// The Camel a doc example compiles as: the module's names in scope, the example's arguments bound
// to their declared parameter types, and the example inside a function. An example that starts
// with a statement keyword is used as a statement; anything else is an expression and becomes
// `let __doc_example = <expression>`. A call that returns nothing cannot be bound, so an
// expression whose compile fails is retried as a statement before the failure is reported (a void
// call is a valid example of a writer such as save_params).
async function exampleSource(module, entries, entry, code, asStatement = false) {
    const names = [...new Set(entries.map((e) => e.name))]
    const lines = []
    if (module.name !== 'globals') {
        lines.push(`import { ${names.join(', ')} } from ${module.name}`)
    }
    const bindings = bindArguments(entry, code)
    const types = new Map()
    for (const name of exampleTypeNames(bindings, code)) {
        const owner = await resolveTypeModule(name, module.name)
        if (owner) types.set(name, owner)
    }
    for (const [name, owner] of types) lines.push(`import { ${name} } from ${owner}`)
    const params = bindings.map((binding) => `${binding.name}: ${binding.type}`).join(', ')
    // An example that returns an error value leaves a function declared to return a `T!`, so it
    // compiles in a fallible body.
    const fallible = /^return\s+error\(/.test(code.trim())
    lines.push(`func __doc_example(${params}): ${fallible ? 'int!' : 'void'} {`)
    // A call of a `void` function has no value to bind, so it is a statement, not an expression.
    if (asStatement || entry.result === 'void' || STATEMENT_STARTS.test(code.trim())) {
        lines.push(`    ${code.trim()}`)
    } else {
        lines.push(`    let __doc_example = ${code.trim()}`)
    }
    lines.push('}')
    lines.push('func main(): int sync { return 0 }')
    return lines.join('\n') + '\n'
}

// The environment a run gives the install. Two facts, both about the install rather than the shell:
//
//   - `CAMEL_HOME` is that install's root, not whatever the shell exported: a doc example compiles
//     with the standard library and the runtime library of the install this run measured.
//   - An install built with a newer toolchain needs a newer `libstdc++` than the host's, and the
//     loader is told where to find one the same way the test runner tells its children:
//     `CAMEL_TEST_EXTRA_LIB_DIRS`, `:`-separated (the runner joins them the same way). Without it a
//     reader who runs this script by hand gets "GLIBCXX_3.4.36 not found" from a binary that the
//     gate runs without trouble, and learns to skip the check rather than to set the path.
function installEnvironment(exe) {
    const env = { ...process.env }
    env.CAMEL_HOME = path.dirname(path.dirname(exe))
    const extra = process.env.CAMEL_TEST_EXTRA_LIB_DIRS
    if (extra && process.platform !== 'win32') {
        const dirs = extra.split(':').filter(Boolean)
        env.LD_LIBRARY_PATH = [...dirs, env.LD_LIBRARY_PATH].filter(Boolean).join(':')
    }
    return env
}

function runCamelCompile(file, cwd) {
    return new Promise((resolve) => {
        const exe = thisTreeCamel('a doc example')
        const env = installEnvironment(exe)
        const child = spawn(exe, [file, 'std::gir'], {
            cwd,
            env,
            stdio: ['ignore', 'pipe', 'pipe']
        })
        let output = ''
        child.stdout.on('data', (chunk) => (output += chunk))
        child.stderr.on('data', (chunk) => (output += chunk))
        child.on('error', (error) => resolve({ ok: false, message: error.message }))
        child.on('close', (code) => {
            if (code === 0) return resolve({ ok: true })
            const line = output
                .split(/\r?\n/)
                .map((entry) => entry.trim())
                .find((entry) => /\b(error|fault)\b/i.test(entry))
            // The diagnostic's own name, taken before the message is shortened: the class of the
            // stop is the kind in `(name=SemanticDiag::UnresolvedReference, ...)`, and a message cut
            // at 240 characters can lose it.
            const name = /[(]name=([A-Za-z]+)Diag::/.exec(line || '') || /error\[([A-Za-z]+)Diag::/.exec(line || '')
            resolve({
                ok: false,
                diagnostic: name === null ? null : name[1],
                message: (line || output.trim().split(/\r?\n/)[0] || `exit ${code}`).slice(0, 240),
            })
        })
    })
}

async function compileExample(job) {
    const { module, entries, entry, code } = job
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-doc-example-'))
    try {
        const file = path.join(dir, 'main.cml')
        // The names the wrapper has to bind for the example to be a program at all: its own bare
        // names, taken as the parameters of `__doc_example` and typed from the declaration. An
        // example that needs none is a program of its own -- nothing is missing from it.
        const bindings = bindArguments(entry, code).map((binding) => binding.name)
        fs.writeFileSync(file, await exampleSource(module, entries, entry, code))
        const result = await runCamelCompile(file, dir)
        if (!result.ok && !STATEMENT_STARTS.test(code)) {
            fs.writeFileSync(file, await exampleSource(module, entries, entry, code, true))
            const asStatement = await runCamelCompile(file, dir)
            if (asStatement.ok) return { ...job, ok: true, bindings }
        }
        return { ...job, ok: result.ok, message: result.message, diagnostic: result.diagnostic, bindings }
    } finally {
        fs.rmSync(dir, { recursive: true, force: true })
    }
}

// Compiles every example of a module, at most JOBS at a time; one temp directory per example. An
// entry that marks its example a fragment is compiled too, and the job remembers that it is one:
// the marker is a claim about what the compiler will say, and only the compiler can settle it.
async function compileExamples(module, entries) {
    const jobs = []
    const fragments = []
    const seen = new Set()
    // How many examples a declaration name has in one file, so each one can be named without a line
    // number: `<file>#<name>#<k>`. The count is of the examples this run compiles, in declaration
    // order, so it moves only when that name's examples are added, removed or reordered -- not when a
    // comment above them grows a line.
    const ordinals = new Map()
    let declared = 0
    for (const entry of entries) {
        if (entry.fragments.length > 0) fragments.push(entry)
        for (const example of entry.examples) {
            declared++
            const code = exampleCode(example)
            // The same call written twice -- the module's declaration file and its computed
            // signatures, or two overloads sharing a doc block -- is one program: dedupe on the
            // text, not the name, so a second entry's *different* example is still compiled.
            const key = `${entry.name}\u0000${code}`
            if (seen.has(key)) continue
            seen.add(key)
            const basis = `${entry.file}#${entry.name}`
            const ordinal = (ordinals.get(basis) || 0) + 1
            ordinals.set(basis, ordinal)
            jobs.push({
                module,
                entries,
                entry,
                code,
                file: entry.file,
                line: entry.line,
                name: entry.name,
                member: `${basis}#${ordinal}`,
                fragment: entry.fragments.length > 0,
            })
        }
    }
    const results = new Array(jobs.length)
    let cursor = 0
    const width = Math.max(1, Math.min(Number(process.env.CAMEL_DOC_JOBS || 8), jobs.length || 1))
    async function worker() {
        while (cursor < jobs.length) {
            const index = cursor++
            results[index] = await compileExample(jobs[index])
        }
    }
    await Promise.all(Array.from({ length: width }, worker))
    return { results: results.filter(Boolean), fragments, declared }
}

// ---- auditing the documentation ------------------------------------------------------------

// The doc-comment rule, as a shape the checker can decide:
//
//   * A name the reference prints has a description: at least one prose line in its `///`
//     block. A block of tags only (`@pure` alone) is not a description. The description is
//     required, not optional: every name is a call a reader will make.
//   * A name the reference prints has an `@example`. It is required, not optional, for the
//     same reason. Compiling it is required too: the checker binds the example's arguments to the
//     declared parameter types and compiles the call. An entry whose example cannot be a program
//     of its own (it is a fragment of a program that has the names the description introduces)
//     says so with `@example-fragment <reason>`: the reason is required, and the reference shows
//     it, so "not compiled" is a decision with a reason, never a silent gap.
//   * A name whose declared result is an error (`T!`) has an `@fails` line naming the error.
//     A name that cannot fail must not be forced to invent one, so `@fails` is required only
//     when the type says the function returns an error. A fault is not in the type: a `@fails`
//     line that names a fault stays optional. A result that is a bare type variable (`T!`, `F!`)
//     passes on an error the function did not define (the error of the function it calls, say);
//     the description says which, and no kind can be named here.
//   * A `@fails` line names what it can be: `fault `Name``, `error `Name``, or a diagnostic
//     kind. A line without a name is not a failure description.
function docProblems(entries) {
    const problems = []
    const seen = new Set()
    for (const entry of entries) {
        if (seen.has(entry.name)) continue
        seen.add(entry.name)
        const where = `${entry.file}:${entry.line}`
        const prose = entry.doc.filter((line) => line.trim() !== '')
        if (prose.length === 0) {
            problems.push(`${where}: '${entry.name}' has no description`)
        }
        if (entry.examples.length === 0) {
            problems.push(`${where}: '${entry.name}' has no @example`)
        }
        for (const example of entry.examples) {
            if (example.trim() === '') problems.push(`${where}: '${entry.name}' has an empty @example`)
        }
        for (const reason of entry.fragments) {
            if (reason.trim() === '') {
                problems.push(`${where}: '${entry.name}' marks its example a fragment without a reason (@example-fragment <reason>)`)
            }
        }
        // `string!` is an error result; `string?` is an optional one, not an error. `T!` is an error
        // the function did not define, so its description says where it comes from instead.
        const result = entry.result.trim()
        if (/!\s*$/.test(result) && !/^[A-Z]!$/.test(result) && entry.fails.length === 0) {
            problems.push(`${where}: '${entry.name}' returns ${entry.result}, so it needs an @fails line naming the error`)
        }
        for (const fails of entry.fails) {
            if (!/\b(fault|error)(\s+value)?\s+`[^`]+`/.test(fails)) {
                problems.push(`${where}: '${entry.name}' has an @fails line that names no fault or error: '${fails}'`)
            }
        }
    }
    return problems
}

// The class of an example that does not compile, read off the diagnostic the compiler reports. The
// text format names it `error[SemanticDiag::UnresolvedReference]`, the short format ends the line
// with `(name=SemanticDiag::UnresolvedReference, code=0x0300000B)`, and an internal error says so in
// words; the first word after the `Diag::` (or the words) is the kind. The vocabulary is the
// compiler's own -- lexical, syntax, semantic, internal -- so a kind this table does not hold is a
// diagnostic type someone added, and the run refuses it rather than filing it under "other". null
// is that refusal.
const EXAMPLE_STOP_NAMES = new Map([
    ['lexical', 'a lexical error'],
    ['syntax', 'a syntax error'],
    ['semantic', 'a semantic error'],
    ['internal', 'an internal error'],
])

function exampleStopClass(result) {
    const text = (result && result.message) || ''
    if (result && result.diagnostic) return EXAMPLE_STOP_NAMES.get(result.diagnostic.toLowerCase()) || null
    if (/\binternal error\b/i.test(text)) return EXAMPLE_STOP_NAMES.get('internal')
    const found = /\b(?:error\[|name=)([A-Za-z]+)Diag::/.exec(text)
    return found === null ? null : EXAMPLE_STOP_NAMES.get(found[1].toLowerCase()) || null
}

// The check the plan runs: the doc-comment rule above, plus every example compiled -- the ones a
// reader is shown and the ones marked a fragment, because a marker is a claim ("this example is not
// a program of its own") and a claim nobody tries stays true only until someone writes a wrong one.
// It needs a built compiler, like --verify-ops.
async function verifyDocs() {
    const problems = []
    let checked = 0
    let fragments = 0
    // What happened to every example of every module, so that none of them is dropped: how many were
    // written, how many are a program of their own, the ones that compile, and the ones that do not,
    // by class and one by one -- each of those named as `<file>#<name>#<k>`, never counted.
    const coverage = {
        written: 0,
        programs: 0,
        compiled: 0,
        members: new Map(),
        unlabelled: [],
        stale: [],
        bound: [],
        thin: [],
        stopped: [],
    }
    const tally = (result) => {
        if (result.ok) {
            coverage.compiled++
            if (result.fragment) {
                // The marker says "not a program of its own". It is stale when the example compiles
                // with nothing bound -- then it *is* a program of its own, and the page says
                // otherwise. It is right when the compile needed the names only the surrounding
                // program has: those names are what the reason has to give, so a reason that does
                // not name one of them is the other failure, `thin` below.
                if (result.bindings.length === 0) {
                    coverage.stale.push(result)
                } else {
                    const reason = result.entry.fragments.join(' ')
                    const unnamed = result.bindings.filter((name) => !new RegExp(`\\b${name}\\b`).test(reason))
                    coverage.bound.push(result)
                    if (unnamed.length > 0) coverage.thin.push({ ...result, unnamed })
                }
            }
            return
        }
        const label = exampleStopClass(result)
        coverage.stopped.push({ ...result, label })
        if (label === null) coverage.unlabelled.push(result)
        else {
            if (!coverage.members.has(label)) coverage.members.set(label, new Map())
            if (!coverage.members.get(label).has(result.member)) {
                coverage.members.get(label).set(result.member, { ...result, label })
            }
        }
    }
    for (const module of MODULES) {
        const pending = pendingReason(module)
        const entries = parseDeclarations(module)
        const compiled = await compileExamples(module, entries)
        coverage.written += compiled.declared
        coverage.programs += compiled.results.length
        fragments += compiled.fragments.length
        for (const result of compiled.results) tally(result)
        if (pending !== null) {
            // Not compiled as a problem -- the module is left out on purpose -- but compiled all the
            // same, because "the examples compile now" is the signal that the omission is over and
            // the page should carry the module. Only the examples the page would present count: a
            // module whose every example is marked a fragment has nothing to show, and the marker is
            // about what the reading can carry, not about what the compiler says. A pending module
            // that still fails says nothing; an empty result says nothing either.
            const presented = compiled.results.filter((result) => !result.fragment)
            if (presented.length > 0 && presented.every((result) => result.ok)) {
                problems.push(
                    `stdlib/${module.name}.cml is listed as pending in MODULES ('${pending}') but its examples ` +
                        `compile now: drop its 'pending' entry in scripts/stdlib-reference.mjs, run --write, and the ` +
                        `page carries the module`,
                )
            }
            continue
        }
        problems.push(...docProblems(entries))
        for (const result of compiled.results) {
            checked++
            // A marked fragment that does not compile is what the marker is for: the run counts it
            // and the class table says why. An example the page presents as a program has to
            // compile, and the failure names the file, the entry and the compiler's own words.
            if (!result.ok && !result.fragment) {
                problems.push(
                    `${result.file}:${result.line}: '${result.name}' has an @example that does not compile: ` +
                        `\`${result.code}\`: ${result.message}`,
                )
            }
        }
    }
    return { problems, checked, fragments, coverage }
}

// The Camel a doc example compiles as: the entry inside `main`, with the module's names in
// scope. An example that starts with a statement keyword is used as a statement; anything else
// is an expression and becomes `let __doc_example = <expression>`. A call that returns nothing
// cannot be bound, so an expression whose compile fails is retried as a statement before the
// failure is reported (a void call is a valid example of a writer such as save_params).
// ---- checking against the registered operators --------------------------------------------

// The install a run measures: `--camel <path>` on the command line, else this tree's `out/latest`.
// The stamp check runs once per process, the first time a mode actually needs the compiler, so
// `--write` / `--verify-doc` / `--audit` -- which read the tree only -- need no install at all, and
// a mode that compiles pays for one `build-info.js check` rather than one per example.
let installChecked = false

function thisTreeCamel(why) {
    const exe = camelPath || defaultCamel(REPO_ROOT)
    if (!installChecked) {
        refuseInstallNotOfThisTree(exe, REPO_ROOT, `stdlib-reference.mjs (${why})`)
        installChecked = true
    }
    return exe
}

function registeredOperators() {
    const exe = thisTreeCamel('--verify-ops')
    const run = spawnSync(exe, ['inspect', '--ops', PROBE], {
        encoding: 'utf8',
        maxBuffer: 1 << 26,
        env: installEnvironment(exe)
    })
    // `spawnSync` reports a binary it could not start (`out/latest` never built, a stale install
    // from another tree) as `error`, with `status === null` and `stdout`/`stderr` both undefined:
    // the message below then read "failed (exit null): undefined" and left the reader to guess
    // which install was missing. `stdlib-declarations.mjs` handles its own `run.error` this way.
    if (run.error) {
        throw new Error(`could not run ${exe} inspect --ops: ${run.error.message}`)
    }
    if (run.status !== 0) {
        throw new Error(`camel inspect --ops failed (exit ${run.status}): ${run.stderr || run.stdout}`)
    }
    return run.stdout
        .split('\n')
        .filter((line) => line.startsWith('OPERATOR '))
        .map((line) => JSON.parse(line.slice('OPERATOR '.length)))
}

function uriMatches(pattern, uri) {
    const source = pattern.split('*').map((part) => part.replace(/[.+?^${}()|[\]\\]/g, '\\$&')).join('.*')
    return new RegExp(`^${source}$`).test(uri)
}

function countsAgree(declared, registered) {
    // -1: any number, -2: the resolver does not tell.
    return declared === registered || declared < 0 || registered < 0
}

// ---- reading the parameter names of a signature ---------------------------------------------
//
// The name at the head of a parameter (`key: K`, `...rest: U...`, `args?: (...)`, `var n: int`) is
// what a named argument binds to, so the names on the two sides have to be the same ones at the
// same positions -- the counts alone agree while the names drift (
// 2026-10-24: `--verify-ops` compares the whole signature, not only the number of parameters).

const BRACKETS = { '(': ')', '[': ']', '{': '}', '<': '>' }

// The index of the bracket closing the one at `open`, or -1. Each group is balanced, so counting
// the opening bracket's own character finds its partner (`<m: Map<K, V>>` closes at the last `>`).
function partnerBracket(text, open) {
    const close = BRACKETS[text[open]]
    let depth = 0
    for (let i = open; i < text.length; i++) {
        if (text[i] === '=' && text[i + 1] === '>') {
            i++
            continue
        }
        if (text[i] === text[open]) depth++
        else if (text[i] === close && --depth === 0) return i
    }
    return -1
}

// The index of the `=>` between a signature's parameters and its result: the last one outside
// every group (the `=>` of a function type written as a parameter is inside one).
function resultArrow(signature) {
    let depth = 0
    let arrow = -1
    for (let i = 0; i < signature.length; i++) {
        const c = signature[i]
        if (c === '=' && signature[i + 1] === '>') {
            if (depth === 0) arrow = i
            i++
            continue
        }
        if ('([{<'.includes(c)) depth++
        else if (')]}>'.includes(c)) depth--
    }
    return arrow
}

// The name the parameter `spelling` writes at its head; '' when it writes none, or writes a
// parameter that is never given by name (a rest parameter collects the arguments left over).
function parameterName(spelling) {
    const part = spelling.trim()
    if (part === '' || part.startsWith('...')) return ''
    const found = /^(?:var\s+)?([A-Za-z_]\w*)\s*\??\s*:/.exec(part)
    return found ? found[1] : ''
}

// The names of the parameters written between brackets, split at the commas that are not nested;
// null when one of them writes no name.
function namesInList(text) {
    const names = []
    let at = 0
    for (;;) {
        let depth = 0
        let comma = -1
        for (let i = at; i < text.length; i++) {
            const c = text[i]
            if (c === '=' && text[i + 1] === '>') {
                i++
                continue
            }
            if ('([{<'.includes(c)) depth++
            else if (')]}>'.includes(c)) depth--
            else if (c === ',' && depth === 0) {
                comma = i
                break
            }
        }
        const part = text.slice(at, comma < 0 ? text.length : comma).trim()
        if (part === '') return comma < 0 ? names : null
        const name = parameterName(part)
        if (name === '') return null
        names.push(name)
        if (comma < 0) return names
        at = comma + 1
    }
}

// The parameter names one registered signature writes, `with` ones first. null when the text is
// not a signature (a computed rule that spells something else), so nothing is compared for it.
function registeredNames(signature) {
    const arrow = resultArrow(signature || '')
    if (arrow < 0) return null
    const head = signature.slice(0, arrow)
    let at = 0
    let withNames = []
    if (head.startsWith('<')) {
        const close = partnerBracket(head, 0)
        if (close < 0) return null
        withNames = namesInList(head.slice(1, close))
        if (withNames === null) return null
        at = close + 1
    }
    while (at < head.length && /\s/.test(head[at])) at++
    if (head[at] !== '(') return null
    const close = partnerBracket(head, at)
    if (close < 0) return null
    const normNames = namesInList(head.slice(at + 1, close))
    if (normNames === null) return null
    return { with: withNames, norm: normNames }
}

// The parameter names a declaration writes, at the same two lists. null when one of its
// parameters writes none (`with <f>` has no name), which is a different problem, not a comparison.
function namesOfDeclaration(entry) {
    const withNames = namesInList(entry.with.join(', '))
    const normNames = namesInList(entry.norm.join(', '))
    if (withNames === null || normNames === null) return null
    return { with: withNames, norm: normNames }
}

// ---- the result type, on both sides ----------------------------------------------------------
//
// A declaration states the type its call has, and the operator states its own in the signature it
// registers: `inspect --ops` prints that signature, and the text after its last `=>` is the
// operator's result (`resultDoc` for a kernel of `tensor`/`nn`, what the resolver computes for a
// module whose rules are C++). Comparing only names, arity, parameter names and purity left the
// result free to be a lie: `one_hot` was declared `int64[...]` while its kernel, its inference and
// every test returned `float32` rows, and nothing read the two statements against each other.
//
// The comparison is on the type, not on the spelling. The authority for which spellings name one
// type is the compiler's own table (`initInnerTypes`, src/compile/gct/builder.cpp): `int` and
// `i64` are Int64, `float` and `f64` are Float64, and `f32`, `i32`, `bf16`, `f16`, `c64`, `c128`,
// `bool`, `string`, `Bytes`, `void` and `never` are their own. Two types are the same only when
// their codes are, so `f32[...]` and `float[...]` stay two types here (design principles 5.1: no
// value converts between them), while `int64[...]`, `i64[...]` and `int[...]` are one. A name this
// table does not hold is not read at all: a type variable (`T[...]`), a module's named type
// (`Gradient<F>`, `Device<f32>`) and an aggregate the C++ side names in prose (`Tensor`,
// `NumericAggregate`) are reported as not compared, never as a difference.

// A result written as one of these names is read; every other name makes the entry unreadable.
// The middle column is the type the compiler holds for that spelling. The spellings are the
// program's own names (src/compile/gct/builder.cpp, `initInnerTypes`), the explicit names of the
// same types (`i64` for Int64, which the declaration files write as `int64`; `float64` for
// Float64), and the names the tensor module writes an element type with (`dtypeName`,
// modules/tensor/dtype.cpp: `float32`, `float64`, `bfloat16`, `float16`, `int32`, `int64`) --
// those are what a kernel's own `resultDoc` says, so a claim of `float32[...]` and a declaration of
// `f32[...]` are the same type here.
const RESULT_SCALARS = new Map([
    ['int', 'int'],
    ['i64', 'int'],
    ['int64', 'int'],
    ['float', 'float'],
    ['f64', 'float'],
    ['float64', 'float'],
    ['f32', 'f32'],
    ['float32', 'f32'],
    ['i32', 'i32'],
    ['int32', 'i32'],
    ['bf16', 'bf16'],
    ['bfloat16', 'bf16'],
    ['f16', 'f16'],
    ['float16', 'f16'],
    ['c64', 'c64'],
    ['c128', 'c128'],
    ['bool', 'bool'],
    ['string', 'string'],
    ['Bytes', 'Bytes'],
    ['void', 'void'],
    ['never', 'never'],
])

// The element types a bare `float` claim stands for: a result whose element type follows an
// argument is one of them (a tensor's elements are never bf16/f16 in a kernel that takes the
// argument's dtype, but the set is the float family the word names).
const FLOAT_ELEMENTS = new Set(['float', 'f32', 'bf16', 'f16'])

// How many names an exception list shows in one line before it counts the rest.
const EXCEPTIONS_SHOWN = 12

// The type a signature states, with the conditions a declaration may add after it removed: a
// `when ...` or `uses ...` clause is about the operator, not about its result.
function withoutConditions(text) {
    const source = (text || '').trim()
    let depth = 0
    for (let i = 0; i < source.length; i++) {
        const c = source[i]
        if (c === '=' && source[i + 1] === '>') {
            i++
            continue
        }
        if ('([{<'.includes(c)) depth++
        else if (')]}>'.includes(c)) depth--
        else if (depth === 0 && /\s/.test(c) && /^(?:when|uses)\b/.test(source.slice(i + 1))) {
            return source.slice(0, i).trimEnd()
        }
    }
    return source
}

// The commas of `text` that are not inside a group, or null when the text does not balance.
function splitTopLevel(text, separator = ',') {
    const parts = []
    let at = 0
    let depth = 0
    for (let i = 0; i < text.length; i++) {
        const c = text[i]
        if (c === '=' && text[i + 1] === '>') {
            i++
            continue
        }
        if ('([{<'.includes(c)) depth++
        else if (')]}>'.includes(c)) {
            if (--depth < 0) return null
        } else if (c === separator && depth === 0) {
            parts.push(text.slice(at, i))
            at = i + 1
        }
    }
    if (depth !== 0) return null
    parts.push(text.slice(at))
    return parts
}

// The index of the `[` that opens a result's dimension list, or -1 when none does. The `[` of the
// list is read before the group-opening branch: it is the suffix this looks for, not a group.
function dimensionIndex(text) {
    let depth = 0
    for (let i = 0; i < text.length; i++) {
        const c = text[i]
        if (c === '[') {
            if (depth === 0) return i
            depth++
        } else if ('({<'.includes(c)) depth++
        else if (')}>]'.includes(c)) depth--
    }
    return -1
}

// What a dimension list says, in the form the two sides are compared in: `[...]` is any number of
// dimensions, `[?]` is one of unknown extent (`[]` is that list too), and `[n, m]` keeps the
// extents it writes. null when the text is not a dimension list.
function dimensionsOf(text) {
    if (text === '') return ''
    if (!text.startsWith('[') || partnerBracket(text, 0) !== text.length - 1) return null
    const inner = text.slice(1, -1).trim()
    if (inner === '') return '[?]'
    const parts = splitTopLevel(inner)
    if (parts === null) return null
    const written = parts.map((part) => {
        const one = part.trim()
        if (/^\.\.[A-Za-z_]\w*$/.test(one)) return '..'
        if (one === '?') return '?'
        return one.replace(/\s+/g, '')
    })
    if (written.length > 0 && written.every((one) => one === '..')) return '[...]'
    return `[${written.join(', ')}]`
}

// The result as a type the two sides can be compared on: `{text, opaque}`, or null when the text
// is not a result type at all. `opaque` marks a name only the module can read (a type variable, a
// named type, an aggregate), which makes the two sides incomparable rather than different.
// A tuple writes its parts in parentheses and may carry a dimension list of its own
// (`(f32[...], f32[...])[]`), and a pack may be written with a trailing comma
// (`(...ElementOf<A>,)`), which is the same tuple.
function canonicalResult(text) {
    const source = withoutConditions(text)
    if (source === '') return null
    if (source.startsWith('(')) {
        const close = partnerBracket(source, 0)
        if (close < 0) return null
        const dimensions = dimensionsOf(source.slice(close + 1).trim())
        if (dimensions === null) return null
        const parts = splitTopLevel(source.slice(1, close))
        if (parts === null) return null
        const kept = parts.filter((part, i) => !(i === parts.length - 1 && part.trim() === ''))
        if (kept.length === 0) return { text: `()${dimensions || ''}`, opaque: true, family: false }
        const inner = kept.map((part) => canonicalResult(part))
        if (inner.some((one) => one === null)) return null
        return {
            text: `(${inner.map((one) => one.text).join(', ')})${dimensions || ''}`,
            opaque: inner.some((one) => one.opaque),
            family: false,
        }
    }
    const at = dimensionIndex(source)
    const base = (at < 0 ? source : source.slice(0, at)).trim()
    const dimensions = dimensionsOf(at < 0 ? '' : source.slice(at).trim())
    if (dimensions === null || base === '') return null
    const known = RESULT_SCALARS.get(base)
    if (known === undefined) {
        // A name the compiler's table does not hold: one module's own type (a type variable, a
        // named type) or a sentence (`the tensor's element type`). A suffix is kept so that two
        // spellings of the same variable still agree.
        return { text: `${base}${dimensions}`, opaque: true, family: false }
    }
    // `float` alone is the word the tensor module writes where an operator gives back a tensor of
    // its argument's element type: `softmax`, `layer_norm`, `linear` and `softmax_grad` return
    // `dtypeOf<T>()` of the input (modules/tensor/kernels/reduce.cpp) and their `resultDoc` says
    // `float[...]`, while an operator that returns f64 says `float64[...]` (`from_bytes_f64`,
    // `to_bytes_f64`). So a bare `float` in a claim is the element family, not one element type.
    return { text: `${known}${dimensions}`, opaque: false, family: base === 'float' }
}

// The result an operator claims: the text after the last `=>` of the signature it registers.
function claimedResult(signature) {
    const arrow = resultArrow(signature || '')
    return arrow < 0 ? '' : signature.slice(arrow + 2)
}

// ---- reading a type function (`match type`) --------------------------------------------------
//
// A type function is an ordinary function on types (design principles 1: a type is a compile-time
// value), and the library writes one as `match type`:
//
//   export type WithElement<A, E> = match type A {
//       case X[...Dims] => E[...Dims]
//       case X[...] => E[...]
//   }
//
// That text reaches a signature on both sides, and it is the same function on both. A declaration
// may write it where it writes the result; an operator declared `bf16::<A>(x: A):
// WithElement<A, bf16>` registers the name applied -- the compiler prints what the function answers
// with the element fixed, in the words of the definition:
//
//   (...) => match type A { case X[...Dims] => bf16[...Dims]; case X[...] => bf16[...] }
//
// The checker read neither. `=>` inside the text made it say "a function type", `[{]` made the
// result unreadable to `canonicalResult`, and two promises that are the same were listed as an
// exception instead of being compared -- the one answer this comparison exists to give. What
// follows reads the shape as what it is: a result that depends on the argument's shape, compared
// case by case, and, when two of them differ, at the dimension that differs.

// The type functions the library defines, by name, as `{params, body}` with `body` the `match type`
// text. Read once, on the first comparison that needs them: a mode that renders the page, audits
// the table or checks the examples never reads one.
let typeFunctionTable = null

function typeFunctions() {
    if (typeFunctionTable !== null) return typeFunctionTable
    const table = new Map()
    const visit = (dir) => {
        for (const item of fs.readdirSync(dir, { withFileTypes: true })) {
            const full = path.join(dir, item.name)
            if (item.isDirectory()) {
                visit(full)
                continue
            }
            if (!item.name.endsWith('.cml')) continue
            const lines = fs.readFileSync(full, 'utf8').replace(/\r\n/g, '\n').split('\n')
            for (let i = 0; i < lines.length; i++) {
                const found = /^\s*(?:export\s+)?type\s+([A-Z]\w*)\s*(?:<([^>]*)>)?\s*=\s*(.+)$/.exec(lines[i])
                if (found === null || !/^match\s+type\b/.test(found[3].trim())) continue
                let body = found[3].trim()
                for (let j = i + 1; braceDepth(body) > 0 && j < lines.length; j++) body += ` ${lines[j].trim()}`
                const params = found[2] === undefined ? [] : splitTopLevel(found[2]).map((one) => one.trim())
                table.set(found[1], { params, body })
            }
        }
    }
    visit(STDLIB_DIR)
    typeFunctionTable = table
    return table
}

// The index of the `=>` separating a case's pattern from its result, or -1 when there is none at
// the top level. `=>` inside a group (a function-typed result) belongs to that group.
function topLevelArrow(text) {
    let depth = 0
    for (let i = 0; i < text.length; i++) {
        const c = text[i]
        if (c === '=' && text[i + 1] === '>') {
            if (depth === 0) return i
            i++
            continue
        }
        if ('([{<'.includes(c)) depth++
        else if (')]}>'.includes(c)) depth--
    }
    return -1
}

// A `match type` text as its subject and its cases, or null when the text is not one. One reader
// serves both sides: the form the compiler prints for a registered result is the form a
// declaration writes, and a reader that took one and not the other would compare a promise with
// its own text.
function parseMatchType(text) {
    const source = (text || '').trim()
    if (!/^match\s+type\b/.test(source)) return null
    let depth = 0
    let open = -1
    for (let i = 0; i < source.length; i++) {
        const c = source[i]
        if (c === '=' && source[i + 1] === '>') {
            i++
            continue
        }
        if ('([<'.includes(c)) depth++
        else if (')]>'.includes(c)) depth--
        else if (c === '{' && depth === 0) {
            open = i
            break
        }
    }
    if (open < 0) return null
    const close = partnerBracket(source, open)
    if (close !== source.length - 1) return null
    const subject = source.slice('match type'.length, open).trim()
    if (subject === '') return null
    const cases = matchTypeCases(source.slice(open + 1, close))
    if (cases === null) return null
    return { subject, cases: cases.list, elided: cases.elided }
}

// The cases of a `match type` body: `case <pattern> => <result>`, separated by `;` (the form the
// compiler prints) or one `case` per line (the form the library writes). A lone `...` is the rest
// of a case list not stated, so the cases it stands for are not compared and are not called a
// difference. A case this reader cannot read makes the whole text unreadable rather than partly
// compared: half a case list is not a promise.
function matchTypeCases(body) {
    const chunks = []
    let depth = 0
    let start = 0
    for (let i = 0; i < body.length; i++) {
        const c = body[i]
        if (c === '=' && body[i + 1] === '>') {
            i++
            continue
        }
        if ('([{'.includes(c)) {
            depth++
            continue
        }
        if (')]}'.includes(c)) {
            if (--depth < 0) return null
            continue
        }
        if (depth !== 0) continue
        if (c === ';') {
            chunks.push(body.slice(start, i))
            start = i + 1
            continue
        }
        if (/^case\b/.test(body.slice(i)) && (i === start || /[\s;]/.test(body[i - 1]))) {
            if (i > start) chunks.push(body.slice(start, i))
            start = i
        }
    }
    chunks.push(body.slice(start))
    const list = []
    let elided = false
    for (const chunk of chunks) {
        const piece = chunk.trim()
        if (piece === '') continue
        if (/^\.{2,3}$/.test(piece)) {
            elided = true
            continue
        }
        if (!/^case\b/.test(piece)) return null
        const rest = piece.slice('case'.length).trim()
        const arrow = topLevelArrow(rest)
        if (arrow < 0) return null
        const pattern = rest.slice(0, arrow).trim()
        const result = rest.slice(arrow + 2).trim()
        if (pattern === '' || result === '') return null
        list.push({ pattern, result })
    }
    if (list.length === 0) return null
    return { list, elided }
}

// One side of a case -- its pattern or its result -- as the two sides are compared on it: its base
// type, through the spellings the compiler holds (`int64` is `int`, `bfloat16` is `bf16`), and the
// dimension list it writes. The dimension list keeps the names it binds: `bf16[...Dims]` says "the
// argument's extents" and `bf16[?...]` does not, so the two are not the same promise and the name
// is what tells them apart. null when the text is not a type this reader holds.
function matchPiece(text) {
    const source = (text || '').trim().replace(/\s+/g, ' ')
    if (source === '') return null
    const at = dimensionIndex(source)
    const base = (at < 0 ? source : source.slice(0, at)).trim()
    if (base === '') return null
    let entries = []
    if (at >= 0) {
        const list = source.slice(at).trim()
        if (!list.startsWith('[') || partnerBracket(list, 0) !== list.length - 1) return null
        const inner = list.slice(1, -1).trim()
        if (inner === '') {
            entries = ['?']
        } else {
            const parts = splitTopLevel(inner)
            if (parts === null) return null
            entries = parts.map((part) => part.trim().replace(/\s+/g, ''))
            if (entries.every((one) => one === '...')) entries = ['...']
        }
    }
    const known = RESULT_SCALARS.get(base)
    return { base: known === undefined ? base : known, written: base, entries }
}

// How the two sides of one part of a case stand: equal, different at a named place, or one of them
// not a type this reader holds (null -- the caller hands that pair to the comparison it had).
function compareMatchPieces(declared, operator, what) {
    const mine = matchPiece(declared)
    const theirs = matchPiece(operator)
    if (mine === null || theirs === null) return { same: null }
    if (mine.base !== theirs.base) {
        return { same: false, at: `the ${what}'s type`, mine: mine.written, theirs: theirs.written }
    }
    const count = Math.max(mine.entries.length, theirs.entries.length)
    for (let i = 0; i < count; i++) {
        const a = mine.entries[i] === undefined ? '(none)' : mine.entries[i]
        const b = theirs.entries[i] === undefined ? '(none)' : theirs.entries[i]
        if (a !== b) return { same: false, at: `the ${what}'s dimension ${i + 1}`, mine: a, theirs: b }
    }
    return { same: true }
}

// A type function expanded into a pack, with the container around it kept for comparison. For
// example, `(...ElementOf<A>)[]` has body `ElementOf<A>` and suffix `[]`; the registered form may
// write the body out as a `match type` and keep a trailing comma inside the pack.
function matchTypeExpansion(text) {
    const source = (text || '').trim()
    if (!source.startsWith('(...')) return null
    const close = partnerBracket(source, 0)
    if (close < 0) return null
    return {
        body: source.slice(4, close).trim().replace(/,$/, '').trim(),
        suffix: source.slice(close + 1).trim(),
    }
}

// The `match type` form a text stands for: the text itself, or the definition of the name it
// applies when the library defines that name as a type function. null when the text is a name this
// reader has no definition for, or a type function applied to a subject the type layer would have
// evaluated: neither is a form this comparison reads, and both stay with the exception classes.
function matchTypeForm(text) {
    const source = withoutConditions(text)
    const expansion = matchTypeExpansion(source)
    if (expansion !== null) {
        const inner = matchTypeForm(expansion.body)
        return inner === null ? null : { ...inner, suffix: expansion.suffix }
    }
    const direct = parseMatchType(source)
    if (direct !== null) return { ...direct, suffix: '' }
    const applied = applyTypeFunction(source)
    if (applied === null) return null
    const evaluated = parseMatchType(applied)
    return evaluated === null ? null : { ...evaluated, suffix: '' }
}

// `Name<arguments>` with `Name` a type function, instantiated as the definition writes it. The
// subject is the parameter the body matches on; a call still written on a type variable is the
// form a registered signature prints, and the only one this reader compares.
function applyTypeFunction(text) {
    const source = (text || '').trim()
    const found = /^([A-Z]\w*)\s*</.exec(source)
    if (found === null) return null
    const known = typeFunctions().get(found[1])
    if (known === undefined) return null
    const open = source.indexOf('<', found[1].length)
    const close = partnerBracket(source, open)
    if (close !== source.length - 1) return null
    const args = splitTopLevel(source.slice(open + 1, close))
    if (args === null || args.length !== known.params.length) return null
    const values = new Map(known.params.map((name, i) => [name, args[i].trim()]))
    const bodyOpen = known.body.indexOf('{')
    if (bodyOpen < 0) return null
    const subject = known.body.slice('match type'.length, bodyOpen).trim()
    const given = values.get(subject)
    if (given === undefined || !/^[A-Z]$/.test(given)) return null
    let body = known.body
    for (const [name, value] of values) body = body.replace(new RegExp(`\\b${name}\\b`, 'g'), value)
    return body
}

// Whether a declaration's result and the registered result are the same type function: an answer
// when both sides are one, and null when they are not both -- that pair belongs to the comparison
// the caller already has (`canonicalResult` and its exception classes), and this reader does not
// take it over.
function matchTypeVerdict(declaredText, operatorText) {
    const mine = matchTypeForm(declaredText)
    const theirs = matchTypeForm(operatorText)
    if (mine === null || theirs === null) return null
    if (mine.suffix !== theirs.suffix) {
        return {
            same: false,
            why: `the declaration's container \`${mine.suffix}\` and the operator's \`${theirs.suffix}\` differ`,
        }
    }
    const subject = compareMatchPieces(mine.subject, theirs.subject, 'subject')
    if (subject.same !== true) {
        return {
            same: false,
            why: `the declaration matches on \`${mine.subject}\` and the operator on \`${theirs.subject}\``,
        }
    }
    const shared = Math.min(mine.cases.length, theirs.cases.length)
    for (let i = 0; i < shared; i++) {
        const a = mine.cases[i]
        const b = theirs.cases[i]
        const pattern = compareMatchPieces(a.pattern, b.pattern, 'pattern')
        if (pattern.same === null) {
            return {
                same: false,
                why: `case ${i + 1}: the declaration's pattern \`${a.pattern}\` is not a type this checker reads`,
            }
        }
        if (pattern.same === false) {
            return {
                same: false,
                why: `case ${i + 1}: ${pattern.at} is \`${pattern.mine}\`, and the operator's is \`${pattern.theirs}\``,
            }
        }
        const result = compareMatchPieces(a.result, b.result, 'result')
        if (result.same === null) {
            return {
                same: false,
                why: `case ${i + 1}: the declaration's result \`${a.result}\` is not a type this checker reads`,
            }
        }
        if (result.same === false) {
            return {
                same: false,
                why: `case ${i + 1}: ${result.at} is \`${result.mine}\`, and the operator's is \`${result.theirs}\``,
            }
        }
    }
    if (mine.cases.length !== theirs.cases.length && !mine.elided && !theirs.elided) {
        return {
            same: false,
            why: `the declaration states ${mine.cases.length} case(s) and the operator ${theirs.cases.length}`,
        }
    }
    return {
        same: true,
        why: `the declaration's type function and the operator's are the same ${shared} case(s)`,
    }
}

// What a registered parameter says it takes (`modules/tensor/ops/op_def.cpp`, `kindText`). These
// are not types but sets of them, and `float[...]` in one is the operator's spelling for "a tensor",
// not for f64: the kind is what the argument reader takes, and a tensor of any element type is one
// it takes. A signature a resolver builds instead writes real types; those are compared as types.
const KIND_SETS = new Map([
    ['Tensor', ['tensor']],
    ['float[...] | number[]', ['tensor', 'array']],
    ['float[...] | number', ['tensor', 'number']],
    ['float[...] | number[] | number', ['tensor', 'array', 'number']],
    ['int', ['int']],
    ['number', ['number']],
    ['bool', ['bool']],
    ['string', ['string']],
    ['Bytes', ['Bytes']],
    ['int[]', ['array']],
])

// Whether a kind takes everything a need asks for: an `int` is a `number`. A kind that takes a
// tensor does not take an array, and the other way round, and `f32` is not `float`.
function kindCovers(kind, need) {
    if (kind === need) return true
    return kind === 'number' && ['int', 'i32', 'f32', 'float'].includes(need)
}

// A type variable: `A` in `bf16(x: A)` binds whatever the caller passes, so it takes everything.
const TYPE_VARIABLE = /^[A-Z]$/

// The exceptions this check carries, class by class, as data rather than prose -- and **one member at
// a time**. A member is a place this checker cannot read: `<operator> (<uri>)#result` on the result
// half, `<operator> (<uri>)#parameter` on the parameter half. Its value is the sentence that says why
// it is an exception, in this checker's own words ("the rule writes a function type, and no type
// variable holds one"), never "because it has always been here".
//
// ## Why members and not counts
//
// Author's ruling of 2026-11-03 13:30 (`docs/handover/reviews/questions-2026-10-29.md`): a count that
// can move while the behaviour stays the same protects nothing. The per-class counts this table used
// to hold were exactly that: an overload that started writing a different spelling moved the class
// while nothing about the library changed, and the failure said "records 6, and the run found 7" -- a
// number, not a name, so the reader had to bisect to find out who moved.
//
// Each class is now a table of its members, and the run compares it in both directions, naming the
// member every time:
//
//   * in the run and not in the table -> `entered`: the operator, what the two sides say, and the
//     request to add it with its reason;
//   * in the table and not in the run -> `left`: the operator, and the request to take it off --
//     that removal is the record of it having become readable.
//
// No line numbers anywhere: a line moves with every edit above it. One name can be several members:
// `from_bytes` registers one URI per element type, so it is five, while a name registered once is one
// member however many declarations answer to it.
const EXCEPTIONS = {
    // The operator's own result text is a shape no type variable holds: a function type, a tuple, a
    // record, a `gradient of W0`, a generic application (`Device<T>`), a named aggregate (`Tensor`),
    // a bare type variable, or no result text at all; and the cases where the rule reads and the
    // declaration is the side that does not (`T[...]` from `from_bytes`, `Jvp<F>` from the
    // transforms).
    result: {
        'rule states no result': {
            'tree_leaves (ckpt:tree_leaves)#result':
                'the rule registers no result text at all (`tree_leaves (type-directed)`); the declaration writes `L`',
        },
        'rule states a function type': {
            'attempt (:mark/attempt)#result':
                'the rule answers the function it was given, `((...args: A) => T!)`; the declaration states only the error `F!`',
            'first_with_as_param (transforms:first_with_as_param)#result':
                'the rule answers a function type -- the reinterpreted function, `((the first `with` parameter, then the parameters) => any)` -- and the declaration writes it `FirstWithAsParam<F>`, a type function this checker does not expand',
            'split (transforms:split)#result':
                'the rule answers a pair whose second half is a function type, `(float, (float) => float)`; the declaration spells the same pair as text, and a function type is not compared',
        },
        'rule states a tuple': {
            'batch_norm_train (nn:batch_norm_train)#result':
                'the rule answers `(Tensor, Tensor, Tensor)`; the declaration writes the same three as `f32[...]`',
            'batch_norm_train_grad (nn:batch_norm_train_grad)#result':
                'the rule answers `(Tensor, Tensor, Tensor)`; the declaration writes the same three as `f32[...]`',
            'grad (autodiff:grad_aux)#result':
                'the rule answers `(gradient of W0, aux)`, a pair one half of which is a phrase; the declaration writes `GradientAndAux<F>`',
            'jvp (transforms:jvp)#result':
                'the rule answers `(value, tangent)`, two prose words; the declaration writes `Jvp<F>`',
            'transpose (transforms:transpose)#result':
                'the rule answers `(cotangents..)`, a rest pack; the declaration writes `Transpose<F>`',
            'with_grad (autodiff:with_grad)#result':
                'the rule answers `(float, gradient of W0)`; the declaration writes `ValueAndGradient<F>`',
            'with_grad (autodiff:with_grad_aux)#result':
                'the rule answers `((float, aux), gradient of W0)`, a nested pair; the declaration writes `ValueAndGradientAndAux<F>`',
        },
        'rule states a record': {
            'effects (graph:effects)#result':
                'the rule answers a record literal, `{ external: bool, ... }`; the declaration names it `Effects`',
        },
        'rule states a gradient-of result': {
            'grad (autodiff:grad)#result':
                'the rule answers `gradient of W0`, a phrase about a derivative; the declaration writes `Gradient<F>`',
        },
        'rule states a generic application': {
            'upload (tiles:upload)#result':
                'the rule answers `Device<T>`, a generic application; the declaration narrows it to `Device<f32[...]>`',
        },
        'rule states a named type': {
            'avg_pool2d (nn:avg_pool2d)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
            'avg_pool2d_grad (nn:avg_pool2d_grad)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
            'batch_norm (nn:batch_norm)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
            'conv2d (nn:conv2d)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
            'conv2d_bias_grad (nn:conv2d_bias_grad)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
            'conv2d_input_grad (nn:conv2d_input_grad)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
            'conv2d_kernel_grad (nn:conv2d_kernel_grad)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
            'embedding (nn:embedding)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
            'embedding_table_grad (nn:embedding_table_grad)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
            'max_pool2d (nn:max_pool2d)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
            'max_pool2d_grad (nn:max_pool2d_grad)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
            'run_preferred_resident (tiles:run_preferred_resident)#result':
                'the rule answers `DeviceAggregate`, a named aggregate; the declaration writes `Device<f32>`',
            'softmax_cross_entropy_grad (nn:softmax_cross_entropy_grad)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
            'softmax_cross_entropy_grad (nn:softmax_cross_entropy_labels_grad)#result':
                'the rule answers `Tensor`, a named aggregate of the module; the declaration writes the `f32[...]` it produces',
        },
        'rule states a type variable': {
            'download (tiles:download)#result':
                'the rule answers `T`, the element type of the device it read; the declaration writes `f32[...]`',
        },
        'declaration states a type variable': {
            'from_bytes (tensor:from_bytes_bf16)#result':
                'the element type comes from the bytes, so the declaration writes `T[...]` and this rule answers `bfloat16[...]`',
            'from_bytes (tensor:from_bytes_bool)#result':
                'the element type comes from the bytes, so the declaration writes `T[...]` and this rule answers `bool[...]`',
            'from_bytes (tensor:from_bytes_f16)#result':
                'the element type comes from the bytes, so the declaration writes `T[...]` and this rule answers `float16[...]`',
            'from_bytes (tensor:from_bytes_f32)#result':
                'the element type comes from the bytes, so the declaration writes `T[...]` and this rule answers `float32[...]`',
            'from_bytes (tensor:from_bytes_f64)#result':
                'the element type comes from the bytes, so the declaration writes `T[...]` and this rule answers `float64[...]`',
            'from_bytes (tensor:from_bytes_i32)#result':
                'the element type comes from the bytes, so the declaration writes `T[...]` and this rule answers `int32[...]`',
            'from_bytes (tensor:from_bytes_i64)#result':
                'the element type comes from the bytes, so the declaration writes `T[...]` and this rule answers `int64[...]`',
            'load_npy (tensor:load_npy_bool)#result':
                'the element type comes from the file, so the declaration writes `T[...]` and this rule answers `bool[...]`',
            'load_npy (tensor:load_npy_f32)#result':
                'the element type comes from the file, so the declaration writes `T[...]` and this rule answers `float32[...]`',
            'load_npy (tensor:load_npy_f64)#result':
                'the element type comes from the file, so the declaration writes `T[...]` and this rule answers `float64[...]`',
            'load_npy (tensor:load_npy_i32)#result':
                'the element type comes from the file, so the declaration writes `T[...]` and this rule answers `int32[...]`',
            'load_npy (tensor:load_npy_i64)#result':
                'the element type comes from the file, so the declaration writes `T[...]` and this rule answers `int64[...]`',
            'remat (transforms:remat)#result':
                'the rule answers the argument\'s own type (`(f: F) => F`), which the declaration writes `F`; this rule states `float` for the two-parameter function it was asked about',
        },
        'declaration states a generic application': {
            'vmap (transforms:vmap)#result':
                'the declaration writes `Vmap<F>`, a type no spelling names, and the rule answers `f32[]`',
        },
    },
    // The same question one position to the left: a parameter the operator writes as a function type,
    // a `grad of P`, a named aggregate (`NumericAggregate`, `Device<T>`), an untyped pack; a
    // declaration that writes a bare `T`; and two parameter lists that are not a list at all or not
    // the same length.
    parameter: {
        'rule states no parameter list': {
            'tree_leaves (ckpt:tree_leaves)#parameter':
                'the registered signature is `tree_leaves (type-directed)`, not a list of `name: type` positions',
        },
        'declared arity differs from rule arity': {
            'slice (:op/slice_bytes)#parameter':
                'the declaration writes one parameter and the rule registers six positions, so there is no position-by-position pair',
            'slice (:op/slice_str)#parameter':
                'the declaration writes one parameter and the rule registers six positions, so there is no position-by-position pair',
            'slice (:op/slice_sub)#parameter':
                'the declaration writes one parameter and the rule registers six positions, so there is no position-by-position pair',
            'upload (tiles:upload)#parameter':
                'the declaration writes one parameter and the rule registers three, so there is no position-by-position pair',
        },
        'untyped pack ..rest': {
            'eprintln (:io/eprintln)#parameter':
                'the rule writes `(x: T, ...rest)`, a pack with no type, so no position maps to the declaration',
            'print (:io/print)#parameter':
                'the rule writes `(x: T, ...rest)`, a pack with no type, so no position maps to the declaration',
            'println (:io/println)#parameter':
                'the rule writes `(x: T, ...rest)`, a pack with no type, so no position maps to the declaration',
        },
        'function-typed parameter': {
            'attempt (:mark/attempt)#parameter':
                'parameter 1 is `(...args: A) => T`, a function type whose arguments are a pack',
            'grad (autodiff:grad)#parameter':
                'parameter 1 is `<W...>(N...) => float`, a function type with a `with` pack',
            'grad (autodiff:grad_aux)#parameter':
                'parameter 1 is `<W...>(N...) => (float, aux)`, a function type with a `with` pack',
            'jvp (transforms:jvp)#parameter':
                'parameter 1 is `(...) => any`, a function type whose positions are not spelled',
            'first_with_as_param (transforms:first_with_as_param)#parameter':
                'parameter 1 is `(...) => any`, a function type whose positions are not spelled',
            'remat (transforms:remat)#parameter':
                'parameter 1 is `(float, float) => float`, a function type the declaration writes as a bare `F`',
            'split (transforms:split)#parameter':
                'parameter 1 is `(float, float) => (float, float)`, a function type the declaration writes as a bare `F`',
            'transpose (transforms:transpose)#parameter':
                'parameter 1 is a union of function types, `(float) => float | (float, ...) => float`',
            'vmap (transforms:vmap)#parameter':
                'parameter 1 is `(f32) => f32`, a function type the declaration writes as a bare `F`',
            'with_grad (autodiff:with_grad)#parameter':
                'parameter 1 is `<W...>(N...) => float`, a function type with a `with` pack',
            'with_grad (autodiff:with_grad_aux)#parameter':
                'parameter 1 is `<W...>(N...) => (float, aux)`, a function type with a `with` pack',
        },
        'grad of P': {
            'adam (nn:adam)#parameter':
                'parameter 2 is `grad of P`, a phrase about a parameter\'s derivative rather than a type',
            'sgd (nn:sgd)#parameter':
                'parameter 2 is `grad of P`, a phrase about a parameter\'s derivative rather than a type',
        },
        'named-type parameter': {
            'benchmark (tiles:benchmark)#parameter':
                'parameter 1 is `NumericAggregate`, a named aggregate of the tiles module',
            'download (tiles:download)#parameter':
                'parameter 1 is `Device<T>`, a generic application, not a type a declaration can write',
            'inspect (tiles:inspect)#parameter':
                'parameter 1 is `NumericAggregate`, a named aggregate of the tiles module',
            'run_preferred_resident (tiles:run_preferred_resident)#parameter':
                'parameter 1 is `NumericAggregate`, a named aggregate of the tiles module',
            'source (tiles:source)#parameter':
                'parameter 1 is `NumericAggregate`, a named aggregate of the tiles module',
        },
    }
}

// The numbers used to count every overload of the tree, the operators a merge brought in included:
// `transforms:remat`'s declaration writes the type variable (`f: F` in, `F` out) while the operator
// states the function type the call gets (`((float, float) => float) => (float, float) => float`), so
// neither side can be read from the other, and the tree either has that operator or it does not. That
// is now said by the member `remat (transforms:remat)#result` being on the list or not: a tree that
// has not merged the operator finds no such member, and the failure names it instead of counting one
// fewer in a class.
//
// The examples that do not compile, by the kind of diagnostic that stopped them, and the examples a
// `@example-fragment` marker calls "not compiled" that the checker compiles anyway. Both are member
// tables the run compares its own members with: a member that appears, or one that goes away, is
// named -- so the way past is to say which example it is and why it stops there, never to move a
// number.
//
// A member of this table is the example, not the name: `<file>#<name>#<k>`, where `k` counts the
// examples that declaration name has in that file, in declaration order. A file and a name are what
// a reader needs; `k` keeps two examples of one name apart without a line number, which would move
// with every edit above it.
const EXAMPLE_STOPS = {
    // What stops here needs a value no example program can write, and the marker on each says which:
    // a `Bytes` built with `bytes`'s `encode`, a node or a transaction or a rule set of a pass, the
    // stream a tee reads, a Python callable, a function the gradient needs a `with` parameter on. The
    // member is the example -- `<file>#<name>#<k>` -- and the sentence beside it names what the example
    // is missing, so a member that enters this list says which example stopped and why, and one that
    // leaves says which example the marker no longer excuses.
    'a semantic error': {
        'stdlib/fs.cml#append_bytes#1':
            'the example hands `encode(\'abc\')` to an operator that takes `Bytes`, and `encode` is `bytes`\'s name, which this page does not import',
        'stdlib/fs.cml#read_bytes#1':
            'the example compares the read against `encode(\'\')`, which is `bytes`\'s name and not this module\'s',
        'stdlib/fs.cml#read_chunk#1':
            'the example compares the read against `encode(\'\')`, which is `bytes`\'s name and not this module\'s',
        'stdlib/fs.cml#write_bytes#1':
            'the example builds its `Bytes` with `encode(\'abc\')`, which is `bytes`\'s name',
        'stdlib/graph.cml#constant#1':
            '`n` is a node of the surrounding pass, and no example program has one',
        'stdlib/graph.cml#erase#1':
            '`t` is an open transaction and `n` a node of its graph: a doc example has neither',
        'stdlib/graph.cml#eval#1':
            '`t` is an open transaction and `n` a node of its graph: a doc example has neither',
        'stdlib/graph.cml#replace#1':
            '`t` is an open transaction and `n` a node of its graph: a doc example has neither',
        'stdlib/hash.cml#sha256#1':
            'the example builds its `Bytes` value with `encode(\'abc\')`, which is `bytes`\'s name',
        'stdlib/prelude.cml#len#3':
            'the `Bytes` overload\'s example builds its value with `encode(\'h€\')`, which is `bytes`\'s name',
        'stdlib/prelude.cml#slice#3':
            'the `Bytes` overload\'s example names `encode`, which is `bytes`\'s, and a doc example is compiled with this module\'s names only',
        'stdlib/prelude.cml#tee_note#1':
            'the last operand is the stream a tee reads, and no example program has one',
        'stdlib/python.cml#call#1':
            'the callable is a Python object from `attr`, and `sqrt` is a name the surrounding program binds',
        'stdlib/rewrite.cml#explain#1':
            'a run and two of its values come from the caller, and `a` is one of those values',
        'stdlib/rewrite.cml#explain_all#1':
            'a run comes from the caller, and `r` is it',
        'stdlib/rewrite.cml#extract#1':
            'the cost function comes from `rules`, and `rules::nodes` is not a name the page binds',
        'stdlib/rewrite.cml#rewrite#1':
            'a rule set comes from the caller, and `rules` is it',
        'stdlib/rewrite.cml#run#1':
            'a rule set comes from the caller, and `rules` is it',
        'stdlib/rewrite.cml#saturate#1':
            'a rule set comes from the caller, and `rules` is it',
        'stdlib/rewrite.cml#why_not#1':
            'a run, a rule and a value come from the caller, and `target` is one of them',
        'stdlib/vjp.cml#grad#1':
            '`grad` is generic in the function it takes (`F`), so the checker can write no value for the parameter: the function whose first `with` parameter is differentiated is a declaration in the surrounding program',
        'stdlib/signatures/autodiff.cml#grad#1':
            '`loss` must have a `with` parameter for its gradient to exist, and only a declaration in the surrounding program gives it one',
        'stdlib/signatures/autodiff.cml#grad#2':
            'the function must return a pair whose first element is the loss, which only a declaration in the surrounding program writes',
        'stdlib/signatures/autodiff.cml#with_grad#1':
            '`loss` must have a `with` parameter for its gradient to exist, and only a declaration in the surrounding program gives it one',
        'stdlib/signatures/autodiff.cml#with_grad#2':
            'the function must return a pair whose first element is the loss, which only a declaration in the surrounding program writes',
        'stdlib/signatures/transforms.cml#first_with_as_param#1':
            'the example reinterprets a function that has a `with` parameter, and a `with` list is written on a declaration: no lambda this page can write inline has one'
    },
    // `an internal error` is gone (2026-11-03). The class held the examples that reached the
    // compiler with a function-typed parameter bound by name: `split`'s own example, which kept a
    // live dereference node, and then, while the split ABI moved, `stdlib/vjp.cml`'s example and the
    // two fragments beside it, which still destructured the pair `split(f)` no longer answers. The
    // step that lane recorded as pending has landed: every one of those examples compiles, so a run
    // that finds nothing in this class is the run this table asks for, and the class is removed
    // rather than left holding a number nothing points at.
}

// A fragment is a claim -- "this example is not a program of its own" -- and the marker is only
// honest while the compiler agrees *and* something really is missing. Both halves are checked: an
// example that compiles with nothing bound is a program of its own, so its marker is stale; an
// example that compiles only once a name is bound has a marker that is right, but its reason has to
// give that name. This is the swept number: the 54 markers that used to excuse a program that
// compiles are gone -- the 17 examples that stood alone lost theirs (16 markers: two examples of
// `range` share one), and the 37 that need a name keep it and say which -- so the count of stale
// markers is nothing, and it cannot grow quietly: a new marker on a program that compiles shows up
// here the moment the example is compiled.
const STALE_FRAGMENTS = 0

// Compare what the run found with what the two member tables above record. Returns the problems, so
// the caller can exit non-zero: a list is only kept honest if something reads it back.
//
// Every comparison here is a comparison of members, in both directions, and every message names the
// member: "entered" says who joined a class and what the two sides say, "left" says who is no longer
// in it. Nowhere does a message say "records 6, the run found 7" -- that was the failure the author's
// ruling of 2026-11-03 13:30 retired, because the number left the reader to bisect for a name.
function exampleProblems(coverage) {
    const problems = []
    const stopped = coverage.programs - coverage.compiled
    const counted = coverage.unlabelled.length + [...coverage.members.values()].reduce(
        (sum, one) => sum + one.size,
        0,
    )
    if (stopped !== counted) {
        problems.push(
            `${stopped} example(s) do not compile, and ${counted} of them were counted: an example that is neither ` +
                'compiled nor counted is the skip this check exists to refuse',
        )
    }
    for (const one of coverage.unlabelled) {
        problems.push(
            `an example that does not compile with no class: ${one.member} '${one.name}': ${one.message}`,
        )
    }
    const labels = new Set([...coverage.members.keys(), ...Object.keys(EXAMPLE_STOPS)])
    for (const label of [...labels].sort()) {
        const ran = coverage.members.get(label) || new Map()
        const kept = EXAMPLE_STOPS[label]
        if (kept === undefined) {
            problems.push(
                `the example class '${label}' is not in EXAMPLE_STOPS: ${ran.size} example(s) stop there, and a ` +
                    'new class is a new promise to keep -- add it with the members named below, each with the ' +
                    'reason it stops',
            )
        }
        for (const member of [...ran.keys()].sort()) {
            if (kept !== undefined && Object.hasOwn(kept, member)) continue
            const one = ran.get(member)
            problems.push(
                `entered: '${label}' + ${member} -- ${one.message.trim().slice(0, 200)}`,
            )
        }
        for (const member of Object.keys(kept || {}).sort()) {
            if (ran.has(member)) continue
            problems.push(
                `left: '${label}' - ${member} -- it does not stop here any more, so take it off the table and ` +
                    'say in the commit what made it compile (or where it moved)',
            )
        }
    }
    if (coverage.stale.length !== STALE_FRAGMENTS) {
        problems.push(
            `STALE_FRAGMENTS records ${STALE_FRAGMENTS}, and ${coverage.stale.length} example(s) a marker calls ` +
                'not compiled compile with nothing bound: update the number in the same commit and say why, or ' +
                'drop the marker',
        )
    }
    // The markers that are right have to say what is missing: a reason that does not name a name the
    // example is compiled against is the same silence in the other direction.
    for (const one of coverage.thin) {
        problems.push(
            `the marker of ${one.member} '${one.name}' does not say what the example is missing: it ` +
                `compiles once ${one.unnamed.map((name) => `\`${name}\``).join(', ')} is bound, and the reason ` +
                `gives ${one.entry.fragments.map((reason) => `\`${reason}\``).join(' ')}`,
        )
    }
    return problems
}

// Compare what the run found with what EXCEPTIONS records, member by member and both ways round.
// Returns the problems, so the caller can exit non-zero: a list is only kept honest if something
// reads it back, and a member is only a record if the run says which one moved.
function exceptionProblems(found) {
    const problems = []
    for (const half of ['result', 'parameter']) {
        const { byLabel, unlabelled } = found[half]
        const recorded = EXCEPTIONS[half]
        for (const one of unlabelled) {
            problems.push(`an exception with no label: ${one.member} -- ${one.detail}`)
        }
        const labels = new Set([...Object.keys(byLabel), ...Object.keys(recorded)])
        for (const label of [...labels].sort()) {
            const ran = byLabel[label] || new Map()
            const kept = recorded[label]
            if (kept === undefined) {
                problems.push(
                    `the exception class '${label}' is not in EXCEPTIONS.${half}: ${ran.size} member(s) carry it, ` +
                        'and a new class is a new promise to keep -- add it with the members named below, each ' +
                        'with the sentence that says why it cannot be read',
                )
            }
            for (const member of [...ran.keys()].sort()) {
                if (kept !== undefined && Object.hasOwn(kept, member)) continue
                const one = ran.get(member)
                problems.push(`entered: '${label}' + ${member} -- ${one.detail}`)
            }
            for (const member of Object.keys(kept || {}).sort()) {
                if (ran.has(member)) continue
                problems.push(
                    `left: '${label}' - ${member} -- it is not in the run any more, so take it off the table ` +
                        'and say in the commit what made it readable (or which class it moved to)',
                )
            }
        }
    }
    return problems
}

// The class a result the checker cannot read belongs to, or null when it belongs to none of them.
// The rule's side is read first: it is the operator that decides what it gives back; when it reads,
// the declaration that does not is the side the class names.
function resultStopClass(claimedType, claimed, promised) {
    if ((claimedType || '').trim() === '') return 'rule states no result'
    if (claimed === null || claimed.opaque) {
        const shape = classifyOpaque(claimedType)
        return shape === null ? null : `rule states ${shape}`
    }
    for (const one of promised) {
        if (one.declared !== null && !one.declared.opaque) continue
        const shape = classifyOpaque(one.declaredText)
        return shape === null ? null : `declaration states ${shape}`
    }
    return null
}

// What a piece of text that is not a type this checker reads looks like, as a phrase: 'a function
// type', 'a tuple', 'a record', 'a gradient-of result', 'a generic application', 'a type variable'
// or 'a named type'. A type variable is a single capital letter (`T`, `A`), which is how the
// declarations and the registered signatures spell it; a longer capitalised word is a named type
// (`Tensor`, `NumericAggregate`, `DeviceAggregate`). null when the text has no shape this checker
// has a class for -- that exception is unlabelled, and the run refuses it.
function classifyOpaque(text) {
    const source = (text || '').trim()
    // A `match type` result is a function on the argument's shape, not a function type: the `=>`
    // inside it separated one case's pattern from its result. Read as a function type, every
    // signature written with a type function was an exception instead of a comparison.
    if (/^match\s+type\b/.test(source)) return 'a match-type result'
    if (source.includes('=>')) return 'a function type'
    if (source.startsWith('(')) return 'a tuple'
    if (source.startsWith('{')) return 'a record'
    if (/\bof\b/.test(source)) return 'a gradient-of result'
    if (/^[A-Z]\w*</.test(source)) return 'a generic application'
    if (/^[A-Z](\[.*\])?$/.test(source)) return 'a type variable'
    if (/^[A-Z]\w*/.test(source)) return 'a named type'
    return null
}

// Whether the two sides write the same type. A named type (`Json`, `Graph`, `Map<K, V>`) is opaque
// to this checker, so the texts are all it has; the same text on both sides is the comparison.
function sameTypeText(declared, operator) {
    const mine = canonicalResult(declared)
    const theirs = canonicalResult(operator)
    return mine !== null && theirs !== null && mine.text === theirs.text
}

// How one position stands: 'fit' when the operator takes what the declaration promises, 'mismatch'
// when both sides are read and it does not, 'unreadable' when one side is a type this checker has
// no reading for and the two do not even spell it the same way.
function paramVerdict(declared, operator) {
    const accepted = operatorAccepts(operator)
    if (accepted !== undefined && accepted.any === true) return 'fit'
    if (typeof declared === 'string' && sameTypeText(declared, operator)) return 'fit'
    const needs = declaredNeeds(declared)
    if (accepted === undefined || needs === null) return 'unreadable'
    return needs.every((need) => accepted.kinds.some((kind) => kindCovers(kind, need))) ? 'fit' : 'mismatch'
}

// The classes a parameter that is not a type this checker reads can belong to, by the shape
// classifyOpaque names. The same shape is the same class on either side: a function-typed parameter
// is one whether the operator or the declaration writes it.
const PARAMETER_SHAPES = new Map([
    ['a match-type result', 'match-type parameter'],
    ['a function type', 'function-typed parameter'],
    ['a tuple', 'tuple-typed parameter'],
    ['a record', 'record-typed parameter'],
    ['a gradient-of result', 'grad of P'],
    ['a generic application', 'named-type parameter'],
    ['a type variable', 'bare type variable parameter'],
    ['a named type', 'named-type parameter'],
])

// The class a parameter the checker cannot compare belongs to, or null when it belongs to none.
// `declared` and `operator` are the two texts at the position that stopped it, `stop` says which
// part of the parameter list was unreadable: 'no-list' (no parameter list at all), 'pack' (a pack
// written without a type), 'arity' or 'position'.
function parameterStopClass(stop, declared, operator) {
    if (stop === 'no-list') return 'rule states no parameter list'
    if (stop === 'pack') return 'untyped pack ..rest'
    if (stop === 'arity') return 'declared arity differs from rule arity'
    const shape = classifyOpaque(operator) || classifyOpaque(declared)
    return shape === null ? null : PARAMETER_SHAPES.get(shape) || null
}

// What an operator's parameter text accepts, as `{ kinds: [...] }` or `{ any: true }`, or undefined
// when the text is neither a kind, a union of them, one scalar type nor a tensor.
function operatorAccepts(text) {
    const source = (text || '').trim()
    const direct = KIND_SETS.get(source)
    if (direct !== undefined) return { kinds: direct }
    const parts = splitTopLevel(source, '|')
    if (parts !== null && parts.length > 1) {
        const kinds = []
        for (const part of parts) {
            const one = operatorAccepts(part)
            if (one === undefined) return undefined
            if (one.any === true) return { any: true }
            for (const kind of one.kinds) if (!kinds.includes(kind)) kinds.push(kind)
        }
        return { kinds }
    }
    if (TYPE_VARIABLE.test(source)) return { any: true }
    const at = dimensionIndex(source)
    if (at >= 0) {
        const base = source.slice(0, at).trim()
        if (TYPE_VARIABLE.test(base) || RESULT_SCALARS.get(base) !== undefined) {
            // `[]` is a rank-one array and every other suffix a tensor, the same reading the
            // declaration side takes.
            return { kinds: [source.slice(at).trim() === '[]' ? 'array' : 'tensor'] }
        }
        return undefined
    }
    const scalar = RESULT_SCALARS.get(source)
    if (scalar !== undefined) return { kinds: [scalar] }
    return undefined
}

// What a declaration requires of one position, as the set it names: 'tensor', 'array', 'int',
// 'i32', 'f32', 'float', 'bf16', 'f16', 'c64', 'c128', 'bool', 'string', 'Bytes' or 'number'. null
// when the text is not a type this checker reads -- a bare type variable (`F`), a module's own type
// (`OptimizerState`, `Device<f32[...]>`), a function type (`(a: float) => float`) or a tuple.
function declaredNeed(text) {
    const source = (text || '').trim()
    if (source === '') return null
    // A function type, a tuple or a record: nothing a parameter kind names.
    if (source.startsWith('(') || source.startsWith('{')) return null
    if (source === 'Tensor') return 'tensor'
    const at = dimensionIndex(source)
    if (at < 0) {
        if (source === 'number') return 'number'
        const known = RESULT_SCALARS.get(source)
        if (known === undefined) return null
        // `void` and `never` are not values a parameter takes.
        if (known === 'void' || known === 'never') return null
        return known
    }
    // The bracket suffix decides the arrangement: `[]` is the rank-one array a kind writes as
    // `int[]` (the element type a declaration names there is narrower than what the kind says, so
    // it is not compared), and every other suffix -- `[...]`, `[?]`, `[n]`, `[n, m]` -- is a
    // tensor, including a type variable applied to dimensions.
    if (source.slice(at).trim() === '[]') return 'array'
    return dimensionsOf(source.slice(at).trim()) === null ? null : 'tensor'
}

// Every set a declared parameter names, or null when one of them is a type this checker does not
// read. A union names several alternatives and every one of them has to be taken.
function declaredNeeds(text) {
    const parts = splitTopLevel((text || '').trim(), '|')
    if (parts === null) return null
    const needs = []
    for (const part of parts) {
        const one = part.trim()
        if (one === '') return null
        const need = declaredNeed(one)
        if (need === null) return null
        if (!needs.includes(need)) needs.push(need)
    }
    return needs.length === 0 ? null : needs
}

// The parameter types a registered signature states, in order, or null when its text cannot be read
// as a list of `name: type` positions.
function signatureParams(signature) {
    let text = (signature || '').trim()
    // A signature may open with its const type parameters (`<count: int> (x: T) => T`, or
    // `::<T>(bytes: Bytes, shape: int[]) => T[...]`); they are not positions of the call, and the
    // declaration states them as `::<count: int>`.
    if (text.startsWith('::')) text = text.slice(2).trim()
    if (text.startsWith('<')) {
        const close = partnerBracket(text, 0)
        if (close < 0) return null
        text = text.slice(close + 1).trim()
    }
    if (!text.startsWith('(')) return null
    const close = partnerBracket(text, 0)
    if (close < 0) return null
    const inner = text.slice(1, close).trim()
    if (inner === '') return []
    const parts = splitTopLevel(inner)
    if (parts === null) return null
    const types = []
    for (const part of parts) {
        const named = /^(?:\.{2,3})?(\w+)\s*\??\s*:\s*(.+)$/.exec(part.trim())
        if (named !== null) {
            types.push(named[2].trim())
            continue
        }
        // A position with no name is just its type (`(c128) => string`); a pack with no type
        // (`..rest`) has none to compare, and the overload is listed instead.
        const bare = part.trim()
        if (bare === '' || /^\.{2,3}/.test(bare)) return null
        types.push(bare)
    }
    return types
}

function verifyOps() {
    const registered = registeredOperators()
    const problems = []
    // The overloads whose result type the two sides state in a form this checker cannot compare:
    // a bounded list, printed by the caller, never a silent skip.
    const unchecked = []
    let checked = 0
    // The overloads whose result type the two sides state in a form this comparison reads and
    // finds equal. A run that compared no result type at all promises nothing, so it is refused
    // the way a run that reached no operator is.
    let resultsCompared = 0
    // The same two counts for the parameters: what the operator says it takes, position by
    // position, against what the declaration says, and the overloads whose parameter types are
    // stated in a form neither side can be read from.
    let paramsCompared = 0
    const paramsUnchecked = []
    for (const module of MODULES.filter((m) => m.kind === 'cpp' || m.native)) {
        const registeredName = module.registered || module.name
        const file = path.relative(REPO_ROOT, sourceOf(module)).replace(/\\/g, '/')
        // Every operator the module registers and a declaration may name. A type-qualified alias
        // (`Tensor.shape`) is the same operator under a type namespace, so it is not a name a
        // declaration must write: it is checked below, against the plain name.
        const allOps = registered.filter(
            (op) => op.module === registeredName && !op.name.startsWith('__') && (!module.only || module.only.includes(op.name)),
        )
        const ops = allOps.filter((op) => !op.name.includes('::'))
        if (allOps.length === 0) {
            // A module whose only native kernels are private exports no operator to verify.
            if (!parseDeclarations(module).some((e) => !e.value && e.native)) continue
            problems.push(`${file}: the module '${registeredName}' registers no operator (is it imported by ${path.relative(REPO_ROOT, PROBE)}?)`)
            continue
        }
        // Values are not operators: inspect --ops does not list them. Only the body-less
        // declarations are verified, whichever language the module is written in: a function
        // with a body is a Camel function, not an operator.
        const entries = parseDeclarations(module).filter(
            (e) => !e.value && e.native,
        )
        // The same module's private operators: a kernel an `#impl` function runs is declared
        // body-less in the module's own file and registered by the compiler, so `inspect --ops`
        // lists it marked private and it is checked here like any other declaration. Leaving it
        // out was the gap: a private kernel's signature and purity went unread.
        const kernels = parseKernels(module)
        // A module whose only operators are private kernels is verified too: `allOps` above holds
        // them, so it is not skipped for registering nothing.
        const declaredNames = new Set(entries.filter((e) => e.file === file).map((e) => e.name))
        // A module may publish an operator under a type namespace too (`Tensor.shape`). That
        // name is C++'s, because a declaration cannot write it, and the compiler points it at
        // the declaration when the declaration replaces the module's entity
        // (`Module::replaceTypeQualifiedAliases`). A declared name's alias must be that
        // declaration, and its parameter counts are the proof: a declaration answers with the
        // counts it was written with, a stale C++ group answers -2 ("does not tell").
        const aliases = allOps.filter((op) => op.name.includes('::'))
        for (const alias of aliases) {
            const leaf = alias.name.split('::').pop()
            if (!declaredNames.has(leaf)) {
                // A computed operator's alias is C++'s own business, like the operator: its URI
                // must be a registered one (`inspect --ops` printed it, so it is).
                continue
            }
            const sibling = ops.find((op) => op.name === leaf && op.uri === alias.uri)
            if (!sibling) {
                problems.push(
                    `${file}: the type-qualified alias '${alias.name}' (${alias.uri}) has no plain '${leaf}' ` +
                        `operator with the same URI`,
                )
                continue
            }
            if (!(alias.with === sibling.with && alias.norm === sibling.norm && alias.norm >= 0)) {
                problems.push(
                    `${file}: the type-qualified alias '${alias.name}' does not resolve to the declaration of ` +
                        `'${leaf}' (${alias.with} with-parameter(s) and ${alias.norm} parameter(s); the declaration ` +
                        `has ${sibling.with}/${sibling.norm}): C++ still exports the old group under the alias, so ` +
                        `one operator has two signatures`,
                )
            }
        }
        // The module that declares its signatures one by one and publishes them under a type
        // namespace publishes every declared name there: `Tensor.shape` is not a second
        // signature, so it costs nothing to require it.
        if (module.declaration && module.registered !== '<builtin>' && aliases.length > 0) {
            for (const name of declaredNames) {
                if (!aliases.some((alias) => alias.name.split('::').pop() === name)) {
                    problems.push(
                        `${file}: the declared '${name}' has no type-qualified alias, but the module publishes ` +
                            `its other operators under one (${aliases[0].name.split('::').slice(0, -1).join('::')}.${name})`,
                    )
                }
            }
        }
        for (const op of ops) {
            // A private operator is answered by the module's private declarations: an exported
            // declaration that happened to have the same name is a different operator.
            const pool = op.private === true ? kernels : entries
            const sameName = pool.filter((e) => e.name === op.name)
            if (sameName.length === 0) {
                problems.push(
                    `${file}: '${op.name}' (${op.uri}) is registered${
                        op.private === true ? ' as a private kernel' : ''
                    } but not declared`,
                )
                continue
            }
            const matching = sameName.filter(
                (e) => uriMatches(e.uri, op.uri) && countsAgree(e.with.length ? countParams(e.with) : 0, op.with) && countsAgree(countParams(e.norm), op.norm),
            )
            if (matching.length === 0) {
                problems.push(
                    `${file}: no declaration of '${op.name}' matches the registered ${op.uri} with ${op.with} with-parameter(s) and ${op.norm} parameter(s): ${op.signature}`,
                )
                continue
            }
            checked++
            // A parameter's name is what a named argument binds to, so the declaration and the
            // registered rule have to give a position the same name: comparing the counts alone
            // left the two free to drift apart (`len`'s string rule recorded no name at all, and
            // the page and the compiler disagreed for as long as nothing read the names). The
            // comparison is per position, so a name that several declarations share type by type
            // is compared against every rule that answers to it.
            //
            // The parameter types are compared the way a kind is compared with a type, not the way
            // two types are. A declaration of one name differs from the registered rule in type
            // (that is what an overload is: `len` is declared for `string`, `T[...]`, `Map<K, V>`
            // and `Set<T>`, and one rule of `len` writes `Map<K, V> | Set<T>`), and the two sides
            // spell the types in different words (`i32` for `int`): comparing them as equal texts
            // reports one difference per overload and per alias, which is what it did when it was
            // tried (408 differences). What the two sides do say is comparable, though, because
            // the operator's side is a set of kinds (`Tensor` is any tensor) and the declaration's
            // is one type: the declaration may narrow a kind, but it may not promise a value the
            // operator does not take, because that is a reader sent to a call the operator refuses.
            // A kind this table does not hold, a parameter text that is not `name: type`, and a
            // declared type that is a bare variable or a module's own type are all "not compared":
            // counted and named, the same bounded list the result half has.
            const ruleParams = signatureParams(op.signature)
            const promisedParams = []
            for (const entry of matching) {
                const params = declaredParams(entry, ruleParams)
                const key = params.map((one) => one.type).join('\u0001')
                if (promisedParams.some((other) => other.key === key)) continue
                const verdicts =
                    ruleParams === null || params.length !== ruleParams.length
                        ? null
                        : params.map((one, i) => paramVerdict(one.type, ruleParams[i]))
                promisedParams.push({ entry, params, verdicts, key })
            }
            const fits = (one) => one.verdicts !== null && one.verdicts.every((verdict) => verdict === 'fit')
            const readableParams = (one) => one.verdicts !== null && one.verdicts.every((verdict) => verdict !== 'unreadable')
            if (promisedParams.length > 0 && promisedParams.some(fits)) {
                // One of the name's declarations stating what this operator takes is what the
                // promise is for this operator, the same rule the result half follows: a name
                // declared once per type it accepts has several declarations and several rules.
                paramsCompared++
            } else if (promisedParams.some(readableParams)) {
                // Both sides of these are read and no declaration fits: the first position one
                // promises more than the operator takes is the difference.
                for (const one of promisedParams.filter(readableParams)) {
                    const at = one.verdicts.findIndex((verdict) => verdict === 'mismatch')
                    problems.push(
                        `${one.entry.file}:${one.entry.line}: '${one.entry.name}' declares parameter ${at + 1} ` +
                            `\`${one.params[at].type}\`, and ${op.uri} takes \`${ruleParams[at]}\`: ${op.signature}`,
                    )
                }
            } else {
                // One side or the other is not readable as a type, so the overload is named with the
                // piece that stopped the comparison, and that piece classifies the stop.
                const first = promisedParams[0]
                let stop = 'no-list'
                let detail = `the registered signature \`${op.signature}\` is not a list of \`name: type\` positions`
                let at = -1
                if (ruleParams === null && /\([^)]*\.\./.test(op.signature)) {
                    stop = 'pack'
                    detail = `the registered signature \`${op.signature}\` writes a pack without a type`
                } else if (ruleParams !== null && promisedParams.some((one) => one.verdicts === null)) {
                    stop = 'arity'
                    detail = `the declaration states ${first.params.length} parameter(s) and the operator ${ruleParams.length}`
                } else if (ruleParams !== null) {
                    at = first.verdicts.findIndex((verdict) => verdict === 'unreadable')
                    stop = 'position'
                    detail =
                        operatorAccepts(ruleParams[at]) === undefined
                            ? `parameter ${at + 1}: the operator takes \`${ruleParams[at]}\`, which is not a type this checker reads`
                            : `parameter ${at + 1}: the declaration takes \`${first.params[at].type}\`, which is not a type this checker reads`
                }
                // The stop is classified from the two texts at the position that stopped it, and the
                // result half still runs: one half stopping says nothing about the other.
                paramsUnchecked.push({
                    name: op.name,
                    uri: op.uri,
                    member: `${op.name} (${op.uri})#parameter`,
                    stop,
                    label: parameterStopClass(stop, at >= 0 ? first.params[at].type : '', at >= 0 ? ruleParams[at] : ''),
                    detail,
                })
            }
            const ruleNames = registeredNames(op.signature)
            if (ruleNames !== null) {
                for (const entry of matching) {
                    const declared = namesOfDeclaration(entry)
                    if (declared === null) continue
                    for (const [side, what] of [['with', 'with'], ['norm', 'ordinary']]) {
                        const mine = declared[side]
                        const theirs = ruleNames[side]
                        if (mine.length !== theirs.length) continue
                        for (let i = 0; i < mine.length; i++) {
                            if (mine[i] === theirs[i]) continue
                            problems.push(
                                `${entry.file}:${entry.line}: '${entry.name}' names ${what} parameter ${i + 1} ` +
                                    `'${mine[i]}', and ${op.uri} registers '${theirs[i]}': ${op.signature}`,
                            )
                        }
                    }
                }
            }
            // The result type, the one part of the signature nothing compared: a declaration is
            // the reference's promise of what a call gives back, and the operator's own signature
            // is what it actually gives back. A promise the two do not share is reported here.
            //
            // A name may be declared several times for the types it accepts, and each of those
            // declarations is a promise about the rules it answers: one of them stating this
            // operator's result is the name's result for this rule, so a declaration is compared
            // only when none of them agrees.
            const claimedText = claimedResult(op.signature)
            // A `uses ...` or `when ...` clause after the result is about the operator, not about
            // its type, the same reading the declaration's side takes.
            const claimedType = withoutConditions(claimedText)
            const claimed = canonicalResult(claimedType)
            const promised = []
            for (const entry of matching) {
                const declaredText = withoutConditions(entry.result)
                const key = `${entry.name}\u0000${declaredText}`
                if (promised.some((one) => one.key === key)) continue
                promised.push({ entry, declaredText, declared: canonicalResult(entry.result), key })
            }
            // A result written as a type function is compared by its rules, not by its text: the
            // declaration's `WithElement<A, bf16>` and the registered `match type A { ... }` are one
            // function, and two of them that differ differ at a case and a dimension this reader
            // names (design principles 1: a type function is an ordinary function, so its identity
            // is what it answers, not how it is spelled). Only a pair of two type functions takes
            // this path: every other pair keeps the comparison below, which is the one it had.
            const typeFunctionsCompared = promised
                .map((one) => ({ one, reading: matchTypeVerdict(one.declaredText, claimedType) }))
                .filter((each) => each.reading !== null)
            const functionAgrees = typeFunctionsCompared.some((each) => each.reading.same)
            const agreed =
                functionAgrees ||
                (claimed !== null && promised.some((one) => one.declared !== null && one.declared.text === claimed.text))
            // A claim the module writes as the element family (`float[...]`) does not say which
            // element type the result has, so a declaration that writes one of that family is not
            // a difference: the operator takes the element type from its argument and the
            // declaration is the one instance it supports. It is not agreement either -- the
            // element type is unchecked there -- so it is listed, with what the two sides say.
            const followsArgument =
                claimed !== null &&
                claimed.family &&
                promised.some((one) => one.declared !== null && FLOAT_ELEMENTS.has(one.declared.text.replace(/\[.*$/, '')))
            const readable =
                claimed !== null && !claimed.opaque && promised.every((one) => one.declared !== null && !one.declared.opaque)
            // A claim this checker cannot read but the declaration writes in the same words is the
            // same promise: the text is all either side has (`Json[]?` is opaque to the reader and
            // identical on both sides here).
            const spelledAlike =
                claimed === null &&
                promised.some((one) => one.declaredText.replace(/\s+/g, '') === claimedType.replace(/\s+/g, ''))
            if (agreed || spelledAlike) {
                resultsCompared++
            } else if (typeFunctionsCompared.length > 0) {
                // Both sides are type functions and no declaration answers what the operator does:
                // the reading names the case and the place inside it that differs, so the reader of
                // the failure learns which dimension to look at rather than that the texts differ.
                for (const { one, reading } of typeFunctionsCompared) {
                    problems.push(
                        `${one.entry.file}:${one.entry.line}: '${one.entry.name}' declares the result \`${one.declaredText}\`, ` +
                            `and ${op.uri} registers \`${claimedText.trim()}\`: ${reading.why}`,
                    )
                }
            } else if (readable && !followsArgument) {
                for (const one of promised) {
                    problems.push(
                        `${one.entry.file}:${one.entry.line}: '${one.entry.name}' declares the result \`${one.declaredText}\`, ` +
                            `and ${op.uri} registers \`${claimedText}\`: ${op.signature}`,
                    )
                }
            } else {
                const label = readable
                    ? 'declaration-names-an-element-the-rule-takes-from-its-argument'
                    : resultStopClass(claimedType, claimed, promised)
                for (const one of promised) {
                    unchecked.push({
                        name: one.entry.name,
                        uri: op.uri,
                        member: `${one.entry.name} (${op.uri})#result`,
                        declared: one.declaredText,
                        claimed: claimedText.trim(),
                        label,
                        detail: `declares \`${one.declaredText}\`, the operator says \`${claimedText.trim()}\``,
                    })
                }
            }
            // One operator is declared once per type it accepts (unions are retired), and the
            // declarations of a name share one description: the doc comment is read with the
            // first of them.
            const firstEntry = sameName[0]
            for (const entry of matching) {
                if (entry !== firstEntry) {
                    continue
                }
                const declaredPure = entry.purity === 'pure'
                // A declaration that writes neither tag states nothing about purity: an
                // operator's purity is registered in C++ and a declaration need not repeat it
                // (design principles 6.4, 10.4), so there is nothing to compare. A tag that *is*
                // written is a claim about the operator, and it has to agree with it.
                if (entry.purity === null) {
                    continue
                }
                if (declaredPure !== (op.purity === 'pure')) {
                    problems.push(
                        `${entry.file}:${entry.line}: '${entry.name}' is ${declaredPure ? '@pure' : '@effect'} but ${op.uri} is registered ${op.purity}`,
                    )
                }
            }
        }
        for (const kernel of kernels) {
            const hit = allOps.some((op) => op.private === true && op.name === kernel.name && uriMatches(kernel.uri, op.uri))
            if (!hit) {
                problems.push(
                    `${kernel.file}:${kernel.line}: the private kernel '${kernel.name}' (${kernel.uri}) is declared but no such operator is registered`,
                )
            }
        }
        for (const entry of entries) {
            const hit = allOps.some((op) => op.name === entry.name && uriMatches(entry.uri, op.uri))
            if (!hit) {
                problems.push(`${entry.file}:${entry.line}: '${entry.name}' (${entry.uri}) is declared but no such operator is registered`)
            }
            if (entries.find((each) => each.name === entry.name) !== entry) {
                continue
            }
            if (entry.doc.length === 0) problems.push(`${entry.file}:${entry.line}: '${entry.name}' has no description`)
            if (entry.examples.length === 0) problems.push(`${entry.file}:${entry.line}: '${entry.name}' has no @example`)
        }
    }
    // A signature file without a module of the reference would never be checked.
    for (const file of fs.readdirSync(SIGNATURE_DIR)) {
        const name = file.replace(/\.cml$/, '')
        if (!MODULES.some((m) => m.kind === 'cpp' && m.name === name)) {
            problems.push(`stdlib/signatures/${file}: not a module of the reference (see MODULES in scripts/stdlib-reference.mjs)`)
        }
    }
    for (const module of MODULES.filter((m) => m.kind === 'camel')) {
        let previous = null
        for (const entry of parseDeclarations(module)) {
            // An overload written right after another one shares its comment.
            if (entry.doc.length === 0 && previous !== entry.name) {
                problems.push(`stdlib/${module.name}.cml:${entry.line}: '${entry.name}' has no description`)
            }
            previous = entry.name
        }
    }
    // The exceptions, classed the way EXCEPTIONS records them: one member per place this checker
    // cannot read, keyed `<operator> (<uri>)#result` or `...#parameter`, with the ones that carry no
    // class kept apart so the run can refuse them by name.
    const tally = (list) => {
        const byLabel = {}
        const unlabelled = []
        for (const one of list) {
            if (typeof one.label !== 'string' || one.label === '') {
                unlabelled.push(one)
                continue
            }
            if (byLabel[one.label] === undefined) byLabel[one.label] = new Map()
            if (!byLabel[one.label].has(one.member)) byLabel[one.label].set(one.member, one)
        }
        return { byLabel, unlabelled, total: list.length }
    }
    return {
        problems,
        checked,
        unchecked,
        resultsCompared,
        paramsCompared,
        paramsUnchecked,
        found: { result: tally(unchecked), parameter: tally(paramsUnchecked) },
    }
}

// ---- the probes that hold the type-function reader -------------------------------------------
//
// `--verify-ops` compares two texts a person never sees side by side: a declaration, and the
// signature a registry prints for the operator it declares. A reader for a shape of those texts can
// be wrong in two directions, and a run over the library is silent about both: it can call two
// promises different because it read one of them wrong, and it can call them the same because it
// read neither. So the shape is measured on its own, against declarations written for the
// measurement. `test/cases/modules/stdlib/match_type_probes.decl` is read by the same reader the
// library's declarations are read by, each probe says which answer it must get, and a probe that
// expects a difference says which place the reading has to name -- a verdict that is right about
// "different" and silent about the dimension is half an answer.
function verifyMatchType(declarationFile, probeFile) {
    const shownPath = (file) => path.relative(REPO_ROOT, file).replace(/\\/g, '/')
    const declared = readDeclarations(declarationFile, { name: 'probe', kind: 'cpp' })
    const probes = JSON.parse(fs.readFileSync(probeFile, 'utf8'))
    if (!Array.isArray(probes) || probes.length === 0) {
        console.log(`${shownPath(probeFile)}: no probe, so nothing about the type-function reader is measured`)
        process.exit(1)
    }
    const problems = []
    let asExpected = 0
    for (const [index, probe] of probes.entries()) {
        const entry = declared.find((one) => one.name === probe.declaration)
        if (entry === undefined) {
            problems.push(
                `probe ${index + 1}: the declaration '${probe.declaration}' is not in ${shownPath(declarationFile)}`,
            )
            continue
        }
        const reading = matchTypeVerdict(entry.result, probe.operator)
        const verdict = reading === null ? 'unreadable' : reading.same ? 'match' : 'mismatch'
        const why =
            reading === null
                ? `the declaration's result \`${entry.result}\` and the operator's \`${probe.operator}\` are not both a type function this checker reads`
                : reading.why
        console.log(`MATCH-TYPE ${index + 1} ${verdict}: ${probe.declaration} -- ${why}`)
        if (verdict !== probe.expect) {
            problems.push(
                `probe ${index + 1} (${probe.declaration}): the reader answered ${verdict}, and the probe says ${probe.expect}`,
            )
            continue
        }
        const missing = (probe.says || []).filter((piece) => !why.includes(piece))
        if (missing.length > 0) {
            problems.push(
                `probe ${index + 1} (${probe.declaration}): the reading does not name ${missing
                    .map((one) => `\`${one}\``)
                    .join(', ')}`,
            )
            continue
        }
        asExpected++
    }
    for (const problem of problems) console.log(problem)
    if (problems.length > 0) {
        console.log(`${problems.length} probe(s) were read differently from what the probe says`)
        process.exit(1)
    }
    console.log(`the type-function reader answers as the probes say: ${asExpected} probe(s), ${asExpected} as expected`)
}

// ---- main -----------------------------------------------------------------------------------

const USAGE =
    'usage: node scripts/stdlib-reference.mjs --write | --verify-doc | --verify-ops | --verify-docs | --audit | --verify-match-type <declarations> <probes> [--compile] [--camel <path>]'

// `--camel <path>` names the install a mode that needs a compiler measures; it is taken out of the
// arguments here so the mode stays the first word that is not a flag.
const argv = process.argv.slice(2)
const flags = []
for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--camel') {
        if (i + 1 >= argv.length) {
            console.log('--camel needs the path of the install to measure')
            console.log(USAGE)
            process.exit(2)
        }
        camelPath = path.resolve(argv[++i])
    } else if (argv[i].startsWith('--camel=')) {
        camelPath = path.resolve(argv[i].slice('--camel='.length))
    } else {
        flags.push(argv[i])
    }
}

const mode = flags[0] || '--verify-doc'
const bodyProblems = signatureBodyProblems()
if (bodyProblems.length > 0) {
    for (const problem of bodyProblems) console.log(problem)
    console.log(`${bodyProblems.length} declaration(s) in the signature files carry a body; a declaration has no body`)
    process.exit(1)
}
if (mode === '--write' || mode === '--verify-doc') {
    announcePending()
    const text = renderDoc()
    const rendered = (text.match(/^#### /gm) || []).length
    if (rendered === 0) {
        console.log('the reference would have no entry: MODULES and the declarations it reads produced nothing')
        process.exit(1)
    }
    if (mode === '--write') {
        fs.writeFileSync(DOC_PATH, text)
        console.log(`wrote docs/stdlib-reference.md (${(text.match(/^#### /gm) || []).length} entries)`)
    } else {
        const current = fs.existsSync(DOC_PATH) ? fs.readFileSync(DOC_PATH, 'utf8').replace(/\r\n/g, '\n') : ''
        if (current !== text) {
            console.log('docs/stdlib-reference.md is out of date: run `node scripts/stdlib-reference.mjs --write`')
            process.exit(1)
        }
        console.log('docs/stdlib-reference.md is up to date')
    }
} else if (mode === '--audit') {
    // What the module table and the library on disk say to each other, and what each module's
    // documentation has and has not -- the report the task asks for, from the same parser the
    // reference is generated with. `--compile` also compiles every example (needs a built compiler).
    //
    // The table is the account of what a program can import and what the page documents, so the
    // audit checks it against the library instead of trusting it: every entry has to name a file
    // that is there -- a module whose name has drifted from its file is exactly how a table rots,
    // and it is the one failure that costs nothing to catch -- and every library file no entry
    // reads is named in a line, so an omission is on the screen rather than a gap nobody sees.
    //
    // The second half reports and does not fail. Not every file in `stdlib/` is a module a program
    // imports: `std.cml` declares the core's passes, `rulesets/transpose.cml` is a table a
    // transform reads, `prelude.cml` is the source the built-in page is read from. A rule that
    // every file is an entry would have to list those exceptions anyway, and a list nobody checks
    // is not a check.
    //
    // `pending` is a column and not an absence: a module the page leaves out is still a row, with
    // the reason it is out beside its counts, so no row can be read as "documented" while the
    // module is out of the page.
    const compile = flags.includes('--compile')
    const problems = []
    const rows = []
    let totals = { names: 0, entries: 0, noDoc: 0, noExample: 0, examples: 0, fragments: 0, compileBroken: 0 }
    const shown = (file) => path.relative(REPO_ROOT, file).replace(/\\/g, '/')
    for (const module of MODULES) {
        const pending = pendingReason(module)
        const files = [sourceOf(module)]
        if (module.computed) files.push(path.join(STDLIB_DIR, module.computed))
        const absent = files.filter((file) => !fs.existsSync(file))
        for (const file of absent) {
            problems.push(`${shown(file)}: what the module '${module.name}' reads is not in the library`)
        }
        // An entry that reads nothing has no row: its counts would be zeros, and a row of zeros
        // reads as "nothing missing". The mismatch above is what says it is broken.
        if (absent.length > 0) continue
        const entries = parseDeclarations(module)
        // The declarations of one name are one entry of the reference: the first is the one whose
        // comment the page prints, and the counts are per name for that reason.
        const names = [...new Set(entries.map((entry) => entry.name))]
        const firsts = names.map((name) => entries.find((entry) => entry.name === name))
        const noDoc = firsts.filter((entry) => entry.doc.filter((line) => line.trim() !== '').length === 0).length
        const noExample = firsts.filter((entry) => entry.examples.length === 0).length
        const examples = firsts.reduce((sum, entry) => sum + entry.examples.length, 0)
        const fragments = firsts.filter((entry) => entry.fragments.length > 0).length
        for (const problem of docProblems(entries)) console.log(`DOC ${problem}`)
        // A pending module's examples cannot compile -- that is what pending says -- so compiling
        // them would report each one as broken and read as a defect of the module rather than as
        // the state it is in.
        let compileBroken = null
        if (compile && pending === null) {
            const compiled = await compileExamples(module, entries)
            const broken = compiled.results.filter((result) => !result.ok)
            compileBroken = broken.length
            for (const result of broken) {
                console.log(`BROKEN ${result.file}:${result.line}: ${result.name}: ${result.message}`)
            }
        }
        rows.push({ module: module.name, pending, names: names.length, entries: entries.length, noDoc, noExample, examples, fragments, compileBroken })
        totals.names += names.length
        totals.entries += entries.length
        totals.noDoc += noDoc
        totals.noExample += noExample
        totals.examples += examples
        totals.fragments += fragments
        totals.compileBroken += compileBroken === null ? 0 : compileBroken
    }
    // The other direction: the files of the library that no module of the table reads.
    const readBy = new Set()
    for (const module of MODULES) {
        readBy.add(path.resolve(sourceOf(module)))
        if (module.computed) readBy.add(path.resolve(path.join(STDLIB_DIR, module.computed)))
    }
    const unread = []
    for (const dir of [STDLIB_DIR, path.join(STDLIB_DIR, 'rulesets')]) {
        if (!fs.existsSync(dir)) continue
        for (const file of fs.readdirSync(dir)) {
            if (!file.endsWith('.cml')) continue
            const full = path.resolve(path.join(dir, file))
            if (!readBy.has(full)) unread.push(shown(full))
        }
    }
    // A module the page leaves out is announced with its reason in every mode, and this one is a
    // mode too: a `pending` column says *that* it is out, and only this line says why.
    announcePending()
    console.log(
        'auditing: the module table against the library on disk (every entry reads a file that is ' +
            'there) and, for each module, what its declarations have and have not',
    )
    for (const problem of problems) console.log(`MISMATCH ${problem}`)
    if (unread.length > 0) {
        console.log(`files of the library no module of the table reads: ${unread.join(', ')}`)
    }
    const column = (row) =>
        `${row.module.padEnd(14)} ${String(row.pending === null ? '-' : 'yes').padStart(7)} ` +
        `${String(row.names).padStart(5)} ${String(row.entries).padStart(7)} ${String(row.noDoc).padStart(5)} ` +
        `${String(row.noExample).padStart(9)} ${String(row.examples).padStart(8)} ${String(row.fragments).padStart(9)} ` +
        `${String(row.compileBroken === null ? (row.pending === null ? '-' : 'n/a') : row.compileBroken).padStart(6)}`
    console.log('module         pending names entries noDoc noExample examples fragments broken')
    for (const row of rows) console.log(column(row))
    console.log(column({ module: 'total', pending: null, ...totals }))
    if (problems.length > 0) {
        console.log(`${problems.length} entr(ies) of the module table do not match the library`)
        process.exit(1)
    }
    console.log(`the module table matches the library: ${MODULES.length} entry(ies), ${unread.length} file(s) it does not read`)
} else if (mode === '--verify-docs') {
    announcePending()
    const { problems, checked, fragments, coverage } = await verifyDocs()
    for (const problem of problems) console.log(problem)
    // Every example of every module, counted: the ones that compile, the ones that do not (by the
    // kind of diagnostic that stopped them), and the ones a marker calls "not compiled" that compile
    // anyway. A name is never lost between those lists; the last line of the run says which counts
    // the script records, and exampleProblems refuses a run whose numbers moved.
    const stopped = coverage.programs - coverage.compiled
    const announceExamples = (list, what, detail) => {
        if (list.length === 0) return
        const names = list.map((one) => one.name).filter((name, i, all) => all.indexOf(name) === i)
        const shown = names.slice(0, EXCEPTIONS_SHOWN)
        const more = names.length > shown.length ? `, and ${names.length - shown.length} more` : ''
        console.log(`the ${what}: ${shown.join(', ')}${more}`)
        for (const one of list.slice(0, EXCEPTIONS_SHOWN)) {
            console.log(`    ${one.file}:${one.line}: '${one.name}': ${detail(one)}`)
        }
    }
    console.log(
        `the examples: ${coverage.written} written in the modules, ${coverage.programs} a program of their own, ` +
            `${coverage.compiled} compile, ${stopped} do not, and ${coverage.stale.length} marked a fragment compile ` +
            `with nothing bound`,
    )
    for (const label of [...coverage.members.keys()].sort()) {
        console.log(`    ${label}: ${coverage.members.get(label).size}`)
    }
    // Every example that does not compile, by name: the class it stops on and the compiler's own
    // words, cut to the part that says what is wrong.
    announceExamples(
        coverage.stopped,
        'examples that do not compile',
        (one) => `${one.label === null ? 'no class' : one.label}: ${one.message.slice(0, 120)}`,
    )
    announceExamples(
        coverage.stale,
        'examples a marker calls not compiled that compile with nothing bound',
        () => 'the example compiles with nothing bound, so it is a program of its own and the marker is stale',
    )
    // The markers that are right: the example compiles only once the wrapper binds the names the
    // page's program would have to supply, and each of those names is what the reason has to give.
    announceExamples(
        coverage.bound,
        'examples a marker calls not compiled that compile only with a name bound',
        (one) => `binds ${one.bindings.join(', ')}`,
    )
    const mismatched = exampleProblems(coverage)
    for (const problem of mismatched) console.log(problem)
    if (problems.length > 0) {
        console.log(`${problems.length} documentation problem(s) in ${checked} compiled example(s)`)
        process.exit(1)
    }
    if (mismatched.length > 0) {
        // `mismatched` is one line per problem, and a problem is what a member did: it entered a
        // class EXAMPLE_STOPS has no room for, it entered a class that exists, it left one, it
        // stopped somewhere with no class at all, or a marker went stale. Each of those lines names
        // the example, so this number is only how many of them stand above it -- the diagnosis is the
        // names, not the total.
        console.log(
            `${mismatched.length} example problem(s) between the run and the script's records: an example ` +
                'that entered, one that left, or a marker that is stale -- each is named above',
        )
        process.exit(1)
    }
    console.log(
        `every declared name has a description and an @example: ${checked} example(s) checked, ` +
            `${fragments} entry(ies) marked a fragment were compiled too`,
    )
    console.log(
        `the example counts are the ones the script records: ${stopped} that do not compile in ` +
            `${Object.keys(EXAMPLE_STOPS).length} class(es), ${STALE_FRAGMENTS} marked a fragment that compile alone, ` +
            `${coverage.bound.length} that compile only with the name they say they need`,
    )
} else if (mode === '--verify-match-type') {
    // The type-function reader on its own, against declarations written for the measurement: the
    // one mode that reads neither the library's declarations nor a compiler, so a reader that
    // stopped reading the shape fails here and not only inside a run over 569 overloads.
    if (flags[1] === undefined || flags[2] === undefined) {
        console.log('--verify-match-type needs the file of declarations and the file of probes')
        console.log(USAGE)
        process.exit(2)
    }
    const resolve = (one) => (path.isAbsolute(one) ? one : path.join(REPO_ROOT, one))
    verifyMatchType(resolve(flags[1]), resolve(flags[2]))
} else if (mode === '--verify-ops') {
    const { problems, checked, unchecked, resultsCompared, paramsCompared, paramsUnchecked, found } = verifyOps()
    for (const problem of problems) console.log(problem)
    // The overloads whose types could not be read on one side or the other: a bounded exception,
    // named, classified and never a silent skip. A name that leaves a list (its type became
    // readable) and a name that joins it both show in the run's output, and a failure adds what the
    // two sides said.
    const announce = (what, why, list, classes, detail) => {
        if (list.length === 0) return
        const names = list.map((one) => one.name).filter((name, i, all) => all.indexOf(name) === i)
        // One list length, so how many names are shown and how many are counted as left over cannot
        // drift apart.
        const shown = names.slice(0, EXCEPTIONS_SHOWN)
        const more = names.length > shown.length ? `, and ${names.length - shown.length} more` : ''
        console.log(`the declared ${what} was not compared for ${names.length} operator(s) -- ${why}: ${shown.join(', ')}${more}`)
        // The classes, as the members EXCEPTIONS records them, then the same bounded detail as the
        // names, so a reader of a green run still knows which piece is not read and why.
        for (const label of Object.keys(classes.byLabel).sort()) {
            console.log(`    ${label}: ${classes.byLabel[label].size}`)
        }
        for (const one of list.slice(0, EXCEPTIONS_SHOWN)) console.log(`    ${detail(one)}`)
    }
    announce(
        'result type',
        'a type variable, a named type or a sentence on one side, or an element type the operator takes from its argument',
        unchecked,
        found.result,
        (one) => `${one.name} (${one.uri}): ${one.detail}`,
    )
    announce(
        'parameter types',
        'a parameter that is a function type, a module\'s own type, a nameless pack or a bare type variable on one side',
        paramsUnchecked,
        found.parameter,
        (one) => `${one.name} (${one.uri}): ${one.detail}`,
    )
    // The list is data: the run gathers its own exceptions by class and compares the members with the
    // members EXCEPTIONS holds, both ways round. A stop with no class fails, a member the table does
    // not hold fails with its name, and a member the run no longer finds fails with its name too, so
    // the list cannot grow or shrink without someone writing down which place moved.
    const mismatched = exceptionProblems(found)
    for (const problem of mismatched) console.log(problem)
    if (problems.length > 0) {
        console.log(`${problems.length} difference(s) between the declarations and the registered operators`)
        process.exit(1)
    }
    if (mismatched.length > 0) {
        // One line per problem, and a problem is what a member did: it entered a class the table has
        // no room for, it entered a class that exists, it left one, or its example stopped carrying a
        // class at all. Each of those lines names the member, so this number is only how many of them
        // stand above it -- the diagnosis is the names, not the total.
        console.log(
            `${mismatched.length} exception problem(s) between the run and EXCEPTIONS: a member that entered, ` +
                'one that left, or one that carries no class -- each is named above',
        )
        process.exit(1)
    }
    // What the run compared its own lists with, so a reader of a green run sees the size of the table
    // it agreed with as well as the classes it printed above.
    const recorded = (half) => {
        const members = Object.values(EXCEPTIONS[half]).map((kept) => Object.keys(kept).length)
        return `${members.reduce((sum, one) => sum + one, 0)} in ${members.length} class(es)`
    }
    console.log(
        `the exception lists are the ones EXCEPTIONS records: result ${recorded('result')}, parameter ${recorded('parameter')}`,
    )
    // A run that reached no operator compared nothing: the probe imported nothing the reference
    // lists, or MODULES is empty. It has to say that, not report agreement.
    if (checked === 0) {
        console.log('verified 0 registered operator overloads: the declarations were compared with nothing')
        process.exit(1)
    }
    // The same for each half of the signature that compares types: a run that read no result type
    // or no parameter type at all has compared no promise and must not report agreement either.
    if (resultsCompared === 0 || paramsCompared === 0) {
        console.log(
            `compared the declared result type of ${resultsCompared} and the declared parameter types of ${paramsCompared} ` +
                'of the registered operators: no signature states types this checker can read',
        )
        process.exit(1)
    }
    console.log(`the declarations agree with the ${checked} registered operator overloads`)
    const left = (list) =>
        list.length > 0 ? `; ${list.length} overload(s) with types this checker cannot read are listed above` : ''
    console.log(`the declared result type agrees with the operator's own for ${resultsCompared} of them${left(unchecked)}`)
    console.log(`the declared parameter types fit what the operator takes for ${paramsCompared} of them${left(paramsUnchecked)}`)
} else {
    console.log(USAGE)
    process.exit(2)
}


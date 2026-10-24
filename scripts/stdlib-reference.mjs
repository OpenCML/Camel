// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The standard library reference, read from the signature files and the library sources.
//
//   node scripts/stdlib-reference.mjs --write       regenerate docs/stdlib-reference.md
//   node scripts/stdlib-reference.mjs --verify-doc  docs/stdlib-reference.md is what --write would produce
//   node scripts/stdlib-reference.mjs --verify-ops  the signature files list exactly the operators the
//                                                   modules register (needs a built compiler)
//   node scripts/stdlib-reference.mjs --verify-docs every name has a description and an @example, and
//                                                   every example compiles (needs a built compiler)
//   node scripts/stdlib-reference.mjs --audit [--compile]
//                                                   the gap of each module as a table; --compile also
//                                                   compiles (needs a built compiler)
//
// A run that renders no entry or compares no registered operator is refused (exit 1): agreement
// over nothing is not a pass.
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
//   #op<'protocol:operator'>                       (only when the URI is not `<module>:<name>`;
//   with <a: A>                                     `*` stands for any text, for a group of
//   export func name(x: X): R                       overloads that differ by type)
//
// A declaration never carries a body. The old form wrote the URI in one — `func name(...): R {
// return '<uri>' }` — and it is gone (handover H107): a `func` with a body is an ordinary function,
// and --verify-ops refuses a signature file whose entry still has one.
//
// A value a module exports (`math::pi`; design principles 10.1) needs neither @pure nor a URI
// (it is not an operator). A declaration module cannot write it — a `let` needs an initializer —
// so the value's entry is recorded in a comment, and the C++ module remains the one source of
// the value itself:
//
//   /// What it is.
//   /// @example <code>    // <result>
//   // value name: T
//
// (A signature file that is not compiled may write the value itself as `export let name: T = ...`.)
//
// A library module written in Camel (stdlib/<module>.cml) is its own signature file: its
// `export func` declarations and their `///` comments are read the same way.
//
// When a declaration cannot express an operator — its result type comes from a type rule, or
// from a type argument — that operator stays in the C++ resolver and keeps its entry in
// stdlib/signatures/<module>.cml, for reference only (json: `encode`, `decode_as`; handover
// H115). The module's `computed` field names that file; the reference reads the declaration
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

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const SIGNATURE_DIR = path.join(REPO_ROOT, 'stdlib', 'signatures')
const STDLIB_DIR = path.join(REPO_ROOT, 'stdlib')
const DOC_PATH = path.join(REPO_ROOT, 'docs', 'stdlib-reference.md')
const PROBE = path.join(REPO_ROOT, 'test', 'cases', 'modules', 'stdlib', 'registry_all.cml')

// The modules of the reference, in the order of the document. `source` is the file read:
// a signature file for a C++ module, the library itself for a module written in Camel.
const MODULES = [
    { name: 'globals', title: 'Built-in functions', registered: '<builtin>', kind: 'cpp' },
    {
        name: 'str',
        title: '`str`: strings',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (H19).
        source: 'str.cml',
        declaration: true,
    },
    {
        name: 'bytes',
        title: '`bytes`: immutable binary values and UTF-8 conversion',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (H19).
        source: 'bytes.cml',
        declaration: true,
    },
    { name: 'ckpt', title: '`ckpt`: parameter trees and checkpoints', kind: 'cpp' },
    {
        name: 'tree',
        title: '`tree`: parameter trees written in Camel',
        kind: 'camel',
        // The walks are written with `match type`, `static for` and rest parameters, so a
        // function whose result shape follows its argument has no result type a declaration can
        // spell yet: it is rendered as `?` (an inferred result), not as `void`.
    },
    {
        name: 'tensor',
        title: '`tensor`: multi-dimensional numeric arrays',
        kind: 'cpp',
        // The fixed signatures are declared; the ones whose element type or shape follows an
        // argument stay in C++ and are recorded as computed (handover H151).
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
        // (the sorts, argsort, flatten, unique), and the page says so. Those declarations are
        // checked against the registered operators like a C++ module's.
        native: true,
    },
    {
        name: 'math',
        title: '`math`: numbers and bits',
        kind: 'cpp',
        // The declarations only: the three names whose result type C++ computed from their
        // argument (sqrt; the mixed-integer shifts) are written out per spelling (handover H202).
        source: 'math.cml',
        declaration: true,
    },
    {
        name: 'statistics',
        title: '`statistics`',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (H19).
        source: 'statistics.cml',
        declaration: true,
    },
    {
        name: 'random',
        title: '`random`',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (H19). The
        // three operators whose result type comes from their array argument are generic
        // declarations too (handover H202).
        source: 'random.cml',
        declaration: true,
    },
    {
        name: 'time',
        title: '`time`',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (H19).
        source: 'time.cml',
        declaration: true,
    },
    {
        name: 'fs',
        title: '`fs`: files',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (H19).
        source: 'fs.cml',
        declaration: true,
    },
    {
        name: 'io',
        title: '`io`: terminal input',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (H19).
        source: 'io.cml',
        declaration: true,
    },
    {
        name: 'process',
        title: '`process`: the process and the machine',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (H19;
        // H163). `exit` is not a group of this module and is not declared here.
        source: 'process.cml',
        declaration: true,
    },
    {
        name: 'env',
        title: '`env`: environment',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (H19;
        // H163).
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
        // (handover H115).
        source: 'json.cml',
        declaration: true,
        computed: 'signatures/json.cml',
    },
    {
        name: 'hash',
        title: '`hash`',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (H19;
        // H163). `sha256` is two declarations with two `#op` URIs.
        source: 'hash.cml',
        declaration: true,
    },
    {
        name: 're',
        title: '`re`: regular expressions',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (H19).
        source: 're.cml',
        declaration: true,
    },
    {
        name: 'para',
        title: '`para`: unordered iteration',
        kind: 'cpp',
        // The declarations of `rendezvous` and of the two names that take the element type of
        // their array argument (handover H202).
        source: 'para.cml',
        declaration: true,
    },
    {
        name: 'this',
        title: '`this`',
        kind: 'cpp',
        // The declaration, not a signature file: it is what the module compiles (H19, H146).
        source: 'this.cml',
        declaration: true,
    },
    {
        name: 'graph',
        title: '`graph`: a program as a value',
        kind: 'cpp',
        // The declarations, and, in the signature file, the reference entries of the four
        // operators whose type C++ computes (a function value's type). `constant` and
        // `add_constant` take the type of their sample and are written out per sample type
        // (handover H180, H202).
        source: 'graph.cml',
        declaration: true,
        computed: 'signatures/graph.cml',
    },
    {
        name: 'rewrite',
        title: '`rewrite`: rules applied to a program',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (handover
        // H183). The three reserved groups the `rule`/`analysis` declarations compile to have no
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
        // signature file named by `computed:` (handover H156).
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
        // numeric types" and cannot write an optional parameter (handover H166).
        source: 'pyplot.cml',
        declaration: true,
    },
    {
        name: 'python',
        title: '`python`: the embedded interpreter',
        kind: 'cpp',
        // Six fixed signatures are declared in stdlib/python.cml; `py_call` (two URIs),
        // `py_print`/`py_println` are variadic and `py_wrap`/`py_unwrap` move a type through the
        // module type `PyObject<T>`, so their reference entries live in the signature file named
        // by `computed:` (handover H166).
        source: 'python.cml',
        declaration: true,
        computed: 'signatures/python.cml',
    },
    {
        name: 'nn',
        title: '`nn`: neural-network primitives',
        kind: 'cpp',
        // `save_params` is declared in stdlib/nn.cml; the eighteen operators whose signature C++
        // computes from the tensors they receive (the convolution, pooling, batch-normalization,
        // embedding and loss operators, and the three optimizers) keep their reference entries in
        // the signature file named by `computed:` (handover H165).
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
        // named by `computed:` (handover H165).
        source: 'autodiff.cml',
        declaration: true,
        computed: 'signatures/autodiff.cml',
    },
    {
        name: 'sim',
        title: '`sim`: Monte Carlo simulation',
        kind: 'cpp',
        // Both operators have fixed signatures and are declared in stdlib/sim.cml; nothing is
        // computed, so the module has no signature file (handover H179).
        source: 'sim.cml',
        declaration: true,
    },
    {
        name: 'onnx',
        title: '`onnx`: export a program as an ONNX model',
        kind: 'cpp',
        // `supported_operators` is declared in stdlib/onnx.cml; `export_model` takes a function
        // of any parameter list and an example that must be its parameter tree, so its reference
        // entry is in the signature file named by `computed:` (handover H179).
        source: 'onnx.cml',
        declaration: true,
        computed: 'signatures/onnx.cml',
    },
]

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
// tuple, `args?: (...PyObject) | PyObject[]` (`py_call`'s keyword URI), accepts any number of
// arguments too, and the C++ resolver reports its count as -1 (handover H166).
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
// page would still agree with the registered operators (handover H127).
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

function readDeclarations(file, module) {
    const lines = fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n').split('\n')
    const relative = path.relative(REPO_ROOT, file).replace(/\\/g, '/')
    const keyword = 'export func'
    const entries = []
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
        // (a `DynamicFuncTypeResolver` stays the one source of its type; handover H113). The
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
        // A C++ module's value has no declaration to write: the declaration file records its
        // reference entry in a comment, and the C++ module stays the one source of the value
        // (math's pi, e, inf and nan; handover H113). It carries the same doc comment as an
        // operator, above the `// value` line.
        const docValue = module.kind === 'cpp' && trimmed.match(/^\/\/ value (\w+)\s*:\s*(.+)$/)
        if (docValue) {
            const entry = {
                module: module.name,
                name: docValue[1],
                section,
                line: i + 1,
                file: relative,
                value: true,
                with: [],
                norm: [],
                result: docValue[2],
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
            pendingUri = null
            continue
        }
        const valueMatch = module.kind === 'cpp' && trimmed.match(/^export let (\w+)\s*:\s*([^=]+?)\s*=/)
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
        // is that token: src/parse/ast/builder.cpp, docOf). A `#op<'module:function'>` names the
        // operator a C++ declaration implements when it does not follow the convention (design
        // principles 6.4; handover H19, step 1). This used to end the comment for a module written
        // in Camel, which lost the description of every such declaration from the page.
        if (trimmed.startsWith('#')) {
            const tagged = trimmed.match(/^#\[?op<['"]([^'"]+)['"]>/)
            if (tagged) {
                pendingUri = tagged[1]
            } else if (module.kind === 'cpp') {
                // A signature file writes `#op<...>` and nothing else: another tag there is a typo,
                // not a declaration.
                throw new Error(`${path.basename(file)}:${i + 1}: unknown tag on a declaration`)
            }
            continue
        }
    // A declaration of `operator ==` names a reserved operator group (`__eq__`), which has no
    // Camel name to list: `inspect --ops` hides those names, `--verify-ops` filters them out, and
    // the page introduces the operators of a type in the module's text. Reading the declaration as
    // an entry called `operator ==` would ask --verify-ops for an operator of that name, which
    // does not exist (handover H141).
    if (/^export\s+func\s+operator\b/.test(trimmed)) {
        doc = []
        withText = ''
        pendingUri = null
        continue
    }
    if (trimmed.startsWith(keyword + ' ')) {
            const rest = trimmed.slice(keyword.length + 1)
            const name = rest.slice(0, rest.indexOf('(')).replace(/::<.*>$/, '')
            const args = bracketed(rest, rest.indexOf('('))
            const after = rest.slice(args.end + 1)
            // A pass declaration (design principles 9.3): a body-less function whose condition
            // is the pass applicability. A pass is a function the pipeline applies to the
            // program, not an operator the module registers; it is documented in docs/cli.md,
            // and its parameters are the declaration's `with` list.
            if (!declarationBody(after, lines, i) &&
                /\bwhen\s+is_function::<\w+>\(\)\s*$/.test(after)) {
                doc = []
                withText = ''
                pendingUri = null
                continue
            }
            const result = after.replace(/^\s*:\s*/, '').replace(/\s*(sync\s*)?\{\s*$/, '').trim()
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
                native: module.kind === 'camel' && !declarationBody(after, lines, i),
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
            if (module.kind === 'cpp' || entry.native) {
                // `module:function`, or what the `#op` tag above says.
                entry.uri = pendingUri || `${module.name}:${name}`
            }
            entries.push(entry)
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
    out.push('  `T!` and the program handles it with `!!`, `?->` or `match`. A result that may not exist is')
    out.push('  `T?`, handled with `??`. An entry without a failure line does not fail.')
    out.push("- **Named arguments**: a call may give a parameter by name (`f(x: 1)`, `len(x: xs)`) when")
    out.push('  the name gives that position the same name in every form the callee has, whether the callee is')
    out.push('  a Camel declaration or a group of built-in rules; the type of the argument then decides which')
    out.push("  form runs (`len`'s one parameter is `x` for a string, an array, a map and a set, so one name")
    out.push('  reaches all four). The name is written in the list that holds the parameter: an ordinary one')
    out.push("  in the parentheses (`len(x: xs)`), a `with` one in the angle brackets (`j->get<key: 'port'>`).")
    out.push('  A form that gives a position no name, or leaves its count to the arguments (a rest list --')
    out.push("  `py_call`'s two forms differ in arity as well), is one no named argument reaches: the call")
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
    const parsed = MODULES.map((module) => ({ module, entries: parseDeclarations(module) }))
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
        // (handover H151). Ordering by the declarations rather than by the first entry lets a
        // computed file keep the module's order even when only a few of its sections hold a
        // computed name (handover H180). Within a section, overloads written one after another
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
                } else {
                    out.push(`- With effects: ${entry.effect || 'not pure'}.`)
                }
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

const STATEMENT_STARTS = /^(let|var|if|for|while|return|sync|match|throw)\b/
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

function declaredParams(entry) {
    return entry.norm
        .map((text) => {
            const match = text.match(/^(\w+)\s*\??\s*:\s*(.+)$/)
            return match ? { text, name: match[1], type: match[2].trim() } : null
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
    'throw', 'sync', 'with', 'type', 'struct', 'enum', 'rule', 'export', 'module', 'import', 'as', 'is',
])

function bindArguments(entry, code) {
    const params = declaredParams(entry)
    const call = findCall(code, entry.name)
    if (!call) return []
    const bindings = []
    const bind = (name, param) => {
        if (!param || !NAME.test(name) || RESERVED.has(name)) return
        // A parameter typed by a type variable cannot be written in the wrapper: its type is not a
        // type of the program (`T[]`, `Map<K, V>`, `(...args: A) => T`). The example has to
        // supply a concrete value instead; the compile below reports the name as unresolved.
        if (/\b[A-Z]\b/.test(param.type)) return
        if (!bindings.some((binding) => binding.name === name)) bindings.push({ name, type: param.type })
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
    const fallible = /^throw\b/.test(code.trim())
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

function runCamelCompile(file, cwd) {
    return new Promise((resolve) => {
        const env = { ...process.env }
        env.CAMEL_HOME = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
        const child = spawn(camelExe(), [file, 'std::gir'], { cwd, env, stdio: ['ignore', 'pipe', 'pipe'] })
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
            resolve({ ok: false, message: (line || output.trim().split(/\r?\n/)[0] || `exit ${code}`).slice(0, 240) })
        })
    })
}

async function compileExample(job) {
    const { module, entries, entry, code } = job
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-doc-example-'))
    try {
        const file = path.join(dir, 'main.cml')
        fs.writeFileSync(file, await exampleSource(module, entries, entry, code))
        const result = await runCamelCompile(file, dir)
        if (!result.ok && !STATEMENT_STARTS.test(code)) {
            fs.writeFileSync(file, await exampleSource(module, entries, entry, code, true))
            const asStatement = await runCamelCompile(file, dir)
            if (asStatement.ok) return { ...job, ok: true }
        }
        return { ...job, ok: result.ok, message: result.message }
    } finally {
        fs.rmSync(dir, { recursive: true, force: true })
    }
}

// Compiles every example of a module, at most JOBS at a time; one temp directory per example. An
// entry that marks its example a fragment is not compiled: the marker is the record of that, and
// docProblems requires the reason.
async function compileExamples(module, entries) {
    const jobs = []
    const fragments = []
    const seen = new Set()
    for (const entry of entries) {
        if (seen.has(entry.name)) continue
        seen.add(entry.name)
        if (entry.fragments.length > 0) {
            fragments.push(entry)
            continue
        }
        for (const example of entry.examples) {
            jobs.push({ module, entries, entry, code: exampleCode(example), file: entry.file, line: entry.line, name: entry.name })
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
    return { results: results.filter(Boolean), fragments }
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

// The check the plan runs: the doc-comment rule above, plus every example compiled. It needs a
// built compiler, like --verify-ops.
async function verifyDocs() {
    const problems = []
    let checked = 0
    let fragments = 0
    for (const module of MODULES) {
        const entries = parseDeclarations(module)
        problems.push(...docProblems(entries))
        const compiled = await compileExamples(module, entries)
        fragments += compiled.fragments.length
        for (const result of compiled.results) {
            checked++
            if (!result.ok) {
                problems.push(
                    `${result.file}:${result.line}: '${result.name}' has an @example that does not compile: ` +
                        `\`${result.code}\`: ${result.message}`,
                )
            }
        }
    }
    return { problems, checked, fragments }
}

// The Camel a doc example compiles as: the entry inside `main`, with the module's names in
// scope. An example that starts with a statement keyword is used as a statement; anything else
// is an expression and becomes `let __doc_example = <expression>`. A call that returns nothing
// cannot be bound, so an expression whose compile fails is retried as a statement before the
// failure is reported (a void call is a valid example of a writer such as save_params).
// ---- checking against the registered operators --------------------------------------------

function camelExe() {
    if (process.env.CAMEL_EXE) return process.env.CAMEL_EXE
    return path.join(REPO_ROOT, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
}

function registeredOperators() {
    const exe = camelExe()
    const run = spawnSync(exe, ['inspect', '--ops', PROBE], { encoding: 'utf8', maxBuffer: 1 << 26 })
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
// same positions -- the counts alone agree while the names drift (the language author's ruling,
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

function verifyOps() {
    const registered = registeredOperators()
    const problems = []
    let checked = 0
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
            problems.push(`${file}: the module '${registeredName}' registers no operator (is it imported by ${path.relative(REPO_ROOT, PROBE)}?)`)
            continue
        }
        // Values are not operators: inspect --ops does not list them. A module written in Camel
        // is verified only for its native declarations: its Camel functions are bodies, not
        // operators.
        const entries = parseDeclarations(module).filter(
            (e) => !e.value && (module.kind === 'cpp' || e.native),
        )
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
        // signature, so it costs nothing to require it (handover H151).
        if (module.declaration && aliases.length > 0) {
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
            const sameName = entries.filter((e) => e.name === op.name)
            if (sameName.length === 0) {
                problems.push(`${file}: '${op.name}' (${op.uri}) is registered but not declared`)
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
            // The types are not compared the same way. A declaration of one name may differ from
            // the registered rule in type (that is what an overload is: `len` is declared for
            // `string`, `T[..]`, `Map<K, V>` and `Set<T>`, and one rule of `len` writes
            // `Map<K, V> | Set<T>`), and the two sides spell the same type in different words
            // (`i32` for `int`, `(a: float, b: float) => float` for `(...) => NumericAggregate`,
            // `P` for `grad of P`): comparing them item by item reports one difference per
            // overload and per alias, which is what it did when it was tried (408 differences,
            // still 15 when only names with one declaration and one rule were compared).
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
            // One operator is declared once per type it accepts (unions are retired), and the
            // declarations of a name share one description: the doc comment is read with the
            // first of them.
            const firstEntry = sameName[0]
            for (const entry of matching) {
                if (entry !== firstEntry) {
                    continue
                }
                const declaredPure = entry.purity === 'pure'
                if (entry.purity === null) {
                    problems.push(`${entry.file}:${entry.line}: '${entry.name}' says neither @pure nor @effect`)
                } else if (declaredPure !== (op.purity === 'pure')) {
                    problems.push(
                        `${entry.file}:${entry.line}: '${entry.name}' is ${declaredPure ? '@pure' : '@effect'} but ${op.uri} is registered ${op.purity}`,
                    )
                }
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
    return { problems, checked }
}

// ---- main -----------------------------------------------------------------------------------

const mode = process.argv[2] || '--verify-doc'
const bodyProblems = signatureBodyProblems()
if (bodyProblems.length > 0) {
    for (const problem of bodyProblems) console.log(problem)
    console.log(`${bodyProblems.length} declaration(s) in the signature files carry a body; a declaration has no body`)
    process.exit(1)
}
if (mode === '--write' || mode === '--verify-doc') {
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
    // What each module's documentation has, and what it does not: the table the task H189 asks
    // for, from the same parser the reference is generated with. `--compile` also compiles every
    // example (needs a built compiler).
    const compile = process.argv.includes('--compile')
    const rows = []
    let totals = { names: 0, entries: 0, noDoc: 0, noExample: 0, examples: 0, fragments: 0, compileBroken: 0 }
    for (const module of MODULES) {
        const entries = parseDeclarations(module)
        const firsts = names.map((name) => entries.find((entry) => entry.name === name))
        const noDoc = firsts.filter((entry) => entry.doc.filter((line) => line.trim() !== '').length === 0).length
        const noExample = firsts.filter((entry) => entry.examples.length === 0).length
        const examples = firsts.reduce((sum, entry) => sum + entry.examples.length, 0)
        const fragments = firsts.filter((entry) => entry.fragments.length > 0).length
        for (const problem of docProblems(entries)) console.log(`DOC ${problem}`)
        let compileBroken = 0
        if (compile) {
            const compiled = await compileExamples(module, entries)
            const broken = compiled.results.filter((result) => !result.ok)
            compileBroken = broken.length
            for (const result of broken) {
                console.log(`BROKEN ${result.file}:${result.line}: ${result.name}: ${result.message}`)
            }
        }
        rows.push({ module: module.name, names: names.length, entries: entries.length, noDoc, noExample, examples, fragments, compileBroken })
        totals.names += names.length
        totals.entries += entries.length
        totals.noDoc += noDoc
        totals.noExample += noExample
        totals.examples += examples
        totals.fragments += fragments
        totals.compileBroken += compileBroken
    }
    console.log('module         names entries noDoc noExample examples fragments broken')
    for (const row of rows) {
        console.log(
            `${row.module.padEnd(14)} ${String(row.names).padStart(5)} ${String(row.entries).padStart(7)} ` +
                `${String(row.noDoc).padStart(5)} ${String(row.noExample).padStart(9)} ` +
                `${String(row.examples).padStart(8)} ${String(row.fragments).padStart(9)} ${String(row.compileBroken).padStart(6)}`,
        )
    }
    console.log(
        `${'total'.padEnd(14)} ${String(totals.names).padStart(5)} ${String(totals.entries).padStart(7)} ` +
            `${String(totals.noDoc).padStart(5)} ${String(totals.noExample).padStart(9)} ` +
            `${String(totals.examples).padStart(8)} ${String(totals.fragments).padStart(9)} ${String(totals.compileBroken).padStart(6)}`,
    )
} else if (mode === '--verify-docs') {
    const { problems, checked, fragments } = await verifyDocs()
    for (const problem of problems) console.log(problem)
    if (problems.length > 0) {
        console.log(`${problems.length} documentation problem(s) in ${checked} compiled example(s)`)
        process.exit(1)
    }
    console.log(
        `every declared name has a description and an @example: ${checked} example(s) compile, ` +
            `${fragments} marked fragment(s) not compiled`,
    )
} else if (mode === '--verify-ops') {
    const { problems, checked } = verifyOps()
    for (const problem of problems) console.log(problem)
    if (problems.length > 0) {
        console.log(`${problems.length} difference(s) between the declarations and the registered operators`)
        process.exit(1)
    }
    // A run that reached no operator compared nothing: the probe imported nothing the reference
    // lists, or MODULES is empty. It has to say that, not report agreement.
    if (checked === 0) {
        console.log('verified 0 registered operator overloads: the declarations were compared with nothing')
        process.exit(1)
    }
    console.log(`the declarations agree with the ${checked} registered operator overloads`)
} else {
    console.log('usage: node scripts/stdlib-reference.mjs --write | --verify-doc | --verify-ops | --verify-docs | --audit [--compile]')
    process.exit(2)
}


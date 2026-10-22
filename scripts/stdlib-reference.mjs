// The standard library reference, read from the signature files and the library sources.
//
//   node scripts/stdlib-reference.mjs --write       regenerate docs/stdlib-reference.md
//   node scripts/stdlib-reference.mjs --verify-doc  docs/stdlib-reference.md is what --write would produce
//   node scripts/stdlib-reference.mjs --verify-ops  the signature files list exactly the operators the
//                                                   modules register (needs a built compiler)
//
// A run that renders no entry or compares no registered operator is refused (exit 1): agreement
// over nothing is not a pass.
//
// stdlib/signatures/<module>.cml declares the operators a C++ module exports (globals.cml: the
// built-in ones), one body-less `export func` each, with a doc comment:
//
//   /// What it does, in one or more lines.
//   /// @pure                      or  @effect <what the effect is>
//   /// @fails <fault or error and when>          (optional; several conditions separated by `;`)
//   /// @example <code>    // <result>
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
import path from 'path'
import { spawnSync } from 'child_process'
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
        name: 'tensor',
        title: '`tensor`: multi-dimensional numeric arrays',
        kind: 'cpp',
        // The fixed signatures are declared; the ones whose element type or shape follows an
        // argument stay in C++ and are recorded as computed (handover H151).
        source: 'tensor.cml',
        declaration: true,
        computed: 'signatures/tensor.cml',
    },
    { name: 'array', title: '`array`: arrays and higher-order functions', kind: 'camel' },
    {
        name: 'math',
        title: '`math`: numbers and bits',
        kind: 'cpp',
        // The declarations, and, in the signature file, the reference entries of the three names
        // whose result type C++ computes from their argument (sqrt; the mixed-integer shifts).
        // One convention for every declaration module: the computed half is a signature file
        // named by `computed:` (handover H180).
        source: 'math.cml',
        declaration: true,
        computed: 'signatures/math.cml',
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
        name: 'algo',
        title: '`algo`: sorting and array primitives',
        kind: 'cpp',
        // The declaration of `argsort`, and, in the signature file, the reference entries of the
        // seven names whose result type C++ computes from their array argument (handover H115,
        // H128, H135).
        source: 'algo.cml',
        declaration: true,
        computed: 'signatures/algo.cml',
    },
    {
        name: 'random',
        title: '`random`',
        kind: 'cpp',
        // The declarations, not a signature file: they are what the module compiles (H19). The
        // three operators whose result type comes from their array argument stay in the C++
        // resolver; the signature file keeps only their reference entries (handover H115, H128).
        source: 'random.cml',
        declaration: true,
        computed: 'signatures/random.cml',
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
    { name: 'process', title: '`process`: the process and the machine', kind: 'cpp' },
    { name: 'env', title: '`env`: environment', kind: 'cpp' },
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
    { name: 'hash', title: '`hash`', kind: 'cpp' },
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
        // The declaration of `rendezvous`, and, in the signature file, the reference entries of
        // the two names whose signature C++ computes from their array argument (handover H115,
        // H128, H146).
        source: 'para.cml',
        declaration: true,
        computed: 'signatures/para.cml',
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
        // The declarations, and, in the signature file, the reference entries of the operators
        // whose type C++ computes (`constant` and `add_constant` take the type of their sample;
        // the four of a function value take the function's type). One convention for every
        // declaration module: the computed half is a signature file named by `computed:`
        // (handover H180).
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
            const decl = trimmed.match(/^(?:export\s+)?func\s+([A-Za-z_][\w:]*)\s*\(/)
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
                with: [],
                norm: splitParams(args.inner),
                result,
                doc: [],
                purity: null,
                effect: '',
                fails: [],
                examples: [],
                uri: `${module.name}:${dynamicMatch[1]}`,
            }
            for (const text of doc) {
                if (text.startsWith('@pure')) entry.purity = 'pure'
                else if (text.startsWith('@effect')) {
                    entry.purity = 'effect'
                    entry.effect = text.slice('@effect'.length).trim()
                } else if (text.startsWith('@fails')) entry.fails.push(text.slice('@fails'.length).trim())
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
                value: true,
                with: [],
                norm: [],
                result: docValue[2],
                doc: [],
                purity: 'value',
                effect: '',
                fails: [],
                examples: [],
                uri: null,
            }
            for (const text of doc) {
                if (text.startsWith('@example')) entry.examples.push(text.slice('@example'.length).trim())
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
                uri: null,
            }
            for (const text of doc) {
                if (text.startsWith('@example')) entry.examples.push(text.slice('@example'.length).trim())
                else if (!text.startsWith('@')) entry.doc.push(text)
            }
            entries.push(entry)
            doc = []
            withText = ''
            continue
        }
        // A C++ module names the operator of a declaration with `#op<'module:function'>` when it
    // does not follow the convention; the line belongs to the declaration, so it does not end
    // the pending comment (design principles 6.4; handover H19, step 1).
    if (module.kind === 'cpp' && trimmed.startsWith('#')) {
        const tagged = trimmed.match(/^#\[?op<['"]([^'"]+)['"]>/)
        if (!tagged) throw new Error(`${path.basename(file)}:${i + 1}: unknown tag on a declaration`)
        pendingUri = tagged[1]
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
            const name = rest.slice(0, rest.indexOf('('))
            const args = bracketed(rest, rest.indexOf('('))
            const after = rest.slice(args.end + 1)
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
                doc: [],
                purity: null,
                effect: '',
                fails: [],
                examples: [],
                uri: null,
            }
            for (const text of doc) {
                if (text.startsWith('@pure')) entry.purity = 'pure'
                else if (text.startsWith('@effect')) {
                    entry.purity = 'effect'
                    entry.effect = text.slice('@effect'.length).trim()
                } else if (text.startsWith('@fails')) entry.fails.push(text.slice('@fails'.length).trim())
                else if (text.startsWith('@example')) entry.examples.push(text.slice('@example'.length).trim())
                else entry.doc.push(text)
            }
            if (module.kind === 'cpp') {
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

// The comment at the top of the file, without the note that it is not compiled.
function parseHeader(module) {
    const text = fs.readFileSync(sourceOf(module), 'utf8').replace(/\r\n/g, '\n')
    const match = text.match(/^\/\*\*\n([\s\S]*?)\n \*\//)
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
    return `${entry.name}: ${withPart}(${entry.norm.join(', ')}) => ${entry.result || 'void'}`
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
    out.push('- Write one call that has with-arguments per statement when several appear in one argument')
    out.push('  list: `f<a>(x), g<b>(y)` is read as comparisons.')
    out.push('')
out.push('Not in this reference: the profiler (`perf`).')
    out.push('')
    out.push('| Module | Import | Written in | Functions |')
    out.push('|---|---|---|---|')
    const parsed = MODULES.map((module) => ({ module, entries: parseDeclarations(module) }))
    for (const { module, entries } of parsed) {
        const names = [...new Set(entries.map((e) => e.name))]
        const anchor = module.name === 'globals' ? 'built-in-functions' : module.name
        out.push(
            `| [${module.name === 'globals' ? 'built-in' : module.name}](#${anchor}) | ` +
                `${module.name === 'globals' ? 'none' : '`import { ... } from ' + module.name + '`'} | ` +
                `${module.kind === 'camel' ? 'Camel' : 'C++'} | ${names.length} |`,
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
                    out.push(result ? `- Example: \`${code}\` gives \`${result}\`` : `- Example: \`${code}\``)
                }
                out.push('')
            }
        }
    }
    return out.join('\n').replace(/\n{3,}/g, '\n\n').trimEnd() + '\n'
}

// ---- checking against the registered operators --------------------------------------------

function camelExe() {
    if (process.env.CAMEL_EXE) return process.env.CAMEL_EXE
    return path.join(REPO_ROOT, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
}

function registeredOperators() {
    const run = spawnSync(camelExe(), ['inspect', '--ops', PROBE], { encoding: 'utf8', maxBuffer: 1 << 26 })
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

function verifyOps() {
    const registered = registeredOperators()
    const problems = []
    let checked = 0
    for (const module of MODULES.filter((m) => m.kind === 'cpp')) {
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
        // Values are not operators: inspect --ops does not list them.
        const entries = parseDeclarations(module).filter((e) => !e.value)
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
    console.log('usage: node scripts/stdlib-reference.mjs --write | --verify-doc | --verify-ops')
    process.exit(2)
}
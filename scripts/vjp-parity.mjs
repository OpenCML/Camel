#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The two tables that decide whether a step can be differentiated, and the difference between
// them.
//
// A derivative law is written down twice in this tree, by two mechanisms that do not know about
// each other:
//
//   the engine's table   a C++ rule per operator URI, registered with
//                        `camel::core::DerivativeRegistry::instance().setRule(uri, fn)` --
//                        `src/builtin/operators/derivative.cpp` (the core's arithmetic,
//                        conversions, array writes and the tangent operators),
//                        `modules/tensor/ops/*.cpp` (`setVjp(defs, "<name>", &fn)`, which
//                        `modules/tensor/ops/registry.cpp` turns into `tensor:<name>`) and
//                        `modules/math/module.cpp`, `modules/autodiff/operators.cpp`.
//                        `modules/autodiff/pullback.cpp` asks it by URI (`findRule`) and refuses
//                        the node when the answer is nothing.
//
//   the library's table  one `export rule <name>` per law in `stdlib/rulesets/transpose.cml`
//                        (the reverse side) and `stdlib/rulesets/jvp.cml` (the forward side).
//                        The compiler asks it by *key*, not by URI:
//                        `src/core/forward.cpp#ruleKeyOf` reads the key off the operator --
//                        `:op/add_d` is `+` at two operands, `tensor:pick_at` is the call
//                        `pick_at` at two, a conversion is its destination's spelling at one --
//                        and `src/parse/ast/rule_head.cpp#lookupRules` answers the laws filed
//                        under it.
//
// Nothing compared the two, and the shape of the failure that produced is a half-account: a law
// the library answers and the engine refuses. `tensor:pick_at` was one -- the library filed
// `stdlib/rulesets/transpose.cml#pick_at_t` and `stdlib/rulesets/jvp.cml#jvp_idx_arr`, the
// engine's table had no row, so `vjp` answered `[0.0, 1.0, 0.0]` for a program `grad` refused.
// Each one is found by a person meeting it. This is the comparison instead.
//
// What is compared is one key per row, because that is the unit both sides can be stated in.
// Every registration is reduced to the key the compiler would ask for it (`ruleKeyOf`), every
// rule of the two rulesets is reduced to the key it files (`leftHeadOf`), and the two key sets
// are diffed:
//
//   both          a key on both sides: the step is answered either way. Counted, not listed.
//   library-only  the library answers the key and no engine registration reduces to it. This is
//                 the `pick_at` shape, and the table a reader acts on.
//   engine-only   an engine registration reduces to no key the library files. Legal (the
//                 tangent operators are machinery no program writes, a conversion to a discrete
//                 type has no tangent to carry) but not silently: each row here is a decision,
//                 and the decision is written in the exemption list below.
//
// Equality is not the property asked for, because the two tables are not meant to be equal: a
// law can belong to the library alone (乙: the step exists only in a transformed graph, or the
// engine's pass is the one that must answer it) and to the engine alone (丙: the operator is
// machinery). So the property is: **every difference is written down, with who owns it and why**
// -- `scripts/vjp-parity-exemptions.tsv`, one row per key, three fields, no wildcard. A
// difference that is not in that file is the debt this check exists to catch, and it is caught in
// both directions: a new law in `stdlib/rulesets/*.cml` with no `setVjp` behind it is a
// library-only row, and a new `setRule` with no law beside it is an engine-only row.
//
//   node scripts/vjp-parity.mjs                  check: the tree's differences are the list
//   node scripts/vjp-parity.mjs --tables         print the three tables and decide nothing
//   node scripts/vjp-parity.mjs --json           the same as --tables, as JSON
//   node scripts/vjp-parity.mjs --root DIR       read a tree other than this one (the self-test)
//   node scripts/vjp-parity.mjs --exempt FILE    read another exemption list (the self-test)
//
// Exit: 0 when every difference is exempt, 1 when one is not, 2 when the tree cannot be read
// (a missing file, a ruleset that files no rule at all -- a reader that stopped matching must
// fail rather than report a clean tree).

import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

const SELF = url.fileURLToPath(import.meta.url)
const REPO_ROOT = path.resolve(path.dirname(SELF), '..')

// ---- the key the compiler asks for (`camel/core/forward.cpp#ruleKeyOf`) -----------------------
//
// The mirror of the C++ side, and the reason it has to be a mirror rather than a guess: the key
// is what the lookup is filed under, so a difference between the two spellings is a difference
// between two questions. Everything below is read from the compiler and quoted, not invented.

// `kWrittenOperations`: the operations written as a token, and the token. An operator whose
// result has a tangent space is here; `%`, `==`, `<`, `&&` and `!` are absent because an integer
// or a boolean has no space to ask about.
const WRITTEN_OPERATIONS = [
    { name: 'add', head: '+', arity: 2 },
    { name: 'sub', head: '-', arity: 2 },
    { name: 'mul', head: '*', arity: 2 },
    { name: 'div', head: '/', arity: 2 },
    { name: 'neg', head: '-', arity: 1 },
    { name: 'pow', head: '^', arity: 2 },
]

// `kRuleKeyExceptions`: the operators whose key cannot be derived from the name, with the reason
// the compiler gives them. The guard below refuses to run if the compiler holds a row this script
// does not -- a rule key this script does not know how to read would make the whole comparison
// quietly miss a row.
//
// The six axis reductions are one shape of the question: their URI keeps an identity of the
// implementation (`tensor:sum_axis`), and the one spelling a program writes is the whole
// reduction's name with the axis and the flag in `<>` (`sum<axis, keepdims>(xs)`), so their laws
// are filed under `sum` at the one operand the parentheses carry (author's ruling, 2026-11-05).
const RULE_KEY_EXCEPTIONS = [
    { uri: ':op/idx_arr', head: 'index_into', arity: 2 },
    { uri: 'tensor:argmax_axis', head: 'argmax', arity: 1 },
    { uri: 'tensor:argmin_axis', head: 'argmin', arity: 1 },
    { uri: 'tensor:max_axis', head: 'max', arity: 1 },
    { uri: 'tensor:mean_axis', head: 'mean', arity: 1 },
    { uri: 'tensor:min_axis', head: 'min', arity: 1 },
    { uri: 'tensor:multiply', head: 'mul_tensor', arity: 2 },
    { uri: 'tensor:sum_axis', head: 'sum', arity: 1 },
]

// `typeCodeToString` (`src/core/type/base.cpp`) for the codes a conversion can target: the key of
// a conversion is its destination's spelling at one operand.
const TYPE_CODE_SPELLING = new Map([
    ['Int32', 'i32'],
    ['Int64', 'int'],
    ['Float32', 'f32'],
    ['Float64', 'float'],
    ['BFloat16', 'bf16'],
    ['Float16', 'f16'],
    ['String', 'string'],
    ['Bool', 'bool'],
    ['Complex64', 'c64'],
    ['Complex128', 'c128'],
])

// ---- options ---------------------------------------------------------------------------------

function option(name, fallback) {
    const at = process.argv.indexOf(name)
    if (at === -1) {
        return fallback
    }
    const value = process.argv[at + 1]
    if (value === undefined || value.startsWith('--')) {
        return { error: `${name} needs a value` }
    }
    return value
}

const rootOption = option('--root', REPO_ROOT)
if (rootOption && rootOption.error) {
    console.log(`FAIL ${rootOption.error}`)
    process.exit(2)
}
const ROOT = path.resolve(rootOption)
const TABLES = process.argv.includes('--tables')
const JSON_MODE = process.argv.includes('--json')
const DEFAULT_EXEMPTIONS = path.join(ROOT, 'scripts', 'vjp-parity-exemptions.tsv')
const exemptionsOption = option('--exempt', DEFAULT_EXEMPTIONS)
if (exemptionsOption && exemptionsOption.error) {
    console.log(`FAIL ${exemptionsOption.error}`)
    process.exit(2)
}
const EXEMPTIONS = path.resolve(exemptionsOption)

function fail(message) {
    console.log(`FAIL ${message}`)
    process.exit(2)
}

function read(relative) {
    const file = path.join(ROOT, relative)
    try {
        return fs.readFileSync(file, 'utf8')
    } catch {
        return fail(`${relative} is not in the tree: the comparison reads both tables from it`)
    }
}

// Comments carry no semantics, and both the rulesets and the C++ are heavily commented -- a
// commented-out `setVjp` or a rule quoted inside a `///` block would otherwise count as a
// registration. The comment text is blanked rather than removed so every offset, and therefore
// every line number, is the one a reader can open.
function withoutComments(text, { slashes = true } = {}) {
    let out = text.replace(/\/\*[\s\S]*?\*\//g, (block) => block.replace(/[^\n]/g, ' '))
    if (slashes) {
        out = out.replace(/\/\/[^\n]*/g, (line) => ' '.repeat(line.length))
    }
    return out
}

function lineOf(text, offset) {
    let line = 1
    for (let i = 0; i < offset && i < text.length; i++) {
        if (text[i] === '\n') {
            line++
        }
    }
    return line
}

// ---- the library's table ---------------------------------------------------------------------
//
// One row per rule declaration in the two rulesets that hold laws: the rule's symbol (`pick_at_t`,
// `jvp_pick_at`), the key it files (read from the head of its left side, `src/parse/ast/
// rule_head.cpp#leftHeadOf`), and the left side itself, so a reader can see what was read.

const RULESETS = ['stdlib/rulesets/transpose.cml', 'stdlib/rulesets/jvp.cml']

/// The number of top-level commas plus one: `f(a, b)` takes two operands. Commas inside brackets
/// are the types' (`f::<T, U>(a)`), and the caller has already cut those off.
function arityOfArguments(text) {
    const trimmed = text.trim()
    if (trimmed === '') {
        return 0
    }
    let depth = 0
    let count = 1
    for (const character of trimmed) {
        if ('([<{'.includes(character)) {
            depth++
        } else if (')]>}'.includes(character)) {
            depth--
        } else if (character === ',' && depth === 0) {
            count++
        }
    }
    return count
}

/// The key a rule files: the head of its left side and how many operands it is given.
/// `x + c` is the token `+` at two, `-x` is `-` at one, `pick_at(xs, at)` is the call `pick_at`
/// at two, `float(x)` is the conversion `float` at one.
function headOfLeft(left) {
    const text = left.trim()
    if (text === '') {
        return null
    }
    // A call, with or without the static arguments a left side may fix (`log_softmax<0>(x)`) or
    // the type arguments it may name (`f::<T>(a, b)`). The head is the callee's name and the
    // operand count is what the parentheses hold, which is what `leftHeadOf` reads.
    const call = text.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*(?:::\s*)?(?:<[^]*?>)?\s*\(([^]*)\)$/)
    if (call) {
        return { head: call[1], arity: arityOfArguments(call[2]) }
    }
    // A binary token between two operands: `x - c`. The operands are screened for brackets first,
    // so a call already returned above and `xs[i] - c` is not read as a call.
    const binary = text.match(/^([^()[\]]+?)\s*([-+*/^])\s*([^()[\]]+)$/)
    if (binary) {
        return { head: binary[2], arity: 2 }
    }
    const prefix = text.match(/^([-!~])\s*[^()[\]]+$/)
    if (prefix) {
        return { head: prefix[1], arity: 1 }
    }
    return null
}

/// The left side of a rule: everything up to the first `=>` (or `<=>`) that is not inside
/// brackets. A rule's right side is a function of the cotangent and holds `=>` of its own; the
/// left is a pattern and holds none.
function leftSideOf(body) {
    let depth = 0
    for (let i = 0; i < body.length; i++) {
        const character = body[i]
        if ('([{'.includes(character)) {
            depth++
        } else if (')]}'.includes(character)) {
            depth--
        } else if (depth === 0 && character === '=' && body[i + 1] === '>') {
            return body.slice(0, i).replace(/<\s*$/, '')
        }
    }
    return body
}

/// The body of the `index`-th `{...}` block starting at or after `from`, by brace depth.
function bracedBody(text, from) {
    const open = text.indexOf('{', from)
    if (open === -1) {
        return null
    }
    let depth = 0
    for (let i = open; i < text.length; i++) {
        if (text[i] === '{') {
            depth++
        } else if (text[i] === '}') {
            depth--
            if (depth === 0) {
                return { open, body: text.slice(open + 1, i), end: i }
            }
        }
    }
    return null
}

function readLibrary() {
    const rules = []
    const keysByHead = new Map() // head -> Set of arities, for the module-operator lookup
    for (const relative of RULESETS) {
        const raw = read(relative)
        const text = withoutComments(raw)
        for (const match of text.matchAll(/^[ \t]*(?:export[ \t]+)?rule[ \t]+([A-Za-z_][A-Za-z0-9_]*)/gm)) {
            const symbol = match[1]
            const after = match.index + match[0].length
            const braced = bracedBody(text, after)
            if (braced === null) {
                fail(`${relative}: the rule '${symbol}' has no body`)
            }
            const left = leftSideOf(braced.body)
            const key = headOfLeft(left)
            if (key === null) {
                // A rule whose left side this reader cannot name is a rule whose key the
                // comparison cannot state: reporting it as nothing would hide a law.
                fail(
                    `${relative}:${lineOf(raw, match.index)}: the left side of '${symbol}' is not ` +
                        `a shape this reader names: ${JSON.stringify(left.trim().slice(0, 80))}`
                )
            }
            rules.push({
                symbol,
                file: relative,
                line: lineOf(raw, match.index),
                left: left.trim().replace(/\s+/g, ' '),
                head: key.head,
                arity: key.arity,
                key: `${key.head}@${key.arity}`,
            })
            if (!keysByHead.has(key.head)) {
                keysByHead.set(key.head, new Set())
            }
            keysByHead.get(key.head).add(key.arity)
        }
    }
    if (rules.length === 0) {
        fail('the two rulesets file no rule: the reader stopped matching, so nothing was compared')
    }
    return { rules, keysByHead }
}

// ---- the engine's table -----------------------------------------------------------------------
//
// Every `setRule(uri, fn)` and every `setVjp(defs, "name", fn)` in `src/` and `modules/`. A
// registration whose URI is a template parameter (`registerFloatRules<F64>` writing
// `F::add`) is resolved through the struct that supplies it and the instantiation that asks for
// it, so the two float widths are read as the four registers they are instead of as one.

const ENGINE_ROOTS = ['src', 'modules']
const SKIP_DIRS = new Set(['.git', 'out', 'node_modules', 'third_party', 'build', 'sdks', 'test'])

function* cppFiles(dir) {
    let entries
    try {
        entries = fs.readdirSync(dir, { withFileTypes: true })
    } catch {
        return
    }
    for (const entry of entries) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) {
            if (!SKIP_DIRS.has(entry.name)) {
                yield* cppFiles(full)
            }
        } else if (entry.isFile() && /\.(cpp|cc|h)$/.test(entry.name)) {
            yield full
        }
    }
}

/// The protocol a `setVjp(defs, ...)` lands in: `OpRegistry::instance().add("<protocol>", ...)`
/// in the tensor module's catalog. Read, not assumed, and refused when the catalog names more
/// than one, because then one of the two would be attributed to the wrong namespace.
function tensorProtocol() {
    const text = withoutComments(read('modules/tensor/ops/catalog.cpp'))
    const protocols = new Set()
    for (const match of text.matchAll(/OpRegistry::instance\(\)\.add\(\s*"([^"]+)"/g)) {
        protocols.add(match[1])
    }
    if (protocols.size !== 1) {
        fail(
            `modules/tensor/ops/catalog.cpp registers ${protocols.size} protocol(s) ` +
                `(${[...protocols].join(', ')}): a setVjp cannot be attributed by this reader`
        )
    }
    return [...protocols][0]
}

function readEngine() {
    const protocol = tensorProtocol()
    const registrations = []
    let transportsRead = 0
    for (const root of ENGINE_ROOTS) {
        for (const file of cppFiles(path.join(ROOT, root))) {
            const relative = path.relative(ROOT, file).replaceAll(path.sep, '/')
            const raw = fs.readFileSync(file, 'utf8')
            const text = withoutComments(raw)
            // The struct constants a templated registration writes its URIs as.
            const constants = new Map() // struct name -> Map member -> uri
            for (const struct of text.matchAll(/struct[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*\{([^]*?)\n\};/g)) {
                const members = new Map()
                for (const member of struct[2].matchAll(
                    /static[ \t]+constexpr[ \t]+std::string_view[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*"([^"]+)"/g
                )) {
                    members.set(member[1], member[2])
                }
                if (members.size > 0) {
                    constants.set(struct[1], members)
                }
            }
            // A templated registrar and the structs it is instantiated with: `registerFloatRules`
            // at `<F64>` and `<F32>` means `F::add` is both `:op/add_d` and `:op/add_f`, and not
            // `:op/add_bf` -- which is a struct with the same member that no one instantiates.
            const instantiations = new Map() // registrar -> [struct]
            for (const instantiation of text.matchAll(
                /([A-Za-z_][A-Za-z0-9_]*)[ \t]*<[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*>[ \t]*\(/g
            )) {
                if (!instantiations.has(instantiation[1])) {
                    instantiations.set(instantiation[1], [])
                }
                instantiations.get(instantiation[1]).push(instantiation[2])
            }
            const registrarOf = (offset) => {
                // The innermost `template <typename F> void name(` before this offset.
                const before = text.slice(0, offset)
                const found = [...before.matchAll(/template[ \t]*<[ \t]*typename[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*>[ \t]*[^;{}()]*?([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(/g)]
                if (found.length === 0) {
                    return null
                }
                const last = found[found.length - 1]
                return { parameter: last[1], name: last[2] }
            }
            const resolve = (argument, offset) => {
                const literal = argument.match(/^"([^"]+)"$/)
                if (literal) {
                    return [literal[1]]
                }
                const member = argument.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*::\s*([A-Za-z_][A-Za-z0-9_]*)$/)
                if (member === null) {
                    return []
                }
                const registrar = registrarOf(offset)
                if (registrar !== null && registrar.parameter === member[1]) {
                    const uris = []
                    for (const instantiated of instantiations.get(registrar.name) || []) {
                        const members = constants.get(instantiated)
                        if (members && members.has(member[2])) {
                            uris.push(members.get(member[2]))
                        }
                    }
                    return uris
                }
                // A plain `F64::add`: the struct names the URI itself.
                const members = constants.get(member[1])
                return members && members.has(member[2]) ? [members.get(member[2])] : []
            }
            // The loop form, which both registrations use for a list of names:
            // `for (std::string_view name : {"lt", "le", ...}) { setVjp(defs, name, ...) }` and
            // `for (const char *uri : {":op/atoi", ...}) { registry.setRule(uri, ...) }`. One
            // registration writes the rule once, so each name in the list is a row.
            const loops = []
            for (const loop of text.matchAll(
                /for\s*\([^)]*:\s*\{([^}]*)\}\s*\)\s*\{([^]*?)\n\s*\}/g
            )) {
                const names = [...loop[1].matchAll(/"([^"]+)"/g)].map((literal) => literal[1])
                if (names.length > 0) {
                    loops.push({ start: loop.index, end: loop.index + loop[0].length, names })
                }
            }
            // A registration argument that is a loop's induction variable is that loop's list;
            // anything else is one value.
            const namesOf = (argument) => {
                const literal = argument.match(/^"([^"]+)"$/)
                return literal ? [literal[1]] : []
            }
            // A call, never the method's own definition (`src/core/derivative.cpp` declares
            // `setRule(std::string_view, VjpRule)`, and a definition registers nothing). The one
            // registration whose URI is not a value this reader can name is the transport:
            // `modules/tensor/ops/registry.cpp` copies `OpDef::vjp` into the registry under
            // `<protocol>:<def.name>`, and a `setVjp` is what put a function there. It is counted
            // rather than resolved, and the count is asserted below, because a transport that
            // stopped existing would make every `setVjp` row disappear without a refusal here.
            let transports = 0
            for (const match of text.matchAll(/\.\s*setRule\s*\(\s*([^,()]+?)\s*,\s*([^,()]+?)\s*\)/g)) {
                const argument = match[1].trim()
                if (/\.vjp$/.test(match[2].trim())) {
                    transports += 1
                    continue
                }
                let uris = resolve(argument, match.index)
                if (uris.length === 0) {
                    const loop = loops.find((candidate) => match.index >= candidate.start && match.index < candidate.end)
                    uris = loop ? loop.names : []
                }
                if (uris.length === 0) {
                    fail(
                        `${relative}:${lineOf(raw, match.index)}: the URI of a setRule is not one ` +
                            `this reader resolves: ${argument}`
                    )
                }
                for (const uri of uris) {
                    registrations.push({ uri, file: relative, line: lineOf(raw, match.index), call: 'setRule' })
                }
            }
            if (transports > 0) {
                transportsRead += transports
            }
            for (const match of text.matchAll(/\bsetVjp\([ \t]*\w+[ \t]*,[ \t]*([^,]+?)[ \t]*,/g)) {
                const argument = match[1].trim()
                let names = namesOf(argument)
                if (names.length === 0) {
                    const loop = loops.find((candidate) => match.index >= candidate.start && match.index < candidate.end)
                    names = loop ? loop.names : []
                }
                if (names.length === 0) {
                    fail(
                        `${relative}:${lineOf(raw, match.index)}: the name of a setVjp is not one ` +
                            `this reader resolves: ${argument}`
                    )
                }
                for (const name of names) {
                    registrations.push({
                        uri: `${protocol}:${name}`,
                        file: relative,
                        line: lineOf(raw, match.index),
                        call: 'setVjp',
                    })
                }
            }
        }
    }
    if (registrations.length === 0) {
        fail('no derivative rule was read from the engine: the reader stopped matching, so nothing was compared')
    }
    if (transportsRead === 0) {
        fail(
            'the transport that copies an `OpDef::vjp` into the registry is gone: the `setVjp` ' +
                'rows would be registered by nothing'
        )
    }
    return registrations
}

/// Whether an operator URI is a conversion, and to which type: the registrations with their two
/// types are in `src/builtin/executors/builtin.cpp#kConversions`, which is where the compiler
/// reads them too. A `:op/*` name that is not there is not a conversion, whatever its letters.
function conversionTargets() {
    const text = withoutComments(read('src/builtin/executors/builtin.cpp'))
    const targets = new Map()
    for (const match of text.matchAll(
        /\{\s*"(op\/[a-z0-9_]+)"\s*,[^,]*,\s*TypeCode::([A-Za-z0-9_]+)\s*,\s*TypeCode::([A-Za-z0-9_]+)\s*\}/g
    )) {
        targets.set(`:${match[1]}`, match[3])
    }
    if (targets.size === 0) {
        fail('src/builtin/executors/builtin.cpp names no conversion: the reader stopped matching')
    }
    return targets
}

/// The key an operator URI reduces to, as `src/core/forward.cpp#ruleKeyOf` states it. A `:op/`
/// name that is neither a written operation nor a conversion, and a module operator whose name
/// the rulesets file no rule under, reduce to a key with no operand count: the compiler's answer
/// for them is "no key at all", which is a fact about the library's table (it files nothing the
/// compiler would ask for there) and is reported as such rather than dropped.
function keyOfUri(uri, library, conversions) {
    for (const exception of RULE_KEY_EXCEPTIONS) {
        if (exception.uri === uri) {
            return { head: exception.head, arity: exception.arity }
        }
    }
    if (uri.startsWith(':op/')) {
        // The name *inside* the namespace: `:op/add_d` gives `add_d`. The type suffix is whatever
        // follows the first `_`, so the operation is `add`; the prefix's own colon is not the
        // separator `nameOf` cuts at, which is why the namespace is stripped first.
        const inside = uri.slice(':op/'.length)
        const underscore = inside.indexOf('_')
        const operation = underscore === -1 ? inside : inside.slice(0, underscore)
        const written = WRITTEN_OPERATIONS.find((entry) => entry.name === operation)
        if (written) {
            return { head: written.head, arity: written.arity }
        }
        const target = conversions.get(uri)
        if (target !== undefined) {
            const spelling = TYPE_CODE_SPELLING.get(target)
            if (spelling === undefined) {
                fail(`a conversion to TypeCode::${target} has no spelling this reader knows`)
            }
            return { head: spelling, arity: 1 }
        }
        return { head: inside, arity: null }
    }
    const colon = uri.lastIndexOf(':')
    const name = colon === -1 ? uri : uri.slice(colon + 1)
    const arities = library.keysByHead.get(name)
    if (arities === undefined) {
        // The family's index is where the compiler takes the operand count from, so a name the
        // rulesets file no rule under is a key that does not exist: the engine answers a step the
        // library was never asked about. Kept as a key-less row rather than dropped.
        return { head: name, arity: null }
    }
    if (arities.size !== 1) {
        fail(
            `the rulesets file '${name}' at ${[...arities].join(' and ')} operand(s): the engine's ` +
                `operand count for it is what the index says, and it says two things`
        )
    }
    return { head: name, arity: [...arities][0] }
}

// ---- the exemption list -----------------------------------------------------------------------
//
// One row per difference, three fields: the key's marker (the operator's name, or the token for a
// written operation), who owns the row, and why it is a difference rather than a gap. A
// wildcard is refused: the point of the file is that a reader can answer "why is this one
// allowed" for every row without asking anyone.

function readExemptions() {
    let text
    try {
        text = fs.readFileSync(EXEMPTIONS, 'utf8')
    } catch {
        fail(
            `${path.relative(ROOT, EXEMPTIONS).replaceAll(path.sep, '/')} is not there: every ` +
                `difference between the two tables is written down in it`
        )
    }
    const rows = []
    const problems = []
    text.split(/\r?\n/).forEach((line, index) => {
        const trimmed = line.trim()
        if (trimmed === '' || trimmed.startsWith('#')) {
            return
        }
        const fields = line.split('\t').map((field) => field.trim())
        if (fields.length !== 3) {
            problems.push(`${index + 1}: ${fields.length} field(s), the list is marker, owner, reason`)
            return
        }
        const [marker, owner, reason] = fields
        if (marker.includes('*')) {
            problems.push(`${index + 1}: '${marker}' is a wildcard, the list holds one row per key`)
            return
        }
        if (owner === '' || reason === '') {
            problems.push(`${index + 1}: '${marker}' has no owner or no reason`)
            return
        }
        rows.push({ marker, owner, reason, line: index + 1 })
    })
    if (problems.length > 0) {
        console.log('FAIL the exemption list is not the shape it claims:')
        for (const problem of problems) {
            console.log(`  scripts/vjp-parity-exemptions.tsv:${problem}`)
        }
        process.exit(2)
    }
    return rows
}

// ---- the comparison ---------------------------------------------------------------------------

const library = readLibrary()
const engine = readEngine()
const conversions = conversionTargets()
const exemptions = readExemptions()
const exemptByMarker = new Map(exemptions.map((row) => [row.marker, row]))

function markerOf(head) {
    // A token head (`+`, `-`) is its own marker; a name is the name.
    return head
}

// The library's keys, one entry per key with every rule filed under it.
const libraryKeys = new Map()
for (const rule of library.rules) {
    if (!libraryKeys.has(rule.key)) {
        libraryKeys.set(rule.key, { head: rule.head, arity: rule.arity, rules: [] })
    }
    libraryKeys.get(rule.key).rules.push(rule)
}

// The engine's keys, with every registration that reduces to each.
const engineKeys = new Map()
const engineUnkeyed = new Map() // head -> registrations whose operand count no ruleset states
for (const registration of engine) {
    const key = keyOfUri(registration.uri, library, conversions)
    const bucket = key.arity === null ? engineUnkeyed : engineKeys
    const id = key.arity === null ? key.head : `${key.head}@${key.arity}`
    if (!bucket.has(id)) {
        bucket.set(id, { head: key.head, arity: key.arity, registrations: [] })
    }
    bucket.get(id).registrations.push(registration)
}

const rows = { both: [], libraryOnly: [], engineOnly: [] }
for (const [key, entry] of libraryKeys) {
    if (engineKeys.has(key)) {
        const other = engineKeys.get(key)
        rows.both.push({
            key,
            head: entry.head,
            arity: entry.arity,
            rules: entry.rules,
            registrations: other.registrations,
        })
    } else {
        rows.libraryOnly.push({
            key,
            head: entry.head,
            arity: entry.arity,
            rules: entry.rules,
        })
    }
}
for (const [key, entry] of engineKeys) {
    if (!libraryKeys.has(key)) {
        rows.engineOnly.push({ key, head: entry.head, arity: entry.arity, registrations: entry.registrations })
    }
}
for (const [key, entry] of engineUnkeyed) {
    rows.engineOnly.push({ key, head: entry.head, arity: null, registrations: entry.registrations })
}
rows.libraryOnly.sort((a, b) => (a.key < b.key ? -1 : a.key > b.key ? 1 : 0))
rows.engineOnly.sort((a, b) => (a.key < b.key ? -1 : a.key > b.key ? 1 : 0))

// Which differences the list does not hold, and which rows of the list no longer describe one.
const newDebt = []
for (const row of [...rows.libraryOnly, ...rows.engineOnly]) {
    if (!exemptByMarker.has(markerOf(row.head))) {
        newDebt.push(row)
    }
}
const stale = exemptions.filter(
    (exemption) =>
        ![...rows.libraryOnly, ...rows.engineOnly].some((row) => row.head === exemption.marker)
)

if (JSON_MODE) {
    console.log(
        JSON.stringify(
            {
                both: rows.both.map((row) => ({ key: row.key })),
                libraryOnly: rows.libraryOnly,
                engineOnly: rows.engineOnly,
                newDebt: newDebt.map((row) => row.key),
                stale: stale.map((row) => row.marker),
            },
            null,
            2
        )
    )
    process.exit(newDebt.length === 0 ? 0 : 1)
}

const where = (rule) => `${rule.file}#${rule.symbol}`
const at = (registration) => `${registration.file} (${registration.call}, line ${registration.line})`

if (TABLES) {
    console.log(`both tables answer ${rows.both.length} key(s)`)
    console.log('')
    console.log(`the library answers and the engine does not: ${rows.libraryOnly.length}`)
    for (const row of rows.libraryOnly) {
        const exempt = exemptByMarker.get(row.head)
        console.log(`  ${row.key}${exempt ? '  [exempt]' : '  [NEW]'}`)
        for (const rule of row.rules) {
            console.log(`      ${where(rule)}  left side: ${rule.left}`)
        }
        console.log(`      the engine's table: no row reduces to ${row.key}`)
    }
    console.log('')
    console.log(`the engine answers and the library does not: ${rows.engineOnly.length}`)
    for (const row of rows.engineOnly) {
        const exempt = exemptByMarker.get(row.head)
        console.log(`  ${row.key}${exempt ? '  [exempt]' : '  [NEW]'}`)
        for (const registration of row.registrations) {
            console.log(`      ${registration.uri}: ${at(registration)}`)
        }
        console.log(`      the library's table: no rule files ${row.key}`)
    }
    console.log('')
    console.log(`${exemptions.length} exemption(s) in the list, ${stale.length} of them stale`)
    process.exit(newDebt.length === 0 ? 0 : 1)
}

if (newDebt.length > 0) {
    console.log('the two derivative tables differ where the exemption list says nothing:')
    for (const row of newDebt) {
        if (row.rules) {
            console.log(`  library-only ${row.key}:`)
            for (const rule of row.rules) {
                console.log(`    ${where(rule)} files it, the engine's table has no row for it`)
            }
        } else {
            console.log(`  engine-only ${row.key}:`)
            for (const registration of row.registrations) {
                console.log(`    ${registration.uri} at ${at(registration)}, no rule files the key`)
            }
        }
    }
    console.log(
        `FAIL ${newDebt.length} difference(s) with no exemption: either a law was added and no ` +
            `engine rule answers it, or a rule was added and no law files its key`
    )
    process.exit(1)
}

if (stale.length > 0) {
    for (const exemption of stale) {
        console.log(
            `note scripts/vjp-parity-exemptions.tsv:${exemption.line}: '${exemption.marker}' is ` +
                `exempt and is no longer a difference: the row can go`
        )
    }
}
console.log(
    `PASS the two derivative tables differ only where the list says so: ${library.rules.length} ` +
        `law(s) filed under ${libraryKeys.size} key(s), ${engine.length} engine registration(s) ` +
        `under ${engineKeys.size + engineUnkeyed.size} key(s), ${rows.both.length} key(s) both ` +
        `answer, ${rows.libraryOnly.length} library-only and ${rows.engineOnly.length} engine-only ` +
        `exempt, 0 new`
)
process.exit(0)
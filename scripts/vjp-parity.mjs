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
//                        `src/stdlib/std/builtin/operators/derivative.cpp` (the core's arithmetic,
//                        conversions, array writes and the tangent operators),
//                        `src/stdlib/tensor/ops/*.cpp` (`setVjp(defs, "<name>", &fn)`, which
//                        `src/stdlib/tensor/ops/registry.cpp` turns into `tensor:<name>`) and
//                        `src/stdlib/math/module.cpp`, `src/stdlib/autodiff/operators.cpp`.
//                        `src/stdlib/autodiff/pullback.cpp` asks it by URI (`findRule`) and refuses
//                        the node when the answer is nothing.
//
//   the library's table  one `export rule <name>` per law in `src/stdlib/rulesets/transpose.cml`
//                        (the reverse side) and `src/stdlib/rulesets/jvp.cml` (the forward side).
//                        The compiler asks it by *key*, not by URI:
//                        `src/kernel/core/forward.cpp#ruleKeyOf` reads the key off the operator --
//                        `:op/add_d` is `+` at two operands, `tensor:pick_at` is the call
//                        `pick_at` at two, a conversion is its destination's spelling at one --
//                        and `src/kernel/core/rule_index.cpp#lookupRules` answers the laws filed
//                        under it. (Until 0.4 it answered from the syntax tree; the answer is
//                        the same one, read from `RuleObject` instead of from `RuleHead`.)
//
// Nothing compared the two, and the shape of the failure that produced is a half-account: a law
// the library answers and the engine refuses. `tensor:pick_at` was one -- the library filed
// `src/stdlib/rulesets/transpose.cml#pick_at_t` and `src/stdlib/rulesets/jvp.cml#jvp_idx_arr`, the
// engine's table had no row, so `vjp` answered `[0.0, 1.0, 0.0]` for a program `grad` refused.
// Each one is found by a person meeting it. This is the comparison instead.
//
// What is compared is one key per row, because that is the unit both sides can be stated in.
// Every registration is reduced to the key the compiler would ask for it (`ruleKeyOf`), every
// rule of the two rulesets is reduced to the key it files (`leftHeadOf`), and the two key sets
// are diffed:
//
//   both          a key on both sides, with a law in **each** half of the library: the step is
//                 answered either way. Counted, not listed.
//   forward-only  a key on both sides whose library law is the forward one only: `jvp` answers it
//                 and `vjp` does not. Counted here rather than in `both`, because a count of keys
//                 cannot tell the two apart and "the library answers it" is then true of one
//                 direction: `layer_norm` is this shape, and so is every `nn` law that landed
//                 without its adjoint. Listed by name under `--tables` and `--json`, like the two
//                 rows below.
//   reverse-only  a key on both sides whose library law is the reverse one only: `vjp` answers it
//                 and `jvp` does not. The engine answers `vjp`, so the two halves disagree the
//                 other way round.
//   neither-half  a key on both sides whose library law is neither (`vmap.cml` files batching
//                 rules, which answer no derivative). It used to be counted as `both`.
//   library-only  the library answers the key and no engine registration reduces to it. This is
//                 the `pick_at` shape, and the table a reader acts on.
//   engine-only   an engine registration reduces to no key the library files. Legal (the
//                 tangent operators are machinery no program writes, a conversion to a discrete
//                 type has no tangent to carry) but not silently: each row here is a decision,
//                 and the decision is written in the exemption list below.
//
// The third column is why the split exists: `both` counted a key whose only law was `jvp_conv2d`
// beside one whose two halves are the pair an adjoint is, and route five's question -- can the
// engine's table be deleted -- is a question about the **pair**, not about a key. A tree whose
// engine-only count reached zero while five keys had one half would read green and lose a
// capability; this is the column that says so.
//
// Equality is not the property asked for, because the two tables are not meant to be equal: a
// law can belong to the library alone (乙: the step exists only in a transformed graph, or the
// engine's pass is the one that must answer it) and to the engine alone (丙: the operator is
// machinery). So the property is: **every difference is written down, with who owns it and why**
// -- `scripts/vjp-parity-exemptions.tsv`, one row per key, three fields, no wildcard. A
// difference that is not in that file is the debt this check exists to catch, and it is caught in
// both directions: a new law in `src/stdlib/rulesets/*.cml` with no `setVjp` behind it is a
// library-only row, and a new `setRule` with no law beside it is an engine-only row.
//
//   node scripts/vjp-parity.mjs                  check: the tree's differences are the list
//   node scripts/vjp-parity.mjs --strict         the same check, and a row that holds no
//                                                difference is a failure (see `stale` below)
//   node scripts/vjp-parity.mjs --tables         print the three tables and decide nothing
//   node scripts/vjp-parity.mjs --json           the same as --tables, as JSON
//   node scripts/vjp-parity.mjs --closure        the other direction of the same two files: does
//                                                every key `transpose.cml` files have a `jvp.cml`
//                                                law? (see "the closure" below)
//   node scripts/vjp-parity.mjs --reaches        the third question of the same law set: did any
//                                                program apply this law? Runs the corpus with
//                                                `--trace transforms` (see "which law a program
//                                                applied" below); needs a built install
//   node scripts/vjp-parity.mjs --root DIR       read a tree other than this one (the self-test)
//   node scripts/vjp-parity.mjs --exempt FILE    read another exemption list (the self-test)
//
// Exit: 0 when every difference is exempt, 1 when one is not, 2 when the tree cannot be read
// (a missing file, a ruleset that files no rule at all -- a reader that stopped matching must
// fail rather than report a clean tree).
//
// ---- `stale`, and why the count is printed every time ----------------------------------------
//
// The check above is one-directional: a difference with no row is debt, a row with no difference
// is silence. Silence is the wrong reading. A row that has outlived its difference still carries
// an owner and a reason, so to every reader it looks exactly like a live row -- the list grows,
// and the day someone asks "what is `gelu` still exempt from" the answer is nothing, said in the
// same three fields as the rows that are real. That is the shape of a lookup that answers with a
// plausible value because the true one is not there.
//
// So every reading prints `stale=<n>`: how many rows name a marker the tree's differences no
// longer carry. `--strict` turns that count into an exit code, for the gate; the default reading
// prints it and stays green, because a lane may be looking at a tree whose library and engine
// moved one commit apart, and a hard red there would be refused rather than read. The two
// readings are one number apart on purpose: the number is always there, and failing on it is a
// decision the caller makes by name.

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import url from 'node:url'
import { spawn } from 'node:child_process'

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
// are filed under `sum` at the one operand the parentheses carry.
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

// `typeCodeToString` (`src/kernel/core/type/base.cpp`) for the codes a conversion can target: the key of
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
const CLOSURE = process.argv.includes('--closure')
// `--reaches` asks a different question of the same law set: not "is there a law for this key" but
// "did a program apply it". It runs the corpus and reads the `LAW_APPLIED` records the walks write
// (`src/stdlib/transforms/law_readings.h`), so nothing here parses a sentence or counts a name a
// person typed.
const REACHES = process.argv.includes('--reaches')
// `stale > 0` is a failure only when asked for by name: the count is reported either way, and the
// default reading stays green so a mid-merge tree can still be read.
const STRICT = process.argv.includes('--strict')
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
// One row per rule declaration in every ruleset the directory holds: the rule's symbol (`pick_at_t`,
// `jvp_pick_at`), the key it files (read from the head of its left side, `src/parse/src/ast/
// rule_head.cpp#leftHeadOf`), and the left side itself, so a reader can see what was read.
//
// The rulesets are read by directory, not from a list of names. The list this replaced named two
// files (`transpose.cml`, `jvp.cml`) while the directory held five, and the three it did not name
// were read by nothing: `vmap.cml` files 27 laws -- `argmax`, `sum`, `where`, `concat`, `dim`,
// `unsqueeze`, ... -- so five keys they file were reported as keys the library files no law for,
// and five rows of the exemption list were written against that blind reading. A law in a file no
// line of this script opens is a law nothing compares, and no error says so: each table stays
// complete in itself, which is the shape of every defect this comparison exists for.
//
// So the read set is the top of `src/stdlib/rulesets/`, and `unreadRulesets` below is the relation that
// keeps it the whole directory rather than its top level: a ruleset that moves into a subdirectory
// stops being read the day it moves, and a green check would not say so.

const RULESET_DIRECTORY = 'src/stdlib/rulesets'

function rulesetFiles() {
    let names
    try {
        names = fs.readdirSync(path.join(ROOT, RULESET_DIRECTORY))
    } catch {
        return fail(
            `${RULESET_DIRECTORY} is not in the tree: the comparison reads its rulesets from it`
        )
    }
    const files = names.filter((name) => name.endsWith('.cml')).sort()
    if (files.length === 0) {
        // An empty read set makes every relation below true of nothing.
        return fail(
            `${RULESET_DIRECTORY} holds no .cml: a reader that found nothing compared nothing`
        )
    }
    return files.map((name) => `${RULESET_DIRECTORY}/${name}`)
}

const RULESETS = rulesetFiles()

// The two halves of the derivative library, by the file each writes its laws in. The names are not
// a list of rulesets -- `RULESETS` above is read from the directory and must stay that way -- but of
// the **pair** a key needs to be answered in both directions: `jvp.cml` answers the tangent of the
// result from the tangents of the operands, `transpose.cml` answers the cotangents of the operands
// from the cotangent of the result, and `src/stdlib/rulesets/vmap.cml` answers neither (its laws
// batch an operator over lanes). A key filed by a fourth ruleset some day is neither half, and is
// reported as such rather than assumed into one.
const FORWARD_RULESET = 'src/stdlib/rulesets/jvp.cml'
const BACKWARD_RULESET = 'src/stdlib/rulesets/transpose.cml'

/// Which half of the pair files a law: `'forward'`, `'backward'`, or `'neither'`.
function halfOf(file) {
    if (file === FORWARD_RULESET) {
        return 'forward'
    }
    if (file === BACKWARD_RULESET) {
        return 'backward'
    }
    return 'neither'
}

/// The `.cml` files under `src/stdlib/rulesets` that declare a rule and are not in the read set.
///
/// The read set is the top of the directory, so a ruleset one level down is read by nothing while
/// the check stays green -- the same defect, moved. A file is recognised by what it says (`rule`,
/// comments blanked) rather than by its name, and every one is named: a red that says only how many
/// places were missed leaves the reader to work out which, which is the work this check exists to do.
function unreadRulesets() {
    const readSet = new Set(RULESETS)
    const stray = []
    const walk = (directory) => {
        let entries
        try {
            entries = fs.readdirSync(directory, { withFileTypes: true })
        } catch {
            return
        }
        for (const entry of entries) {
            const full = path.join(directory, entry.name)
            if (entry.isDirectory()) {
                walk(full)
                continue
            }
            if (!entry.name.endsWith('.cml')) {
                continue
            }
            const relative = path.relative(ROOT, full).replaceAll(path.sep, '/')
            if (readSet.has(relative)) {
                continue
            }
            const text = withoutComments(fs.readFileSync(full, 'utf8'))
            if (/^[ \t]*(?:export[ \t]+)?rule[ \t]+[A-Za-z_]/m.test(text)) {
                stray.push(relative)
            }
        }
    }
    walk(path.join(ROOT, RULESET_DIRECTORY))
    return stray.sort()
}

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
    // The read set is a directory, so it has to be the directory that was read: a ruleset it does
    // not hold is a law nothing compares, and each one is named rather than counted.
    for (const unread of unreadRulesets()) {
        fail(
            `${unread} declares rules and is not read: the library is read from ` +
                `${RULESET_DIRECTORY}/*.cml, so its laws are compared by nothing. It belongs at the ` +
                `top of that directory, or this reader has to read it`
        )
    }
    if (rules.length === 0) {
        fail(
            `${RULESET_DIRECTORY} files no rule: the reader stopped matching, so nothing was compared`
        )
    }
    return { rules, keysByHead }
}

// ---- the engine's table -----------------------------------------------------------------------
//
// Every `setRule(uri, fn)` and every `setVjp(defs, "name", fn)` in the engine's sources and in the
// modules. A registration whose URI is a template parameter (`registerFloatRules<F64>` writing
// `F::add`) is resolved through the struct that supplies it and the instantiation that asks for
// it, so the two float widths are read as the four registers they are instead of as one.
//
// The roots are the block directories the layout map files the sources under, not `src/` and
// `modules/`: after the move the engine is `src/kernel/`, `src/compile/` and `src/parse/`,
// and a module is `src/stdlib/<name>`. They are spelled out because a bare `'src'` is a word as much
// as a path -- the rewrite that carries the move will not touch it -- and a root list that no longer
// exists makes every law look unanswered, which is how this comparison reported a whole table as
// missing. The names from before the move stay in the list, so a checkout of that layout is read
// too; a root the tree does not have is skipped, which `readEngine` checks by name.
const ENGINE_ROOTS = ['src/kernel', 'src/compile', 'src/parse', 'src/stdlib', 'kernel', 'compile', 'parse', 'lib']
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

/// The protocol each module publishes its operators under, read from every
/// `OpRegistry::instance().add("<protocol>", ...)` under `src/stdlib` and grouped two ways: by the
/// file that writes the call and by the module (`src/stdlib/<name>`) the file belongs to.
///
/// The URI of a module operator is `<protocol>:<def.name>` (`src/stdlib/tensor/ops/registry.cpp`
/// copies `OpDef` into the registry under it), so the protocol of a `setVjp` is the one of the
/// module that **publishes the list the call is written against** -- and that is not always one
/// module: `src/stdlib/nn/tensor_ops.cpp` publishes its own list from the same file
/// (`OpRegistry::instance().add("nn", nnTensorOps())`), while every `src/stdlib/tensor/ops/*.cpp`
/// returns its list to the module's catalog (`src/stdlib/tensor/ops/catalog.cpp`), which is the
/// only file there that calls `add`. Reading the tensor catalog's protocol for the whole tree filed
/// the `nn` rows under `tensor:` -- the right key, the wrong namespace printed beside it
/// Read, not assumed; a module that publishes two protocols is refused rather than
/// guessed, because one of the two would be attributed to the wrong namespace.
function moduleProtocols() {
    const byFile = new Map()
    const byModule = new Map()
    for (const file of cppFiles(path.join(ROOT, 'src/stdlib'))) {
        const relative = path.relative(ROOT, file).replaceAll(path.sep, '/')
        const text = withoutComments(fs.readFileSync(file, 'utf8'))
        for (const match of text.matchAll(/OpRegistry::instance\(\)\.add\(\s*"([^"]+)"/g)) {
            byFile.set(relative, match[1])
            const module = relative.split('/').slice(0, 3).join('/')
            if (!byModule.has(module)) {
                byModule.set(module, new Set())
            }
            byModule.get(module).add(match[1])
        }
    }
    if (byFile.size === 0) {
        fail(
            'no module under src/stdlib calls `OpRegistry::instance().add(...)`: the protocol of a ' +
                '`setVjp` row would be invented by this reader'
        )
    }
    return { byFile, byModule }
}

function readEngine() {
    const protocols = moduleProtocols()
    // The protocol of the file a `setVjp` is written in: its own if it publishes one, else the one
    // its module publishes. Called only from a `setVjp` row, so a file with neither is not a refusal.
    const protocolOf = (relative) => {
        const own = protocols.byFile.get(relative)
        if (own !== undefined) {
            return own
        }
        const module = relative.split('/').slice(0, 3).join('/')
        const published = protocols.byModule.get(module)
        if (published === undefined) {
            fail(
                `${relative}: a setVjp is written here and ${module} publishes no operator protocol: ` +
                    `the URI of the row cannot be stated`
            )
        }
        if (published.size !== 1) {
            fail(
                `${module} registers ${published.size} protocol(s) (${[...published].join(', ')}): ` +
                    `the URI of a setVjp in ${relative} cannot be attributed by this reader`
            )
        }
        return [...published][0]
    }
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
            // A call, never the method's own definition (`src/kernel/core/derivative.cpp` declares
            // `setRule(std::string_view, VjpRule)`, and a definition registers nothing). The one
            // registration whose URI is not a value this reader can name is the transport:
            // `src/stdlib/tensor/ops/registry.cpp` copies `OpDef::vjp` into the registry under
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
                        uri: `${protocolOf(relative)}:${name}`,
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
/// types are in `src/stdlib/std/builtin/executors/builtin.cpp#kConversions`, which is where the compiler
/// reads them too. A `:op/*` name that is not there is not a conversion, whatever its letters.
function conversionTargets() {
    const text = withoutComments(read('src/stdlib/std/builtin/executors/builtin.cpp'))
    const targets = new Map()
    for (const match of text.matchAll(
        /\{\s*"(op\/[a-z0-9_]+)"\s*,[^,]*,\s*TypeCode::([A-Za-z0-9_]+)\s*,\s*TypeCode::([A-Za-z0-9_]+)\s*\}/g
    )) {
        targets.set(`:${match[1]}`, match[3])
    }
    if (targets.size === 0) {
        fail('src/stdlib/std/builtin/executors/builtin.cpp names no conversion: the reader stopped matching')
    }
    return targets
}

/// The key an operator URI reduces to, as `src/kernel/core/forward.cpp#ruleKeyOf` states it. A `:op/`
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

// ---- the closure of the forward half over the backward half -----------------------------------
//
// The two rulesets are not independent: the library is `transpose(split(reinterp<jvp>(f)))`, so one
// half writes the operators the other half then walks. A key `transpose.cml` files a law for and
// `jvp.cml` does not is a step the composition can take in one direction only, and the refusal it
// produces at run time names nothing (`transforms: jvp has no forward transform for this function`),
// which is why the closure is stated here, before a user meets it.
//
// The unit is the same key the comparison above uses -- `head@arity`, the key the compiler derives
// from the operator (`src/kernel/core/forward.cpp#ruleKeyOf`) -- and not the parameter-type list: a family
// may file several laws under one key (`jvp.cml` files four for `float(x)`), and what the walk asks
// is whether the key has a law at all. The reported operator is the key and, beside it, the backward
// law that has no twin, so the repair names the file and the symbol to write.
//
// `library.rules` carries both files with the relative path each was read from, so a `--root` copy
// is read the same way and a law appended to the copy is reported like one in the tree.
if (CLOSURE) {
    // The two files are the module's constants (`FORWARD_RULESET`/`BACKWARD_RULESET` above), so a
    // ruleset that moves is one edit and not two.
    const backward = library.rules.filter((rule) => rule.file === BACKWARD_RULESET)
    const forward = library.rules.filter((rule) => rule.file === FORWARD_RULESET)
    const forwardKeys = new Set(forward.map((rule) => rule.key))
    const backwardKeys = new Set(backward.map((rule) => rule.key))
    const missing = backward.filter((rule) => !forwardKeys.has(rule.key))
    const lines = [
        `closure: ${BACKWARD_RULESET} files ${backward.length} law(s) over ${backwardKeys.size} key(s); ` +
            `${FORWARD_RULESET} files ${forward.length} law(s) over ${forwardKeys.size} key(s)`,
    ]
    for (const rule of missing) {
        lines.push(`  no forward law for '${rule.key}': ${rule.file}#${rule.symbol} (${rule.left})`)
    }
    lines.push(
        missing.length === 0
            ? '  0 missing: every key the backward half files has a forward law'
            : `  ${missing.length} missing`
    )
    console.log(lines.join('\n'))
    process.exit(missing.length === 0 ? 0 : 1)
}

// ---- which law a program applied --------------------------------------------------------------
//
// The comparison above answers "is there a law for this key"; `--closure` answers "does the other
// half file it too". Neither answers the third question a reader of `src/stdlib/rulesets/*.cml`
// asks: **did any program ever apply this law?** Until this mode existed the only record of that was
// prose a person kept -- markers in test cases saying a law is "filed but not walked" -- and prose
// kept by hand goes stale without saying so (a law given a working right side keeps its marker, and
// the marker then tells the next reader the opposite of the truth).
//
// So the walks write what they applied, one record per application, in the shape a program reads
// (`src/stdlib/transforms/law_readings.h`):
//
//     LAW_APPLIED {"transform":"transpose","function":"…","law":"conv2d_t","uri":"nn:conv2d"}
//
// and this mode runs the corpus once with `--trace transforms` and turns the records into a table.
// A law is **reached** when some program's run printed a record naming it. The reason a row gives is
// one of three, and they are decided by what the corpus did, not by what a case says about itself:
//
//   reached                          a program applied it, and at least one of those programs names
//                                    no law of its own: an example, a showcase, a benchmark, or a
//                                    test that walks it as part of the behaviour it pins. This is
//                                    the row that is evidence a user can reach the law.
//   reached only by a test that       every program that applied it is under `test/` and names the
//   names it                         law's symbol -- in its own text or in the plan entry that
//                                    runs it. A test written to reach a law is evidence that the
//                                    law can be reached, and no evidence that anything else does.
//   no program states it             nothing in the corpus applied it.
//
// Which half of the tree is which is a path rule and stays one: `test/` holds tests, and the four
// directories beside it hold the programs the tree ships.
//
// The corpus is **every** `.cml` under those roots, not a list: a list is the hand-kept thing this
// replaces, and a case added tomorrow is in the corpus without an edit here. The cost is one camel
// run per program at the default pass, which is why this is a mode of its own and not part of the
// gate's default reading.
//
//   node scripts/vjp-parity.mjs --reaches                  run the corpus, print the counts and the
//                                                          rows that are not `reached`
//   node scripts/vjp-parity.mjs --reaches --write           write the table to COVERAGE_TABLE
//   node scripts/vjp-parity.mjs --reaches --all             print every row
//   node scripts/vjp-parity.mjs --reaches --corpus DIR      run one directory (a smoke reading)
//   node scripts/vjp-parity.mjs --reaches --limit N         the first N programs of the corpus
//   --camel <install>    the install to run (default `out/latest`); the runs need a built binary
//   --jobs N             how many runs at a time (default 4)
//
// Exit: 0 when the table was written, or when the table in the tree is what this run computed; 1
// when it is not (the reading's stale half); 2 when the corpus or the binary cannot be read -- a
// mode that ran nothing must not answer "no law is unreached".
const COVERAGE_TABLE = 'docs/design/program-transforms/law-coverage.tsv'
const CORPUS_ROOTS = ['test/cases', 'examples', 'showcase', 'benchmarks']

if (REACHES) {
    const installOption = option('--camel', path.join(ROOT, 'out', 'latest'))
    if (installOption && installOption.error) {
        console.log(`FAIL ${installOption.error}`)
        process.exit(2)
    }
    const INSTALL = path.resolve(installOption)
    const runsAtOnce = Number(option('--jobs', '4'))
    if (!Number.isInteger(runsAtOnce) || runsAtOnce < 1) {
        console.log('FAIL --jobs takes a positive whole number')
        process.exit(2)
    }
    const corpusOption = option('--corpus', null)
    if (corpusOption && corpusOption.error) {
        console.log(`FAIL ${corpusOption.error}`)
        process.exit(2)
    }
    const ALL_ROWS = process.argv.includes('--all')
    const limitOption = option('--limit', null)
    if (limitOption && limitOption.error) {
        console.log(`FAIL ${limitOption.error}`)
        process.exit(2)
    }
    const LIMIT = limitOption === null ? null : Number(limitOption)
    if (LIMIT !== null && (!Number.isInteger(LIMIT) || LIMIT < 1)) {
        console.log('FAIL --limit takes a positive whole number')
        process.exit(2)
    }

    const executable = path.join(
        INSTALL,
        'bin',
        process.platform === 'win32' ? 'camel.exe' : 'camel'
    )
    if (!fs.existsSync(executable)) {
        console.log(
            `FAIL ${path.relative(ROOT, executable) || executable} is not in the tree: --reaches ` +
                `runs the corpus, so it needs a built install (--camel <install> names another)`
        )
        process.exit(2)
    }

    /// Every file under `directory` whose name ends with `suffix`, relative to `ROOT` with forward
    /// slashes, in path order.
    function programsUnder(directory, suffix = '.cml') {
        const found = []
        const walk = (at) => {
            let entries
            try {
                entries = fs.readdirSync(at, { withFileTypes: true })
            } catch {
                return
            }
            for (const entry of entries) {
                const full = path.join(at, entry.name)
                if (entry.isDirectory()) {
                    walk(full)
                } else if (entry.isFile() && entry.name.endsWith(suffix)) {
                    found.push(path.relative(ROOT, full).replaceAll(path.sep, '/'))
                }
            }
        }
        walk(path.join(ROOT, directory))
        return found
    }

    const corpusRoots = corpusOption === null ? CORPUS_ROOTS : [corpusOption]
    let corpus = []
    for (const root of corpusRoots) {
        const held = programsUnder(root)
        if (held.length === 0) {
            console.log(`FAIL ${root} holds no .cml: a corpus that is empty reaches nothing`)
            process.exit(2)
        }
        corpus = corpus.concat(held)
    }
    corpus.sort()
    const totalPrograms = corpus.length
    if (LIMIT !== null) {
        corpus = corpus.slice(0, LIMIT)
    }

    // What a test says about itself: its own text, and the plan entries that run it. A law's symbol
    // is a word, so the match is a word -- `average_pool` must not name `_pool_t` by accident.
    const planTextOf = new Map()
    {
        const plans = programsUnder('test/plans', '.plan.toml')
        for (const plan of plans) {
            const text = fs.readFileSync(path.join(ROOT, plan), 'utf8')
            const blocks = text.split(/^\[\[tests\]\][^\n]*$/m)
            for (const block of blocks) {
                const caseMatch = block.match(/^[ \t]*case[ \t]*=[ \t]*"([^"]+)"$/m)
                if (!caseMatch || !caseMatch[1].endsWith('.cml')) {
                    continue
                }
                const relative = path
                    .relative(ROOT, path.resolve(path.dirname(path.join(ROOT, plan)), caseMatch[1]))
                    .replaceAll(path.sep, '/')
                planTextOf.set(relative, (planTextOf.get(relative) || '') + '\n' + block)
            }
        }
    }

    const namesLaw = (program, symbol) => {
        const pattern = new RegExp(`(^|[^A-Za-z0-9_])${symbol}([^A-Za-z0-9_]|$)`)
        if (pattern.test(fs.readFileSync(path.join(ROOT, program), 'utf8'))) {
            return true
        }
        const plan = planTextOf.get(program)
        return plan !== undefined && pattern.test(plan)
    }

    // One run per program, in a scratch directory of its own: a case that writes beside itself
    // cannot touch the tree, and two runs cannot read each other's files.
    const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'vjp-parity-reaches-'))
    const record = /LAW_APPLIED (\{[^\n]*\})/g

    const runOne = (program) =>
        new Promise((resolve) => {
            const child = spawn(executable, ['--trace', 'transforms', path.join(ROOT, program)], {
                cwd: scratch,
                env: { ...process.env, CAMEL_HOME: INSTALL },
                stdio: ['ignore', 'ignore', 'pipe'],
                timeout: 30000,
            })
            let stderr = ''
            child.stderr.on('data', (chunk) => {
                // A run that floods the trace is a run whose laws are already recorded; the cap is
                // on the bytes kept, not on the laws read, because the first records are the ones
                // that name the law.
                if (stderr.length < 4 * 1024 * 1024) {
                    stderr += chunk.toString('utf8')
                }
            })
            child.on('error', () => resolve({ program, code: -1, laws: [] }))
            child.on('close', (code) => {
                const laws = []
                for (const match of stderr.matchAll(record)) {
                    try {
                        const reading = JSON.parse(match[1])
                        if (typeof reading.law === 'string' && reading.law !== '') {
                            laws.push(reading.law)
                        }
                    } catch {
                        // A record the walk wrote and this reader cannot read is a broken reading,
                        // and it is counted below: silence here would turn a broken record into a
                        // law nobody reached.
                        laws.push(null)
                    }
                }
                resolve({ program, code, laws })
            })
        })

    const runs = new Array(corpus.length)
    {
        let next = 0
        const worker = async () => {
            while (next < corpus.length) {
                const at = next
                next += 1
                runs[at] = await runOne(corpus[at])
            }
        }
        await Promise.all(Array.from({ length: Math.min(runsAtOnce, corpus.length) }, worker))
    }

    const unreadable = runs.flatMap((run) => run.laws.filter((law) => law === null))
    if (unreadable.length > 0) {
        console.log(`FAIL ${unreadable.length} LAW_APPLIED record(s) could not be read as JSON`)
        process.exit(2)
    }

    // Which programs reached each law. `library.rules` is the law set, so a ruleset added tomorrow
    // is in the table without an edit here; a `LAW_APPLIED` naming a symbol no ruleset declares is a
    // name the reader and the walk spell differently, and it is reported rather than ignored.
    const reachedBy = new Map()
    for (const run of runs) {
        for (const law of run.laws) {
            if (!reachedBy.has(law)) {
                reachedBy.set(law, new Set())
            }
            reachedBy.get(law).add(run.program)
        }
    }
    const declared = new Set(library.rules.map((rule) => rule.symbol))
    const undeclared = [...reachedBy.keys()].filter((law) => !declared.has(law)).sort()

    const rows = library.rules
        .map((rule) => {
            const by = [...(reachedBy.get(rule.symbol) || [])].sort()
            let reason
            if (by.length === 0) {
                reason = 'no program states it'
            } else if (
                by.every((program) => program.startsWith('test/') && namesLaw(program, rule.symbol))
            ) {
                reason = 'reached only by a test that names it'
            } else {
                reason = 'reached'
            }
            return { ...rule, reason, programs: by, witness: by[0] || '-' }
        })
        .sort((left, right) => (left.symbol < right.symbol ? -1 : left.symbol > right.symbol ? 1 : 0))

    const header = [
        '# Which law a program applied, out of every law the rulesets declare.',
        '#',
        '# Generated by `node scripts/vjp-parity.mjs --reaches --write`. Do not edit: the same',
        '# command without `--write` recomputes the table, compares it with this file and names every',
        '# row that moved. Re-running it at the same tree reproduces these bytes.',
        '#',
        '# The law set: every `rule` of src/stdlib/rulesets/*.cml, the same read `--closure` uses.',
        `# The corpus: every .cml under ${CORPUS_ROOTS.join(', ')}, run once with`,
        '#   `--trace transforms` at the default pass. A program reaches a law when its trace printed',
        '#   `LAW_APPLIED {...,"law":"<symbol>",...}` (src/stdlib/transforms/law_readings.h).',
        '#',
        '# reason, for the row\'s law:',
        '#   reached                     a program applied it and at least one of those programs names',
        '#                               no law of its own -- an example, a showcase, a benchmark, or a',
        '#                               test that walks it as part of what it pins.',
        '#   reached only by a test      every program that applied it is a test, and every one of',
        '#     that names it              them names the law in its text or in the plan entry that',
        '#                               runs it.',
        '#   no program states it        no program in the corpus applied it.',
        '#',
        '# The columns are the law, the key the compiler files it under, the ruleset that declares it,',
        '# the reason, how many programs applied it, and the first of those in path order.',
        'law\tkey\truleset\treason\tprograms\twitness',
    ]

    const body = rows.map(
        (row) =>
            `${row.symbol}\t${row.key}\t${row.file}\t${row.reason}\t${row.programs.length}\t${row.witness}`
    )
    const table = header.concat(body).join('\n') + '\n'

    const counts = {
        reached: rows.filter((row) => row.reason === 'reached').length,
        named: rows.filter((row) => row.reason === 'reached only by a test that names it').length,
        none: rows.filter((row) => row.reason === 'no program states it').length,
    }
    const silent = runs.filter((run) => run.code !== 0).length
    // The rows that are not `reached` are the ones a reader acts on, so they print in full; the
    // reached ones print last and as a sample, because a count of them is not evidence that the
    // reading discriminates, and eight named laws with the program that applied each are.
    const reachedRows = rows.filter((row) => row.reason === 'reached')
    const shown =
        ALL_ROWS || LIMIT !== null
            ? rows
            : [...rows.filter((row) => row.reason !== 'reached'), ...reachedRows.slice(0, 8)]

    for (const row of shown) {
        console.log(
            `  ${row.reason}: ${row.symbol} (${row.key}) set by ${row.file}` +
                (row.programs.length > 0
                    ? `, ${row.programs.length} program(s), first ${row.witness}`
                    : '')
        )
    }
    for (const law of undeclared) {
        console.log(
            `  a LAW_APPLIED names '${law}', which no ruleset declares: the reader and the walk ` +
                `spell the law differently, so no row here is that law's`
        )
    }

    if (process.argv.includes('--write')) {
        const file = path.join(ROOT, COVERAGE_TABLE)
        fs.mkdirSync(path.dirname(file), { recursive: true })
        fs.writeFileSync(file, table, 'utf8')
        console.log(
            `wrote ${COVERAGE_TABLE}: ${rows.length} row(s) -- ${counts.reached} reached, ` +
                `${counts.named} reached only by a test that names it, ${counts.none} no program ` +
                `states it`
        )
        console.log(
            `corpus ${corpus.length} of ${totalPrograms} program(s), ${runsAtOnce} at a time, ` +
                `${silent} run(s) exited non-zero (a case that dies still reports what it applied)`
        )
        process.exit(0)
    }

    const committedPath = path.join(ROOT, COVERAGE_TABLE)
    if (!fs.existsSync(committedPath)) {
        console.log(
            `no ${COVERAGE_TABLE} in the tree: this run computed ${rows.length} row(s) and wrote ` +
                `nothing (--write writes it). ${corpus.length} of ${totalPrograms} program(s) ran, ` +
                `${counts.reached} reached, ${counts.named} reached only by a test that names it, ` +
                `${counts.none} no program states it`
        )
        process.exit(2)
    }
    const committed = fs.readFileSync(committedPath, 'utf8')
    if (committed !== table) {
        const before = committed.split('\n')
        const after = table.split('\n')
        const at = before.findIndex((line, index) => line !== after[index])
        console.log(
            `FAIL ${COVERAGE_TABLE} is not what the corpus says: first difference at line ${at + 1}`
        )
        console.log(`  the tree: ${JSON.stringify(before[at] ?? '')}`)
        console.log(`  this run: ${JSON.stringify(after[at] ?? '')}`)
        process.exit(1)
    }
    console.log(
        `PASS the corpus reached every law the way ${COVERAGE_TABLE} says: ${rows.length} law(s), ` +
            `${counts.reached} reached, ${counts.named} reached only by a test that names it, ` +
            `${counts.none} no program states it (${corpus.length} program(s), ${runsAtOnce} at a time)`
    )
    process.exit(0)
}

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

const rows = {
    both: [],
    forwardOnly: [],
    reverseOnly: [],
    neitherHalf: [],
    libraryOnly: [],
    engineOnly: [],
}
for (const [key, entry] of libraryKeys) {
    if (engineKeys.has(key)) {
        const other = engineKeys.get(key)
        const row = {
            key,
            head: entry.head,
            arity: entry.arity,
            rules: entry.rules,
            registrations: other.registrations,
        }
        // Which halves answer this key: the pair is what an adjoint is, and a key with one of the
        // two is answered in one direction only. A family files several laws under one key
        // (`jvp.cml` files four for `float(x)`), so the halves are collected, not read off one rule.
        const halves = new Set(entry.rules.map((rule) => halfOf(rule.file)))
        const forward = halves.has('forward')
        const backward = halves.has('backward')
        if (forward && backward) {
            rows.both.push(row)
        } else if (forward) {
            rows.forwardOnly.push(row)
        } else if (backward) {
            rows.reverseOnly.push(row)
        } else {
            rows.neitherHalf.push(row)
        }
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
const byKey = (a, b) => (a.key < b.key ? -1 : a.key > b.key ? 1 : 0)
for (const bucket of [rows.both, rows.forwardOnly, rows.reverseOnly, rows.neitherHalf]) {
    bucket.sort(byKey)
}
rows.libraryOnly.sort(byKey)
rows.engineOnly.sort(byKey)

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
                forwardOnly: rows.forwardOnly.map((row) => ({ key: row.key })),
                reverseOnly: rows.reverseOnly.map((row) => ({ key: row.key })),
                neitherHalf: rows.neitherHalf.map((row) => ({ key: row.key })),
                libraryOnly: rows.libraryOnly,
                engineOnly: rows.engineOnly,
                newDebt: newDebt.map((row) => row.key),
                stale: stale.map((row) => row.marker),
                staleCount: stale.length,
            },
            null,
            2
        )
    )
    process.exit(newDebt.length === 0 && !(STRICT && stale.length > 0) ? 0 : 1)
}

const where = (rule) => `${rule.file}#${rule.symbol}`
const at = (registration) => `${registration.file} (${registration.call}, line ${registration.line})`

if (TABLES) {
    console.log(`both tables answer ${rows.both.length} key(s), both halves of the library`)
    console.log('')
    const oneHalf = [
        ['forward-only', 'the forward half answers it and the reverse does not', rows.forwardOnly],
        ['reverse-only', 'the reverse half answers it and the forward does not', rows.reverseOnly],
        [
            'neither-half',
            'a law files it and neither half of the derivative library answers it',
            rows.neitherHalf,
        ],
    ]
    for (const [name, why, bucket] of oneHalf) {
        console.log(`${name} (${why}): ${bucket.length}`)
        for (const row of bucket) {
            console.log(`  ${row.key}`)
            for (const rule of row.rules) {
                console.log(`      ${where(rule)}  left side: ${rule.left}`)
            }
            for (const registration of row.registrations) {
                console.log(`      ${registration.uri}: ${at(registration)}`)
            }
        }
        console.log('')
    }
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
    process.exit(newDebt.length === 0 && !(STRICT && stale.length > 0) ? 0 : 1)
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
    if (STRICT) {
        // The list is the account, and a row that holds nothing makes the account longer than the
        // fact it accounts for. `--strict` is the gate's reading: the count printed below is the
        // same number, and this is the one place that spends it.
        console.log(
            `FAIL ${stale.length} exemption row(s) hold no difference: the list has ` +
                `${exemptions.length} row(s) and the tree's differences carry ` +
                `${new Set([...rows.libraryOnly, ...rows.engineOnly].map((row) => row.head)).size} ` +
                `distinct marker(s). A dead row reads exactly like a live one, so it goes (drop ` +
                `--strict to report this and still exit 0)`
        )
        process.exit(1)
    }
}
console.log(
    `PASS the two derivative tables differ only where the list says so: ${library.rules.length} ` +
        `law(s) filed under ${libraryKeys.size} key(s), ${engine.length} engine registration(s) ` +
        `under ${engineKeys.size + engineUnkeyed.size} key(s), ${rows.both.length} key(s) both ` +
        `answer (each half), ${rows.forwardOnly.length} forward-only, ${rows.reverseOnly.length} ` +
        `reverse-only, ${rows.neitherHalf.length} with a law that is neither half, ` +
        `${rows.libraryOnly.length} library-only and ${rows.engineOnly.length} engine-only ` +
        `exempt, 0 new, stale=${stale.length}`
)
process.exit(0)
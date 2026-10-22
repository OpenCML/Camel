#!/usr/bin/env node
// A module that has moved to a declaration module stays migrated (handover H137).
//
//   node scripts/stdlib-declarations.mjs --check
//
// H19 moves a fixed-signature C++ module from `stdlib/signatures/<module>.cml` to
// `stdlib/<module>.cml` and deletes the C++ `OperatorGroup` (H94, H104, H113, H115, H126,
// H128, H131). The compiler and `--verify-ops` check the *result* of that move, but nothing
// checks that a later edit does not move the module back: re-adding a `StaticFuncTypeResolver`
// for a name the declaration already fixes, leaving the old signature file behind, or exporting
// a second copy of the signature from C++ all leave `--verify-ops` green, because the second
// copy agrees with the declaration. This script is the guard for those shapes.
//
// A module is a *declaration module* when `stdlib/<module>.cml` exists, its header says
// `module <module>`, and a C++ module of that name registers itself with `BuiltinModule`. The
// set is read from the tree, not from a list in this script: the next batch (`time`, `algo`,
// `statistics`, `tree`/`ckpt`) is covered the day its `.cml` lands.
//
// The four checks, for every declaration module:
//
//   1. C++ registers no `StaticFuncTypeResolver` for a name the declaration fixes. A name
//      whose signature C++ computes (`DynamicFuncTypeResolver`) may stay in C++: `math`'s
//      `sqrt`/`shl`/`shr` (recorded as `// dynamic func` in `stdlib/math.cml`), `random`'s
//      `choice`/`sample`/`shuffle` and `json`'s `encode`/`decode_as` (recorded in the
//      module's `computed:` signature file). This is "no static resolver for a declared
//      name", not "no resolver at all".
//
//   2. `stdlib/signatures/<module>.cml` does not exist unless the reference generator
//      (`scripts/stdlib-reference.mjs`) names it in that module's `computed:` field. A stray
//      copy is a second list of signatures that nothing reads (`--verify-ops` would keep
//      passing, because it reads the declaration and the computed file only). The generator's
//      table is read as text, and the declaration module must be listed there with
//      `declaration: true` and `source: '<module>.cml'`, or `--verify-ops` would never see it.
//
//   3. Every declaration has an operator behind it. This direction is already checked by
//      `node scripts/stdlib-reference.mjs --verify-ops` (every declaration names a registered
//      operator, every registered operator is declared, parameter counts and purity agree);
//      this script reuses it instead of growing a second copy of that comparison.
//
//   4. The module's export group is the declaration's. The C++ `exportEntity` calls may export
//      only the operators whose signature the declaration cannot express (the computed ones);
//      they may not export a declared name, and a computed entry with no exported group is a
//      signature the declaration does not back. `git grep -n exportEntity modules/<module>`
//      shows the calls; this check resolves the loops over `get*Groups()` so it also sees the
//      groups exported by `exportEntity(group->name(), group)`.
//
// The output is one line per problem, each naming the file, the module and the operator, and a
// final line starting `migrated modules stay migrated:`. Exit 1 when there is a problem.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const STDLIB_DIR = path.join(REPO_ROOT, 'stdlib')
const SIGNATURE_DIR = path.join(STDLIB_DIR, 'signatures')
const MODULES_DIR = path.join(REPO_ROOT, 'modules')
const REFERENCE = path.join(REPO_ROOT, 'scripts', 'stdlib-reference.mjs')

const relative = (file) => path.relative(REPO_ROOT, file).replace(/\\/g, '/')

function read(file) {
    return fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n')
}

// ---- a little C++ text scanning -------------------------------------------------------------

// Comments become spaces (newlines stay, so line numbers survive), string and character
// literals stay. The files are not parsed; a name that only appears in a comment is not a
// resolver, and parentheses inside literals or comments do not confuse the matching.
function stripComments(text) {
    const out = [...text]
    let state = 'code'
    let i = 0
    while (i < text.length) {
        const c = text[i]
        const n = text[i + 1]
        if (state === 'code') {
            if (c === '/' && n === '/') {
                state = 'line'
                out[i] = ' '
                out[i + 1] = ' '
                i += 2
                continue
            }
            if (c === '/' && n === '*') {
                state = 'block'
                out[i] = ' '
                out[i + 1] = ' '
                i += 2
                continue
            }
            if (c === '"') state = 'string'
            else if (c === "'") state = 'char'
            i += 1
            continue
        }
        if (state === 'line') {
            if (c === '\n') state = 'code'
            else out[i] = ' '
            i += 1
            continue
        }
        if (state === 'block') {
            if (c === '*' && n === '/') {
                out[i] = ' '
                out[i + 1] = ' '
                state = 'code'
                i += 2
                continue
            }
            if (c !== '\n') out[i] = ' '
            i += 1
            continue
        }
        // string or character literal
        if (c === '\\') {
            i += 2
            continue
        }
        if ((state === 'string' && c === '"') || (state === 'char' && c === "'")) state = 'code'
        i += 1
    }
    return out.join('')
}

function skipQuoted(text, at) {
    const quote = text[at]
    for (let i = at + 1; i < text.length; i++) {
        if (text[i] === '\\') {
            i += 1
            continue
        }
        if (text[i] === quote) return i
    }
    return text.length - 1
}

const CLOSING = { '(': ')', '[': ']', '{': '}' }

function matchDelimiter(text, open) {
    const close = CLOSING[text[open]]
    if (!close) return -1
    let depth = 0
    for (let i = open; i < text.length; i++) {
        const c = text[i]
        if (c === '"' || c === "'") {
            i = skipQuoted(text, i)
            continue
        }
        if (c === text[open]) depth += 1
        else if (c === close) {
            depth -= 1
            if (depth === 0) return i
        }
    }
    return -1
}

function lineAt(text, index) {
    let line = 1
    for (let i = 0; i < index && i < text.length; i++) if (text[i] === '\n') line += 1
    return line
}

function stringLiterals(text) {
    const out = []
    for (let i = 0; i < text.length; i++) {
        if (text[i] !== '"') continue
        const end = skipQuoted(text, i)
        out.push(text.slice(i + 1, end))
        i = end
    }
    return out
}

function listCppFiles(dir) {
    const files = []
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const file = path.join(dir, entry.name)
        if (entry.isDirectory()) files.push(...listCppFiles(file))
        else if (/\.(cpp|cc|h|hpp)$/.test(entry.name)) files.push(file)
    }
    return files
}

// ---- the reference generator's module table -------------------------------------------------

// The top-level `{ ... }` groups of a table body: the entries of `MODULES`, without the
// nested arrays an entry may hold (`only: [...]`).
function topLevelObjects(text) {
    const objects = []
    let depth = 0
    let start = -1
    for (let i = 0; i < text.length; i++) {
        const c = text[i]
        if (c === '"' || c === "'") {
            i = skipQuoted(text, i)
            continue
        }
        if (c === '{') {
            if (depth === 0) start = i
            depth += 1
        } else if (c === '}') {
            depth -= 1
            if (depth === 0 && start >= 0) {
                objects.push(text.slice(start, i + 1))
                start = -1
            }
        }
    }
    return objects
}

function field(object, name) {
    const match = object.match(new RegExp(`\\b${name}\\s*:\\s*'([^']*)'`))
    return match ? match[1] : null
}

function readReferenceModules() {
    const text = stripComments(read(REFERENCE))
    const start = text.indexOf('const MODULES = [')
    if (start < 0) throw new Error('const MODULES = [ not found in scripts/stdlib-reference.mjs')
    const open = text.indexOf('[', start)
    const close = matchDelimiter(text, open)
    if (close < 0) throw new Error('unbalanced MODULES array in scripts/stdlib-reference.mjs')
    return topLevelObjects(text.slice(open + 1, close))
        .map((object) => ({
            name: field(object, 'name'),
            kind: field(object, 'kind'),
            source: field(object, 'source'),
            declaration: /\bdeclaration\s*:\s*true\b/.test(object),
            computed: field(object, 'computed'),
        }))
        .filter((module) => module.name)
}

// ---- the tree's declaration modules ---------------------------------------------------------

function escapeRegExp(text) {
    return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
}

// A C++ module registers itself with `BuiltinModule("<name>", ...)` in its module.cpp (the
// files at the top of a module directory or in its src/). A module whose C++ side has gone
// away is a plain Camel module, not a declaration module.
function cppModuleDir(name) {
    const pattern = new RegExp(`\\bBuiltinModule\\s*\\(\\s*"${escapeRegExp(name)}"`)
    for (const entry of fs.readdirSync(MODULES_DIR, { withFileTypes: true })) {
        if (!entry.isDirectory()) continue
        const dir = path.join(MODULES_DIR, entry.name)
        for (const candidate of [path.join(dir, 'module.cpp'), path.join(dir, 'src', 'module.cpp')]) {
            if (!fs.existsSync(candidate)) continue
            if (pattern.test(stripComments(read(candidate)))) return dir
        }
    }
    return null
}

function declarationModules() {
    const modules = []
    for (const file of fs.readdirSync(STDLIB_DIR).sort()) {
        if (!file.endsWith('.cml')) continue
        const name = file.slice(0, -'.cml'.length)
        const dir = cppModuleDir(name)
        if (!dir) continue
        const source = path.join(STDLIB_DIR, file)
        const header = read(source).match(/^\s*module\s+([A-Za-z_]\w*)\s*$/m)
        // `stdlib/perf.cml` says `module util`: it is not the module named after its file.
        if (!header || header[1] !== name) continue
        modules.push({ name, cppDir: dir, source })
    }
    return modules
}

// The declarations of a file: each `#op<'uri'>` binds the next `func`, and a default URI is
// `<module>:<name>`. `// dynamic func` records a signature C++ computes (math); a body-less
// `func` is the same declaration without an export.
function readDeclarations(file, moduleName) {
    const lines = read(file).split('\n')
    const declarations = []
    const dynamic = []
    let pendingUri = null
    for (let i = 0; i < lines.length; i++) {
        const trimmed = lines[i].trim()
        const tag = trimmed.match(/^#\[?op<['"]([^'"]+)['"]>/)
        if (tag) {
            pendingUri = tag[1]
            continue
        }
        const dyn = trimmed.match(/^\/\/\s*dynamic func\s+([A-Za-z_]\w*)\s*\(/)
        if (dyn) {
            dynamic.push({ name: dyn[1], line: i + 1 })
            pendingUri = null
            continue
        }
        const declaration = trimmed.match(/^(?:export\s+)?func\s+([A-Za-z_]\w*)\s*\(/)
        if (declaration) {
            declarations.push({
                name: declaration[1],
                uri: pendingUri || `${moduleName}:${declaration[1]}`,
                line: i + 1,
            })
            pendingUri = null
        }
    }
    return { declarations, dynamic }
}

// The signatures C++ computes and a declaration cannot express: `// dynamic func` entries in
// the declaration file, and the declarations of the module's `computed:` signature file.
function computedSignatures(module, entry) {
    const result = new Map()
    const own = readDeclarations(module.source, module.name)
    for (const dynamic of own.dynamic) {
        result.set(dynamic.name, { file: relative(module.source), line: dynamic.line })
    }
    if (entry && entry.computed) {
        const file = path.join(STDLIB_DIR, entry.computed)
        if (fs.existsSync(file)) {
            for (const declaration of readDeclarations(file, module.name).declarations) {
                result.set(declaration.name, { file: relative(file), line: declaration.line })
            }
        }
    }
    return result
}

// ---- the C++ side ---------------------------------------------------------------------------

// The operator groups a module's C++ sources create, the groups it exports, and the functions
// a `for (... : get*Groups()) exportEntity(group->name(), group)` loop iterates. Comments are
// stripped first, so a name in a comment is not a resolver or an export.
function analyseCpp(dir) {
    const groups = []
    const exports = []
    const loops = []
    const factories = new Set()
    const loopFunctions = new Map()
    const files = listCppFiles(dir).map((file) => ({ file, text: stripComments(read(file)) }))

    for (const { file, text } of files) {
        const relativeFile = relative(file)
        const fileGroups = []
        const factoryRanges = []

        for (const match of text.matchAll(/\bOperatorGroup\s*::\s*create\s*\(/g)) {
            const open = match.index + match[0].length - 1
            const close = matchDelimiter(text, open)
            if (close < 0) continue
            const body = text.slice(open + 1, close)
            const name = body.match(/^\s*"([^"]*)"/)
            fileGroups.push({
                file: relativeFile,
                line: lineAt(text, open),
                pos: open,
                factory: null,
                name: name ? name[1] : null,
                uris: stringLiterals(body).filter((literal) => /^[A-Za-z_]\w*:[A-Za-z_]\w*$/.test(literal)),
                static: /\bStaticFuncTypeResolver\b/.test(body),
                dynamic: /\bDynamicFuncTypeResolver\b/.test(body),
            })
        }

        // `oper_group_ptr_t shift(const std::string &name)` creates the group whose name is its
        // argument; a loop that calls `shift("shl")` therefore exports `shl`.
        for (const match of text.matchAll(/\b([A-Za-z_]\w*)\s*\([^)]*\)\s*\{/g)) {
            const open = match.index + match[0].length - 1
            const close = matchDelimiter(text, open)
            if (close < 0) continue
            const body = text.slice(open + 1, close)
            if (!/\bOperatorGroup\s*::\s*create\s*\(/.test(body)) continue
            factories.add(match[1])
            factoryRanges.push({ name: match[1], start: match.index, end: close })
        }
        for (const group of fileGroups) {
            const range = factoryRanges.find((each) => group.pos >= each.start && group.pos <= each.end)
            if (range) group.factory = range.name
        }
        groups.push(...fileGroups)

        for (const match of text.matchAll(/\b([A-Za-z_]\w*)\s*\(\s*\)\s*\{/g)) {
            const open = match.index + match[0].length - 1
            const close = matchDelimiter(text, open)
            if (close < 0) continue
            loopFunctions.set(match[1], text.slice(open + 1, close))
        }

        for (const match of text.matchAll(/\bexportEntity\s*\(/g)) {
            const open = match.index + match[0].length - 1
            const close = matchDelimiter(text, open)
            if (close < 0) continue
            const body = text.slice(open + 1, close)
            const literal = body.match(/^\s*"([^"]*)"/)
            const reference = body.match(/^\s*Reference\s*\(\s*"([^"]*)"\s*\)/)
            const loop = body.match(/^\s*([A-Za-z_]\w*)\s*->\s*name\s*\(\s*\)/)
            exports.push({
                file: relativeFile,
                line: lineAt(text, open),
                name: literal ? literal[1] : reference ? reference[1] : null,
                loopVariable: loop ? loop[1] : null,
            })
        }

        for (const match of text.matchAll(
            /\bfor\s*\([^)]*?\b([A-Za-z_]\w*)\s*:\s*([A-Za-z_]\w*)\s*\(\s*\)\s*\)\s*\{?\s*exportEntity\s*\(\s*\1\s*->\s*name\s*\(\s*\)\s*,\s*\1\s*\)/g,
        )) {
            loops.push({ file: relativeFile, line: lineAt(text, match.index), fn: match[2] })
        }
    }

    // Calls such as shift("shl") name the groups a factory creates; they attribute a resolver
    // that lives in the factory to every operator the module instantiates it for.
    const factoryCalls = new Map()
    for (const name of factories) factoryCalls.set(name, [])
    for (const { text } of files) {
        for (const match of text.matchAll(/\b([A-Za-z_]\w*)\s*\(\s*"([^"]*)"\s*\)/g)) {
            if (match[1] === 'create' || match[1] === 'Reference') continue
            const calls = factoryCalls.get(match[1])
            if (calls) calls.push(match[2])
        }
    }

    return { groups, exports, loops, factories, factoryCalls, loopFunctions }
}

// The group names a `for (... : getOperatorGroups())` loop exports, or null when the names are
// computed (a group created with a variable name) and this checker cannot see them.
function loopGroupNames(loop, analysis) {
    const body = analysis.loopFunctions.get(loop.fn)
    if (body === undefined) return null
    const names = []
    for (const match of body.matchAll(/\bOperatorGroup\s*::\s*create\s*\(/g)) {
        const open = match.index + match[0].length - 1
        const close = matchDelimiter(body, open)
        if (close < 0) return null
        const name = body.slice(open + 1, close).match(/^\s*"([^"]*)"/)
        if (!name) return null
        names.push(name[1])
    }
    for (const match of body.matchAll(/\b([A-Za-z_]\w*)\s*\(\s*"([^"]*)"\s*\)/g)) {
        if (match[1] === 'create' || match[1] === 'Reference') continue
        if (analysis.factories.has(match[1])) names.push(match[2])
    }
    return names
}

// ---- the checks -----------------------------------------------------------------------------

function staticChecks() {
    const problems = []
    const reference = readReferenceModules()
    const modules = declarationModules()
    let computedCount = 0

    for (const module of modules) {
        const entry = reference.find((each) => each.name === module.name)
        const source = relative(module.source)
        const declared = readDeclarations(module.source, module.name).declarations
        const declaredNames = new Set(declared.map((declaration) => declaration.name))
        const declaredUris = new Set(declared.map((declaration) => declaration.uri))
        const computed = computedSignatures(module, entry)
        computedCount += computed.size

        // The reference generator must see the module, or --verify-ops never checks it.
        if (!entry) {
            problems.push(`${source}: '${module.name}' is a declaration module but MODULES in scripts/stdlib-reference.mjs does not list it`)
        } else {
            if (!entry.declaration) {
                problems.push(`${source}: '${module.name}' is a declaration module but its MODULES entry is not declaration: true`)
            }
            if (entry.source !== `${module.name}.cml`) {
                problems.push(`${source}: the MODULES entry of '${module.name}' reads '${entry.source ?? '(none)'}', not '${module.name}.cml'`)
            }
            if (entry.computed && entry.computed !== `signatures/${module.name}.cml`) {
                problems.push(`${source}: the MODULES entry of '${module.name}' names computed: '${entry.computed}', not 'signatures/${module.name}.cml'`)
            }
        }

        // (2) a signature file survives only when the reference generator names it as computed.
        const signatureFile = path.join(SIGNATURE_DIR, `${module.name}.cml`)
        const namesSignatureFile = entry && entry.computed === `signatures/${module.name}.cml`
        if (fs.existsSync(signatureFile) && !namesSignatureFile) {
            problems.push(
                `${relative(signatureFile)}: the migrated module '${module.name}' still has a signature file; ` +
                    `the reference generator's computed: field does not name it (its declarations are in ${source})`,
            )
        }
        if (!fs.existsSync(signatureFile) && namesSignatureFile) {
            problems.push(`${relative(signatureFile)}: the reference generator names it as the computed signatures of '${module.name}', but the file does not exist`)
        }

        // One signature, one place: a name cannot be both declared and computed.
        for (const name of declaredNames) {
            if (computed.has(name)) {
                const where = computed.get(name)
                problems.push(`${where.file}:${where.line}: '${name}' is a computed signature of '${module.name}' and is also declared in ${source}`)
            }
        }

        const analysis = analyseCpp(module.cppDir)

        // (1) no StaticFuncTypeResolver for a name the declaration fixes.
        for (const group of analysis.groups.filter((each) => each.static)) {
            const candidates = []
            if (group.name) candidates.push(group.name)
            if (group.factory) candidates.push(...(analysis.factoryCalls.get(group.factory) || []))
            for (const uri of group.uris) candidates.push(uri.slice(uri.indexOf(':') + 1))
            const declaredHit = candidates.find((name) => declaredNames.has(name))
            const uriHit = group.uris.find((uri) => declaredUris.has(uri))
            if (declaredHit || uriHit) {
                const name = declaredHit || uriHit.slice(uri.indexOf(':') + 1)
                problems.push(
                    `${group.file}:${group.line}: the migrated module '${module.name}' has a StaticFuncTypeResolver ` +
                        `for '${name}', but ${source} already declares it`,
                )
            } else {
                const label =
                    [...new Set(candidates)].join(', ') || 'an operator this checker cannot name'
                problems.push(
                    `${group.file}:${group.line}: the migrated module '${module.name}' has a StaticFuncTypeResolver ` +
                        `for ${label}, but neither the declarations nor a computed signature account for it`,
                )
            }
        }

        // (4) the export group is the declaration's: C++ may export only computed names.
        const exported = new Set()
        for (const exportedCall of analysis.exports) {
            if (exportedCall.name) exported.add(exportedCall.name)
        }
        for (const loop of analysis.loops) {
            const names = loopGroupNames(loop, analysis)
            if (names === null) {
                problems.push(
                    `${loop.file}:${loop.line}: cannot resolve the operator groups the loop over ${loop.fn}() exports; ` +
                        `give the groups literal names so the export group can be checked against '${module.name}'`,
                )
                continue
            }
            for (const name of names) exported.add(name)
        }
        for (const exportedCall of analysis.exports) {
            if (!exportedCall.name && !exportedCall.loopVariable) {
                problems.push(`${exportedCall.file}:${exportedCall.line}: the migrated module '${module.name}' exports a group this checker cannot name`)
            }
        }

        for (const name of exported) {
            if (declaredNames.has(name)) {
                const call = analysis.exports.find((each) => each.name === name) || analysis.loops[0]
                problems.push(
                    `${call ? `${call.file}:${call.line}: ` : ''}the migrated module '${module.name}' exports '${name}' ` +
                        `from C++, but ${source} already declares it`,
                )
            } else if (!computed.has(name)) {
                const call = analysis.exports.find((each) => each.name === name) || analysis.loops[0]
                problems.push(
                    `${call ? `${call.file}:${call.line}: ` : ''}the migrated module '${module.name}' exports '${name}' ` +
                        `from C++, but no computed signature records it`,
                )
            }
        }
        for (const [name, where] of computed) {
            if (!exported.has(name)) {
                problems.push(`${where.file}:${where.line}: the computed signature '${name}' of '${module.name}' has no operator group exported by C++`)
            }
        }
    }

    return { problems, modules, computedCount }
}

// (3) Reused, not reimplemented: --verify-ops already checks both directions between the
// declarations and the registered operators, their parameter counts and their purity.
function verifyOps() {
    const run = spawnSync(process.execPath, [REFERENCE, '--verify-ops'], {
        encoding: 'utf8',
        maxBuffer: 1 << 26,
    })
    if (run.error) {
        return { ok: false, lines: [`could not run node scripts/stdlib-reference.mjs --verify-ops: ${run.error.message}`] }
    }
    if (run.status !== 0) {
        const output = `${run.stdout || ''}${run.stderr || ''}`.trim()
        const lines = output.split('\n').filter(Boolean).map((line) => `--verify-ops: ${line}`)
        return { ok: false, lines }
    }
    return { ok: true, summary: (run.stdout || '').trim().split('\n').filter(Boolean).pop() || '' }
}

function main() {
    const mode = process.argv[2] || '--check'
    if (mode !== '--check') {
        console.log('usage: node scripts/stdlib-declarations.mjs --check')
        process.exit(2)
    }

    const { problems, modules, computedCount } = staticChecks()
    const ops = verifyOps()
    if (!ops.ok) problems.push(...ops.lines)
    for (const problem of problems) console.log(problem)

    if (problems.length > 0) {
        console.log(
            `migrated modules stay migrated: ${problems.length} problem(s) across ${modules.length} declaration module(s)`,
        )
        process.exit(1)
    }
    console.log(
        `migrated modules stay migrated: ${modules.length} declaration module(s), ${computedCount} computed signature(s) ` +
            `left in C++, 0 problem(s)`,
    )
    if (ops.summary) console.log(`every declaration is behind a registered operator (${ops.summary})`)
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    main()
}

export { staticChecks, verifyOps, declarationModules, readDeclarations, readReferenceModules }
#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A module that has moved to a declaration module stays migrated (handover H137), and the
// computed signatures a declaration cannot express have one recorded place (handover H180).
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
// The checks, for every declaration module:
//
//   1. C++ registers no `StaticFuncTypeResolver` for a name the declaration fixes. A name
//      whose signature C++ computes (`DynamicFuncTypeResolver`) may stay in C++: this is
//      "no static resolver for a declared name", not "no resolver at all". The computed
//      names themselves are recorded in the module's `computed:` signature file (check 5).
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
// And one check for the registry as a whole:
//
//   5. Every C++ module of `modules/` that registers operators is either imported by
//      `registry_all.cml` — the program `--verify-ops` reads — and listed in MODULES, or named
//      in UNCHECKED_MODULES with a reason and a probe that imports it. A module that registers
//      operators and is in neither place is a hole: `--verify-ops` never sees it and every check
//      stays green (handover H166 found `python`/`pyplot` that way; H173 found `sim`, which
//      registry_all imported while MODULES did not list it, and the four modules no program
//      imported at all). A module imported but absent from MODULES is the same hole and is
//      reported too.
//
// And one for the notation:
//
//   6. A computed name has exactly one place: `stdlib/signatures/<module>.cml`, named by the
//      MODULES entry's `computed:` field. The inline `// dynamic func` form is retired (H180):
//      it cannot carry an `#op` URI, so a computed name whose operator URI differs from
//      `<module>:<name>` has no inline spelling. An inline record is a problem even when the
//      name is also in the file (the pointer and the file are then two places); a module whose
//      `computed:` field names a file that records no name is a pointer that does not agree
//      with reality. A name recorded in neither place is caught by the exported-name loop of
//      check 4 ("no computed signature records it").
//
// The reserved groups are the exception to (1) and (4): `__neq__` because `!=` cannot be declared
// (design principles 4.3 — an overload of `==` gives it, and `operator !=` is
// OperatorNotOverloadable), and the compiler negates a *Camel* overload of `==`, not a native
// one, so a module whose types have their own `==` keeps the `__neq__` group in C++. Rewrite's
// `__rule`, `__rule_when` and `__analysis` are the compiled form of the language's `rule` and
// `analysis` declarations, so they are exported by C++ as well (handover H183). None of them has a
// Camel name: neither the declaration file nor the reference page carries it, and `--verify-ops`
// filters reserved names out. Every other reserved group stays checked.
// Two shapes that the tensor module writes are resolved rather than reported:
//
//   - A module that exports the groups of its operator registry
//     (`for (g : OpRegistry::instance().operatorGroups("<module>"))`, `modules/tensor` and
//     `modules/nn`, where the registry is reached through `camel::tensor::ops::`). The names are
//     the OpDef table, so the check reads the C++ skip list (`kDeclaredOperators`) and requires
//     it to be exactly the declaration names, both ways; the loop then exports exactly the
//     computed signatures, which `--verify-ops` proves is the rest of the registered operators.
//     The loop must skip the declared names. A module that publishes a type-qualified alias must
//     publish it for every registry operator, i.e. from this loop; a module that publishes no
//     alias at all, like `nn`, need not invent one.
//
//   - A type-qualified alias (`exportEntity(Reference({"Tensor", name}), group)`). A
//     declaration has no syntax for `Tensor.shape`, so C++ publishes it, and the compiler
//     points it at the declaration when the declaration replaces the module's entity
//     (`Module::replaceTypeQualifiedAliases`, handover H151). This is not a second signature:
//     `--verify-ops` requires the alias to resolve to the same operator as the plain name
//     (same URI, same parameter counts, which a stale C++ group would not report).
//
// A loop over a group factory (`for (g : getOperatorGroups())`, also when the call is qualified)
// is resolved by reading the `OperatorGroup::create` calls in the factory's body. A module may
// have both shapes: `nn` exports its tensor operators from the registry and its optimizers from
// a literal group list.
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

// The reserved groups a migrated module may keep outside its declarations: see the note above.
const UNDERIVABLE_OPERATORS = new Set(['__neq__', '__rule', '__rule_when', '__analysis'])

// Modules that register operators but are deliberately not verified, each with the fact that
// makes the exclusion true. `registry_all.cml` is the program `--verify-ops` reads, so a module
// it does not import is not verified; an entry here must (a) have no declaration file
// `stdlib/<module>.cml` and no `stdlib/signatures/<module>.cml`, so there is no signature source
// being ignored, and (b) name a `covered_by` program that imports it, so `inspect --ops` still
// lists its operators and another plan still checks their purity.
//
// `nn`, `autodiff` and `onnx` are the tensor stack's layers. Their signatures are computed by
// the tensor OpRegistry and the C++ resolvers (H157 inventories them name by name); the
// reference page excludes the tensor stack by design, and writing a signature file for them
// would be the declaration migration the H19 ledger assigns, not coverage. Their purity is
// checked by `registry.purity_declared_tensor_stack` over `registry_tensor.cml`.
//
// Empty since H179 declared `onnx` (`supported_operators` in stdlib/onnx.cml, `export_model` in
// stdlib/signatures/onnx.cml): every module that registers operators is now in the probe and in
// MODULES. The list stays, with its checks, so an exclusion has to be justified the same way.
const UNCHECKED_MODULES = []

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
            registered: field(object, 'registered'),
            declaration: /\bdeclaration\s*:\s*true\b/.test(object),
            computed: field(object, 'computed'),
        }))
        .filter((module) => module.name)
}

// ---- the tree's declaration modules ---------------------------------------------------------

// The name a module directory registers itself with, or null when its C++ side has no
// `BuiltinModule` (a plain Camel module). Only `module.cpp` and `src/module.cpp` are read: a
// `BuiltinModule` in some other file is not the module's registration.
function builtinModuleName(dir) {
    const pattern = /\bBuiltinModule\s*\(\s*"([^"]+)"/
    for (const candidate of [path.join(dir, 'module.cpp'), path.join(dir, 'src', 'module.cpp')]) {
        if (!fs.existsSync(candidate)) continue
        const match = stripComments(read(candidate)).match(pattern)
        if (match) return match[1]
    }
    return null
}

// A C++ module registers itself with `BuiltinModule("<name>", ...)` in its module.cpp (the
// files at the top of a module directory or in its src/). A module whose C++ side has gone
// away is a plain Camel module, not a declaration module.
function cppModuleDir(name) {
    for (const entry of fs.readdirSync(MODULES_DIR, { withFileTypes: true })) {
        if (!entry.isDirectory()) continue
        const dir = path.join(MODULES_DIR, entry.name)
        if (builtinModuleName(dir) === name) return dir
    }
    return null
}

// Every module of `modules/` that registers operators: a declaration module registers them
// through `stdlib/<module>.cml`, and any other module through the operator groups its C++
// exports (including the registry loop `tensor` writes). `io` is the shape that used to look
// empty: it has no `exportEntity` but five declarations, so it is an operator module too.
function operatorModules() {
    const found = new Map()
    for (const module of declarationModules()) {
        const declared = readDeclarations(module.source, module.name).declarations
        if (declared.length > 0) found.set(module.name, module.cppDir)
    }
    for (const entry of fs.readdirSync(MODULES_DIR, { withFileTypes: true })) {
        if (!entry.isDirectory()) continue
        const dir = path.join(MODULES_DIR, entry.name)
        const name = builtinModuleName(dir)
        if (!name || found.has(name)) continue
        const analysis = analyseCpp(dir)
        if (analysis.exports.length === 0 && analysis.registryLoops.length === 0) continue
        found.set(name, dir)
    }
    return [...found.entries()].map(([name, dir]) => ({ name, dir }))
}

// The modules a Camel program imports. The reference probe is the only caller that matters, but
// an exclusion names its own probe, so this takes the file.
const IMPORT_RE =
    /^[ \t]*import[ \t]+(?:[A-Za-z_]\w*[ \t]+as[ \t]+[A-Za-z_]\w*[ \t]+from[ \t]+|\{[^}]*\}[ \t]+from[ \t]+|\*[ \t]+as[ \t]+[A-Za-z_]\w*[ \t]+from[ \t]+)?([A-Za-z_]\w*)/gm

function probeImportsOf(file) {
    const names = new Set()
    for (const match of stripComments(read(file)).matchAll(IMPORT_RE)) names.add(match[1])
    return names
}

// (5) The registry is complete: every module that registers operators is either verified (the
// reference probe imports it and MODULES lists it) or excluded with a reason that this check
// proves. Read from the tree, so a new module cannot join the build without naming itself.
function coverageProblems(reference) {
    const problems = []
    const probe = path.join(REPO_ROOT, 'test', 'cases', 'modules', 'stdlib', 'registry_all.cml')
    const imported = probeImportsOf(probe)
    const operators = operatorModules()
    const operatorNames = new Set(operators.map((module) => module.name))
    const excluded = new Map(UNCHECKED_MODULES.map((entry) => [entry.name, entry]))
    const inReference = (name) => reference.find((module) => module.name === name && module.kind === 'cpp')
    const where = (module) => `modules/${path.basename(module.dir)}`

    for (const module of operators) {
        const entry = inReference(module.name)
        const exclusion = excluded.get(module.name)
        const inProbe = imported.has(module.name)
        if (inProbe && !entry) {
            problems.push(
                `${where(module)}: '${module.name}' registers operators and registry_all.cml imports it, ` +
                    `but MODULES in scripts/stdlib-reference.mjs does not list it, so --verify-ops never ` +
                    `checks it (add the module to the reference, or drop the import and exclude it)`,
            )
        }
        if (inProbe && exclusion) {
            problems.push(
                `${where(module)}: '${module.name}' is in UNCHECKED_MODULES but registry_all.cml imports it; ` +
                    `a module that is in the probe must be verified, not excluded`,
            )
        }
        if (!inProbe && !exclusion) {
            problems.push(
                `${where(module)}: '${module.name}' registers operators but registry_all.cml does not import ` +
                    `it and UNCHECKED_MODULES in scripts/stdlib-declarations.mjs does not exclude it with a ` +
                    `reason; --verify-ops never sees it`,
            )
        }
        if (entry && !inProbe) {
            problems.push(
                `${where(module)}: MODULES lists '${module.name}' but registry_all.cml does not import it, so ` +
                    `--verify-ops cannot see the module it is supposed to check`,
            )
        }
    }

    for (const module of reference) {
        if (module.kind !== 'cpp' || module.registered === '<builtin>') continue
        if (!operatorNames.has(module.name)) {
            problems.push(
                `scripts/stdlib-reference.mjs: MODULES lists '${module.name}', but no module of modules/ ` +
                    `registers operators under that name`,
            )
        }
    }

    for (const entry of UNCHECKED_MODULES) {
        if (!entry.reason || entry.reason.trim().length < 40) {
            problems.push(
                `scripts/stdlib-declarations.mjs: UNCHECKED_MODULES names '${entry.name}' without a reason`,
            )
        }
        if (!operatorNames.has(entry.name)) {
            problems.push(
                `scripts/stdlib-declarations.mjs: UNCHECKED_MODULES names '${entry.name}', which registers no ` +
                    `operators`,
            )
        }
        if (inReference(entry.name)) {
            problems.push(
                `scripts/stdlib-declarations.mjs: '${entry.name}' is excluded but MODULES lists it; checked ` +
                    `modules must not stay in the exclusion list`,
            )
        }
        if (imported.has(entry.name)) {
            problems.push(
                `scripts/stdlib-declarations.mjs: '${entry.name}' is excluded but registry_all.cml imports it`,
            )
        }
        for (const file of [
            path.join(STDLIB_DIR, `${entry.name}.cml`),
            path.join(SIGNATURE_DIR, `${entry.name}.cml`),
        ]) {
            if (fs.existsSync(file)) {
                problems.push(
                    `${relative(file)}: '${entry.name}' is excluded, but this signature source exists; check ` +
                        `the module instead of excluding it`,
                )
            }
        }
        if (!entry.covered_by) {
            problems.push(
                `scripts/stdlib-declarations.mjs: UNCHECKED_MODULES names '${entry.name}' without covered_by; ` +
                    `an exclusion must say which probe still imports and lists the module`,
            )
            continue
        }
        const coveredBy = path.join(REPO_ROOT, entry.covered_by)
        if (!fs.existsSync(coveredBy)) {
            problems.push(
                `scripts/stdlib-declarations.mjs: the covered_by program '${entry.covered_by}' of '${entry.name}' ` +
                    `does not exist`,
            )
        } else if (!probeImportsOf(coveredBy).has(entry.name)) {
            problems.push(
                `${entry.covered_by}: it does not import '${entry.name}', so the exclusion of '${entry.name}' ` +
                    `names a probe that does not cover it`,
            )
        }
    }

    return problems
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
        const declaration = trimmed.match(/^(?:export\s+)?func\s+([A-Za-z_]\w*)\s*(?:::<[^>]*>)?\s*\(/)
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

// The computed signatures of a module, with the place each name is recorded in. A computed name
// has exactly one such place (H180):
//
//   - `stdlib/signatures/<module>.cml`, named by the module's `computed:` field. This is the
//     invariant; it is the only place a computed name is recorded.
//   - an inline `// dynamic func` in the declaration file. This is the retired convention: it
//     cannot carry an `#op` URI (a computed name whose operator URI differs from `<module>:<name>`
//     has no inline spelling), so the check reports every inline record instead of accepting it.
//
// `inline` and `file` are maps from name to the first record, for the problem messages; `all` is
// the union, which keeps `--verify-ops`'s "every computed name has an exported group" check going
// while a wrong-place record is being reported.
function computedPlaces(module, entry) {
    const own = readDeclarations(module.source, module.name)
    const inline = new Map()
    for (const dynamic of own.dynamic) {
        if (!inline.has(dynamic.name)) {
            inline.set(dynamic.name, { file: relative(module.source), line: dynamic.line })
        }
    }
    const file = new Map()
    let filePath = null
    if (entry && entry.computed) {
        filePath = path.join(STDLIB_DIR, entry.computed)
        if (fs.existsSync(filePath)) {
            for (const declaration of readDeclarations(filePath, module.name).declarations) {
                if (!file.has(declaration.name)) {
                    file.set(declaration.name, {
                        file: relative(filePath),
                        line: declaration.line,
                    })
                }
            }
        }
    }
    const all = new Map([...inline, ...file])
    return { inline, file, all, filePath }
}

// ---- the C++ side ---------------------------------------------------------------------------

// The operator groups a module's C++ sources create, the groups it exports, and the functions
// a `for (... : get*Groups()) exportEntity(group->name(), group)` loop iterates. Comments are
// stripped first, so a name in a comment is not a resolver or an export.
//
// Two more shapes are read, both written by `modules/tensor`:
// - a loop over the module's operator registry
//   (`for (g : OpRegistry::instance().operatorGroups("<module>"))`), whose groups are the OpDef
//   table rather than `OperatorGroup::create` calls; the names it exports are the module's
//   operators minus the skip list `kDeclaredOperators`, and --verify-ops proves that set is the
//   declarations plus the computed signatures;
// - a type-qualified alias export (`exportEntity(Reference({"Tensor", leaf}), group)`), which
//   publishes an operator under a type namespace. A declaration cannot write that name; the
//   compiler points the alias at the declaration when it replaces the module's entity
//   (Module::replaceTypeQualifiedAliases), and --verify-ops checks that every alias resolves to
//   the same operator as the plain name.
function analyseCpp(dir) {
    const groups = []
    const exports = []
    const loops = []
    const registryLoops = []
    const declaredList = []
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
            // `Reference(std::vector<std::string>{"Tensor"}, ...)`: the same operator under a
            // type namespace (see the comment above).
            const alias = body.match(
                /^\s*Reference\s*\(\s*(?:std\s*::\s*)?vector\s*<\s*(?:std\s*::\s*)?string\s*>\s*\{\s*"([^"]+)"\s*\}\s*,\s*(?:"([^"]+)"|([A-Za-z_]\w*)\s*->\s*name\s*\(\s*\))\s*\)/,
            )
            exports.push({
                file: relativeFile,
                line: lineAt(text, open),
                name: literal ? literal[1] : reference ? reference[1] : null,
                loopVariable: loop ? loop[1] : null,
                alias: alias ? { prefix: alias[1], leaf: alias[2] || null } : null,
            })
        }

        // The skip list of a module that exports its operator registry: the names whose groups
        // the module does not export because `stdlib/<module>.cml` declares them.
        for (const match of text.matchAll(
            /\bconstexpr\s+(?:std\s*::\s*)?string_view\s+kDeclaredOperators\s*\[\s*\]\s*=\s*\{([^}]*)\}/g,
        )) {
            for (const name of match[1].matchAll(/"([^"]+)"/g)) declaredList.push(name[1])
        }

        // `for (g : OpRegistry::instance().operatorGroups("<module>"))`: the group names are the
        // module's operator table, which no text can list; the loop's body says whether the
        // declared names are skipped and whether the type-qualified alias is exported. The
        // registry may be reached through a namespace (`camel::tensor::ops::OpRegistry`, nn).
        for (const match of text.matchAll(
            /\bfor\s*\([^)]*?\b([A-Za-z_]\w*)\s*:\s*(?:(?:[A-Za-z_]\w*)\s*::\s*)*OpRegistry\s*::\s*instance\s*\(\s*\)\s*\.\s*operatorGroups\s*\(\s*"([^"]+)"\s*\)\s*\)/g,
        )) {
            const brace = text.indexOf('{', match.index + match[0].length)
            const body = brace < 0 ? '' : text.slice(brace, matchDelimiter(text, brace) + 1)
            registryLoops.push({
                file: relativeFile,
                line: lineAt(text, match.index),
                variable: match[1],
                module: match[2],
                skipsDeclared: /\bisDeclaredOperators?\s*\(/.test(body),
                alias: new RegExp(
                    `Reference\\s*\\(\\s*(?:std\\s*::\\s*)?vector\\s*<\\s*(?:std\\s*::\\s*)?string\\s*>\\s*\\{\\s*"[^"]+"\\s*\\}\\s*,\\s*${match[1]}\\s*->\\s*name\\s*\\(\\s*\\)\\s*\\)`,
                ).test(body),
            })
        }

        for (const match of text.matchAll(
            /\bfor\s*\([^)]*?\b([A-Za-z_]\w*)\s*:\s*(?:(?:[A-Za-z_]\w*)\s*::\s*)*([A-Za-z_]\w*)\s*\(\s*\)\s*\)\s*\{?\s*exportEntity\s*\(\s*\1\s*->\s*name\s*\(\s*\)\s*,\s*\1\s*\)/g,
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

    return {
        groups,
        exports,
        loops,
        registryLoops,
        declaredList,
        factories,
        factoryCalls,
        loopFunctions,
    }
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

    // Declared modules derive from `stdlib/<module>.cml` + the C++ module's `BuiltinModule`; if
    // that derivation finds nothing, every loop below passes over nothing.
    if (modules.length === 0) {
        problems.push(
            'no declaration module under stdlib/: no `<module>.cml` matches a C++ `BuiltinModule`; ' +
                'the migration this check proves cannot be empty, so its derivation is broken',
        )
    }

    for (const module of modules) {
        const entry = reference.find((each) => each.name === module.name)
        const source = relative(module.source)
        const declared = readDeclarations(module.source, module.name).declarations
        const declaredNames = new Set(declared.map((declaration) => declaration.name))
        const declaredUris = new Set(declared.map((declaration) => declaration.uri))
        const places = computedPlaces(module, entry)
        const computed = places.all
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

        // (5) The computed half has one convention and one place (handover H180). It is the
        // module's signature file, named by `computed:`. An inline `// dynamic func` cannot carry
        // an `#op` URI, so it cannot record every computed name; it is not an alternative.
        for (const [name, where] of places.inline) {
            const also = places.file.get(name)
            problems.push(
                `${where.file}:${where.line}: the computed signature '${name}' of '${module.name}' is recorded inline as ` +
                    `'// dynamic func'${also ? ` and in ${also.file}:${also.line}` : ''}; a computed name has exactly one ` +
                    `place: stdlib/signatures/${module.name}.cml, named by the MODULES entry's computed: field (handover H180)`,
            )
        }
        // The pointer must agree with the file: a module whose MODULES entry names a computed
        // file has to record at least one computed name there. (A missing file is check 2.)
        if (entry && entry.computed && places.filePath && fs.existsSync(places.filePath) && places.file.size === 0) {
            problems.push(
                `${relative(places.filePath)}: the MODULES entry names it as the computed signatures of '${module.name}', ` +
                    `but it records no computed name`,
            )
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
            if (UNDERIVABLE_OPERATORS.has(group.name)) continue
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

        // (4) the export group is the declaration's: C++ may export only computed names, and a
        // type-qualified alias (`Tensor.shape`) is not an export group of its own.
        const exported = new Set()
        for (const exportedCall of analysis.exports) {
            if (exportedCall.alias) continue
            if (exportedCall.name && !UNDERIVABLE_OPERATORS.has(exportedCall.name)) {
                exported.add(exportedCall.name)
            }
        }
        const registryLoops = analysis.registryLoops.filter(
            (loop) => loop.module === module.name,
        )
        if (analysis.registryLoops.length > registryLoops.length) {
            for (const loop of analysis.registryLoops.filter(
                (each) => each.module !== module.name,
            )) {
                problems.push(
                    `${loop.file}:${loop.line}: the migrated module '${module.name}' exports the operator ` +
                        `registry of '${loop.module}', not its own`,
                )
            }
        }
        // A loop over a group factory names the groups it exports by reading the factory's
        // `OperatorGroup::create` calls, whether or not the module also exports a registry (`nn`
        // has one of each).
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
        // Whether the module publishes type-qualified aliases at all: `tensor` publishes
        // `Tensor.<name>` for every operator, while `nn` publishes plain names only.
        const publishesAliases = analysis.exports.some((exportedCall) => exportedCall.alias !== null)
        if (registryLoops.length > 0) {
            // The module exports the groups of its OpDef table: the names are not written in
            // C++, so the set is the declarations plus the computed signatures (--verify-ops
            // proves the registered operators are exactly that set), and the C++ skip list must
            // be the declarations, both ways. A name that is not in the skip list is exported by
            // the loop, so it may not be declared; a declared name that is missing from the skip
            // list would be exported beside its declaration.
            if (analysis.declaredList.length === 0) {
                problems.push(
                    `${registryLoops[0].file}:${registryLoops[0].line}: the loop over the operator registry exports ` +
                        `every group; give it a kDeclaredOperators skip list so the declaration-only names are checked`,
                )
            }
            for (const loop of registryLoops) {
                if (publishesAliases && !loop.alias) {
                    problems.push(
                        `${loop.file}:${loop.line}: the loop over the operator registry does not export the ` +
                            `type-qualified alias of a name; a module that publishes 'Type.name' must publish it ` +
                            `for every operator (handover H151)`,
                    )
                }
                if (!loop.skipsDeclared) {
                    problems.push(
                        `${loop.file}:${loop.line}: the loop over the operator registry does not skip the declared ` +
                            `names (isDeclaredOperators); a declared operator would be exported beside its declaration`,
                    )
                }
            }
            const skipped = new Set(analysis.declaredList)
            for (const name of declaredNames) {
                if (!skipped.has(name)) {
                    problems.push(
                        `${source}: '${name}' is declared, but kDeclaredOperators in C++ does not skip it`,
                    )
                }
            }
            for (const name of skipped) {
                if (!declaredNames.has(name)) {
                    problems.push(
                        `kDeclaredOperators skips '${name}', but ${source} does not declare it`,
                    )
                }
            }
            for (const name of computed.keys()) exported.add(name)
        }
        for (const exportedCall of analysis.exports) {
            if (exportedCall.alias || exportedCall.name || exportedCall.loopVariable) continue
            problems.push(`${exportedCall.file}:${exportedCall.line}: the migrated module '${module.name}' exports a group this checker cannot name`)
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

    problems.push(...coverageProblems(reference))


    return { problems, modules, computedCount, excludedCount: UNCHECKED_MODULES.length }
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
    const args = process.argv.slice(2)
    const mode = args[0] || '--check'
    if (args.length > 1) {
        console.log(`--check: unknown argument "${args[1]}"`)
        console.log('usage: node scripts/stdlib-declarations.mjs --check')
        process.exit(2)
    }
    if (mode !== '--check') {
        console.log('usage: node scripts/stdlib-declarations.mjs --check')
        process.exit(2)
    }

    const { problems, modules, computedCount, excludedCount } = staticChecks()
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
            `left in C++, ${excludedCount} excluded module(s), 0 problem(s)`,
    )
    if (ops.summary) console.log(`every declaration is behind a registered operator (${ops.summary})`)
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    main()
}

export { staticChecks, verifyOps, declarationModules, readDeclarations, readReferenceModules }

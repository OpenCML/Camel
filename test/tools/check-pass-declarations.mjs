#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A pass is a function of the standard library (design principles 9.3): its name,
 * documentation and parameters are one body-less declaration in the module's `.cml` file, and
 * the pipeline resolves it by module and name. This guard is what keeps the second declaration
 * from coming back:
 *
 *   * no `declarePassParams`, `initPassScope`, `PassScope`, `registerPass(`, `corePasses(`,
 *     `findCorePass` or `modulePasses(` in the sources: those are the C++ pass registry and its
 *     parameter strings;
 *   * no parameter declaration written as a string literal
 *     (`"safe_fp: bool = false, ..."`) outside `stdlib/`, where the declaration belongs;
 *   * every pass declared in `stdlib/std.cml` has exactly one kernel registered with
 *     `registerCoreKernel`, and every kernel is declared: a bijection, stated as such rather than
 *     as a count. A count moves with every pass the language gains while the property it was
 *      meant to guard does not, which is why the rule that wrote the declarations down asks for
 *     the relation and not the number; what the two sets must not be is empty, and that lower
 *     bound is asserted so a run that examined nothing fails here instead of passing.
 *   * every pass a module contributes is a pass-shaped declaration in `stdlib/<module>.cml`
 *     (`when is_function::<F>()`), and every declaration has a registration — the same relation
 *     one level up, over the modules themselves, with each side asserted non-empty for the same
 *     reason: a module that contributes no pass (`ckpt` registers operators and resources only)
 *     declares none, and that is the bijection's empty case, not a module to skip;
 *   * every alias resolves: a core alias names a declared pass, and a module alias names a pass
 *     the same module registered;
 *   * every C++ parameter list left is a test-only pass or the Camel pass whose declaration is
 *     its own `with` list; the list is pinned, so a new one cannot appear quietly.
 *
 *   node test/tools/check-pass-declarations.mjs           check the declarations, kernels and aliases
 *   node test/tools/check-pass-declarations.mjs --names   print the declared pass names, one per
 *                                                         line, and decide nothing
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const SELF = url.fileURLToPath(import.meta.url)
const REPO_ROOT = path.resolve(HERE, '..', '..')

const ROOTS = ['src', 'include', 'modules', 'stdlib', 'tools', 'scripts', 'examples']
const SKIP_DIRS = new Set(['.git', 'out', 'node_modules', 'third_party', 'build', 'sdks'])
const SOURCE_EXTENSIONS = new Set(['.cml', '.cpp', '.cc', '.c', '.h', '.hpp', '.mjs', '.js'])

const problems = []

function* files(dir) {
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
                yield* files(full)
            }
        } else if (
            entry.isFile() &&
            full !== SELF &&
            SOURCE_EXTENSIONS.has(path.extname(entry.name))
        ) {
            yield full
        }
    }
}

// (1) The registry and its parameter strings are gone.
const FORBIDDEN = [
    { name: 'the C++ pass registry', pattern: /\bdeclarePassParams\s*\(/ },
    { name: 'the pass scope initializer', pattern: /\binitPassScope\b/ },
    { name: 'the pass scope type', pattern: /\bPassScope\b/ },
    { name: 'the old registration entry point', pattern: /\bregisterPass\s*\(/ },
    { name: 'the core pass table', pattern: /\bcorePasses\s*\(/ },
    { name: 'the core pass lookup', pattern: /\bfindCorePass\b/ },
    { name: 'the module pass table', pattern: /\bmodulePasses\s*\(/ },
]
// (2) A parameter declaration is written in stdlib, never in a C++ string literal.
const PARAMETER_STRING = /"[A-Za-z_][A-Za-z0-9_]*\s*:\s*(bool|int|float|string)\s*=/

for (const root of ROOTS) {
    for (const file of files(path.join(REPO_ROOT, root))) {
        const relative = path.relative(REPO_ROOT, file).replaceAll(path.sep, '/')
        if (relative === 'stdlib/std.cml') {
            continue
        }
        const text = fs.readFileSync(file, 'utf8')
        text.split(/\r?\n/).forEach((line, index) => {
            for (const { name, pattern } of FORBIDDEN) {
                if (pattern.test(line)) {
                    problems.push(`${relative}:${index + 1}: ${name}: ${line.trim()}`)
                }
            }
            if (PARAMETER_STRING.test(line)) {
                problems.push(
                    `${relative}:${index + 1}: a pass parameter declared in a C++ string ` +
                        `(the declaration belongs in stdlib): ${line.trim()}`
                )
            }
        })
    }
}

// (3) Every declaration has exactly one kernel and every kernel a declaration.
const declarations = []
{
    const file = path.join(REPO_ROOT, 'stdlib', 'std.cml')
    let namespace = []
    for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
        const opened = line.trim().match(/^namespace\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{$/)
        if (opened) {
            namespace.push(opened[1])
            continue
        }
        if (line.trim() === '}') {
            namespace.pop()
            continue
        }
        const func = line.trim().match(/^export\s+func\s+([A-Za-z_][A-Za-z0-9_]*)::<F>/)
        if (func) {
            declarations.push([...namespace, func[1]].join('::'))
        }
    }
}
const implementations = []
{
    const file = path.join(REPO_ROOT, 'src', 'execute', 'pass.cpp')
    const text = fs.readFileSync(file, 'utf8')
    for (const match of text.matchAll(/registerCoreKernel\(\s*"([a-z_][a-z0-9_:]*)"/g)) {
        implementations.push(match[1])
    }
}
// The property is the bijection below: every declaration has one kernel and every kernel one
// declaration. Two counts could only restate it, and a count of how many passes the language has
// moves whenever a pass is added while the relation does not. What must not happen is a set that
// came out empty — a parse that stopped matching would satisfy "every X is a Y" and a bijection
// between two empty sets — so the populations are asserted non-empty here, and the run that
// examined zero passes fails instead of reporting a pass.
if (declarations.length === 0 || implementations.length === 0) {
    problems.push(
        `the bijection was checked over ${declarations.length} declaration(s) and ` +
            `${implementations.length} kernel(s): one side is empty, so nothing was compared`
    )
}
for (const name of declarations) {
    if (implementations.filter((kernel) => kernel === name).length !== 1) {
        const found = implementations.filter((kernel) => kernel === name).length
        problems.push(
            found === 0
                ? `stdlib/std.cml declares '${name}' and no kernel implements it`
                : `stdlib/std.cml declares '${name}' and ${found} kernels implement it`
        )
    }
}
for (const name of implementations) {
    if (declarations.filter((declared) => declared === name).length !== 1) {
        const found = declarations.filter((declared) => declared === name).length
        problems.push(
            found === 0
                ? `src/execute/pass.cpp implements '${name}' and no declaration names it`
                : `src/execute/pass.cpp implements '${name}' ${found} times`
        )
    }
}

// The declared names, as the pipeline writes them (`std::`, and the declaration's namespaces
// below it), for a check that compares a listing of the pipeline against them rather than against
// a number: `--names` prints the set and decides nothing. A broken declaration set still fails
// the mode, so the names a check reads are never a guess.
if (process.argv.includes('--names')) {
    if (problems.length > 0) {
        for (const problem of problems) console.log(`FAIL ${problem}`)
        console.log(`FAIL the declarations are not settled: ${problems.length} problem(s)`)
        process.exit(1)
    }
    console.log(declarations.map((name) => `std::${name}`).sort().join('\n'))
    process.exit(0)
}

// (4) A module's pass is a declaration in the module's `.cml` file, and every registration has
// one. A declaration is pass-shaped when its condition is the pipeline's (`is_function::<T>()`).
// The number of ordinary parameters a declaration line writes: the commas at the top level of its
// first parenthesised list, plus one. A pass takes one (the function it is applied to); a
// declaration with the same condition and more is an operator that wants a function.
function ordinaryParameterCount(line) {
    const open = line.indexOf('(')
    if (open < 0) {
        return -1
    }
    let depth = 0
    let count = 0
    let seen = false
    for (let i = open + 1; i < line.length; i++) {
        const c = line[i]
        if ('([<{'.includes(c)) {
            depth++
        } else if (')]>}'.includes(c)) {
            if (depth === 0) {
                break
            }
            depth--
        } else if (c === ',' && depth === 0) {
            count++
        } else if (!/\s/.test(c)) {
            seen = true
        }
    }
    return seen ? count + 1 : 0
}

const moduleDeclarations = new Set()
for (const entry of fs.readdirSync(path.join(REPO_ROOT, 'stdlib'))) {
    if (!entry.endsWith('.cml') || entry === 'std.cml') {
        continue
    }
    const module = entry.slice(0, -4)
    const text = fs.readFileSync(path.join(REPO_ROOT, 'stdlib', entry), 'utf8')
    for (const match of text.matchAll(
        /^\s*export\s+func\s+([A-Za-z_][A-Za-z0-9_]*)\s*::\s*<[^>]+>\s*\(/gm
    )) {
        const lineEnd = text.indexOf('\n', match.index)
        const line = text.slice(match.index, lineEnd < 0 ? undefined : lineEnd)
        const compact = line.replace(/\s+/g, '')
        if (!compact.includes('whenis_function::<')) {
            continue
        }
        // A pass is a function transform, `(f: F): F!` (design principles 9.1); a declaration of
        // another shape declares an operator of a function value, and one with a body is a pass
        // written in Camel: neither has a registration in C++.
        const shaped = compact.match(/\(([A-Za-z_][A-Za-z0-9_]*):([A-Za-z_][A-Za-z0-9_]*)\):([A-Za-z_][A-Za-z0-9_]*)!/)
        if (!shaped || shaped[2] !== shaped[3] || compact.endsWith('{')) {
            continue
        }
        moduleDeclarations.add(`${module}::${match[1]}`)
    }
}

// Every alias resolves, and every registration is declared.
let aliasCount = 0
const registrations = new Set()
const aliases = []
for (const root of ['modules', 'src']) {
    for (const file of files(path.join(REPO_ROOT, root))) {
        if (!file.endsWith('.cpp')) {
            continue
        }
        const relative = path.relative(REPO_ROOT, file).replaceAll(path.sep, '/')
        const text = fs.readFileSync(file, 'utf8')
        const constants = new Map()
        for (const match of text.matchAll(
            /constexpr\s+const\s+char\s+\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]+)"/g
        )) {
            constants.set(match[1], match[2])
        }
        for (const match of text.matchAll(
            /registerModulePass\(\s*([A-Za-z_][A-Za-z0-9_]*|"[^"]+")/g
        )) {
            let value = match[1]
            if (value.startsWith('"')) {
                value = value.slice(1, -1)
            } else if (constants.has(value)) {
                value = constants.get(value)
            } else {
                continue // `module + "::" + function`: a Camel pass, resolved at run time
            }
            registrations.add(value)
        }
        for (const match of text.matchAll(
            /registerPassAlias\(\s*"([^"]+)"\s*,\s*([A-Za-z_][A-Za-z0-9_]*|"[^"]+")\s*\)/g
        )) {
            let target = match[2]
            if (target.startsWith('"')) {
                target = target.slice(1, -1)
            } else if (constants.has(target)) {
                target = constants.get(target)
            }
            aliases.push({ relative, alias: match[1], target })
        }
    }
}
const corePaths = new Set(implementations.map((name) => `std::${name}`))
// The core's aliases are a table of names, not a registry of kernels: each names a declared
// pass, and resolution tries the written name first (so an alias never shadows a declaration).
{
    const file = path.join(REPO_ROOT, 'src', 'execute', 'pass.cpp')
    const text = fs.readFileSync(file, 'utf8')
    const start = text.indexOf('const std::map<std::string, std::string> passAliases = {')
    const end = text.indexOf('\n};', start)
    if (start < 0 || end < 0) {
        problems.push('src/execute/pass.cpp: the core alias table is not where the guard reads it')
    } else {
        for (const match of text.slice(start, end).matchAll(/\{"(std::[^"]+)", "(std::[^"]+)"\}/g)) {
            aliasCount += 1
            if (!corePaths.has(match[2])) {
                problems.push(`the core alias '${match[1]}' names '${match[2]}', not a pass kernel`)
            }
        }
    }
}
const knownPasses = new Set([...corePaths, ...registrations])
for (const { relative, alias, target } of aliases) {
    aliasCount += 1
    if (!knownPasses.has(target)) {
        problems.push(`${relative}: the alias '${alias}' names '${target}', which is no pass`)
    }
}
// Test-only passes are registered by the module but declared nowhere: they exist to be named by
// a test's pipeline and are not part of a module's language surface.
const isTestOnly = (path) => {
    const separator = path.indexOf('::')
    const funcName = path.slice(separator + 2)
    return path.slice(0, separator) === 'test' || funcName.startsWith('test_')
}
// The module-level bijection, stated once as the relation the rule asks for and not as a count:
// every registered pass is a pass-shaped declaration of its module's `.cml` and every such
// declaration is registered. What must not happen is a side that came out empty — a walk that
// stopped matching would satisfy "every X is a Y" and a bijection between two empty sets, and the
// diagnostic that says what a module provides would then read a declaration file nothing read
// here. So the two modules sets are asserted non-empty, and the run that examined zero modules
// fails instead of reporting a pass.
const registeredModules = new Set(
    [...registrations]
        .filter((path) => !path.startsWith('std::') && !isTestOnly(path))
        .map((path) => path.slice(0, path.indexOf('::')))
)
const declaringModules = new Set(
    [...moduleDeclarations].map((path) => path.slice(0, path.indexOf('::')))
)
if (declaringModules.size === 0 || registeredModules.size === 0) {
    problems.push(
        `the module-level bijection was checked over ${declaringModules.size} declaring module(s) ` +
            `and ${registeredModules.size} module(s) contributing a pass: one side is empty, so ` +
            `nothing was compared`
    )
}
for (const module of registeredModules) {
    if (!declaringModules.has(module)) {
        problems.push(`the module '${module}' contributes a pass and no declaration of it was read`)
    }
}
// A function transform a module implements as an operator of its own (an engine of `rewrite`,
// called as `f->rewrite<rules>`) is declared in the same shape as a pass and has a kernel table
// entry of the same name in the module's C++, but no pass registration: it is no pass.
const operatorNames = (module) => {
    const names = new Set()
    for (const file of files(path.join(REPO_ROOT, 'modules', module))) {
        if (!file.endsWith('.cpp')) {
            continue
        }
        for (const match of fs.readFileSync(file, 'utf8').matchAll(/\{\s*"([a-z_0-9]+)"\s*,/g)) {
            names.add(match[1])
        }
    }
    return names
}
for (const declared of [...moduleDeclarations]) {
    const module = declared.slice(0, declared.indexOf('::'))
    const name = declared.slice(declared.indexOf('::') + 2)
    if (!registrations.has(declared) && operatorNames(module).has(name)) {
        moduleDeclarations.delete(declared)
    }
}
const declaringModulesNow = new Set(
    [...moduleDeclarations].map((path) => path.slice(0, path.indexOf('::')))
)
for (const module of [...declaringModules]) {
    if (!declaringModulesNow.has(module)) {
        declaringModules.delete(module)
    }
}
for (const module of declaringModules) {
    if (!registeredModules.has(module)) {
        problems.push(`the module '${module}' declares a pass and no registration implements it`)
    }
}
for (const path of registrations) {
    if (path.startsWith('std::') || isTestOnly(path)) {
        continue
    }
    if (!moduleDeclarations.has(path)) {
        problems.push(
            `a pass is registered and its module's .cml declares no function for it: ${path}`
        )
    }
}
for (const path of moduleDeclarations) {
    if (!registrations.has(path)) {
        problems.push(`a module declares the pass '${path}' and no registration implements it`)
    }
}

// (5) What is left in C++ is the known gap, pinned so it cannot grow quietly: the test-only
// passes, and the Camel pass whose declaration is its own `with` list. A built-in pass's
// parameters are its module's `.cml` declaration.
const CPP_PARAMETERS = new Map([
    ['modules/tensor/passes/fuse.cpp', 4], // tensor::test_echo, test-only
    ['src/execute/pass.cpp', 5], // test::params, test-only
    // Not a second declaration: built from the Camel function's `with` list, which is the
    // pass's declaration.
    ['src/execute/camel_pass.cpp', 1],
])
for (const root of ['src', 'modules']) {
    for (const file of files(path.join(REPO_ROOT, root))) {
        if (!/\.(cpp|h)$/.test(file)) {
            continue
        }
        const relative = path.relative(REPO_ROOT, file).replaceAll(path.sep, '/')
        const text = fs.readFileSync(file, 'utf8')
        const named = (text.match(/PassParam\{/g) || []).length
        const aggregate =
            relative === 'src/execute/pass.cpp'
                ? (text.match(/^\s*\{"[a-z_]+", PassParamType::/gm) || []).length
                : 0
        const count = named + aggregate
        if (count === 0) {
            continue
        }
        if (!CPP_PARAMETERS.has(relative)) {
            problems.push(
                `${relative}: a pass parameter list in C++; a built-in pass declares its ` +
                    `parameters in the standard library`
            )
        } else if (CPP_PARAMETERS.get(relative) !== count) {
            problems.push(
                `${relative}: ${count} pass parameter entries, the guard knows ` +
                    `${CPP_PARAMETERS.get(relative)}; a module pass's parameters must not grow ` +
                    `in C++`
            )
        }
    }
}

if (problems.length > 0) {
    for (const problem of problems) {
        console.log(`FAIL ${problem}`)
    }
    console.log(`FAIL the passes are not declarations: ${problems.length} problem(s)`)
    process.exit(1)
}
console.log(
    `PASS passes are declarations: ${declarations.length} core declaration(s) and ` +
        `${implementations.length} kernel(s), a bijection (each declared pass exactly one kernel ` +
        `and each kernel exactly one declaration), ${moduleDeclarations.size} contributed by ` +
        `modules, each with one registration, ${aliasCount} aliases resolved, no C++ pass ` +
        `registry and no parameter string`
)
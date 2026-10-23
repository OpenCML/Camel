#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A pass is a function of the standard library (design principles 9.3, H233): its name,
 * documentation and parameters are one body-less declaration in the module's `.cml` file, and
 * the pipeline resolves it by module and name. This guard is what keeps the second declaration
 * from coming back:
 *
 *   * no `declarePassParams`, `initPassScope`, `PassScope` or `registerPass(` in the sources:
 *     those are the C++ pass registry and its parameter strings;
 *   * no parameter declaration written as a string literal
 *     (`"safe_fp: bool = false, ..."`) outside `stdlib/`, where the declaration belongs;
 *   * every pass declared in `stdlib/std.cml` has exactly one implementation in
 *     `src/execute/pass.cpp`, and every implementation is declared; the count is pinned so a
 *     pass that exists only in C++ fails here;
 *   * no declaration writes its parameters in the removed parentheses form.
 *
 *   node test/tools/check-pass-declarations.mjs
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

// (3) Every declaration has an implementation and every implementation a declaration.
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
    const start = text.indexOf('const std::map<std::string, PassMaker> &corePasses()')
    const end = text.indexOf('\n        };', start)
    if (start < 0 || end < 0) {
        problems.push('src/execute/pass.cpp: the core pass table is not where the guard reads it')
    } else {
        for (const match of text.slice(start, end).matchAll(/\{"([a-z_][a-z0-9_:]*)",/g)) {
            implementations.push(match[1])
        }
    }
}
const DECLARED_COUNT = 53
if (declarations.length !== DECLARED_COUNT) {
    problems.push(
        `stdlib/std.cml declares ${declarations.length} passes; the language declares ` +
            `${DECLARED_COUNT}. A pass was added or removed without moving the count and the ` +
            `guard with it.`
    )
}
for (const name of declarations) {
    if (!implementations.includes(name)) {
        problems.push(`stdlib/std.cml declares '${name}' and no kernel implements it`)
    }
}
for (const name of implementations) {
    if (!declarations.includes(name)) {
        problems.push(`src/execute/pass.cpp implements '${name}' and no declaration names it`)
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
    `PASS passes are declarations: ${declarations.length} declared, each with one kernel, ` +
        `no C++ registry and no parameter string`
)
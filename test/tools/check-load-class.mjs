#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Two tests that can write the same place, or that bind the same port, pass or fail by the
 * order the scheduler happens to run them in. This guard reads every plan and reports such a
 * declaration before it becomes a red run on a busy machine:
 *
 *   * a location in `args` or an environment value that the plan does not already hold as a file
 *     is a place the test creates; if two tests name it, they share it (the guard names both).
 *     A value with no separator is a name relative to something the plan also declares, and is
 *     not read;
 *   * a port written as a literal (`--port 8765`, `port=8765`) is one machine-wide resource: two
 *     tests using it cannot run at once, and a leftover listener decides the result;
 *   * a fixed OS temporary path in a case file (`/tmp/x`, `C:\...\Temp\x`) is outside the scratch
 *     directory the runner owns, so a previous run's file can be read as this run's output. A
 *     case that knows `CAMEL_TEST_TMPDIR` is skipped: the fixed path in it is the manual-run
 *     fallback, which the runner never takes.
 *
 * What this guard cannot see, and does not claim to: a path assembled from a variable, from a
 * case's own `mkdtemp`, or by joining two declared names (a computed name cannot be decided
 * statically at all); a path only a `verify_script` names (it writes where its payload says); a
 * port taken by a process the test starts itself (the debugger and LSP clients pick a free one at
 * run time, and a collision there is not visible here).
 *
 * The wall-clock half of the same class is enforced by the runner, not here: it fails a passing
 * test that used more than a quarter of its `timeout_ms` (see TIMEOUT_MARGIN in run-tests.mjs),
 * with the measured number, and the plans' own comments state the measurement behind a tight
 * bound. Neither half can prove a test has no other load-sensitive dependency; what they catch is
 * the two shapes that have actually failed: a shared writable place, and a bound with no margin.
 *
 *   node test/tools/check-load-class.mjs [--root <checkout>]
 *
 * The self-test under test/tools/selftest/load-class/ runs it on a fixture that breaks each rule.
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

import { parseTomlFile } from './toml-lite.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..')

let root = REPO_ROOT
for (let i = 2; i < process.argv.length; i++) {
    if (process.argv[i] === '--root' && i + 1 < process.argv.length) {
        root = path.resolve(process.argv[++i])
    } else {
        console.log(`FAIL unknown argument: ${process.argv[i]}`)
        process.exit(2)
    }
}

const problems = []
const warnings = []

function* planFiles(dir) {
    let entries
    try {
        entries = fs.readdirSync(dir, { withFileTypes: true })
    } catch {
        return
    }
    for (const entry of entries) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) {
            yield* planFiles(full)
        } else if (entry.isFile() && entry.name.endsWith('.plan.toml')) {
            yield full
        }
    }
}

/** The path a word names, or null when the word is a flag, a pass name, a number or a template. */
function pathInWord(word) {
    if (typeof word !== 'string') return null
    const text = word.trim()
    if (!text || text.startsWith('-')) return null
    // A value the runner expands is this test's own: ${test_tmp} is the scratch directory and
    // ${case} is a checked-in input.
    if (text.includes('${') || text.includes('{tmp}') || text.includes('%')) return null
    if (/\s/.test(text)) return null // a command line or prose, not one path
    // A pass name (`std::fvm`) or a pass with a `--timeit` count (`std::nvm/10`) is not a path.
    if (/^[A-Za-z_][A-Za-z0-9_:]*(?:\/\d+)?$/.test(text)) return null
    const eq = text.indexOf('=')
    const value = eq > 0 ? text.slice(eq + 1) : text
    if (!value || value.startsWith('-') || /^\d+$/.test(value)) return null
    // A name without a separator is relative to something the plan also declares (an input
    // path, CAMEL_TEST_DIR, the program's own directory); only a spelled-out location is read.
    if (!/[\\/]/.test(value)) return null
    return value
}

function existsAsFileOrDir(base, value) {
    const candidate = path.isAbsolute(value) ? value : path.resolve(base, value)
    try {
        return fs.statSync(candidate).isFile() || fs.statSync(candidate).isDirectory()
    } catch {
        return false
    }
}

function portInWords(words) {
    const ports = []
    for (let i = 0; i < words.length; i++) {
        const word = String(words[i] ?? '').trim()
        if (!word) continue
        if (word === '--port' || word === '-p' || word === '--listen') {
            const next = String(words[i + 1] ?? '').trim()
            if (/^\d+$/.test(next)) ports.push(Number(next))
            continue
        }
        for (const match of word.matchAll(/(?:^|[^A-Za-z])(?:--)?port=(\d+)/gi)) {
            ports.push(Number(match[1]))
        }
    }
    return ports
}

/** Removes comments and docstrings so a path named in prose is not read as a path the case uses. */
function withoutComments(text, extension) {
    if (extension === '.py') {
        return text
            .replace(/"""[\s\S]*?"""/g, '')
            .replace(/'''[\s\S]*?'''/g, '')
            .replace(/#.*$/gm, '')
    }
    return text.replace(/\/\/.*$/gm, '').replace(/\/\*[\s\S]*?\*\//g, '')
}

const writers = new Map() // resolved path -> [test label]
let declarations = 0

for (const planPath of planFiles(path.join(root, 'test', 'plans'))) {
    let plan
    try {
        plan = parseTomlFile(planPath)
    } catch (error) {
        problems.push(`${path.relative(root, planPath)}: cannot be parsed: ${error.message}`)
        continue
    }
    const defaults = plan.defaults || {}
    const planRelative = path.relative(root, planPath)
    for (const test of plan.tests || []) {
        const label = `${plan.id || planRelative}:${test.name || '<unnamed>'}`
        const words = [
            ...(defaults.args || []),
            ...(test.args || []),
            ...(defaults.envs || []),
            ...(test.envs || []),
        ].map(String)
        const planDir = path.dirname(planPath)
        const caseDir = test.case ? path.dirname(path.resolve(planDir, test.case)) : planDir

        for (const word of words) {
            const value = pathInWord(word)
            if (value === null) continue
            declarations++
            // A path that exists next to the plan, the case or the repository is a checked-in
            // input: reading it from two tests is what fixtures are for.
            if (
                existsAsFileOrDir(root, value) ||
                existsAsFileOrDir(planDir, value) ||
                existsAsFileOrDir(caseDir, value)
            ) {
                continue
            }
            const resolved = path.isAbsolute(value) ? value : path.resolve(root, value)
            const key = path.normalize(resolved)
            if (!writers.has(key)) writers.set(key, [])
            writers.get(key).push(label)
        }

        for (const port of portInWords(words)) {
            if (port === 0) continue
            problems.push(
                `${label}: binds the fixed port ${port}. A port is one machine-wide resource: ` +
                    `two tests cannot use it at once and a leftover listener decides the result. ` +
                    `Ask the OS for a free port (port 0) instead of naming one.`
            )
        }

        // A case that names an absolute path reads or writes outside the runner's scratch
        // directory, where a file from an earlier run or another checkout can answer for it. A
        // case that knows CAMEL_TEST_TMPDIR is not this: the fixed path in it is the manual-run
        // fallback (`if root == '' then 'test/tmp/x'`), which the runner never takes.
        const casePath = test.case ? path.resolve(planDir, test.case) : null
        if (!casePath || !fs.existsSync(casePath)) continue
        const extension = path.extname(casePath).toLowerCase()
        if (!['.cml', '.mjs', '.js', '.py'].includes(extension)) continue
        // The selftest fixtures are the counter-example this guard is checked against; they name
        // the bad paths on purpose and are never run as tests. Only when scanning the checkout:
        // with --root pointing at a fixture, the fixture is exactly what must be reported.
        const fixtures = path.join(REPO_ROOT, 'test', 'tools', 'selftest')
        if (root === REPO_ROOT && casePath.startsWith(fixtures + path.sep)) continue
        const text = withoutComments(fs.readFileSync(casePath, 'utf8'), extension)
        if (text.includes('CAMEL_TEST_TMPDIR')) continue
        for (const match of text.matchAll(/["'`]([^"'`\n]{1,200})["'`]/g)) {
            const literal = match[1]
            // A fixed OS temporary path: the shared place this guard is about. An HTTP route
            // (`/api/state`) and a deliberately missing fixture (`/no/such.cml`) are not it.
            const absoluteTemp =
                /^\/(?:tmp|var\/tmp)\//.test(literal) ||
                (/^[A-Za-z]:[\\/]/i.test(literal) && /[\\/](?:tmp|temp)[\\/]/i.test(literal))
            if (!absoluteTemp) continue
            problems.push(
                `${label}: ${path.relative(root, casePath)} names the absolute path ` +
                    `'${literal}'. A test writes under the scratch directory the runner gives it ` +
                    `(${'${test_tmp}'} in the plan, CAMEL_TEST_TMPDIR in the process); an ` +
                    `absolute path can be a file another test or an earlier run left behind.`
            )
        }
    }
}

for (const [target, labels] of writers) {
    const targetRelative = path.relative(root, target)
    if (labels.length > 1) {
        problems.push(
            `${labels.join(' and ')} both name ${targetRelative} as an output. ` +
                `Two tests that write one path pass or fail by the order they run in; each must ` +
                `write under its own ${'${test_tmp}'}.`
        )
    } else {
        warnings.push(
            `${labels[0]} names ${targetRelative}, which is not a checked-in file: the test ` +
                `creates it at a fixed place (one test, so no collision today).`
        )
    }
}

for (const warning of warnings) console.log(`WARN ${warning}`)

if (problems.length > 0) {
    for (const problem of problems) console.log(`FAIL ${problem}`)
    console.log(
        `FAIL no test writes a fixed path another test can write or binds a fixed port: ` +
            `${problems.length} problem(s)`
    )
    process.exit(1)
}
console.log(
    `PASS every test writes under its own scratch directory and asks for a free port ` +
        `(${declarations} declared path(s) checked against ${path.relative(REPO_ROOT, root) || '.'}; ` +
        `${warnings.length} single fixed path(s) reported above)`
)
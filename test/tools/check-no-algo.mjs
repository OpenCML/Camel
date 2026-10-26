#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The `algo` module is gone: its algorithms are `array`'s, and the module that only loaded their
 * kernels was deleted. This guard is what keeps
 * it gone:
 *
 *   * no `stdlib/algo.cml` and no `modules/algo/` may reappear;
 *   * no source, test, example or benchmark may name an `algo:` operator, an `algo::` function
 *     or import `algo`.
 *
 * The scan reads every source file under the code roots (Camel, C++, CMake, JavaScript and
 * Python), so a new `algo:` URI anywhere in code fails here even when no test imports it. Plans
 * and `docs/` are not scanned: a plan names the module in its expectations, the handover and
 * review records describe it as it was, and rewriting history is not the point of the guard.
 * `test/results/` (the plan runner's log tree) and `tmp/` are not source either: they are what a run
 * wrote, they are not part of the checkout, and walking them would cost this guard the tree's size
 * rather than its own work (`isRunnerOutput` in `runner-output.mjs` is where that is decided).
 *
 *   node test/tools/check-no-algo.mjs
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'
import { isRunnerOutput } from './runner-output.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const SELF = url.fileURLToPath(import.meta.url)
const REPO_ROOT = path.resolve(HERE, '..', '..')

// The roots that carry code. `docs/` and `out/` are deliberately absent.
const ROOTS = ['src', 'include', 'modules', 'stdlib', 'tools', 'scripts', 'examples', 'benchmarks', 'test']
const SKIP_DIRS = new Set(['.git', 'out', 'node_modules', 'third_party', 'build', 'sdks'])
// Not source: the trees a run writes (`test/results` and `tmp`), which grow per run and are not part
// of the checkout. Which paths those are is one answer, in runner-output.mjs beside the runner, so a
// tree a run starts writing does not have to be added here; the log tree used to hold a copy of
// this very script. A `results/` directory elsewhere (the benchmark and example outputs) holds no
// scanned extension and is walked.
// Source files only: a plan names the module in its expectations and a README names it in prose.
const SOURCE_EXTENSIONS = new Set(['.cml', '.cpp', '.cc', '.c', '.h', '.hpp', '.mjs', '.js', '.py', '.cmake', '.rule'])

const PATTERNS = [
    { name: 'an `algo:` operator URI', pattern: /\balgo:/ },
    { name: 'a call through `algo::`', pattern: /\balgo::/ },
    { name: 'an `import algo`', pattern: /\bimport\s+algo\b/ },
    { name: 'a `from algo` import', pattern: /\bfrom\s+algo\b/ },
    { name: 'a `stdlib/algo` path', pattern: /\bstdlib\/algo\b/ },
    { name: 'a `modules/algo` path', pattern: /\bmodules\/algo\b/ },
]

const problems = []

// (1) The module's files are gone for good.
for (const relative of ['stdlib/algo.cml', 'modules/algo']) {
    if (fs.existsSync(path.join(REPO_ROOT, relative))) {
        problems.push(`${relative} exists again: the module was folded into \`array\``)
    }
}

// (2) Nothing names it. `docs/` is history and is left out; every other text file is read.
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
            if (!SKIP_DIRS.has(entry.name) && !isRunnerOutput(full, REPO_ROOT)) {
                yield* files(full)
            }
        } else if (
            entry.isFile() &&
            full !== SELF &&
            (SOURCE_EXTENSIONS.has(path.extname(entry.name)) || entry.name === 'CMakeLists.txt')
        ) {
            yield full
        }
    }
}

for (const root of ROOTS) {
    for (const file of files(path.join(REPO_ROOT, root))) {
        let bytes
        try {
            bytes = fs.readFileSync(file)
        } catch {
            continue
        }
        // Binary fixtures (a `.cmo`, an image) are not source; a NUL byte marks them.
        if (bytes.includes(0)) {
            continue
        }
        const text = bytes.toString('utf8')
        if (!text.includes('algo')) {
            continue
        }
        const relative = path.relative(REPO_ROOT, file).replaceAll(path.sep, '/')
        text.split(/\r?\n/).forEach((line, index) => {
            for (const { name, pattern } of PATTERNS) {
                if (pattern.test(line)) {
                    problems.push(`${relative}:${index + 1}: ${name}: ${line.trim()}`)
                }
            }
        })
    }
}

if (problems.length > 0) {
    for (const problem of problems) {
        console.log(`FAIL ${problem}`)
    }
    console.log(`FAIL the algo module is back: ${problems.length} reference(s)`)
    process.exit(1)
}
console.log('PASS no algo module: no file, no operator URI, no import under the code roots')
#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The `algo` module is gone: its algorithms are `array`'s, and the module that only loaded their
 * kernels was deleted. This guard is what keeps
 * it gone:
 *
 *   * no `algo` module may reappear: neither `algo.cml` under `stdlib/` nor a
 *     `modules/algo/` directory;
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
 * Three classes of path, and telling them apart is the point of this file (2026-11-22). It used to
 * probe its `algo` paths with `existsSync` and walk whichever roots it happened to find, so a PASS
 * rested on a scope it never named: every probed path was absent -- which for those paths is the
 * expected reading, see GONE_PATHS -- while the code roots it actually read were only assumed to be
 * there.
 *
 *   * REQUIRED_ROOTS should be there. They are walked, and a missing one is exit 2 with its path: a
 *     scope that quietly lost a root is the failure this guard was fixed for.
 *   * LEGACY_ROOTS are the names the layout had before the move to `kernel/parse/compile/lib`.
 *     They are walked when present, so a checkout of either layout is scanned, but their absence is
 *     today's layout and is reported, not failed.
 *   * GONE_PATHS must NOT be there, and their absence is *half of this guard's PASS*: the deleted
 *     module's directory and its file. "Not found" is the expected answer for these, not a skipped
 *     check; found is a FAIL.
 *
 * Every run prints the scope it covered -- the roots walked, the legacy names, the paths that must
 * stay gone, how many files it read, how many of them named `algo`, and how many rules matched --
 * so a PASS says what it looked at, not only what it did not find.
 *
 * Exit status: 0 clean, 1 a returned path or a reference, 2 nothing to scan (a required root is
 * missing, or the walk read no file).
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

// The roots that carry code in this layout, and the names the old one gave them. `docs/` and
// `out/` are deliberately absent from both: `docs/` is a record and `out/` is what a build wrote.
// The layout move put the compiler in `kernel/`, `parse/`, `compile/` and the library in `lib/`
// (which took `stdlib/` and `modules/`); the old names stay listed so a checkout of either layout
// is walked.
const REQUIRED_ROOTS = [
    'kernel',
    'parse',
    'compile',
    'lib',
    'tools',
    'scripts',
    'examples',
    'benchmarks',
    'test'
]
const LEGACY_ROOTS = ['src', 'include', 'modules', 'stdlib']
// Paths that must not exist. `lib/algo/` is where a module lives after the layout move,
// `stdlib/algo.cml` and `modules/algo/` where it lived before it; the file is named as well as the
// directory, so a FAIL points at the exact path and not only at its parent.
const GONE_PATHS = ['lib/algo', 'lib/algo/algo.cml', 'stdlib/algo.cml', 'modules/algo']

const SKIP_DIRS = new Set(['.git', 'out', 'node_modules', 'third_party', 'build', 'sdks'])
// Not source: the trees a run writes (`test/results` and `tmp`), which grow per run and are not part
// of the checkout. Which paths those are is one answer, in runner-output.mjs beside the runner, so a
// tree a run starts writing does not have to be added here; the log tree used to hold a copy of
// this very script. A `results/` directory elsewhere (the benchmark and example outputs) holds no
// scanned extension and is walked.
// Source files only: a plan names the module in its expectations and a README names it in prose.
const SOURCE_EXTENSIONS = new Set([
    '.cml',
    '.cpp',
    '.cc',
    '.c',
    '.h',
    '.hpp',
    '.mjs',
    '.js',
    '.py',
    '.cmake',
    '.rule'
])

const PATTERNS = [
    { name: 'an `algo:` operator URI', pattern: /\balgo:/ },
    { name: 'a call through `algo::`', pattern: /\balgo::/ },
    { name: 'an `import algo`', pattern: /\bimport\s+algo\b/ },
    { name: 'a `from algo` import', pattern: /\bfrom\s+algo\b/ },
    { name: 'a `stdlib/algo` path', pattern: /\bstdlib\/algo\b/ },
    { name: 'a `modules/algo` path', pattern: /\bmodules\/algo\b/ },
    { name: 'a `lib/algo` path', pattern: /\blib\/algo\b/ }
]

const problems = []
const counts = { filesRead: 0, filesNamingAlgo: 0, ruleHits: 0 }

const full = (relative) => path.join(REPO_ROOT, relative)
const exists = (relative) => fs.existsSync(full(relative))

// What the scan covers, decided once and printed on every run, PASS or not.
const missingRequired = REQUIRED_ROOTS.filter((relative) => !exists(relative))
const presentRequired = REQUIRED_ROOTS.filter((relative) => exists(relative))
const presentLegacy = LEGACY_ROOTS.filter((relative) => exists(relative))
const absentLegacy = LEGACY_ROOTS.filter((relative) => !exists(relative))
const returnedPaths = GONE_PATHS.filter((relative) => exists(relative))

function printScope() {
    const required = `${presentRequired.length} of ${REQUIRED_ROOTS.length} required root(s) present`
    console.log(
        `scope: ${required}, ${counts.filesRead} file(s) read, ` +
            `${counts.filesNamingAlgo} of them naming \`algo\`, ${counts.ruleHits} rule hit(s)`
    )
    console.log(`  required, scanned: ${presentRequired.join(' ')}`)
    if (missingRequired.length > 0) {
        const named = missingRequired.map((relative) => `${relative} (${full(relative)})`)
        console.log(`  required, MISSING: ${named.join(', ')}`)
    }
    console.log(
        presentLegacy.length === 0
            ? `  legacy, walked when present; absent in this layout: ${LEGACY_ROOTS.join(' ')}`
            : `  legacy, walked when present; here: ${presentLegacy.join(' ')}; ` +
                  `absent: ${absentLegacy.join(' ')}`
    )
    console.log(
        returnedPaths.length === 0
            ? `  must not exist, and none does: ${GONE_PATHS.join(' ')}`
            : `  must not exist, but is present: ${returnedPaths.join(' ')}`
    )
}

// (1) The paths that must stay gone. Their absence is part of PASS, so it is answered from this
// list by name rather than by walking something that is not there.
for (const relative of returnedPaths) {
    problems.push(`${relative} exists again: the module was folded into \`array\``)
}

// (2) The scope. A required root that is not there is not a smaller scan, it is no answer at all:
// the walk below would find fewer files and the PASS line would look exactly the same.
if (missingRequired.length > 0) {
    printScope()
    console.log(
        `FAIL the scan has no scope: ${missingRequired.length} required root(s) are missing, ` +
            'so nothing under them was searched'
    )
    process.exit(2)
}

// (3) Nothing names it. `docs/` is history and is left out; every other source file is read.
function* files(dir) {
    let entries
    try {
        entries = fs.readdirSync(dir, { withFileTypes: true })
    } catch {
        return
    }
    for (const entry of entries) {
        const child = path.join(dir, entry.name)
        if (entry.isDirectory()) {
            if (!SKIP_DIRS.has(entry.name) && !isRunnerOutput(child, REPO_ROOT)) {
                yield* files(child)
            }
        } else if (
            entry.isFile() &&
            child !== SELF &&
            (SOURCE_EXTENSIONS.has(path.extname(entry.name)) || entry.name === 'CMakeLists.txt')
        ) {
            yield child
        }
    }
}

for (const root of [...REQUIRED_ROOTS, ...LEGACY_ROOTS]) {
    for (const file of files(full(root))) {
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
        counts.filesRead++
        const text = bytes.toString('utf8')
        if (!text.includes('algo')) {
            continue
        }
        counts.filesNamingAlgo++
        const relative = path.relative(REPO_ROOT, file).replaceAll(path.sep, '/')
        text.split(/\r?\n/).forEach((line, index) => {
            for (const { name, pattern } of PATTERNS) {
                if (pattern.test(line)) {
                    counts.ruleHits++
                    problems.push(`${relative}:${index + 1}: ${name}: ${line.trim()}`)
                }
            }
        })
    }
}

// A walk that read nothing is the same failure in another form: every root was there and empty.
if (counts.filesRead === 0) {
    printScope()
    console.log('FAIL the walk read no file: an empty scan is not a clean tree')
    process.exit(2)
}

printScope()
if (problems.length > 0) {
    for (const problem of problems) {
        console.log(`FAIL ${problem}`)
    }
    console.log(`FAIL the algo module is back: ${problems.length} reference(s)`)
    process.exit(1)
}
console.log('PASS no algo module: no file, no operator URI, no import under the code roots')
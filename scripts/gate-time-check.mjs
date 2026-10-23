#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The gate's build-side facts, measured on a machine that has the Release build tree:
 *
 *   node scripts/gate-time-check.mjs [--config Release] [--header include/camel/core/operator.h]
 *
 * It runs on the build host (the gate host keeps out/latest), not on a workstation that cannot
 * build. Three checks, each of which can go red:
 *
 *   1. an unchanged tree is a no-op: `ninja -f build-Release.ninja -n` says "no work to do".
 *      Red when any configure-time clock leaks into a compiled header (the build footprint, a
 *      generated .inc rewritten on every configure, a shuffled include list).
 *   2. touching one header rebuilds exactly the objects that include it -- no more, no fewer.
 *      The dependents are read from ninja's own dependency database (`ninja -t deps`), so the
 *      expected set comes from the tree, not from a literal in this file. Red when a target that
 *      does not include the header rebuilds (an over-broad dependency, a shared generated file),
 *      or when an object that does include it is missed. The count is printed for the report.
 *   3. `scripts/build-info.js check` accepts the install: this is the precondition the helper's
 *      -NoBuild path refuses on, so a -NoBuild gate run can skip the build only when this passes.
 *
 * `--stamp-rebuild` adds a fourth, measured check and rebuilds the tree, so it is not part of a
 * routine run:
 *
 *   4. moving the build stamp (`BUILD_FOOTPRINT`, written by `configure_file` into
 *      `build_config.h`) rebuilds the stamp's own translation unit in each target and relinks it,
 *      and nothing else. The check rewrites the generated headers exactly as a reconfigure does
 *      when the pin in `build/footprint.json` moves, runs ninja for real, and names every object
 *      and link edge it saw. Red when a heavy translation unit includes `build_config.h` again:
 *      `tools/camel-cli/config.cpp` (17 s) and `tools/debugger/src/repl.cpp` (15 s) did, so any
 *      stamp change cost 32 s of compilation for a string that reaches one `cout`.
 *      `--repeat N` measures the same change N times and prints the spread, because the build
 *      host is shared and a single number cannot tell a real difference from load.
 *
 * The header's timestamps are restored in a finally block; a check that dies mid-way leaves the
 * tree as it found it. `--stamp-rebuild` restores the generated headers and rebuilds them in the
 * same finally block, so the binaries on disk match the tree again when it returns.
 */
import fs from 'fs'
import path from 'path'
import { spawnSync } from 'child_process'
import { fileURLToPath } from 'url'

const here = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.dirname(here)
const buildDir = path.join(repoRoot, 'build')

function usage(message) {
    if (message) console.error(`[gate-time-check] ${message}`)
    console.error(
        'usage: node scripts/gate-time-check.mjs [--config Release|Debug] [--header <path under the repository>]\n' +
            '       [--stamp-rebuild [--repeat N]]'
    )
    process.exit(2)
}

const options = { config: 'Release', header: 'include/camel/core/operator.h', stampRebuild: false, repeat: 1 }
const argv = process.argv.slice(2)
for (let i = 0; i < argv.length; i++) {
    const next = () => {
        if (i + 1 >= argv.length) usage(`${argv[i]} needs a value`)
        return argv[++i]
    }
    if (argv[i] === '--config') options.config = next()
    else if (argv[i] === '--header') options.header = next()
    else if (argv[i] === '--stamp-rebuild') options.stampRebuild = true
    else if (argv[i] === '--repeat') {
        options.repeat = Number(next())
        if (!Number.isInteger(options.repeat) || options.repeat < 1) usage('--repeat needs a positive integer')
    } else if (argv[i] === '--help' || argv[i] === '-h') usage()
    else usage(`unknown argument "${argv[i]}"`)
}

const ninjaFile = path.join(buildDir, `build-${options.config}.ninja`)
if (!fs.existsSync(ninjaFile)) {
    usage(`${path.relative(repoRoot, ninjaFile)} does not exist: run the gate (or \`npm run build\`) on this machine first`)
}

// Ninja is a build-tool dependency, not something to hard-code: CMake recorded the one it used.
function findNinja() {
    const cache = path.join(buildDir, 'CMakeCache.txt')
    const match = fs.existsSync(cache) ? fs.readFileSync(cache, 'utf-8').match(/^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$/m) : null
    const candidate = process.env.NINJA || (match && match[1].trim())
    if (!candidate) usage('cannot find ninja: set NINJA, or keep build/CMakeCache.txt')
    if (!fs.existsSync(candidate)) usage(`ninja at ${candidate} does not exist (set NINJA)`)
    return candidate
}

const ninja = findNinja()
const problems = []

function runNinja(args) {
    const proc = spawnSync(ninja, ['-f', ninjaFile, ...args], {
        cwd: buildDir,
        encoding: 'utf8',
        // `ninja -t deps` prints every object's inputs: hundreds of megabytes of text on this
        // tree, well past spawnSync's 1 MiB default.
        maxBuffer: 512 * 1024 * 1024,
        env: { ...process.env, LC_ALL: 'C' },
    })
    if (proc.error) throw proc.error
    return { status: proc.status, output: `${proc.stdout}${proc.stderr}` }
}

// --- 1. an unchanged tree is a no-op ---------------------------------------------------------
const noop = runNinja(['-n'])
const noopEdges = dirtyObjects(noop.output)
if (noop.status !== 0) {
    problems.push(`unchanged tree: ninja -n failed (${noop.status}): ${noop.output.trim().split('\n').slice(-3).join(' | ')}`)
} else if (noopEdges.length > 0) {
    problems.push(
        `unchanged tree: ninja -n wants to build ${noopEdges.length} object(s), first ${noopEdges[0]}: ` +
            'a configure-time value reaches a compiled header'
    )
}
console.log(`[gate-check] unchanged tree: ${noopEdges.length} object(s) to build (expected 0)`)

function dirtyObjects(output) {
    return [...output.matchAll(/^\[\d+\/\d+\] Building CXX object (.+)$/gm)].map((m) => m[1].trim())
}

// --- 2. a header change rebuilds exactly its dependents --------------------------------------
// Every object ninja has a dependency record for, with the recorded inputs. The record is what
// ninja itself uses to decide freshness, so "depends on the header" is measured, not guessed.
const deps = runNinja(['-t', 'deps'])
if (deps.status !== 0) {
    problems.push(`header dependents: ninja -t deps failed (${deps.status})`)
}
const byObject = new Map()
let current = null
for (const line of deps.output.split(/\r?\n/)) {
    if (!line.trim()) continue
    if (!line.startsWith(' ')) {
        current = line.replace(/: #deps .*$/, '').trim()
        if (current.endsWith('.o')) byObject.set(current, [])
        else current = null
    } else if (current) {
        // Dependency paths are as the compiler wrote them: relative to the build directory for the
        // checkout's own files, absolute for the toolchain's. Normalize them so the header's
        // absolute path is what gets compared.
        byObject.get(current).push(path.resolve(buildDir, line.trim()))
    }
}

const header = path.resolve(repoRoot, options.header)
if (!fs.existsSync(header)) usage(`header ${options.header} does not exist`)
if (byObject.size === 0) {
    problems.push('header dependents: ninja -t deps returned no object records; the measurement is empty')
}
const dependents = [...byObject]
    .filter(([, inputs]) => inputs.includes(header))
    .map(([object]) => object)
    .sort()

const stat = fs.statSync(header)
const now = new Date()
try {
    fs.utimesSync(header, now, now)
    const touched = runNinja(['-n'])
    const rebuilt = dirtyObjects(touched.output).sort()
    const missing = dependents.filter((object) => !rebuilt.includes(object))
    const extra = rebuilt.filter((object) => !dependents.includes(object))
    console.log(
        `[gate-check] touched ${options.header}: ${rebuilt.length} object(s) rebuild; ` +
            `${dependents.length} object(s) depend on it (${byObject.size} recorded objects)`
    )
    if (dependents.length === 0) {
        problems.push(`header dependents: no object records a dependency on ${options.header}; the measurement is empty`)
    }
    if (missing.length > 0) {
        problems.push(
            `header dependents: ${missing.length} dependent(s) would NOT rebuild, first ${missing[0]}`
        )
    }
    if (extra.length > 0) {
        problems.push(
            `header dependents: ${extra.length} object(s) rebuild although they do not include the header, first ${extra[0]}`
        )
    }
} finally {
    fs.utimesSync(header, stat.atime, stat.mtime)
}

// --- 3. the -NoBuild precondition ------------------------------------------------------------
const stamp = spawnSync(process.execPath, [path.join(here, 'build-info.js'), 'check', '--tag', 'latest'], {
    cwd: repoRoot,
    encoding: 'utf8',
})
if (stamp.status !== 0) {
    problems.push(
        `-NoBuild precondition: build-info check exited ${stamp.status}: ${(stamp.stdout || stamp.stderr).trim().split('\n').slice(-3).join(' | ')}`
    )
} else {
    console.log('[gate-check] -NoBuild precondition: out/latest is stamped from this tree')
}

// --- 4. a stamp change rebuilds one small translation unit per target ------------------------
// The generated header exists to carry one string into the binary. Every translation unit that
// includes it pays its own compilation for a value only its own `cout` reads, so the header's
// dependents are the whole blast radius of a stamp change. `build_stamp.cpp` is the unit that is
// allowed to depend on it: it is five lines and its only job is that include.
if (options.stampRebuild) {
    if (noopEdges.length > 0) {
        problems.push(
            'stamp change: the tree was not up to date before the measurement; build it first, then re-run'
        )
    } else {
        const stampHeaders = [
            ...new Set([...byObject.values()].flat().filter((input) => path.basename(input) === 'build_config.h')),
        ].sort()
        if (stampHeaders.length === 0) {
            problems.push(
                'stamp change: no object records a dependency on a generated build_config.h; the measurement is empty'
            )
        }
        const stampDependents = [...byObject]
            .filter(([, inputs]) => inputs.some((input) => stampHeaders.includes(input)))
            .map(([object]) => object)
            .sort()
        const wrongUnits = stampDependents.filter((object) => !/(^|\/)build_stamp\.cpp(\.o|\.obj)$/.test(object))
        if (wrongUnits.length > 0) {
            problems.push(
                `stamp change: ${wrongUnits.length} translation unit(s) besides build_stamp.cpp include the stamp header, ` +
                    `first ${wrongUnits[0]}: every stamp change recompiles them`
            )
        }
        if (stampDependents.length === 0) {
            problems.push('stamp change: no object depends on the stamp header; the measurement is empty')
        }
        for (const header of stampHeaders) {
            if (!stampDependents.some((object) => byObject.get(object).includes(header))) {
                problems.push(
                    `stamp change: ${path.relative(buildDir, header)} reaches no build_stamp.cpp; the stamp in the binary is not the one this build configured`
                )
            }
        }

        function stampText(original, value) {
            const define = /(#define\s+BUILD_FOOTPRINT\s+")[^"]*(")/
            if (!define.test(original)) throw new Error('build_config.h has no BUILD_FOOTPRINT define')
            return original.replace(define, (_match, before, after) => `${before}${value}${after}`)
        }

        function buildEdges(output) {
            return {
                objects: [...output.matchAll(/^\[\d+\/\d+\] Building CXX object (.+)$/gm)].map((m) => m[1].trim()),
                links: [...output.matchAll(/^\[\d+\/\d+\] Linking (?:CXX|C|ASM) executable (.+)$/gm)].map((m) =>
                    m[1].trim()
                ),
            }
        }

        const saved = new Map()
        for (const header of stampHeaders) {
            const stat = fs.statSync(header)
            saved.set(header, { text: fs.readFileSync(header, 'utf-8'), atime: stat.atime, mtime: stat.mtime })
        }
        const baseline = saved.size > 0 ? (saved.get(stampHeaders[0]).text.match(/BUILD_FOOTPRINT\s+"([^"]*)"/) || [])[1] : undefined
        if (!baseline) {
            problems.push('stamp change: the generated header has no BUILD_FOOTPRINT value; the measurement is empty')
        }
        const targetDirs = [
            ...new Set(stampDependents.map((object) => object.replace(/CMakeFiles[\\/].*$/, ''))),
        ].sort()
        const measurements = []
        // A real stamp change is a cache miss for every unit that includes the header; a reused
        // probe value would let ccache turn the second measurement into a hit and understate it.
        const probeTag = `${Date.now()}_${process.pid}`
        try {
            for (let run = 1; run <= options.repeat && baseline; run++) {
                const value = `${baseline}_probe${probeTag}_${run}`
                for (const header of stampHeaders) {
                    fs.writeFileSync(header, stampText(saved.get(header).text, value))
                }
                const started = process.hrtime.bigint()
                const build = runNinja([])
                const seconds = Number(process.hrtime.bigint() - started) / 1e9
                const edges = buildEdges(build.output)
                const rebuilt = [...new Set(edges.objects)].sort()
                const missing = stampDependents.filter((object) => !rebuilt.includes(object))
                const extra = rebuilt.filter((object) => !stampDependents.includes(object))
                const stray = edges.links.filter(
                    (link) => !targetDirs.some((dir) => path.resolve(buildDir, link).startsWith(path.resolve(buildDir, dir)))
                )
                console.log(
                    `[gate-check] stamp change ${run}: ${rebuilt.length} object(s), ${edges.links.length} link(s), ` +
                        `${seconds.toFixed(1)} s`
                )
                console.log(`[gate-check]   objects: ${rebuilt.join(', ') || '(none)'}`)
                console.log(`[gate-check]   links:   ${edges.links.join(', ') || '(none)'}`)
                if (build.status !== 0) {
                    problems.push(
                        `stamp change ${run}: ninja failed (${build.status}): ${build.output.trim().split('\n').slice(-3).join(' | ')}`
                    )
                }
                if (missing.length > 0) {
                    problems.push(`stamp change ${run}: ${missing.length} stamp unit(s) did NOT rebuild, first ${missing[0]}`)
                }
                if (extra.length > 0) {
                    problems.push(
                        `stamp change ${run}: ${extra.length} object(s) rebuilt that do not include the stamp header, first ${extra[0]}`
                    )
                }
                for (const dir of targetDirs) {
                    if (!edges.links.some((link) => path.resolve(buildDir, link).startsWith(path.resolve(buildDir, dir)))) {
                        problems.push(`stamp change ${run}: ${dir} was not relinked`)
                    }
                }
                if (stray.length > 0) {
                    problems.push(`stamp change ${run}: ${stray.length} unrelated target(s) relinked, first ${stray[0]}`)
                }
                measurements.push({ seconds, objects: rebuilt.length })
            }
        } finally {
            // Put the header and the binaries back: the objects were compiled with the probe value,
            // so restoring only the header would leave a binary that prints a stamp the tree does
            // not configure. Touching the header makes ninja rebuild the stamp units once more.
            for (const [header, original] of saved) {
                fs.writeFileSync(header, original.text)
                const now = new Date()
                fs.utimesSync(header, now, now)
            }
            if (saved.size > 0) {
                const restore = runNinja([])
                if (restore.status !== 0) {
                    problems.push(
                        `stamp change: restoring the tree failed (${restore.status}): ${restore.output.trim().split('\n').slice(-2).join(' | ')}`
                    )
                }
                const settled = runNinja(['-n'])
                const stillDirty = dirtyObjects(settled.output)
                if (stillDirty.length > 0) {
                    problems.push(`stamp change: ${stillDirty.length} object(s) still want to build after the measurement`)
                } else if (!settled.output.includes('no work to do')) {
                    problems.push(
                        'stamp change: ninja -n wants work after the measurement but printed no object lines; the dry-run format changed'
                    )
                }
            }
        }
        if (measurements.length > 1) {
            const times = measurements.map((measurement) => measurement.seconds)
            const spread = Math.max(...times) - Math.min(...times)
            console.log(
                `[gate-check] stamp change spread over ${times.length} runs: ` +
                    `${times.map((time) => `${time.toFixed(1)} s`).join(', ')} (${spread.toFixed(1)} s)`
            )
        }
    }
}

if (problems.length > 0) {
    console.error(problems.join('\n'))
    process.exit(1)
}
console.log('[gate-check] ok')
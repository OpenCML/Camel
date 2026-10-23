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
 * The header's timestamps are restored in a finally block; a check that dies mid-way leaves the
 * tree as it found it.
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
        'usage: node scripts/gate-time-check.mjs [--config Release|Debug] [--header <path under the repository>]'
    )
    process.exit(2)
}

const options = { config: 'Release', header: 'include/camel/core/operator.h' }
const argv = process.argv.slice(2)
for (let i = 0; i < argv.length; i++) {
    const next = () => {
        if (i + 1 >= argv.length) usage(`${argv[i]} needs a value`)
        return argv[++i]
    }
    if (argv[i] === '--config') options.config = next()
    else if (argv[i] === '--header') options.header = next()
    else if (argv[i] === '--help' || argv[i] === '-h') usage()
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

if (problems.length > 0) {
    console.error(problems.join('\n'))
    process.exit(1)
}
console.log('[gate-check] ok')
#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

//
// How many translation units a header reaches: the reading the header-weight report takes, as a
// script the plan runner can run.
//
//   node test/tools/header-weight.mjs <header> [--db <compile_commands.json>]
//
// `--db` defaults to `<repo>/build/compile_commands.json`, where `<repo>` is the directory two
// levels above this file -- not the cwd, so the reading does not depend on where it is run from (the
// report's own python required `cd build/`, which is what made it hard to repeat).
//
// `direct` counts the source files whose own `#include` names the header. `total` counts the source
// files whose **include closure** reaches it: the includes are followed, each resolved through the
// `-I` directories the database records, to whatever depth they go. Both count *source files*, the
// unit one build compiles once per configuration; the database holds several entries per file and
// the report says to multiply, not to count entries.
//
// **What `total` is not.** It is the closure over the `#include` lines the tree contains, not the
// compiler's own account of what it opened: a header reached through a macro or a generated include
// is missing, and one in a branch that is switched off is counted. A reader who wants the compiler's
// answer must ask the compiler; the report's appendix does that with `-H`, and that is a different
// reading -- the two must not be mixed in one table. This file gives the closure because it is
// deterministic (the same tree gives the same number twice) and costs nothing to run.
//
// The runner dispatches a plan case on its `tool` field, not on the extension and not on `kind`:
// without `tool = "node"` a `.mjs` case is handed to `camel` and the script's options reach it.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const INCLUDE = /^\s*#\s*include\s*[<"]([^>"]+)[>"]/

function repoRoot() {
    const here = path.dirname(fileURLToPath(import.meta.url))
    return path.dirname(path.dirname(here))
}

function parseArguments(argv) {
    const options = { header: null, db: null }
    for (let i = 0; i < argv.length; i += 1) {
        if (argv[i] === '--db') options.db = argv[++i]
        else if (!options.header) options.header = argv[i]
    }
    return options
}

function loadDatabase(dbPath) {
    const db = JSON.parse(fs.readFileSync(dbPath, 'utf8'))
    const includeDirs = []
    const sources = []
    for (const entry of db) {
        const args = entry.arguments || String(entry.command || '').split(/\s+/)
        for (let i = 0; i < args.length; i += 1) {
            let dir = null
            if (args[i] === '-I' && i + 1 < args.length) dir = args[i + 1]
            else if (args[i].startsWith('-I') && args[i].length > 2) dir = args[i].slice(2)
            if (dir && !includeDirs.includes(path.normalize(dir))) includeDirs.push(path.normalize(dir))
        }
        if (!sources.includes(entry.file)) sources.push(entry.file)
    }
    return { size: db.length, includeDirs, sources }
}

/// The includes a file names: its quoted and angled `#include` lines, in order.
function includesOf(file, cache) {
    if (cache.has(file)) return cache.get(file)
    const names = []
    try {
        for (const line of fs.readFileSync(file, 'utf8').split('\n')) {
            const match = INCLUDE.exec(line)
            if (match) names.push(match[1])
        }
    } catch {
        /* a file the database names but the tree does not have: it contributes no includes */
    }
    cache.set(file, names)
    return names
}

/// Where an include name lands: beside the including file first, then along the `-I` directories.
/// Only files inside the repository are followed -- an include that resolves into the toolchain is
/// not this tree's weight.
function resolve(name, including, includeDirs, root) {
    const candidates = [path.resolve(path.dirname(including), name)]
    for (const dir of includeDirs) candidates.push(path.resolve(root, dir, name))
    for (const candidate of candidates) {
        if (candidate.startsWith(path.resolve(root) + path.sep) && fs.existsSync(candidate)) {
            return candidate
        }
    }
    return null
}

/// Whether the header is anywhere in the file's include closure. `seen` is per source file, so a
/// cycle between headers costs nothing and cannot loop.
function reaches(file, header, root, includeDirs, cache, seen) {
    if (seen.has(file)) return false
    seen.add(file)
    for (const name of includesOf(file, cache)) {
        const target = resolve(name, file, includeDirs, root)
        if (target === null) continue
        if (target === header) return true
        if (reaches(target, header, root, includeDirs, cache, seen)) return true
    }
    return false
}

function main() {
    const options = parseArguments(process.argv.slice(2))
    if (!options.header) {
        console.error('usage: node test/tools/header-weight.mjs <header> [--db <compile_commands.json>]')
        return 2
    }
    const root = repoRoot()
    const dbPath = options.db || path.join(root, 'build', 'compile_commands.json')
    const header = path.resolve(root, options.header.replace(/\\/g, '/'))
    if (!fs.existsSync(dbPath)) {
        console.error(`header-weight: no compile database at ${dbPath}`)
        return 2
    }

    const { size, includeDirs, sources } = loadDatabase(dbPath)
    const cache = new Map()
    let direct = 0
    let total = 0
    for (const source of sources) {
        const file = path.isAbsolute(source) ? source : path.resolve(root, source)
        const here = includesOf(file, cache).some((name) => resolve(name, file, includeDirs, root) === header)
        if (here) {
            direct += 1
            total += 1
            continue
        }
        if (reaches(file, header, root, includeDirs, cache, new Set([file]))) total += 1
    }

    const lines = [
        `header: ${path.relative(root, header)}`,
        `db: ${dbPath} entries=${size} sources=${sources.length}`,
        `direct: ${direct}`,
        `total: ${total} of ${sources.length} source file(s) name it or reach it through includes`
    ]
    for (const line of lines) console.log(line)
    // A plan reports a passing case by its `[SUMMARY]`, not by echoing stdout, so the reading is also
    // written where another lane can pick it up without re-running it.
    const record = path.join(root, 'test', 'results', 'header-weight.txt')
    try {
        fs.mkdirSync(path.dirname(record), { recursive: true })
        fs.writeFileSync(record, `${lines.join('\n')}\n`)
    } catch {
        /* the reading is on stdout either way */
    }
    return 0
}

process.exit(main())
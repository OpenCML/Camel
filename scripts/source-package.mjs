#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What a checkout carries besides the source.
//
// A tree that was built, tested and rolled up is not the tree that was cloned, and the difference
// is every file that is not source: build output, caches, the install, test scratch, editor
// residue, the state files our own tooling writes. Nothing in that set is worth shipping, and some
// of it is worth not shipping twice over -- `out/` is 300 MB of binaries, `test/results/` is one
// directory per run, and `internal/tools/camel-gate.local.json` names a machine.
//
//   node scripts/source-package.mjs                 list every trace, exit 1 if there is one
//   node scripts/source-package.mjs --table         the same inventory as a markdown table
//   node scripts/source-package.mjs --clean         delete them, then print what went
//   node scripts/source-package.mjs --clean --dry-run
//                                                   print `would remove <path>`, delete nothing
//   node scripts/source-package.mjs --root <dir>    inventory that checkout (default: this one)
//
// It needs no network, no `node_modules` and no `.git`: the classes below are declared, not derived
// from `git status`, because the tree this is meant to check is the one that was extracted from an
// archive and has no history to ask. `scripts/clean.js` answers the narrower question "may I delete
// this build's output"; this answers "is this tree still only source", which is the question a
// package being handed to somebody else asks.
//
// The classes, and why each is a trace rather than an input:
//
//   build-tree      `build/`, `out/`, `dist/` -- produced by `npm run build`, never read by it
//   caches          `.cache/`, `.ccache/`, `node_modules/`, `__pycache__/`, `.venv/`, `vendor/`
//   test-scratch    `tmp/`, `test/results/`, profiler output -- one directory per run
//   binaries        `.cmo` / `.so` / `.dll` / `.exe` beside the sources (scripts/check-source-artifacts.mjs
//                   owns that scan; this reads it so the two cannot disagree)
//   tool-state      `.camel-opts.json`, `internal/tools/camel-gate.local.json`, `.pypirc`, ...
//   editor-residue  `.idea/`, `.vs/`, `.cursor/`, `.claude/`, and the local goal/report notes
//   package-staging `pypi/camel-lang/{bin,stdlib,dist,build,src/camel/.cmlenv}`, `*.egg-info/`,
//                   `src/stdlib/{sdks,dlls}` -- the wheel's ingredients and its vendored runtimes
//
// Files that carry identity rather than process (`pyproject.toml`'s authors, the copyright line,
// the repository URL in a document) are *tracked source* and therefore outside this table: deleting
// them would break the build. They are a separate pass over tracked files, not this one.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { scanSourceArtifacts } from './check-source-artifacts.mjs'
import { sourceOnlyReason } from './clean.js'

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url))
const HERE = path.resolve(SCRIPT_DIR, '..')

// What is source rather than a trace, even though a class above would match its shape: the standard
// library and the one tracked `.cmo` are `sourceOnlyReason` in `scripts/clean.js`, read from there so
// that this inventory and `npm run clean` cannot name different things.
const TRACKED_EDITOR_FILES = new Set([
    '.vscode/launch.json',
    '.vscode/tasks.json',
    '.vscode/settings.json'
])

/** @type {{id: string, label: string, why: string, paths: string[], globs?: string[]}[]} */
const CLASSES = [
    {
        id: 'build-tree',
        label: 'Build tree and installs',
        why: '`npm run build` writes these and reads none of them',
        paths: ['build', 'out', 'dist', 'CMakeUserPresets.json'],
        globs: ['cmake-build-*', 'out_lr*']
    },
    {
        id: 'caches',
        label: 'Caches and installed packages',
        why: 'compiler, npm and Python caches; reproducible by rebuilding',
        paths: ['node_modules', 'vendor', 'legacy', '.venv', '.ccache', '.cache'],
        globs: ['**/node_modules', '**/__pycache__', '**/.cache']
    },
    {
        id: 'test-scratch',
        label: 'Test and profiler scratch',
        why: 'one directory per run, under tmp/ and test/results/',
        paths: ['tmp', 'test/results', 'profile_reports', 'perf.data', 'perf.data.old', 'profile.json.gz', 'result.json'],
        globs: []
    },
    {
        id: 'binaries',
        label: 'Binaries beside the sources',
        why: 'a module binary or a linker output in the source tree (scripts/check-source-artifacts.mjs)',
        paths: ['camel.exe', 'libcamel.dll', 'libcamel.so', 'libcamel.dylib', 'python311.dll'],
        globs: []
    },
    {
        id: 'tool-state',
        label: 'State written by our own tooling',
        why: 'names this machine or this run: the gate config, the remembered CMake options, the device tree',
        paths: [
            '.camel-opts.json',
            '.todolist',
            '.opencmlrc',
            '.pypirc',
            'internal/tools/camel-gate.local.json',
            'CMakeUserPresets.json'
        ],
        globs: ['.camel-device-tree-v*']
    },
    {
        id: 'editor-residue',
        label: 'Editor and local notes',
        why: 'what an editor or an agent wrote into the checkout',
        paths: ['.idea', '.vs', '.cursor', '.claude', 'LOG.md', 'GOAL.md', 'REVIEW.md', 'goal', 'scratch'],
        globs: []
    },
    {
        id: 'package-staging',
        label: 'Package staging and vendored runtimes',
        why: 'the wheel\'s ingredients (copied out of out/<tag>/) and the Python trees that were synced in',
        paths: [
            'pypi/camel-lang/bin',
            'pypi/camel-lang/stdlib',
            'pypi/camel-lang/wrapper-dist',
            'pypi/camel-lang/dist',
            'pypi/camel-lang/build',
            'pypi/camel-lang/src/camel/.cmlenv',
            'pypi/camel-lang/python.stash',
            'pypi/camel-lang/src/camel_lang.egg-info',
            'src/stdlib/sdks',
            'src/stdlib/dlls'
        ],
        globs: ['*.egg-info']
    }
]

function toPosix(rel) {
    return rel.split(path.sep).join('/')
}

function usage(message) {
    if (message) {
        console.error(`source-package: ${message}`)
    }
    console.error(
        'usage: node scripts/source-package.mjs [--table] [--clean [--dry-run]] [--root <dir>]'
    )
    process.exit(2)
}

function sizeOf(abs) {
    let stat
    try {
        stat = fs.lstatSync(abs)
    } catch {
        return 0
    }
    if (!stat.isDirectory()) {
        return stat.size
    }
    let total = 0
    let entries
    try {
        entries = fs.readdirSync(abs, { withFileTypes: true })
    } catch {
        return 0
    }
    for (const entry of entries) {
        if (entry.name === '.git') continue
        total += sizeOf(path.join(abs, entry.name))
    }
    return total
}

/** Relative paths matching `pattern` under `root`, `*` within one segment and `**` across segments. */
function globRelative(root, pattern, prune) {
    const segments = pattern.split('/')
    const found = []
    const children = (dir) => {
        try {
            return fs.readdirSync(dir, { withFileTypes: true })
        } catch {
            return []
        }
    }
    const walk = (dir, index, relPrefix) => {
        if (index >= segments.length) return
        const segment = segments[index]
        // `**` is zero or more directories: match the rest here, and also descend one level.
        if (segment === '**') {
            walk(dir, index + 1, relPrefix)
            for (const entry of children(dir)) {
                if (entry.name === '.git' || !entry.isDirectory()) continue
                const rel = relPrefix ? `${relPrefix}/${entry.name}` : entry.name
                if (prune.has(rel)) continue
                walk(path.join(dir, entry.name), index, rel)
            }
            return
        }
        const last = index === segments.length - 1
        for (const entry of children(dir)) {
            if (entry.name === '.git') continue
            if (!matchSegment(segment, entry.name)) continue
            const rel = relPrefix ? `${relPrefix}/${entry.name}` : entry.name
            if (last) {
                found.push(rel)
            } else if (entry.isDirectory()) {
                walk(path.join(dir, entry.name), index + 1, rel)
            }
        }
    }
    walk(root, 0, '')
    return found
}

function matchSegment(pattern, name) {
    if (!pattern.includes('*')) return pattern === name
    const escaped = pattern.replace(/[.+^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '[^/]*')
    return new RegExp(`^${escaped}$`).test(name)
}

function isKept(rel) {
    return sourceOnlyReason(rel) !== null || TRACKED_EDITOR_FILES.has(rel)
}

/** Every trace under `root`, as `{ classId, classLabel, why, rel, bytes }`. */
export function scanSourcePackage(root) {
    /** @type {Map<string, {classId: string, classLabel: string, why: string, rel: string, abs: string}>} */
    const byRel = new Map()
    const add = (classId, classLabel, why, rel, abs) => {
        const posix = toPosix(rel)
        if (byRel.has(posix) || isKept(posix)) return
        // One entry per subtree: `editors/vscode/node_modules` is one trace, not one per package
        // inside it -- a hit under a directory already reported says nothing new.
        for (const reported of byRel.keys()) {
            if (posix.startsWith(`${reported}/`)) return
        }
        // ... and the opposite order (a glob finds a child before the parent is added) keeps the
        // parent and drops the child.
        for (const reported of [...byRel.keys()]) {
            if (reported.startsWith(`${posix}/`)) byRel.delete(reported)
        }
        byRel.set(posix, { classId, classLabel, why, rel: posix, abs })
    }

    for (const group of CLASSES) {
        for (const rel of group.paths) {
            const abs = path.resolve(root, rel)
            const inside = path.relative(root, abs)
            if (inside.startsWith('..') || path.isAbsolute(inside)) continue
            if (!fs.existsSync(abs)) continue
            // A tracked editor file inside a declared directory stays (`TRACKED_EDITOR_FILES`).
            add(group.id, group.label, group.why, inside || rel, abs)
        }
        // The declared directories, as relative paths: a glob does not need to look inside them.
        const prune = new Set()
        for (const other of CLASSES) {
            for (const rel of other.paths) {
                const abs = path.resolve(root, rel)
                try {
                    if (fs.statSync(abs).isDirectory()) prune.add(toPosix(path.relative(root, abs)))
                } catch {
                    // a path that is not there prunes nothing
                }
            }
        }
        for (const pattern of group.globs) {
            for (const rel of globRelative(root, pattern, prune)) {
                add(group.id, group.label, group.why, rel, path.resolve(root, rel))
            }
        }
    }

    // Binaries beside the sources: the same scan the build runs, so a `.cmo` this table would miss
    // is a `.cmo` the build would have failed on already.
    const { artifacts } = scanSourceArtifacts(root)
    for (const rel of artifacts) {
        add(
            'binaries',
            'Binaries beside the sources',
            'a module binary or a linker output in the source tree (scripts/check-source-artifacts.mjs)',
            rel,
            path.resolve(root, rel)
        )
    }

    // Sizes are read last, once the set is final: a directory that was dropped in favour of its
    // parent never gets walked.
    return [...byRel.values()]
        .map((trace) => ({ ...trace, bytes: sizeOf(trace.abs) }))
        .sort((a, b) => (a.classId === b.classId ? a.rel.localeCompare(b.rel) : a.classId.localeCompare(b.classId)))
}

function megabytes(bytes) {
    return (bytes / (1024 * 1024)).toFixed(1)
}

function main() {
    const args = process.argv.slice(2)
    let clean = false
    let dryRun = false
    let table = false
    let rootArg = HERE
    for (let i = 0; i < args.length; i += 1) {
        const arg = args[i]
        if (arg === '--clean') clean = true
        else if (arg === '--dry-run') dryRun = true
        else if (arg === '--table') table = true
        else if (arg === '--root') {
            if (i + 1 >= args.length || args[i + 1].startsWith('--')) usage('--root needs a directory')
            rootArg = args[(i += 1)]
        } else usage(`unknown argument: ${arg}`)
    }
    if (dryRun && !clean) usage('--dry-run is only meaningful with --clean')
    const root = path.resolve(rootArg)
    if (!fs.existsSync(root)) usage(`no such directory: ${rootArg}`)

    const traces = scanSourcePackage(root)

    if (table) {
        const byClass = new Map()
        for (const trace of traces) {
            const entry = byClass.get(trace.classId) ?? { count: 0, bytes: 0 }
            entry.count += 1
            entry.bytes += trace.bytes
            byClass.set(trace.classId, entry)
        }
        console.log('| class | files/dirs | size | why it is not source |')
        console.log('|---|---|---|---|')
        for (const group of CLASSES) {
            const entry = byClass.get(group.id)
            console.log(
                `| \`${group.id}\` | ${entry ? entry.count : 0} | ${megabytes(entry ? entry.bytes : 0)} MB | ${group.why} |`
            )
        }
        const total = traces.reduce((sum, trace) => sum + trace.bytes, 0)
        console.log(`| **all** | ${traces.length} | ${megabytes(total)} MB | |`)
        return 0
    }

    if (clean) {
        let removed = 0
        let bytes = 0
        for (const trace of traces) {
            if (dryRun) {
                console.log(`would remove ${trace.rel} (${trace.classId})`)
                continue
            }
            fs.rmSync(path.resolve(root, trace.rel), { recursive: true, force: true })
            console.log(`removed ${trace.rel} (${trace.classId})`)
            removed += 1
            bytes += trace.bytes
        }
        console.log(
            dryRun
                ? `source-package: ${traces.length} trace(s) would be removed; nothing was deleted`
                : `source-package: ${removed} trace(s), ${megabytes(bytes)} MB removed`
        )
        return 0
    }

    if (traces.length === 0) {
        console.log('source-package: this tree carries no process trace; it is only source')
        return 0
    }
    for (const trace of traces) {
        console.log(`${trace.rel}: ${trace.classId} (${megabytes(trace.bytes)} MB) -- ${trace.why}`)
    }
    const total = traces.reduce((sum, trace) => sum + trace.bytes, 0)
    console.error(
        `source-package: ${traces.length} process trace(s), ${megabytes(total)} MB; ` +
            'run with --clean to remove them (--table for the inventory by class)'
    )
    return 1
}

const THIS_FILE = path.join(SCRIPT_DIR, 'source-package.mjs')
const entry = process.argv[1] ? path.resolve(process.argv[1]) : ''
if (process.platform === 'win32' ? entry.toLowerCase() === THIS_FILE.toLowerCase() : entry === THIS_FILE) {
    process.exit(main())
}
#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

//  `npm run clean`.
//
// A build, an install and a test run each leave a tree behind that no source file needs. This
// command removes them, and it never needs a build: it only reads and deletes paths.
//
//   npm run clean                    build + out + test scratch + polluted source files
//   npm run clean -- --build         build/ and cmake-build-*/
//   npm run clean -- --out           out/<tag>/ (out/latest, out/debug, release tag mirrors)
//   npm run clean -- --tmp           tmp/test/run-* and test/results/log
//   npm run clean -- --source        the files scripts/check-source-artifacts.mjs reports
//   npm run clean -- --all           all four tiers, spelled out (the same set as the default)
//   npm run clean -- --dry-run       print `would remove <path>`; delete nothing
//   npm run clean -- --root <dir>    clean that checkout instead of this one
//
// The default is a safe delete, not a dry run: every default tier is git-ignored and can be
// reproduced by the next build or test run, and the checks below are enforced rather than
// advisory, so running it blind on a checkout with local work cannot lose that work.
//
// Safety, in the order each candidate is checked:
//
//   1. it must resolve inside `--root` (default: this checkout) — a symlink that points outside
//      is left alone, and a path outside the repository is never a candidate;
//   2. it must belong to one of the four tiers above;
//   3. `.git` is never deleted. A directory candidate that contains a `.git` entry anywhere is
//      left in place and named, because a recursive delete would take it too;
//   4. `stdlib/*.cml`, the tracked fixture `test/cases/modules/pass_not_found/unloadable.cmo`,
//      and any path `git ls-files` reports are never deleted. A tracked path inside a tier is
//      reported as skipped and makes the command exit 1;
//   5. the delete is `fs.rmSync`, never a shell `rm -rf`; the path was resolved and checked first.
//
// `out/latest` is removed by the `out` tier and the default. There is no keep flag: a caller who
// wants the install to survive asks for `--build --tmp`, which is narrower and explicit.
//
// `node scripts/check-source-artifacts.mjs --clean` delegates here (`--source`), so the guard and
// `npm run clean` have one implementation of the delete.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { BASEDIR } from './common.js'
import { scanSourceArtifacts } from './check-source-artifacts.mjs'
import { LOG_DIR, SCRATCH_DIR, runnerOutputPath } from '../test/tools/runner-output.mjs'

const THIS_FILE = fileURLToPath(import.meta.url)
const TIERS = ['build', 'out', 'tmp', 'source']
const TRACKED_FIXTURE = 'test/cases/modules/pass_not_found/unloadable.cmo'
const GIT_MAX_BUFFER = 64 * 1024 * 1024

function usage(message) {
    if (message) {
        console.error(`clean: ${message}`)
    }
    console.error(
        'usage: node scripts/clean.js [--build] [--out] [--tmp] [--source] [--all] [--dry-run] [--root <dir>]\n' +
            '       no tier flag selects all four tiers (the default)'
    )
    process.exit(2)
}

function toPosix(rel) {
    return rel.split(path.sep).join('/')
}

function isInside(root, target) {
    const rel = path.relative(root, target)
    return rel === '' || (!rel.startsWith('..') && !path.isAbsolute(rel))
}

/** The reason `rel` does not belong in the list, or null when it may be removed. */
function protectedReason(rel) {
    if (rel.split('/').includes('.git')) {
        return '.git is never touched'
    }
    if (/^stdlib\/[^/]+\.cml$/.test(rel)) {
        return 'stdlib/*.cml is source'
    }
    if (rel === TRACKED_FIXTURE) {
        return 'the tracked .cmo test fixture'
    }
    return null
}

/** The path from `abs` down to the first `.git` entry, or null when the tree has none. */
function findGitEntry(abs, rel) {
    let entries
    try {
        entries = fs.readdirSync(abs, { withFileTypes: true })
    } catch {
        return null
    }
    for (const entry of entries) {
        const childRel = `${rel}/${entry.name}`
        if (entry.name === '.git') {
            return childRel
        }
        if (entry.isDirectory()) {
            const found = findGitEntry(path.join(abs, entry.name), childRel)
            if (found) {
                return found
            }
        }
    }
    return null
}

/**
 * Resolve a candidate against the checkout's real root. Returns null when it is not provably
 * inside the repository (a broken link, a link whose target is elsewhere, or a path that escaped
 * by `..`). The returned `abs` is the path to delete: deleting a symlink removes the link, not
 * its target.
 */
function resolveCandidate(root, rel) {
    const abs = path.resolve(root, rel)
    if (!isInside(root, abs)) {
        return null
    }
    let real = null
    try {
        real = fs.realpathSync(abs)
    } catch {
        real = null
    }
    if (real !== null) {
        return isInside(root, real) ? { abs, real } : null
    }
    let realParent
    try {
        realParent = fs.realpathSync(path.dirname(abs))
    } catch {
        realParent = null
    }
    return realParent !== null && isInside(root, realParent) ? { abs, real: null } : null
}

function trackedPaths(root) {
    const result = spawnSync('git', ['-C', root, 'ls-files', '-z'], {
        maxBuffer: GIT_MAX_BUFFER,
        windowsHide: true
    })
    if (result.error || result.status !== 0 || !result.stdout) {
        return null
    }
    return new Set(
        result.stdout
            .toString('utf8')
            .split('\0')
            .filter(Boolean)
            .map((rel) => rel.split(path.sep).join('/'))
    )
}

function isTracked(tracked, rel) {
    if (tracked === null) {
        return false
    }
    if (tracked.has(rel)) {
        return true
    }
    const prefix = `${rel}/`
    for (const file of tracked) {
        if (file.startsWith(prefix)) {
            return true
        }
    }
    return false
}

function buildCandidates(root) {
    const rels = []
    if (fs.existsSync(path.join(root, 'build'))) {
        rels.push('build')
    }
    let entries = []
    try {
        entries = fs.readdirSync(root, { withFileTypes: true })
    } catch {
        entries = []
    }
    const cmakeDirs = entries
        .filter((entry) => entry.name.startsWith('cmake-build-') && (entry.isDirectory() || entry.isSymbolicLink()))
        .map((entry) => entry.name)
        .sort()
    return [...rels, ...cmakeDirs]
}

function outCandidates(root) {
    const outDir = path.join(root, 'out')
    if (!fs.existsSync(outDir)) {
        return []
    }
    let entries = []
    try {
        entries = fs.readdirSync(outDir, { withFileTypes: true })
    } catch {
        return []
    }
    return entries.map((entry) => `out/${entry.name}`).sort()
}

function tmpCandidates(root) {
    const rels = []
    const runRoot = runnerOutputPath(SCRATCH_DIR, root)
    if (fs.existsSync(runRoot)) {
        let entries = []
        try {
            entries = fs.readdirSync(runRoot, { withFileTypes: true })
        } catch {
            entries = []
        }
        for (const entry of entries) {
            if (entry.name.startsWith('run-') && (entry.isDirectory() || entry.isSymbolicLink())) {
                rels.push(`${SCRATCH_DIR}/${entry.name}`)
            }
        }
    }
    // The run's record is one tree: the retention keeps the newest run directories in it, this tier
    // removes the tree. Both name it through test/tools/runner-output.mjs, which is the one
    // definition of the paths a run writes.
    if (fs.existsSync(runnerOutputPath(LOG_DIR, root))) {
        rels.push(LOG_DIR)
    }
    return rels.sort()
}

function sourceCandidates(root) {
    return scanSourceArtifacts(root).artifacts.map(toPosix).sort()
}

function collect(root, tiers) {
    const candidates = []
    const add = (tier, rels) => {
        for (const rel of rels) {
            candidates.push({ tier, rel })
        }
    }
    if (tiers.has('build')) add('build', buildCandidates(root))
    if (tiers.has('out')) add('out', outCandidates(root))
    if (tiers.has('tmp')) add('tmp', tmpCandidates(root))
    if (tiers.has('source')) add('source', sourceCandidates(root))
    return candidates
}

function parseArgs(argv) {
    const selected = new Set()
    let dryRun = false
    let root = BASEDIR
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        switch (arg) {
            case '--build':
            case '--out':
            case '--tmp':
            case '--source':
                selected.add(arg.slice(2))
                break
            case '--all':
                for (const tier of TIERS) selected.add(tier)
                break
            case '--dry-run':
                dryRun = true
                break
            case '--root': {
                const value = argv[++i]
                if (!value || value.startsWith('--')) usage('--root needs a directory')
                root = value
                break
            }
            case '--help':
            case '-h':
                usage()
                break
            default:
                usage(`unknown argument: ${arg}`)
        }
    }
    if (selected.size === 0) {
        for (const tier of TIERS) selected.add(tier)
    }
    return { tiers: selected, dryRun, root }
}

function main() {
    const options = parseArgs(process.argv.slice(2))
    const root = path.resolve(options.root)
    if (!fs.existsSync(root) || !fs.statSync(root).isDirectory()) {
        usage(`no such directory: ${options.root}`)
    }
    const rootReal = fs.realpathSync(root)
    const tiers = options.tiers
    const tierList = TIERS.filter((tier) => tiers.has(tier)).join(', ')
    console.log(`clean: ${tierList}${options.dryRun ? ' (dry run; nothing is deleted)' : ''}`)

    const tracked = trackedPaths(rootReal)
    if (tracked === null) {
        console.log('clean: git is unavailable; tracked paths cannot be recognised, the tier and path rules still apply')
    }

    const candidates = collect(rootReal, tiers)
    let removed = 0
    let skipped = 0
    const skip = (rel, reason) => {
        skipped++
        console.log(`clean: skip ${rel}: ${reason}`)
    }

    for (const { rel } of candidates) {
        const reason = protectedReason(rel)
        if (reason !== null) {
            skip(rel, reason)
            continue
        }
        const resolved = resolveCandidate(rootReal, rel)
        if (resolved === null) {
            skip(rel, 'does not resolve inside the repository')
            continue
        }
        let isDirectory = false
        try {
            isDirectory = fs.lstatSync(resolved.abs).isDirectory()
        } catch {
            isDirectory = false
        }
        if (isDirectory) {
            const gitEntry = findGitEntry(resolved.abs, rel)
            if (gitEntry !== null) {
                skip(rel, `contains ${gitEntry}; .git is never deleted`)
                continue
            }
        }
        if (isTracked(tracked, rel)) {
            skip(rel, 'tracked by git')
            continue
        }
        if (options.dryRun) {
            console.log(`would remove ${rel}`)
            removed++
            continue
        }
        try {
            fs.rmSync(resolved.abs, { recursive: true, force: true })
            console.log(`removed ${rel}`)
            removed++
        } catch (error) {
            skipped++
            console.log(`clean: could not remove ${rel}: ${error.message}`)
        }
    }

    if (removed === 0 && skipped === 0) {
        console.log('clean: nothing to remove')
    } else {
        const verb = options.dryRun ? 'would be removed' : 'removed'
        console.log(
            `clean: ${removed} path(s) ${verb}${skipped > 0 ? `; ${skipped} path(s) left in place` : ''}`
        )
    }
    if (skipped > 0) {
        process.exitCode = 1
    }
}

const entry = process.argv[1] ? path.resolve(process.argv[1]) : ''
const self = path.resolve(THIS_FILE)
if (process.platform === 'win32' ? entry.toLowerCase() === self.toLowerCase() : entry === self) {
    main()
}
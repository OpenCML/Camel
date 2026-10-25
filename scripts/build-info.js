#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * `out/<tag>/build-info.json`: what a build stamped, and whether an install still matches the tree.
 *
 * The stamp is written by the build itself (`scripts/common.js: writeBuildInfo`, called at the end
 * of `collect-out`). This script never writes during a build; it reads the stamp and compares it
 * with the tree the caller is about to verify.
 *
 *   node scripts/build-info.js check --tag latest [--allow-stale] [--root DIR]
 *   node scripts/build-info.js show  --tag latest [--root DIR]
 *   node scripts/build-info.js stamp --tag TAG [--config Release] [--root DIR]
 *
 * `check` is what `camel-remote.ps1 -NoBuild` runs on the host before the plan:
 *
 *   * exit 0 -- the install was built from the same source content as this tree; say nothing;
 *   * exit 4 -- refuse: the stamp is missing, unusable, or names different sources. The message
 *     names both revisions (HEAD and dirty state), both fingerprints, and the files that moved;
 *   * exit 2 -- usage error.
 *
 * `--allow-stale` is the explicit override behind `camel-remote.ps1 -AllowStale`: it prints the
 * same refusal text, adds that it is continuing anyway, and exits 0. It is never implied.
 *
 * Why content and not `HEAD`: a commit that only touches `docs/`, `test/` or `scripts/` changes the
 * commit but not one byte the build reads, so refusing there would cry wolf. The fingerprint covers
 * exactly the paths `common.js: BUILD_INFO_ROOTS` lists (C++ sources and headers, modules, tools,
 * third_party, stdlib, CMake glue, conanfile); see the comment there for why each is in and what is
 * out (docs/test/scripts are inputs to nothing that lands in the install).
 */
import fs from 'fs'
import path from 'path'
import {
    BASEDIR,
    BUILD_INFO_FILE,
    BUILD_INFO_SCHEMA,
    FINGERPRINT_VERSION,
    buildInfoPath,
    gitState,
    sourceFingerprint,
    writeBuildInfo
} from './common.js'

const EXIT_OK = 0
const EXIT_USAGE = 2
const EXIT_STALE = 4

const MAX_LISTED_FILES = 20

function usage(message) {
    if (message) {
        console.error(`[build-info] ${message}`)
    }
    console.error('usage: node scripts/build-info.js check|show|stamp [--tag latest|debug|<tag>] [--allow-stale] [--config Release] [--root DIR]')
    process.exit(EXIT_USAGE)
}

function parseArgs(argv) {
    const options = { command: null, tag: 'latest', allowStale: false, config: 'Release', root: BASEDIR }
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        const next = () => {
            if (i + 1 >= argv.length) usage(`${arg} needs a value`)
            return argv[++i]
        }
        if (arg === 'check' || arg === 'show' || arg === 'stamp') {
            options.command = arg
        } else if (arg === '--tag') {
            options.tag = next()
        } else if (arg.startsWith('--tag=')) {
            options.tag = arg.slice('--tag='.length)
        } else if (arg === '--config') {
            options.config = next()
        } else if (arg.startsWith('--config=')) {
            options.config = arg.slice('--config='.length)
        } else if (arg === '--root') {
            options.root = path.resolve(next())
        } else if (arg.startsWith('--root=')) {
            options.root = path.resolve(arg.slice('--root='.length))
        } else if (arg === '--allow-stale') {
            options.allowStale = true
        } else if (arg === '--help' || arg === '-h') {
            usage()
        } else {
            usage(`unknown argument "${arg}"`)
        }
    }
    if (!options.command) {
        usage('a command is required')
    }
    return options
}

function revisionLabel(git) {
    if (!git.available) return 'unknown revision (git unavailable)'
    if (!git.revision) return 'unknown revision'
    const sha = git.revision.slice(0, 10)
    if (git.dirty === true) return `${sha} (dirty: ${git.dirtyCount} file${git.dirtyCount === 1 ? '' : 's'})`
    if (git.dirty === false) return `${sha} (clean)`
    return sha
}

function fingerprintLabel(fingerprint) {
    if (!fingerprint || typeof fingerprint.hash !== 'string') return 'none'
    return `${fingerprint.hash.slice(0, 12)} (${fingerprint.files} source files)`
}

function changedFiles(stampFingerprint, currentFingerprint) {
    const before = stampFingerprint.fileHashes || {}
    const after = currentFingerprint.fileHashes || {}
    const added = []
    const removed = []
    const modified = []
    for (const [rel, hash] of Object.entries(after)) {
        if (!Object.hasOwn(before, rel)) {
            added.push(rel)
        } else if (before[rel] !== hash) {
            modified.push(rel)
        }
    }
    for (const rel of Object.keys(before)) {
        if (!Object.hasOwn(after, rel)) {
            removed.push(rel)
        }
    }
    return { added: added.sort(), removed: removed.sort(), modified: modified.sort() }
}

function printChangedFiles(stampFingerprint, currentFingerprint) {
    const { added, removed, modified } = changedFiles(stampFingerprint, currentFingerprint)
    const total = added.length + removed.length + modified.length
    if (total === 0) {
        // Same aggregate hash with no per-file difference cannot normally happen; say so rather
        // than print an empty list that looks like nothing changed.
        console.log('  (the aggregate fingerprint differs, but no listed file does -- the stamp and this tree disagree about which files exist)')
        return
    }
    console.log(`  changed since the build: ${total} source file${total === 1 ? '' : 's'}`)
    const lines = []
    for (const rel of modified) lines.push(`    ~ ${rel}`)
    for (const rel of added) lines.push(`    + ${rel} (new)`)
    for (const rel of removed) lines.push(`    - ${rel} (gone)`)
    for (const line of lines.slice(0, MAX_LISTED_FILES)) {
        console.log(line)
    }
    if (lines.length > MAX_LISTED_FILES) {
        console.log(`    ... and ${lines.length - MAX_LISTED_FILES} more`)
    }
}

function printMismatch(tag, stamp, current, allowStale) {
    const stampGit = stamp.git === false
        ? { available: false }
        : { available: true, revision: stamp.revision, dirty: stamp.dirty, dirtyCount: stamp.dirtyCount }
    console.log(`[camel-remote] -NoBuild refused: out/${tag} was built from different sources than this tree.`)
    console.log(`  built from: ${revisionLabel(stampGit)}, at ${stamp.builtAt || 'unknown time'}`)
    console.log(`              fingerprint ${fingerprintLabel(stamp.fingerprint)}`)
    console.log(`  this tree:  ${revisionLabel(current.git)}`)
    console.log(`              fingerprint ${fingerprintLabel(current.fingerprint)}`)
    printChangedFiles(stamp.fingerprint, current.fingerprint)
    console.log('  The plan would run the old binary and the old out/' + tag + '/stdlib, not this tree.')
    if (allowStale) {
        console.log('  -AllowStale was given: continuing anyway; this result does not describe this tree.')
    } else {
        console.log('  Re-run without -NoBuild (it builds this tree and stamps the install), or pass -AllowStale to run anyway.')
    }
}

function refuse(message, allowStale) {
    console.log(`[camel-remote] -NoBuild refused: ${message}`)
    if (allowStale) {
        console.log('  -AllowStale was given: continuing anyway; this result does not describe this tree.')
        return
    }
    process.exit(EXIT_STALE)
}

function commandCheck(options) {
    const { tag, root } = options
    const stampPath = buildInfoPath(tag, root)
    if (!fs.existsSync(stampPath)) {
        refuse(
            `out/${tag} has no ${BUILD_INFO_FILE}.\n` +
                '  The install was never built, or it was collected by an older build that did not stamp.\n' +
                '  Without the stamp there is no way to tell which sources produced it.\n' +
                `  Run once without -NoBuild: it builds this tree and stamps out/${tag}.`,
            options.allowStale
        )
        return
    }

    let stamp
    try {
        stamp = JSON.parse(fs.readFileSync(stampPath, 'utf-8'))
    } catch (error) {
        refuse(
            `out/${tag}/${BUILD_INFO_FILE} is not readable JSON (${error.message}).\n` +
                `  Run once without -NoBuild to rebuild and restamp out/${tag}.`,
            options.allowStale
        )
        return
    }
    if (!stamp || stamp.schema !== BUILD_INFO_SCHEMA || !stamp.fingerprint ||
        stamp.fingerprint.version !== FINGERPRINT_VERSION) {
        refuse(
            `out/${tag}/${BUILD_INFO_FILE} was written by a different form of this guard ` +
                `(schema ${stamp && stamp.schema}, fingerprint ${stamp && stamp.fingerprint && stamp.fingerprint.version}; ` +
                `this build writes schema ${BUILD_INFO_SCHEMA}, fingerprint ${FINGERPRINT_VERSION}).\n` +
                `  Run once without -NoBuild to rebuild and restamp out/${tag}.`,
            options.allowStale
        )
        return
    }

    const current = { git: gitState(root), fingerprint: sourceFingerprint(root) }
    if (stamp.fingerprint.hash === current.fingerprint.hash) {
        return // The install matches these sources: no new noise on a correct run.
    }

    printMismatch(tag, stamp, current, options.allowStale)
    if (!options.allowStale) {
        process.exit(EXIT_STALE)
    }
}

function commandShow(options) {
    const { tag, root } = options
    const stampPath = buildInfoPath(tag, root)
    if (!fs.existsSync(stampPath)) {
        console.error(`[build-info] no ${path.relative(root, stampPath)}`)
        process.exit(EXIT_STALE)
    }
    process.stdout.write(fs.readFileSync(stampPath, 'utf-8'))
    const stamp = JSON.parse(fs.readFileSync(stampPath, 'utf-8'))
    const current = sourceFingerprint(root)
    console.log(`current tree fingerprint: ${fingerprintLabel(current)}`)
    console.log(stamp.fingerprint.hash === current.hash ? 'out/' + tag + ' matches this tree' : 'out/' + tag + ' does NOT match this tree')
}

function main() {
    const options = parseArgs(process.argv.slice(2))
    if (options.command === 'check') {
        commandCheck(options)
    } else if (options.command === 'show') {
        commandShow(options)
    } else {
        // `stamp` writes the same file collect-out writes. It exists for the report's local
        // preflight and for inspecting what a build would record; it is not part of a build.
        writeBuildInfo(options.config, options.tag, options.root)
    }
}

main()
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
 *   node scripts/build-info.js check --tag latest [--allow-stale] [--config Release|Debug] [--root DIR]
 *   node scripts/build-info.js check --install DIR [--config Release|Debug] [--root DIR]
 *   node scripts/build-info.js show  --tag latest [--root DIR]
 *   node scripts/build-info.js show  --install DIR [--root DIR]
 *   node scripts/build-info.js stamp --tag TAG [--config Release] [--root DIR]
 *
 * `check` and `show` take `--install DIR` instead of `--tag`: they read `DIR/build-info.json` and
 * compare it with `--root`'s sources exactly as `--tag` does. A stamp lives inside the install it
 * describes, so an install outside this tree's `out/` is read by its own path rather than assumed
 * uncomparable; the fingerprint then decides whether it is this tree's build. (`stamp` writes the
 * file a build writes, so it names a tag and takes no `--install`.)
 *
 * `check` is what `camel-remote.ps1 -NoBuild` runs on the host before the plan:
 *
 *   * exit 0 -- the install was built from the same source content and the same build options as
 *     this tree; say nothing;
 *   * exit 4 -- refuse: the stamp is missing, unusable, or names different sources or different
 *     build options. The message names both revisions (HEAD and dirty state), both fingerprints,
 *     the source files that moved, and every option that moved;
 *   * exit 2 -- usage error.
 *
 * Two installs of the same sources can be different builds. The option values live outside every
 * fingerprinted path (`.camel-opts.json` is git-ignored, and `-D` flags are the build's, not the
 * tree's), so the stamp records the option set a rebuild would pass -- see
 * `common.js: buildOptions` for what is in it and what is deliberately out -- and `check` compares
 * it beside the source hash. A stamp from before that record (schema 2 and older) is refused as
 * untrustworthy rather than matched: it cannot prove which options produced the install.
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
    BUILD_OPTIONS_VERSION,
    buildInfoPath,
    buildOptions,
    describeOptionSet,
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
    console.error('usage: node scripts/build-info.js check|show|stamp [--tag latest|debug|<tag>] [--install DIR] [--allow-stale] [--config Release|Debug] [--root DIR]')
    process.exit(EXIT_USAGE)
}

function parseArgs(argv) {
    const options = { command: null, tag: 'latest', allowStale: false, config: null, root: BASEDIR, install: null }
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
        } else if (arg === '--install') {
            options.install = path.resolve(next())
        } else if (arg.startsWith('--install=')) {
            options.install = path.resolve(arg.slice('--install='.length))
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

function optionsLabel(options) {
    if (!options || typeof options.hash !== 'string') return 'none (the build recorded no options)'
    return options.hash.slice(0, 12)
}

// What moved between the option set the install was built with and the one a build of this tree
// would use now. Name the option, not just the two digests: "ENABLE_FASTVM_JIT: built OFF, a
// rebuild would use ON" is the sentence a reader can act on.
function buildOptionDifferences(before, after) {
    const lines = []
    const beforeCmake = before.cmake || {}
    const afterCmake = after.cmake || {}
    for (const [name, value] of Object.entries(afterCmake)) {
        if (!Object.hasOwn(beforeCmake, name)) {
            lines.push(`${name}: not recorded by the build, a rebuild would use ${value}`)
        } else if (beforeCmake[name] !== value) {
            lines.push(`${name}: built ${beforeCmake[name]}, a rebuild would use ${value}`)
        }
    }
    for (const [name, value] of Object.entries(beforeCmake)) {
        if (!Object.hasOwn(afterCmake, name)) {
            lines.push(`${name}: built ${value}, the tree no longer sets it`)
        }
    }
    const beforeEnv = before.env || {}
    for (const [name, value] of Object.entries(after.env || {})) {
        if (beforeEnv[name] !== value) {
            lines.push(
                `${name}: built ${beforeEnv[name] === undefined ? 'not recorded' : beforeEnv[name]}, ` +
                    `a rebuild would use ${value}`
            )
        }
    }
    if (before.platform !== after.platform) {
        lines.push(`platform: built for ${before.platform || 'an unrecorded platform'}, this tree builds on ${after.platform}`)
    }
    if (before.compilerFlags !== after.compilerFlags) {
        lines.push(
            `compiler: built with "${before.compilerFlags || 'not recorded'}", ` +
                `a rebuild would use "${after.compilerFlags}"`
        )
    }
    return lines
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

function printMismatch(label, stamp, current, allowStale, differs) {
    const changed = []
    if (differs.sources) changed.push('sources')
    if (differs.options) changed.push('build options')
    if (differs.config) changed.push('configuration')
    const headline =
        changed.length === 1 && changed[0] === 'sources'
            ? 'was built from different sources than this tree'
            : `was built from different ${changed.join(' and ')} than this tree would build now`
    const stampGit = stamp.git === false
        ? { available: false }
        : { available: true, revision: stamp.revision, dirty: stamp.dirty, dirtyCount: stamp.dirtyCount }
    console.log(`[camel-remote] -NoBuild refused: ${label} ${headline}.`)
    console.log(`  built from: ${revisionLabel(stampGit)}, at ${stamp.builtAt || 'unknown time'}`)
    console.log(
        `              fingerprint ${fingerprintLabel(stamp.fingerprint)}, ` +
            `build options ${optionsLabel(stamp.buildOptions)} (${stamp.config || 'unknown config'})`
    )
    console.log(`  this tree:  ${revisionLabel(current.git)}`)
    console.log(
        `              fingerprint ${fingerprintLabel(current.fingerprint)}, ` +
            `build options ${optionsLabel(current.buildOptions)} (${differs.config || current.config || 'unknown config'})`
    )
    if (differs.sources) {
        printChangedFiles(stamp.fingerprint, current.fingerprint)
    }
    if (differs.options) {
        if (differs.optionLines.length > 0) {
            console.log(`  build options that moved: ${differs.optionLines.length}`)
            for (const line of differs.optionLines.slice(0, MAX_LISTED_FILES)) {
                console.log(`    ~ ${line}`)
            }
            if (differs.optionLines.length > MAX_LISTED_FILES) {
                console.log(`    ... and ${differs.optionLines.length - MAX_LISTED_FILES} more`)
            }
        } else {
            console.log(
                '  (the recorded option sets differ, but no listed option does -- the stamp and this ' +
                    'build disagree about which options exist)'
            )
        }
        console.log(`  built with: ${describeOptionSet(stamp.buildOptions || {})}`)
        console.log(`  now a build would pass: ${describeOptionSet(current.buildOptions)}`)
    }
    if (differs.config) {
        console.log(
            `  the install was collected as a ${stamp.config} build; this run is about the ${differs.config} build`
        )
    }
    if (differs.options && !differs.sources) {
        console.log(
            '  Same source bytes, other options: the binary and the ' +
                `${label}/stdlib it runs were compiled with the option set above, so the reading would ` +
                'answer the old configuration and not this tree\'s.'
        )
    } else if (differs.config && !differs.sources && !differs.options) {
        console.log(
            `  Same source bytes, another configuration: the plan would run a ` +
                `${stamp.config || 'unknown'} install where this run is about the ${differs.config} one.`
        )
    } else {
        console.log(`  The plan would run the old binary and the old ${label}/stdlib, not this tree.`)
    }
    if (allowStale) {
        console.log('  -AllowStale was given: continuing anyway; this result does not describe this tree.')
    } else {
        console.log(
            '  Re-run without -NoBuild (it builds this tree with these options and restamps the install), ' +
                'or pass -AllowStale to run anyway.'
        )
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

// Where the stamp of the install a command was asked about lives, and what to call that install in
// the message: `out/<tag>` for a tag, the install's own path when `--install` named one. An install
// under this tree's `out/<tag>` keeps its tag as the name, because that is the name its reader
// knows.
function stampTarget(options) {
    if (!options.install) {
        return { stampPath: buildInfoPath(options.tag, options.root), label: `out/${options.tag}` }
    }
    const relative = path.relative(path.join(options.root, 'out'), options.install)
    const inside = relative !== '' && !relative.startsWith('..') && !path.isAbsolute(relative)
    return {
        stampPath: path.join(options.install, BUILD_INFO_FILE),
        label: inside ? `out/${relative.split(path.sep)[0]}` : options.install,
    }
}

function commandCheck(options) {
    const { label, stampPath } = stampTarget(options)
    if (!fs.existsSync(stampPath)) {
        refuse(
            `${label} has no ${BUILD_INFO_FILE}.\n` +
                '  The install was never built, or it was collected by an older build that did not stamp.\n' +
                '  Without the stamp there is no way to tell which sources produced it.\n' +
                `  Run once without -NoBuild: it builds this tree and stamps ${label}.`,
            options.allowStale
        )
        return
    }

    let stamp
    try {
        stamp = JSON.parse(fs.readFileSync(stampPath, 'utf-8'))
    } catch (error) {
        refuse(
            `${label}/${BUILD_INFO_FILE} is not readable JSON (${error.message}).\n` +
                `  Run once without -NoBuild to rebuild and restamp ${label}.`,
            options.allowStale
        )
        return
    }
    if (!stamp || stamp.schema !== BUILD_INFO_SCHEMA || !stamp.fingerprint ||
        stamp.fingerprint.version !== FINGERPRINT_VERSION) {
        refuse(
            `${label}/${BUILD_INFO_FILE} was written by a different form of this guard ` +
                `(schema ${stamp && stamp.schema}, fingerprint ${stamp && stamp.fingerprint && stamp.fingerprint.version}; ` +
                `this build writes schema ${BUILD_INFO_SCHEMA}, fingerprint ${FINGERPRINT_VERSION}).\n` +
                `  A stamp that predates the build-option record cannot prove which options produced the ` +
                `install, so it is treated as untrustworthy rather than matched.\n` +
                `  Run once without -NoBuild to rebuild and restamp ${label}.`,
            options.allowStale
        )
        return
    }
    if (!stamp.buildOptions || stamp.buildOptions.version !== BUILD_OPTIONS_VERSION) {
        refuse(
            `${label}/${BUILD_INFO_FILE} records no build options (schema ${stamp.schema}, ` +
                `options ${stamp.buildOptions ? stamp.buildOptions.version : 'missing'}; this build records ` +
                `version ${BUILD_OPTIONS_VERSION}).\n` +
                `  Two builds of these sources with different CMake options are two different installs, and ` +
                `without the option record there is no way to tell this one from the other.\n` +
                `  Run once without -NoBuild to rebuild and restamp ${label}.`,
            options.allowStale
        )
        return
    }

    const current = {
        git: gitState(options.root),
        fingerprint: sourceFingerprint(options.root),
        buildOptions: buildOptions(options.root),
        config: options.config
    }
    const optionLines = buildOptionDifferences(stamp.buildOptions, current.buildOptions)
    const differs = {
        sources: stamp.fingerprint.hash !== current.fingerprint.hash,
        options: stamp.buildOptions.hash !== current.buildOptions.hash,
        optionLines,
        // Only when the caller said which configuration it is about (`camel-remote.ps1` passes the
        // configuration it builds): a bare `check --tag latest` cannot tell Release from
        // RelWithDebInfo, and both live in `out/latest`.
        config: options.config && stamp.config !== options.config ? options.config : null
    }
    if (!differs.sources && !differs.options && !differs.config) {
        return // The install matches these sources and this option set: no new noise on a correct run.
    }

    printMismatch(label, stamp, current, options.allowStale, differs)
    if (!options.allowStale) {
        process.exit(EXIT_STALE)
    }
}

function commandShow(options) {
    const { label, stampPath } = stampTarget(options)
    if (!fs.existsSync(stampPath)) {
        console.error(`[build-info] no ${path.relative(options.root, stampPath) || stampPath}`)
        process.exit(EXIT_STALE)
    }
    process.stdout.write(fs.readFileSync(stampPath, 'utf-8'))
    const stamp = JSON.parse(fs.readFileSync(stampPath, 'utf-8'))
    const current = sourceFingerprint(options.root)
    const options1 = buildOptions(options.root)
    console.log(`current tree fingerprint: ${fingerprintLabel(current)}, build options ${optionsLabel(options1)}`)
    console.log(stamp.fingerprint.hash === current.hash ? label + ' matches this tree\'s sources' : label + ' does NOT match this tree')
    if (stamp.buildOptions && typeof stamp.buildOptions.hash === 'string') {
        console.log(
            stamp.buildOptions.hash === options1.hash
                ? label + ' matches this tree\'s build options'
                : label + ' does NOT match this tree\'s build options'
        )
    }
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
        writeBuildInfo(options.config || 'Release', options.tag, options.root)
    }
}

main()
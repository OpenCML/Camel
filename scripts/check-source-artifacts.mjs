#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

//  The source tree is not an install. A `.cmo` is the binary of a C++ module and
// comes out of a build; a `.so` / `.dll` / `.pdb` / `.o` beside the sources is build output that
// belongs in `build/` or `out/<tag>/`. The module loader finds modules beside the executable and
// under `CAMEL_HOME` (`include/camel/utils/install_layout.h`), and `collect-out` puts the
// manifest's `.cmo` into `out/<tag>/stdlib`, so nothing has to be written next to `stdlib/*.cml`.
//
//   node scripts/check-source-artifacts.mjs              exit 1 and one line per artifact
//   node scripts/check-source-artifacts.mjs --clean      delete what would be reported
//   node scripts/check-source-artifacts.mjs --clean --dry-run
//                                                        list the delete, remove nothing
//   node scripts/check-source-artifacts.mjs --root <dir> check that checkout (default: this one)
//
//  `--clean` delegates to `node scripts/clean.js --source`: the guard and `npm run clean`
// share one implementation of the delete, and one is the place a new safety rule goes.
//
// What is checked: the directories the build reads (BUILD_INFO_ROOTS in scripts/common.js) for a
// C++ module binary under `stdlib/` or `modules/`, and for a linker output (`.so`, `.dll`, `.pdb`,
// `.o`, `.obj`, `.a`, `.lib`, `.dylib`, `.exe`, `.ilk`) anywhere under them. Two directories hold
// vendored binaries rather than build output and are skipped: `modules/python/sdks/` and
// `modules/python/dlls/` (both git-ignored). The one tracked `.cmo` is
//  `test/cases/modules/pass_not_found/unloadable.cmo`: a 72-byte text fixture, outside the
// roots above, which this check never reads; `feat/modules/pass_not_found` pins that it stays
// text, so it is not a compiled object an audit should count.
//
// The build runs this at the end of `copyBuildArtifacts` (scripts/common.js), so a step that
// starts writing next to the sources fails that build with the file named. The
// `feat/tools/portability` plan runs it too.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url))
const THIS_FILE = path.join(SCRIPT_DIR, 'check-source-artifacts.mjs')
const HERE = path.resolve(SCRIPT_DIR, '..')

// The directories a build reads (BUILD_INFO_ROOTS in scripts/common.js). `test/` is not one of
// them: a test case is read at plan time, and its fixtures may be whatever they test.
const SOURCE_DIRS = ['src', 'include', 'modules', 'tools', 'stdlib', 'third_party', 'cmake']
// Vendored Python runtimes: binary by nature, not produced by this build, and git-ignored.
const VENDORED = /^modules\/python\/(?:sdks|dlls)\//
// A `.cmo` is a module binary only in the two places a module can be built from.
const CMO_DIRS = new Set(['stdlib', 'modules'])
// Linker output: produced by a build (or a test helper), never an input of one.
const LINKER_OUTPUT = new Set(['.so', '.dll', '.pdb', '.o', '.obj', '.a', '.lib', '.dylib', '.exe', '.ilk'])

/**
 * Everything a build must not leave next to the sources, relative to `root`.
 *  Exported for `scripts/clean.js`, which owns the delete.
 * @param {string} root
 * @returns {{ artifacts: string[], scanned: number }}
 */
export function scanSourceArtifacts(root) {
    const artifacts = []
    let scanned = 0

    const walk = (dir) => {
        let entries
        try {
            entries = fs.readdirSync(dir, { withFileTypes: true })
        } catch {
            return // an unreadable directory is not this check's failure to report
        }
        for (const entry of entries) {
            const full = path.join(dir, entry.name)
            const rel = path.relative(root, full).split(path.sep).join('/')
            if (entry.isDirectory()) {
                if (VENDORED.test(`${rel}/`)) continue
                walk(full)
                continue
            }
            if (!entry.isFile()) continue
            scanned++
            const ext = path.extname(entry.name).toLowerCase()
            if (ext === '.cmo') {
                if (CMO_DIRS.has(rel.split('/')[0])) artifacts.push(rel)
                continue
            }
            if (LINKER_OUTPUT.has(ext)) artifacts.push(rel)
        }
    }

    for (const dir of SOURCE_DIRS) {
        const full = path.join(root, dir)
        if (fs.existsSync(full)) walk(full)
    }
    artifacts.sort()
    return { artifacts, scanned }
}

function usage(message) {
    console.error(`camel: ${message}`)
    console.error('usage: node scripts/check-source-artifacts.mjs [--clean [--dry-run]] [--root <dir>]')
    process.exit(2)
}

function main() {
    const args = process.argv.slice(2)
    const clean = args.includes('--clean')
    const dryRun = args.includes('--dry-run')
    if (dryRun && !clean) usage('--dry-run is only meaningful with --clean')
    const rootAt = args.indexOf('--root')
    const root = rootAt >= 0 ? args[rootAt + 1] : HERE
    if (rootAt >= 0 && (!root || root.startsWith('--'))) usage('--root needs a directory')
    const rootPath = path.resolve(root)
    if (!fs.existsSync(rootPath)) usage(`no such directory: ${root}`)

    if (clean) {
        // One implementation of the delete: scripts/clean.js owns it, this guard only reports.
        const cleanArgs = [path.join(SCRIPT_DIR, 'clean.js'), '--source', '--root', rootPath]
        if (dryRun) cleanArgs.push('--dry-run')
        const result = spawnSync(process.execPath, cleanArgs, { cwd: process.cwd(), stdio: 'inherit' })
        process.exit(result.status === null ? 1 : result.status)
    }

    const { artifacts, scanned } = scanSourceArtifacts(rootPath)
    if (artifacts.length > 0) {
        for (const rel of artifacts) {
            console.log(`${rel}: build output in the source tree`)
        }
        console.error(
            `source tree artifacts: ${artifacts.length} build artifact(s) under the source directories; ` +
                'build/ and out/<tag>/ are the places for them (run with --clean to delete)'
        )
        process.exit(1)
    }
    console.log(
        `source tree artifacts: ${scanned} files under ${SOURCE_DIRS.join(', ')}, ` +
            'no build output in the source tree'
    )
}

// Imported by scripts/clean.js for `scanSourceArtifacts`; only a direct run is a check.
const entry = process.argv[1] ? path.resolve(process.argv[1]) : ''
if (process.platform === 'win32' ? entry.toLowerCase() === THIS_FILE.toLowerCase() : entry === THIS_FILE) {
    main()
}
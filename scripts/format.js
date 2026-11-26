// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import { execSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'
import { formatWithPin, readPin } from './clang-format-pin.js'
import { explicitFilesArg, splitLineList, uniqueFiles } from './file-list.js'

// Formatting is pinned to one clang-format major version (`.clang-format-version`). Versions of
// clang-format lay the same source out differently, so a machine with another version would rewrite
// hundreds of lines a commit never touched. When the formatter on PATH (or the one named by
// CAMEL_CLANG_FORMAT) is not the pinned major, or is missing, no file is rewritten and one line says so.
//
// The layout moved once: `src/`, `include/` and `modules/` became `src/kernel/`, `src/parse/`, `src/compile/`
// and `src/stdlib/`. The roots are probed rather than assumed, so a tree of either layout is covered: a
// root is formatted when this tree has it. When none of them exists the run fails instead of
// reporting that there was nothing to do, because a directory that was never reached is not the
// same as a directory with nothing in it.
const LAYOUT_ROOTS = ['src', 'include', 'modules', 'tools', 'kernel', 'parse', 'compile', 'lib']
const repoRoot = path.dirname(path.dirname(fileURLToPath(import.meta.url)))
const dirs = LAYOUT_ROOTS.filter((dir) => fs.existsSync(path.join(repoRoot, dir)))

if (dirs.length === 0) {
    console.error(`None of the source roots exist under ${repoRoot}: ${LAYOUT_ROOTS.join(', ')}`)
    process.exit(1)
}

function getChangedFiles() {
    try {
        const output = execSync('git diff --name-only --diff-filter=ACMR HEAD', { encoding: 'utf-8' })
        return splitLineList(output)
    } catch (err) {
        console.error('Error getting changed files from git:', err)
        return []
    }
}

function formatTargets(files) {
    return uniqueFiles(files).filter(
        (file) =>
            /\.(cpp|h)$/.test(file) && dirs.some((dir) => file.startsWith(`${dir}/`))
    )
}

const changedFiles = formatTargets(explicitFilesArg() ?? getChangedFiles())

if (changedFiles.length === 0) {
    console.log('No modified .cpp or .h files to format.')
} else {
    formatWithPin(changedFiles, {
        bin: process.env.CAMEL_CLANG_FORMAT || 'clang-format',
        pin: readPin(repoRoot),
    })
}

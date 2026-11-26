// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import { execFileSync, execSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'
import { changedRanges, formatWithPin, readPin } from './clang-format-pin.js'
import { explicitFilesArg, splitLineList, uniqueFiles } from './file-list.js'

// Only the lines a commit changes are formatted. The layout of the tree comes from several clang-format
// versions and no single version leaves it free of violations, so formatting a whole file would rewrite
// lines nobody touched. For each file the new-side ranges of its diff against HEAD become `--lines`
// arguments; a file without added or changed lines is not formatted.
//
// The formatter is pinned to one major version (`.clang-format-version`), the one on the machine where
// commits are made: other versions break the same lines differently, so a formatter of another major
// would still reformat the changed lines in a style the next commit undoes. When the formatter on PATH
// (or the one named by CAMEL_CLANG_FORMAT) is not the pinned major, or is missing, no file is rewritten
// and one line says so.
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

// With --files (the pre-commit hook) the files are staged and not partially so, so the staged diff is the
// change. Without it the change is the working tree against HEAD.
const explicitFiles = explicitFilesArg()
const changedFiles = formatTargets(explicitFiles ?? getChangedFiles())

function rangesOf(file) {
    const args = ['diff', ...(explicitFiles ? ['--cached'] : []), '-U0', 'HEAD', '--', file]
    try {
        return changedRanges(execFileSync('git', args, { encoding: 'utf-8', cwd: repoRoot }))
    } catch {
        return []
    }
}

if (changedFiles.length === 0) {
    console.log('No modified .cpp or .h files to format.')
} else {
    formatWithPin(changedFiles, {
        bin: process.env.CAMEL_CLANG_FORMAT || 'clang-format',
        pin: readPin(repoRoot),
        rangesOf,
    })
}

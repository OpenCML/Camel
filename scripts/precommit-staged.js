// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import fs from 'fs'
import path from 'path'
import { spawnSync } from 'child_process'
import { fileURLToPath } from 'url'
import { splitNulList } from './file-list.js'

const __filename = fileURLToPath(import.meta.url)
const REPO_ROOT = path.dirname(path.dirname(__filename))

// Markers for a commit whose content is not a plain edit of HEAD: a merge, a
// revert or a cherry-pick. While one of them exists the index holds a combined
// result, so "everything staged" is not the same as "what this commit changes".
const MULTI_PARENT_MARKERS = ['MERGE_HEAD', 'REVERT_HEAD', 'CHERRY_PICK_HEAD']

function run(command, args, options = {}) {
    const proc = spawnSync(command, args, {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        stdio: options.stdio || 'pipe',
    })
    if (proc.error) {
        throw new Error(proc.error.message)
    }
    if (options.allowExitCodes?.includes(proc.status)) {
        return proc
    }
    if (proc.status !== 0) {
        const detail = `${proc.stdout || ''}${proc.stderr || ''}`.trim()
        throw new Error(`${command} ${args.join(' ')} failed${detail ? `:\n${detail}` : ''}`)
    }
    return proc
}

function git(args, options = {}) {
    return run('git', args, options)
}

function gitPathList(args) {
    const proc = git([...args, '-z'])
    return splitNulList(proc.stdout)
}

function hasUnstagedChanges(file) {
    const proc = git(['diff', '--quiet', '--', file], { allowExitCodes: [0, 1] })
    return proc.status === 1
}

function rejectPartialStaging(files) {
    const partialFiles = files.filter(hasUnstagedChanges)
    if (partialFiles.length === 0) {
        return
    }
    console.error('[precommit] Refusing to auto-format with partially staged files.')
    console.error('[precommit] Stage the full file or split the changes before committing:')
    for (const file of partialFiles) {
        console.error(`  - ${file}`)
    }
    process.exit(1)
}

// The tree the commit is being made from. Reads the object database only; it
// never touches the working tree or the index.
function resolveHeadTree() {
    try {
        return git(['rev-parse', 'HEAD^{tree}']).stdout.trim()
    } catch {
        // Unborn HEAD (first commit): there is nothing to compare against.
        return null
    }
}

function resolveIndexTree() {
    try {
        return git(['write-tree']).stdout.trim()
    } catch {
        // Unmerged entries still in the index: git commit refuses them anyway.
        return null
    }
}

// Returns the name of the marker file if a merge/revert/cherry-pick is in
// progress, else null. --git-path keeps this correct inside a linked worktree,
// where .git is a file and the markers live in .git/worktrees/<name>/.
function inProgressMultiParentOperation() {
    for (const marker of MULTI_PARENT_MARKERS) {
        const markerPath = git(['rev-parse', '--git-path', marker]).stdout.trim()
        if (markerPath && fs.existsSync(path.resolve(REPO_ROOT, markerPath))) {
            return marker
        }
    }
    return null
}

// Files that differ between the index and the first parent of the commit about
// to be created. At pre-commit time HEAD is still the checked-out branch, i.e.
// the first parent itself: the merge commit does not exist yet, so HEAD^1 would
// reach one generation too far back and drag in the branch's own last commit.
// For a revert or a cherry-pick the same holds: their parent is HEAD.
function changedAgainstFirstParent() {
    const firstParent = git(['rev-parse', 'HEAD']).stdout.trim()
    return gitPathList([
        'diff',
        '--cached',
        '--name-only',
        '--diff-filter=ACMR',
        firstParent,
    ])
}

function runFormatterScripts(stagedFiles) {
    run(process.execPath, ['scripts/format.js', '--files', ...stagedFiles], { stdio: 'inherit' })
    run(process.execPath, ['scripts/update.js', '--files', ...stagedFiles], { stdio: 'inherit' })
}

function main() {
    const beforeTree = resolveHeadTree()
    const marker = inProgressMultiParentOperation()

    // Plain commit: the staged set is the commit. Merge/revert/cherry-pick: only
    // the paths this commit really changes against its first parent.
    const stagedFiles = marker
        ? changedAgainstFirstParent()
        : gitPathList(['diff', '--cached', '--name-only', '--diff-filter=ACMR'])
    if (stagedFiles.length === 0) {
        console.log('[precommit] No staged files.')
        return
    }

    if (marker) {
        console.log(
            `[precommit] ${marker} present: formatting the ${stagedFiles.length} file(s) this commit changes against its first parent.`
        )
    } else {
        rejectPartialStaging(stagedFiles)
    }

    runFormatterScripts(stagedFiles)
    git(['add', '--', ...stagedFiles], { stdio: 'inherit' })

    // Formatting (and update.js) must not leave the commit empty. Comparing the
    // index tree with the tree before the commit covers both scripts, and it
    // stays off the working tree: write-tree only reads the index.
    const afterTree = resolveIndexTree()
    if (beforeTree && afterTree && afterTree === beforeTree) {
        console.error(
            '[precommit] Formatting cancelled the change: the staged tree is identical to the tree before this commit.'
        )
        console.error('[precommit] Refusing to record an empty commit.')
        process.exit(1)
    }
}

try {
    main()
} catch (error) {
    console.error(`[precommit] ${error.message}`)
    process.exit(1)
}
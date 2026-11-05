#!/usr/bin/env node
/**
 * `scripts/bisect-merge.mjs` takes its readings through the gate's one entry.
 *
 * The hole this pins: `#parseArgs` defaulted `--wrapper` to `path.resolve(<tree>, '..',
 * 'camel-remote.ps1')`. For a lane tree under `D:\Projects\Camel-wt\` that resolves to the copy left
 * over from before the machine-specific values moved into `internal/tools/camel-gate.local.json`: it
 * runs, it prints ordinary `[camel-remote]` lines, and it is wider than the entry (no `--config`, so
 * `-Tier debug -NoBuild` is verified against a Release install; a plan's stderr is not brought back;
 * `$sshHost` and `$bare` are literals). A bisect therefore took every reading through a gate whose
 * numbers looked normal and were about an install the tree never built.
 *
 * The default now resolves `internal/tools/camel-remote.ps1` from the script's own location. This case
 * reads that back with two `--print-wrapper` calls whose `--path` values differ and do not exist:
 *
 *   1. the resolved wrapper ends with `internal/tools/camel-remote.ps1`, and the file is there;
 *   2. both calls print the same path -- the default does not depend on where the worktree sits;
 *   3. neither is `<tree>/../camel-remote.ps1`, the default that was measured before the fix.
 *
 * `--print-wrapper` stops inside `#parseArgs`, so no build, no push and no host is involved; a guard
 * that cannot print what it resolved could not be falsified, which is why the flag exists.
 *
 * Usage: node wrapper_default.mjs
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const BISECT = path.join(REPO_ROOT, 'scripts', 'bisect-merge.mjs')

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}`, ...(ok || detail === undefined ? [] : [`\n     ${detail}`]))
    if (!ok) failed = true
}

/// One `--print-wrapper` run. The tree does not have to exist: the wrapper is resolved before
/// anything looks at the worktree.
function printWrapper(tree) {
    const result = spawnSync(
        process.execPath,
        [BISECT, '--print-wrapper', '--path', tree, '--start', 'HEAD', '--plan', 'smoke'],
        { encoding: 'utf8' },
    )
    const output = `${result.stdout ?? ''}${result.stderr ?? ''}`
    const line = (result.stdout ?? '').split(/\r?\n/).find((item) => item.startsWith('[bisect] wrapper '))
    return {
        status: result.status,
        wrapper: line === undefined ? '' : line.slice('[bisect] wrapper '.length).trim(),
        output,
    }
}

const treeA = path.join(os.tmpdir(), 'camel-bisect-lane-a')
const treeB = path.join(os.tmpdir(), 'camel-bisect-lane-b')
const first = printWrapper(treeA)
const second = printWrapper(treeB)
const slash = (item) => item.replace(/\\/g, '/')

console.log(`[reading] --path ${treeA} -> ${first.wrapper}`)
console.log(`[reading] --path ${treeB} -> ${second.wrapper}`)

check('the first call exits 0', first.status === 0, `${first.status}: ${first.output}`)
check('the second call exits 0', second.status === 0, `${second.status}: ${second.output}`)
check(
    'the resolved wrapper ends with internal/tools/camel-remote.ps1',
    slash(first.wrapper).endsWith('internal/tools/camel-remote.ps1'),
    first.wrapper,
)
check(
    'the resolved wrapper is a file of this checkout',
    first.wrapper !== '' && fs.existsSync(first.wrapper),
    first.wrapper,
)
check(
    'two different --path values resolve the same wrapper, so the default does not follow the worktree',
    first.wrapper === second.wrapper,
    `${first.wrapper} / ${second.wrapper}`,
)
check(
    'neither call resolves <tree>/../camel-remote.ps1, the copy beside the worktrees',
    first.wrapper !== path.resolve(treeA, '..', 'camel-remote.ps1') &&
        second.wrapper !== path.resolve(treeB, '..', 'camel-remote.ps1'),
    `${first.wrapper} / ${second.wrapper}`,
)

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)
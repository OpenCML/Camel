// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The balance of the references the plan gives back, over every case the tree has.
 *
 * `test::plan_refs` prints, per graph, `produced + copies == give-backs + kept + packed`. That
 * arithmetic is a property of the walk, not of a program, so it must hold on every graph of every
 * case; one graph that does not balance means the walk missed a path, and a missed path is a
 * reference given back too early once a back end executes the table
 * (docs/handover/reports/2026-11-15-the-refs-that-come-back.cn.md).
 *
 * One case at a time, without the arguments its own plan gives it: a case whose compilation needs
 * those arguments cannot be read here and is counted as skipped, not as balanced. A case that
 * compiles and prints no table is an error: the pass must speak for every program it is given.
 *
 *   PLAN_REFS_SWEEP cases=<n> read=<n> graphs=<n> unbalanced=<n> skipped=<n> marked=<n>
 *   with-give=<n>
 *
 * `marked` is every position the tree's plans name as a marked operator, `with-give` how many of them
 * have a reference coming back there. The two are read together and not compared: an operator over
 * integers packs nothing to give back, so `with-give < marked` says what the programs are, not what
 * the plan missed. What a non-zero `with-give` buys is a counted position a back end can be caught
 * being short by, which is what the four-back-end readings of 2026-11-16 are taken on.
 *
 * followed by one `PLAN_REFS_BAD <case>` line per unbalanced graph. Exit 1 if anything is unbalanced
 * or a compiled case printed no table.
 */

import { readdir } from 'node:fs/promises'
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const REPO = path.dirname(HERE)
const CASES = path.join(REPO, 'test', 'cases')
const CONCURRENCY = Number(process.env.PLAN_REFS_JOBS || 8)
const TIMEOUT_MS = Number(process.env.PLAN_REFS_TIMEOUT_MS || 20000)
// Where the cases run. A case is a program, and a program that writes a relative path writes it
// where it is: `export_model<out: 'affine.onnx'>` writes the model to the process's cwd, and
// `test/cases/modules/std/env/unicode/env_paths.cml` writes `数据 文件 🐫.txt` and `子 目录/嵌套 文件.txt`
// beside its own `get_cwd()`. Started from the checkout root -- which is what a child of the runner
// inherits -- those fourteen files landed in the repository root on every `feat` gate: the next
// `[provenance]` line read `(dirty: 14 files)` instead of a commit, `camel-remote.ps1` carried them
// to the host on every run, and `--changed` counted them as changed files.
//
// So each case gets a directory of its own, under the runner's per-test scratch (`CAMEL_TEST_TMPDIR`,
// removed with the test) or under the system temporary directory when this script is run directly.
// One directory per case and not one for the sweep: two cases of the 3,844 write the same file name,
// and a leftover that one case can read changes what the next one does.
const RUN_ROOT = process.env.CAMEL_TEST_TMPDIR
    ? path.join(path.resolve(process.env.CAMEL_TEST_TMPDIR), 'plan-refs')
    : fs.mkdtempSync(path.join(os.tmpdir(), 'camel-plan-refs-'))

function camelBinary() {
    if (process.env.CAMEL_BIN) return process.env.CAMEL_BIN
    const home = process.env.CAMEL_HOME
    if (!home) {
        console.error('check-plan-refs: CAMEL_HOME is not set and CAMEL_BIN is empty')
        process.exit(2)
    }
    return path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
}

async function casesUnder(directory) {
    const found = []
    for (const entry of (await readdir(directory, { withFileTypes: true })).sort((a, b) =>
        a.name.localeCompare(b.name))) {
        const full = path.join(directory, entry.name)
        if (entry.isDirectory()) {
            found.push(...(await casesUnder(full)))
        } else if (entry.name.endsWith('.cml')) {
            found.push(full)
        }
    }
    return found
}

function run(binary, args, cwd) {
    return new Promise((resolve) => {
        const child = spawn(binary, args, { cwd, stdio: ['ignore', 'pipe', 'pipe'] })
        let stdout = ''
        let stderr = ''
        const timer = setTimeout(() => {
            child.kill('SIGKILL')
            resolve({ timedOut: true, code: null, stdout, stderr })
        }, TIMEOUT_MS)
        child.stdout.on('data', (chunk) => {
            stdout += chunk
        })
        child.stderr.on('data', (chunk) => {
            stderr += chunk
        })
        child.on('error', (error) => {
            clearTimeout(timer)
            resolve({ timedOut: false, code: -1, stdout, stderr: String(error) })
        })
        child.on('close', (code) => {
            clearTimeout(timer)
            resolve({ timedOut: false, code, stdout, stderr })
        })
    })
}

/// Remove a scratch directory, and never let the removal become the sweep's answer. What a case
/// wrote is not this sweep's evidence, so a directory that will not go is housekeeping left behind,
/// not a case that failed -- and it must not turn into one: an unhelpful `EBUSY` here rejected the
/// worker inside `Promise.all`, which took the whole run down with exit 2 and a stack trace about
/// `rmSync` in place of the balance reading. `force` only suppresses ENOENT, and on Windows a file
/// the case had just written can still be held for a moment after the child is gone (the scanner
/// reading it, the last handle not yet released), so retry briefly first and then let an
/// windows-only "still busy" stay behind.
function dropTree(dir) {
    try {
        fs.rmSync(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 50 })
    } catch (error) {
        if (!['EBUSY', 'EPERM', 'ENOTEMPTY'].includes(error?.code)) throw error
    }
}

/// The tables of one case: its graphs' summary lines, and the ones that do not balance.
function tablesOf(stdout) {
    const summaries = []
    const unbalanced = []
    for (const line of stdout.split('\n')) {
        if (!line.startsWith('PLAN_REFS ')) continue
        if (!line.includes(' balanced ')) continue
        summaries.push(line)
        if (!line.endsWith(' balanced yes')) unbalanced.push(line)
    }
    return { summaries, unbalanced }
}

/// The marked operators of one case: how many positions the table names, and how many of them have a
/// reference coming back. A position with `gives 0` is not a gap -- an operator over integers packs
/// nothing to give back -- so the two counts are read together and neither is compared with the
/// other here. What the second count is for: a marked operator whose position carries a give row is
/// one where a back end that does not count can be seen being short by exactly that row, which is
/// what `PLAN_REF_MARKED` makes readable instead of inferring from a `live` that the collector can
/// even out after the run (docs/handover/reports/2026-11-16-what-the-missing-count-was-and-was-not.cn.md).
function markedOf(stdout) {
    let marked = 0
    let withGive = 0
    for (const line of stdout.split('\n')) {
        if (!line.startsWith('PLAN_REF_MARKED ')) continue
        if (!/ at \d+ node \d+ gives \d+$/.test(line)) continue // the per-graph summary line
        marked += 1
        if (!line.endsWith(' gives 0')) withGive += 1
    }
    return { marked, withGive }
}

async function main() {
    const binary = camelBinary()
    const files = (await casesUnder(CASES)).sort()
    const bad = []
    const silent = []
    let read = 0
    let graphs = 0
    let skipped = 0
    let marked = 0
    let withGive = 0

    let next = 0
    async function worker() {
        for (;;) {
            const index = next++
            if (index >= files.length) return
            const file = files[index]
            // A directory of its own, named by the case it held so a directory left behind after a
            // crash says which case wrote it. It goes whether the case passed, failed or timed out:
            // what a case writes is not this sweep's evidence.
            const cwd = path.join(RUN_ROOT, path.relative(CASES, file).replace(/[\\/]/g, '__'))
            fs.mkdirSync(cwd, { recursive: true })
            const result = await run(binary, [file, 'test::plan_refs'], cwd)
            dropTree(cwd)
            const { summaries, unbalanced } = tablesOf(result.stdout)
            if (summaries.length === 0) {
                // No table: either the case does not compile without the arguments its plan gives
                // it, or the pass said nothing about a program it was handed. The second is a
                // defect; the first is a case this sweep cannot read.
                if (result.code === 0) {
                    silent.push(path.relative(REPO, file))
                } else {
                    skipped += 1
                }
                continue
            }
            read += 1
            graphs += summaries.length
            const marks = markedOf(result.stdout)
            marked += marks.marked
            withGive += marks.withGive
            for (const line of unbalanced) {
                bad.push(`${path.relative(REPO, file)}: ${line}`)
            }
        }
    }
    await Promise.all(Array.from({ length: Math.min(CONCURRENCY, files.length) }, worker))
    // The sweep's own root goes too, unless the runner gave it one it owns (`CAMEL_TEST_TMPDIR`,
    // removed with the test) -- a script run by hand leaves nothing behind either.
    if (!process.env.CAMEL_TEST_TMPDIR) dropTree(RUN_ROOT)

    console.log(
        `PLAN_REFS_SWEEP cases=${files.length} read=${read} graphs=${graphs} ` +
            `unbalanced=${bad.length} skipped=${skipped} silent=${silent.length} ` +
            `marked=${marked} with-give=${withGive}`)
    for (const line of bad.slice(0, 20)) console.log(`PLAN_REFS_BAD ${line}`)
    for (const file of silent.slice(0, 20)) console.log(`PLAN_REFS_SILENT ${file}`)
    if (bad.length === 0 && silent.length === 0 && read > 0) {
        console.log('every graph of every case that compiles balances')
        process.exit(0)
    }
    process.exit(1)
}

main().catch((error) => {
    console.error(`check-plan-refs: ${error?.stack || error}`)
    process.exit(2)
})
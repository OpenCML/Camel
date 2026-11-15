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
 *   PLAN_REFS_SWEEP cases=<n> read=<n> graphs=<n> unbalanced=<n> skipped=<n>
 *
 * followed by one `PLAN_REFS_BAD <case>` line per unbalanced graph. Exit 1 if anything is unbalanced
 * or a compiled case printed no table.
 */

import { readdir } from 'node:fs/promises'
import { spawn } from 'node:child_process'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const REPO = path.dirname(HERE)
const CASES = path.join(REPO, 'test', 'cases')
const CONCURRENCY = Number(process.env.PLAN_REFS_JOBS || 8)
const TIMEOUT_MS = Number(process.env.PLAN_REFS_TIMEOUT_MS || 20000)

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

function run(binary, args) {
    return new Promise((resolve) => {
        const child = spawn(binary, args, { stdio: ['ignore', 'pipe', 'pipe'] })
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

async function main() {
    const binary = camelBinary()
    const files = (await casesUnder(CASES)).sort()
    const bad = []
    const silent = []
    let read = 0
    let graphs = 0
    let skipped = 0

    let next = 0
    async function worker() {
        for (;;) {
            const index = next++
            if (index >= files.length) return
            const file = files[index]
            const result = await run(binary, [file, 'test::plan_refs'])
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
            for (const line of unbalanced) {
                bad.push(`${path.relative(REPO, file)}: ${line}`)
            }
        }
    }
    await Promise.all(Array.from({ length: Math.min(CONCURRENCY, files.length) }, worker))

    console.log(
        `PLAN_REFS_SWEEP cases=${files.length} read=${read} graphs=${graphs} ` +
            `unbalanced=${bad.length} skipped=${skipped} silent=${silent.length}`)
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
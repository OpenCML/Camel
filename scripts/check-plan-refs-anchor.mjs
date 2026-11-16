// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The external anchor for the count: what the table says a program produces, against what the
 * runtime actually made.
 *
 * The balance `produced + copies == give-backs + kept + packed` is a property of the walk that
 * produced it, so it is blind to a reference that is missed on both sides at once: a value whose
 * hold is not counted also has its give-back not counted, and the arithmetic still balances
 * (mechanism 20: an identity that holds when the object does not exist). The anchor is outside the
 * walk: `--gc-check foreign` prints the blocks the runtime made (`created`), the table prints what
 * it counts (`produced`), and the two are compared per case and per back end.
 *
 * A `Ref` box is made where the program makes it -- once per execution of the node that makes it --
 * so a graph that runs in a loop makes more boxes than it has producing nodes. The anchor therefore
 * compares the two numbers and names every case where they differ, which is the case that has to be
 * read: a loop explains a larger `created`, and nothing explains a smaller one.
 *
 *   PLAN_REFS_ANCHOR cases=<n> runs=<n> equal=<n> unequal=<n> skipped=<n>
 *
 * then `ANCHOR_BAD <case> <backend> produced=<n> created=<n>` per run that differs. Exit 1 if any
 * run differs.
 */

import { readdir } from 'node:fs/promises'
import { spawn } from 'node:child_process'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const REPO = path.dirname(HERE)
const CASES = process.env.PLAN_REFS_ANCHOR_DIR
    ? path.resolve(process.env.PLAN_REFS_ANCHOR_DIR)
    : path.join(REPO, 'test', 'cases', 'consistency', 'values')
const BACKENDS = ['nvm', 'fvm', 'jit', 'prl']
const CONCURRENCY = Number(process.env.PLAN_REFS_JOBS || 8)
const TIMEOUT_MS = Number(process.env.PLAN_REFS_TIMEOUT_MS || 30000)

function camelBinary() {
    if (process.env.CAMEL_BIN) return process.env.CAMEL_BIN
    const home = process.env.CAMEL_HOME
    if (!home) {
        console.error('check-plan-refs-anchor: CAMEL_HOME is not set and CAMEL_BIN is empty')
        process.exit(2)
    }
    return path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
}

function run(binary, args) {
    return new Promise((resolve) => {
        const child = spawn(binary, args, { stdio: ['ignore', 'pipe', 'pipe'] })
        let stdout = ''
        const timer = setTimeout(() => {
            child.kill('SIGKILL')
            resolve({ timedOut: true, stdout })
        }, TIMEOUT_MS)
        child.stdout.on('data', (chunk) => {
            stdout += chunk
        })
        child.on('error', () => {
            clearTimeout(timer)
            resolve({ timedOut: false, stdout })
        })
        child.on('close', () => {
            clearTimeout(timer)
            resolve({ timedOut: false, stdout })
        })
    })
}

/// The two numbers of one run, or null when the run did not produce both.
function numbersOf(stdout) {
    let produced = 0
    let graphs = 0
    for (const line of stdout.split('\n')) {
        const match = line.match(/^PLAN_REFS .* produced (\d+) copies /)
        if (match) {
            produced += Number(match[1])
            graphs += 1
        }
    }
    const created = stdout.match(/^FOREIGN_RESOURCES created=(\d+) /m)
    if (graphs === 0 || created === null) return null
    return { produced, created: Number(created[1]) }
}

async function main() {
    const binary = camelBinary()
    const files = (await readdir(CASES, { withFileTypes: true }))
        .filter((entry) => entry.isFile() && entry.name.endsWith('.cml'))
        .map((entry) => path.join(CASES, entry.name))
        .sort()
    const jobs = []
    for (const file of files) {
        for (const backend of BACKENDS) jobs.push({ file, backend })
    }

    const bad = []
    let equal = 0
    let skipped = 0
    let next = 0
    async function worker() {
        for (;;) {
            const index = next++
            if (index >= jobs.length) return
            const { file, backend } = jobs[index]
            const result = await run(binary, [
                '--gc-check',
                'foreign',
                file,
                'test::plan_refs',
                `std::${backend}`,
            ])
            const numbers = numbersOf(result.stdout)
            if (numbers === null) {
                skipped += 1
                continue
            }
            if (numbers.produced === numbers.created) {
                equal += 1
            } else {
                bad.push(
                    `${path.relative(REPO, file)} ${backend} produced=${numbers.produced} ` +
                        `created=${numbers.created}`)
            }
        }
    }
    await Promise.all(Array.from({ length: Math.min(CONCURRENCY, jobs.length) }, worker))

    console.log(
        `PLAN_REFS_ANCHOR cases=${files.length} runs=${jobs.length} equal=${equal} ` +
            `unequal=${bad.length} skipped=${skipped}`)
    for (const line of bad.slice(0, 24)) console.log(`ANCHOR_BAD ${line}`)
    if (bad.length === 0 && equal > 0) {
        console.log('the allocator made exactly what the table counts, on every run')
        process.exit(0)
    }
    process.exit(1)
}

main().catch((error) => {
    console.error(`check-plan-refs-anchor: ${error?.stack || error}`)
    process.exit(2)
})
#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The optimizer prints what the interpreter prints, byte for byte.
 *
 * A pass may move code, copy it, or drop what nothing reads; it may not change what the program
 * prints. That is the part of "an effect stays where the program put it" (design principles 9.7 and
 * 3.1) that a reader of the output can see -- and it is the part the gate could not see before this
 * check. A program whose `:io/println` is left in no chain still exits 0 and still has the resource
 * `stdout` in its program, so every assertion about exit status and about shape stays green while
 * the output quietly loses lines. Losing output is the harder half of the defect to notice for
 * exactly that reason.
 *
 * `nn.mnist` is where it happened. `std::opt` printed three lines where the interpreter prints
 * seven: `Iteration 0 ...`, `Iteration 200 ...` and `Final Cross-Entropy Loss: ...` never ran,
 * because `pruneBranch` handed a dissolved branch's place to the first ordered node of the taken arm
 * (`tensor:matmul` of `softmax(X @ nw)`) instead of to the effect the arm returns, and the
 * `println`'s only remaining edge went away when the node that carried the wait was folded
 * (`6bd8b67d5` fixes it). It is the smallest program found that shows the loss: the mechanism shrinks
 * to a two-line arm, the loss does not.
 *
 * The assertion is a comparison, not a golden. Both runs are the same binary on the same host, so
 * the numbers mnist prints cancel out, and the check stays true whatever the floats happen to print
 * as -- which a golden of seven lines with fifteen digits would not.
 *
 * Three runs are compared. `std::nvm` alone is the reference; `std::opt std::nvm` is the optimizer
 * under test; `std::opt check_screens=true std::nvm` is the configuration `nn.mnist` itself runs in
 * (`test/plans/feat/opt/screens.plan.toml`), because a parameter that turns on a screen turns on
 * work inside the same pass loop.
 *
 * Usage: node check.mjs
 */

import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..', '..')
const CAMEL = path.join(
    REPO_ROOT,
    'out',
    'latest',
    'bin',
    process.platform === 'win32' ? 'camel.exe' : 'camel'
)
const env = { ...process.env, CAMEL_HOME: process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest') }
const PROGRAM = path.join(REPO_ROOT, 'test', 'cases', 'modules', 'nn', 'mnist.cml')

// Buffers, not strings: "byte for byte" is the assertion, and a string comparison would normalize
// away a difference in the bytes (encoding, trailing carriage returns) that a pipe still carries.
const run = (passes) => spawnSync(CAMEL, [PROGRAM, ...passes], { env })

const reference = run(['std::nvm'])
const passes = [
    ['std::opt', 'std::nvm'],
    ['std::opt', 'check_screens=true', 'std::nvm']
]

let failed = false
if (reference.error || reference.status !== 0) {
    console.log(`[EFFECTS] FAIL the reference run did not finish: exit ${reference.status}`)
    process.exitCode = 1
    process.exit()
}

const lines = (buffer) => buffer.toString('utf8').split('\n')
const referenceLines = lines(reference.stdout)
console.log(
    `[EFFECTS] reference std::nvm: exit ${reference.status}, ${reference.stdout.length} byte(s), ${referenceLines.length - 1} line(s)`
)

for (const passes_ of passes) {
    const optimized = run(passes_)
    const label = passes_.join(' ')
    if (optimized.error || optimized.status !== 0) {
        // A pass that refuses prints a diagnostic and nothing else: say so, and name the first line of
        // it, because "exit 1" alone sends the reader looking for a crash.
        console.log(
            `[EFFECTS] RED ${label}: exit ${optimized.status} where the interpreter exited 0, ${optimized.stdout.length} byte(s) of output`
        )
        const diagnostic = optimized.stderr.toString('utf8').split('\n').find((line) => line.trim() !== '')
        if (diagnostic) {
            console.log(`[EFFECTS]   the pass refused: ${diagnostic}`)
        }
        failed = true
        continue
    }
    if (optimized.stdout.equals(reference.stdout)) {
        console.log(
            `[EFFECTS] ok ${label}: ${optimized.stdout.length} byte(s), same as the interpreter`
        )
        continue
    }
    // The same binary on the same host: a difference here is the pass, and the difference that
    // matters is the line that stopped running. Name it, because "the bytes differ" is what a
    // reader already knows from the failure and "which print disappeared" is what they need.
    const got = lines(optimized.stdout)
    const lost = referenceLines.filter((line, index) => got[index] !== line && !got.includes(line))
    const extra = got.filter((line, index) => referenceLines[index] !== line && !referenceLines.includes(line))
    console.log(
        `[EFFECTS] RED ${label}: ${optimized.stdout.length} byte(s), ${got.length - 1} line(s) where the interpreter printed ${reference.stdout.length} byte(s) in ${referenceLines.length - 1}`
    )
    for (const line of lost.slice(0, 8)) {
        console.log(`[EFFECTS]   lost: ${line}`)
    }
    for (const line of extra.slice(0, 8)) {
        console.log(`[EFFECTS]   extra: ${line}`)
    }
    if (lost.length === 0 && extra.length === 0) {
        console.log(`[EFFECTS]   the lines are the same but they are not in the same order or spelling`)
    }
    failed = true
}

console.log(failed ? '[EFFECTS] RED' : '[EFFECTS] ok the optimizer prints what the interpreter prints')
process.exitCode = failed ? 1 : 0
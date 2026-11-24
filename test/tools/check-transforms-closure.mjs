#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The closure of the forward half over the backward half, and the reading that says it bites.
//
// `lib/rulesets/transpose.cml` files a law for every operator a transposed walk can meet, and
// `lib/rulesets/jvp.cml` has to answer each of them: the library is
// `transpose(split(reinterp<jvp>(f)))`, so one half writes the operators the other half walks. A key
// with a backward law and no forward law is a step the composition takes in one direction only, and
// the refusal a user meets names nothing -- `transforms: jvp has no forward transform for this
// function`, no operator, no file, no line. That sentence is why this is a check and not a habit.
//
// Two readings, and the second is the one that makes the first worth anything:
//
//   the real tree        -- `0 missing`: every key `transpose.cml` files has a `jvp.cml` law. This
//                           is the reading the plan pins.
//   a law with no twin   -- a copy of the two rulesets with one backward law added (`fake_closure_op_t`,
//                           a name neither file had) and no forward law beside it: the check must
//                           name that key and exit 1. A checker that answered `0 missing` for this
//                           reading would be green on the day someone files a backward law alone,
//                           which is the shape this file exists to catch.
//
// The copy holds both rulesets and nothing else: `--closure` reads the two files and stops, so the
// engine's sources are not part of this reading.
//
// Usage: node test/tools/check-transforms-closure.mjs
//        node test/tools/check-transforms-closure.mjs --keep   (leaves the temp root for inspection)

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const GATE = path.join(ROOT, 'scripts', 'vjp-parity.mjs')
const FAKE = 'fake_closure_op'
const KEEP = process.argv.includes('--keep')

const failures = []

/// The check, run the way the plan runs it: as a program, so its exit code is the claim. Output goes
/// through a file rather than a pipe, for the reason `check-vjp-parity.mjs` gives -- a pipe is not
/// available under every sandbox this runs in.
function runCheck(args) {
    const log = path.join(os.tmpdir(), `transforms-closure-${process.pid}-${Math.random().toString(16).slice(2)}.log`)
    const fd = fs.openSync(log, 'w')
    const run = spawnSync(process.execPath, [GATE, ...args], { stdio: ['ignore', fd, fd] })
    fs.closeSync(fd)
    const output = fs.readFileSync(log, 'utf8')
    fs.rmSync(log, { force: true })
    if (run.error) {
        return { code: null, output: `${output}\n${run.error.message}` }
    }
    return { code: run.status, output }
}

function report(name, ok, detail) {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail === undefined ? '' : `: ${detail}`}`)
    if (!ok) {
        failures.push({ name, detail })
    }
}

// ---- the real tree ---------------------------------------------------------------------------

const real = runCheck(['--closure'])
report(
    'every key the backward half files has a forward law',
    real.code === 0 && /0 missing/.test(real.output),
    real.code === 0
        ? real.output.trim().split('\n')[0]
        : `exit ${real.code}: ${real.output.trim().split('\n').slice(1).join(' | ')}`
)

// ---- a backward law with no forward twin -------------------------------------------------------

const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'transforms-closure-copy-'))
fs.mkdirSync(path.join(scratch, 'lib', 'rulesets'), { recursive: true })
for (const name of ['transpose.cml', 'jvp.cml']) {
    fs.copyFileSync(
        path.join(ROOT, 'lib', 'rulesets', name),
        path.join(scratch, 'lib', 'rulesets', name)
    )
}

// One backward law, filed under a head neither file has, and no forward law beside it: the shape of
// the day an operator is added to the transposed walk alone.
const copied = path.join(scratch, 'lib', 'rulesets', 'transpose.cml')
fs.appendFileSync(
    copied,
    `\n// written by test/tools/check-transforms-closure.mjs: a backward law with no forward twin.\n` +
        `export rule ${FAKE}_t::<T, X, A>(t: X, at: A) when is_float::<T>() {\n` +
        `    ${FAKE}(t, at) => (ct: T) => (ct, ct)\n` +
        `}\n`
)

const unclosed = runCheck(['--root', scratch, '--closure'])
report(
    'a backward law with no forward twin is reported by its key',
    unclosed.code === 1 && unclosed.output.includes(FAKE),
    unclosed.code === 1
        ? `exit 1 and the report names '${FAKE}@2'`
        : `exit ${unclosed.code} and the report ${unclosed.output.includes(FAKE) ? 'names' : 'does not name'} '${FAKE}'`
)

if (!KEEP) {
    fs.rmSync(scratch, { recursive: true, force: true })
} else {
    console.log(`note the copy is left at ${scratch}`)
}

if (failures.length > 0) {
    console.log(`\nFAIL ${failures.length} of 2 reading(s) of the closure check`)
    process.exit(1)
}
console.log('\nPASS the closure check answers the two readings it exists for')
process.exit(0)